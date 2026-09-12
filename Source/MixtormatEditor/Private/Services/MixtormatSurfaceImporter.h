// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMixtormatImportResult
{
	bool bCancelled = false;
	int32 ImportedSurfaceCount = 0;
	int32 ImportedMaskCount = 0;
	int32 ImportedNormalCount = 0;
	int32 ImportedEffectCount = 0;
	int32 ReimportedTextureCount = 0;
	int32 ReusedTextureCount = 0;
	int32 GeneratedHeightCount = 0;
	int32 GeneratedThumbnailCount = 0;
	int32 ReusedThumbnailCount = 0;
	TArray<FString> Errors;

	FText ToMessage() const;
};

class FMixtormatSurfaceImporter final
{
public:
	static FString GetPluginTexturesRoot();
	static TArray<FString> EnumerateShippedSourceDirectories();
	static FMixtormatImportResult ImportShippedMasks();
	static FMixtormatImportResult ImportShippedNormals();
	static FMixtormatImportResult ImportShippedEffects();
	static FString GetDefaultSourceDirectory();
	static FMixtormatImportResult ImportDefaultLibrary();
	static FMixtormatImportResult ReimportShippedLibrary();
	static FMixtormatImportResult ImportFromDialog();
	static FMixtormatImportResult ImportMasksFromDialog();
	static FMixtormatImportResult ImportMaskDirectory(const FString& SourceDirectory);
	static FMixtormatImportResult ImportDirectory(
		const FString& SourceDirectory,
		bool bUsePluginDestination = false);
};
