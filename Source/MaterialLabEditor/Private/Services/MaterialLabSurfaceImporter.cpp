#include "Services/MaterialLabSurfaceImporter.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/Texture2D.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "MaterialEditingLibrary.h"
#include "MaterialLabEffect.h"
#include "MaterialLabNormalHeightGenerator.h"
#include "MaterialLabSurface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "Interfaces/IPluginManager.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"

namespace MaterialLabImporter
{
	struct FTextureSet
	{
		FString BaseName;
		FString BaseColorFile;
		FString NormalFile;
		FString RamFile;
		FString RamHeightFile;

		bool IsComplete() const
		{
			return !BaseColorFile.IsEmpty()
				&& !NormalFile.IsEmpty()
				&& (!RamFile.IsEmpty() || !RamHeightFile.IsEmpty());
		}
	};

	struct FEffectTextureSet
	{
		FString BaseName;
		FString PeelDataFile;
		FString MaskFile;
		FString HeightFile;
		FString SdfFile;
		FString BentNormalFile;

		bool IsComplete() const
		{
			return !PeelDataFile.IsEmpty()
				&& !MaskFile.IsEmpty()
				&& !HeightFile.IsEmpty()
				&& !SdfFile.IsEmpty();
		}
	};

	enum class EMapType : uint8
	{
		BaseColor,
		Normal,
		Ram,
		RamHeight,
		EffectData,
		EffectNormal
	};

	bool ParseMapName(const FString& Stem, FString& OutBaseName, EMapType& OutMapType)
	{
		if (Stem.EndsWith(TEXT("_RAMH"), ESearchCase::IgnoreCase))
		{
			OutBaseName = Stem.LeftChop(5);
			OutMapType = EMapType::RamHeight;
			return true;
		}

		if (Stem.EndsWith(TEXT("_RAM"), ESearchCase::IgnoreCase))
		{
			OutBaseName = Stem.LeftChop(4);
			OutMapType = EMapType::Ram;
			return true;
		}

		if (Stem.EndsWith(TEXT("_BC"), ESearchCase::IgnoreCase))
		{
			OutBaseName = Stem.LeftChop(3);
			OutMapType = EMapType::BaseColor;
			return true;
		}
		if (Stem.EndsWith(TEXT("_N"), ESearchCase::IgnoreCase))
		{
			OutBaseName = Stem.LeftChop(2);
			OutMapType = EMapType::Normal;
			return true;
		}
		return false;
	}

	bool ParseEffectMapName(const FString& Stem, FString& OutBaseName, FString& OutSuffix)
	{
		const TCHAR* Suffixes[] = {TEXT("_PDM"), TEXT("_MSK"), TEXT("_SDF"), TEXT("_BN"), TEXT("_H")};
		for (const TCHAR* Suffix : Suffixes)
		{
			const FString SuffixString(Suffix);
			if (Stem.EndsWith(SuffixString, ESearchCase::IgnoreCase))
			{
				OutBaseName = Stem.LeftChop(SuffixString.Len());
				OutSuffix = SuffixString;
				return true;
			}
		}
		return false;
	}

	bool ConfigureTexture(UTexture2D& Texture, const EMapType MapType)
	{
		const bool bNormalMap = MapType == EMapType::Normal
			|| MapType == EMapType::EffectNormal;
		const bool bEffectMap = MapType == EMapType::EffectData
			|| MapType == EMapType::EffectNormal;
		const bool bSRGB = MapType == EMapType::BaseColor;
		const TextureCompressionSettings Compression = bNormalMap
			? TC_Normalmap
			: (MapType == EMapType::BaseColor ? TC_Default : TC_Masks);
		const bool bLODGroupChanged = bNormalMap
			&& Texture.LODGroup != TEXTUREGROUP_WorldNormalMap;
		const bool bMipSettingsChanged = bEffectMap
			&& Texture.MipGenSettings != TMGS_NoMipmaps;
		const bool bStreamingChanged = !Texture.NeverStream;
		const bool bAlphaPreservationChanged = MapType == EMapType::RamHeight
			&& Texture.CompressionNoAlpha;
		if (Texture.SRGB == bSRGB
			&& Texture.CompressionSettings == Compression
			&& !bLODGroupChanged
			&& !bMipSettingsChanged
			&& !bStreamingChanged
			&& !bAlphaPreservationChanged)
		{
			return false;
		}

		Texture.Modify();
		Texture.SRGB = bSRGB;
		Texture.CompressionSettings = Compression;
		if (MapType == EMapType::RamHeight)
		{
			Texture.CompressionNoAlpha = false;
		}
		Texture.NeverStream = true;
		if (bNormalMap)
		{
			Texture.LODGroup = TEXTUREGROUP_WorldNormalMap;
		}
		if (bEffectMap)
		{
			Texture.MipGenSettings = TMGS_NoMipmaps;
		}
		Texture.PostEditChange();
		Texture.MarkPackageDirty();
		return true;
	}

	bool IsPluginAssetPath(const FString& PackagePath)
	{
		return PackagePath.StartsWith(TEXT("/MaterialLab/"), ESearchCase::CaseSensitive);
	}

	bool SavePluginAsset(UObject& Asset, const TCHAR* AssetLabel, TArray<FString>& Errors)
	{
		const FString PackageName = Asset.GetOutermost()->GetName();
		if (!IsPluginAssetPath(PackageName))
		{
			Errors.Add(FString::Printf(
				TEXT("Refused to save %s outside the MaterialLab plugin: %s."),
				AssetLabel,
				*PackageName));
			return false;
		}

		UEditorAssetSubsystem* AssetSubsystem = GEditor
			? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
			: nullptr;
		if (!AssetSubsystem)
		{
			Errors.Add(FString::Printf(
				TEXT("The editor asset subsystem is unavailable; %s was not saved."),
				AssetLabel));
			return false;
		}
		if (!AssetSubsystem->SaveLoadedAsset(&Asset, false))
		{
			Errors.Add(FString::Printf(TEXT("Failed to save %s: %s."), AssetLabel, *PackageName));
			return false;
		}
		return true;
	}

	FString NormalizeSourcePath(const FString& Path)
	{
		FString Normalized = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Normalized);
		return Normalized;
	}

	bool IsPluginSourceFile(const FString& Filename)
	{
		FString SourceRoot = NormalizeSourcePath(FMaterialLabSurfaceImporter::GetPluginTexturesRoot());
		FPaths::NormalizeDirectoryName(SourceRoot);
		SourceRoot += TEXT("/");
		return NormalizeSourcePath(Filename).StartsWith(SourceRoot, ESearchCase::IgnoreCase);
	}

	bool HasPluginSourceChanged(const UTexture2D& Texture, const FString& Filename)
	{
		if (!Texture.AssetImportData)
		{
			return true;
		}

		const FAssetImportInfo& SourceData = Texture.AssetImportData->GetSourceData();
		if (SourceData.SourceFiles.IsEmpty())
		{
			return true;
		}

		if (!NormalizeSourcePath(Texture.AssetImportData->GetFirstFilename()).Equals(
			NormalizeSourcePath(Filename),
			ESearchCase::IgnoreCase))
		{
			return true;
		}

		return SourceData.SourceFiles[0].FileHash != FMD5Hash::HashFile(*Filename);
	}

	UTexture2D* ImportTexture(
		IAssetTools& AssetTools,
		const FString& Filename,
		const FString& DestinationPath,
		const EMapType MapType,
		FMaterialLabImportResult& Result)
	{
		if (!IsPluginAssetPath(DestinationPath))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Refused to import a texture outside the MaterialLab plugin: %s."),
				*DestinationPath));
			return nullptr;
		}

		const FString AssetName = ObjectTools::SanitizeObjectName(
			FPaths::GetBaseFilename(Filename));
		const FString ObjectPath = FString::Printf(
			TEXT("%s/%s.%s"), *DestinationPath, *AssetName, *AssetName);

		if (UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath))
		{
			bool bReimported = false;
			if (IsPluginSourceFile(Filename) && HasPluginSourceChanged(*ExistingTexture, Filename))
			{
				if (!FReimportManager::Instance()->Reimport(
					ExistingTexture,
					false,
					false,
					Filename))
				{
					Result.Errors.Add(FString::Printf(
						TEXT("Failed to reimport changed plugin texture %s from %s."),
						*ObjectPath,
						*Filename));
					return nullptr;
				}
				bReimported = true;
				++Result.ReimportedTextureCount;
			}
			else
			{
				++Result.ReusedTextureCount;
			}

			const bool bSettingsChanged = ConfigureTexture(*ExistingTexture, MapType);
			if (bReimported || bSettingsChanged)
			{
				SavePluginAsset(*ExistingTexture, TEXT("imported texture"), Result.Errors);
			}
			return ExistingTexture;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Filename;
		Task->DestinationPath = DestinationPath;
		Task->DestinationName = AssetName;
		Task->bAutomated = true;
		Task->bReplaceExisting = false;
		Task->bReplaceExistingSettings = false;
		Task->bSave = true;

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(Task);
		AssetTools.ImportAssetTasks(Tasks);

		for (const FString& ImportedObjectPath : Task->ImportedObjectPaths)
		{
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ImportedObjectPath))
			{
				ConfigureTexture(*Texture, MapType);
				SavePluginAsset(*Texture, TEXT("imported texture"), Result.Errors);
				return Texture;
			}
		}

		return nullptr;
	}

	bool ReadTextureSourceBGRA8(
		UTexture2D& Texture,
		const TCHAR* TextureLabel,
		TArray<FColor>& OutPixels,
		FString& OutError)
	{
		const int32 Width = Texture.Source.GetSizeX();
		const int32 Height = Texture.Source.GetSizeY();
		if (Width <= 0 || Height <= 0)
		{
			OutError = FString::Printf(TEXT("The %s texture has no source pixels."), TextureLabel);
			return false;
		}
		if (Texture.Source.GetFormat() != TSF_BGRA8)
		{
			OutError = FString::Printf(
				TEXT("The %s source format must be BGRA8; received format %d."),
				TextureLabel,
				static_cast<int32>(Texture.Source.GetFormat()));
			return false;
		}

		const uint8* SourceData = Texture.Source.LockMip(0);
		if (!SourceData)
		{
			OutError = FString::Printf(TEXT("Failed to lock the %s source pixels."), TextureLabel);
			return false;
		}
		OutPixels.SetNumUninitialized(Width * Height);
		FMemory::Memcpy(
			OutPixels.GetData(),
			SourceData,
			OutPixels.Num() * sizeof(FColor));
		Texture.Source.UnlockMip(0);
		return true;
	}

	UTexture2D* CreateOrUpdateGeneratedRAMH(
		const FString& DestinationPath,
		const FString& AssetName,
		const int32 Width,
		const int32 Height,
		const TArray<FColor>& RamPixels,
		const TArray<uint8>& HeightPixels,
		FMaterialLabImportResult& Result)
	{
		if (!IsPluginAssetPath(DestinationPath))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Refused to create derived RAMH outside the MaterialLab plugin: %s."),
				*DestinationPath));
			return nullptr;
		}
		if (RamPixels.Num() != Width * Height || HeightPixels.Num() != Width * Height)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Derived RAMH pixel counts do not match %dx%d for %s."),
				Width,
				Height,
				*AssetName));
			return nullptr;
		}

		const FString PackageName = FString::Printf(TEXT("%s/%s"), *DestinationPath, *AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		const bool bCreated = Texture == nullptr;
		if (bCreated)
		{
			if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
			{
				Result.Errors.Add(FString::Printf(
					TEXT("Cannot create derived RAMH because an incompatible asset exists at %s."),
					*ExistingObject->GetPathName()));
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			Texture = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Texture)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to create or load derived RAMH %s."),
				*ObjectPath));
			return nullptr;
		}
		if (!bCreated && Texture->AssetImportData
			&& !Texture->AssetImportData->GetSourceData().SourceFiles.IsEmpty())
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Refused to overwrite non-generated texture at derived RAMH path %s."),
				*ObjectPath));
			return nullptr;
		}

		TArray<FColor> PackedPixels = RamPixels;
		for (int32 PixelIndex = 0; PixelIndex < PackedPixels.Num(); ++PixelIndex)
		{
			PackedPixels[PixelIndex].A = HeightPixels[PixelIndex];
		}

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(
			Width,
			Height,
			1,
			1,
			TSF_BGRA8,
			reinterpret_cast<const uint8*>(PackedPixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_Masks;
		Texture->CompressionNoAlpha = false;
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
		if (!SavePluginAsset(*Texture, TEXT("derived RAMH texture"), Result.Errors))
		{
			return nullptr;
		}
		++Result.GeneratedHeightCount;
		return Texture;
	}

	FString MakeDerivedHeightSourceHash(
		const FString& NormalFile,
		const FString& RamFile,
		const bool bFlipNormalY)
	{
		const FString NormalHash = LexToString(FMD5Hash::HashFile(*NormalFile));
		const FString RamHash = LexToString(FMD5Hash::HashFile(*RamFile));
		return FString::Printf(
			TEXT("FFT2:Y%d:%s:%s"),
			bFlipNormalY ? 1 : 0,
			*NormalHash,
			*RamHash);
	}

	FString MakeDerivedRAMHAssetName(const FString& BaseName)
	{
		return ObjectTools::SanitizeObjectName(BaseName + TEXT("_RAMH_Derived"));
	}

	UTexture2D* LoadReusableDerivedRAMH(
		const UMaterialLabSurface& Surface,
		const FString& DestinationPath,
		const FString& BaseName,
		const FString& SourceHash)
	{
		if (Surface.BlendHeightProvenance
			!= EMaterialLabBlendHeightProvenance::DerivedFromNormal
			|| !Surface.DerivedHeightSourceHash.Equals(SourceHash, ESearchCase::CaseSensitive))
		{
			return nullptr;
		}
		const FString AssetName = MakeDerivedRAMHAssetName(BaseName);
		const FString ObjectPath = FString::Printf(
			TEXT("%s/%s.%s"),
			*DestinationPath,
			*AssetName,
			*AssetName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		return Texture
			&& (!Texture->AssetImportData
				|| Texture->AssetImportData->GetSourceData().SourceFiles.IsEmpty())
			? Texture
			: nullptr;
	}

	UTexture2D* BuildDerivedRAMH(
		UTexture2D& Normal,
		UTexture2D& Ram,
		const FString& DestinationPath,
		const FString& BaseName,
		FMaterialLabImportResult& Result)
	{
		const FIntPoint NormalSize(Normal.Source.GetSizeX(), Normal.Source.GetSizeY());
		const FIntPoint RamSize(Ram.Source.GetSizeX(), Ram.Source.GetSizeY());
		if (NormalSize != RamSize)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Cannot derive height for %s because source dimensions differ: N %dx%d, RAM %dx%d."),
				*BaseName,
				NormalSize.X,
				NormalSize.Y,
				RamSize.X,
				RamSize.Y));
			return nullptr;
		}

		TArray<FColor> NormalPixels;
		FString NormalSourceError;
		if (!ReadTextureSourceBGRA8(
			Normal,
			TEXT("normal"),
			NormalPixels,
			NormalSourceError))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to read normal source for %s: %s"),
				*BaseName,
				*NormalSourceError));
			return nullptr;
		}

		TArray<uint8> HeightPixels;
		FString GenerationError;
		if (!FMaterialLabNormalHeightGenerator::Generate(
			NormalPixels,
			NormalSize,
			Normal.bFlipGreenChannel,
			HeightPixels,
			GenerationError))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to derive height for %s: %s"),
				*BaseName,
				*GenerationError));
			return nullptr;
		}

		TArray<FColor> RamPixels;
		FString SourceError;
		if (!ReadTextureSourceBGRA8(
			Ram,
			TEXT("packed RAM"),
			RamPixels,
			SourceError))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to pack derived height for %s: %s"),
				*BaseName,
				*SourceError));
			return nullptr;
		}

		const FString AssetName = MakeDerivedRAMHAssetName(BaseName);
		return CreateOrUpdateGeneratedRAMH(
			DestinationPath,
			AssetName,
			RamSize.X,
			RamSize.Y,
			RamPixels,
			HeightPixels,
			Result);
	}

	void SetIdentity(
		UMaterialLabSurface& Surface,
		const FString& BaseName,
		const FString& Family)
	{
		Surface.Family = FName(*Family);
		Surface.SourceTextureBaseName = BaseName;

		FString Identity = BaseName;
		Identity.RemoveFromStart(TEXT("TX_"));

		TArray<FString> Parts;
		Identity.ParseIntoArray(Parts, TEXT("_"), true);
		if (Parts.Num() < 3)
		{
			return;
		}

		Surface.Subtype = FName(*Parts[1]);
		Surface.Structure = Parts.Num() >= 5 ? FName(*Parts[3]) : FName(TEXT("Plain"));

		const FString& VariantPart = Parts.Last();
		Surface.Variant = FMath::Max(1, FCString::Atoi(*VariantPart));
		if (Parts.Num() >= 4)
		{
			Surface.Finish = FName(*Parts[2]);
			Surface.DisplayName = FText::FromString(FString::Printf(
				TEXT("%s · %s"), *Parts[1], *Parts[2]));
		}
		else
		{
			Surface.Finish = FName(TEXT("Standard"));
			Surface.DisplayName = FText::FromString(FString::Printf(
				TEXT("%s · %s"), *Parts[1], *VariantPart));
		}
	}

	UMaterialLabSurface* CreateOrLoadSurface(
		IAssetTools& AssetTools,
		const FString& AssetName,
		const FString& DestinationPath)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("%s/%s.%s"), *DestinationPath, *AssetName, *AssetName);
		if (UMaterialLabSurface* ExistingSurface = LoadObject<UMaterialLabSurface>(nullptr, *ObjectPath))
		{
			return ExistingSurface;
		}

		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UMaterialLabSurface::StaticClass();
		return Cast<UMaterialLabSurface>(AssetTools.CreateAsset(
			AssetName,
			DestinationPath,
			UMaterialLabSurface::StaticClass(),
			Factory));
	}

	UMaterialLabEffect* CreateOrLoadEffect(
		IAssetTools& AssetTools,
		const FString& AssetName,
		const FString& DestinationPath)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("%s/%s.%s"), *DestinationPath, *AssetName, *AssetName);
		if (UMaterialLabEffect* ExistingEffect = LoadObject<UMaterialLabEffect>(nullptr, *ObjectPath))
		{
			return ExistingEffect;
		}

		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UMaterialLabEffect::StaticClass();
		return Cast<UMaterialLabEffect>(AssetTools.CreateAsset(
			AssetName,
			DestinationPath,
			UMaterialLabEffect::StaticClass(),
			Factory));
	}

	void SetEffectIdentity(UMaterialLabEffect& Effect, const FString& BaseName)
	{
		FString Identity = BaseName;
		Identity.RemoveFromStart(TEXT("TX_Effect_"));
		TArray<FString> Parts;
		Identity.ParseIntoArray(Parts, TEXT("_"), true);

		Effect.DisplayName = FText::FromString(Parts.Num() > 1
			? FString::Printf(TEXT("%s · %s"), *Parts[0], *Parts[1])
			: Identity);
		Effect.Category = Parts.IsEmpty() ? FName(TEXT("Effects")) : FName(*Parts[0]);
		Effect.EffectType = EMaterialLabEffectType::Peeling;
		Effect.SourceTextureBaseName = BaseName;
	}

	UMaterial* LoadSubstrateMaster()
	{
		return LoadObject<UMaterial>(
			nullptr,
			TEXT("/MaterialLab/Materials/M_MaterialLab_Substrate.M_MaterialLab_Substrate"));
	}

	UMaterialInstanceConstant* CreateOrUpdatePreviewMaterial(
		IAssetTools& AssetTools,
		UMaterial& Parent,
		const FString& SurfaceAssetName,
		const FString& Family,
		UTexture2D& BaseColor,
		UTexture2D& Normal,
		UTexture2D& Ram,
		const float DefaultIOR)
	{
		FString InstanceName = SurfaceAssetName;
		InstanceName.RemoveFromStart(TEXT("ML_"));
		InstanceName = TEXT("MI_") + InstanceName;
		const FString DestinationPath = FString::Printf(TEXT("/MaterialLab/Materials/Instances/%s"), *Family);
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *InstanceName, *InstanceName);

		UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
		if (!Instance)
		{
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = &Parent;
			Instance = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
				InstanceName,
				DestinationPath,
				UMaterialInstanceConstant::StaticClass(),
				Factory));
		}
		if (!Instance)
		{
			return nullptr;
		}

		Instance->Modify();
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("ML_BaseColor"), &BaseColor);
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("ML_Normal"), &Normal);
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("ML_RAM"), &Ram);
		const float SafeIOR = FMath::Max(1.0f, DefaultIOR);
		const float DielectricF0 = FMath::Square((SafeIOR - 1.0f) / (SafeIOR + 1.0f));
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
			Instance,
			TEXT("ML_DielectricF0"),
			DielectricF0);
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
			Instance,
			TEXT("ML_UsePackedF0"),
			0.0f);
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
		return Instance;
	}
}

FText FMaterialLabImportResult::ToMessage() const
{
	if (bCancelled)
	{
		return NSLOCTEXT("MaterialLabImporter", "Cancelled", "Import cancelled.");
	}

	FString Message = FString::Printf(
		TEXT("Imported or updated %d surfaces, %d masks, %d standalone normals, and %d effects. Generated %d normal-derived height textures. Reimported %d changed textures; reused %d unchanged textures."),
		ImportedSurfaceCount,
		ImportedMaskCount,
		ImportedNormalCount,
		ImportedEffectCount,
		GeneratedHeightCount,
		ReimportedTextureCount,
		ReusedTextureCount);

	if (!Errors.IsEmpty())
	{
		Message += TEXT("\n\nIssues:\n- ");
		Message += FString::Join(Errors, TEXT("\n- "));
	}
	return FText::FromString(Message);
}

FString FMaterialLabSurfaceImporter::GetPluginTexturesRoot()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MaterialLab"));
	if (Plugin.IsValid())
	{
		return FPaths::ConvertRelativePathToFull(
			Plugin->GetBaseDir() / TEXT("Content/Textures"));
	}
	return FString();
}

TArray<FString> FMaterialLabSurfaceImporter::EnumerateShippedSourceDirectories()
{
	TArray<FString> Directories;
	const FString Root = GetPluginTexturesRoot();
	if (Root.IsEmpty() || !IFileManager::Get().DirectoryExists(*Root))
	{
		return Directories;
	}

	TArray<FString> FamilyDirectories;
	IFileManager::Get().FindFiles(
		FamilyDirectories,
		*(Root / TEXT("*")),
		false,
		true);
	for (const FString& FamilyDirectory : FamilyDirectories)
	{
		if (FamilyDirectory.Equals(TEXT("Masks"), ESearchCase::IgnoreCase)
			|| FamilyDirectory.Equals(TEXT("Normals"), ESearchCase::IgnoreCase)
			|| FamilyDirectory.Equals(TEXT("Effects"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString SourceDirectory = Root / FamilyDirectory / TEXT("Source");
		if (!IFileManager::Get().DirectoryExists(*SourceDirectory))
		{
			continue;
		}

		TArray<FString> PngFiles;
		IFileManager::Get().FindFiles(
			PngFiles,
			*(SourceDirectory / TEXT("*.png")),
			true,
			false);
		if (!PngFiles.IsEmpty())
		{
			Directories.Add(SourceDirectory);
		}
	}
	return Directories;
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportShippedMasks()
{
	using namespace MaterialLabImporter;
	FMaterialLabImportResult Result;
	const FString TexturesRoot = GetPluginTexturesRoot();
	if (TexturesRoot.IsEmpty())
	{
		return Result;
	}
	const FString MaskSourceRoot = TexturesRoot / TEXT("Masks/Source");
	if (!IFileManager::Get().DirectoryExists(*MaskSourceRoot))
	{
		Result.Errors.Add(FString::Printf(TEXT("Mask source folder is missing: %s."), *MaskSourceRoot));
		return Result;
	}

	TArray<FString> MaskFiles;
	IFileManager::Get().FindFiles(
		MaskFiles,
		*(MaskSourceRoot / TEXT("*.png")),
		true,
		false);
	if (MaskFiles.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(TEXT("No mask PNG files were found under %s."), *MaskSourceRoot));
		return Result;
	}
	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	for (const FString& MaskFilename : MaskFiles)
	{
		const FString MaskFile = MaskSourceRoot / MaskFilename;
		if (ImportTexture(
			AssetTools,
			MaskFile,
			TEXT("/MaterialLab/Masks"),
			EMapType::Ram,
			Result))
		{
			++Result.ImportedMaskCount;
		}
		else
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to import mask %s."),
				*FPaths::GetCleanFilename(MaskFile)));
		}
	}
	return Result;
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportShippedNormals()
{
	using namespace MaterialLabImporter;
	FMaterialLabImportResult Result;
	const FString TexturesRoot = GetPluginTexturesRoot();
	if (TexturesRoot.IsEmpty())
	{
		return Result;
	}

	const FString NormalSourceRoot = TexturesRoot / TEXT("Normals/Source");
	if (!IFileManager::Get().DirectoryExists(*NormalSourceRoot))
	{
		return Result;
	}

	TArray<FString> NormalFiles;
	IFileManager::Get().FindFilesRecursive(
		NormalFiles,
		*NormalSourceRoot,
		TEXT("*.png"),
		true,
		false);
	if (NormalFiles.IsEmpty())
	{
		return Result;
	}

	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	for (const FString& NormalFile : NormalFiles)
	{
		FString Category = FPaths::GetPath(NormalFile);
		const FString NormalSourceRootWithSlash = NormalSourceRoot + TEXT("/");
		FPaths::MakePathRelativeTo(Category, *NormalSourceRootWithSlash);
		Category.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Category.IsEmpty() || Category == TEXT("."))
		{
			Category = TEXT("General");
		}

		const FString DestinationPath = TEXT("/MaterialLab/Normals/") + Category;
		if (ImportTexture(
			AssetTools,
			NormalFile,
			DestinationPath,
			EMapType::Normal,
			Result))
		{
			++Result.ImportedNormalCount;
		}
		else
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to import standalone normal %s."),
				*FPaths::GetCleanFilename(NormalFile)));
		}
	}
	return Result;
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportShippedEffects()
{
	using namespace MaterialLabImporter;
	FMaterialLabImportResult Result;
	const FString TexturesRoot = GetPluginTexturesRoot();
	if (TexturesRoot.IsEmpty())
	{
		return Result;
	}

	const FString EffectSourceRoot = TexturesRoot / TEXT("Effects");
	if (!IFileManager::Get().DirectoryExists(*EffectSourceRoot))
	{
		return Result;
	}

	TArray<FString> EffectFiles;
	IFileManager::Get().FindFilesRecursive(
		EffectFiles,
		*EffectSourceRoot,
		TEXT("*.png"),
		true,
		false);

	TMap<FString, FEffectTextureSet> TextureSets;
	for (const FString& EffectFile : EffectFiles)
	{
		const FString Stem = FPaths::GetBaseFilename(EffectFile);
		FString BaseName;
		FString Suffix;
		if (!ParseEffectMapName(Stem, BaseName, Suffix))
		{
			continue;
		}

		FEffectTextureSet& Set = TextureSets.FindOrAdd(BaseName);
		Set.BaseName = BaseName;
		if (Suffix == TEXT("_PDM")) Set.PeelDataFile = EffectFile;
		else if (Suffix == TEXT("_MSK")) Set.MaskFile = EffectFile;
		else if (Suffix == TEXT("_H")) Set.HeightFile = EffectFile;
		else if (Suffix == TEXT("_SDF")) Set.SdfFile = EffectFile;
		else if (Suffix == TEXT("_BN")) Set.BentNormalFile = EffectFile;
	}

	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	for (const TPair<FString, FEffectTextureSet>& Pair : TextureSets)
	{
		const FEffectTextureSet& Set = Pair.Value;
		if (!Set.IsComplete())
		{
			Result.Errors.Add(FString::Printf(
				TEXT("%s requires matching _PDM, _MSK, _H, and _SDF maps."),
				*Set.BaseName));
			continue;
		}

		FString Identity = Set.BaseName;
		Identity.RemoveFromStart(TEXT("TX_Effect_"));
		TArray<FString> Parts;
		Identity.ParseIntoArray(Parts, TEXT("_"), true);
		const FString Category = Parts.IsEmpty() ? TEXT("General") : Parts[0];
		const FString TexturePath = FString::Printf(
			TEXT("/MaterialLab/Textures/Effects/%s/Raw"), *Category);
		const FString EffectPath = FString::Printf(TEXT("/MaterialLab/Effects/%s"), *Category);

		UTexture2D* PeelData = ImportTexture(AssetTools, Set.PeelDataFile, TexturePath, EMapType::EffectData, Result);
		UTexture2D* Mask = ImportTexture(AssetTools, Set.MaskFile, TexturePath, EMapType::EffectData, Result);
		UTexture2D* Height = ImportTexture(AssetTools, Set.HeightFile, TexturePath, EMapType::EffectData, Result);
		UTexture2D* Sdf = ImportTexture(AssetTools, Set.SdfFile, TexturePath, EMapType::EffectData, Result);
		UTexture2D* BentNormal = Set.BentNormalFile.IsEmpty()
			? nullptr
			: ImportTexture(AssetTools, Set.BentNormalFile, TexturePath, EMapType::EffectNormal, Result);
		if (!PeelData || !Mask || !Height || !Sdf
			|| (!Set.BentNormalFile.IsEmpty() && !BentNormal))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to import the peeling texture set for %s."), *Set.BaseName));
			continue;
		}

		const int32 SizeX = PeelData->GetSizeX();
		const int32 SizeY = PeelData->GetSizeY();
		const auto MatchesSize = [SizeX, SizeY](const UTexture2D* Texture)
		{
			return !Texture || (Texture->GetSizeX() == SizeX && Texture->GetSizeY() == SizeY);
		};
		if (!MatchesSize(Mask) || !MatchesSize(Height) || !MatchesSize(Sdf) || !MatchesSize(BentNormal))
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Effect texture dimensions do not match for %s."),
				*Set.BaseName));
			continue;
		}

		FString EffectAssetName = Identity;
		EffectAssetName = TEXT("MLFX_") + EffectAssetName;
		UMaterialLabEffect* Effect = CreateOrLoadEffect(
			AssetTools,
			EffectAssetName,
			EffectPath);
		if (!Effect)
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to create effect asset %s."), *EffectAssetName));
			continue;
		}

		Effect->Modify();
		SetEffectIdentity(*Effect, Set.BaseName);
		Effect->PeelData = PeelData;
		Effect->Mask = Mask;
		Effect->Height = Height;
		Effect->SDF = Sdf;
		Effect->BentNormal = BentNormal;
		Effect->MarkPackageDirty();
		SavePluginAsset(*Effect, TEXT("Material Lab effect"), Result.Errors);
		++Result.ImportedEffectCount;
	}

	UMaterialLabEffect* StainEffect = CreateOrLoadEffect(
		AssetTools,
		TEXT("MLFX_Stain"),
		TEXT("/MaterialLab/Effects/Stain"));
	if (!StainEffect)
	{
		Result.Errors.Add(TEXT("Failed to create the built-in Stain effect asset."));
	}
	else
	{
		StainEffect->Modify();
		StainEffect->DisplayName = NSLOCTEXT("MaterialLabImporter", "StainEffectName", "Stain");
		StainEffect->Category = TEXT("Stain");
		StainEffect->EffectType = EMaterialLabEffectType::Stain;
		StainEffect->SourceTextureBaseName = TEXT("BuiltIn_Stain");
		StainEffect->DefaultStainHeightWarp = 0.35f;
		StainEffect->MarkPackageDirty();
		SavePluginAsset(*StainEffect, TEXT("Material Lab effect"), Result.Errors);
		++Result.ImportedEffectCount;
	}

	return Result;
}

FString FMaterialLabSurfaceImporter::GetDefaultSourceDirectory()
{
	const TArray<FString> Directories = EnumerateShippedSourceDirectories();
	return Directories.IsEmpty() ? FString() : Directories[0];
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportDefaultLibrary()
{
	FMaterialLabImportResult Result;
	for (const FString& SourceDirectory : EnumerateShippedSourceDirectories())
	{
		const FMaterialLabImportResult Step = ImportDirectory(SourceDirectory);
		Result.ImportedSurfaceCount += Step.ImportedSurfaceCount;
		Result.ReimportedTextureCount += Step.ReimportedTextureCount;
		Result.ReusedTextureCount += Step.ReusedTextureCount;
		Result.GeneratedHeightCount += Step.GeneratedHeightCount;
		Result.Errors.Append(Step.Errors);
	}
	const FMaterialLabImportResult MaskStep = ImportShippedMasks();
	Result.ImportedMaskCount += MaskStep.ImportedMaskCount;
	Result.ReimportedTextureCount += MaskStep.ReimportedTextureCount;
	Result.ReusedTextureCount += MaskStep.ReusedTextureCount;
	Result.Errors.Append(MaskStep.Errors);

	const FMaterialLabImportResult NormalStep = ImportShippedNormals();
	Result.ImportedNormalCount += NormalStep.ImportedNormalCount;
	Result.ReimportedTextureCount += NormalStep.ReimportedTextureCount;
	Result.ReusedTextureCount += NormalStep.ReusedTextureCount;
	Result.Errors.Append(NormalStep.Errors);

	const FMaterialLabImportResult EffectStep = ImportShippedEffects();
	Result.ImportedEffectCount += EffectStep.ImportedEffectCount;
	Result.ReimportedTextureCount += EffectStep.ReimportedTextureCount;
	Result.ReusedTextureCount += EffectStep.ReusedTextureCount;
	Result.Errors.Append(EffectStep.Errors);
	if (Result.Errors.IsEmpty() && !Result.bCancelled)
	{
		Result.bCancelled = false;
	}
	return Result;
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ReimportShippedLibrary()
{
	return ImportDefaultLibrary();
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportFromDialog()
{
	FMaterialLabImportResult Result;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		Result.Errors.Add(TEXT("Desktop platform services are unavailable."));
		return Result;
	}

	FString SelectedDirectory;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!DesktopPlatform->OpenDirectoryDialog(
		ParentWindow,
		TEXT("Choose Material Lab texture export folder"),
		GetDefaultSourceDirectory(),
		SelectedDirectory))
	{
		Result.bCancelled = true;
		return Result;
	}

	return ImportDirectory(SelectedDirectory);
}

FMaterialLabImportResult FMaterialLabSurfaceImporter::ImportDirectory(const FString& SourceDirectory)
{
	using namespace MaterialLabImporter;
	FMaterialLabImportResult Result;

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(SourceDirectory / TEXT("*.png")), true, false);
	if (Files.IsEmpty())
	{
		Result.Errors.Add(TEXT("No PNG texture files were found in the selected folder."));
		return Result;
	}

	TMap<FString, FTextureSet> TextureSets;
	for (const FString& File : Files)
	{
		const FString Stem = FPaths::GetBaseFilename(File);
		FString BaseName;
		EMapType MapType;
		if (!ParseMapName(Stem, BaseName, MapType))
		{
			continue;
		}

		FTextureSet& Set = TextureSets.FindOrAdd(BaseName);
		Set.BaseName = BaseName;
		const FString FullPath = SourceDirectory / File;
		switch (MapType)
		{
		case EMapType::BaseColor: Set.BaseColorFile = FullPath; break;
		case EMapType::Normal: Set.NormalFile = FullPath; break;
		case EMapType::Ram:
			Set.RamFile = FullPath;
			break;
		case EMapType::RamHeight:
			Set.RamHeightFile = FullPath;
			break;
		default: break;
		}
	}

	UMaterial* PreviewMaster = LoadSubstrateMaster();
	if (!PreviewMaster)
	{
		Result.Errors.Add(TEXT("Required master material is missing: /MaterialLab/Materials/M_MaterialLab_Substrate"));
		return Result;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	for (const TPair<FString, FTextureSet>& Pair : TextureSets)
	{
		const FTextureSet& Set = Pair.Value;
		if (!Set.IsComplete())
		{
			TArray<FString> MissingMaps;
			if (Set.BaseColorFile.IsEmpty()) MissingMaps.Add(TEXT("_BC"));
			if (Set.NormalFile.IsEmpty()) MissingMaps.Add(TEXT("_N"));
			if (Set.RamFile.IsEmpty() && Set.RamHeightFile.IsEmpty())
			{
				MissingMaps.Add(TEXT("_RAM or _RAMH"));
			}
			Result.Errors.Add(FString::Printf(
				TEXT("%s is missing %s."),
				*Set.BaseName,
				*FString::Join(MissingMaps, TEXT(", "))));
			continue;
		}
		const bool bHasAuthoredHeight = !Set.RamHeightFile.IsEmpty();
		if (bHasAuthoredHeight && !Set.RamFile.IsEmpty())
		{
			Result.Errors.Add(FString::Printf(
				TEXT("%s contains both _RAM and _RAMH; authored _RAMH was selected."),
				*Set.BaseName));
		}

		FString Identity = Set.BaseName;
		Identity.RemoveFromStart(TEXT("TX_"));
		TArray<FString> Parts;
		Identity.ParseIntoArray(Parts, TEXT("_"), true);
		const FString Family = Parts.IsEmpty() ? TEXT("Uncategorized") : Parts[0];
		const FString TexturePath = FString::Printf(TEXT("/MaterialLab/Textures/%s/Raw"), *Family);
		const FString SurfacePath = FString::Printf(TEXT("/MaterialLab/Surfaces/%s"), *Family);
		FString SurfaceAssetName = Set.BaseName;
		SurfaceAssetName.RemoveFromStart(TEXT("TX_"));
		SurfaceAssetName = TEXT("ML_") + SurfaceAssetName;
		UMaterialLabSurface* Surface = CreateOrLoadSurface(
			AssetTools,
			SurfaceAssetName,
			SurfacePath);
		if (!Surface)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Failed to create surface asset %s."),
				*SurfaceAssetName));
			continue;
		}

		UTexture2D* BaseColor = ImportTexture(AssetTools, Set.BaseColorFile, TexturePath, EMapType::BaseColor, Result);
		UTexture2D* Normal = ImportTexture(AssetTools, Set.NormalFile, TexturePath, EMapType::Normal, Result);
		const FString& PackedSourceFile = bHasAuthoredHeight ? Set.RamHeightFile : Set.RamFile;
		UTexture2D* SourceRam = ImportTexture(
			AssetTools,
			PackedSourceFile,
			TexturePath,
			bHasAuthoredHeight ? EMapType::RamHeight : EMapType::Ram,
			Result);
		if (!BaseColor || !Normal || !SourceRam)
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to import the _BC, _N, and packed RAM texture set for %s."), *Set.BaseName));
			continue;
		}
		const FIntPoint BaseColorSourceSize(
			BaseColor->Source.GetSizeX(),
			BaseColor->Source.GetSizeY());
		const FIntPoint NormalSourceSize(
			Normal->Source.GetSizeX(),
			Normal->Source.GetSizeY());
		const FIntPoint RamSourceSize(
			SourceRam->Source.GetSizeX(),
			SourceRam->Source.GetSizeY());
		if (BaseColorSourceSize != NormalSourceSize
			|| BaseColorSourceSize != RamSourceSize)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Texture source dimensions do not match for %s: BC %dx%d, N %dx%d, RAM %dx%d."),
				*Set.BaseName,
				BaseColorSourceSize.X,
				BaseColorSourceSize.Y,
				NormalSourceSize.X,
				NormalSourceSize.Y,
				RamSourceSize.X,
				RamSourceSize.Y));
			continue;
		}

		UTexture2D* Ram = SourceRam;
		FString DerivedHeightSourceHash;
		EMaterialLabBlendHeightProvenance HeightProvenance = bHasAuthoredHeight
			? EMaterialLabBlendHeightProvenance::AuthoredRAMH
			: EMaterialLabBlendHeightProvenance::None;
		if (!bHasAuthoredHeight)
		{
			DerivedHeightSourceHash = MakeDerivedHeightSourceHash(
				Set.NormalFile,
				Set.RamFile,
				Normal->bFlipGreenChannel);
			UTexture2D* DerivedRAMH = LoadReusableDerivedRAMH(
				*Surface,
				TexturePath,
				Set.BaseName,
				DerivedHeightSourceHash);
			if (DerivedRAMH)
			{
				++Result.ReusedTextureCount;
			}
			else
			{
				DerivedRAMH = BuildDerivedRAMH(
					*Normal,
					*SourceRam,
					TexturePath,
					Set.BaseName,
					Result);
			}
			if (DerivedRAMH)
			{
				Ram = DerivedRAMH;
				HeightProvenance = EMaterialLabBlendHeightProvenance::DerivedFromNormal;
			}
		}

		UMaterialInstanceConstant* PreviewMaterial = CreateOrUpdatePreviewMaterial(
			AssetTools,
			*PreviewMaster,
			SurfaceAssetName,
			Family,
			*BaseColor,
			*Normal,
			*Ram,
			Surface->DefaultIOR);
		if (!PreviewMaterial)
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to create preview instance for %s."), *Set.BaseName));
			continue;
		}

		Surface->Modify();
		SetIdentity(*Surface, Set.BaseName, Family);
		Surface->BaseColor = BaseColor;
		Surface->Normal = Normal;
		Surface->RoughnessAOMetallic = Ram;
		Surface->bHasBlendHeight = HeightProvenance
			!= EMaterialLabBlendHeightProvenance::None;
		Surface->BlendHeightProvenance = HeightProvenance;
		Surface->DerivedHeightSourceHash = HeightProvenance
			== EMaterialLabBlendHeightProvenance::DerivedFromNormal
			? DerivedHeightSourceHash
			: FString();
		Surface->PreviewMaterial = PreviewMaterial;
		Surface->MarkPackageDirty();
		const bool bPreviewSaved = SavePluginAsset(
			*PreviewMaterial,
			TEXT("preview material instance"),
			Result.Errors);
		const bool bSurfaceSaved = SavePluginAsset(
			*Surface,
			TEXT("Material Lab surface"),
			Result.Errors);
		if (bPreviewSaved && bSurfaceSaved)
		{
			++Result.ImportedSurfaceCount;
		}
	}

	return Result;
}
