// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"
#include "UObject/NameTypes.h"

// The runtime parameter CONTRACT: hard validity bounds and the shader's mathematical
// behavior, for exactly the parameters that have one.
//
// This is deliberately SPARSE. Everything else about a parameter -- identity, type, default,
// family, UI ergonomics -- is discovered from reflection (the UPROPERTY itself: its name,
// its type, its CDO initializer, its Category, its UIMin/UIMax/Delta meta). Only the facts
// the shader implies and reflection cannot express live here, and a parameter without an
// entry simply has no bound: that is a legal, expected state, not an omission.
//
// Why this must be runtime code and not UPROPERTY meta: meta is editor-only and stripped
// from packaged builds, while the compositor sanitizes with these bounds in every target.
// (UI ergonomics have no such constraint, which is why THEY live in meta.)
//
// Keys are (Owner, FName) -- the parameter definition, never an instance. No LayerId, no
// ChildId, no family: the family a field belongs to is its UPROPERTY Category.
struct MIXTORMATRUNTIME_API FMixtormatParameterContract
{
	// Unset means unbounded in that direction. Set only where the math or the pass demands
	// it: divisors, saturate-fed channels, dispatch budgets.
	TOptional<float> HardMin;
	TOptional<float> HardMax;

	// The shader consumes the value as Value / NormalizationScale. 1.0 = none.
	float NormalizationScale = 1.0f;

	// The shader saturates this value (or the channel it feeds) after binding. Values past
	// the range stay legal -- they just stop changing the result.
	bool bShaderSaturates = false;
};

// Keys are (Owner, FName, ValueType) -- the parameter definition, never an instance. No
// LayerId, no ChildId, no family: the family a field belongs to is its UPROPERTY Category.
struct MIXTORMATRUNTIME_API FMixtormatParameterContractKey
{
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Effect;
	FName Parameter;
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	bool operator==(const FMixtormatParameterContractKey&) const = default;

	friend uint32 GetTypeHash(const FMixtormatParameterContractKey& Key)
	{
		// Unqualified on purpose: FName's overload lives in the global namespace but is not
		// guaranteed to be declared ahead of this header in every translation unit, and the
		// qualified form suppresses argument-dependent lookup -- which is exactly how this
		// failed to compile in one TU while passing in another.
		return HashCombine(
			HashCombine(GetTypeHash(Key.Owner), GetTypeHash(Key.Parameter)),
			GetTypeHash(Key.ValueType));
	}
};

// Kept as the shared identity type across the editor tooling (authoring database, dev menu,
// address display) so every surface spells the parameter identity the same way.
using FMixtormatParameterDefinitionKey = FMixtormatParameterContractKey;

namespace MixtormatParameterContracts
{
	// Null when the parameter carries no contract.
	MIXTORMATRUNTIME_API const FMixtormatParameterContract* TryGet(
		EMixtormatParameterOwnerType Owner, FName Parameter);

	// The single sanitization path the compositor uses for authored values: non-finite falls
	// back to the parameter's CDO default (read through reflection), then HardMin/HardMax
	// clamp where they exist. A parameter with no contract is still NaN-guarded to its
	// reflected default.
	MIXTORMATRUNTIME_API float SanitizeFloat(
		EMixtormatParameterOwnerType Owner, FName Parameter, float Value);
	MIXTORMATRUNTIME_API int32 SanitizeInt32(
		EMixtormatParameterOwnerType Owner, FName Parameter, int32 Value);

	// The parameter's compiled default -- its struct initializer, read from the reflected
	// CDO. Fallback applies only when the property itself cannot be found (stale name), which
	// dev-ensures. This is what keeps render-data defaults and reset values from ever
	// needing a second copy of the number.
	MIXTORMATRUNTIME_API float DefaultFloat(
		EMixtormatParameterOwnerType Owner, FName Parameter, float Fallback);
}
