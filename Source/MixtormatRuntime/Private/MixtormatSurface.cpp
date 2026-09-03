#include "MixtormatSurface.h"

void UMixtormatSurface::PostLoad()
{
	Super::PostLoad();
	if (bHasBlendHeight
		&& BlendHeightProvenance == EMixtormatBlendHeightProvenance::None)
	{
		BlendHeightProvenance = EMixtormatBlendHeightProvenance::AuthoredRAMH;
	}
	bHasBlendHeight = BlendHeightProvenance
		!= EMixtormatBlendHeightProvenance::None;
}

FPrimaryAssetId UMixtormatSurface::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatSurface"), GetFName());
}
