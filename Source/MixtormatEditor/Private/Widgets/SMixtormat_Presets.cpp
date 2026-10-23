// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "Services/MixtormatPresetStore.h"
#include "Services/MixtormatRecipeService.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace MixtormatPresetUi
{
	bool ChooseJson(const TSharedRef<SWidget>& Parent, bool bSave, FString& OutFilename)
	{
		IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
		if (!Platform) { return false; }
		TArray<FString> Files;
		const void* Window = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(Parent);
		const FString Title = bSave ? LOCTEXT("ExportTypedPresetTitle", "Export Preset JSON").ToString() : LOCTEXT("ImportTypedPresetTitle", "Import Preset JSON").ToString();
		const bool bChosen = bSave
			? Platform->SaveFileDialog(Window, Title, FPaths::ProjectDir(), TEXT("Preset.json"), TEXT("JSON (*.json)|*.json"), EFileDialogFlags::None, Files)
			: Platform->OpenFileDialog(Window, Title, FPaths::ProjectDir(), FString(), TEXT("JSON (*.json)|*.json"), EFileDialogFlags::None, Files);
		if (!bChosen || Files.Num() != 1) { return false; }
		OutFilename = Files[0];
		return true;
	}

	TSharedRef<SWidget> NameMenu(const FString& InitialName,
		TFunction<bool(const FString&, const FString&, const FString&, FText&)> Save)
	{
		struct FEntry { FString Name, Category, Description; FText Error; };
		const TSharedRef<FEntry> State = MakeShared<FEntry>();
		State->Name = InitialName;
		const auto Commit = [State, Save]()
		{
			State->Error = FText::GetEmpty();
			if (Save(State->Name.TrimStartAndEnd(), State->Category.TrimStartAndEnd(), State->Description, State->Error))
			{ FSlateApplication::Get().DismissAllMenus(); }
		};
		MixtormatMenu::FBuilder Menu;
		Menu.Caption(LOCTEXT("PresetNameEntryHeading", "SAVE USER PRESET"));
		const auto AddField = [&Menu, State, Commit](const FText& Hint, FString FEntry::* Member)
		{
			Menu.Widget(SNew(SMixtormatTextFieldGradient)
			[
				SNew(SEditableTextBox)
				.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
				.HintText(Hint)
				.Text(FText::FromString(State.Get().*Member))
				.OnTextChanged_Lambda([State, Member](const FText& Text) { State.Get().*Member = Text.ToString(); })
				.OnTextCommitted_Lambda([Commit](const FText&, ETextCommit::Type Type) { if (Type == ETextCommit::OnEnter) { Commit(); } })
			]);
		};
		AddField(LOCTEXT("TypedPresetName", "Name"), &FEntry::Name);
		AddField(LOCTEXT("TypedPresetCategory", "Category (optional)"), &FEntry::Category);
		AddField(LOCTEXT("TypedPresetDescription", "Description (optional)"), &FEntry::Description);
		Menu.Widget(SNew(STextBlock).Text_Lambda([State]() { return State->Error; }).AutoWrapText(true)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel"))));
		Menu.Caption(LOCTEXT("TypedPresetCommitHelp", "Enter to save as a new preset. Choose a unique name."));
		return Menu.Build();
	}
}

TSharedRef<SWidget> SMixtormat::MakeModulePresetButton()
{
	return SNew(SMixtormatHelp)
		.Text(LOCTEXT("ModulePresetHelp", "Parameter-only presets. IDs, connections, bindings and scoped modules stay on the destination. Bound parameters remain bound. Skin also retains Amount, channel write flags and its enable state."))
		[
			SNew(SComboButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
			.ContentPadding(FMargin(0.0f)).HasDownArrow(false)
			.OnGetMenuContent(this, &SMixtormat::BuildModulePresetMenu)
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("ModulePresets", "Presets"))
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildModulePresetMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	Menu.Caption(LOCTEXT("ModulePresetHeading", "MODULE PRESETS"));
	if (!Child) { Menu.Caption(LOCTEXT("ModulePresetMissing", "No module selected.")); return Menu.Build(); }
	const FMixtormatLayerChild Expected = *Child;
	const bool bEditable = !Child->IsInstance();
	const FText Restriction = LOCTEXT("ModulePresetInstance", "Instances inherit settings. Edit the source module instead.");
	Menu.SubMenu(LOCTEXT("SaveModulePreset", "Save Preset…"), nullptr,
		FOnGetContent::CreateLambda([this, Address]()
		{
			const FMixtormatLayerChild* Current = ResolveChildAt(Address);
			return MixtormatPresetUi::NameMenu(Current ? Current->DisplayName.ToString() : FString(),
				[this, Address](const FString& Name, const FString& Category, const FString& Description, FText& Error)
				{
					const FMixtormatLayerChild* Source = ResolveChildAt(Address);
					if (!Source) { Error = LOCTEXT("PresetSourceGone", "The source module no longer exists."); return false; }
					MixtormatPresetStore::FModulePreset Preset;
					if (!MixtormatPresetStore::CaptureModule(*Source, Preset, Error)) { return false; }
					Preset.Name = Name; Preset.Category = Category; Preset.Description = Description;
					return MixtormatPresetStore::SaveModule(Preset, Error);
				});
		})).Enabled(bEditable).ToolTip(bEditable
				? LOCTEXT("SaveModulePresetHelp", "Save local parameter settings only. Connections, bindings and scoped child modules are not copied.") : Restriction);
	const bool bDialogs = FDesktopPlatformModule::Get() != nullptr;
	Menu.Item(LOCTEXT("ImportModulePreset", "Import JSON…"), nullptr,
		FSimpleDelegate::CreateLambda([this, Expected]()
		{
			FString Filename;
			if (!MixtormatPresetUi::ChooseJson(AsShared(), false, Filename)) { return; }
			FText Error;
			WorkingStatusText = MixtormatPresetStore::ImportModule(Filename, Expected, Error)
				? LOCTEXT("ModulePresetImported", "Imported module preset").ToString() : Error.ToString();
		})).Enabled(bDialogs).ToolTip(LOCTEXT("ModuleImportHelp", "Import compatible JSON as a new user preset. Requires platform file dialogs; does not edit the module."));
	Menu.Item(LOCTEXT("ExportModulePreset", "Export Current JSON…"), nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			const FMixtormatLayerChild* Source = ResolveChildAt(Address);
			MixtormatPresetStore::FModulePreset Preset;
			FText Error;
			if (!Source) { WorkingStatusText = LOCTEXT("PresetSourceGone", "The source module no longer exists.").ToString(); return; }
						if (!MixtormatPresetStore::CaptureModule(*Source, Preset, Error)) { WorkingStatusText = Error.ToString(); return; }
			FString Filename;
			if (!MixtormatPresetUi::ChooseJson(AsShared(), true, Filename)) { return; }
			Preset.Name = FPaths::GetBaseFilename(Filename);
			WorkingStatusText = MixtormatPresetStore::ExportModule(Filename, Preset, Error)
				? LOCTEXT("ModulePresetExported", "Exported module preset").ToString() : Error.ToString();
		})).Enabled(bDialogs && bEditable).ToolTip(LOCTEXT("ModuleExportHelp", "Export this module's local parameter settings to a new JSON file. Instances must be edited at their source. Requires platform file dialogs."));
	Menu.Separator();
	TArray<MixtormatPresetStore::FModulePreset> Presets;
	FText Error;
	MixtormatPresetStore::ListModules(Expected, Presets, Error);
	if (!Error.IsEmpty()) { Menu.Caption(Error); }
	Menu.SubMenu(LOCTEXT("OverwriteModulePreset", "Overwrite Preset…"), nullptr,
		FOnGetContent::CreateLambda([this, Address, Presets]()
		{
			MixtormatMenu::FBuilder OverwriteMenu;
			OverwriteMenu.Caption(LOCTEXT("OverwriteModuleHeading", "REPLACE WITH CURRENT SETTINGS"));
			for (const MixtormatPresetStore::FModulePreset& Preset : Presets)
			{
				OverwriteMenu.Item(FText::FromString(Preset.Name), nullptr,
					FSimpleDelegate::CreateLambda([this, Address, Preset]()
					{
						const FMixtormatLayerChild* Source = ResolveChildAt(Address);
						if (!Source) { WorkingStatusText = LOCTEXT("PresetSourceGone", "The source module no longer exists.").ToString(); return; }
						MixtormatPresetStore::FModulePreset Current;
						FText Issue;
						if (!MixtormatPresetStore::CaptureModule(*Source, Current, Issue)
							|| !MixtormatPresetStore::OverwriteModule(Preset, Current, Issue))
						{ WorkingStatusText = Issue.ToString(); return; }
						WorkingStatusText = FString::Printf(TEXT("Overwrote preset %s"), *Preset.Name);
					})).ToolTip(LOCTEXT("OverwriteModuleEntryHelp", "Replace this preset with the current module settings. Saved in your user preset library."));
			}
			return OverwriteMenu.Build();
		})).Enabled(bEditable && !Presets.IsEmpty()).ToolTip(bEditable
			? LOCTEXT("OverwriteModulePresetHelp", "Replace any listed preset, including built-in looks, with current settings.") : Restriction);
	Menu.Separator();
	for (const MixtormatPresetStore::FModulePreset& Preset : Presets)
	{
		const FText Label = FText::FromString(Preset.Name);
		Menu.Item(Label, nullptr, FSimpleDelegate::CreateLambda([this, Address, Preset]()
		{
			MixtormatPresetStore::FModulePreset Loaded = Preset;
			FText Issue;
			FMixtormatLayerChild* Destination = ResolveChildAt(Address);
			if (!Destination) { WorkingStatusText = LOCTEXT("PresetDestinationGone", "The destination module no longer exists.").ToString(); return; }
			if (!Preset.bBuiltIn && !MixtormatPresetStore::LoadModule(Preset.Filename, Loaded, Issue))
			{ WorkingStatusText = Issue.ToString(); return; }
			FMixtormatLayerChild Replacement = *Destination;
			if (!MixtormatPresetStore::ApplyModule(Loaded, Replacement, Issue)) { WorkingStatusText = Issue.ToString(); return; }
			RecordEditHistory();
			LastHistoryRecordTime = 0.0;
			*Destination = MoveTemp(Replacement);
			SyncChildInstances();
			RecordEditHistory();
			LastHistoryRecordTime = 0.0;
			bIsWorkingMaterialDirty = !IsCurrentStateSaved();
			SyncSelectedLayerControls();
			RefreshLayeredPreview(false);
			RebuildLayerList();
			WorkingStatusText = FString::Printf(TEXT("Applied preset %s; destination bindings retained"), *Loaded.Name);
		})).Enabled(bEditable).ToolTip(bEditable ? FText::FromString(Preset.Description) : Restriction);
	}
	if (Presets.IsEmpty()) { Menu.Caption(LOCTEXT("ModulePresetsEmpty", "No compatible saved presets.")); }
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildRecipeCaptureMenu(const FGuid GroupId)
{
	MixtormatPresetStore::FRecipePreset Snapshot;
	FText Error;
	if (!MixtormatRecipeService::Capture(WorkingLayers, WorkingLayerGroups, GroupId, Snapshot, Error))
	{
		MixtormatMenu::FBuilder Menu; Menu.Caption(Error); return Menu.Build();
	}
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("RecipeDependenciesHeading", "REFERENCED ASSETS — NOT EMBEDDED"));
	if (Snapshot.Dependencies.IsEmpty()) { Menu.Caption(LOCTEXT("RecipeNoAssets", "No asset dependencies.")); }
	for (const FSoftObjectPath& Asset : Snapshot.Dependencies) { Menu.Caption(FText::FromString(Asset.ToString())); }
	Menu.Caption(LOCTEXT("RecipeGlobalContract", "Captures only this group. Destination global/final settings are not imported."));
		Menu.Caption(LOCTEXT("RecipeLabelContract", "Names are saved as displayed text, not live localization/string-table links."));
	Menu.Widget(MixtormatPresetUi::NameMenu(Snapshot.Name,
		[this, Snapshot](const FString& Name, const FString& Category, const FString& Description, FText& Issue)
		{
			// Save exactly the snapshot whose dependencies were shown above.
			MixtormatPresetStore::FRecipePreset Recipe = Snapshot;
			Recipe.Name = Name; Recipe.Category = Category; Recipe.Description = Description;
			if (!MixtormatPresetStore::SaveRecipe(Recipe, Issue)) { return false; }
			WorkingStatusText = FString::Printf(TEXT("Saved recipe %s"), *Recipe.Name);
			RefreshRecipeGallery();
			return true;
		}));
	return Menu.Build();
}

FReply SMixtormat::InsertRecipe(const FString& Filename)
{
	if (!bHasWorkingMaterial)
	{
		WorkingStatusText = LOCTEXT("RecipeNeedsDocument", "Create or open a material before inserting a recipe.").ToString();
		return FReply::Handled();
	}
	MixtormatPresetStore::FRecipePreset Loaded, Copy;
	FText Error;
	// Append keeps existing layer indices and contiguous group membership unchanged.
	if (!MixtormatPresetStore::LoadRecipe(Filename, Loaded, Error)
		|| !MixtormatRecipeService::PrepareInsertion(Loaded, WorkingLayers.Num(), Copy, Error))
	{
		WorkingStatusText = Error.ToString();
		return FReply::Handled();
	}
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	const FGuid NewGroupId = Copy.Group.GroupId;
	WorkingLayers.Append(MoveTemp(Copy.Layers));
	WorkingLayerGroups.Add(MoveTemp(Copy.Group));
	SyncChildInstances();
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	SoloLayerIndex = INDEX_NONE;
	SelectedLayerChildIds.Reset();
	SelectionAnchorLayerChildId.Invalidate();
	SelectedGroupChildIds.Reset();
	SelectionAnchorGroupChildId.Invalidate();
	SelectLayerGroup(NewGroupId);
	RefreshLayeredPreview(false);
	RebuildLayerList();
	RebuildMaskList();
	WorkingStatusText = FString::Printf(TEXT("Inserted editable recipe %s"), *Loaded.Name);
	return FReply::Handled();
}

void SMixtormat::ImportRecipeJson()
{
	FString Filename;
	if (!MixtormatPresetUi::ChooseJson(AsShared(), false, Filename)) { return; }
	FText Error;
	if (!MixtormatPresetStore::ImportRecipe(Filename, Error)) { WorkingStatusText = Error.ToString(); return; }
	RefreshRecipeGallery();
	WorkingStatusText = LOCTEXT("RecipeImported", "Imported user recipe").ToString();
}

TSharedRef<SWidget> SMixtormat::BuildRecipeGalleryActions()
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(LOCTEXT("RecipeImport", "Import Recipe JSON…"), nullptr, FSimpleDelegate::CreateSP(this, &SMixtormat::ImportRecipeJson))
		.Enabled(FDesktopPlatformModule::Get() != nullptr)
		.ToolTip(LOCTEXT("RecipeImportHint", "Validate and save a new user recipe. Outside references and missing assets are rejected. Requires platform file dialogs."));
	Menu.Item(LOCTEXT("RecipeRefresh", "Refresh Recipes"), nullptr, FSimpleDelegate::CreateSP(this, &SMixtormat::RefreshRecipeGallery));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildRecipeEntryMenu(FString Filename)
{
	MixtormatMenu::FBuilder Menu;
	MixtormatPresetStore::FRecipePreset Recipe;
	FText Error;
	if (!MixtormatPresetStore::LoadRecipe(Filename, Recipe, Error)) { Menu.Caption(Error); return Menu.Build(); }
	Menu.Caption(FText::FromString(Recipe.Name));
	if (!Recipe.Description.IsEmpty()) { Menu.Caption(FText::FromString(Recipe.Description)); }
	Menu.Item(LOCTEXT("InsertEditableRecipe", "Insert Editable Group"), nullptr,
		FSimpleDelegate::CreateLambda([this, Filename]() { InsertRecipe(Filename); }))
		.Enabled(bHasWorkingMaterial).ToolTip(LOCTEXT("RecipeInsertHelp", "Append a new independent editable group. Create or open a material first. All document/global settings stay unchanged."));
	Menu.SubMenu(LOCTEXT("CopyRecipeToUser", "Save User Copy…"), nullptr,
		FOnGetContent::CreateLambda([this, Recipe]()
		{
			return MixtormatPresetUi::NameMenu(Recipe.Name,
				[this, Recipe](const FString& Name, const FString& Category, const FString& Description, FText& Issue) mutable
				{
					Recipe.Id = FGuid::NewGuid(); Recipe.Name = Name; Recipe.Category = Category; Recipe.Description = Description;
					Recipe.bShipped = false;
					if (!MixtormatPresetStore::SaveRecipe(Recipe, Issue)) { return false; }
					RefreshRecipeGallery(); return true;
				});
		}));
	Menu.Item(LOCTEXT("ExportRecipeJson", "Export JSON…"), nullptr,
		FSimpleDelegate::CreateLambda([this, Recipe]()
		{
			FString Destination;
			if (!MixtormatPresetUi::ChooseJson(AsShared(), true, Destination)) { return; }
			FText Issue;
			WorkingStatusText = MixtormatPresetStore::ExportRecipe(Destination, Recipe, Issue)
				? LOCTEXT("RecipeExported", "Exported recipe JSON").ToString() : Issue.ToString();
		})).Enabled(FDesktopPlatformModule::Get() != nullptr)
		.ToolTip(LOCTEXT("RecipeExportHint", "Export to a new JSON file. Referenced assets are not embedded. Shipped entries are never changed. Requires platform file dialogs."));
	Menu.Caption(LOCTEXT("RecipeAssets", "REFERENCED ASSETS — NOT EMBEDDED"));
	for (const FSoftObjectPath& Asset : Recipe.Dependencies) { Menu.Caption(FText::FromString(Asset.ToString())); }
	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
