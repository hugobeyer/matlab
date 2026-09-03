#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A container that paints a two-stop linear gradient behind its content.
//
// Slate brushes cannot express a gradient, so the layer stack's states -- an opened layer's blue
// fading down, a selected child's blue fading in from the right, a hidden layer's dark sweep --
// have no brush equivalent. FSlateDrawElement::MakeGradient does, but only from a custom paint,
// which is what this wraps.
//
// Direction follows the CSS the design was specified in: Vertical is 180deg (Start at the top),
// Horizontal is 90deg (Start at the left). A 270deg gradient is the horizontal one with its stops
// swapped, which is how the selected child row reads blue on its right edge.
class SMixtormatGradientBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGradientBox)
		: _StartColor(FLinearColor::Transparent)
		, _EndColor(FLinearColor::Transparent)
		, _Orientation(Orient_Vertical)
		, _CornerRadius(0.0f)
	{}
		SLATE_ATTRIBUTE(FLinearColor, StartColor)
		SLATE_ATTRIBUTE(FLinearColor, EndColor)
		SLATE_ARGUMENT(EOrientation, Orientation)
		SLATE_ARGUMENT(float, CornerRadius)
		SLATE_ATTRIBUTE(FMargin, Padding)
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
	TAttribute<FLinearColor> StartColor;
	TAttribute<FLinearColor> EndColor;
	EOrientation Orientation = Orient_Vertical;
	float CornerRadius = 0.0f;
};
