#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
class UMaterialLabMaterial;
class UTexture2D;
class UTextureRenderTarget2D;

enum class EMaterialLabBakeStage : uint8
{
	Readback,
	CreateTextures,
	CreateMaterial,
	Save
};

struct FMaterialLabBakeSettings
{
	FString DestinationPath;
	FString BaseName;
};

struct FMaterialLabBakeResult
{
	UTexture2D* BaseColor = nullptr;
	UTexture2D* Normal = nullptr;
	UTexture2D* RAM = nullptr;
	UTexture2D* Height = nullptr;
	UMaterialInstanceConstant* Material = nullptr;
	TArray<FString> CreatedAssetPaths;
	TArray<FString> UpdatedAssetPaths;
	TArray<FString> SavedAssetPaths;
	TArray<FString> FailedAssetPaths;
	TArray<FText> Errors;

	bool Succeeded() const
	{
		return BaseColor && Normal && RAM && Height && Material && Errors.IsEmpty();
	}
};

class FMaterialLabBakeService final
{
public:
	static bool ValidateSettings(const FMaterialLabBakeSettings& Settings, FText& OutError);
	static TArray<FString> GetOutputAssetNames(const FMaterialLabBakeSettings& Settings);
	static TArray<FString> GetOutputObjectPaths(const FMaterialLabBakeSettings& Settings);
	static TArray<FString> FindExistingOutputObjectPaths(const FMaterialLabBakeSettings& Settings);

	static FMaterialLabBakeResult Bake(
		UMaterialLabMaterial& Recipe,
		UTextureRenderTarget2D& BaseColorTarget,
		UTextureRenderTarget2D& NormalTarget,
		UTextureRenderTarget2D& RAMTarget,
		UTextureRenderTarget2D& HeightTarget,
		const FMaterialLabBakeSettings& Settings,
		const TFunction<void(EMaterialLabBakeStage)>& Progress);
};
