#pragma once

#include "CoreMinimal.h"

class MIXTORMATSHADERS_API FMixtormatNormalHeightGenerator final
{
public:
	static bool Generate(
		TConstArrayView<FColor> NormalPixels,
		FIntPoint Size,
		bool bFlipNormalY,
		TArray<uint8>& OutHeight,
		FString& OutError);
};
