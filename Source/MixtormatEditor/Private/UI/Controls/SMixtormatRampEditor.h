// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// The one ramp editor both ramp modules share.
//
// A ramp is an ordered list of points over one X domain, and everything about where a point is and
// how it is edited is the same whether the point carries a scalar or a colour: the signed domain,
// zero at the centre, add/move/delete/reset, the grid, the interpolation toolbar and the focus
// behaviour. Only the payload and its painting differ, so only those are virtual here.
//
//     shared ramp interaction/editor
//       ├─ Scalar Ramp: X -> float
//       └─ Color Ramp:  X -> color
class SMixtormatRampEditorBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatRampEditorBase)
		: _Height(MixtormatTokens::ScalarRampHeight)
		, _DomainMin(0.0f)
		, _DomainMax(1.0f)
	{}
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, DomainMin)
		SLATE_ARGUMENT(float, DomainMax)
		SLATE_EVENT(FSimpleDelegate, OnBeginInteractiveEdit)
		SLATE_EVENT(FSimpleDelegate, OnEndInteractiveEdit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

protected:
	// ---- Derived contract ---------------------------------------------------------------------
	virtual int32 GetPointCount() const = 0;
	virtual int32 GetMaxPoints() const = 0;
	virtual float GetPointX(int32 Index) const = 0;
	virtual void SetPointX(int32 Index, float X) = 0;
	// Inserts a point and returns its index. GraphY is already in ramp space.
	virtual int32 InsertPointAt(float X, float GraphY) = 0;
	virtual void RemovePoint(int32 Index) = 0;
	virtual void ResetPoints() = 0;
	// Escape-arm bookkeeping on drag end; a ramp with no Y escape has nothing to do.
	virtual void FinishPointDrag() {}
	// Captures whatever a drag needs to stay stable (view band, start value, modifiers).
	virtual void BeginPointDrag(int32 Index, bool bCreated, const FGeometry& Geometry, const FPointerEvent& Event) {}
	// Applies one drag step. GraphX/GraphY are already in ramp space.
	virtual void ApplyPointDrag(int32 Index, float GraphX, float GraphY, const FVector2f& ScreenPos) = 0;
	// Swaps parallel per-point state when two adjacent points cross during a drag. The default
	// implementation does nothing; a ramp with parallel state (e.g. EscapeArms) overrides it.
	virtual void SwapPointState(int32 IndexA, int32 IndexB) {}
	// Called after a point is removed so the editor can keep its selection on a sensible neighbour.
	virtual void NotifyPointRemoved(int32 RemovedIndex) {}
	// Called after a point is inserted so the editor can select the new point.
	virtual void NotifyPointInserted(int32 InsertedIndex) {}
	// Returns true when the editor allows the dragged point to cross its neighbours during drag.
	// Endpoints are locked by default; interior points cross freely.
	virtual bool AllowsPointCrossing() const { return true; }
	// Returns true when the endpoint at the given index is locked (cannot move past its neighbour).
	virtual bool IsEndpointLocked(int32 Index) const { return Index == 0 || Index == GetPointCount() - 1; }
	// Called after any edit: notify the owner and repaint.
	virtual void OnRampEdited(bool bInteractive) = 0;
	virtual void PaintRampContent(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size) const = 0;
	virtual void PaintPointMarker(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size, int32 Index, bool bActive, bool bHover) const = 0;
	virtual int32 GetInterpolationCount() const = 0;
	virtual int32 GetInterpolation() const = 0;
	virtual void SetInterpolation(int32 Index) = 0;
	virtual FText GetInterpolationLabel(int32 Index) const = 0;
	virtual const FSlateBrush* GetInterpolationIcon(int32 Index) const = 0;
	// The Y band the grid and the point markers are drawn in. A colour ramp keeps a fixed band.
	virtual float GetViewYMin() const { return 0.0f; }
	virtual float GetViewYMax() const { return 1.0f; }
	// The Y band pointer input is mapped through. The scalar ramp freezes it for the duration of a
	// drag so a live auto-zoom cannot move the value under the cursor; painting keeps the live band.
	virtual float GetInputViewYMin() const { return GetViewYMin(); }
	virtual float GetInputViewYMax() const { return GetViewYMax(); }
	virtual void FrameView() {}

	// ---- Shared helpers -----------------------------------------------------------------------
	float XToScreen(const FVector2D& Size, float X) const;
	float ScreenToX(const FVector2D& Size, float ScreenX) const;
	float YToScreen(const FVector2D& Size, float Y) const;
	FVector2f GraphToScreen(const FVector2D& Size, float X, float Y) const;
	FVector2f ScreenToGraph(const FVector2D& Size, const FVector2D& Position) const;
	int32 HitPoint(const FVector2D& Size, const FVector2D& Position) const;
	// Swaps two adjacent points during a drag. Updates payload, parallel state, and every
	// index-based selection so the dragged payload stays selected across the swap.
	void SwapPoints(int32 IndexA, int32 IndexB);
	void PaintGrid(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
		const FVector2D& Size) const;
	void NotifyEdit(bool bInteractive);
	void SelectInterpolation(int32 Index);
	void ResetRamp();
	void BuildLayout();

	float Height = MixtormatTokens::ScalarRampHeight;
	float DomainMin = 0.0f;
	float DomainMax = 1.0f;
	bool bDragging = false;
	int32 DragPoint = INDEX_NONE;
	int32 HoverPoint = INDEX_NONE;
	// Persistent selection. Click selects, dragging keeps it, deletion moves it to a sensible
	// neighbour, and crossing keeps it attached to the dragged payload.
	int32 SelectedPoint = INDEX_NONE;
	FSimpleDelegate OnBeginInteractiveEdit;
	FSimpleDelegate OnEndInteractiveEdit;
};
