// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuNoisePasses.h"

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatGeneratorTypes.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

// The Noise module's GPU pass.
//
// A Noise module is the cheapest kind of generator: one dispatch, no solve, no temporaries, no
// node cache. Everything it publishes -- the raw value, the signed module height, the gradient
// and the Worley cell IDs -- comes out of that single pass, and the shared signed normalization
// in AddGeneratorLayerPasses runs on the height afterwards, exactly as it does for every other
// module.
//
// The settings ride FMixtormatNoiseRenderStore from the gather; see that header for why.

namespace MixtormatGpuCompositor
{
namespace
{
	// One Noise dispatch. All outputs, no stages: the field has no iterative or neighbourhood
	// structure to resolve, so there is nothing to split.
	class FMixtormatNoiseCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatNoiseCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatNoiseCS, FGlobalShader);

		class FValueOnlyDim : SHADER_PERMUTATION_BOOL("MIXTORMAT_NOISE_VALUE_ONLY");
		using FPermutationDomain = TShaderPermutationDomain<FValueOnlyDim>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			// The Generator layer's UV placement, the same six globals every module binds.
			SHADER_PARAMETER(uint32, GeneratorLayer)
			SHADER_PARAMETER(FVector2f, GeneratorUVScale)
			SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
			SHADER_PARAMETER(int32, GeneratorUVRotation)
			SHADER_PARAMETER(uint32, GeneratorUVFlipU)
			SHADER_PARAMETER(uint32, GeneratorUVFlipV)
			SHADER_PARAMETER(uint32, UsePreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
			SHADER_PARAMETER(int32, NoiseType)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(float, Scale)
			SHADER_PARAMETER(int32, Detail)
			SHADER_PARAMETER(float, Roughness)
			SHADER_PARAMETER(float, Lacunarity)
			SHADER_PARAMETER(float, LayerMix)
			SHADER_PARAMETER(FVector2f, OffsetUV)
			SHADER_PARAMETER(FVector2f, Wave)
			SHADER_PARAMETER(float, PhaseOffset)
			SHADER_PARAMETER(float, PhasorFrequency)
			SHADER_PARAMETER(float, PhasorAnisotropy)
			SHADER_PARAMETER(float, PhasorPhaseVariation)
			SHADER_PARAMETER(float, PhasorOrientationVariation)
			SHADER_PARAMETER(int32, PhasorComponents)
			SHADER_PARAMETER(float, PhasorScale)
			SHADER_PARAMETER(float, PhasorBias)
			SHADER_PARAMETER(int32, WorleyMetric)
			SHADER_PARAMETER(float, WorleyJitter)
			SHADER_PARAMETER(float, WorleyCellDepth)
			SHADER_PARAMETER(float, DistortionStrength)
			SHADER_PARAMETER(float, DistortionJaggedness)
			SHADER_PARAMETER(float, JaggedSharpness)
			SHADER_PARAMETER(float, JaggedDetail)
			SHADER_PARAMETER(int32, DistortionPeriod)
			SHADER_PARAMETER(int32, DistortionOctaves)
			SHADER_PARAMETER(float, DistortionRoughness)
			SHADER_PARAMETER(float, DistortionLacunarity)
			SHADER_PARAMETER(float, DistortionCurlMix)
			SHADER_PARAMETER(FVector2f, DistortionDirectionUV)
			SHADER_PARAMETER(uint32, ProducesIds)
			// Scoped mask, mirroring Rock/Cracks/Pebbles/Cliff. Noise used to bind neither flag nor
			// texture and added its height ungated, so any mask scoped beneath a Noise generator --
			// Noise Gate included -- was gathered correctly and then silently discarded.
			SHADER_PARAMETER(uint32, HasMask)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutValue)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutGradient)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutIds)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FMixtormatNoiseCS,
		"/Plugin/Mixtormat/Private/MixtormatNoise.usf", "MainCS", SF_Compute);

	class FMixtormatNoiseCoverageCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatNoiseCoverageCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatNoiseCoverageCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, CoverageSigned)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CoverageValue)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCoverage)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatNoiseCoverageCS,
		"/Plugin/Mixtormat/Private/MixtormatNoise.usf", "CoverageCS", SF_Compute);

	// Bars / Phasor: the authored direction snapped to a tileable integer wave vector.
	//
	// A global stripe at an arbitrary angle does not close on the tile, so the phase direction snaps to
	// the nearest small coprime integer vector and is scaled so a whole number of cycles crosses
	// the tile -- the same lattice snap Strata Carver's bedding uses, kept local here because the
	// original is file-private to the generator-pass file. Scale is cycles across the tile; the
	// snap changes the effective angle, never the count.
	FVector2f ResolveNoiseWave(const float Scale, const float DirectionDegrees)
	{
		const float Radians = FMath::DegreesToRadians(DirectionDegrees);
		// The phase advances across the stripes: 0 degrees is horizontal stripes, so the phase
		// direction is (sin, cos) -- matching Strata Carver's bedding convention.
		const FVector2f Want(FMath::Sin(Radians), FMath::Cos(Radians));

		FIntPoint Best(0, 1);
		float BestAlignment = -2.0f;
		constexpr int32 MaxComponent = 3;
		for (int32 P = -MaxComponent; P <= MaxComponent; ++P)
		{
			for (int32 Q = -MaxComponent; Q <= MaxComponent; ++Q)
			{
				if ((P == 0 && Q == 0)
					|| FMath::GreatestCommonDivisor(FMath::Abs(P), FMath::Abs(Q)) != 1)
				{
					continue;
				}
				const float Alignment =
					(Want.X * P + Want.Y * Q) / FMath::Sqrt(static_cast<float>(P * P + Q * Q));
				if (Alignment > BestAlignment)
				{
					BestAlignment = Alignment;
					Best = FIntPoint(P, Q);
				}
			}
		}

		const float Length = FMath::Sqrt(static_cast<float>(Best.X * Best.X + Best.Y * Best.Y));
		const int32 Cycles = FMath::Max(FMath::RoundToInt(FMath::Max(Scale, 1.0f) / Length), 1);
		return FVector2f(Best.X * Cycles, Best.Y * Cycles);
	}

	class FMixtormatNoiseFlowCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatNoiseFlowCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatNoiseFlowCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CompletedHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlow)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFlowValidity)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatNoiseFlowCS,
		"/Plugin/Mixtormat/Private/MixtormatNoise.usf", "FlowCS", SF_Compute);

	// The family-to-kind contract now lives in Runtime (MixtormatOutputReferences::NoiseValueKind),
	// so the producer and the reference validators read one definition. See the USF header.

	// Only the Worley family genuinely produces cells; the others have no stable identity to
	// publish, and faking one to make the output lists match is exactly what not to do.
	bool NoiseProducesIds(const EMixtormatNoiseType Type)
	{
		return Type == EMixtormatNoiseType::WorleyF1
			|| Type == EMixtormatNoiseType::WorleyF2
			|| Type == EMixtormatNoiseType::WorleyF1MinusF2;
	}

	bool NoiseUsesWave(const EMixtormatNoiseType Type)
	{
		return Type == EMixtormatNoiseType::Bars || Type == EMixtormatNoiseType::Phasor;
	}
}

FMixtormatNoiseRenderData ResolveNoiseRenderData(const FMixtormatNoise& Noise)
{
	const FMixtormatNoise Defaults;
	const auto Finite = [](const float Value, const float Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	};
	FMixtormatNoiseRenderData Out;
	Out.Type = static_cast<int32>(Noise.NoiseType);
	Out.Seed = Noise.NoiseSeed;
	Out.Scale = FMath::Max(Finite(Noise.NoiseScale, Defaults.NoiseScale), 1.0f);
	Out.Detail = FMath::Clamp(Noise.NoiseDetail, 1, 8);
	Out.LayerMix = FMath::Clamp(Finite(Noise.NoiseLayerMix, Defaults.NoiseLayerMix), 0.0f, 1.0f);
	Out.Roughness = Finite(Noise.NoiseRoughness, Defaults.NoiseRoughness);
	Out.Lacunarity = FMath::Max(Finite(Noise.NoiseLacunarity, Defaults.NoiseLacunarity), 1.0f);
	Out.OffsetX = Finite(Noise.NoiseOffsetX, Defaults.NoiseOffsetX);
	Out.OffsetY = Finite(Noise.NoiseOffsetY, Defaults.NoiseOffsetY);
	Out.Direction = Finite(Noise.NoiseDirection, Defaults.NoiseDirection);
	Out.PhasorFrequency = FMath::Clamp(Finite(Noise.NoisePhasorFrequency, Defaults.NoisePhasorFrequency), 0.0f, 12.0f);
	Out.PhasorAnisotropy = FMath::Clamp(Finite(Noise.NoisePhasorAnisotropy, Defaults.NoisePhasorAnisotropy), 0.0f, 8.0f);
	Out.PhasorPhaseVariation = FMath::Clamp(Finite(Noise.NoisePhasorPhaseVariation, Defaults.NoisePhasorPhaseVariation), 0.0f, 1.0f);
	Out.PhasorOrientationVariation = FMath::Clamp(Finite(Noise.NoisePhasorOrientationVariation, Defaults.NoisePhasorOrientationVariation), 0.0f, 3.14159265f);
	Out.PhasorComponents = FMath::Clamp(Noise.NoisePhasorComponents, 1, 4);
	Out.PhasorScale = FMath::Clamp(Finite(Noise.NoisePhasorScale, Defaults.NoisePhasorScale), 0.0f, 4.0f);
	Out.PhasorBias = FMath::Clamp(Finite(Noise.NoisePhasorBias, Defaults.NoisePhasorBias), -1.0f, 1.0f);
	Out.WorleyMetric = FMath::Clamp(static_cast<int32>(Noise.NoiseWorleyMetric), 0, 2);
	Out.WorleyJitter = FMath::Clamp(Finite(Noise.NoiseWorleyJitter, Defaults.NoiseWorleyJitter), 0.0f, 1.0f);
	Out.WorleyCellDepth = FMath::Clamp(Finite(Noise.NoiseWorleyCellDepth, Defaults.NoiseWorleyCellDepth), 0.0f, 1.0f);
	Out.DistortionStrength = FMath::Clamp(Finite(Noise.NoiseDistortionStrength, Defaults.NoiseDistortionStrength), 0.0f, 2.0f);
	Out.DistortionJaggedness = FMath::Clamp(Finite(Noise.NoiseDistortionJaggedness, Defaults.NoiseDistortionJaggedness), 0.0f, 2.0f);
	Out.JaggedSharpness = FMath::Clamp(Finite(Noise.NoiseJaggedSharpness, Defaults.NoiseJaggedSharpness), 0.0f, 1.0f);
	Out.JaggedDetail = FMath::Clamp(Finite(Noise.NoiseJaggedDetail, Defaults.NoiseJaggedDetail), 0.0f, 1.0f);
	Out.DistortionFrequency = FMath::Clamp(Finite(Noise.NoiseDistortionFrequency, Defaults.NoiseDistortionFrequency), 1.0f, 64.0f);
	Out.DistortionOctaves = FMath::Clamp(Noise.NoiseDistortionOctaves, 1, 8);
	Out.DistortionRoughness = FMath::Clamp(Finite(Noise.NoiseDistortionRoughness, Defaults.NoiseDistortionRoughness), 0.0f, 1.0f);
	Out.DistortionLacunarity = FMath::Clamp(Finite(Noise.NoiseDistortionLacunarity, Defaults.NoiseDistortionLacunarity), 1.0f, 4.0f);
	Out.DistortionCurlMix = FMath::Clamp(Finite(Noise.NoiseDistortionCurlMix, Defaults.NoiseDistortionCurlMix), 0.0f, 1.0f);
	Out.DistortionDirection = Finite(Noise.NoiseDistortionDirection, Defaults.NoiseDistortionDirection);
	return Out;
}

void FMixtormatNoiseRenderStore::Set(const FMixtormatNoiseRenderKey& Key, const FMixtormatNoiseRenderData& Data)
{
	FScopeLock Lock(&Guard);
	if (Entries.Num() >= MaxEntries && !Entries.Contains(Key))
	{
		Entries.Empty(MaxEntries);
	}
	Entries.Add(Key, Data);
}

bool FMixtormatNoiseRenderStore::Find(const FMixtormatNoiseRenderKey& Key, FMixtormatNoiseRenderData& OutData) const
{
	FScopeLock Lock(&Guard);
	if (const FMixtormatNoiseRenderData* Found = Entries.Find(Key))
	{
		OutData = *Found;
		return true;
	}
	return false;
}

FMixtormatNoiseRenderStore& MixtormatNoiseRenderStore()
{
	static FMixtormatNoiseRenderStore Store;
	return Store;
}

void AddNoiseFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	const int32 SourceChildIndex, FRDGTextureRef Height)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Flow = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Noise.Flow"));
	FRDGTextureRef Validity = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Noise.FlowValidity"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatNoiseFlowCS::FParameters>();
	P->OutputSize = Size;
	P->CompletedHeight = Height;
	P->OutFlow = Ctx.GraphBuilder.CreateUAV(Flow);
	P->OutFlowValidity = Ctx.GraphBuilder.CreateUAV(Validity);
	TShaderMapRef<FMixtormatNoiseFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Noise.Flow.C%d", SourceChildIndex),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	// No extra smoothing is authored on this output; the resolved field fills both Flow slots.
	Ctx.PublishedFieldOutputs.Add(
		PublishedKey(Layer, SourceChildIndex, FName(TEXT("FlowDirection"))),
		FPublishedField{EMixtormatPublishedFieldKind::Flow, Flow, Flow, Validity, false});
}

namespace
{
struct FNoiseFields
{
	FRDGTextureRef Value = nullptr;
	FRDGTextureRef Height = nullptr;
	FRDGTextureRef Gradient = nullptr;
	FRDGTextureRef Ids = nullptr;
};

// Null Layer selects source-local, value-only dispatch, with no generator companion allocations.
// No scoped generator mask is resolved for inline Noise mask sources.
FNoiseFields AddNoiseFieldPass(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext* LayerCtx,
	const FMixtormatNoiseRenderData& Noise, const FLayerRenderData* Layer,
	const int32 LayerIndex, const int32 SourceChildIndex,
	FRDGTextureRef PreUV = nullptr)
{
	const bool bValueOnly = Layer == nullptr;
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

	const EMixtormatNoiseType NoiseType = static_cast<EMixtormatNoiseType>(Noise.Type);
	const bool bProducesIds = !bValueOnly && NoiseProducesIds(NoiseType);

	// Bars and Phasor share the snapped tileable orientation. Existing Bars phase is unchanged.
	FVector2f Wave(0.0f, 1.0f);
	float PhaseOffset = 0.0f;
	if (NoiseUsesWave(NoiseType))
	{
		Wave = ResolveNoiseWave(Noise.Scale, Noise.Direction);
		// A constant phase slide: any constant leaves the field periodic, and the golden-ratio
		// fractional parts spread successive seeds across the stripe positions.
		PhaseOffset = FMath::Frac(FMath::Abs(static_cast<float>(Noise.Seed)) * 0.61803398875f);
	}

	const auto MakeTexture = [&GraphBuilder, Size](const EPixelFormat Format, const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	FRDGTextureRef Value = MakeTexture(PF_R32_FLOAT, TEXT("Mixtormat.Noise.Value"));
	FRDGTextureRef Height = bValueOnly ? nullptr : MakeTexture(PF_R32_FLOAT, TEXT("Mixtormat.Noise.Height"));
	FRDGTextureRef Gradient = bValueOnly ? nullptr : MakeTexture(PF_G32R32F, TEXT("Mixtormat.Noise.Gradient"));
	// The ID map exists only where cells exist. The other families bind the context's empty ID
	// texture instead of leaving the slot null -- the binding has to be satisfied, and the
	// ProducesIds uniform keeps the write off it.
	FRDGTextureRef Ids = bProducesIds
		? MakeTexture(PF_R32_UINT, TEXT("Mixtormat.Noise.Ids"))
		: (bValueOnly ? nullptr : Ctx.EmptyRegionIds);

	{
		FMixtormatNoiseCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatNoiseCS::FValueOnlyDim>(bValueOnly);
		TShaderMapRef<FMixtormatNoiseCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatNoiseCS::FParameters>();
		P->OutputSize = Size;
		// The Generator layer's UV placement, the same six values FillGeneratorPlacement writes
		// for every other module. Kept local: that helper is file-private to the generator-pass
		// file, and the six lines are the whole of it.
		P->GeneratorLayer = bValueOnly ? 0u : 1u;
		P->GeneratorUVScale = Layer
			? FVector2f(Layer->Tiling * Layer->UVScaleX, Layer->Tiling * Layer->UVScaleY) : FVector2f(1.0f, 1.0f);
		P->GeneratorUVOffset = Layer ? Layer->UVOffset : FVector2f(0.0f, 0.0f);
		P->GeneratorUVRotation = Layer ? Layer->Rotation : 0;
		P->GeneratorUVFlipU = Layer && Layer->bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = Layer && Layer->bFlipV ? 1u : 0u;
		P->UsePreGenerationUV = !bValueOnly && PreUV ? 1u : 0u;
		P->PreGenerationUV = PreUV ? PreUV : Ctx.EmptyPatternUV;
		P->NoiseType = Noise.Type;
		P->Seed = static_cast<uint32>(Noise.Seed);
		P->Scale = Noise.Scale;
		P->Detail = Noise.Detail;
		P->Roughness = Noise.Roughness;
		P->Lacunarity = Noise.Lacunarity;
		P->LayerMix = Noise.LayerMix;
		P->OffsetUV = FVector2f(Noise.OffsetX, Noise.OffsetY);
		P->Wave = Wave;
		P->PhaseOffset = PhaseOffset;
		P->PhasorFrequency = Noise.PhasorFrequency;
		P->PhasorAnisotropy = Noise.PhasorAnisotropy;
		P->PhasorPhaseVariation = Noise.PhasorPhaseVariation;
		P->PhasorOrientationVariation = Noise.PhasorOrientationVariation;
		P->PhasorComponents = Noise.PhasorComponents;
		P->PhasorScale = Noise.PhasorScale;
		P->PhasorBias = Noise.PhasorBias;
		P->WorleyMetric = Noise.WorleyMetric;
		P->WorleyJitter = Noise.WorleyJitter;
		P->WorleyCellDepth = Noise.WorleyCellDepth;
		P->DistortionStrength = Noise.DistortionStrength;
		P->DistortionJaggedness = Noise.DistortionJaggedness;
		P->JaggedSharpness = Noise.JaggedSharpness;
		P->JaggedDetail = Noise.JaggedDetail;
		P->DistortionPeriod = FMath::RoundToInt(Noise.DistortionFrequency);
		P->DistortionOctaves = Noise.DistortionOctaves;
		P->DistortionRoughness = Noise.DistortionRoughness;
		P->DistortionLacunarity = Noise.DistortionLacunarity;
		P->DistortionCurlMix = Noise.DistortionCurlMix;
		const float WarpAngle = FMath::DegreesToRadians(Noise.DistortionDirection);
		P->DistortionDirectionUV = FVector2f(FMath::Sin(WarpAngle), FMath::Cos(WarpAngle));
		P->ProducesIds = bProducesIds ? 1u : 0u;
		// Scoped mask. Height-only by design: Value and Gradient stay ungated so Noise keeps
		// working as a mask source and Height Push/Warp consumers are unaffected. No mask means
		// HasMask 0 and the shader substitutes 1 -- "no mask" and "a white mask" must not collapse
		// to the same picture.
		const bool bHasMask = LayerCtx && Layer && !bValueOnly
			&& HasScopedGeneratorMasks(*Layer, SourceChildIndex);
		P->HasMask = bHasMask ? 1u : 0u;
		P->ScopedMask = bHasMask
			? AddScopedFeatureMask(Ctx, *LayerCtx, *Layer, SourceChildIndex, true)
			: Value;
		P->OutValue = GraphBuilder.CreateUAV(Value);
		P->OutHeight = Height ? GraphBuilder.CreateUAV(Height) : nullptr;
		P->OutGradient = Gradient ? GraphBuilder.CreateUAV(Gradient) : nullptr;
		P->OutIds = Ids ? GraphBuilder.CreateUAV(Ids) : nullptr;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Noise.L%d.C%d", LayerIndex, SourceChildIndex),
			Shader, P, Groups);
	}

	return {Value, Height, Gradient, Ids};
}
} // namespace

FRDGTextureRef AddNoiseCoveragePass(FMixtormatComposeContext& Ctx, FRDGTextureRef Value, const bool bSigned)
{
	check(Value);
	const FIntPoint Size = Value->Desc.Extent;
	FRDGTextureRef Coverage = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Noise.Coverage"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatNoiseCoverageCS::FParameters>();
	P->OutputSize = Size;
	P->CoverageSigned = bSigned ? 1u : 0u;
	P->CoverageValue = Value;
	P->OutCoverage = Ctx.GraphBuilder.CreateUAV(Coverage);
	TShaderMapRef<FMixtormatNoiseCoverageCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Noise.Coverage"),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Coverage;
}

FRDGTextureRef AddNoiseMaskPass(FMixtormatComposeContext& Ctx, const FMixtormatNoise& Noise)
{
	const FMixtormatNoiseRenderData Resolved = ResolveNoiseRenderData(Noise);
	const FNoiseFields Fields = AddNoiseFieldPass(Ctx, nullptr, Resolved, nullptr, INDEX_NONE, INDEX_NONE);
	const bool bSigned = MixtormatOutputReferences::NoiseValueKind(
		static_cast<EMixtormatNoiseType>(Resolved.Type)) == EMixtormatPublishedFieldKind::ScalarSigned;
	return AddNoiseCoveragePass(Ctx, Fields.Value, bSigned);
}

void AddNoisePasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const int32 SourceChildIndex, FGeneratorBundle* Bundle,
	FRDGTextureRef PreUV)
{
	FMixtormatNoiseRenderData Noise;
	// Preserve the generator's gathered-settings miss behavior.
	if (!MixtormatNoiseRenderStore().Find(FMixtormatNoiseRenderKey{Layer.LayerId, SourceChildIndex}, Noise))
	{
		return;
	}
	const EMixtormatNoiseType NoiseType = static_cast<EMixtormatNoiseType>(Noise.Type);
	const bool bProducesIds = NoiseProducesIds(NoiseType);
	const FNoiseFields Fields = AddNoiseFieldPass(Ctx, &LayerCtx, Noise, &Layer, LayerCtx.LayerIndex, SourceChildIndex, PreUV);
	FRDGTextureRef Value = Fields.Value;
	FRDGTextureRef Height = Fields.Height;
	FRDGTextureRef Gradient = Fields.Gradient;
	FRDGTextureRef Ids = Fields.Ids;

	if (Bundle)
	{
		Bundle->Height = Height;
		// Only the Worley family produces stable cell IDs. EmptyRegionIds is non-null (a
		// binding placeholder), so gating on Ids alone would publish a fake ID map for every
		// non-Worley family.
		if (bProducesIds)
		{
			Bundle->RegionIds = Ids;
		}
		// All families publish Gradient in generator-domain coordinates, but its meaning differs:
		// analytic covector for lattice families; direction for Worley/Bars. Structural completed
		// warp preserves that existing public frame by sampling, never by choosing a transform.
		Bundle->GradientDescriptor = {FGeneratorBundle::EFieldSemantic::SourceFrameVector,
			FGeneratorBundle::EFieldUnits::GeneratorDomain};
	}

	// Publication: the raw field and its generator-domain Gradient, under this child's address.
	// A completed structural warp transport-samples this declared source-frame data; consumers
	// requiring destination derivatives must apply their own explicit family-aware conversion.
	Ctx.PublishedFieldOutputs.Add(
		PublishedKey(Layer, SourceChildIndex, FName(TEXT("Value"))),
		FPublishedField{MixtormatOutputReferences::NoiseValueKind(NoiseType), Value, nullptr, nullptr, false});
	Ctx.PublishedFieldOutputs.Add(
		PublishedKey(Layer, SourceChildIndex, FName(TEXT("Gradient"))),
		FPublishedField{EMixtormatPublishedFieldKind::Vector2, Gradient, nullptr, nullptr, false});
}
}