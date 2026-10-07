// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatOverlayPanel.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWidget.h"

// The floating panels' shared geometry: placement, clamping, auto-fit height and the drag/resize
// interaction. The Inspector and the left panel both run through here; nothing in this file knows
// which panel it is moving.

namespace
{
	// The target remains hit-testable at rest; only its corner outline appears on hover.
	class SMixtormatOverlayResizeCorner : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatOverlayResizeCorner) : _Right(false), _Bottom(true) {}
			SLATE_ARGUMENT(bool, Right)
			SLATE_ARGUMENT(bool, Bottom)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bRight = InArgs._Right;
			bBottom = InArgs._Bottom;
		}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			return FVector2D(MixtormatTokens::OverlayPanelGripSize);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			if (IsHovered())
			{
				const float Thickness = MixtormatTokens::InspectorHairlineThickness;
				const FVector2f Size(AllottedGeometry.GetLocalSize());
				const float EdgeX = bRight ? Size.X - Thickness : Thickness;
				const float EdgeY = bBottom ? Size.Y - Thickness : Thickness;
				const TArray<FVector2f> Corner = {
					FVector2f(EdgeX, bBottom ? Thickness : Size.Y - Thickness),
					FVector2f(EdgeX, EdgeY),
					FVector2f(bRight ? Thickness : Size.X - Thickness, EdgeY)
				};
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
					Corner, ESlateDrawEffect::None,
					FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)
						* InWidgetStyle.GetColorAndOpacityTint(), true, Thickness);
			}
			return LayerId;
		}

	private:
		bool bRight = false;
		bool bBottom = true;
	};
}

float MixtormatOverlay::GetHeight(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds)
{
	// Frozen during a gesture: auto-fit must not fight the drag, and a resize has already made the
	// height explicit.
	if (!State.bHeightAuto || State.bDragging || State.bResizing)
	{
		return static_cast<float>(State.Size.Y);
	}
	// The panel's own desired size: identity row and banner plus the active scroll content, at the
	// width the host has given it. Measuring the host instead would read the height override back
	// and the panel could never shrink.
	const double Content = Panel.IsValid() ? Panel->GetDesiredSize().Y : 0.0;
	const double Available = Bounds.Y > 0.0
		? FMath::Max(0.0, Bounds.Y - MixtormatTokens::OverlayPanelInset * 2.0)
		: 0.0;
	if (Content <= 0.0)
	{
		// Not laid out yet (first entry after a rebuild): keep the last known height rather than
		// collapsing the panel to nothing for a frame.
		return static_cast<float>(State.Size.Y > 0.0 ? State.Size.Y : Available);
	}
	State.Size.Y = Available > 0.0 ? FMath::Min(Content, Available) : Content;
	return static_cast<float>(State.Size.Y);
}

void MixtormatOverlay::Place(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds, const float InitialWidth, const bool bFromRightEdge)
{
	if (State.bPlaced)
	{
		return;
	}
	const float Inset = MixtormatTokens::OverlayPanelInset;
	State.Size.X = InitialWidth;
	State.Size.Y = 0.0f;
	State.bHeightAuto = true;
	// Measured before placing, so the first entry is already the content's height rather than the
	// viewport's.
	GetHeight(State, Panel, Bounds);
	State.Position = FVector2D(
		bFromRightEdge ? FMath::Max<double>(Inset, Bounds.X - State.Size.X - Inset) : Inset,
		Inset);
	State.bPlaced = true;
}

void MixtormatOverlay::Clamp(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds)
{
	if (Bounds.X <= 0.0f || Bounds.Y <= 0.0f)
	{
		return;
	}
	State.Size.X = FMath::Clamp(State.Size.X,
		FMath::Min<double>(MixtormatTokens::OverlayPanelMinWidth, Bounds.X), Bounds.X);
	if (State.bHeightAuto)
	{
		GetHeight(State, Panel, Bounds);
	}
	else
	{
		State.Size.Y = FMath::Clamp(State.Size.Y,
			FMath::Min<double>(MixtormatTokens::OverlayPanelMinHeight, Bounds.Y), Bounds.Y);
	}
	State.Position = FVector2D(
		FMath::Clamp(State.Position.X, 0.0f, FMath::Max(0.0f, Bounds.X - State.Size.X)),
		FMath::Clamp(State.Position.Y, 0.0f, FMath::Max(0.0f, Bounds.Y - State.Size.Y)));
}

void MixtormatOverlay::FitHeight(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds)
{
	State.bHeightAuto = true;
	GetHeight(State, Panel, Bounds);
	Clamp(State, Panel, Bounds);
}

int32 MixtormatOverlay::HitResizeCorner(const FMixtormatOverlayPanelState& State, const FVector2D& ScreenPosition)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(State.ResizeGrips); ++Index)
	{
		const TSharedPtr<SWidget> Grip = State.ResizeGrips[Index].Pin();
		if (Grip.IsValid() && Grip->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool MixtormatOverlay::IsHit(const FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition)
{
	return LocalPosition.X >= State.Position.X && LocalPosition.X <= State.Position.X + State.Size.X
		&& LocalPosition.Y >= State.Position.Y && LocalPosition.Y <= State.Position.Y + State.Size.Y;
}

void MixtormatOverlay::BeginInteraction(FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition, const int32 ResizeCorner)
{
	State.ResizeCorner = ResizeCorner;
	State.bResizing = ResizeCorner != INDEX_NONE;
	State.bDragging = !State.bResizing;
	// A resize gesture makes the height explicit: the user has said how tall the panel is, and
	// auto-fit must not take it back (D23).
	if (State.bResizing)
	{
		State.bHeightAuto = false;
	}
	State.DragOrigin = LocalPosition;
	State.PositionAtDragStart = State.Position;
	State.SizeAtDragStart = State.Size;
}

void MixtormatOverlay::UpdateInteraction(FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition, const FVector2D& Bounds)
{
	const FVector2D Delta = LocalPosition - State.DragOrigin;
	if (State.bResizing)
	{
		const bool bLeft = State.ResizeCorner % 2 == 0;
		const bool bTop = State.ResizeCorner < 2;
		// The opposite corner stays fixed, including at the minimum size and viewport edges.
		const FVector2D Anchor = State.PositionAtDragStart + FVector2D(
			bLeft ? State.SizeAtDragStart.X : 0.0f,
			bTop ? State.SizeAtDragStart.Y : 0.0f);
		const FVector2D Maximum(
			FMath::Max(0.0f, bLeft ? Anchor.X : Bounds.X - Anchor.X),
			FMath::Max(0.0f, bTop ? Anchor.Y : Bounds.Y - Anchor.Y));
		State.Size = FVector2D(
			FMath::Clamp(State.SizeAtDragStart.X + (bLeft ? -Delta.X : Delta.X),
				FMath::Min<double>(MixtormatTokens::OverlayPanelMinWidth, Maximum.X), Maximum.X),
			FMath::Clamp(State.SizeAtDragStart.Y + (bTop ? -Delta.Y : Delta.Y),
				FMath::Min<double>(MixtormatTokens::OverlayPanelMinHeight, Maximum.Y), Maximum.Y));
		State.Position = Anchor - FVector2D(
			bLeft ? State.Size.X : 0.0f,
			bTop ? State.Size.Y : 0.0f);
	}
	else
	{
		State.Position = State.PositionAtDragStart + Delta;
	}
	// The panel is null here on purpose: a gesture freezes auto-fit, so clamping only needs the
	// explicit size and the bounds.
	Clamp(State, TSharedPtr<SWidget>(), Bounds);
}

void MixtormatOverlay::CancelInteraction(FMixtormatOverlayPanelState& State)
{
	State.bDragging = false;
	State.bResizing = false;
	State.ResizeCorner = INDEX_NONE;
}

TSharedRef<SWidget> MixtormatOverlay::MakeResizeCorner(FMixtormatOverlayPanelState& State, const int32 Index, const FText& ToolTip)
{
	return SAssignNew(State.ResizeGrips[Index], SBox)
		.WidthOverride(MixtormatTokens::OverlayPanelGripSize)
		.HeightOverride(MixtormatTokens::OverlayPanelGripSize)
		.ToolTipText(ToolTip)
		.Visibility_Lambda([&State]()
		{
			return State.bFloating ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatOverlayResizeCorner)
			.Right(Index % 2 != 0)
			.Bottom(Index >= 2)
		];
}
