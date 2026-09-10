#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"

namespace MixtormatParameterBinding
{
	// Ensures every layer/child has persistent identity. Existing valid IDs are preserved.
	MIXTORMATRUNTIME_API void EnsureStableIds(TArray<FMixtormatLayer>& Layers);

	// A duplicated layer/child must not share identity with its source. References inside a
	// duplicated layer are intentionally preserved; only the duplicated objects receive new IDs.
	MIXTORMATRUNTIME_API void RegenerateLayerIdentity(FMixtormatLayer& Layer, bool bRegenerateChildren = true);
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

	MIXTORMATRUNTIME_API const FMixtormatLayerChild* FindChild(
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& LayerId,
		const FGuid& ChildId);

	// Substitutes each instance's payload from the child it names, immediately before references
	// resolve. Mask instances keep local BlendMode and Shaping.bInvert values because those control
	// this placement in the mask chain; bindings inherited for those two fields are discarded.
	// Reads authored data only. Missing, self-referencing or cyclic sources keep local values.
	MIXTORMATRUNTIME_API void ResolveChildInstances(
		const TArray<FMixtormatLayer>& SourceLayers,
		FMixtormatLayer& InOutLayer);

	// Whether an instance of Source may sit at DestChildIndex in DestLayerId. Placement is checked
	// when the instance is made; a later reorder can invalidate it, and that case falls back to the
	// resolve-time behaviour above rather than being prevented here.
	MIXTORMATRUNTIME_API EInstancePlacement ClassifyInstancePlacement(
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& SourceLayerId,
		const FGuid& SourceChildId,
		const FGuid& DestLayerId,
		int32 DestChildIndex);

	// Bakes an instance down to a plain child holding the source's current payload.
	MIXTORMATRUNTIME_API bool BreakChildInstance(
		const TArray<FMixtormatLayer>& Layers,
		FMixtormatLayerChild& InOutChild);

	// Rewrites every address in the stack that named ChildId under OldLayerId to name it under
	// NewLayerId. A moved child is the same child, so the references, drivers and instances
	// pointing at it have to follow it across rather than break.
	MIXTORMATRUNTIME_API void RemapChildParent(
		TArray<FMixtormatLayer>& Layers,
		const FGuid& ChildId,
		const FGuid& OldLayerId,
		const FGuid& NewLayerId);

	// Resolves exact parameter references into a transient layer copy before render-data gathering.
	// Local authored values are untouched in the asset/editor data. Missing, incompatible or cyclic
	// references simply leave the local value in place.
	MIXTORMATRUNTIME_API void ApplyDirectReferences(
		const TArray<FMixtormatLayer>& SourceLayers,
		FMixtormatLayer& InOutLayer);

	MIXTORMATRUNTIME_API bool IsReferenceSourceValid(
		const TArray<FMixtormatLayer>& Layers,
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
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Destination);

	// Writes an authored value straight into the parameter at Address. Type-checked against the
	// property it lands on, so a mismatched address fails rather than reinterpreting the value.
	MIXTORMATRUNTIME_API bool TryWriteFloat(
		TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		float Value);
	MIXTORMATRUNTIME_API bool TryWriteInt(
		TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		int32 Value);
	MIXTORMATRUNTIME_API bool TryWriteBool(
		TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		bool Value);

	MIXTORMATRUNTIME_API bool TryResolveFloat(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		float& OutValue);
	MIXTORMATRUNTIME_API bool TryResolveInt(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		int32& OutValue);
	MIXTORMATRUNTIME_API bool TryResolveBool(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		bool& OutValue);
}
