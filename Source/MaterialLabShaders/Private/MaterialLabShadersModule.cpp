#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FMaterialLabShadersModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MaterialLab"));
		check(Plugin.IsValid());
		AddShaderSourceDirectoryMapping(
			TEXT("/Plugin/MaterialLab"),
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
};

IMPLEMENT_MODULE(FMaterialLabShadersModule, MaterialLabShaders)
