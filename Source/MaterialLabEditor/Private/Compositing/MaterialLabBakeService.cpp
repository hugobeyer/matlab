#include "Compositing/MaterialLabBakeService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "MaterialLabMaterial.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace MaterialLabBake
{
	constexpr int32 BaseColorOutputIndex = 0;
	constexpr int32 NormalOutputIndex = 1;
	constexpr int32 RAMOutputIndex = 2;
	constexpr int32 HeightOutputIndex = 3;
	constexpr int32 MaterialOutputIndex = 4;

	FString MakePackageName(const FString& DestinationPath, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s"), *DestinationPath, *AssetName);
	}

	FString MakeObjectPath(const FString& DestinationPath, const FString& AssetName)
	{
		return FString::Printf(
			TEXT("%s/%s.%s"),
			*DestinationPath,
			*AssetName,
			*AssetName);
	}

	bool ReadTarget(
		UTextureRenderTarget2D& Target,
		const bool bConvertToSRGB,
		TArray<FColor>& OutPixels)
	{
		FTextureRenderTargetResource* Resource = Target.GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return false;
		}

		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(bConvertToSRGB);
		return Resource->ReadPixels(OutPixels, Flags);
	}

	bool ReadHeightTarget(UTextureRenderTarget2D& Target, TArray<uint16>& OutPixels)
	{
		FTextureRenderTargetResource* Resource = Target.GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return false;
		}

		TArray<FLinearColor> LinearPixels;
		FReadSurfaceDataFlags Flags(RCM_MinMax);
		Flags.SetLinearToGamma(false);
		if (!Resource->ReadLinearColorPixels(LinearPixels, Flags))
		{
			return false;
		}

		OutPixels.SetNumUninitialized(LinearPixels.Num());
		for (int32 PixelIndex = 0; PixelIndex < LinearPixels.Num(); ++PixelIndex)
		{
			OutPixels[PixelIndex] = static_cast<uint16>(FMath::RoundToInt(
				FMath::Clamp(LinearPixels[PixelIndex].R, 0.0f, 1.0f) * 65535.0f));
		}
		return true;
	}

	UTexture2D* CreateOrUpdateTexture(
		const FString& PackageName,
		const FString& AssetName,
		const int32 Width,
		const int32 Height,
		const TArray<FColor>& Pixels,
		const bool bSRGB,
		const TextureCompressionSettings Compression,
		const TextureGroup LODGroup)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("%s.%s"),
			*PackageName,
			*AssetName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		const bool bCreated = Texture == nullptr;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Texture = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Texture)
		{
			return nullptr;
		}

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(
			Width,
			Height,
			1,
			1,
			TSF_BGRA8,
			reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = bSRGB;
		Texture->CompressionSettings = Compression;
		Texture->LODGroup = LODGroup;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->NeverStream = true;
		Texture->Filter = TF_Default;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		return Texture;
	}

	UTexture2D* CreateOrUpdateHeightTexture(
		const FString& PackageName,
		const FString& AssetName,
		const int32 Width,
		const int32 Height,
		const TArray<uint16>& Pixels)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		const bool bCreated = Texture == nullptr;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Texture = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Texture)
		{
			return nullptr;
		}

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(
			Width,
			Height,
			1,
			1,
			TSF_G16,
			reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_Grayscale;
		Texture->LODGroup = TEXTUREGROUP_World;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->NeverStream = true;
		Texture->Filter = TF_Default;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		return Texture;
	}

	UMaterialInstanceConstant* CreateOrUpdateMaterial(
		const FString& DestinationPath,
		const FString& AssetName,
		UMaterialInterface& Master)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("%s/%s.%s"),
			*DestinationPath,
			*AssetName,
			*AssetName);
		UMaterialInstanceConstant* Instance =
			LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
		if (!Instance)
		{
			UMaterialInstanceConstantFactoryNew* Factory =
				NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = &Master;
			IAssetTools& AssetTools =
				FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			Instance = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
				AssetName,
				DestinationPath,
				UMaterialInstanceConstant::StaticClass(),
				Factory));
		}
		if (Instance)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(&Master);
			Instance->PostEditChange();
		}
		return Instance;
	}
}

bool FMaterialLabBakeService::ValidateSettings(
	const FMaterialLabBakeSettings& Settings,
	FText& OutError)
{
	if (Settings.DestinationPath.IsEmpty())
	{
		OutError = NSLOCTEXT("MaterialLabBake", "MissingDestination", "Choose an output destination folder.");
		return false;
	}
	if (Settings.BaseName.IsEmpty())
	{
		OutError = NSLOCTEXT("MaterialLabBake", "MissingBaseName", "Enter an output base name.");
		return false;
	}

	for (const FString& ObjectPath : GetOutputObjectPaths(Settings))
	{
		if (!FPackageName::IsValidObjectPath(ObjectPath, &OutError))
		{
			return false;
		}
	}
	return true;
}

TArray<FString> FMaterialLabBakeService::GetOutputAssetNames(
	const FMaterialLabBakeSettings& Settings)
{
	return {
		FString::Printf(TEXT("T_%s_BC"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_N"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_RAM"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_H"), *Settings.BaseName),
		FString::Printf(TEXT("MI_%s"), *Settings.BaseName)
	};
}

TArray<FString> FMaterialLabBakeService::GetOutputObjectPaths(
	const FMaterialLabBakeSettings& Settings)
{
	using namespace MaterialLabBake;
	TArray<FString> Paths;
	for (const FString& AssetName : GetOutputAssetNames(Settings))
	{
		Paths.Add(MakeObjectPath(Settings.DestinationPath, AssetName));
	}
	return Paths;
}

TArray<FString> FMaterialLabBakeService::FindExistingOutputObjectPaths(
	const FMaterialLabBakeSettings& Settings)
{
	using namespace MaterialLabBake;
	TArray<FString> ExistingPaths;
	for (const FString& AssetName : GetOutputAssetNames(Settings))
	{
		const FString PackageName = MakePackageName(Settings.DestinationPath, AssetName);
		if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
		{
			ExistingPaths.Add(MakeObjectPath(Settings.DestinationPath, AssetName));
		}
	}
	return ExistingPaths;
}

FMaterialLabBakeResult FMaterialLabBakeService::Bake(
	UMaterialLabMaterial& Recipe,
	UTextureRenderTarget2D& BaseColorTarget,
	UTextureRenderTarget2D& NormalTarget,
	UTextureRenderTarget2D& RAMTarget,
	UTextureRenderTarget2D& HeightTarget,
	const FMaterialLabBakeSettings& Settings,
	const TFunction<void(EMaterialLabBakeStage)>& Progress)
{
	using namespace MaterialLabBake;
	FMaterialLabBakeResult Result;

	FText SettingsError;
	if (!ValidateSettings(Settings, SettingsError))
	{
		Result.Errors.Add(SettingsError);
		return Result;
	}

	const FIntPoint Resolution(BaseColorTarget.SizeX, BaseColorTarget.SizeY);
	if (Resolution.X <= 0
		|| Resolution.Y <= 0
		|| NormalTarget.SizeX != Resolution.X
		|| NormalTarget.SizeY != Resolution.Y
		|| RAMTarget.SizeX != Resolution.X
		|| RAMTarget.SizeY != Resolution.Y
		|| HeightTarget.SizeX != Resolution.X
		|| HeightTarget.SizeY != Resolution.Y)
	{
		Result.Errors.Add(NSLOCTEXT("MaterialLabBake", "InvalidTargets", "Compositor outputs have invalid or mismatched dimensions."));
		return Result;
	}

	const FString RecipePackageName = Recipe.GetOutermost()->GetName();
	if (!FPackageName::IsValidLongPackageName(RecipePackageName))
	{
		Result.Errors.Add(NSLOCTEXT("MaterialLabBake", "UnsavedRecipe", "Save the Material Lab recipe before baking."));
		return Result;
	}

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/MaterialLab/Materials/M_MaterialLab_Substrate.M_MaterialLab_Substrate"));
	if (!Master)
	{
		Result.Errors.Add(NSLOCTEXT(
			"MaterialLabBake",
			"MissingMaster",
			"Required master is missing: /MaterialLab/Materials/M_MaterialLab_Substrate.M_MaterialLab_Substrate"));
		return Result;
	}

	UEditorAssetSubsystem* AssetSubsystem = GEditor
		? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
		: nullptr;
	if (!AssetSubsystem)
	{
		Result.Errors.Add(NSLOCTEXT(
			"MaterialLabBake",
			"MissingAssetSubsystem",
			"Bake cannot start because the editor asset subsystem is unavailable."));
		return Result;
	}

	const TArray<FString> AssetNames = GetOutputAssetNames(Settings);
	const TArray<FString> ObjectPaths = GetOutputObjectPaths(Settings);
	TArray<bool> bOutputExisted;
	bOutputExisted.Init(false, ObjectPaths.Num());
	const UClass* ExpectedClasses[] = {
		UTexture2D::StaticClass(),
		UTexture2D::StaticClass(),
		UTexture2D::StaticClass(),
		UTexture2D::StaticClass(),
		UMaterialInstanceConstant::StaticClass()
	};
	for (int32 OutputIndex = 0; OutputIndex < ObjectPaths.Num(); ++OutputIndex)
	{
		if (UObject* ExistingAsset = LoadObject<UObject>(nullptr, *ObjectPaths[OutputIndex]))
		{
			bOutputExisted[OutputIndex] = true;
			if (!ExistingAsset->IsA(ExpectedClasses[OutputIndex]))
			{
				Result.Errors.Add(FText::Format(
					NSLOCTEXT(
						"MaterialLabBake",
						"IncompatibleOutput",
						"Cannot update {0}: the existing asset has an incompatible type."),
					FText::FromString(ObjectPaths[OutputIndex])));
			}
		}
	}
	if (!Result.Errors.IsEmpty())
	{
		return Result;
	}

	if (Progress)
	{
		Progress(EMaterialLabBakeStage::Readback);
	}
	FlushRenderingCommands();
	TArray<FColor> BaseColorPixels;
	TArray<FColor> NormalPixels;
	TArray<FColor> RAMPixels;
	TArray<uint16> HeightPixels;
	if (!ReadTarget(BaseColorTarget, true, BaseColorPixels)
		|| !ReadTarget(NormalTarget, false, NormalPixels)
		|| !ReadTarget(RAMTarget, false, RAMPixels)
		|| !ReadHeightTarget(HeightTarget, HeightPixels))
	{
		Result.Errors.Add(NSLOCTEXT("MaterialLabBake", "ReadbackFailed", "Failed to read compositor output from the GPU."));
		return Result;
	}

	if (Progress)
	{
		Progress(EMaterialLabBakeStage::CreateTextures);
	}
	const FString& BaseColorName = AssetNames[BaseColorOutputIndex];
	const FString& NormalName = AssetNames[NormalOutputIndex];
	const FString& RAMName = AssetNames[RAMOutputIndex];
	const FString& HeightName = AssetNames[HeightOutputIndex];
	Result.BaseColor = CreateOrUpdateTexture(
		MakePackageName(Settings.DestinationPath, BaseColorName),
		BaseColorName,
		Resolution.X,
		Resolution.Y,
		BaseColorPixels,
		true,
		TC_Default,
		TEXTUREGROUP_World);
	Result.Normal = CreateOrUpdateTexture(
		MakePackageName(Settings.DestinationPath, NormalName),
		NormalName,
		Resolution.X,
		Resolution.Y,
		NormalPixels,
		false,
		TC_Normalmap,
		TEXTUREGROUP_WorldNormalMap);
	Result.RAM = CreateOrUpdateTexture(
		MakePackageName(Settings.DestinationPath, RAMName),
		RAMName,
		Resolution.X,
		Resolution.Y,
		RAMPixels,
		false,
		TC_Masks,
		TEXTUREGROUP_World);
	Result.Height = CreateOrUpdateHeightTexture(
		MakePackageName(Settings.DestinationPath, HeightName),
		HeightName,
		Resolution.X,
		Resolution.Y,
		HeightPixels);
	UObject* TextureOutputs[] = {Result.BaseColor, Result.Normal, Result.RAM, Result.Height};
	for (int32 TextureIndex = 0; TextureIndex < UE_ARRAY_COUNT(TextureOutputs); ++TextureIndex)
	{
		const FString& OutputPath = ObjectPaths[TextureIndex];
		if (!TextureOutputs[TextureIndex])
		{
			Result.FailedAssetPaths.AddUnique(OutputPath);
			Result.Errors.Add(FText::Format(
				NSLOCTEXT("MaterialLabBake", "TextureCreationFailed", "Failed to create or update {0}."),
				FText::FromString(OutputPath)));
			continue;
		}

		if (bOutputExisted[TextureIndex])
		{
			Result.UpdatedAssetPaths.AddUnique(OutputPath);
		}
		else
		{
			Result.CreatedAssetPaths.AddUnique(OutputPath);
		}
	}
	if (!Result.Errors.IsEmpty())
	{
		return Result;
	}

	if (Progress)
	{
		Progress(EMaterialLabBakeStage::CreateMaterial);
	}
	const FString& MaterialName = AssetNames[MaterialOutputIndex];
	Result.Material = CreateOrUpdateMaterial(Settings.DestinationPath, MaterialName, *Master);
	if (!Result.Material)
	{
		Result.FailedAssetPaths.AddUnique(ObjectPaths[MaterialOutputIndex]);
		Result.Errors.Add(FText::Format(
			NSLOCTEXT("MaterialLabBake", "MaterialCreationFailed", "Failed to create or update {0}."),
			FText::FromString(ObjectPaths[MaterialOutputIndex])));
		return Result;
	}
	if (bOutputExisted[MaterialOutputIndex])
	{
		Result.UpdatedAssetPaths.AddUnique(ObjectPaths[MaterialOutputIndex]);
	}
	else
	{
		Result.CreatedAssetPaths.AddUnique(ObjectPaths[MaterialOutputIndex]);
	}

	TArray<FName> FailedParameters;
	const auto SetTextureParameter = [&FailedParameters, &Result](
		const FName ParameterName,
		UTexture2D* Texture)
	{
		if (!UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
			Result.Material,
			ParameterName,
			Texture))
		{
			FailedParameters.Add(ParameterName);
		}
	};
	const auto SetScalarParameter = [&FailedParameters, &Result](
		const FName ParameterName,
		const float Value)
	{
		if (!UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
			Result.Material,
			ParameterName,
			Value))
		{
			FailedParameters.Add(ParameterName);
		}
	};

	SetTextureParameter(TEXT("ML_BaseColor"), Result.BaseColor);
	SetTextureParameter(TEXT("ML_Normal"), Result.Normal);
	SetTextureParameter(TEXT("ML_RAM"), Result.RAM);
	SetTextureParameter(TEXT("ML_Height"), Result.Height);
	SetScalarParameter(TEXT("ML_Tiling"), 1.0f);
	SetScalarParameter(TEXT("ML_RoughnessBias"), 0.5f);
	SetScalarParameter(TEXT("ML_RoughnessContrast"), 1.0f);
	SetScalarParameter(TEXT("ML_RoughnessOffset"), 0.0f);
	SetScalarParameter(TEXT("ML_NormalIntensity"), 1.0f);
	SetScalarParameter(TEXT("ML_DielectricF0"), 0.04f);
	SetScalarParameter(TEXT("ML_UsePackedF0"), 1.0f);
	if (!FailedParameters.IsEmpty())
	{
		TArray<FString> FailedParameterNames;
		FailedParameterNames.Reserve(FailedParameters.Num());
		for (const FName ParameterName : FailedParameters)
		{
			FailedParameterNames.Add(ParameterName.ToString());
		}
		Result.Errors.Add(FText::Format(
			NSLOCTEXT(
				"MaterialLabBake",
				"ParameterAssignmentFailed",
				"Failed to update {0}; the protected master is missing required parameters: {1}."),
			FText::FromString(ObjectPaths[MaterialOutputIndex]),
			FText::FromString(FString::Join(FailedParameterNames, TEXT(", ")))));
	}
	Result.Material->PostEditChange();
	Result.Material->MarkPackageDirty();
	if (!FailedParameters.IsEmpty())
	{
		Result.FailedAssetPaths.AddUnique(ObjectPaths[MaterialOutputIndex]);
		return Result;
	}

	Recipe.Modify();
	Recipe.BakedBaseColor = Result.BaseColor;
	Recipe.BakedNormal = Result.Normal;
	Recipe.BakedRAM = Result.RAM;
	Recipe.BakedHeight = Result.Height;
	Recipe.BakedMaterial = Result.Material;
	Recipe.MarkPackageDirty();
	Result.UpdatedAssetPaths.AddUnique(Recipe.GetPathName());

	if (Progress)
	{
		Progress(EMaterialLabBakeStage::Save);
	}

	const auto SaveAsset = [&Result, AssetSubsystem](UObject* Asset)
	{
		const FString AssetPath = Asset->GetPathName();
		if (!AssetSubsystem->SaveLoadedAsset(Asset, false))
		{
			Result.FailedAssetPaths.AddUnique(AssetPath);
			Result.Errors.Add(FText::Format(
				NSLOCTEXT(
					"MaterialLabBake",
					"AssetSaveFailed",
					"Failed to save {0}."),
				FText::FromString(AssetPath)));
			return;
		}
		Result.SavedAssetPaths.AddUnique(AssetPath);
	};
	SaveAsset(Result.BaseColor);
	SaveAsset(Result.Normal);
	SaveAsset(Result.RAM);
	SaveAsset(Result.Height);
	SaveAsset(Result.Material);
	SaveAsset(&Recipe);
	return Result;
}
