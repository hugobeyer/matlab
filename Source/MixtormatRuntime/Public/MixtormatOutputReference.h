// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.generated.h"

struct FMixtormatLayer;
struct FMixtormatMaskLayer;
struct FMixtormatSourceEntry;

// Scalar outputs remain ordinary Mask children with their existing published-source fields.
//
// Values are append-only: the first four keep their serialized numeric values, so saved assets and
// the prefix cache stay valid. A kind carries its semantic contract -- never collapse two kinds
// (Vector2 / Flow / UVMap, or ScalarSigned / SDF) just because their storage formats may coincide.
UENUM(BlueprintType)
enum class EMixtormatPublishedFieldKind : uint8
{
	RegionIds UMETA(DisplayName = "Region IDs"),
	Flow UMETA(DisplayName = "Flow"),
	UVMap UMETA(DisplayName = "UV Map"),
	// A colour field published by a Generator-layer Height Color Ramp, for later albedo/material use.
	Color UMETA(DisplayName = "Color"),
	// Nominal 0..1 scalar domain. Generic scalar data; not inherently a mask.
	Scalar01 UMETA(DisplayName = "Scalar 0..1"),
	// Signed scalar data. Must preserve negative values.
	ScalarSigned UMETA(DisplayName = "Scalar Signed"),
	// Signed-distance semantic data, distinct from an arbitrary signed scalar.
	SDF UMETA(DisplayName = "Signed Distance"),
	// Generic 2-component vector field. Not a Flow (directional transport) and not a UVMap
	// (absolute/transformed coordinates).
	Vector2 UMETA(DisplayName = "Vector 2")
};

// Where a published output lives. Layer remains zero for serialized compatibility: every existing
// reference has only SourceLayerId/SourceChildId and therefore retains its established meaning.
UENUM(BlueprintType)
enum class EMixtormatOutputReferenceOwnerKind : uint8
{
	Layer UMETA(DisplayName = "Layer"),
	Shelf UMETA(DisplayName = "Sources Shelf")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatOutputReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	FGuid SourceLayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	FGuid SourceChildId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	FName OutputName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	EMixtormatPublishedFieldKind Kind = EMixtormatPublishedFieldKind::RegionIds;

	// A Flow placement traces the referenced influence-weighted field into destination UVs.
	// These are placement controls, not copies of the producer's solve settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference|Flow", meta = (EditCondition = "Kind == EMixtormatPublishedFieldKind::Flow", UIMin = "-4.0", UIMax = "4.0"))
	float FlowAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference|Flow", meta = (EditCondition = "Kind == EMixtormatPublishedFieldKind::Flow", UIMin = "0.0", UIMax = "1.0"))
	float FlowTraceLength = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference|Flow", meta = (EditCondition = "Kind == EMixtormatPublishedFieldKind::Flow", ClampMin = "1", UIMax = "64"))
	int32 FlowSteps = 16;

	// Appended owner discriminator. Layer is the legacy default and continues to use
	// SourceLayerId/SourceChildId; Shelf uses SourceShelfId/SourceChildId instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	EMixtormatOutputReferenceOwnerKind OwnerKind = EMixtormatOutputReferenceOwnerKind::Layer;

	// Stable Sources shelf entry identity. Ignored for legacy Layer references.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output Reference")
	FGuid SourceShelfId;

	bool IsShelfSource() const { return OwnerKind == EMixtormatOutputReferenceOwnerKind::Shelf; }
	bool IsLayerSource() const { return OwnerKind == EMixtormatOutputReferenceOwnerKind::Layer; }
	bool HasKnownOwnerKind() const { return IsLayerSource() || IsShelfSource(); }

	bool HasSource() const
	{
		if (!HasKnownOwnerKind()) { return false; }
		const FGuid& OwnerId = IsShelfSource() ? SourceShelfId : SourceLayerId;
		return OwnerId.IsValid() && SourceChildId.IsValid() && !OutputName.IsNone();
	}
};

namespace MixtormatOutputReferences
{
	// Shelf endpoints are representable before producer evaluation exists. This status intentionally
	// separates a malformed/missing endpoint from one that is structurally valid but not yet
	// executable, so callers can offer repair instead of guessing a layer fallback.
	enum class EShelfSourceReferenceIssue : uint8
	{
		None,
		NotShelfReference,
		InvalidOwnerKind,
		Unset,
		InvalidFieldKind,
		WrongOutputName,
		MissingSource,
		DuplicateSourceIdentity,
		MissingChild,
		WrongSourceKind,
		DisabledSource,
		Unevaluated
	};

	struct FShelfSourceReferenceStatus
	{
		EShelfSourceReferenceIssue Issue = EShelfSourceReferenceIssue::Unset;
		int32 SourceIndex = INDEX_NONE;
		bool bHasResolvedEndpoint = false;
		bool bCanExecute = false;
	};

	// Transient structural eligibility only; never serialized or a guarantee of GPU availability.
	enum class EStructuralLinkIssue : uint8
	{
		None, Unset, MissingLayer, MissingChild, DuplicateIdentity,
		DisabledLayer, DisabledSource, DisabledTarget, DisabledReference,
		WrongOwnerLayer, WrongModuleType, ScopedModule, WrongSourceKind,
		WrongSourceScope, IncompleteSourceScope, InvalidSourceScope,
		ForwardSource, ForwardTarget, WrongTargetKind, ScopedTarget,
		UnavailableEffectAsset
	};

	struct FStructuralEdgeStatus
	{
		EStructuralLinkIssue Issue = EStructuralLinkIssue::Unset;
		int32 LayerIndex = INDEX_NONE;
		int32 ChildIndex = INDEX_NONE;
	};

	struct FStructuralLinkStatus
	{
		FStructuralEdgeStatus Source;
		FStructuralEdgeStatus Target;
		EStructuralLinkIssue ModuleIssue = EStructuralLinkIssue::None;
		bool bModuleEnabled = false;
		bool bCanExecuteStructurally = false;
	};

	// Pass effective, instance-resolved layers. Indices address that projection, not authored
	// group rows. Group provenance cannot be inferred here; Editor must gate group authoring.
	// Candidate overrides never mutate the arrays. Disabled modules retain independent edges.
	MIXTORMATRUNTIME_API FStructuralLinkStatus EvaluateStructuralLink(
		const TArray<FMixtormatLayer>& Layers,
		int32 ModuleLayerIndex,
		int32 ModuleChildIndex,
		const FMixtormatOutputReference* ProposedSource = nullptr,
		const FGuid* ProposedTarget = nullptr);

	// Mirrors Gather's mixed projection without changing its render predicates: effective
	// layers for sources/Warp targets, binding-resolved destination for module/Push targets.
	MIXTORMATRUNTIME_API FStructuralLinkStatus EvaluateStructuralLinkForGather(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		int32 ModuleLayerIndex,
		int32 ModuleChildIndex,
		const FMixtormatLayer& ResolvedModuleLayer);

	// Preserves Gather's first eligible later Strata target rule, including duplicate GUIDs.
	MIXTORMATRUNTIME_API int32 ResolveHeightPushTarget(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FGuid& TargetChildId);

	// Gather's binding/instance-resolved layer may differ from the effective array entry.
	// Use that exact layer for target eligibility without copying the entire projection.
	MIXTORMATRUNTIME_API int32 ResolveHeightPushTarget(
		const FMixtormatLayer& Layer,
		int32 DestinationChildIndex,
		const FGuid& TargetChildId);

	// True for every kind the reference system understands. Exhaustive over the enum, so an
	// unrecognised (future) value can never silently pass reference validation.
	MIXTORMATRUNTIME_API bool IsValidFieldKind(EMixtormatPublishedFieldKind Kind);

	// The published output name a kind is canonically addressed by -- the same name its producer
	// publishes -- or NAME_None when the producer chooses its own output name. RegionIds, Flow,
	// UVMap and Color keep their established names; the generic scalar/vector field kinds are
	// addressed by whatever name the producer published.
	MIXTORMATRUNTIME_API FName CanonicalFieldOutputName(EMixtormatPublishedFieldKind Kind);

	// Validates an explicit Sources shelf endpoint without treating it as a layer producer. Until
	// Phase B adds producer evaluation, a well-formed enabled endpoint reports Unevaluated rather
	// than becoming a valid material dependency.
	MIXTORMATRUNTIME_API FShelfSourceReferenceStatus ClassifyShelfSourceReference(
		const TArray<FMixtormatSourceEntry>& Sources,
		const FMixtormatOutputReference& Reference);

	// Effective layers only: group addresses must first pass through BuildEffectiveLayers.
	// Returns the authored source-child index, never a compacted render-child index.
	MIXTORMATRUNTIME_API int32 ResolveEarlierSource(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		const FMixtormatOutputReference& Reference);

	// Effective layers (expand layer groups first). Destination must be an existing child;
	// Reference replaces that child's field edge for validation, so editor proposals need not
	// mutate the asset. RegionIds may read completed local producers or earlier layers only.
	// Checks enabled producers, scope completion order, group/input and Combine dependencies,
	// and active-path cycles. Availability of the actual texture is checked by the compositor.
	// Flow/UV retain their existing earlier-layer-only contract.
	MIXTORMATRUNTIME_API bool ValidateDependency(
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& DestinationLayerId,
		const FGuid& DestinationChildId,
		const FMixtormatOutputReference& Reference);

	// Explicit generator sockets, Height Push signed Height, or Structural Warp Flow/UVMap: completed
	// signed Height, Flow or UVMap from an earlier
	// generator scope in this layer or an earlier layer. Rejects self/forward scope reads,
	// disabled owners and wrong output kinds. Legacy layer-wide Flow/UV validation is unchanged.
	// Strictly decreasing evaluation order makes socket cycles impossible.
	MIXTORMATRUNTIME_API int32 ResolveGeneratorInputSource(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference);

	// Ordered Structural Warp: explicit later, enabled, unscoped generator target in the same
	// Generator layer. Returns its authored child index; no implicit target or group support.
	MIXTORMATRUNTIME_API int32 ResolveStructuralWarpTarget(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FGuid& TargetChildId);

	// Published mask addressing; Noise.Value requires an enabled, completed generator scope.
	// Other published mask outputs retain their existing address-resolution behavior.
	MIXTORMATRUNTIME_API int32 ResolvePublishedMaskSource(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FMixtormatMaskLayer& Mask);

	// Returns the authored source index only after ValidateDependency succeeds.
	MIXTORMATRUNTIME_API int32 ResolveSource(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference);
}
