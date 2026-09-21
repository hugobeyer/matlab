// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Framework/SlateDelegates.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Gallery/MixtormatGalleryDelegates.h"
#include "Widgets/SCompoundWidget.h"

class FAssetThumbnailPool;
class SMenuAnchor;

class SMixtormatMaskCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatMaskCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, MaskPath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMixtormatMaskSelected, OnSelected)
		SLATE_EVENT(FOnMixtormatSurfaceGalleryZoom, OnGalleryZoom)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	FText DisplayName;
	FSoftObjectPath MaskPath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMixtormatMaskSelected OnSelected;
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};
