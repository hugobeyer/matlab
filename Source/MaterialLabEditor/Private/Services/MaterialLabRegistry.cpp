#include "Services/MaterialLabRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "MaterialLabEffect.h"
#include "MaterialLabMask.h"
#include "MaterialLabSurface.h"
#include "Modules/ModuleManager.h"

TArray<FMaterialLabSurfaceEntry> FMaterialLabRegistry::GetSurfaces()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterialLabSurface::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Surfaces")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMaterialLabSurfaceEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		const UMaterialLabSurface* Surface = Cast<UMaterialLabSurface>(Asset.GetAsset());
		if (!Surface)
		{
			continue;
		}

		FMaterialLabSurfaceEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.AssetPath = Asset.GetSoftObjectPath();
		if (Surface->PreviewMaterial)
		{
			Entry.ThumbnailAsset = FAssetData(Surface->PreviewMaterial.Get());
		}
		Entry.DisplayName = Surface->DisplayName.IsEmpty()
			? FText::FromName(Asset.AssetName)
			: Surface->DisplayName;
		Entry.Family = Surface->Family;
		if (Entry.Family.IsNone())
		{
			FString Family = Asset.PackagePath.ToString();
			Family.RemoveFromStart(TEXT("/MaterialLab/Surfaces/"));
			FString Remainder;
			Family.Split(TEXT("/"), &Family, &Remainder);
			Entry.Family = Family.IsEmpty() ? FName(TEXT("Uncategorized")) : FName(*Family);
		}
		Entry.Subtype = Surface->Subtype;
		Entry.Finish = Surface->Finish;
	}

	Entries.Sort([](const FMaterialLabSurfaceEntry& A, const FMaterialLabSurfaceEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMaterialLabMaskEntry> FMaterialLabRegistry::GetMasks()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterialLabMask::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Masks")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMaterialLabMaskEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		const UObject* MaskObject = Asset.GetAsset();
		const UMaterialLabMask* Mask = Cast<UMaterialLabMask>(MaskObject);
		const UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
		if (!Mask && !Texture)
		{
			continue;
		}

		FMaterialLabMaskEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.AssetPath = Asset.GetSoftObjectPath();
		if (Mask)
		{
			if (Mask->Thumbnail)
			{
				Entry.ThumbnailAsset = FAssetData(Mask->Thumbnail.Get());
			}
			else if (Mask->MaskTexture)
			{
				Entry.ThumbnailAsset = FAssetData(Mask->MaskTexture.Get());
			}
			Entry.DisplayName = Mask->DisplayName.IsEmpty()
				? FText::FromName(Asset.AssetName)
				: Mask->DisplayName;
			Entry.Category = Mask->Category;
		}
		else
		{
			Entry.ThumbnailAsset = Asset;
			Entry.DisplayName = FText::FromName(Asset.AssetName);
			Entry.Category = FName(TEXT("Texture"));
		}
	}

	Entries.Sort([](const FMaterialLabMaskEntry& A, const FMaterialLabMaskEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMaterialLabNormalEntry> FMaterialLabRegistry::GetNormals()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Normals")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMaterialLabNormalEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		FMaterialLabNormalEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.AssetPath = Asset.GetSoftObjectPath();
		Entry.ThumbnailAsset = Asset;
		Entry.DisplayName = FText::FromName(Asset.AssetName);

		FString Category = Asset.PackagePath.ToString();
		Category.RemoveFromStart(TEXT("/MaterialLab/Normals/"));
		if (Category.IsEmpty() || Category == TEXT("/MaterialLab/Normals"))
		{
			Category = TEXT("General");
		}
		Entry.Category = FName(*Category);
	}

	Entries.Sort([](const FMaterialLabNormalEntry& A, const FMaterialLabNormalEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMaterialLabEffectEntry> FMaterialLabRegistry::GetEffects()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterialLabEffect::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Effects")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMaterialLabEffectEntry> Entries;
	Entries.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		const UMaterialLabEffect* Effect = Cast<UMaterialLabEffect>(Asset.GetAsset());
		if (!Effect)
		{
			continue;
		}

		FMaterialLabEffectEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.AssetPath = Asset.GetSoftObjectPath();
		if (Effect->Mask)
		{
			Entry.ThumbnailAsset = FAssetData(Effect->Mask.Get());
		}
		else if (Effect->PeelData)
		{
			Entry.ThumbnailAsset = FAssetData(Effect->PeelData.Get());
		}
		Entry.DisplayName = Effect->DisplayName.IsEmpty()
			? FText::FromName(Asset.AssetName)
			: Effect->DisplayName;
		Entry.Category = Effect->Category;
	}

	Entries.Sort([](const FMaterialLabEffectEntry& A, const FMaterialLabEffectEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});
	return Entries;
}
