// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

// One category for the editor module. LogTemp is not acceptable in a shipping plugin: it gives
// users no way to filter or silence Mixtormat output, and Epic's plugin guidelines call it out.
// Asset migration keeps its own narrower category -- see LogMixtormatAssetMigration.
MIXTORMATEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogMixtormat, Log, All);

class FSpawnTabArgs;
class IConsoleObject;
class SDockTab;

class FMixtormatEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenMixtormatTab();
	void RunAssetMigrationCommand(const TArray<FString>& Args);
	TSharedRef<SDockTab> SpawnMixtormatTab(const FSpawnTabArgs& SpawnTabArgs);
	void RegisterSettings();
	void UnregisterSettings();

	IConsoleObject* AssetMigrationCommand = nullptr;
	static const FName MixtormatTabName;
};
