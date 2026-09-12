// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaskShaping.generated.h"

// The shaping chain every mask-producing node shares: contrast about the midpoint, an offset that
// lifts the whole signal, a balance that thins or thickens what is left, then invert.
//
// The maths has always been shared -- one MixtormatShapeMask in MixtormatMaskOps.ush, called by
// the texture mask, the generated mask, craquelure and the colour ID. What was not shared was
// everything describing it: four structs each declared their own Balance/Contrast/Offset/bInvert
// with their own clamps, and the inspector wrote the rows out four times with four different
// slider ranges. The behaviour could not drift because the shader is one function; the ranges did,
// and ended up at 0-2, 0-16 and unclamped for the same parameter.
//
// So this is the declaration half of that function, in one place. Embed it and the node gets the
// ranges, the tooltips and the rows for free.
//
// Adopted by FMixtormatMaskLayer. FMixtormatGeneratedMask, FMixtormatCraquelure,
// FMixtormatColorIdMask and UMixtormatMask's own defaults are the remaining copies, each one line
// once someone wants them migrated.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskShaping
{
	GENERATED_BODY()

	// Flips the mask after every other stage, so it inverts what you see rather than what was
	// sampled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping")
	bool bInvert = false;

	// Thins toward black above 0.5, thickens toward white below it, as a power curve -- so it
	// erodes what is there rather than fading it out. The shader saturates this, which is why the
	// clamp is 0-1 and not the 0-2 and 0-16 it used to be in various places.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Balance = 0.5f;

	// About a fixed 0.5 midpoint, which is the correct pivot because masks are stored raw rather
	// than sRGB. The clamp stays generous for assets authored against the old range; the editor
	// slider is the narrower one, since everything useful lives under about 4.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float Contrast = 1.0f;

	// Lifts the signal after contrast and inside the same saturate, so +1 reaches full white and
	// -1 full black from any input at any contrast. There is nothing past that: a larger offset
	// clips to the same result.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Offset = 0.0f;
};

// Editor slider ranges, which are deliberately not the clamps above.
//
// A clamp is what an asset may hold; a range is what the drag has to resolve. Contrast is the pair
// that differs: values over about 4 are a cliff rather than a control, but an asset authored when
// the slider went to 10 keeps its value instead of being silently re-graded.
namespace MixtormatMaskShapingRange
{
	constexpr double BalanceMin = 0.0;
	constexpr double BalanceMax = 1.0;
	constexpr double BalanceDefault = 0.5;

	constexpr double ContrastMin = 0.0;
	constexpr double ContrastMax = 4.0;
	constexpr double ContrastDefault = 1.0;

	constexpr double OffsetMin = -1.0;
	constexpr double OffsetMax = 1.0;
	constexpr double OffsetDefault = 0.0;

	constexpr double SnapDelta = 0.01;
}
