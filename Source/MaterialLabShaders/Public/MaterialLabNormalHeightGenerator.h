#pragma once

#include "CoreMinimal.h"

class MATERIALLABSHADERS_API FMaterialLabNormalHeightGenerator final
{
public:
	static bool Generate(
		TConstArrayView<FColor> NormalPixels,
		FIntPoint Size,
		bool bFlipNormalY,
		TArray<uint8>& OutHeight,
		FString& OutError);
};
