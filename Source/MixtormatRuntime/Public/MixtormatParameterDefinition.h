// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatEffect.h"
#include "MixtormatMaterial.h"
#include "UObject/NameTypes.h"

// Canonical parameter definitions: one runtime-safe record of what a parameter *is* -- its
// default, its hard validity bounds, and the shader's mathematical contract (normalization,
// saturation) -- keyed by owner + name + value type.
//
// Deliberately NOT UPROPERTY metadata. Editor-only meta (ClampMin/UIMax and friends) is
// stripped from packaged builds and must never be what runtime validation reads; this header
// has no reflection dependency and is safe for the compositor, the render-data defaults and
// the binding layer alike.
//
// The key is the parameter *definition*, never an instance: no LayerId, no ChildId. A driver,
// a reference and a direct edit all land on the same definition, and one dev UI-range override
// covers every instance of the parameter at once.
//
// HardMin/HardMax are legal/runtime-safety bounds only -- divisors, saturate-fed channels,
// dispatch budgets. They are not the slider range and must never grow to match one. The UI's
// ergonomic range lives in the editor module (MixtormatParameterUiMeta); a slider may stop at
// 4.0 while HardMax is unset, and a typed 8.0 or 16.0 then reaches the shader intact.
// Shared shader-side divisor floor for the width/radius parameters whose falloff math
// divides by them. Declared here because several family definition files need the same floor.
constexpr float MixtormatParameterDivisorFloor = 1.0e-4f;

struct MIXTORMATRUNTIME_API FMixtormatParameterDefinitionKey
{
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Effect;
	FName Parameter;
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	bool operator==(const FMixtormatParameterDefinitionKey&) const = default;

	friend uint32 GetTypeHash(const FMixtormatParameterDefinitionKey& Key)
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

// How a parameter participates in authoring. Only PersistentDevTunable parameters get the
// Developer menu's persistent editing UI (label/default/UI range/snap, saved into the plugin's
// authoring database and shipped with Mixtormat). Structural plumbing, identity, placement and
// enum/bool fields have no definitions at all, so they are excluded by construction; Fixed
// exists for the numeric fields that are real parameters but whose shipped numbers are final
// (placement tiling, seeds).
enum class EMixtormatParameterAuthoringPolicy : uint8
{
	// Engineering value: compiled default only, no authoring UI.
	Fixed,
	// Migrated: has a definition and UI metadata, but the shipped numbers are final.
	Tunable,
	// Authoring-tunable: persistently editable through the plugin authoring database.
	PersistentDevTunable
};

struct MIXTORMATRUNTIME_API FMixtormatParameterDefinition
{
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Effect;
	FName Parameter;
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	// The reset/creation value. Serialized structs keep their own initializers for now; a
	// validation test asserts the two never drift.
	float Default = 0.0f;

	// Unset means unbounded in that direction. Set only where the math or the pass actually
	// requires it.
	TOptional<float> HardMin;
	TOptional<float> HardMax;

	// The shader consumes the value as Value / NormalizationScale. 1.0 means none. Displayed
	// by the developer Parameter Info panel so the effective scale is visible without opening
	// the .usf.
	float NormalizationScale = 1.0f;

	// The shader saturates this value (or the channel it feeds) after binding. Displayed as a
	// contract note, never enforced editor-side: values past 1.0 remain legal, they just stop
	// changing the result.
	bool bShaderSaturates = false;

	// Authoring participation. Tunable unless a family migration says otherwise; every
	// definition today is Breakup, so the family default follows.
	EMixtormatParameterAuthoringPolicy Policy = EMixtormatParameterAuthoringPolicy::Tunable;
	EMixtormatEffectType EffectFamily = EMixtormatEffectType::Breakup;

	FMixtormatParameterDefinitionKey GetKey() const
	{
		return {Owner, Parameter, ValueType};
	}

	// Non-finite falls back to Default; HardMin/HardMax then clamp where they exist. This is
	// the single sanitization path the compositor uses for authored values.
	float SanitizeFloat(float Value) const;

	// Integer companion: rounds through the same hard bounds. Used for pass-budget params
	// (cell counts, iterations) whose bounds are dispatch safety, not UI ergonomics.
	int32 SanitizeInt32(int32 Value) const;
};

namespace MixtormatParameterDefinitions
{
	// Null when no definition exists -- the normal case for a not-yet-migrated family. Callers
	// must treat null as "no opinion" and keep whatever fallback they already use.
	MIXTORMATRUNTIME_API const FMixtormatParameterDefinition* TryGet(
		const FMixtormatParameterDefinitionKey& Key);

	// Convenience wrappers for the compositor's hot path. A missing definition is a bug in the
	// caller's key: it ensures in dev and applies Fallback in shipping, so a typo can never
	// turn a guarded value into an unguarded one.
	MIXTORMATRUNTIME_API float SanitizeFloat(
		EMixtormatParameterOwnerType Owner,
		FName Parameter,
		EMixtormatParameterValueType ValueType,
		float Value);
	MIXTORMATRUNTIME_API int32 SanitizeInt32(
		EMixtormatParameterOwnerType Owner,
		FName Parameter,
		int32 Value);

	// The definition's default, or Fallback when the key is absent (dev-ensured, same reason
	// as above). Used by render-data member initializers so the struct's defaults and the
	// table cannot drift apart silently.
	MIXTORMATRUNTIME_API float DefaultFloat(
		EMixtormatParameterOwnerType Owner,
		FName Parameter,
		float Fallback);

	// Enumerates every registered definition. Test and tooling surface; the compositor and UI
	// paths use the keyed lookups above.
	MIXTORMATRUNTIME_API void ForEach(TFunctionRef<void(const FMixtormatParameterDefinition&)> Visitor);

	// How family definition files register their entries at module load. Each effect family
	// owns one MixtormatParameterDefinitions.<Family>.cpp and calls this exactly once from a
	// static initializer; the keyed lookups above are lazy, so every file is registered before
	// the first query. Duplicate keys ensure in dev and keep the first registration.
	MIXTORMATRUNTIME_API void Register(TConstArrayView<FMixtormatParameterDefinition> Entries);
}
