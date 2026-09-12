// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatEffect.h"

FPrimaryAssetId UMixtormatEffect::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatEffect"), GetFName());
}
