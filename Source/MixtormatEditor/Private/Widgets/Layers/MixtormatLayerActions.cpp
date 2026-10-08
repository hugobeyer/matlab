// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Parameters/MixtormatParameterAuthoring.h"
#include "Services/MixtormatPaths.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

void SMixtormat::InitializeNewLayer(
	FMixtormatLayer& Layer, const EMixtormatLayerType LayerType, const int32 LayerNumber) const
{
	Layer.Type = LayerType;
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
		// Mid-dark neutral rather than white, so a new fill reads as a surface under the lights.
		Layer.BaseColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f);
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		// A fill is its own surface: its normal replaces the one below rather than reorienting onto it.
		Layer.NormalBlendMode = EMixtormatNormalBlendMode::Override;
		// And its height replaces what is below, rather than only rising above it.
		Layer.HeightBlend.Op = EMixtormatHeightOp::Replace;
		break;
	case EMixtormatLayerType::Generator:
	{
		int32 GeneratorNumber = 1;
		FText DefaultName;
		do
		{
			DefaultName = FText::Format(LOCTEXT("GeneratorLayerNumber", "Generator Layer {0}"),
				FText::AsNumber(GeneratorNumber++));
		}
		while (WorkingLayers.ContainsByPredicate([&Layer, &DefaultName](const FMixtormatLayer& Existing)
		{
			return &Existing != &Layer && Existing.DisplayName.EqualTo(DefaultName);
		}));
		Layer.DisplayName = DefaultName;
		// A generator replaces the height below it rather than only rising above it.
		Layer.HeightBlend.Op = EMixtormatHeightOp::Replace;
		break;
	}
	}
}

FReply SMixtormat::AddGeneratorLayer(const EMixtormatGeneratorType Type)
{
	AddLayerOrStartMaterial(EMixtormatLayerType::Generator);
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| WorkingLayers[SelectedLayerIndex].Type != EMixtormatLayerType::Generator)
	{
		return FReply::Handled();
	}

	// The first module is the layer's first child; CreateChild selects it and refreshes the stack.
	return CreateChild(FMixtormatAddTarget::Layer(SelectedLayerIndex), CreationKindForGenerator(Type));
}

FReply SMixtormat::AddLayerOrStartMaterial(const EMixtormatLayerType LayerType)
{
	return bHasWorkingMaterial ? AddWorkingLayer(LayerType) : StartNewMaterialWith(LayerType);
}

FReply SMixtormat::AddWorkingLayer(const EMixtormatLayerType LayerType)
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}
	if (LayerType == EMixtormatLayerType::Material && SelectedSurfacePath.IsNull())
	{
		WorkingStatusText = TEXT("Select a library surface first");
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers.AddDefaulted_GetRef();
	InitializeNewLayer(Layer, LayerType, WorkingLayers.Num());

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
	bool bDefaultGeneratorName = false;
	if (Copy.Type == EMixtormatLayerType::Generator)
	{
		for (int32 Number = 1; Number <= WorkingLayers.Num(); ++Number)
		{
			if (Copy.DisplayName.EqualTo(FText::Format(
				LOCTEXT("GeneratorLayerNumber", "Generator Layer {0}"), FText::AsNumber(Number))))
			{
				bDefaultGeneratorName = true;
				break;
			}
		}
	}
	if (bDefaultGeneratorName)
	{
		InitializeNewLayer(Copy, EMixtormatLayerType::Generator, WorkingLayers.Num() + 1);
	}
	else
	{
		Copy.DisplayName = FText::Format(
			LOCTEXT("CopiedLayerName", "{0} Copy"), Copy.DisplayName);
	}
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

bool SMixtormat::CanCreateGroup() const
{
	// Always available with a material open: with a selection it groups those layers, without one
	// it creates an empty group to drop layers into.
	return bHasWorkingMaterial;
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
		// An empty group. It has no members yet, so it has no position in the stack and the list
		// renders it after it; the header is the drop target that gives it its first member.
		FMixtormatLayerGroup& Group = WorkingLayerGroups.AddDefaulted_GetRef();
		Group.DisplayName = MakeUniqueGroupName();
		const FGuid NewGroupId = Group.GroupId;

		// The same selection state clicking the header produces: the group is the subject, no
		// shared child, and the layer-child selection is dropped rather than left looking selected.
		SelectLayerGroup(NewGroupId);
		CollapsedGroupIds.Remove(NewGroupId);

		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		WorkingStatusText = TEXT("Created an empty group");
		SyncSelectedLayerControls();
		RebuildLayerList();
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

	TArray<FMixtormatLayer> ProposedLayers = WorkingLayers;
	TArray<FMixtormatLayerGroup> ProposedGroups = WorkingLayerGroups;
	const int32 DroppedReferences =
		MixtormatUI::ReorderLayersByPermutation(ProposedLayers, NewOrder);

	FMixtormatLayerGroup& Group = ProposedGroups.AddDefaulted_GetRef();
	Group.DisplayName = MakeUniqueGroupName();
	for (int32 Index = InsertAt; Index < InsertAt + Selected.Num(); ++Index)
	{
		ProposedLayers[Index].GroupId = Group.GroupId;
	}
	const FGuid NewGroupId = Group.GroupId;

	// Taking layers out of another group can leave what remains of it split around the new block.
	// ValidateGroups repairs that the same way it repairs a corrupted asset -- by ungrouping the
	// strays rather than moving layers again -- so count what it took before letting it run.
	int32 StrandedCount = 0;
	{
		TArray<FGuid> MembershipBefore;
		MembershipBefore.Reserve(ProposedLayers.Num());
		for (const FMixtormatLayer& Layer : ProposedLayers)
		{
			MembershipBefore.Add(Layer.GroupId);
		}
		MixtormatLayerGroups::ValidateGroups(ProposedLayers, ProposedGroups);
		for (int32 Index = 0; Index < ProposedLayers.Num(); ++Index)
		{
			if (MembershipBefore[Index].IsValid() && !ProposedLayers[Index].GroupId.IsValid())
			{
				++StrandedCount;
			}
		}
	}
	FText MoveReason;
	if (!StructuralLinksPreserved(ProposedLayers, ProposedGroups, MoveReason))
	{
		WorkingStatusText = MoveReason.ToString();
		return FReply::Handled();
	}
	WorkingLayers = MoveTemp(ProposedLayers);
	WorkingLayerGroups = MoveTemp(ProposedGroups);

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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(WorkingLayers[LayerIndex].Children[InsertAt]);
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
	if (Layer.Type != EMixtormatLayerType::Material)
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
		Replacement.TilingX = FMath::Max(FMath::RoundToInt(Mask->DefaultTiling), 1);
		Replacement.TilingY = Replacement.TilingX;
		// Offset is deliberately not carried here, unlike when a mask is first added: replacing
		// the picture under an existing mask keeps the offset the user dialled against it.
		Replacement.Shaping.Balance = Mask->DefaultBalance;
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

	Copies = CopyChildSubtree(MoveTemp(Copies), Layer.LayerId, Layer.LayerId);

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

FReply SMixtormat::AddIdGroupSource(
	const FMixtormatChildAddress& Source, const FMixtormatChildAddress& Dest)
{
	if (!CanAddIdGroupSource(Source, Dest)) { return FReply::Unhandled(); }
	FMixtormatLayerChild Reference;
	MakeRegionIdsReference(*ResolveChildAt(Source), Source, Reference);
	TArray<FMixtormatLayerChild>* Children = ResolveContainer(Dest);
	const int32 InsertAt = InsertScopedChild(*Children, ResolveChildIndexAt(Dest), MoveTemp(Reference));
	if (InsertAt == INDEX_NONE) { return FReply::Unhandled(); }
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
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::GoToChildInstanceSource(const FMixtormatChildAddress& Address)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return FReply::Unhandled();
	}
	const bool bOutputReference = Child->Type == EMixtormatLayerChildType::OutputReference && !Child->IsInstance();
	const FGuid SourceLayerId = bOutputReference ? Child->OutputReference.SourceLayerId : Child->SourceLayerId;
	const FGuid SourceChildId = bOutputReference ? Child->OutputReference.SourceChildId : Child->SourceChildId;
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
	const FMixtormatLayerChild* NewSourceChild = MixtormatParameterBinding::FindChild(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, NewSource.OwnerId, NewSource.ChildId);
	if (NewSourceChild && IsGeneratorFlow(*NewSourceChild) && !Placement->ScopeOwnerChildId.IsValid())
	{
		return FReply::Unhandled();
	}
	if (Placement->ScopeOwnerChildId.IsValid())
	{
		const int32 OwnerIndex = FindChildById(*Container, Placement->ScopeOwnerChildId);
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
	if (!Effect || Effect->EffectType == EMixtormatEffectType::Peeling
		|| MixtormatIsGeneratorFlowEffect(Effect->EffectType))
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Group->Children[InsertAt]);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Group->Children[InsertAt]);
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
		MixtormatParameterAuthoring::ApplyAuthoringDefaults(*Child);
		FinishGroupChildEdit(GroupId);
	}
	return FReply::Handled();
}

FReply SMixtormat::AddEffectToGroup(const FGuid GroupId, const FSoftObjectPath EffectPath)
{
	const UMixtormatEffect* Effect = Cast<UMixtormatEffect>(EffectPath.TryLoad());
	if (!Effect || Effect->EffectType == EMixtormatEffectType::Peeling
		|| MixtormatIsGeneratorFlowEffect(Effect->EffectType))
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(*Child);
	FinishGroupChildEdit(GroupId);
	return FReply::Handled();
}

FReply SMixtormat::AddProceduralChildToGroup(
	const FGuid GroupId,
	const EMixtormatLayerChildType ChildType)
{
	if (FMixtormatLayerChild* Child = AppendGroupChild(GroupId, ChildType))
	{
		MixtormatParameterAuthoring::ApplyAuthoringDefaults(*Child);
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
	const FGuid SelectedChildId = SelectedGroupId == GroupId
		&& Group->Children.IsValidIndex(SelectedGroupChildIndex)
		? Group->Children[SelectedGroupChildIndex].ChildId : FGuid();
	// Anything scoped beneath this child goes with it, the same as removing a layer child: a
	// blur whose owner has left gates nothing.
	if (Group->Children[ChildIndex].Type == EMixtormatLayerChildType::IdGroup)
	{
		Group->Children.RemoveAt(ChildIndex, FindSubtreeEnd(Group->Children, ChildIndex) - ChildIndex);
	}
	else
	{
		const FGuid RemovedId = Group->Children[ChildIndex].ChildId;
		Group->Children.RemoveAll([&RemovedId](const FMixtormatLayerChild& Candidate)
		{
			return Candidate.ChildId == RemovedId || Candidate.ScopeOwnerChildId == RemovedId;
		});
	}
	if (SelectedGroupId == GroupId)
	{
		SelectedGroupChildIndex = FindChildById(Group->Children, SelectedChildId);
	}
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
	MixtormatLayersPrivate::SetChildEnabled(Child, !MixtormatLayersPrivate::IsChildEnabled(Child));
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
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
		MixtormatParameterAuthoring::ApplyAuthoringDefaults(*Child);
		FinishGroupChildEdit(GroupId);
	}
	return FReply::Handled();
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

FReply SMixtormat::CreateChild(const FMixtormatAddTarget Target, const EMixtormatChildCreation Kind)
{
	// Cluster IDs remains unavailable.
	if (Kind == EMixtormatChildCreation::ClusterIds)
	{
		return FReply::Handled();
	}

	if (!CanCreateChild(Target))
	{
		return FReply::Handled();
	}
	const EMixtormatLayerChildType CreatedType = ChildTypeForCreation(Kind);
	if ((CreatedType == EMixtormatLayerChildType::Generator
		|| CreatedType == EMixtormatLayerChildType::HeightBlend
		|| CreatedType == EMixtormatLayerChildType::HeightCurve
		|| CreatedType == EMixtormatLayerChildType::HeightColorRamp
		|| CreatedType == EMixtormatLayerChildType::HeightPush
		|| CreatedType == EMixtormatLayerChildType::StructuralWarp)
		&& !CanAddGeneratorModule(Target))
	{
		return FReply::Handled();
	}
	if (Target.ScopeOwnerChildId.IsValid())
	{
		const FMixtormatChildAddress Owner = Target.IsGroup()
			? FMixtormatChildAddress{EMixtormatChildOwnerType::Group, Target.GroupId, Target.ScopeOwnerChildId}
			: FMixtormatChildAddress{EMixtormatChildOwnerType::Layer,
				WorkingLayers[Target.LayerIndex].LayerId, Target.ScopeOwnerChildId};
		TArray<FMixtormatLayerChild>* Children = ResolveContainer(Owner);
		FMixtormatLayerChild Child;
		ApplyChildCreationDefaults(Child, Kind);
		const int32 CreatedIndex = InsertScopedChild(*Children, ResolveChildIndexAt(Owner), MoveTemp(Child));
		if (CreatedIndex == INDEX_NONE)
		{
			return FReply::Handled();
		}
		ApplyLinkDefaults((*Children)[CreatedIndex], Owner.OwnerId);
		if (Target.IsGroup())
		{
			FinishGroupChildEdit(Target.GroupId, CreatedIndex);
		}
		else
		{
			SetLayerExpanded(Target.LayerIndex, true);
			SelectWorkingChild(Target.LayerIndex, CreatedIndex);
			RefreshLayeredPreview();
			RebuildLayerList();
		}
		return FReply::Handled();
	}

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
	const int32 CreatedIndex = Layer.Children.AddDefaulted();
	ApplyChildCreationDefaults(Layer.Children[CreatedIndex], Kind);
	ApplyLinkDefaults(Layer.Children[CreatedIndex], Layer.LayerId);
	SetLayerExpanded(Target.LayerIndex, true);
	SelectWorkingChild(Target.LayerIndex, CreatedIndex);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

bool SMixtormat::PrepareStructuralModuleForTarget(const FGuid TargetLayerId, const FGuid TargetChildId,
	const EMixtormatLayerChildType ModuleType, TArray<FMixtormatLayer>& ProposedLayers,
	int32& LayerIndex, int32& InsertIndex, FText& OutReason) const
{
	OutReason = LOCTEXT("StructuralCreationUnavailable", "Requires an enabled, unscoped generator target in a Generator layer");
	if (!bHasWorkingMaterial || !TargetLayerId.IsValid() || !TargetChildId.IsValid()
		|| (ModuleType != EMixtormatLayerChildType::HeightPush
			&& ModuleType != EMixtormatLayerChildType::StructuralWarp))
	{
		return false;
	}
	LayerIndex = INDEX_NONE;
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (WorkingLayers[Index].LayerId != TargetLayerId) { continue; }
		if (LayerIndex != INDEX_NONE)
		{
			OutReason = LOCTEXT("StructuralCreationDuplicateLayer", "Target layer identity is ambiguous");
			return false;
		}
		LayerIndex = Index;
	}
	if (!WorkingLayers.IsValidIndex(LayerIndex)) { return false; }
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.Type != EMixtormatLayerType::Generator || !Layer.bEnabled) { return false; }
	InsertIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
	{
		if (Layer.Children[Index].ChildId != TargetChildId) { continue; }
		if (InsertIndex != INDEX_NONE)
		{
			OutReason = LOCTEXT("StructuralCreationDuplicateTarget", "Target child identity is ambiguous");
			return false;
		}
		InsertIndex = Index;
	}
	if (!Layer.Children.IsValidIndex(InsertIndex)) { return false; }
	const FMixtormatLayerChild& Target = Layer.Children[InsertIndex];
	if (Target.Type != EMixtormatLayerChildType::Generator || Target.IsInstance()
		|| Target.ScopeOwnerChildId.IsValid() || !IsChildEnabled(Target)
		|| (ModuleType == EMixtormatLayerChildType::HeightPush
			&& Target.Generator.Type != EMixtormatGeneratorType::StrataCarver))
	{
		return false;
	}
	// Insert at the root boundary, never inside the preceding owner's contiguous mask/tool block.
	if (FindSiblingRoot(Layer.Children, InsertIndex, FGuid()) != InsertIndex
		|| (InsertIndex > 0 && FindSubtreeEnd(Layer.Children, InsertIndex - 1) > InsertIndex))
	{
		OutReason = LOCTEXT("StructuralCreationScopeBoundary", "Insertion would split an existing child subtree");
		return false;
	}

	FMixtormatLayerChild Module;
	ApplyChildCreationDefaults(Module, ModuleType == EMixtormatLayerChildType::HeightPush
		? EMixtormatChildCreation::HeightPush : EMixtormatChildCreation::StructuralWarp);
	Module.ScopeOwnerChildId.Invalidate();
	FMixtormatOutputReference& Source = ModuleType == EMixtormatLayerChildType::HeightPush
		? Module.HeightPush.Source : Module.StructuralWarp.Source;
	Source.SourceLayerId.Invalidate();
	Source.SourceChildId.Invalidate();
	if (ModuleType == EMixtormatLayerChildType::HeightPush) { Module.HeightPush.TargetChildId = TargetChildId; }
	else { Module.StructuralWarp.TargetChildId = TargetChildId; }
	ApplyLinkDefaults(Module, TargetLayerId);
	ProposedLayers = WorkingLayers;
	ProposedLayers[LayerIndex].Children.Insert(MoveTemp(Module), InsertIndex);

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(ProposedLayers, WorkingLayerGroups, Effective);
	const int32 EffectiveLayerIndex = Effective.IndexOfByPredicate([&](const FMixtormatLayer& Candidate)
		{ return Candidate.LayerId == TargetLayerId; });
	if (!Effective.IsValidIndex(EffectiveLayerIndex)) { return false; }
	FMixtormatLayer Resolved = Effective[EffectiveLayerIndex];
	const FGuid ModuleId = ProposedLayers[LayerIndex].Children[InsertIndex].ChildId;
	const int32 ModuleIndex = Resolved.Children.IndexOfByPredicate([&](const FMixtormatLayerChild& Candidate)
		{ return Candidate.ChildId == ModuleId; });
	MixtormatParameterBinding::ApplyDirectReferences(FMixtormatBindingScope{Effective, WorkingLayerGroups}, Resolved);
	const auto Status = MixtormatOutputReferences::EvaluateStructuralLinkForGather(
		Effective, EffectiveLayerIndex, ModuleIndex, Resolved);
	if (Status.ModuleIssue != MixtormatOutputReferences::EStructuralLinkIssue::None
		|| Status.Target.Issue != MixtormatOutputReferences::EStructuralLinkIssue::None)
	{
		OutReason = LOCTEXT("StructuralCreationInvalidTarget", "Target is unavailable in the effective generator stack");
		return false;
	}
	if (!PublishedOutputPlacementsValid(FMixtormatBindingScope{ProposedLayers, WorkingLayerGroups}))
	{
		OutReason = LOCTEXT("StructuralCreationPublishedPlacement", "Insertion requires valid published-output placement");
		return false;
	}
	if (!StructuralLinksPreserved(ProposedLayers, WorkingLayerGroups, OutReason)) { return false; }
	OutReason = FText::GetEmpty();
	return true;
}

FReply SMixtormat::CreateStructuralModuleForTarget(const FGuid TargetLayerId, const FGuid TargetChildId,
	const EMixtormatLayerChildType ModuleType)
{
	TArray<FMixtormatLayer> ProposedLayers;
	int32 LayerIndex = INDEX_NONE;
	int32 InsertIndex = INDEX_NONE;
	FText Reason;
	if (!PrepareStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType,
		ProposedLayers, LayerIndex, InsertIndex, Reason))
	{
		WorkingStatusText = Reason.ToString();
		return FReply::Handled();
	}
	WorkingLayers = MoveTemp(ProposedLayers);
	SetLayerExpanded(LayerIndex, true);
	// Commit selection here: SelectWorkingChild can submit a second preview refresh in debug mode.
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = InsertIndex;
	bHasSelectedLayer = true;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	RefreshLayeredPreview(false);
	LastHistoryRecordTime = 0.0;
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
	SyncSelectedLayerControls();
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

FReply SMixtormat::ReplaceOutputReferenceSource(
	const FMixtormatChildAddress& Dest, const FMixtormatOutputReference& Reference)
{
	FMixtormatLayerChild* Child = ResolveChildAt(Dest);
	if (!Child || Child->Type != EMixtormatLayerChildType::OutputReference || Child->IsInstance()
		|| Reference.Kind != Child->OutputReference.Kind) { return FReply::Unhandled(); }
	FMixtormatLayerChild Candidate = *Child;
	Candidate.OutputReference = Reference;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	if (!IsPublishedSourceEnabled(Scope, Reference.SourceLayerId, Reference.SourceChildId)
		|| !CanReadPublishedOutputAt(Scope, Candidate, Dest.OwnerId, ResolveChildIndexAt(Dest)))
	{
		return FReply::Unhandled();
	}
	const bool bEnabled = Child->OutputReference.bEnabled;
	Child->OutputReference = Reference;
	Child->OutputReference.bEnabled = bEnabled;
	RefreshLayeredPreview();
	RebuildLayerList();
	SyncSelectedLayerControls();
	return FReply::Handled();
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

FReply SMixtormat::AddFilterToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::SurfaceIds);
}

FReply SMixtormat::AddHsvFilterToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::HsvFromIds);
}

FReply SMixtormat::AddPatternIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::PatternIds);
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


FReply SMixtormat::AddRampIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::RampFromIds);
}

FReply SMixtormat::AddRandomIdToLayer(const int32 LayerIndex)
{
	return CreateChild(
		FMixtormatAddTarget::Layer(LayerIndex),
		EMixtormatChildCreation::RandomFromIds);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	SelectedLayerIndex = LayerIndex;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
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
		&& ChildType != EMixtormatLayerChildType::BoundaryFromIds
		&& ChildType != EMixtormatLayerChildType::PatternId

		&& ChildType != EMixtormatLayerChildType::IdGroup
		&& ChildType != EMixtormatLayerChildType::OutputReference
		&& ChildType != EMixtormatLayerChildType::Generator
		&& ChildType != EMixtormatLayerChildType::HeightBlend
		&& ChildType != EMixtormatLayerChildType::HeightCurve
		&& ChildType != EMixtormatLayerChildType::HeightColorRamp
		&& ChildType != EMixtormatLayerChildType::HeightPush
		&& ChildType != EMixtormatLayerChildType::StructuralWarp)
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
	MixtormatLayersPrivate::SetChildEnabled(*ResolveChild(LayerIndex, ChildIndex), bEnabled);

	RefreshLayeredPreview();
	RebuildLayerList();
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::AddGeneratorFlow(
	const FMixtormatChildAddress& Owner, const EMixtormatEffectType Type)
{
	if (!MixtormatIsGeneratorFlowEffect(Type) || !CanAddGeneratorFlow(Owner))
	{
		return FReply::Handled();
	}
	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = Type;
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	const FMixtormatLayerChild* ScopeOwner = ResolveChildAt(Owner);
	if (ScopeOwner && ScopeOwner->Type == EMixtormatLayerChildType::Generator
		&& ScopeOwner->Generator.Type == EMixtormatGeneratorType::Noise)
	{
		Child.Effect.GeneratorFlowSource = EMixtormatGeneratorFlowSource::Height;
	}
	const int32 InsertAt = Owner.ChildId.IsValid()
		? InsertScopedChild(*ResolveContainer(Owner), ResolveChildIndexAt(Owner), MoveTemp(Child))
		: ResolveContainer(Owner)->Add(MoveTemp(Child));
	if (InsertAt == INDEX_NONE)
	{
		return FReply::Handled();
	}
	if (Owner.OwnerType == EMixtormatChildOwnerType::Group)
	{
		FinishGroupChildEdit(Owner.OwnerId, InsertAt);
	}
	else
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Owner](const FMixtormatLayer& Layer) { return Layer.LayerId == Owner.OwnerId; });
		SetLayerExpanded(LayerIndex, true);
		SelectWorkingChild(LayerIndex, InsertAt);
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Layer.Children[InsertAt]);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = InsertAt;
	SelectedMaskIndex = INDEX_NONE;
	SetLayerExpanded(LayerIndex, true);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
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
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
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

#undef LOCTEXT_NAMESPACE
