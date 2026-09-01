#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
struct FMaterialLabLayer;

class FMaterialLabLayerPreview final
{
public:
	static UMaterial* CreateMaterial();
	static void ApplyLayers(
		UMaterialInstanceDynamic& MaterialInstance,
		const TArray<FMaterialLabLayer>& Layers);
};
