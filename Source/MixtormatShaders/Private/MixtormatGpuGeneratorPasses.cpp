// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "Compositing/MixtormatComposeHash.h"

#include "Algo/AnyOf.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

// The GENERATORS category.
//
// A generator is not an effect and not a mask. Effects -- Erosion, Breakup, Worn Edges, Grade --
// are filters over a layer that has already been composited, which is why every one of them is
// queued during the child loop and dispatched after AddLayerCompositePass. Masks produce 0..1
// coverage and join the mask chain. A generator does neither: it rewrites the layer's *input*
// height before anything has read it, so the layer composites as though the carved surface were
// the one that had been authored.
//
// That single property is the whole category, and everything here exists to preserve it:
//
//     Layer source height
//         -> AddLayerInputPass            resolves it into output space
//         -> AddLayerHeightSmoothPasses   optional pre-smooth
//         -> AddGeneratorPasses           <- here
//         -> mask children, effects, AddLayerCompositePass
//
// AddGeneratorPasses is called once per layer from the layer loop and walks that layer's
// generator children in authored order, each one carving the height the previous one produced.
// It reassigns LayerCtx.LayerInputHeight and LayerCtx.LayerInputN in place -- exactly what
// AddLayerHeightSmoothPasses does one line above it -- and returns. Nothing downstream needs to
// know a generator ran.

namespace MixtormatGpuCompositor
{
namespace
{
	// The grid a generator's distance solve runs on.
	//
	// A propagated distance is smooth at the scale of the cells that seeded it, so solving it at 4K
	// costs sixteen times what 1K does for a result that is identical after the resolve refines it.
	// The cap is on the grid rather than on a divisor, so a material solves at the same physical
	// scale at every export resolution and a 1K preview predicts the 4K bake. The divisor is a
	// power of two so a non-square tile keeps its aspect and the wrap stays exact on both axes.
	constexpr int32 DistanceSolveMaxSize = 1024;

	FIntPoint DistanceSolveResolution(const FIntPoint Resolution)
	{
		const int32 Longest = FMath::Max(Resolution.X, Resolution.Y);
		int32 Divisor = 1;
		while (Longest / Divisor > DistanceSolveMaxSize)
		{
			Divisor *= 2;
		}
		return FIntPoint(
			FMath::Max(Resolution.X / Divisor, 1),
			FMath::Max(Resolution.Y / Divisor, 1));
	}

	// True when this generator has at least one enabled mask scoped beneath it.
	//
	// Asked before the scoped mask is resolved, because "there is no mask" and "there is a mask
	// that happens to be white" are different: the first must leave Mask Influence completely
	// inert whatever it is set to, and the second must honour it. Collapsing the two would make
	// a generator with no mask behave as though Mask Influence were always 1, which is the same
	// picture only by accident.
	bool HasScopedMasks(const FLayerRenderData& Layer, const int32 OwnerSourceChildIndex)
	{
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type == EMixtormatLayerChildType::Mask
				&& Child.ScopeOwnerSourceChildIndex == OwnerSourceChildIndex
				&& Child.Mask.Weight != 0.0f)
			{
				return true;
			}
		}
		return false;
	}
}

// Strata beds as ramps and faces, in closed form. Writes the bed IDs, the
// position inside each bed and a per-bed random alongside the height.
class FMixtormatStrataCarverResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(FIntPoint, BedWave)
		SHADER_PARAMETER(FIntPoint, BedPerp)
		SHADER_PARAMETER(int32, BedPeriod)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, ThicknessVariation)
		SHADER_PARAMETER(float, HeightVariation)
		SHADER_PARAMETER(float, Verticality)
		SHADER_PARAMETER(float, RampShape)
		SHADER_PARAMETER(float, Bend)
		SHADER_PARAMETER(int32, BendScale)
		SHADER_PARAMETER(float, Breakup)
		SHADER_PARAMETER(float, HeightFollow)
		SHADER_PARAMETER(float, Lamination)
		SHADER_PARAMETER(float, CrossBedding)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolveRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutBedIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBedPosition)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBedRandom)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"ResolveCS",
	SF_Compute);

class FMixtormatRockFormationCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRockFormationCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRockFormationCS, FGlobalShader);

	// 0 = build chunk geometry, 1 = field (cached), 2 = combine, 3 = normalised outputs.
	class FStage : SHADER_PERMUTATION_INT("ROCK_STAGE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(float, Style)
		SHADER_PARAMETER(int32, Cells)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, FractureAmount)
		SHADER_PARAMETER(float, ChamferAmount)
		SHADER_PARAMETER(float, FractureHeightBias)
		SHADER_PARAMETER(float, GapAmount)
		SHADER_PARAMETER(float, ChamferRandom)
		SHADER_PARAMETER(float, Spin)
		SHADER_PARAMETER(float, SpinRandom)
		SHADER_PARAMETER(float, TiltAngle)
		SHADER_PARAMETER(float, TiltDirection)
		SHADER_PARAMETER(float, TiltRandom)
		SHADER_PARAMETER(float, SizeRandom)
		SHADER_PARAMETER(float, Stretch)
		SHADER_PARAMETER(float, StretchAngle)
		SHADER_PARAMETER(float, StretchRandom)
		SHADER_PARAMETER(float, HeightClusters)
		SHADER_PARAMETER(float, Skew)
		SHADER_PARAMETER(float, EdgeJag)
		SHADER_PARAMETER(float, JagScale)
		SHADER_PARAMETER(float, JagDetail)
		SHADER_PARAMETER(float, ChamferJag)
		SHADER_PARAMETER(float, RimChips)
		SHADER_PARAMETER(float, RimChipSize)
		SHADER_PARAMETER(float, FacetChips)
		SHADER_PARAMETER(int32, FacetIterations)
		SHADER_PARAMETER(float, FacetFalloff)
		SHADER_PARAMETER(float, FacetRandom)
		SHADER_PARAMETER(float, FacetAlign)
		SHADER_PARAMETER(uint32, HeightMode)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(int32, MaxLeaves)
		SHADER_PARAMETER(int32, CellsV)
		SHADER_PARAMETER(float, RowHeight)
		SHADER_PARAMETER(float, WallSlope)
		SHADER_PARAMETER(float, ChamferSlope)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRockLeaf>, OutLeaves)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutEdges)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutEdgeTags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutLeafCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRockLeaf>, Leaves)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Edges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EdgeTags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, Vertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LeafCounts)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RockHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RockIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RockBoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockNormalizedHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockSlope)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockGap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockTop)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockChamfer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockWall)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockTopRamp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockChamferRamp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockWallRamp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockEdgeDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutRockBoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutRockIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRockFormationCS,
	"/Plugin/Mixtormat/Private/MixtormatRockFormation.usf",
	"MainCS",
	SF_Compute);

class FMixtormatPebblesCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPebblesCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPebblesCS, FGlobalShader);

	// 0 = field (cached), 1 = combine into the layer's input height.
	class FStage : SHADER_PERMUTATION_INT("PEBBLE_STAGE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(int32, Seed)
		SHADER_PARAMETER(int32, Cells)
		SHADER_PARAMETER(float, Density)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(float, StoneScale)
		SHADER_PARAMETER(float, ScaleVariation)
		SHADER_PARAMETER(float, RotationDegrees)
		SHADER_PARAMETER(int32, Cuts)
		SHADER_PARAMETER(int32, DirectionMode)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, Chamfer)
		SHADER_PARAMETER(float, Steepness)
		SHADER_PARAMETER(float, SteepnessVariation)
		SHADER_PARAMETER(float, BiasVariation)
		SHADER_PARAMETER(float, HeightGain)
		SHADER_PARAMETER(float, HeightVariation)
		SHADER_PARAMETER(uint32, FacetIds)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PebbleHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPebbleHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPebbleCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPebbleEdgeDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPebbleRandom)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPebbleIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPebblesCS,
	"/Plugin/Mixtormat/Private/MixtormatPebbles.usf",
	"MainCS",
	SF_Compute);

class FMixtormatCracksCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCracksCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCracksCS, FGlobalShader);
	// Field, arrival seed, propagation, chamfer resolve, combine.
	class FStage : SHADER_PERMUTATION_INT("CRACK_STAGE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(int32, Seed)
		SHADER_PARAMETER(int32, Cells)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Rough)
		SHADER_PARAMETER(float, Scale)
		SHADER_PARAMETER(float, Detail)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, WidthVariation)
		SHADER_PARAMETER(float, WidthScale)
		SHADER_PARAMETER(float, LineVariation)
		SHADER_PARAMETER(float, RegionVariation)
		SHADER_PARAMETER(float, Chip)
		SHADER_PARAMETER(float, ChipSize)
		SHADER_PARAMETER(float, Gap)
		SHADER_PARAMETER(float, GapWidth)
		SHADER_PARAMETER(float, Slip)
		SHADER_PARAMETER(float, Tilt)
		SHADER_PARAMETER(float, ChamferAmount)
		SHADER_PARAMETER(float, ChamferStart)
		SHADER_PARAMETER(float, ChamferEnd)
		SHADER_PARAMETER(float, ChamferLow)
		SHADER_PARAMETER(float, ChamferHigh)
		SHADER_PARAMETER(float, ChamferNoise)
		SHADER_PARAMETER(float, ChamferNoiseScale)
		SHADER_PARAMETER(uint32, HasGate)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GateMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutCrackIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackRandom)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCrackBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutChamferCut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArrival)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatCracksCS,
	"/Plugin/Mixtormat/Private/MixtormatCracks.usf", "MainCS", SF_Compute);

// Measured-range normalization for generator height fields. See MixtormatFieldRange.usf.
class FMixtormatFieldRangeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFieldRangeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFieldRangeCS, FGlobalShader);

	// 0 reduce min/max, 1 normalize.
	class FStage : SHADER_PERMUTATION_INT("RANGE_STAGE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, OutLow)
		SHADER_PARAMETER(float, OutHigh)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceField)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutRange)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Range)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFieldRangeCS,
	"/Plugin/Mixtormat/Private/MixtormatFieldRange.usf",
	"MainCS",
	SF_Compute);

// Already inside namespace MixtormatGpuCompositor (opened at the top of the file).
// A height field remapped from its own measured min/max onto [OutLow, OutHigh], on the GPU (no
// readback). Always a new texture: the source (a cached generator field, or the final height) is
// never written by this.
FRDGTextureRef AddNormalizeFieldPasses(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Field,
	const FIntPoint Size,
	const float OutLow,
	const float OutHigh,
	const TCHAR* Name)
{
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
	FRDGBufferRef RangeBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2), TEXT("Mixtormat.FieldRange"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RangeBuffer), 0u);
	{
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(0);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->SourceField = Field;
		P->OutRange = GraphBuilder.CreateUAV(RangeBuffer);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FieldRange.Reduce"), Shader, P, Groups);
	}
	FRDGTextureRef Normalized = GraphBuilder.CreateTexture(Field->Desc, Name);
	{
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(1);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->OutLow = OutLow;
		P->OutHigh = OutHigh;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Normalized);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FieldRange.Normalize"), Shader, P, Groups);
	}
	return Normalized;
}

// Generator flow tools scoped under a Rock Formation. One parameter struct for every stage;
// ClearUnusedGraphResources drops what a stage does not read. See MixtormatGeneratorFlow.usf.
class FMixtormatGeneratorFlowCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorFlowCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorFlowCS, FGlobalShader);

	// 0 seed, 1 jump flood, 2 resolve, 3 apply, 4 direction preview, 5 UV grid preview,
	// 6 pack a scalar signed distance (Pebbles) into the seed stage's boundary pair,
	// 7 one axis of the direction blur, 8 trace a published field into destination UVs.
	class FStage : SHADER_PERMUTATION_INT("FLOW_STAGE", 9);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(int32, JumpStep)
		SHADER_PARAMETER(uint32, Source)
		SHADER_PARAMETER(float, Tangent)
		SHADER_PARAMETER(float, Angle)
		SHADER_PARAMETER(float, Bend)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER(float, Smooth)
		SHADER_PARAMETER(FIntPoint, BlurAxis)
		SHADER_PARAMETER(float, Reach)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, OffsetAlong)
		SHADER_PARAMETER(float, OffsetAcross)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, HasCoverage)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(float, ShapeOffset)
		SHADER_PARAMETER(float, Bulge)
		SHADER_PARAMETER(float, TraceLength)
		SHADER_PARAMETER(int32, Steps)
		SHADER_PARAMETER(float, WarpStrength)
		SHADER_PARAMETER(uint32, CarveMode)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Falloff)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RockHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SeedData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, JumpIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FlowField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FlowSmooth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowValidity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, WarpedUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarBoundary)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Coverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSeedData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutJump)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlowField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutInfluence)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutValidity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCarveMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutWarpedUV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCoverage)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatGeneratorFlowCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorFlow.usf",
	"MainCS",
	SF_Compute);


class FMixtormatGeneratorBundleCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorBundleCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorBundleCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("BUNDLE_STAGE", 9);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, CoverageMode)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, WarpedUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, IdField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VectorField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScalar)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutVector)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorBundleCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorBundle.usf", "MainCS", SF_Compute);

namespace
{
	// Shared payload/index contract. A layer is not a synthetic child and never takes the
	// child ID-phase publication path; children keep their field-only and memo execution.
	struct FGeneratorPassInput
	{
		const FGeneratorRenderData& Generator;
		int32 SourceChildIndex;
		bool bGeneratorLayer;
	};

	template<typename TParameters>
	void FillGeneratorPlacement(TParameters* P, const FLayerRenderData& Layer, const bool bGeneratorLayer)
	{
		P->GeneratorLayer = bGeneratorLayer ? 1u : 0u;
		P->GeneratorUVScale = bGeneratorLayer
			? FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY)
			: FVector2f(1.0f, 1.0f);
		P->GeneratorUVOffset = bGeneratorLayer ? Layer.UVOffset : FVector2f::ZeroVector;
		P->GeneratorUVRotation = bGeneratorLayer ? Layer.Rotation : 0;
		P->GeneratorUVFlipU = bGeneratorLayer && Layer.bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = bGeneratorLayer && Layer.bFlipV ? 1u : 0u;
	}

	FRDGTextureRef AddBundleCoverage(FMixtormatComposeContext& Ctx, const uint32 Mode,
		FRDGTextureRef Scalar = nullptr, FRDGTextureRef Boundary = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Coverage = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.Coverage"));
		FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(4);
		TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
		P->OutputSize = Size;
		P->CoverageMode = Mode;
		// Mode is a uniform, so RDG needs both resources bound even for constant coverage.
		P->ScalarField = Scalar ? Scalar : Ctx.EmptyDriverSignal;
		P->BoundaryField = Boundary ? Boundary : Ctx.EmptyPatternUV;
		P->OutScalar = GraphBuilder.CreateUAV(Coverage);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.Coverage"),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Coverage;
	}

	FRDGTextureRef RemapBundleField(FMixtormatComposeContext& Ctx, FRDGTextureRef Field,
		FRDGTextureRef WarpedUV, const int32 Stage, FRDGTextureRef Boundary = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FRDGTextureDesc Desc = Stage == 8
					? FRDGTextureDesc::Create2D(Size, PF_G32R32F, FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV) : Field->Desc;
				FRDGTextureRef Moved = GraphBuilder.CreateTexture(Desc, TEXT("Mixtormat.Generator.MovedField"));
		FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(Stage);
		TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
		P->OutputSize = Size;
		P->WarpedUV = WarpedUV;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		if (Stage == 1)
		{
			P->IdField = Field;
			P->OutIds = GraphBuilder.CreateUAV(Moved);
		}
		else if (Stage == 2 || Stage == 3 || Stage == 5 || Stage == 8)
		{
			P->VectorField = Field;
			P->ScalarField = Field;
			P->BoundaryField = Field;
			P->OutVector = GraphBuilder.CreateUAV(Moved);
		}
		else
		{
			P->ScalarField = Field;
			P->BoundaryField = Boundary;
			P->OutScalar = GraphBuilder.CreateUAV(Moved);
		}
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.Remap%d", Stage),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Moved;
	}

	void RemapGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle, FRDGTextureRef WarpedUV)
	{
		if (Bundle.RegionIds) { Bundle.RegionIds = RemapBundleField(Ctx, Bundle.RegionIds, WarpedUV, 1); }
		if (Bundle.CentreUV) { Bundle.CentreUV = RemapBundleField(Ctx, Bundle.CentreUV, WarpedUV, 5); }
		if (Bundle.Orientation) { Bundle.Orientation = RemapBundleField(Ctx, Bundle.Orientation, WarpedUV, 6); }
		for (TPair<FName, FRDGTextureRef>& Mask : Bundle.NamedMasks)
		{
			const bool bDistance = Mask.Key == FName(TEXT("RockEdgeDistance"))
				|| Mask.Key == FName(TEXT("PebbleEdgeDistance"));
			Mask.Value = RemapBundleField(Ctx, Mask.Value, WarpedUV,
				bDistance && Bundle.BoundaryField ? 7 : 0, Bundle.BoundaryField);
		}
		// Rebuild the local distance metric before the next tool seeds against the moved outline.
		if (Bundle.BoundaryField)
		{
			Bundle.BoundaryField = RemapBundleField(Ctx, Bundle.BoundaryField, WarpedUV, 2);
		}
	}

	// The bedding as an integer lattice vector, which is what lets the beds tile.
	//
	// A bedding plane at an arbitrary angle does not close on the tile, so the direction snaps to
	// the nearest small integer vector (p, q) and the beds are that vector scaled by N. N is
	// chosen so the bed spacing stays near what Frequency asked for whatever the direction.
	// Scaling by N also makes N the number of beds before the per-bed randoms repeat.
	struct FStrataLattice
	{
		FIntPoint Wave;
		FIntPoint Perp;
		int32 Period = 1;
	};

	FStrataLattice MakeStrataLattice(const float Frequency, const float RotationDegrees)
	{
		// Beds lie across the direction (sin, cos): rotation 0 is horizontal beds, and 180 is the
		// same beds with their faces turned the other way.
		const float Radians = FMath::DegreesToRadians(RotationDegrees);
		const FVector2f Want(FMath::Sin(Radians), FMath::Cos(Radians));

		FIntPoint Best(0, 1);
		float BestAlignment = -2.0f;
		constexpr int32 MaxComponent = 3;
		for (int32 P = -MaxComponent; P <= MaxComponent; ++P)
		{
			for (int32 Q = -MaxComponent; Q <= MaxComponent; ++Q)
			{
				if ((P == 0 && Q == 0) || FMath::GreatestCommonDivisor(FMath::Abs(P), FMath::Abs(Q)) != 1)
				{
					continue;
				}
				const float Alignment = (Want.X * P + Want.Y * Q) / FMath::Sqrt(static_cast<float>(P * P + Q * Q));
				if (Alignment > BestAlignment)
				{
					BestAlignment = Alignment;
					Best = FIntPoint(P, Q);
				}
			}
		}

		const float Length = FMath::Sqrt(static_cast<float>(Best.X * Best.X + Best.Y * Best.Y));
		FStrataLattice Lattice;
		Lattice.Period = FMath::Max(FMath::RoundToInt(Frequency / Length), 1);
		Lattice.Wave = FIntPoint(Best.X * Lattice.Period, Best.Y * Lattice.Period);
		Lattice.Perp = FIntPoint(-Lattice.Wave.Y, Lattice.Wave.X);
		return Lattice;
	}

	// One Strata Carver child.
	//
	// Takes the height currently standing as the layer's input and returns the carved one. The
	// caller chains them, so two carvers on one layer are two carves of one surface rather than
	// two competing for the same slot.
	//
	// The bedding coordinate reads the source height (Height Follow), so this cannot publish in
	// the ID phase: its bed IDs exist from here on, for the children below it, and UV From IDs
	// -- which resolves before the layer's source is read -- cannot key off them.
	FRDGTextureRef AddStrataCarverPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef SourceHeight,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FStrataCarverRenderData& Carver = Child.Generator.StrataCarver;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FIntPoint Size = Request.Resolution;

		// The scoped mask. Independent scope, so the chain starts from white rather than
		// inheriting whatever the layer mask happens to be at this row: a mask authored under a
		// generator describes where *this generator* acts, and folding the layer's own coverage
		// into it would make Mask Influence mean two different things depending on row order.
		const bool bHasScopedMask = !Child.bGeneratorLayer && HasScopedMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef ScopedMask = bHasScopedMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
			: (Child.bGeneratorLayer ? SourceHeight : LayerCtx.CombinedMask);

		// Region IDs published above this generator in the same layer, by the nearest producer.
		// Absent is the normal case and not an error -- a generator makes its own structure and
		// never needs one.
		FRDGTextureRef RegionIds =
			Child.bGeneratorLayer ? nullptr : FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		const bool bHasRegionIds = RegionIds != nullptr;
		if (!bHasRegionIds)
		{
			RegionIds = Ctx.EmptyRegionIds;
		}

		const FStrataLattice Lattice = MakeStrataLattice(Carver.StrataFrequency, Carver.StrataRotation);

		const auto MakeField = [&GraphBuilder, Size](const EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef CarvedHeight =
			MakeField(PF_R32_FLOAT, TEXT("Mixtormat.StrataCarvedHeight"));
		FRDGTextureRef BedIds = MakeField(PF_R32_UINT, TEXT("Mixtormat.StrataBedIds"));
		FRDGTextureRef BedPosition = MakeField(PF_R16F, TEXT("Mixtormat.StrataBedPosition"));
		FRDGTextureRef BedRandom = MakeField(PF_R16F, TEXT("Mixtormat.StrataBedRandom"));

		{
			FMixtormatStrataCarverResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverResolveCS::FParameters>();
			FillGeneratorPlacement(P, Layer, Child.bGeneratorLayer);
						P->BedWave = Lattice.Wave;
			P->BedPerp = Lattice.Perp;
			P->BedPeriod = Lattice.Period;
			P->OutputSize = Size;
			P->Seed = Carver.Seed;
			P->ThicknessVariation = Carver.ThicknessVariation;
			P->HeightVariation = Carver.HeightVariation;
			P->Verticality = Carver.Verticality;
			P->RampShape = Carver.RampShape;
			P->Bend = Carver.Bend;
			P->BendScale = Carver.BendScale;
			P->Breakup = Carver.Breakup;
			P->HeightFollow = Carver.HeightFollow;
			P->Lamination = Carver.Lamination;
			P->CrossBedding = Carver.CrossBedding;
			P->Depth = Carver.Depth;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->SourceHeight = SourceHeight;
			P->ResolveMask = ScopedMask;
			P->ResolveRegionIds = RegionIds;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(CarvedHeight);
			P->OutBedIds = GraphBuilder.CreateUAV(BedIds);
			P->OutBedPosition = GraphBuilder.CreateUAV(BedPosition);
			P->OutBedRandom = GraphBuilder.CreateUAV(BedRandom);
			TShaderMapRef<FMixtormatStrataCarverResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.L%d.C%d", LayerIndex, Child.SourceChildIndex),
				Shader, P,
				FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		}

		if (Bundle)
				{
					Bundle->Height = CarvedHeight;
					Bundle->Coverage = AddBundleCoverage(Ctx, 0);
					Bundle->RegionIds = BedIds;
					Bundle->BoundaryField = RemapBundleField(Ctx, BedPosition, nullptr, 8);
					Bundle->NamedMasks.Add(FName(TEXT("StrataPosition")), BedPosition);
					Bundle->NamedMasks.Add(FName(TEXT("StrataRandom")), BedRandom);
					return CarvedHeight;
				}

				// Bed IDs for the ID consumers below this row (a colour or hue per bed), and the position
		// and a per-bed random as masks -- position is the natural scalar for a colour ramp.
		PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, BedIds);
		const TPair<FName, FRDGTextureRef> Masks[2] = {
			{FName(TEXT("StrataPosition")), BedPosition},
			{FName(TEXT("StrataRandom")), BedRandom}};
		for (const TPair<FName, FRDGTextureRef>& Mask : Masks)
		{
			Ctx.PublishedMaskOutputs.Add(
				FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, Mask.Key}, Mask.Value);
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, Mask.Key,
				LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewMaskBlitPass(GraphBuilder, Mask.Value,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}
		}
		if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
			LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewRegionIdsBlitPass(GraphBuilder, BedIds, nullptr,
				Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
		}

		return CarvedHeight;
	}

	// One Rock Formation child.
	//
	// The field (height, top / chamfer / wall coverage, edge distance, chunk IDs) is a function
	// of the field settings and the resolution only, so it goes through the node cache and is
	// evaluated again only when one of those settings changes. It is the most expensive pass in
	// the plugin per pixel -- polygon clipping for every nearby cell and BSP node -- which is
	// exactly why editing anything else must not re-run it.
	//
	// Height Mode and Height Scale resolve the cached raw field in cheap passes, so neither
	// setting needs to rebuild the geometry or re-evaluate the field.
	// Mirrors the shader's preset table for the three values the C++ side needs to size buffers
	// and bind constants: row multiplier, split count and wall angle.
	struct FRockLayout
	{
		int32 CellsU = 1;
		int32 CellsV = 1;
		float RowHeight = 1.0f;
		int32 MaxLeaves = 1;
		float WallSlope = 1.0f;
		float ChamferSlope = 1.0f;
	};

	FRockLayout ResolveRockLayout(const FRockFormationRenderData& Rock)
	{
		// row_mul, splits, wall angle for cliff, layered, boulder, rubble.
		static const float Preset[4][3] = {
			{1.0f, 3.0f, 72.0f}, {2.0f, 2.0f, 72.0f}, {1.0f, 2.0f, 58.0f}, {1.0f, 1.0f, 60.0f}};
		// Match ROCK_STYLE_FROM / ROCK_STYLE_TO; only the table position is bounded.
		const float Style = FMath::Clamp(FMath::Lerp(1.0f, 2.5f, Rock.Style), 0.0f, 3.0f);
		const int32 Row = FMath::Min(static_cast<int32>(Style), 2);
		const float T = Style - static_cast<float>(Row);
		const auto Blend = [&](const int32 Column)
		{
			return FMath::Lerp(Preset[Row][Column], Preset[Row + 1][Column], T);
		};

		FRockLayout Layout;
		Layout.CellsU = FMath::Max(Rock.Cells, 1);
		Layout.CellsV = FMath::Max(Rock.Rows, 1);
		Layout.RowHeight = static_cast<float>(Layout.CellsU) / static_cast<float>(Layout.CellsV);
		const int32 Splits = FMath::Max(FMath::RoundToInt(Blend(1) * Rock.Fracture), 0);
		// Every internal BSP node has two children, so a tree with Splits internal nodes has
		// Splits + 1 leaves. Bounded by the per-cell buffer slot count, not by taste.
		Layout.MaxLeaves = FMath::Clamp(Splits + 1, 1, 256);
		Layout.WallSlope = FMath::Tan(FMath::DegreesToRadians(Blend(2)));
		Layout.ChamferSlope = FMath::Min(FMath::Tan(FMath::DegreesToRadians(42.0f)), Layout.WallSlope);
		return Layout;
	}

	// GPU mirror of FRockLeaf in MixtormatRockFormation.usf, for the buffer stride only.
	struct FRockLeafStride
	{
		// Cx Cy, Sx Sy, TopConstant, Top, Radius, Rv, JagOffset, JagSize.
		float Floats[10];
		// Ck, Pk, ChipOn, EdgeCount, VertexCount, Id, SeamBits, Pad.
		uint32 Uints[8];
	};
	static_assert(sizeof(FRockLeafStride) == 72, "Rock leaf stride must match FRockLeaf in HLSL");
	static constexpr int32 RockMaxVertices = 24;

	bool IsFlowToolChild(const FChildRenderData& Candidate, const int32 OwnerSourceChildIndex)
	{
		return Candidate.Type == EMixtormatLayerChildType::Effect
			&& MixtormatIsGeneratorFlowEffect(Candidate.Effect.Type)
			&& Candidate.ScopeOwnerSourceChildIndex == OwnerSourceChildIndex;
	}

	bool IsPreviewingChild(const FRenderRequest& Request, const int32 LayerIndex, const int32 ChildIndex)
	{
		return Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::ChildOutput
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == ChildIndex;
	}

	// An item that would leave the height unchanged. Still solved while its preview is up.
	bool IsNeutralFlowTool(const FEffectRenderData& Flow)
	{
		if (Flow.GeneratorFlowAmount == 0.0f)
		{
			return true;
		}
		switch (Flow.Type)
		{
		case EMixtormatEffectType::ShapeDeform:
			return Flow.GeneratorFlowShapeOffset == 0.0f && Flow.GeneratorFlowBulge == 0.0f;
		case EMixtormatEffectType::GeneratorFlow:
			return Flow.GeneratorFlowTraceLength == 0.0f || Flow.GeneratorFlowWarpStrength == 0.0f;
		case EMixtormatEffectType::FlowCarve:
			return Flow.GeneratorFlowTraceLength == 0.0f || Flow.GeneratorFlowDepth == 0.0f;
		default:
			return true;
		}
	}

	bool IsPreviewingAnyFlowTool(
		const FRenderRequest& Request,
		const int32 LayerIndex,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex)
	{
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (IsFlowToolChild(Candidate, OwnerSourceChildIndex)
				&& IsPreviewingChild(Request, LayerIndex, Candidate.SourceChildIndex))
			{
				return true;
			}
		}
		return false;
	}

	bool IsFlowFieldDemanded(const FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer, const int32 ChildIndex)
	{
		return Ctx.PublishedFieldDemand.Contains(
			FPublishedFieldKey{Layer.LayerId, ChildIndex, FName(TEXT("FlowDirection"))})
			|| Ctx.PublishedFieldDemand.Contains(
				FPublishedFieldKey{Layer.LayerId, ChildIndex, FName(TEXT("WarpedUV"))});
	}

	bool HasActiveFlowTools(
		const FMixtormatComposeContext& Ctx,
		const int32 LayerIndex,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex)
	{
		const FRenderRequest& Request = Ctx.Request;
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (IsFlowToolChild(Candidate, OwnerSourceChildIndex)
				&& (!IsNeutralFlowTool(Candidate.Effect)
					|| IsPreviewingChild(Request, LayerIndex, Candidate.SourceChildIndex)
					|| IsFlowFieldDemanded(Ctx, Layer, Candidate.SourceChildIndex)))
			{
				return true;
			}
		}
		return false;
	}

	// Pebbles publishes a scalar signed distance with a 1e9 no-hit sentinel; the seed stage
	// reads a (distance, outline-sampled) pair. One cheap pass, never written into the cache.
	FRDGTextureRef PackScalarBoundary(FMixtormatComposeContext& Ctx, FRDGTextureRef Scalar)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Packed = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.GeneratorFlow.Boundary"));
		FMixtormatGeneratorFlowCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorFlowCS::FStage>(6);
		TShaderMapRef<FMixtormatGeneratorFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
		P->OutputSize = Size;
		P->ScalarBoundary = Scalar;
		P->OutBoundary = GraphBuilder.CreateUAV(Packed);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.GeneratorFlow.PackBoundary"),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Packed;
	}

	// Shape Deform / Generator Flow / Flow Carve under one generator, in authored order.
	//
	// Each item solves its own direction field (its Source, Radius and bend differ) against the
	// height the previous item left, then transforms that height. RockField is the owner's cached
	// height and is never written: every result is a new texture, so the node cache and the
	// per-layer memo keep the undeformed field. The boundary field is the owner's original one --
	// a Shape Deform above does not re-derive it. Published masks/IDs stay undeformed: they are
	// published in the ID phase, before this runs.
	//
	// InOutCoverage, when the owner has one (Pebbles), is transformed with the height so the
	// owner's combine gates the moved height by moved coverage. Null for Rock Formation.
	FRDGTextureRef AddGeneratorFlowToolPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef BoundaryField,
		FRDGTextureRef RockField,
		FRDGTextureRef& InOutCoverage,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FIntPoint Size = Request.Resolution;
		// The capped solve grid; the resolve refines distance at full res.
		const FIntPoint SolveSize = DistanceSolveResolution(Size);
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const FIntVector SolveGroups(
			FMath::DivideAndRoundUp(SolveSize.X, 8), FMath::DivideAndRoundUp(SolveSize.Y, 8), 1);
		FRHISamplerState* const Sampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

		const auto MakeTexture = [&GraphBuilder](const FIntPoint Extent, const EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Extent, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		const auto StageShader = [](const int32 Stage)
		{
			FMixtormatGeneratorFlowCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatGeneratorFlowCS::FStage>(Stage);
			return TShaderMapRef<FMixtormatGeneratorFlowCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		};

		FRDGTextureRef Current = RockField;
		for (const FChildRenderData& FlowChild : Layer.Children)
		{
			if (!IsFlowToolChild(FlowChild, OwnerSourceChildIndex))
			{
				continue;
			}
			const FEffectRenderData& Flow = FlowChild.Effect;
			const int32 FlowIndex = FlowChild.SourceChildIndex;
			const bool bPreviewing = IsPreviewingChild(Request, LayerIndex, FlowIndex);
			const bool bNeutral = IsNeutralFlowTool(Flow);
			if (bNeutral && !bPreviewing && !IsFlowFieldDemanded(Ctx, Layer, FlowIndex))
			{
				continue;
			}

			// Independent scope, as under Strata Carver: the item's mask says where this item
			// acts, not where the layer is.
			const bool bHasMask = HasScopedMasks(Layer, FlowIndex);
			FRDGTextureRef Mask = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, FlowIndex, true)
				: Current;

			const auto Fill = [&](FMixtormatGeneratorFlowCS::FParameters* P)
			{
				P->OutputSize = Size;
				P->SolveSize = SolveSize;
				P->JumpStep = 1;
				P->Source = Flow.GeneratorFlowSource;
				P->Tangent = Flow.GeneratorFlowTangent;
				P->Angle = Flow.GeneratorFlowAngle;
				P->Bend = Flow.GeneratorFlowBend;
				P->Seed = Flow.GeneratorFlowSeed;
				P->Radius = Flow.GeneratorFlowRadius;
				P->Smooth = Flow.GeneratorFlowSmooth;
				P->BlurAxis = FIntPoint(1, 0);
				P->Reach = Flow.GeneratorFlowReach;
				P->Feather = Flow.GeneratorFlowFeather;
				P->Amount = Flow.GeneratorFlowAmount;
				P->OffsetAlong = Flow.GeneratorFlowOffsetAlong;
				P->OffsetAcross = Flow.GeneratorFlowOffsetAcross;
				P->HasMask = bHasMask ? 1u : 0u;
				P->Mode = Flow.Type == EMixtormatEffectType::ShapeDeform ? 0u
					: (Flow.Type == EMixtormatEffectType::GeneratorFlow ? 1u : 2u);
				P->ShapeOffset = Flow.GeneratorFlowShapeOffset;
				P->Bulge = Flow.GeneratorFlowBulge;
				P->TraceLength = Flow.GeneratorFlowTraceLength;
				P->Steps = Flow.GeneratorFlowSteps;
				P->WarpStrength = Flow.GeneratorFlowWarpStrength;
				P->CarveMode = Flow.GeneratorFlowCarveMode;
				P->Depth = Flow.GeneratorFlowDepth;
				P->Width = Flow.GeneratorFlowWidth;
				P->Falloff = Flow.GeneratorFlowFalloff;
				P->LinearWrapSampler = Sampler;
				P->RockHeight = Current;
				P->BoundaryField = BoundaryField;
				P->FlowMask = Mask;
				P->HasCoverage = InOutCoverage ? 1u : 0u;
				P->GeneratorLayer = Bundle ? 1u : 0u;
				P->Coverage = InOutCoverage ? InOutCoverage : Current;
			};

			// Seed.
			FRDGTextureRef SeedData = MakeTexture(SolveSize, PF_A32B32G32R32F, TEXT("Mixtormat.GeneratorFlow.Seeds"));
			FRDGTextureRef Jump[2] = {
				MakeTexture(SolveSize, PF_G32R32F, TEXT("Mixtormat.GeneratorFlow.JumpA")),
				MakeTexture(SolveSize, PF_G32R32F, TEXT("Mixtormat.GeneratorFlow.JumpB"))};
			{
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(0);
				auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
				Fill(P);
				P->OutSeedData = GraphBuilder.CreateUAV(SeedData);
				P->OutJump = GraphBuilder.CreateUAV(Jump[0]);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Seed.L%d.C%d", LayerIndex, FlowIndex),
					Shader, P, SolveGroups);
			}

			// Jump flood: halving strides from the largest power of two within half the grid,
			// then one extra stride-1 pass to repair the usual JFA misses.
			int32 Read = 0;
			{
				int32 Stride = 1;
				while (Stride * 2 <= FMath::Max(SolveSize.X, SolveSize.Y) / 2)
				{
					Stride *= 2;
				}
				TArray<int32, TInlineAllocator<16>> Strides;
				for (; Stride >= 1; Stride /= 2)
				{
					Strides.Add(Stride);
				}
				Strides.Add(1);
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(1);
				for (const int32 Step : Strides)
				{
					auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
					Fill(P);
					P->JumpStep = Step;
					P->JumpIn = Jump[Read];
					P->OutJump = GraphBuilder.CreateUAV(Jump[1 - Read]);
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Jump%d.L%d.C%d", Step, LayerIndex, FlowIndex),
						Shader, P, SolveGroups);
					Read = 1 - Read;
				}
			}

			// Resolve at full resolution. Half precision holds a unit direction, a UV distance
			// under 1 and a 0..1 influence; WarpedUV below must stay 32F to resolve 4K texels.
			FRDGTextureRef FlowField = MakeTexture(Size, PF_FloatRGBA, TEXT("Mixtormat.GeneratorFlow.Field"));
			FRDGTextureRef Influence = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.Influence"));
			FRDGTextureRef Validity = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.Validity"));
			{
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(2);
				auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
				Fill(P);
				P->SeedData = SeedData;
				P->JumpIn = Jump[Read];
				P->OutFlowField = GraphBuilder.CreateUAV(FlowField);
				P->OutInfluence = GraphBuilder.CreateUAV(Influence);
				P->OutValidity = GraphBuilder.CreateUAV(Validity);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Resolve.L%d.C%d", LayerIndex, FlowIndex),
					Shader, P, Groups);
			}

			// Smooth: X then Y. Skipped at zero, where the raw field doubles as the smoothed one.
			FRDGTextureRef FlowSmooth = FlowField;
			if (Flow.GeneratorFlowSmooth != 0.0f)
			{
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(7);
				FRDGTextureRef Blurred[2] = {
					MakeTexture(Size, PF_FloatRGBA, TEXT("Mixtormat.GeneratorFlow.SmoothX")),
					MakeTexture(Size, PF_FloatRGBA, TEXT("Mixtormat.GeneratorFlow.SmoothY"))};
				FRDGTextureRef Input = FlowField;
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
					Fill(P);
					P->BlurAxis = Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1);
					P->FlowField = Input;
					P->OutFlowField = GraphBuilder.CreateUAV(Blurred[Axis]);
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Smooth%d.L%d.C%d", Axis, LayerIndex, FlowIndex),
						Shader, P, Groups);
					Input = Blurred[Axis];
				}
				FlowSmooth = Blurred[1];
			}

			// Apply.
			FRDGTextureRef Transformed = MakeTexture(Size, Current->Desc.Format, TEXT("Mixtormat.GeneratorFlow.Height"));
			FRDGTextureRef CarveMask = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.CarveMask"));
			FRDGTextureRef WarpedUV = MakeTexture(Size, PF_G32R32F, TEXT("Mixtormat.GeneratorFlow.WarpedUV"));
			FRDGTextureRef MovedCoverage = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.Coverage"));
			{
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(3);
				auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
				Fill(P);
				P->FlowField = FlowField;
				P->FlowSmooth = FlowSmooth;
				P->OutHeight = GraphBuilder.CreateUAV(Transformed);
				P->OutCarveMask = GraphBuilder.CreateUAV(CarveMask);
				P->OutWarpedUV = GraphBuilder.CreateUAV(WarpedUV);
				P->OutCoverage = GraphBuilder.CreateUAV(MovedCoverage);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Apply.L%d.C%d", LayerIndex, FlowIndex),
					Shader, P, Groups);
			}

			Ctx.PublishedFieldOutputs.Add(
				FPublishedFieldKey{Layer.LayerId, FlowIndex, FName(TEXT("FlowDirection"))},
				FPublishedField{EMixtormatPublishedFieldKind::Flow, FlowField, FlowSmooth, Validity, false});
			if (Flow.Type != EMixtormatEffectType::FlowCarve)
			{
				Ctx.PublishedFieldOutputs.Add(
					FPublishedFieldKey{Layer.LayerId, FlowIndex, FName(TEXT("WarpedUV"))},
					FPublishedField{EMixtormatPublishedFieldKind::UVMap, WarpedUV, nullptr, nullptr, false});
			}

			// Selected-item previews.
			if (bPreviewing)
			{
				FRDGTextureRef Debug = Ctx.OutputDebug[Request.PublishedTargetIndex];
				const auto BlitMask = [&](const TCHAR* Name, FRDGTextureRef MaskSource)
				{
					if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, FName(Name),
						LayerIndex, FlowIndex))
					{
						AddDebugPreviewMaskBlitPass(GraphBuilder, MaskSource, Debug, Size);
					}
				};
				BlitMask(TEXT("Influence"), Influence);
				BlitMask(TEXT("Validity"), Validity);
				BlitMask(TEXT("CarveMask"), CarveMask);
				const auto BlitStage = [&](const int32 Stage, const EMixtormatPreviewOutputKind Kind, const TCHAR* Name)
				{
					if (!IsChildOutputPreviewTarget(Request, Kind, FName(Name), LayerIndex, FlowIndex))
					{
						return;
					}
					TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(Stage);
					auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
					Fill(P);
					// The direction preview shows the smoothed field the tools actually trace.
					P->FlowField = Stage == 4 ? FlowSmooth : FlowField;
					P->FlowValidity = Validity;
					P->WarpedUV = WarpedUV;
					P->OutputDebug = GraphBuilder.CreateUAV(Debug);
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.GeneratorFlow.Preview%d.L%d.C%d", Stage, LayerIndex, FlowIndex),
						Shader, P, Groups);
				};
				BlitStage(4, EMixtormatPreviewOutputKind::FlowDirection, TEXT("FlowDirection"));
				BlitStage(5, EMixtormatPreviewOutputKind::WarpedUVGrid, TEXT("WarpedUVGrid"));
			}

			if (!bNeutral)
			{
				Current = Transformed;
				if (InOutCoverage)
				{
					InOutCoverage = MovedCoverage;
				}
				if (Bundle)
				{
					Bundle->Height = Current;
					Bundle->Coverage = InOutCoverage;
					if (Flow.Type != EMixtormatEffectType::FlowCarve)
					{
						RemapGeneratorBundle(Ctx, *Bundle, WarpedUV);
						BoundaryField = Bundle->BoundaryField;
					}
					if (Bundle->NamedMasks.Contains(FName(TEXT("PebbleCoverage"))))
					{
						Bundle->NamedMasks[FName(TEXT("PebbleCoverage"))] = InOutCoverage;
					}
				}
			}
		}
		return Current;
	}

	FRDGTextureRef AddRockFormationPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef SourceHeight,
		const bool bFieldOnly = false,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRockFormationRenderData& Rock = Child.Generator.RockFormation;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const FRockLayout Layout = ResolveRockLayout(Rock);
		const auto FillParameters = [&Rock, &Layout, &Layer, &Child, Size](FMixtormatRockFormationCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.bGeneratorLayer);
			P->OutputSize = Size;
			P->MaxLeaves = Layout.MaxLeaves;
			P->CellsV = Layout.CellsV;
			P->RowHeight = Layout.RowHeight;
			P->WallSlope = Layout.WallSlope;
			P->ChamferSlope = Layout.ChamferSlope;
			P->Style = Rock.Style;
			P->Cells = Rock.Cells;
			P->Seed = Rock.Seed;
			P->FractureAmount = Rock.Fracture;
			P->ChamferAmount = Rock.Chamfer;
			P->FractureHeightBias = Rock.FractureHeightBias;
			P->GapAmount = Rock.Gap;
			P->ChamferRandom = Rock.ChamferRandom;
			P->Spin = Rock.Spin;
			P->SpinRandom = Rock.SpinRandom;
			P->TiltAngle = Rock.TiltAngle;
			P->TiltDirection = Rock.TiltDirection;
			P->TiltRandom = Rock.TiltRandom;
			P->SizeRandom = Rock.SizeRandom;
			P->Stretch = Rock.Stretch;
			P->StretchAngle = Rock.StretchAngle;
			P->StretchRandom = Rock.StretchRandom;
			P->HeightClusters = Rock.HeightClusters;
			P->Skew = Rock.Skew;
			P->EdgeJag = Rock.EdgeJag;
			P->JagScale = Rock.JagScale;
			P->JagDetail = Rock.JagDetail;
			P->ChamferJag = Rock.ChamferJag;
			P->RimChips = Rock.RimChips;
			P->RimChipSize = Rock.RimChipSize;
			P->FacetChips = Rock.FacetChips;
			P->FacetIterations = Rock.FacetIterations;
			P->FacetFalloff = Rock.FacetFalloff;
			P->FacetRandom = Rock.FacetRandom;
			P->FacetAlign = Rock.FacetAlign;
			// Build and Field must never bake these combine-only settings into cached outputs.
			P->HeightMode = static_cast<uint32>(EMixtormatRockHeightMode::Raw);
			P->HeightScale = 1.0f;
		};

		// Fixed cache slots: height, top, chamfer, wall, signed boundary distance, IDs,
		// signed boundary distance + outline-sampled flag (RG32F), then the top, chamfer and wall
		// ramps. Normalised height, slope and gap are derived once per layer in slots 10..12;
		// only the original ten slots use the shared node cache's fixed-size output array.
		constexpr int32 RockCachedSlotCount = 10;
		constexpr int32 RockSlotCount = 13;
		static_assert(RockCachedSlotCount * sizeof(TRefCountPtr<IPooledRenderTarget>)
					<= sizeof(FMixtormatNodeCacheEntry::Outputs),
			"Rock cache slots must fit the shared node cache entry");
		FRDGTextureRef Outputs[RockSlotCount] = {};
		// Produced once per layer: the ID phase may already have run it (see AddGeneratorFieldPasses).
		if (const TArray<FRDGTextureRef, TInlineAllocator<7>>* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Slot = 0; Slot < RockSlotCount; ++Slot)
			{
				Outputs[Slot] = (*Memo)[Slot];
			}
		}
		else
		{
			FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
			const uint64 NodeKey = NodeCache && Rock.FieldKey != 0
				? MixtormatComposeHash::Combine(Rock.FieldKey, Child.bGeneratorLayer ? 0x526F636B09ull : 0x526F636B08ull)
				: 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit =
				NodeKey != 0 ? NodeCache->Find(NodeKey, Size)
					: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			if (Hit.IsValid())
			{
				static const TCHAR* const Names[RockCachedSlotCount] = {
					TEXT("Mixtormat.Rock.Height"), TEXT("Mixtormat.Rock.Top"), TEXT("Mixtormat.Rock.Chamfer"),
					TEXT("Mixtormat.Rock.Wall"), TEXT("Mixtormat.Rock.EdgeDistance"), TEXT("Mixtormat.Rock.Ids"),
					TEXT("Mixtormat.Rock.BoundaryField"), TEXT("Mixtormat.Rock.TopRamp"),
					TEXT("Mixtormat.Rock.ChamferRamp"), TEXT("Mixtormat.Rock.WallRamp")};
				for (int32 Slot = 0; Slot < RockCachedSlotCount; ++Slot)
				{
					Outputs[Slot] = Hit->Outputs[Slot].IsValid()
						? GraphBuilder.RegisterExternalTexture(Hit->Outputs[Slot], Names[Slot])
						: nullptr;
				}
			}
			bool bComplete = true;
			for (int32 Slot = 0; Slot < RockCachedSlotCount; ++Slot)
			{
				bComplete &= Outputs[Slot] != nullptr;
			}
			if (!bComplete)
			{
				const auto Make = [&GraphBuilder, Size](const EPixelFormat Format, const TCHAR* Name)
				{
					return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
						Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
				};
				Outputs[0] = Make(PF_R32_FLOAT, TEXT("Mixtormat.Rock.Height"));
				Outputs[1] = Make(PF_R16F, TEXT("Mixtormat.Rock.Top"));
				Outputs[2] = Make(PF_R16F, TEXT("Mixtormat.Rock.Chamfer"));
				Outputs[3] = Make(PF_R16F, TEXT("Mixtormat.Rock.Wall"));
				Outputs[4] = Make(PF_R32_FLOAT, TEXT("Mixtormat.Rock.EdgeDistance"));
				Outputs[5] = Make(PF_R32_UINT, TEXT("Mixtormat.Rock.Ids"));
				Outputs[6] = Make(PF_G32R32F, TEXT("Mixtormat.Rock.BoundaryField"));
				Outputs[7] = Make(PF_R16F, TEXT("Mixtormat.Rock.TopRamp"));
				Outputs[8] = Make(PF_R16F, TEXT("Mixtormat.Rock.ChamferRamp"));
				Outputs[9] = Make(PF_R16F, TEXT("Mixtormat.Rock.WallRamp"));

				// Build: each cell's chunk geometry, once, into buffers the field reads.
				const uint32 CellCount = static_cast<uint32>(Layout.CellsU) * static_cast<uint32>(Layout.CellsV);
				const uint32 LeafSlots = CellCount * static_cast<uint32>(Layout.MaxLeaves);
				FRDGBufferRef LeafBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRockLeafStride), LeafSlots), TEXT("Mixtormat.Rock.Leaves"));
				FRDGBufferRef EdgeBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), LeafSlots * RockMaxVertices), TEXT("Mixtormat.Rock.Edges"));
				FRDGBufferRef EdgeTagBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LeafSlots * RockMaxVertices), TEXT("Mixtormat.Rock.EdgeTags"));
				FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), LeafSlots * RockMaxVertices), TEXT("Mixtormat.Rock.Vertices"));
				FRDGBufferRef CountBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CellCount), TEXT("Mixtormat.Rock.LeafCounts"));
				{
					FMixtormatRockFormationCS::FPermutationDomain BuildPermutation;
					BuildPermutation.Set<FMixtormatRockFormationCS::FStage>(0);
					TShaderMapRef<FMixtormatRockFormationCS> BuildShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), BuildPermutation);
					auto* B = GraphBuilder.AllocParameters<FMixtormatRockFormationCS::FParameters>();
					FillParameters(B);
					B->OutLeaves = GraphBuilder.CreateUAV(LeafBuffer);
					B->OutEdges = GraphBuilder.CreateUAV(EdgeBuffer);
					B->OutEdgeTags = GraphBuilder.CreateUAV(EdgeTagBuffer);
					B->OutVertices = GraphBuilder.CreateUAV(VertexBuffer);
					B->OutLeafCounts = GraphBuilder.CreateUAV(CountBuffer);
					ClearUnusedGraphResources(BuildShader, B);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.RockFormation.Build.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
						BuildShader, B, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(CellCount), 64), 1, 1));
				}

				FMixtormatRockFormationCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatRockFormationCS::FStage>(1);
				TShaderMapRef<FMixtormatRockFormationCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatRockFormationCS::FParameters>();
				FillParameters(P);
				P->Leaves = GraphBuilder.CreateSRV(LeafBuffer);
				P->Edges = GraphBuilder.CreateSRV(EdgeBuffer);
				P->EdgeTags = GraphBuilder.CreateSRV(EdgeTagBuffer);
				P->Vertices = GraphBuilder.CreateSRV(VertexBuffer);
				P->LeafCounts = GraphBuilder.CreateSRV(CountBuffer);
				P->OutRockHeight = GraphBuilder.CreateUAV(Outputs[0]);
				P->OutRockTop = GraphBuilder.CreateUAV(Outputs[1]);
				P->OutRockChamfer = GraphBuilder.CreateUAV(Outputs[2]);
				P->OutRockWall = GraphBuilder.CreateUAV(Outputs[3]);
				P->OutRockEdgeDistance = GraphBuilder.CreateUAV(Outputs[4]);
				P->OutRockIds = GraphBuilder.CreateUAV(Outputs[5]);
				P->OutRockBoundaryField = GraphBuilder.CreateUAV(Outputs[6]);
				P->OutRockTopRamp = GraphBuilder.CreateUAV(Outputs[7]);
				P->OutRockChamferRamp = GraphBuilder.CreateUAV(Outputs[8]);
				P->OutRockWallRamp = GraphBuilder.CreateUAV(Outputs[9]);
				// The field stage reads no texture; the combine-only bindings stay unset.
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.RockFormation.Field.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
					Shader, P, Groups);

				if (NodeKey != 0)
				{
					TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Entry =
						MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = NodeKey;
					Entry->Resolution = Size;
					for (int32 Slot = 0; Slot < RockCachedSlotCount; ++Slot)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Slot], &Entry->Outputs[Slot]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}

			// Derive scalar gates after either cache registration or field generation. A separate
			// dispatch can safely read neighbouring heights and keeps the ten-slot cache unchanged.
			{
				static const TCHAR* const Names[3] = {
					TEXT("Mixtormat.Rock.NormalizedHeightGate"), TEXT("Mixtormat.Rock.Slope"), TEXT("Mixtormat.Rock.Gap")};
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
				{
					Outputs[10 + Index] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
						Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Names[Index]);
				}
				FMixtormatRockFormationCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatRockFormationCS::FStage>(3);
				TShaderMapRef<FMixtormatRockFormationCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatRockFormationCS::FParameters>();
				FillParameters(P);
				P->RockHeight = Outputs[0];
				P->RockIds = Outputs[5];
				P->RockBoundaryField = Outputs[6];
				P->OutRockNormalizedHeight = GraphBuilder.CreateUAV(Outputs[10]);
				P->OutRockSlope = GraphBuilder.CreateUAV(Outputs[11]);
				P->OutRockGap = GraphBuilder.CreateUAV(Outputs[12]);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.RockFormation.Outputs.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
					Shader, P, Groups);
			}

			// Reusable outputs retain the existing masks, ramps and signed distance. The three
			// normalised gates share their original (pre-flow) geometry frame. Slot 6 is not published.
			if (!Child.bGeneratorLayer)
			{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Outputs[5]);
			static const TCHAR* const MaskNames[10] = {
				TEXT("RockTop"), TEXT("RockChamfer"), TEXT("RockWall"), TEXT("RockEdgeDistance"),
				TEXT("RockTopRamp"), TEXT("RockChamferRamp"), TEXT("RockWallRamp"),
				TEXT("RockHeight"), TEXT("RockSlope"), TEXT("RockGap")};
			static const int32 MaskSlots[10] = {1, 2, 3, 4, 7, 8, 9, 10, 11, 12};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(MaskNames); ++Index)
			{
				const FName OutputName(MaskNames[Index]);
				const FRDGTextureRef Output = Outputs[MaskSlots[Index]];
				Ctx.PublishedMaskOutputs.Add(
					FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, OutputName}, Output);
				// The edge distance is signed and in UV widths, so it gets its own preview kind --
				// the capability row names the same kind, or the eye never matches.
				const bool bSignedDistance = MaskSlots[Index] == 4;
				if (IsChildOutputPreviewTarget(Request,
					bSignedDistance ? EMixtormatPreviewOutputKind::SignedDistance : EMixtormatPreviewOutputKind::Mask,
					OutputName, LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					if (bSignedDistance)
					{
						AddDebugPreviewSignedDistanceBlitPass(GraphBuilder, Output, (float)Size.X,
							Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
					}
					else
					{
						AddDebugPreviewMaskBlitPass(GraphBuilder, Output,
							Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
					}
				}
			}
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
				LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(GraphBuilder, Outputs[5], nullptr,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}

			}
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, RockSlotCount);
		}
		// Independent normalised gate, including field-only evaluation; never the mode-selected
		// or flow-modified layer height and never affected by HeightScale.
		if (!Child.bGeneratorLayer) { LayerCtx.ResolvedRockHeights.Add(Child.SourceChildIndex, Outputs[10]); }
		if (Bundle)
		{
			Bundle->Coverage = AddBundleCoverage(Ctx, 2, nullptr, Outputs[6]);
			Bundle->RegionIds = Outputs[5];
			Bundle->BoundaryField = Outputs[6];
			static const TCHAR* const Names[10] = {
				TEXT("RockTop"), TEXT("RockChamfer"), TEXT("RockWall"), TEXT("RockEdgeDistance"),
				TEXT("RockTopRamp"), TEXT("RockChamferRamp"), TEXT("RockWallRamp"),
				TEXT("RockHeight"), TEXT("RockSlope"), TEXT("RockGap")};
			static const int32 Slots[10] = {1, 2, 3, 4, 7, 8, 9, 10, 11, 12};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
			{
				Bundle->NamedMasks.Add(FName(Names[Index]), Outputs[Slots[Index]]);
			}
		}
		if (bFieldOnly)
		{
			return nullptr;
		}

		const auto Combine = [&](FRDGTextureRef Field, const EMixtormatRockHeightMode HeightMode,
			const float HeightScale, const TCHAR* Name)
		{
			FRDGTextureRef Combined = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
								Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
			FMixtormatRockFormationCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatRockFormationCS::FStage>(2);
			TShaderMapRef<FMixtormatRockFormationCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatRockFormationCS::FParameters>();
			FillParameters(P);
			P->HeightMode = static_cast<uint32>(HeightMode);
			P->HeightScale = HeightScale;
			P->RockHeight = Field;
			P->RockIds = Outputs[5];
			P->OutHeight = GraphBuilder.CreateUAV(Combined);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("%s.L%d.C%d", Name, LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
			return Combined;
		};

		// Resolve the mode after cache extraction; Measured never mutates the cached raw field.
		FRDGTextureRef RockField = Outputs[0];
		if (Rock.HeightMode == EMixtormatRockHeightMode::Measured)
		{
			RockField = AddNormalizeFieldPasses(GraphBuilder, RockField, Size,
				0.0f, 1.0f, TEXT("Mixtormat.Rock.NormalizedHeight"));
		}
		const bool bHasFlowTools = !Child.bGeneratorLayer
						&& HasActiveFlowTools(Ctx, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex);
		if (bHasFlowTools)
		{
			// Resolve floors and analytic bounds before flow moves pixels: the cached IDs are in
			// the original frame. The final combine must not remap or reset those moved pixels.
			if (Rock.HeightMode != EMixtormatRockHeightMode::Raw)
			{
				RockField = Combine(RockField, Rock.HeightMode, 1.0f, TEXT("Mixtormat.Rock.ResolveHeightMode"));
			}
			FRDGTextureRef NoCoverage = nullptr;
			RockField = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, Outputs[6], RockField, NoCoverage);
		}
		FRDGTextureRef Height = Combine(RockField, bHasFlowTools ? EMixtormatRockHeightMode::Raw : Rock.HeightMode,
			Rock.HeightScale, TEXT("Mixtormat.Rock.LayerHeight"));
		if (Bundle) { Bundle->Height = Height; }
		return Height;
	}

	FRDGTextureRef AddCracksPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef SourceHeight,
		const bool bFieldOnly = false,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FCracksRenderData& Cracks = Child.Generator.Cracks;
		const FIntPoint Size = Request.Resolution;
		FIntPoint SolveSize = DistanceSolveResolution(Size);
		while (FMath::Max(SolveSize.X, SolveSize.Y) > 256)
		{
			SolveSize.X = FMath::Max(SolveSize.X / 2, 1);
			SolveSize.Y = FMath::Max(SolveSize.Y / 2, 1);
		}
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const FIntVector SolveGroups(FMath::DivideAndRoundUp(SolveSize.X, 8), FMath::DivideAndRoundUp(SolveSize.Y, 8), 1);
		const auto Fill = [&Cracks, &Layer, &Child, Size, SolveSize](FMixtormatCracksCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.bGeneratorLayer);
			P->OutputSize = Size;
			P->SolveSize = SolveSize;
			P->Seed = Cracks.Seed;
			P->Cells = Cracks.Cells;
			P->Jitter = Cracks.Jitter;
			P->Width = Cracks.Width;
			P->Depth = Cracks.Depth;
			P->Rough = Cracks.Rough;
			P->Scale = Cracks.Scale;
			P->Detail = Cracks.Detail;
			P->Feather = Cracks.Feather;
			P->WidthVariation = Cracks.WidthVariation;
			P->WidthScale = Cracks.WidthScale;
			P->LineVariation = Cracks.LineVariation;
			P->RegionVariation = Cracks.RegionVariation;
			P->Chip = Cracks.Chip;
			P->ChipSize = Cracks.ChipSize;
			P->Gap = Cracks.Gap;
			P->GapWidth = Cracks.GapWidth;
			P->Slip = Cracks.Slip;
			P->Tilt = Cracks.Tilt;
			P->ChamferAmount = Cracks.ChamferAmount;
			P->ChamferStart = Cracks.ChamferStart;
			P->ChamferEnd = Cracks.ChamferEnd;
			P->ChamferLow = Cracks.ChamferLow;
			P->ChamferHigh = Cracks.ChamferHigh;
			P->ChamferNoise = Cracks.ChamferNoise;
			P->ChamferNoiseScale = Cracks.ChamferNoiseScale;
			P->HeightScale = Cracks.HeightScale;
		};
		const auto Make = [&GraphBuilder, Size](EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		// Field key deliberately omits chamfer, blend and flow settings.
		constexpr int32 SlotCount = 6;
		FRDGTextureRef Outputs[SlotCount] = {};
		if (const TArray<FRDGTextureRef, TInlineAllocator<7>>* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				Outputs[Slot] = (*Memo)[Slot];
			}
		}
		else
		{
			FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
			const uint64 NodeKey = NodeCache && Cracks.FieldKey != 0
				? MixtormatComposeHash::Combine(Cracks.FieldKey, Child.bGeneratorLayer ? 0x437261636B74ull : 0x437261636B73ull) : 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit = NodeKey != 0
				? NodeCache->Find(NodeKey, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			static const TCHAR* const Names[SlotCount] = {
				TEXT("Mixtormat.Cracks.Height"), TEXT("Mixtormat.Cracks.Mask"),
				TEXT("Mixtormat.Cracks.Distance"), TEXT("Mixtormat.Cracks.Ids"),
				TEXT("Mixtormat.Cracks.Random"), TEXT("Mixtormat.Cracks.Boundary")};
			bool bComplete = Hit.IsValid();
			for (int32 Slot = 0; bComplete && Slot < SlotCount; ++Slot)
			{
				bComplete = Hit->Outputs[Slot].IsValid();
			}
			if (bComplete)
			{
				for (int32 Slot = 0; Slot < SlotCount; ++Slot)
				{
					Outputs[Slot] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[Slot], Names[Slot]);
				}
			}
			else
			{
				Outputs[0] = Make(PF_R32_FLOAT, Names[0]);
				Outputs[1] = Make(PF_R16F, Names[1]);
				Outputs[2] = Make(PF_R32_FLOAT, Names[2]);
				Outputs[3] = Make(PF_R32_UINT, Names[3]);
				Outputs[4] = Make(PF_R16F, Names[4]);
				Outputs[5] = Make(PF_G32R32F, Names[5]);
				FMixtormatCracksCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatCracksCS::FStage>(0);
				TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
				Fill(P);
				P->OutCrackHeight = GraphBuilder.CreateUAV(Outputs[0]);
				P->OutCrackMask = GraphBuilder.CreateUAV(Outputs[1]);
				P->OutCrackDistance = GraphBuilder.CreateUAV(Outputs[2]);
				P->OutCrackIds = GraphBuilder.CreateUAV(Outputs[3]);
				P->OutCrackRandom = GraphBuilder.CreateUAV(Outputs[4]);
				P->OutCrackBoundary = GraphBuilder.CreateUAV(Outputs[5]);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Cracks.Field.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
					Shader, P, Groups);
				if (NodeKey != 0)
				{
					TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Entry =
						MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = NodeKey;
					Entry->Resolution = Size;
					for (int32 Slot = 0; Slot < SlotCount; ++Slot)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Slot], &Entry->Outputs[Slot]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}
			if (!Child.bGeneratorLayer)
			{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Outputs[3]);
			static const TCHAR* const MaskNames[3] = { TEXT("CrackMask"), TEXT("CrackDistance"), TEXT("PieceRandom") };
			const int32 MaskSlots[3] = { 1, 2, 4 };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FName OutputName(MaskNames[Index]);
				Ctx.PublishedMaskOutputs.Add(
					FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, OutputName}, Outputs[MaskSlots[Index]]);
				if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, OutputName,
					LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					AddDebugPreviewMaskBlitPass(GraphBuilder, Outputs[MaskSlots[Index]],
						Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
				}
			}
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
				LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(GraphBuilder, Outputs[3], nullptr,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}
			}
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Coverage = AddBundleCoverage(Ctx, 1, Outputs[1]);
			Bundle->RegionIds = Outputs[3];
			Bundle->BoundaryField = Outputs[5];
			Bundle->NamedMasks.Add(FName(TEXT("CrackMask")), Outputs[1]);
			Bundle->NamedMasks.Add(FName(TEXT("CrackDistance")), Outputs[2]);
			Bundle->NamedMasks.Add(FName(TEXT("PieceRandom")), Outputs[4]);
		}
		if (bFieldOnly)
		{
			return nullptr;
		}

		FRDGTextureRef Field = Outputs[0];
		const bool bHasGate = !Child.bGeneratorLayer && HasScopedMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef Gate = bHasGate
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true) : Outputs[1];
		FRDGTextureRef ChamferCut = Make(PF_R16F, TEXT("Mixtormat.Cracks.ChamferCut"));
		FRDGTextureRef Chamfered = Make(PF_R32_FLOAT, TEXT("Mixtormat.Cracks.Chamfered"));
		// Arrival is computed only after the cached field; its noise and gate never invalidate it.
		FRDGTextureRef Arrival[2];
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Arrival[Index] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				SolveSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Cracks.Arrival"));
		}
		{
			FMixtormatCracksCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatCracksCS::FStage>(1);
			TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
			Fill(P);
			P->CrackDistance = Outputs[2];
			P->OutArrival = GraphBuilder.CreateUAV(Arrival[0]);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Cracks.ArrivalSeed.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, SolveGroups);
		}
		// Eight-neighbour monotone relaxation with wrapped neighbours. Each pass crosses one
		// solve texel; the last pass cannot exceed the tile diameter.
		int32 Read = 0;
		const int32 Iterations = Cracks.ChamferAmount != 0.0f
			? FMath::Min(FMath::Max(SolveSize.X, SolveSize.Y), 128) : 0;
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			FMixtormatCracksCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatCracksCS::FStage>(2);
			TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
			Fill(P);
			P->Arrival = Arrival[Read];
			P->OutArrival = GraphBuilder.CreateUAV(Arrival[1 - Read]);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Cracks.Arrival.%d.L%d.C%d", Iteration, LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, SolveGroups);
			Read = 1 - Read;
		}
		{
			FMixtormatCracksCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatCracksCS::FStage>(3);
			TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
			Fill(P);
			P->HasGate = bHasGate ? 1u : 0u;
			P->CrackHeight = Field;
			P->CrackDistance = Outputs[2];
			P->Arrival = Arrival[Read];
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->GateMask = Gate;
			P->OutHeight = GraphBuilder.CreateUAV(Chamfered);
			P->OutChamferCut = GraphBuilder.CreateUAV(ChamferCut);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Cracks.Chamfer.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
		}
		Field = Chamfered;
		const FName CutName(TEXT("ChamferCut"));
		if (Bundle) { Bundle->NamedMasks.Add(CutName, ChamferCut); }
		if (!Child.bGeneratorLayer)
		{
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, CutName}, ChamferCut);
		if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, CutName,
			LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, ChamferCut,
				Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
		}
		if (HasActiveFlowTools(Ctx, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
		{
			FRDGTextureRef NoCoverage = nullptr;
			Field = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, Outputs[5], Field, NoCoverage);
		}
		}
		FRDGTextureRef Combined = Make(PF_R32_FLOAT, TEXT("Mixtormat.Cracks.LayerHeight"));
		FMixtormatCracksCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatCracksCS::FStage>(4);
		TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
		Fill(P);
		P->CrackHeight = Field;
		P->OutHeight = GraphBuilder.CreateUAV(Combined);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Cracks.Combine.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, P, Groups);
		if (Bundle) { Bundle->Height = Combined; }
		return Combined;
	}

	// One Pebbles child. Same shape as Rock Formation: the scatter field is cached against its
	// settings and resolution, and Height Scale turns it into the layer's height without re-running it.
	FRDGTextureRef AddPebblesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef SourceHeight,
		const bool bFieldOnly = false,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FPebblesRenderData& Pebbles = Child.Generator.Pebbles;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const auto FillParameters = [&Pebbles, &Layer, &Child, Size](FMixtormatPebblesCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.bGeneratorLayer);
			P->OutputSize = Size;
			P->Seed = Pebbles.Seed;
			P->Cells = Pebbles.Cells;
			P->Density = Pebbles.Density;
			P->Jitter = Pebbles.Jitter;
			P->StoneScale = Pebbles.Scale;
			P->ScaleVariation = Pebbles.ScaleVariation;
			P->RotationDegrees = Pebbles.Rotation;
			P->Cuts = Pebbles.Cuts;
			P->DirectionMode = Pebbles.Direction;
			P->Irregularity = Pebbles.Irregularity;
			P->Chamfer = Pebbles.Chamfer;
			P->Steepness = Pebbles.Steepness;
			P->SteepnessVariation = Pebbles.SteepnessVariation;
			P->BiasVariation = Pebbles.BiasVariation;
			P->HeightGain = Pebbles.HeightGain;
			P->HeightVariation = Pebbles.HeightVariation;
			P->FacetIds = Pebbles.bFacetIds ? 1u : 0u;
			P->HeightScale = Pebbles.HeightScale;
		};

		// Node-cache slots: height, coverage, edge distance, random, IDs.
		constexpr int32 SlotCount = 5;
		FRDGTextureRef Outputs[SlotCount] = {};
		// Produced once per layer: the ID phase may already have run it (see AddGeneratorFieldPasses).
		if (const TArray<FRDGTextureRef, TInlineAllocator<7>>* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				Outputs[Slot] = (*Memo)[Slot];
			}
		}
		else
		{
			FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
			const uint64 NodeKey = NodeCache && Pebbles.FieldKey != 0
				? MixtormatComposeHash::Combine(Pebbles.FieldKey, Child.bGeneratorLayer ? 0x506562626C66ull : 0x506562626C65ull)
				: 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit =
				NodeKey != 0 ? NodeCache->Find(NodeKey, Size)
					: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			static const TCHAR* const Names[SlotCount] = {
				TEXT("Mixtormat.Pebbles.Height"), TEXT("Mixtormat.Pebbles.Coverage"),
				TEXT("Mixtormat.Pebbles.EdgeDistance"), TEXT("Mixtormat.Pebbles.Random"),
				TEXT("Mixtormat.Pebbles.Ids")};
			bool bComplete = Hit.IsValid();
			for (int32 Slot = 0; bComplete && Slot < SlotCount; ++Slot)
			{
				bComplete = Hit->Outputs[Slot].IsValid();
			}
			if (bComplete)
			{
				for (int32 Slot = 0; Slot < SlotCount; ++Slot)
				{
					Outputs[Slot] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[Slot], Names[Slot]);
				}
			}
			else
			{
				const auto Make = [&GraphBuilder, Size](const EPixelFormat Format, const TCHAR* Name)
				{
					return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
						Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
				};
				Outputs[0] = Make(PF_R32_FLOAT, Names[0]);
				Outputs[1] = Make(PF_R16F, Names[1]);
				Outputs[2] = Make(PF_R32_FLOAT, Names[2]);
				Outputs[3] = Make(PF_R16F, Names[3]);
				Outputs[4] = Make(PF_R32_UINT, Names[4]);

				FMixtormatPebblesCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatPebblesCS::FStage>(0);
				TShaderMapRef<FMixtormatPebblesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatPebblesCS::FParameters>();
				FillParameters(P);
				P->OutPebbleHeight = GraphBuilder.CreateUAV(Outputs[0]);
				P->OutPebbleCoverage = GraphBuilder.CreateUAV(Outputs[1]);
				P->OutPebbleEdgeDistance = GraphBuilder.CreateUAV(Outputs[2]);
				P->OutPebbleRandom = GraphBuilder.CreateUAV(Outputs[3]);
				P->OutPebbleIds = GraphBuilder.CreateUAV(Outputs[4]);
				ClearUnusedGraphResources(Shader, P);
				FComputeShaderUtils::AddPass(GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Pebbles.Field.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
					Shader, P, Groups);

				if (NodeKey != 0)
				{
					TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Entry =
						MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = NodeKey;
					Entry->Resolution = Size;
					for (int32 Slot = 0; Slot < SlotCount; ++Slot)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Slot], &Entry->Outputs[Slot]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}

			// Stone (or facet) IDs for the ID consumers below this row; masks for Copy Output.
			if (!Child.bGeneratorLayer)
			{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Outputs[4]);
			static const TCHAR* const MaskNames[3] = {
				TEXT("PebbleCoverage"), TEXT("PebbleEdgeDistance"), TEXT("PebbleRandom")};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FName OutputName(MaskNames[Index]);
				Ctx.PublishedMaskOutputs.Add(
					FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, OutputName}, Outputs[1 + Index]);
				// The edge distance is signed and in UV widths, so it gets its own preview kind --
				// the capability row names the same kind, or the eye never matches.
				const bool bSignedDistance = Index == 1;
				if (IsChildOutputPreviewTarget(Request,
					bSignedDistance ? EMixtormatPreviewOutputKind::SignedDistance : EMixtormatPreviewOutputKind::Mask,
					OutputName, LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					if (bSignedDistance)
					{
						AddDebugPreviewSignedDistanceBlitPass(GraphBuilder, Outputs[1 + Index], (float)Size.X,
							Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
					}
					else
					{
						AddDebugPreviewMaskBlitPass(GraphBuilder, Outputs[1 + Index],
							Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
					}
				}
			}
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
				LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(GraphBuilder, Outputs[4], nullptr,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}

			}
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Coverage = Outputs[1];
			Bundle->RegionIds = Outputs[4];
			Bundle->BoundaryField = PackScalarBoundary(Ctx, Outputs[2]);
			Bundle->NamedMasks.Add(FName(TEXT("PebbleCoverage")), Outputs[1]);
			Bundle->NamedMasks.Add(FName(TEXT("PebbleEdgeDistance")), Outputs[2]);
			Bundle->NamedMasks.Add(FName(TEXT("PebbleRandom")), Outputs[3]);
		}
		if (bFieldOnly)
		{
			return nullptr;
		}

		// Scoped flow tools, as under Rock Formation: height and coverage move together.
		FRDGTextureRef PebbleField = Outputs[0];
		FRDGTextureRef PebbleCoverage = Outputs[1];
		if (!Child.bGeneratorLayer && HasActiveFlowTools(Ctx, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
		{
			PebbleField = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex,
				PackScalarBoundary(Ctx, Outputs[2]), Outputs[0], PebbleCoverage);
		}

		FRDGTextureRef Combined = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
					Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.Pebbles.LayerHeight"));
		{
			FMixtormatPebblesCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatPebblesCS::FStage>(1);
			TShaderMapRef<FMixtormatPebblesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatPebblesCS::FParameters>();
			FillParameters(P);
			P->PebbleHeight = PebbleField;
			P->OutHeight = GraphBuilder.CreateUAV(Combined);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Pebbles.Combine.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
		}
		if (Bundle) { Bundle->Height = Combined; }
		return Combined;
	}


}

void AddOutputReferencePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
{
	if (!Layer.bEnabled) { return; }
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::OutputReference) { continue; }
		const FOutputReferenceRenderData& Reference = Child.OutputReference;
		const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Reference.Source);
		if (!Source || Source->Kind != Reference.Kind || !Source->IsComplete()) { continue; }
		// Copy the bundle before Add can reallocate the registry. No producer is reevaluated.
		const FPublishedField Field = *Source;
		Ctx.PublishedFieldOutputs.Add(
			FPublishedFieldKey{Layer.LayerId, Child.SourceChildIndex, Reference.Source.Output}, Field);
		if (Reference.Kind == EMixtormatPublishedFieldKind::RegionIds)
		{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Field.Texture);
			continue;
		}
		if (Reference.Kind == EMixtormatPublishedFieldKind::UVMap)
		{
			LayerCtx.ReferencedUV = Field.Texture;
			continue;
		}
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Coordinates = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.OutputReference.FlowUV"));
		FMixtormatGeneratorFlowCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorFlowCS::FStage>(8);
		TShaderMapRef<FMixtormatGeneratorFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
		P->OutputSize = Size;
		P->TraceLength = Reference.FlowTraceLength;
		P->WarpStrength = Reference.FlowAmount;
		P->Steps = Reference.FlowSteps;
		P->FlowField = Field.Texture;
		P->FlowSmooth = Field.FlowSmooth;
		P->FlowValidity = Field.Validity;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		P->OutWarpedUV = GraphBuilder.CreateUAV(Coordinates);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.OutputReference.Flow.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		LayerCtx.ReferencedUV = Coordinates;
		Ctx.PublishedFieldOutputs.Add(
			FPublishedFieldKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("WarpedUV"))},
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Coordinates, nullptr, nullptr, false});
	}
}

void AddGeneratorLayerPasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
{
	if (!Layer.bEnabled || !Layer.bGenerator) { return; }
	FGeneratorBundle& Bundle = LayerCtx.GeneratorBundle;
	Bundle = FGeneratorBundle();
	const FGeneratorPassInput Input{Layer.Generator, INDEX_NONE, true};
	switch (Layer.Generator.Type)
	{
	case EMixtormatGeneratorType::StrataCarver:
		// Height Follow reads the composite below, never this layer's own height or IDs.
		AddStrataCarverPasses(Ctx, LayerCtx, Layer, Input,
			Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)], &Bundle);
		break;
	case EMixtormatGeneratorType::RockFormation:
		AddRockFormationPasses(Ctx, LayerCtx, Layer, Input, nullptr, false, &Bundle);
		break;
	case EMixtormatGeneratorType::Pebbles:
		AddPebblesPasses(Ctx, LayerCtx, Layer, Input, nullptr, false, &Bundle);
		break;
	case EMixtormatGeneratorType::Cracks:
		AddCracksPasses(Ctx, LayerCtx, Layer, Input, nullptr, false, &Bundle);
		break;
	}
	if (!Bundle.Height || !Bundle.Coverage) { return; }
	if (HasActiveFlowTools(Ctx, LayerCtx.LayerIndex, Layer, INDEX_NONE))
	{
		Bundle.Height = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, INDEX_NONE,
			Bundle.BoundaryField, Bundle.Height, Bundle.Coverage, &Bundle);
	}

	// This is the only layer-generator publication point. Consumers in the local ID phase
	// see post-flow geometry; cross-layer ID selection belongs to Step 3b.
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
	if (Bundle.RegionIds)
	{
		PublishRegionIds(LayerCtx.RegionIdMaps, INDEX_NONE, Bundle.RegionIds);
		if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::RegionIds,
			NAME_None, LayerCtx.LayerIndex, INDEX_NONE))
		{
			AddDebugPreviewRegionIdsBlitPass(Ctx.GraphBuilder, Bundle.RegionIds, nullptr, Debug, Size);
		}
	}
	for (const TPair<FName, FRDGTextureRef>& Mask : Bundle.NamedMasks)
	{
		Ctx.PublishedMaskOutputs.Add(FPublishedMaskKey{Layer.LayerId, INDEX_NONE, Mask.Key}, Mask.Value);
		const bool bDistance = Mask.Key == FName(TEXT("RockEdgeDistance"))
			|| Mask.Key == FName(TEXT("PebbleEdgeDistance"));
		if (IsChildOutputPreviewTarget(Ctx.Request,
			bDistance ? EMixtormatPreviewOutputKind::SignedDistance : EMixtormatPreviewOutputKind::Mask,
			Mask.Key, LayerCtx.LayerIndex, INDEX_NONE))
		{
			if (bDistance)
			{
				AddDebugPreviewSignedDistanceBlitPass(Ctx.GraphBuilder, Mask.Value, (float)Size.X, Debug, Size);
			}
			else
			{
				AddDebugPreviewMaskBlitPass(Ctx.GraphBuilder, Mask.Value, Debug, Size);
			}
		}
	}
}

void AddGeneratorFieldPasses(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer)
{
	// Generators whose field depends only on their own settings publish their Region IDs here,
	// in the ID phase, so UV From IDs -- which resolves before the layer's source is read --
	// can key off them. Their height is still mixed in later by AddGeneratorPasses.
	if (!Layer.bEnabled)
	{
		return;
	}
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::Generator)
		{
			continue;
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::RockFormation)
		{
			AddRockFormationPasses(Ctx, LayerCtx, Layer,
							FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false}, nullptr, true);
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::Pebbles)
		{
			AddPebblesPasses(Ctx, LayerCtx, Layer,
							FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false}, nullptr, true);
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::Cracks)
		{
			AddCracksPasses(Ctx, LayerCtx, Layer,
							FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false}, nullptr, true);
		}
	}
}

void AddGeneratorPasses(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer)
{
	const bool bHasGenerator = Layer.Children.ContainsByPredicate(
		[](const FChildRenderData& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Generator
							&& (Child.Generator.Type == EMixtormatGeneratorType::StrataCarver
								|| Child.Generator.Type == EMixtormatGeneratorType::RockFormation
								|| Child.Generator.Type == EMixtormatGeneratorType::Pebbles
								|| Child.Generator.Type == EMixtormatGeneratorType::Cracks);
		});
	if (!bHasGenerator)
	{
		return;
	}

	// The layer's own maps, resolved into output space. Idempotent -- if a height smooth or a
	// flow warp already asked for this pass it costs nothing, and going through it is what makes
	// the layer's UV transform and pattern UV basis apply exactly once, to the surface the
	// generator is about to carve rather than twice to the carve itself.
	AddLayerInputPass(Ctx, LayerCtx, Layer);

	// The height as it stood before any generator ran. Kept for the normal pass at the end: the
	// relief normal is one delta across the whole chain rather than one per generator, which is
	// both cheaper and more correct -- two carves of the same groove compose into one slope, and
	// RNM-combining each of them separately would tilt the normal twice for a surface that only
	// moved once.
	FRDGTextureRef SourceHeight = LayerCtx.LayerInputHeight;

	// Authored order. A generator's output is the layer's height, so each one replaces what the
	// one before it produced; how the result meets the stack below is the layer's Height Op. The
	// chain is still carried in LayerCtx.LayerInputHeight as well as in the local, so what a later
	// generator reads -- Strata's Height Follow -- is the previous generator's height.
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::Generator)
		{
			continue;
		}
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			LayerCtx.LayerInputHeight = AddStrataCarverPasses(
				Ctx, LayerCtx, Layer, FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false},
				LayerCtx.LayerInputHeight);
			break;
		case EMixtormatGeneratorType::Cracks:
			LayerCtx.LayerInputHeight = AddCracksPasses(
				Ctx, LayerCtx, Layer, FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false},
				LayerCtx.LayerInputHeight);
			break;
		case EMixtormatGeneratorType::RockFormation:
			LayerCtx.LayerInputHeight = AddRockFormationPasses(
				Ctx, LayerCtx, Layer, FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false},
				LayerCtx.LayerInputHeight);
			break;
		case EMixtormatGeneratorType::Pebbles:
			LayerCtx.LayerInputHeight = AddPebblesPasses(
				Ctx, LayerCtx, Layer, FGeneratorPassInput{Child.Generator, Child.SourceChildIndex, false},
				LayerCtx.LayerInputHeight);
			break;
		}
	}

	FRDGTextureRef CarvedHeight = LayerCtx.LayerInputHeight;
	LayerCtx.bGeneratedHeight = CarvedHeight != SourceHeight;
	if (CarvedHeight == SourceHeight)
	{
		// Every generator on this layer was neutral. Nothing carved, so nothing to re-derive --
		// and skipping is not merely an optimisation: running the delta pass over an unchanged
		// height would still rewrite LayerInputN through the RNM combine, which is not the
		// identity in the last bits.
		return;
	}

	// The normal, through the shared convention and nothing else.
	//
	// AddHeightDerivedNormalPass is the one height->normal in the plugin -- erosion, craquelure
	// relief, region relief and worn edges all go through it -- and it takes the *delta* between
	// the height before and after, derives a relief normal from it and RNM-combines that onto the
	// normal that was already there. A private Sobel here with its own strength would be a fourth
	// convention, and the header at MixtormatHeightNormal.ush documents what three of those cost
	// the last time: the same height field differentiated in worlds three orders of magnitude
	// apart.
	//
	// HeightDerivedNormalStrength, which is what every other caller of this pass uses --
	// erosion, craquelure relief and region relief all pass it, and passing anything else here
	// would be the private convention this node is not allowed to have. It is not the neutral
	// gain: MixtormatHeightDeltaNormal carries its own Sobel rather than going through
	// MixtormatHeightNormal.ush, and its /8 is cancelled by this 8 to land on the pixel-space
	// gradient the header names as this pass's convention. ReliefNormalStrength is the neutral
	// gain for the *other* path -- the shaders that include MixtormatHeightNormal.ush, where the
	// pixel-to-slope conversion has already happened -- and using it here would make the same
	// carve depth read eight times shallower than an erosion of the same depth.
	//
	// A second target rather than writing LayerInputN in place -- RDG will not let one pass read
	// and write the same resource, and the composite reads whichever one this leaves behind.
	FRDGTextureRef FormedNormal = Ctx.GraphBuilder.CreateTexture(
		LayerCtx.LayerInputN->Desc, TEXT("Mixtormat.FormationNormal"));

	AddHeightDerivedNormalPass(
		Ctx,
		SourceHeight,
		CarvedHeight,
		LayerCtx.LayerInputN,
		LayerCtx.LayerInputRAM,
		FormedNormal,
		nullptr,
		Ctx.Request.Resolution,
		HeightDerivedNormalStrength,
		false,
		TEXT("FormationRecipe"));
	LayerCtx.LayerInputN = FormedNormal;
}

}
