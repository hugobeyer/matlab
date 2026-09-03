#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FMixtormatMaskEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

struct FMixtormatSurfaceEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Family;
	FName Subtype;
	FName Finish;
};

struct FMixtormatNormalEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

struct FMixtormatEffectEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

class FMixtormatRegistry final
{
public:
	static TArray<FMixtormatSurfaceEntry> GetSurfaces();
	static TArray<FMixtormatMaskEntry> GetMasks();
	static TArray<FMixtormatNormalEntry> GetNormals();
	static TArray<FMixtormatEffectEntry> GetEffects();
};
