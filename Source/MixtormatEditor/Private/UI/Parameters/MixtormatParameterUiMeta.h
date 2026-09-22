// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterDefinition.h"

// Editor-only parameter UI metadata, kept strictly apart from the runtime definitions.
//
// The runtime table (FMixtormatParameterDefinition) owns what a parameter IS: default, hard
// validity bounds, shader contract. This module owns what the SLIDER looks like: the ergonomic
// drag range and snap step. UiMin/UiMax are never restrictions -- SMixtormatSlider passes typed
// values through unclamped, so typing 8.0 on a 0..4 slider works and only a runtime HardMin /
// HardMax can stop a value. The lookup order everywhere is:
//
//     session dev override  ->  UiMeta  ->  the call site's literal arguments
//
// so existing slider call sites keep working unchanged while a family migrates.
//
// Nothing here serializes: the tables are code, the overrides are a session-lifetime map, and
// the whole module is editor-only. Packaged builds never see it.
struct FMixtormatParameterUiMeta
{
	float UiMin = 0.0f;
	float UiMax = 1.0f;
	float Snap = 0.0f;

	// Optional prose for the developer Parameter Info panel. Only for cases the structured
	// fields cannot say -- never a restatement of NormalizationScale/bShaderSaturates.
	const TCHAR* ShaderNote = nullptr;
};

namespace MixtormatParameterUi
{
	// The parameter-definition identity an address names. Canonical metadata is per definition,
	// never per instance -- no LayerId/ChildId -- so one dev override covers every instance of
	// a parameter. Unset when the address does not resolve to a parameter.
	TOptional<FMixtormatParameterDefinitionKey> DefinitionKeyOf(
		const FMixtormatParameterAddress& Address);

	// Gated by the Mixtormat.Developer.ParameterMeta console variable. The Developer submenu in
	// the parameter context menu exists only while this is true.
	bool IsDeveloperMetaEnabled();

	// The UiMeta row for a parameter, null for unmigrated families.
	const FMixtormatParameterUiMeta* TryGetUiMeta(
		const FMixtormatParameterDefinitionKey& Key);

	// One bound of the slider range for a parameter: dev override, else UiMeta, else Fallback.
	// The current-value getter already rebuilds the address every paint, so this is written to
	// be called per frame from slider attributes.
	float ResolveUiBound(
		const FMixtormatParameterDefinitionKey& Key,
		const float Fallback,
		const bool bMax);

	// The snap step; overrides never touch it, only the UiMeta table can change it.
	float ResolveUiSnap(const FMixtormatParameterDefinitionKey& Key, const float Fallback);

	// Dev-session UI range overrides, keyed by parameter definition so one override covers
	// every instance of the parameter. Never serialized, never touches HardMin/HardMax, dies
	// with the editor process.
	bool HasUiRangeOverride(const FMixtormatParameterDefinitionKey& Key);
	void SetUiRangeOverride(
		const FMixtormatParameterDefinitionKey& Key, float Min, float Max);
	void ClearUiRangeOverride(const FMixtormatParameterDefinitionKey& Key);

	// Migration consistency guard: when a call site's literal arguments and the UiMeta table
	// both exist and disagree, say so once per parameter instead of silently picking one.
	// Literal default mismatches against the runtime definition are caught by the automation
	// test in MixtormatParameterDefinitionTests; this covers what that test cannot see, a
	// UiMeta row edited without its call site.
	void ReportLiteralMismatch(
		const FMixtormatParameterDefinitionKey& Key,
		float LiteralMin, float LiteralMax, float LiteralSnap);
}
