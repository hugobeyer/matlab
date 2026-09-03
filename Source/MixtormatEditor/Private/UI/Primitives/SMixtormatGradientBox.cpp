#include "UI/Primitives/SMixtormatGradientBox.h"

#include "Rendering/DrawElements.h"

void SMixtormatGradientBox::Construct(const FArguments& InArgs)
{
	StartColor = InArgs._StartColor;
	EndColor = InArgs._EndColor;
	Orientation = InArgs._Orientation;
	CornerRadius = InArgs._CornerRadius;
	MultiplyStart = InArgs._MultiplyStart;
	MultiplyEnd = InArgs._MultiplyEnd;

	ChildSlot
	.Padding(InArgs._Padding)
	[
		InArgs._Content.Widget
	];
}

int32 SMixtormatGradientBox::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());

	// Stop positions are local-space points along the gradient axis, so a two-stop gradient runs
	// corner to corner on whichever axis the orientation names. Vertical is CSS 180deg (start at
	// the top); horizontal is 90deg (start at the left). A 270deg gradient is the horizontal one
	// with its colours swapped, which is how a right-anchored highlight is expressed.
	const bool bVertical = Orientation == Orient_Vertical;
	TArray<FSlateGradientStop> Stops;
	Stops.Reserve(2);
	Stops.Emplace(FVector2f::ZeroVector, StartColor.Get(FLinearColor::Transparent));
	Stops.Emplace(
		bVertical ? FVector2f(0.0f, Size.Y) : FVector2f(Size.X, 0.0f),
		EndColor.Get(FLinearColor::Transparent));

	FSlateDrawElement::MakeGradient(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Stops,
		Orientation,
		ESlateDrawEffect::None,
		FVector4f(CornerRadius));

	int32 Layer = LayerId;

	// Optional second pass across the other axis. Compositing black at alpha a leaves src * (1-a),
	// so a black-to-transparent overlay is a multiply -- which is how the value fills darken toward
	// one edge without Slate having a blend mode for it.
	const FLinearColor MulStart = MultiplyStart.Get(FLinearColor::Transparent);
	const FLinearColor MulEnd = MultiplyEnd.Get(FLinearColor::Transparent);
	if (MulStart.A > 0.0f || MulEnd.A > 0.0f)
	{
		const EOrientation CrossAxis = bVertical ? Orient_Horizontal : Orient_Vertical;
		TArray<FSlateGradientStop> MulStops;
		MulStops.Reserve(2);
		MulStops.Emplace(FVector2f::ZeroVector, MulStart);
		MulStops.Emplace(
			CrossAxis == Orient_Vertical ? FVector2f(0.0f, Size.Y) : FVector2f(Size.X, 0.0f),
			MulEnd);

		++Layer;
		FSlateDrawElement::MakeGradient(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(),
			MulStops,
			CrossAxis,
			ESlateDrawEffect::None,
			FVector4f(CornerRadius));
	}

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer + 1, InWidgetStyle, bParentEnabled);
}
