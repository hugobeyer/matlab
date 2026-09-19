// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Services/MixtormatPaths.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

#include "ObjectTools.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Controls/SMixtormatTile.h"
#include "Widgets/SToolTip.h"

// The layer stack: layer and child operations, selection, and the layer list UI.

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	constexpr int32 MaximumScopeDepth = 4;

	EMixtormatEffectType EffectTypeOf(const FMixtormatLayerChild& Child)
	{
		if (const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous())
		{
			return Asset->EffectType;
		}
		return Child.Effect.ProceduralType;
	}

	bool IsFlowWarp(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Effect
			&& EffectTypeOf(Child) == EMixtormatEffectType::FlowWarp;
	}

	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Effect;
	}

	bool CanOwnScopedBlurs(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask;
	}

	bool CanOwnFlowWarp(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask
			|| (Child.Type == EMixtormatLayerChildType::Effect
				&& MixtormatEffectClassOf(EffectTypeOf(Child))
					== EMixtormatEffectClass::Surface);
	}

	bool IsMaskFilter(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Blur
			|| Child.Type == EMixtormatLayerChildType::Curvature;
	}

	bool CanKeepScopedPlacement(
		const FMixtormatLayerChild& Owner,
		const FMixtormatLayerChild& Child);

	int32 FindChildById(const FMixtormatLayer& Layer, const FGuid& ChildId)
	{
		return ChildId.IsValid()
			? Layer.Children.IndexOfByPredicate([&ChildId](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == ChildId;
			})
			: INDEX_NONE;
	}

	int32 GetScopeDepth(const FMixtormatLayer& Layer, const int32 ChildIndex)
	{
		if (!Layer.Children.IsValidIndex(ChildIndex))
		{
			return 0;
		}

		int32 Depth = 0;
		int32 CurrentIndex = ChildIndex;
		FGuid OwnerId = Layer.Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid())
		{
			if (Visited.Contains(OwnerId))
			{
				return MaximumScopeDepth + 1;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Layer, OwnerId);
			if (OwnerIndex == INDEX_NONE
				|| !CanKeepScopedPlacement(
					Layer.Children[OwnerIndex],
					Layer.Children[CurrentIndex]))
			{
				return MaximumScopeDepth + 1;
			}
			++Depth;
			CurrentIndex = OwnerIndex;
			OwnerId = Layer.Children[OwnerIndex].ScopeOwnerChildId;
		}
		return Depth;
	}

	bool IsDescendantOf(
		const FMixtormatLayer& Layer,
		const int32 ChildIndex,
		const FGuid& AncestorId)
	{
		if (!Layer.Children.IsValidIndex(ChildIndex) || !AncestorId.IsValid())
		{
			return false;
		}

		FGuid OwnerId = Layer.Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid() && !Visited.Contains(OwnerId))
		{
			if (OwnerId == AncestorId)
			{
				return true;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Layer, OwnerId);
			if (OwnerIndex == INDEX_NONE)
			{
				return false;
			}
			OwnerId = Layer.Children[OwnerIndex].ScopeOwnerChildId;
		}
		return false;
	}

	int32 FindSubtreeEnd(const FMixtormatLayer& Layer, const int32 RootIndex)
	{
		if (!Layer.Children.IsValidIndex(RootIndex))
		{
			return RootIndex;
		}
		const FGuid RootId = Layer.Children[RootIndex].ChildId;
		int32 End = RootIndex + 1;
		while (Layer.Children.IsValidIndex(End) && IsDescendantOf(Layer, End, RootId))
		{
			++End;
		}
		return End;
	}

	int32 FindSiblingRoot(
		const FMixtormatLayer& Layer,
		const int32 ChildIndex,
		const FGuid& ParentId)
	{
		int32 CurrentIndex = ChildIndex;
		TSet<FGuid> Visited;
		while (Layer.Children.IsValidIndex(CurrentIndex))
		{
			const FMixtormatLayerChild& Current = Layer.Children[CurrentIndex];
			if (Current.ScopeOwnerChildId == ParentId)
			{
				return CurrentIndex;
			}
			if (!Current.ScopeOwnerChildId.IsValid()
				|| Visited.Contains(Current.ScopeOwnerChildId))
			{
				return INDEX_NONE;
			}
			Visited.Add(Current.ScopeOwnerChildId);
			CurrentIndex = FindChildById(Layer, Current.ScopeOwnerChildId);
		}
		return INDEX_NONE;
	}

	bool CanAddScopedChild(const FMixtormatLayer& Layer, const int32 OwnerIndex)
	{
		return Layer.Children.IsValidIndex(OwnerIndex)
			&& GetScopeDepth(Layer, OwnerIndex) < MaximumScopeDepth;
	}

	bool CanKeepScopedPlacement(
		const FMixtormatLayerChild& Owner,
		const FMixtormatLayerChild& Child)
	{
		if (Child.Type == EMixtormatLayerChildType::Mask)
		{
			return CanOwnScopedMasks(Owner);
		}
		if (IsMaskFilter(Child))
		{
			return CanOwnScopedBlurs(Owner);
		}
		return IsFlowWarp(Child) && CanOwnFlowWarp(Owner);
	}

	bool BuildMaskLayerFromPath(const FSoftObjectPath& MaskPath, FMixtormatMaskLayer& OutMask)
	{
		UObject* MaskObject = MaskPath.TryLoad();
		if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
		{
			OutMask.Mask = TSoftObjectPtr<UMixtormatMask>(MaskPath);
			OutMask.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
			OutMask.TilingX = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
			OutMask.TilingY = OutMask.TilingX;
			OutMask.Shaping.Balance = FMath::Clamp(Mask->DefaultBalance, 0.0f, 1.0f);
			OutMask.Shaping.Contrast = Mask->DefaultContrast;
			OutMask.Shaping.Offset = Mask->DefaultOffset;
			OutMask.Shaping.bInvert = Mask->bDefaultInvert;
			return true;
		}
		if (Cast<UTexture2D>(MaskObject))
		{
			OutMask.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
			return true;
		}
		return false;
	}
}

FReply SMixtormat::AddWorkingLayer(const EMixtormatLayerType LayerType)
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}
	if (LayerType != EMixtormatLayerType::Fill && SelectedSurfacePath.IsNull())
	{
		WorkingStatusText = TEXT("Select a library surface first");
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers.AddDefaulted_GetRef();
	Layer.Type = LayerType;
	const int32 LayerNumber = WorkingLayers.Num();

	switch (LayerType)
	{
	case EMixtormatLayerType::Material:
		Layer.DisplayName = SelectedLibrarySurfaceName.IsEmpty()
			? FText::Format(LOCTEXT("MaterialLayerNumber", "Material Layer {0}"), FText::AsNumber(LayerNumber))
			: SelectedLibrarySurfaceName;
		Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SelectedSurfacePath);
		break;
	case EMixtormatLayerType::Fill:
		Layer.DisplayName = FText::Format(LOCTEXT("FillLayerNumber", "Fill Layer {0}"), FText::AsNumber(LayerNumber));
		Layer.bOverrideBaseColor = true;
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		break;
	case EMixtormatLayerType::Effect:
		Layer.DisplayName = FText::Format(LOCTEXT("EffectLayerNumber", "Effect Layer {0}"), FText::AsNumber(LayerNumber));
		Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SelectedSurfacePath);
		break;
	}

	SelectedLayerIndex = WorkingLayers.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::DuplicateSelectedLayer()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FReply::Handled();
	}

	SoloLayerIndex = INDEX_NONE;
	FMixtormatLayer Copy = WorkingLayers[SelectedLayerIndex];
	MixtormatParameterBinding::RegenerateLayerIdentity(Copy);
	Copy.DisplayName = FText::Format(
		LOCTEXT("CopiedLayerName", "{0} Copy"),
		Copy.DisplayName);
	WorkingLayers.Insert(Copy, SelectedLayerIndex + 1);
	MixtormatUI::RemapHeightReferencesAfterInsert(WorkingLayers, SelectedLayerIndex + 1);
	++SelectedLayerIndex;
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::DeleteSelectedLayer()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FReply::Handled();
	}

	SoloLayerIndex = INDEX_NONE;
	const int32 DeletedLayerIndex = SelectedLayerIndex;
	WorkingLayers.RemoveAt(DeletedLayerIndex);
	MixtormatUI::RemapHeightReferencesAfterDelete(WorkingLayers, DeletedLayerIndex);
	// The bottom layer is deletable now, so the stack can empty. Clamping into an empty array
	// would land on -1 by arithmetic accident; say INDEX_NONE outright instead. The compositor
	// already renders an empty stack as the bare substrate.
	SelectedLayerIndex = WorkingLayers.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(DeletedLayerIndex - 1, 0, WorkingLayers.Num() - 1);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::MoveSelectedLayer(const int32 Direction)
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FReply::Handled();
	}

	// Clamped to 0, not 1. Position 0 is an ordinary position a layer may be moved into.
	const int32 TargetIndex = FMath::Clamp(
		SelectedLayerIndex + Direction,
		0,
		WorkingLayers.Num() - 1);
	if (TargetIndex != SelectedLayerIndex)
	{
		SoloLayerIndex = INDEX_NONE;
		const int32 SourceIndex = SelectedLayerIndex;
		WorkingLayers.Swap(SourceIndex, TargetIndex);
		MixtormatUI::RemapHeightReferencesAfterMove(WorkingLayers, SourceIndex, TargetIndex);
		SelectedLayerIndex = TargetIndex;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMixtormat::HandleLayerDropped(
	const int32 SourceLayerIndex,
	const int32 TargetLayerIndex)
{
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !WorkingLayers.IsValidIndex(TargetLayerIndex)
		|| SourceLayerIndex == TargetLayerIndex)
	{
		return FReply::Unhandled();
	}

	SoloLayerIndex = INDEX_NONE;

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
	const int32 DroppedReferences = MixtormatUI::ReorderLayersByPermutation(WorkingLayers, NewOrder);

	// Membership follows position, which is what makes one drag do both directions: landing
	// against a group joins it, landing anywhere else leaves it.
	const FGuid JoinedGroupId = ResolveGroupMembershipAt(TargetLayerIndex);
	const bool bChangedGroup = WorkingLayers[TargetLayerIndex].GroupId != JoinedGroupId;
	WorkingLayers[TargetLayerIndex].GroupId = JoinedGroupId;
	MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);

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
		if (Index < InsertIndex)
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

	SoloLayerIndex = INDEX_NONE;
	const int32 DroppedReferences =
		MixtormatUI::ReorderLayersByPermutation(WorkingLayers, NewOrder);
	// The block carried its GroupId with it, so the run is still whole; validation is here to
	// catch a group the block landed in the middle of.
	MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);

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
	int32 FirstIndex = INDEX_NONE;
	int32 LastIndex = INDEX_NONE;
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !MixtormatLayerGroups::GetGroupRange(WorkingLayers, TargetGroupId, FirstIndex, LastIndex))
	{
		return FReply::Unhandled();
	}
	if (WorkingLayers[SourceLayerIndex].GroupId == TargetGroupId)
	{
		return FReply::Handled();
	}

	// The top of the run. A layer joining a group has to land inside it, and the top is the one
	// position that is unambiguous whether the layer came from above or below.
	const int32 TargetIndex = SourceLayerIndex < FirstIndex ? LastIndex : FirstIndex;
	const FReply Result = HandleLayerDropped(SourceLayerIndex, TargetIndex);

	// HandleLayerDropped derives membership from the neighbours, which is right for a reorder but
	// not for this: the user named the group, so say so rather than letting adjacency decide.
	if (WorkingLayers.IsValidIndex(TargetIndex))
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
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FGuid();
	}
	const FGuid Below = WorkingLayers.IsValidIndex(LayerIndex - 1)
		? WorkingLayers[LayerIndex - 1].GroupId : FGuid();
	const FGuid Above = WorkingLayers.IsValidIndex(LayerIndex + 1)
		? WorkingLayers[LayerIndex + 1].GroupId : FGuid();

	// Between two members of one group means inside it -- refusing there would leave the run
	// split, which is the one thing the contiguity invariant cannot survive.
	if (Below.IsValid() && Below == Above)
	{
		return Below;
	}
	// Against one edge only: keep the layer's own membership if it already matches that
	// neighbour, so reordering inside a group does not shuffle layers out of it.
	const FGuid Own = WorkingLayers[LayerIndex].GroupId;
	if (Own.IsValid() && (Own == Below || Own == Above))
	{
		return Own;
	}
	return FGuid();
}

TArray<int32> SMixtormat::GetSelectedLayerIndices() const
{
	TArray<int32> Indices;
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (SelectedLayerIds.Contains(WorkingLayers[Index].LayerId))
		{
			Indices.Add(Index);
		}
	}
	// Falling back to the inspector's layer keeps the button working before anything has been
	// multi-selected, which is the state the panel opens in.
	if (Indices.IsEmpty() && WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		Indices.Add(SelectedLayerIndex);
	}
	return Indices;
}

bool SMixtormat::CanCreateGroupFromSelection() const
{
	return bHasWorkingMaterial && !GetSelectedLayerIndices().IsEmpty();
}

FText SMixtormat::MakeUniqueGroupName() const
{
	for (int32 Suffix = 1;; ++Suffix)
	{
		const FText Candidate = FText::Format(
			LOCTEXT("GroupNameFormat", "Group {0}"), FText::AsNumber(Suffix));
		const bool bTaken = WorkingLayerGroups.ContainsByPredicate(
			[&Candidate](const FMixtormatLayerGroup& Group)
			{
				return Group.DisplayName.EqualTo(Candidate);
			});
		if (!bTaken)
		{
			return Candidate;
		}
	}
}

FReply SMixtormat::CreateGroupFromSelection()
{
	const TArray<int32> Selected = GetSelectedLayerIndices();
	if (Selected.IsEmpty())
	{
		return FReply::Handled();
	}

	// Gather the selection into one block ending where its topmost layer already sits. Landing it
	// anywhere else would move layers the user did not pick further than grouping requires.
	const int32 TopSelected = Selected.Last();
	TArray<int32> Others;
	Others.Reserve(WorkingLayers.Num() - Selected.Num());
	int32 InsertAt = 0;
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (Selected.Contains(Index))
		{
			continue;
		}
		if (Index < TopSelected)
		{
			++InsertAt;
		}
		Others.Add(Index);
	}

	TArray<int32> NewOrder;
	NewOrder.Reserve(WorkingLayers.Num());
	NewOrder.Append(Others.GetData(), InsertAt);
	NewOrder.Append(Selected);
	for (int32 Index = InsertAt; Index < Others.Num(); ++Index)
	{
		NewOrder.Add(Others[Index]);
	}

	// Identities to follow across the permutation. Expansion and multi-select are already keyed on
	// GUIDs and need nothing; solo and the inspector's layer are still indices.
	const FGuid SoloLayerId = WorkingLayers.IsValidIndex(SoloLayerIndex)
		? WorkingLayers[SoloLayerIndex].LayerId : FGuid();
	const FGuid InspectorLayerId = WorkingLayers.IsValidIndex(SelectedLayerIndex)
		? WorkingLayers[SelectedLayerIndex].LayerId : FGuid();

	const int32 DroppedReferences =
		MixtormatUI::ReorderLayersByPermutation(WorkingLayers, NewOrder);

	FMixtormatLayerGroup& Group = WorkingLayerGroups.AddDefaulted_GetRef();
	Group.DisplayName = MakeUniqueGroupName();
	for (int32 Index = InsertAt; Index < InsertAt + Selected.Num(); ++Index)
	{
		WorkingLayers[Index].GroupId = Group.GroupId;
	}
	const FGuid NewGroupId = Group.GroupId;

	// Taking layers out of another group can leave what remains of it split around the new block.
	// ValidateGroups repairs that the same way it repairs a corrupted asset -- by ungrouping the
	// strays rather than moving layers again -- so count what it took before letting it run.
	int32 StrandedCount = 0;
	{
		TArray<FGuid> MembershipBefore;
		MembershipBefore.Reserve(WorkingLayers.Num());
		for (const FMixtormatLayer& Layer : WorkingLayers)
		{
			MembershipBefore.Add(Layer.GroupId);
		}
		MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);
		for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
		{
			if (MembershipBefore[Index].IsValid() && !WorkingLayers[Index].GroupId.IsValid())
			{
				++StrandedCount;
			}
		}
	}

	const auto FindLayerIndex = [this](const FGuid& LayerId)
	{
		return LayerId.IsValid()
			? WorkingLayers.IndexOfByPredicate(
				[&LayerId](const FMixtormatLayer& Candidate)
				{
					return Candidate.LayerId == LayerId;
				})
			: INDEX_NONE;
	};
	SoloLayerIndex = FindLayerIndex(SoloLayerId);
	SelectedLayerIndex = FindLayerIndex(InspectorLayerId);
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	SelectedGroupId = NewGroupId;
	CollapsedGroupIds.Remove(NewGroupId);
	// The selection has been consumed. Leaving it standing would let a second press of the button
	// pull the same layers straight back out into another new group.
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();

	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	if (DroppedReferences > 0 || StrandedCount > 0)
	{
		// Both losses are consequences of the move, not failures, and neither is visible in the
		// stack -- so they are said once here rather than left for the user to discover.
		WorkingStatusText = FString::Printf(
			TEXT("Grouped %d layers · %d height reference(s) dropped · %d layer(s) left their old group"),
			Selected.Num(), DroppedReferences, StrandedCount);
	}
	else
	{
		WorkingStatusText = FString::Printf(TEXT("Grouped %d layers"), Selected.Num());
	}
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::UngroupLayerGroup(const FGuid GroupId)
{
	if (!MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId))
	{
		return FReply::Handled();
	}
	// Order and contents are untouched; only the membership goes. Shared children go with the
	// group, which is why this is Ungroup and not Delete.
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.GroupId == GroupId)
		{
			Layer.GroupId.Invalidate();
		}
	}
	WorkingLayerGroups.RemoveAll(
		[&GroupId](const FMixtormatLayerGroup& Candidate)
		{
			return Candidate.GroupId == GroupId;
		});
	CollapsedGroupIds.Remove(GroupId);
	if (SelectedGroupId == GroupId)
	{
		SelectedGroupId.Invalidate();
	}
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

bool SMixtormat::IsGroupExpanded(const FGuid& GroupId) const
{
	return !CollapsedGroupIds.Contains(GroupId);
}

FReply SMixtormat::ToggleGroupExpanded(const FGuid GroupId)
{
	if (CollapsedGroupIds.Contains(GroupId))
	{
		CollapsedGroupIds.Remove(GroupId);
	}
	else
	{
		CollapsedGroupIds.Add(GroupId);
	}
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::SelectLayerGroup(const FGuid GroupId)
{
	SelectedGroupId = GroupId;
	// The header, not one of its shared children.
	SelectedGroupChildIndex = INDEX_NONE;
	// One subject for the inspector: picking the group drops the layer-child selection rather
	// than leaving two things looking selected at once.
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	return FReply::Handled();
}

FReply SMixtormat::SetLayerGroupEnabled(const FGuid GroupId, const bool bEnabled)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || Group->bEnabled == bEnabled)
	{
		return FReply::Handled();
	}
	Group->bEnabled = bEnabled;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

bool SMixtormat::IsLayerExpanded(const int32 LayerIndex) const
{
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& ExpandedLayerIds.Contains(WorkingLayers[LayerIndex].LayerId);
}

void SMixtormat::SetLayerExpanded(const int32 LayerIndex, const bool bExpanded)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return;
	}
	const FGuid& LayerId = WorkingLayers[LayerIndex].LayerId;
	if (bExpanded)
	{
		ExpandedLayerIds.Add(LayerId);
	}
	else
	{
		ExpandedLayerIds.Remove(LayerId);
	}
}

bool SMixtormat::IsLayerMultiSelected(const int32 LayerIndex) const
{
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& SelectedLayerIds.Contains(WorkingLayers[LayerIndex].LayerId);
}

void SMixtormat::UpdateMultiSelection(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return;
	}
	const FGuid ClickedId = WorkingLayers[LayerIndex].LayerId;

	// Read live rather than plumbed through the row: OnSelected is a bare FSimpleDelegate, and it
	// runs synchronously out of the row's OnMouseButtonDown, so the keys held are still the keys
	// that were held for this click.
	const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
	const bool bToggle = Modifiers.IsControlDown() || Modifiers.IsCommandDown();
	const bool bExtend = Modifiers.IsShiftDown();

	if (bExtend && SelectionAnchorLayerId.IsValid())
	{
		const int32 AnchorIndex = WorkingLayers.IndexOfByPredicate(
			[this](const FMixtormatLayer& Candidate)
			{
				return Candidate.LayerId == SelectionAnchorLayerId;
			});
		if (AnchorIndex != INDEX_NONE)
		{
			SelectedLayerIds.Reset();
			const int32 First = FMath::Min(AnchorIndex, LayerIndex);
			const int32 Last = FMath::Max(AnchorIndex, LayerIndex);
			for (int32 Index = First; Index <= Last; ++Index)
			{
				SelectedLayerIds.Add(WorkingLayers[Index].LayerId);
			}
			return;
		}
	}

	if (bToggle)
	{
		// A plain toggle, including off. SelectedLayerIndex follows the click regardless, because
		// what the inspector shows and what the Group button acts on are two different questions.
		if (SelectedLayerIds.Contains(ClickedId))
		{
			SelectedLayerIds.Remove(ClickedId);
		}
		else
		{
			SelectedLayerIds.Add(ClickedId);
		}
		SelectionAnchorLayerId = ClickedId;
		return;
	}

	// Clicking inside an existing multi-selection keeps it. Right-click runs through here before
	// the context menu opens, so collapsing here would mean picking three layers and then being
	// offered "Create Group" for one of them -- the selection destroyed by the act of acting on it.
	if (SelectedLayerIds.Num() > 1 && SelectedLayerIds.Contains(ClickedId))
	{
		return;
	}

	SelectedLayerIds.Reset();
	SelectedLayerIds.Add(ClickedId);
	SelectionAnchorLayerId = ClickedId;
}

FReply SMixtormat::SelectWorkingLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	UpdateMultiSelection(LayerIndex);
	SyncSelectedLayerControls();
	RebuildMaskList();
	// No RebuildLayerList here. Selection highlight is an attribute lambda on each row, so it
	// repaints on its own -- and a rebuild would destroy the row that is, right now, part way
	// through opening its context menu on its own SMenuAnchor.
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ClusterIds)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FReply SMixtormat::SelectWorkingChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	const bool bEffect = ResolveChild(LayerIndex, ChildIndex)->Type
		== EMixtormatLayerChildType::Effect;
	SelectedEffectIndex = bEffect ? ChildIndex : INDEX_NONE;
	SelectedMaskIndex = bEffect ? INDEX_NONE : ChildIndex;
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	// No RebuildLayerList() here: every row's selected-tint and state is attribute-bound already,
	// so nothing needs new widgets. Rebuilding tore down the very row a right-click had just opened
	// its context menu on, closing it before it could show.
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ClusterIds)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedLayerEffect()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedEffectIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedEffectIndex);
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedLayerEffect() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedEffectIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedEffectIndex);
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

const FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

int32 SMixtormat::GetSelectedChildIndex() const
{
	if (SelectedLayerIndex == INDEX_NONE && SelectedGroupId.IsValid())
	{
		return SelectedGroupChildIndex;
	}
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return INDEX_NONE;
	}

	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	if (Layer.Children.IsValidIndex(SelectedEffectIndex)
		&& Layer.Children[SelectedEffectIndex].Type == EMixtormatLayerChildType::Effect)
	{
		return SelectedEffectIndex;
	}
	if (Layer.Children.IsValidIndex(SelectedMaskIndex)
		&& (Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Mask
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Generated
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Craquelure
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::ColorId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Filter
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::HsvFilter
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::RandomId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::RampId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::PatternId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Blur
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Curvature))
	{
		return SelectedMaskIndex;
	}
	return INDEX_NONE;
}

FText SMixtormat::GetSelectedBadgeText() const
{
	// The inspector strip mirrors the row that selected it, so it prints the same derived mark --
	// the child's when a child is selected, the layer's otherwise.
	if (const FMixtormatLayerChild* GroupChild =
		SelectedLayerIndex == INDEX_NONE ? ResolveChild(INDEX_NONE, GetSelectedChildIndex()) : nullptr)
	{
		return MixtormatLayerBadges::ForChild(*GroupChild);
	}
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FText::GetEmpty();
	}
	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	const int32 ChildIndex = GetSelectedChildIndex();
	return Layer.Children.IsValidIndex(ChildIndex)
		? MixtormatLayerBadges::ForChild(Layer.Children[ChildIndex])
		: MixtormatLayerBadges::ForLayer(Layer);
}

void SMixtormat::SetWorkingLayerEnabled(const ECheckBoxState CheckState, const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return;
	}

	WorkingLayers[LayerIndex].bEnabled = CheckState == ECheckBoxState::Checked;
	if (SelectedLayerIndex == LayerIndex)
	{
		bHasSelectedLayer = true;
	}
	RefreshLayeredPreview();
}

void SMixtormat::SyncSelectedLayerControls()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		bHasSelectedLayer = false;
		return;
	}

	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	CurrentTiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
	CurrentRoughnessBias = Layer.RoughnessBias;
	CurrentRoughnessContrast = Layer.RoughnessContrast;
	CurrentRoughnessOffset = Layer.RoughnessOffset;

	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(GetLayerDisplayName(SelectedLayerIndex));
	}
	if (SelectedIdentityText.IsValid())
	{
		// The row's source field, not the layer's kind: "MAT - RUST ORANGE" answers what the layer
		// is made of, which is what the stack prints in the same position.
		SelectedIdentityText->SetText(GetLayerSourceText(SelectedLayerIndex));
	}
	if (SelectedThumbnailBox.IsValid())
	{
		const int32 RowThumbnailCount = LayerThumbnails.Num();
		SelectedThumbnailBox->SetContent(BuildLayerThumbnail(SelectedLayerIndex));
		// BuildLayerThumbnail parks what it made in the row list. The strip is not a row and is
		// remade on every selection, so its thumbnail moves to its own slot and replaces the last
		// one instead of piling up until the stack next rebuilds.
		if (LayerThumbnails.Num() > RowThumbnailCount)
		{
			SelectedStripThumbnail = LayerThumbnails.Pop();
		}
	}
	if (SelectedMapsText.IsValid())
	{
		if (Layer.Type == EMixtormatLayerType::Fill)
		{
			SelectedMapsText->SetText(LOCTEXT("FillLayerMaps", "Generated BC · RAM"));
		}
		else if (!Layer.SourceComposition.IsNull())
		{
			SelectedMapsText->SetText(Layer.SourceComposition.LoadSynchronous()
				? LOCTEXT("ReferenceLayerMaps", "Live composition channels")
				: LOCTEXT("MissingReferenceLayerMaps", "Missing referenced composition"));
		}
		else if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
		{
			SelectedMapsText->SetText(FText::FromString(FString::Printf(
				TEXT("BC %s  N %s  %s %s"),
				Surface->BaseColor ? TEXT("✓") : TEXT("—"),
				Surface->Normal ? TEXT("✓") : TEXT("—"),
				MixtormatUI::PackedMapLabel(*Surface),
				Surface->RoughnessAOMetallic ? TEXT("✓") : TEXT("—"))));
		}
	}

	const int32 SelectedChildIndex = GetSelectedChildIndex();
	if (Layer.Children.IsValidIndex(SelectedChildIndex))
	{
		const FMixtormatLayerChild& Child = Layer.Children[SelectedChildIndex];
		if (SelectedSurfaceText.IsValid())
		{
			SelectedSurfaceText->SetText(GetLayerChildName(Child));
		}
		if (SelectedIdentityText.IsValid())
		{
			// Mirror the row's target/owner label so nested selection keeps its context.
			SelectedIdentityText->SetText(
				GetLayerChildSourceText(SelectedLayerIndex, SelectedChildIndex));
		}
		if (SelectedMapsText.IsValid())
		{
			SelectedMapsText->SetText(Child.Type == EMixtormatLayerChildType::Effect
				? LOCTEXT("SelectedEffectMaps", "FX")
				: Child.Type == EMixtormatLayerChildType::Generated
					? LOCTEXT("SelectedGeneratedMaps", "GENERATED MASK")
					: Child.Type == EMixtormatLayerChildType::Craquelure
						? LOCTEXT("SelectedCraquelureMaps", "CRAQUELURE")
						: Child.Type == EMixtormatLayerChildType::ColorId
							? LOCTEXT("SelectedColorIdMaps", "COLOR ID")
							: Child.Type == EMixtormatLayerChildType::Filter
								? LOCTEXT("SelectedFilterMaps", "CLUSTER IDS · INTEGER DATA")
							: Child.Type == EMixtormatLayerChildType::HsvFilter
								? LOCTEXT("SelectedHsvFilterMaps", "HSV FROM IDS · ALBEDO")
							: Child.Type == EMixtormatLayerChildType::RandomId
								? LOCTEXT("SelectedRandomIdMaps", "RANDOM FROM IDS")
							: Child.Type == EMixtormatLayerChildType::RampId
								? LOCTEXT("SelectedRampIdMaps", "RAMP FROM IDS · HEIGHT + NORMAL")
							: Child.Type == EMixtormatLayerChildType::PatternId
								? LOCTEXT("SelectedPatternIdMaps", "PATTERN IDS · INTEGER DATA · UV · RELIEF")
								: LOCTEXT("SelectedMaskMaps", "MASK"));
		}
	}
}

FReply SMixtormat::AssignMaskToLayer(const int32 LayerIndex, const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatMaskLayer NewMask;
	if (!BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}

	// Bisecting a freeze: back to the pre-session rule while the cause is narrowed down.
	// First mask on the container replaces, later ones multiply. The chain seeds at black, so
	// Multiply on a first mask would resolve to nothing -- hence the special case.
	const bool bHasTopLevelMask = Layer.Children.ContainsByPredicate(
		[](const FMixtormatLayerChild& Existing)
		{
			return Existing.Type == EMixtormatLayerChildType::Mask
				&& !Existing.ScopeOwnerChildId.IsValid();
		});
	NewMask.BlendMode = bHasTopLevelMask
		? EMixtormatMaskBlendMode::Multiply
		: EMixtormatMaskBlendMode::Replace;
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Mask;
	Child.Mask = MoveTemp(NewMask);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

// A Blur scoped beneath the mask it softens, inserted after any blurs already on it so the
// order in the stack is the order they were added. It is a child rather than a field on the mask
// for one reason: only a child can be a driver's destination, be published as a source, or stand
// as one definition behind several instances.
FReply SMixtormat::AddBlurToMask(const int32 LayerIndex, const int32 OwnerChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(OwnerChildIndex)
		|| !CanOwnScopedBlurs(WorkingLayers[LayerIndex].Children[OwnerChildIndex])
		|| !CanAddScopedChild(WorkingLayers[LayerIndex], OwnerChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid OwnerId = Layer.Children[OwnerChildIndex].ChildId;
	const int32 InsertAt = FindSubtreeEnd(Layer, OwnerChildIndex);

	FMixtormatLayerChild BlurChild;
	BlurChild.Type = EMixtormatLayerChildType::Blur;
	BlurChild.ScopeOwnerChildId = OwnerId;
	Layer.Children.Insert(MoveTemp(BlurChild), InsertAt);

	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = InsertAt;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

// The other mask filter, inserted the same way and for the same reasons as a Blur.
FReply SMixtormat::AddCurvatureToMask(const int32 LayerIndex, const int32 OwnerChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(OwnerChildIndex)
		|| !CanOwnScopedBlurs(WorkingLayers[LayerIndex].Children[OwnerChildIndex])
		|| !CanAddScopedChild(WorkingLayers[LayerIndex], OwnerChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid OwnerId = Layer.Children[OwnerChildIndex].ChildId;
	const int32 InsertAt = FindSubtreeEnd(Layer, OwnerChildIndex);

	FMixtormatLayerChild CurvatureChild;
	CurvatureChild.Type = EMixtormatLayerChildType::Curvature;
	CurvatureChild.ScopeOwnerChildId = OwnerId;
	Layer.Children.Insert(MoveTemp(CurvatureChild), InsertAt);

	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = InsertAt;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AssignScopedMaskToChild(
	const int32 LayerIndex,
	const int32 OwnerChildIndex,
	const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(OwnerChildIndex)
		|| !CanOwnScopedMasks(WorkingLayers[LayerIndex].Children[OwnerChildIndex])
		|| !CanAddScopedChild(WorkingLayers[LayerIndex], OwnerChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatMaskLayer NewMask;
	if (!BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}
	// Bisecting a freeze: pre-session value while the cause is narrowed down.
	NewMask.BlendMode = EMixtormatMaskBlendMode::Multiply;

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid OwnerId = Layer.Children[OwnerChildIndex].ChildId;
	const int32 InsertAt = FindSubtreeEnd(Layer, OwnerChildIndex);

	FMixtormatLayerChild ScopedMask;
	ScopedMask.Type = EMixtormatLayerChildType::Mask;
	ScopedMask.ScopeOwnerChildId = OwnerId;
	ScopedMask.Mask = MoveTemp(NewMask);
	Layer.Children.Insert(MoveTemp(ScopedMask), InsertAt);

	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = InsertAt;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

// Swap the material under a layer and keep everything the layer says about it.
//
// The difference from deleting the layer and adding a new one -- which was the only way to do
// this -- is everything that is not the surface: the children stacked on it, the UV transform,
// the roughness shaping, the overrides, the layer's position in the stack and its selection.
// Re-authoring all of that to try a different material is the reason this exists.
//
// The name follows the surface, because nothing else sets it: layer names are derived at
// creation and there is no rename, so a row still reading "Rusted Iron" over polished steel
// would be wrong with no way to correct it.
FReply SMixtormat::ReplaceSurfaceInLayer(const int32 LayerIndex, const FSoftObjectPath SurfacePath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.Type != EMixtormatLayerType::Material && Layer.Type != EMixtormatLayerType::Effect)
	{
		return FReply::Handled();
	}
	const UMixtormatSurface* ReplacementSurface = Cast<UMixtormatSurface>(SurfacePath.TryLoad());
	if (!ReplacementSurface)
	{
		return FReply::Handled();
	}

	Layer.SourceComposition.Reset();
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SurfacePath);
	Layer.DisplayName = ReplacementSurface->DisplayName.IsEmpty()
		? FText::FromString(ReplacementSurface->GetName()) : ReplacementSurface->DisplayName;
	for (const FMixtormatSurfaceEntry& Surface : FMixtormatRegistry::GetSurfaces())
	{
		if (Surface.AssetPath == SurfacePath)
		{
			Layer.DisplayName = Surface.DisplayName;
			break;
		}
	}

	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ReplaceMaskInLayer(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| ResolveChild(LayerIndex, ChildIndex)->Type != EMixtormatLayerChildType::Mask)
	{
		return FReply::Handled();
	}

	UObject* MaskObject = MaskPath.TryLoad();
	FMixtormatMaskLayer Replacement = ResolveChild(LayerIndex, ChildIndex)->Mask;
	Replacement.Mask.Reset();
	Replacement.MaskTexture.Reset();
	Replacement.PublishedSourceLayerId.Invalidate();
	Replacement.PublishedSourceChildId.Invalidate();
	Replacement.PublishedSourceOutput = NAME_None;
	if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
	{
		Replacement.Mask = TSoftObjectPtr<UMixtormatMask>(MaskPath);
		Replacement.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
		Replacement.TilingX = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
		Replacement.TilingY = Replacement.TilingX;
		// Offset is deliberately not carried here, unlike when a mask is first added: replacing
		// the picture under an existing mask keeps the offset the user dialled against it.
		Replacement.Shaping.Balance = FMath::Clamp(Mask->DefaultBalance, 0.0f, 1.0f);
		Replacement.Shaping.Contrast = Mask->DefaultContrast;
		Replacement.Shaping.bInvert = Mask->bDefaultInvert;
	}
	else if (Cast<UTexture2D>(MaskObject))
	{
		Replacement.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
	}
	else
	{
		return FReply::Handled();
	}

	ResolveChild(LayerIndex, ChildIndex)->Mask = MoveTemp(Replacement);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ClearLayerMask(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
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

	for (int32 ChildIndex = Layer.Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
	{
		const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
		if (Child.Type == EMixtormatLayerChildType::Mask
			&& !Child.ScopeOwnerChildId.IsValid())
		{
			Layer.Children.RemoveAt(ChildIndex, FindSubtreeEnd(Layer, ChildIndex) - ChildIndex);
		}
	}

	if (SelectedLayerIndex == LayerIndex)
	{
		SelectedEffectIndex = FindChildById(Layer, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer, SelectedMaskId);
		if ((SelectedEffectId.IsValid() && SelectedEffectIndex == INDEX_NONE)
			|| (SelectedMaskId.IsValid() && SelectedMaskIndex == INDEX_NONE))
		{
			bBypassSelectedChild = false;
		}
		SyncSelectedLayerControls();
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

// Masks and the filters scoped under them, because the same menu action removes either and the
// row dispatch cannot tell them apart. Removing a mask takes its filters with it: they name it by
// ChildId, and a filter left behind would point at a child that no longer exists and be dropped
// silently by the gather instead of visibly by this.
FReply SMixtormat::RemoveMaskFromLayer(const int32 LayerIndex, const int32 ChildIndex)
{
	const bool bRemovable = WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& (ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Mask
			|| ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Blur
			|| ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Curvature);
	if (bRemovable)
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		const int32 SubtreeEnd = FindSubtreeEnd(Layer, ChildIndex);

		// Selection is restored by identity rather than by shifting indices down one, because a
		// mask takes its scoped filters with it and that is any number of children, not one.
		// RemoveLayerEffect has always done it this way for the same reason.
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

		Layer.Children.RemoveAt(ChildIndex, SubtreeEnd - ChildIndex);

		if (SelectedLayerIndex == LayerIndex)
		{
			const auto FindById = [&Layer](const FGuid& Id)
			{
				return Id.IsValid()
					? Layer.Children.IndexOfByPredicate(
						[&Id](const FMixtormatLayerChild& Candidate)
						{
							return Candidate.ChildId == Id;
						})
					: INDEX_NONE;
			};
			SelectedEffectIndex = FindById(SelectedEffectId);
			SelectedMaskIndex = FindById(SelectedMaskId);
			if (SelectedMaskIndex == INDEX_NONE)
			{
				bBypassSelectedChild = false;
			}
			SyncSelectedLayerControls();
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
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
	const int32 TargetRootIndex = FindSiblingRoot(Layer, TargetChildIndex, SourceParentId);
	if (TargetRootIndex == INDEX_NONE || TargetRootIndex == SourceChildIndex)
	{
		return FReply::Unhandled();
	}

	const int32 SourceSubtreeEnd = FindSubtreeEnd(Layer, SourceChildIndex);
	const int32 TargetSubtreeEnd = FindSubtreeEnd(Layer, TargetRootIndex);
	if (TargetRootIndex < SourceSubtreeEnd && TargetSubtreeEnd > SourceChildIndex)
	{
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
		SelectedEffectIndex = FindChildById(Layer, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer, SelectedMaskId);
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::DuplicateLayerChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const int32 InsertAt = FindSubtreeEnd(Layer, ChildIndex);
	TArray<FMixtormatLayerChild> Copies;
	for (int32 CopyIndex = ChildIndex; CopyIndex < InsertAt; ++CopyIndex)
	{
		Copies.Add(Layer.Children[CopyIndex]);
	}

	TMap<FGuid, FGuid> ChildIdRemap;
	for (FMixtormatLayerChild& Copy : Copies)
	{
		const FGuid OldChildId = Copy.ChildId;
		Copy.SourceLayerId = FGuid();
		Copy.SourceChildId = FGuid();
		MixtormatParameterBinding::RegenerateChildIdentity(Copy);
		ChildIdRemap.Add(OldChildId, Copy.ChildId);
	}
	for (FMixtormatLayerChild& Copy : Copies)
	{
		if (const FGuid* NewOwnerId = ChildIdRemap.Find(Copy.ScopeOwnerChildId))
		{
			Copy.ScopeOwnerChildId = *NewOwnerId;
		}
	}

	const int32 NewChildIndex = InsertAt;
	for (int32 CopyIndex = 0; CopyIndex < Copies.Num(); ++CopyIndex)
	{
		Layer.Children.Insert(MoveTemp(Copies[CopyIndex]), InsertAt + CopyIndex);
	}
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children[NewChildIndex].Type == EMixtormatLayerChildType::Effect
		? NewChildIndex
		: INDEX_NONE;
	SelectedMaskIndex = Layer.Children[NewChildIndex].Type == EMixtormatLayerChildType::Effect
		? INDEX_NONE
		: NewChildIndex;
	bHasSelectedLayer = true;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
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
	if (IsMaskFilter(SourceLayer.Children[ChildIndex]))
	{
		return FReply::Unhandled();
	}

	const FGuid OldLayerId = SourceLayer.LayerId;
	const FGuid NewLayerId = WorkingLayers[DestLayerIndex].LayerId;
	const int32 SubtreeEnd = FindSubtreeEnd(SourceLayer, ChildIndex);
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
		const int32 TopLevelRoot = FindSiblingRoot(DestLayer, DestChildIndex, FGuid());
		InsertAt = TopLevelRoot == INDEX_NONE ? DestLayer.Children.Num() : TopLevelRoot;
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		const FGuid MovedChildId = MovedChildren[MoveIndex].ChildId;
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
		MixtormatParameterBinding::RemapChildParent(
			WorkingLayers, MovedChildId, OldLayerId, NewLayerId);
	}

	SetLayerExpanded(DestLayerIndex, true);
	SelectWorkingChild(DestLayerIndex, InsertAt);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

void SMixtormat::CopyLayerChild(const int32 LayerIndex, const int32 ChildIndex, const bool bAsInstance)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	ChildClipboard = Child;
	bChildClipboardIsInstance = bAsInstance;
	// Copying an instance as an instance yields its source, not the instance. Pointing at the
	// instance would build a chain for the resolver to unwind with nothing gained by it.
	if (Child.IsInstance())
	{
		ChildClipboardSourceLayerId = Child.SourceLayerId;
		ChildClipboardSourceChildId = Child.SourceChildId;
	}
	else
	{
		ChildClipboardSourceLayerId = WorkingLayers[LayerIndex].LayerId;
		ChildClipboardSourceChildId = Child.ChildId;
	}
}

void SMixtormat::CopyInstanceMaskFromWear(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}

	const FMixtormatLayer& SourceLayer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& SourceChild = SourceLayer.Children[ChildIndex];
	if (SourceChild.Type != EMixtormatLayerChildType::Effect)
	{
		return;
	}

	EMixtormatEffectType Type = SourceChild.Effect.ProceduralType;
	if (const UMixtormatEffect* Asset = SourceChild.Effect.Effect.LoadSynchronous())
	{
		Type = Asset->EffectType;
	}
	if (Type != EMixtormatEffectType::WornEdges)
	{
		return;
	}

	FMixtormatLayerChild PublishedMask;
	PublishedMask.Type = EMixtormatLayerChildType::Mask;
	PublishedMask.Mask.bEnabled = true;
	PublishedMask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	PublishedMask.Mask.Weight = 1.0f;
	PublishedMask.Mask.PublishedSourceLayerId = SourceLayer.LayerId;
	PublishedMask.Mask.PublishedSourceChildId = SourceChild.ChildId;
	PublishedMask.Mask.PublishedSourceOutput = TEXT("Wear");

	ChildClipboard = MoveTemp(PublishedMask);
	ChildClipboardSourceLayerId.Invalidate();
	ChildClipboardSourceChildId.Invalidate();
	bChildClipboardIsInstance = false;
	WorkingStatusText = TEXT("Wear instance mask copied");
}

void SMixtormat::CopyInstanceMaskFromPatternGap(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}

	const FMixtormatLayer& SourceLayer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& SourceChild = SourceLayer.Children[ChildIndex];
	if (SourceChild.Type != EMixtormatLayerChildType::PatternId)
	{
		return;
	}

	FMixtormatLayerChild PublishedMask;
	PublishedMask.Type = EMixtormatLayerChildType::Mask;
	PublishedMask.Mask.bEnabled = true;
	PublishedMask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	PublishedMask.Mask.Weight = 1.0f;
	PublishedMask.Mask.PublishedSourceLayerId = SourceLayer.LayerId;
	PublishedMask.Mask.PublishedSourceChildId = SourceChild.ChildId;
	PublishedMask.Mask.PublishedSourceOutput = TEXT("Gap");

	ChildClipboard = MoveTemp(PublishedMask);
	ChildClipboardSourceLayerId.Invalidate();
	ChildClipboardSourceChildId.Invalidate();
	bChildClipboardIsInstance = false;
	WorkingStatusText = TEXT("Pattern gap instance mask copied");
}

bool SMixtormat::CanPasteLayerChild() const
{
	return ChildClipboard.IsSet() && !IsMaskFilter(ChildClipboard.GetValue());
}

int32 SMixtormat::ResolveInstanceInsertIndex(
	const int32 DestLayerIndex,
	const int32 AnchorChildIndex) const
{
	if (!ChildClipboard.IsSet() || !WorkingLayers.IsValidIndex(DestLayerIndex))
	{
		return INDEX_NONE;
	}
	const FMixtormatLayer& DestLayer = WorkingLayers[DestLayerIndex];

	// A layer header names no row and means the top; a child row means directly above that row.
	const int32 Preferred = DestLayer.Children.IsValidIndex(AnchorChildIndex)
		? AnchorChildIndex
		: 0;

	// A source in this same layer has to stay earlier than its instance, and the source never
	// moves to make that true -- so the instance moves down instead, to the first slot after it.
	// Cross-layer sources are already earlier by whole layers and keep the asked-for row.
	int32 Insert = Preferred;
	if (DestLayer.LayerId == ChildClipboardSourceLayerId)
	{
		const int32 SourceChildIndex = DestLayer.Children.IndexOfByPredicate(
			[this](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == ChildClipboardSourceChildId;
			});
		if (SourceChildIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		Insert = FMath::Max(Insert, SourceChildIndex + 1);
	}

	// Still classified, never assumed. This only picks a candidate; the policy decides.
	return MixtormatParameterBinding::ClassifyInstancePlacement(
		WorkingLayers,
		ChildClipboardSourceLayerId,
		ChildClipboardSourceChildId,
		DestLayer.LayerId,
		Insert) == MixtormatParameterBinding::EInstancePlacement::Valid
		? Insert
		: INDEX_NONE;
}

bool SMixtormat::CanPasteChildInstance(const int32 DestLayerIndex, const int32 AnchorChildIndex) const
{
	return ResolveInstanceInsertIndex(DestLayerIndex, AnchorChildIndex) != INDEX_NONE;
}

FText SMixtormat::GetChildInstancePasteReason(
	const int32 DestLayerIndex,
	const int32 AnchorChildIndex) const
{
	if (!ChildClipboard.IsSet())
	{
		return LOCTEXT("InstanceNothingCopied", "Nothing copied. Use Copy as Instance on a child first.");
	}
	if (!WorkingLayers.IsValidIndex(DestLayerIndex))
	{
		return FText::GetEmpty();
	}
	const int32 Insert = ResolveInstanceInsertIndex(DestLayerIndex, AnchorChildIndex);
	if (Insert != INDEX_NONE)
	{
		return LOCTEXT("InstancePasteReady", "Place a live instance of the copied child.");
	}
	using EPlacement = MixtormatParameterBinding::EInstancePlacement;
	switch (MixtormatParameterBinding::ClassifyInstancePlacement(
		WorkingLayers,
		ChildClipboardSourceLayerId,
		ChildClipboardSourceChildId,
		WorkingLayers[DestLayerIndex].LayerId,
		WorkingLayers[DestLayerIndex].Children.IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : 0))
	{
	case EPlacement::SourceMissing:
		return LOCTEXT("InstanceSourceGone", "The copied child no longer exists.");
	case EPlacement::SelfReference:
		return LOCTEXT("InstanceSelf", "A child cannot be an instance of itself.");
	default:
		return LOCTEXT(
			"InstanceOrder",
			"The source composites after this layer. An instance can only read a child that resolves before it -- paste into a layer below the source.");
	}
}

FReply SMixtormat::PasteLayerChild(const int32 LayerIndex)
{
	if (!CanPasteLayerChild() || !WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Unhandled();
	}
	// A plain paste is a duplicate: fresh identity, and no tie to where it came from.
	FMixtormatLayerChild Pasted = ChildClipboard.GetValue();
	Pasted.SourceLayerId = FGuid();
	Pasted.SourceChildId = FGuid();
	// A layer-header paste has no feature owner. Duplicate the mask payload, not a
	// placement link that may name a child in another layer.
	Pasted.ScopeOwnerChildId.Invalidate();
	const int32 NewChildIndex = WorkingLayers[LayerIndex].Children.Add(MoveTemp(Pasted));
	MixtormatParameterBinding::RegenerateChildIdentity(WorkingLayers[LayerIndex].Children[NewChildIndex]);
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, NewChildIndex);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::PasteChildInstance(const int32 LayerIndex, const int32 AnchorChildIndex)
{
	int32 Insert = ResolveInstanceInsertIndex(LayerIndex, AnchorChildIndex);
	if (Insert == INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	FMixtormatLayerChild Instance = ChildClipboard.GetValue();
	Instance.ChildId = FGuid::NewGuid();
	Instance.SourceLayerId = ChildClipboardSourceLayerId;
	Instance.SourceChildId = ChildClipboardSourceChildId;
	Instance.ScopeOwnerChildId.Invalidate();

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.Children.IsValidIndex(AnchorChildIndex)
		&& CanAddScopedChild(Layer, AnchorChildIndex)
		&& CanKeepScopedPlacement(Layer.Children[AnchorChildIndex], Instance))
	{
		const int32 ScopedInsert = FindSubtreeEnd(Layer, AnchorChildIndex);
		// Same-layer instances may have to remain below their source. Only attach when
		// that ordering still permits a contiguous owner subtree.
		if (Insert <= ScopedInsert)
		{
			Insert = ScopedInsert;
			Instance.ScopeOwnerChildId = Layer.Children[AnchorChildIndex].ChildId;
		}
	}
	Layer.Children.Insert(MoveTemp(Instance), Insert);
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Insert);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

void SMixtormat::SyncChildInstances()
{
	FGuid SelectedChildId;
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FMixtormatLayer& SelectedLayer = WorkingLayers[SelectedLayerIndex];
		const int32 SelectedChildIndex = GetSelectedChildIndex();
		if (SelectedLayer.Children.IsValidIndex(SelectedChildIndex))
		{
			SelectedChildId = SelectedLayer.Children[SelectedChildIndex].ChildId;
		}
	}

	// Against a snapshot: an instance may name a child in a layer this loop has already rewritten,
	// and a chain has to read authored sources rather than half-updated mirrors.
	const TArray<FMixtormatLayer> Snapshot = WorkingLayers;
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		MixtormatParameterBinding::ResolveChildInstances(
			Snapshot, Layer);
	}

	// An instance source can change its payload kind. Keep selection on the same identity and move
	// it to the matching effect/mask selection lane after the mirror updates.
	if (SelectedChildId.IsValid() && WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FMixtormatLayer& SelectedLayer = WorkingLayers[SelectedLayerIndex];
		const int32 SelectedChildIndex = FindChildById(SelectedLayer, SelectedChildId);
		if (SelectedLayer.Children.IsValidIndex(SelectedChildIndex))
		{
			const bool bEffect = SelectedLayer.Children[SelectedChildIndex].Type
				== EMixtormatLayerChildType::Effect;
			SelectedEffectIndex = bEffect ? SelectedChildIndex : INDEX_NONE;
			SelectedMaskIndex = bEffect ? INDEX_NONE : SelectedChildIndex;
		}
	}
}

bool SMixtormat::IsSelectedChildInstance() const
{
	const int32 ChildIndex = GetSelectedChildIndex();
	return WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[SelectedLayerIndex].Children[ChildIndex].IsInstance();
}

bool SMixtormat::IsSelectedInstanceBroken() const
{
	if (!IsSelectedChildInstance())
	{
		return false;
	}
	const FMixtormatLayerChild& Child =
		WorkingLayers[SelectedLayerIndex].Children[GetSelectedChildIndex()];
	return MixtormatParameterBinding::FindChild(
		WorkingLayers, Child.SourceLayerId, Child.SourceChildId) == nullptr;
}

FText SMixtormat::GetSelectedInstanceSourceText() const
{
	if (!IsSelectedChildInstance())
	{
		return FText::GetEmpty();
	}
	const FMixtormatLayerChild& Child =
		WorkingLayers[SelectedLayerIndex].Children[GetSelectedChildIndex()];
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Child.SourceLayerId)
		{
			continue;
		}
		for (const FMixtormatLayerChild& Candidate : Layer.Children)
		{
			if (Candidate.ChildId == Child.SourceChildId)
			{
				// Named whether or not the source layer is visible. Hiding a layer stops it
				// compositing; it does not stop its children owning their data.
				return FText::Format(
					LOCTEXT("InstanceSourceLine", "Source: {0} / {1}"),
					Layer.DisplayName,
					GetLayerChildName(Candidate));
			}
		}
	}
	return LOCTEXT("InstanceSourceBroken", "Source is missing. Showing the last values it gave.");
}

bool SMixtormat::IsParameterLocked(const FMixtormatParameterAddress& Target) const
{
	if (!Target.IsValid() || !Target.ChildId.IsValid())
	{
		return false;
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Target.LayerId)
		{
			continue;
		}
		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			if (Child.ChildId == Target.ChildId)
			{
				if (!Child.IsInstance())
				{
					return false;
				}
				const bool bLocalMaskBlend = Child.Type == EMixtormatLayerChildType::Mask
					&& Target.Owner == EMixtormatParameterOwnerType::Mask
					&& Target.Parameter == GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, BlendMode);
				const bool bLocalMaskInvert = Child.Type == EMixtormatLayerChildType::Mask
					&& Target.Owner == EMixtormatParameterOwnerType::MaskShaping
					&& Target.Parameter == GET_MEMBER_NAME_CHECKED(FMixtormatMaskShaping, bInvert);
				return !bLocalMaskBlend && !bLocalMaskInvert;
			}
		}
	}
	return false;
}

TSharedRef<SWidget> SMixtormat::BuildInstanceBanner()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	// A band above the rows rather than a wash over them: the values still have to be read, and
	// what changes is who may write them.
	auto Action = [this, &Style](const FText& Label, const FText& Hint, TFunction<void()> OnClick)
	{
		return SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(MixtormatTokens::RowGap)
			.ToolTipText(Hint)
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(Label)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
			];
	};

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return IsSelectedChildInstance() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.Padding(FMargin(MixtormatTokens::CardGap, 0.0f, MixtormatTokens::CardGap, MixtormatTokens::CardGap))
		[
			SNew(SBorder)
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
			.Padding(FMargin(MixtormatTokens::CardGap))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetSelectedInstanceSourceText(); })
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				.Padding(0.0f, MixtormatTokens::RowGap, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"InstanceReadOnlyHint",
						"Inherited from the source and read-only here. Break Instance to edit a copy."))
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				.Padding(0.0f, MixtormatTokens::RowGap, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::RowGap, 0.0f)
					[
						Action(
							LOCTEXT("InstanceGoToSource", "Go to Source"),
							LOCTEXT("InstanceGoToSourceHint", "Select the child this instance mirrors."),
							[this]() { GoToChildInstanceSource(SelectedLayerIndex, GetSelectedChildIndex()); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::RowGap, 0.0f)
					[
						Action(
							LOCTEXT("InstanceBreak", "Break Instance"),
							LOCTEXT("InstanceBreakHint", "Keep the values it is showing as this child's own and edit them here."),
							[this]() { BreakChildInstanceAt(SelectedLayerIndex, GetSelectedChildIndex()); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMixtormatChip)
						.Text(LOCTEXT("InstanceReplaceSource", "Replace Source"))
						.OnGetMenuContent_Lambda([this]()
						{
							return BuildReplaceInstanceSourceMenu(SelectedLayerIndex, GetSelectedChildIndex());
						})
					]
				]
			]
		];
}

FReply SMixtormat::GoToChildInstanceSource(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Unhandled();
	}
	const FGuid SourceLayerId = ResolveChild(LayerIndex, ChildIndex)->SourceLayerId;
	const FGuid SourceChildId = ResolveChild(LayerIndex, ChildIndex)->SourceChildId;
	for (int32 SourceLayerIndex = 0; SourceLayerIndex < WorkingLayers.Num(); ++SourceLayerIndex)
	{
		if (WorkingLayers[SourceLayerIndex].LayerId != SourceLayerId)
		{
			continue;
		}
		const int32 SourceChildIndex = WorkingLayers[SourceLayerIndex].Children.IndexOfByPredicate(
			[&SourceChildId](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.ChildId == SourceChildId;
			});
		if (SourceChildIndex != INDEX_NONE)
		{
			SetLayerExpanded(SourceLayerIndex, true);
			SelectWorkingChild(SourceLayerIndex, SourceChildIndex);
			RebuildLayerList();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SMixtormat::BreakChildInstanceAt(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Unhandled();
	}
	// Resolved against a snapshot, because the child being broken lives in the same array the
	// resolve reads from.
	const TArray<FMixtormatLayer> Snapshot = WorkingLayers;
	if (!MixtormatParameterBinding::BreakChildInstance(
		Snapshot, *ResolveChild(LayerIndex, ChildIndex)))
	{
		return FReply::Unhandled();
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	SyncSelectedLayerControls();
	return FReply::Handled();
}

void SMixtormat::CopyChildInstanceReference(const int32 LayerIndex, const int32 ChildIndex)
{
	CopyLayerChild(LayerIndex, ChildIndex, true);
}

FReply SMixtormat::ReplaceChildInstanceSource(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const FGuid NewSourceLayerId,
	const FGuid NewSourceChildId)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Unhandled();
	}
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& Placement = Layer.Children[ChildIndex];
	if (Placement.ScopeOwnerChildId.IsValid())
	{
		const int32 OwnerIndex = FindChildById(Layer, Placement.ScopeOwnerChildId);
		const FMixtormatLayerChild* NewSource = MixtormatParameterBinding::FindChild(
			WorkingLayers, NewSourceLayerId, NewSourceChildId);
		if (!Layer.Children.IsValidIndex(OwnerIndex)
			|| !NewSource
			|| !CanKeepScopedPlacement(Layer.Children[OwnerIndex], *NewSource))
		{
			return FReply::Unhandled();
		}
	}
	if (MixtormatParameterBinding::ClassifyInstancePlacement(
		WorkingLayers,
		NewSourceLayerId,
		NewSourceChildId,
		Layer.LayerId,
		ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
	{
		return FReply::Unhandled();
	}
	ResolveChild(LayerIndex, ChildIndex)->SourceLayerId = NewSourceLayerId;
	ResolveChild(LayerIndex, ChildIndex)->SourceChildId = NewSourceChildId;
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	SyncSelectedLayerControls();
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildMoveChildToLayerMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("MoveChildToLayerCaption", "Move To"));
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& IsMaskFilter(*ResolveChild(LayerIndex, ChildIndex)))
	{
		Menu.Item(
			LOCTEXT("MoveMaskFilterWithMask", "Move the owning mask instead"),
			nullptr,
			FSimpleDelegate()).Enabled(false);
		return Menu.Build();
	}
	for (int32 DestIndex = 0; DestIndex < WorkingLayers.Num(); ++DestIndex)
	{
		if (DestIndex == LayerIndex)
		{
			continue;
		}
		Menu.Item(
			WorkingLayers[DestIndex].DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, DestIndex]()
			{
				MoveChildToLayer(LayerIndex, ChildIndex, DestIndex);
			}));
	}
	if (Menu.IsEmpty())
	{
		Menu.Item(LOCTEXT("MoveChildNoLayers", "No other layer"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildReplaceInstanceSourceMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("ReplaceInstanceSourceCaption", "Source"));
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return Menu.Build();
	}
	const FMixtormatLayer& DestLayer = WorkingLayers[LayerIndex];
	const FGuid DestLayerId = DestLayer.LayerId;
	const FMixtormatLayerChild& Placement = DestLayer.Children[ChildIndex];
	const int32 OwnerIndex = FindChildById(DestLayer, Placement.ScopeOwnerChildId);
	const FMixtormatLayerChild* ScopeOwner = DestLayer.Children.IsValidIndex(OwnerIndex)
		? &DestLayer.Children[OwnerIndex]
		: nullptr;
	// Only what this position can legally read is offered, so the menu cannot put the instance
	// into a state the paste path would have refused.
	for (int32 SourceLayerIndex = 0; SourceLayerIndex < WorkingLayers.Num(); ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Candidate : SourceLayer.Children)
		{
			if (Candidate.IsInstance()
				|| (ScopeOwner && !CanKeepScopedPlacement(*ScopeOwner, Candidate)))
			{
				continue;
			}
			if (MixtormatParameterBinding::ClassifyInstancePlacement(
				WorkingLayers,
				SourceLayer.LayerId,
				Candidate.ChildId,
				DestLayerId,
				ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
			{
				continue;
			}
			const FGuid NewSourceLayerId = SourceLayer.LayerId;
			const FGuid NewSourceChildId = Candidate.ChildId;
			Menu.Item(
				FText::Format(
					LOCTEXT("ReplaceInstanceSourceEntry", "{0} / {1}"),
					SourceLayer.DisplayName,
					GetLayerChildName(Candidate)),
				nullptr,
				FSimpleDelegate::CreateLambda(
					[this, LayerIndex, ChildIndex, NewSourceLayerId, NewSourceChildId]()
				{
					ReplaceChildInstanceSource(LayerIndex, ChildIndex, NewSourceLayerId, NewSourceChildId);
				}));
		}
	}
	if (Menu.IsEmpty())
	{
		Menu.Item(LOCTEXT("ReplaceInstanceNoSource", "Nothing above this position"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
	return Menu.Build();
}

void SMixtormat::AddSharedChildMenuItems(
	MixtormatMenu::FBuilder& Menu,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	const bool bValid = WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex);
	const bool bInstance = bValid && ResolveChild(LayerIndex, ChildIndex)->IsInstance();

	Menu.Separator();
	Menu.Item(
		LOCTEXT("CopyChildContext", "Copy"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			CopyLayerChild(LayerIndex, ChildIndex, false);
		}));
	Menu.Item(
		LOCTEXT("CopyChildAsInstanceContext", "Copy as Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			CopyLayerChild(LayerIndex, ChildIndex, true);
		}));
	Menu.Item(
		LOCTEXT("PasteChildInstanceHereContext", "Paste Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			PasteChildInstance(LayerIndex, ChildIndex);
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			return CanPasteChildInstance(LayerIndex, ChildIndex);
		}));
	Menu.SubMenu(
		LOCTEXT("MoveChildToLayerContext", "Move to Layer..."),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildMoveChildToLayerMenu, LayerIndex, ChildIndex));

	if (!bInstance)
	{
		return;
	}
	Menu.Separator();
	Menu.Caption(LOCTEXT("ChildInstanceCaption", "Instance"));
	Menu.Item(
		LOCTEXT("GoToInstanceSourceContext", "Go to Source"),
		FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.ArrowUp")),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			GoToChildInstanceSource(LayerIndex, ChildIndex);
		}));
	Menu.Item(
		LOCTEXT("BreakInstanceContext", "Break Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			BreakChildInstanceAt(LayerIndex, ChildIndex);
		}));
	Menu.SubMenu(
		LOCTEXT("ReplaceInstanceSourceContext", "Replace Source"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildReplaceInstanceSourceMenu, LayerIndex, ChildIndex));
	Menu.Item(
		LOCTEXT("CopyInstanceReferenceContext", "Copy Instance Reference"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			CopyChildInstanceReference(LayerIndex, ChildIndex);
		}));
}

FReply SMixtormat::ToggleLayerExpanded(const int32 LayerIndex)
{
	SetLayerExpanded(LayerIndex, !IsLayerExpanded(LayerIndex));
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AssignNormalTexture(const int32 LayerIndex, const FSoftObjectPath NormalPath)
{
	if (WorkingLayers.IsValidIndex(LayerIndex) && Cast<UTexture2D>(NormalPath.TryLoad()))
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		Layer.ChannelMode = EMixtormatLayerChannelMode::NormalDetail;
		Layer.NormalSourceType = EMixtormatNormalSourceType::Texture;
		Layer.NormalTexture = TSoftObjectPtr<UTexture2D>(NormalPath);
		RefreshLayeredPreview();
		RebuildLayerList();
		SyncSelectedLayerControls();
	}
	return FReply::Handled();
}

FReply SMixtormat::AddEffectToLayer(const int32 LayerIndex, const FSoftObjectPath EffectPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.Type != EMixtormatLayerType::Material && Layer.Type != EMixtormatLayerType::Fill)
	{
		return FReply::Handled();
	}

	const UMixtormatEffect* Effect = Cast<UMixtormatEffect>(EffectPath.TryLoad());
	if (!Effect)
	{
		return FReply::Handled();
	}

	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	FMixtormatLayerEffect& LayerEffect = Child.Effect;
	LayerEffect.Effect = TSoftObjectPtr<UMixtormatEffect>(EffectPath);
	// Compatibility for an older recipe or external caller that still supplies MLFX_Stain: the
	// child resolves as Stain and runs the transport solve on its own defaults. The asset's stain
	// colour and roughness are dropped, because the effect resolves a layer mask and shades
	// nothing.
	if (Effect->EffectType != EMixtormatEffectType::Stain)
	{
		LayerEffect.Front = Effect->DefaultFront;
		LayerEffect.Width = Effect->DefaultWidth;
		LayerEffect.MacroWarp = Effect->DefaultMacroWarp;
		LayerEffect.MicroWarp = Effect->DefaultMicroWarp;
		LayerEffect.MicroMorph = Effect->DefaultMicroMorph;
		LayerEffect.Thickness = Effect->DefaultThickness;
		LayerEffect.Lift = Effect->DefaultLift;
		LayerEffect.DetailStrength = Effect->DefaultDetailStrength;
	}
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ToggleLayerEffect(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Effect)
	{
		FMixtormatLayerEffect& Effect = ResolveChild(LayerIndex, ChildIndex)->Effect;
		Effect.bEnabled = !Effect.bEnabled;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveLayerEffect(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Effect)
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		const int32 SubtreeEnd = FindSubtreeEnd(Layer, ChildIndex);
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

		Layer.Children.RemoveAt(ChildIndex, SubtreeEnd - ChildIndex);
		if (SelectedLayerIndex == LayerIndex)
		{
			SelectedEffectIndex = SelectedEffectId.IsValid()
				? Layer.Children.IndexOfByPredicate([SelectedEffectId](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == SelectedEffectId;
				})
				: INDEX_NONE;
			SelectedMaskIndex = SelectedMaskId.IsValid()
				? Layer.Children.IndexOfByPredicate([SelectedMaskId](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == SelectedMaskId;
				})
				: INDEX_NONE;
			if ((SelectedEffectId.IsValid() && SelectedEffectIndex == INDEX_NONE)
				|| (SelectedMaskId.IsValid() && SelectedMaskIndex == INDEX_NONE))
			{
				bBypassSelectedChild = false;
			}
			SyncSelectedLayerControls();
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

// Reached by every child the row treats as a mask, which since the filters arrived means the
// mask filters too: the toggle dispatch sends anything that is not an Effect or a generated
// producer here, so a Type check for Mask alone left a Blur's and a Curvature's checkbox inert.
void SMixtormat::SetMaskEnabled(const ECheckBoxState CheckState, const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}

	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	const bool bEnabled = CheckState == ECheckBoxState::Checked;
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Mask: Child.Mask.bEnabled = bEnabled; break;
	case EMixtormatLayerChildType::Blur: Child.Blur.bEnabled = bEnabled; break;
	case EMixtormatLayerChildType::Curvature: Child.Curvature.bEnabled = bEnabled; break;
	default: return;
	}
	RefreshLayeredPreview();
}

void SMixtormat::SetMaskBlendMode(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const EMixtormatMaskBlendMode BlendMode)
{
	// Guarded through ResolveChild, not WorkingLayers: the mask panel calls this with
	// SelectedLayerIndex, which is INDEX_NONE while a group's shared mask is the subject, and an
	// index guard would let the menu open and then quietly drop the write.
	if (ResolveChild(LayerIndex, ChildIndex)
		&& ResolveChild(LayerIndex, ChildIndex)->Type == EMixtormatLayerChildType::Mask)
	{
		ResolveChild(LayerIndex, ChildIndex)->Mask.BlendMode = BlendMode;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
}

FReply SMixtormat::OpenFillColorPicker(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| WorkingLayers[LayerIndex].Type != EMixtormatLayerType::Fill)
	{
		return FReply::Handled();
	}

	LastHistoryRecordTime = 0.0;
	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.InitialColor = WorkingLayers[LayerIndex].BaseColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this,
		&SMixtormat::SetFillBaseColor,
		LayerIndex);
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(
		this,
		&SMixtormat::RestoreFillBaseColor,
		LayerIndex);
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

void SMixtormat::SetFillBaseColor(FLinearColor NewColor, const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		NewColor.A = WorkingLayers[LayerIndex].BaseColor.A;
		WorkingLayers[LayerIndex].BaseColor = NewColor;
		RefreshLayeredPreview();
	}
}

void SMixtormat::RestoreFillBaseColor(FLinearColor OriginalColor, const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		OriginalColor.A = WorkingLayers[LayerIndex].BaseColor.A;
		WorkingLayers[LayerIndex].BaseColor = OriginalColor;
		SynchronizeHistoryAfterCancelledEdit();
		RefreshLayeredPreview(false);
	}
}


void SMixtormat::RebuildLayerList()
{
	if (!LayerListBox.IsValid())
	{
		return;
	}

	LayerListBox->ClearChildren();
	LayerThumbnails.Reset();
	LayerRowWidgets.Reset();
	GroupRowWidgets.Reset();

	// A group-aware walk of the same array, in the same order. The stack the compositor sees is
	// untouched -- this only decides which rows are drawn and how far in. Because a group's
	// members are contiguous, a header is emitted exactly once, at its first member.
	for (int32 LayerIndex = 0; LayerIndex < WorkingLayers.Num(); ++LayerIndex)
	{
		const FGuid GroupId = WorkingLayers[LayerIndex].GroupId;
		const FMixtormatLayerGroup* Group =
			MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
		if (!Group)
		{
			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				BuildLayerRow(LayerIndex)
			];
			continue;
		}

		const bool bFirstMember = LayerIndex == 0 || WorkingLayers[LayerIndex - 1].GroupId != GroupId;
		int32 GroupFirstIndex = INDEX_NONE;
		int32 GroupLastIndex = INDEX_NONE;
		MixtormatLayerGroups::GetGroupRange(WorkingLayers, GroupId, GroupFirstIndex, GroupLastIndex);
		if (bFirstMember)
		{
			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(SMixtormatGroupRowDropTarget)
				.TargetGroupId(GroupId)
				.FirstMemberIndex(GroupFirstIndex)
				.LastMemberIndex(GroupLastIndex)
				.OnLayerDropped(this, &SMixtormat::HandleLayerDroppedOnGroup)
				.OnLayerInsertedAt(this, &SMixtormat::HandleLayerInsertedAt)
				.OnGroupInsertedAt(this, &SMixtormat::HandleGroupInsertedAt)
				.OnSurfaceInsertedAt(this, &SMixtormat::HandleSurfaceDroppedAt)
				[
					BuildLayerGroupRow(GroupId)
				]
			];
		}

		// A collapsed group hides its members without touching their own expanded state, so
		// reopening it puts every layer back the way it was left.
		if (!IsGroupExpanded(GroupId))
		{
			continue;
		}

		// The shared stack, listed once under the header rather than repeated on every member --
		// which is exactly what it is: one authored copy, applied to each of them at compose time.
		if (bFirstMember)
		{
			for (int32 ChildIndex = 0; ChildIndex < Group->Children.Num(); ++ChildIndex)
			{
				LayerListBox->AddSlot()
				.AutoHeight()
				.Padding(MixtormatTokens::LayerScopeIndent, 0.0f, 0.0f, 2.0f)
				[
					BuildGroupChildRow(GroupId, ChildIndex)
				];
			}
		}

		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(MixtormatTokens::LayerScopeIndent, 0.0f, 0.0f, 2.0f)
		[
			BuildLayerRow(LayerIndex)
		];
	}
}

void SMixtormat::RebuildMaskList()
{
	if (!MaskListBox.IsValid())
	{
		return;
	}

	MaskListBox->ClearChildren();
	MaskThumbnails.Reset();

	const TArray<FMixtormatMaskEntry> Masks = FMixtormatRegistry::GetMasks();
	for (int32 MaskIndex = 0; MaskIndex < Masks.Num(); ++MaskIndex)
	{
		const FMixtormatMaskEntry& Mask = Masks[MaskIndex];
		MaskListBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			BuildMaskCard(Mask.DisplayName, Mask.AssetPath, Mask.ThumbnailAsset, true)
		];
	}

	if (Masks.IsEmpty())
	{
		MaskListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"EmptyMaskRegistry",
				"No masks found. Repair or reinstall Mixtormat, or import a PNG mask folder."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

TSharedRef<SWidget> SMixtormat::BuildLayerStackPanel()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SMixtormatLayerDropTarget)
		.OnSurfaceDropped(this, &SMixtormat::HandleSurfaceDropped)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::LayerStackWidth)
			[
				SNew(SBorder)
				.Padding(MixtormatTokens::PanelPadding)
				.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Collapsed : EVisibility::Visible; })
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton)
							.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
							.Text(LOCTEXT("CreateWorkingMaterial", "Create Material"))
							.IsEnabled_Lambda([this]() { return SelectedPreviewMaterial.IsValid(); })
							.ToolTipText(LOCTEXT("CreateWorkingMaterialHint", "Select a saved library surface first, then create a nondestructive layered recipe."))
							.OnClicked(this, &SMixtormat::StartNewMaterial)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
							.Text(LOCTEXT("OpenWorkingMaterialFromLayers", "Open Saved Recipe..."))
							.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([]()
							{
								return FMixtormatRegistry::GetSurfaces().IsEmpty()
									? LOCTEXT("NoSavedSurfaces", "No saved Mixtormat surfaces were found. Import a complete texture set or open an existing recipe.")
									: LOCTEXT("SelectSurfaceToBegin", "Select or drag a library surface to begin.");
							})
							.AutoWrapText(true)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(MixtormatTokens::LayerStackHeaderHeight)
						[
							SNew(SHorizontalBox)
							.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
							// The count reads first, on the leading edge; the two creation glyphs
							// pair up on the trailing edge, same grouping as the text buttons below.
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							.HAlign(HAlign_Left).VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerRowInsetLeading, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return FText::Format(LOCTEXT("LayerCountCompact", "{0} LAYERS"), FText::AsNumber(WorkingLayers.Num()));
								})
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
							]
							// Two creations, two glyphs: a square for a material, a circle for a fill. The
							// same icon button the eye and the chevrons are, so the bar costs one row and
							// no plate.
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SMixtormatIconButton)
								.Icon(MixtormatIcons::LayerMaterial())
								.ToolTip(LOCTEXT("AddMaterialLayerHintCompact", "Add a material layer from the selected library surface."))
								.OnClicked_Lambda([this]() { AddWorkingLayer(EMixtormatLayerType::Material); })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerRowInsetTrailing, 0.0f)
							[
								SNew(SMixtormatIconButton)
								.Icon(MixtormatIcons::LayerFill())
								.ToolTip(LOCTEXT("AddFillLayerHintCompact", "Create a constant Base Color, Roughness, IOR, and Metallic fill layer."))
								.OnClicked_Lambda([this]() { AddWorkingLayer(EMixtormatLayerType::Fill); })
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()[SAssignNew(LayerListBox, SVerticalBox)]
					]
					// A hairline like the one above the scroll box, so the permanent controls read
					// as their own footer rather than as one more row of the stack.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
					// Permanent creation controls, below the rows rather than inside any one of
					// them, so they stay put -- and stay reachable -- whether the stack holds
					// forty layers or none. Same two creations as the header pair above, just
					// spelled out in text since this is the row a user lands on with an empty
					// stack and no icon-only glyph to already have learned. Right-aligned to match
					// the header pair's trailing edge.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::LayerRowGap, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.HeightOverride(MixtormatTokens::ButtonHeight)
							[
								SNew(SButton)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.ToolTipText(LOCTEXT("AddMaterialLayerBottomHint", "Add a material layer from the selected library surface."))
								.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Material); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(MixtormatTokens::IconButtonSize)
										.HeightOverride(MixtormatTokens::IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerMaterial())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddMaterialLayerBottom", "Layer"))
									]
								]
							]
						]
						// Beside the two Add buttons rather than in the stack: grouping acts on the
						// selection, so it belongs with the other things that change the stack
						// rather than with anything a single row owns.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
							.HeightOverride(MixtormatTokens::ButtonHeight)
							[
								SNew(SButton)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.IsEnabled_Lambda([this]() { return CanCreateGroupFromSelection(); })
								.ToolTipText_Lambda([this]()
								{
									const int32 Count = GetSelectedLayerIndices().Num();
									if (Count == 0)
									{
										return LOCTEXT(
											"CreateGroupDisabledHint",
											"Select one or more layers first. Shift click for a run, Ctrl click to add one.");
									}
									return FText::Format(
										LOCTEXT(
											"CreateGroupHint",
											"Group {0} selected layer(s). They are gathered into one block, so layers between them move and height references that end up pointing upward are dropped."),
										FText::AsNumber(Count));
								})
								.OnClicked_Lambda([this]() { return CreateGroupFromSelection(); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(MixtormatTokens::IconButtonSize)
										.HeightOverride(MixtormatTokens::IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::Folder())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("CreateGroupBottom", "Group"))
									]
								]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerRowInsetTrailing, 0.0f)
						[
							SNew(SBox)
							.HeightOverride(MixtormatTokens::ButtonHeight)
							[
								SNew(SButton)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.ToolTipText(LOCTEXT("AddFillLayerBottomHint", "Create a constant Base Color, Roughness, IOR, and Metallic fill layer."))
								.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Fill); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(MixtormatTokens::IconButtonSize)
										.HeightOverride(MixtormatTokens::IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerFill())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddFillLayerBottom", "Fill Layer"))
									]
								]
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLayerThumbnail(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

	// A fill layer has no asset to preview, so its own colour is the thumbnail. Read through a
	// lambda rather than captured, because the colour picker edits it live.
	if (Layer.Type == EMixtormatLayerType::Fill)
	{
		return SNew(SColorBlock)
			.Color_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].BaseColor
					: MixtormatPalette::Panel();
			})
			.Size(FVector2D(MixtormatTokens::LayerThumbnailSize, MixtormatTokens::LayerThumbnailSize));
	}

	// Thumbnails are pooled and must be kept alive for as long as the widget is: LayerThumbnails
	// is that ownership, and RebuildLayerList resets it in step with the rows.
	const int32 Size = static_cast<int32>(MixtormatTokens::LayerThumbnailSize);
	if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
		&& Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
		&& !Layer.NormalTexture.IsNull())
	{
		if (UTexture2D* Texture = Layer.NormalTexture.LoadSynchronous())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail =
				MakeShared<FAssetThumbnail>(FAssetData(Texture), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}
	else if (!Layer.SourceComposition.IsNull())
	{
		if (UMixtormatMaterial* Composition = Layer.SourceComposition.LoadSynchronous())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				FAssetData(Composition), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}
	else if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
	{
		if (Surface->PreviewMaterial)
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				FAssetData(Surface->PreviewMaterial.Get()), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}

	return SNew(SColorBlock)
		.Color(MixtormatPalette::Panel())
		.Size(FVector2D(MixtormatTokens::LayerThumbnailSize, MixtormatTokens::LayerThumbnailSize));
}

FText SMixtormat::GetLayerDisplayName(const int32 LayerIndex) const
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.SourceComposition.IsNull())
	{
		return Layer.DisplayName;
	}
	const FText Name = Layer.DisplayName.IsEmpty()
		? FText::FromString(Layer.SourceComposition.ToSoftObjectPath().GetAssetName())
		: Layer.DisplayName;
	return FText::Format(LOCTEXT("ReferenceLayerName", "{0} (ref)"), Name);
}

FText SMixtormat::GetLayerSourceText(const int32 LayerIndex) const
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

	if (!Layer.SourceComposition.IsNull())
	{
		const UMixtormatMaterial* Composition = Layer.SourceComposition.LoadSynchronous();
		const FText Name = Composition && !Composition->DisplayName.IsEmpty()
			? Composition->DisplayName
			: FText::FromString(Layer.SourceComposition.ToSoftObjectPath().GetAssetName());
		return Composition
			? FText::Format(LOCTEXT("ReferenceLayerSource", "REF - {0}"), Name)
			: FText::Format(LOCTEXT("MissingReferenceLayerSource", "MISSING REF - {0}"), Name);
	}

	// What the layer is made of, which is a different question from what it is called. A surface
	// name when there is one, because that is the answer a user is scanning for; the layer's kind
	// only when there is no asset behind it to name.
	if (Layer.Type == EMixtormatLayerType::Fill)
	{
		return LOCTEXT("FillLayerSource", "FILL");
	}
	if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
		&& Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
		&& !Layer.NormalTexture.IsNull())
	{
		return FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName().ToUpper());
	}
	if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
	{
		const FText Name = Surface->DisplayName.IsEmpty()
			? FText::FromString(Layer.SourceSurface.ToSoftObjectPath().GetAssetName())
			: Surface->DisplayName;
		return FText::FromString(Name.ToString().ToUpper());
	}
	return Layer.Type == EMixtormatLayerType::Effect
		? LOCTEXT("EffectLayerSource", "EFFECT")
		: LOCTEXT("MaterialLayerSource", "MATERIAL");
}

FText SMixtormat::GetLayerChildName(const FMixtormatLayerChild& Child) const
{
	// An instance is named for what it shows, marked for what it is. The arrow is the whole
	// difference in the stack -- an instance row is otherwise the same row as its source, which is
	// the point of it.
	if (Child.IsInstance())
	{
		FMixtormatLayerChild Named = Child;
		Named.SourceLayerId = FGuid();
		Named.SourceChildId = FGuid();
		return FText::Format(
			LOCTEXT("InstanceChildName", "{0} {1}"),
			FText::FromString(TEXT("\u2197")),
			GetLayerChildName(Named));
	}

	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.UsesLayerValues())
	{
		// Named for the channel, because there is no asset to name it after and two of them on
		// one layer differ by nothing else.
		return FText::Format(
			LOCTEXT("LayerValuesMaskName", "Layer {0}"),
			MixtormatUI::LayerValueChannelText(Child.Mask.LayerValueChannel));
	}

	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.HasPublishedSource())
	{
		return Child.Mask.PublishedSourceOutput == TEXT("Wear")
			? LOCTEXT("PublishedWearMaskName", "Wear Mask")
			: FText::Format(
				LOCTEXT("PublishedMaskName", "{0} Mask"),
				FText::FromName(Child.Mask.PublishedSourceOutput));
	}

	if (Child.Type == EMixtormatLayerChildType::Effect)
	{
		const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
		if (Asset)
		{
			switch (Asset->EffectType)
			{
			case EMixtormatEffectType::Peeling: return LOCTEXT("PeelingEffectName", "Peeling");
			case EMixtormatEffectType::Stain:
				return Child.Effect.StainMode == EMixtormatStainMode::Deposit
					? LOCTEXT("DepositStainEffectName", "Stain Deposit")
					: LOCTEXT("WetStainEffectName", "Wet Stain");
			case EMixtormatEffectType::Grade:   return LOCTEXT("GradeEffectName", "Grade");
			case EMixtormatEffectType::Chipping: return LOCTEXT("ChippingEffectName", "Chipping");
			case EMixtormatEffectType::WornEdges: return LOCTEXT("WornEdgesEffectName", "Worn Edges");
			case EMixtormatEffectType::FlowWarp: return LOCTEXT("FlowWarpEffectName", "Flow Warp");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("LayerBlurEffectName", "Layer Blur");
			case EMixtormatEffectType::Runoff:  return LOCTEXT("RunoffEffectName", "Runoff");
			default:                            return LOCTEXT("ErosionEffectName", "Erosion");
			}
		}
		switch (Child.Effect.ProceduralType)
		{
		case EMixtormatEffectType::Stain:
			return Child.Effect.StainMode == EMixtormatStainMode::Deposit
				? LOCTEXT("DepositStainEffectName", "Stain Deposit")
				: LOCTEXT("WetStainEffectName", "Wet Stain");
		case EMixtormatEffectType::Erosion: return LOCTEXT("ErosionEffectName", "Erosion");
		case EMixtormatEffectType::Grade:   return LOCTEXT("GradeEffectName", "Grade");
		case EMixtormatEffectType::Chipping: return LOCTEXT("ChippingEffectName", "Chipping");
		case EMixtormatEffectType::WornEdges: return LOCTEXT("WornEdgesEffectName", "Worn Edges");
		case EMixtormatEffectType::FlowWarp: return LOCTEXT("FlowWarpEffectName", "Flow Warp");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("LayerBlurEffectName", "Layer Blur");
		case EMixtormatEffectType::Runoff:  return LOCTEXT("RunoffEffectName", "Runoff");
		default:                            return LOCTEXT("ProceduralPeelName", "Peeling (Procedural)");
		}
	}
	if (Child.Type == EMixtormatLayerChildType::Generated)
	{
		return LOCTEXT("GeneratedChildName", "Generated Mask");
	}
	if (Child.Type == EMixtormatLayerChildType::Craquelure)
	{
		return LOCTEXT("CraquelureChildName", "Craquelure");
	}
	if (Child.Type == EMixtormatLayerChildType::Filter)
	{
		return LOCTEXT("ClusterFilterChildName", "Cluster IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::HsvFilter)
	{
		return LOCTEXT("HsvFilterChildName", "HSV From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::RandomId)
	{
		return LOCTEXT("RandomIdChildName", "Random From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::RampId)
	{
		return LOCTEXT("RampIdChildName", "Ramp From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::PatternId)
	{
		return LOCTEXT("PatternIdChildName", "Pattern IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::Blur)
	{
		// Named for what it does rather than for the mask it is on: the row sits indented under
		// that mask already, so repeating its name would be the only thing on the line.
		return LOCTEXT("BlurChildName", "Blur");
	}
	if (Child.Type == EMixtormatLayerChildType::Curvature)
	{
		return LOCTEXT("CurvatureChildName", "Curvature");
	}
	if (Child.Type == EMixtormatLayerChildType::ColorId)
	{
		// Named after its map rather than after itself, the way a painted mask row is: two id
		// nodes on one layer are two selections out of the same map, and "Color ID" twice says
		// nothing about which is which.
		const FSoftObjectPath IdPath = Child.ColorId.IdTexture.ToSoftObjectPath();
		return IdPath.IsNull()
			? LOCTEXT("ColorIdChildName", "Color ID")
			: FText::FromString(IdPath.GetAssetName());
	}
	const FSoftObjectPath MaskPath = !Child.Mask.Mask.IsNull()
		? Child.Mask.Mask.ToSoftObjectPath()
		: Child.Mask.MaskTexture.ToSoftObjectPath();
	return FText::FromString(MaskPath.GetAssetName());
}

FText SMixtormat::GetLayerChildSourceText(
	const int32 LayerIndex,
	const int32 ChildIndex) const
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FText::GetEmpty();
	}

	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
	if (!Child.ScopeOwnerChildId.IsValid())
	{
		return IsFlowWarp(Child)
			? LOCTEXT("FlowWarpLayerTarget", "TARGET · LAYER")
			: MixtormatLayerBadges::KindForChild(Child);
	}

	const int32 OwnerIndex = FindChildById(Layer, Child.ScopeOwnerChildId);
	if (!Layer.Children.IsValidIndex(OwnerIndex))
	{
		return LOCTEXT("MissingScopeOwner", "OWNER MISSING");
	}
	const FMixtormatLayerChild& Owner = Layer.Children[OwnerIndex];
	if (IsFlowWarp(Child))
	{
		return Owner.Type == EMixtormatLayerChildType::Mask
			? LOCTEXT("FlowWarpMaskTarget", "TARGET · MASK")
			: LOCTEXT("FlowWarpEffectTarget", "TARGET · FX");
	}
	if (Child.Type == EMixtormatLayerChildType::Mask)
	{
		return IsFlowWarp(Owner)
			? LOCTEXT("FlowWarpGateMask", "GATES · WARP")
			: LOCTEXT("EffectGateMask", "GATES · FX");
	}
	if (IsMaskFilter(Child))
	{
		return LOCTEXT("MaskFilterSource", "FILTER · MASK");
	}
	return MixtormatLayerBadges::KindForChild(Child);
}

TSharedRef<SWidget> SMixtormat::BuildLayerChildIcon(const int32 LayerIndex, const int32 ChildIndex)
{
	const FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);

	// Every child shows what kind of thing it is, masks included. An 11px thumbnail of a mask is a
	// grey smudge that says less than the glyph does, and it cost a pooled thumbnail per row; which
	// mask it actually is now answers on hover, at a size worth looking at.
	return SNew(SImage)
		.Image(Child.Type == EMixtormatLayerChildType::Effect
			? MixtormatIcons::Effect()
			: (Child.Type == EMixtormatLayerChildType::Generated
					|| Child.Type == EMixtormatLayerChildType::Craquelure
					|| Child.Type == EMixtormatLayerChildType::Filter
					|| Child.Type == EMixtormatLayerChildType::HsvFilter
					|| Child.Type == EMixtormatLayerChildType::RandomId
					|| Child.Type == EMixtormatLayerChildType::RampId
					|| Child.Type == EMixtormatLayerChildType::PatternId)
				? MixtormatIcons::Generated()
				: MixtormatIcons::Mask())
		.ColorAndOpacity(FSlateColor(MixtormatPalette::RowText()));
}

TSharedPtr<IToolTip> SMixtormat::BuildMaskPreviewTooltip(const int32 LayerIndex, const int32 ChildIndex)
{
	// Which mask this row is carrying, answered by showing it. The row itself only has room for a
	// glyph, so the picture is what hovering buys -- at the size the picker draws one, since the
	// question being asked is the same question the picker answers.
	const FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	if (Child.Type != EMixtormatLayerChildType::Mask)
	{
		return nullptr;
	}

	const FSoftObjectPath MaskPath = !Child.Mask.Mask.IsNull()
		? Child.Mask.Mask.ToSoftObjectPath()
		: Child.Mask.MaskTexture.ToSoftObjectPath();
	UObject* MaskObject = MaskPath.TryLoad();
	if (!MaskObject)
	{
		return nullptr;
	}

	UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
	if (const UMixtormatMask* MaskAsset = Cast<UMixtormatMask>(MaskObject))
	{
		Texture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
	}
	if (!Texture)
	{
		return nullptr;
	}

	const int32 Size = static_cast<int32>(MixtormatTokens::MaskTileSize);
	TSharedPtr<FAssetThumbnail> Thumbnail =
		MakeShared<FAssetThumbnail>(FAssetData(Texture), Size, Size, ThumbnailPool);
	LayerThumbnails.Add(Thumbnail);

	return SNew(SToolTip)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::MaskTileSize)
				.HeightOverride(MixtormatTokens::MaskTileSize)
				[
					Thumbnail->MakeThumbnailWidget()
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
				.Text(FText::FromString(MaskPath.GetAssetName()))
			]
		];
}

FMixtormatLayerChild* SMixtormat::ResolveChild(const int32 LayerIndex, const int32 ChildIndex)
{
	return const_cast<FMixtormatLayerChild*>(
		const_cast<const SMixtormat*>(this)->ResolveChild(LayerIndex, ChildIndex));
}

const FMixtormatLayerChild* SMixtormat::ResolveChild(
	const int32 LayerIndex,
	const int32 ChildIndex) const
{
	if (LayerIndex == INDEX_NONE)
	{
		// No layer, but a group selected: the child is one of that group's shared children.
		const FMixtormatLayerGroup* Group =
			MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SelectedGroupId);
		return Group && Group->Children.IsValidIndex(ChildIndex)
			? &Group->Children[ChildIndex]
			: nullptr;
	}
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		? &WorkingLayers[LayerIndex].Children[ChildIndex]
		: nullptr;
}

FMixtormatLayerChild* SMixtormat::AppendGroupChild(
	const FGuid GroupId,
	const EMixtormatLayerChildType ChildType)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group)
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = Group->Children.AddDefaulted_GetRef();
	Child.Type = ChildType;
	return &Child;
}

void SMixtormat::FinishGroupChildEdit(const FGuid GroupId)
{
	// A shared child is broadcast to every member, so unlike a rename this genuinely changes what
	// the compositor draws and has to ask for a new composite.
	// SelectGroupChild, not SelectLayerGroup: the getters the inspector is built from read
	// SelectedMaskIndex / SelectedEffectIndex against a cleared layer index, and SelectGroupChild is
	// what points those lanes at the group's stack. Setting SelectedGroupChildIndex alone left the
	// panel with nothing to show, which is why a shared child added here appeared but could not be
	// edited.
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	SelectGroupChild(GroupId, Group ? Group->Children.Num() - 1 : INDEX_NONE);
	CollapsedGroupIds.Remove(GroupId);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
}

FReply SMixtormat::AddMaskToGroup(const FGuid GroupId, const FSoftObjectPath MaskPath)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	FMixtormatMaskLayer NewMask;
	if (!Group || !BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}
	// Bisecting a freeze: pre-session value while the cause is narrowed down.
	NewMask.BlendMode = EMixtormatMaskBlendMode::Multiply;
	if (FMixtormatLayerChild* Child = AppendGroupChild(GroupId, EMixtormatLayerChildType::Mask))
	{
		Child->Mask = MoveTemp(NewMask);
		FinishGroupChildEdit(GroupId);
	}
	return FReply::Handled();
}

FReply SMixtormat::AddEffectToGroup(const FGuid GroupId, const FSoftObjectPath EffectPath)
{
	const UMixtormatEffect* Effect = Cast<UMixtormatEffect>(EffectPath.TryLoad());
	if (!Effect)
	{
		return FReply::Handled();
	}
	FMixtormatLayerChild* Child = AppendGroupChild(GroupId, EMixtormatLayerChildType::Effect);
	if (!Child)
	{
		return FReply::Handled();
	}
	FMixtormatLayerEffect& LayerEffect = Child->Effect;
	LayerEffect.Effect = TSoftObjectPtr<UMixtormatEffect>(EffectPath);
	// Stain resolves a layer mask on its own defaults and shades nothing, so it takes none of the
	// asset's shape values -- the same exception AddEffectToLayer makes.
	if (Effect->EffectType != EMixtormatEffectType::Stain)
	{
		LayerEffect.Front = Effect->DefaultFront;
		LayerEffect.Width = Effect->DefaultWidth;
		LayerEffect.MacroWarp = Effect->DefaultMacroWarp;
		LayerEffect.MicroWarp = Effect->DefaultMicroWarp;
		LayerEffect.MicroMorph = Effect->DefaultMicroMorph;
		LayerEffect.Thickness = Effect->DefaultThickness;
		LayerEffect.Lift = Effect->DefaultLift;
		LayerEffect.DetailStrength = Effect->DefaultDetailStrength;
	}
	FinishGroupChildEdit(GroupId);
	return FReply::Handled();
}

FReply SMixtormat::AddProceduralChildToGroup(
	const FGuid GroupId,
	const EMixtormatLayerChildType ChildType)
{
	if (AppendGroupChild(GroupId, ChildType))
	{
		FinishGroupChildEdit(GroupId);
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveGroupChild(const FGuid GroupId, const int32 ChildIndex)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !Group->Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}
	// Anything scoped beneath this child goes with it, the same as removing a layer child: a
	// blur whose owner has left gates nothing.
	const FGuid RemovedId = Group->Children[ChildIndex].ChildId;
	Group->Children.RemoveAll([&RemovedId](const FMixtormatLayerChild& Candidate)
	{
		return Candidate.ChildId == RemovedId || Candidate.ScopeOwnerChildId == RemovedId;
	});
	SelectedGroupChildIndex = INDEX_NONE;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ToggleGroupChildEnabled(const FGuid GroupId, const int32 ChildIndex)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !Group->Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}
	FMixtormatLayerChild& Child = Group->Children[ChildIndex];
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Effect:      Child.Effect.bEnabled = !Child.Effect.bEnabled; break;
	case EMixtormatLayerChildType::Generated:   Child.Generated.bEnabled = !Child.Generated.bEnabled; break;
	case EMixtormatLayerChildType::Craquelure:  Child.Craquelure.bEnabled = !Child.Craquelure.bEnabled; break;
	case EMixtormatLayerChildType::ColorId:     Child.ColorId.bEnabled = !Child.ColorId.bEnabled; break;
	case EMixtormatLayerChildType::Filter:      Child.Filter.bEnabled = !Child.Filter.bEnabled; break;
	case EMixtormatLayerChildType::HsvFilter:   Child.HsvFilter.bEnabled = !Child.HsvFilter.bEnabled; break;
	case EMixtormatLayerChildType::RandomId:    Child.RandomId.bEnabled = !Child.RandomId.bEnabled; break;
	case EMixtormatLayerChildType::RampId:      Child.RampId.bEnabled = !Child.RampId.bEnabled; break;
	case EMixtormatLayerChildType::PatternId:   Child.PatternId.bEnabled = !Child.PatternId.bEnabled; break;
	default:                                    Child.Mask.bEnabled = !Child.Mask.bEnabled; break;
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

bool SMixtormat::IsGroupChildEnabled(const FMixtormatLayerChild& Child)
{
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Effect:      return Child.Effect.bEnabled;
	case EMixtormatLayerChildType::Generated:   return Child.Generated.bEnabled;
	case EMixtormatLayerChildType::Craquelure:  return Child.Craquelure.bEnabled;
	case EMixtormatLayerChildType::ColorId:     return Child.ColorId.bEnabled;
	case EMixtormatLayerChildType::Filter:      return Child.Filter.bEnabled;
	case EMixtormatLayerChildType::HsvFilter:   return Child.HsvFilter.bEnabled;
	case EMixtormatLayerChildType::RandomId:    return Child.RandomId.bEnabled;
	case EMixtormatLayerChildType::RampId:      return Child.RampId.bEnabled;
	case EMixtormatLayerChildType::PatternId:   return Child.PatternId.bEnabled;
	default:                                    return Child.Mask.bEnabled;
	}
}

FReply SMixtormat::RenameLayer(const FGuid LayerId, const FText NewName)
{
	FMixtormatLayer* Layer = WorkingLayers.FindByPredicate(
		[&LayerId](const FMixtormatLayer& Candidate) { return Candidate.LayerId == LayerId; });
	// A blank name is refused rather than replaced with a default: the layer already has a name
	// worth keeping, and substituting one silently discards it.
	if (!Layer || NewName.IsEmptyOrWhitespace() || Layer->DisplayName.EqualTo(NewName))
	{
		RebuildLayerList();
		return FReply::Handled();
	}
	Layer->DisplayName = NewName;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	// No RefreshLayeredPreview: a name is not an input to any pass, and recomposing on a rename
	// would put a full GPU frame behind an edit that cannot change a pixel.
	SyncSelectedLayerControls();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::RenameLayerGroup(const FGuid GroupId, const FText NewName)
{
	FMixtormatLayerGroup* Group =
		MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || NewName.IsEmptyOrWhitespace() || Group->DisplayName.EqualTo(NewName))
	{
		RebuildLayerList();
		return FReply::Handled();
	}
	Group->DisplayName = NewName;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
	return FReply::Handled();
}

bool SMixtormat::BeginRenameSelection()
{
	// The group first: selecting a group clears the layer selection but leaves SelectedLayerIndex
	// where it was, so asking the layer first would rename whatever was picked before the group.
	const bool bRenamingGroup = SelectedGroupId.IsValid();
	if (bRenamingGroup)
	{
		if (!GroupRowWidgets.Contains(SelectedGroupId))
		{
			return false;
		}
	}
	else if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !LayerRowWidgets.Contains(WorkingLayers[SelectedLayerIndex].LayerId))
	{
		return false;
	}

	// One frame later, not now. The context-menu route arrives while the menu is still dismissing
	// and while RebuildLayerList has just replaced every row -- opening the box against a widget
	// that is on its way out means the caret lands nowhere and F2 looks dead.
	const FGuid GroupId = SelectedGroupId;
	const FGuid LayerId = bRenamingGroup
		? FGuid()
		: WorkingLayers[SelectedLayerIndex].LayerId;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this, GroupId, LayerId](double, float)
		{
			if (GroupId.IsValid())
			{
				if (const TWeakPtr<SMixtormatLayerGroupRow>* Row = GroupRowWidgets.Find(GroupId))
				{
					if (const TSharedPtr<SMixtormatLayerGroupRow> Pinned = Row->Pin())
					{
						Pinned->BeginRename();
					}
				}
			}
			else if (const TWeakPtr<SMixtormatLayerRow>* Row = LayerRowWidgets.Find(LayerId))
			{
				if (const TSharedPtr<SMixtormatLayerRow> Pinned = Row->Pin())
				{
					Pinned->BeginRename();
				}
			}
			return EActiveTimerReturnType::Stop;
		}));
	return true;
}

FReply SMixtormat::SelectGroupChild(const FGuid GroupId, const int32 ChildIndex)
{
	SelectedGroupId = GroupId;
	SelectedGroupChildIndex = ChildIndex;
	// ResolveChild only reaches a group's shared stack when there is no layer index, so the layer
	// lane has to be cleared before the mask/effect lanes can address a shared child at all.
	SelectedLayerIndex = INDEX_NONE;
	bHasSelectedLayer = false;
	// The inspector's payload panels are selected by these two lanes, not by SelectedGroupChildIndex:
	// every one of them except the effect panel reads SelectedMaskIndex, so every non-effect type
	// rides the mask lane -- the same split SelectWorkingChild makes for a layer's own children.
	// Resolving first also keeps an empty or missing group (FinishGroupChildEdit passes Num() - 1)
	// from pointing a lane at an index that is not there.
	const FMixtormatLayerChild* Child = ResolveChild(INDEX_NONE, ChildIndex);
	const bool bEffect = Child && Child->Type == EMixtormatLayerChildType::Effect;
	SelectedEffectIndex = bEffect ? ChildIndex : INDEX_NONE;
	SelectedMaskIndex = (Child && !bEffect) ? ChildIndex : INDEX_NONE;
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	return FReply::Handled();
}

// A shared child's row. Read-only compared with a layer's: no drop target and no reorder, because
// moving children between containers is not built yet and a row that accepts a drag it cannot
// honour is worse than one that does not offer it.
TSharedRef<SWidget> SMixtormat::BuildGroupChildRow(const FGuid GroupId, const int32 ChildIndex)
{
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !Group->Children.IsValidIndex(ChildIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FMixtormatLayerChild& Child = Group->Children[ChildIndex];

	return SNew(SMixtormatLayerChildRow)
		.Name(GetLayerChildName(Child))
		// KindForChild rather than GetLayerChildSourceText: that one resolves scope owners through
		// a layer, and this child's container is a group.
		.Kind(MixtormatLayerBadges::KindForChild(Child))
		.Badge(MixtormatLayerBadges::ForChild(Child))
		.bActive_Lambda([this, GroupId, ChildIndex]()
		{
			const FMixtormatLayerGroup* Current =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Current && Current->Children.IsValidIndex(ChildIndex)
				&& IsGroupChildEnabled(Current->Children[ChildIndex]);
		})
		.bSelected_Lambda([this, GroupId, ChildIndex]()
		{
			return SelectedGroupId == GroupId && SelectedGroupChildIndex == ChildIndex;
		})
		.OnSelected_Lambda([this, GroupId, ChildIndex]() { SelectGroupChild(GroupId, ChildIndex); })
		.OnToggleActive_Lambda([this, GroupId, ChildIndex]()
		{
			ToggleGroupChildEnabled(GroupId, ChildIndex);
		})
		.OnGetContextMenu_Lambda([this, GroupId, ChildIndex]()
		{
			return BuildGroupChildContextMenu(GroupId, ChildIndex);
		});
}

TSharedRef<SWidget> SMixtormat::BuildGroupChildContextMenu(
	const FGuid GroupId,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("RemoveGroupChildContext", "Delete"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
		{
			RemoveGroupChild(GroupId, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGroupAddEffectMenu(const FGuid GroupId)
{
	MixtormatMenu::FBuilder Menu;
	for (const FMixtormatEffectEntry& Entry : FMixtormatRegistry::GetEffects())
	{
		Menu.Item(
			Entry.DisplayName,
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, GroupId, EffectPath = Entry.AssetPath]()
			{
				AddEffectToGroup(GroupId, EffectPath);
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGroupAddFilterMenu(const FGuid GroupId)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatLayerChildType> Filters[] = {
		{ LOCTEXT("AddClusterFilterChild", "Cluster IDs"), EMixtormatLayerChildType::Filter },
		{ LOCTEXT("AddPatternIdChild", "Pattern IDs"), EMixtormatLayerChildType::PatternId },
		{ LOCTEXT("AddHsvFilterChild", "HSV From IDs"), EMixtormatLayerChildType::HsvFilter },
		{ LOCTEXT("AddRampIdChild", "Ramp From IDs"), EMixtormatLayerChildType::RampId },
	};
	for (const TPair<FText, EMixtormatLayerChildType>& Filter : Filters)
	{
		Menu.Item(
			Filter.Key,
			MixtormatIcons::Generated(),
			FSimpleDelegate::CreateLambda([this, GroupId, Type = Filter.Value]()
			{
				AddProceduralChildToGroup(GroupId, Type);
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerGroupRow(const FGuid GroupId)
{
	const TSharedRef<SMixtormatLayerGroupRow> Row = SNew(SMixtormatLayerGroupRow)
		.Name_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group ? Group->DisplayName : FText::GetEmpty();
		})
		.MemberCount_Lambda([this, GroupId]()
		{
			int32 Count = 0;
			for (const FMixtormatLayer& Layer : WorkingLayers)
			{
				Count += Layer.GroupId == GroupId ? 1 : 0;
			}
			return Count;
		})
		.bEnabled_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return !Group || Group->bEnabled;
		})
		.bExpanded_Lambda([this, GroupId]() { return IsGroupExpanded(GroupId); })
		.bSelected_Lambda([this, GroupId]() { return SelectedGroupId == GroupId; })
		.OnSelected_Lambda([this, GroupId]() { SelectLayerGroup(GroupId); })
		.OnToggleExpanded_Lambda([this, GroupId]() { ToggleGroupExpanded(GroupId); })
		.OnToggleEnabled_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			SetLayerGroupEnabled(GroupId, Group ? !Group->bEnabled : true);
		})
		.OnNameCommitted_Lambda([this, GroupId](const FText& Text, ETextCommit::Type)
		{
			RenameLayerGroup(GroupId, Text);
		})
		.OnDragDetected_Lambda([this, GroupId](const FGeometry&, const FPointerEvent&)
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group
				? FReply::Handled().BeginDragDrop(
					FMixtormatGroupDragDropOp::New(GroupId, Group->DisplayName))
				: FReply::Unhandled();
		})
		.OnGetContextMenu(this, &SMixtormat::BuildLayerGroupContextMenu, GroupId);
	GroupRowWidgets.Add(GroupId, Row);
	return Row;
}

TSharedRef<SWidget> SMixtormat::BuildLayerGroupContextMenu(const FGuid GroupId)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group =
		MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	const bool bGroupEnabled = !Group || Group->bEnabled;

	// Everything added here is authored once and applies to every member. That is the reason a
	// group exists -- the alternative is the same effect copied into each layer by hand.
	Menu.Caption(LOCTEXT("GroupAddSection", "Add · Shared"));
	const FSoftObjectPath GroupMaskPath = SelectedMaskPath;
	Menu.Item(
		FText::Format(
			LOCTEXT("AddSelectedMaskToGroup", "Add Mask · {0}"),
			SelectedLibraryMaskName.IsEmpty()
				? LOCTEXT("NoSelectedLibraryMask", "Select Mask from Gallery")
				: SelectedLibraryMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, GroupId, GroupMaskPath]()
		{
			AddMaskToGroup(GroupId, GroupMaskPath);
		}))
		.Enabled(TAttribute<bool>(!GroupMaskPath.IsNull()));
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAddEffectMenu, GroupId));
	Menu.SubMenu(
		LOCTEXT("AddFilterChild", "Filter"),
		MixtormatIcons::Generated(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAddFilterMenu, GroupId));
	Menu.Item(
		LOCTEXT("AddGeneratedChild", "Generated Mask"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			AddProceduralChildToGroup(GroupId, EMixtormatLayerChildType::Generated);
		}));
	Menu.Item(
		LOCTEXT("AddColorIdChild", "Color ID Mask"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			AddProceduralChildToGroup(GroupId, EMixtormatLayerChildType::ColorId);
		}));
	Menu.Item(
		LOCTEXT("AddRandomIdChild", "Random From IDs"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			AddProceduralChildToGroup(GroupId, EMixtormatLayerChildType::RandomId);
		}));

	Menu.Separator();
	Menu.Item(
		LOCTEXT("RenameGroupContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			SelectLayerGroup(GroupId);
			BeginRenameSelection();
		}))
		.Shortcut(LOCTEXT("RenameGroupShortcut", "F2"));
	Menu.Separator();
	Menu.Item(
		bGroupEnabled
			? LOCTEXT("DisableGroupContext", "Disable")
			: LOCTEXT("EnableGroupContext", "Enable"),
		bGroupEnabled ? MixtormatIcons::EyeOff() : MixtormatIcons::Eye(),
		FSimpleDelegate::CreateLambda([this, GroupId, bGroupEnabled]()
		{
			SetLayerGroupEnabled(GroupId, !bGroupEnabled);
		}));

	Menu.Separator();
	// Ungroup, not Delete: it releases the layers and keeps every one of them, which is the only
	// group operation that cannot lose work.
	Menu.Item(
		LOCTEXT("UngroupGroupContext", "Ungroup"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerRow(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FText DisplayName = GetLayerDisplayName(LayerIndex);
	const TWeakPtr<SMixtormat> WeakOwner = StaticCastSharedRef<SMixtormat>(AsShared());

	const FGuid LayerId = Layer.LayerId;
	TSharedPtr<SMixtormatLayerRow> Row;
	TSharedRef<SMixtormatLayerContainer> Container = SNew(SMixtormatLayerContainer)
		.bExpanded_Lambda([WeakOwner, LayerIndex]()
		{
			const TSharedPtr<SMixtormat> Owner = WeakOwner.Pin();
			return Owner.IsValid() && Owner->IsLayerExpanded(LayerIndex);
		})
		.Header()
		[
			SAssignNew(Row, SMixtormatLayerRow)
			.Name(DisplayName)
			// The raw authored name, not the decorated one above: committing "Foo (ref)" back
			// would write the decoration into the layer and it would grow on every rename.
			.EditableName_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].DisplayName
					: FText::GetEmpty();
			})
			.OnNameCommitted_Lambda([this, LayerId](const FText& Text, ETextCommit::Type)
			{
				RenameLayer(LayerId, Text);
			})
			.bReference(!Layer.SourceComposition.IsNull())
			.Source(GetLayerSourceText(LayerIndex))
			.Badge(MixtormatLayerBadges::ForLayer(Layer))
			.ColorBadge_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					? MixtormatLayerBadges::ForColorBlendMode(
						WorkingLayers[LayerIndex].BaseColorBlendMode)
					: FText::GetEmpty();
			})
			.bCanDisable(true)
			.Thumbnail()[BuildLayerThumbnail(LayerIndex)]
			.bEnabled_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].bEnabled;
			})
			.bExpanded_Lambda([WeakOwner, LayerIndex]()
			{
				const TSharedPtr<SMixtormat> Owner = WeakOwner.Pin();
				return Owner.IsValid() && Owner->IsLayerExpanded(LayerIndex);
			})
			.bSelected_Lambda([this, LayerIndex]()
			{
				return SelectedLayerIndex == LayerIndex || IsLayerMultiSelected(LayerIndex);
			})
			.bSolo_Lambda([this, LayerIndex]() { return SoloLayerIndex == LayerIndex; })
			.OnSelected_Lambda([this, LayerIndex]() { SelectWorkingLayer(LayerIndex); })
			.OnToggleExpanded_Lambda([this, LayerIndex]() { ToggleLayerExpanded(LayerIndex); })
			.OnToggleEnabled_Lambda([this, LayerIndex]()
			{
				if (!WorkingLayers.IsValidIndex(LayerIndex))
				{
					return;
				}
				SetWorkingLayerEnabled(
					WorkingLayers[LayerIndex].bEnabled ? ECheckBoxState::Unchecked : ECheckBoxState::Checked,
					LayerIndex);
			})
			.OnToggleSolo_Lambda([this, LayerIndex]() { ToggleLayerSolo(LayerIndex); })
			.OnGetContextMenu(this, &SMixtormat::BuildLayerContextMenu, LayerIndex)
			.OnDragDetected_Lambda([this, LayerIndex, DisplayName](const FGeometry&, const FPointerEvent&)
			{
				return FReply::Handled().BeginDragDrop(
					FMixtormatLayerDragDropOp::New(LayerIndex, DisplayName));
			})
		];

	LayerRowWidgets.Add(LayerId, Row);

	for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
	{
		const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
		const bool bEffect = Child.Type == EMixtormatLayerChildType::Effect;
		const bool bBlur = Child.Type == EMixtormatLayerChildType::Blur;
		const bool bCurvature = Child.Type == EMixtormatLayerChildType::Curvature;
		// Procedural children share row actions, not mask blending semantics.
		const bool bGenerated = Child.Type == EMixtormatLayerChildType::Generated
			|| Child.Type == EMixtormatLayerChildType::Craquelure
			|| Child.Type == EMixtormatLayerChildType::ColorId
			|| Child.Type == EMixtormatLayerChildType::Filter
			|| Child.Type == EMixtormatLayerChildType::HsvFilter
			|| Child.Type == EMixtormatLayerChildType::RandomId
			|| Child.Type == EMixtormatLayerChildType::RampId
			|| Child.Type == EMixtormatLayerChildType::PatternId;
		const FText ChildName = GetLayerChildName(Child);

		Container->AddChild(
			SNew(SBox)
			.Padding(FMargin(
				GetScopeDepth(Layer, ChildIndex) * MixtormatTokens::LayerScopeIndent,
				0.0f,
				0.0f,
				0.0f))
			[
			SNew(SMixtormatChildDropTarget)
			.LayerIndex(LayerIndex)
			.ChildIndex(ChildIndex)
			.OnChildReordered(this, &SMixtormat::ReorderLayerChild)
			.OnChildMovedToLayer(this, &SMixtormat::MoveChildToLayer)
			[
				SNew(SMixtormatLayerChildRow)
				.ToolTip(BuildMaskPreviewTooltip(LayerIndex, ChildIndex))
				.Name(ChildName)
				.Kind(GetLayerChildSourceText(LayerIndex, ChildIndex))
				.Badge(MixtormatLayerBadges::ForChild(Child))
				.Icon()[BuildLayerChildIcon(LayerIndex, ChildIndex)]
				.bActive_Lambda([this, LayerIndex, ChildIndex]()
				{
					return IsLayerChildEnabled(LayerIndex, ChildIndex);
				})
				.bSelected_Lambda([this, LayerIndex, ChildIndex, bEffect]()
				{
					// Effects and masks are selected through separate indices, so which one to
					// compare against depends on what the child is.
					return SelectedLayerIndex == LayerIndex
						&& (bEffect ? SelectedEffectIndex : SelectedMaskIndex) == ChildIndex;
				})
				.OnSelected_Lambda([this, LayerIndex, ChildIndex]()
				{
					SelectWorkingChild(LayerIndex, ChildIndex);
				})
				.OnToggleActive_Lambda([this, LayerIndex, ChildIndex, bEffect, bGenerated]()
				{
					const ECheckBoxState Next = IsLayerChildEnabled(LayerIndex, ChildIndex)
						? ECheckBoxState::Unchecked
						: ECheckBoxState::Checked;
					if (bEffect)
					{
						ToggleLayerEffect(LayerIndex, ChildIndex);
					}
					else if (bGenerated)
					{
						SetGeneratedEnabled(Next, LayerIndex, ChildIndex);
					}
					else
					{
						SetMaskEnabled(Next, LayerIndex, ChildIndex);
					}
				})
				.OnGetContextMenu_Lambda([this, LayerIndex, ChildIndex, bEffect, bGenerated, bBlur, bCurvature]()
				{
					if (bBlur || bCurvature)
					{
						// One menu: a blur and a curvature offer the same actions, because what
						// they have in common -- scoped, consumed, instanceable -- is everything
						// the menu is about.
						return BuildBlurContextMenu(LayerIndex, ChildIndex);
					}
					if (bGenerated)
					{
						return BuildGeneratedContextMenu(LayerIndex, ChildIndex);
					}
					return bEffect
						? BuildEffectContextMenu(LayerIndex, ChildIndex)
						: BuildMaskContextMenu(LayerIndex, ChildIndex);
				})
				// Decided here, not in the lambda: Child is a reference into an array the row
				// outlives, and the answer cannot change without the row being rebuilt anyway.
				.OnDragDetected_Lambda(
					[this, LayerIndex, ChildIndex, ChildName, bCanLeaveLayer = !IsMaskFilter(Child)]
					(const FGeometry&, const FPointerEvent&)
				{
					return FReply::Handled().BeginDragDrop(
						FMixtormatChildDragDropOp::New(
							LayerIndex, ChildIndex, ChildName, bCanLeaveLayer));
				})
			]
		]);
	}

	return SNew(SMixtormatLayerRowDropTarget)
		.TargetLayerIndex(LayerIndex)
		.OnLayerInsertedAt(this, &SMixtormat::HandleLayerInsertedAt)
		.OnGroupInsertedAt(this, &SMixtormat::HandleGroupInsertedAt)
		.OnSurfaceInsertedAt(this, &SMixtormat::HandleSurfaceDroppedAt)
		.OnChildMovedToLayer(this, &SMixtormat::MoveChildToLayer)
		.OnMaskDropped(this, &SMixtormat::AssignMaskToLayer)
		[
			Container
		];
}

bool SMixtormat::IsLayerChildEnabled(const int32 LayerIndex, const int32 ChildIndex) const
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return false;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Effect:    return Child.Effect.bEnabled;
	case EMixtormatLayerChildType::Generated: return Child.Generated.bEnabled;
	case EMixtormatLayerChildType::Craquelure: return Child.Craquelure.bEnabled;
	case EMixtormatLayerChildType::ColorId:   return Child.ColorId.bEnabled;
	case EMixtormatLayerChildType::Filter:    return Child.Filter.bEnabled;
	case EMixtormatLayerChildType::HsvFilter: return Child.HsvFilter.bEnabled;
	case EMixtormatLayerChildType::RandomId:  return Child.RandomId.bEnabled;
	case EMixtormatLayerChildType::RampId:    return Child.RampId.bEnabled;
	case EMixtormatLayerChildType::PatternId: return Child.PatternId.bEnabled;
	case EMixtormatLayerChildType::Blur:      return Child.Blur.bEnabled;
	case EMixtormatLayerChildType::Curvature: return Child.Curvature.bEnabled;
	default:                                  return Child.Mask.bEnabled;
	}
}

FReply SMixtormat::ToggleLayerSolo(const int32 LayerIndex)
{
	// Solo and the before/after comparison answer the same question, so turning one on turns the
	// other off rather than leaving the preview showing something neither setting describes.
	SoloLayerIndex = SoloLayerIndex == LayerIndex ? INDEX_NONE : LayerIndex;
	if (SoloLayerIndex != INDEX_NONE)
	{
		bShowCompositionBefore = false;
	}
	RefreshLayeredPreview(false);
	RebuildLayerList();
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildLayerContextMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;

	// Creation lives here and nowhere else. The stack used to carry an "Add Child" button at the
	// bottom of every expanded layer, which cost a row of height per layer to say something the
	// right button already implies.
	Menu.Caption(LOCTEXT("LayerAddSection", "Add"));
	const FSoftObjectPath SelectedLayerMaskPath = SelectedMaskPath;
	const FText SelectedLayerMaskName = SelectedLibraryMaskName.IsEmpty()
		? LOCTEXT("NoSelectedLibraryMask", "Select Mask from Gallery")
		: SelectedLibraryMaskName;
	Menu.Item(
		FText::Format(LOCTEXT("AddSelectedMaskToLayer", "Add Mask · {0}"), SelectedLayerMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, SelectedLayerMaskPath]()
		{
			AssignMaskToLayer(LayerIndex, SelectedLayerMaskPath);
		}))
		.Enabled(TAttribute<bool>(!SelectedLayerMaskPath.IsNull()));
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddEffectMenu, LayerIndex));
	Menu.SubMenu(
		LOCTEXT("AddFilterChild", "Filter"),
		MixtormatIcons::Generated(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFilterMenu, LayerIndex));
	Menu.Item(
		LOCTEXT("AddGeneratedChild", "Generated Mask"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddGeneratedMaskToLayer(LayerIndex); }));
	// Beside the other mask producers rather than under the asset picker above, because it needs
	// no asset: it reads the layer it is added to.
	Menu.Item(
		LOCTEXT("AddLayerValuesChild", "Layer Values Mask"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddLayerValuesMaskToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddColorIdChild", "Color ID Mask"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddColorIdMaskToLayer(LayerIndex); }));
	// Listed with the mask producers rather than under Filter, because that is what it is: it
	// emits 0..1 coverage and blends like any other mask. Only what it reads is unusual.
	Menu.Item(
		LOCTEXT("AddRandomIdChild", "Random From IDs"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddRandomIdToLayer(LayerIndex); }));

	Menu.Separator();

	// Solo is reachable two ways on purpose: ctrl or alt on the eye for someone who knows, and
	// here for someone who does not. A modifier that exists nowhere in the UI is a secret.
	Menu.Item(
		LOCTEXT("SoloLayerContext", "Solo"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { ToggleLayerSolo(LayerIndex); }))
		.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return SoloLayerIndex == LayerIndex;
		}));
	Menu.Item(
		LOCTEXT("DisableLayerContext", "Disable"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			if (!WorkingLayers.IsValidIndex(LayerIndex))
			{
				return;
			}
			SetWorkingLayerEnabled(
				WorkingLayers[LayerIndex].bEnabled ? ECheckBoxState::Unchecked : ECheckBoxState::Checked,
				LayerIndex);
		}))
		.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].bEnabled;
		}));

	Menu.Separator();

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& (WorkingLayers[LayerIndex].Type == EMixtormatLayerType::Material
			|| WorkingLayers[LayerIndex].Type == EMixtormatLayerType::Effect))
	{
		// Reuse the persistent bottom-library selection instead of opening a second thumbnail gallery.
		// Capturing the path keeps the action deterministic for the lifetime of this menu.
		const FSoftObjectPath ReplacementPath = SelectedSurfacePath;
		const FText ReplacementName = SelectedLibrarySurfaceName.IsEmpty()
			? LOCTEXT("SelectedMaterialFallback", "Selected Material")
			: SelectedLibrarySurfaceName;
		Menu.Item(
			FText::Format(LOCTEXT("ReplaceWithSelectedSurfaceContext", "Replace with {0}"), ReplacementName),
			MixtormatIcons::LayerMaterial(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ReplacementPath]()
			{
				ReplaceSurfaceInLayer(LayerIndex, ReplacementPath);
			}))
			.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));
	}

	Menu.Item(
		LOCTEXT("RenameLayerContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			SelectWorkingLayer(LayerIndex);
			BeginRenameSelection();
		}))
		.Shortcut(LOCTEXT("RenameShortcut", "F2"));
	Menu.Item(
		LOCTEXT("DuplicateLayerContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]() { DuplicateSelectedLayer(); }));

	// Acts on the multi-selection, not on the row the menu opened over, so shift-picking a run of
	// layers and right-clicking any of them does the same thing as the toolbar button.
	// The menu is rebuilt on every open, so the count in the label is the count at open time.
	const int32 SelectionCount = GetSelectedLayerIndices().Num();
	Menu.Item(
		SelectionCount > 1
			? FText::Format(LOCTEXT("CreateGroupFromManyContext", "Group {0} Layers"),
				FText::AsNumber(SelectionCount))
			: LOCTEXT("CreateGroupContext", "Create Group"),
		MixtormatIcons::Folder(),
		FSimpleDelegate::CreateLambda([this]() { CreateGroupFromSelection(); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]() { return CanCreateGroupFromSelection(); }));
	if (WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].GroupId.IsValid())
	{
		const FGuid GroupId = WorkingLayers[LayerIndex].GroupId;
		Menu.Item(
			LOCTEXT("UngroupLayerContext", "Ungroup"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));
	}

	// Paste lands a copy at the end of the layer. Paste Instance lands a live one at the top,
	// which is what a layer header means, except that a source in this same layer pushes it to the
	// first slot below that source. The row stays visible and disabled when nothing works.
	Menu.Item(
		LOCTEXT("PasteChildContext", "Paste"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { PasteLayerChild(LayerIndex); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]() { return CanPasteLayerChild(); }));
	Menu.Item(
		LOCTEXT("PasteChildInstanceContext", "Paste Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { PasteChildInstance(LayerIndex); }))
		.Enabled(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return CanPasteChildInstance(LayerIndex, INDEX_NONE);
		}));

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask
				&& !Child.ScopeOwnerChildId.IsValid();
		}))
	{
		Menu.Item(
			LOCTEXT("ClearLayerMasksContext", "Remove All Masks"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex]() { ClearLayerMask(LayerIndex); }));
	}

	Menu.Separator();
	Menu.Item(
		LOCTEXT("DeleteLayerContext", "Delete"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this]() { DeleteSelectedLayer(); }))
		.Destructive();

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddMaskMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	if (FMixtormatRegistry::GetMasks().IsEmpty())
	{
		Menu.Item(LOCTEXT("MasksUnavailable", "No masks available"), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}

	// The same grid the replace menu opens: a mask is picked by looking at it.
	Menu.Widget(
		SNew(SBox)
		.WidthOverride(MixtormatTokens::MaskPickerWidth)
		.MaxDesiredHeight(MixtormatTokens::MaskPickerMaxHeight)
		[
			BuildMaskGallery([this, LayerIndex](const FSoftObjectPath& Path)
			{
				AssignMaskToLayer(LayerIndex, Path);
			})
		]);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddScopedMaskMenu(
	const int32 LayerIndex,
	const int32 OwnerChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(OwnerChildIndex)
		|| !CanOwnScopedMasks(WorkingLayers[LayerIndex].Children[OwnerChildIndex])
		|| !CanAddScopedChild(WorkingLayers[LayerIndex], OwnerChildIndex)
		|| FMixtormatRegistry::GetMasks().IsEmpty())
	{
		Menu.Item(LOCTEXT("ScopedMasksUnavailable", "No masks available"), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}

	Menu.Widget(
		SNew(SBox)
		.WidthOverride(MixtormatTokens::MaskPickerWidth)
		.MaxDesiredHeight(MixtormatTokens::MaskPickerMaxHeight)
		[
			BuildMaskGallery([this, LayerIndex, OwnerChildIndex](const FSoftObjectPath& Path)
			{
				AssignScopedMaskToChild(LayerIndex, OwnerChildIndex, Path);
			})
		]);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddFilterMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("AddClusterFilterChild", "Cluster IDs"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddFilterToLayer(LayerIndex); }))
		.Enabled(WorkingLayers.IsValidIndex(LayerIndex));
	Menu.Item(
		LOCTEXT("AddPatternIdChild", "Pattern IDs"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddPatternIdToLayer(LayerIndex); }))
		.Enabled(WorkingLayers.IsValidIndex(LayerIndex));
	Menu.Item(
		LOCTEXT("AddHsvFilterChild", "HSV From IDs"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddHsvFilterToLayer(LayerIndex); }))
		.Enabled(WorkingLayers.IsValidIndex(LayerIndex));
	Menu.Item(
		LOCTEXT("AddRampIdChild", "Ramp From IDs"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddRampIdToLayer(LayerIndex); }))
		.Enabled(WorkingLayers.IsValidIndex(LayerIndex));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddEffectMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	const TArray<FMixtormatEffectEntry> Effects = FMixtormatRegistry::GetEffects();
	for (const FMixtormatEffectEntry& Entry : Effects)
	{
		Menu.Item(
			Entry.DisplayName,
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, EffectPath = Entry.AssetPath]()
			{
				AddEffectToLayer(LayerIndex, EffectPath);
			}));
	}

	// Procedural, so they are not discovered from an imported asset set and always available.
	if (!Effects.IsEmpty())
	{
		Menu.Separator();
	}
	Menu.Item(
		LOCTEXT("AddCraquelureChild", "Craquelure"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddCraquelureToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddWetStainEffect", "Wet Stain"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			AddStainToLayer(LayerIndex, EMixtormatStainMode::Wet);
		}));
	Menu.Item(
		LOCTEXT("AddDepositStainEffect", "Stain Deposit"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			AddStainToLayer(LayerIndex, EMixtormatStainMode::Deposit);
		}));
	Menu.Item(
		LOCTEXT("AddRunoffEffect", "Runoff"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddRunoffToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddErosionEffect", "Erosion"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddErosionToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddGradeEffect", "Grade"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddGradeToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddChippingEffect", "Chipping"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddChippingToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddWornEdgesEffect", "Worn Edges"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddWornEdgesToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddFlowWarpEffect", "Flow Warp · Targets Layer"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddFlowWarpToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddLayerBlurEffect", "Layer Blur"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddLayerBlurToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddProceduralPeelEffect", "Peeling (Procedural)"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddProceduralPeelingToLayer(LayerIndex); }));

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildEffectContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	bool bWornEdges = false;
	bool bCanNestChild = false;
	bool bCanOwnFlowWarp = false;
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& SourceChild = *ResolveChild(LayerIndex, ChildIndex);
		bCanNestChild = CanAddScopedChild(WorkingLayers[LayerIndex], ChildIndex);
		bCanOwnFlowWarp = CanOwnFlowWarp(SourceChild);
		if (SourceChild.Type == EMixtormatLayerChildType::Effect)
		{
			EMixtormatEffectType Type = SourceChild.Effect.ProceduralType;
			if (const UMixtormatEffect* Asset = SourceChild.Effect.Effect.LoadSynchronous())
			{
				Type = Asset->EffectType;
			}
			bWornEdges = Type == EMixtormatEffectType::WornEdges;
		}
	}
	if (bWornEdges)
	{
		Menu.Item(
			LOCTEXT("CopyWearInstanceMask", "Copy Instance Mask from Wear"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
			{
				CopyInstanceMaskFromWear(LayerIndex, ChildIndex);
			}));
		Menu.Separator();
	}

	const FSoftObjectPath SelectedEffectMaskPath = SelectedMaskPath;
	const FText SelectedEffectMaskName = SelectedLibraryMaskName.IsEmpty()
		? LOCTEXT("NoSelectedEffectMask", "Select Mask from Gallery")
		: SelectedLibraryMaskName;
	Menu.Item(
		FText::Format(LOCTEXT("AddSelectedMaskToEffect", "Add Gating Mask · {0}"), SelectedEffectMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, SelectedEffectMaskPath]()
		{
			AssignScopedMaskToChild(LayerIndex, ChildIndex, SelectedEffectMaskPath);
		}))
		.Enabled(TAttribute<bool>(!SelectedEffectMaskPath.IsNull() && bCanNestChild));
	Menu.Item(
		LOCTEXT("AddFlowWarpToEffect", "Add Flow Warp · Targets This Effect"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			AddFlowWarpToLayer(LayerIndex, ChildIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild && bCanOwnFlowWarp));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateEffectChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, LayerIndex, ChildIndex);
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveEffectChild", "Remove Effect"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveLayerEffect(LayerIndex, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	// Filters emit data rather than coverage, so there is nothing for a blend mode to mean on
	// one. The random-value mask is a mask and keeps its submenu like every other mask row.
	const EMixtormatLayerChildType RowType =
		WorkingLayers.IsValidIndex(LayerIndex)
			&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
			? ResolveChild(LayerIndex, ChildIndex)->Type
			: EMixtormatLayerChildType::Mask;
	const bool bFilter = RowType == EMixtormatLayerChildType::Filter
		|| RowType == EMixtormatLayerChildType::HsvFilter
		|| RowType == EMixtormatLayerChildType::RampId
		|| RowType == EMixtormatLayerChildType::PatternId;

	if (RowType == EMixtormatLayerChildType::PatternId)
	{
		Menu.Item(
			LOCTEXT("CopyPatternGapInstanceMask", "Copy Instance Mask from Gap"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
			{
				CopyInstanceMaskFromPatternGap(LayerIndex, ChildIndex);
			}));
		Menu.Separator();
	}

	if (!bFilter)
	{
		Menu.SubMenu(
			LOCTEXT("GeneratedBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildGeneratedBlendModeMenu, LayerIndex, ChildIndex));
		Menu.Separator();
	}
	Menu.Item(
		LOCTEXT("DuplicateGeneratedChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, LayerIndex, ChildIndex);
	Menu.Separator();
	// Named after the row it is on. This menu serves generated masks, craquelure and colour id
	// nodes, and "Remove Generated Mask" on a craquelure row reads like the wrong entry. Resolved
	// here rather than bound, because the menu is rebuilt on every right-click.
	FText RemoveLabel = LOCTEXT("RemoveGeneratedChild", "Remove Generated Mask");
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		switch (ResolveChild(LayerIndex, ChildIndex)->Type)
		{
		case EMixtormatLayerChildType::Craquelure:
			RemoveLabel = LOCTEXT("RemoveCraquelureChild", "Remove Craquelure");
			break;
		case EMixtormatLayerChildType::ColorId:
			RemoveLabel = LOCTEXT("RemoveColorIdChild", "Remove Color ID Mask");
			break;
		case EMixtormatLayerChildType::Filter:
			RemoveLabel = LOCTEXT("RemoveFilterChild", "Remove Cluster IDs");
			break;
		case EMixtormatLayerChildType::HsvFilter:
			RemoveLabel = LOCTEXT("RemoveHsvFilterChild", "Remove HSV From IDs");
			break;
		case EMixtormatLayerChildType::RandomId:
			RemoveLabel = LOCTEXT("RemoveRandomIdChild", "Remove Random From IDs");
			break;
		case EMixtormatLayerChildType::RampId:
			RemoveLabel = LOCTEXT("RemoveRampIdChild", "Remove Ramp From IDs");
			break;
		case EMixtormatLayerChildType::PatternId:
			RemoveLabel = LOCTEXT("RemovePatternIdChild", "Remove Pattern IDs");
			break;
		default:
			break;
		}
	}

	Menu.Item(
		RemoveLabel,
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveGeneratedFromLayer(LayerIndex, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

// Fewer entries than a mask's, because a blur has none of what that menu offers: no source to
// replace, no blend mode, nothing to publish. What it shares is the instancing and the removal.
TSharedRef<SWidget> SMixtormat::BuildBlurContextMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("DuplicateBlurContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, LayerIndex, ChildIndex);
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveMaskFilterContext", "Remove Filter"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveMaskFromLayer(LayerIndex, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

FMixtormatMaskCurvature* SMixtormat::GetSelectedLayerCurvature()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Curvature ? &Child.Curvature : nullptr;
}

const FMixtormatMaskCurvature* SMixtormat::GetSelectedLayerCurvature() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child =
		*ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Curvature ? &Child.Curvature : nullptr;
}

FMixtormatMaskBlur* SMixtormat::GetSelectedLayerBlur()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Blur ? &Child.Blur : nullptr;
}

const FMixtormatMaskBlur* SMixtormat::GetSelectedLayerBlur() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child =
		*ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Blur ? &Child.Blur : nullptr;
}

TSharedRef<SWidget> SMixtormat::BuildMaskContextMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	// A menu, like every other right-click in the stack. This used to open a 340px gallery titled
	// REPLACE MASK with two buttons under it -- so right-clicking a mask did something entirely
	// unlike right-clicking the effect directly beneath it, and the common actions were below the
	// fold of a picker you had not asked for. Replacing is still here; it is one entry now.
	MixtormatMenu::FBuilder Menu;
	const bool bCanNestChild = WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(MaskIndex)
		&& CanAddScopedChild(WorkingLayers[LayerIndex], MaskIndex);
	Menu.SubMenu(
		LOCTEXT("MaskBlendModeContext", "Blend Mode"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskBlendModeMenu, LayerIndex, MaskIndex));
	const FSoftObjectPath ReplacementPath = SelectedMaskPath;
	const FText ReplacementName = SelectedLibraryMaskName.IsEmpty()
		? LOCTEXT("NoSelectedReplacementMask", "Select Mask from Gallery")
		: SelectedLibraryMaskName;
	Menu.Item(
		FText::Format(LOCTEXT("ReplaceWithSelectedMask", "Replace with {0}"), ReplacementName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex, ReplacementPath]()
		{
			ReplaceMaskInLayer(LayerIndex, MaskIndex, ReplacementPath);
		}))
		.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));
	Menu.Item(
		LOCTEXT("AddBlurToMaskContext", "Add Blur"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddBlurToMask(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Item(
		LOCTEXT("AddCurvatureToMaskContext", "Add Curvature"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddCurvatureToMask(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Item(
		LOCTEXT("AddFlowWarpToMask", "Add Flow Warp · Targets This Mask"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddFlowWarpToLayer(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateMaskContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			DuplicateLayerChild(LayerIndex, MaskIndex);
		}));
	AddSharedChildMenuItems(Menu, LayerIndex, MaskIndex);
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DeleteMaskContext", "Remove Mask"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			RemoveMaskFromLayer(LayerIndex, MaskIndex);
		}))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskReplacementMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	// The gallery keeps its own grid and its own scrolling; the panel gives it nothing but the
	// ground, which is why it is added as a widget rather than as rows.
	MixtormatMenu::FBuilder Menu;
	Menu.Widget(
		SNew(SBox)
		.WidthOverride(MixtormatTokens::MaskPickerWidth)
		.MaxDesiredHeight(MixtormatTokens::MaskPickerMaxHeight)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				BuildMaskReplacementGallery(LayerIndex, MaskIndex)
			]
		]);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskBlendModeMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		// Ticked rather than merely listed: the mode a mask is already in is the thing you most
		// want to know when you open this.
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex, Mode]()
			{
				SetMaskBlendMode(LayerIndex, MaskIndex, Mode);
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, MaskIndex, Mode]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].Children.IsValidIndex(MaskIndex)
					&& WorkingLayers[LayerIndex].Children[MaskIndex].Mask.BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildNormalSourceMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	if (WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].SourceSurface.IsNull())
	{
		Menu.Caption(LOCTEXT("NormalSourceFromLayer", "From this layer"));
		Menu.Item(
			LOCTEXT("UseSurfaceNormal", "Surface Normal"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex]()
			{
				if (!WorkingLayers.IsValidIndex(LayerIndex))
				{
					return;
				}
				WorkingLayers[LayerIndex].NormalSourceType = EMixtormatNormalSourceType::Surface;
				RefreshLayeredPreview();
				RebuildLayerList();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].NormalSourceType == EMixtormatNormalSourceType::Surface;
			}));
		Menu.Separator();
	}

	const TArray<FMixtormatNormalEntry> Normals = FMixtormatRegistry::GetNormals();
	if (!Normals.IsEmpty())
	{
		Menu.Caption(LOCTEXT("NormalSourceLibrary", "Library"));
	}
	for (const FMixtormatNormalEntry& Normal : Normals)
	{
		Menu.Item(
			Normal.DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, Path = Normal.AssetPath]()
			{
				AssignNormalTexture(LayerIndex, Path);
			}));
	}
	if (Normals.IsEmpty())
	{
		Menu.Item(LOCTEXT("NormalsUnavailable", "No standalone normals available"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddLayerMenu()
{
	// Two kinds of layer, since Effect stopped being one and Normal Detail became a composition.
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		FText::Format(
			LOCTEXT("AddSelectedMaterialLayer", "Material · {0}"),
			SelectedLibrarySurfaceName.IsEmpty()
				? LOCTEXT("NoSelectedLibraryMaterial", "Select from Library")
				: SelectedLibrarySurfaceName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this]() { AddWorkingLayer(EMixtormatLayerType::Material); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]() { return !SelectedSurfacePath.IsNull(); }));
	Menu.Item(
		LOCTEXT("AddFillLayer", "Fill"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this]() { AddWorkingLayer(EMixtormatLayerType::Fill); }));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBorder)
		.Padding(FMargin(8.0f, 7.0f))
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.InsetPanel")))
		.Visibility_Lambda([this]()
		{
			return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MaskBarHeading", "MASKS · SELECT, THEN RMB A LAYER OR EFFECT"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontMaskBarHeading))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT("ImportUserMasksHint", "Import PNG masks from a folder"))
					.OnClicked(this, &SMixtormat::ImportMasks)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::ToolbarIconSize)
						.HeightOverride(MixtormatTokens::ToolbarIconSize)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Folder")))
						]
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SMixtormatGalleryScrollBox)
				.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
				[
					SAssignNew(MaskListBox, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(
						MixtormatTokens::MaskGalleryTileGap,
						MixtormatTokens::MaskGalleryTileGap))
				]
			]
		];
}

void SMixtormat::ZoomMaskGallery(const int32 Direction)
{
	MaskGalleryTileSize = FMath::Clamp(
		MaskGalleryTileSize + Direction * MixtormatTokens::MaskGalleryTileStep,
		MixtormatTokens::MaskGalleryTileMinimum,
		MixtormatTokens::MaskGalleryTileMaximum);
	RebuildMaskList();
}

TSharedRef<SWidget> SMixtormat::BuildMaskGallery(TFunction<void(const FSoftObjectPath&)> OnChosen)
{
	// One grid, whether the mask is being added or replaced. Picking a mask is the same question
	// both times, and it was answered two ways: a gallery for replacing, a list of names for
	// adding -- so the choice you made blind was the one that created the thing.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(
			MixtormatTokens::MaskGalleryTileGap,
			MixtormatTokens::MaskGalleryTileGap));

	for (const FMixtormatMaskEntry& Mask : FMixtormatRegistry::GetMasks())
	{
		const FSoftObjectPath Path = Mask.AssetPath;
		Grid->AddSlot()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SMixtormatTile)
				.TileSize_Lambda([this]() { return MaskGalleryTileSize; })
				.DisplayName(Mask.DisplayName)
				.ThumbnailAsset(Mask.ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.ThumbnailResolution(FMath::RoundToInt(MixtormatTokens::MaskGalleryTileMaximum))
				.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
				.OnActivated(FMixtormatOnTileActivated::CreateLambda([OnChosen, Path]()
				{
					FSlateApplication::Get().DismissAllMenus();
					OnChosen(Path);
				}))
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
			[
				MixtormatUI::BuildLibraryOwnershipBadge(Path, false)
			]
		];
	}
	return Grid;
}


TSharedRef<SWidget> SMixtormat::BuildMaskReplacementGallery(const int32 LayerIndex, const int32 MaskIndex)
{
	return BuildMaskGallery([this, LayerIndex, MaskIndex](const FSoftObjectPath& Path)
	{
		ReplaceMaskInLayer(LayerIndex, MaskIndex, Path);
	});
}

TSharedRef<SWidget> SMixtormat::BuildMaskCard(
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset,
	const bool bCompact)
{
	const float CardSize = bCompact ? MaskGalleryTileSize : 52.0f;
	const float ThumbnailSize = bCompact ? FMath::Max(CardSize - 2.0f, 1.0f) : 42.0f;
	TSharedRef<SWidget> ThumbnailWidget = SNew(SBorder)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ThumbnailBackground")));
	if (ThumbnailAsset.IsValid())
	{
		UObject* ThumbnailObject = ThumbnailAsset.GetAsset();
		UTexture2D* ThumbnailTexture = Cast<UTexture2D>(ThumbnailObject);
		if (const UMixtormatMask* MaskAsset = Cast<UMixtormatMask>(ThumbnailObject))
		{
			ThumbnailTexture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
		}
		if (ThumbnailTexture)
		{
			ThumbnailWidget = SNew(SMixtormatTextureTile)
				.Texture(ThumbnailTexture)
				.ImageSize(FVector2D(ThumbnailSize, ThumbnailSize));
		}
	}

	if (bCompact)
	{
		return SNew(SMixtormatMaskCard)
			.DisplayName(Name)
			.MaskPath(AssetPath)
			.ThumbnailAsset(ThumbnailAsset)
			.ThumbnailPool(ThumbnailPool)
			.OnSelected(this, &SMixtormat::SelectMask)
			.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
			.OnGetContextMenu(this, &SMixtormat::BuildMaskLibraryContextMenu, AssetPath)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SMixtormatTile)
					.TileSize_Lambda([this]() { return MaskGalleryTileSize; })
					.DisplayName(Name)
					.ThumbnailAsset(ThumbnailAsset)
					.ThumbnailPool(ThumbnailPool)
					.ThumbnailResolution(FMath::RoundToInt(MixtormatTokens::MaskGalleryTileMaximum))
					.bShowName(false)
					.bShowNameOnHover(false)
					.bSelected_Lambda([this, AssetPath]() { return SelectedMaskPath == AssetPath; })
					.ToolTip(Name)
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
				[
					MixtormatUI::BuildLibraryOwnershipBadge(AssetPath, false)
				]
			];
	}

	return SNew(SButton)
		.ContentPadding(5.0f)
		.OnClicked_Lambda([this, Name, AssetPath]() { return SelectMask(Name, AssetPath); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ThumbnailWidget]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(2.0f)
					[
						MixtormatUI::BuildLibraryOwnershipBadge(AssetPath, false)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Name)
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildMaskLibraryContextMenu(const FSoftObjectPath AssetPath)
{
	const bool bIsUserAsset = MixtormatUI::IsUserLibraryAsset(AssetPath);
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("LibraryMaskContextCaption", "Library Mask"))
		.Item(
			LOCTEXT("BrowseLibraryMask", "Show in Content Browser"),
			MixtormatUI::LucideIcon(TEXT("folder-open")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::BrowseLibraryAsset, AssetPath))
		.Separator()
		.Item(
			LOCTEXT("RemoveImportedMask", "Remove Imported Mask…"),
			MixtormatUI::LucideIcon(TEXT("trash-2")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::RemoveImportedMask, AssetPath))
		.Enabled(bIsUserAsset)
		.Destructive();
	return Menu.Build();
}

void SMixtormat::RemoveImportedMask(const FSoftObjectPath AssetPath)
{
	if (!MixtormatUI::IsUserLibraryAsset(AssetPath))
	{
		return;
	}

	UObject* MaskObject = AssetPath.TryLoad();
	if (!MaskObject)
	{
		RebuildMaskList();
		return;
	}

	TArray<FAssetData> Assets;
	const auto AddUserAsset = [&Assets](UObject* Asset)
	{
		if (Asset && MixtormatUI::IsUserLibraryAsset(FSoftObjectPath(Asset->GetPathName())))
		{
			Assets.AddUnique(FAssetData(Asset));
		}
	};
	AddUserAsset(MaskObject);

	UTexture2D* MaskTexture = Cast<UTexture2D>(MaskObject);
	if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
	{
		MaskTexture = Mask->MaskTexture;
		AddUserAsset(Mask->MaskTexture);
		AddUserAsset(Mask->Thumbnail);
	}
	if (MaskTexture)
	{
		const FString ThumbnailName = MaskTexture->GetName() + TEXT("_Thumbnail");
		const FString ThumbnailPath = FString::Printf(
			TEXT("%s/%s.%s"),
			*FMixtormatPaths::ProjectLibraryMaskThumbnailsRoot(),
			*ThumbnailName,
			*ThumbnailName);
		AddUserAsset(LoadObject<UObject>(nullptr, *ThumbnailPath));
	}

	ObjectTools::DeleteAssets(Assets, true);
	if (SelectedMaskPath == AssetPath)
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath).IsValid())
		{
			SelectedMaskPath.Reset();
			SelectedLibraryMaskName = FText::GetEmpty();
		}
	}
	RebuildMaskList();
}

FReply SMixtormat::AddColorIdMaskToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::ColorId;

	// One entry to start. An id mask with an empty set selects nothing and is dropped before it
	// reaches the graph, so a new node would otherwise sit in the stack looking broken until the
	// first colour was added by hand.
	Child.ColorId.Colors.Add(FLinearColor::Red);

	SelectedLayerIndex = LayerIndex;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatColorIdMask* SMixtormat::GetSelectedColorId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::ColorId ? &Child.ColorId : nullptr;
}

const FMixtormatColorIdMask* SMixtormat::GetSelectedColorId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::ColorId ? &Child.ColorId : nullptr;
}

FReply SMixtormat::AddFilterToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Filter;
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatClusterFilter* SMixtormat::GetSelectedFilter()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Filter ? &Child.Filter : nullptr;
}

const FMixtormatClusterFilter* SMixtormat::GetSelectedFilter() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Filter ? &Child.Filter : nullptr;
}

bool SMixtormat::CanPreviewSelectedFilter() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| bBypassSelectedChild
		|| !WorkingLayers[SelectedLayerIndex].bEnabled)
	{
		return false;
	}

	if (const FMixtormatPatternFilter* Pattern = GetSelectedPatternId())
	{
		return Pattern->bEnabled;
	}

	const FMixtormatClusterFilter* Filter = GetSelectedFilter();
	if (!Filter || !Filter->bEnabled)
	{
		return false;
	}

	// Normal-detail layers remain eligible when their surface carries the packed source.
	const UMixtormatSurface* Surface = WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
	return Surface && Surface->RoughnessAOMetallic;
}

FReply SMixtormat::AddHsvFilterToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::HsvFilter;
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::HsvFilter ? &Child.HsvFilter : nullptr;
}

const FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::HsvFilter ? &Child.HsvFilter : nullptr;
}

FReply SMixtormat::AddPatternIdToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::PatternId;
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::PatternId ? &Child.PatternId : nullptr;
}

const FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::PatternId ? &Child.PatternId : nullptr;
}

FReply SMixtormat::AddRampIdToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::RampId;
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RampId ? &Child.RampId : nullptr;
}

const FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RampId ? &Child.RampId : nullptr;
}

FReply SMixtormat::AddRandomIdToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::RandomId;
	SetLayerExpanded(LayerIndex, true);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RandomId ? &Child.RandomId : nullptr;
}

const FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RandomId ? &Child.RandomId : nullptr;
}

FReply SMixtormat::AddCraquelureToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Craquelure;
	SelectedLayerIndex = LayerIndex;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
}

const FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
}

// An ordinary Mask child whose source is the layer's own values.
//
// Deliberately not a child type of its own. Everything that makes a mask useful -- the blend into
// the chain, the placement, the shaping, a scoped Blur or Curvature under it, publishing it,
// instancing it -- already belongs to FMixtormatMaskLayer, and a separate type would have to
// re-earn all of it. This sets one enum.
FReply SMixtormat::AddLayerValuesMaskToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Mask;
	Child.Mask.Source = EMixtormatMaskSource::LayerValues;
	// Bisecting a freeze: back to the pre-session rule while the cause is narrowed down.
	// First mask on the container replaces, later ones multiply. The chain seeds at black, so
	// Multiply on a first mask would resolve to nothing -- hence the special case.
	const bool bHasTopLevelMask = Layer.Children.ContainsByPredicate(
		[](const FMixtormatLayerChild& Existing)
		{
			return Existing.Type == EMixtormatLayerChildType::Mask
				&& !Existing.ScopeOwnerChildId.IsValid();
		});
	Child.Mask.BlendMode = bHasTopLevelMask
		? EMixtormatMaskBlendMode::Multiply
		: EMixtormatMaskBlendMode::Replace;
	SelectedLayerIndex = LayerIndex;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AddGeneratedMaskToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Generated;
	SelectedLayerIndex = LayerIndex;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::RemoveGeneratedFromLayer(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Handled();
	}

	// All procedural children routed through the shared row actions, including data filters.
	const EMixtormatLayerChildType ChildType = ResolveChild(LayerIndex, ChildIndex)->Type;
	if (ChildType != EMixtormatLayerChildType::Generated
		&& ChildType != EMixtormatLayerChildType::Craquelure
		&& ChildType != EMixtormatLayerChildType::ColorId
		&& ChildType != EMixtormatLayerChildType::Filter
		&& ChildType != EMixtormatLayerChildType::HsvFilter
		&& ChildType != EMixtormatLayerChildType::RandomId
		&& ChildType != EMixtormatLayerChildType::RampId
		&& ChildType != EMixtormatLayerChildType::PatternId)
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
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
	const int32 SubtreeEnd = FindSubtreeEnd(Layer, ChildIndex);
	Layer.Children.RemoveAt(ChildIndex, SubtreeEnd - ChildIndex);
	if (SelectedLayerIndex == LayerIndex)
	{
		SelectedEffectIndex = FindChildById(Layer, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer, SelectedMaskId);
		if ((SelectedEffectId.IsValid() && SelectedEffectIndex == INDEX_NONE)
			|| (SelectedMaskId.IsValid() && SelectedMaskIndex == INDEX_NONE))
		{
			bBypassSelectedChild = false;
		}
	}
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

void SMixtormat::SetGeneratedEnabled(
	const ECheckBoxState CheckState,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}

	// The shared procedural-child row routes mask producers and data filters here.
	const bool bEnabled = CheckState == ECheckBoxState::Checked;
	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Generated:
		Child.Generated.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::Craquelure:
		Child.Craquelure.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::ColorId:
		Child.ColorId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::Filter:
		Child.Filter.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::HsvFilter:
		Child.HsvFilter.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::RandomId:
		Child.RandomId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::RampId:
		Child.RampId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::PatternId:
		Child.PatternId.bEnabled = bEnabled;
		break;
	default:
		return;
	}

	RefreshLayeredPreview();
	RebuildLayerList();
}

FMixtormatGeneratedMask* SMixtormat::GetSelectedGeneratedMask()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generated ? &Child.Generated : nullptr;
}

const FMixtormatGeneratedMask* SMixtormat::GetSelectedGeneratedMask() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generated ? &Child.Generated : nullptr;
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedBlendModeMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				if (!ResolveChild(LayerIndex, ChildIndex)
					|| ResolveChild(LayerIndex, ChildIndex)->Type
						!= EMixtormatLayerChildType::Generated)
				{
					return;
				}
				ResolveChild(LayerIndex, ChildIndex)->Generated.BlendMode = Mode;
				RefreshLayeredPreview();
				RebuildLayerList();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				return ResolveChild(LayerIndex, ChildIndex)
					&& ResolveChild(LayerIndex, ChildIndex)->Generated.BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

FReply SMixtormat::AddStainToLayer(
	const int32 LayerIndex,
	const EMixtormatStainMode Mode)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Stain;
	Child.Effect.StainMode = Mode;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedStain()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Stain
		: Effect->ProceduralType != EMixtormatEffectType::Stain))
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedStain() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Stain
		: Effect->ProceduralType != EMixtormatEffectType::Stain))
	{
		return nullptr;
	}
	return Effect;
}

// The cheap streak, next to Wet Stain in the menu because that is what an artist is choosing
// between. No mode to pick: Runoff has one behaviour.
FReply SMixtormat::AddRunoffToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Runoff;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedRunoff()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Runoff
		: Effect->ProceduralType != EMixtormatEffectType::Runoff))
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedRunoff() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Runoff
		: Effect->ProceduralType != EMixtormatEffectType::Runoff))
	{
		return nullptr;
	}
	return Effect;
}

FReply SMixtormat::AddErosionToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Erosion;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AddChippingToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Chipping;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}


FReply SMixtormat::AddWornEdgesToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::WornEdges;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AddGradeToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Grade;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedGrade()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Grade)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedGrade() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Grade)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedChipping()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Chipping)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedChipping() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Chipping)
	{
		return nullptr;
	}
	return Effect;
}


FMixtormatLayerEffect* SMixtormat::GetSelectedWornEdges()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::WornEdges)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedWornEdges() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::WornEdges)
	{
		return nullptr;
	}
	return Effect;
}

FReply SMixtormat::AddFlowWarpToLayer(
	const int32 LayerIndex,
	const int32 OwnerChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const bool bScoped = Layer.Children.IsValidIndex(OwnerChildIndex);
	if (OwnerChildIndex != INDEX_NONE
		&& (!bScoped
			|| !CanOwnFlowWarp(Layer.Children[OwnerChildIndex])
			|| !CanAddScopedChild(Layer, OwnerChildIndex)))
	{
		return FReply::Handled();
	}

	const int32 InsertAt = bScoped
		? FindSubtreeEnd(Layer, OwnerChildIndex)
		: Layer.Children.Num();
	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
	if (bScoped)
	{
		Child.ScopeOwnerChildId = Layer.Children[OwnerChildIndex].ChildId;
	}
	Layer.Children.Insert(MoveTemp(Child), InsertAt);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = InsertAt;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedFlowWarp()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::FlowWarp
		? Effect
		: nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedFlowWarp() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::FlowWarp
		? Effect
		: nullptr;
}

FReply SMixtormat::AddLayerBlurToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::LayerBlur;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedLayerBlurEffect()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::LayerBlur
		? Effect
		: nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedLayerBlurEffect() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::LayerBlur
		? Effect
		: nullptr;
}

FReply SMixtormat::AddProceduralPeelingToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	// Null effect asset plus a Peeling procedural type is what selects the generated field.
	Child.Effect.ProceduralType = EMixtormatEffectType::Peeling;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatLayerEffect* SMixtormat::GetSelectedProceduralPeel()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Peeling)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedProceduralPeel() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Peeling)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedErosion()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Erosion)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedErosion() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Erosion)
	{
		return nullptr;
	}
	return Effect;
}

#undef LOCTEXT_NAMESPACE
