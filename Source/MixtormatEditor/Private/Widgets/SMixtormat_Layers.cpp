#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Controls/SMixtormatTile.h"

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
	bHasSelectedLayer = Copy.bEnabled;
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
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].bEnabled;
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
	bHasSelectedLayer = WorkingLayers[SelectedLayerIndex].bEnabled;
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
	bHasSelectedLayer = WorkingLayers[LayerIndex].bEnabled;
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
	RebuildLayerList();
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
		&& Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Mask)
	{
		return SelectedMaskIndex;
	}
	return INDEX_NONE;
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
		bHasSelectedLayer = WorkingLayers[LayerIndex].bEnabled;
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
		const FText LayerType = Layer.Type == EMixtormatLayerType::Fill
			? LOCTEXT("FillLayerIdentity", "Fill")
			: Layer.Type == EMixtormatLayerType::Effect
				? LOCTEXT("EffectLayerIdentity", "Effect")
				: LOCTEXT("MaterialLayerIdentity", "Material");
		SelectedIdentityText->SetText(LayerType);
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

	if (GetSelectedLayerEffect())
	{
		if (SelectedSurfaceText.IsValid()) SelectedSurfaceText->SetText(LOCTEXT("SelectedPeelingEffect", "Peeling"));
		if (SelectedIdentityText.IsValid()) SelectedIdentityText->SetText(LOCTEXT("SelectedPeelingIdentity", "Effect"));
		if (SelectedMapsText.IsValid()) SelectedMapsText->SetText(LOCTEXT("SelectedPeelingMaps", "PDM · MSK · H · SDF"));
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

FReply SMixtormat::SetLayerNormalDetail(const int32 LayerIndex, const bool bNormalDetail)
{
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0)
	{
		WorkingLayers[LayerIndex].ChannelMode = bNormalDetail
			? EMixtormatLayerChannelMode::NormalDetail
			: EMixtormatLayerChannelMode::CompleteSurface;
		RefreshLayeredPreview();
		RebuildLayerList();
		SyncSelectedLayerControls();
	}
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

FReply SMixtormat::HandleLayerMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& PointerEvent,
	const int32 LayerIndex)
{
	SelectWorkingLayer(LayerIndex);
	if (PointerEvent.GetEffectingButton() == EKeys::RightMouseButton
		&& LayerIndex > 0
		&& WorkingLayers.IsValidIndex(LayerIndex)
		&& LayerContextAnchors.IsValidIndex(LayerIndex)
		&& LayerContextAnchors[LayerIndex].IsValid())
	{
		LayerContextAnchors[LayerIndex]->SetIsOpen(true);
	}
	return FReply::Handled();
}

void SMixtormat::RebuildLayerList()
{
	if (!LayerListBox.IsValid())
	{
		return;
	}

	LayerListBox->ClearChildren();
	LayerContextAnchors.Reset();
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
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SComboButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
						.ButtonContent()
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Add")))]
													+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
													[SNew(STextBlock).Text(LOCTEXT("AddLayer", "Add Layer"))]
												]
						.OnGetMenuContent(this, &SMixtormat::BuildAddLayerMenu)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
						.ToolTipText(LOCTEXT("MoveLayerUpHint", "Move layer up"))
						.IsEnabled_Lambda([this]() { return SelectedLayerIndex > 1; })
						.OnClicked_Lambda([this]() { return MoveSelectedLayer(-1); })
						[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.ArrowUp")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
						.ToolTipText(LOCTEXT("MoveLayerDownHint", "Move layer down"))
						.IsEnabled_Lambda([this]()
						{
							return SelectedLayerIndex > 0
								&& SelectedLayerIndex < WorkingLayers.Num() - 1;
						})
						.OnClicked_Lambda([this]() { return MoveSelectedLayer(1); })
						[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.ArrowDown")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
						.ToolTipText(LOCTEXT("DuplicateLayerHint", "Duplicate selected layer"))
						.IsEnabled_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex); })
						.OnClicked(this, &SMixtormat::DuplicateSelectedLayer)
						[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Duplicate")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
						.ToolTipText(LOCTEXT("DeleteLayerHint", "Delete selected layer"))
						.IsEnabled_Lambda([this]() { return SelectedLayerIndex > 0; })
						.OnClicked(this, &SMixtormat::DeleteSelectedLayer)
						[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Trash")))]
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

TSharedRef<SWidget> SMixtormat::BuildLayerRow(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FText TypeLabel = LayerIndex == 0
		? LOCTEXT("BaseLockedLabel", "BASE · LOCKED")
		: Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
			? LOCTEXT("NormalDetailLayerLabel", "NORMAL DETAIL")
			: Layer.Type == EMixtormatLayerType::Fill
				? LOCTEXT("FillLayerLabel", "FILL")
				: Layer.Type == EMixtormatLayerType::Effect
					? LOCTEXT("EffectLayerLabel", "EFFECT")
					: LOCTEXT("MaterialLayerLabel", "MATERIAL");
	int32 MaskCount = 0;
	int32 EffectCount = 0;
	for (const FMixtormatLayerChild& Child : Layer.Children)
	{
		MaskCount += (Child.Type == EMixtormatLayerChildType::Mask
			|| Child.Type == EMixtormatLayerChildType::Generated) ? 1 : 0;
		EffectCount += Child.Type == EMixtormatLayerChildType::Effect ? 1 : 0;
	}
	const FText LayerSummary = Layer.Children.IsEmpty()
		? TypeLabel
		: FText::Format(
			LOCTEXT("LayerChildSummary", "{0} · {1} effect(s) · {2} mask(s)"),
			TypeLabel,
			FText::AsNumber(EffectCount),
			FText::AsNumber(MaskCount));

	TSharedRef<SWidget> LayerThumbnail = SNew(SColorBlock)
		.Color_Lambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex)
				&& WorkingLayers[LayerIndex].Type == EMixtormatLayerType::Fill
				? WorkingLayers[LayerIndex].BaseColor
				: FLinearColor(0.08f, 0.08f, 0.08f);
		})
		.Size(FVector2D(32.0f, 32.0f));
	if (Layer.Type != EMixtormatLayerType::Fill)
	{
		if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
			&& Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
			&& !Layer.NormalTexture.IsNull())
		{
			if (UTexture2D* Texture = Layer.NormalTexture.LoadSynchronous())
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(Texture), 32, 32, ThumbnailPool);
				LayerThumbnails.Add(Thumbnail);
				LayerThumbnail = Thumbnail->MakeThumbnailWidget();
			}
		}
		else if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
		{
			if (Surface->PreviewMaterial)
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
					FAssetData(Surface->PreviewMaterial.Get()), 32, 32, ThumbnailPool);
				LayerThumbnails.Add(Thumbnail);
				LayerThumbnail = Thumbnail->MakeThumbnailWidget();
			}
		}
	}


	TSharedPtr<SMenuAnchor> ContextAnchor;
	TSharedRef<SMenuAnchor> Row = SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.OnGetMenuContent(this, &SMixtormat::BuildLayerContextMenu, LayerIndex)
		[
			SNew(SBorder)
			.Padding(4.0f)
			.BorderImage_Lambda([this, LayerIndex]()
			{
				return SelectedLayerIndex == LayerIndex
					? FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.LayerCardSelected"))
					: FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.LayerCard"));
			})
			.OnMouseButtonDown(this, &SMixtormat::HandleLayerMouseButtonDown, LayerIndex)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(12.0f)
					.Visibility(LayerIndex > 0 ? EVisibility::Visible : EVisibility::Hidden)
					[
						SNew(SMixtormatLayerDragHandle)
						.LayerIndex(LayerIndex)
						.DisplayName(Layer.DisplayName)
						[
							SNew(SImage)
							.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))
							.ToolTipText(LOCTEXT("LayerDragHandleHint", "Drag to reorder"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
					.ToolTipText(LOCTEXT("SoloLayerHint", "Preview only this layer"))
					.IsChecked_Lambda([this, LayerIndex]()
					{
						return SoloLayerIndex == LayerIndex
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, LayerIndex](const ECheckBoxState State)
					{
						SoloLayerIndex = State == ECheckBoxState::Checked ? LayerIndex : INDEX_NONE;
						if (SoloLayerIndex != INDEX_NONE)
						{
							bShowCompositionBefore = false;
						}
						RefreshLayeredPreview(false);
						RebuildLayerList();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SoloLayerButton", "S"))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsEnabled(LayerIndex > 0)
					.IsChecked_Lambda([this, LayerIndex]()
					{
						return WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].bEnabled
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(this, &SMixtormat::SetWorkingLayerEnabled, LayerIndex)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(32.0f).HeightOverride(32.0f)[LayerThumbnail]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(5.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Layer.DisplayName)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LayerSummary)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.DragHandle")))
					.ContentPadding(FMargin(2.0f, 0.0f))
					.ToolTipText(LOCTEXT("ToggleLayerDetails", "Show layer details"))
					.OnClicked(this, &SMixtormat::ToggleLayerExpanded, LayerIndex)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(ExpandedLayerIndices.Contains(LayerIndex)
							? TEXT("Icons.ChevronDown")
							: TEXT("Icons.ChevronRight")))
					]
				]
			]
		];

	LayerContextAnchors.SetNum(FMath::Max(LayerContextAnchors.Num(), LayerIndex + 1));
	LayerContextAnchors[LayerIndex] = ContextAnchor;

	TSharedRef<SVerticalBox> RowContent = SNew(SVerticalBox);
	RowContent->AddSlot().AutoHeight()[Row];
	if (ExpandedLayerIndices.Contains(LayerIndex))
	{

		if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail)
		{
			const FText SourceText = Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
				? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName())
				: FText::Format(LOCTEXT("SurfaceNormalSource", "Surface · {0}"), Layer.DisplayName);
			RowContent->AddSlot().AutoHeight().Padding(20.0f, 2.0f, 4.0f, 2.0f)
			[SNew(STextBlock).Text(SourceText).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		}

		for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
		{
			const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
			const bool bEffect = Child.Type == EMixtormatLayerChildType::Effect;
			const bool bGenerated = Child.Type == EMixtormatLayerChildType::Generated;
			FText ChildName;
			FText ChildSummary;
			TSharedRef<SWidget> ChildIcon = SNew(SImage).Image(MixtormatUI::LucideIcon(TEXT("nodes")));
			if (bEffect)
			{
				const UMixtormatEffect* EffectAsset = Child.Effect.Effect.LoadSynchronous();
				if (EffectAsset && EffectAsset->EffectType == EMixtormatEffectType::Peeling)
				{
					ChildName = LOCTEXT("PeelingEffectName", "Peeling");
				}
				else if (EffectAsset && EffectAsset->EffectType == EMixtormatEffectType::Stain)
				{
					ChildName = LOCTEXT("StainEffectName", "Stain");
				}
				else if (Child.Effect.Effect.IsNull()
					&& Child.Effect.ProceduralType == EMixtormatEffectType::Erosion)
				{
					ChildName = LOCTEXT("ErosionEffectName", "Erosion");
				}
				else if (Child.Effect.Effect.IsNull()
					&& Child.Effect.ProceduralType == EMixtormatEffectType::Peeling)
				{
					ChildName = LOCTEXT("ProceduralPeelName", "Peeling (Procedural)");
				}
				else
				{
					ChildName = FText::FromString(Child.Effect.Effect.ToSoftObjectPath().GetAssetName());
				}
				if (Child.Effect.Effect.IsNull()
					&& Child.Effect.ProceduralType == EMixtormatEffectType::Erosion)
				{
					// The pass count is fixed now, so it says nothing about this child.
					// Amount is what distinguishes one erosion child from another.
					ChildSummary = FText::FromString(FString::Printf(
						TEXT("Filter · Height · %.2f"),
						Child.Effect.ErosionAmount));
				}
				else
				{
					ChildSummary = FText::FromString(FString::Printf(TEXT("Effect · %.2f"), Child.Effect.Strength));
				}
			}
			else if (bGenerated)
			{
				const FMixtormatGeneratedMask& Gen = Child.Generated;
				ChildName = LOCTEXT("GeneratedChildName", "Generated Mask");
				ChildSummary = FText::Format(
					LOCTEXT("GeneratedChildSummary", "{0} · {1}"),
					MixtormatUI::MaskBlendModeText(Gen.BlendMode),
					FText::FromString(FString::Printf(TEXT("%.2f"), Gen.Weight)));
			}
			else
			{
				const FMixtormatMaskLayer& MaskLayer = Child.Mask;
				const FSoftObjectPath MaskPath = !MaskLayer.Mask.IsNull()
					? MaskLayer.Mask.ToSoftObjectPath() : MaskLayer.MaskTexture.ToSoftObjectPath();
				ChildName = FText::FromString(MaskPath.GetAssetName());
				ChildSummary = FText::Format(
					LOCTEXT("MaskChildSummary", "{0} · {1}"),
					MixtormatUI::MaskBlendModeText(MaskLayer.BlendMode),
					FText::FromString(FString::Printf(TEXT("%.2f"), MaskLayer.Weight)));
				if (UObject* MaskObject = MaskPath.TryLoad())
				{
					UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
					if (const UMixtormatMask* MaskAsset = Cast<UMixtormatMask>(MaskObject))
					{
						Texture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
					}
					if (Texture)
					{
						TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
							FAssetData(Texture), 24, 24, ThumbnailPool);
						LayerThumbnails.Add(Thumbnail);
						ChildIcon = Thumbnail->MakeThumbnailWidget();
					}
				}
			}

			RowContent->AddSlot().AutoHeight().Padding(8.0f, 1.0f, 4.0f, 1.0f)
			[
				SNew(SMixtormatChildStackItem)
				.LayerIndex(LayerIndex)
				.ChildIndex(ChildIndex)
				.DisplayName(ChildName)
				.OnGetMenuContent_Lambda([this, LayerIndex, ChildIndex, bEffect, bGenerated]()
				{
					if (bGenerated)
					{
						return BuildGeneratedContextMenu(LayerIndex, ChildIndex);
					}
					return bEffect
						? BuildEffectContextMenu(LayerIndex, ChildIndex)
						: BuildMaskContextMenu(LayerIndex, ChildIndex);
				})
				.OnSelected(this, &SMixtormat::SelectWorkingChild)
				.OnChildReordered(this, &SMixtormat::ReorderLayerChild)
				[
					SNew(SBox)
					.HeightOverride(34.0f)
					[
						SNew(SBorder)
						.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
						.BorderImage_Lambda([this, LayerIndex, ChildIndex, bEffect]()
						{
							const bool bSelected = SelectedLayerIndex == LayerIndex
								&& (bEffect ? SelectedEffectIndex : SelectedMaskIndex) == ChildIndex;
							return bSelected
								? FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.LayerCardSelected"))
								: FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.LayerCard"));
						})
						.ToolTipText(LOCTEXT("ChildRowHint", "LMB: select · drag: reorder · overflow: actions"))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
							[
								SNew(SMixtormatHierarchyConnector)
								.IsLast(ChildIndex == Layer.Children.Num() - 1
									&& (LayerIndex == 0
										|| (Layer.Type != EMixtormatLayerType::Material
											&& Layer.Type != EMixtormatLayerType::Fill)))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))]
							+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([this, LayerIndex, ChildIndex]()
								{
									if (!WorkingLayers.IsValidIndex(LayerIndex)
										|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
									{
										return ECheckBoxState::Unchecked;
									}
									const FMixtormatLayerChild& Current = WorkingLayers[LayerIndex].Children[ChildIndex];
									bool bEnabled = Current.Mask.bEnabled;
									if (Current.Type == EMixtormatLayerChildType::Effect)
									{
										bEnabled = Current.Effect.bEnabled;
									}
									else if (Current.Type == EMixtormatLayerChildType::Generated)
									{
										bEnabled = Current.Generated.bEnabled;
									}
									return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this, LayerIndex, ChildIndex, bEffect, bGenerated](const ECheckBoxState State)
								{
									if (bEffect)
									{
										ToggleLayerEffect(LayerIndex, ChildIndex);
									}
									else if (bGenerated)
									{
										SetGeneratedEnabled(State, LayerIndex, ChildIndex);
									}
									else
									{
										SetMaskEnabled(State, LayerIndex, ChildIndex);
									}
								})
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)[ChildIcon]]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(STextBlock).Text(ChildName).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
							+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(ChildSummary)
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SComboButton)
								.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
								.ContentPadding(2.0f)
								.HasDownArrow(false)
								.OnGetMenuContent_Lambda([this, LayerIndex, ChildIndex, bEffect]()
								{
									return bEffect
										? BuildEffectContextMenu(LayerIndex, ChildIndex)
										: BuildMaskContextMenu(LayerIndex, ChildIndex);
								})
								.ButtonContent()[SNew(SImage).Image(MixtormatUI::LucideIcon(TEXT("ellipsis")))]
							]
						]
					]
				]
			];
		}
		if (LayerIndex > 0
			&& (Layer.Type == EMixtormatLayerType::Material || Layer.Type == EMixtormatLayerType::Fill))
		{
			RowContent->AddSlot().AutoHeight().Padding(8.0f, 1.0f, 4.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
				[SNew(SMixtormatHierarchyConnector).IsLast(true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.ContentPadding(FMargin(7.0f, 2.0f))
					.HasDownArrow(false)
					.OnGetMenuContent(this, &SMixtormat::BuildAddChildMenu, LayerIndex)
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[SNew(SImage).Image(MixtormatUI::LucideIcon(TEXT("plus")))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("AddChild", "Add Child"))]
					]
				]
			];
		}
	}

	return SNew(SMixtormatLayerRowDropTarget)
		.TargetLayerIndex(LayerIndex)
		.OnLayerDropped(this, &SMixtormat::HandleLayerDropped)
		.OnMaskDropped(this, &SMixtormat::AssignMaskToLayer)
		[RowContent];
}

TSharedRef<SWidget> SMixtormat::BuildLayerContextMenu(const int32 LayerIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection(TEXT("MixtormatLayerActions"), LOCTEXT("LayerActionsSection", "Layer"));
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask;
		}))
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ClearLayerMasksContext", "Remove All Masks"),
			LOCTEXT("ClearLayerMasksContextHint", "Remove every mask child from this layer."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { ClearLayerMask(LayerIndex); })));
	}
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateLayerContext", "Duplicate Layer"),
		LOCTEXT("DuplicateLayerContextHint", "Duplicate this layer and its child effects and masks."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { DuplicateSelectedLayer(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteLayerContext", "Delete Layer"),
		LOCTEXT("DeleteLayerContextHint", "Delete this layer."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { DeleteSelectedLayer(); })));
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMixtormat::BuildAddChildMenu(const int32 LayerIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddMaskChild", "Mask"),
		LOCTEXT("AddMaskChildHint", "Append a reusable mask child."),
		FNewMenuDelegate::CreateLambda([this, LayerIndex](FMenuBuilder& MaskMenu)
		{
			const TArray<FMixtormatMaskEntry> Masks = FMixtormatRegistry::GetMasks();
			for (const FMixtormatMaskEntry& Entry : Masks)
			{
				MaskMenu.AddMenuEntry(
					Entry.DisplayName,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, MaskPath = Entry.AssetPath]()
					{
						AssignMaskToLayer(LayerIndex, MaskPath);
					})));
			}
			if (Masks.IsEmpty())
			{
				MaskMenu.AddMenuEntry(
					LOCTEXT("MasksUnavailable", "No masks available"),
					LOCTEXT("MasksUnavailableHint", "Import or reimport reusable masks first."),
					FSlateIcon(),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
			}
		}),
		false,
		FSlateIcon());
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		LOCTEXT("AddEffectChildHint", "Append an ordered effect child."),
		FNewMenuDelegate::CreateLambda([this, LayerIndex](FMenuBuilder& EffectMenu)
		{
			const TArray<FMixtormatEffectEntry> Effects = FMixtormatRegistry::GetEffects();
			for (const FMixtormatEffectEntry& Entry : Effects)
			{
				EffectMenu.AddMenuEntry(
					Entry.DisplayName,
					FText::FromName(Entry.Category),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, EffectPath = Entry.AssetPath]()
					{
						AddEffectToLayer(LayerIndex, EffectPath);
					})));
			}
			// Procedural, so it is not discovered from an imported asset set.
			EffectMenu.AddMenuEntry(
				LOCTEXT("AddErosionEffect", "Erosion"),
				LOCTEXT("AddErosionEffectHint", "Carve the height accumulated below this layer."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]()
				{
					AddErosionToLayer(LayerIndex);
				})));
			EffectMenu.AddMenuEntry(
				LOCTEXT("AddProceduralPeelEffect", "Peeling (Procedural)"),
				LOCTEXT("AddProceduralPeelEffectHint", "Grow a peel from noise and the surface below. Needs no imported maps."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]()
				{
					AddProceduralPeelingToLayer(LayerIndex);
				})));
			if (Effects.IsEmpty())
			{
				EffectMenu.AddMenuEntry(
					LOCTEXT("EffectsUnavailable", "No effects available"),
					LOCTEXT("EffectsUnavailableHint", "Reimport the shipped library to create effect assets."),
					FSlateIcon(),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
			}
		}),
		false,
		FSlateIcon());
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddGeneratedChild", "Generated Mask"),
		LOCTEXT("AddGeneratedChildHint", "Append a mask derived from the surface below this layer."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]()
		{
			AddGeneratedMaskToLayer(LayerIndex);
		})));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMixtormat::BuildEffectContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RemoveEffectChild", "Remove Effect"),
		LOCTEXT("RemoveEffectChildHint", "Remove this effect child."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveLayerEffect(LayerIndex, ChildIndex);
		})));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMixtormat::BuildAddLayerMenu()
{
	return SNew(SBorder)
		.Padding(6.0f)
		.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.Text(FText::Format(
					LOCTEXT("AddSelectedMaterialLayer", "Material Layer · {0}"),
					SelectedLibrarySurfaceName.IsEmpty()
						? LOCTEXT("NoSelectedLibraryMaterial", "Select from Library")
						: SelectedLibrarySurfaceName))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddMaterialLayerHint", "Inherit the currently selected immutable library material."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Material); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddFillLayer", "Fill Layer"))
				.ToolTipText(LOCTEXT("AddFillLayerHint", "Create a constant Base Color, Roughness, IOR, and Metallic surface."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Fill); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddEffectLayer", "Effect Layer"))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddEffectLayerHint", "Add the selected library surface as a reusable replacement or coating effect."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Effect); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddNormalDetailLayer", "Normal Detail Layer"))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddNormalDetailLayerHint", "Use only the selected surface normal and preserve BC/RAM."))
				.OnClicked_Lambda([this]()
				{
					AddWorkingLayer(EMixtormatLayerType::Effect);
					return SetLayerNormalDetail(SelectedLayerIndex, true);
				})
			]
		];
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

TSharedRef<SWidget> SMixtormat::BuildMaskBlendModeMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
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
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(MixtormatUI::MaskBlendModeText(Mode))
			.OnClicked_Lambda([this, LayerIndex, MaskIndex, Mode]()
			{
				SetMaskBlendMode(LayerIndex, MaskIndex, Mode);
				return FReply::Handled();
			})
		];
	}
	return SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[Menu];
}

TSharedRef<SWidget> SMixtormat::BuildMaskContextMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	return SNew(SBox)
		.WidthOverride(340.0f)
		.MaxDesiredHeight(460.0f)
		[
			SNew(SBorder)
			.Padding(6.0f)
			.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
				[SNew(STextBlock).Text(LOCTEXT("ReplaceMaskHeading", "REPLACE MASK")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
				+ SVerticalBox::Slot().FillHeight(1.0f)[BuildMaskReplacementGallery(LayerIndex, MaskIndex)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.Text(LOCTEXT("DeleteMaskContext", "Remove This Mask"))
					.OnClicked(this, &SMixtormat::RemoveMaskFromLayer, LayerIndex, MaskIndex)
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildMaskReplacementGallery(const int32 LayerIndex, const int32 MaskIndex)
{
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
			.OnActivated(FMixtormatOnTileActivated::CreateLambda([this, LayerIndex, MaskIndex, Path]()
			{
				ReplaceMaskInLayer(LayerIndex, MaskIndex, Path);
			}))
		];
	}
	return Grid;
}

TSharedRef<SWidget> SMixtormat::BuildNormalSourceMenu(const int32 LayerIndex)
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	if (WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].SourceSurface.IsNull())
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(LOCTEXT("UseSurfaceNormal", "Use Surface Normal"))
			.OnClicked_Lambda([this, LayerIndex]()
			{
				if (WorkingLayers.IsValidIndex(LayerIndex))
				{
					WorkingLayers[LayerIndex].NormalSourceType = EMixtormatNormalSourceType::Surface;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
				return FReply::Handled();
			})
		];
	}
	for (const FMixtormatNormalEntry& Normal : FMixtormatRegistry::GetNormals())
	{
		Menu->AddSlot().AutoHeight()
		[SNew(SButton).Text(Normal.DisplayName).OnClicked_Lambda([this, LayerIndex, Path = Normal.AssetPath]() { return AssignNormalTexture(LayerIndex, Path); })];
	}
	return SNew(SBox).WidthOverride(240.0f).MaxDesiredHeight(420.0f)
		[SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[SNew(SScrollBox) + SScrollBox::Slot()[Menu]]];
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

TSharedRef<SWidget> SMixtormat::BuildGeneratedContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RemoveGeneratedChild", "Remove Generated Mask"),
		LOCTEXT("RemoveGeneratedChildHint", "Remove this generated mask from the layer."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveGeneratedFromLayer(LayerIndex, ChildIndex);
		})));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedBlendModeMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
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
		MenuBuilder.AddMenuEntry(
			MixtormatUI::MaskBlendModeText(Mode),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				if (WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
					&& WorkingLayers[LayerIndex].Children[ChildIndex].Type
						== EMixtormatLayerChildType::Generated)
				{
					WorkingLayers[LayerIndex].Children[ChildIndex].Generated.BlendMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			})));
	}
	return MenuBuilder.MakeWidget();
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
