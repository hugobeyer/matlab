// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Style/MixtormatCompositing.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

// Cover for the shared rendering primitives introduced by the prototype port.
//
//   Saturate     CSS `filter: saturate(x)`, applied per paint layer rather than hoisted into a
//                palette role. The accent is authored at three different saturations, so the helper
//                must be a pure transform: the input has to come back untouched for the next caller.
//
//   SoftLight    the W3C separable blend. It is the one operation that cannot be reached by
//                rearranging a single colour, which is exactly why it needed writing out. These
//                tests pin the three anchors that distinguish it from an add and from a multiply,
//                and assert that it disagrees with both -- a soft-light that silently degenerated
//                into multiply would still satisfy "darkens a dark ground" on its own.
//
//   Falloff      the shared power curve behind the slider fill, the foldout lift and the card body.
//                Power 1 must be a straight lerp or every ramp authored against it drifts.
//
// These are pure functions and the file is deliberately free of Slate draw calls, RHI and
// production widgets. A widget would only add a dependency on layout that these answers do not have.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatCompositingTests
{
	constexpr float Tolerance = 1.0e-5f;

	bool Near(const float A, const float B)
	{
		return FMath::IsNearlyEqual(A, B, Tolerance);
	}

	bool NearColor(const FLinearColor& A, const FLinearColor& B)
	{
		return Near(A.R, B.R) && Near(A.G, B.G) && Near(A.B, B.B) && Near(A.A, B.A);
	}

	// FStop has no operator<, so TArray::IsSorted does not apply; the ordering claim is checked
	// directly instead of being papered over with a comparison the type does not support.
	bool IsOrdered(const TArray<MixtormatGradient::FStop, TInlineAllocator<16>>& Stops)
	{
		for (int32 Index = 1; Index < Stops.Num(); ++Index)
		{
			if (Stops[Index].Position < Stops[Index - 1].Position)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSaturateTest,
	"Mixtormat.Style.Compositing.Saturate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSaturateTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositingTests;
	using namespace MixtormatCompositing;
	const FLinearColor Source(0.4f, 0.2f, 0.1f, 0.5f);

	// 1.0 is the identity, exactly -- callers pass it unconditionally on unsaturated surfaces, so
	// any drift here would tint every one of them.
	TestTrue(TEXT("Saturation 1 is the identity"), NearColor(Saturate(Source, 1.0f), Source));

	// 0 collapses to Rec.709 luma on all three channels, which is what greyscale means.
	const FLinearColor Grey = Saturate(Source, 0.0f);
	const float Luma = 0.2126f * Source.R + 0.7152f * Source.G + 0.0722f * Source.B;
	TestTrue(TEXT("Saturation 0 collapses to luma"), Near(Grey.R, Luma) && Near(Grey.G, Luma) && Near(Grey.B, Luma));

	// Above 1 pushes channels away from luma. A warm colour has R above luma and B below, so the
	// two must move in opposite directions -- a lerp that moved both the same way would be a
	// brightness change wearing a saturation name.
	const FLinearColor Boosted = Saturate(Source, 2.0f);
	TestTrue(TEXT("Saturation 2 lifts R above luma"), Boosted.R > Luma);
	TestTrue(TEXT("Saturation 2 drops B below luma"), Boosted.B < Luma);
	TestTrue(TEXT("Saturation 2 increases chroma"),
		Boosted.R - Boosted.B > Source.R - Source.B);

	// Alpha is untouched at every amount: saturation is a colour operation, and letting it touch
	// alpha would silently restyle the surface behind the fill as well as the fill.
	TestTrue(TEXT("Alpha preserved at 0"), Near(Saturate(Source, 0.0f).A, Source.A));
	TestTrue(TEXT("Alpha preserved at 1"), Near(Saturate(Source, 1.0f).A, Source.A));
	TestTrue(TEXT("Alpha preserved at 2"), Near(Saturate(Source, 2.0f).A, Source.A));

	// The helper must be a pure transform. The shared palette colour is reused by surfaces at
	// different saturations, so an in-place variant here would make the second caller depend on
	// whichever surface painted first.
	TestTrue(TEXT("Input is not mutated"), NearColor(Source, FLinearColor(0.4f, 0.2f, 0.1f, 0.5f)));

	// A neutral colour has no chroma to amplify, so every amount returns it unchanged. This is the
	// case that would break loudly if the implementation ever divided by a chroma term.
	const FLinearColor Neutral(0.5f, 0.5f, 0.5f, 1.0f);
	TestTrue(TEXT("Neutral colour is stable at 1.5"), NearColor(Saturate(Neutral, 1.5f), Neutral));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSoftLightTest,
	"Mixtormat.Style.Compositing.BlendModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSoftLightTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositingTests;
	using namespace MixtormatCompositing;

	const FLinearColor Opaque(1.0f, 1.0f, 1.0f, 1.0f);

	// Additive: source added at its alpha, ground alpha preserved. A zero-alpha source must add
	// nothing at all -- that is the "contribution reaches zero at the seam" case the foldout lift
	// depends on.
	const FLinearColor Ground(0.2f, 0.2f, 0.2f, 1.0f);
	const FLinearColor Clear(0.8f, 0.4f, 0.1f, 0.0f);
	TestTrue(TEXT("Additive ignores a clear source"), NearColor(Additive(Ground, Clear), Ground));

	const FLinearColor Lifted = Additive(Ground, FLinearColor(0.8f, 0.4f, 0.1f, 0.5f));
	TestTrue(TEXT("Additive adds source at alpha"), Near(Lifted.R, 0.2f + 0.8f * 0.5f));
	TestTrue(TEXT("Additive clamps at white"), Near(Additive(Opaque, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)).R, 1.0f));
	TestTrue(TEXT("Additive preserves ground alpha"), Near(Additive(Ground, FLinearColor(0.8f, 0.4f, 0.1f, 0.5f)).A, Ground.A));

	// Multiply: black at alpha a must reduce to ground * (1 - a), which is the exact pass
	// SMixtormatGradientBox already paints. Pinning the two together is what stops a new caller
	// from getting a second, subtly different darkening.
	const FLinearColor Shaded = Multiply(Ground, FLinearColor(0.0f, 0.0f, 0.0f, 0.25f));
	TestTrue(TEXT("Multiply black-at-alpha matches ground * (1 - a)"), Near(Shaded.R, Ground.R * 0.75f));
	TestTrue(TEXT("Multiply preserves ground alpha"), Near(Shaded.A, Ground.A));
	TestTrue(TEXT("Multiply with a clear source is a no-op"),
		NearColor(Multiply(Ground, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)), Ground));
	TestTrue(TEXT("Multiply with white is a no-op"),
		NearColor(Multiply(Ground, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)), Ground));

	// Soft-light's three anchors, straight from the spec.
	TestTrue(TEXT("Soft-light: black source darkens"),
		Near(SoftLightChannel(0.5f, 0.0f), 0.25f));
	TestTrue(TEXT("Soft-light: mid source is the identity"),
		Near(SoftLightChannel(0.5f, 0.5f), 0.5f));
	TestTrue(TEXT("Soft-light: white source lightens"),
		Near(SoftLightChannel(0.5f, 1.0f), 0.5f + (FMath::Sqrt(0.5f) - 0.5f)));

	// D(Cb) is piecewise, and the sqrt branch above 0.25 is load-bearing rather than cosmetic.
	// The polynomial alone returns 3.712 at Cb = 0.8, which clamping alone would hide as a
	// flat white: the result would stop responding to the source colour entirely.
	TestTrue(TEXT("Soft-light stays in range on a light ground"),
		SoftLightChannel(0.8f, 1.0f) <= 1.0f);
	TestTrue(TEXT("Soft-light lightening branch is not clamped flat"),
		!Near(SoftLightChannel(0.8f, 0.75f), SoftLightChannel(0.8f, 1.0f)));
	TestTrue(TEXT("Soft-light is monotonic in the source"),
		SoftLightChannel(0.5f, 0.25f) < SoftLightChannel(0.5f, 0.75f));

	// The property that justifies the whole helper: soft-light disagrees with both neighbours. If
	// it matched multiply it would be the second-axis darkening pass again, and if it matched
	// additive it would be the lift, and both are the wrong shape for a tint layer.
	const FLinearColor Backdrop(0.5f, 0.5f, 0.5f, 1.0f);
	const FLinearColor Dark(0.0f, 0.0f, 0.0f, 1.0f);
	const FLinearColor Light(1.0f, 1.0f, 1.0f, 1.0f);
	TestFalse(TEXT("Soft-light differs from multiply (dark source)"),
		Near(SoftLightChannel(0.5f, 0.0f), Multiply(Backdrop, Dark).R));
	TestFalse(TEXT("Soft-light differs from multiply (light source)"),
		Near(SoftLightChannel(0.5f, 1.0f), Multiply(Backdrop, Light).R));
	TestFalse(TEXT("Soft-light differs from additive (dark source)"),
		Near(SoftLightChannel(0.5f, 0.0f), Additive(Backdrop, Dark).R));

	// A light source on a dark ground lightens it. Additive can brighten too, but only upward from
	// zero; soft-light is also able to darken a light ground with a dark source, so neither
	// operation's result is a special case of the other.
	TestTrue(TEXT("Soft-light lightens a dark ground"),
		SoftLightChannel(0.2f, 1.0f) > 0.2f);
	TestTrue(TEXT("Soft-light darkens a light ground"),
		SoftLightChannel(0.8f, 0.0f) < 0.8f);

	// Coverage: a clear source leaves the backdrop untouched, and the result's alpha is the
	// backdrop's. Both matter -- these layers sit on a panel that must stay as opaque as it was.
	TestTrue(TEXT("Soft-light with a clear source is a no-op"),
		NearColor(SoftLight(Backdrop, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)), Backdrop));
	TestTrue(TEXT("Soft-light preserves backdrop alpha"),
		Near(SoftLight(Backdrop, Dark).A, Backdrop.A));

	// Strength scales coverage, not direction.
	TestTrue(TEXT("Soft-light strength scales coverage"),
		Near(SoftLight(Backdrop, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 0.5f).R, 0.5f + (FMath::Sqrt(0.5f) - 0.5f) * 0.5f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatFalloffTest,
	"Mixtormat.Style.Gradient.Falloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatFalloffTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositingTests;
	using namespace MixtormatGradient;
	using MixtormatGradient::FStop;

	// Power 1 is the straight lerp. Everything authored as a ramp is calibrated against this, so a
	// regression here silently bends every falloff in the tool at once.
	TestTrue(TEXT("Power 1 is linear"), Near(FalloffValue(0.25f, 0.8f, 0.2f, 1.0f), 0.65f));
	TestTrue(TEXT("Power 1 at the ends"), Near(FalloffValue(0.0f, 0.8f, 0.2f, 1.0f), 0.8f)
		&& Near(FalloffValue(1.0f, 0.8f, 0.2f, 1.0f), 0.2f));

	// The documented reading of the exponent, for a decreasing fade: below 1 falls away early,
	// above 1 holds the start value longer. Compared at the midpoint so the two differ visibly.
	const float Shallow = FalloffValue(0.5f, 1.0f, 0.0f, 0.5f);
	const float Linear = FalloffValue(0.5f, 1.0f, 0.0f, 1.0f);
	const float Steep = FalloffValue(0.5f, 1.0f, 0.0f, 2.0f);
	TestTrue(TEXT("Power below 1 fades earlier"), Shallow < Linear);
	TestTrue(TEXT("Power above 1 holds the start longer"), Steep > Linear);
	TestTrue(TEXT("Distinct powers produce distinct curves"), !Near(Shallow, Steep));

	// The prototype clamps the exponent at 0.01 rather than letting pow(t, 0) do something
	// undefined at t = 0. A zero or negative power must therefore still be finite and monotonic.
	TestTrue(TEXT("Zero power is clamped, not undefined"), FMath::IsFinite(FalloffValue(0.0f, 1.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Negative power is clamped"), FMath::IsFinite(FalloffValue(0.5f, 1.0f, 0.0f, -1.0f)));
	TestTrue(TEXT("Clamped power still decreases"),
		FalloffValue(0.5f, 1.0f, 0.0f, 0.0f) < FalloffValue(0.0f, 1.0f, 0.0f, 0.0f));

	// An increasing ramp, which is the slider fill's active state: the exponent reverses direction
	// with the endpoints, so a swap of start and end must give the mirrored curve.
	TestTrue(TEXT("Increasing ramp works"),
		Near(FalloffValue(0.0f, 0.2f, 0.8f, 1.0f), 0.2f)
		&& Near(FalloffValue(1.0f, 0.2f, 0.8f, 1.0f), 0.8f));

	// Stop generation.
	const FLinearColor Accent(0.3f, 0.6f, 0.7f, 1.0f);
	TArray<FStop, TInlineAllocator<16>> Stops;
	MixtormatGradient::AppendFalloffStops(Stops, Accent, 0.9f, 0.0f, 1.0f, 0.0f, 1.0f, 8);
	TestEqual(TEXT("Requested sample count"), Stops.Num(), 8);
	TestTrue(TEXT("Stops span the requested range"),
		Near(Stops[0].Position, 0.0f) && Near(Stops.Last().Position, 1.0f));
	TestTrue(TEXT("Stop positions are ordered"), IsOrdered(Stops));

	// Only alpha varies along a falloff; the RGB is carried through untouched. A ramp that
	// interpolated the colour as well would be a different effect from the one authored.
	TestTrue(TEXT("Stop RGB is carried through unchanged"),
		Near(Stops[3].Color.R, Accent.R) && Near(Stops[3].Color.G, Accent.G) && Near(Stops[3].Color.B, Accent.B));
	TestTrue(TEXT("Stop alpha follows the curve"),
		Near(Stops[0].Color.A, 0.9f) && Near(Stops.Last().Color.A, 0.0f));

	// Appending must append, not clear: callers build a ramp and then keep adding stops to it.
	MixtormatGradient::AppendFalloffStops(Stops, Accent, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f, 4);
	TestEqual(TEXT("Append adds to the existing stops"), Stops.Num(), 12);

	// A one-stop ramp is a flat fill that Paint refuses to draw, so it is raised to two rather than
	// silently producing nothing.
	TArray<FStop, TInlineAllocator<16>> Single;
	MixtormatGradient::AppendFalloffStops(Single, Accent, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1);
	TestEqual(TEXT("Sample count is clamped to 2"), Single.Num(), 2);

	// A sub-span, which is how a falloff covers only part of a surface.
	TArray<FStop, TInlineAllocator<16>> Span;
	MixtormatGradient::AppendFalloffStops(Span, Accent, 1.0f, 0.0f, 1.0f, 0.25f, 0.5f, 4);
	TestTrue(TEXT("Stop positions respect the requested span"),
		Near(Span[0].Position, 0.25f) && Near(Span.Last().Position, 0.5f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS