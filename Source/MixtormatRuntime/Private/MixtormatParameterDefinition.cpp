// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatParameterDefinition.h"
#include "MixtormatReliefScaling.h"

#include "Misc/AssertionMacros.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

// The sparse contract table. One row per parameter that actually carries a bound or a
// documented shader behavior; everything else about every parameter is reflection.
//
// A row is added only when the shader's math demands it: the value feeds a divisor
// (widths, radii, kernels), drives a dispatch budget (ray counts, cell counts, iterations),
// feeds a saturate() (recorded as bShaderSaturates), or has a normalization scale. If none
// of those apply, the parameter has NO row and needs none.
namespace
{
	// Divisor floor shared by every width/radius that reaches a falloff division.
	constexpr float DivisorFloor = 1.0e-4f;

	const TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterContract>& Contracts()
	{
		static const TMap<FMixtormatParameterContractKey, FMixtormatParameterContract> Table = []()
		{
			using ET = EMixtormatParameterOwnerType;
			TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterContract> Built;

			const auto Add = [&Built](ET Owner, const TCHAR* Name, FMixtormatParameterContract C)
			{
				FMixtormatParameterDefinitionKey Key;
				Key.Owner = Owner;
				Key.Parameter = FName(Name);
				Built.Add(Key, C);
			};
			const auto Saturated = [](FMixtormatParameterContract C = {})
			{
				C.bShaderSaturates = true;
				return C;
			};

			// ---- Layer-owned shader facts.
			Add(ET::Layer, TEXT("HeightBoost"), {.HardMin = 0.0f, .HardMax = MixtormatRelief::MaxHeightBoost});
			Add(ET::Layer, TEXT("NormalIntensity"), {.HardMin = 0.0f, .HardMax = MixtormatRelief::MaxNormalStrength});
			Add(ET::Layer, TEXT("FuzzInfluence"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Layer, TEXT("F0Influence"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Layer, TEXT("NormalInfluence"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Layer, TEXT("HeightInfluence"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Layer, TEXT("IOR"), {.HardMin = 1.0f, .HardMax = 3.0f});

			// ---- Breakup.
			// Scale's upper bound is the derived cell-count budget, not taste.
			Add(ET::Effect, TEXT("BreakupScale"), {.HardMin = 1.0f, .HardMax = 64.0f});
			Add(ET::Effect, TEXT("BreakupDensity"), Saturated());
			// Size feeds the shape metric's radius; zero or negative cannot exist as a piece.
			Add(ET::Effect, TEXT("BreakupSize"), {.HardMin = 0.001f});
			// Shader: lerp(1.0, max(stretch, 1.0), rt) with per-piece reciprocal flips -- the
			// value is a magnitude with randomized orientation, so below 1 is folded away.
			Add(ET::Effect, TEXT("BreakupStretch"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("BreakupAngularity"), Saturated());
			Add(ET::Effect, TEXT("BreakupDistortionFrequency"), {.HardMin = 1.0f, .HardMax = 16.0f});
			// Integer-period sinusoid frequency; 0 has no period.
			Add(ET::Effect, TEXT("BreakupSmoothness"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("BreakupGapWidth"), {.HardMin = 0.0f});
			// Widths reach divisors in the falloff math.
			Add(ET::Effect, TEXT("BreakupFoldWidth"), {.HardMin = DivisorFloor});
			Add(ET::Effect, TEXT("BreakupCreaseWidth"), {.HardMin = DivisorFloor});
			Add(ET::Effect, TEXT("BreakupPushWidth"), {.HardMin = DivisorFloor});
			Add(ET::Effect, TEXT("BreakupPushRelief"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("BreakupVariation"), Saturated());
			Add(ET::Effect, TEXT("BreakupAmount"), Saturated());
			// The RAM channel saturates after the signed offset: any magnitude is legal.
			Add(ET::Effect, TEXT("BreakupRoughnessAmount"), Saturated());
			Add(ET::Effect, TEXT("BreakupNormalStrength"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("BreakupNormalSharpness"), Saturated());
			Add(ET::Effect, TEXT("BreakupAOAmount"), Saturated());
			// AO radius divides in the occlusion falloff.
			Add(ET::Effect, TEXT("BreakupAORadius"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("BreakupMaskTiling"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("BreakupSeed"), {.HardMin = 0.0f});

			// ---- Erosion. The shader keeps its own epsilon guards; only true floors here.
			Add(ET::Effect, TEXT("ErosionRadius"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("ErosionIterations"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("ErosionGravityForce"), Saturated());
			// Negative exponents on negative slopes would NaN the pow.
			Add(ET::Effect, TEXT("ErosionSlopePower"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("ErosionSeed"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("ErosionMaskTiling"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("ErosionRoughnessAmount"), Saturated());

			// ---- Grade.
			// Applied as pow(c, 1/Gamma): gamma 0 has no reciprocal.
			Add(ET::Effect, TEXT("GradeGamma"), {.HardMin = 0.05f});
			Add(ET::Effect, TEXT("GradeTonemapStrength"), Saturated());

			// ---- Layer Blur. Kernel radius is a real tap budget: 32 is the pass allocation.
			Add(ET::Effect, TEXT("LayerBlurRadiusX"), {.HardMin = 0.0f, .HardMax = 32.0f});
			Add(ET::Effect, TEXT("LayerBlurRadiusY"), {.HardMin = 0.0f, .HardMax = 32.0f});

			// ---- Flow Warp.
			Add(ET::Effect, TEXT("FlowWarpScale"), {.HardMin = 1.0f, .HardMax = 128.0f});
			Add(ET::Effect, TEXT("FlowWarpSeed"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("FlowWarpMaskSlopeInfluence"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("FlowWarpHeightSlopeInfluence"), {.HardMin = 0.0f});
			// Derivative radii are kernel taps: 1..64 is the pass's sampling budget.
			Add(ET::Effect, TEXT("FlowWarpDerivativeKernelX"), {.HardMin = 1.0f, .HardMax = 64.0f});
			Add(ET::Effect, TEXT("FlowWarpDerivativeKernelY"), {.HardMin = 1.0f, .HardMax = 64.0f});

			// ---- Runoff.
			// Texel reach at 1K; the 8..512 span also picks the stratum-count thresholds.
			Add(ET::Effect, TEXT("RunoffStreakRadius"), {.HardMin = 8.0f, .HardMax = 512.0f});
			Add(ET::Effect, TEXT("RunoffStreakSoftness"), {.HardMin = 0.05f, .HardMax = 1.0f});
			Add(ET::Effect, TEXT("RunoffSurfaceInfluence"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Effect, TEXT("RunoffStrataAmount"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Effect, TEXT("RunoffWarpScale"), {.HardMin = 1.0f, .HardMax = 64.0f});
			Add(ET::Effect, TEXT("RunoffWarpAmount"), {.HardMin = 0.0f, .HardMax = 2.0f});
			Add(ET::Effect, TEXT("RunoffLipStrength"), {.HardMin = 0.0f, .HardMax = 1.0f});
			// Weight of the resolved runoff in the layer's mask chain.
			Add(ET::Effect, TEXT("RunoffStrength"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Effect, TEXT("RunoffSeed"), {.HardMin = 0.0f, .HardMax = 9999.0f});

			// ---- Worn Edges.
			Add(ET::Effect, TEXT("EdgeWearRadius"), {.HardMin = 1.0f, .HardMax = 64.0f});
			Add(ET::Effect, TEXT("EdgeWearSlope"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearStrength"), Saturated());
			Add(ET::Effect, TEXT("EdgeWearFeather"), {.HardMin = 0.0f});
			// Ray count drives an unrolled loop: 8..32 is the dispatch budget.
			Add(ET::Effect, TEXT("EdgeWearDirections"), {.HardMin = 8.0f, .HardMax = 32.0f});
			Add(ET::Effect, TEXT("EdgeWearGravity"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearSeed"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearMacroScale"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearMacroAmount"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearCellScale"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearCellAmount"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearRidgeScale"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearRidgeAmount"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearMicroScale"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearMicroAmount"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearWarpScale"), {.HardMin = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearWarpAmount"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearIdVariation"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearIdRadius"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearIdSlope"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearIdStrength"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearIdNoise"), {.HardMin = 0.0f});
			Add(ET::Effect, TEXT("EdgeWearRoughnessWeight"), {.HardMin = 0.0f, .HardMax = 1.0f});
			Add(ET::Effect, TEXT("EdgeWearRoughnessOffset"), {.HardMin = -1.0f, .HardMax = 1.0f});

			return Built;
		}();

		return Table;
	}

	// The reflected default of one parameter: read from the CDO instance of the owner struct.
	// Cached per key -- the CDO never changes after load.
	struct FReflectedDefault
	{
		float Value = 0.0f;
		bool bFound = false;
	};

	FReflectedDefault GetReflectedDefault(
		EMixtormatParameterOwnerType Owner, FName Parameter)
	{
		static TMap<FMixtormatParameterDefinitionKey, FReflectedDefault> Cache;

		FMixtormatParameterDefinitionKey Key;
		Key.Owner = Owner;
		Key.Parameter = Parameter;
		if (const FReflectedDefault* Cached = Cache.Find(Key))
		{
			return *Cached;
		}

		FReflectedDefault Result;
		if (Owner == EMixtormatParameterOwnerType::Effect)
		{
			static const FMixtormatLayerEffect CDODefaults;
			if (const FProperty* Property =
				FMixtormatLayerEffect::StaticStruct()->FindPropertyByName(Parameter))
			{
				if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					Result.Value = *Float->ContainerPtrToValuePtr<float>(&CDODefaults);
					Result.bFound = true;
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					Result.Value = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(&CDODefaults));
					Result.bFound = true;
				}
			}
		}
		else if (Owner == EMixtormatParameterOwnerType::Layer)
		{
			static const FMixtormatLayer CDODefaults;
			if (const FProperty* Property =
				FMixtormatLayer::StaticStruct()->FindPropertyByName(Parameter))
			{
				if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					Result.Value = *Float->ContainerPtrToValuePtr<float>(&CDODefaults);
					Result.bFound = true;
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					Result.Value = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(&CDODefaults));
					Result.bFound = true;
				}
			}
		}

		if (!Result.bFound)
		{
			ensureMsgf(false,
				TEXT("Mixtormat: no reflected default for parameter '%s' -- check the name "
					"against the owner struct."), *Parameter.ToString());
		}
		Cache.Add(Key, Result);
		return Result;
	}
}

namespace MixtormatParameterContracts
{
	const FMixtormatParameterContract* TryGet(
		const EMixtormatParameterOwnerType Owner, const FName Parameter)
	{
		FMixtormatParameterContractKey Key;
		Key.Owner = Owner;
		Key.Parameter = Parameter;
		return Contracts().Find(Key);
	}

	const TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterContract>& GetAll()
	{
		return Contracts();
	}

	float SanitizeFloat(
		const EMixtormatParameterOwnerType Owner, const FName Parameter, const float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return GetReflectedDefault(Owner, Parameter).Value;
		}
		if (const FMixtormatParameterContract* Contract = TryGet(Owner, Parameter))
		{
			float Sanitized = Value;
			if (Contract->HardMin.IsSet())
			{
				Sanitized = FMath::Max(Sanitized, Contract->HardMin.GetValue());
			}
			if (Contract->HardMax.IsSet())
			{
				Sanitized = FMath::Min(Sanitized, Contract->HardMax.GetValue());
			}
			return Sanitized;
		}
		// No contract: no opinion. The value passes through.
		return Value;
	}

	int32 SanitizeInt32(
		const EMixtormatParameterOwnerType Owner, const FName Parameter, const int32 Value)
	{
		return FMath::RoundToInt(SanitizeFloat(Owner, Parameter, static_cast<float>(Value)));
	}

	float DefaultFloat(
		const EMixtormatParameterOwnerType Owner, const FName Parameter, const float Fallback)
	{
		const FReflectedDefault Default = GetReflectedDefault(Owner, Parameter);
		return Default.bFound ? Default.Value : Fallback;
	}
}
