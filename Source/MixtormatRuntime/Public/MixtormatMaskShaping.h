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
// Embedded by FMixtormatMaskLayer, FMixtormatGeneratedMask, FMixtormatCraquelure,
// FMixtormatColorIdMask and FMixtormatRandomIdMask. Every mask-shaping owner now resolves the
// same declared fields and editor ranges.
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float Balance = 0.5f;

	// About a fixed 0.5 midpoint, which is the correct pivot because masks are stored raw rather
	// than sRGB. The clamp stays generous for assets authored against the old range; the editor
	// slider is the narrower one, since everything useful lives under about 4.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float Contrast = 1.0f;

	// Lifts the signal after contrast and inside the same saturate, so +1 reaches full white and
	// -1 full black from any input at any contrast. There is nothing past that: a larger offset
	// clips to the same result.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shaping", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float Offset = 0.0f;
};


