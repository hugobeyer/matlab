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
			Add(ET::Layer, TEXT("HeightBoost"), {});
			Add(ET::Layer, TEXT("NormalIntensity"), {});
			Add(ET::Layer, TEXT("FuzzInfluence"), {});
			Add(ET::Layer, TEXT("F0Influence"), {});
			Add(ET::Layer, TEXT("NormalInfluence"), {});
			Add(ET::Layer, TEXT("HeightInfluence"), {});
			Add(ET::Layer, TEXT("IOR"), {});

			// ---- Breakup.
			// Scale's upper bound is the derived cell-count budget, not taste.
			Add(ET::Effect, TEXT("BreakupScale"), {});
			Add(ET::Effect, TEXT("BreakupDensity"), Saturated());
			// Size feeds the shape metric's radius; zero or negative cannot exist as a piece.
			Add(ET::Effect, TEXT("BreakupSize"), {});
			// Shader: lerp(1.0, max(stretch, 1.0), rt) with per-piece reciprocal flips -- the
			// value is a magnitude with randomized orientation, so below 1 is folded away.
			Add(ET::Effect, TEXT("BreakupStretch"), {});
			Add(ET::Effect, TEXT("BreakupAngularity"), Saturated());
			Add(ET::Effect, TEXT("BreakupDistortionFrequency"), {});
			// Integer-period sinusoid frequency; 0 has no period.
			Add(ET::Effect, TEXT("BreakupSmoothness"), {});
			Add(ET::Effect, TEXT("BreakupGapWidth"), {});
			// Widths reach divisors in the falloff math.
			Add(ET::Effect, TEXT("BreakupFoldWidth"), {});
			Add(ET::Effect, TEXT("BreakupCreaseWidth"), {});
			Add(ET::Effect, TEXT("BreakupPushWidth"), {});
			Add(ET::Effect, TEXT("BreakupPushRelief"), {});
			Add(ET::Effect, TEXT("BreakupVariation"), {});
			Add(ET::Effect, TEXT("BreakupAmount"), Saturated());
			// The RAM channel saturates after the signed offset: any magnitude is legal.
			Add(ET::Effect, TEXT("BreakupRoughnessAmount"), Saturated());
			Add(ET::Effect, TEXT("BreakupNormalStrength"), {});
			Add(ET::Effect, TEXT("BreakupNormalSharpness"), {});
			// AO radius divides in the occlusion falloff.
			Add(ET::Effect, TEXT("BreakupMaskTiling"), {});
			Add(ET::Effect, TEXT("BreakupSeed"), {});

			// ---- Erosion. The shader keeps its own epsilon guards; only true floors here.
			Add(ET::Effect, TEXT("ErosionRadius"), {});
			Add(ET::Effect, TEXT("ErosionIterations"), {});
			Add(ET::Effect, TEXT("ErosionGravityForce"), {});
			// Negative exponents on negative slopes would NaN the pow.
			Add(ET::Effect, TEXT("ErosionSlopePower"), {});
			Add(ET::Effect, TEXT("ErosionSeed"), {});
			Add(ET::Effect, TEXT("ErosionMaskTiling"), {});
			Add(ET::Effect, TEXT("ErosionRoughnessAmount"), Saturated());

			// ---- Grade.
			// Applied as pow(c, 1/Gamma): gamma 0 has no reciprocal.
			Add(ET::Effect, TEXT("GradeGamma"), {});
			Add(ET::Effect, TEXT("GradeTonemapStrength"), {});

			// ---- Layer Blur. Kernel radius is a real tap budget: 32 is the pass allocation.
			Add(ET::Effect, TEXT("LayerBlurRadiusX"), {});
			Add(ET::Effect, TEXT("LayerBlurRadiusY"), {});

			// ---- Flow Warp.
			Add(ET::Effect, TEXT("FlowWarpScale"), {});
			Add(ET::Effect, TEXT("FlowWarpSeed"), {});
			Add(ET::Effect, TEXT("FlowWarpMaskSlopeInfluence"), {});
			Add(ET::Effect, TEXT("FlowWarpHeightSlopeInfluence"), {});
			// Derivative radii are kernel taps: 1..64 is the pass's sampling budget.
			Add(ET::Effect, TEXT("FlowWarpDerivativeKernelX"), {});
			Add(ET::Effect, TEXT("FlowWarpDerivativeKernelY"), {});

			// ---- Runoff.
			// Texel reach at 1K; the 8..512 span also picks the stratum-count thresholds.
			Add(ET::Effect, TEXT("RunoffStreakRadius"), {});
			Add(ET::Effect, TEXT("RunoffStreakSoftness"), {});
			Add(ET::Effect, TEXT("RunoffSurfaceInfluence"), {});
			Add(ET::Effect, TEXT("RunoffStrataAmount"), {});
			Add(ET::Effect, TEXT("RunoffWarpScale"), {});
			Add(ET::Effect, TEXT("RunoffWarpAmount"), {});
			Add(ET::Effect, TEXT("RunoffLipStrength"), {});
			// Weight of the resolved runoff in the layer's mask chain.
			Add(ET::Effect, TEXT("RunoffStrength"), {});
			Add(ET::Effect, TEXT("RunoffSeed"), {});

			// ---- Worn Edges.
			Add(ET::Effect, TEXT("EdgeWearRadius"), {});
			Add(ET::Effect, TEXT("EdgeWearSlope"), {});
			Add(ET::Effect, TEXT("EdgeWearStrength"), {});
			Add(ET::Effect, TEXT("EdgeWearFeather"), {});
			// Ray count drives an unrolled loop: 8..32 is the dispatch budget.
			Add(ET::Effect, TEXT("EdgeWearDirections"), {});
			Add(ET::Effect, TEXT("EdgeWearGravity"), {});
			Add(ET::Effect, TEXT("EdgeWearSeed"), {});
			Add(ET::Effect, TEXT("EdgeWearMacroScale"), {});
			Add(ET::Effect, TEXT("EdgeWearMacroAmount"), {});
			Add(ET::Effect, TEXT("EdgeWearCellScale"), {});
			Add(ET::Effect, TEXT("EdgeWearCellAmount"), {});
			Add(ET::Effect, TEXT("EdgeWearRidgeScale"), {});
			Add(ET::Effect, TEXT("EdgeWearRidgeAmount"), {});
			Add(ET::Effect, TEXT("EdgeWearMicroScale"), {});
			Add(ET::Effect, TEXT("EdgeWearMicroAmount"), {});
			Add(ET::Effect, TEXT("EdgeWearWarpScale"), {});
			Add(ET::Effect, TEXT("EdgeWearWarpAmount"), {});
			Add(ET::Effect, TEXT("EdgeWearIdVariation"), {});
			Add(ET::Effect, TEXT("EdgeWearIdRadius"), {});
			Add(ET::Effect, TEXT("EdgeWearIdSlope"), {});
			Add(ET::Effect, TEXT("EdgeWearIdStrength"), {});
			Add(ET::Effect, TEXT("EdgeWearIdNoise"), {});
			Add(ET::Effect, TEXT("EdgeWearRoughnessWeight"), {});
			Add(ET::Effect, TEXT("EdgeWearRoughnessOffset"), {});

			// ---- Procedural peeling.
			Add(ET::Effect, TEXT("PeelRandomSeed"), {});
			Add(ET::Effect, TEXT("PeelMaskTiling"), {});
			Add(ET::Effect, TEXT("PeelSeedThreshold"), {});
			Add(ET::Effect, TEXT("PeelSeedCurvatureBias"), Saturated());
			Add(ET::Effect, TEXT("PeelGrowthStrength"), {});
			Add(ET::Effect, TEXT("PeelMacroPeriod"), {});
			Add(ET::Effect, TEXT("PeelMicroPeriod"), {});
			Add(ET::Effect, TEXT("PeelSeedNoiseWeight"), {});
			Add(ET::Effect, TEXT("PeelSizeVariation"), {});
			Add(ET::Effect, TEXT("PeelClusterPeriod"), {});
			Add(ET::Effect, TEXT("PeelClusterAmount"), Saturated());
			Add(ET::Effect, TEXT("PeelWarpPeriod"), {});
			Add(ET::Effect, TEXT("PeelWarpSource"), {});
			Add(ET::Effect, TEXT("PeelCurvatureRadius"), {});
			Add(ET::Effect, TEXT("Width"), {});
			Add(ET::Effect, TEXT("MicroMorph"), Saturated());
			Add(ET::Effect, TEXT("Thickness"), {});
			Add(ET::Effect, TEXT("Lift"), {});
			Add(ET::Effect, TEXT("DetailStrength"), {});
			Add(ET::Effect, TEXT("PeelLiftVariation"), {});
			Add(ET::Effect, TEXT("PeelCornerLift"), Saturated());
			Add(ET::Effect, TEXT("PeelCornerRadius"), {});
			Add(ET::Effect, TEXT("PeelIDInfluence"), {});

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
		else if (Owner == EMixtormatParameterOwnerType::IdGroup)
		{
			static const FMixtormatIdGroup Defaults;
			if (const FProperty* Property = FMixtormatIdGroup::StaticStruct()->FindPropertyByName(Parameter))
			{
				if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					Result.Value = *Float->ContainerPtrToValuePtr<float>(&Defaults);
					Result.bFound = true;
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					Result.Value = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(&Defaults));
					Result.bFound = true;
				}
			}
		}
		else if (Owner == EMixtormatParameterOwnerType::BoundaryId)
		{
			static const FMixtormatBoundaryIdFilter Defaults;
			if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(
				FMixtormatBoundaryIdFilter::StaticStruct(), Parameter))
			{
				Result.Value = *Property->ContainerPtrToValuePtr<float>(&Defaults);
				Result.bFound = true;
			}
		}
		else if (Owner == EMixtormatParameterOwnerType::Generator)
		{
			// Generator parameter names are unique across payloads. The owner address remains the
			// stable category while reflection still supplies the payload's compiled default.
			static const FMixtormatStrataCarver StrataDefaults;
			static const FMixtormatCracks CrackDefaults;
			static const FMixtormatRockFormation RockDefaults;
			static const FMixtormatPebbles PebbleDefaults;
			const void* Defaults = &StrataDefaults;
			const FProperty* Property = FMixtormatStrataCarver::StaticStruct()->FindPropertyByName(Parameter);
			if (!Property)
			{
				Property = FMixtormatCracks::StaticStruct()->FindPropertyByName(Parameter);
				Defaults = &CrackDefaults;
			}
			if (!Property)
			{
				Property = FMixtormatRockFormation::StaticStruct()->FindPropertyByName(Parameter);
				Defaults = &RockDefaults;
			}
			if (!Property)
			{
				Property = FMixtormatPebbles::StaticStruct()->FindPropertyByName(Parameter);
				Defaults = &PebbleDefaults;
			}
			if (Property)
			{
				if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					Result.Value = *Float->ContainerPtrToValuePtr<float>(Defaults);
					Result.bFound = true;
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					Result.Value = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(Defaults));
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
