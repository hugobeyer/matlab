// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../../Shaders/Private/MixtormatColorRampCapacity.ush"
#include "MixtormatColorRamp.generated.h"

UENUM(BlueprintType)
enum class EMixtormatColorRampInterpolation : uint8
{
	Constant,
	Linear,
	Smooth
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatColorRampStop
{
	GENERATED_BODY()

	FMixtormatColorRampStop() = default;
	FMixtormatColorRampStop(const float InX, const FLinearColor& InColor) : X(InX), Color(InColor) {}

	// Position along the ramp domain. The domain is authored by the consumer: the generator
	// height ramp runs -1..1 with zero at the centre, so 0 is a stop position, not the low end.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	float X = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	FLinearColor Color = FLinearColor::Black;
};

// A reusable colour ramp, the colour counterpart of FMixtormatScalarRamp. Stops are evenly
// addressed by X and evaluated by the shared GPU/CPU helpers; the domain is whatever the caller
// feeds in, so the same struct serves a 0..1 mask ramp and a -1..1 signed height ramp.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatColorRamp
{
	GENERATED_BODY()

	static constexpr int32 MaxStops = MIXTORMAT_COLOR_RAMP_MAX_STOPS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	TArray<FMixtormatColorRampStop> Stops = {
		FMixtormatColorRampStop{0.0f, FLinearColor::Black},
		FMixtormatColorRampStop{1.0f, FLinearColor::White}
	};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	EMixtormatColorRampInterpolation Interpolation = EMixtormatColorRampInterpolation::Linear;

	void Sanitize();
};
