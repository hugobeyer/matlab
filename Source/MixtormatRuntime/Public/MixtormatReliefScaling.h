// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// How Height Booster is allowed to reach a normal.
//
// A layer carries two independent kinds of relief and they must not be counted twice against the
// same control:
//
//   * the authored/source normal map -- micro detail, the scratches and grain that the height map
//     does not resolve. It is its own signal, not a picture of the height.
//   * height-derived normals -- the macro relief the structural passes reconstruct from the height
//     they just wrote (erosion, chipping, craquelure relief, region relief, the border lift).
//
// The compositor used to set the source normal's gain to HeightBoost outright, so raising the
// booster steepened the authored normal *and* steepened the height every derived pass
// differentiates. A surface whose height and normal maps describe the same relief -- which is the
// normal case for a scanned or baked material -- therefore got the same bump twice and read
// overdriven well before the slider ran out.
//
// The fix is to let the booster own the height and only the height. It reaches the derived normals
// once, implicitly: MixtormatComposite.usf scales the height about its midpoint in
// SampleIncomingHeight, every structural pass differentiates that already-boosted height with a
// fixed Sobel gain, so the derived normal's slope already tracks the booster without anyone
// multiplying it a second time.
//
// On the exponent arithmetic, since the brief asked for a softer-than-linear derived response:
// the derived response as it stands is ~linear in HeightBoost. Multiplying the derived passes by
// sqrt(HeightBoost) on top of that would make the total HeightBoost^1.5 -- steeper, not softer.
// A sqrt *total* would instead need the passes divided by sqrt(HeightBoost), which flattens
// existing content at any boost above 1. Neither is applied here; see the audit notes.
namespace MixtormatRelief
{
	// Upper bound on the booster wherever it is consumed. Shared so the height path and anything
	// reasoning about the normal path cannot drift apart on what "clamped" means.
	inline constexpr float MaxHeightBoost = 8.0f;

	inline float ClampHeightBoost(const float HeightBoost)
	{
		return FMath::Clamp(HeightBoost, 0.0f, MaxHeightBoost);
	}

	// Gain on the authored/source normal map. Neutral, deliberately independent of the booster:
	// micro detail is not what the booster deepens.
	inline float SourceNormalScale(const float /*HeightBoost*/)
	{
		return 1.0f;
	}

	// Extra gain applied to a height-derived normal on top of the boosted height it is
	// differentiated from. One, because that height already carries the booster: anything else
	// here is the second count this whole change exists to remove.
	inline float DerivedNormalScale(const float /*HeightBoost*/)
	{
		return 1.0f;
	}

	// What the booster is actually for: the height itself.
	inline float HeightScale(const float HeightBoost)
	{
		return ClampHeightBoost(HeightBoost);
	}
}
