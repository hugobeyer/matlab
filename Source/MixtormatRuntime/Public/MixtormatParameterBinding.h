// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"

// What a parameter address is allowed to name.
//
// An address identifies its container by LayerId, and a group has no layer. Rather than give the
// address a second field -- which every serialized binding would have to grow -- a group's own
// GroupId occupies that slot, and this says whether groups are in scope for the lookup. Nothing
// already written to disk carries a group id there, so old assets resolve exactly as before.
//
// Constructible from a bare layer array so every existing call site keeps compiling and keeps its
// current behaviour: no groups passed means no groups resolved.
struct MIXTORMATRUNTIME_API FMixtormatBindingScope
{
	const TArray<FMixtormatLayer>* Layers = nullptr;
	const TArray<FMixtormatLayerGroup>* Groups = nullptr;

	FMixtormatBindingScope(const TArray<FMixtormatLayer>& InLayers)
		: Layers(&InLayers)
	{
	}

	FMixtormatBindingScope(
		const TArray<FMixtormatLayer>& InLayers,
		const TArray<FMixtormatLayerGroup>& InGroups)
		: Layers(&InLayers)
		, Groups(&InGroups)
	{
	}

	const TArray<FMixtormatLayer>& GetLayers() const { return *Layers; }
};

// The same, for the two entry points that write. Kept apart rather than made one struct with four
// pointers: a caller holding only const data must not be able to reach a write by accident.
struct MIXTORMATRUNTIME_API FMixtormatMutableBindingScope
{
	TArray<FMixtormatLayer>* Layers = nullptr;
	TArray<FMixtormatLayerGroup>* Groups = nullptr;

	FMixtormatMutableBindingScope(TArray<FMixtormatLayer>& InLayers)
		: Layers(&InLayers)
	{
	}

	FMixtormatMutableBindingScope(
		TArray<FMixtormatLayer>& InLayers,
		TArray<FMixtormatLayerGroup>& InGroups)
		: Layers(&InLayers)
		, Groups(&InGroups)
	{
	}

	operator FMixtormatBindingScope() const
	{
		return Groups
			? FMixtormatBindingScope(*Layers, *Groups)
			: FMixtormatBindingScope(*Layers);
	}
};

namespace MixtormatParameterBinding
{
	// Ensures every layer/child has persistent identity. Existing valid IDs are preserved.
	MIXTORMATRUNTIME_API void EnsureStableIds(TArray<FMixtormatLayer>& Layers);
	// The document form includes authored groups. Group IDs share the owner-ID namespace with
	// layers, and group children share the child-ID namespace with layer children.
	MIXTORMATRUNTIME_API void EnsureStableIds(
		TArray<FMixtormatLayer>& Layers,
		TArray<FMixtormatLayerGroup>& Groups);

	// A duplicated layer/child must not share identity with its source. References inside a
	// duplicated layer are intentionally preserved; only the duplicated objects receive new IDs.
	MIXTORMATRUNTIME_API void RegenerateLayerIdentity(FMixtormatLayer& Layer, bool bRegenerateChildren = true);

	// Copies a complete recipe into another document without sharing identity. Every reference,
	// driver, instance and published mask source that points inside the batch follows the copy.
	MIXTORMATRUNTIME_API void RegenerateLayerIdentities(TArray<FMixtormatLayer>& Layers);
	MIXTORMATRUNTIME_API void RegenerateLayerIdentities(
		TArray<FMixtormatLayer>& Layers,
		TArray<FMixtormatLayerGroup>& Groups);
	MIXTORMATRUNTIME_API void RegenerateChildIdentity(FMixtormatLayerChild& Child);

	// Why an instance placement was refused. The editor turns these into the reason it shows on a
	// disabled menu row; the runtime keeps them wordless.
	enum class EInstancePlacement : uint8
	{
		Valid,
		SourceMissing,
		SelfReference,
		// The source composites after the position asked for. Nothing here reorders children to
		// make that work -- ID producer order, the mask accumulator and the post-composite filters
		// all depend on the order the user authored.
		SourceEvaluatesLater
	};

	// Copies everything a child carries except its identity -- so a field added to
	// FMixtormatLayerChild is inherited by instances without this having to learn about it.
	MIXTORMATRUNTIME_API void CopyChildPayload(
		const FMixtormatLayerChild& From,
		FMixtormatLayerChild& To);

	// LayerId names a layer, or -- when Layers.Groups is set and no layer matches -- a group's
	// shared children, the same convention every address in this file already uses.
	MIXTORMATRUNTIME_API const FMixtormatLayerChild* FindChild(
		const FMixtormatBindingScope& Layers,
		const FGuid& LayerId,
		const FGuid& ChildId);

	// Substitutes each instance's payload from the child it names, immediately before references
	// resolve. Mask instances keep local BlendMode and Shaping.bInvert values because those control
	// this placement in the mask chain; bindings inherited for those two fields are discarded.
	// Reads authored data only. Missing, self-referencing or cyclic sources keep local values.
	MIXTORMATRUNTIME_API void ResolveChildInstances(
		const FMixtormatBindingScope& SourceLayers,
		FMixtormatLayer& InOutLayer);

	// Whether an instance of Source may sit at DestChildIndex in DestLayerId. Placement is checked
	// when the instance is made; a later reorder can invalidate it, and that case falls back to the
	// resolve-time behaviour above rather than being prevented here.
	//
	// SourceLayerId/DestLayerId each name a layer or (when Layers.Groups is set) a group. A group
	// has no single position in composite order -- BuildEffectiveLayers appends its children onto
	// every member layer's own tail -- so ordering against one is conservative: valid only when the
	// whole member range is strictly on one side, never when it partially overlaps.
	MIXTORMATRUNTIME_API EInstancePlacement ClassifyInstancePlacement(
		const FMixtormatBindingScope& Layers,
		const FGuid& SourceLayerId,
		const FGuid& SourceChildId,
		const FGuid& DestLayerId,
		int32 DestChildIndex);

	// Bakes an instance down to a plain child holding the source's current payload.
	MIXTORMATRUNTIME_API bool BreakChildInstance(
		const FMixtormatBindingScope& Layers,
		FMixtormatLayerChild& InOutChild);

	// Rewrites every address in the stack that named ChildId under OldLayerId to name it under
	// NewLayerId. A moved child is the same child, so the references, drivers and instances
	// pointing at it have to follow it across rather than break. Takes the mutable scope, not a
	// bare layer array, because a child can now move into a group's shared stack: a reference held
	// by a group child is exactly as real as one held by a layer child, and has to follow the same
	// way.
	MIXTORMATRUNTIME_API void RemapChildParent(
		const FMixtormatMutableBindingScope& Scope,
		const FGuid& ChildId,
		const FGuid& OldLayerId,
		const FGuid& NewLayerId);

	// Resolves exact parameter references into a transient layer copy before render-data gathering.
	// Local authored values are untouched in the asset/editor data. Missing, incompatible or cyclic
	// references simply leave the local value in place.
	MIXTORMATRUNTIME_API void ApplyDirectReferences(
		const FMixtormatBindingScope& SourceLayers,
		FMixtormatLayer& InOutLayer);

	MIXTORMATRUNTIME_API bool IsReferenceSourceValid(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Source);

	MIXTORMATRUNTIME_API bool AreReferenceTypesCompatible(
		const FMixtormatParameterAddress& Destination,
		const FMixtormatParameterAddress& Source);

	// The parameter a linked edit must land on.
	//
	// Walks the chain of Link references out of Destination to the parameter that is not itself
	// following one -- so a run of links all write to a single authority rather than to each
	// other. Returns an invalid address when Destination is not linked, when a step is broken or
	// type-incompatible, when the chain closes on itself, or when the parameter it arrives at is
	// itself referencing something (writing there would be overwritten on the next resolve).
	MIXTORMATRUNTIME_API FMixtormatParameterAddress ResolveLinkTarget(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Destination);

	// Writes an authored value straight into the parameter at Address. Type-checked against the
	// property it lands on, so a mismatched address fails rather than reinterpreting the value.
	MIXTORMATRUNTIME_API bool TryWriteFloat(
		const FMixtormatMutableBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		float Value);
	MIXTORMATRUNTIME_API bool TryWriteInt(
		const FMixtormatMutableBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		int32 Value);
	MIXTORMATRUNTIME_API bool TryWriteBool(
		const FMixtormatMutableBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		bool Value);

	MIXTORMATRUNTIME_API bool TryResolveFloat(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		float& OutValue);
	MIXTORMATRUNTIME_API bool TryResolveInt(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		int32& OutValue);
	MIXTORMATRUNTIME_API bool TryResolveBool(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		bool& OutValue);

	// Enums are written by their integer value. The address's TypeName must match the property's
	// enum, so a mismatched address fails rather than reinterpreting the value.
	MIXTORMATRUNTIME_API bool TryWriteEnum(
		const FMixtormatMutableBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		int64 Value);
	MIXTORMATRUNTIME_API bool TryResolveEnum(
		const FMixtormatBindingScope& Layers,
		const FMixtormatParameterAddress& Address,
		int64& OutValue);
}
