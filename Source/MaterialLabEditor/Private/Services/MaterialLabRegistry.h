#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FMaterialLabMaskEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

struct FMaterialLabSurfaceEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Family;
	FName Subtype;
	FName Finish;
};

struct FMaterialLabNormalEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

struct FMaterialLabEffectEntry
{
	FSoftObjectPath AssetPath;
	FAssetData ThumbnailAsset;
	FText DisplayName;
	FName Category;
};

class FMaterialLabRegistry final
{
public:
	static TArray<FMaterialLabSurfaceEntry> GetSurfaces();
	static TArray<FMaterialLabMaskEntry> GetMasks();
	static TArray<FMaterialLabNormalEntry> GetNormals();
	static TArray<FMaterialLabEffectEntry> GetEffects();
};
