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
#include "Widgets/SNullWidget.h"

// The floating panels: the placement cycles, the shared stack both panels float in, the fronting
// that decides which one a press lands on, and the drag/resize interaction that moves them.
//
// The geometry itself is shared with the left panel and lives in SMixtormatOverlayPanel.cpp; this
// file is the workspace wiring around it.

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
	if (bIsBaking)
	{
		return FReply::Handled();
	}
	switch (LeftPanelPlacement)
	{
	case ELeftPanelPlacement::Docked: LeftPanelPlacement = ELeftPanelPlacement::Overlay; break;
	case ELeftPanelPlacement::Overlay: LeftPanelPlacement = ELeftPanelPlacement::Hidden; break;
	case ELeftPanelPlacement::Hidden: LeftPanelPlacement = ELeftPanelPlacement::Docked; break;
	}
	ApplyLeftPanelPlacement();
	return FReply::Handled();
}

void SMixtormat::ApplyLeftPanelPlacement()
{

	LeftPanelOverlay.bFloating = LeftPanelPlacement == ELeftPanelPlacement::Overlay;
	if (LeftPanelOverlay.bFloating)
	{
		// The layer stack travels alone; the pinned rail and selected page remain in the Preview overlay.
		MixtormatOverlay::Place(LeftPanelOverlay, LeftPanel, GetPreviewViewportBounds(),
			MixtormatTokens::LayerStackWidth, false);
		MixtormatOverlay::Clamp(LeftPanelOverlay, LeftPanel, GetPreviewViewportBounds());
	}
	LeftPanelDockHost->SetContent(SNullWidget::NullWidget);
	LeftPanelOverlayHost->SetContent(SNullWidget::NullWidget);
	(LeftPanelOverlay.bFloating ? LeftPanelOverlayHost : LeftPanelDockHost)
		->SetContent(LeftPanel.ToSharedRef());
	SyncLeftCellPage();
}

void SMixtormat::SyncLeftCellPage()
{
	// The cell can only show the layer stack while it is docked; otherwise the rail selects the
	// last page that can live there, so the highlight always matches what the cell holds.
	if (LeftPanelPlacement == ELeftPanelPlacement::Docked)
	{
		ShowLeftPage(0);
	}
	else if (LeftTabIndex == 0)
	{
		ShowLeftPage(LastNonLayersPage);
	}
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
	// A frame per panel: the slot padding is the panel's position, so the frame owns the geometry
	// and the host owns the size. Both frames are self-hit-test-invisible -- empty viewport must
	// still reach the viewport underneath -- while the hosts stay hit-testable.
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

	LeftPanelOverlayFrame = SNew(SBox)
		.Padding_Lambda([this]()
		{
			MixtormatOverlay::Clamp(LeftPanelOverlay, LeftPanel, GetPreviewViewportBounds());
			return FMargin(LeftPanelOverlay.Position.X, LeftPanelOverlay.Position.Y, 0.0f, 0.0f);
		})
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SAssignNew(LeftPanelOverlayHost, SBox)
			.WidthOverride_Lambda([this]() { return LeftPanelOverlay.Size.X; })
			.HeightOverride_Lambda([this]()
			{
				return MixtormatOverlay::GetHeight(LeftPanelOverlay, LeftPanel, GetPreviewViewportBounds());
			})
			.Clipping(EWidgetClipping::ClipToBounds)
			.Visibility_Lambda([this]()
			{
				return LeftPanelPlacement == ELeftPanelPlacement::Overlay
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[LeftPanelPlacement == ELeftPanelPlacement::Overlay
				? LeftPanel.ToSharedRef() : SNullWidget::NullWidget]
		];

	// Back slot first, front slot last: an SOverlay paints and hit-tests in slot order, so the
	// front slot is the panel a press lands on.
	bAppliedLeftPanelInFront = bLeftPanelInFront;
	return SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SAssignNew(FloatingPanelBackSlot, SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[bLeftPanelInFront ? InspectorOverlayFrame.ToSharedRef() : LeftPanelOverlayFrame.ToSharedRef()]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(FloatingPanelFrontSlot, SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[bLeftPanelInFront ? LeftPanelOverlayFrame.ToSharedRef() : InspectorOverlayFrame.ToSharedRef()]
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

void SMixtormat::BringFloatingPanelToFront(const bool bLeftPanel)
{
	if (bLeftPanelInFront == bLeftPanel)
	{
		return;
	}
	bLeftPanelInFront = bLeftPanel;
	// Deferred to the next tick: reparenting the frames mid-event would invalidate the widget path
	// the press is still travelling down.
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		ApplyFloatingPanelOrder();
		return EActiveTimerReturnType::Stop;
	}));
}

void SMixtormat::ApplyFloatingPanelOrder()
{
	if (!FloatingPanelBackSlot.IsValid() || !FloatingPanelFrontSlot.IsValid()
		|| !InspectorOverlayFrame.IsValid() || !LeftPanelOverlayFrame.IsValid()
		|| bAppliedLeftPanelInFront == bLeftPanelInFront)
	{
		return;
	}
	bAppliedLeftPanelInFront = bLeftPanelInFront;
	FloatingPanelBackSlot->SetContent(
		bLeftPanelInFront ? InspectorOverlayFrame.ToSharedRef() : LeftPanelOverlayFrame.ToSharedRef());
	FloatingPanelFrontSlot->SetContent(
		bLeftPanelInFront ? LeftPanelOverlayFrame.ToSharedRef() : InspectorOverlayFrame.ToSharedRef());
}

FReply SMixtormat::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Local = GetPreviewViewportLocalPosition(MouseEvent.GetScreenSpacePosition());
		const bool bLeftHit = LeftPanelPlacement == ELeftPanelPlacement::Overlay
			&& MixtormatOverlay::IsHit(LeftPanelOverlay, Local);
		const bool bInspectorHit = InspectorPlacement == EInspectorPlacement::Overlay
			&& MixtormatOverlay::IsHit(InspectorOverlay, Local);
		// An overlap belongs to the panel already in front; a press on the other one brings it
		// forward. The press itself is left unhandled so it still reaches the control under it.
		if (bLeftHit != bInspectorHit)
		{
			BringFloatingPanelToFront(bLeftHit);
		}
		// A press outside the quick controls dismisses them, and is left unhandled so it still
		// reaches whatever it landed on.
		if (bQuickControlsOpen
			&& (Local.X < QuickControlsPosition.X
				|| Local.X > QuickControlsPosition.X + QuickControlsSize.X
				|| Local.Y < QuickControlsPosition.Y
				|| Local.Y > QuickControlsPosition.Y + QuickControlsSize.Y))
		{
			CloseQuickControls();
		}
	}
	return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMixtormat::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
		// The front panel is asked first: an overlap belongs to it, and its own chrome takes the
		// press before the panel behind is considered.
		const int32 Order[2] = { bLeftPanelInFront ? 1 : 0, bLeftPanelInFront ? 0 : 1 };
		for (const int32 Index : Order)
		{
			FMixtormatOverlayPanelState& State = Index == 0 ? InspectorOverlay : LeftPanelOverlay;
			const bool bFloating = Index == 0
				? InspectorPlacement == EInspectorPlacement::Overlay
				: LeftPanelPlacement == ELeftPanelPlacement::Overlay;
			if (!bFloating)
			{
				continue;
			}
			// Top corner resize targets take priority over the header's drag area.
			if (const int32 Corner = MixtormatOverlay::HitResizeGrip(State, ScreenPosition); Corner != INDEX_NONE)
			{
				MixtormatOverlay::BeginInteraction(State, GetPreviewViewportLocalPosition(ScreenPosition), Corner);
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
			if (const TSharedPtr<SWidget> Header = State.Header.Pin();
				Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				MixtormatOverlay::BeginInteraction(State, GetPreviewViewportLocalPosition(ScreenPosition), INDEX_NONE);
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMixtormat::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (InspectorOverlay.bDragging || InspectorOverlay.bResizing)
	{
		MixtormatOverlay::UpdateInteraction(InspectorOverlay,
			GetPreviewViewportLocalPosition(MouseEvent.GetScreenSpacePosition()), GetPreviewViewportBounds());
		return FReply::Handled();
	}
	if (LeftPanelOverlay.bDragging || LeftPanelOverlay.bResizing)
	{
		MixtormatOverlay::UpdateInteraction(LeftPanelOverlay,
			GetPreviewViewportLocalPosition(MouseEvent.GetScreenSpacePosition()), GetPreviewViewportBounds());
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

FReply SMixtormat::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (InspectorOverlay.bDragging || InspectorOverlay.bResizing)
	{
		MixtormatOverlay::CancelInteraction(InspectorOverlay);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (LeftPanelOverlay.bDragging || LeftPanelOverlay.bResizing)
	{
		MixtormatOverlay::CancelInteraction(LeftPanelOverlay);
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
	MixtormatOverlay::CancelInteraction(InspectorOverlay);
	MixtormatOverlay::CancelInteraction(LeftPanelOverlay);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FCursorReply SMixtormat::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const FVector2D ScreenPosition = CursorEvent.GetScreenSpacePosition();
	const int32 Order[2] = { bLeftPanelInFront ? 1 : 0, bLeftPanelInFront ? 0 : 1 };
	for (const int32 Index : Order)
	{
		const FMixtormatOverlayPanelState& State = Index == 0 ? InspectorOverlay : LeftPanelOverlay;
		const bool bFloating = Index == 0
			? InspectorPlacement == EInspectorPlacement::Overlay
			: LeftPanelPlacement == ELeftPanelPlacement::Overlay;
		if (!bFloating)
		{
			continue;
		}
		const int32 Corner = State.bResizing
			? State.ResizeCorner : MixtormatOverlay::HitResizeGrip(State, ScreenPosition);
		if (Corner != INDEX_NONE)
		{
			// A corner shows the diagonal it moves; a side grip shows the single axis.
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
		if (const TSharedPtr<SWidget> Header = State.Header.Pin();
			Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return FCursorReply::Cursor(EMouseCursor::GrabHand);
		}
	}
	return SCompoundWidget::OnCursorQuery(MyGeometry, CursorEvent);
}

#undef LOCTEXT_NAMESPACE
