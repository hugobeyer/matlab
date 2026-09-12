// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMixtormatAssetMigrationReport
{
	bool bApplyRequested = false;
	bool bRenameSucceeded = false;
	int32 SurfaceAssetCount = 0;
	int32 PlannedSurfaceRenameCount = 0;
	int32 PlannedSupportRenameCount = 0;
	int32 AlreadyCanonicalCount = 0;
	int32 VerifiedTargetCount = 0;
	TArray<FString> RenameLines;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool CanApply() const;
	FString ToString() const;
};

class FMixtormatAssetMigration final
{
public:
	static FMixtormatAssetMigrationReport Run(bool bApply);
};
