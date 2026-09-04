#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Controls/SMixtormatTile.h"
#include "Widgets/SToolTip.h"

// The layer stack: layer and child operations, selection, and the layer list UI.

#define LOCTEXT_NAMESPACE "SMixtormat"

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
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex) || SelectedLayerIndex == 0)
	{
		return FReply::Handled();
	}

	SoloLayerIndex = INDEX_NONE;
	const int32 DeletedLayerIndex = SelectedLayerIndex;
	WorkingLayers.RemoveAt(DeletedLayerIndex);
	MixtormatUI::RemapHeightReferencesAfterDelete(WorkingLayers, DeletedLayerIndex);
	SelectedLayerIndex = FMath::Clamp(DeletedLayerIndex - 1, 0, WorkingLayers.Num() - 1);
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
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex) || SelectedLayerIndex == 0)
	{
		return FReply::Handled();
	}

	const int32 TargetIndex = FMath::Clamp(
		SelectedLayerIndex + Direction,
		1,
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
		|| SourceLayerIndex <= 0
		|| TargetLayerIndex <= 0
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
	if (bWasBypassingChild)
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
	if (bWasBypassingChild)
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
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Generated))
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
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
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
					: LOCTEXT("SelectedMaskMaps", "MASK"));
		}
	}
}

FReply SMixtormat::AssignMaskToLayer(const int32 LayerIndex, const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
	{
		return FReply::Handled();
	}

	FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	UObject* MaskObject = MaskPath.TryLoad();
	FMixtormatMaskLayer NewMask;
	if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
	{
		NewMask.Mask = TSoftObjectPtr<UMixtormatMask>(MaskPath);
		NewMask.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
		NewMask.Tiling = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
		NewMask.Balance = Mask->DefaultBalance;
		NewMask.Contrast = Mask->DefaultContrast;
		NewMask.Offset = Mask->DefaultOffset;
		NewMask.bInvert = Mask->bDefaultInvert;
	}
	else if (Cast<UTexture2D>(MaskObject))
	{
		NewMask.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
	}
	else
	{
		return FReply::Handled();
	}

	const bool bHasMask = Layer.Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask;
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
	if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
	{
		Replacement.Mask = TSoftObjectPtr<UMixtormatMask>(MaskPath);
		Replacement.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
		Replacement.Tiling = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
		Replacement.Balance = Mask->DefaultBalance;
		Replacement.Contrast = Mask->DefaultContrast;
		Replacement.bInvert = Mask->bDefaultInvert;
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
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0)
	{
		FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedEffectIndex != INDEX_NONE)
			{
				int32 RemovedBeforeSelection = 0;
				for (int32 ChildIndex = 0;
					ChildIndex < FMath::Min(SelectedEffectIndex, Layer.Children.Num());
					++ChildIndex)
				{
					RemovedBeforeSelection += Layer.Children[ChildIndex].Type
						== EMixtormatLayerChildType::Mask ? 1 : 0;
				}
				SelectedEffectIndex -= RemovedBeforeSelection;
			}
			SelectedMaskIndex = INDEX_NONE;
		}
		Layer.Children.RemoveAll([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask;
		});
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

	FMixtormatLayerChild MovedChild = MoveTemp(WorkingLayers[LayerIndex].Children[SourceChildIndex]);
	WorkingLayers[LayerIndex].Children.RemoveAt(SourceChildIndex);
	WorkingLayers[LayerIndex].Children.Insert(MoveTemp(MovedChild), TargetChildIndex);
	if (SelectedLayerIndex == LayerIndex)
	{
		auto UpdateSelectedIndex = [SourceChildIndex, TargetChildIndex](int32& SelectedIndex)
		{
			if (SelectedIndex == SourceChildIndex)
			{
				SelectedIndex = TargetChildIndex;
			}
			else if (SourceChildIndex < TargetChildIndex
				&& SelectedIndex > SourceChildIndex && SelectedIndex <= TargetChildIndex)
			{
				--SelectedIndex;
			}
			else if (SourceChildIndex > TargetChildIndex
				&& SelectedIndex >= TargetChildIndex && SelectedIndex < SourceChildIndex)
			{
				++SelectedIndex;
			}
		};
		UpdateSelectedIndex(SelectedEffectIndex);
		UpdateSelectedIndex(SelectedMaskIndex);
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
	const int32 NewChildIndex = ChildIndex + 1;
	Layer.Children.Insert(Layer.Children[ChildIndex], NewChildIndex);
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
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0 && Cast<UTexture2D>(NormalPath.TryLoad()))
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
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
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
	if (Effect->EffectType == EMixtormatEffectType::Stain)
	{
		LayerEffect.StainColor = Effect->DefaultStainColor;
		LayerEffect.StainRoughness = Effect->DefaultStainRoughness;
		LayerEffect.StainHeightInfluence = Effect->DefaultStainHeightInfluence;
		LayerEffect.StainHeightWarp = Effect->DefaultStainHeightWarp;
		LayerEffect.StainHeightBias = Effect->DefaultStainHeightBias;
		LayerEffect.StainHeightContrast = Effect->DefaultStainHeightContrast;
	}
	else
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
		WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedEffectIndex == ChildIndex)
			{
				SelectedEffectIndex = INDEX_NONE;
			}
			else if (SelectedEffectIndex > ChildIndex)
			{
				--SelectedEffectIndex;
			}
			if (SelectedMaskIndex > ChildIndex)
			{
				--SelectedMaskIndex;
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

FReply SMixtormat::OpenStainColorPicker(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Effect)
	{
		return FReply::Handled();
	}

	FMixtormatLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
	LastHistoryRecordTime = 0.0;
	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.InitialColor = Effect.StainColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this,
		&SMixtormat::SetStainColor,
		LayerIndex,
		ChildIndex);
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(
		this,
		&SMixtormat::RestoreStainColor,
		LayerIndex,
		ChildIndex);
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

void SMixtormat::SetStainColor(
	FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		FMixtormatLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
		NewColor.A = Effect.StainColor.A;
		Effect.StainColor = NewColor;
		RefreshLayeredPreview();
	}
}

void SMixtormat::RestoreStainColor(
	FLinearColor OriginalColor,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		FMixtormatLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
		OriginalColor.A = Effect.StainColor.A;
		Effect.StainColor = OriginalColor;
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
			BuildMaskCard(SelectedLayerIndex, Mask.DisplayName, Mask.AssetPath, Mask.ThumbnailAsset, true)
		];
	}

	if (Masks.IsEmpty())
	{
		MaskListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EmptyMaskRegistry", "No mask assets in /MaterialLab/Masks"))
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
			.WidthOverride(MixtormatUI::LayerStackWidth)
			[
				SNew(SBorder)
				.Padding(MixtormatUI::PanelPadding)
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
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
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
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
						[
							SNew(SMixtormatIconButton)
							.Icon(MixtormatIcons::LayerFill())
							.ToolTip(LOCTEXT("AddFillLayerHintCompact", "Create a constant Base Color, Roughness, IOR, and Metallic fill layer."))
							.OnClicked_Lambda([this]() { AddWorkingLayer(EMixtormatLayerType::Fill); })
						]
						// The count sits on the far edge, so the glyphs read as a pair rather than as
						// three things in a row.
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						.HAlign(HAlign_Right).VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerRowInsetTrailing, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								return FText::Format(LOCTEXT("LayerCountCompact", "{0} LAYERS"), FText::AsNumber(WorkingLayers.Num()));
							})
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()[SAssignNew(LayerListBox, SVerticalBox)]
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
			return Thumbnail->MakeThumbnailWidget();
		}
	}
	else if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
	{
		if (Surface->PreviewMaterial)
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				FAssetData(Surface->PreviewMaterial.Get()), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget();
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
	if (LayerIndex == 0)
	{
		return LOCTEXT("BaseLayerSource", "BASE");
	}
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
	if (Child.Type == EMixtormatLayerChildType::Effect)
	{
		const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
		if (Asset)
		{
			return Asset->EffectType == EMixtormatEffectType::Peeling
				? LOCTEXT("PeelingEffectName", "Peeling")
				: Asset->EffectType == EMixtormatEffectType::Stain
					? LOCTEXT("StainEffectName", "Stain")
					: LOCTEXT("ErosionEffectName", "Erosion");
		}
		return Child.Effect.ProceduralType == EMixtormatEffectType::Erosion
			? LOCTEXT("ErosionEffectName", "Erosion")
			: LOCTEXT("ProceduralPeelName", "Peeling (Procedural)");
	}
	if (Child.Type == EMixtormatLayerChildType::Generated)
	{
		return LOCTEXT("GeneratedChildName", "Generated Mask");
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
			: Child.Type == EMixtormatLayerChildType::Generated
				? MixtormatIcons::Generated()
				: MixtormatIcons::Mask())
		.ColorAndOpacity(FSlateColor(MixtormatPalette::CaptionText()));
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
	// The base layer is the material itself: it cannot be hidden, moved or dragged onto.
	const bool bIsBase = LayerIndex == 0;
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
			.bCanDisable(!bIsBase)
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
				return LayerIndex > 0
					? FReply::Handled().BeginDragDrop(
						FMixtormatLayerDragDropOp::New(LayerIndex, DisplayName))
					: FReply::Unhandled();
			})
		];

	for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
	{
		const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
		const bool bEffect = Child.Type == EMixtormatLayerChildType::Effect;
		const bool bGenerated = Child.Type == EMixtormatLayerChildType::Generated;
		const FText ChildName = GetLayerChildName(Child);

		Group->AddChild(
			SNew(SMixtormatChildDropTarget)
			.LayerIndex(LayerIndex)
			.ChildIndex(ChildIndex)
			.OnChildReordered(this, &SMixtormat::ReorderLayerChild)
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
	const bool bIsBase = LayerIndex == 0;

	// Creation lives here and nowhere else. The stack used to carry an "Add Child" button at the
	// bottom of every expanded layer, which cost a row of height per layer to say something the
	// right button already implies.
	Menu.Caption(LOCTEXT("LayerAddSection", "Add"));
	Menu.SubMenu(
		LOCTEXT("AddMaskChild", "Mask"),
		MixtormatIcons::Mask(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMaskMenu, LayerIndex));
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddEffectMenu, LayerIndex));
	Menu.Item(
		LOCTEXT("AddGeneratedChild", "Generated Mask"),
		MixtormatIcons::Generated(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddGeneratedMaskToLayer(LayerIndex); }));

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
		.Enabled(!bIsBase)
		.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].bEnabled;
		}));

	Menu.Separator();

	Menu.Item(
		LOCTEXT("DuplicateLayerContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]() { DuplicateSelectedLayer(); }))
		.Shortcut(LOCTEXT("DuplicateLayerShortcut", "Ctrl D"));

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask;
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
		.Enabled(!bIsBase)
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
		LOCTEXT("AddErosionEffect", "Erosion"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddErosionToLayer(LayerIndex); }));
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
	Menu.Item(
		LOCTEXT("DuplicateEffectChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}))
		.Shortcut(LOCTEXT("DuplicateChildShortcut", "Ctrl D"));
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
	Menu.SubMenu(
		LOCTEXT("GeneratedBlendModeContext", "Blend Mode"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGeneratedBlendModeMenu, LayerIndex, ChildIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateGeneratedChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}))
		.Shortcut(LOCTEXT("DuplicateGeneratedShortcut", "Ctrl D"));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveGeneratedChild", "Remove Generated Mask"),
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
	Menu.SubMenu(
		LOCTEXT("ReplaceMaskContext", "Replace Mask"),
		MixtormatIcons::Mask(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskReplacementMenu, LayerIndex, MaskIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateMaskContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			DuplicateLayerChild(LayerIndex, MaskIndex);
		}))
		.Shortcut(LOCTEXT("DuplicateMaskShortcut", "Ctrl D"));
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
			BuildMaskReplacementGallery(LayerIndex, MaskIndex)
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
	return SNew(SBorder)
		.Padding(FMargin(8.0f, 7.0f))
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.InsetPanel")))
		.Visibility_Lambda([this]()
		{
			return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MaskBarHeading", "MASKS · DRAG ONTO A LAYER"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(MaskListBox, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(3.0f, 3.0f))
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildMaskGallery(TFunction<void(const FSoftObjectPath&)> OnChosen)
{
	// One grid, whether the mask is being added or replaced. Picking a mask is the same question
	// both times, and it was answered two ways: a gallery for replacing, a list of names for
	// adding -- so the choice you made blind was the one that created the thing.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(MixtormatTokens::TileGap, MixtormatTokens::TileGap));

	for (const FMixtormatMaskEntry& Mask : FMixtormatRegistry::GetMasks())
	{
		const FSoftObjectPath Path = Mask.AssetPath;
		Grid->AddSlot()
		[
			SNew(SMixtormatTile)
			.TileSize(MixtormatTokens::MaskTileSize)
			.DisplayName(Mask.DisplayName)
			.ThumbnailAsset(Mask.ThumbnailAsset)
			.ThumbnailPool(ThumbnailPool)
			.OnActivated(FMixtormatOnTileActivated::CreateLambda([OnChosen, Path]()
			{
				OnChosen(Path);
			}))
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
	const int32 LayerIndex,
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset,
	const bool bCompact)
{
	const float ThumbnailSize = bCompact ? 56.0f : 42.0f;
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
		return SNew(SBox)
			.WidthOverride(MixtormatUI::MaskTileSize)
			.HeightOverride(MixtormatUI::MaskTileSize)
			[
				SNew(SMixtormatMaskCard)
				.LayerIndex(LayerIndex)
				.DisplayName(Name)
				.MaskPath(AssetPath)
				.ThumbnailAsset(ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.OnSelected(this, &SMixtormat::AssignMaskToLayer)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.ThumbnailCard")))
					.ContentPadding(3.0f)
					.ToolTipText(Name)
					.IsEnabled_Lambda([this, LayerIndex]()
					{
						return WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0 && WorkingLayers[LayerIndex].bEnabled;
					})
					[SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]]
				]
			];
	}

	return SNew(SButton)
		.ContentPadding(5.0f)
		.OnClicked_Lambda([this, LayerIndex, AssetPath]() { return AssignMaskToLayer(LayerIndex, AssetPath); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Name)
			]
		];
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
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Generated)
	{
		return FReply::Handled();
	}

	WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
	if (SelectedLayerIndex == LayerIndex && SelectedMaskIndex == ChildIndex)
	{
		SelectedMaskIndex = INDEX_NONE;
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
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Generated)
	{
		return;
	}

	WorkingLayers[LayerIndex].Children[ChildIndex].Generated.bEnabled =
		CheckState == ECheckBoxState::Checked;
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
