// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatMenuPanel.h"

#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatMenuPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(InArgs._MinWidth)
		.Padding(InArgs._Padding)
		[
			InArgs._Content.Widget
		]
	];
}

int32 SMixtormatMenuPanel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const float MenuHeight = AllottedGeometry.GetLocalSize().Y;
	const Mixtormat::FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
	Mixtormat::FMixtormatSurfaceRecipe Recipe = Mixtormat::MakeMenuPanelRecipe(Theme, MenuHeight);
	const int32 SurfaceLayer = Mixtormat::FMixtormatSurfacePainter::PaintSurface(
		OutDrawElements, LayerId, AllottedGeometry, Recipe,
		FMixtormatThemeStore::GetResolved().Palette, InWidgetStyle);
	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, SurfaceLayer + 1, InWidgetStyle, bParentEnabled);
}
