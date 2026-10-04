// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"

// Cover for the recipe builders: theme -> drawable recipe.
//
// These are the layer where an authored number becomes a colour reference, a blend and a ramp, so
// they are where a typo stops being a number and starts being a visual. Everything here is pure and
// runs without Slate, RHI or a widget.
//
// The shared thread through the file: a builder must carry the *design's* numbers, not plausible
// ones. Several assertions are therefore about exact values (0.64 -> 0.13, 400-weight Regular)
// because a builder that silently substituted a reasonable-looking number would pass every
// structural check and still render wrong.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatRecipesTests
{
	constexpr float Tolerance = 1.0e-4f;

	bool Near(const float A, const float B)
	{
		return FMath::IsNearlyEqual(A, B, Tolerance);
	}

	// Returns BY VALUE. A reference here would dangle: MakeDefaultTheme produces a temporary, and
	// every caller below binds it to a local const reference, which extends the lifetime. Binding
	// one reference to another temporary would not.
	Mixtormat::FMixtormatTheme DefaultTheme()
	{
		return Mixtormat::MakeDefaultTheme();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRampBuilderTest,
	"Mixtormat.Style.Recipes.Ramps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatRampBuilderTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatRecipesTests;
	using namespace Mixtormat;

	// The well's recess: 0.64 -> 0.13, authored exactly.
	const FMixtormatRamp Shade = MakeLinearRamp(EMixtormatAxis::Vertical, 0.64f, 0.13f, 2);
	TestEqual(TEXT("A two-sample ramp has two points"), Shade.Points.Num(), 2);
	TestTrue(TEXT("Ramp spans the axis"),
		Near(Shade.Points[0].Position, 0.0f) && Near(Shade.Points[1].Position, 1.0f));
	TestTrue(TEXT("Ramp carries the authored start"), Near(Shade.Points[0].Value, 0.64f));
	TestTrue(TEXT("Ramp carries the authored end"), Near(Shade.Points[1].Value, 0.13f));

	// A ramp needs two points to be a ramp. One would be a flat fill the painter rejects, so the
	// builder raises it rather than emitting something undrawable.
	const FMixtormatRamp Single = MakeLinearRamp(EMixtormatAxis::Vertical, 0.5f, 0.2f, 1);
	TestEqual(TEXT("A one-sample ramp is raised to two"), Single.Points.Num(), 2);

	// The fill's shade pass keeps its midpoint where the prototype put it. An even two-point ramp
	// here would flatten the eased low point into a straight darkening.
	const FMixtormatRamp Mid = MakeMidpointRamp(EMixtormatAxis::Horizontal, 0.25f, 0.0f, 0.63f, 0.02f);
	TestEqual(TEXT("A midpoint ramp has three points"), Mid.Points.Num(), 3);
	TestTrue(TEXT("Midpoint keeps its authored position"), Near(Mid.Points[1].Position, 0.63f));
	TestTrue(TEXT("Midpoint carries its authored value"), Near(Mid.Points[1].Value, 0.0f));

	// Clamped rather than emitted: a midpoint outside 0..1 is a mis-authored theme value, and
	// ValidateTheme reports it, but the builder must not emit an unorderable ramp either.
	const FMixtormatRamp Clamped = MakeMidpointRamp(EMixtormatAxis::Horizontal, 1.0f, 0.0f, 1.4f, 0.0f);
	TestTrue(TEXT("An out-of-range midpoint is clamped"), Near(Clamped.Points[1].Position, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatWellRecipeTest,
	"Mixtormat.Style.Recipes.Well",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatWellRecipeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatRecipesTests;
	using namespace Mixtormat;

	const FMixtormatTheme& T = DefaultTheme();
	const FMixtormatSurfaceRecipe Rest = MakeWellRecipe(T, EMixtormatWellState::Rest);
	const FMixtormatSurfaceRecipe Hover = MakeWellRecipe(T, EMixtormatWellState::Hover);

	// Ground under a black Multiply recess. Base and one layer.
	TestTrue(TEXT("Base is the Ground role"), Rest.Base.Role == EMixtormatColorRole::Ground);
	TestEqual(TEXT("The well is one layer at rest"), Rest.Layers.Num(), 1);
	TestTrue(TEXT("The recess is a Shade source"), Rest.Layers[0].Source.Role == EMixtormatColorRole::Shade);
	TestTrue(TEXT("The recess multiplies"),
		Rest.Layers[0].Blend == MixtormatCompositing::EMixtormatBlendMode::Multiply);

	// The design's numbers, not plausible ones: --well-shade-top 0.64, --well-shade-bottom 0.13.
	TestTrue(TEXT("Recess carries the authored top"), Near(Rest.Layers[0].OpacityRamp.Points[0].Value, 0.64f));
	TestTrue(TEXT("Recess carries the authored bottom"), Near(Rest.Layers[0].OpacityRamp.Points[1].Value, 0.13f));

	// One outline, top and bottom edges, on a vertical ramp -- which is what makes the painter give
	// the top edge the ramp's start and the bottom its end.
	TestEqual(TEXT("The well has one border"), Rest.Borders.Num(), 1);
	TestTrue(TEXT("The outline is top and bottom only"),
		Rest.Borders[0].bTop && Rest.Borders[0].bBottom
		&& !Rest.Borders[0].bLeft && !Rest.Borders[0].bRight);
	TestTrue(TEXT("The outline ramps vertically"), Rest.Borders[0].OpacityRamp.Axis == EMixtormatAxis::Vertical);

	// The per-edge endpoint multiplied by the overall intensity. This product is the thing a flat
	// border colour used to throw away, so it is asserted as a product and not just as "differs".
	TestTrue(TEXT("Outline top is intensity x endpoint"),
		Near(Rest.Borders[0].OpacityRamp.Points[0].Value, T.Well.BorderOpacity * T.Well.BorderTopOpacity));
	TestTrue(TEXT("Outline bottom is intensity x endpoint"),
		Near(Rest.Borders[0].OpacityRamp.Points[1].Value, T.Well.BorderOpacity * T.Well.BorderBottomOpacity));

	// Rest and Hover must not be two copies of the surface (§16). They share the base and the
	// recess exactly; only the border's numbers and the hover lift differ.
	TestEqual(TEXT("Hover shares the layer count at the base"), Hover.Layers.Num(), Rest.Layers.Num() + 1);
	TestTrue(TEXT("Hover shares the recess"), Near(Hover.Layers[0].OpacityRamp.Points[0].Value, 0.64f));
	TestTrue(TEXT("Hover's extra layer is the additive lift"),
		Hover.Layers[1].Blend == MixtormatCompositing::EMixtormatBlendMode::Additive
		&& Near(Hover.Layers[1].Strength, T.Well.HoverLiftOpacity));
	TestTrue(TEXT("Hover outlines brighter"),
		Hover.Borders[0].OpacityRamp.Points[0].Value > Rest.Borders[0].OpacityRamp.Points[0].Value);

	// Flat: ground only. A control that is mid-edit needs the field itself to read, which means no
	// recess and no rim.
	const FMixtormatSurfaceRecipe Flat = MakeFlatWellRecipe(T);
	TestEqual(TEXT("A flat well has no layers"), Flat.Layers.Num(), 0);
	TestEqual(TEXT("A flat well has no border"), Flat.Borders.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatFillRecipeTest,
	"Mixtormat.Style.Recipes.Fill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatFillRecipeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatRecipesTests;
	using namespace Mixtormat;

	const FMixtormatTheme& T = DefaultTheme();
	const FMixtormatSurfaceRecipe Fill = MakeFillRecipe(T, EMixtormatFillState::Rest);

	// The well's own ground and recess, then the accent body, then the shade. The painter produces
	// an opaque surface, so a fill that omitted the first two would erase the trough beneath it.
	TestEqual(TEXT("The fill carries the well plus two passes"), Fill.Layers.Num(), 3);
	TestTrue(TEXT("Layer 0 is the recess"),
		Fill.Layers[0].Source.Role == EMixtormatColorRole::Shade
		&& Fill.Layers[0].Blend == MixtormatCompositing::EMixtormatBlendMode::Multiply);
	TestTrue(TEXT("Layer 1 is the accent body"),
		Fill.Layers[1].Source.Role == EMixtormatColorRole::Accent
		&& Fill.Layers[1].Blend == MixtormatCompositing::EMixtormatBlendMode::Additive);
	TestTrue(TEXT("Layer 2 is the shade pass"),
		Fill.Layers[2].Source.Role == EMixtormatColorRole::Shade
		&& Fill.Layers[2].Blend == MixtormatCompositing::EMixtormatBlendMode::Multiply);

	// The two passes run on different axes. This is the assertion that would catch someone
	// "simplifying" both to vertical, which is what made the fill read as lit from one edge.
	TestTrue(TEXT("The body ramps vertically"), Fill.Layers[1].OpacityRamp.Axis == EMixtormatAxis::Vertical);
	TestTrue(TEXT("The shade ramps horizontally"), Fill.Layers[2].OpacityRamp.Axis == EMixtormatAxis::Horizontal);
	TestEqual(TEXT("The shade keeps its midpoint"), Fill.Layers[2].OpacityRamp.Points.Num(), 3);
	TestTrue(TEXT("The shade midpoint is at 63%"),
		Near(Fill.Layers[2].OpacityRamp.Points[1].Position, T.Fill.ShadeMidPosition));

	// Saturation is per state, not per role: the prototype authors the accent at 0.7 here, 1.4 on
	// hover and 1.0 when active, and a shared role could not carry three values.
	TestTrue(TEXT("Rest saturation is the authored 0.7"), Near(Fill.Layers[1].Source.Saturation, T.Fill.Saturation));
	TestTrue(TEXT("Hover saturation differs from rest"),
		Near(MakeFillRecipe(T, EMixtormatFillState::Hover).Layers[1].Source.Saturation, T.Fill.HoverSaturation));
	TestTrue(TEXT("Active saturation differs from rest"),
		Near(MakeFillRecipe(T, EMixtormatFillState::Active).Layers[1].Source.Saturation, T.Fill.ActiveSaturation));

	// The states really are different numbers, not one ramp reused.
	TestTrue(TEXT("Hover raises the body top"),
		MakeFillRecipe(T, EMixtormatFillState::Hover).Layers[1].OpacityRamp.Points[0].Value
			> Fill.Layers[1].OpacityRamp.Points[0].Value);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatButtonRecipeTest,
	"Mixtormat.Style.Recipes.Button",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatButtonRecipeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatRecipesTests;
	using namespace Mixtormat;

	const FMixtormatTheme& T = DefaultTheme();
	const FMixtormatSurfaceRecipe Rest = MakeButtonRecipe(T, EMixtormatButtonState::Rest);

	// An Accent body over Ground, and an Additive hairline on top.
	TestEqual(TEXT("The button is one body layer"), Rest.Layers.Num(), 1);
	TestTrue(TEXT("The body is the accent"), Rest.Layers[0].Source.Role == EMixtormatColorRole::Accent);
	TestTrue(TEXT("The body uses the authored blend"),
		Rest.Layers[0].Blend == MixtormatCompositing::EMixtormatBlendMode::Normal);
	TestEqual(TEXT("The button has one hairline"), Rest.Borders.Num(), 1);
	TestTrue(TEXT("The hairline uses its own blend, independent of the body"),
		Rest.Borders[0].Blend == MixtormatCompositing::EMixtormatBlendMode::Additive
		&& Rest.Layers[0].Blend == MixtormatCompositing::EMixtormatBlendMode::Normal);

	// Top edge only: components.css draws the hairline as ::after with a border-top, and the
	// separator is drawn separately by the widget.
	TestTrue(TEXT("The hairline is the top edge"),
		Rest.Borders[0].bTop && !Rest.Borders[0].bBottom);

	// The hairline's opacity lives in the colour reference, and the ramp is flat -- the design's
	// falloff is along the body, not along the rim.
	TestTrue(TEXT("The hairline carries its authored opacity"),
		Near(Rest.Borders[0].Source.Opacity, T.Button.HairlineOpacity));
	TestTrue(TEXT("The hairline ramps uniformly"),
		Near(Rest.Borders[0].OpacityRamp.Points[0].Value, Rest.Borders[0].OpacityRamp.Points[1].Value));

	const FMixtormatSurfaceRecipe Selected = MakeButtonRecipe(T, EMixtormatButtonState::Selected);
	TestTrue(TEXT("Selected raises the body top"),
		Selected.Layers[0].OpacityRamp.Points[0].Value > Rest.Layers[0].OpacityRamp.Points[0].Value);
	TestTrue(TEXT("Selected brightens the hairline"),
		Selected.Borders[0].Source.Opacity > Rest.Borders[0].Source.Opacity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatThemeStoreTest,
	"Mixtormat.Style.Recipes.ThemeStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatThemeStoreTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatRecipesTests;

	// The store resolves on first use, so a caller can ask for colours during static init or from a
	// widget constructor without an explicit startup step.
	const FMixtormatResolvedStyle& Initial = FMixtormatThemeStore::GetResolved();
	TestTrue(TEXT("The resolved style has a palette"),
		Near(Initial.Palette.Get(EMixtormatColorRole::Ground).R,
			FMixtormatThemeStore::GetTheme().Palette.Ground.R));

	// A change must propagate: retint the ground and the resolved palette follows, which is the
	// whole reason the store exists.
	FMixtormatTheme Edited = FMixtormatThemeStore::GetTheme();
	Edited.Palette.Accent = FLinearColor(0.5f, 0.25f, 0.125f, 1.0f);
	FMixtormatThemeStore::SetTheme(MoveTemp(Edited));

	TestTrue(TEXT("A theme edit reaches the resolved palette"),
		Near(FMixtormatThemeStore::GetResolved().Palette.Get(EMixtormatColorRole::Accent).G, 0.25f));

	// A rebuild is requested once and consumed once. A flag that stayed set would rebuild every
	// widget on the next unrelated edit.
	FMixtormatThemeStore::Refresh();
	TestTrue(TEXT("A change requests a rebuild"), FMixtormatThemeStore::ConsumeRebuildRequest());
	TestFalse(TEXT("The rebuild request is consumed once"), FMixtormatThemeStore::ConsumeRebuildRequest());

	// Out-of-range input is clamped and reported rather than resolved as authored, so the resolved
	// style never disagrees with the number that survived.
	FMixtormatTheme Invalid = FMixtormatThemeStore::GetTheme();
	Invalid.Well.ShadeTop = 4.0f;
	FMixtormatThemeStore::SetTheme(MoveTemp(Invalid));

	TestTrue(TEXT("An out-of-range value is clamped"),
		Near(FMixtormatThemeStore::GetTheme().Well.ShadeTop, 1.0f));
	TestTrue(TEXT("The clamp is reported"),
		FMixtormatThemeStore::GetValidationIssues().Num() > 0);

	// Leave the store as it was found, so test order cannot matter.
	FMixtormatThemeStore::ResetToDefaults();
	TestTrue(TEXT("Reset restores a clean theme"),
		FMixtormatThemeStore::GetValidationIssues().Num() == 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS