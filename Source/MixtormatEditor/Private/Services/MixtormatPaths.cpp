#include "Services/MixtormatPaths.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace
{
	const FName CurrentProductName(TEXT("Mixtormat"));
	const FName CurrentPluginName(TEXT("Mixtormat"));

	FString PackageChild(const FString& Root, const FString& Child)
	{
		return Root + TEXT("/") + Child;
	}

	FString ObjectPath(const FString& PackageRoot, const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *PackageRoot, AssetName, AssetName);
	}
}

FName FMixtormatPaths::ProductName()
{
	return CurrentProductName;
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

FString FMixtormatPaths::ProjectLibraryRoot()
{
	return TEXT("/Game/Mixtormat/Library");
}

FString FMixtormatPaths::ProjectLibrarySurfacesRoot()
{
	return PackageChild(ProjectLibraryRoot(), TEXT("Surfaces"));
}

FString FMixtormatPaths::ProjectLibrarySurfaceFamilyRoot(const FString& Family)
{
	return PackageChild(ProjectLibrarySurfacesRoot(), Family);
}

FString FMixtormatPaths::ProjectLibraryRawTextureFamilyRoot(const FString& Family)
{
	return PackageChild(PackageChild(PackageChild(ProjectLibraryRoot(), TEXT("Textures")), Family), TEXT("Raw"));
}

FString FMixtormatPaths::ProjectLibrarySurfaceThumbnailFamilyRoot(const FString& Family)
{
	return PackageChild(PackageChild(PackageChild(ProjectLibraryRoot(), TEXT("Thumbnails")), TEXT("Surfaces")), Family);
}

FString FMixtormatPaths::ProjectLibraryMasksRoot()
{
	return PackageChild(ProjectLibraryRoot(), TEXT("Masks"));
}

FString FMixtormatPaths::ProjectLibraryMaskThumbnailsRoot()
{
	return PackageChild(PackageChild(ProjectLibraryRoot(), TEXT("Thumbnails")), TEXT("Masks"));
}

FString FMixtormatPaths::ProjectLibraryMaterialInstanceFamilyRoot(const FString& Family)
{
	return PackageChild(PackageChild(PackageChild(ProjectLibraryRoot(), TEXT("Materials")), TEXT("Instances")), Family);
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

FString FMixtormatPaths::ThumbnailsRoot()
{
	return PackageChild(PluginContentRoot(), TEXT("Thumbnails"));
}

FString FMixtormatPaths::SurfaceThumbnailsRoot()
{
	return PackageChild(ThumbnailsRoot(), TEXT("Surfaces"));
}

FString FMixtormatPaths::SurfaceThumbnailFamilyRoot(const FString& Family)
{
	return PackageChild(SurfaceThumbnailsRoot(), Family);
}

FString FMixtormatPaths::MaskThumbnailsRoot()
{
	return PackageChild(ThumbnailsRoot(), TEXT("Masks"));
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
	return ObjectPath(MaterialsRoot(), TEXT("M_Mixtormat_Substrate"));
}

FString FMixtormatPaths::PreviewMaterialObjectPath()
{
	return ObjectPath(MaterialsRoot(), TEXT("MI_Mixtormat_Preview"));
}

FString FMixtormatPaths::StudioFloorMaterialObjectPath()
{
	return ObjectPath(MaterialsRoot(), TEXT("MI_Mixtormat_StudioFloor"));
}

FString FMixtormatPaths::SphereMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_Mixtormat_Sphere"));
}

FString FMixtormatPaths::PlaneMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_Mixtormat_Plane"));
}

FString FMixtormatPaths::CubeMeshObjectPath()
{
	return ObjectPath(MeshesRoot(), TEXT("SM_Mixtormat_Cube"));
}

FString FMixtormatPaths::ProjectMaterialsRoot()
{
	return TEXT("/Game/Mixtormat/Materials");
}

FString FMixtormatPaths::LiveThemePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), CurrentProductName.ToString(), TEXT("LiveTheme.json"));
}
