#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class UTexture2D;

DECLARE_DELEGATE(FMixtormatOnTileActivated);
DECLARE_DELEGATE_OneParam(FMixtormatOnTileZoom, int32);

// One thumbnail tile, for every grid in the tool.
//
// The surface library, the mask replacement gallery and the mask picker were three separate
// implementations at three sizes -- 90, 62, and a text menu with no thumbnail at all -- for what is
// the same thing: an image you click, optionally named, optionally selected. This is that thing;
// only the size and what the caption says differ per call site.
//
// The name is an overlay on the image rather than a row beneath it, so showing it costs picture
// instead of layout height. Greyscale masks in particular are hard to tell apart at small sizes,
// which is why the caption defaults to on rather than to hover-only.
class SMixtormatTile final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatTile)
		: _TileSize(MixtormatTokens::MaskTileSize)
		, _ThumbnailResolution(0)
		, _bShowName(true)
		, _bSelected(false)
	{}
		// Square edge length of the whole tile, border included.
		SLATE_ATTRIBUTE(float, TileSize)
		SLATE_ARGUMENT(FText, DisplayName)
		// Texture assets draw directly with strong ownership. Other editor assets temporarily use
		// the shared pool until surface thumbnails migrate to persistent textures.
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_ARGUMENT(int32, ThumbnailResolution)
		// Caption strip across the bottom. Off for dense grids that name the hovered tile once
		// somewhere else instead.
		SLATE_ARGUMENT(bool, bShowName)
		SLATE_ATTRIBUTE(bool, bSelected)
		// Short all-caps mark in the top-left -- "INV" for an inverted mask, a family for a
		// surface. Empty for none.
		SLATE_ATTRIBUTE(FText, Badge)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FMixtormatOnTileActivated, OnActivated)
		SLATE_EVENT(FMixtormatOnTileZoom, OnGalleryZoom)
		// Overlaid on hover, top-right: an add affordance, a menu, whatever the call site needs.
		SLATE_NAMED_SLOT(FArguments, HoverContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	const FSlateBrush* GetBorderBrush() const;

	TAttribute<float> TileSize;
	TAttribute<bool> bSelected;
	FMixtormatOnTileActivated OnActivated;
	FMixtormatOnTileZoom OnGalleryZoom;

	// Both paths own their resource for the full widget lifetime. Masks use the persistent texture
	// path; surface materials temporarily retain the legacy pooled handle.
	TSharedPtr<FAssetThumbnail> Thumbnail;
	TStrongObjectPtr<UTexture2D> ThumbnailTexture;
	TUniquePtr<FSlateBrush> ThumbnailBrush;
	bool bPressed = false;
};
