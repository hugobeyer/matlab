// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Gallery/MixtormatGalleryDelegates.h"
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
		SScrollBox::Construct(
			SScrollBox::FArguments()
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
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
};
