#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
class UMixtormatMaterial;
class UTexture2D;
class UTextureRenderTarget2D;

enum class EMixtormatBakeStage : uint8
{
	Readback,
	CreateTextures,
	CreateMaterial,
	Save
};

struct FMixtormatBakeSettings
{
	FString DestinationPath;
	FString BaseName;
};

struct FMixtormatBakeResult
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

class FMixtormatBakeService final
{
public:
	static bool ValidateSettings(const FMixtormatBakeSettings& Settings, FText& OutError);
	static TArray<FString> GetOutputAssetNames(const FMixtormatBakeSettings& Settings);
	static TArray<FString> GetOutputObjectPaths(const FMixtormatBakeSettings& Settings);
	static TArray<FString> FindExistingOutputObjectPaths(const FMixtormatBakeSettings& Settings);

	static FMixtormatBakeResult Bake(
		UMixtormatMaterial& Recipe,
		UTextureRenderTarget2D& BaseColorTarget,
		UTextureRenderTarget2D& NormalTarget,
		UTextureRenderTarget2D& RAMTarget,
		UTextureRenderTarget2D& HeightTarget,
		const FMixtormatBakeSettings& Settings,
		const TFunction<void(EMixtormatBakeStage)>& Progress);
};
