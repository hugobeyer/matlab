// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatReliefScaling.h"

// Cover for the one thing Height Booster is not allowed to do: count the same relief twice.
//
// A layer can carry relief in two places at once -- an authored normal map and a height map that
// describes the same bumps -- and the compositor reconstructs a normal from the height as well.
// Gaining the authored normal by the booster while the derived normal also steepens with it is
// how a perfectly ordinary scanned material ended up overdriven two notches into the slider.
//
// These are model tests, not GPU tests, and the distinction matters when reading a failure. They
// pin two things:
//
//   * the scaling policy itself, MixtormatReliefScaling.h, which is what the compositor and the
//     preview both call and the only place the answer is decided;
//   * the composite's normal arithmetic, reimplemented here from MixtormatComposite.usf -- the
//     same DecodeNormal, the same reoriented blend, the same midpoint height boost, the same
//     Sobel-over-eight the structural passes use.
//
// The reimplementation is the part that can drift: if MixtormatComposite.usf's blend or
// SampleIncomingHeight's boost changes shape, these keep passing while the real pipeline moves.
// They are still worth having, because the failure they are built to catch -- a gain reappearing
// on the source normal -- is a change to the scaling policy, which they read directly rather
// than model. What they cannot see is anything a shader does that the model does not.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatReliefScalingTests
{
	// The boosters the brief names, plus the ends of the range.
	static const TArray<float> Boosts = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f };

	// Slope of a normal: |xy| / z, the tangent of its tilt. The honest measure of "how much
	// relief is this normal claiming", and unlike the raw xy it does not saturate as the normal
	// approaches the horizon.
	inline double Slope(const FVector3d& Normal)
	{
		return FVector2d(Normal.X, Normal.Y).Length() / FMath::Max(Normal.Z, 1.0e-6);
	}

	// MixtormatComposite.usf: DecodeNormal. A two-channel map, gained, Z rebuilt.
	inline FVector3d DecodeNormal(const FVector2d& EncodedXY, const double Intensity)
	{
		const FVector2d XY = EncodedXY * Intensity;
		return FVector3d(XY.X, XY.Y, FMath::Sqrt(FMath::Max(1.0 - XY.SizeSquared(), 0.0)))
			.GetSafeNormal();
	}

	// MixtormatComposite.usf: BlendReorientedNormals.
	inline FVector3d BlendReorientedNormals(const FVector3d& Base, const FVector3d& Detail)
	{
		const FVector3d T = Base + FVector3d(0.0, 0.0, 1.0);
		const FVector3d U = FVector3d(-Detail.X, -Detail.Y, Detail.Z);
		return (T * (T.Dot(U) / FMath::Max(T.Z, 1.0e-5)) - U).GetSafeNormal();
	}

	// MixtormatComposite.usf: SampleIncomingHeight's boost, signed about the midpoint.
	inline double BoostHeight(const double Height, const double HeightBoost)
	{
		return FMath::Clamp((Height - 0.5) * HeightBoost + 0.5, 0.0, 1.0);
	}

	// What a structural pass gets out of a height ramp: MixtormatHeightDeltaNormal.usf's Sobel
	// with the shipped NormalStrength of 8, which the shader divides by 8 again, so the gradient
	// of the boosted height is exactly the slope the derived normal carries. One texel apart in
	// X, which is all a one-dimensional ramp needs.
	inline FVector3d DerivedNormalFromRamp(
		const double HeightPerTexel,
		const double HeightBoost,
		const double DerivedScale)
	{
		const double Left = BoostHeight(0.5 - HeightPerTexel, HeightBoost);
		const double Right = BoostHeight(0.5 + HeightPerTexel, HeightBoost);
		const double Gradient = (Right - Left) * 0.5 * DerivedScale;
		return FVector3d(Gradient, 0.0, 1.0).GetSafeNormal();
	}

	// The full layer normal the composite arrives at for a given booster: authored normal gained
	// by the policy, height-derived normal blended over it with RNM, both influences applied the
	// way the composite applies them.
	inline FVector3d CompositeNormal(
		const FVector2d& AuthoredXY,
		const double HeightPerTexel,
		const double HeightBoost,
		const double NormalInfluence,
		const double HeightInfluence)
	{
		const FVector3d Source = DecodeNormal(
			AuthoredXY, MixtormatRelief::SourceNormalScale(static_cast<float>(HeightBoost)));

		// NormalInfluence weights the detail normal toward flat before the blend, exactly as
		// the composite's WeightedDetail does.
		const FVector3d WeightedSource = FVector3d(
			Source.X * NormalInfluence,
			Source.Y * NormalInfluence,
			FMath::Lerp(1.0, Source.Z, NormalInfluence)).GetSafeNormal();

		const FVector3d Derived = DerivedNormalFromRamp(
			HeightPerTexel * HeightInfluence,
			HeightBoost,
			MixtormatRelief::DerivedNormalScale(static_cast<float>(HeightBoost)));

		return BlendReorientedNormals(WeightedSource, Derived);
	}

	// The behaviour this change removes, kept as a fixture so the regression is measured rather
	// than asserted from memory: MixtormatGpuCompositor.cpp used to set the source normal's gain
	// to the clamped Height Booster, so the authored normal steepened alongside the height every
	// structural pass differentiates.
	inline FVector3d LegacyCompositeNormal(
		const FVector2d& AuthoredXY,
		const double HeightPerTexel,
		const double HeightBoost)
	{
		const FVector3d Source = DecodeNormal(
			AuthoredXY, FMath::Clamp(HeightBoost, 0.0, 8.0));
		return BlendReorientedNormals(Source, DerivedNormalFromRamp(HeightPerTexel, HeightBoost, 1.0));
	}

	// A height delta a structural effect carves, then what the derived pass makes of it. Chipping,
	// erosion, craquelure relief and region relief all end on the same shared pass over the
	// already-boosted height, so one model covers them all.
	inline double StructuralEffectSlope(const double CarvedDepthPerTexel, const double HeightBoost)
	{
		return Slope(DerivedNormalFromRamp(
			CarvedDepthPerTexel,
			HeightBoost,
			MixtormatRelief::DerivedNormalScale(static_cast<float>(HeightBoost))));
	}

	constexpr double Tolerance = 1.0e-6;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSourceNormalIgnoresHeightBoostTest,
	"Mixtormat.Relief.SourceNormalIgnoresHeightBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSourceNormalIgnoresHeightBoostTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	// The regression itself. The compositor used to hand the source normal HeightBoost outright;
	// anything that reintroduces that tie fails here first and everything else second.
	for (const float Boost : Boosts)
	{
		TestEqual(
			FString::Printf(TEXT("SourceNormalScale at boost %.1f"), Boost),
			MixtormatRelief::SourceNormalScale(Boost),
			1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatDerivedNormalCountedOnceTest,
	"Mixtormat.Relief.DerivedNormalCountedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatDerivedNormalCountedOnceTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	// No second multiplier on the derived side: the boosted height it differentiates already
	// carries the booster, so the pass gain stays neutral.
	for (const float Boost : Boosts)
	{
		TestEqual(
			FString::Printf(TEXT("DerivedNormalScale at boost %.1f"), Boost),
			MixtormatRelief::DerivedNormalScale(Boost),
			1.0f);
	}

	// And the booster does reach the height, which is the half of the contract that stops this
	// from being a global flatten.
	TestEqual(
		TEXT("HeightScale passes the booster through"), MixtormatRelief::HeightScale(4.0f), 4.0f);
	TestEqual(TEXT("HeightScale clamps the top"), MixtormatRelief::HeightScale(99.0f), 8.0f);
	TestEqual(TEXT("HeightScale clamps the bottom"), MixtormatRelief::HeightScale(-3.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatMatchingHeightAndNormalTest,
	"Mixtormat.Relief.MatchingHeightAndNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatMatchingHeightAndNormalTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	// The case the bug was reported against: a surface whose height map and normal map describe
	// the same relief, taken across the boosters the brief names.
	//
	// The result is still allowed to move -- the booster is supposed to deepen the height -- so
	// the assertions are about how fast, measured two ways: against the old behaviour, and
	// against the height contribution taken on its own.
	const FVector2d AuthoredXY(0.15, 0.0);
	constexpr double HeightPerTexel = 0.04;

	const double DerivedAtOne = StructuralEffectSlope(HeightPerTexel, 1.0);
	const double AuthoredReference = Slope(CompositeNormal(AuthoredXY, 0.0, 1.0, 1.0, 1.0));
	const double CombinedAtOne = Slope(CompositeNormal(AuthoredXY, HeightPerTexel, 1.0, 1.0, 1.0));

	for (const float Boost : { 1.0f, 2.0f, 4.0f })
	{
		const double Combined = Slope(CompositeNormal(AuthoredXY, HeightPerTexel, Boost, 1.0, 1.0));
		const double HeightOnly = StructuralEffectSlope(HeightPerTexel, Boost);
		const double SourceOnly = Slope(CompositeNormal(AuthoredXY, 0.0, Boost, 1.0, 1.0));

		// The authored half is the same relief at every booster.
		TestEqual(
			FString::Printf(TEXT("Authored slope is boost-invariant at %.1f"), Boost),
			SourceOnly,
			AuthoredReference,
			Tolerance);

		// Against the old behaviour, reproduced here rather than described: the compositor used
		// to gain the authored normal by the clamped booster as well. At boost 1 the two agree
		// exactly -- nothing about the neutral case changed -- and above it the old one runs
		// away, which is the doubling. At boost 4 with this fixture it reaches roughly three
		// times the new slope, and a steeper authored normal drives it clean into the horizon.
		const double LegacyCombined = Slope(LegacyCompositeNormal(AuthoredXY, HeightPerTexel, Boost));
		if (Boost == 1.0f)
		{
			TestEqual(
				TEXT("Boost 1 is unchanged by this fix"), Combined, LegacyCombined, Tolerance);
		}
		else
		{
			TestTrue(
				FString::Printf(TEXT("Boost %.1f no longer doubles the relief"), Boost),
				Combined < LegacyCombined - 1.0e-4);
		}

		// No relief doubling as the booster rises: the combined slope grows no faster than the
		// height contribution does, which is once. Two contributions each scaling with the
		// booster would exceed this bound.
		TestTrue(
			FString::Printf(TEXT("Combined relief grows at most linearly at %.1f"), Boost),
			Combined <= CombinedAtOne * Boost + Tolerance);

		// And the derived half tracks the booster once, linearly, rather than compounding.
		TestEqual(
			FString::Printf(TEXT("Derived slope is linear in boost at %.1f"), Boost),
			HeightOnly,
			DerivedAtOne * Boost,
			1.0e-4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatReliefSourceCombinationsTest,
	"Mixtormat.Relief.SourceCombinations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatReliefSourceCombinationsTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	const FVector2d AuthoredXY(0.15, 0.0);
	constexpr double HeightPerTexel = 0.04;

	// Normal-only: nothing for the booster to act on, so raising it must change nothing at all.
	// This is the case a global flatten would also pass, which is why the height-only case below
	// has to fail a flatten.
	const double NormalOnlyAtOne = Slope(CompositeNormal(AuthoredXY, 0.0, 1.0, 1.0, 1.0));
	for (const float Boost : Boosts)
	{
		TestEqual(
			FString::Printf(TEXT("Normal-only material is unmoved at boost %.1f"), Boost),
			Slope(CompositeNormal(AuthoredXY, 0.0, Boost, 1.0, 1.0)),
			NormalOnlyAtOne,
			Tolerance);
	}

	// Height-only: the booster is the whole control here and must still bite. A change that
	// merely flattened normals globally would fail this.
	const double HeightOnlyAtOne =
		Slope(CompositeNormal(FVector2d::ZeroVector, HeightPerTexel, 1.0, 1.0, 1.0));
	const double HeightOnlyAtFour =
		Slope(CompositeNormal(FVector2d::ZeroVector, HeightPerTexel, 4.0, 1.0, 1.0));
	TestTrue(
		TEXT("Height-only relief deepens with the booster"),
		HeightOnlyAtFour > HeightOnlyAtOne * 3.0);

	// Height + normal: the authored micro detail survives alongside the height-derived relief at
	// every booster. That is the other half of "do not just flatten".
	for (const float Boost : Boosts)
	{
		const double Both = Slope(CompositeNormal(AuthoredXY, HeightPerTexel, Boost, 1.0, 1.0));
		const double NormalOnly = Slope(CompositeNormal(AuthoredXY, 0.0, Boost, 1.0, 1.0));
		TestTrue(
			FString::Printf(TEXT("Authored detail survives alongside height at boost %.1f"), Boost),
			Both >= NormalOnly - Tolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatReliefInfluencesTest,
	"Mixtormat.Relief.Influences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatReliefInfluencesTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	const FVector2d AuthoredXY(0.15, 0.0);
	constexpr double HeightPerTexel = 0.04;

	for (const float Boost : Boosts)
	{
		// Normal Influence 0 mutes the authored normal and leaves the height-derived relief,
		// which is the separation this whole change is for: the two are independently addressable.
		const double NoNormal = Slope(CompositeNormal(AuthoredXY, HeightPerTexel, Boost, 0.0, 1.0));
		const double HeightOnly =
			Slope(CompositeNormal(FVector2d::ZeroVector, HeightPerTexel, Boost, 1.0, 1.0));
		TestEqual(
			FString::Printf(TEXT("Normal Influence 0 leaves only derived relief at boost %.1f"), Boost),
			NoNormal,
			HeightOnly,
			1.0e-4);

		// Height Influence 0 mutes the derived relief and leaves the authored normal, unscaled
		// by the booster. Under the old tie this line still moved with the slider.
		const double NoHeight = Slope(CompositeNormal(AuthoredXY, HeightPerTexel, Boost, 1.0, 0.0));
		const double NormalOnly = Slope(CompositeNormal(AuthoredXY, 0.0, 1.0, 1.0, 1.0));
		TestEqual(
			FString::Printf(TEXT("Height Influence 0 leaves only the authored normal at boost %.1f"), Boost),
			NoHeight,
			NormalOnly,
			1.0e-4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStructuralEffectHeightDeltasTest,
	"Mixtormat.Relief.StructuralEffectHeightDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatStructuralEffectHeightDeltasTest::RunTest(const FString&)
{
	using namespace MixtormatReliefScalingTests;

	// Chipping, erosion, craquelure relief and region relief all carve the accumulated height and
	// then hand the before/after pair to the one shared height-derived normal pass at a fixed
	// gain. So they share one question: does the carved relief pick the booster up once, or twice?
	//
	// Depths chosen to stay off the 0..1 clamp at boost 8, where a saturated height would flatten
	// the gradient and hide a doubling rather than expose it.
	const TArray<double> CarvedDepths = { 0.01, 0.02, 0.05 };

	for (const double Depth : CarvedDepths)
	{
		const double AtOne = StructuralEffectSlope(Depth, 1.0);
		for (const float Boost : { 2.0f, 4.0f, 8.0f })
		{
			const double Boosted = StructuralEffectSlope(Depth, Boost);
			TestEqual(
				FString::Printf(TEXT("Carved delta %.2f scales once at boost %.1f"), Depth, Boost),
				Boosted,
				AtOne * Boost,
				1.0e-4);
		}
	}

	// Zero booster flattens the structural relief entirely, which is the documented meaning of 0.
	TestEqual(TEXT("Booster 0 removes carved relief"), StructuralEffectSlope(0.05, 0.0), 0.0, Tolerance);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
