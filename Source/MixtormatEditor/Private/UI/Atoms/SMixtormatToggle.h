// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Style/MixtormatTheme.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A boolean, drawn as a well that fills.
//
// Slate's checkbox marks itself with a glyph -- a tick, or a cross for the undetermined state.
// At the size an inspector row affords, a glyph is a shape to decode rather than a state to
// notice, and neither mark survives the size legibly. A filled box does: the eye reads presence,
// not form, and the same well-and-fill vocabulary already carries every slider and chip in the
// panel, so a toggle stops looking like a control borrowed from somewhere else.
//
// The well itself is painted by SMixtormatWellBox, which delegates to the shared well painter
// so this control cannot drift from the slider trough or the dropdown chip.
class SMixtormatToggle final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatToggle) {}
		SLATE_ATTRIBUTE(ECheckBoxState, IsChecked)
		SLATE_EVENT(FOnCheckStateChanged, OnCheckStateChanged)
		SLATE_ATTRIBUTE(FText, ToolTip)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// The fill, as a recipe rather than as two colours.
	//
	// The mixed (Undetermined) state keeps the resting fill: that is a real visual for "this row
	// disagrees with itself", so it is preserved rather than folded into the on state. Unchecked
	// yields a recipe with the fill layer disabled, which is the same result as a zero-alpha fill
	// and one fewer element.
	//
	// Rebuilt per paint rather than cached in a brush, because a brush name cannot be interpolated
	// and a later state animation needs a rest value and a state value to lerp between.
	//
	// Qualified: this widget is at global scope and the recipe lives in namespace Mixtormat.
	Mixtormat::FMixtormatSurfaceRecipe GetFillRecipe() const;

	TAttribute<ECheckBoxState> IsChecked;
};
