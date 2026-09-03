#include "Services/MixtormatRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatSurface.h"
#include "Modules/ModuleManager.h"

TArray<FMixtormatSurfaceEntry> FMixtormatRegistry::GetSurfaces()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMixtormatSurface::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Surfaces")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMixtormatSurfaceEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		const UMixtormatSurface* Surface = Cast<UMixtormatSurface>(Asset.GetAsset());
		if (!Surface)
		{
			continue;
		}

		FMixtormatSurfaceEntry& Entry = Entries.AddDefaulted_GetRef();
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

	Entries.Sort([](const FMixtormatSurfaceEntry& A, const FMixtormatSurfaceEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMixtormatMaskEntry> FMixtormatRegistry::GetMasks()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMixtormatMask::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Masks")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMixtormatMaskEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		const UObject* MaskObject = Asset.GetAsset();
		const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject);
		const UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
		if (!Mask && !Texture)
		{
			continue;
		}

		FMixtormatMaskEntry& Entry = Entries.AddDefaulted_GetRef();
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

	Entries.Sort([](const FMixtormatMaskEntry& A, const FMixtormatMaskEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMixtormatNormalEntry> FMixtormatRegistry::GetNormals()
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

	TArray<FMixtormatNormalEntry> Entries;
	Entries.Reserve(Assets.Num());

	for (const FAssetData& Asset : Assets)
	{
		FMixtormatNormalEntry& Entry = Entries.AddDefaulted_GetRef();
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

	Entries.Sort([](const FMixtormatNormalEntry& A, const FMixtormatNormalEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});

	return Entries;
}

TArray<FMixtormatEffectEntry> FMixtormatRegistry::GetEffects()
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMixtormatEffect::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(TEXT("/MaterialLab/Effects")));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);

	TArray<FMixtormatEffectEntry> Entries;
	Entries.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		const UMixtormatEffect* Effect = Cast<UMixtormatEffect>(Asset.GetAsset());
		if (!Effect)
		{
			continue;
		}

		FMixtormatEffectEntry& Entry = Entries.AddDefaulted_GetRef();
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

	Entries.Sort([](const FMixtormatEffectEntry& A, const FMixtormatEffectEntry& B)
	{
		return A.DisplayName.ToString() < B.DisplayName.ToString();
	});
	return Entries;
}
