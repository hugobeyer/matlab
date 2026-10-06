// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatScalarRemap.generated.h"

// A reusable signed remap payload: zero-preserving normalization, signed input range, curve,
// balance, contrast, offset, invert, and an Amount lerp against the original input.
//
// Embedded by the existing Height Curve / Height Remap child. Mask semantics are fundamentally
// different (0..1 domain, 0.5 pivot, 1-x invert, saturation) and are not reused here.
//
// Defaults reproduce the current identity behaviour so existing assets load unchanged.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatScalarRemap
{
	GENERATED_BODY()

	// Optional zero-preserving max-absolute normalization to -1..1 before the signed input range.
	// A constant/degenerate input resolves deterministically without producing NaNs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	bool bNormalizeInput = false;

	// Signed input range. The pivot is zero: positive values divide by InputMax, negative by
	// abs(InputMin). Default -1..1 matches the canonical signed domain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	float InputMin = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	float InputMax = 1.0f;

	// Sign-preserving power transform: sign(x) * pow(abs(x), exponent). Identity at 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	float Balance = 1.0f;

	// Multiplicative contrast pivoted around zero. Identity at 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	float Contrast = 1.0f;

	// Signed addition. Identity at 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	float Offset = 0.0f;

	// True means -x, not 1-x. Identity when false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	bool bInvert = false;

	// The signed-domain curve. The X axis is signed; the ramp is not saturated to 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap")
	FMixtormatScalarRamp Curve;

	// How much of the remapped result replaces the original input. Identity at 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Remap", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Amount = 1.0f;

	FMixtormatScalarRemap()
	{
		Curve.DomainMin = -1.0f;
		Curve.DomainMax = 1.0f;
		Curve.Points = {
			FMixtormatScalarRampPoint{-1.0f, -1.0f},
			FMixtormatScalarRampPoint{0.0f, 0.0f},
			FMixtormatScalarRampPoint{1.0f, 1.0f}
		};
		Curve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
	}
};
