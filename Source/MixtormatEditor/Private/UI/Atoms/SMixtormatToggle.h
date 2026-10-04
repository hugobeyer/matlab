// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
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
	FLinearColor GetFillTop() const;
	FLinearColor GetFillBottom() const;

	TAttribute<ECheckBoxState> IsChecked;
};
