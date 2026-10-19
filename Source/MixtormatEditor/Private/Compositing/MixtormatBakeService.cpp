// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatBakeService.h"

#include "Services/MixtormatPaths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace MixtormatBake
{
	constexpr int32 BaseColorOutputIndex = 0;
	constexpr int32 NormalOutputIndex = 1;
	constexpr int32 RAMOutputIndex = 2;
	constexpr int32 HeightOutputIndex = 3;
	constexpr int32 MaterialOutputIndex = 4;
	constexpr int32 RAMHOutputIndex = 5;
	constexpr int32 SurfaceOutputIndex = 6;

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
		const TextureGroup LODGroup,
		const bool bPreserveAlpha = false)
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
		if (bPreserveAlpha)
		{
			// TC_Masks discards alpha by default, and the alpha is the height.
			Texture->CompressionNoAlpha = false;
		}
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


	// UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue and
	// SetMaterialInstanceScalarParameterValue are confirmed broken in UE 5.8: both declare
	// `bool bResult = false;` and return it unconditionally, never setting it to true even when
	// the assignment succeeds (Engine/Source/Editor/MaterialEditor/Private/MaterialEditingLibrary.cpp).
	// So the bake cannot use that return value as a success signal. Instead, the master's real
	// parameter contract is enumerated up front and every required DA_* name is checked against
	// it before anything is created; the Set calls below are then just execution, not validation.
	const FName RequiredTextureParameters[] = {
		TEXT("DA_BaseColor"),
		TEXT("DA_Normal"),
		TEXT("DA_RAMH"),
		TEXT("DA_Height"),
	};
	// DA_DielectricF0 / DA_UsePackedF0 are deliberately not required here: the master computes F0
	// itself via MaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0 (BaseColor + Metallic),
	// confirmed by inspecting the master's own expression list. There is no manual F0 override
	// input in the graph for these two names to bind to.
	const FName RequiredScalarParameters[] = {
		TEXT("DA_FuzzInfluence"),
		TEXT("DA_Tiling"),
		TEXT("DA_RoughnessBias"),
		TEXT("DA_RoughnessContrast"),
		TEXT("DA_RoughnessOffset"),
		TEXT("DA_NormalIntensity"),
	};

	TArray<FName> FindMissingMasterParameters(UMaterialInterface& Master)
	{
		TArray<FMaterialParameterInfo> TextureParameterInfo;
		TArray<FGuid> TextureParameterIds;
		Master.GetAllTextureParameterInfo(TextureParameterInfo, TextureParameterIds);
		TSet<FName> AvailableTextureParameters;
		AvailableTextureParameters.Reserve(TextureParameterInfo.Num());
		for (const FMaterialParameterInfo& Info : TextureParameterInfo)
		{
			AvailableTextureParameters.Add(Info.Name);
		}

		TArray<FMaterialParameterInfo> ScalarParameterInfo;
		TArray<FGuid> ScalarParameterIds;
		Master.GetAllScalarParameterInfo(ScalarParameterInfo, ScalarParameterIds);
		TSet<FName> AvailableScalarParameters;
		AvailableScalarParameters.Reserve(ScalarParameterInfo.Num());
		for (const FMaterialParameterInfo& Info : ScalarParameterInfo)
		{
			AvailableScalarParameters.Add(Info.Name);
		}

		TArray<FName> Missing;
		for (const FName& Name : RequiredTextureParameters)
		{
			if (!AvailableTextureParameters.Contains(Name))
			{
				Missing.Add(Name);
			}
		}
		for (const FName& Name : RequiredScalarParameters)
		{
			if (!AvailableScalarParameters.Contains(Name))
			{
				Missing.Add(Name);
			}
		}
		TArray<FMaterialParameterInfo> VectorParameterInfo;
		TArray<FGuid> VectorParameterIds;
		Master.GetAllVectorParameterInfo(VectorParameterInfo, VectorParameterIds);
		if (!VectorParameterInfo.ContainsByPredicate([](const FMaterialParameterInfo& Info)
			{ return Info.Name == FName(TEXT("DA_FuzzColor")); }))
		{
			Missing.Add(TEXT("DA_FuzzColor"));
		}
		return Missing;
	}

	UMixtormatSurface* CreateOrUpdateSurface(
		const FString& PackageName,
		const FString& AssetName,
		UTexture2D* BaseColor,
		UTexture2D* Normal,
		UTexture2D* RAMH,
		UMaterialInterface* PreviewMaterial,
		const FText& DisplayName)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		UMixtormatSurface* Surface = LoadObject<UMixtormatSurface>(nullptr, *ObjectPath);
		const bool bCreated = Surface == nullptr;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Surface = NewObject<UMixtormatSurface>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Surface)
		{
			return nullptr;
		}

		Surface->Modify();
		Surface->DisplayName = DisplayName;
		Surface->BaseColor = BaseColor;
		Surface->Normal = Normal;
		Surface->RoughnessAOMetallic = RAMH;
		// Authored rather than derived: the height in that alpha is the composite's own, read
		// back from the height target, not something reconstructed from the normal.
		Surface->bHasBlendHeight = true;
		Surface->BlendHeightProvenance = EMixtormatBlendHeightProvenance::AuthoredRAMH;
		// The bake's per-pixel F0 lived in the alpha the height now occupies, so a single value
		// stands in for it. 1.5 is the struct's own default and the usual dielectric.
		Surface->DefaultIOR = 1.5f;
		Surface->PreviewMaterial = PreviewMaterial;
		Surface->PostEditChange();
		Surface->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Surface);
		}
		return Surface;
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

TArray<FMixtormatDiscoveredTextureParameter> FMixtormatBakeService::DiscoverTextureParameters(UMaterialInterface* Master)
{
	TArray<FMixtormatDiscoveredTextureParameter> Discovered;
	if (!Master)
	{
		return Discovered;
	}

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Master->GetAllTextureParameterInfo(ParameterInfos, ParameterGuids);

	TMap<FName, EMaterialSamplerType> KnownSamplers;
	if (UMaterial* RootMaterial = Master->GetMaterial())
	{
		auto InspectExpressions = [&KnownSamplers](auto& Self, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, TSet<const UMaterialFunctionInterface*>& VisitedFunctions) -> void
		{
			for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
			{
				if (!Expr)
				{
					continue;
				}
				if (const UMaterialExpressionTextureSampleParameter* TextureSample = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
				{
					if (!TextureSample->ParameterName.IsNone())
					{
						KnownSamplers.FindOrAdd(TextureSample->ParameterName, TextureSample->SamplerType);
					}
				}
				else if (const UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
				{
					if (FuncCall->MaterialFunction && !VisitedFunctions.Contains(FuncCall->MaterialFunction))
					{
						VisitedFunctions.Add(FuncCall->MaterialFunction);
						if (const UMaterialFunction* Func = Cast<UMaterialFunction>(FuncCall->MaterialFunction))
						{
							Self(Self, Func->GetExpressions(), VisitedFunctions);
						}
					}
				}
			}
		};

		TSet<const UMaterialFunctionInterface*> VisitedFunctions;
		InspectExpressions(InspectExpressions, RootMaterial->GetExpressions(), VisitedFunctions);
	}

	Discovered.Reserve(ParameterInfos.Num());
	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		FMixtormatDiscoveredTextureParameter Param;
		Param.ParameterInfo = Info;
		if (const EMaterialSamplerType* FoundSampler = KnownSamplers.Find(Info.Name))
		{
			Param.SamplerType = *FoundSampler;
			Param.bHasKnownSamplerType = true;
		}
		Discovered.Add(Param);
	}

	return Discovered;
}

bool FMixtormatBakeService::ValidateMaterialSettings(
	const FMixtormatBakeMaterialSettings& MaterialSettings,
	FText& OutError)
{
	if (MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Default)
	{
		const FString MasterPath = FMixtormatPaths::MasterMaterialObjectPath();
		UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, *MasterPath);
		if (!Master)
		{
			OutError = FText::Format(
				NSLOCTEXT("MixtormatBake", "MissingMaster", "Required master is missing: {0}"),
				FText::FromString(MasterPath));
			return false;
		}

		const TArray<FName> Missing = MixtormatBake::FindMissingMasterParameters(*Master);
		if (!Missing.IsEmpty())
		{
			TArray<FString> MissingNames;
			for (const FName& Name : Missing)
			{
				MissingNames.Add(Name.ToString());
			}
			OutError = FText::Format(
				NSLOCTEXT("MixtormatBake", "MasterContractMismatch", "Master material does not expose the parameters Mixtormat requires.\nMissing parameters: {0}"),
				FText::FromString(FString::Join(MissingNames, TEXT(", "))));
			return false;
		}
		return true;
	}

	// Custom Mode
	if (MaterialSettings.CustomMaster.IsNull())
	{
		OutError = NSLOCTEXT("MixtormatBake", "MissingCustomMaster", "Choose a custom material master.");
		return false;
	}

	UMaterialInterface* CustomMaster = MaterialSettings.CustomMaster.LoadSynchronous();
	if (!CustomMaster)
	{
		OutError = FText::Format(
			NSLOCTEXT("MixtormatBake", "FailedLoadCustomMaster", "Could not load custom material master: {0}"),
			FText::FromString(MaterialSettings.CustomMaster.ToString()));
		return false;
	}

	const TArray<FMixtormatDiscoveredTextureParameter> Discovered = DiscoverTextureParameters(CustomMaster);
	TSet<FMaterialParameterInfo> AvailableParams;
	TMap<FMaterialParameterInfo, EMaterialSamplerType> SamplerMap;
	for (const FMixtormatDiscoveredTextureParameter& Param : Discovered)
	{
		AvailableParams.Add(Param.ParameterInfo);
		if (Param.bHasKnownSamplerType)
		{
			SamplerMap.Add(Param.ParameterInfo, Param.SamplerType);
		}
	}

	const FMixtormatBakeMaterialMapping* Mappings[] = {
		&MaterialSettings.BaseColorMapping,
		&MaterialSettings.NormalMapping,
		&MaterialSettings.RAMMapping,
		&MaterialSettings.HeightMapping
	};
	const TCHAR* OutputNames[] = {
		TEXT("Base Color"),
		TEXT("Normal"),
		TEXT("Packed RAM"),
		TEXT("Height")
	};

	TSet<FMaterialParameterInfo> AssignedTargets;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FMixtormatBakeMaterialMapping& Mapping = *Mappings[Index];
		if (!Mapping.IsAssigned())
		{
			continue;
		}

		if (!AvailableParams.Contains(Mapping.ParameterInfo))
		{
			OutError = FText::Format(
				NSLOCTEXT("MixtormatBake", "MappedParamNotFound", "Parameter '{0}' assigned to {1} was not found on material '{2}'."),
				FText::FromName(Mapping.ParameterInfo.Name),
				FText::FromString(OutputNames[Index]),
				FText::FromString(CustomMaster->GetName()));
			return false;
		}

		if (AssignedTargets.Contains(Mapping.ParameterInfo))
		{
			OutError = FText::Format(
				NSLOCTEXT("MixtormatBake", "DuplicateTargetParam", "Duplicate parameter assignment: '{0}' is assigned to multiple outputs."),
				FText::FromName(Mapping.ParameterInfo.Name));
			return false;
		}
		AssignedTargets.Add(Mapping.ParameterInfo);

		if (Index == 3) // Height
		{
			if (const EMaterialSamplerType* SamplerType = SamplerMap.Find(Mapping.ParameterInfo))
			{
				if (*SamplerType == SAMPLERTYPE_Color || *SamplerType == SAMPLERTYPE_Normal)
				{
					OutError = FText::Format(
						NSLOCTEXT("MixtormatBake", "HeightSamplerMismatch", "Parameter '{0}' uses a Color or Normal sampler type, which is incompatible with 16-bit Grayscale Height textures. Expected Grayscale or Linear Grayscale."),
						FText::FromName(Mapping.ParameterInfo.Name));
					return false;
				}
			}
		}
	}

	return true;
}

bool FMixtormatBakeService::ValidateSettings(
	const FMixtormatBakeSettings& Settings,
	FText& OutError)
{
	if (Settings.DestinationPath.IsEmpty())
	{
		OutError = NSLOCTEXT("MixtormatBake", "MissingDestination", "Choose an output destination folder.");
		return false;
	}
	if (Settings.BaseName.IsEmpty())
	{
		OutError = NSLOCTEXT("MixtormatBake", "MissingBaseName", "Enter an output base name.");
		return false;
	}

	for (const FString& ObjectPath : GetOutputObjectPaths(Settings))
	{
		if (!FPackageName::IsValidObjectPath(ObjectPath, &OutError))
		{
			return false;
		}
	}

	return ValidateMaterialSettings(Settings.MaterialSettings, OutError);
}

TArray<FString> FMixtormatBakeService::GetOutputAssetNames(
	const FMixtormatBakeSettings& Settings)
{
	return {
		FString::Printf(TEXT("T_%s_BC"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_N"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_RAM"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_H"), *Settings.BaseName),
		FString::Printf(TEXT("MI_%s"), *Settings.BaseName),
		FString::Printf(TEXT("T_%s_RAMH"), *Settings.BaseName),
		FString::Printf(TEXT("SF_%s"), *Settings.BaseName)
	};
}

TArray<FString> FMixtormatBakeService::GetOutputObjectPaths(
	const FMixtormatBakeSettings& Settings)
{
	using namespace MixtormatBake;
	TArray<FString> Paths;
	for (const FString& AssetName : GetOutputAssetNames(Settings))
	{
		Paths.Add(MakeObjectPath(Settings.DestinationPath, AssetName));
	}
	return Paths;
}

TArray<FString> FMixtormatBakeService::FindExistingOutputObjectPaths(
	const FMixtormatBakeSettings& Settings)
{
	using namespace MixtormatBake;
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

FMixtormatBakeResult FMixtormatBakeService::Bake(
	UMixtormatMaterial& Recipe,
	UTextureRenderTarget2D& BaseColorTarget,
	UTextureRenderTarget2D& NormalTarget,
	UTextureRenderTarget2D& RAMTarget,
	UTextureRenderTarget2D& HeightTarget,
	const FMixtormatBakeSettings& Settings,
	const TFunction<void(EMixtormatBakeStage)>& Progress)
{
	using namespace MixtormatBake;
	FMixtormatBakeResult Result;

	FText SettingsError;
	if (!ValidateSettings(Settings, SettingsError))
	{
		Result.Errors.Add(SettingsError);
		return Result;
	}

	FText ReferenceError;
	if (!MixtormatCompositionReferences::Validate(
		Recipe.Layers, FSoftObjectPath(&Recipe), ReferenceError))
	{
		Result.Errors.Add(ReferenceError);
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
		Result.Errors.Add(NSLOCTEXT("MixtormatBake", "InvalidTargets", "Compositor outputs have invalid or mismatched dimensions."));
		return Result;
	}

	const FString RecipePackageName = Recipe.GetOutermost()->GetName();
	if (!FPackageName::IsValidLongPackageName(RecipePackageName))
	{
		Result.Errors.Add(NSLOCTEXT("MixtormatBake", "UnsavedRecipe", "Save the Mixtormat recipe before baking."));
		return Result;
	}

	UMaterialInterface* Master = nullptr;
	if (Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Custom)
	{
		Master = Settings.MaterialSettings.CustomMaster.LoadSynchronous();
		if (!Master)
		{
			Result.Errors.Add(NSLOCTEXT("MixtormatBake", "MissingCustomMasterBake", "Custom master material is missing or invalid."));
			return Result;
		}
	}
	else
	{
		const FString MasterPath = FMixtormatPaths::MasterMaterialObjectPath();
		Master = LoadObject<UMaterialInterface>(nullptr, *MasterPath);
		if (!Master)
		{
			Result.Errors.Add(FText::Format(
				NSLOCTEXT("MixtormatBake", "MissingMaster", "Required master is missing: {0}"),
				FText::FromString(MasterPath)));
			return Result;
		}

		const TArray<FName> MissingMasterParameters = FindMissingMasterParameters(*Master);
		if (!MissingMasterParameters.IsEmpty())
		{
			TArray<FString> MissingParameterNames;
			MissingParameterNames.Reserve(MissingMasterParameters.Num());
			for (const FName& ParameterName : MissingMasterParameters)
			{
				MissingParameterNames.Add(ParameterName.ToString());
			}
			Result.Errors.Add(FText::Format(
				NSLOCTEXT(
					"MixtormatBake",
					"MasterContractMismatch",
					"Master material does not expose the parameters Mixtormat requires.\nMaster object path: {0}\nMaster class: {1}\nMissing parameters: {2}"),
				FText::FromString(MasterPath),
				FText::FromString(Master->GetClass()->GetName()),
				FText::FromString(FString::Join(MissingParameterNames, TEXT(", ")))));
			return Result;
		}
	}

	UEditorAssetSubsystem* AssetSubsystem = GEditor
		? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
		: nullptr;
	if (!AssetSubsystem)
	{
		Result.Errors.Add(NSLOCTEXT(
			"MixtormatBake",
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
		UMaterialInstanceConstant::StaticClass(),
		UTexture2D::StaticClass(),
		UMixtormatSurface::StaticClass()
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
						"MixtormatBake",
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
		Progress(EMixtormatBakeStage::Readback);
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
		Result.Errors.Add(NSLOCTEXT("MixtormatBake", "ReadbackFailed", "Failed to read compositor output from the GPU."));
		return Result;
	}

	if (Progress)
	{
		Progress(EMixtormatBakeStage::CreateTextures);
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

	// The layer-facing copy. A layer reads its height from the RAM alpha, and the bake puts F0
	// there, so the two cannot be the same texture -- this is RAM's RGB with the height target
	// in its alpha. Height comes back 16-bit and the texture source is 8-bit BGRA, so it is
	// taken from the high byte.
	const FString& RAMHName = AssetNames[RAMHOutputIndex];
	TArray<FColor> RAMHPixels = RAMPixels;
	const int32 RAMHPixelCount = FMath::Min(RAMHPixels.Num(), HeightPixels.Num());
	for (int32 PixelIndex = 0; PixelIndex < RAMHPixelCount; ++PixelIndex)
	{
		RAMHPixels[PixelIndex].A = static_cast<uint8>(HeightPixels[PixelIndex] >> 8);
	}
	Result.RAMH = CreateOrUpdateTexture(
		MakePackageName(Settings.DestinationPath, RAMHName),
		RAMHName,
		Resolution.X,
		Resolution.Y,
		RAMHPixels,
		false,
		TC_Masks,
		TEXTUREGROUP_World,
		true);
	if (!Result.RAMH)
	{
		Result.FailedAssetPaths.AddUnique(ObjectPaths[RAMHOutputIndex]);
		Result.Errors.Add(FText::Format(
			NSLOCTEXT("MixtormatBake", "RAMHCreationFailed", "Failed to create or update {0}."),
			FText::FromString(ObjectPaths[RAMHOutputIndex])));
	}
	UObject* TextureOutputs[] = {Result.BaseColor, Result.Normal, Result.RAM, Result.Height};
	for (int32 TextureIndex = 0; TextureIndex < UE_ARRAY_COUNT(TextureOutputs); ++TextureIndex)
	{
		const FString& OutputPath = ObjectPaths[TextureIndex];
		if (!TextureOutputs[TextureIndex])
		{
			Result.FailedAssetPaths.AddUnique(OutputPath);
			Result.Errors.Add(FText::Format(
				NSLOCTEXT("MixtormatBake", "TextureCreationFailed", "Failed to create or update {0}."),
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
		Progress(EMixtormatBakeStage::CreateMaterial);
	}
	const FString& MaterialName = AssetNames[MaterialOutputIndex];
	Result.Material = CreateOrUpdateMaterial(Settings.DestinationPath, MaterialName, *Master);
	if (!Result.Material)
	{
		Result.FailedAssetPaths.AddUnique(ObjectPaths[MaterialOutputIndex]);
		Result.Errors.Add(FText::Format(
			NSLOCTEXT("MixtormatBake", "MaterialCreationFailed", "Failed to create or update {0}."),
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

	const FString TargetMaterialObjectPath = MakeObjectPath(Settings.DestinationPath, MaterialName);
	const bool bIsSameOutputInstance =
		Recipe.LastAppliedBakeOverrides.MaterialInstance.ToSoftObjectPath() == FSoftObjectPath(TargetMaterialObjectPath);

	TArray<FMaterialParameterInfo> AppliedParameters;

	if (Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Custom)
	{
		TSet<FMaterialParameterInfo> NewAssigned;
		const FMixtormatBakeMaterialMapping* Mappings[] = {
			&Settings.MaterialSettings.BaseColorMapping,
			&Settings.MaterialSettings.NormalMapping,
			&Settings.MaterialSettings.RAMMapping,
			&Settings.MaterialSettings.HeightMapping
		};
		UTexture2D* Textures[] = {
			Result.BaseColor,
			Result.Normal,
			Result.RAM,
			Result.Height
		};

		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Mappings[Index]->IsAssigned() && Textures[Index])
			{
				NewAssigned.Add(Mappings[Index]->ParameterInfo);
				AppliedParameters.Add(Mappings[Index]->ParameterInfo);
				Result.Material->SetTextureParameterValueEditorOnly(Mappings[Index]->ParameterInfo, Textures[Index]);
			}
		}

		if (bIsSameOutputInstance)
		{
			for (const FMaterialParameterInfo& PrevInfo : Recipe.LastAppliedBakeOverrides.ParameterInfos)
			{
				if (!NewAssigned.Contains(PrevInfo))
				{
					Result.Material->TextureParameterValues.RemoveAll([&PrevInfo](const FTextureParameterValue& Val)
					{
						return Val.ParameterInfo == PrevInfo;
					});
				}
			}
		}
	}
	else
	{
		AppliedParameters.Add(FMaterialParameterInfo(TEXT("DA_BaseColor")));
		AppliedParameters.Add(FMaterialParameterInfo(TEXT("DA_Normal")));
		AppliedParameters.Add(FMaterialParameterInfo(TEXT("DA_RAMH")));
		AppliedParameters.Add(FMaterialParameterInfo(TEXT("DA_Height")));

		Result.Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DA_BaseColor")), Result.BaseColor);
		Result.Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DA_Normal")), Result.Normal);
		Result.Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DA_RAMH")), Result.RAM);
		Result.Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DA_Height")), Result.Height);

		if (bIsSameOutputInstance)
		{
			TSet<FMaterialParameterInfo> DefaultSet(AppliedParameters);
			for (const FMaterialParameterInfo& PrevInfo : Recipe.LastAppliedBakeOverrides.ParameterInfos)
			{
				if (!DefaultSet.Contains(PrevInfo))
				{
					Result.Material->TextureParameterValues.RemoveAll([&PrevInfo](const FTextureParameterValue& Val)
					{
						return Val.ParameterInfo == PrevInfo;
					});
				}
			}
		}

		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
			Result.Material,
			TEXT("DA_FuzzInfluence"),
			MixtormatCompositionReferences::ComputeFuzzInfluence(Recipe.Layers, Recipe.LayerGroups));
		const TOptional<FLinearColor> FuzzColor =
			MixtormatCompositionReferences::ComputeFuzzColor(Recipe.Layers, Recipe.LayerGroups);
		if (FuzzColor.IsSet())
		{
			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
				Result.Material, TEXT("DA_FuzzColor"), FuzzColor.GetValue());
		}
		else
		{
			Result.Material->VectorParameterValues.RemoveAll([](const FVectorParameterValue& Value)
			{
				return Value.ParameterInfo.Name == FName(TEXT("DA_FuzzColor"));
			});
		}
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Result.Material, TEXT("DA_Tiling"), 1.0f);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Result.Material, TEXT("DA_RoughnessBias"), 0.5f);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Result.Material, TEXT("DA_RoughnessContrast"), 0.0f);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Result.Material, TEXT("DA_RoughnessOffset"), 0.0f);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Result.Material, TEXT("DA_NormalIntensity"), 1.0f);
	}

	Result.Material->PostEditChange();
	Result.Material->MarkPackageDirty();

	const FString& SurfaceName = AssetNames[SurfaceOutputIndex];
	Result.Surface = CreateOrUpdateSurface(
		MakePackageName(Settings.DestinationPath, SurfaceName),
		SurfaceName,
		Result.BaseColor,
		Result.Normal,
		Result.RAMH,
		Result.Material,
		Recipe.DisplayName.IsEmpty()
			? FText::FromString(Recipe.GetName())
			: Recipe.DisplayName);
	if (!Result.Surface)
	{
		Result.FailedAssetPaths.AddUnique(ObjectPaths[SurfaceOutputIndex]);
		Result.Errors.Add(FText::Format(
			NSLOCTEXT("MixtormatBake", "SurfaceCreationFailed", "Failed to create or update {0}."),
			FText::FromString(ObjectPaths[SurfaceOutputIndex])));
	}

	Recipe.Modify();
	Recipe.BakedBaseColor = Result.BaseColor;
	Recipe.BakedNormal = Result.Normal;
	Recipe.BakedRAM = Result.RAM;
	Recipe.BakedHeight = Result.Height;
	Recipe.BakedMaterial = Result.Material;
	Recipe.BakedSurface = Result.Surface;
	Recipe.BakeMaterialSettings = Settings.MaterialSettings;
	Recipe.LastAppliedBakeOverrides.MaterialInstance = Result.Material;
	Recipe.LastAppliedBakeOverrides.ParameterInfos = AppliedParameters;
	Recipe.MarkPackageDirty();
	Result.UpdatedAssetPaths.AddUnique(Recipe.GetPathName());

	if (Progress)
	{
		Progress(EMixtormatBakeStage::Save);
	}

	const auto SaveAsset = [&Result, AssetSubsystem](UObject* Asset)
	{
		const FString AssetPath = Asset->GetPathName();
		if (!AssetSubsystem->SaveLoadedAsset(Asset, false))
		{
			Result.FailedAssetPaths.AddUnique(AssetPath);
			Result.Errors.Add(FText::Format(
				NSLOCTEXT(
					"MixtormatBake",
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
	if (Result.RAMH)
	{
		SaveAsset(Result.RAMH);
	}
	if (Result.Surface)
	{
		SaveAsset(Result.Surface);
	}
	SaveAsset(&Recipe);
	return Result;
}
