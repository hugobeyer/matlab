// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatMask.h"

FPrimaryAssetId UMixtormatMask::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatMask"), GetFName());
}
