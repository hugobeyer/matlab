#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Paints a linear gradient behind its content, with an optional second gradient across the other
// axis that acts as a multiply.
//
// Slate brushes cannot express a gradient, so every surface in the design system that fades --
// a group header's tint, a slider well, a value fill, a menu's top lip -- has no brush equivalent.
// FSlateDrawElement::MakeGradient does, but only from a custom paint, which is what this wraps.
//
// Direction follows the CSS the design was specified in: Vertical is 180deg (Start at the top),
// Horizontal is 90deg (Start at the left). A 270deg gradient is the horizontal one with its colours
// swapped.
class SMixtormatGradientBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGradientBox)
		: _StartColor(FLinearColor::Transparent)
		, _EndColor(FLinearColor::Transparent)
		, _MultiplyStart(FLinearColor::Transparent)
		, _MultiplyEnd(FLinearColor::Transparent)
		, _Orientation(Orient_Vertical)
		, _CornerRadius(0.0f)
		, _Padding(FMargin(0.0f))
	{}
		SLATE_ATTRIBUTE(FLinearColor, StartColor)
		SLATE_ATTRIBUTE(FLinearColor, EndColor)

		// Second pass across the opposite axis. Compositing black at alpha a leaves src * (1 - a),
		// so a black-to-transparent pair here darkens one edge exactly as a multiply blend would.
		// Leave both transparent for a single-axis gradient.
		SLATE_ATTRIBUTE(FLinearColor, MultiplyStart)
		SLATE_ATTRIBUTE(FLinearColor, MultiplyEnd)

		SLATE_ARGUMENT(EOrientation, Orientation)
		SLATE_ARGUMENT(float, CornerRadius)
		SLATE_ARGUMENT(FMargin, Padding)
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
	TAttribute<FLinearColor> MultiplyStart;
	TAttribute<FLinearColor> MultiplyEnd;
	EOrientation Orientation = Orient_Vertical;
	float CornerRadius = 0.0f;
};
