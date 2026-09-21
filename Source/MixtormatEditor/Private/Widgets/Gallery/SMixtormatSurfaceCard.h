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

class SMixtormatSurfaceCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSurfaceCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_NAMED_SLOT(FArguments, HoverContent)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, SurfacePath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMixtormatSurfaceSelected, OnSelected)
		SLATE_EVENT(FOnMixtormatSurfaceGalleryZoom, OnGalleryZoom)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

private:
	FText DisplayName;
	FSoftObjectPath SurfacePath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMixtormatSurfaceSelected OnSelected;
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};
