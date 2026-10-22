// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatLayerTypes.h"

// Ramp payloads retain their authored domain. Applying to another domain requires an explicit remap.
namespace MixtormatPresetStore
{
	FString GetUserPresetDirectory();
	enum class ERampKind : uint8 { Scalar, Color };

	struct FRampPreset
	{
		FGuid Id;
		FString Name;
		ERampKind Kind = ERampKind::Scalar;
		float DomainMin = 0.0f;
		float DomainMax = 1.0f;
		FMixtormatScalarRamp Scalar;
		FMixtormatColorRamp Color;
	};

	struct FDocumentInfo
	{
		FGuid Id = FGuid::NewGuid();
		FString Name;
		FString Description;
		FString Category;
		TArray<FSoftObjectPath> Dependencies;
		FString Filename;
		bool bShipped = false;
	};

	struct FModulePreset : FDocumentInfo
	{
		// Built-ins carry their payload in memory; file presets are reloaded on apply.
		bool bBuiltIn = false;
		FMixtormatLayerChild Settings;
	};

	struct FRecipePreset : FDocumentInfo
	{
		FMixtormatLayerGroup Group;
		TArray<FMixtormatLayer> Layers;
	};

	bool CaptureModule(const FMixtormatLayerChild& Child, FModulePreset& OutPreset, FText& Error);
	bool ApplyModule(const FModulePreset& Preset, FMixtormatLayerChild& Destination, FText& Error);
	bool IsModuleCompatible(const FMixtormatLayerChild& A, const FMixtormatLayerChild& B);
	bool SaveModule(const FModulePreset& Preset, FText& Error);
	bool LoadModule(const FString& Filename, FModulePreset& OutPreset, FText& Error);
	bool ListModules(const FMixtormatLayerChild& Child, TArray<FModulePreset>& OutPresets, FText& Error);
	bool ImportModule(const FString& Filename, const FMixtormatLayerChild& Expected, FText& Error);
	bool ExportModule(const FString& Filename, const FModulePreset& Preset, FText& Error);
	bool SaveRecipe(const FRecipePreset& Preset, FText& Error);
	bool LoadRecipe(const FString& Filename, FRecipePreset& OutPreset, FText& Error);
	bool ListRecipes(TArray<FRecipePreset>& OutPresets, FText& Error);
	bool ImportRecipe(const FString& Filename, FText& Error);
	bool ExportRecipe(const FString& Filename, const FRecipePreset& Preset, FText& Error);

	bool ListRamps(ERampKind Kind, TArray<FRampPreset>& OutPresets, FText& OutError);
	bool SaveRamp(const FRampPreset& Preset, FText& OutError);
	bool ImportRamp(const FString& Filename, ERampKind ExpectedKind, FRampPreset& OutPreset, FText& OutError);
	bool ExportRamp(const FString& Filename, const FRampPreset& Preset, FText& OutError);
}
