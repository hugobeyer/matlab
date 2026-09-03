#pragma once

// The drag operations the layer stack and the library speak in.
//
// Moved out of SMixtormatInternal.h unchanged: these are working behaviour, and the UI migration
// re-skins what carries them, not what they do. Each one is a payload plus the ghost that follows
// the cursor -- the ghost is built here rather than by the dragging widget so that a surface
// dragged from the gallery looks the same as one dragged from anywhere else.

#include "CoreMinimal.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

class FMixtormatSurfaceDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatSurfaceDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath SurfacePath;

	static TSharedRef<FMixtormatSurfaceDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InSurfacePath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatSurfaceDragDropOp> Operation =
			MakeShared<FMixtormatSurfaceDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->SurfacePath = InSurfacePath;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("DropSurfaceLayer", "Add {0} to Layers"),
			InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(MixtormatPalette::ThumbnailPlaceholder())
			.Size(FVector2D(
				MixtormatTokens::DragGhostThumbnailSize,
				MixtormatTokens::DragGhostThumbnailSize));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(
				ThumbnailAsset,
				40,
				40,
				ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}

		Operation->DecoratorWidget =
			SNew(SBorder)
			.RenderOpacity(MixtormatTokens::DragGhostOpacity)
			.Padding(FMargin(
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(MixtormatTokens::DragGhostPadding)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[SNew(SBox).WidthOverride(MixtormatTokens::DragGhostThumbnailSize).HeightOverride(MixtormatTokens::DragGhostThumbnailSize)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(InDisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("CreateLayerGhost", "Create material layer")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
					]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

class FMixtormatMaskDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatMaskDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath MaskPath;

	static TSharedRef<FMixtormatMaskDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InMaskPath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatMaskDragDropOp> Operation = MakeShared<FMixtormatMaskDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->MaskPath = InMaskPath;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("AddMaskDrag", "Add {0} to a layer"), InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(MixtormatPalette::ThumbnailPlaceholder())
			.Size(FVector2D(
				MixtormatTokens::DragGhostThumbnailSize,
				MixtormatTokens::DragGhostThumbnailSize));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(ThumbnailAsset, 40, 40, ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(MixtormatTokens::DragGhostOpacity)
			.Padding(FMargin(
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(MixtormatTokens::DragGhostPadding)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(MixtormatTokens::DragGhostThumbnailSize).HeightOverride(MixtormatTokens::DragGhostThumbnailSize)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(InDisplayName)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

class FMixtormatChildDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatChildDragDropOp, FDecoratedDragDropOp)

	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;

	static TSharedRef<FMixtormatChildDragDropOp> New(
		const int32 InLayerIndex,
		const int32 InChildIndex,
		const FText& Name)
	{
		TSharedRef<FMixtormatChildDragDropOp> Operation = MakeShared<FMixtormatChildDragDropOp>();
		Operation->LayerIndex = InLayerIndex;
		Operation->ChildIndex = InChildIndex;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("ReorderChildDrag", "Move {0}"), Name);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(MixtormatTokens::DragGhostOpacity)
			.Padding(FMargin(
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(MixtormatTokens::DragGhostPadding, MixtormatTokens::DragGhostShadowInset))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f)[SNew(STextBlock).Text(Name)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

class FMixtormatLayerDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatLayerDragDropOp, FDecoratedDragDropOp)

	int32 SourceLayerIndex = INDEX_NONE;

	static TSharedRef<FMixtormatLayerDragDropOp> New(
		const int32 InSourceLayerIndex,
		const FText& DisplayName)
	{
		TSharedRef<FMixtormatLayerDragDropOp> Operation =
			MakeShared<FMixtormatLayerDragDropOp>();
		Operation->SourceLayerIndex = InSourceLayerIndex;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("MoveLayerDrag", "Move {0}"),
			DisplayName);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(MixtormatTokens::DragGhostOpacity)
			.Padding(FMargin(
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				MixtormatTokens::DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(MixtormatTokens::DragGhostPadding, MixtormatTokens::DragGhostShadowOffsetY))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(DisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

#undef LOCTEXT_NAMESPACE
