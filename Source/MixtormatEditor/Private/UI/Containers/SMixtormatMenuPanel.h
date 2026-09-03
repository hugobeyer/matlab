#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// The ground every popover sits on.
//
// A menu is the one surface in the tool that is its own window: it floats over whatever the panel
// beneath it happens to be showing, so unlike a group header it cannot take its weight from a
// container behind it. That is why it is the only place besides the drag ghost that gets a drop
// shadow, and why its tint has to land on an opaque ground of its own.
//
// Three stops, from the canvas: tinted blue at the very top, on flat ground by the base of the
// first row, and a hair darker for the rest. The middle stop sits at a FIXED height rather than a
// fraction, so a menu of two items and a menu of twelve have the same lip instead of the same
// ramp -- which is the difference between a lit edge and a blue wash.
class SMixtormatMenuPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatMenuPanel)
		: _Padding(FMargin(0.0f, MixtormatTokens::MenuPanelPadding))
		, _MinWidth(0.0f)
	{}
		SLATE_ARGUMENT(FMargin, Padding)
		// Menus that hold a grid rather than a list set their own width; a plain list takes the
		// standard one so a column of popovers does not shuffle as you move between them.
		SLATE_ARGUMENT(float, MinWidth)
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
};
