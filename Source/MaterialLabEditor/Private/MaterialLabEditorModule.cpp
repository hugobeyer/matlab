#include "MaterialLabEditorModule.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Style/MaterialLabStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMaterialLab.h"

#define LOCTEXT_NAMESPACE "MaterialLabEditorModule"

const FName FMaterialLabEditorModule::MaterialLabTabName(TEXT("MaterialLab"));

void FMaterialLabEditorModule::StartupModule()
{
	FMaterialLabStyle::Initialize();

	FGlobalTabmanager::Get()->RegisterTabSpawner(
			MaterialLabTabName,
			FOnSpawnTab::CreateRaw(this, &FMaterialLabEditorModule::SpawnMaterialLabTab))
			.SetDisplayName(LOCTEXT("MaterialLabTabTitle", "Material Lab"))
			.SetTooltipText(LOCTEXT("MaterialLabTabTooltip", "Open the Material Lab workspace."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMaterialLabEditorModule::RegisterMenus));
}

void FMaterialLabEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MaterialLabTabName);
	FMaterialLabStyle::Shutdown();
}

void FMaterialLabEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
	FToolMenuSection& Section = WindowMenu->FindOrAddSection(TEXT("WindowLayout"));

	Section.AddMenuEntry(
			TEXT("OpenMaterialLab"),
			LOCTEXT("OpenMaterialLabLabel", "Material Lab"),
			LOCTEXT("OpenMaterialLabTooltip", "Open the Material Lab workspace."),
			FSlateIcon(FMaterialLabStyle::GetStyleSetName(), TEXT("MaterialLab.Icon.Globe")),
			FUIAction(FExecuteAction::CreateRaw(this, &FMaterialLabEditorModule::OpenMaterialLabTab)));

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& ToolBarSection = ToolBar->FindOrAddSection("PluginTools");
	ToolBarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("OpenMaterialLab"),
			FUIAction(FExecuteAction::CreateRaw(this, &FMaterialLabEditorModule::OpenMaterialLabTab)),
			LOCTEXT("MaterialLabToolbarLabel", "Material Lab"),
			LOCTEXT("MaterialLabToolbarTooltip", "Open the Material Lab workspace."),
			FSlateIcon(FMaterialLabStyle::GetStyleSetName(), TEXT("MaterialLab.Icon.Globe"))));
}

void FMaterialLabEditorModule::OpenMaterialLabTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(MaterialLabTabName);
}

TSharedRef<SDockTab> FMaterialLabEditorModule::SpawnMaterialLabTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SMaterialLab> MaterialLab = SNew(SMaterialLab);
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			MaterialLab
		];
	Tab->SetCanCloseTab(SDockTab::FCanCloseTab::CreateSP(MaterialLab, &SMaterialLab::CanCloseTab));
	return Tab;
}

IMPLEMENT_MODULE(FMaterialLabEditorModule, MaterialLabEditor)

#undef LOCTEXT_NAMESPACE
