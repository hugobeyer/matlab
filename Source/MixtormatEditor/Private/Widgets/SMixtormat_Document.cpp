#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

// Material lifecycle: new/open/save, import, composition resolution, and baking.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::ImportSurfaces()
{
	const FMixtormatImportResult Result = FMixtormatSurfaceImporter::ImportFromDialog();
	if (!Result.bCancelled)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Result.ToMessage());
		RebuildCategoryList();
		RebuildSurfaceList();
	}
	return FReply::Handled();
}

FReply SMixtormat::ReimportShippedLibrary()
{
	const FMixtormatImportResult Result = FMixtormatSurfaceImporter::ReimportShippedLibrary();
	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildMaskList();
	WorkingStatusText = Result.Errors.IsEmpty()
		? FString::Printf(
			TEXT("Reimported shipped library (%d surface(s), %d mask(s), %d effect(s))"),
			Result.ImportedSurfaceCount,
			Result.ImportedMaskCount,
			Result.ImportedEffectCount)
		: TEXT("Reimport reported issues");
	FMessageDialog::Open(EAppMsgType::Ok, Result.ToMessage());
	return FReply::Handled();
}

FReply SMixtormat::StartNewMaterial()
{
	if (!SelectedPreviewMaterial.IsValid())
	{
		return FReply::Handled();
	}

	bHasWorkingMaterial = true;
	WorkingMaterialAsset.Reset();
	WorkingMaterialName = TEXT("Untitled MatLab Material");
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	WorkingLayers.Reset();

	FMixtormatLayer& BaseLayer = WorkingLayers.AddDefaulted_GetRef();
	BaseLayer.DisplayName = SelectedLibrarySurfaceName;
	BaseLayer.Type = EMixtormatLayerType::Material;
	BaseLayer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SelectedSurfacePath);
	BaseLayer.Tiling = CurrentTiling;
	BaseLayer.RoughnessBias = CurrentRoughnessBias;
	BaseLayer.RoughnessContrast = CurrentRoughnessContrast;
	BaseLayer.RoughnessOffset = CurrentRoughnessOffset;
	BaseLayer.NormalIntensity = CurrentNormalIntensity;

	SelectedLayerIndex = 0;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(SelectedLibrarySurfaceName);
	}
	SavedLayers.Reset();
	ResetEditHistory(false);
	bIsWorkingMaterialDirty = true;
	RefreshLayeredPreview(false);
	WorkingStatusText = TEXT("New material · unsaved");
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::NewWorkingMaterial()
{
	if (!ConfirmDiscardUnsavedChanges())
	{
		return FReply::Handled();
	}

	bHasWorkingMaterial = false;
	bHasSelectedLayer = false;
	bIsWorkingMaterialDirty = false;
	SelectedLayerIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	WorkingLayers.Reset();
	SavedLayers.Reset();
	WorkingMaterialAsset.Reset();
	ResetEditHistory(true);
	WorkingMaterialName = TEXT("No material");
	WorkingStatusText = TEXT("Select a library material, then drag it into Layers");
	if (bPreviewDisplacementEnabled)
	{
		PreviewSelectedSurfaceWithDisplacement();
	}
	else if (SelectedPreviewMaterial.IsValid())
	{
		for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
		{
			if (Viewport.IsValid())
			{
				Viewport->SetPreviewMaterial(SelectedPreviewMaterial.Get());
			}
		}
	}
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::OpenWorkingMaterial()
{
	if (!ConfirmDiscardUnsavedChanges())
	{
		return FReply::Handled();
	}

	FOpenAssetDialogConfig DialogConfig;
	DialogConfig.DialogTitleOverride = LOCTEXT("OpenMixtormatMaterial", "Open Mixtormat Material");
	DialogConfig.DefaultPath = TEXT("/Game");
	DialogConfig.AssetClassNames.Add(UMixtormatMaterial::StaticClass()->GetClassPathName());
	DialogConfig.bAllowMultipleSelection = false;

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TArray<FAssetData> Assets =
		ContentBrowserModule.Get().CreateModalOpenAssetDialog(DialogConfig);
	if (Assets.IsEmpty())
	{
		return FReply::Handled();
	}

	UMixtormatMaterial* MaterialAsset = Cast<UMixtormatMaterial>(Assets[0].GetAsset());
	if (!MaterialAsset || MaterialAsset->Layers.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("InvalidMixtormatMaterial", "The selected recipe has no base layer."));
		return FReply::Handled();
	}

	WorkingMaterialAsset.Reset(MaterialAsset);
	WorkingLayers = MaterialAsset->Layers;
	WorkingLayers[0].bEnabled = true;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	WorkingMaterialName = MaterialAsset->DisplayName.IsEmpty()
		? MaterialAsset->GetName()
		: MaterialAsset->DisplayName.ToString();
	bHasWorkingMaterial = true;
	SelectedLayerIndex = 0;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;

	if (const UMixtormatSurface* BaseSurface = WorkingLayers[0].SourceSurface.LoadSynchronous())
	{
		SelectedSurfacePath = WorkingLayers[0].SourceSurface.ToSoftObjectPath();
		SelectedLibrarySurfaceName = WorkingLayers[0].DisplayName;
		SelectedPreviewMaterial.Reset(Cast<UMaterialInstanceConstant>(BaseSurface->PreviewMaterial.Get()));
	}

	SyncSelectedLayerControls();
	ResetEditHistory(true);
	RefreshLayeredPreview(false);
	bIsWorkingMaterialDirty = false;
	WorkingStatusText = FString::Printf(TEXT("Opened %s"), *WorkingMaterialName);
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::SaveWorkingMaterial()
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}
	if (!WorkingMaterialAsset.IsValid())
	{
		return SaveWorkingMaterialAs();
	}

	UMixtormatMaterial* MaterialAsset = WorkingMaterialAsset.Get();
	MaterialAsset->Modify();
	MaterialAsset->DisplayName = FText::FromString(WorkingMaterialName);
	MaterialAsset->Layers = WorkingLayers;
	MaterialAsset->MarkPackageDirty();
	bool bSaved = false;
	if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
	{
		bSaved = AssetSubsystem->SaveLoadedAsset(MaterialAsset, false);
	}
	bIsWorkingMaterialDirty = !bSaved;
	if (bSaved)
	{
		SavedLayers = WorkingLayers;
		CurrentHistoryState.Layers = WorkingLayers;
	}
	WorkingStatusText = bSaved
		? FString::Printf(TEXT("Saved %s"), *WorkingMaterialName)
		: TEXT("Save failed");
	return FReply::Handled();
}

FReply SMixtormat::SaveWorkingMaterialAs()
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	FSaveAssetDialogConfig DialogConfig;
	DialogConfig.DialogTitleOverride = LOCTEXT("SaveMixtormatMaterialAs", "Save Mixtormat Material As");
	DialogConfig.DefaultPath = TEXT("/Game/MaterialLab/Materials");
	DialogConfig.DefaultAssetName = TEXT("MLM_Untitled");
	DialogConfig.AssetClassNames.Add(UMixtormatMaterial::StaticClass()->GetClassPathName());
	DialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const FString ObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(DialogConfig);
	if (ObjectPath.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
	UPackage* Package = CreatePackage(*PackageName);
	UMixtormatMaterial* MaterialAsset = Package
		? NewObject<UMixtormatMaterial>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional)
		: nullptr;
	if (!MaterialAsset)
	{
		WorkingStatusText = FString::Printf(TEXT("Save failed · %s"), *ObjectPath);
		return FReply::Handled();
	}
	FAssetRegistryModule::AssetCreated(MaterialAsset);

	WorkingMaterialAsset.Reset(MaterialAsset);
	WorkingMaterialName = MaterialAsset->GetName();
	MaterialAsset->Modify();
	MaterialAsset->DisplayName = FText::FromString(WorkingMaterialName);
	MaterialAsset->Layers = WorkingLayers;
	MaterialAsset->MarkPackageDirty();
	bool bSaved = false;
	if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
	{
		bSaved = AssetSubsystem->SaveLoadedAsset(MaterialAsset, false);
	}
	bIsWorkingMaterialDirty = !bSaved;
	if (bSaved)
	{
		SavedLayers = WorkingLayers;
		CurrentHistoryState.Layers = WorkingLayers;
	}
	WorkingStatusText = bSaved
		? FString::Printf(TEXT("Saved %s"), *WorkingMaterialName)
		: TEXT("Save failed");
	return FReply::Handled();
}

FReply SMixtormat::SetCompositionResolution(const int32 Resolution)
{
	if (Resolution == 1024 || Resolution == 2048 || Resolution == 4096)
	{
		CompositionResolution = Resolution;
		WorkingStatusText = FString::Printf(
			TEXT("Preview and bake resolution: %d × %d"),
			CompositionResolution,
			CompositionResolution);
		RefreshLayeredPreview(false);
	}
	FSlateApplication::Get().DismissAllMenus();
	return FReply::Handled();
}

FReply SMixtormat::BakeWorkingMaterial()
{
	if (!WorkingMaterialAsset.IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("SaveBeforeBake", "Save the Mixtormat recipe before baking."));
		return FReply::Handled();
	}

	const FSoftObjectPath RecipePath(WorkingMaterialAsset.Get());
	if (BakeSettingsRecipePath != RecipePath)
	{
		BakeSettingsRecipePath = RecipePath;
		BakeOutputBaseName = WorkingMaterialAsset->GetName();
		BakeDestinationPath = FString::Printf(
			TEXT("%s/Baked/%s"),
			*FPackageName::GetLongPackagePath(WorkingMaterialAsset->GetOutermost()->GetName()),
			*WorkingMaterialAsset->GetName());
	}

	TSharedPtr<SMixtormatBakeSettingsDialog> SettingsDialog;
	const TSharedRef<SWindow> SettingsWindow = SNew(SWindow)
		.Title(LOCTEXT("BakeSettingsTitle", "Bake Material"))
		.ClientSize(FVector2D(480.0f, 410.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(SettingsDialog, SMixtormatBakeSettingsDialog)
			.InitialSettings(FMixtormatBakeSettings{BakeDestinationPath, BakeOutputBaseName})
			.Resolution(CompositionResolution)
		];
	FSlateApplication::Get().AddModalWindow(
		SettingsWindow,
		FSlateApplication::Get().FindWidgetWindow(AsShared()),
		false);
	if (!SettingsDialog->WasAccepted())
	{
		WorkingStatusText = TEXT("Bake canceled");
		return FReply::Handled();
	}

	const FMixtormatBakeSettings Settings = SettingsDialog->GetSettings();
	BakeDestinationPath = Settings.DestinationPath;
	BakeOutputBaseName = Settings.BaseName;
	return ExecuteBake(Settings, true);
}

FReply SMixtormat::ExecuteBake(
	const FMixtormatBakeSettings& Settings,
	const bool bConfirmExistingOutputs)
{
	if (!WorkingMaterialAsset.IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("MissingRebakeRecipe", "Re-bake stopped because the recipe is no longer available."));
		return FReply::Handled();
	}

	const TArray<FString> ExistingOutputs =
		FMixtormatBakeService::FindExistingOutputObjectPaths(Settings);
	if (bConfirmExistingOutputs && !ExistingOutputs.IsEmpty())
	{
		const FText ConflictMessage = FText::Format(
			LOCTEXT(
				"BakeOutputConflict",
				"These outputs already exist:\n\n{0}\n\nUpdate them in place, or cancel without changes."),
			FText::FromString(FString::Join(ExistingOutputs, TEXT("\n"))));
		if (!ShowMixtormatActionDialog(
			AsShared(),
			LOCTEXT("BakeConflictTitle", "Existing Bake Outputs"),
			ConflictMessage,
			LOCTEXT("UpdateBakeOutputs", "Update Existing"),
			LOCTEXT("CancelBakeConflict", "Cancel")))
		{
			WorkingStatusText = TEXT("Bake canceled · existing outputs unchanged");
			return FReply::Handled();
		}
	}

	SaveWorkingMaterial();
	if (bIsWorkingMaterialDirty || PreviewViewports.IsEmpty() || !PreviewViewports[0].IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("BakeSaveFailed", "Bake stopped because the recipe could not be saved."));
		return FReply::Handled();
	}

	FMixtormatBakeResult Result;
	{
		TGuardValue<bool> BakingGuard(bIsBaking, true);
		FScopedSlowTask SlowTask(5.0f, LOCTEXT("BakeProgressTitle", "Baking Mixtormat outputs..."));
		SlowTask.MakeDialog(false);
		WorkingStatusText = TEXT("Bake · Compose");
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("BakeStageCompose", "Compose"));
		if (!PreviewViewports[0]->ComposeLayersAtResolution(WorkingLayers, CompositionResolution))
		{
			RefreshLayeredPreview(false);
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("BakeComposeFailed", "Compose failed. Check the current recipe and compositor output."));
			WorkingStatusText = TEXT("Bake failed · Compose");
			return FReply::Handled();
		}

		UTextureRenderTarget2D* BaseColor = PreviewViewports[0]->GetCompositedBaseColor();
		UTextureRenderTarget2D* Normal = PreviewViewports[0]->GetCompositedNormal();
		UTextureRenderTarget2D* RAM = PreviewViewports[0]->GetCompositedRAM();
		UTextureRenderTarget2D* Height = PreviewViewports[0]->GetCompositedHeight();
		if (!BaseColor || !Normal || !RAM || !Height)
		{
			RefreshLayeredPreview(false);
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("MissingBakeOutputs", "Readback cannot start because the GPU compositor has no valid outputs."));
			WorkingStatusText = TEXT("Bake failed · Readback");
			return FReply::Handled();
		}

		const auto ReportProgress = [this, &SlowTask](const EMixtormatBakeStage Stage)
		{
			FText StageText;
			switch (Stage)
			{
			case EMixtormatBakeStage::Readback:
				StageText = LOCTEXT("BakeStageReadback", "Readback");
				break;
			case EMixtormatBakeStage::CreateTextures:
				StageText = LOCTEXT("BakeStageCreateTextures", "Create Textures");
				break;
			case EMixtormatBakeStage::CreateMaterial:
				StageText = LOCTEXT("BakeStageCreateMaterial", "Create Material");
				break;
			default:
				StageText = LOCTEXT("BakeStageSave", "Save");
				break;
			}
			WorkingStatusText = FString::Printf(TEXT("Bake · %s"), *StageText.ToString());
			SlowTask.EnterProgressFrame(1.0f, StageText);
		};
		Result = FMixtormatBakeService::Bake(
			*WorkingMaterialAsset.Get(),
			*BaseColor,
			*Normal,
			*RAM,
			*Height,
			Settings,
			ReportProgress);
	}

	RefreshLayeredPreview(false);
	if (!Result.Succeeded())
	{
		FString ErrorText = TEXT("Bake failed:\n\n");
		for (const FText& Error : Result.Errors)
		{
			if (!ErrorText.EndsWith(TEXT("\n\n")))
			{
				ErrorText += TEXT("\n");
			}
			ErrorText += FString::Printf(TEXT("• %s"), *Error.ToString());
		}

		const auto AppendAssetPaths = [&ErrorText](
			const TCHAR* Heading,
			const TArray<FString>& AssetPaths)
		{
			if (AssetPaths.IsEmpty())
			{
				return;
			}
			ErrorText += FString::Printf(TEXT("\n\n%s\n"), Heading);
			for (const FString& AssetPath : AssetPaths)
			{
				ErrorText += FString::Printf(TEXT("• %s\n"), *AssetPath);
			}
		};
		AppendAssetPaths(TEXT("Created in memory:"), Result.CreatedAssetPaths);
		AppendAssetPaths(TEXT("Updated in memory:"), Result.UpdatedAssetPaths);
		AppendAssetPaths(TEXT("Saved successfully:"), Result.SavedAssetPaths);
		AppendAssetPaths(TEXT("Failed or incomplete:"), Result.FailedAssetPaths);

		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorText));
		WorkingStatusText = TEXT("Bake failed · review reported asset paths");
		return FReply::Handled();
	}

	const TArray<FString> OutputPaths = FMixtormatBakeService::GetOutputObjectPaths(Settings);
	const FText SuccessMessage = FText::Format(
		LOCTEXT(
			"BakeSucceeded",
			"Bake completed successfully:\n\n{0}"),
		FText::FromString(FString::Join(OutputPaths, TEXT("\n"))));
	WorkingStatusText = FString::Printf(
		TEXT("Baked %s · 5 outputs saved"),
		*Settings.BaseName);

	switch (ShowMixtormatBakeResultDialog(AsShared(), SuccessMessage))
	{
	case EMixtormatBakeResultAction::Reveal:
	{
		TArray<FAssetData> Assets;
		Assets.Reserve(5);
		Assets.Add(FAssetData(Result.BaseColor));
		Assets.Add(FAssetData(Result.Normal));
		Assets.Add(FAssetData(Result.RAM));
		Assets.Add(FAssetData(Result.Height));
		Assets.Add(FAssetData(Result.Material));
		FContentBrowserModule& ContentBrowserModule =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		WorkingStatusText += TEXT(" · revealed in Content Browser");
		break;
	}
	case EMixtormatBakeResultAction::Open:
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor
			? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
			: nullptr;
		if (!AssetEditorSubsystem || !AssetEditorSubsystem->OpenEditorForAsset(Result.Material))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("OpenBakedMaterialFailed", "Could not open {0}."),
					FText::FromString(Result.Material->GetPathName())));
			WorkingStatusText += TEXT(" · material instance could not be opened");
		}
		else
		{
			WorkingStatusText += TEXT(" · material instance opened");
		}
		break;
	}
	case EMixtormatBakeResultAction::Apply:
		ApplyBakedMaterialToSelectedActors(*Result.Material);
		break;
	case EMixtormatBakeResultAction::Rebake:
		return ExecuteBake(Settings, false);
	default:
		break;
	}
	return FReply::Handled();
}

void SMixtormat::ApplyBakedMaterialToSelectedActors(UMaterialInterface& Material)
{
	struct FMaterialAssignment
	{
		AActor* Actor = nullptr;
		UMeshComponent* Component = nullptr;
		int32 SlotIndex = INDEX_NONE;
	};

	USelection* SelectedActors = GEditor ? GEditor->GetSelectedActors() : nullptr;
	if (!SelectedActors)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("ApplyBakeNoSelection", "Select one or more actors with material slots first."));
		return;
	}

	TArray<FMaterialAssignment> Assignments;
	TSet<AActor*> TargetActors;
	int32 ReplacementCount = 0;
	int32 AlreadyAssignedCount = 0;
	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = Cast<AActor>(*SelectionIt);
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* Component : MeshComponents)
		{
			if (!IsValid(Component))
			{
				continue;
			}
			for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
			{
				UMaterialInterface* ExistingMaterial = Component->GetMaterial(SlotIndex);
				if (ExistingMaterial == &Material)
				{
					++AlreadyAssignedCount;
					continue;
				}
				ReplacementCount += ExistingMaterial ? 1 : 0;
				Assignments.Add({Actor, Component, SlotIndex});
				TargetActors.Add(Actor);
			}
		}
	}

	if (Assignments.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			AlreadyAssignedCount > 0
				? LOCTEXT("ApplyBakeAlreadyAssigned", "The baked material is already assigned to the selected actors.")
				: LOCTEXT("ApplyBakeNoSlots", "The selected actors have no compatible material slots."));
		return;
	}

	if (ReplacementCount > 0)
	{
		const FText Confirmation = FText::Format(
			LOCTEXT(
				"ConfirmReplaceSelectedMaterials",
				"Applying {0} will replace {1} existing material assignment(s) across {2} selected actor(s).\n\nCancel preserves every current assignment."),
			FText::FromString(Material.GetPathName()),
			FText::AsNumber(ReplacementCount),
			FText::AsNumber(TargetActors.Num()));
		if (!ShowMixtormatActionDialog(
			AsShared(),
			LOCTEXT("ReplaceSelectedMaterialsTitle", "Replace Existing Materials?"),
			Confirmation,
			LOCTEXT("ReplaceAndApplyBakedMaterial", "Replace and Apply"),
			LOCTEXT("CancelApplyBakedMaterial", "Cancel")))
		{
			WorkingStatusText = TEXT("Apply canceled · actor materials unchanged");
			return;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyBakedMaterialTransaction", "Apply Baked Material"));
	TSet<UMeshComponent*> ModifiedComponents;
	for (const FMaterialAssignment& Assignment : Assignments)
	{
		if (!ModifiedComponents.Contains(Assignment.Component))
		{
			Assignment.Component->Modify();
			ModifiedComponents.Add(Assignment.Component);
		}
		Assignment.Actor->Modify();
		Assignment.Component->SetMaterial(Assignment.SlotIndex, &Material);
	}
	for (UMeshComponent* Component : ModifiedComponents)
	{
		Component->PostEditChange();
	}
	for (AActor* Actor : TargetActors)
	{
		Actor->PostEditChange();
		Actor->MarkPackageDirty();
	}

	WorkingStatusText = FString::Printf(
		TEXT("Applied %s · %d slot(s) on %d actor(s) · Undo available"),
		*Material.GetName(),
		Assignments.Num(),
		TargetActors.Num());
}

bool SMixtormat::CanCloseTab()
{
	return ConfirmDiscardUnsavedChanges();
}

bool SMixtormat::ConfirmDiscardUnsavedChanges()
{
	if (!bIsWorkingMaterialDirty)
	{
		return true;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(
		EAppMsgType::YesNoCancel,
		LOCTEXT(
			"SaveUnsavedMaterialChanges",
			"Save changes to the current Mixtormat recipe?\n\nYes: Save\nNo: Discard\nCancel: Keep editing"));
	if (Choice == EAppReturnType::Yes)
	{
		SaveWorkingMaterial();
		return !bIsWorkingMaterialDirty;
	}
	return Choice == EAppReturnType::No;
}

#undef LOCTEXT_NAMESPACE
