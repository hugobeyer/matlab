#include "MaterialLabSurface.h"

void UMaterialLabSurface::PostLoad()
{
	Super::PostLoad();
	if (bHasBlendHeight
		&& BlendHeightProvenance == EMaterialLabBlendHeightProvenance::None)
	{
		BlendHeightProvenance = EMaterialLabBlendHeightProvenance::AuthoredRAMH;
	}
	bHasBlendHeight = BlendHeightProvenance
		!= EMaterialLabBlendHeightProvenance::None;
}

FPrimaryAssetId UMaterialLabSurface::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MaterialLabSurface"), GetFName());
}
