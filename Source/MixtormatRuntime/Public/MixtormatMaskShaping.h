// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatMaskShaping.generated.h"

// Controls how blended scalar values are constrained.
UENUM(BlueprintType)
enum class EMixtormatMaskOutputRange : uint8
{
	Clamp = 0 UMETA(DisplayName = "Clamp"),
	Unclamped = 1 UMETA(DisplayName = "Unclamped"),
	Mirror = 2 UMETA(DisplayName = "Mirror")
};

// Shared mask shaping: optional input normalization, levels, scalar curve,
// value offset and invert. Input levels and curve replace redundant
// balance/contrast adjustments on every mask producer.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskShaping
{
	GENERATED_BODY()

	// Remaps this node's incoming scalar field from its measured minimum and maximum to 0..1.
		// Uses the shared GPU field-range utility: a constant field maps to zero before levels/shaping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping")
	bool bNormalizeInput = false;

	// Black and white points applied after optional input normalization.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float InputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float InputMax = 1.0f;

	// Flips the mask after every other stage, so it inverts what you see rather than what was
	// sampled.
	// Identity by default, preserving every existing mask's output.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping")
	FMixtormatScalarRamp CurveBias;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping")
	bool bInvert = false;

	// Lifts the curve's output before the final saturation. +1 reaches white
	// and -1 reaches black. There is nothing past that: a larger offset
	// clips to the same result.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float Offset = 0.0f;

	// Range policy applied to the blended scalar mask.
	// Default: Clamp (0..1), preserving current behavior.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending")
	EMixtormatMaskOutputRange OutputRange = EMixtormatMaskOutputRange::Clamp;

	// Frequency scale of scalar reflection into 0..1 when OutputRange is Mirror.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending", meta = (UIMin = "0.01", UIMax = "10.0", Delta = "0.01"))
	float MirrorScale = 1.0f;
};


