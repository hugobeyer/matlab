// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Style/MixtormatTheme.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

// Cover for the generic surface painter's compositing half.
//
// The painter's drawing half is one MakeBox or one MakeGradient and has nothing to get wrong once
// the colours are right, so it is not tested here -- there is no window to paint into without a
// renderer, and asserting on draw-element internals would test Slate rather than Mixtormat.
//
// What IS tested is CompositeSurface, which is where every interesting decision lives:
//
//   Ramp positions   The defining ramp's authored positions must survive into the samples. The
//                    fill's shade pass puts its midpoint at 63%, and a painter that resampled onto
//                    an even grid would quietly move it -- a difference no assertion on colour
//                    alone would catch, which is why the positions are checked directly.
//
//   Opacity          The result is opaque. Every blend preserves the backdrop's alpha, so carrying
//                    it through would repaint the base's translucency on top of a composite that
//                    already accounted for it.
//
//   State            A modifier must move the numbers, not rebuild the recipe, and a source or
//                    blend override must actually replace what the layer authored.
//
//   Flat vs ramped   One colour means the painter takes the box path; anything else takes the
//                    gradient path. Getting this backwards is a silent perf regression.
//
// Deliberately free of Slate draw calls, RHI and production widgets, for the same reason the
// existing primitive tests are.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatSurfacePainterTests
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

	// A palette with just the roles these tests composite over. Distinct values so a wrong role is a
	// wrong number rather than a coincidence.
	Mixtormat::FMixtormatResolvedPalette MakePalette()
	{
		Mixtormat::FMixtormatResolvedPalette Palette;
		Palette.Set(Mixtormat::EMixtormatColorRole::Ground, FLinearColor(0.20f, 0.22f, 0.24f, 1.0f));
		Palette.Set(Mixtormat::EMixtormatColorRole::Accent, FLinearColor(0.80f, 0.40f, 0.10f, 1.0f));
		Palette.Set(Mixtormat::EMixtormatColorRole::Shade, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		Palette.Set(Mixtormat::EMixtormatColorRole::Hairline, FLinearColor(0.40f, 0.50f, 0.55f, 1.0f));
		return Palette;
	}

	Mixtormat::FMixtormatSurfaceRecipe MakeFlatRecipe()
	{
		Mixtormat::FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = Mixtormat::MakeColorRef(Mixtormat::EMixtormatColorRole::Ground);
		return Recipe;
	}

	Mixtormat::FMixtormatPaintLayer MakeShadeLayer(
		const Mixtormat::FMixtormatRamp& Ramp,
		const MixtormatCompositing::EMixtormatBlendMode Blend)
	{
		Mixtormat::FMixtormatPaintLayer Layer;
		Layer.Source = Mixtormat::MakeColorRef(Mixtormat::EMixtormatColorRole::Shade);
		Layer.Blend = Blend;
		Layer.OpacityRamp = Ramp;
		return Layer;
	}

	Mixtormat::FMixtormatRamp MakeRamp(const Mixtormat::EMixtormatAxis Axis)
	{
		Mixtormat::FMixtormatRamp Ramp;
		Ramp.Axis = Axis;
		Ramp.Points.Add({ 0.0f, 0.6f });
		Ramp.Points.Add({ 1.0f, 0.2f });
		return Ramp;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceRampEvaluationTest,
	"Mixtormat.Style.SurfacePainter.EvaluateRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceRampEvaluationTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfacePainterTests;
	using namespace Mixtormat;

	Mixtormat::FMixtormatRamp Ramp = MakeRamp(EMixtormatAxis::Vertical);

	// Piecewise linear through the authored points.
	TestTrue(TEXT("At the first stop"), Near(EvaluateRamp(Ramp, 0.0f), 0.6f));
	TestTrue(TEXT("At the last stop"), Near(EvaluateRamp(Ramp, 1.0f), 0.2f));
	TestTrue(TEXT("Midpoint interpolates"), Near(EvaluateRamp(Ramp, 0.5f), 0.4f));

	// Clamped outside the authored domain, not extrapolated. A ramp extrapolating past its ends
	// would let a layer grow stronger toward a surface edge than the theme ever asked for.
	TestTrue(TEXT("Below the first stop clamps"), Near(EvaluateRamp(Ramp, -0.5f), 0.6f));
	TestTrue(TEXT("Above the last stop clamps"), Near(EvaluateRamp(Ramp, 1.5f), 0.2f));

	// An empty ramp is fully on. Returning 0 would make a layer that forgot its ramp invisible,
	// which is the failure mode that ships instead of the one that gets noticed.
	Mixtormat::FMixtormatRamp Empty;
	TestTrue(TEXT("Empty ramp is fully opaque"), Near(EvaluateRamp(Empty, 0.5f), 1.0f));

	Mixtormat::FMixtormatRamp Single;
	Single.Axis = EMixtormatAxis::Vertical;
	Single.Points.Add({ 0.0f, 0.35f });
	TestTrue(TEXT("Single-point ramp is that point's value"), Near(EvaluateRamp(Single, 0.9f), 0.35f));

	// A midpoint at 63% must stay at 63%. This is the fill's shade pass, and a painter that
	// resampled onto an even grid would slide it to 50% -- still a plausible-looking gradient, which
	// is why it is pinned by position rather than left to a colour comparison to catch.
	Mixtormat::FMixtormatRamp Midpoint;
	Midpoint.Axis = EMixtormatAxis::Horizontal;
	Midpoint.Points.Add({ 0.0f, 0.25f });
	Midpoint.Points.Add({ 0.63f, 0.0f });
	Midpoint.Points.Add({ 1.0f, 0.02f });
	TestTrue(TEXT("Midpoint stop keeps its value"), Near(EvaluateRamp(Midpoint, 0.63f), 0.0f));
	TestTrue(TEXT("Value before the midpoint"), Near(EvaluateRamp(Midpoint, 0.315f), 0.125f));

	// Two points at the same position are a step, not a division by zero.
	Mixtormat::FMixtormatRamp Stepped;
	Stepped.Axis = EMixtormatAxis::Vertical;
	Stepped.Points.Add({ 0.5f, 0.1f });
	Stepped.Points.Add({ 0.5f, 0.9f });
	TestTrue(TEXT("Coincident stops resolve to the later value"), Near(EvaluateRamp(Stepped, 0.5f), 0.9f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceBorderPositionTest,
	"Mixtormat.Style.SurfacePainter.BorderEdgePosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceBorderPositionTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfacePainterTests;
	using namespace Mixtormat;

	// The well's outline: a vertical fade, which is what makes its top edge brighter than its
	// bottom. Top must sample the ramp's start and bottom its end or the fade runs the wrong way.
	TestTrue(TEXT("Vertical: top is the ramp start"),
		Near(BorderEdgePosition(EMixtormatAxis::Vertical, EMixtormatBorderEdge::Top), 0.0f));
	TestTrue(TEXT("Vertical: bottom is the ramp end"),
		Near(BorderEdgePosition(EMixtormatAxis::Vertical, EMixtormatBorderEdge::Bottom), 1.0f));

	// A side edge spans the whole height, so there is no single position along a vertical ramp that
	// describes it. Its centre is the honest answer.
	TestTrue(TEXT("Vertical: left spans the height"),
		Near(BorderEdgePosition(EMixtormatAxis::Vertical, EMixtormatBorderEdge::Left), 0.5f));
	TestTrue(TEXT("Vertical: right spans the height"),
		Near(BorderEdgePosition(EMixtormatAxis::Vertical, EMixtormatBorderEdge::Right), 0.5f));

	// Mirrored for a horizontal surface, which is the fill's shade axis.
	TestTrue(TEXT("Horizontal: left is the ramp start"),
		Near(BorderEdgePosition(EMixtormatAxis::Horizontal, EMixtormatBorderEdge::Left), 0.0f));
	TestTrue(TEXT("Horizontal: right is the ramp end"),
		Near(BorderEdgePosition(EMixtormatAxis::Horizontal, EMixtormatBorderEdge::Right), 1.0f));
	TestTrue(TEXT("Horizontal: top spans the width"),
		Near(BorderEdgePosition(EMixtormatAxis::Horizontal, EMixtormatBorderEdge::Top), 0.5f));
	TestTrue(TEXT("Horizontal: bottom spans the width"),
		Near(BorderEdgePosition(EMixtormatAxis::Horizontal, EMixtormatBorderEdge::Bottom), 0.5f));

	// A flat surface has nothing to vary along, so the position is arbitrary but must be stable --
	// left and right have to agree or a flat border would draw two different colours.
	TestTrue(TEXT("Flat: every edge agrees"),
		Near(BorderEdgePosition(EMixtormatAxis::None, EMixtormatBorderEdge::Left),
			BorderEdgePosition(EMixtormatAxis::None, EMixtormatBorderEdge::Right)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceCompositeFlatTest,
	"Mixtormat.Style.SurfacePainter.CompositeSurface.Flat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceCompositeFlatTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfacePainterTests;
	using namespace Mixtormat;

	const FMixtormatResolvedPalette Palette = MakePalette();
	const FMixtormatStateModifier State;
	FMixtormatSurfaceSamples Samples;

	// A recipe with no layers is its base, and one colour is the signal that the painter may take
	// the box path instead of the gradient path.
	const int32 Count = CompositeSurface(MakeFlatRecipe(), Palette, State, Samples);
	TestEqual(TEXT("A flat recipe yields one stop"), Count, 1);
	TestTrue(TEXT("A flat recipe has no axis"), Samples.Axis == EMixtormatAxis::None);
	TestTrue(TEXT("A flat recipe is its base colour"),
		NearColor(Samples.Colors[0], Palette.Get(EMixtormatColorRole::Ground)));

	// A disabled layer must contribute nothing at all, not "nothing much".
	FMixtormatSurfaceRecipe Recipe = MakeFlatRecipe();
	Mixtormat::FMixtormatPaintLayer Disabled = MakeShadeLayer(MakeRamp(EMixtormatAxis::Vertical),
		MixtormatCompositing::EMixtormatBlendMode::Multiply);
	Disabled.bEnabled = false;
	Recipe.Layers.Add(Disabled);
	CompositeSurface(Recipe, Palette, State, Samples);
	TestEqual(TEXT("A disabled layer is skipped"), Samples.Colors.Num(), 1);
	TestTrue(TEXT("A disabled layer changes nothing"),
		NearColor(Samples.Colors[0], Palette.Get(EMixtormatColorRole::Ground)));

	// One stop is not a ramp, so a single-point ramp must not promote a flat surface to a gradient.
	Mixtormat::FMixtormatSurfaceRecipe SinglePoint = MakeFlatRecipe();
	Mixtormat::FMixtormatRamp OneStop;
	OneStop.Axis = EMixtormatAxis::Vertical;
	OneStop.Points.Add({ 0.0f, 0.5f });
	SinglePoint.Layers.Add(MakeShadeLayer(OneStop, MixtormatCompositing::EMixtormatBlendMode::Multiply));
	CompositeSurface(SinglePoint, Palette, State, Samples);
	TestEqual(TEXT("A single-stop ramp stays flat"), Samples.Colors.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceCompositeRampTest,
	"Mixtormat.Style.SurfacePainter.CompositeSurface.Ramped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceCompositeRampTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfacePainterTests;
	using namespace Mixtormat;

	const FMixtormatResolvedPalette Palette = MakePalette();
	const FMixtormatStateModifier State;
	FMixtormatSurfaceSamples Samples;

	const FLinearColor Ground = Palette.Get(EMixtormatColorRole::Ground);

	// The well: Ground under a black Multiply falloff from 0.6 to 0.2. Black at alpha a under
	// multiply is ground * (1 - a), which is the exact arithmetic the prototype writes as
	// `mix-blend-mode: multiply`.
	FMixtormatSurfaceRecipe Recipe = MakeFlatRecipe();
	Recipe.Layers.Add(MakeShadeLayer(MakeRamp(EMixtormatAxis::Vertical),
		MixtormatCompositing::EMixtormatBlendMode::Multiply));

	const int32 Count = CompositeSurface(Recipe, Palette, State, Samples);
	TestEqual(TEXT("A two-stop ramp yields two samples"), Count, 2);
	TestTrue(TEXT("A ramped surface reports its axis"), Samples.Axis == EMixtormatAxis::Vertical);
	TestTrue(TEXT("Positions span the axis"),
		Near(Samples.Positions[0], 0.0f) && Near(Samples.Positions[1], 1.0f));
	TestTrue(TEXT("Multiply darkens by the ramp at the top"),
		Near(Samples.Colors[0].R, Ground.R * (1.0f - 0.6f)));
	TestTrue(TEXT("Multiply darkens by the ramp at the bottom"),
		Near(Samples.Colors[1].R, Ground.R * (1.0f - 0.2f)));

	// The result is opaque regardless of the base's alpha. A translucent base that leaked through
	// would be composited a second time by Slate, against whatever the widget put behind.
	TestTrue(TEXT("The result is opaque"), Near(Samples.Colors[0].A, 1.0f));

	// Authored positions survive into the samples, including one that is not on an even grid.
	FMixtormatSurfaceRecipe Midpointed = MakeFlatRecipe();
	Midpointed.Layers.Add(MakeShadeLayer([]
	{
		Mixtormat::FMixtormatRamp Ramp;
		Ramp.Axis = EMixtormatAxis::Horizontal;
		Ramp.Points.Add({ 0.0f, 1.0f });
		Ramp.Points.Add({ 0.63f, 0.0f });
		Ramp.Points.Add({ 1.0f, 1.0f });
		return Ramp;
	}(), MixtormatCompositing::EMixtormatBlendMode::Normal));
	CompositeSurface(Midpointed, Palette, State, Samples);
	TestEqual(TEXT("A three-stop ramp yields three samples"), Samples.Positions.Num(), 3);
	TestTrue(TEXT("The authored midpoint position is preserved"), Near(Samples.Positions[1], 0.63f));

	// A ramp longer than the cap is subsampled, and the subsample keeps the shape rather than
	// truncating to the first few stops.
	FMixtormatSurfaceRecipe Long = MakeFlatRecipe();
	Mixtormat::FMixtormatRamp Dense;
	Dense.Axis = EMixtormatAxis::Vertical;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Dense.Points.Add({ static_cast<float>(Index) / 39.0f, 1.0f - static_cast<float>(Index) / 39.0f });
	}
	Long.Layers.Add(MakeShadeLayer(Dense, MixtormatCompositing::EMixtormatBlendMode::Normal));
	CompositeSurface(Long, Palette, State, Samples);
	TestEqual(TEXT("A long ramp is capped"),
		Samples.Positions.Num(), FMixtormatSurfaceSamples::MaxStops);
	TestTrue(TEXT("A capped ramp still spans the whole axis"),
		Near(Samples.Positions[0], 0.0f) && Near(Samples.Positions.Last(), 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceStateModifierTest,
	"Mixtormat.Style.SurfacePainter.StateModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceStateModifierTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfacePainterTests;
	using namespace Mixtormat;

	const FMixtormatResolvedPalette Palette = MakePalette();
	const FLinearColor Ground = Palette.Get(EMixtormatColorRole::Ground);
	FMixtormatSurfaceSamples Samples;

	FMixtormatSurfaceRecipe Recipe = MakeFlatRecipe();
	Recipe.Layers.Add(MakeShadeLayer(MakeRamp(EMixtormatAxis::Vertical),
		MixtormatCompositing::EMixtormatBlendMode::Multiply));

	// Rest.
	CompositeSurface(Recipe, Palette, FMixtormatStateModifier(), Samples);
	const float Rest = Samples.Colors[0].R;
	TestTrue(TEXT("Rest darkens the base"), Rest < Ground.R);

	// Hover is the same recipe with a stronger number -- never a second recipe. If this did not
	// move, every hover in the tool would silently be inert.
	FMixtormatStateModifier Hovered;
	Hovered.Strength = 0.5f;
	CompositeSurface(Recipe, Palette, Hovered, Samples);
	TestTrue(TEXT("State strength changes the result"), Samples.Colors[0].R < Rest);

	// A source override replaces what the layer authored rather than nudging it.
	FMixtormatStateModifier Overridden;
	FMixtormatColorRef AccentRef = MakeColorRef(EMixtormatColorRole::Accent);
	AccentRef.Opacity = 1.0f;
	Overridden.SourceOverride = AccentRef;
	CompositeSurface(Recipe, Palette, Overridden, Samples);
	// The layer is still Multiply, but against an opaque accent it lightens nothing and darkens by
	// the ramp's weight only -- the point is that it is no longer black.
	TestTrue(TEXT("A source override replaces the layer's colour"),
		Samples.Colors[0].R > Ground.R * (1.0f - 0.6f));

	// A blend override replaces the compositing mode, not just the colour.
	FMixtormatSurfaceRecipe Tinted = MakeFlatRecipe();
	Tinted.Layers.Add(MakeShadeLayer(MakeRamp(EMixtormatAxis::Vertical),
		MixtormatCompositing::EMixtormatBlendMode::Additive));
	FMixtormatStateModifier Normalised;
	Normalised.BlendOverride = MixtormatCompositing::EMixtormatBlendMode::Normal;
	CompositeSurface(Tinted, Palette, Normalised, Samples);
	TestTrue(TEXT("A blend override changes the compositing"),
		!NearColor(Samples.Colors[0], FLinearColor(Ground.R, Ground.G, Ground.B, 1.0f)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS