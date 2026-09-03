#pragma once

#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class FMixtormatEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenMixtormatTab();
	TSharedRef<SDockTab> SpawnMixtormatTab(const FSpawnTabArgs& SpawnTabArgs);

	static const FName MixtormatTabName;
};
