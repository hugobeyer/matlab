// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatResolvedStyle.h"
#include "Style/MixtormatTheme.h"

// Builds drawable recipes from the authored theme.
//
// The painter knows how to composite a recipe. These know what a *well* is made of. Between them
// sits the translation from "a component has these authored numbers" to "a surface has these
// colour references, blend modes and ramps" -- which is the whole content of the component
// definition, and the reason a widget migrating to this system stops carrying colour logic.
//
// Recipes reference palette roles or explicitly authored local source colours, never baked
// compositing results. Tint, opacity and saturation are resolved by the shared painter.
//
// State is a parameter, not a second recipe. §16 forbids duplicating a surface per state, so
// Rest/Hover/Active share their base and shade and differ only where the design genuinely authors
// different numbers -- for the well, that is the border's four opacity values and nothing else.
namespace Mixtormat
{
	// Ramp helpers. Authored, not resolved: these produce the Position/Value pairs that go into a
	// recipe, and the painter evaluates them at paint time.
	FMixtormatRamp MakeLinearRamp(EMixtormatAxis Axis, float Start, float End, int32 Samples = 6);

	// A three-point ramp with a movable middle. The fill's shade pass is the only one: start/mid/end
	// is a curve, not a falloff, and routing it through a two-point form would move the midpoint.
	FMixtormatRamp MakeMidpointRamp(EMixtormatAxis Axis, float Start, float Mid, float MidPosition, float End);

	// A ramp sampled through the shared power curve rather than interpolated straight between its
	// endpoints.
	//
	// This exists because the fill's body is authored with FillFalloffPower 0.05 -- it holds near its
	// top value and drops late, which is what reads as a lit surface. A linear ramp through the same
	// two endpoints is a different gradient that happens to share its endpoints, and it is exactly
	// the kind of substitution that survives every structural check.
	FMixtormatRamp MakeFalloffRamp(EMixtormatAxis Axis, float Start, float End, float Power, int32 Samples);

	enum class EMixtormatLayerKind : uint8 { Layer, Group, Child };

		struct FMixtormatLayerRecipeContext
		{
			EMixtormatLayerKind Kind = EMixtormatLayerKind::Layer;
			bool bHovered = false;
			bool bSelected = false;
			bool bVisible = true;
			bool bReference = false;
			bool bInstanceSource = false;
			FLinearColor GroupTint = FLinearColor::Transparent;
		};

		FMixtormatSurfaceRecipe MakeLayerBodyRecipe(
			const FMixtormatTheme& Theme, const FMixtormatLayerRecipeContext& Context);
		// Pure overlays: Base is ignored by CompositeOverlay. Caller owns band/edge geometry.
		FMixtormatSurfaceRecipe MakeLayerGroupCrossRecipe(
			const FMixtormatTheme& Theme, const FMixtormatLayerRecipeContext& Context);
		FMixtormatSurfaceRecipe MakeLayerGlowRecipe(const FMixtormatTheme& Theme, float ReachFraction);
		// Non-selected hairlines are for Layer/Group only; selected hairlines apply to every kind.
		FMixtormatSurfaceRecipe MakeLayerHairlineRecipe(const FMixtormatTheme& Theme, bool bSelected);

		// Ground only, for container margins and gaps between separately arranged bands.
	FMixtormatSurfaceRecipe MakeGroundRecipe();

	// Ground -> local lift -> Accent, then an enabled-only Additive top hairline.
	FMixtormatSurfaceRecipe MakeFoldoutRecipe(
		const FMixtormatTheme& Theme, bool bHovered = false, bool bEnabled = true);

	// Callers map geometry to Seam = nominalHeader / (nominalHeader + max(reach, 0))
	// and TailFraction = min(reach / bodyHeight, .5). Zero tail means a flat body.
	// Bands stay square: the caller clips the whole group or supplies a compact-card radius.
	FMixtormatSurfaceRecipe MakeCardHeaderRecipe(const FMixtormatTheme& Theme, float Seam);
	FMixtormatSurfaceRecipe MakeCardBodyRecipe(
		const FMixtormatTheme& Theme, float Seam, float TailFraction);

	enum class EMixtormatWellState : uint8
	{
		Rest,
		Hover,
	};

	enum class EMixtormatMenuRowState : uint8 { Normal, Hover, Checked, Disabled, Destructive, DestructiveHover };

	FMixtormatSurfaceRecipe MakeMenuPanelRecipe(const FMixtormatTheme& Theme, float MenuHeight);
	FMixtormatSurfaceRecipe MakeMenuRowRecipe(
		const FMixtormatTheme& Theme, EMixtormatMenuRowState State);

enum class EMixtormatGalleryTileState : uint8 { Rest, Hover, Selected, SelectedHover };
FMixtormatSurfaceRecipe MakeGalleryTileRecipe(
		const FMixtormatResolvedStyle& Style, EMixtormatGalleryTileState State);
	// The caption strip: a flat Shade ground behind the tile's name. Structurally separate from the
		// tile surface because it is an overlay on the picture rather than the picture's own ground, so
		// it gets its own small recipe rather than a variant of the tile's.
		FMixtormatSurfaceRecipe MakeGalleryCaptionRecipe();

enum class EMixtormatPreviewPlateState : uint8 { Rest, Hover, Pressed, Checked };
// The translucent plate behind one viewport overlay control. Built from the resolved preview
	// style because its whole point is carrying the authored plate alpha to the screen: the base is
	// the plate colour at `--overlay-plate-opacity`, not an opaque ground with a plate laid over it.
	//
	// `--overlay-ground-opacity` belongs to the overlay cluster's own gradient backdrop, which this
	// recipe deliberately does not reproduce. Combining the two here is what made the plate opaque.
	FMixtormatSurfaceRecipe MakePreviewPlateRecipe(
		const FMixtormatResolvedStyle& Style, EMixtormatPreviewPlateState State = EMixtormatPreviewPlateState::Rest);

	// Ground, a black Multiply shade falling top to bottom, and a hairline outline.
	//
	// The two states differ only in the border's four opacity numbers. Base and shade are identical
	// and are built once, so this is a parameter and not a duplicated surface.
	FMixtormatSurfaceRecipe MakeWellRecipe(const FMixtormatTheme& Theme, EMixtormatWellState State = EMixtormatWellState::Rest);

	// The flat variant: ground only, no recess and no outline. For a control that is mid-edit and
	// needs the field itself to read.
	FMixtormatSurfaceRecipe MakeFlatWellRecipe(const FMixtormatTheme& Theme);

	enum class EMixtormatFillState : uint8
	{
		Rest,
		Hover,
		Active,
		Disabled,
	};

	// The slider fill: the well's own ground and recess, then an Additive accent body, then a black
	// Multiply shade running the other way with a midpoint.
	//
	// It carries the well's base and shade rather than assuming them, because the painter produces
	// an opaque surface: a fill drawn over the trough without them would erase the trough beneath
	// its own edges. Both come from the same authored numbers, so the seam is invisible.
	FMixtormatSurfaceRecipe MakeFillRecipe(const FMixtormatTheme& Theme, EMixtormatFillState State = EMixtormatFillState::Rest);

	// A toggle is a well, plus the fill when checked (§19). It owns no surface of its own -- this is
	// the first proof that recipes compose rather than accumulate.
	//
	// The fill is painted over the SAME rect as the well, not inset inside it: SMixtormatSurfaceBox's
	// padding insets the content, not the surface. That is what keeps the two recesses aligned --
	// the well's ramp is authored over the well's height, and a fill composited into an inset rect
	// would compress the same ramp into a shorter box and leave a visible step at its edges.
	//
	// The well state is a parameter because the fill covers the well: if only the well lifted on
	// hover, the area under the fill would stay dark and the control would read as two surfaces.
	//
	// Checked + hovered is the composition of the two parameters, not a fourth case.
	FMixtormatSurfaceRecipe MakeCheckedToggleRecipe(
		const FMixtormatTheme& Theme,
		EMixtormatWellState WellState = EMixtormatWellState::Rest,
		EMixtormatFillState FillState = EMixtormatFillState::Rest);

	// The states a button surface can be in.
//
// Disabled is deliberately absent: it is the same numbers at a lower opacity, carried on
// FMixtormatStateModifier rather than being a fourth appearance (§16). A widget with a Disabled
// flag collapses it onto these three.
enum class EMixtormatButtonState : uint8
	{
		Rest,
		Hover,
		Selected,
	};

	// The shared button: an Accent body gradient over Ground, an Additive hairline whose blend is
	// independent of the body's, and a separator tick. One recipe behind tabs, segmented controls,
	// top-bar actions and layer-add actions (§20).
	//
	// `bShowSeparator` is a layout decision, not a visual one: the prototype draws the separator only
	// on every button but the last in a run, and the recipe does not know what a run is.
	FMixtormatSurfaceRecipe MakeButtonRecipe(const FMixtormatTheme& Theme, EMixtormatButtonState State = EMixtormatButtonState::Rest, bool bShowSeparator = false);

	// The label colour for a button state, from the resolved palette rather than the legacy one.
	//
	// Returned separately from the recipe because text is not a paint layer: it reaches the screen
	// through the widget style's foreground, and a label has to be able to change colour without its
	// plate changing. A selected button's label is Accent, everything else is Text at the authored
	// button opacity.
	FLinearColor MakeButtonTextColor(
		const FMixtormatResolvedPalette& Palette,
		const FMixtormatTheme& Theme,
		EMixtormatButtonState State);
}