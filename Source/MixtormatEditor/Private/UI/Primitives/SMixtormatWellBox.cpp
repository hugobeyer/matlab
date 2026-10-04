// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Primitives/SMixtormatWellBox.h"

#include "Rendering/DrawElements.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Primitives/MixtormatGradientPainter.h"
#include "UI/Primitives/MixtormatWell.h"

void SMixtormatWellBox::Construct(const FArguments& InArgs)
{
	Hovered = InArgs._IsHovered;
	Enabled = InArgs._IsEnabled;
	bDrawDisabledShade = InArgs._bDisabledShade;
	DisabledShadeTop = InArgs._DisabledShadeTop;
	DisabledShadeBottom = InArgs._DisabledShadeBottom;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

int32 SMixtormatWellBox::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FVector2f Size(AllottedGeometry.GetLocalSize());

	MixtormatWell::FParams Params;
	Params.bHovered = Hovered.Get(false);

	// Ground and recess first: they are behind the content by definition.
	MixtormatWell::PaintBackground(
		OutDrawElements, LayerId, AllottedGeometry, Size, Params);

	const int32 ChildLayer = SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);

	// The border goes on after the child, which is the whole reason this widget exists.
	MixtormatWell::PaintBorder(
		OutDrawElements, ChildLayer, AllottedGeometry, Size, Params);

	int32 Layer = ChildLayer + 1;

	if (bDrawDisabledShade && !Enabled.Get(true))
	{
		const MixtormatGradient::FStop Shade[] = {
			{ 0.0f, FLinearColor(0.0f, 0.0f, 0.0f, DisabledShadeTop.Get(0.0f)) },
			{ 1.0f, FLinearColor(0.0f, 0.0f, 0.0f, DisabledShadeBottom.Get(0.0f)) },
		};
		MixtormatGradient::Paint(
			OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Size,
			Orient_Vertical, Shade, FVector4f(0.0f));
		++Layer;
	}

	return Layer;
}