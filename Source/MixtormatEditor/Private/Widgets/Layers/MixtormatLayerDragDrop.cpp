// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatOutputReference.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace MixtormatLayersPrivate
{
	static FGuid ResolveGroupMembershipForLayers(const TArray<FMixtormatLayer>& Layers, const int32 LayerIndex)
	{
		if (!Layers.IsValidIndex(LayerIndex)) { return FGuid(); }
		const FGuid Below = Layers.IsValidIndex(LayerIndex - 1) ? Layers[LayerIndex - 1].GroupId : FGuid();
		const FGuid Above = Layers.IsValidIndex(LayerIndex + 1) ? Layers[LayerIndex + 1].GroupId : FGuid();
		if (Below.IsValid() && Below == Above) { return Below; }
		const FGuid Own = Layers[LayerIndex].GroupId;
		return Own.IsValid() && (Own == Below || Own == Above) ? Own : FGuid();
	}

	void SMixtormatIdGroupChildDropTarget::Construct(const FArguments& InArgs)
	{
			Address = InArgs._Address;
			OnCanDrop = InArgs._OnCanDrop;
			OnIdDrop = InArgs._OnIdDrop;
			ChildSlot[InArgs._Content.Widget];
		}

	FReply SMixtormatIdGroupChildDropTarget::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
	{
			const TSharedPtr<FMixtormatChildDragDropOp> Operation =
				Event.GetOperationAs<FMixtormatChildDragDropOp>();
			return Operation.IsValid() && OnCanDrop.IsBound() && OnCanDrop.Execute(*Operation, Address)
				? FReply::Handled() : FReply::Unhandled();
		}

	FReply SMixtormatIdGroupChildDropTarget::OnDrop(const FGeometry&, const FDragDropEvent& Event)
	{
			const TSharedPtr<FMixtormatChildDragDropOp> Operation =
				Event.GetOperationAs<FMixtormatChildDragDropOp>();
			return Operation.IsValid() && OnIdDrop.IsBound()
				? OnIdDrop.Execute(*Operation, Address) : FReply::Unhandled();
		}
}

FReply SMixtormat::HandleLayerDropped(
	const int32 SourceLayerIndex,
	const int32 TargetLayerIndex,
	const bool bRecordHistory, const FGuid* ExplicitGroupId)
{
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !WorkingLayers.IsValidIndex(TargetLayerIndex)
		|| SourceLayerIndex == TargetLayerIndex)
	{
		return FReply::Unhandled();
	}

	// One layer moving is a permutation like any other, so it goes through the same helper the
	// group gather uses rather than a second remap that could disagree with it.
	TArray<int32> NewOrder;
	NewOrder.Reserve(WorkingLayers.Num());
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (Index != SourceLayerIndex)
		{
			NewOrder.Add(Index);
		}
	}
	NewOrder.Insert(SourceLayerIndex, TargetLayerIndex);
	TArray<FMixtormatLayer> ProposedLayers = WorkingLayers;
	TArray<FMixtormatLayerGroup> ProposedGroups = WorkingLayerGroups;
	const int32 DroppedReferences = MixtormatUI::ReorderLayersByPermutation(ProposedLayers, NewOrder);
	// Validate the final membership, not an intermediate reorder that an explicit group drop overrides.
	const FGuid JoinedGroupId = ExplicitGroupId ? *ExplicitGroupId
		: ResolveGroupMembershipForLayers(ProposedLayers, TargetLayerIndex);
	const bool bChangedGroup = ProposedLayers[TargetLayerIndex].GroupId != JoinedGroupId;
	ProposedLayers[TargetLayerIndex].GroupId = JoinedGroupId;
	MixtormatLayerGroups::ValidateGroups(ProposedLayers, ProposedGroups);
	WorkingLayers = MoveTemp(ProposedLayers);
	WorkingLayerGroups = MoveTemp(ProposedGroups);

	SelectedLayerIndex = TargetLayerIndex;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	if (DroppedReferences > 0)
	{
		WorkingStatusText = FString::Printf(
			TEXT("Moved layer · %d height reference(s) dropped"), DroppedReferences);
	}
	else if (bChangedGroup)
	{
		WorkingStatusText = JoinedGroupId.IsValid()
			? TEXT("Moved layer into group")
			: TEXT("Moved layer out of its group");
	}
	if (bRecordHistory)
	{
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	}
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

// InsertIndex is a slot in the array as it stands now, before the source is taken out of it.
//
// Removing a layer that sits below the slot shifts everything above it down one, so the
// destination has to come down with it. Getting this wrong puts the layer one row from the line
// the user was looking at, and it compiles perfectly either way.
// A library surface dropped at a chosen position rather than on the end of the stack.
//
// The layer is created directly at the resolved slot and the height references are remapped once
// for that insert. Appending and then shuffling the layer down would remap on every step, and
// each of those remaps is a chance for a reference to be dropped that had no reason to move.
FReply SMixtormat::HandleSurfaceDroppedAt(
	const FText DisplayName,
	const FSoftObjectPath AssetPath,
	const int32 InsertIndex,
	const FGuid GroupId)
{
	if (!bHasWorkingMaterial)
	{
		// Nothing to insert into yet, so position has no meaning -- this is the first layer.
		return HandleSurfaceDropped(DisplayName, AssetPath);
	}
	if (AssetPath.IsNull())
	{
		return FReply::Unhandled();
	}
	const int32 Slot = FMath::Clamp(InsertIndex, 0, WorkingLayers.Num());

	SelectSurface(DisplayName, AssetPath);

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Material;
	Layer.DisplayName = DisplayName.IsEmpty()
		? FText::Format(
			LOCTEXT("MaterialLayerNumber", "Material Layer {0}"),
			FText::AsNumber(WorkingLayers.Num() + 1))
		: DisplayName;
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(AssetPath);
	// Set before the insert so validation never sees a layer sitting inside a run without
	// belonging to it, which is the shape it would repair by ungrouping the neighbours.
	Layer.GroupId = GroupId;

	WorkingLayers.Insert(MoveTemp(Layer), Slot);
	MixtormatUI::RemapHeightReferencesAfterInsert(WorkingLayers, Slot);

	// An ungrouped insert still has to answer for where it landed: dropped between two members of
	// one group, it joins them, because the alternative is a run with a hole in it.
	if (!GroupId.IsValid())
	{
		WorkingLayers[Slot].GroupId = ResolveGroupMembershipAt(Slot);
	}
	MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);

	SoloLayerIndex = INDEX_NONE;
	SelectedLayerIndex = Slot;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

int32 SMixtormat::InsertIndexToMoveTarget(const int32 SourceIndex, const int32 InsertIndex)
{
	return SourceIndex < InsertIndex ? InsertIndex - 1 : InsertIndex;
}

FReply SMixtormat::HandleLayerInsertedAt(const int32 SourceLayerIndex, const int32 InsertIndex)
{
	const int32 TargetIndex = InsertIndexToMoveTarget(SourceLayerIndex, InsertIndex);
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| TargetIndex < 0
		|| TargetIndex >= WorkingLayers.Num())
	{
		return FReply::Unhandled();
	}
	if (TargetIndex == SourceLayerIndex)
	{
		// Dropped on its own edge: the stack does not change, but membership still might, because
		// the line the user aimed at may sit outside the group the layer is currently in.
		const FGuid Resolved = ResolveGroupMembershipAt(TargetIndex);
		if (WorkingLayers[TargetIndex].GroupId == Resolved)
		{
			return FReply::Handled();
		}
	}
	return HandleLayerDropped(SourceLayerIndex, TargetIndex);
}

FReply SMixtormat::HandleGroupInsertedAt(const FGuid GroupId, const int32 InsertIndex)
{
	int32 FirstIndex = INDEX_NONE;
	int32 LastIndex = INDEX_NONE;
	if (!MixtormatLayerGroups::GetGroupRange(WorkingLayers, GroupId, FirstIndex, LastIndex))
	{
		return FReply::Unhandled();
	}
	// Dropping a group on its own edges is a no-op rather than a move that lands where it started.
	if (InsertIndex >= FirstIndex && InsertIndex <= LastIndex + 1)
	{
		return FReply::Handled();
	}

	// Groups do not nest, so a line drawn between two members of another group -- reachable by
	// dropping on one of that group's ordinary member rows, which know nothing about the group
	// they belong to -- is snapped to that group's nearer outer edge instead of spliced in. Left
	// alone, the splice below would land the block mid-run and ValidateGroups would "fix" the
	// split by ungrouping whichever of that group's members ended up on the far side.
	//
	// One pass is enough: every group here is already a contiguous, non-overlapping run (the same
	// invariant ValidateGroups enforces after every structural edit, this one included), so a
	// snapped edge is always either before the first group in the stack, after the last, or
	// sitting exactly on the shared boundary between two adjacent ones -- never inside a second
	// group's span.
	int32 TargetIndex = InsertIndex;
	for (const FMixtormatLayerGroup& OtherGroup : WorkingLayerGroups)
	{
		if (OtherGroup.GroupId == GroupId)
		{
			continue;
		}
		int32 OtherFirst = INDEX_NONE;
		int32 OtherLast = INDEX_NONE;
		if (!MixtormatLayerGroups::GetGroupRange(WorkingLayers, OtherGroup.GroupId, OtherFirst, OtherLast))
		{
			continue;
		}
		if (TargetIndex > OtherFirst && TargetIndex <= OtherLast)
		{
			TargetIndex = (TargetIndex - OtherFirst <= OtherLast + 1 - TargetIndex)
				? OtherFirst
				: OtherLast + 1;
			break;
		}
	}
	// The snap can land back on the dragged group's own edge (it sits right next to whichever
	// group absorbed the line), which is the same no-op the raw-index check above exists for.
	if (TargetIndex >= FirstIndex && TargetIndex <= LastIndex + 1)
	{
		return FReply::Handled();
	}

	// The block moves as one. Built the same way the group gather is: everything else in order,
	// then the block spliced in at the slot the line was drawn on.
	const int32 BlockCount = LastIndex - FirstIndex + 1;
	TArray<int32> Others;
	Others.Reserve(WorkingLayers.Num() - BlockCount);
	int32 InsertAt = 0;
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (Index >= FirstIndex && Index <= LastIndex)
		{
			continue;
		}
		if (Index < TargetIndex)
		{
			++InsertAt;
		}
		Others.Add(Index);
	}

	TArray<int32> NewOrder;
	NewOrder.Reserve(WorkingLayers.Num());
	NewOrder.Append(Others.GetData(), InsertAt);
	for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
	{
		NewOrder.Add(Index);
	}
	for (int32 Index = InsertAt; Index < Others.Num(); ++Index)
	{
		NewOrder.Add(Others[Index]);
	}

	TArray<FMixtormatLayer> ProposedLayers = WorkingLayers;
	TArray<FMixtormatLayerGroup> ProposedGroups = WorkingLayerGroups;
	const int32 DroppedReferences =
		MixtormatUI::ReorderLayersByPermutation(ProposedLayers, NewOrder);
	// The block carried its GroupId with it, so the run is still whole, and the snap above already
	// kept it out of another group's run. This is the general backstop for shapes that snap does
	// not cover -- hand-edited data, a merge -- not the normal path for a group-on-group drop.
	MixtormatLayerGroups::ValidateGroups(ProposedLayers, ProposedGroups);
	WorkingLayers = MoveTemp(ProposedLayers);
	WorkingLayerGroups = MoveTemp(ProposedGroups);

	SelectedGroupId = GroupId;
	SelectedGroupChildIndex = INDEX_NONE;
	SelectedLayerIndex = INDEX_NONE;
	bHasSelectedLayer = false;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = DroppedReferences > 0
		? FString::Printf(
			TEXT("Moved group · %d height reference(s) dropped"), DroppedReferences)
		: TEXT("Moved group");
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::HandleLayerDroppedOnGroup(
	const int32 SourceLayerIndex,
	const FGuid TargetGroupId)
{
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !MixtormatLayerGroups::FindGroup(WorkingLayerGroups, TargetGroupId))
	{
		return FReply::Unhandled();
	}
	if (WorkingLayers[SourceLayerIndex].GroupId == TargetGroupId)
	{
		return FReply::Handled();
	}

	int32 FirstIndex = INDEX_NONE;
	int32 LastIndex = INDEX_NONE;
	if (!MixtormatLayerGroups::GetGroupRange(WorkingLayers, TargetGroupId, FirstIndex, LastIndex))
	{
		// An empty group has no run to land inside, so the dropped layer becomes its first member
		// and keeps its position; the group then spans exactly that layer.
		TArray<FMixtormatLayer> ProposedLayers = WorkingLayers;
		TArray<FMixtormatLayerGroup> ProposedGroups = WorkingLayerGroups;
		ProposedLayers[SourceLayerIndex].GroupId = TargetGroupId;
		MixtormatLayerGroups::ValidateGroups(ProposedLayers, ProposedGroups);
		WorkingLayerGroups = MoveTemp(ProposedGroups);
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		RefreshLayeredPreview();
		RebuildLayerList();
		return FReply::Handled();
	}

	// The top of the run. A layer joining a group has to land inside it, and the top is the one
	// position that is unambiguous whether the layer came from above or below.
	const int32 TargetIndex = SourceLayerIndex < FirstIndex ? LastIndex : FirstIndex;
	// Record once below, after the explicit group membership is finalized.
	const FGuid MovedLayerId = WorkingLayers[SourceLayerIndex].LayerId;
	const FReply Result = HandleLayerDropped(SourceLayerIndex, TargetIndex, false, &TargetGroupId);

	// The named group was included in the validated projection; finalize history only on success.
	if (Result.IsEventHandled() && WorkingLayers.IsValidIndex(TargetIndex)
		&& WorkingLayers[TargetIndex].LayerId == MovedLayerId
		&& WorkingLayers[TargetIndex].GroupId == TargetGroupId)
	{
		WorkingLayers[TargetIndex].GroupId = TargetGroupId;
		MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return Result;
}

// Which group, if any, a layer at this position belongs to.
//
// Derived from the neighbours rather than carried by the layer, because a group owns a contiguous
// run: a layer that lands inside or against a run is in it, and one that lands anywhere else is
// not. That single rule is what lets the same drag move a layer in and out.
FGuid SMixtormat::ResolveGroupMembershipAt(const int32 LayerIndex) const
{
	return ResolveGroupMembershipForLayers(WorkingLayers, LayerIndex);
}

FReply SMixtormat::ReorderLayerChild(
	const int32 LayerIndex,
	const int32 SourceChildIndex,
	const int32 TargetChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(SourceChildIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(TargetChildIndex)
		|| SourceChildIndex == TargetChildIndex)
	{
		return FReply::Unhandled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid SourceParentId = Layer.Children[SourceChildIndex].ScopeOwnerChildId;
	const int32 TargetRootIndex = FindSiblingRoot(Layer.Children, TargetChildIndex, SourceParentId);
	if (TargetRootIndex == INDEX_NONE || TargetRootIndex == SourceChildIndex)
	{
		return FReply::Unhandled();
	}

	const int32 SourceSubtreeEnd = FindSubtreeEnd(Layer.Children, SourceChildIndex);
	const int32 TargetSubtreeEnd = FindSubtreeEnd(Layer.Children, TargetRootIndex);
	if (TargetRootIndex < SourceSubtreeEnd && TargetSubtreeEnd > SourceChildIndex)
	{
		return FReply::Unhandled();
	}
	FText MoveReason;
	if (!CanMovePublishedOutputs(MakeChildAddress(LayerIndex, SourceChildIndex),
		{EMixtormatChildOwnerType::Layer, Layer.LayerId, FGuid()},
		TargetRootIndex < SourceChildIndex ? TargetRootIndex : TargetSubtreeEnd, &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

	FGuid SelectedEffectId;
	FGuid SelectedMaskId;
	if (SelectedLayerIndex == LayerIndex)
	{
		if (Layer.Children.IsValidIndex(SelectedEffectIndex))
		{
			SelectedEffectId = Layer.Children[SelectedEffectIndex].ChildId;
		}
		if (Layer.Children.IsValidIndex(SelectedMaskIndex))
		{
			SelectedMaskId = Layer.Children[SelectedMaskIndex].ChildId;
		}
	}

	const int32 SourceCount = SourceSubtreeEnd - SourceChildIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(SourceCount);
	for (int32 MoveIndex = 0; MoveIndex < SourceCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(Layer.Children[SourceChildIndex + MoveIndex]));
	}
	Layer.Children.RemoveAt(SourceChildIndex, SourceCount);

	const int32 InsertAt = TargetRootIndex < SourceChildIndex
		? TargetRootIndex
		: TargetSubtreeEnd - SourceCount;
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		Layer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}

	if (SelectedLayerIndex == LayerIndex)
	{
		SelectedEffectIndex = FindChildById(Layer.Children, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer.Children, SelectedMaskId);
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ReorderGroupChild(
	const FGuid GroupId,
	const int32 SourceChildIndex,
	int32 TargetChildIndex)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group
		|| !Group->Children.IsValidIndex(SourceChildIndex)
		|| !Group->Children.IsValidIndex(TargetChildIndex)
		// Scoped mask/flow tools still travel with their owner; ID children can reorder as siblings.
		|| (Group->Children[SourceChildIndex].ScopeOwnerChildId.IsValid()
			&& !IsIdGroupChild(Group->Children[SourceChildIndex])))
	{
		return FReply::Unhandled();
	}
	// Reordering cannot change containment. ID Group nesting is handled by the inner drop target.
	const FGuid SourceParentId = Group->Children[SourceChildIndex].ScopeOwnerChildId;
	TargetChildIndex = FindSiblingRoot(Group->Children, TargetChildIndex, SourceParentId);
	if (TargetChildIndex == INDEX_NONE || TargetChildIndex == SourceChildIndex)
	{
		return FReply::Unhandled();
	}
	FText MoveReason;
	if (!CanMovePublishedOutputs(MakeGroupChildAddress(GroupId, SourceChildIndex),
		{EMixtormatChildOwnerType::Group, GroupId, FGuid()},
		TargetChildIndex < SourceChildIndex ? TargetChildIndex : FindSubtreeEnd(Group->Children, TargetChildIndex), &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

	FGuid SelectedChildId;
	if (SelectedGroupId == GroupId && Group->Children.IsValidIndex(SelectedGroupChildIndex))
	{
		SelectedChildId = Group->Children[SelectedGroupChildIndex].ChildId;
	}

	// Contiguous by construction: a subtree only ever arrives as one intact block (AppendGroupChild
	// adds a lone unscoped child, MoveChildToGroup inserts a whole extracted subtree), and removal
	// (RemoveGroupChild) deletes matched children without reordering the survivors. Nothing in this
	// function's own splice below breaks that either, so FindSubtreeEnd's positional walk can
	// trust it.
	const int32 SourceSubtreeEnd = FindSubtreeEnd(Group->Children, SourceChildIndex);
	const int32 TargetSubtreeEnd = FindSubtreeEnd(Group->Children, TargetChildIndex);
	const int32 SourceCount = SourceSubtreeEnd - SourceChildIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(SourceCount);
	for (int32 MoveIndex = 0; MoveIndex < SourceCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(Group->Children[SourceChildIndex + MoveIndex]));
	}
	Group->Children.RemoveAt(SourceChildIndex, SourceCount);

	const int32 InsertAt = TargetChildIndex < SourceChildIndex
		? TargetChildIndex
		: TargetSubtreeEnd - SourceCount;
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		Group->Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}

	if (SelectedChildId.IsValid())
	{
		SelectedGroupChildIndex = Group->Children.IndexOfByPredicate(
			[&SelectedChildId](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.ChildId == SelectedChildId;
			});
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

// The mirror of MoveChildToGroup: a shared child stops being shared and becomes one layer's own.
//
// Not a symmetric "move" in what it means to the user, even though the splice is the same shape.
// A shared child applies to every member of the group; taking it out leaves every other member
// without it. That is the point of the gesture -- "this one only" -- so nothing is copied to the
// members left behind, which is also the one thing this cannot undo by dropping it back.
FReply SMixtormat::MoveGroupChildToLayer(
	const FGuid GroupId,
	const int32 ChildIndex,
	const int32 DestLayerIndex,
	const int32 DestChildIndex)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group
		|| !Group->Children.IsValidIndex(ChildIndex)
		|| !WorkingLayers.IsValidIndex(DestLayerIndex)
		// ID children can leave a group; scoped masks/flow tools cannot leave their owner.
		|| (Group->Children[ChildIndex].ScopeOwnerChildId.IsValid()
			&& !IsIdGroupChild(Group->Children[ChildIndex])))
	{
		return FReply::Unhandled();
	}
	if (IsMaskFilter(Group->Children[ChildIndex])
		|| Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Behavior)
	{
		return FReply::Unhandled();
	}
	const TArray<FMixtormatLayerChild>& DestChildren = WorkingLayers[DestLayerIndex].Children;
	const int32 DestRoot = DestChildren.IsValidIndex(DestChildIndex)
		? FindSiblingRoot(DestChildren, DestChildIndex, FGuid()) : INDEX_NONE;
	FText MoveReason;
	if (!CanMovePublishedOutputs(MakeGroupChildAddress(GroupId, ChildIndex),
		{EMixtormatChildOwnerType::Layer, WorkingLayers[DestLayerIndex].LayerId, FGuid()},
		DestRoot == INDEX_NONE ? DestChildren.Num() : DestRoot, &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

	const int32 SubtreeEnd = FindSubtreeEnd(Group->Children, ChildIndex);
	const int32 MoveCount = SubtreeEnd - ChildIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(MoveCount);
	for (int32 MoveIndex = 0; MoveIndex < MoveCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(Group->Children[ChildIndex + MoveIndex]));
	}
	Group->Children.RemoveAt(ChildIndex, MoveCount);
	MovedChildren[0].ScopeOwnerChildId.Invalidate();

	FMixtormatLayer& DestLayer = WorkingLayers[DestLayerIndex];
	const FGuid NewLayerId = DestLayer.LayerId;
	int32 InsertAt = DestLayer.Children.Num();
	if (DestLayer.Children.IsValidIndex(DestChildIndex))
	{
		const int32 TopLevelRoot = FindSiblingRoot(DestLayer.Children, DestChildIndex, FGuid());
		InsertAt = TopLevelRoot == INDEX_NONE ? DestLayer.Children.Num() : TopLevelRoot;
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}
	// Insert the complete subtree first so references between moved children are remapped too.
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups},
			DestLayer.Children[InsertAt + MoveIndex].ChildId, GroupId, NewLayerId);
	}

	// Both lanes, the way SelectWorkingLayer clears them: SelectWorkingChild below sets the layer
	// lane but leaves the group lane alone, and a stale SelectedGroupId would keep the group header
	// highlighted beside the newly selected child -- and would send the next F2 to the group's name
	// rather than to the layer's (BeginRenameSelection branches on SelectedGroupId being valid).
	if (SelectedGroupId == GroupId)
	{
		SelectedGroupId.Invalidate();
		SelectedGroupChildIndex = INDEX_NONE;
	}
	SetLayerExpanded(DestLayerIndex, true);
	SelectWorkingChild(DestLayerIndex, InsertAt);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}


FReply SMixtormat::MoveChildToLayer(
	const int32 SourceLayerIndex,
	const int32 ChildIndex,
	const int32 DestLayerIndex,
	const int32 DestChildIndex)
{
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !WorkingLayers.IsValidIndex(DestLayerIndex)
		|| !WorkingLayers[SourceLayerIndex].Children.IsValidIndex(ChildIndex)
		|| SourceLayerIndex == DestLayerIndex)
	{
		return FReply::Unhandled();
	}

	FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
	if (IsMaskFilter(SourceLayer.Children[ChildIndex])
		|| SourceLayer.Children[ChildIndex].Type == EMixtormatLayerChildType::Behavior)
	{
		return FReply::Unhandled();
	}
	const TArray<FMixtormatLayerChild>& DestChildren = WorkingLayers[DestLayerIndex].Children;
	const int32 DestRoot = DestChildren.IsValidIndex(DestChildIndex)
		? FindSiblingRoot(DestChildren, DestChildIndex, FGuid()) : INDEX_NONE;
	FText MoveReason;
	if (!CanMovePublishedOutputs(MakeChildAddress(SourceLayerIndex, ChildIndex),
		{EMixtormatChildOwnerType::Layer, WorkingLayers[DestLayerIndex].LayerId, FGuid()},
		DestRoot == INDEX_NONE ? DestChildren.Num() : DestRoot, &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

	const FGuid OldLayerId = SourceLayer.LayerId;
	const FGuid NewLayerId = WorkingLayers[DestLayerIndex].LayerId;
	const int32 SubtreeEnd = FindSubtreeEnd(SourceLayer.Children, ChildIndex);
	const int32 MoveCount = SubtreeEnd - ChildIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(MoveCount);
	for (int32 MoveIndex = 0; MoveIndex < MoveCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(SourceLayer.Children[ChildIndex + MoveIndex]));
	}
	SourceLayer.Children.RemoveAt(ChildIndex, MoveCount);
	MovedChildren[0].ScopeOwnerChildId.Invalidate();

	FMixtormatLayer& DestLayer = WorkingLayers[DestLayerIndex];
	int32 InsertAt = DestLayer.Children.Num();
	if (DestLayer.Children.IsValidIndex(DestChildIndex))
	{
		const int32 TopLevelRoot = FindSiblingRoot(DestLayer.Children, DestChildIndex, FGuid());
		InsertAt = TopLevelRoot == INDEX_NONE ? DestLayer.Children.Num() : TopLevelRoot;
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups},
			DestLayer.Children[InsertAt + MoveIndex].ChildId, OldLayerId, NewLayerId);
	}

	SetLayerExpanded(DestLayerIndex, true);
	SelectWorkingChild(DestLayerIndex, InsertAt);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::MoveChildToGroup(
	const int32 SourceLayerIndex,
	const int32 ChildIndex,
	const FGuid GroupId)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !WorkingLayers[SourceLayerIndex].Children.IsValidIndex(ChildIndex)
		|| !Group)
	{
		return FReply::Unhandled();
	}

	FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
	if (IsMaskFilter(SourceLayer.Children[ChildIndex])
		|| SourceLayer.Children[ChildIndex].Type == EMixtormatLayerChildType::Behavior)
	{
		return FReply::Unhandled();
	}
	FText MoveReason;
	if (!CanMovePublishedOutputs(MakeChildAddress(SourceLayerIndex, ChildIndex),
		{EMixtormatChildOwnerType::Group, GroupId, FGuid()}, Group->Children.Num(), &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}

	const FGuid OldLayerId = SourceLayer.LayerId;
	const int32 SubtreeEnd = FindSubtreeEnd(SourceLayer.Children, ChildIndex);
	const int32 MoveCount = SubtreeEnd - ChildIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(MoveCount);
	for (int32 MoveIndex = 0; MoveIndex < MoveCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(SourceLayer.Children[ChildIndex + MoveIndex]));
	}
	SourceLayer.Children.RemoveAt(ChildIndex, MoveCount);
	MovedChildren[0].ScopeOwnerChildId.Invalidate();

	// Appended, the same as every other way a shared child arrives (AddMaskToGroup and siblings):
	// the group's stack has no reorder yet, so there is only one place to put it.
	const int32 InsertAt = Group->Children.Num();
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		Group->Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups},
			Group->Children[InsertAt + MoveIndex].ChildId, OldLayerId, GroupId);
	}

	CollapsedGroupIds.Remove(GroupId);
	SelectGroupChild(GroupId, InsertAt);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

bool SMixtormat::CanMovePublishedOutputs(
	const FMixtormatChildAddress& Source,
	const FMixtormatChildAddress& Dest,
	const int32 InsertIndex, FText* OutReason) const
{
	if (OutReason) { *OutReason = FText::GetEmpty(); }
	const TArray<FMixtormatLayerChild>* SourceChildren = ResolveContainer(Source);
	const TArray<FMixtormatLayerChild>* DestChildren = ResolveContainer(Dest);
	const int32 SourceIndex = ResolveChildIndexAt(Source);
	if (!SourceChildren || !DestChildren || !SourceChildren->IsValidIndex(SourceIndex))
	{
		return false;
	}
	const int32 Count = FindSubtreeEnd(*SourceChildren, SourceIndex) - SourceIndex;
	for (int32 Index = SourceIndex; Index < SourceIndex + Count; ++Index)
	{
		// A Behavior names its generator through ScopeOwnerChildId, so it only means
		// anything inside the container that generator lives in. Carrying one across owners,
		// or landing it under a new owner, would leave it naming a child that is no longer
		// there -- which gather treats as unowned rather than as a silently broken rewrite.
		if ((*SourceChildren)[Index].Type == EMixtormatLayerChildType::Behavior
			&& (Source.OwnerType != Dest.OwnerType || Source.OwnerId != Dest.OwnerId || Dest.ChildId.IsValid()))
		{
			if (OutReason)
			{
				*OutReason = FText::Format(LOCTEXT("BehaviorMovePlacement", "Cannot move {0}: a Behavior must stay in the layer that owns its generator."),
					GetLayerChildName((*SourceChildren)[Index]));
			}
			return false;
		}
	}
	if (InsertIndex < 0 || InsertIndex > DestChildren->Num()
		|| (SourceChildren == DestChildren && InsertIndex > SourceIndex && InsertIndex < SourceIndex + Count))
	{
		return false;
	}
	// Check the proposed order, including consumers left behind when their producer moves.
	// No working state is mutated on a rejected drag.
	TArray<FMixtormatLayer> Layers = WorkingLayers;
	TArray<FMixtormatLayerGroup> Groups = WorkingLayerGroups;
	const auto FindChildren = [&Layers, &Groups](const FMixtormatChildAddress& Address) -> TArray<FMixtormatLayerChild>*
	{
		if (Address.OwnerType == EMixtormatChildOwnerType::Group)
		{
			FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, Address.OwnerId);
			return Group ? &Group->Children : nullptr;
		}
		FMixtormatLayer* Layer = Layers.FindByPredicate(
			[&Address](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Address.OwnerId; });
		return Layer ? &Layer->Children : nullptr;
	};
	TArray<FMixtormatLayerChild>* ProjectedSource = FindChildren(Source);
	TArray<FMixtormatLayerChild>* ProjectedDest = FindChildren(Dest);
	TArray<FMixtormatLayerChild> Moved;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Moved.Add(MoveTemp((*ProjectedSource)[SourceIndex + Index]));
	}
	ProjectedSource->RemoveAt(SourceIndex, Count);
	if (Dest.ChildId.IsValid())
	{
		Moved[0].ScopeOwnerChildId = Dest.ChildId;
	}
	else if (Source.OwnerId != Dest.OwnerId)
	{
		Moved[0].ScopeOwnerChildId.Invalidate();
	}
	const int32 ProjectedInsert = InsertIndex
		- (SourceChildren == DestChildren && SourceIndex < InsertIndex ? Count : 0);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ProjectedDest->Insert(MoveTemp(Moved[Index]), ProjectedInsert + Index);
	}
	if (Source.OwnerId != Dest.OwnerId)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			MixtormatParameterBinding::RemapChildParent(FMixtormatMutableBindingScope{Layers, Groups},
				(*ProjectedDest)[ProjectedInsert + Index].ChildId, Source.OwnerId, Dest.OwnerId);
		}
	}
	return PublishedOutputPlacementsValid(FMixtormatBindingScope{Layers, Groups});
}

bool SMixtormat::CanMoveChildIntoIdGroup(
	const FMixtormatChildAddress& Source,
	const FMixtormatChildAddress& Dest, FText* OutReason) const
{
	const TArray<FMixtormatLayerChild>* SourceChildren = ResolveContainer(Source);
	const TArray<FMixtormatLayerChild>* DestChildren = ResolveContainer(Dest);
	const int32 SourceIndex = ResolveChildIndexAt(Source);
	const int32 OwnerIndex = ResolveChildIndexAt(Dest);
	if (!SourceChildren || !DestChildren
		|| !SourceChildren->IsValidIndex(SourceIndex) || !DestChildren->IsValidIndex(OwnerIndex)
		|| (*DestChildren)[OwnerIndex].Type != EMixtormatLayerChildType::IdGroup
		|| !IsIdGroupChild((*SourceChildren)[SourceIndex])
		|| !CanAddScopedChild(*DestChildren, OwnerIndex))
	{
		return false;
	}
	if (SourceChildren == DestChildren
		&& (Source.ChildId == Dest.ChildId || IsDescendantOf(*SourceChildren, OwnerIndex, Source.ChildId)))
	{
		return false;
	}
	const int32 SourceDepth = GetScopeDepth(*SourceChildren, SourceIndex);
	const int32 NewDepth = GetScopeDepth(*DestChildren, OwnerIndex) + 1;
	const int32 SubtreeEnd = FindSubtreeEnd(*SourceChildren, SourceIndex);
	for (int32 Index = SourceIndex; Index < SubtreeEnd; ++Index)
	{
		const int32 Depth = GetScopeDepth(*SourceChildren, Index);
		if (Depth > MaximumScopeDepth || NewDepth + Depth - SourceDepth > MaximumScopeDepth)
		{
			return false;
		}
	}
	return CanMovePublishedOutputs(Source, Dest, FindSubtreeEnd(*DestChildren, OwnerIndex), OutReason);
}

FReply SMixtormat::MoveChildIntoIdGroup(
	const FMixtormatChildAddress& Source,
	const FMixtormatChildAddress& Dest)
{
	FText MoveReason;
	if (!CanMoveChildIntoIdGroup(Source, Dest, &MoveReason))
	{
		if (!MoveReason.IsEmpty()) { WorkingStatusText = MoveReason.ToString(); return FReply::Handled(); }
		return FReply::Unhandled();
	}
	TArray<FMixtormatLayerChild>* SourceChildren = ResolveContainer(Source);
	TArray<FMixtormatLayerChild>* DestChildren = ResolveContainer(Dest);
	const int32 SourceIndex = ResolveChildIndexAt(Source);
	const int32 Count = FindSubtreeEnd(*SourceChildren, SourceIndex) - SourceIndex;
	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		MovedChildren.Add(MoveTemp((*SourceChildren)[SourceIndex + Index]));
	}
	SourceChildren->RemoveAt(SourceIndex, Count);
	MovedChildren[0].ScopeOwnerChildId = Dest.ChildId;
	// Re-resolve after removal: source and destination may be the same child array.
	const int32 InsertAt = FindSubtreeEnd(*DestChildren, ResolveChildIndexAt(Dest));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		DestChildren->Insert(MoveTemp(MovedChildren[Index]), InsertAt + Index);
	}
	if (Source.OwnerId != Dest.OwnerId)
	{
		// The entire subtree must be present before remapping references between its children.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			MixtormatParameterBinding::RemapChildParent(
				FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups},
				(*DestChildren)[InsertAt + Index].ChildId, Source.OwnerId, Dest.OwnerId);
		}
	}
	if (Dest.OwnerType == EMixtormatChildOwnerType::Group)
	{
		FinishGroupChildEdit(Dest.OwnerId, InsertAt);
	}
	else
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Dest](const FMixtormatLayer& Layer) { return Layer.LayerId == Dest.OwnerId; });
		SelectedGroupId.Invalidate();
		SelectedGroupChildIndex = INDEX_NONE;
		SetLayerExpanded(LayerIndex, true);
		SelectWorkingChild(LayerIndex, InsertAt);
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	RebuildMaskList();
	return FReply::Handled();
}

FMixtormatChildAddress SMixtormat::GetDraggedChildAddress(const FMixtormatChildDragDropOp& Operation) const
{
	return Operation.GroupId.IsValid()
		? MakeGroupChildAddress(Operation.GroupId, Operation.ChildIndex)
		: MakeChildAddress(Operation.LayerIndex, Operation.ChildIndex);
}

bool SMixtormat::CanDropChildIntoIdGroup(
	const FMixtormatChildDragDropOp&,
	const FMixtormatChildAddress Dest) const
{
	const FMixtormatLayerChild* Owner = ResolveChildAt(Dest);
	// Reserve the group row even for a rejected gesture; it must never move the producer instead.
	return Owner && Owner->Type == EMixtormatLayerChildType::IdGroup;
}

FReply SMixtormat::DropChildIntoIdGroup(
	const FMixtormatChildDragDropOp& Operation,
	const FMixtormatChildAddress Dest)
{
	const FMixtormatChildAddress Source = GetDraggedChildAddress(Operation);
	const FMixtormatLayerChild* Owner = ResolveChildAt(Dest);
	if (!Owner || Owner->Type != EMixtormatLayerChildType::IdGroup)
	{
		return FReply::Unhandled();
	}
	if (!CanAddIdGroupSource(Source, Dest))
	{
		WorkingStatusText = LOCTEXT("IdGroupSourceBlocked", "Requires available Region IDs from this owner or an earlier owner, without feedback.").ToString();
		return FReply::Handled();
	}
	AddIdGroupSource(Source, Dest);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
