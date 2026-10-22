// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MixtormatMaterial.h"
#include "Materials/MaterialParameters.h"

class UMaterialInstanceConstant;
class UMaterialInterface;
class UMixtormatMaterial;
class UMixtormatSurface;
class UTexture2D;
class UTextureRenderTarget2D;

enum class EMixtormatBakeStage : uint8
{
	Readback,
	CreateTextures,
	CreateMaterial,
	Save
};

struct FMixtormatDiscoveredTextureParameter
{
	FMaterialParameterInfo ParameterInfo;
	EMaterialSamplerType SamplerType = SAMPLERTYPE_Color;
	bool bHasKnownSamplerType = false;
};

struct FMixtormatBakeSettings
{
	FString DestinationPath;
	FString BaseName;
	// Per-bake, initialized from UMixtormatEditorSettings when the bake dialog opens for a recipe
	// it has not seen yet; never written back to that config. Drives the GPU compositor's target
	// resolution for this bake only -- see SMixtormat::ExecuteBake.
	int32 Resolution = 2048;
	// Infrastructure only -- see UMixtormatEditorSettings::DefaultBakeAASamples. Not read by
	// FMixtormatBakeService::Bake.
	int32 AASamples = 1;

	// Target master material parent mode and parameter mappings
	FMixtormatBakeMaterialSettings MaterialSettings;

	// Transient compose result, not a dialog preference. The caller must refresh this after
	// ComposeLayersAtResolution; allocated/default-filled optical targets alone are not authored data.
	bool bHasAuthoredSurfaceOptics = false;
};

struct FMixtormatBakeResult
{
	UTexture2D* BaseColor = nullptr;
	UTexture2D* Normal = nullptr;
	UTexture2D* RAM = nullptr;
	UTexture2D* Height = nullptr;
	// Linear float RGBA: F0 / fuzz amount / fuzz roughness / SSS amount.
	UTexture2D* Optics = nullptr;
	UTexture2D* FuzzColorMap = nullptr;
	// Linear float RGB mean-free-path (cm), alpha reserved; never UNorm readback.
	UTexture2D* Subsurface = nullptr;
	// RAM with height repacked into its alpha for layer sourcing. Optics retains F0.
	UTexture2D* RAMH = nullptr;
	UMixtormatSurface* Surface = nullptr;
	UMaterialInstanceConstant* Material = nullptr;
	TArray<FString> CreatedAssetPaths;
	TArray<FString> UpdatedAssetPaths;
	TArray<FString> SavedAssetPaths;
	TArray<FString> FailedAssetPaths;
	TArray<FText> Errors;
	// E.g. a legacy parent without the shared optical graph; textures still export.
	TArray<FText> Warnings;

	bool Succeeded() const
	{
		return BaseColor && Normal && RAM && Height && Optics && FuzzColorMap
			&& Subsurface && RAMH && Surface && Material && Errors.IsEmpty();
	}
};

class FMixtormatBakeService final
{
public:
	// Checks the chosen material's actual global declarations, not MID override values.
	// A parameter contract is necessary, but does not certify graph wiring or renderer support.
	static bool HasSurfaceOpticsParameters(UMaterialInterface& Material, bool& bHasAny);
	static bool ValidateSettings(const FMixtormatBakeSettings& Settings, FText& OutError);
	static bool ValidateMaterialSettings(const FMixtormatBakeMaterialSettings& MaterialSettings, FText& OutError);
	static TArray<FMixtormatDiscoveredTextureParameter> DiscoverTextureParameters(UMaterialInterface* Master);
	static TArray<FString> GetOutputAssetNames(const FMixtormatBakeSettings& Settings);
	static TArray<FString> GetOutputObjectPaths(const FMixtormatBakeSettings& Settings);
	static TArray<FString> FindExistingOutputObjectPaths(const FMixtormatBakeSettings& Settings);

	static FMixtormatBakeResult Bake(
		UMixtormatMaterial& Recipe,
		UTextureRenderTarget2D& BaseColorTarget,
		UTextureRenderTarget2D& NormalTarget,
		UTextureRenderTarget2D& RAMTarget,
		UTextureRenderTarget2D& HeightTarget,
		UTextureRenderTarget2D& OpticsTarget,
		UTextureRenderTarget2D& FuzzColorTarget,
		UTextureRenderTarget2D& SubsurfaceTarget,
		const FMixtormatBakeSettings& Settings,
		const TFunction<void(EMixtormatBakeStage)>& Progress);
};
