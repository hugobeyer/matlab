// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A container that paints the shared well *around* whatever it holds: background and recess below
// the child, border and optional disabled recess above it.
//
// This exists as its own widget rather than as two lines inside each caller because of paint
// ordering. The well's border has to sit above the content it encloses -- a toggle's fill, a chip's
// label -- while the well's ground and recess sit below it. SCompoundWidget draws its children
// after its own OnPaint, so a caller cannot get "background, then child, then border" out of its
// own paint function without duplicating the whole thing. Doing it here means the toggle, the chip
// and anything else that needs a well gets the same ordering by construction.
class SMixtormatWellBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatWellBox) {}
		// Lifts the recess and brightens the border to the hover set.
		SLATE_ATTRIBUTE(bool, IsHovered)
		SLATE_ATTRIBUTE(bool, IsEnabled)
		// Paints its own black recess over the content when disabled, instead of letting a caller
		// drop the widget's opacity. A faded well, fill and label read as one uniformly dimmed
		// object; a disabled control needs the recess to stay *relative* to what is inside it.
		SLATE_ARGUMENT(bool, bDisabledShade)
		SLATE_ATTRIBUTE(float, DisabledShadeTop)
		SLATE_ATTRIBUTE(float, DisabledShadeBottom)
		// Required, not decorative: without a declared slot the generated arguments have no
		// operator[], so a caller's `[ ... ]` fails to parse rather than failing to compile.
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	TAttribute<bool> Hovered;
	TAttribute<bool> Enabled;
	bool bDrawDisabledShade = false;
	TAttribute<float> DisabledShadeTop;
	TAttribute<float> DisabledShadeBottom;
};