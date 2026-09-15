// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaskBlur.generated.h"

// A separable Gaussian, as a node rather than a field.
//
// The blur itself already existed -- MixtormatMaskBlur.usf, two 1D passes, wrap-sampled so it
// cannot seam a tileable material -- but only as something the compositor ran internally for the
// contact-AO and border-lift fields. Nothing in a recipe could reach it.
//
// Carried as its own child so it can be driven and instanced like any other node. That is the
// whole reason it is not simply a Radius on FMixtormatMaskLayer: a field cannot be a driver's
// destination, cannot be published as a source, and cannot appear twice off one definition.
//
// Per axis rather than one radius and a direction enum. Two numbers cost exactly what one does --
// the shader takes an Axis and runs once per direction, so a zero radius skips that dispatch
// entirely -- and each axis is separately bindable, which an enum could never be. X alone smears
// horizontally, Y alone vertically, both together is an ordinary Gaussian, and unequal values are
// anisotropic.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskBlur
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	bool bEnabled = true;

	// In texels at the composition resolution, so a blur set at one resolution lands in the same
	// place at another. Zero skips the pass for that axis rather than running a one-tap identity.
	//
	// The ceiling is the shader's: it unrolls to 32 taps, and sigma is half the radius, so beyond
	// 32 the kernel would be truncated somewhere it has not yet decayed and the cut would show as
	// a ring.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (DisplayName = "Radius X", ClampMin = "0.0", ClampMax = "32.0"))
	float RadiusX = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (DisplayName = "Radius Y", ClampMin = "0.0", ClampMax = "32.0"))
	float RadiusY = 4.0f;

	bool BlursAnything() const
	{
		return bEnabled && (RadiusX > 0.0f || RadiusY > 0.0f);
	}
};

// Editor slider ranges. The clamp is what an asset may hold; this is what the drag resolves.
namespace MixtormatMaskBlurRange
{
	constexpr double RadiusMin = 0.0;
	constexpr double RadiusMax = 32.0;
	constexpr double RadiusDefault = 4.0;
	constexpr double SnapDelta = 0.1;
}
