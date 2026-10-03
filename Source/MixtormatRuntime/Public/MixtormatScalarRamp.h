// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../../Shaders/Private/MixtormatScalarRampCapacity.ush"
#include "MixtormatScalarRamp.generated.h"

UENUM(BlueprintType)
enum class EMixtormatScalarRampInterpolation : uint8
{
	Constant,
	Linear,
	Spline,
	BSpline
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatScalarRampPoint
{
	GENERATED_BODY()

	FMixtormatScalarRampPoint() = default;
	FMixtormatScalarRampPoint(const float InX, const float InY) : X(InX), Y(InY) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Ramp")
	float X = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Ramp")
	float Y = 0.0f;
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatScalarRamp
{
	GENERATED_BODY()

	static constexpr int32 MaxPoints = MIXTORMAT_SCALAR_RAMP_MAX_POINTS;
	static constexpr float MinOutput = -3.0f;
	static constexpr float MaxOutput = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Ramp")
	TArray<FMixtormatScalarRampPoint> Points = {
		FMixtormatScalarRampPoint{0.0f, 0.0f},
		FMixtormatScalarRampPoint{1.0f, 1.0f}
	};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalar Ramp")
	EMixtormatScalarRampInterpolation Interpolation = EMixtormatScalarRampInterpolation::Linear;

	void ResetToIdentity();
	void Sanitize();
};
