// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatThemeSchema.h"
#include "Services/MixtormatPaths.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// Function-local statics rather than namespace-scope ones: these are non-trivial aggregates and
	// a static initialisation order dependency between this file and anything that reads the theme
	// during module load would be a genuinely hard bug to find.
	Mixtormat::FMixtormatTheme GTheme;
	Mixtormat::FMixtormatResolvedStyle GResolved;
	TArray<FText> GValidationIssues;
	bool GInitialised = false;
	bool GRebuildRequested = false;
	FMixtormatThemeStore::EStartupLoadResult GStartupLoadResult = FMixtormatThemeStore::EStartupLoadResult::UsingCompiledDefaults;
}

void FMixtormatThemeStore::EnsureInitialised()
{
	if (!GInitialised)
	{
		GInitialised = true;
		LoadSavedThemeOrDefaults();
	}
}

void FMixtormatThemeStore::LoadSavedThemeOrDefaults()
{
	// Schema loading merges recognized saved values over the current theme. Seed it first so
	// themes written before a later schema addition inherit the authored defaults for that field.
	GTheme = Mixtormat::MakeDefaultTheme();
	Mixtormat::ValidateTheme(GTheme, GValidationIssues);
	Mixtormat::ResolveTheme(GTheme, GResolved);

	const FString SavePath = Mixtormat::FMixtormatThemeSchema::SavePath();
	if (!IFileManager::Get().FileExists(*SavePath))
	{
		GStartupLoadResult = EStartupLoadResult::UsingCompiledDefaults;
		return;
	}

	FString Error;
	TArray<FText> Issues;
	if (Mixtormat::FMixtormatThemeSchema::Load(Error, Issues))
	{
		GStartupLoadResult = EStartupLoadResult::LoadedSavedTheme;
		return;
	}

	GTheme = Mixtormat::MakeDefaultTheme();
	Mixtormat::ValidateTheme(GTheme, GValidationIssues);
	Mixtormat::ResolveTheme(GTheme, GResolved);
	GStartupLoadResult = EStartupLoadResult::SavedThemeInvalid;
	GValidationIssues.Add(FText::FromString(Error));
}

const Mixtormat::FMixtormatResolvedStyle& FMixtormatThemeStore::GetResolved()
{
	EnsureInitialised();
	return GResolved;
}

const Mixtormat::FMixtormatTheme& FMixtormatThemeStore::GetTheme()
{
	EnsureInitialised();
	return GTheme;
}

void FMixtormatThemeStore::SetTheme(Mixtormat::FMixtormatTheme InTheme)
{
	EnsureInitialised();

	GTheme = MoveTemp(InTheme);

	// Validate before resolve, so the resolved colours correspond to the numbers that survived
	// validation rather than to the ones the author briefly typed.
	GValidationIssues.Reset();
	Mixtormat::ValidateTheme(GTheme, GValidationIssues);
	Mixtormat::ResolveTheme(GTheme, GResolved);

	GRebuildRequested = true;
}

void FMixtormatThemeStore::Refresh()
{
	EnsureInitialised();

	GValidationIssues.Reset();
	Mixtormat::ValidateTheme(GTheme, GValidationIssues);
	Mixtormat::ResolveTheme(GTheme, GResolved);

	GRebuildRequested = true;
}

void FMixtormatThemeStore::ResetToDefaults()
{
	SetTheme(Mixtormat::MakeDefaultTheme());
}

const TArray<FText>& FMixtormatThemeStore::GetValidationIssues()
{
	EnsureInitialised();
	return GValidationIssues;
}

bool FMixtormatThemeStore::ConsumeRebuildRequest()
{
	EnsureInitialised();

	// Cleared on read: a flag that stayed set would rebuild every widget on the next unrelated edit.
	const bool bWasRequested = GRebuildRequested;
	GRebuildRequested = false;
	return bWasRequested;
}

FMixtormatThemeStore::EStartupLoadResult FMixtormatThemeStore::GetStartupLoadResult()
{
	EnsureInitialised();
	return GStartupLoadResult;
}