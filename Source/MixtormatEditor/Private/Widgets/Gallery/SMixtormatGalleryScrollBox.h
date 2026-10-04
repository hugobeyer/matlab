// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Gallery/MixtormatGalleryDelegates.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "Widgets/Layout/SScrollBox.h"

// Handles gallery zoom even when the cursor is over empty scroll-box background. Tile widgets
// still consume the same gesture themselves, so bubbling never applies one wheel step twice.
class SMixtormatGalleryScrollBox final : public SScrollBox
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGalleryScrollBox)
		: _Orientation(Orient_Vertical) {}
		SLATE_ARGUMENT(EOrientation, Orientation)
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnMixtormatSurfaceGalleryZoom, OnGalleryZoom)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnGalleryZoom = InArgs._OnGalleryZoom;
		// The prototype gallery scrolls without inset edge fades; retain all scrolling behavior.
		GalleryStyle = FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>(TEXT("ScrollBox"));
		GalleryStyle.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource())
			.SetLeftShadowBrush(FSlateNoResource()).SetRightShadowBrush(FSlateNoResource());
		SScrollBox::Construct(
			SScrollBox::FArguments()
			.Style(&GalleryStyle)
			.Orientation(InArgs._Orientation)
			+ SScrollBox::Slot()
			[
				InArgs._Content.Widget
			]);
	}

	virtual FReply OnMouseWheel(
		const FGeometry& Geometry,
		const FPointerEvent& Event) override
	{
		const FReply ZoomReply = MixtormatGallery::HandleZoomWheel(Event, OnGalleryZoom);
		return ZoomReply.IsEventHandled() ? ZoomReply : SScrollBox::OnMouseWheel(Geometry, Event);
	}

private:
	// SScrollBox keeps this address; the adapter must live as long as the widget.
	FScrollBoxStyle GalleryStyle;
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
};
