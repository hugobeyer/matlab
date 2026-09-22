// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatParameterDefinition.h"

#include "Misc/AssertionMacros.h"
#include "UObject/Class.h"

// The Breakup pilot table. Every entry was read off four sources, not one: the serialized
// initializer in FMixtormatLayerEffect, the compositor's validation block in
// RequestComposeInternal, the shader's actual use of the uniform in MixtormatBreakup.usf, and
// the inspector's current slider literals. Where those disagreed, the entry records the
// *validity* bound, not the old UI bound -- see the saturates/normalization notes.
//
// Later effect families append here in their own migration; nothing else in the engine
// depends on the table being complete.
namespace
{
	// Shared shader-side divisor floor for the width/radius parameters whose falloff math
	// divides by them.
	constexpr float DivisorFloor = 1.0e-4f;

	const TArray<FMixtormatParameterDefinition>& Definitions()
	{
		static const TArray<FMixtormatParameterDefinition> Table = []()
		{
			using ET = EMixtormatParameterOwnerType;
			using VT = EMixtormatParameterValueType;
			using Policy = EMixtormatParameterAuthoringPolicy;

			return TArray<FMixtormatParameterDefinition>({
				// Shape. Scale's upper bound is the derived cell-count budget, not taste.
				{ET::Effect, TEXT("BreakupScale"), VT::Int, 6.0f, 1.0f, 64.0f, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupDensity"), VT::Float, 0.72f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				// Size feeds the shape metric's radius; zero or negative cannot exist as a piece.
				{ET::Effect, TEXT("BreakupSize"), VT::Float, 0.32f, 0.001f, {}, 1.0f, false, Policy::PersistentDevTunable},
				// The shader lerp(1.0, max(stretchAmount, 1.0), rt) and then flips the reciprocal
				// per piece, so the value is a magnitude with randomized orientation: below 1 is
				// folded into the same >= 1 magnitude. The hard floor is 1, matching the shader.
				{ET::Effect, TEXT("BreakupStretch"), VT::Float, 1.6f, 1.0f, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupAngularity"), VT::Float, 0.72f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupIrregularity"), VT::Float, 0.38f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupInset"), VT::Float, 0.0f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupDistortion"), VT::Float, 5.6f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				// Integer-period sinusoid frequency; 0 has no period.
				{ET::Effect, TEXT("BreakupDistortionFrequency"), VT::Int, 3.0f, 1.0f, 16.0f, 1.0f, false, Policy::PersistentDevTunable},

				// Structure.
				{ET::Effect, TEXT("BreakupRelief"), VT::Float, -0.06f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupThicknessVariation"), VT::Float, 0.30f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupGapWidth"), VT::Float, 2.0f, 0.0f, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupGapDepth"), VT::Float, 0.02f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupGapVariation"), VT::Float, 0.35f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupFold"), VT::Float, 0.025f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				// Widths reach divisors in the falloff math.
				{ET::Effect, TEXT("BreakupFoldWidth"), VT::Float, 16.0f, DivisorFloor, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupCrease"), VT::Float, 0.018f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupCreaseWidth"), VT::Float, 1.25f, DivisorFloor, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupPush"), VT::Float, 0.0f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupPushWidth"), VT::Float, 24.0f, DivisorFloor, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupPushRelief"), VT::Float, 0.035f, 0.0f, {}, 1.0f, false, Policy::PersistentDevTunable},

				// Variation. The shader saturates Variation itself.
				{ET::Effect, TEXT("BreakupDetail"), VT::Float, 0.5f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupVariation"), VT::Float, 0.25f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},

				// Shading.
				{ET::Effect, TEXT("BreakupAmount"), VT::Float, 1.0f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				// The channel result saturates after the signed offset, so any magnitude is legal.
				{ET::Effect, TEXT("BreakupRoughnessAmount"), VT::Float, 0.0f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupNormalStrength"), VT::Float, 2.0f, 0.0f, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupNormalSharpness"), VT::Float, 0.75f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupAOAmount"), VT::Float, 0.35f, {}, {}, 1.0f, true, Policy::PersistentDevTunable},
				// AO radius divides in the occlusion falloff.
				{ET::Effect, TEXT("BreakupAORadius"), VT::Float, 8.0f, 1.0f, {}, 1.0f, false, Policy::PersistentDevTunable},

				// Size spread feeds the floored SizeMin/SizeMax derivation, so it needs no hard
				// bound of its own. Smoothness is the CSG blend radius; negative inverts a blend.
				{ET::Effect, TEXT("BreakupSizeVariation"), VT::Float, 0.3125f, {}, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupSmoothness"), VT::Float, 0.30f, 0.0f, {}, 1.0f, false, Policy::PersistentDevTunable},

				// Placement: authoring-tunable like everything else the developer may retune.
				{ET::Effect, TEXT("BreakupMaskTiling"), VT::Int, 1.0f, 1.0f, {}, 1.0f, false, Policy::PersistentDevTunable},
				{ET::Effect, TEXT("BreakupSeed"), VT::Int, 1.0f, 0.0f, {}, 1.0f, false, Policy::PersistentDevTunable},
			});
		}();

		return Table;
	}

	const TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterDefinition*>& Index()
	{
		static const TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterDefinition*> Map = []()
		{
			TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterDefinition*> Built;
			for (const FMixtormatParameterDefinition& Definition : Definitions())
			{
				Built.Add(Definition.GetKey(), &Definition);
			}
			return Built;
		}();

		return Map;
	}
}

float FMixtormatParameterDefinition::SanitizeFloat(const float Value) const
{
	if (!FMath::IsFinite(Value))
	{
		return Default;
	}
	float Sanitized = Value;
	if (HardMin.IsSet())
	{
		Sanitized = FMath::Max(Sanitized, HardMin.GetValue());
	}
	if (HardMax.IsSet())
	{
		Sanitized = FMath::Min(Sanitized, HardMax.GetValue());
	}
	return Sanitized;
}

int32 FMixtormatParameterDefinition::SanitizeInt32(const int32 Value) const
{
	float Sanitized = SanitizeFloat(static_cast<float>(Value));
	return FMath::RoundToInt(Sanitized);
}

namespace MixtormatParameterDefinitions
{
	const FMixtormatParameterDefinition* TryGet(const FMixtormatParameterDefinitionKey& Key)
	{
		return Index().FindRef(Key);
	}

	namespace
	{
		const FMixtormatParameterDefinition& GetOrFallback(
			const EMixtormatParameterOwnerType Owner,
			const FName Parameter,
			const EMixtormatParameterValueType ValueType)
		{
			static const FMixtormatParameterDefinition Neutral;
			const FMixtormatParameterDefinition* Found = TryGet({Owner, Parameter, ValueType});
			if (!ensureMsgf(Found,
				TEXT("Mixtormat: no parameter definition for %s.%s -- the key is stale or misspelled"),
				*StaticEnum<EMixtormatParameterOwnerType>()->GetNameByValue(static_cast<int64>(Owner)).ToString(),
				*Parameter.ToString()))
			{
				return Neutral;
			}
			return *Found;
		}
	}

	float SanitizeFloat(
		const EMixtormatParameterOwnerType Owner,
		const FName Parameter,
		const EMixtormatParameterValueType ValueType,
		const float Value)
	{
		return GetOrFallback(Owner, Parameter, ValueType).SanitizeFloat(Value);
	}

	int32 SanitizeInt32(
		const EMixtormatParameterOwnerType Owner,
		const FName Parameter,
		const int32 Value)
	{
		return GetOrFallback(Owner, Parameter, EMixtormatParameterValueType::Int).SanitizeInt32(Value);
	}

	float DefaultFloat(
		const EMixtormatParameterOwnerType Owner,
		const FName Parameter,
		const float Fallback)
	{
		const FMixtormatParameterDefinition* Found = TryGet({Owner, Parameter, EMixtormatParameterValueType::Float});
		if (Found)
		{
			return Found->Default;
		}
		ensureMsgf(false,
			TEXT("Mixtormat: no parameter definition for Effect.%s -- render-data fallback %g applied"),
			*Parameter.ToString(), Fallback);
		return Fallback;
	}

	void ForEach(TFunctionRef<void(const FMixtormatParameterDefinition&)> Visitor)
	{
		for (const FMixtormatParameterDefinition& Definition : Definitions())
		{
			Visitor(Definition);
		}
	}
}
