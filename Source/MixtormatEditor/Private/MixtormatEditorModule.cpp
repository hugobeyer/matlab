// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatEditorModule.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatAssetMigration.h"
#include "Style/MixtormatStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMixtormat.h"

DEFINE_LOG_CATEGORY(LogMixtormat);

#define LOCTEXT_NAMESPACE "MixtormatEditorModule"

const FName FMixtormatEditorModule::MixtormatTabName(TEXT("Mixtormat"));

void FMixtormatEditorModule::StartupModule()
{
	FMixtormatStyle::Initialize();

	// Nomad, not the base RegisterTabSpawner: this is a standalone workspace window that docks
	// anywhere, and only the nomad registration is matched by the UnregisterNomadTabSpawner in
	// ShutdownModule. Registered one way and unregistered the other, the spawner outlived the
	// module that owned it and pointed at freed memory after a Live Coding reload.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			MixtormatTabName,
			FOnSpawnTab::CreateRaw(this, &FMixtormatEditorModule::SpawnMixtormatTab))
			.SetDisplayName(LOCTEXT("MixtormatTabTitle", "Mixtormat"))
			.SetTooltipText(LOCTEXT("MixtormatTabTooltip", "Open the Mixtormat workspace."))
			.SetIcon(FSlateIcon(FMixtormatStyle::GetStyleSetName(), TEXT("Mixtormat.Brand.Icon")))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMixtormatEditorModule::RegisterMenus));

	AssetMigrationCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Mixtormat.MigrateAssets"),
		TEXT("Preview Mixtormat asset renames. Pass Apply to execute the validated batch."),
		FConsoleCommandWithArgsDelegate::CreateRaw(
			this,
			&FMixtormatEditorModule::RunAssetMigrationCommand),
		ECVF_Default);

	RegisterSettings();
}

void FMixtormatEditorModule::ShutdownModule()
{
	UnregisterSettings();

	if (AssetMigrationCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AssetMigrationCommand);
		AssetMigrationCommand = nullptr;
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MixtormatTabName);
	FMixtormatStyle::Shutdown();
}

// UMixtormatEditorSettings already declares Config=EditorPerProjectUserSettings and overrides
// GetCategoryName()/GetSectionName() for "Plugins"/"Mixtormat", which is enough for UDeveloperSettings
// reflection-based discovery in most cases -- but that discovery is not guaranteed for a
// plugin-owned settings class, so this registers the same CDO explicitly to make the placement at
// Editor Preferences > Plugins > Mixtormat deterministic rather than incidental.
void FMixtormatEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			TEXT("Editor"),
			TEXT("Plugins"),
			TEXT("Mixtormat"),
			LOCTEXT("MixtormatSettingsName", "Mixtormat"),
			LOCTEXT("MixtormatSettingsDescription", "Editor preferences for Mixtormat debug visualization."),
			GetMutableDefault<UMixtormatEditorSettings>());
	}
}

void FMixtormatEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings(TEXT("Editor"), TEXT("Plugins"), TEXT("Mixtormat"));
	}
}

void FMixtormatEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
	FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));

	Section.AddMenuEntry(
			TEXT("OpenMixtormat"),
			LOCTEXT("OpenMixtormatLabel", "Mixtormat"),
			LOCTEXT("OpenMixtormatTooltip", "Open the Mixtormat workspace."),
			FSlateIcon(FMixtormatStyle::GetStyleSetName(), TEXT("Mixtormat.Brand.Icon")),
			FUIAction(FExecuteAction::CreateRaw(this, &FMixtormatEditorModule::OpenMixtormatTab)));

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& ToolBarSection = ToolBar->FindOrAddSection("PluginTools");
	ToolBarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("OpenMixtormat"),
			FUIAction(FExecuteAction::CreateRaw(this, &FMixtormatEditorModule::OpenMixtormatTab)),
			LOCTEXT("MixtormatToolbarLabel", "Mixtormat"),
			LOCTEXT("MixtormatToolbarTooltip", "Open the Mixtormat workspace."),
			FSlateIcon(FMixtormatStyle::GetStyleSetName(), TEXT("Mixtormat.Brand.Icon"))));
}

void FMixtormatEditorModule::OpenMixtormatTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(MixtormatTabName);
}

void FMixtormatEditorModule::RunAssetMigrationCommand(const TArray<FString>& Args)
{
	const bool bApply = Args.Num() == 1 && Args[0].Equals(TEXT("Apply"), ESearchCase::IgnoreCase);
	if (!Args.IsEmpty() && !bApply)
	{
		UE_LOG(LogMixtormat, Error,
			TEXT("Usage: Mixtormat.MigrateAssets [Apply]. Without Apply, the command is a dry run."));
		return;
	}

	FMixtormatAssetMigration::Run(bApply);
}

TSharedRef<SDockTab> FMixtormatEditorModule::SpawnMixtormatTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SMixtormat> Mixtormat = SNew(SMixtormat);
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			Mixtormat
		];
	Tab->SetCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Mixtormat, &SMixtormat::CanCloseTab));
	return Tab;
}

IMPLEMENT_MODULE(FMixtormatEditorModule, MixtormatEditor)

#undef LOCTEXT_NAMESPACE
