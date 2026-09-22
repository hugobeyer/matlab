// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Services/MixtormatPaths.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Parameters/MixtormatParameterAuthoring.h"

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
		// Generators as well as effects. A mask scoped under a generator is the whole of its
		// Mask Influence: it is the seed source when there is one, and it steers seed
		// probability, propagation cost and carve amplitude. Without this the control exists
		// with nothing to read.
		return Child.Type == EMixtormatLayerChildType::Effect
			|| Child.Type == EMixtormatLayerChildType::Generator;
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

	// Scoping is a property of a child array, not of what owns one. A layer's Children and a
	// group's shared Children are the same shape and obey the same rules, so these take the array
	// -- which is what lets one set of creators serve both containers. (They were duplicated per
	// container before, because the layer versions took an FMixtormatLayer a group cannot supply.)
	int32 FindChildById(const TArray<FMixtormatLayerChild>& Children, const FGuid& ChildId)
	{
		return ChildId.IsValid()
			? Children.IndexOfByPredicate([&ChildId](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == ChildId;
			})
			: INDEX_NONE;
	}

	int32 GetScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return 0;
		}

		int32 Depth = 0;
		int32 CurrentIndex = ChildIndex;
		FGuid OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid())
		{
			if (Visited.Contains(OwnerId))
			{
				return MaximumScopeDepth + 1;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Children, OwnerId);
			if (OwnerIndex == INDEX_NONE
				|| !CanKeepScopedPlacement(
					Children[OwnerIndex],
					Children[CurrentIndex]))
			{
				return MaximumScopeDepth + 1;
			}
			++Depth;
			CurrentIndex = OwnerIndex;
			OwnerId = Children[OwnerIndex].ScopeOwnerChildId;
		}
		return Depth;
	}

	// How far a row is indented.
	//
	// Real scope and nothing else. Combine IDs used to be indented one extra level whenever an ID
	// producer sat above it, on the theory that the indent showed what it reads -- but every ID
	// consumer in the plugin reads the nearest producer above it, Combine included, and indenting
	// only this one claimed a containment that does not exist. Pattern IDs, Cluster IDs and
	// Combine IDs are siblings in the stack, and the row now says so. A genuine ScopeOwnerChildId
	// still indents, here as everywhere.
	int32 GetDisplayScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		return GetScopeDepth(Children, ChildIndex);
	}

	bool IsDescendantOf(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& AncestorId)
	{
		if (!Children.IsValidIndex(ChildIndex) || !AncestorId.IsValid())
		{
			return false;
		}

		FGuid OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid() && !Visited.Contains(OwnerId))
		{
			if (OwnerId == AncestorId)
			{
				return true;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Children, OwnerId);
			if (OwnerIndex == INDEX_NONE)
			{
				return false;
			}
			OwnerId = Children[OwnerIndex].ScopeOwnerChildId;
		}
		return false;
	}

	int32 FindSubtreeEnd(const TArray<FMixtormatLayerChild>& Children, const int32 RootIndex)
	{
		if (!Children.IsValidIndex(RootIndex))
		{
			return RootIndex;
		}
		const FGuid RootId = Children[RootIndex].ChildId;
		int32 End = RootIndex + 1;
		while (Children.IsValidIndex(End) && IsDescendantOf(Children, End, RootId))
		{
			++End;
		}
		return End;
	}

	// The ancestor of ChildIndex whose own owner is ParentId -- so FGuid() asks for the top-level
	// root of whatever subtree the index lands in.
	int32 FindSiblingRoot(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& ParentId)
	{
		int32 CurrentIndex = ChildIndex;
		TSet<FGuid> Visited;
		while (Children.IsValidIndex(CurrentIndex))
		{
			const FMixtormatLayerChild& Current = Children[CurrentIndex];
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
			CurrentIndex = FindChildById(Children, Current.ScopeOwnerChildId);
		}
		return INDEX_NONE;
	}

	bool CanAddScopedChild(const TArray<FMixtormatLayerChild>& Children, const int32 OwnerIndex)
	{
		return Children.IsValidIndex(OwnerIndex)
			&& GetScopeDepth(Children, OwnerIndex) < MaximumScopeDepth;
	}

	// Puts Child under the owner at OwnerChildIndex and returns where it landed, or INDEX_NONE if
	// that owner cannot take it. At the end of the owner's subtree, so the owner and everything
	// already gating through it stay one contiguous block -- which is the invariant every walk
	// here depends on.
	//
	// The type-pair rule is CanKeepScopedPlacement's, the same one a drag is validated against, so
	// what the menus offer and what a drop accepts cannot drift apart.
	int32 InsertScopedChild(
		TArray<FMixtormatLayerChild>& Children,
		const int32 OwnerChildIndex,
		FMixtormatLayerChild&& Child)
	{
		if (!Children.IsValidIndex(OwnerChildIndex)
			|| !CanKeepScopedPlacement(Children[OwnerChildIndex], Child)
			|| !CanAddScopedChild(Children, OwnerChildIndex))
		{
			return INDEX_NONE;
		}
		const int32 InsertAt = FindSubtreeEnd(Children, OwnerChildIndex);
		Child.ScopeOwnerChildId = Children[OwnerChildIndex].ChildId;
		Children.Insert(MoveTemp(Child), InsertAt);
		return InsertAt;
	}

	// The three scoped things a mask can carry, as prototypes for InsertScopedChild. Flow Warp is
	// an Effect with a procedural type rather than a type of its own, which is why this exists
	// instead of the callers passing an enum.
	FMixtormatLayerChild MakeScopedPrototype(const EMixtormatLayerChildType ChildType)
	{
		FMixtormatLayerChild Child;
		Child.Type = ChildType;
		return Child;
	}

	FMixtormatLayerChild MakeFlowWarpPrototype()
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::Effect;
		Child.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
		return Child;
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

	// Everything a newly created child needs beyond its Type, in one place.
	//
	// Both Add menus and both containers come through here, which is the point: the red first
	// colour on a Color ID mask and the LayerValues source on a Layer Values mask used to exist
	// only on the layer path, so the same entry on a group produced a differently configured node.
	// The serialised child type one menu entry produces. Several kinds share one: Texture Mask and
	// Layer Values Mask are both Mask, and Strata Carver is a Generator. That collapse is exactly
	// why EMixtormatChildCreation exists alongside the type.
	EMixtormatLayerChildType ChildTypeForCreation(const EMixtormatChildCreation Kind)
	{
		switch (Kind)
		{
		case EMixtormatChildCreation::PatternIds:      return EMixtormatLayerChildType::PatternId;
		case EMixtormatChildCreation::ClusterIds:      return EMixtormatLayerChildType::Filter;
		case EMixtormatChildCreation::CombineIds:      return EMixtormatLayerChildType::CombineId;
		case EMixtormatChildCreation::HsvFromIds:      return EMixtormatLayerChildType::HsvFilter;
		case EMixtormatChildCreation::RampFromIds:     return EMixtormatLayerChildType::RampId;
		case EMixtormatChildCreation::UvFromIds:       return EMixtormatLayerChildType::UvFromIds;
		case EMixtormatChildCreation::ReliefFromIds:   return EMixtormatLayerChildType::ReliefFromIds;
		case EMixtormatChildCreation::GeneratedMask:   return EMixtormatLayerChildType::Generated;
		case EMixtormatChildCreation::ColorIdMask:     return EMixtormatLayerChildType::ColorId;
		case EMixtormatChildCreation::RandomFromIds:   return EMixtormatLayerChildType::RandomId;
		case EMixtormatChildCreation::StrataCarver:    return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::Peeling:         return EMixtormatLayerChildType::Effect;
		default:                                       return EMixtormatLayerChildType::Mask;
		}
	}

	void ApplyChildCreationDefaults(
		FMixtormatLayerChild& Child,
		const EMixtormatChildCreation Kind)
	{
		Child.Type = ChildTypeForCreation(Kind);
		switch (Kind)
		{
		case EMixtormatChildCreation::PatternIds:
			// New Patterns own topology only; keep serialized defaults for existing nodes.
			Child.PatternId.bUVVariation = false;
			Child.PatternId.HeightAmount = 0.0f;
			Child.PatternId.BevelHeight = 0.0f;
			Child.PatternId.GapHeight = 0.0f;
			Child.PatternId.EdgeRoughnessAmount = 0.0f;
			Child.PatternId.AOAmount = 0.0f;
			break;
		case EMixtormatChildCreation::LayerValuesMask:
			// Fixed at creation and never offered as a switch afterwards. What a mask reads is
			// its identity -- a Layer Values mask has no asset to name and is told apart from a
			// Texture mask by exactly this -- so flipping it on a live node would silently turn
			// one kind of node into another, and an instance of it would change kind with it.
			Child.Mask.Source = EMixtormatMaskSource::LayerValues;
			// Replace, whatever is already on the stack. A new mask is added to be looked at, and
			// Multiply against an existing mask shows nothing wherever that mask is dark -- which
			// reads as the mask having failed to load rather than as two masks combining.
			Child.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
			break;
		case EMixtormatChildCreation::ColorIdMask:
			// One entry to start. A Color Range mask with an empty set selects nothing and is
			// dropped before it reaches the graph, so a new node would otherwise sit in the stack
			// looking broken until the first colour was added by hand. Exact ID ignores the list,
			// and an unused red entry there costs nothing.
			Child.ColorId.Colors.Add(FLinearColor::Red);
			break;
		case EMixtormatChildCreation::StrataCarver:
			Child.Generator.Type = EMixtormatGeneratorType::StrataCarver;
			break;
		case EMixtormatChildCreation::Peeling:
			Child.Effect.Effect.Reset();
			Child.Effect.ProceduralType = EMixtormatEffectType::Peeling;
			break;
		default:
			// Every remaining kind is fully described by its type.
			break;
		}

		// Genuinely-new effect children receive the current persistent plugin defaults;
		// duplication and instance resolve never pass through here, so authored values on
		// copies and serialized assets are untouched.
		if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			MixtormatParameterAuthoring::ApplyAuthoringDefaults(
				Child.Effect, Child.Effect.ProceduralType);
		}
	}

	// Writes the Link-mode reference that makes Y the same value as X, using the existing
	// reference system exactly as the context menu does -- Y holds a binding with Mode = Link
	// naming X, so editing either side lands on X and unlinking is the existing Clear Reference.
	// Creation-time only: old saved materials are never touched, and a pair that starts unequal
	// by design gets no link at all.
	void LinkChildPair(
		FMixtormatLayerChild& Child,
		const FGuid& ContainerId,
		const EMixtormatParameterOwnerType Owner,
		const FName XName,
		const FName YName,
		const EMixtormatParameterValueType ValueType)
	{
		FMixtormatParameterAddress Source;
		Source.LayerId = ContainerId;
		Source.ChildId = Child.ChildId;
		Source.Owner = Owner;
		Source.Parameter = XName;
		Source.ValueType = ValueType;

		FMixtormatParameterBinding& Binding = Child.ParameterBindings.AddDefaulted_GetRef();
		Binding.DestinationOwner = Owner;
		Binding.DestinationParameter = YName;
		Binding.ValueType = ValueType;
		Binding.Reference.bEnabled = true;
		Binding.Reference.Mode = EMixtormatReferenceMode::Link;
		Binding.Reference.Source = Source;
	}

	// Which of a new child's X/Y pairs are one uniform quantity split across axes, and therefore
	// start linked. Deliberately short: offsets, Rows/Columns and every min/max range are
	// independent by meaning and stay unlinked.
	void ApplyLinkDefaults(FMixtormatLayerChild& Child, const FGuid& ContainerId)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask:
			// Symmetric tiling. A Layer Values mask has no placement block at all, so it stays out.
			if (Child.Mask.Source == EMixtormatMaskSource::Texture)
			{
				LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Mask,
					GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, TilingX),
					GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, TilingY),
					EMixtormatParameterValueType::Int);
			}
			break;
		case EMixtormatLayerChildType::ColorId:
			// Same symmetric tiling as a texture mask.
			LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::ColorId,
				GET_MEMBER_NAME_CHECKED(FMixtormatColorIdMask, TilingX),
				GET_MEMBER_NAME_CHECKED(FMixtormatColorIdMask, TilingY),
				EMixtormatParameterValueType::Int);
			break;
		case EMixtormatLayerChildType::Blur:
			// Equal radii are the ordinary Gaussian; the pair is anisotropic only on purpose.
			LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Blur,
				GET_MEMBER_NAME_CHECKED(FMixtormatMaskBlur, RadiusX),
				GET_MEMBER_NAME_CHECKED(FMixtormatMaskBlur, RadiusY),
				EMixtormatParameterValueType::Float);
			break;
		case EMixtormatLayerChildType::Effect:
			if (Child.Effect.ProceduralType == EMixtormatEffectType::LayerBlur)
			{
				LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Effect,
					GET_MEMBER_NAME_CHECKED(FMixtormatLayerEffect, LayerBlurRadiusX),
					GET_MEMBER_NAME_CHECKED(FMixtormatLayerEffect, LayerBlurRadiusY),
					EMixtormatParameterValueType::Float);
			}
			break;
		default:
			break;
		}
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

	SoloLayerIndex = INDEX_NONE;
	const int32 DroppedReferences =
		MixtormatUI::ReorderLayersByPermutation(WorkingLayers, NewOrder);
	// The block carried its GroupId with it, so the run is still whole, and the snap above already
	// kept it out of another group's run. This is the general backstop for shapes that snap does
	// not cover -- hand-edited data, a merge -- not the normal path for a group-on-group drop.
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
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput)
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
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput)
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
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::UvFromIds
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::ReliefFromIds
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::PatternId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::CombineId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Generator
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

	// Replace, whatever is already on the stack. A new mask is added to be looked at, and
	// Multiply against an existing mask shows nothing wherever that mask is dark -- which reads
	// as the mask having failed to load rather than as two masks combining. The chain starts from
	// white, so Replace is what makes it visible on its own; combining is a deliberate second
	// step through the row's Blend Mode.
	NewMask.BlendMode = EMixtormatMaskBlendMode::Replace;
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Mask;
	Child.Mask = MoveTemp(NewMask);
	ApplyLinkDefaults(Child, Layer.LayerId);
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
// Blur and Curvature are the two mask filters, inserted the same way and for the same reasons:
// they gate the mask above them, so they live inside its subtree rather than beside it.
FReply SMixtormat::AddBlurToMask(const int32 LayerIndex, const int32 OwnerChildIndex)
{
	return AddMaskFilterToLayerChild(LayerIndex, OwnerChildIndex, EMixtormatLayerChildType::Blur);
}

FReply SMixtormat::AddCurvatureToMask(const int32 LayerIndex, const int32 OwnerChildIndex)
{
	return AddMaskFilterToLayerChild(LayerIndex, OwnerChildIndex, EMixtormatLayerChildType::Curvature);
}

FReply SMixtormat::AddMaskFilterToLayerChild(
	const int32 LayerIndex,
	const int32 OwnerChildIndex,
	const EMixtormatLayerChildType ChildType)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}
	const int32 InsertAt = InsertScopedChild(
		WorkingLayers[LayerIndex].Children,
		OwnerChildIndex,
		MakeScopedPrototype(ChildType));
	if (InsertAt == INDEX_NONE)
	{
		return FReply::Handled();
	}
	ApplyLinkDefaults(WorkingLayers[LayerIndex].Children[InsertAt], WorkingLayers[LayerIndex].LayerId);

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
		|| !CanAddScopedChild(WorkingLayers[LayerIndex].Children, OwnerChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatMaskLayer NewMask;
	if (!BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}
	// Replace, whatever is already on the stack. A new mask is added to be looked at, and
	// Multiply against an existing mask shows nothing wherever that mask is dark -- which reads
	// as the mask having failed to load rather than as two masks combining. The chain starts from
	// white, so Replace is what makes it visible on its own; combining is a deliberate second
	// step through the row's Blend Mode.
	NewMask.BlendMode = EMixtormatMaskBlendMode::Replace;

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid OwnerId = Layer.Children[OwnerChildIndex].ChildId;
	const int32 InsertAt = FindSubtreeEnd(Layer.Children, OwnerChildIndex);

	FMixtormatLayerChild ScopedMask;
	ScopedMask.Type = EMixtormatLayerChildType::Mask;
	ScopedMask.ScopeOwnerChildId = OwnerId;
	ScopedMask.Mask = MoveTemp(NewMask);
	Layer.Children.Insert(MoveTemp(ScopedMask), InsertAt);
	ApplyLinkDefaults(Layer.Children[InsertAt], Layer.LayerId);

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
	// Guarded through ResolveChild rather than by layer index, so the body and the guard agree on
	// which container they are talking about. Nothing addresses a group's shared child through here
	// yet; an index guard would answer "no such child" instead of resolving one if something did.
	if (!ResolveChild(LayerIndex, ChildIndex)
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
			Layer.Children.RemoveAt(ChildIndex, FindSubtreeEnd(Layer.Children, ChildIndex) - ChildIndex);
		}
	}

	if (SelectedLayerIndex == LayerIndex)
	{
		SelectedEffectIndex = FindChildById(Layer.Children, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer.Children, SelectedMaskId);
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
		const int32 SubtreeEnd = FindSubtreeEnd(Layer.Children, ChildIndex);

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
		// The source has to be top-level: BuildGroupChildRow gives a scoped filter no drag source
		// (bCanLeaveLayer is false for one), but a direct call is refused the same way a drag would
		// have been.
		|| Group->Children[SourceChildIndex].ScopeOwnerChildId.IsValid())
	{
		return FReply::Unhandled();
	}
	// The target can land inside a scoped subtree, and that still means "reorder relative to its
	// root". The rows do render depth now, so this is a choice rather than a consequence: a scoped
	// filter has exactly one valid place -- inside its owner's subtree -- so the only thing a drop
	// aimed at one can sensibly mean is moving the subtree it belongs to.
	TargetChildIndex = FindSiblingRoot(Group->Children, TargetChildIndex, FGuid());
	if (TargetChildIndex == INDEX_NONE || TargetChildIndex == SourceChildIndex)
	{
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
		// Top-level only, the same rule ReorderGroupChild enforces: a scoped filter cannot be
		// orphaned from the mask it filters, and it travels as part of that mask's subtree instead.
		|| Group->Children[ChildIndex].ScopeOwnerChildId.IsValid())
	{
		return FReply::Unhandled();
	}
	if (IsMaskFilter(Group->Children[ChildIndex]))
	{
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
		const FGuid MovedChildId = MovedChildren[MoveIndex].ChildId;
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
		// GroupId as the old parent: a group's shared children are addressed by GroupId in the
		// same slot a layer's are addressed by LayerId, which is what MoveChildToGroup wrote on the
		// way in. RemapChildParent compares that id rather than resolving it, so the reverse
		// direction works without it having to know a group from a layer.
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups}, MovedChildId, GroupId, NewLayerId);
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
	// Recorded here, unlike MoveChildToLayer/MoveChildToGroup, which record nothing -- see the note
	// on those two. Un-sharing is destructive to every other member of the group, so it is the last
	// move that should be missing from the undo stack.
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::DuplicateLayerChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const int32 InsertAt = FindSubtreeEnd(Layer.Children, ChildIndex);
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

// Note: neither this nor MoveChildToGroup calls RecordEditHistory or marks the document dirty,
// so a move between two containers is currently absent from the undo stack. MoveGroupChildToLayer
// does record, because leaving a group destroys the child for every other member -- but the
// inconsistency is real and predates it.
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
		const FGuid MovedChildId = MovedChildren[MoveIndex].ChildId;
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups}, MovedChildId, OldLayerId, NewLayerId);
	}

	SetLayerExpanded(DestLayerIndex, true);
	SelectWorkingChild(DestLayerIndex, InsertAt);
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
	if (IsMaskFilter(SourceLayer.Children[ChildIndex]))
	{
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
		const FGuid MovedChildId = MovedChildren[MoveIndex].ChildId;
		Group->Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
		// GroupId, not a LayerId: a group's shared children are addressed by GroupId in the same
		// slot a layer's are addressed by LayerId, so this is the same remap MoveChildToLayer does.
		// The scope carries WorkingLayerGroups too, so a reference held by another shared child
		// follows this one across exactly as a layer child's would.
		MixtormatParameterBinding::RemapChildParent(
			FMixtormatMutableBindingScope{WorkingLayers, WorkingLayerGroups}, MovedChildId, OldLayerId, GroupId);
	}

	CollapsedGroupIds.Remove(GroupId);
	SelectGroupChild(GroupId, InsertAt);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FMixtormatChildAddress SMixtormat::MakeChildAddress(const int32 LayerIndex, const int32 ChildIndex) const
{
	FMixtormatChildAddress Address;
	if (WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		Address.OwnerType = EMixtormatChildOwnerType::Layer;
		Address.OwnerId = WorkingLayers[LayerIndex].LayerId;
		Address.ChildId = WorkingLayers[LayerIndex].Children[ChildIndex].ChildId;
	}
	return Address;
}

FMixtormatChildAddress SMixtormat::MakeGroupChildAddress(const FGuid GroupId, const int32 ChildIndex) const
{
	FMixtormatChildAddress Address;
	if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId))
	{
		if (Group->Children.IsValidIndex(ChildIndex))
		{
			Address.OwnerType = EMixtormatChildOwnerType::Group;
			Address.OwnerId = GroupId;
			Address.ChildId = Group->Children[ChildIndex].ChildId;
		}
	}
	return Address;
}

FMixtormatChildAddress SMixtormat::GetSelectedChildAddress() const
{
	const int32 ChildIndex = GetSelectedChildIndex();
	return SelectedGroupId.IsValid() && SelectedLayerIndex == INDEX_NONE
		? MakeGroupChildAddress(SelectedGroupId, ChildIndex)
		: MakeChildAddress(SelectedLayerIndex, ChildIndex);
}

TArray<FMixtormatLayerChild>* SMixtormat::ResolveContainer(const FMixtormatChildAddress& Address)
{
	return const_cast<TArray<FMixtormatLayerChild>*>(
		const_cast<const SMixtormat*>(this)->ResolveContainer(Address));
}

const TArray<FMixtormatLayerChild>* SMixtormat::ResolveContainer(const FMixtormatChildAddress& Address) const
{
	if (Address.OwnerType == EMixtormatChildOwnerType::Group)
	{
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Address.OwnerId);
		return Group ? &Group->Children : nullptr;
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId == Address.OwnerId)
		{
			return &Layer.Children;
		}
	}
	return nullptr;
}

FMixtormatLayerChild* SMixtormat::ResolveChildAt(const FMixtormatChildAddress& Address)
{
	return const_cast<FMixtormatLayerChild*>(
		const_cast<const SMixtormat*>(this)->ResolveChildAt(Address));
}

const FMixtormatLayerChild* SMixtormat::ResolveChildAt(const FMixtormatChildAddress& Address) const
{
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	if (!Container)
	{
		return nullptr;
	}
	return Container->FindByPredicate([&Address](const FMixtormatLayerChild& Child)
	{
		return Child.ChildId == Address.ChildId;
	});
}

int32 SMixtormat::ResolveChildIndexAt(const FMixtormatChildAddress& Address) const
{
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	return Container
		? Container->IndexOfByPredicate([&Address](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == Address.ChildId;
		})
		: INDEX_NONE;
}

void SMixtormat::CopyChild(const FMixtormatChildAddress& Address, const bool bAsInstance)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return;
	}
	FMixtormatChildClipboard Clipboard;
	Clipboard.Mode = bAsInstance ? EMixtormatChildClipboardMode::Instance : EMixtormatChildClipboardMode::Copy;
	Clipboard.Payload = *Child;
	// Copying an instance as an instance yields its source, not the instance. Pointing at the
	// instance would build a chain for the resolver to unwind with nothing gained by it.
	if (Child->IsInstance())
	{
		Clipboard.Source.OwnerId = Child->SourceLayerId;
		Clipboard.Source.ChildId = Child->SourceChildId;
		Clipboard.Source.OwnerType = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Child->SourceLayerId)
			? EMixtormatChildOwnerType::Group
			: EMixtormatChildOwnerType::Layer;
	}
	else
	{
		Clipboard.Source = Address;
	}
	ChildClipboard = MoveTemp(Clipboard);
}

void SMixtormat::CopyChildOutput(const FMixtormatChildAddress& Address, const FName OutputName)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return;
	}
	const FMixtormatChildCapabilities Capabilities = GetChildCapabilities(*Child);
	const FMixtormatPublishedOutputDesc* Output = Capabilities.Outputs.FindByPredicate(
		[OutputName](const FMixtormatPublishedOutputDesc& Candidate)
		{
			return Candidate.Name == OutputName && Candidate.bCopyableAsMask;
		});
	if (!Output)
	{
		return;
	}

	FMixtormatLayerChild PublishedMask;
	PublishedMask.Type = EMixtormatLayerChildType::Mask;
	PublishedMask.Mask.bEnabled = true;
	PublishedMask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	PublishedMask.Mask.Weight = 1.0f;
	PublishedMask.Mask.PublishedSourceLayerId = Address.OwnerId;
	PublishedMask.Mask.PublishedSourceChildId = Address.ChildId;
	PublishedMask.Mask.PublishedSourceOutput = OutputName;

	FMixtormatChildClipboard Clipboard;
	Clipboard.Mode = EMixtormatChildClipboardMode::PublishedOutput;
	Clipboard.Payload = MoveTemp(PublishedMask);
	Clipboard.Source = Address;
	Clipboard.PublishedOutput = OutputName;
	ChildClipboard = MoveTemp(Clipboard);
	WorkingStatusText = FText::Format(
		LOCTEXT("ChildOutputCopied", "{0} output copied"), Output->Label).ToString();
}

int32 SMixtormat::ResolvePasteInsertIndex(
	const FMixtormatChildAddress& Dest,
	const int32 AnchorChildIndex) const
{
	if (!ChildClipboard.IsSet())
	{
		return INDEX_NONE;
	}
	const TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	if (!DestContainer)
	{
		return INDEX_NONE;
	}
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();

	if (Clipboard.Mode != EMixtormatChildClipboardMode::Instance)
	{
		// A plain duplicate or a published-output mask is severed from its source, so any position
		// works -- except a mask filter, which only ever travels scoped beneath the mask it filters
		// and can never be pasted as a standalone row.
		if (IsMaskFilter(Clipboard.Payload))
		{
			return INDEX_NONE;
		}
		return DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : DestContainer->Num();
	}

	// A container's top-level paste (a layer header, or a group's own top) names no row and means
	// the top; a child row means directly above that row.
	const int32 Preferred = DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : 0;

	// A source in this same container has to stay earlier than its instance, and the source never
	// moves to make that true -- so the instance moves down instead, to the first slot after it. A
	// source in a different container is already earlier by the cross-container ordering rule below
	// and keeps the asked-for row.
	int32 Insert = Preferred;
	if (Clipboard.Source.OwnerId == Dest.OwnerId)
	{
		const int32 SourceChildIndex = DestContainer->IndexOfByPredicate(
			[&Clipboard](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == Clipboard.Source.ChildId;
			});
		if (SourceChildIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		Insert = FMath::Max(Insert, SourceChildIndex + 1);
	}

	// Still classified, never assumed. This only picks a candidate; the policy decides.
	return MixtormatParameterBinding::ClassifyInstancePlacement(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
		Clipboard.Source.OwnerId,
		Clipboard.Source.ChildId,
		Dest.OwnerId,
		Insert) == MixtormatParameterBinding::EInstancePlacement::Valid
		? Insert
		: INDEX_NONE;
}

bool SMixtormat::CanPasteChild(const FMixtormatChildAddress& Dest, const int32 AnchorChildIndex) const
{
	return ResolvePasteInsertIndex(Dest, AnchorChildIndex) != INDEX_NONE;
}

FText SMixtormat::GetChildPasteReason(
	const FMixtormatChildAddress& Dest,
	const int32 AnchorChildIndex) const
{
	if (!ChildClipboard.IsSet())
	{
		return LOCTEXT("PasteNothingCopied", "Nothing copied. Use Copy, Copy as Instance or Copy Output on a child first.");
	}
	if (!ResolveContainer(Dest))
	{
		return FText::GetEmpty();
	}
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();
	if (Clipboard.Mode != EMixtormatChildClipboardMode::Instance)
	{
		if (IsMaskFilter(Clipboard.Payload))
		{
			return LOCTEXT("PasteMaskFilterNotStandalone", "A Blur or Curvature only travels with the mask it filters.");
		}
		return LOCTEXT("PasteReady", "Place a copy of what was copied.");
	}
	const int32 Insert = ResolvePasteInsertIndex(Dest, AnchorChildIndex);
	if (Insert != INDEX_NONE)
	{
		return LOCTEXT("InstancePasteReady", "Place a live instance of the copied child.");
	}
	const TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	using EPlacement = MixtormatParameterBinding::EInstancePlacement;
	switch (MixtormatParameterBinding::ClassifyInstancePlacement(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
		Clipboard.Source.OwnerId,
		Clipboard.Source.ChildId,
		Dest.OwnerId,
		DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : 0))
	{
	case EPlacement::SourceMissing:
		return LOCTEXT("InstanceSourceGone", "The copied child no longer exists.");
	case EPlacement::SelfReference:
		return LOCTEXT("InstanceSelf", "A child cannot be an instance of itself.");
	default:
		return LOCTEXT(
			"InstanceOrder",
			"The source composites after this position. An instance can only read a child that resolves before it.");
	}
}

FReply SMixtormat::PasteChild(const FMixtormatChildAddress& Dest, const int32 AnchorChildIndex)
{
	const int32 Insert = ResolvePasteInsertIndex(Dest, AnchorChildIndex);
	TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	if (Insert == INDEX_NONE || !DestContainer)
	{
		return FReply::Unhandled();
	}
	const FMixtormatChildClipboard Clipboard = ChildClipboard.GetValue();
	int32 FinalInsert = Insert;

	if (Clipboard.Mode == EMixtormatChildClipboardMode::Instance)
	{
		FMixtormatLayerChild Instance = Clipboard.Payload;
		Instance.ChildId = FGuid::NewGuid();
		Instance.SourceLayerId = Clipboard.Source.OwnerId;
		Instance.SourceChildId = Clipboard.Source.ChildId;
		Instance.ScopeOwnerChildId.Invalidate();

		if (DestContainer->IsValidIndex(AnchorChildIndex)
			&& CanAddScopedChild(*DestContainer, AnchorChildIndex)
			&& CanKeepScopedPlacement((*DestContainer)[AnchorChildIndex], Instance))
		{
			const int32 ScopedInsert = FindSubtreeEnd(*DestContainer, AnchorChildIndex);
			// Same-container instances may have to remain below their source. Only attach when
			// that ordering still permits a contiguous owner subtree.
			if (FinalInsert <= ScopedInsert)
			{
				FinalInsert = ScopedInsert;
				Instance.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
			}
		}
		DestContainer->Insert(MoveTemp(Instance), FinalInsert);
	}
	else
	{
		// A plain paste (or a published-output mask, which is built with no identity of its own) is
		// a duplicate: fresh identity, and no tie to where it came from.
		FMixtormatLayerChild Pasted = Clipboard.Payload;
		Pasted.SourceLayerId = FGuid();
		Pasted.SourceChildId = FGuid();
		// No feature owner at paste time. Duplicate the mask payload, not a placement link that may
		// name a child in another container.
		Pasted.ScopeOwnerChildId.Invalidate();
		DestContainer->Insert(MoveTemp(Pasted), FinalInsert);
		MixtormatParameterBinding::RegenerateChildIdentity((*DestContainer)[FinalInsert]);
	}

	if (Dest.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		const int32 DestLayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Dest](const FMixtormatLayer& Layer) { return Layer.LayerId == Dest.OwnerId; });
		SetLayerExpanded(DestLayerIndex, true);
		SelectWorkingChild(DestLayerIndex, FinalInsert);
	}
	else
	{
		CollapsedGroupIds.Remove(Dest.OwnerId);
		SelectGroupChild(Dest.OwnerId, FinalInsert);
	}
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
	const TArray<FMixtormatLayerGroup> GroupSnapshot = WorkingLayerGroups;
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		MixtormatParameterBinding::ResolveChildInstances(
			FMixtormatBindingScope{Snapshot, GroupSnapshot}, Layer);
	}
	// A group's own shared children can be instances too now that Copy as Instance can target a
	// group (see PasteChild) -- ResolveChildInstances only ever mirrors a layer's Children, so each
	// group's stack is wrapped in a scratch layer to reuse it rather than a second resolver.
	for (FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		FMixtormatLayer Scratch;
		Scratch.Children = Group.Children;
		MixtormatParameterBinding::ResolveChildInstances(
			FMixtormatBindingScope{Snapshot, GroupSnapshot}, Scratch);
		Group.Children = MoveTemp(Scratch.Children);
	}

	// An instance source can change its payload kind. Keep selection on the same identity and move
	// it to the matching effect/mask selection lane after the mirror updates.
	if (SelectedChildId.IsValid() && WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FMixtormatLayer& SelectedLayer = WorkingLayers[SelectedLayerIndex];
		const int32 SelectedChildIndex = FindChildById(SelectedLayer.Children, SelectedChildId);
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
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	return Child && Child->IsInstance();
}


FText SMixtormat::GetSelectedInstanceSourceText() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	if (!Child || !Child->IsInstance())
	{
		return FText::GetEmpty();
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId == Child->SourceLayerId)
		{
			if (const FMixtormatLayerChild* Source = Layer.Children.FindByPredicate(
				[Child](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Child->SourceChildId; }))
			{
				return FText::Format(
					LOCTEXT("InstanceSourceLine", "Source: {0} / {1}"),
					Layer.DisplayName,
					GetLayerChildName(*Source));
			}
		}
	}
	if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Child->SourceLayerId))
	{
		if (const FMixtormatLayerChild* Source = Group->Children.FindByPredicate(
			[Child](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Child->SourceChildId; }))
		{
			return FText::Format(
				LOCTEXT("InstanceGroupSourceLine", "Source: {0} / {1}"),
				Group->DisplayName,
				GetLayerChildName(*Source));
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
							[this]() { GoToChildInstanceSource(GetSelectedChildAddress()); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::RowGap, 0.0f)
					[
						Action(
							LOCTEXT("InstanceBreak", "Break Instance"),
							LOCTEXT("InstanceBreakHint", "Keep the values it is showing as this child's own and edit them here."),
							[this]() { BreakChildInstanceAt(GetSelectedChildAddress()); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMixtormatChip)
						.Text(LOCTEXT("InstanceReplaceSource", "Replace Source"))
						.OnGetMenuContent_Lambda([this]()
						{
							return BuildReplaceInstanceSourceMenu(GetSelectedChildAddress());
						})
					]
				]
			]
		];
}

FReply SMixtormat::GoToChildInstanceSource(const FMixtormatChildAddress& Address)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return FReply::Unhandled();
	}
	const FGuid SourceLayerId = Child->SourceLayerId;
	const FGuid SourceChildId = Child->SourceChildId;
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
	// Not a layer -- the source may be a group's shared child.
	if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SourceLayerId))
	{
		const int32 SourceChildIndex = Group->Children.IndexOfByPredicate(
			[&SourceChildId](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.ChildId == SourceChildId;
			});
		if (SourceChildIndex != INDEX_NONE)
		{
			CollapsedGroupIds.Remove(SourceLayerId);
			SelectGroupChild(SourceLayerId, SourceChildIndex);
			RebuildLayerList();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SMixtormat::BreakChildInstanceAt(const FMixtormatChildAddress& Address)
{
	FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return FReply::Unhandled();
	}
	// Resolved against a snapshot, because the child being broken lives in the same containers the
	// resolve reads from.
	const TArray<FMixtormatLayer> LayerSnapshot = WorkingLayers;
	const TArray<FMixtormatLayerGroup> GroupSnapshot = WorkingLayerGroups;
	if (!MixtormatParameterBinding::BreakChildInstance(
		FMixtormatBindingScope{LayerSnapshot, GroupSnapshot}, *Child))
	{
		return FReply::Unhandled();
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	SyncSelectedLayerControls();
	return FReply::Handled();
}

void SMixtormat::CopyChildInstanceReference(const FMixtormatChildAddress& Address)
{
	CopyChild(Address, true);
}

FReply SMixtormat::ReplaceChildInstanceSource(
	const FMixtormatChildAddress& Address,
	FMixtormatChildAddress NewSource)
{
	FMixtormatLayerChild* Placement = ResolveChildAt(Address);
	TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	if (!Placement || !Container)
	{
		return FReply::Unhandled();
	}
	if (Placement->ScopeOwnerChildId.IsValid())
	{
		const int32 OwnerIndex = FindChildById(*Container, Placement->ScopeOwnerChildId);
		const FMixtormatLayerChild* NewSourceChild = MixtormatParameterBinding::FindChild(
			FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, NewSource.OwnerId, NewSource.ChildId);
		if (!Container->IsValidIndex(OwnerIndex)
			|| !NewSourceChild
			|| !CanKeepScopedPlacement((*Container)[OwnerIndex], *NewSourceChild))
		{
			return FReply::Unhandled();
		}
	}
	const int32 ChildIndex = ResolveChildIndexAt(Address);
	if (MixtormatParameterBinding::ClassifyInstancePlacement(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
		NewSource.OwnerId,
		NewSource.ChildId,
		Address.OwnerId,
		ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
	{
		return FReply::Unhandled();
	}
	Placement->SourceLayerId = NewSource.OwnerId;
	Placement->SourceChildId = NewSource.ChildId;
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

TSharedRef<SWidget> SMixtormat::BuildReplaceInstanceSourceMenu(const FMixtormatChildAddress Address)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("ReplaceInstanceSourceCaption", "Source"));
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	const FMixtormatLayerChild* Placement = ResolveChildAt(Address);
	if (!Container || !Placement)
	{
		return Menu.Build();
	}
	const int32 ChildIndex = ResolveChildIndexAt(Address);
	const int32 OwnerIndex = FindChildById(*Container, Placement->ScopeOwnerChildId);
	const FMixtormatLayerChild* ScopeOwner = Container->IsValidIndex(OwnerIndex)
		? &(*Container)[OwnerIndex]
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
				FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				SourceLayer.LayerId,
				Candidate.ChildId,
				Address.OwnerId,
				ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
			{
				continue;
			}
			const FMixtormatChildAddress NewSource{
				EMixtormatChildOwnerType::Layer, SourceLayer.LayerId, Candidate.ChildId};
			Menu.Item(
				FText::Format(
					LOCTEXT("ReplaceInstanceSourceEntry", "{0} / {1}"),
					SourceLayer.DisplayName,
					GetLayerChildName(Candidate)),
				nullptr,
				FSimpleDelegate::CreateLambda([this, Address, NewSource]()
				{
					ReplaceChildInstanceSource(Address, NewSource);
				}));
		}
	}
	// A group's shared children are exactly as valid a source as a layer's -- see
	// ClassifyInstancePlacement's conservative member-range ordering rule for groups.
	for (const FMixtormatLayerGroup& SourceGroup : WorkingLayerGroups)
	{
		for (const FMixtormatLayerChild& Candidate : SourceGroup.Children)
		{
			if (Candidate.IsInstance()
				|| (ScopeOwner && !CanKeepScopedPlacement(*ScopeOwner, Candidate)))
			{
				continue;
			}
			if (MixtormatParameterBinding::ClassifyInstancePlacement(
				FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				SourceGroup.GroupId,
				Candidate.ChildId,
				Address.OwnerId,
				ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
			{
				continue;
			}
			const FMixtormatChildAddress NewSource{
				EMixtormatChildOwnerType::Group, SourceGroup.GroupId, Candidate.ChildId};
			Menu.Item(
				FText::Format(
					LOCTEXT("ReplaceInstanceSourceGroupEntry", "{0} / {1}"),
					SourceGroup.DisplayName,
					GetLayerChildName(Candidate)),
				nullptr,
				FSimpleDelegate::CreateLambda([this, Address, NewSource]()
				{
					ReplaceChildInstanceSource(Address, NewSource);
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
	const FMixtormatChildAddress& Address)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	const bool bInstance = Child && Child->IsInstance();

	Menu.Separator();
	Menu.Item(
		LOCTEXT("CopyChildContext", "Copy"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChild(Address, false);
		}));
	Menu.Item(
		LOCTEXT("CopyChildAsInstanceContext", "Copy as Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChild(Address, true);
		}));
	if (Child)
	{
		for (const FMixtormatPublishedOutputDesc& Output : GetCopyableOutputs(GetChildCapabilities(*Child)))
		{
			Menu.Item(
				FText::Format(LOCTEXT("CopyChildOutputContext", "Copy Output · {0}"), Output.Label),
				MixtormatIcons::Mask(),
				FSimpleDelegate::CreateLambda([this, Address, OutputName = Output.Name]()
				{
					CopyChildOutput(Address, OutputName);
				}));
		}
	}
	Menu.Item(
		LOCTEXT("PasteChildContext", "Paste"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			PasteChild(Address, ResolveChildIndexAt(Address));
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, Address]()
		{
			return CanPasteChild(Address, ResolveChildIndexAt(Address));
		}));
	if (Address.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
		const int32 ChildIndex = ResolveChildIndexAt(Address);
		Menu.SubMenu(
			LOCTEXT("MoveChildToLayerContext", "Move to Layer..."),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMoveChildToLayerMenu, LayerIndex, ChildIndex));
	}

	if (!bInstance)
	{
		return;
	}
	Menu.Separator();
	Menu.Caption(LOCTEXT("ChildInstanceCaption", "Instance"));
	Menu.Item(
		LOCTEXT("GoToInstanceSourceContext", "Go to Source"),
		FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.ArrowUp")),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			GoToChildInstanceSource(Address);
		}));
	Menu.Item(
		LOCTEXT("BreakInstanceContext", "Break Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			BreakChildInstanceAt(Address);
		}));
	Menu.SubMenu(
		LOCTEXT("ReplaceInstanceSourceContext", "Replace Source"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildReplaceInstanceSourceMenu, Address));
	Menu.Item(
		LOCTEXT("CopyInstanceReferenceContext", "Copy Instance Reference"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChildInstanceReference(Address);
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
	if (!Effect || Effect->EffectType == EMixtormatEffectType::Peeling)
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
	// The database speaks per family; an asset child's family is the asset's type.
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(LayerEffect, Effect->EffectType);
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
	// Guarded through ResolveChild, for the same reason as ReplaceMaskInLayer above.
	if (ResolveChild(LayerIndex, ChildIndex)
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
		const int32 SubtreeEnd = FindSubtreeEnd(Layer.Children, ChildIndex);
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
				.OnChildDropped(this, &SMixtormat::MoveChildToGroup)
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
				// One indent for being inside the group, plus one per scope level -- the same
				// depth-times-indent a layer's own children get, so a blur under a shared mask
				// reads as being under it rather than beside it.
				LayerListBox->AddSlot()
				.AutoHeight()
				.Padding(
					MixtormatTokens::LayerScopeIndent
						* (1 + GetDisplayScopeDepth(Group->Children, ChildIndex)),
					0.0f,
					0.0f,
					2.0f)
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
			case EMixtormatEffectType::Breakup: return LOCTEXT("BreakupEffectName", "Breakup");
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
		case EMixtormatEffectType::Breakup: return LOCTEXT("BreakupEffectName", "Breakup");
		case EMixtormatEffectType::WornEdges: return LOCTEXT("WornEdgesEffectName", "Worn Edges");
		case EMixtormatEffectType::FlowWarp: return LOCTEXT("FlowWarpEffectName", "Flow Warp");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("LayerBlurEffectName", "Layer Blur");
		case EMixtormatEffectType::Runoff:  return LOCTEXT("RunoffEffectName", "Runoff");
		default:                            return LOCTEXT("ProceduralPeelName", "Peeling");
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
	if (Child.Type == EMixtormatLayerChildType::Generator)
	{
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			return LOCTEXT("StrataCarverChildName", "Strata Carver");
		}
		return LOCTEXT("GeneratorChildName", "Generator");
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
	if (Child.Type == EMixtormatLayerChildType::UvFromIds)
	{
		return LOCTEXT("UvIdChildName", "UV From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::ReliefFromIds)
	{
		return LOCTEXT("ReliefIdChildName", "Relief From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::PatternId)
	{
		return LOCTEXT("PatternIdChildName", "Pattern IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::CombineId)
	{
		return LOCTEXT("CombineIdChildName", "Combine IDs");
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

	const int32 OwnerIndex = FindChildById(Layer.Children, Child.ScopeOwnerChildId);
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
		if (Owner.Type == EMixtormatLayerChildType::Generator)
		{
			// Not "GATES". A mask under an effect decides where that effect is allowed to act;
			// a mask under a generator steers it -- seed placement, propagation cost, carve
			// depth -- and only reaches a plain multiply at the very end. Calling both gating
			// would teach the wrong thing about Mask Influence on the one row where it matters.
			return LOCTEXT("GeneratorSteerMask", "STEERS · GEN");
		}
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
				// A generator takes the effect glyph rather than the generated-mask one. It is
				// neither, but of the two it is the structural node -- it writes height -- and
				// the generated glyph on this row would suggest coverage.
				|| Child.Type == EMixtormatLayerChildType::Generator
			? MixtormatIcons::Effect()
			: (Child.Type == EMixtormatLayerChildType::Generated
					|| Child.Type == EMixtormatLayerChildType::Craquelure
					|| Child.Type == EMixtormatLayerChildType::Filter
					|| Child.Type == EMixtormatLayerChildType::HsvFilter
					|| Child.Type == EMixtormatLayerChildType::RandomId
					|| Child.Type == EMixtormatLayerChildType::RampId
					|| Child.Type == EMixtormatLayerChildType::UvFromIds
					|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
					|| Child.Type == EMixtormatLayerChildType::PatternId
					|| Child.Type == EMixtormatLayerChildType::CombineId)
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

// SelectIndex names the child to leave selected. INDEX_NONE means "the one just appended", which
// is what every append path wants; a scoped insert lands in the middle and passes its own.
void SMixtormat::FinishGroupChildEdit(const FGuid GroupId, const int32 SelectIndex)
{
	// A shared child is broadcast to every member, so unlike a rename this genuinely changes what
	// the compositor draws and has to ask for a new composite.
	// SelectGroupChild, not SelectLayerGroup: the getters the inspector is built from read
	// SelectedMaskIndex / SelectedEffectIndex against a cleared layer index, and SelectGroupChild is
	// what points those lanes at the group's stack. Setting SelectedGroupChildIndex alone left the
	// panel with nothing to show, which is why a shared child added here appeared but could not be
	// edited.
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	const int32 ResolvedIndex = SelectIndex != INDEX_NONE
		? SelectIndex
		: (Group ? Group->Children.Num() - 1 : INDEX_NONE);
	SelectGroupChild(GroupId, ResolvedIndex);
	CollapsedGroupIds.Remove(GroupId);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
}

// The scoped children a group's shared mask can carry, and the reason the helpers above take a
// child array rather than an FMixtormatLayer: a group's stack obeys the same scoping rules, and
// the runtime already remaps ScopeOwnerChildId per member when it flattens the group (see
// MixtormatLayerGroups). So a blur authored once on a shared mask gates that mask in every member.
FReply SMixtormat::AddMaskFilterToGroupChild(
	const FGuid GroupId,
	const int32 OwnerChildIndex,
	const EMixtormatLayerChildType ChildType)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group)
	{
		return FReply::Handled();
	}
	const int32 InsertAt = InsertScopedChild(
		Group->Children,
		OwnerChildIndex,
		MakeScopedPrototype(ChildType));
	if (InsertAt == INDEX_NONE)
	{
		return FReply::Handled();
	}
	// The insert index, not the appended default: a filter lands inside its owner's subtree, which
	// is nowhere near the end of the stack.
	FinishGroupChildEdit(GroupId, InsertAt);
	return FReply::Handled();
}

FReply SMixtormat::AddFlowWarpToGroupChild(const FGuid GroupId, const int32 OwnerChildIndex)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group)
	{
		return FReply::Handled();
	}
	const int32 InsertAt =
		InsertScopedChild(Group->Children, OwnerChildIndex, MakeFlowWarpPrototype());
	if (InsertAt == INDEX_NONE)
	{
		return FReply::Handled();
	}
	FinishGroupChildEdit(GroupId, InsertAt);
	return FReply::Handled();
}

FReply SMixtormat::AddMaskToGroup(const FGuid GroupId, const FSoftObjectPath MaskPath)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	FMixtormatMaskLayer NewMask;
	if (!Group || !BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}
	// Replace, whatever is already on the stack. A new mask is added to be looked at, and
	// Multiply against an existing mask shows nothing wherever that mask is dark -- which reads
	// as the mask having failed to load rather than as two masks combining. The chain starts from
	// white, so Replace is what makes it visible on its own; combining is a deliberate second
	// step through the row's Blend Mode.
	//
	// Note this cuts deeper on a group than on a layer: a shared mask is appended after each
	// member's own masks, so Replace discards what those members had. That is the same rule the
	// row's Blend Mode exists to change, and it is consistent with every other way a mask arrives.
	NewMask.BlendMode = EMixtormatMaskBlendMode::Replace;
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
	if (!Effect || Effect->EffectType == EMixtormatEffectType::Peeling)
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
	// The database speaks per family; an asset child's family is the asset's type.
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(LayerEffect, Effect->EffectType);
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
	case EMixtormatLayerChildType::UvFromIds:   Child.UvId.bEnabled = !Child.UvId.bEnabled; break;
	case EMixtormatLayerChildType::ReliefFromIds: Child.ReliefId.bEnabled = !Child.ReliefId.bEnabled; break;
	case EMixtormatLayerChildType::PatternId:   Child.PatternId.bEnabled = !Child.PatternId.bEnabled; break;
	case EMixtormatLayerChildType::CombineId:   Child.CombineId.bEnabled = !Child.CombineId.bEnabled; break;
	case EMixtormatLayerChildType::Generator:   Child.Generator.bEnabled = !Child.Generator.bEnabled; break;
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
	case EMixtormatLayerChildType::UvFromIds:   return Child.UvId.bEnabled;
	case EMixtormatLayerChildType::ReliefFromIds: return Child.ReliefId.bEnabled;
	case EMixtormatLayerChildType::PatternId:   return Child.PatternId.bEnabled;
	case EMixtormatLayerChildType::CombineId:   return Child.CombineId.bEnabled;
	case EMixtormatLayerChildType::Generator:   return Child.Generator.bEnabled;
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

// A shared child's row. A child can arrive from a layer by dropping it on the group header
// (MoveChildToGroup, always appended), reorders among its top-level siblings the same way a layer
// child does (ReorderGroupChild), and leaves the group by being dropped on a layer or one of its
// child rows (MoveGroupChildToLayer). A scoped filter does none of the three -- it cannot leave
// the owner it filters -- and group-to-group is still not built, so a drag aimed at another
// group's header refuses rather than lighting up for a drop that would do nothing.
TSharedRef<SWidget> SMixtormat::BuildGroupChildRow(const FGuid GroupId, const int32 ChildIndex)
{
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !Group->Children.IsValidIndex(ChildIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FMixtormatLayerChild& Child = Group->Children[ChildIndex];
	const FText ChildName = GetLayerChildName(Child);
	// Doubles as "can leave the group" on the layer-side targets, so it has to match every guard
	// MoveGroupChildToLayer applies -- otherwise a row lights up for a release that is then
	// refused. A top-level mask filter is unreachable today (one only ever arrives scoped), but
	// the two ends are kept in step rather than relying on that.
	const bool bCanReorder = !Child.ScopeOwnerChildId.IsValid() && !IsMaskFilter(Child);

	return SNew(SMixtormatGroupChildDropTarget)
		.GroupId(GroupId)
		.ChildIndex(ChildIndex)
		.OnChildReordered(this, &SMixtormat::ReorderGroupChild)
		[
			SNew(SMixtormatLayerChildRow)
			.Name(ChildName)
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
			})
			// Decided here, not in the lambda: Child is a reference into an array the row outlives,
			// and the answer cannot change without the row being rebuilt anyway.
			.OnDragDetected_Lambda([this, GroupId, ChildIndex, ChildName, bCanReorder]
				(const FGeometry&, const FPointerEvent&)
			{
				return FReply::Handled().BeginDragDrop(
					FMixtormatChildDragDropOp::NewFromGroup(GroupId, ChildIndex, ChildName, bCanReorder));
			})
		];
}

TSharedRef<SWidget> SMixtormat::BuildGroupChildContextMenu(
	const FGuid GroupId,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	// Same rule the drag has: a scoped filter travels with the child it filters, never alone.
	const bool bCanLeaveGroup = Group
		&& Group->Children.IsValidIndex(ChildIndex)
		&& !Group->Children[ChildIndex].ScopeOwnerChildId.IsValid()
		&& !IsMaskFilter(Group->Children[ChildIndex]);
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		// INDEX_NONE for the layer lane throughout: ResolveChild reaches a group's shared stack
		// only that way, and the row selected this child before opening the menu, so
		// SelectedGroupId already names the group these resolve against.
		// Typed, not INDEX_NONE inline: the delegate payload would deduce the literal's own type
		// rather than the int32 the method takes.
		const int32 NoLayerLane = INDEX_NONE;
		Menu.SubMenu(
			LOCTEXT("MaskBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateSP(
				this, &SMixtormat::BuildMaskBlendModeMenu, NoLayerLane, ChildIndex));
		// The same entry a layer's mask has, and the only way to replace one: a grid inside a
		// context menu is not how a mask gets picked -- that is the gallery, or a drag from it.
		const FSoftObjectPath ReplacementPath = SelectedMaskPath;
		Menu.Item(
			FText::Format(
				LOCTEXT("ReplaceWithSelectedMask", "Replace with {0}"),
				SelectedLibraryMaskName.IsEmpty()
					? LOCTEXT("NoSelectedReplacementMask", "Select Mask from Gallery")
					: SelectedLibraryMaskName),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex, ReplacementPath]()
			{
				// ReplaceMaskInLayer resolves through ResolveChild, which reaches a group's shared
				// stack only when the layer lane is clear and SelectedGroupId names the group --
				// so the selection is moved onto this child first rather than assumed.
				SelectGroupChild(GroupId, ChildIndex);
				ReplaceMaskInLayer(INDEX_NONE, ChildIndex, ReplacementPath);
			}))
			.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));

		// A shared mask carries the same scoped children a layer's mask does. The group is
		// flattened into each member at compose time and ScopeOwnerChildId is rebound per member,
		// so one blur authored here gates this mask in every one of them.
		const bool bCanNestChild = CanAddScopedChild(Group->Children, ChildIndex);
		Menu.Item(
			LOCTEXT("AddBlurToMaskContext", "Add Blur"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddMaskFilterToGroupChild(GroupId, ChildIndex, EMixtormatLayerChildType::Blur);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Item(
			LOCTEXT("AddCurvatureToMaskContext", "Add Curvature"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddMaskFilterToGroupChild(GroupId, ChildIndex, EMixtormatLayerChildType::Curvature);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Item(
			LOCTEXT("AddFlowWarpToMask", "Add Flow Warp · Targets This Mask"),
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddFlowWarpToGroupChild(GroupId, ChildIndex);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Separator();
	}
	if (bCanLeaveGroup && !WorkingLayers.IsEmpty())
	{
		// "Move to Layer", not "Unshare": the destination has to be named, and there is no
		// sensible default for it -- the child belonged to every member equally.
		Menu.SubMenu(
			LOCTEXT("MoveGroupChildToLayerContext", "Move to Layer..."),
			nullptr,
			FOnGetContent::CreateSP(
				this, &SMixtormat::BuildMoveGroupChildToLayerMenu, GroupId, ChildIndex));
		Menu.Separator();
	}
	// Copy / Copy as Instance / Copy Output / Paste, and (for an instance) Go to Source / Break
	// Instance / Replace Source / Copy Instance Reference -- the same rows a layer child's menus
	// build via AddSharedChildMenuItems, driven by the same address-based clipboard rather than a
	// second, group-specific copy of this logic.
	AddSharedChildMenuItems(Menu, MakeGroupChildAddress(GroupId, ChildIndex));
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

TSharedRef<SWidget> SMixtormat::BuildMoveGroupChildToLayerMenu(
	const FGuid GroupId,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("MoveGroupChildToLayerCaption", "Move To"));
	// Every layer, including the group's own members: moving a shared child onto one member is
	// exactly the "this one only" case, and refusing it there would be the surprising answer.
	for (int32 DestIndex = 0; DestIndex < WorkingLayers.Num(); ++DestIndex)
	{
		Menu.Item(
			WorkingLayers[DestIndex].DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex, DestIndex]()
			{
				// INDEX_NONE: no row was aimed at, so it appends -- the same thing the menu
				// version of the layer-to-layer move does.
				MoveGroupChildToLayer(GroupId, ChildIndex, DestIndex, INDEX_NONE);
			}));
	}
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
	Menu.Item(
		LOCTEXT("AddPeelingEffect", "Peeling"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			CreateChild(FMixtormatAddTarget::Group(GroupId), EMixtormatChildCreation::Peeling);
		}));
	return Menu.Build();
}

// Not AddProceduralChildToGroup: that one only sets Type, and a generator needs its kind set as
// well before the child is of any use. AppendGroupChild hands back the child precisely so a
// creator that has more than one field to fill can fill it.
FReply SMixtormat::AddGeneratorToGroup(
	const FGuid GroupId,
	const EMixtormatGeneratorType GeneratorType)
{
	if (FMixtormatLayerChild* Child =
		AppendGroupChild(GroupId, EMixtormatLayerChildType::Generator))
	{
		Child->Generator.Type = GeneratorType;
		FinishGroupChildEdit(GroupId);
	}
	return FReply::Handled();
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
		.AccentColor_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group ? Group->AccentColor : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		})
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

FReply SMixtormat::SetLayerGroupAccentColor(const FGuid GroupId, const FLinearColor AccentColor)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group)
	{
		return FReply::Handled();
	}
	Group->AccentColor = AccentColor;
	// No RefreshLayeredPreview: the colour is editor organisation and the compositor never reads
	// it, so asking for a new composite would be work for nothing. It is still an edit worth
	// undoing and worth saving.
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RebuildLayerList();
	return FReply::Handled();
}

// A strip of swatches rather than a submenu of named colours or the engine's colour dialog: it is
// one click from the menu that opened it, and a colour is a thing you point at, not a thing you
// read the name of. The leftmost clears back to no colour.
TSharedRef<SWidget> SMixtormat::BuildGroupAccentMenu(const FGuid GroupId)
{
	const TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	const auto AddSwatch =
		[this, &Strip, GroupId](const FLinearColor& Swatch, const FText& ToolTip)
	{
		Strip->AddSlot()
		.AutoWidth()
		.Padding(MixtormatTokens::GroupAccentSwatchGap * 0.5f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::GroupAccentSwatchSize)
			.HeightOverride(MixtormatTokens::GroupAccentSwatchSize)
			.ToolTipText(ToolTip)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
				.ContentPadding(0.0f)
				.OnClicked_Lambda([this, GroupId, Swatch]()
				{
					FSlateApplication::Get().DismissAllMenus();
					return SetLayerGroupAccentColor(GroupId, Swatch);
				})
				[
					SNew(SColorBlock)
					.Color(Swatch.A > 0.0f ? Swatch : MixtormatPalette::RaisedPanel())
					.ShowBackgroundForAlpha(false)
					.Size(FVector2D(
						MixtormatTokens::GroupAccentSwatchSize,
						MixtormatTokens::GroupAccentSwatchSize))
				]
			]
		];
	};

	AddSwatch(
		FLinearColor(0.0f, 0.0f, 0.0f, 0.0f),
		LOCTEXT("GroupAccentNone", "No colour"));
	for (const FLinearColor& Swatch : MixtormatPalette::GroupAccents())
	{
		AddSwatch(Swatch, LOCTEXT("GroupAccentSwatch", "Tag this group with this colour"));
	}

	MixtormatMenu::FBuilder Menu;
	Menu.Widget(SNew(SBox).Padding(MixtormatTokens::GroupAccentSwatchGap)[Strip]);
	return Menu.Build();
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
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAddEffectMenu, GroupId));
	// The same four submenus the layer menu builds, from the same four functions. A shared child
	// works here for the reason every other one does: BuildEffectiveLayers appends a remapped copy
	// of the group's stack to each member before composition, so one node authored here runs over
	// every member's own input with that member's own scoped masks and region IDs rebound to it.
	AddCreationSections(Menu, FMixtormatAddTarget::Group(GroupId));

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
	Menu.SubMenu(
		LOCTEXT("GroupAccentContext", "Colour"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAccentMenu, GroupId));
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
			|| Child.Type == EMixtormatLayerChildType::UvFromIds
			|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
			|| Child.Type == EMixtormatLayerChildType::PatternId
			|| Child.Type == EMixtormatLayerChildType::CombineId
			// A generator joins them for the row, not for the semantics. What the shared
			// procedural row gives it is the right ones: an enable toggle that writes its own
			// flag, a context menu with no blend mode on it, and the shared duplicate/instance
			// items. It is not a mask and must not fall through to the mask row, which would
			// toggle FMixtormatLayerChild::Mask on a child that has none.
			|| Child.Type == EMixtormatLayerChildType::Generator;
		const FText ChildName = GetLayerChildName(Child);

		Container->AddChild(
			SNew(SBox)
			.Padding(FMargin(
				GetDisplayScopeDepth(Layer.Children, ChildIndex) * MixtormatTokens::LayerScopeIndent,
				0.0f,
				0.0f,
				0.0f))
			[
			SNew(SMixtormatChildDropTarget)
			.LayerIndex(LayerIndex)
			.ChildIndex(ChildIndex)
			.OnChildReordered(this, &SMixtormat::ReorderLayerChild)
			.OnChildMovedToLayer(this, &SMixtormat::MoveChildToLayer)
			.OnGroupChildMovedToLayer(this, &SMixtormat::MoveGroupChildToLayer)
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
		.OnGroupChildMovedToLayer(this, &SMixtormat::MoveGroupChildToLayer)
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
	case EMixtormatLayerChildType::UvFromIds: return Child.UvId.bEnabled;
	case EMixtormatLayerChildType::ReliefFromIds: return Child.ReliefId.bEnabled;
	case EMixtormatLayerChildType::PatternId: return Child.PatternId.bEnabled;
	case EMixtormatLayerChildType::CombineId: return Child.CombineId.bEnabled;
	case EMixtormatLayerChildType::Blur:      return Child.Blur.bEnabled;
	case EMixtormatLayerChildType::Curvature: return Child.Curvature.bEnabled;
	case EMixtormatLayerChildType::Generator: return Child.Generator.bEnabled;
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
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddEffectMenu, LayerIndex));
	// IDs / Filter / Masks / Generators, built by the same four functions the group menu calls.
	AddCreationSections(Menu, FMixtormatAddTarget::Layer(LayerIndex));

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

	// A plain copy lands at the end of the layer; an instance lands at the top, except that a
	// source in this same layer pushes it to the first slot below that source. One Paste row for
	// both -- the clipboard's own Mode decides which -- stays visible and disabled when nothing
	// works.
	Menu.Item(
		LOCTEXT("PasteChildContext", "Paste"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			if (WorkingLayers.IsValidIndex(LayerIndex))
			{
				PasteChild({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()});
			}
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex)
				&& CanPasteChild({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()}, INDEX_NONE);
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

bool SMixtormat::CanCreateChild(const FMixtormatAddTarget& Target) const
{
	return Target.IsGroup()
		? MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Target.GroupId) != nullptr
		: WorkingLayers.IsValidIndex(Target.LayerIndex);
}

FReply SMixtormat::CreateChild(const FMixtormatAddTarget Target, const EMixtormatChildCreation Kind)
{
	if (Target.IsGroup())
	{
		// AppendGroupChild takes a type and hands the child back precisely so a creator with more
		// than one field to fill can fill it, which is what ApplyChildCreationDefaults does here.
		if (FMixtormatLayerChild* Child =
			AppendGroupChild(Target.GroupId, ChildTypeForCreation(Kind)))
		{
			ApplyChildCreationDefaults(*Child, Kind);
			ApplyLinkDefaults(*Child, Target.GroupId);
			FinishGroupChildEdit(Target.GroupId);
		}
		return FReply::Handled();
	}

	if (!WorkingLayers.IsValidIndex(Target.LayerIndex))
	{
		return FReply::Handled();
	}
	// Note the asymmetry with the group branch above, which is pre-existing rather than a choice
	// made here: FinishGroupChildEdit records edit history and marks the document dirty, and no
	// Add*ToLayer creator ever has. Creating a layer child is therefore still not undoable, the
	// same as before this function collapsed the ten of them into one. Left alone deliberately --
	// changing it changes the undo stack, which is not this refactor's to move.
	FMixtormatLayer& Layer = WorkingLayers[Target.LayerIndex];
	FMixtormatLayerChild& CreatedChild = Layer.Children.AddDefaulted_GetRef();
	ApplyChildCreationDefaults(CreatedChild, Kind);
	ApplyLinkDefaults(CreatedChild, Layer.LayerId);
	SetLayerExpanded(Target.LayerIndex, true);
	SelectWorkingChild(Target.LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AddTextureMask(const FMixtormatAddTarget Target, const FSoftObjectPath MaskPath)
{
	// Straight through to the two creators the gallery's drag-and-drop already calls, rather than
	// a third way of building a mask child. "Texture Mask..." in the menu and a mask dragged out
	// of the gallery have to produce the same node, and the cheapest guarantee of that is that
	// they are the same call.
	return Target.IsGroup()
		? AddMaskToGroup(Target.GroupId, MaskPath)
		: AssignMaskToLayer(Target.LayerIndex, MaskPath);
}

// The Add tree, authored once.
//
// IDs create or modify Region IDs; Filters consume them and transform something else; Masks make
// coverage; Generators write structural height. The categories are the plugin's own taxonomy and
// the submenu each node sits in is the claim the inspector and the compositor make about it -- an
// ID node buried under Filter said the opposite of what it does.
void SMixtormat::AddCreationSections(MixtormatMenu::FBuilder& Menu, const FMixtormatAddTarget Target)
{
	Menu.SubMenu(
		LOCTEXT("AddIdsChild", "IDs"),
		MixtormatIcons::Generated(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddIdsMenu, Target));
	Menu.SubMenu(
		LOCTEXT("AddFilterChild", "Filter"),
		MixtormatIcons::Generated(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFiltersMenu, Target));
	Menu.SubMenu(
		LOCTEXT("AddMasksChild", "Masks"),
		MixtormatIcons::Mask(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Target));
	// Its own category beside Effect and Filter, not inside either. What separates it is *when*
	// it runs: an effect filters the layer after it has composited, a generator rewrites the
	// height the layer composites from. Filing it under Effect would be the first step toward
	// implementing it as one.
	Menu.SubMenu(
		LOCTEXT("AddGeneratorChild", "Generators"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddGeneratorsMenu, Target));
}

TSharedRef<SWidget> SMixtormat::BuildAddIdsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddPatternIdChild", "Pattern IDs"), EMixtormatChildCreation::PatternIds },
		{ LOCTEXT("AddClusterFilterChild", "Cluster IDs"), EMixtormatChildCreation::ClusterIds },
		{ LOCTEXT("AddCombineIdChild", "Combine IDs"), EMixtormatChildCreation::CombineIds },
	};
	const bool bEnabled = CanCreateChild(Target);
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Generated(),
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]()
			{
				CreateChild(Target, Kind);
			}))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddFiltersMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddHsvFilterChild", "HSV From IDs"), EMixtormatChildCreation::HsvFromIds },
		{ LOCTEXT("AddRampIdChild", "Ramp From IDs"), EMixtormatChildCreation::RampFromIds },
		// The two halves Pattern IDs used to carry itself. Beside Ramp rather than under IDs,
		// because they consume Region IDs and change something else -- which is what a Filter is.
		{ LOCTEXT("AddUvIdChild", "UV From IDs"), EMixtormatChildCreation::UvFromIds },
		{ LOCTEXT("AddReliefIdChild", "Relief From IDs"), EMixtormatChildCreation::ReliefFromIds },
	};
	const bool bEnabled = CanCreateChild(Target);
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Generated(),
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]()
			{
				CreateChild(Target, Kind);
			}))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddMasksMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const bool bEnabled = CanCreateChild(Target);

	// Texture Mask first, and it takes whatever the gallery has selected. The ellipsis is the
	// honest part of the label: unlike everything below it, this one cannot create anything until
	// a mask has been picked downstairs.
	const FSoftObjectPath MaskPath = SelectedMaskPath;
	Menu.Item(
		FText::Format(
			LOCTEXT("AddTextureMaskChild", "Texture Mask · {0}"),
			SelectedLibraryMaskName.IsEmpty()
				? LOCTEXT("NoSelectedLibraryMask", "Select Mask from Gallery")
				: SelectedLibraryMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, Target, MaskPath]()
		{
			AddTextureMask(Target, MaskPath);
		}))
		.Enabled(TAttribute<bool>(bEnabled && !MaskPath.IsNull()));

	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		// Beside the other mask producers rather than under the asset picker above, because it
		// needs no asset: it reads the layer it is added to.
		{ LOCTEXT("AddLayerValuesChild", "Layer Values Mask"), EMixtormatChildCreation::LayerValuesMask },
		{ LOCTEXT("AddGeneratedChild", "Generated Mask"), EMixtormatChildCreation::GeneratedMask },
		{ LOCTEXT("AddColorIdChild", "Color ID Mask"), EMixtormatChildCreation::ColorIdMask },
		// Listed with the mask producers rather than under Filter, because that is what it is: it
		// emits 0..1 coverage and blends like any other mask. Only what it reads is unusual.
		{ LOCTEXT("AddRandomIdChild", "Random From IDs"), EMixtormatChildCreation::RandomFromIds },
	};
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]()
			{
				CreateChild(Target, Kind);
			}))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddGeneratorsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("AddStrataCarverChild", "Strata Carver"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			CreateChild(Target, EMixtormatChildCreation::StrataCarver);
		}))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
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
		LOCTEXT("AddBreakupEffect", "Breakup"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddBreakupToLayer(LayerIndex); }));
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
		LOCTEXT("AddPeelingEffect", "Peeling"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			CreateChild(FMixtormatAddTarget::Layer(LayerIndex), EMixtormatChildCreation::Peeling);
		}));

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildEffectContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	bool bCanNestChild = false;
	bool bCanOwnFlowWarp = false;
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& SourceChild = *ResolveChild(LayerIndex, ChildIndex);
		bCanNestChild = CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex);
		bCanOwnFlowWarp = CanOwnFlowWarp(SourceChild);
	}
	// Copy Instance Mask from Wear/Gap/Edge/Pieces used to be hardcoded here per effect type; it is
	// now the generic Copy Output section AddSharedChildMenuItems builds below from
	// GetChildCapabilities, driven by bCopyableAsMask rather than a bWornEdges/bBreakup switch.

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
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
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
	// A generator has no blend mode either, and for a stronger reason than a filter does: a
	// filter at least emits something into the layer, whereas a generator rewrites the height
	// the layer is built from before any blending exists to take part in.
	const bool bGenerator = RowType == EMixtormatLayerChildType::Generator;
	const bool bFilter = RowType == EMixtormatLayerChildType::Filter
		|| RowType == EMixtormatLayerChildType::HsvFilter
		|| RowType == EMixtormatLayerChildType::RampId
		|| RowType == EMixtormatLayerChildType::UvFromIds
		|| RowType == EMixtormatLayerChildType::ReliefFromIds
		|| RowType == EMixtormatLayerChildType::PatternId
		|| RowType == EMixtormatLayerChildType::CombineId
		|| bGenerator;

	// The scoped mask, which is the whole of Mask Influence. Offered here rather than in the
	// effect menu because a generator uses the procedural row -- but it is the same
	// AssignScopedMaskToChild an effect calls, validated through the same CanOwnScopedMasks, so
	// a mask dragged onto a generator and a mask added from this menu land identically.
	if (bGenerator)
	{
		const bool bCanNestChild = WorkingLayers.IsValidIndex(LayerIndex)
			&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex);
		const FSoftObjectPath SelectedGeneratorMaskPath = SelectedMaskPath;
		const FText SelectedGeneratorMaskName = SelectedLibraryMaskName.IsEmpty()
			? LOCTEXT("NoSelectedGeneratorMask", "Select Mask from Gallery")
			: SelectedLibraryMaskName;
		Menu.Item(
			FText::Format(
				LOCTEXT("AddSelectedMaskToGenerator", "Add Gating Mask · {0}"),
				SelectedGeneratorMaskName),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda(
				[this, LayerIndex, ChildIndex, SelectedGeneratorMaskPath]()
			{
				AssignScopedMaskToChild(LayerIndex, ChildIndex, SelectedGeneratorMaskPath);
			}))
			.Enabled(TAttribute<bool>(!SelectedGeneratorMaskPath.IsNull() && bCanNestChild));
		Menu.Separator();
	}

	// Copy Instance Mask from Gap used to be hardcoded here for a PatternId row; it is now the
	// generic Copy Output section AddSharedChildMenuItems builds below (Pattern IDs' Gap is
	// bCopyableAsMask but deliberately not bPreviewable -- see MixtormatChildCapabilities.h).

	if (!bFilter)
	{
		Menu.SubMenu(
			LOCTEXT("GeneratedBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildGeneratedBlendModeMenu, LayerIndex, ChildIndex));
		Menu.Separator();
	}
	// Merge against Subtract is the one thing about a combiner worth switching without going to
	// the inspector first, because the two read so differently on the same map.
	if (RowType == EMixtormatLayerChildType::CombineId)
	{
		Menu.SubMenu(
			LOCTEXT("CombineModeContext", "Combine Mode"),
			nullptr,
			FOnGetContent::CreateSP(
				this, &SMixtormat::BuildCombineIdModeMenuFor, LayerIndex, ChildIndex));
		Menu.Separator();
	}
	Menu.Item(
		LOCTEXT("DuplicateGeneratedChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
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
		case EMixtormatLayerChildType::UvFromIds:
			RemoveLabel = LOCTEXT("RemoveUvIdChild", "Remove UV From IDs");
			break;
		case EMixtormatLayerChildType::ReliefFromIds:
			RemoveLabel = LOCTEXT("RemoveReliefIdChild", "Remove Relief From IDs");
			break;
		case EMixtormatLayerChildType::PatternId:
			RemoveLabel = LOCTEXT("RemovePatternIdChild", "Remove Pattern IDs");
			break;
		case EMixtormatLayerChildType::CombineId:
			RemoveLabel = LOCTEXT("RemoveCombineIdChild", "Remove Combine IDs");
			break;
		case EMixtormatLayerChildType::Generator:
			RemoveLabel = LOCTEXT("RemoveGeneratorChild", "Remove Generator");
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
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
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
		&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, MaskIndex);
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
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, MaskIndex));
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
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::ColorIdMask);
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
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::ClusterIds);
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

FReply SMixtormat::AddHsvFilterToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::HsvFromIds);
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
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::PatternIds);
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

// A generator, added to the end of the layer's chain like any other child.
//
// Where it lands in the row order matters less than it does for a mask or an effect: every
// generator on a layer runs together, before the child loop, in authored order among themselves.
// What the row position does decide is which Region ID map it sees -- FindRegionIdsAbove takes
// the nearest producer above it -- and which masks are scoped beneath it.
FReply SMixtormat::AddStrataCarverToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::StrataCarver);
}

FMixtormatStrataCarver* SMixtormat::GetSelectedStrataCarver()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generator
		&& Child.Generator.Type == EMixtormatGeneratorType::StrataCarver
		? &Child.Generator.StrataCarver
		: nullptr;
}

const FMixtormatStrataCarver* SMixtormat::GetSelectedStrataCarver() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generator
		&& Child.Generator.Type == EMixtormatGeneratorType::StrataCarver
		? &Child.Generator.StrataCarver
		: nullptr;
}

// True for any generator, whatever kind. The inspector's two visibility lists ask this rather
// than each generator's own getter, so adding a generator does not mean remembering to extend
// two lambdas that fail silently -- a missed entry shows the new panel *and* the layer's own
// sections at the same time, which is what a forgotten one looks like.
bool SMixtormat::HasSelectedGenerator() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::Generator;
}

FMixtormatGenerator* SMixtormat::GetSelectedGenerator()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generator ? &Child.Generator : nullptr;
}

FReply SMixtormat::AddCombineIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::CombineIds);
}

FMixtormatCombineIdFilter* SMixtormat::GetSelectedCombineId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::CombineId ? &Child.CombineId : nullptr;
}

const FMixtormatCombineIdFilter* SMixtormat::GetSelectedCombineId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::CombineId ? &Child.CombineId : nullptr;
}

FReply SMixtormat::AddRampIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::RampFromIds);
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

FMixtormatUvIdFilter* SMixtormat::GetSelectedUvId()
{
	FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::UvFromIds ? &Child->UvId : nullptr;
}

const FMixtormatUvIdFilter* SMixtormat::GetSelectedUvId() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::UvFromIds ? &Child->UvId : nullptr;
}

FMixtormatReliefIdFilter* SMixtormat::GetSelectedReliefId()
{
	FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::ReliefFromIds ? &Child->ReliefId : nullptr;
}

const FMixtormatReliefIdFilter* SMixtormat::GetSelectedReliefId() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::ReliefFromIds ? &Child->ReliefId : nullptr;
}

FReply SMixtormat::AddRandomIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::RandomFromIds);
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
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::LayerValuesMask);
}

FReply SMixtormat::AddGeneratedMaskToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::GeneratedMask);
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
		&& ChildType != EMixtormatLayerChildType::UvFromIds
		&& ChildType != EMixtormatLayerChildType::ReliefFromIds
		&& ChildType != EMixtormatLayerChildType::PatternId
		&& ChildType != EMixtormatLayerChildType::CombineId
		&& ChildType != EMixtormatLayerChildType::Generator)
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
	const int32 SubtreeEnd = FindSubtreeEnd(Layer.Children, ChildIndex);
	Layer.Children.RemoveAt(ChildIndex, SubtreeEnd - ChildIndex);
	if (SelectedLayerIndex == LayerIndex)
	{
		SelectedEffectIndex = FindChildById(Layer.Children, SelectedEffectId);
		SelectedMaskIndex = FindChildById(Layer.Children, SelectedMaskId);
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
	case EMixtormatLayerChildType::UvFromIds:
		Child.UvId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::ReliefFromIds:
		Child.ReliefId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::PatternId:
		Child.PatternId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::CombineId:
		Child.CombineId.bEnabled = bEnabled;
		break;
	case EMixtormatLayerChildType::Generator:
		// The wrapper's flag. The kind chooses a payload; whether the node runs at all is one
		// question for the whole category, and the gather branch asks it before it looks at
		// the kind.
		Child.Generator.bEnabled = bEnabled;
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

FReply SMixtormat::AddBreakupToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::Breakup;
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

FMixtormatLayerEffect* SMixtormat::GetSelectedBreakup()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Breakup)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedBreakup() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Breakup)
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
			|| !CanAddScopedChild(Layer.Children, OwnerChildIndex)))
	{
		return FReply::Handled();
	}

	// Unscoped, a Flow Warp is an ordinary top-level effect; scoped, it targets the mask above it
	// and goes inside that mask's subtree like any other filter.
	int32 InsertAt = Layer.Children.Num();
	if (bScoped)
	{
		InsertAt = InsertScopedChild(Layer.Children, OwnerChildIndex, MakeFlowWarpPrototype());
		if (InsertAt == INDEX_NONE)
		{
			return FReply::Handled();
		}
	}
	else
	{
		Layer.Children.Insert(MakeFlowWarpPrototype(), InsertAt);
	}
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
	ApplyLinkDefaults(Child, Layer.LayerId);
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
