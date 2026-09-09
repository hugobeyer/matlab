#pragma once

#include "Modules/ModuleManager.h"

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

	IConsoleObject* AssetMigrationCommand = nullptr;
	static const FName MixtormatTabName;
};
