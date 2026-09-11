#include "Widgets/SMixtormat.h"
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
	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child)
	{
		// Includes both surface effects and effects classified internally as filters.
		// Discrete-data producers and the standalone ID consumers keep their current contract.
		return Child.Type == EMixtormatLayerChildType::Effect;
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
	FMixtormatLayer MovedLayer = MoveTemp(WorkingLayers[SourceLayerIndex]);
	WorkingLayers.RemoveAt(SourceLayerIndex);
	WorkingLayers.Insert(MoveTemp(MovedLayer), TargetLayerIndex);
	MixtormatUI::RemapHeightReferencesAfterMove(WorkingLayers, SourceLayerIndex, TargetLayerIndex);
	SelectedLayerIndex = TargetLayerIndex;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
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
	SyncSelectedLayerControls();
	RebuildMaskList();
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ClusterIds)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FReply SMixtormat::SelectWorkingChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	const bool bEffect = WorkingLayers[LayerIndex].Children[ChildIndex].Type
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
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedEffectIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedEffectIndex];
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedLayerEffect() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedEffectIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedEffectIndex];
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

const FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

int32 SMixtormat::GetSelectedChildIndex() const
{
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
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::PatternId))
	{
		return SelectedMaskIndex;
	}
	return INDEX_NONE;
}

FText SMixtormat::GetSelectedBadgeText() const
{
	// The inspector strip mirrors the row that selected it, so it prints the same derived mark --
	// the child's when a child is selected, the layer's otherwise.
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
	CurrentNormalIntensity = Layer.NormalIntensity;

	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(Layer.DisplayName);
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
			// MASK / FX / GEN -- the same word the child row prints in this position.
			SelectedIdentityText->SetText(MixtormatLayerBadges::KindForChild(Child));
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

	const bool bHasMask = Layer.Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask
			&& !Child.ScopeOwnerChildId.IsValid();
	});
	NewMask.BlendMode = bHasMask
		? EMixtormatMaskBlendMode::Multiply
		: EMixtormatMaskBlendMode::Replace;
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Mask;
	Child.Mask = MoveTemp(NewMask);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	ExpandedLayerIndices.Add(LayerIndex);
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
		|| !CanOwnScopedMasks(WorkingLayers[LayerIndex].Children[OwnerChildIndex]))
	{
		return FReply::Handled();
	}

	FMixtormatMaskLayer NewMask;
	if (!BuildMaskLayerFromPath(MaskPath, NewMask))
	{
		return FReply::Handled();
	}
	NewMask.BlendMode = EMixtormatMaskBlendMode::Multiply;

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FGuid OwnerId = Layer.Children[OwnerChildIndex].ChildId;
	int32 InsertAt = OwnerChildIndex + 1;
	while (Layer.Children.IsValidIndex(InsertAt)
		&& Layer.Children[InsertAt].ScopeOwnerChildId == OwnerId)
	{
		++InsertAt;
	}

	FMixtormatLayerChild ScopedMask;
	ScopedMask.Type = EMixtormatLayerChildType::Mask;
	ScopedMask.ScopeOwnerChildId = OwnerId;
	ScopedMask.Mask = MoveTemp(NewMask);
	Layer.Children.Insert(MoveTemp(ScopedMask), InsertAt);

	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = InsertAt;
	ExpandedLayerIndices.Add(LayerIndex);
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
	if (!Cast<UMixtormatSurface>(SurfacePath.TryLoad()))
	{
		return FReply::Handled();
	}

	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SurfacePath);
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
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Mask)
	{
		return FReply::Handled();
	}

	UObject* MaskObject = MaskPath.TryLoad();
	FMixtormatMaskLayer Replacement = WorkingLayers[LayerIndex].Children[ChildIndex].Mask;
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

	WorkingLayers[LayerIndex].Children[ChildIndex].Mask = MoveTemp(Replacement);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ClearLayerMask(const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		FGuid SelectedEffectId;
		FGuid SelectedScopedMaskId;
		bool bRemovingSelectedMask = false;
		if (SelectedLayerIndex == LayerIndex)
		{
			if (Layer.Children.IsValidIndex(SelectedEffectIndex))
			{
				SelectedEffectId = Layer.Children[SelectedEffectIndex].ChildId;
			}
			if (Layer.Children.IsValidIndex(SelectedMaskIndex))
			{
				const FMixtormatLayerChild& SelectedMask = Layer.Children[SelectedMaskIndex];
				if (SelectedMask.ScopeOwnerChildId.IsValid())
				{
					SelectedScopedMaskId = SelectedMask.ChildId;
				}
				else if (SelectedMask.Type == EMixtormatLayerChildType::Mask)
				{
					bRemovingSelectedMask = true;
				}
			}
		}
		Layer.Children.RemoveAll([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask
				&& !Child.ScopeOwnerChildId.IsValid();
		});
		if (SelectedLayerIndex == LayerIndex)
		{
			SelectedEffectIndex = SelectedEffectId.IsValid()
				? Layer.Children.IndexOfByPredicate([SelectedEffectId](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == SelectedEffectId;
				})
				: INDEX_NONE;
			SelectedMaskIndex = SelectedScopedMaskId.IsValid()
				? Layer.Children.IndexOfByPredicate([SelectedScopedMaskId](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == SelectedScopedMaskId;
				})
				: INDEX_NONE;
			if (bRemovingSelectedMask)
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

FReply SMixtormat::RemoveMaskFromLayer(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedMaskIndex == ChildIndex)
			{
				SelectedMaskIndex = INDEX_NONE;
				bBypassSelectedChild = false;
			}
			else if (SelectedMaskIndex > ChildIndex)
			{
				--SelectedMaskIndex;
			}
			if (SelectedEffectIndex > ChildIndex)
			{
				--SelectedEffectIndex;
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
	const FMixtormatLayerChild& SourceChild = Layer.Children[SourceChildIndex];
	const FMixtormatLayerChild& TargetChild = Layer.Children[TargetChildIndex];

	// A scoped mask stays inside its owner's block. Owners and their masks move as one
	// unit, and all other rows snap around complete owner blocks instead of splitting them.
	const bool bMovingScopedMask = SourceChild.ScopeOwnerChildId.IsValid();
	if (bMovingScopedMask && TargetChild.ScopeOwnerChildId != SourceChild.ScopeOwnerChildId)
	{
		return FReply::Unhandled();
	}

	int32 SourceBlockEnd = SourceChildIndex + 1;
	if (!bMovingScopedMask && CanOwnScopedMasks(SourceChild))
	{
		while (Layer.Children.IsValidIndex(SourceBlockEnd)
			&& Layer.Children[SourceBlockEnd].ScopeOwnerChildId == SourceChild.ChildId)
		{
			++SourceBlockEnd;
		}
	}
	const int32 SourceBlockCount = SourceBlockEnd - SourceChildIndex;

	int32 TargetBlockStart = TargetChildIndex;
	int32 TargetBlockEnd = TargetChildIndex + 1;
	if (!bMovingScopedMask)
	{
		FGuid TargetOwnerId;
		if (TargetChild.ScopeOwnerChildId.IsValid())
		{
			TargetOwnerId = TargetChild.ScopeOwnerChildId;
			TargetBlockStart = Layer.Children.IndexOfByPredicate(
				[TargetOwnerId](const FMixtormatLayerChild& Candidate)
				{
					return Candidate.ChildId == TargetOwnerId;
				});
			if (TargetBlockStart == INDEX_NONE)
			{
				return FReply::Unhandled();
			}
		}
		else if (CanOwnScopedMasks(TargetChild))
		{
			TargetOwnerId = TargetChild.ChildId;
		}

		TargetBlockEnd = TargetBlockStart + 1;
		while (TargetOwnerId.IsValid()
			&& Layer.Children.IsValidIndex(TargetBlockEnd)
			&& Layer.Children[TargetBlockEnd].ScopeOwnerChildId == TargetOwnerId)
		{
			++TargetBlockEnd;
		}
		if (TargetChild.ScopeOwnerChildId.IsValid()
			&& (TargetChildIndex <= TargetBlockStart || TargetChildIndex >= TargetBlockEnd))
		{
			return FReply::Unhandled();
		}
	}

	if (TargetBlockStart < SourceBlockEnd && TargetBlockEnd > SourceChildIndex)
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

	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(SourceBlockCount);
	for (int32 MoveIndex = 0; MoveIndex < SourceBlockCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(Layer.Children[SourceChildIndex + MoveIndex]));
	}
	Layer.Children.RemoveAt(SourceChildIndex, SourceBlockCount);

	const int32 InsertAt = TargetBlockStart < SourceChildIndex
		? TargetBlockStart
		: TargetBlockEnd - SourceBlockCount;
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		Layer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
	}

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
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::DuplicateLayerChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& Source = Layer.Children[ChildIndex];
	TArray<FMixtormatLayerChild> Copies;
	Copies.Add(Source);
	int32 InsertAt = ChildIndex + 1;
	if (CanOwnScopedMasks(Source))
	{
		while (Layer.Children.IsValidIndex(InsertAt)
			&& Layer.Children[InsertAt].ScopeOwnerChildId == Source.ChildId)
		{
			Copies.Add(Layer.Children[InsertAt]);
			++InsertAt;
		}
	}

	for (FMixtormatLayerChild& Copy : Copies)
	{
		Copy.SourceLayerId = FGuid();
		Copy.SourceChildId = FGuid();
		MixtormatParameterBinding::RegenerateChildIdentity(Copy);
	}
	if (Copies.Num() > 1)
	{
		const FGuid NewOwnerId = Copies[0].ChildId;
		for (int32 CopyIndex = 1; CopyIndex < Copies.Num(); ++CopyIndex)
		{
			Copies[CopyIndex].ScopeOwnerChildId = NewOwnerId;
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
	ExpandedLayerIndices.Add(LayerIndex);
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

	// An owner and its scoped masks move as one visible block. A scoped mask moved alone
	// becomes layer-scoped because owner links are deliberately local to one layer.
	FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
	const FGuid OldLayerId = SourceLayer.LayerId;
	const FGuid NewLayerId = WorkingLayers[DestLayerIndex].LayerId;
	const FGuid OwnerId = SourceLayer.Children[ChildIndex].ChildId;
	int32 MoveCount = 1;
	if (CanOwnScopedMasks(SourceLayer.Children[ChildIndex]))
	{
		while (SourceLayer.Children.IsValidIndex(ChildIndex + MoveCount)
			&& SourceLayer.Children[ChildIndex + MoveCount].ScopeOwnerChildId == OwnerId)
		{
			++MoveCount;
		}
	}

	TArray<FMixtormatLayerChild> MovedChildren;
	MovedChildren.Reserve(MoveCount);
	for (int32 MoveIndex = 0; MoveIndex < MoveCount; ++MoveIndex)
	{
		MovedChildren.Add(MoveTemp(SourceLayer.Children[ChildIndex + MoveIndex]));
	}
	SourceLayer.Children.RemoveAt(ChildIndex, MoveCount);
	if (MoveCount == 1 && MovedChildren[0].ScopeOwnerChildId.IsValid())
	{
		MovedChildren[0].ScopeOwnerChildId.Invalidate();
	}

	FMixtormatLayer& DestLayer = WorkingLayers[DestLayerIndex];
	int32 InsertAt = DestLayer.Children.IsValidIndex(DestChildIndex)
		? DestChildIndex
		: DestLayer.Children.Num();
	if (DestLayer.Children.IsValidIndex(InsertAt)
		&& DestLayer.Children[InsertAt].ScopeOwnerChildId.IsValid())
	{
		const FGuid TargetOwnerId = DestLayer.Children[InsertAt].ScopeOwnerChildId;
		const int32 TargetOwnerIndex = DestLayer.Children.IndexOfByPredicate(
			[TargetOwnerId](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.ChildId == TargetOwnerId;
			});
		InsertAt = TargetOwnerIndex == INDEX_NONE ? InsertAt : TargetOwnerIndex;
	}
	for (int32 MoveIndex = 0; MoveIndex < MovedChildren.Num(); ++MoveIndex)
	{
		const FGuid MovedChildId = MovedChildren[MoveIndex].ChildId;
		DestLayer.Children.Insert(MoveTemp(MovedChildren[MoveIndex]), InsertAt + MoveIndex);
		MixtormatParameterBinding::RemapChildParent(
			WorkingLayers, MovedChildId, OldLayerId, NewLayerId);
	}
	const int32 NewChildIndex = InsertAt;

	// Both layers shifted, so any index taken before the move is stale. Select from where the
	// child actually landed rather than from what was captured.
	ExpandedLayerIndices.Add(DestLayerIndex);
	SelectWorkingChild(DestLayerIndex, NewChildIndex);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

void SMixtormat::CopyLayerChild(const int32 LayerIndex, const int32 ChildIndex, const bool bAsInstance)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
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
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
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

bool SMixtormat::CanPasteLayerChild() const
{
	return ChildClipboard.IsSet();
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
	if (!ChildClipboard.IsSet() || !WorkingLayers.IsValidIndex(LayerIndex))
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
	ExpandedLayerIndices.Add(LayerIndex);
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
	if (Instance.Type == EMixtormatLayerChildType::Mask
		&& Layer.Children.IsValidIndex(AnchorChildIndex)
		&& CanOwnScopedMasks(Layer.Children[AnchorChildIndex]))
	{
		const FGuid OwnerId = Layer.Children[AnchorChildIndex].ChildId;
		int32 ScopedInsert = AnchorChildIndex + 1;
		while (Layer.Children.IsValidIndex(ScopedInsert)
			&& Layer.Children[ScopedInsert].ScopeOwnerChildId == OwnerId)
		{
			++ScopedInsert;
		}
		// Same-layer instances may have to remain below their source. Only attach when
		// that ordering still permits a contiguous owner block.
		if (Insert <= ScopedInsert)
		{
			Insert = ScopedInsert;
			Instance.ScopeOwnerChildId = OwnerId;
		}
	}
	Layer.Children.Insert(MoveTemp(Instance), Insert);
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Insert);
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

void SMixtormat::SyncChildInstances()
{
	// Against a snapshot: an instance may name a child in a layer this loop has already rewritten,
	// and a chain has to read authored sources rather than half-updated mirrors.
	const TArray<FMixtormatLayer> Snapshot = WorkingLayers;
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		MixtormatParameterBinding::ResolveChildInstances(Snapshot, Layer);
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
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Unhandled();
	}
	const FGuid SourceLayerId = WorkingLayers[LayerIndex].Children[ChildIndex].SourceLayerId;
	const FGuid SourceChildId = WorkingLayers[LayerIndex].Children[ChildIndex].SourceChildId;
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
			ExpandedLayerIndices.Add(SourceLayerIndex);
			SelectWorkingChild(SourceLayerIndex, SourceChildIndex);
			RebuildLayerList();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SMixtormat::BreakChildInstanceAt(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Unhandled();
	}
	// Resolved against a snapshot, because the child being broken lives in the same array the
	// resolve reads from.
	const TArray<FMixtormatLayer> Snapshot = WorkingLayers;
	if (!MixtormatParameterBinding::BreakChildInstance(
		Snapshot, WorkingLayers[LayerIndex].Children[ChildIndex]))
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
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Unhandled();
	}
	if (MixtormatParameterBinding::ClassifyInstancePlacement(
		WorkingLayers,
		NewSourceLayerId,
		NewSourceChildId,
		WorkingLayers[LayerIndex].LayerId,
		ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
	{
		return FReply::Unhandled();
	}
	WorkingLayers[LayerIndex].Children[ChildIndex].SourceLayerId = NewSourceLayerId;
	WorkingLayers[LayerIndex].Children[ChildIndex].SourceChildId = NewSourceChildId;
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
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return Menu.Build();
	}
	const FGuid DestLayerId = WorkingLayers[LayerIndex].LayerId;
	// Only what this position can legally read is offered, so the menu cannot put the instance
	// into a state the paste path would have refused.
	for (int32 SourceLayerIndex = 0; SourceLayerIndex < WorkingLayers.Num(); ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Candidate : SourceLayer.Children)
		{
			if (Candidate.IsInstance())
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
	const bool bInstance = bValid && WorkingLayers[LayerIndex].Children[ChildIndex].IsInstance();

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
	if (ExpandedLayerIndices.Contains(LayerIndex))
	{
		ExpandedLayerIndices.Remove(LayerIndex);
	}
	else
	{
		ExpandedLayerIndices.Add(LayerIndex);
	}
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
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::ToggleLayerEffect(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMixtormatLayerChildType::Effect)
	{
		FMixtormatLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
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
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMixtormatLayerChildType::Effect)
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		const FGuid OwnerId = Layer.Children[ChildIndex].ChildId;
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

		Layer.Children.RemoveAll([OwnerId](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == OwnerId || Child.ScopeOwnerChildId == OwnerId;
		});
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

void SMixtormat::SetMaskEnabled(const ECheckBoxState CheckState, const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children[ChildIndex].Mask.bEnabled = CheckState == ECheckBoxState::Checked;
		RefreshLayeredPreview();
	}
}

void SMixtormat::SetMaskBlendMode(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const EMixtormatMaskBlendMode BlendMode)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children[ChildIndex].Mask.BlendMode = BlendMode;
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
	for (int32 LayerIndex = 0; LayerIndex < WorkingLayers.Num(); ++LayerIndex)
	{
		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
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

FText SMixtormat::GetLayerSourceText(const int32 LayerIndex) const
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

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

TSharedRef<SWidget> SMixtormat::BuildLayerChildIcon(const int32 LayerIndex, const int32 ChildIndex)
{
	const FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];

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
	const FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
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

TSharedRef<SWidget> SMixtormat::BuildLayerRow(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FText DisplayName = Layer.DisplayName;
	const TWeakPtr<SMixtormat> WeakOwner = StaticCastSharedRef<SMixtormat>(AsShared());

	TSharedRef<SMixtormatLayerGroup> Group = SNew(SMixtormatLayerGroup)
		.bExpanded_Lambda([WeakOwner, LayerIndex]()
		{
			const TSharedPtr<SMixtormat> Owner = WeakOwner.Pin();
			return Owner.IsValid() && Owner->ExpandedLayerIndices.Contains(LayerIndex);
		})
		.Header()
		[
			SNew(SMixtormatLayerRow)
			.Name(DisplayName)
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
				return Owner.IsValid() && Owner->ExpandedLayerIndices.Contains(LayerIndex);
			})
			.bSelected_Lambda([this, LayerIndex]() { return SelectedLayerIndex == LayerIndex; })
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

	for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
	{
		const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
		const bool bEffect = Child.Type == EMixtormatLayerChildType::Effect;
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

		Group->AddChild(
			SNew(SBox)
			.Padding(FMargin(Child.ScopeOwnerChildId.IsValid() ? 18.0f : 0.0f, 0.0f, 0.0f, 0.0f))
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
				.Kind(MixtormatLayerBadges::KindForChild(Child))
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
				.OnGetContextMenu_Lambda([this, LayerIndex, ChildIndex, bEffect, bGenerated]()
				{
					if (bGenerated)
					{
						return BuildGeneratedContextMenu(LayerIndex, ChildIndex);
					}
					return bEffect
						? BuildEffectContextMenu(LayerIndex, ChildIndex)
						: BuildMaskContextMenu(LayerIndex, ChildIndex);
				})
				.OnDragDetected_Lambda([this, LayerIndex, ChildIndex, ChildName](const FGeometry&, const FPointerEvent&)
				{
					return FReply::Handled().BeginDragDrop(
						FMixtormatChildDragDropOp::New(LayerIndex, ChildIndex, ChildName));
				})
			]
		]);
	}

	return SNew(SMixtormatLayerRowDropTarget)
		.TargetLayerIndex(LayerIndex)
		.OnLayerDropped(this, &SMixtormat::HandleLayerDropped)
		.OnMaskDropped(this, &SMixtormat::AssignMaskToLayer)
		[
			Group
		];
}

bool SMixtormat::IsLayerChildEnabled(const int32 LayerIndex, const int32 ChildIndex) const
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return false;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
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
		LOCTEXT("DuplicateLayerContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]() { DuplicateSelectedLayer(); }))
		.Shortcut(LOCTEXT("DuplicateLayerShortcut", "Ctrl D"));

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
		.Destructive()
		.Shortcut(LOCTEXT("DeleteLayerShortcut", "Del"));

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
		LOCTEXT("AddFlowWarpEffect", "Flow Warp"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddFlowWarpToLayer(LayerIndex); }));
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
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& SourceChild = WorkingLayers[LayerIndex].Children[ChildIndex];
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
		FText::Format(LOCTEXT("AddSelectedMaskToEffect", "Add Mask · {0}"), SelectedEffectMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, SelectedEffectMaskPath]()
		{
			AssignScopedMaskToChild(LayerIndex, ChildIndex, SelectedEffectMaskPath);
		}))
		.Enabled(TAttribute<bool>(!SelectedEffectMaskPath.IsNull()));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateEffectChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}))
		.Shortcut(LOCTEXT("DuplicateChildShortcut", "Ctrl D"));
	AddSharedChildMenuItems(Menu, LayerIndex, ChildIndex);
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveEffectChild", "Remove Effect"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveLayerEffect(LayerIndex, ChildIndex);
		}))
		.Destructive()
		.Shortcut(LOCTEXT("RemoveChildShortcut", "Del"));
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
			? WorkingLayers[LayerIndex].Children[ChildIndex].Type
			: EMixtormatLayerChildType::Mask;
	const bool bFilter = RowType == EMixtormatLayerChildType::Filter
		|| RowType == EMixtormatLayerChildType::HsvFilter
		|| RowType == EMixtormatLayerChildType::RampId
		|| RowType == EMixtormatLayerChildType::PatternId;

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
		}))
		.Shortcut(LOCTEXT("DuplicateGeneratedShortcut", "Ctrl D"));
	AddSharedChildMenuItems(Menu, LayerIndex, ChildIndex);
	Menu.Separator();
	// Named after the row it is on. This menu serves generated masks, craquelure and colour id
	// nodes, and "Remove Generated Mask" on a craquelure row reads like the wrong entry. Resolved
	// here rather than bound, because the menu is rebuilt on every right-click.
	FText RemoveLabel = LOCTEXT("RemoveGeneratedChild", "Remove Generated Mask");
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		switch (WorkingLayers[LayerIndex].Children[ChildIndex].Type)
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
		.Destructive()
		.Shortcut(LOCTEXT("RemoveGeneratedShortcut", "Del"));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskContextMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	// A menu, like every other right-click in the stack. This used to open a 340px gallery titled
	// REPLACE MASK with two buttons under it -- so right-clicking a mask did something entirely
	// unlike right-clicking the effect directly beneath it, and the common actions were below the
	// fold of a picker you had not asked for. Replacing is still here; it is one entry now.
	MixtormatMenu::FBuilder Menu;
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
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateMaskContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			DuplicateLayerChild(LayerIndex, MaskIndex);
		}))
		.Shortcut(LOCTEXT("DuplicateMaskShortcut", "Ctrl D"));
	AddSharedChildMenuItems(Menu, LayerIndex, MaskIndex);
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DeleteMaskContext", "Remove Mask"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			RemoveMaskFromLayer(LayerIndex, MaskIndex);
		}))
		.Destructive()
		.Shortcut(LOCTEXT("RemoveMaskShortcut", "Del"));
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
				SNew(SScrollBox)
				+ SScrollBox::Slot()
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
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatColorIdMask* SMixtormat::GetSelectedColorId()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::ColorId ? &Child.ColorId : nullptr;
}

const FMixtormatColorIdMask* SMixtormat::GetSelectedColorId() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatClusterFilter* SMixtormat::GetSelectedFilter()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Filter ? &Child.Filter : nullptr;
}

const FMixtormatClusterFilter* SMixtormat::GetSelectedFilter() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::HsvFilter ? &Child.HsvFilter : nullptr;
}

const FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::PatternId ? &Child.PatternId : nullptr;
}

const FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::RampId ? &Child.RampId : nullptr;
}

const FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SelectWorkingChild(LayerIndex, Layer.Children.Num() - 1);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::RandomId ? &Child.RandomId : nullptr;
}

const FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
}

const FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
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
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::RemoveGeneratedFromLayer(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	// All procedural children routed through the shared row actions, including data filters.
	const EMixtormatLayerChildType ChildType = WorkingLayers[LayerIndex].Children[ChildIndex].Type;
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

	WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
	if (SelectedLayerIndex == LayerIndex)
	{
		if (SelectedMaskIndex == ChildIndex)
		{
			SelectedMaskIndex = INDEX_NONE;
			bBypassSelectedChild = false;
		}
		else if (SelectedMaskIndex > ChildIndex)
		{
			--SelectedMaskIndex;
		}
		if (SelectedEffectIndex > ChildIndex)
		{
			--SelectedEffectIndex;
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
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return;
	}

	// The shared procedural-child row routes mask producers and data filters here.
	const bool bEnabled = CheckState == ECheckBoxState::Checked;
	FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
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
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMixtormatLayerChildType::Generated ? &Child.Generated : nullptr;
}

const FMixtormatGeneratedMask* SMixtormat::GetSelectedGeneratedMask() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
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
				if (!WorkingLayers.IsValidIndex(LayerIndex)
					|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
					|| WorkingLayers[LayerIndex].Children[ChildIndex].Type
						!= EMixtormatLayerChildType::Generated)
				{
					return;
				}
				WorkingLayers[LayerIndex].Children[ChildIndex].Generated.BlendMode = Mode;
				RefreshLayeredPreview();
				RebuildLayerList();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
					&& WorkingLayers[LayerIndex].Children[ChildIndex].Generated.BlendMode == Mode;
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
	ExpandedLayerIndices.Add(LayerIndex);
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
	ExpandedLayerIndices.Add(LayerIndex);
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
	ExpandedLayerIndices.Add(LayerIndex);
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
	ExpandedLayerIndices.Add(LayerIndex);
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
	ExpandedLayerIndices.Add(LayerIndex);
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

FReply SMixtormat::AddFlowWarpToLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	FMixtormatLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMixtormatLayerChildType::Effect;
	Child.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	ExpandedLayerIndices.Add(LayerIndex);
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
	ExpandedLayerIndices.Add(LayerIndex);
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
