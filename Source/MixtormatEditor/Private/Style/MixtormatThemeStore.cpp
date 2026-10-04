// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatThemeStore.h"

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
}

void FMixtormatThemeStore::EnsureInitialised()
{
	if (!GInitialised)
	{
		GInitialised = true;
		GTheme = Mixtormat::MakeDefaultTheme();
		Mixtormat::ValidateTheme(GTheme, GValidationIssues);
		Mixtormat::ResolveTheme(GTheme, GResolved);
	}
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