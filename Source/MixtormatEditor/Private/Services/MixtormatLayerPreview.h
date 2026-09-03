#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
struct FMixtormatLayer;

class FMixtormatLayerPreview final
{
public:
	static UMaterial* CreateMaterial();
	static void ApplyLayers(
		UMaterialInstanceDynamic& MaterialInstance,
		const TArray<FMixtormatLayer>& Layers);
};
