#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A boolean, drawn as a well that fills.
//
// Slate's checkbox marks itself with a glyph -- a tick, or a cross for the undetermined state.
// At the 14px an inspector row affords, a glyph is a shape to decode rather than a state to
// notice, and neither mark survives the size legibly. A filled box does: the eye reads presence,
// not form, and the same well-and-fill vocabulary already carries every slider and chip in the
// panel, so a toggle stops looking like a control borrowed from somewhere else.
//
// Anatomy is one well with one fill inset inside it by a single pixel. The inset is what makes it
// read as filled -- without it the fill covers the well entirely and the control becomes a plain
// lighter square with no container to be full of.
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
	// Undetermined fills like checked but at the unhovered weight, so a mixed selection reads as
	// "some" rather than as either extreme.
	bool IsFilled() const;
	EVisibility GetFillVisibility() const;
	FLinearColor GetFillTop() const;
	FLinearColor GetFillBottom() const;

	TAttribute<ECheckBoxState> IsChecked;
};
