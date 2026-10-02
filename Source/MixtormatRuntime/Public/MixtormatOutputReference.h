// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.generated.h"

struct FMixtormatLayer;

// Scalar outputs remain ordinary Mask children with their existing published-source fields.
UENUM(BlueprintType)
enum class EMixtormatPublishedFieldKind : uint8
{
	RegionIds UMETA(DisplayName = "Region IDs"),
	Flow UMETA(DisplayName = "Flow"),
	UVMap UMETA(DisplayName = "UV Map")
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

	bool HasSource() const
	{
		return SourceLayerId.IsValid() && SourceChildId.IsValid() && !OutputName.IsNone();
	}
};

namespace MixtormatOutputReferences
{
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

	// Returns the authored source index only after ValidateDependency succeeds.
	MIXTORMATRUNTIME_API int32 ResolveSource(
		const TArray<FMixtormatLayer>& Layers,
		int32 DestinationLayerIndex,
		int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference);
}
