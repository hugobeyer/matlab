#include "Services/MixtormatThumbnailRenderer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
#include "Services/MixtormatPaths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool ReadSourcePixels(UTexture2D& Texture, TArray<FColor>& OutPixels, FString& OutError)
	{
		const int32 Width = Texture.Source.GetSizeX();
		const int32 Height = Texture.Source.GetSizeY();
		if (Width <= 0 || Height <= 0)
		{
			OutError = FString::Printf(TEXT("Mask %s has no source pixels."), *Texture.GetPathName());
			return false;
		}
		const ETextureSourceFormat SourceFormat = Texture.Source.GetFormat();
		if (SourceFormat != TSF_G8 && SourceFormat != TSF_BGRA8)
		{
			OutError = FString::Printf(
				TEXT("Mask %s must use G8 or BGRA8 source pixels; received format %d."),
				*Texture.GetPathName(),
				static_cast<int32>(SourceFormat));
			return false;
		}

		const uint8* SourceData = Texture.Source.LockMip(0);
		if (!SourceData)
		{
			OutError = FString::Printf(TEXT("Failed to read mask source pixels for %s."), *Texture.GetPathName());
			return false;
		}
		OutPixels.SetNumUninitialized(Width * Height);
		if (SourceFormat == TSF_G8)
		{
			for (int32 PixelIndex = 0; PixelIndex < OutPixels.Num(); ++PixelIndex)
			{
				const uint8 Value = SourceData[PixelIndex];
				OutPixels[PixelIndex] = FColor(Value, Value, Value, 255);
			}
		}
		else
		{
			FMemory::Memcpy(OutPixels.GetData(), SourceData, OutPixels.Num() * sizeof(FColor));
		}
		Texture.Source.UnlockMip(0);
		return true;
	}

	TArray<FColor> ResizeMaskToThumbnail(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight)
	{
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		TArray<FColor> Output;
		Output.SetNumUninitialized(Resolution * Resolution);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			const float SourceY = FMath::Clamp(
				((static_cast<float>(Y) + 0.5f) * SourceHeight / Resolution) - 0.5f,
				0.0f,
				static_cast<float>(SourceHeight - 1));
			const int32 Y0 = FMath::FloorToInt(SourceY);
			const int32 Y1 = FMath::Min(Y0 + 1, SourceHeight - 1);
			const float YAlpha = SourceY - FMath::FloorToFloat(SourceY);

			for (int32 X = 0; X < Resolution; ++X)
			{
				const float SourceX = FMath::Clamp(
					((static_cast<float>(X) + 0.5f) * SourceWidth / Resolution) - 0.5f,
					0.0f,
					static_cast<float>(SourceWidth - 1));
				const int32 X0 = FMath::FloorToInt(SourceX);
				const int32 X1 = FMath::Min(X0 + 1, SourceWidth - 1);
				const float XAlpha = SourceX - FMath::FloorToFloat(SourceX);

				const float Top = FMath::Lerp(
					static_cast<float>(SourcePixels[Y0 * SourceWidth + X0].R),
					static_cast<float>(SourcePixels[Y0 * SourceWidth + X1].R),
					XAlpha);
				const float Bottom = FMath::Lerp(
					static_cast<float>(SourcePixels[Y1 * SourceWidth + X0].R),
					static_cast<float>(SourcePixels[Y1 * SourceWidth + X1].R),
					XAlpha);
				const uint8 Value = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(Top, Bottom, YAlpha)));
				Output[Y * Resolution + X] = FColor(Value, Value, Value, 255);
			}
		}
		return Output;
	}

	bool SourcePixelsMatch(UTexture2D& Texture, const TArray<FColor>& Pixels)
	{
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		if (Texture.Source.GetSizeX() != Resolution
			|| Texture.Source.GetSizeY() != Resolution
			|| Texture.Source.GetFormat() != TSF_BGRA8
			|| Pixels.Num() != Resolution * Resolution)
		{
			return false;
		}

		const uint8* ExistingData = Texture.Source.LockMip(0);
		if (!ExistingData)
		{
			return false;
		}
		const bool bMatches = FMemory::Memcmp(
			ExistingData,
			Pixels.GetData(),
			Pixels.Num() * sizeof(FColor)) == 0;
		Texture.Source.UnlockMip(0);
		return bMatches;
	}
}

FMixtormatThumbnailUpdate FMixtormatThumbnailRenderer::CreateOrUpdateMaskThumbnail(
	UTexture2D& MaskTexture)
{
	FMixtormatThumbnailUpdate Result;
	const int32 SourceWidth = MaskTexture.Source.GetSizeX();
	const int32 SourceHeight = MaskTexture.Source.GetSizeY();
	TArray<FColor> SourcePixels;
	if (!ReadSourcePixels(MaskTexture, SourcePixels, Result.Error))
	{
		return Result;
	}

	const TArray<FColor> ThumbnailPixels = ResizeMaskToThumbnail(
		SourcePixels,
		SourceWidth,
		SourceHeight);
	const FString AssetName = MaskTexture.GetName() + TEXT("_Thumbnail");
	const FString PackageName = FString::Printf(
		TEXT("%s/%s"),
		*FMixtormatPaths::MaskThumbnailsRoot(),
		*AssetName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);

	UTexture2D* Thumbnail = LoadObject<UTexture2D>(nullptr, *ObjectPath);
	const bool bCreated = Thumbnail == nullptr;
	if (bCreated)
	{
		if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
		{
			Result.Error = FString::Printf(
				TEXT("Cannot create mask thumbnail because an incompatible asset exists at %s."),
				*ExistingObject->GetPathName());
			return Result;
		}
		UPackage* Package = CreatePackage(*PackageName);
		Thumbnail = NewObject<UTexture2D>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
	}
	if (!Thumbnail)
	{
		Result.Error = FString::Printf(TEXT("Failed to create mask thumbnail %s."), *ObjectPath);
		return Result;
	}
	if (!bCreated && Thumbnail->AssetImportData
		&& !Thumbnail->AssetImportData->GetSourceData().SourceFiles.IsEmpty())
	{
		Result.Error = FString::Printf(
			TEXT("Refused to overwrite imported texture at mask thumbnail path %s."),
			*ObjectPath);
		return Result;
	}

	const bool bPixelsChanged = !SourcePixelsMatch(*Thumbnail, ThumbnailPixels);
	const bool bSettingsChanged = !Thumbnail->SRGB
		|| Thumbnail->CompressionSettings != TC_EditorIcon
		|| Thumbnail->LODGroup != TEXTUREGROUP_UI
		|| Thumbnail->MipGenSettings != TMGS_FromTextureGroup
		|| !Thumbnail->NeverStream
		|| Thumbnail->Filter != TF_Bilinear
		|| Thumbnail->AddressX != TA_Clamp
		|| Thumbnail->AddressY != TA_Clamp;
	Result.Texture = Thumbnail;
	Result.bChanged = bCreated || bPixelsChanged || bSettingsChanged;
	if (!Result.bChanged)
	{
		return Result;
	}

	Thumbnail->Modify();
	Thumbnail->PreEditChange(nullptr);
	if (bPixelsChanged || bCreated)
	{
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		Thumbnail->Source.Init(
			Resolution,
			Resolution,
			1,
			1,
			TSF_BGRA8,
			reinterpret_cast<const uint8*>(ThumbnailPixels.GetData()));
	}
	Thumbnail->SRGB = true;
	Thumbnail->CompressionSettings = TC_EditorIcon;
	Thumbnail->CompressionNoAlpha = true;
	Thumbnail->LODGroup = TEXTUREGROUP_UI;
	Thumbnail->MipGenSettings = TMGS_FromTextureGroup;
	Thumbnail->NeverStream = true;
	Thumbnail->Filter = TF_Bilinear;
	Thumbnail->AddressX = TA_Clamp;
	Thumbnail->AddressY = TA_Clamp;
	Thumbnail->PostEditChange();
	Thumbnail->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Thumbnail);
	}
	return Result;
}
