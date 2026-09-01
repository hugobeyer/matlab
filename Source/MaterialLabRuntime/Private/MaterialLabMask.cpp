#include "MaterialLabMask.h"

FPrimaryAssetId UMaterialLabMask::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MaterialLabMask"), GetFName());
}
