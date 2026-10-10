// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatOverlayPanel.h"
#include "InputCoreTypes.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SNullWidget.h"

// Inspector overlay placement, drag/resize gestures and gallery drawer interactions.
// The Layers/Library/Global navigation remains docked in the left column;
// only Inspector uses floating-panel geometry from SMixtormatOverlayPanel.cpp.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::ToggleInspectorCollapsed()
{
	if (bIsBaking)
	{
		return FReply::Handled();
	}
	switch (InspectorPlacement)
	{
	case EInspectorPlacement::Docked: InspectorPlacement = EInspectorPlacement::Overlay; break;
	case EInspectorPlacement::Overlay: InspectorPlacement = EInspectorPlacement::Hidden; break;
	case EInspectorPlacement::Hidden: InspectorPlacement = EInspectorPlacement::Docked; break;
	}
	bInspectorCollapsed = InspectorPlacement != EInspectorPlacement::Docked;
	InspectorOverlay.bFloating = InspectorPlacement == EInspectorPlacement::Overlay;
	if (InspectorOverlay.bFloating)
	{
		// First entry places it inset from the viewport's right edge at its content height; after
		// that the user's own geometry stands, re-clamped in case the viewport has changed since.
		MixtormatOverlay::Place(InspectorOverlay, InspectorPanel, GetPreviewViewportBounds(),
			MixtormatTokens::InspectorWidth, true);
		MixtormatOverlay::Clamp(InspectorOverlay, InspectorPanel, GetPreviewViewportBounds());
	}
	// Remove the old parent first; no rebuild means scroll and expansion state stay intact.
	InspectorDockHost->SetContent(SNullWidget::NullWidget);
	InspectorOverlayHost->SetContent(SNullWidget::NullWidget);
	(InspectorOverlay.bFloating ? InspectorOverlayHost : InspectorDockHost)
		->SetContent(InspectorPanel.ToSharedRef());
	return FReply::Handled();
}

FReply SMixtormat::ToggleLeftPanelCollapsed()
{
	if (bIsBaking) { return FReply::Handled(); }
	// L toggles the docked Layers page without creating a floating panel.
	return ShowLeftPage(LeftTabIndex == 0 ? LastNonLayersPage : 0);
}

FVector2D SMixtormat::GetPreviewViewportBounds() const
{
	return PreviewViewports.IsValidIndex(0) && PreviewViewports[0].IsValid()
		? PreviewViewports[0]->GetCachedGeometry().GetLocalSize()
		: FVector2D::ZeroVector;
}

FVector2D SMixtormat::GetPreviewViewportLocalPosition(const FVector2D& ScreenPosition) const
{
	return PreviewViewports.IsValidIndex(0) && PreviewViewports[0].IsValid()
		? PreviewViewports[0]->GetCachedGeometry().AbsoluteToLocal(ScreenPosition)
		: FVector2D::ZeroVector;
}

TSharedRef<SWidget> SMixtormat::BuildFloatingPanelStack()
{
	// Inspector's frame owns the positioning, while its host owns size. Empty
	// viewport remains hit-test-transparent to camera controls.
	InspectorOverlayFrame = SNew(SBox)
		.Padding_Lambda([this]()
		{
			// Re-clamped here rather than on a timer: this is the one place that runs whenever the
			// viewport's geometry changes -- window resize, splitter drag, gallery toggle -- and
			// clamping is idempotent, so a settled layout costs one comparison.
			MixtormatOverlay::Clamp(InspectorOverlay, InspectorPanel, GetPreviewViewportBounds());
			return FMargin(InspectorOverlay.Position.X, InspectorOverlay.Position.Y, 0.0f, 0.0f);
		})
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SAssignNew(InspectorOverlayHost, SBox)
			.WidthOverride_Lambda([this]() { return InspectorOverlay.Size.X; })
			.HeightOverride_Lambda([this]()
			{
				return MixtormatOverlay::GetHeight(InspectorOverlay, InspectorPanel, GetPreviewViewportBounds());
			})
			.Clipping(EWidgetClipping::ClipToBounds)
			.Visibility_Lambda([this]()
			{
				return InspectorPlacement == EInspectorPlacement::Overlay
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[InspectorPlacement == EInspectorPlacement::Overlay
				? InspectorPanel.ToSharedRef() : SNullWidget::NullWidget]
		];

	// Layers always belong to the left column. Only Inspector floats.
	return SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			InspectorOverlayFrame.ToSharedRef()
		];
}


TSharedRef<SWidget> SMixtormat::MakeOverlayFitButton(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel)
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatIconStyle& Icon = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)];
	// The gallery's collapse chevron is the precedent: a small plate floating on the panel's edge,
	// present only when it has something to do. Here that is an explicit height, which only a
	// corner drag creates -- so this appears at the edge the drag just moved.
	return SNew(SBox)
		.WidthOverride(Icon.ButtonSize)
		.HeightOverride(Icon.ButtonSize)
		.Visibility_Lambda([&State]()
		{
			return State.bFloating && !State.bHeightAuto ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatHelp)
			.Text(LOCTEXT("FitOverlayHeightHint", "Fit the panel height to its content. Returns to automatic height; width and position stay as they are."))
			[
				SNew(SButton)
				.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.BottomLibraryCollapseButton")))
				.ContentPadding(0.0f)
				.OnClicked_Lambda([this, &State, Panel]()
			{
				MixtormatOverlay::FitHeight(State, Panel, GetPreviewViewportBounds());
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.WidthOverride(Icon.GlyphSize)
				.HeightOverride(Icon.GlyphSize)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage).Image(MixtormatIcons::ChevronUp())
				]
			]
			]
		];
}

FReply SMixtormat::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
		if (InspectorPlacement == EInspectorPlacement::Overlay)
		{
			if (const int32 Corner = MixtormatOverlay::HitResizeGrip(InspectorOverlay, ScreenPosition);
				Corner != INDEX_NONE)
			{
				MixtormatOverlay::BeginInteraction(InspectorOverlay,
					GetPreviewViewportLocalPosition(ScreenPosition), Corner);
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
			if (const TSharedPtr<SWidget> Header = InspectorOverlay.Header.Pin();
				Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				MixtormatOverlay::BeginInteraction(InspectorOverlay,
					GetPreviewViewportLocalPosition(ScreenPosition), INDEX_NONE);
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}
		if (!bIsBaking && !bBottomLibraryCollapsed && GalleryDrawerHeader.IsValid()
			&& GalleryDrawerHeader->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			bGalleryDrawerResizing = true;
			bGalleryDrawerResizeMoved = false;
			GalleryDrawerResizeOriginScreen = ScreenPosition;
			GalleryDrawerHeightAtResizeStart = GalleryDrawerHeight > 0.0f
				? GalleryDrawerHeight
				: FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInitialHeight;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMixtormat::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bGalleryDrawerResizing)
	{
		bGalleryDrawerResizeMoved |= FVector2D::Distance(
			GalleryDrawerResizeOriginScreen, MouseEvent.GetScreenSpacePosition())
			>= FSlateApplication::Get().GetDragTriggerDistance();
		if (!bGalleryDrawerResizeMoved)
		{
			return FReply::Handled();
		}
		const float DeltaY = GalleryDrawerResizeOriginScreen.Y - MouseEvent.GetScreenSpacePosition().Y;
		const float MaximumHeight = FMath::Max(MixtormatTokens::OverlayPanelMinHeight,
			GetCachedGeometry().GetLocalSize().Y - FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInset * 2.0f);
		GalleryDrawerHeight = FMath::Clamp(GalleryDrawerHeightAtResizeStart + DeltaY,
			MixtormatTokens::OverlayPanelMinHeight, MaximumHeight);
		return FReply::Handled();
	}
	if (!bBottomLibraryCollapsed && !bGalleryDrawerAnimating && !bGalleryDrawerResizing
		&& !MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && GalleryDrawerHost.IsValid())
	{
		const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
		const FGeometry& GalleryGeometry = GalleryDrawerHost->GetCachedGeometry();
		if (GalleryGeometry.IsUnderLocation(ScreenPosition))
		{
			bGalleryPointerInside = true;
		}
		else if (bGalleryPointerInside && !bGalleryPinned)
		{
			const FSlateRect Bounds = GalleryGeometry.GetLayoutBoundingRect();
			const float DistanceX = FMath::Max(FMath::Max(Bounds.Left - ScreenPosition.X, 0.0f), ScreenPosition.X - Bounds.Right);
			const float DistanceY = FMath::Max(FMath::Max(Bounds.Top - ScreenPosition.Y, 0.0f), ScreenPosition.Y - Bounds.Bottom);
			if (FMath::Sqrt(FMath::Square(DistanceX) + FMath::Square(DistanceY))
							> FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerAutoCollapseDistance)
			{
				bGalleryPointerInside = false;
				ToggleBottomLibraryCollapsed();
				return FReply::Handled();
			}
		}
	}
	if (InspectorOverlay.bDragging || InspectorOverlay.bResizing)
	{
		MixtormatOverlay::UpdateInteraction(InspectorOverlay,
			GetPreviewViewportLocalPosition(MouseEvent.GetScreenSpacePosition()), GetPreviewViewportBounds());
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

FReply SMixtormat::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bGalleryDrawerResizing && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const bool bClick = !bGalleryDrawerResizeMoved
			&& FVector2D::Distance(GalleryDrawerResizeOriginScreen, MouseEvent.GetScreenSpacePosition())
				< FSlateApplication::Get().GetDragTriggerDistance();
		bGalleryDrawerResizing = false;
		bGalleryDrawerResizeMoved = false;
		if (bClick && !bIsBaking && GalleryDrawerHeader.IsValid()
			&& GalleryDrawerHeader->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
		{
			ToggleBottomLibraryCollapsed();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InspectorOverlay.bDragging || InspectorOverlay.bResizing)
	{
		MixtormatOverlay::CancelInteraction(InspectorOverlay);
		return FReply::Handled().ReleaseMouseCapture();
	}
	// A gesture cancelled by a rebuild can leave the capture behind; release it here rather than
	// letting it swallow the next press.
	if (HasMouseCapture())
	{
		return FReply::Handled().ReleaseMouseCapture();
	}
	return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SMixtormat::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Alt-tab or a modal mid-drag: drop the interaction rather than follow a mouse that is gone.
	bGalleryDrawerResizing = false;
	bGalleryDrawerResizeMoved = false;
	MixtormatOverlay::CancelInteraction(InspectorOverlay);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FCursorReply SMixtormat::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const FVector2D ScreenPosition = CursorEvent.GetScreenSpacePosition();
	if (!bBottomLibraryCollapsed && GalleryDrawerHeader.IsValid()
		&& GalleryDrawerHeader->GetCachedGeometry().IsUnderLocation(ScreenPosition))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
	}
	if (InspectorPlacement == EInspectorPlacement::Overlay)
	{
		const int32 Corner = InspectorOverlay.bResizing
			? InspectorOverlay.ResizeCorner
			: MixtormatOverlay::HitResizeGrip(InspectorOverlay, ScreenPosition);
		if (Corner != INDEX_NONE)
		{
			switch (static_cast<EMixtormatOverlayGrip>(Corner))
			{
			case EMixtormatOverlayGrip::Left:
			case EMixtormatOverlayGrip::Right:
				return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
			case EMixtormatOverlayGrip::TopLeft:
			case EMixtormatOverlayGrip::BottomRight:
				return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
			default:
				return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
			}
		}
		if (const TSharedPtr<SWidget> Header = InspectorOverlay.Header.Pin();
			Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return FCursorReply::Cursor(EMouseCursor::GrabHand);
		}
	}
	return SCompoundWidget::OnCursorQuery(MyGeometry, CursorEvent);
}

#undef LOCTEXT_NAMESPACE
