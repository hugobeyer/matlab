#pragma once

#include "CoreMinimal.h"

class IPlugin;

// Product-facing paths are canonical Mixtormat now. Plugin package paths intentionally remain
// MaterialLab until the atomic plugin/content migration.
struct FMixtormatPaths final
{
	static FName ProductName();
	static FName PluginName();
	static TSharedPtr<IPlugin> FindPlugin();
	static FString PluginBaseDir();
	static FString ResourcesDir();
	static FString SourceTexturesDir();

	static FString PluginContentRoot();
	static FString ProjectLibraryRoot();
	static FString ProjectLibrarySurfacesRoot();
	static FString ProjectLibrarySurfaceFamilyRoot(const FString& Family);
	static FString ProjectLibraryRawTextureFamilyRoot(const FString& Family);
	static FString ProjectLibrarySurfaceThumbnailFamilyRoot(const FString& Family);
	static FString ProjectLibraryMasksRoot();
	static FString ProjectLibraryMaskThumbnailsRoot();
	static FString ProjectLibraryMaterialInstanceFamilyRoot(const FString& Family);
	static FString SurfacesRoot();
	static FString SurfaceFamilyRoot(const FString& Family);
	static FString TexturesRoot();
	static FString RawTextureFamilyRoot(const FString& Family);
	static FString ThumbnailsRoot();
	static FString SurfaceThumbnailsRoot();
	static FString SurfaceThumbnailFamilyRoot(const FString& Family);
	static FString MaskThumbnailsRoot();
	static FString MasksRoot();
	static FString NormalsRoot();
	static FString NormalCategoryRoot(const FString& Category);
	static FString EffectsRoot();
	static FString LightingRoot();
	static FString MaterialsRoot();
	static FString MaterialInstanceFamilyRoot(const FString& Family);
	static FString MeshesRoot();

	static FString MasterMaterialObjectPath();
	static FString PreviewMaterialObjectPath();
	static FString StudioFloorMaterialObjectPath();
	static FString SphereMeshObjectPath();
	static FString PlaneMeshObjectPath();
	static FString CubeMeshObjectPath();
	static FString CylinderMeshObjectPath();

	static FString ProjectMaterialsRoot();
	static FString LiveThemePath();
};
