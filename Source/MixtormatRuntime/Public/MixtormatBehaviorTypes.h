// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.h"
#include "MixtormatBehaviorTypes.generated.h"

// V2 authoring data only. Dispatch is deliberately opt-in: legacy structural modules
// retain their authored-position execution until their compatibility path is defined.
UENUM(BlueprintType)
enum class EMixtormatBehaviorType : uint8
{
	Warp UMETA(DisplayName = "Warp"),
	Push UMETA(DisplayName = "Push"),
	Carve UMETA(DisplayName = "Carve / Deposit"),
	Deform UMETA(DisplayName = "Deform")
};

// The stage is an explicit part of the behavior contract, not inferred from its UI parent.
// PreGeneration acts on the generator's sampling coordinates; PostGeneration acts on its
// native height/bundle before the shared height normalization and output scale.
UENUM(BlueprintType)
enum class EMixtormatBehaviorStage : uint8
{
	PreGeneration UMETA(DisplayName = "Before Generation"),
	PostGeneration UMETA(DisplayName = "After Generation")
};

UENUM(BlueprintType)
enum class EMixtormatBehaviorFieldOrigin : uint8
{
	None UMETA(DisplayName = "None"),
	OwnNativeHeight UMETA(DisplayName = "Own Native Height"),
	OwnBoundary UMETA(DisplayName = "Own Boundary"),
	PreviousRunningHeight UMETA(DisplayName = "Previous Running Height"),
	PublishedOutput UMETA(DisplayName = "Published Output")
};

// A field socket owns either a semantic local snapshot or an explicit typed reference.
// Published references are never silently reinterpreted (Vector2 != Flow != UVMap).
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehaviorFieldInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field")
	EMixtormatBehaviorFieldOrigin Origin = EMixtormatBehaviorFieldOrigin::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field",
		meta = (EditCondition = "Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput"))
	FMixtormatOutputReference Published;

	bool HasPublishedSource() const
	{
		return Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput && Published.HasSource();
	}
};

// Behaviors are ordinary children owned through ScopeOwnerChildId by an earlier Generator.
// Field inputs are slots on the Behavior, not extra serialized layer children.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehavior
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	EMixtormatBehaviorType Type = EMixtormatBehaviorType::Warp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	EMixtormatBehaviorStage Stage = EMixtormatBehaviorStage::PostGeneration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (UIMin = "-4.0", UIMax = "4.0"))
	float Strength = 1.0f;

	// Vector transport / UV source for Warp and Deform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Direction;

	// Signed scalar source for Push, Carve, and height-guided deformation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Height;

	// Optional 0..1 coverage; not a replacement for scoped Mask children.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Influence;
};
