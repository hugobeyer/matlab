// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Gallery/SMixtormatMaskCard.h"

#include "AssetThumbnail.h"
#include "InputCoreTypes.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Input/SMenuAnchor.h"

void SMixtormatMaskCard::Construct(const FArguments& InArgs)
{
	DisplayName = InArgs._DisplayName;
	MaskPath = InArgs._MaskPath;
	ThumbnailAsset = InArgs._ThumbnailAsset;
	ThumbnailPool = InArgs._ThumbnailPool;
	OnSelected = InArgs._OnSelected;
	OnGalleryZoom = InArgs._OnGalleryZoom;
	// No row under the swatch: the mask name is not printed below the thumbnail. It is named in the
	// hover popover instead, which is the same host the buttons use for their help.
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
			SNew(SMixtormatHelp)
			.Text(DisplayName)
			// Never two popovers at once: the context menu is the card's own, and this one is
			// already open whenever the pointer that opens it is still resting on the swatch.
			.Enabled_Lambda([this]()
			{
				return !ContextAnchor.IsValid() || !ContextAnchor->IsOpen();
			})
			[
				InArgs._Content.Widget
			]
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
