#include "UI/Controls/SMixtormatTile.h"

#include "AssetThumbnail.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
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

	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}
	else if (!InArgs._DisplayName.IsEmpty())
	{
		SetToolTipText(InArgs._DisplayName);
	}

	const ISlateStyle& Style = FMixtormatStyle::Get();

	// The image. Thumbnails render asynchronously through the shared pool, so an unrendered one
	// shows the tile background rather than nothing at all.
	TSharedRef<SWidget> Image = SNew(SImage)
		.Image(Style.GetBrush(TEXT("Mixtormat.ThumbnailBackground")));
	if (InArgs._ThumbnailAsset.IsValid() && InArgs._ThumbnailPool.IsValid())
	{
		const int32 Resolution = FMath::RoundToInt(TileSize);
		Thumbnail = MakeShared<FAssetThumbnail>(
			InArgs._ThumbnailAsset,
			Resolution,
			Resolution,
			InArgs._ThumbnailPool);

		// The tile draws its own caption and badge, so the asset thumbnail contributes nothing but
		// the picture: no engine label, no asset-type overlay. Configured here rather than pulled
		// from the inspector's internals, so this widget depends on nothing but the style.
		FAssetThumbnailConfig Config;
		Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
		Config.bAllowAssetSpecificThumbnailOverlay = false;
		Image = Thumbnail->MakeThumbnailWidget(Config);
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
		.Padding(2.0f)
		[
			SNew(SBorder)
			.Visibility_Lambda([Badge = InArgs._Badge]()
			{
				return Badge.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
			})
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Tile.NameStrip")))
			.Padding(FMargin(3.0f, 0.0f))
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.Tile.Name")))
				.Text(InArgs._Badge)
			]
		];
	}

	if (InArgs._bShowName && !InArgs._DisplayName.IsEmpty())
	{
		Stack->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.HeightOverride(MixtormatTokens::TileNameStripHeight)
			[
				SNew(SBorder)
				.BorderImage(Style.GetBrush(TEXT("Mixtormat.Tile.NameStrip")))
				.Padding(FMargin(3.0f, 0.0f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.Tile.Name")))
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
		.Padding(2.0f)
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
		.WidthOverride(TileSize)
		.HeightOverride(TileSize)
		[
			SNew(SBorder)
			.BorderImage(this, &SMixtormatTile::GetBorderBrush)
			.Padding(MixtormatTokens::OutlineWidth)
			[
				Stack
			]
		]
	];
}

const FSlateBrush* SMixtormatTile::GetBorderBrush() const
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	if (bSelected.Get(false))
	{
		return Style.GetBrush(TEXT("Mixtormat.Tile.Selected"));
	}
	return Style.GetBrush(IsHovered()
		? TEXT("Mixtormat.Tile.Hovered")
		: TEXT("Mixtormat.Tile.Normal"));
}

FVector2D SMixtormatTile::ComputeDesiredSize(float) const
{
	return FVector2D(TileSize, TileSize);
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
