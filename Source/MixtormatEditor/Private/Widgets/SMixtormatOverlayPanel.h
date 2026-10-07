// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;

// The grips a floating panel resizes from. Corners first, then the two side edges: the top and
// bottom edges are deliberately absent -- the top edge is the drag handle and the bottom edge
// carries the Fit chevron, so a grip there would fight both.
enum class EMixtormatOverlayGrip : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	Left,
	Right,
	Count
};

// One floating panel's geometry and gesture state.
//
// The Inspector and the left panel float the same way -- dragged by a header, resized from four
// corners, clamped to the viewport, auto-fit height until a corner drag makes it explicit -- so
// the state and the code that moves it are shared. Each panel owns its own instance: two panels
// can float at once, and neither may inherit the other's position, size or gesture.
struct FMixtormatOverlayPanelState
{
	// Top-left corner in viewport-local pixels, and the panel's size. Retained across theme
	// reconstruction, which is why they live here and not on the widgets.
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	// False until the panel first floats; the first entry places it, after that the user's own
	// geometry stands, re-clamped.
	bool bPlaced = false;
	// Auto height follows the panel's content, capped at the viewport. A corner drag makes it
	// explicit and only Fit height returns it to auto; foldout collapse never resets it (D23).
	bool bHeightAuto = true;
	// True while the panel floats. The grips and the header drag only exist in Overlay.
	bool bFloating = false;
	bool bDragging = false;
	bool bResizing = false;
	int32 ResizeCorner = INDEX_NONE;
	FVector2D DragOrigin = FVector2D::ZeroVector;
	FVector2D PositionAtDragStart = FVector2D::ZeroVector;
	FVector2D SizeAtDragStart = FVector2D::ZeroVector;
	// The drag handle: the Inspector's identity row, the left panel's grab margin.
	TWeakPtr<SWidget> Header;
	// Indexed by EMixtormatOverlayGrip.
	TWeakPtr<SWidget> ResizeGrips[static_cast<int32>(EMixtormatOverlayGrip::Count)];
};

// The shared floating-panel machinery. Bounds are the viewport-local rect the panels float in;
// positions and sizes are in the same units, so DPI scaling and viewport movement cannot
// misplace a panel.
namespace MixtormatOverlay
{
	// The height the panel should be drawn at: the explicit height when the user has set one,
	// otherwise the panel's own content height capped at the viewport. Also refreshes
	// State.Size.Y while the height is auto, so clamping and drag snapshots read the same value.
	float GetHeight(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds);

	// First entry: inset from the viewport edges by the panel inset token, at the given width and
	// the panel's own content height.
	void Place(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds, float InitialWidth, bool bFromRightEdge);

	// Keeps the panel inside the viewport. Runs on entry, during drag/resize, and whenever the
	// viewport's geometry changes (window, splitter, gallery).
	void Clamp(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds);

	// Returns the height to auto without touching width or position (D23).
	void FitHeight(FMixtormatOverlayPanelState& State, const TSharedPtr<SWidget>& Panel, const FVector2D& Bounds);

	int32 HitResizeGrip(const FMixtormatOverlayPanelState& State, const FVector2D& ScreenPosition);
	bool IsHit(const FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition);
	void BeginInteraction(FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition, int32 ResizeCorner);
	void UpdateInteraction(FMixtormatOverlayPanelState& State, const FVector2D& LocalPosition, const FVector2D& Bounds);
	void CancelInteraction(FMixtormatOverlayPanelState& State);

	// One resize grip: a hit target that draws its outline only on hover. Registered in the state
	// so the workspace can hit-test it.
	TSharedRef<SWidget> MakeResizeGrip(FMixtormatOverlayPanelState& State, int32 Index, const FText& ToolTip);
}
