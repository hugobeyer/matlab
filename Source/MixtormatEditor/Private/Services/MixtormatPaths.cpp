#include "Services/MixtormatPaths.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace
{
	const FName CurrentPluginName(TEXT("MaterialLab"));

	FString PackageChild(const FString& Root, const FString& Child)
	{
		return Root + TEXT("/") + Child;
	}

	FString ObjectPath(const FString& PackageRoot, const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *PackageRoot, AssetName, AssetName);
	}
}

FName FMixtormatPaths::PluginName()
{
	return CurrentPluginName;
}

TSharedPtr<IPlugin> FMixtormatPaths::FindPlugin()
{
	return IPluginManager::Get().FindPlugin(CurrentPluginName.ToString());
}

FString FMixtormatPaths::PluginBaseDir()
{
	const TSharedPtr<IPlugin> Plugin = FindPlugin();
	return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
}

FString FMixtormatPaths::ResourcesDir()
{
	const FString BaseDir = PluginBaseDir();
	return BaseDir.IsEmpty() ? FString() : FPaths::Combine(BaseDir, TEXT("Resources"));
}

FString FMixtormatPaths::SourceTexturesDir()
{
	const FString BaseDir = PluginBaseDir();
	return BaseDir.IsEmpty() ? FString() : FPaths::Combine(BaseDir, TEXT("Content/Textures"));
}

FString FMixtormatPaths::PluginContentRoot()
{
	return TEXT("/") + CurrentPluginName.ToString();
}

FString FMixtormatPaths::SurfacesRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Surfaces"));
}

FString FMixtormatPaths::SurfaceFamilyRoot(const FString& Family)
{
	return PackageChild(SurfacesRoot(), Family);
}

FString FMixtormatPaths::TexturesRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Textures"));
}

FString FMixtormatPaths::RawTextureFamilyRoot(const FString& Family)
{
	return PackageChild(PackageChild(TexturesRoot(), Family), TEXT("Raw"));
}

FString FMixtormatPaths::MasksRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Masks"));
}

FString FMixtormatPaths::NormalsRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Normals"));
}

FString FMixtormatPaths::NormalCategoryRoot(const FString& Category)
{
	return PackageChild(NormalsRoot(), Category);
}

FString FMixtormatPaths::EffectsRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Effects"));
}

FString FMixtormatPaths::LightingRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Lighting"));
}

FString FMixtormatPaths::MaterialsRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Materials"));
}

FString FMixtormatPaths::MaterialInstanceFamilyRoot(const FString& Family)
{
	return PackageChild(PackageChild(MaterialsRoot(), TEXT("Instances")), Family);
}

FString FMixtormatPaths::MeshesRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Meshes"));
}

FString FMixtormatPaths::MasterMaterialObjectPath()
{
	return ObjectPath(MaterialsRoot(), TEXT("M_MaterialLab_Substrate"));
}

FString FMixtormatPaths::StudioFloorMaterialObjectPath()
{
	return ObjectPath(MaterialsRoot(), TEXT("MI_ML_Studio_Floor"));
}

FString FMixtormatPaths::SphereMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_MaterialLab_Sphere"));
}

FString FMixtormatPaths::PlaneMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_MaterialLab_Plane"));
}

FString FMixtormatPaths::CubeMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_MaterialLab_Cube"));
}

FString FMixtormatPaths::ProjectMaterialsRoot()
{
	return TEXT("/Game/MaterialLab/Materials");
}

FString FMixtormatPaths::LiveThemePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), CurrentPluginName.ToString(), TEXT("LiveTheme.json"));
}
