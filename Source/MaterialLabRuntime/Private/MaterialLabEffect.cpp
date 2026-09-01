#include "MaterialLabEffect.h"

FPrimaryAssetId UMaterialLabEffect::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MaterialLabEffect"), GetFName());
}
