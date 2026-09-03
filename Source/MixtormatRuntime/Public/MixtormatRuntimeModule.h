#pragma once

#include "Modules/ModuleManager.h"

class FMixtormatRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
