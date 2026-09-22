// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Delegates shared by the gallery's scroll container and its two card types. Kept together
// because FOnMixtormatSurfaceGalleryZoom crosses all three widgets -- a card including the scroll
// box header just for that one typedef would be a stranger dependency than this.
//


#include "CoreMinimal.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "UObject/SoftObjectPath.h"

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceSelected,
	FText,
	FSoftObjectPath);
DECLARE_DELEGATE_OneParam(FOnMixtormatSurfaceGalleryZoom, int32);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatMaskSelected, FText, FSoftObjectPath);

namespace MixtormatGallery
{
	// The ctrl+wheel zoom gesture, shared verbatim by the scroll box and both card types: a card
	// still needs to forward the gesture when the cursor is over it rather than the box's own
	// background. FReply::Unhandled() tells the caller to fall through to its own base-class
	// OnMouseWheel, which is why this returns a reply rather than a bool.
	inline FReply HandleZoomWheel(
		const FPointerEvent& Event,
		const FOnMixtormatSurfaceGalleryZoom& OnGalleryZoom)
	{
		const int32 Direction = FMath::Sign(Event.GetWheelDelta());
		if (Event.IsControlDown() && Direction != 0 && OnGalleryZoom.IsBound())
		{
			OnGalleryZoom.Execute(Direction);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
}
