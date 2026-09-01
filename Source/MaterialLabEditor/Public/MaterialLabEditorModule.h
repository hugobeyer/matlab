#pragma once

#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;

class FMaterialLabEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenMaterialLabTab();
	TSharedRef<SDockTab> SpawnMaterialLabTab(const FSpawnTabArgs& SpawnTabArgs);

	static const FName MaterialLabTabName;
};
