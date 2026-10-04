// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatTile.h"

#include "AssetThumbnail.h"
#include "Engine/Texture2D.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatTile::Construct(const FArguments& InArgs)
{
	TileSize = InArgs._TileSize;
	bSelected = InArgs._bSelected;
	OnActivated = InArgs._OnActivated;
	OnGalleryZoom = InArgs._OnGalleryZoom;

	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}
	else if (!InArgs._DisplayName.IsEmpty())
	{
		SetToolTipText(InArgs._DisplayName);
	}

	CaptionTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			FMixtormatThemeStore::GetResolved().Typography, Mixtormat::EMixtormatTextRole::GalleryCaption),
		FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	// The image. Thumbnails render asynchronously through the shared pool; the recipe-painted tile
	// surface remains visible behind an unrendered thumbnail.
	TSharedRef<SWidget> Image = SNew(SImage);
	if (InArgs._ThumbnailAsset.IsValid())
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(InArgs._ThumbnailAsset.GetAsset()))
		{
			ThumbnailTexture.Reset(Texture);
			ThumbnailBrush = MakeUnique<FSlateBrush>();
			ThumbnailBrush->SetResourceObject(Texture);
			ThumbnailBrush->SetImageSize(FVector2D(
				Texture->GetSizeX(),
				Texture->GetSizeY()));
			ThumbnailBrush->DrawAs = ESlateBrushDrawType::Image;
			ThumbnailBrush->Tiling = ESlateBrushTileType::NoTile;
			Image = SNew(SImage).Image(ThumbnailBrush.Get());
		}
		else if (InArgs._ThumbnailPool.IsValid())
		{
			const int32 Resolution = InArgs._ThumbnailResolution > 0
				? InArgs._ThumbnailResolution
				: FMath::RoundToInt(TileSize.Get(MixtormatTokens::MaskTileSize));
			Thumbnail = MakeShared<FAssetThumbnail>(
				InArgs._ThumbnailAsset,
				Resolution,
				Resolution,
				InArgs._ThumbnailPool);

			FAssetThumbnailConfig Config;
			Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
			Config.AllowAssetSpecificThumbnailOverlay = false;
			Config.ShowAssetColor = false;
			Config.ShowAssetBorder = false;
			Image = Thumbnail->MakeThumbnailWidget(Config);
		}
	}

	TSharedRef<SOverlay> Stack = SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			Image
		];

	if (InArgs._Badge.IsSet())
	{
		Stack->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.OverlayInset)
		[
			SNew(SMixtormatSurfaceBox)
			.Visibility_Lambda([Badge = InArgs._Badge]()
			{
				return Badge.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
			})
			.Recipe(Mixtormat::MakeGalleryCaptionRecipe())
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().GalleryLayout.CaptionInset, 0.0f))
			[
				SNew(STextBlock)
				.TextStyle(&CaptionTextStyle)
				.Text(InArgs._Badge)
			]
		];
	}

	if ((InArgs._bShowName || InArgs._bShowNameOnHover) && !InArgs._DisplayName.IsEmpty())
	{
		const bool bAlwaysShowName = InArgs._bShowName;
		Stack->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.Visibility_Lambda([this, bAlwaysShowName]()
			{
				return bAlwaysShowName || IsHovered()
					? EVisibility::HitTestInvisible
					: EVisibility::Collapsed;
			})
			.HeightOverride(FMixtormatThemeStore::GetResolved().GalleryLayout.CaptionHeight)
			[
				SNew(SMixtormatSurfaceBox)
				.Recipe(Mixtormat::MakeGalleryCaptionRecipe())
				.Padding(FMargin(FMixtormatThemeStore::GetResolved().GalleryLayout.CaptionInset, 0.0f))
				[
					SNew(STextBlock)
					.TextStyle(&CaptionTextStyle)
					.Text(InArgs._DisplayName)
				]
			]
		];
	}

	if (InArgs._HoverContent.Widget != SNullWidget::NullWidget)
	{
		Stack->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.OverlayInset)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return IsHovered() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				InArgs._HoverContent.Widget
			]
		];
	}

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride_Lambda([this]()
		{
			return TileSize.Get(MixtormatTokens::MaskTileSize);
		})
		.HeightOverride_Lambda([this]()
		{
			return TileSize.Get(MixtormatTokens::MaskTileSize);
		})
		[
			SNew(SBorder)
			.Padding(0.0f)
			[
				Stack
			]
		]
	];
}

int32 SMixtormatTile::OnPaint(
	const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements, const int32 LayerId, const FWidgetStyle& WidgetStyle,
	const bool bParentEnabled) const
{
	using namespace Mixtormat;
	const bool bSelectedNow = bSelected.Get(false);
	const EMixtormatGalleryTileState State = bSelectedNow
		? (IsHovered() ? EMixtormatGalleryTileState::SelectedHover : EMixtormatGalleryTileState::Selected)
		: (IsHovered() ? EMixtormatGalleryTileState::Hover : EMixtormatGalleryTileState::Rest);
	const FMixtormatSurfaceRecipe Recipe = MakeGalleryTileRecipe(FMixtormatThemeStore::GetResolved(), State);
	FMixtormatSurfaceSamples Samples;
	const FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	CompositeSurface(Recipe, Palette, FMixtormatStateModifier(), Samples);
	FMixtormatSurfaceDrawStyle DrawStyle;
	DrawStyle.Tint = WidgetStyle.GetColorAndOpacityTint();
	DrawStyle.Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	int32 SurfaceLayer = FMixtormatSurfacePainter::PaintBody(Elements, LayerId, Geometry, Recipe, Samples, DrawStyle);
	SurfaceLayer = FMixtormatSurfacePainter::PaintBorders(
		Elements, SurfaceLayer, Geometry, Recipe, Palette, WidgetStyle, Samples, DrawStyle);
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, SurfaceLayer + 1, WidgetStyle, bParentEnabled);
}

FVector2D SMixtormatTile::ComputeDesiredSize(float) const
{
	const float CurrentTileSize = TileSize.Get(MixtormatTokens::MaskTileSize);
	return FVector2D(CurrentTileSize, CurrentTileSize);
}

FReply SMixtormatTile::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const int32 Direction = FMath::Sign(MouseEvent.GetWheelDelta());
	if (MouseEvent.IsControlDown() && Direction != 0 && OnGalleryZoom.IsBound())
	{
		OnGalleryZoom.Execute(Direction);
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
}

FReply SMixtormatTile::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
}

FReply SMixtormatTile::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;

	// Activate only when the release lands on the tile, so a press that wanders off cancels the
	// way a button does.
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		OnActivated.ExecuteIfBound();
	}
	return FReply::Handled();
}
