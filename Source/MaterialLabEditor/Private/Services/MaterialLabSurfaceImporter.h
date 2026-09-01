#pragma once

#include "CoreMinimal.h"

struct FMaterialLabImportResult
{
	bool bCancelled = false;
	int32 ImportedSurfaceCount = 0;
	int32 ImportedMaskCount = 0;
	int32 ImportedNormalCount = 0;
	int32 ImportedEffectCount = 0;
	int32 ReimportedTextureCount = 0;
	int32 ReusedTextureCount = 0;
	int32 GeneratedHeightCount = 0;
	TArray<FString> Errors;

	FText ToMessage() const;
};

class FMaterialLabSurfaceImporter final
{
public:
	static FString GetPluginTexturesRoot();
	static TArray<FString> EnumerateShippedSourceDirectories();
	static FMaterialLabImportResult ImportShippedMasks();
	static FMaterialLabImportResult ImportShippedNormals();
	static FMaterialLabImportResult ImportShippedEffects();
	static FString GetDefaultSourceDirectory();
	static FMaterialLabImportResult ImportDefaultLibrary();
	static FMaterialLabImportResult ReimportShippedLibrary();
	static FMaterialLabImportResult ImportFromDialog();
	static FMaterialLabImportResult ImportDirectory(const FString& SourceDirectory);
};
