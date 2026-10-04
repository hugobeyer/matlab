// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatTheme.h"

// Builds drawable recipes from the authored theme.
//
// The painter knows how to composite a recipe. These know what a *well* is made of. Between them
// sits the translation from "a component has these authored numbers" to "a surface has these
// colour references, blend modes and ramps" -- which is the whole content of the component
// definition, and the reason a widget migrating to this system stops carrying colour logic.
//
// Recipes reference colour *roles*, not colours. That is the property worth protecting: retinting
// the palette restyles every recipe without any of them changing.
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

	enum class EMixtormatWellState : uint8
	{
		Rest,
		Hover,
	};

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

	enum class EMixtormatButtonState : uint8
	{
		Rest,
		Hover,
		Selected,
	};

	// The shared button: an Accent body gradient over Ground, an Additive hairline whose blend is
	// independent of the body's, and a separator. One recipe behind tabs, segmented controls,
	// top-bar actions and layer-add actions (§20).
	FMixtormatSurfaceRecipe MakeButtonRecipe(const FMixtormatTheme& Theme, EMixtormatButtonState State = EMixtormatButtonState::Rest);
}