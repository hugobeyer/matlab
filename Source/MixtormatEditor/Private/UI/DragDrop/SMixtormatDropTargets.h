// Copyright 2026 Hugo Beyer. All Rights Reserved.

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

// Where in a row the cursor is, and therefore what a release there means.
//
// Before/After rather than Above/Below: RebuildLayerList adds index 0 first, so the top of the
// panel is the front of the array and nothing here has to reason about composite order. The
// user-facing wording stays "above"/"below" -- that is what the stack looks like.
enum class EMixtormatRowDropZone : uint8
{
	None,
	Before,
	After,
	Into
};

namespace MixtormatDropZone
{
	// The cursor's height through the row, 0 at the top edge and 1 at the bottom.
	inline float LocalFraction(const FGeometry& Geometry, const FDragDropEvent& Event)
	{
		const float Height = Geometry.GetLocalSize().Y;
		if (Height <= 0.0f)
		{
			return 0.5f;
		}
		const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		return FMath::Clamp(static_cast<float>(Local.Y) / Height, 0.0f, 1.0f);
	}

	inline EMixtormatRowDropZone ForLayerRow(const FGeometry& Geometry, const FDragDropEvent& Event)
	{
		return LocalFraction(Geometry, Event) < 0.5f
			? EMixtormatRowDropZone::Before
			: EMixtormatRowDropZone::After;
	}

	// A group header carries a third meaning, so its edges are narrower than a layer row's halves.
	inline EMixtormatRowDropZone ForGroupRow(
		const FGeometry& Geometry,
		const FDragDropEvent& Event,
		const bool bAllowInto)
	{
		const float Fraction = LocalFraction(Geometry, Event);
		if (!bAllowInto)
		{
			return Fraction < 0.5f ? EMixtormatRowDropZone::Before : EMixtormatRowDropZone::After;
		}
		if (Fraction < MixtormatTokens::GroupRowIntoZoneFraction)
		{
			return EMixtormatRowDropZone::Before;
		}
		if (Fraction > 1.0f - MixtormatTokens::GroupRowIntoZoneFraction)
		{
			return EMixtormatRowDropZone::After;
		}
		return EMixtormatRowDropZone::Into;
	}

	// The line is drawn on the edge the release would insert against, so what the user sees and
	// what happens are the same fact rather than two that have to be kept in step.
	inline TSharedRef<SWidget> MakeInsertionOverlay(TFunction<EMixtormatRowDropZone()> Zone)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()
			.VAlign(VAlign_Top)
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::DropInsertionLineThickness)
				.Visibility_Lambda([Zone]()
				{
					return Zone() == EMixtormatRowDropZone::Before
						? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				[
					SNew(SImage)
					.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DropInsertionLine")))
				]
			]
			+ SOverlay::Slot()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::DropInsertionLineThickness)
				.Visibility_Lambda([Zone]()
				{
					return Zone() == EMixtormatRowDropZone::After
						? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				[
					SNew(SImage)
					.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DropInsertionLine")))
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([Zone]()
				{
					return Zone() == EMixtormatRowDropZone::Into
						? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.CompactRowValidDrop")))
			];
	}
}

class SMixtormatLayerRowDropTarget final : public SCompoundWidget
{
public:
	// Insertion is expressed as a slot index in the pre-removal array: "put it here", counted
	// before anything has moved. The handler owns the shift that removing the source causes.
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnLayerInsertedAt, int32, int32);
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnGroupInsertedAt, FGuid, int32);
	DECLARE_DELEGATE_RetVal_FourParams(
		FReply, FOnSurfaceInsertedAt, FText, FSoftObjectPath, int32, FGuid);

	SLATE_BEGIN_ARGS(SMixtormatLayerRowDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMixtormatMaskDropped, OnMaskDropped)
		SLATE_EVENT(FOnLayerInsertedAt, OnLayerInsertedAt)
		SLATE_EVENT(FOnGroupInsertedAt, OnGroupInsertedAt)
		SLATE_EVENT(FOnSurfaceInsertedAt, OnSurfaceInsertedAt)
		SLATE_EVENT(FOnMixtormatChildMovedToLayer, OnChildMovedToLayer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetLayerIndex = InArgs._TargetLayerIndex;
		OnMaskDropped = InArgs._OnMaskDropped;
		OnLayerInsertedAt = InArgs._OnLayerInsertedAt;
		OnGroupInsertedAt = InArgs._OnGroupInsertedAt;
		OnSurfaceInsertedAt = InArgs._OnSurfaceInsertedAt;
		OnChildMovedToLayer = InArgs._OnChildMovedToLayer;
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
			+ SOverlay::Slot()
			[
				MixtormatDropZone::MakeInsertionOverlay([this]() { return Zone; })
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
		Zone = EMixtormatRowDropZone::None;
		if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatLayerDragDropOp> Operation = Event.GetOperationAs<FMixtormatLayerDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatGroupDragDropOp> Operation = Event.GetOperationAs<FMixtormatGroupDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatChildDragDropOp> Operation = Event.GetOperationAs<FMixtormatChildDragDropOp>())
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
		if (TargetLayerIndex == INDEX_NONE)
		{
			return FReply::Unhandled();
		}

		// A child dropped on a layer body means "append to this layer". There is no position
		// being chosen, so the whole row lights rather than one of its edges.
		if (const TSharedPtr<FMixtormatChildDragDropOp> ChildOp =
			DragDropEvent.GetOperationAs<FMixtormatChildDragDropOp>())
		{
			// A scoped filter cannot be orphaned from the mask it filters, and nothing moves to
			// the layer it already lives on. Refused here rather than at the drop, so the row
			// never lights up for a release that will not be honoured.
			if (!ChildOp->bCanLeaveLayer || ChildOp->LayerIndex == TargetLayerIndex)
			{
				Zone = EMixtormatRowDropZone::None;
				return FReply::Unhandled();
			}
			Zone = EMixtormatRowDropZone::Into;
			ChildOp->SetToolTip(
				FText::Format(LOCTEXT("MoveChildToLayer", "Move {0} to this layer"), ChildOp->Name),
				FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			return FReply::Handled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> LayerOp =
			DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		const TSharedPtr<FMixtormatGroupDragDropOp> GroupOp =
			DragDropEvent.GetOperationAs<FMixtormatGroupDragDropOp>();
		const TSharedPtr<FMixtormatSurfaceDragDropOp> SurfaceOp =
			DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>();
		if (!LayerOp.IsValid() && !GroupOp.IsValid() && !SurfaceOp.IsValid())
		{
			return FReply::Unhandled();
		}
		if (LayerOp.IsValid() && LayerOp->SourceLayerIndex == INDEX_NONE)
		{
			return FReply::Unhandled();
		}

		// Cached, not recomputed on drop: OnDrop's geometry can disagree by a pixel, and then the
		// layer lands somewhere other than the line the user was looking at.
		Zone = MixtormatDropZone::ForLayerRow(MyGeometry, DragDropEvent);
		const FText Hint = Zone == EMixtormatRowDropZone::Before
			? LOCTEXT("DropAboveRow", "Release to place above this layer")
			: LOCTEXT("DropBelowRow", "Release to place below this layer");
		const FSlateBrush* HintIcon = FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add"));
		if (LayerOp.IsValid())
		{
			LayerOp->SetToolTip(Hint, HintIcon);
		}
		else if (GroupOp.IsValid())
		{
			GroupOp->SetToolTip(Hint, HintIcon);
		}
		else
		{
			// Named, because a new layer arriving is harder to place by eye than one already in
			// the stack moving -- no row lifts out to say where it came from.
			SurfaceOp->SetToolTip(
				FText::Format(
					Zone == EMixtormatRowDropZone::Before
						? LOCTEXT("InsertSurfaceAbove", "Insert {0} above this layer")
						: LOCTEXT("InsertSurfaceBelow", "Insert {0} below this layer"),
					SurfaceOp->DisplayName),
				HintIcon);
		}
		return FReply::Handled();
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

		const EMixtormatRowDropZone DropZone = Zone;
		Zone = EMixtormatRowDropZone::None;
		if (TargetLayerIndex == INDEX_NONE || DropZone == EMixtormatRowDropZone::None)
		{
			return FReply::Unhandled();
		}

		if (const TSharedPtr<FMixtormatChildDragDropOp> Operation =
			DragDropEvent.GetOperationAs<FMixtormatChildDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			// INDEX_NONE for the destination child: released on the layer body rather than on one
			// of its rows, so it lands on the end of that layer's stack.
			return OnChildMovedToLayer.IsBound()
				? OnChildMovedToLayer.Execute(
					Operation->LayerIndex, Operation->ChildIndex, TargetLayerIndex, INDEX_NONE)
				: FReply::Unhandled();
		}
		const int32 InsertIndex =
			DropZone == EMixtormatRowDropZone::Before ? TargetLayerIndex : TargetLayerIndex + 1;

		if (const TSharedPtr<FMixtormatLayerDragDropOp> Operation =
			DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			return Operation->SourceLayerIndex != INDEX_NONE && OnLayerInsertedAt.IsBound()
				? OnLayerInsertedAt.Execute(Operation->SourceLayerIndex, InsertIndex)
				: FReply::Unhandled();
		}
		if (const TSharedPtr<FMixtormatGroupDragDropOp> Operation =
			DragDropEvent.GetOperationAs<FMixtormatGroupDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			return Operation->GroupId.IsValid() && OnGroupInsertedAt.IsBound()
				? OnGroupInsertedAt.Execute(Operation->GroupId, InsertIndex)
				: FReply::Unhandled();
		}
		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation =
			DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			// No group named: a surface landing on a layer row takes whatever membership that
			// slot implies, which the handler works out from the neighbours.
			return OnSurfaceInsertedAt.IsBound()
				? OnSurfaceInsertedAt.Execute(
					Operation->DisplayName, Operation->SurfacePath, InsertIndex, FGuid())
				: FReply::Unhandled();
		}
		return FReply::Unhandled();
	}

private:
	int32 TargetLayerIndex = INDEX_NONE;
	FOnMixtormatMaskDropped OnMaskDropped;
	FOnLayerInsertedAt OnLayerInsertedAt;
	FOnGroupInsertedAt OnGroupInsertedAt;
	FOnSurfaceInsertedAt OnSurfaceInsertedAt;
	FOnMixtormatChildMovedToLayer OnChildMovedToLayer;
	EMixtormatRowDropZone Zone = EMixtormatRowDropZone::None;
	bool bMaskDragOver = false;
};

// A group header, as a destination.
//
// Three meanings, split by height: the narrow edges insert either side of the whole group, and the
// middle puts a layer inside it. A group dragged here can only go either side -- groups do not
// nest, so there is no "into" for one.
class SMixtormatGroupRowDropTarget final : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnLayerDroppedOnGroup, int32, FGuid);
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnLayerInsertedAtGroupEdge, int32, int32);
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnGroupInsertedAtGroupEdge, FGuid, int32);
	DECLARE_DELEGATE_RetVal_FourParams(
		FReply, FOnSurfaceInsertedAtGroup, FText, FSoftObjectPath, int32, FGuid);
	// Source layer, source child, target group. A child dropped here leaves its layer and joins the
	// group's shared stack, the same move FOnMixtormatChildMovedToLayer is for a layer target.
	DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnChildDroppedOnGroup, int32, int32, FGuid);

	SLATE_BEGIN_ARGS(SMixtormatGroupRowDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FGuid, TargetGroupId)
		// The group's own span, so an edge drop resolves to a slot index without the target
		// needing to know anything else about the stack.
		SLATE_ARGUMENT(int32, FirstMemberIndex)
		SLATE_ARGUMENT(int32, LastMemberIndex)
		SLATE_EVENT(FOnLayerDroppedOnGroup, OnLayerDropped)
		SLATE_EVENT(FOnLayerInsertedAtGroupEdge, OnLayerInsertedAt)
		SLATE_EVENT(FOnGroupInsertedAtGroupEdge, OnGroupInsertedAt)
		SLATE_EVENT(FOnSurfaceInsertedAtGroup, OnSurfaceInsertedAt)
		SLATE_EVENT(FOnChildDroppedOnGroup, OnChildDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetGroupId = InArgs._TargetGroupId;
		FirstMemberIndex = InArgs._FirstMemberIndex;
		LastMemberIndex = InArgs._LastMemberIndex;
		OnLayerDropped = InArgs._OnLayerDropped;
		OnLayerInsertedAt = InArgs._OnLayerInsertedAt;
		OnGroupInsertedAt = InArgs._OnGroupInsertedAt;
		OnSurfaceInsertedAt = InArgs._OnSurfaceInsertedAt;
		OnChildDropped = InArgs._OnChildDropped;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot()
			[
				MixtormatDropZone::MakeInsertionOverlay([this]() { return Zone; })
			]
		];
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		Zone = EMixtormatRowDropZone::None;
		if (const TSharedPtr<FMixtormatLayerDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatLayerDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatGroupDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatGroupDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		if (const TSharedPtr<FMixtormatChildDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatChildDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& Event) override
	{
		if (!TargetGroupId.IsValid())
		{
			return FReply::Unhandled();
		}

		// A child dropped on the header means "make this shared", the same all-or-nothing meaning
		// a child dropped on a layer body has -- not a position along the group's edges, so the
		// whole row lights rather than one of its thirds.
		if (const TSharedPtr<FMixtormatChildDragDropOp> ChildOp =
			Event.GetOperationAs<FMixtormatChildDragDropOp>())
		{
			if (!ChildOp->bCanLeaveLayer)
			{
				Zone = EMixtormatRowDropZone::None;
				return FReply::Unhandled();
			}
			Zone = EMixtormatRowDropZone::Into;
			ChildOp->SetToolTip(
				FText::Format(LOCTEXT("MoveChildToGroupDrag", "Share {0} across this group"), ChildOp->Name),
				FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			return FReply::Handled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> LayerOp =
			Event.GetOperationAs<FMixtormatLayerDragDropOp>();
		const TSharedPtr<FMixtormatGroupDragDropOp> GroupOp =
			Event.GetOperationAs<FMixtormatGroupDragDropOp>();

		if (LayerOp.IsValid() && LayerOp->SourceLayerIndex != INDEX_NONE)
		{
			Zone = MixtormatDropZone::ForGroupRow(MyGeometry, Event, true);
			LayerOp->SetToolTip(
				Zone == EMixtormatRowDropZone::Into
					? LOCTEXT("ReleaseLayerIntoGroup", "Release to put this layer in the group")
					: Zone == EMixtormatRowDropZone::Before
						? LOCTEXT("DropAboveGroup", "Release to place above this group")
						: LOCTEXT("DropBelowGroup", "Release to place below this group"),
				FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			return FReply::Handled();
		}
		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> SurfaceOp =
			Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Zone = MixtormatDropZone::ForGroupRow(MyGeometry, Event, true);
			SurfaceOp->SetToolTip(
				FText::Format(
					Zone == EMixtormatRowDropZone::Into
						? LOCTEXT("AddSurfaceToGroup", "Add {0} to this group")
						: Zone == EMixtormatRowDropZone::Before
							? LOCTEXT("InsertSurfaceAboveGroup", "Insert {0} above this group")
							: LOCTEXT("InsertSurfaceBelowGroup", "Insert {0} below this group"),
					SurfaceOp->DisplayName),
				FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			return FReply::Handled();
		}
		if (GroupOp.IsValid() && GroupOp->GroupId.IsValid() && GroupOp->GroupId != TargetGroupId)
		{
			// No Into: a group inside a group is not something this model has, so the middle of
			// the row is not offered rather than being offered and refused.
			Zone = MixtormatDropZone::ForGroupRow(MyGeometry, Event, false);
			GroupOp->SetToolTip(
				Zone == EMixtormatRowDropZone::Before
					? LOCTEXT("DropAboveGroup", "Release to place above this group")
					: LOCTEXT("DropBelowGroup", "Release to place below this group"),
				FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& Event) override
	{
		const EMixtormatRowDropZone DropZone = Zone;
		Zone = EMixtormatRowDropZone::None;
		if (!TargetGroupId.IsValid() || DropZone == EMixtormatRowDropZone::None)
		{
			return FReply::Unhandled();
		}

		if (const TSharedPtr<FMixtormatChildDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatChildDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			return OnChildDropped.IsBound()
				? OnChildDropped.Execute(Operation->LayerIndex, Operation->ChildIndex, TargetGroupId)
				: FReply::Unhandled();
		}

		// Before the group is its first member's slot; after it is one past the last.
		const int32 EdgeIndex =
			DropZone == EMixtormatRowDropZone::Before ? FirstMemberIndex : LastMemberIndex + 1;

		if (const TSharedPtr<FMixtormatLayerDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatLayerDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			if (Operation->SourceLayerIndex == INDEX_NONE)
			{
				return FReply::Unhandled();
			}
			if (DropZone == EMixtormatRowDropZone::Into)
			{
				return OnLayerDropped.IsBound()
					? OnLayerDropped.Execute(Operation->SourceLayerIndex, TargetGroupId)
					: FReply::Unhandled();
			}
			return OnLayerInsertedAt.IsBound()
				? OnLayerInsertedAt.Execute(Operation->SourceLayerIndex, EdgeIndex)
				: FReply::Unhandled();
		}
		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			if (!OnSurfaceInsertedAt.IsBound())
			{
				return FReply::Unhandled();
			}
			// Into the middle means the new layer is a member from the moment it exists, so the
			// group is named rather than inferred from where it landed.
			return DropZone == EMixtormatRowDropZone::Into
				? OnSurfaceInsertedAt.Execute(
					Operation->DisplayName, Operation->SurfacePath, LastMemberIndex + 1, TargetGroupId)
				: OnSurfaceInsertedAt.Execute(
					Operation->DisplayName, Operation->SurfacePath, EdgeIndex, FGuid());
		}
		if (const TSharedPtr<FMixtormatGroupDragDropOp> Operation =
			Event.GetOperationAs<FMixtormatGroupDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
			return Operation->GroupId.IsValid()
				&& Operation->GroupId != TargetGroupId
				&& OnGroupInsertedAt.IsBound()
				? OnGroupInsertedAt.Execute(Operation->GroupId, EdgeIndex)
				: FReply::Unhandled();
		}
		return FReply::Unhandled();
	}

private:
	FGuid TargetGroupId;
	int32 FirstMemberIndex = INDEX_NONE;
	int32 LastMemberIndex = INDEX_NONE;
	FOnLayerDroppedOnGroup OnLayerDropped;
	FOnLayerInsertedAtGroupEdge OnLayerInsertedAt;
	FOnGroupInsertedAtGroupEdge OnGroupInsertedAt;
	FOnSurfaceInsertedAtGroup OnSurfaceInsertedAt;
	FOnChildDroppedOnGroup OnChildDropped;
	EMixtormatRowDropZone Zone = EMixtormatRowDropZone::None;
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
