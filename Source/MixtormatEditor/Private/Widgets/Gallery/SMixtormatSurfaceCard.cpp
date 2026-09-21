// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Gallery/SMixtormatSurfaceCard.h"

#include "AssetThumbnail.h"
#include "InputCoreTypes.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

void SMixtormatSurfaceCard::Construct(const FArguments& InArgs)
{
	DisplayName = InArgs._DisplayName;
	SurfacePath = InArgs._SurfacePath;
	ThumbnailAsset = InArgs._ThumbnailAsset;
	ThumbnailPool = InArgs._ThumbnailPool;
	OnSelected = InArgs._OnSelected;
	OnGalleryZoom = InArgs._OnGalleryZoom;
	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				InArgs._Content.Widget
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.Visibility_Lambda([this]()
				{
					return IsHovered()
						? EVisibility::HitTestInvisible
						: EVisibility::Collapsed;
				})
				[
					InArgs._HoverContent.Widget
				]
			]
		]
	];
}

FReply SMixtormatSurfaceCard::OnPreviewMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (ContextAnchor.IsValid())
		{
			ContextAnchor->SetIsOpen(true);
		}
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (OnSelected.IsBound())
		{
			OnSelected.Execute(DisplayName, SurfacePath);
		}
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMixtormatSurfaceCard::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FReply ZoomReply = MixtormatGallery::HandleZoomWheel(MouseEvent, OnGalleryZoom);
	return ZoomReply.IsEventHandled() ? ZoomReply : SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
}

FReply SMixtormatSurfaceCard::OnDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return FReply::Handled().BeginDragDrop(
		FMixtormatSurfaceDragDropOp::New(
			DisplayName,
			SurfacePath,
			ThumbnailAsset,
			ThumbnailPool));
}
