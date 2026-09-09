#pragma once

#include "CoreMinimal.h"

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
	static FMixtormatThumbnailUpdate CreateOrUpdateMaskThumbnail(UTexture2D& MaskTexture);
};
