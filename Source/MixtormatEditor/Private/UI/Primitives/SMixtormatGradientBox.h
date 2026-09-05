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
// swapped, which is how a right-anchored highlight is expressed.
//
// Two things this has to correct for, both of which made earlier gradients wrong rather than
// merely different:
//
//   Slate names a gradient after the direction of its *bands*, not the direction its colour
//   changes, so its Orient_Vertical is a left-to-right ramp. The translation happens at the draw
//   call; callers speak CSS.
//
//   CSS interpolates between stops in sRGB. Slate interpolates vertex colours in linear space,
//   which takes a visibly different path between the same two endpoints -- lighter through the
//   middle, and reading as an eased ramp where the design asks for a steady one. This samples each
//   span in sRGB and emits the samples as Slate stops, so the rendered curve is the CSS curve.
class SMixtormatGradientBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGradientBox)
		: _StartColor(FLinearColor::Transparent)
		, _EndColor(FLinearColor::Transparent)
		, _MultiplyStart(FLinearColor::Transparent)
		, _MultiplyMid(FLinearColor::Transparent)
		, _MultiplyMidPosition(-1.0f)
		, _MultiplyEnd(FLinearColor::Transparent)
		, _Orientation(Orient_Vertical)
		, _CornerRadius(0.0f)
		, _CornerRadii(FVector4f(-1.0f))
		, _Padding(FMargin(0.0f))
	{}
		SLATE_ATTRIBUTE(FLinearColor, StartColor)
		SLATE_ATTRIBUTE(FLinearColor, EndColor)

		// Second pass across the opposite axis. Compositing black at alpha a leaves src * (1 - a),
		// so a black-to-transparent pair here darkens one edge exactly as a multiply blend would.
		// Leave the colours transparent for a single-axis gradient.
		SLATE_ATTRIBUTE(FLinearColor, MultiplyStart)

		// Optional third stop, because the design's shade is not a straight line: the value fill
		// runs 35% black to 10% at 62% and only then to nothing, which falls away quickly and then
		// holds. Two stops draw that as a even ramp and the fill reads as eased.
		// Leave MultiplyMidPosition negative for a two-stop pass.
		SLATE_ATTRIBUTE(FLinearColor, MultiplyMid)
		SLATE_ARGUMENT(float, MultiplyMidPosition)

		SLATE_ATTRIBUTE(FLinearColor, MultiplyEnd)

		SLATE_ARGUMENT(EOrientation, Orientation)
		SLATE_ARGUMENT(float, CornerRadius)

		// Per-corner override, in Slate's order: top-left, top-right, bottom-right, bottom-left.
		// Leave it negative to round every corner by CornerRadius instead.
		SLATE_ARGUMENT(FVector4f, CornerRadii)
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
	TAttribute<FLinearColor> MultiplyMid;
	TAttribute<FLinearColor> MultiplyEnd;
	float MultiplyMidPosition = -1.0f;
	EOrientation Orientation = Orient_Vertical;
	FVector4f CornerRadii = FVector4f(0.0f);
};
