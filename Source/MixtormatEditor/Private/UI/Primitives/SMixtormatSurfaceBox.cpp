// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Primitives/SMixtormatSurfaceBox.h"

#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

void SMixtormatSurfaceBox::Construct(const FArguments& InArgs)
{
	Recipe = InArgs._Recipe;
	bPaintBorders = InArgs._PaintBorders;

	ChildSlot
	.Padding(InArgs._Padding)
	[
		InArgs._Content.Widget
	];
}

int32 SMixtormatSurfaceBox::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	// The widget is at global scope, so every style type is spelled with its namespace. Writing them
	// bare reads as if they were globals and compiles as soon as a using-directive appears.
	using namespace Mixtormat;

	const FMixtormatSurfaceRecipe Surface = Recipe.Get(FMixtormatSurfaceRecipe());
	const FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;

	FMixtormatSurfaceSamples Samples;
	CompositeSurface(Surface, Palette, FMixtormatStateModifier(), Samples);

	// Body first, then the hairlines on top of it, then the content above both. The borders
	// composite against the body's own colours, which is why the samples are computed once and
	// handed to both calls rather than each recompositing for itself.
	int32 Layer = FMixtormatSurfacePainter::PaintBody(
		OutDrawElements, LayerId, AllottedGeometry, Surface, Samples);

	if (bPaintBorders)
	{
		Layer = FMixtormatSurfacePainter::PaintBorders(
			OutDrawElements, Layer, AllottedGeometry, Surface, Palette, InWidgetStyle, Samples);
	}

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer + 1, InWidgetStyle, bParentEnabled);
}