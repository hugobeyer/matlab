#pragma once

// Where a drag can be released, and what happens when it is.
//
// Moved out of SMixtormatInternal.h unchanged. A drop target wraps a row rather than being part
// of it: the row is the drag *source*, and keeping the two apart is what lets the same visual row
// accept a mask, a layer and a child without knowing about any of them.
//
// The child target is new -- it replaces the OnDragOver/OnDrop half of the old
// SMixtormatChildStackItem, whose other half (the grip, the ellipsis button) the design deleted.

#include "CoreMinimal.h"
#include "Style/MixtormatStyle.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceDropped,
	FText,
	FSoftObjectPath);
DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatLayerDropped,
	int32,
	int32);
DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatMaskDropped,
	int32,
	FSoftObjectPath);
DECLARE_DELEGATE_RetVal_ThreeParams(
	FReply,
	FOnMixtormatChildReordered,
	int32,
	int32,
	int32);
// Source layer, source child, destination layer, destination child. A cross-layer drop is a move,
// not a reorder, so it carries the layer it came from -- which a reorder never had to.
DECLARE_DELEGATE_RetVal_FourParams(
	FReply,
	FOnMixtormatChildMovedToLayer,
	int32,
	int32,
	int32,
	int32);

class SMixtormatLayerRowDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerRowDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMixtormatLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMixtormatMaskDropped, OnMaskDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetLayerIndex = InArgs._TargetLayerIndex;
		OnLayerDropped = InArgs._OnLayerDropped;
		OnMaskDropped = InArgs._OnMaskDropped;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bMaskDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.CompactRowValidDrop")))
			]
		];
	}

	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		if (TargetLayerIndex != INDEX_NONE)
		{
			if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(
					LOCTEXT("ReleaseMaskLayer", "Release to append this mask"),
					FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			}
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		bMaskDragOver = false;
		if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex != INDEX_NONE ? FReply::Handled() : FReply::Unhandled();
		}
		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		// Any layer may be dragged to any position, the bottom included. These tests are
		// validity only -- both indices default to INDEX_NONE -- and no longer refuse row 0.
		return Operation.IsValid()
			&& Operation->SourceLayerIndex != INDEX_NONE
			&& TargetLayerIndex != INDEX_NONE
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMixtormatMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex != INDEX_NONE && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		if (!Operation.IsValid()
			|| Operation->SourceLayerIndex == INDEX_NONE
			|| TargetLayerIndex == INDEX_NONE
			|| !OnLayerDropped.IsBound())
		{
			return FReply::Unhandled();
		}
		return OnLayerDropped.Execute(Operation->SourceLayerIndex, TargetLayerIndex);
	}

private:
	int32 TargetLayerIndex = INDEX_NONE;
	FOnMixtormatLayerDropped OnLayerDropped;
	FOnMixtormatMaskDropped OnMaskDropped;
	bool bMaskDragOver = false;
};

class SMixtormatLayerDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnMixtormatSurfaceDropped, OnSurfaceDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnSurfaceDropped = InArgs._OnSurfaceDropped;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		return Event.GetOperationAs<FMixtormatSurfaceDragDropOp>().IsValid()
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>();
		return Operation.IsValid() && OnSurfaceDropped.IsBound()
			? OnSurfaceDropped.Execute(Operation->DisplayName, Operation->SurfacePath)
			: FReply::Unhandled();
	}

private:
	FOnMixtormatSurfaceDropped OnSurfaceDropped;
};

// A child row's drop half.
//
// Takes a child from its own layer as a reorder, and one from another layer as a move. Both land on
// the row that was dropped on, so where a child ends up is where it was aimed -- nothing here
// reorders anything the drop did not ask for.
class SMixtormatChildDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatChildDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(int32, ChildIndex)
		SLATE_EVENT(FOnMixtormatChildReordered, OnChildReordered)
		SLATE_EVENT(FOnMixtormatChildMovedToLayer, OnChildMovedToLayer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		LayerIndex = InArgs._LayerIndex;
		ChildIndex = InArgs._ChildIndex;
		OnChildReordered = InArgs._OnChildReordered;
		OnChildMovedToLayer = InArgs._OnChildMovedToLayer;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatChildDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatChildDragDropOp>();
		if (!Operation.IsValid())
		{
			return FReply::Unhandled();
		}
		// A drop onto the row it started from is the only one with nothing to do.
		return Operation->LayerIndex != LayerIndex || Operation->ChildIndex != ChildIndex
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatChildDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatChildDragDropOp>();
		if (!Operation.IsValid())
		{
			return FReply::Unhandled();
		}
		if (Operation->LayerIndex == LayerIndex)
		{
			return OnChildReordered.IsBound()
				? OnChildReordered.Execute(LayerIndex, Operation->ChildIndex, ChildIndex)
				: FReply::Unhandled();
		}
		return OnChildMovedToLayer.IsBound()
			? OnChildMovedToLayer.Execute(Operation->LayerIndex, Operation->ChildIndex, LayerIndex, ChildIndex)
			: FReply::Unhandled();
	}

private:
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	FOnMixtormatChildReordered OnChildReordered;
	FOnMixtormatChildMovedToLayer OnChildMovedToLayer;
};

#undef LOCTEXT_NAMESPACE
