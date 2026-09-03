#include "MixtormatMask.h"

FPrimaryAssetId UMixtormatMask::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatMask"), GetFName());
}
