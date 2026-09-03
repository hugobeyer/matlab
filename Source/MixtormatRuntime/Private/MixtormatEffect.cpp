#include "MixtormatEffect.h"

FPrimaryAssetId UMixtormatEffect::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatEffect"), GetFName());
}
