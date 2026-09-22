// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatParameterDefinition.h"

// Reflection-backed UI metadata for parameters. There is no table here: the UPROPERTYs
// themselves carry the ergonomics --
//
//     meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.05")
//
// -- and this module reads them (with the CDO initializer as the default value) through one
// cached lookup per parameter. UPROPERTY meta is editor-only and stripped from packaged
// builds, which is exactly right: nothing but the editor UI consumes it. Runtime safety
// bounds live in the runtime contract table (MixtormatParameterDefinition.h) and can never
// be weakened from here.
//
// A parameter whose property carries no UI meta at all resolves as "not found" and callers
// fall back to their literals -- which is what makes family-by-family migration possible.
//
// Everything in this module is editor-only; packaged builds never see it.
struct FMixtormatParameterUiResolution
{
	float UiMin = 0.0f;
	float UiMax = 1.0f;
	float Snap = 0.0f;
	// The compiled default: the CDO initializer, in stored units.
	float Default = 0.0f;
};

namespace MixtormatParameterUi
{
	// The parameter-definition identity an address names. Canonical metadata is per definition,
	// never per instance -- no LayerId/ChildId -- so one dev override covers every instance of
	// a parameter. Unset when the address does not resolve to a parameter.
	TOptional<FMixtormatParameterDefinitionKey> DefinitionKeyOf(
		const FMixtormatParameterAddress& Address);

	// Gated by the Mixtormat.Developer.ParameterMeta console variable. The Developer submenu
	// in the parameter context menu exists only while this is true.
	bool IsDeveloperMetaEnabled();

	// Reflection resolution for one parameter. bFound requires the property to exist AND
	// carry at least one of UIMin/UIMax/Delta -- unannotated families fall back to call-site
	// literals until their migration annotates them. Default is always resolved when the
	// property exists (it comes from the CDO, not from meta).
	bool TryResolveUi(const FMixtormatParameterDefinitionKey& Key, FMixtormatParameterUiResolution& Out);

	// The numeric FProperty for a parameter key, null when the owner struct has no such
	// numeric property (structural fields, unmigrated names, stale keys).
	FProperty* TryFindNumericProperty(const FMixtormatParameterDefinitionKey& Key);

	// Session-only UI range override: dev experimentation, never serialized, never touches
	// hard bounds, dies with the editor process.
	bool HasUiRangeOverride(const FMixtormatParameterDefinitionKey& Key);
	void SetUiRangeOverride(const FMixtormatParameterDefinitionKey& Key, float Min, float Max);
	void ClearUiRangeOverride(const FMixtormatParameterDefinitionKey& Key);

	// One bound / snap of the effective UI range: session override -> reflection -> Fallback.
	// The current-value getter already rebuilds addresses every paint, so these are written
	// to be called per frame from slider attributes.
	float ResolveUiBound(const FMixtormatParameterDefinitionKey& Key, float Fallback, bool bMax);
	float ResolveUiSnap(const FMixtormatParameterDefinitionKey& Key, float Fallback);
	float ResolveUiDefault(const FMixtormatParameterDefinitionKey& Key, float Fallback);

	// Migration guard: warns once per parameter when a call site's literal range disagrees
	// with the property's meta. Guarded by a warned-set; cheap enough to call per paint.
	void ReportLiteralMismatch(
		const FMixtormatParameterDefinitionKey& Key,
		float LiteralMin, float LiteralMax, float LiteralSnap);
}
