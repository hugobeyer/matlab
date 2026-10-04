// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatFoldoutHeader.h"


#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

void SMixtormatFoldoutHeader::Construct(const FArguments& InArgs)
{
	Hovered = InArgs._IsHovered;
	bEnabled = InArgs._bEnabled;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

FVector2D SMixtormatFoldoutHeader::ComputeDesiredSize(const float LayoutScaleMultiplier) const
{
	// Preserve min-height plus header padding, rather than squeezing the content into a fixed bar.
	const FVector2D ChildSize = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
	return FVector2D(
		ChildSize.X,
		FMath::Max(ChildSize.Y,
			FMixtormatThemeStore::GetResolved().FoldoutLayout.Height
				+ FMixtormatThemeStore::GetResolved().FoldoutLayout.HeaderPaddingTop
				+ FMixtormatThemeStore::GetResolved().FoldoutLayout.HeaderPaddingBottom));
}

int32 SMixtormatFoldoutHeader::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	// Keep the caller's hover/enabled semantics: disabled removes the hairline, not the lift.
	const Mixtormat::FMixtormatSurfaceRecipe Recipe = Mixtormat::MakeFoldoutRecipe(
		FMixtormatThemeStore::GetTheme(), Hovered.Get(false), bEnabled.Get(true));
	const int32 SurfaceLayer = Mixtormat::FMixtormatSurfacePainter::PaintSurface(
		OutDrawElements, LayerId, AllottedGeometry, Recipe,
		FMixtormatThemeStore::GetResolved().Palette, InWidgetStyle);

	// Content always paints, including zero-sized headers; it remains above the complete surface.
	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, SurfaceLayer + 1,
		InWidgetStyle, bParentEnabled);
}
