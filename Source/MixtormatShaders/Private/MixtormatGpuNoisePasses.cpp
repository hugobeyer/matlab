// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuNoisePasses.h"

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

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

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			// The Generator layer's UV placement, the same six globals every module binds.
			SHADER_PARAMETER(uint32, GeneratorLayer)
			SHADER_PARAMETER(FVector2f, GeneratorUVScale)
			SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
			SHADER_PARAMETER(int32, GeneratorUVRotation)
			SHADER_PARAMETER(uint32, GeneratorUVFlipU)
			SHADER_PARAMETER(uint32, GeneratorUVFlipV)
			SHADER_PARAMETER(int32, NoiseType)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(float, Scale)
			SHADER_PARAMETER(int32, Detail)
			SHADER_PARAMETER(float, Roughness)
			SHADER_PARAMETER(float, Lacunarity)
			SHADER_PARAMETER(FVector2f, OffsetUV)
			SHADER_PARAMETER(FVector2f, Wave)
			SHADER_PARAMETER(float, PhaseOffset)
			SHADER_PARAMETER(uint32, ProducesIds)
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

	// Bars: the authored direction snapped to the integer wave vector that tiles.
	//
	// A stripe at an arbitrary angle does not close on the tile, so the phase direction snaps to
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

	// The value contract each family publishes. The lattice families and Bars are zero-centred
	// and signed; Ridged, Billow and the Worley distances are 0..1 magnitudes. The module height
	// is the signed remap of whichever of these the family defines -- see the USF header.
	EMixtormatPublishedFieldKind NoiseValueKind(const EMixtormatNoiseType Type)
	{
		switch (Type)
		{
		case EMixtormatNoiseType::Ridged:
		case EMixtormatNoiseType::Billow:
		case EMixtormatNoiseType::WorleyF1:
		case EMixtormatNoiseType::WorleyF2:
		case EMixtormatNoiseType::WorleyF1MinusF2:
			return EMixtormatPublishedFieldKind::Scalar01;
		default: // Gradient, Value, FBM, Bars
			return EMixtormatPublishedFieldKind::ScalarSigned;
		}
	}

	// Only the Worley family genuinely produces cells; the others have no stable identity to
	// publish, and faking one to make the output lists match is exactly what not to do.
	bool NoiseProducesIds(const EMixtormatNoiseType Type)
	{
		return Type == EMixtormatNoiseType::WorleyF1
			|| Type == EMixtormatNoiseType::WorleyF2
			|| Type == EMixtormatNoiseType::WorleyF1MinusF2;
	}

	bool NoiseIsBars(const EMixtormatNoiseType Type)
	{
		return Type == EMixtormatNoiseType::Bars;
	}
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

void AddNoisePasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const int32 SourceChildIndex, FGeneratorBundle* Bundle)
{
	FMixtormatNoiseRenderData Noise;
	// No gathered settings -- the module was disabled or the gather never saw it. Leaving the
	// module out is the same outcome a disabled generator gets.
	if (!MixtormatNoiseRenderStore().Find(FMixtormatNoiseRenderKey{Layer.LayerId, SourceChildIndex}, Noise))
	{
		return;
	}

	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

	const EMixtormatNoiseType NoiseType = static_cast<EMixtormatNoiseType>(Noise.Type);
	const bool bProducesIds = NoiseProducesIds(NoiseType);

	// Bars resolves its tileable wave and its seed phase on the CPU; every other family ignores
	// both uniforms.
	FVector2f Wave(0.0f, 1.0f);
	float PhaseOffset = 0.0f;
	if (NoiseIsBars(NoiseType))
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
	FRDGTextureRef Height = MakeTexture(PF_R32_FLOAT, TEXT("Mixtormat.Noise.Height"));
	FRDGTextureRef Gradient = MakeTexture(PF_G32R32F, TEXT("Mixtormat.Noise.Gradient"));
	// The ID map exists only where cells exist. The other families bind the context's empty ID
	// texture instead of leaving the slot null -- the binding has to be satisfied, and the
	// ProducesIds uniform keeps the write off it.
	FRDGTextureRef Ids = bProducesIds
		? MakeTexture(PF_R32_UINT, TEXT("Mixtormat.Noise.Ids"))
		: Ctx.EmptyRegionIds;

	{
		TShaderMapRef<FMixtormatNoiseCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = GraphBuilder.AllocParameters<FMixtormatNoiseCS::FParameters>();
		P->OutputSize = Size;
		// The Generator layer's UV placement, the same six values FillGeneratorPlacement writes
		// for every other module. Kept local: that helper is file-private to the generator-pass
		// file, and the six lines are the whole of it.
		P->GeneratorLayer = 1u;
		P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
		P->GeneratorUVOffset = Layer.UVOffset;
		P->GeneratorUVRotation = Layer.Rotation;
		P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
		P->NoiseType = Noise.Type;
		P->Seed = static_cast<uint32>(Noise.Seed);
		P->Scale = Noise.Scale;
		P->Detail = Noise.Detail;
		P->Roughness = Noise.Roughness;
		P->Lacunarity = Noise.Lacunarity;
		P->OffsetUV = FVector2f(Noise.OffsetX, Noise.OffsetY);
		P->Wave = Wave;
		P->PhaseOffset = PhaseOffset;
		P->ProducesIds = bProducesIds ? 1u : 0u;
		P->OutValue = GraphBuilder.CreateUAV(Value);
		P->OutHeight = GraphBuilder.CreateUAV(Height);
		P->OutGradient = GraphBuilder.CreateUAV(Gradient);
		P->OutIds = GraphBuilder.CreateUAV(Ids);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Noise.L%d.C%d", LayerCtx.LayerIndex, SourceChildIndex),
			Shader, P, Groups);
	}

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
	}

	// Publication: the raw field and its gradient, under this child's address. The meanings --
	// height, roughness, mask, whatever else -- belong to the consumers; this only states what
	// the field is.
	Ctx.PublishedFieldOutputs.Add(
		FPublishedFieldKey{Layer.LayerId, SourceChildIndex, FName(TEXT("Value"))},
		FPublishedField{NoiseValueKind(NoiseType), Value, nullptr, nullptr, false});
	Ctx.PublishedFieldOutputs.Add(
		FPublishedFieldKey{Layer.LayerId, SourceChildIndex, FName(TEXT("Gradient"))},
		FPublishedField{EMixtormatPublishedFieldKind::Vector2, Gradient, nullptr, nullptr, false});
}
}