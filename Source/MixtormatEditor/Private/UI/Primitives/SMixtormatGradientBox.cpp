#include "UI/Primitives/SMixtormatGradientBox.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

void SMixtormatGradientBox::Construct(const FArguments& InArgs)
{
	StartColor = InArgs._StartColor;
	EndColor = InArgs._EndColor;
	Orientation = InArgs._Orientation;
	CornerRadius = InArgs._CornerRadius;
	MultiplyStart = InArgs._MultiplyStart;
	MultiplyMid = InArgs._MultiplyMid;
	MultiplyEnd = InArgs._MultiplyEnd;
	MultiplyMidPosition = InArgs._MultiplyMidPosition;

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
	const bool bVertical = Orientation == Orient_Vertical;

	const MixtormatGradient::FStop BaseStops[] = {
		{ 0.0f, StartColor.Get(FLinearColor::Transparent) },
		{ 1.0f, EndColor.Get(FLinearColor::Transparent) },
	};
	MixtormatGradient::Paint(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Size,
		Orientation,
		BaseStops,
		CornerRadius);

	int32 Layer = LayerId;

	// Optional second pass across the other axis. Compositing black at alpha a leaves src * (1-a),
	// so a black-to-transparent overlay is a multiply -- which is how the value fills darken toward
	// one edge without Slate having a blend mode for it.
	const FLinearColor MulStart = MultiplyStart.Get(FLinearColor::Transparent);
	const FLinearColor MulMid = MultiplyMid.Get(FLinearColor::Transparent);
	const FLinearColor MulEnd = MultiplyEnd.Get(FLinearColor::Transparent);
	if (MulStart.A > 0.0f || MulMid.A > 0.0f || MulEnd.A > 0.0f)
	{
		const EOrientation CrossAxis = bVertical ? Orient_Horizontal : Orient_Vertical;

		// The shade is not a straight line where the design gives it a midpoint: it falls away
		// quickly and then holds, which is what stops the fill reading as a soft ramp.
		TArray<MixtormatGradient::FStop, TInlineAllocator<3>> MulStops;
		MulStops.Add({ 0.0f, MulStart });
		if (MultiplyMidPosition > 0.0f && MultiplyMidPosition < 1.0f)
		{
			MulStops.Add({ MultiplyMidPosition, MulMid });
		}
		MulStops.Add({ 1.0f, MulEnd });

		++Layer;
		MixtormatGradient::Paint(
			OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(),
			Size, CrossAxis, MulStops, CornerRadius);
	}

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer + 1, InWidgetStyle, bParentEnabled);
}
