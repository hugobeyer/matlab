// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Gallery/SMixtormatGalleryTab.h"

#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormatGalleryTab::Construct(const FArguments& InArgs)
{
	OnActivated = InArgs._OnActivated;
	SetToolTipText(LOCTEXT("RestoreGalleryHint", "Open the material and mask gallery (G)."));

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const auto& Glyph = Resolved.Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)];
	const FSlateColor Tint(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	ChildSlot
	[
		SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(Glyph.GlyphSize)
				.HeightOverride(Glyph.GlyphSize)
				[
					SNew(SImage).Image(MixtormatIcons::ChevronUp()).ColorAndOpacity(Tint)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(Resolved.GalleryLayout.HeaderGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(Glyph.GlyphSize)
				.HeightOverride(Glyph.GlyphSize)
				[
					SNew(SImage).Image(MixtormatIcons::Library()).ColorAndOpacity(Tint)
				]
			]
		]
	];
}

int32 SMixtormatGalleryTab::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	// The same surface the foldout headers wear, top-rounded only.
	const Mixtormat::FMixtormatSurfaceRecipe Recipe = Mixtormat::MakeGalleryTabRecipe(
		FMixtormatThemeStore::GetTheme(), bHovered);
	const int32 SurfaceLayer = Mixtormat::FMixtormatSurfacePainter::PaintSurface(
		OutDrawElements, LayerId, AllottedGeometry, Recipe,
		FMixtormatThemeStore::GetResolved().Palette, InWidgetStyle);

	// Content always paints above the complete surface, including zero-sized tabs.
	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, SurfaceLayer + 1,
		InWidgetStyle, bParentEnabled);
}

FReply SMixtormatGalleryTab::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnActivated.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SMixtormatGalleryTab::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	bHovered = true;
}

void SMixtormatGalleryTab::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
}

#undef LOCTEXT_NAMESPACE
