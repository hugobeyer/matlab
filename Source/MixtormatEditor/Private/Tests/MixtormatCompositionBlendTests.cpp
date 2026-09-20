// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatMaterial.h"
#include "MixtormatReliefScaling.h"
#include "UI/Layers/MixtormatLayerBadges.h"

// Cover for two things that are easy to confuse and must stay apart.
//
//   BLEND               merges this layer's height with the surface below instead of cross-fading
//                       it, and reorients the normal onto what is below. It moves no coverage:
//                       base colour, roughness, AO, metallic and F0 composite exactly as under
//                       OVER. A height operation, nothing more.
//
//   Height Mask Blend   an independently armed feature (bHeightBlendEnabled) where the height
//                       decides *visibility*, and one winner drives every channel together.
//
// Picking BLEND must not switch the second one on -- it drags a dozen authored controls with it
// -- and the second one must keep working when it is armed. Both halves are pinned here.
//
// The UI contract is read off the shipping MixtormatLayerBadges functions. The arithmetic is
// reimplemented from MixtormatComposite.usf: the same smooth maximum, the same coverage split,
// the same normal decode. The reimplementation is what can drift -- if the shader changes shape
// these keep passing while the pipeline moves -- but what they are built to catch is structural:
// a channel picking up a mask it should not have, the height reverting to a plain lerp, a gain
// moving onto the wrong control. What they cannot see is anything the shader does that the model
// does not.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatCompositionBlendTests
{
	constexpr double Tolerance = 1.0e-6;

	inline double Saturate(const double Value)
	{
		return FMath::Clamp(Value, 0.0, 1.0);
	}

	// MixtormatComposite.usf: the layer parameters the height paths read.
	struct FBlendSettings
	{
		double Softness = 0.1;      // HeightRange
		double Threshold = 0.5;     // HeightThreshold
		double Contest = 1.0;       // HeightBlendAmount, saturated
		double Amount = 1.0;        // HeightBlendAmount, unsaturated (sharpens above 1)
		double BaseBias = 0.0;      // HeightBias
		double IncomingBias = 0.0;  // HeightOffset
	};

	// MixtormatComposite.usf: FMixtormatHeightBlend / EvaluateHeightBlend. Height Mask Blending
	// only -- this is the path where coverage comes out of the height contest.
	struct FHeightBlend
	{
		double Weight = 0.0;
		double Height = 0.0;
		double Base = 0.0;
	};

	inline FHeightBlend EvaluateHeightBlend(
		const double BaseHeight,
		const double IncomingHeight,
		const double Coverage,
		const FBlendSettings& Settings)
	{
		const double A = BaseHeight + Settings.BaseBias;
		const double B = IncomingHeight + Settings.IncomingBias;
		const double K = FMath::Max(Settings.Softness, 1.0e-6) / FMath::Max(Settings.Amount, 1.0);

		const double Contested = B + (0.5 - Settings.Threshold);
		const double HeightWinner = Saturate(0.5 + 0.5 * (Contested - A) / K);

		FHeightBlend Result;
		Result.Base = A;
		Result.Weight = FMath::Lerp(Coverage, Coverage * HeightWinner, Settings.Contest);
		Result.Height = FMath::Lerp(A, B, Result.Weight)
			+ K * HeightWinner * (1.0 - HeightWinner) * Coverage * Settings.Contest;
		return Result;
	}

	// MixtormatComposite.usf: the BLEND branch. A polynomial smooth maximum at the layer's
	// Softness, with no coverage in it -- coverage is applied to the result afterwards.
	inline double SmoothHeightMerge(
		const double BaseHeight,
		const double IncomingHeight,
		const double Softness)
	{
		const double K = FMath::Max(Softness, 1.0e-6);
		const double W = Saturate(0.5 + 0.5 * (IncomingHeight - BaseHeight) / K);
		return FMath::Lerp(BaseHeight, IncomingHeight, W) + K * W * (1.0 - W);
	}

	struct FSurface
	{
		double BaseColor = 0.0;
		double Roughness = 0.0;
		double AO = 0.0;
		double Metallic = 0.0;
		double F0 = 0.0;
		double Height = 0.0;
	};

	struct FComposite
	{
		double Alpha = 0.0;
		double BaseColor = 0.0;
		double Roughness = 0.0;
		double AO = 0.0;
		double Metallic = 0.0;
		double F0 = 0.0;
		double NormalWeight = 0.0;
		double Height = 0.0;
	};

	// How the layer composites its height. The three cases are exclusive, and which one runs is
	// the whole subject of this file.
	enum class EHeightMode : uint8
	{
		Over,        // cross-fade, the ordinary case
		Merge,       // BLEND: smooth maximum, coverage untouched
		MaskBlend,   // Height Mask Blending: the height decides coverage too
	};

	// MixtormatComposite.usf: MainCS, the coverage split and the height.
	inline FComposite Composite(
		const FSurface& Previous,
		const FSurface& Incoming,
		const double Opacity,
		const double Mask,
		const double NonHeightCoverage,
		const EHeightMode Mode,
		const FBlendSettings& Settings,
		const double NormalInfluence = 1.0,
		const double HeightInfluence = 1.0)
	{
		const double Coverage = Saturate(Opacity * Mask);

		FHeightBlend Blend;
		Blend.Weight = 1.0;
		Blend.Height = Incoming.Height;
		Blend.Base = Previous.Height;
		if (Mode == EHeightMode::MaskBlend)
		{
			Blend = EvaluateHeightBlend(Previous.Height, Incoming.Height, Coverage, Settings);
		}

		// Only Height Mask Blending takes its coverage from the height. BLEND and OVER share
		// this line, which is what makes them identical on every channel but the height.
		const double SurfaceCoverage = Mode == EHeightMode::MaskBlend ? Blend.Weight : Coverage;

		FComposite Out;
		Out.Alpha = Saturate(SurfaceCoverage * NonHeightCoverage);
		Out.BaseColor = FMath::Lerp(Previous.BaseColor, Incoming.BaseColor, Out.Alpha);
		Out.Roughness = FMath::Lerp(Previous.Roughness, Incoming.Roughness, Out.Alpha);
		Out.AO = FMath::Lerp(Previous.AO, Incoming.AO, Out.Alpha);
		Out.Metallic = FMath::Lerp(Previous.Metallic, Incoming.Metallic, Out.Alpha);
		Out.F0 = FMath::Lerp(Previous.F0, Incoming.F0, Out.Alpha);
		Out.NormalWeight = Out.Alpha * Saturate(NormalInfluence);

		double LayerHeight = 0.0;
		switch (Mode)
		{
		case EHeightMode::MaskBlend:
			LayerHeight = FMath::Lerp(Blend.Base, Blend.Height, NonHeightCoverage);
			break;
		case EHeightMode::Merge:
			LayerHeight = FMath::Lerp(
				Previous.Height,
				SmoothHeightMerge(Previous.Height, Incoming.Height, Settings.Softness),
				Out.Alpha);
			break;
		default:
			LayerHeight = FMath::Lerp(Previous.Height, Incoming.Height, Out.Alpha);
			break;
		}
		Out.Height = FMath::Lerp(Previous.Height, LayerHeight, Saturate(HeightInfluence));
		return Out;
	}

	// MixtormatComposite.usf: MixtormatScaleNormalSlope.
	inline FVector3d ScaleNormalSlope(const FVector3d& Normal, const double Strength)
	{
		return FVector3d(
			Normal.X * Strength,
			Normal.Y * Strength,
			FMath::Max(Normal.Z, 1.0e-4)).GetSafeNormal();
	}

	// MixtormatComposite.usf: DecodeNormal. Decoded first, slope taken up after.
	inline FVector3d DecodeNormal(const FVector2d& EncodedXY, const double Strength)
	{
		const double Z = FMath::Sqrt(FMath::Max(1.0 - EncodedXY.SizeSquared(), 0.0));
		return ScaleNormalSlope(FVector3d(EncodedXY.X, EncodedXY.Y, Z), Strength);
	}

	// The form this replaces: gain the tangent pair, then rebuild Z from whatever is left.
	inline FVector3d LegacyDecodeNormal(const FVector2d& EncodedXY, const double Strength)
	{
		const FVector2d XY = EncodedXY * Strength;
		return FVector3d(
			XY.X, XY.Y, FMath::Sqrt(FMath::Max(1.0 - XY.SizeSquared(), 0.0))).GetSafeNormal();
	}

	// MixtormatComposite.usf: BlendReorientedNormals.
	inline FVector3d BlendReorientedNormals(const FVector3d& Base, const FVector3d& Detail)
	{
		const FVector3d T = Base + FVector3d(0.0, 0.0, 1.0);
		const FVector3d U = FVector3d(-Detail.X, -Detail.Y, Detail.Z);
		return (T * (T.Dot(U) / FMath::Max(T.Z, 1.0e-5)) - U).GetSafeNormal();
	}

	// MixtormatComposite.usf: WeightedDetail, the influence fade toward flat.
	inline FVector3d WeightDetail(const FVector3d& Normal, const double Weight)
	{
		return FVector3d(
			Normal.X * Weight,
			Normal.Y * Weight,
			FMath::Lerp(1.0, Normal.Z, Weight)).GetSafeNormal();
	}

	// |xy|/z, the tangent of the normal's tilt. Unlike raw xy it does not saturate as the normal
	// approaches the horizon, which is exactly the failure the decode fix is about.
	inline double Slope(const FVector3d& Normal)
	{
		return FVector2d(Normal.X, Normal.Y).Length() / FMath::Max(Normal.Z, 1.0e-9);
	}

	FSurface MakeSurface(const double Value, const double Height)
	{
		FSurface S;
		S.BaseColor = Value;
		S.Roughness = Value;
		S.AO = Value;
		S.Metallic = Value;
		S.F0 = Value;
		S.Height = Height;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCompositionContractTest,
	"Mixtormat.Composition.BlendOverContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatCompositionContractTest::RunTest(const FString&)
{
	using namespace MixtormatLayerBadges;

	FMixtormatLayer Layer;

	ApplyComposition(Layer, EComposition::Blend);
	TestEqual(TEXT("BLEND reorients the normal below"),
		Layer.NormalBlendMode, EMixtormatNormalBlendMode::Combine);
	TestEqual(TEXT("BLEND is a surface layer"),
		Layer.ChannelMode, EMixtormatLayerChannelMode::CompleteSurface);
	TestEqual(TEXT("BLEND composites as Replace"),
		Layer.CompositionMode, EMixtormatCompositionMode::Replace);
	TestEqual(TEXT("BLEND round trips"), CompositionOf(Layer), EComposition::Blend);

	ApplyComposition(Layer, EComposition::Over);
	TestEqual(TEXT("OVER replaces the normal below"),
		Layer.NormalBlendMode, EMixtormatNormalBlendMode::Override);
	TestEqual(TEXT("OVER round trips"), CompositionOf(Layer), EComposition::Over);

	ApplyComposition(Layer, EComposition::Coat);
	TestEqual(TEXT("COAT round trips"), CompositionOf(Layer), EComposition::Coat);
	TestEqual(TEXT("COAT composites as a coat"),
		Layer.CompositionMode, EMixtormatCompositionMode::Coat);

	ApplyComposition(Layer, EComposition::Detail);
	TestEqual(TEXT("DETAIL round trips"), CompositionOf(Layer), EComposition::Detail);
	TestEqual(TEXT("DETAIL contributes only a normal"),
		Layer.ChannelMode, EMixtormatLayerChannelMode::NormalDetail);

	// The separation this whole revision is about. Height Mask Blending arms a dozen authored
	// controls and decides coverage from the height; the composition badge must leave it exactly
	// as the artist set it, whichever way the badge is flipped and however often.
	for (const bool bArmed : { false, true })
	{
		for (const EComposition Choice :
			{ EComposition::Blend, EComposition::Over, EComposition::Coat, EComposition::Detail })
		{
			FMixtormatLayer Probe;
			Probe.bHeightBlendEnabled = bArmed;
			ApplyComposition(Probe, Choice);
			TestEqual(
				TEXT("The composition choice never arms or disarms Height Mask Blending"),
				Probe.bHeightBlendEnabled,
				bArmed);
		}
	}

	// Every choice must leave Detail behind, or a layer that had once been Detail keeps
	// contributing only its normal while the control claims it is blending.
	for (const EComposition Choice :
		{ EComposition::Blend, EComposition::Over, EComposition::Coat })
	{
		FMixtormatLayer FromDetail;
		ApplyComposition(FromDetail, EComposition::Detail);
		ApplyComposition(FromDetail, Choice);
		TestEqual(
			TEXT("Leaving DETAIL restores a surface layer"),
			FromDetail.ChannelMode,
			EMixtormatLayerChannelMode::CompleteSurface);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBlendTouchesHeightOnlyTest,
	"Mixtormat.Composition.BlendTouchesHeightOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatBlendTouchesHeightOnlyTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	// BLEND is a height operation. Every other channel has to come out bit-identical to OVER at
	// the same opacity and mask, or the badge is quietly moving colour as a side effect of a
	// choice about relief.
	const FBlendSettings Settings;
	int32 ChannelDifferences = 0;
	int32 HeightDifferences = 0;

	for (const double BaseHeight : { 0.1, 0.35, 0.5, 0.75, 0.9 })
	{
		for (const double IncomingHeight : { 0.15, 0.5, 0.65, 0.95 })
		{
			for (const double Mask : { 0.0, 0.3, 0.6, 1.0 })
			{
				for (const double Opacity : { 0.4, 1.0 })
				{
					const FSurface Below = MakeSurface(0.2, BaseHeight);
					const FSurface Above = MakeSurface(0.9, IncomingHeight);

					const FComposite Blend = Composite(
						Below, Above, Opacity, Mask, 1.0, EHeightMode::Merge, Settings);
					const FComposite Over = Composite(
						Below, Above, Opacity, Mask, 1.0, EHeightMode::Over, Settings);

					ChannelDifferences += FMath::IsNearlyEqual(Blend.Alpha, Over.Alpha, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.BaseColor, Over.BaseColor, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.Roughness, Over.Roughness, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.AO, Over.AO, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.Metallic, Over.Metallic, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.F0, Over.F0, 1.0e-12) ? 0 : 1;
					ChannelDifferences += FMath::IsNearlyEqual(Blend.NormalWeight, Over.NormalWeight, 1.0e-12) ? 0 : 1;

					HeightDifferences += FMath::IsNearlyEqual(Blend.Height, Over.Height, 1.0e-6) ? 0 : 1;
				}
			}
		}
	}

	TestEqual(TEXT("BLEND changes no channel but the height"), ChannelDifferences, 0);

	// ... and it does change the height, or the mode is decorative.
	TestTrue(TEXT("BLEND does change the height"), HeightDifferences > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBlendHeightMergeTest,
	"Mixtormat.Composition.BlendHeightMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatBlendHeightMergeTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	const FBlendSettings Settings;
	const FSurface Below = MakeSurface(0.0, 0.5);

	// The regression this replaces. A cross-fade returns the average of two surfaces where they
	// intersect, so a crossing reads as a crease pressed flat. The smooth maximum keeps the upper
	// surface and rounds the join by K/4.
	const FSurface Level = MakeSurface(1.0, Below.Height);
	const FComposite AtCrossing =
		Composite(Below, Level, 1.0, 1.0, 1.0, EHeightMode::Merge, Settings);
	const FComposite OverAtCrossing =
		Composite(Below, Level, 1.0, 1.0, 1.0, EHeightMode::Over, Settings);

	TestEqual(TEXT("OVER flattens the intersection to the shared height"),
		OverAtCrossing.Height, Below.Height, Tolerance);
	TestTrue(TEXT("BLEND preserves relief at the intersection"),
		AtCrossing.Height > Below.Height + 1.0e-4);
	TestEqual(TEXT("The intersection ridge is the smooth maximum's fillet"),
		AtCrossing.Height - Below.Height, Settings.Softness * 0.25, 1.0e-4);

	// Away from the crossing the fillet vanishes and the merge is exactly max().
	for (const double IncomingHeight : { 0.05, 0.95 })
	{
		const FComposite R = Composite(
			Below, MakeSurface(1.0, IncomingHeight), 1.0, 1.0, 1.0, EHeightMode::Merge, Settings);
		TestEqual(
			FString::Printf(TEXT("BLEND is max() away from the crossing at %.2f"), IncomingHeight),
			R.Height,
			FMath::Max(Below.Height, IncomingHeight),
			1.0e-4);
	}

	// The merge never digs into what is beneath it -- that is what a maximum means, and it is the
	// property that separates BLEND from OVER for a layer sitting lower than its base.
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double IncomingHeight = 0.05 * static_cast<double>(Step);
		const FComposite R = Composite(
			Below, MakeSurface(1.0, IncomingHeight), 1.0, 1.0, 1.0, EHeightMode::Merge, Settings);
		TestTrue(TEXT("BLEND never lowers the surface below"), R.Height >= Below.Height - 1.0e-9);
	}
	const FComposite Lower = Composite(
		Below, MakeSurface(1.0, 0.2), 1.0, 1.0, 1.0, EHeightMode::Over, Settings);
	TestTrue(TEXT("OVER does lower the surface below"), Lower.Height < Below.Height - 1.0e-4);

	// Coverage fades the merge back to the base rather than to the average, so a masked-out
	// texel is untouched and a fully covered one is the full smooth maximum.
	const FComposite NoMask = Composite(
		Below, MakeSurface(1.0, 0.9), 1.0, 0.0, 1.0, EHeightMode::Merge, Settings);
	TestEqual(TEXT("A masked-out texel keeps the height below"),
		NoMask.Height, Below.Height, Tolerance);

	double Previous = Below.Height - 1.0;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const double Mask = 0.1 * static_cast<double>(Step);
		const FComposite R = Composite(
			Below, MakeSurface(1.0, 0.9), 1.0, Mask, 1.0, EHeightMode::Merge, Settings);
		TestTrue(TEXT("The merge is monotone in coverage"), R.Height >= Previous - Tolerance);
		Previous = R.Height;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatHeightMaskBlendTest,
	"Mixtormat.Composition.HeightMaskBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatHeightMaskBlendTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	// The other feature, armed explicitly: here the height *is* allowed to decide coverage, and
	// one winner drives every channel together. That is the whole point of arming it.
	const FBlendSettings Settings;
	const FSurface Below = MakeSurface(0.0, 0.5);

	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double IncomingHeight = 0.25 + 0.025 * static_cast<double>(Step);
		const FComposite R = Composite(
			Below, MakeSurface(1.0, IncomingHeight), 1.0, 1.0, 1.0,
			EHeightMode::MaskBlend, Settings);

		// Incoming channels are all 1 and the surface below all 0, so each composited channel is
		// numerically its own weight. A channel carrying a different mask shows up right here.
		TestEqual(TEXT("Base colour uses the winner"), R.BaseColor, R.Alpha, Tolerance);
		TestEqual(TEXT("Roughness uses the winner"), R.Roughness, R.Alpha, Tolerance);
		TestEqual(TEXT("AO uses the winner"), R.AO, R.Alpha, Tolerance);
		TestEqual(TEXT("Metallic uses the winner"), R.Metallic, R.Alpha, Tolerance);
		TestEqual(TEXT("F0 uses the winner"), R.F0, R.Alpha, Tolerance);
		TestEqual(TEXT("Normal uses the winner"), R.NormalWeight, R.Alpha, Tolerance);

		const double Fillet = R.Height - FMath::Lerp(Below.Height, IncomingHeight, R.Alpha);
		TestTrue(TEXT("Height is the same lerp plus a non-negative fillet"), Fillet >= -1.0e-9);
	}

	// A zero mask is gone whatever its height. Under the old form the mask was added into the
	// height difference, so a tall enough layer punched through where nothing was painted.
	for (const double IncomingHeight : { 0.0, 0.5, 1.0 })
	{
		const FComposite Masked = Composite(
			Below, MakeSurface(1.0, IncomingHeight), 1.0, 0.0, 1.0,
			EHeightMode::MaskBlend, Settings);
		TestEqual(
			FString::Printf(TEXT("Zero mask composites nothing at height %.2f"), IncomingHeight),
			Masked.Alpha, 0.0, Tolerance);
	}

	// A taller layer under a full mask wins outright, a shorter one loses: this is what makes a
	// layer settle into the recesses of what is beneath it instead of tiling over them.
	TestEqual(TEXT("A taller layer wins the contest"),
		Composite(Below, MakeSurface(1.0, 0.8), 1.0, 1.0, 1.0, EHeightMode::MaskBlend, Settings).Alpha,
		1.0, 1.0e-4);
	TestEqual(TEXT("A shorter layer loses the contest"),
		Composite(Below, MakeSurface(1.0, 0.2), 1.0, 1.0, 1.0, EHeightMode::MaskBlend, Settings).Alpha,
		0.0, 1.0e-4);

	// Coverage stays monotone across the mask's whole range. An early formulation sank the
	// incoming surface by a full height range, which left the mask inert until its last few
	// percent -- a soft mask edge simply vanished.
	double Previous = -1.0;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const FComposite R = Composite(
			Below, MakeSurface(1.0, Below.Height), 1.0, 0.1 * static_cast<double>(Step), 1.0,
			EHeightMode::MaskBlend, Settings);
		TestTrue(TEXT("Coverage is monotone in the mask"), R.Alpha >= Previous - Tolerance);
		Previous = R.Alpha;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatHeightMaskBlendAmountZeroTest,
	"Mixtormat.Composition.HeightMaskBlendAmountZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatHeightMaskBlendAmountZeroTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	// At Blend Strength 0 the armed feature must degrade to ordinary OVER, not to an unrelated
	// threshold result. The old form multiplied the mask by this value, so 0 erased the mask from
	// the comparison and left a bare height test: the layer appeared wherever it happened to be
	// taller, ignoring where it was painted. Any formulation that folds the mask into the height
	// difference additively fails here.
	FBlendSettings Off;
	Off.Contest = 0.0;
	Off.Amount = 0.0;

	const FBlendSettings OverSettings;
	int32 Mismatches = 0;
	for (const double BaseHeight : { 0.1, 0.5, 0.9 })
	{
		for (const double IncomingHeight : { 0.2, 0.5, 0.85 })
		{
			for (const double Mask : { 0.0, 0.25, 0.5, 1.0 })
			{
				for (const double Opacity : { 0.5, 1.0 })
				{
					const FSurface Below = MakeSurface(0.0, BaseHeight);
					const FSurface Above = MakeSurface(1.0, IncomingHeight);
					const FComposite Blend = Composite(
						Below, Above, Opacity, Mask, 1.0, EHeightMode::MaskBlend, Off);
					const FComposite Over = Composite(
						Below, Above, Opacity, Mask, 1.0, EHeightMode::Over, OverSettings);
					Mismatches += FMath::IsNearlyEqual(Blend.Alpha, Over.Alpha, 1.0e-9) ? 0 : 1;
					Mismatches += FMath::IsNearlyEqual(Blend.Height, Over.Height, 1.0e-9) ? 0 : 1;
				}
			}
		}
	}
	TestEqual(TEXT("Blend Strength 0 is exactly OVER"), Mismatches, 0);

	// And it departs continuously rather than snapping, so the bottom of the slider is a usable
	// range instead of a cliff.
	const FSurface Below = MakeSurface(0.0, 0.5);
	const FSurface Above = MakeSurface(1.0, 0.65);
	const FComposite Over =
		Composite(Below, Above, 1.0, 0.6, 1.0, EHeightMode::Over, OverSettings);
	double PreviousGap = 0.0;
	for (int32 Step = 1; Step <= 4; ++Step)
	{
		FBlendSettings Partial;
		Partial.Contest = 0.25 * static_cast<double>(Step);
		Partial.Amount = Partial.Contest;
		const FComposite Blend =
			Composite(Below, Above, 1.0, 0.6, 1.0, EHeightMode::MaskBlend, Partial);
		const double Gap = FMath::Abs(Blend.Alpha - Over.Alpha);
		TestTrue(TEXT("Blend Strength departs from OVER monotonically"),
			Gap >= PreviousGap - 1.0e-9);
		PreviousGap = Gap;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatNormalStrengthTest,
	"Mixtormat.Relief.NormalStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatNormalStrengthTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	const FVector2d AuthoredXY(0.30, 0.0);
	const FVector3d Authored = DecodeNormal(AuthoredXY, 1.0);
	const double AuthoredSlope = Slope(Authored);

	// 0 is flat. Not "nearly flat" -- the control's bottom end has to be a usable neutral.
	const FVector3d Flat = DecodeNormal(AuthoredXY, 0.0);
	TestEqual(TEXT("Normal Strength 0 gives a flat normal"), Slope(Flat), 0.0, Tolerance);
	TestEqual(TEXT("Normal Strength 0 points straight up"), Flat.Z, 1.0, 1.0e-6);

	// 1 reproduces the authored map exactly.
	TestEqual(TEXT("Normal Strength 1 reproduces the authored normal X"),
		Authored.X, AuthoredXY.X, 1.0e-9);
	TestEqual(TEXT("Normal Strength 1 reproduces the authored normal Y"),
		Authored.Y, AuthoredXY.Y, 1.0e-9);

	// 2 doubles the tangent slope. This is the whole point of decoding before scaling: the old
	// form gained XY before rebuilding Z, so above 1 it drove the pair outside the unit circle
	// and pinned Z at zero -- it laid the normal on the horizon rather than steepening it.
	for (const double Strength : { 0.5, 2.0, 3.0, 4.0 })
	{
		TestEqual(
			FString::Printf(TEXT("Normal Strength %.1f scales the tangent slope"), Strength),
			Slope(DecodeNormal(AuthoredXY, Strength)),
			AuthoredSlope * Strength,
			1.0e-9);
		TestTrue(
			FString::Printf(TEXT("Normal Strength %.1f keeps Z positive"), Strength),
			DecodeNormal(AuthoredXY, Strength).Z > 1.0e-3);
	}

	// Measured against the form it replaces rather than described. At 1 the two agree exactly, so
	// nothing already authored moves.
	TestEqual(TEXT("Strength 1 is unchanged by this fix"),
		Slope(DecodeNormal(AuthoredXY, 1.0)),
		Slope(LegacyDecodeNormal(AuthoredXY, 1.0)),
		1.0e-9);

	// A steep authored normal is where the old decode fails outright: 0.8 gained by 2 leaves the
	// radicand negative, saturate() pins Z at 0 and the slope runs away.
	const FVector2d SteepXY(0.8, 0.0);
	TestTrue(TEXT("The old decode drives a steep normal to the horizon"),
		LegacyDecodeNormal(SteepXY, 2.0).Z < 1.0e-3);
	TestTrue(TEXT("The new decode keeps a steep normal off the horizon"),
		DecodeNormal(SteepXY, 2.0).Z > 0.2);
	TestEqual(TEXT("The new decode still doubles the steep normal's slope"),
		Slope(DecodeNormal(SteepXY, 2.0)), Slope(DecodeNormal(SteepXY, 1.0)) * 2.0, 1.0e-9);

	// The clamp is the only bound on the control, and it is generous rather than restrictive.
	TestEqual(TEXT("Normal Strength clamps negatives to flat"),
		MixtormatRelief::AuthoredNormalScale(-2.0f), 0.0f);
	TestEqual(TEXT("Normal Strength passes ordinary values through"),
		MixtormatRelief::AuthoredNormalScale(2.5f), 2.5f);
	TestEqual(TEXT("Normal Strength clamps the top"),
		MixtormatRelief::AuthoredNormalScale(99.0f), MixtormatRelief::MaxNormalStrength);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatNormalStrengthIndependenceTest,
	"Mixtormat.Relief.NormalStrengthIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatNormalStrengthIndependenceTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	// Three controls, each owning exactly one thing. Asserted directly rather than inferred.
	const FVector2d AuthoredXY(0.25, 0.0);

	// Height Booster reaches the authored normal through nothing at all.
	const double Reference =
		Slope(DecodeNormal(AuthoredXY, MixtormatRelief::AuthoredNormalScale(1.0f)));
	for (const float Boost : { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f })
	{
		TestEqual(
			FString::Printf(TEXT("Height Booster %.1f does not touch authored strength"), Boost),
			Reference * MixtormatRelief::SourceNormalScale(Boost),
			Reference,
			Tolerance);
	}

	// ... but it does still reach the height, which is the half of the contract that stops this
	// being a global flatten. Covered in depth by Mixtormat.Relief.StructuralEffectHeightDeltas.
	TestEqual(TEXT("Height Booster still scales the height"),
		MixtormatRelief::HeightScale(4.0f), 4.0f);

	// Normal Strength and Normal Influence are not the same control. Strength steepens the map
	// and can exceed 1; Influence fades the layer's contribution and cannot.
	TestTrue(TEXT("Normal Strength above 1 steepens"),
		Slope(DecodeNormal(AuthoredXY, 2.0)) > Slope(DecodeNormal(AuthoredXY, 1.0)));
	TestTrue(TEXT("Normal Influence below 1 attenuates"),
		Slope(WeightDetail(DecodeNormal(AuthoredXY, 1.0), 0.5))
			< Slope(DecodeNormal(AuthoredXY, 1.0)));
	TestEqual(TEXT("Normal Influence 0 mutes the layer normal"),
		Slope(WeightDetail(DecodeNormal(AuthoredXY, 2.0), 0.0)), 0.0, Tolerance);
	TestEqual(TEXT("Normal Influence 1 passes the strengthened normal through"),
		Slope(WeightDetail(DecodeNormal(AuthoredXY, 2.0), 1.0)),
		Slope(DecodeNormal(AuthoredXY, 2.0)),
		1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCompositionNormalsTest,
	"Mixtormat.Composition.Normals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatCompositionNormalsTest::RunTest(const FString&)
{
	using namespace MixtormatCompositionBlendTests;

	// A surface below that is not flat, so "reorient onto it" and "replace it" are distinguishable.
	const FVector3d Below = FVector3d(0.2, -0.1, 1.0).GetSafeNormal();
	const FVector2d AuthoredXY(0.3, 0.15);
	const FVector3d Authored = DecodeNormal(AuthoredXY, 1.0);

	// OVER at full coverage is the incoming normal, exactly. Anything else means the override
	// path is leaking the surface below through.
	const FVector3d Over = FMath::Lerp(Below, Authored, 1.0).GetSafeNormal();
	TestEqual(TEXT("OVER at full coverage gives the incoming normal X"), Over.X, Authored.X, 1.0e-9);
	TestEqual(TEXT("OVER at full coverage gives the incoming normal Y"), Over.Y, Authored.Y, 1.0e-9);
	TestEqual(TEXT("OVER at full coverage gives the incoming normal Z"), Over.Z, Authored.Z, 1.0e-9);

	// OVER at zero coverage leaves the surface below untouched.
	TestEqual(TEXT("OVER at zero coverage keeps the normal below"),
		FMath::Lerp(Below, Authored, 0.0).GetSafeNormal().X, Below.X, 1.0e-9);

	// BLEND reorients rather than replaces, and the authored detail survives the reorientation --
	// that is what RNM is for, and a blend that flattened it would be indistinguishable from a
	// muted Normal Influence.
	const FVector3d Blend = BlendReorientedNormals(Below, Authored);
	TestTrue(TEXT("BLEND RNM preserves authored normal detail"),
		Slope(Blend) > Slope(Below) * 0.5);
	TestTrue(TEXT("BLEND RNM is not simply the incoming normal"),
		!FMath::IsNearlyEqual(Blend.X, Authored.X, 1.0e-4)
		|| !FMath::IsNearlyEqual(Blend.Y, Authored.Y, 1.0e-4));

	// A flat detail normal leaves the surface below exactly as it was, which is the identity RNM
	// has to satisfy or every layer quietly perturbs the one under it.
	const FVector3d Identity = BlendReorientedNormals(Below, FVector3d(0.0, 0.0, 1.0));
	TestEqual(TEXT("RNM with a flat detail is the identity X"), Identity.X, Below.X, 1.0e-6);
	TestEqual(TEXT("RNM with a flat detail is the identity Y"), Identity.Y, Below.Y, 1.0e-6);

	// Normal Strength carries through the blend: a stronger authored map produces more relief
	// after reorientation, not less. Under the old decode a strong map collapsed at this point.
	TestTrue(TEXT("Normal Strength survives the RNM blend"),
		Slope(BlendReorientedNormals(Below, DecodeNormal(AuthoredXY, 2.0)))
			> Slope(BlendReorientedNormals(Below, DecodeNormal(AuthoredXY, 1.0))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
