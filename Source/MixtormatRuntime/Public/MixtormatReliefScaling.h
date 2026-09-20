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

	// Gain the booster is allowed to put on the authored/source normal map. Neutral, always:
	// micro detail is not what the booster deepens. Kept as a named function rather than inlined
	// as 1.0 so the invariant has somewhere to be read and a reintroduced tie fails here first.
	// No production call site consults the booster for normal strength any more.
	inline float SourceNormalScale(const float /*HeightBoost*/)
	{
		return 1.0f;
	}

	// Upper bound on Normal Strength. Generous, because the control is a tangent-slope gain and
	// the decode stays well conditioned at any positive value; this only stops a typed absurdity.
	inline constexpr float MaxNormalStrength = 8.0f;

	// The artist-facing strength of the authored normal map, as a tangent-slope gain: 0 is flat,
	// 1 is the map as authored, 2 doubles its tilt. This is the only control that steepens an
	// imported normal, and Height Booster is deliberately not in its signature -- the booster owns
	// the height and the normals derived from that height, and joining the two here is precisely
	// the double count this header exists to prevent.
	inline float AuthoredNormalScale(const float NormalStrength)
	{
		return FMath::Clamp(NormalStrength, 0.0f, MaxNormalStrength);
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
