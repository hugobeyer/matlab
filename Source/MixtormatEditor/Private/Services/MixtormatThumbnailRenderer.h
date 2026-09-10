#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;

struct FMixtormatThumbnailUpdate
{
	UTexture2D* Texture = nullptr;
	bool bChanged = false;
	FString Error;

	bool IsValid() const
	{
		return Texture != nullptr && Error.IsEmpty();
	}
};

class FMixtormatThumbnailRenderer final
{
public:
	FMixtormatThumbnailRenderer();
	~FMixtormatThumbnailRenderer();

	FMixtormatThumbnailRenderer(const FMixtormatThumbnailRenderer&) = delete;
	FMixtormatThumbnailRenderer& operator=(const FMixtormatThumbnailRenderer&) = delete;

	static FMixtormatThumbnailUpdate CreateOrUpdateMaskThumbnail(
		UTexture2D& MaskTexture,
		const FString& DestinationPath);
	FMixtormatThumbnailUpdate CreateOrUpdateSurfaceThumbnail(
		UMaterialInterface& PreviewMaterial,
		const FString& DestinationPath,
		const FString& SurfaceAssetName);

private:
	class FImpl;
	TUniquePtr<FImpl> Impl;
};
