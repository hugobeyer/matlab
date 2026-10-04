// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Gallery/SMixtormatSurfaceCard.h"

#include "AssetThumbnail.h"
#include "InputCoreTypes.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "UI/Menus/SMixtormatHelp.h"
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
			// No row under the swatch: the material name is not printed below the thumbnail. It is
			// named in the hover popover instead, on the same host the buttons use for their help.
			SNew(SMixtormatHelp)
			.Text(DisplayName)
			// Never two popovers at once: the context menu is the card's own, and this one is
			// already open whenever the pointer that opens it is still resting on the swatch.
			.Enabled_Lambda([this]()
			{
				return !ContextAnchor.IsValid() || !ContextAnchor->IsOpen();
			})
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
