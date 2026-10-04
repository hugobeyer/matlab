// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Gallery/SMixtormatMaskCard.h"
#include "Widgets/Gallery/MixtormatGalleryCaption.h"

#include "AssetThumbnail.h"
#include "InputCoreTypes.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "Widgets/Input/SMenuAnchor.h"

void SMixtormatMaskCard::Construct(const FArguments& InArgs)
{
	DisplayName = InArgs._DisplayName;
	MaskPath = InArgs._MaskPath;
	ThumbnailAsset = InArgs._ThumbnailAsset;
	ThumbnailPool = InArgs._ThumbnailPool;
	OnSelected = InArgs._OnSelected;
	OnGalleryZoom = InArgs._OnGalleryZoom;
	// The caption names the mask on hover rather than permanently. The card is the hovered widget, so
	// this reads as "the pointer is on this swatch" without the tile needing to report anything.
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.Method(EPopupMethod::CreateNewWindow)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			MixtormatGallery::WithCaption(InArgs._Content.Widget, DisplayName,
				TAttribute<EVisibility>::CreateLambda([this]()
				{
					return IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				}))
		]
	];
}

FReply SMixtormatMaskCard::OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (ContextAnchor.IsValid())
		{
			ContextAnchor->SetIsOpen(true);
		}
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnPreviewMouseButtonDown(Geometry, Event);
	}
	if (OnSelected.IsBound())
	{
		OnSelected.Execute(DisplayName, MaskPath);
	}
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatMaskCard::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FReply ZoomReply = MixtormatGallery::HandleZoomWheel(Event, OnGalleryZoom);
	return ZoomReply.IsEventHandled() ? ZoomReply : SCompoundWidget::OnMouseWheel(Geometry, Event);
}

FReply SMixtormatMaskCard::OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return FReply::Handled().BeginDragDrop(
		FMixtormatMaskDragDropOp::New(DisplayName, MaskPath, ThumbnailAsset, ThumbnailPool));
}
