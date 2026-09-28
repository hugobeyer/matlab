// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "Compositing/MixtormatComposeHash.h"

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
	// The grid the distance solve runs on.
	//
	// A propagated distance is a large-scale quantity: the field it produces is smooth at the
	// scale of the cells that seeded it, and solving it at 4K would cost sixteen times the work
	// of solving it at 1K for a result that is visually identical after the remap. Sixteen taps
	// per pixel per iteration at 128 iterations is 34 billion texture loads at 4K and 537 million
	// at 512 -- the difference between a composite that takes minutes and one that takes a
	// fraction of a second.
	//
	// The cap is on the grid, not on a divisor, so the same material solves at the same physical
	// scale at every export resolution. 512 and 1024 and 4096 all solve at 512 and produce the
	// same strata; only the resolve is finer. That is the property an artist actually needs --
	// a preview at 1K that predicts the 4K bake.
	//
	// What the coarse grid costs is edge crispness in the bands, and the bands are analytic:
	// ResolveCS re-evaluates the same strata phase at full resolution and folds it back in, so
	// the detail that matters comes back without the solve paying for it. The same trade the
	// peel front and the wet stain already make.
	constexpr int32 StrataSolveMaxSize = 1024;

	// Divisor rather than a straight clamp, so a non-square tile keeps its aspect and the wrap
	// stays exact on both axes. Rounded up to a power of two because every composition
	// resolution in this plugin is one, and a non-power-of-two divisor would put the solve grid
	// off the texel lattice the mask and ID maps are sampled on.
	FIntPoint StrataSolveResolution(const FIntPoint Resolution)
	{
		const int32 Longest = FMath::Max(Resolution.X, Resolution.Y);
		int32 Divisor = 1;
		while (Longest / Divisor > StrataSolveMaxSize)
		{
			Divisor *= 2;
		}
		return FIntPoint(
			FMath::Max(Resolution.X / Divisor, 1),
			FMath::Max(Resolution.Y / Divisor, 1));
	}

	// The jump schedule.
	//
	// Strides halve from JumpStart to 1 and then the schedule restarts, so a long iteration
	// budget alternates reach and relaxation instead of spending everything after the fifth
	// iteration crawling one texel at a time. The wide passes carry a front across the tile; the
	// stride-1 passes are where the recursive strata push accumulates and the bands form.
	//
	// The schedule is a function of JumpStart alone. Nothing here is keyed to the iteration
	// count, which is what lets Iterations be 1 or 64 or 128 without a special case: 1 runs the
	// widest jump only, 64 runs the cycle a dozen times, and neither is a different algorithm.
	int32 StrataJumpStride(const int32 Iteration, const int32 JumpStart)
	{
		const int32 Start = FMath::Max(JumpStart, 1);
		int32 Halvings = 0;
		while ((Start >> Halvings) > 1)
		{
			++Halvings;
		}
		const int32 CycleLength = Halvings + 1;
		return FMath::Max(Start >> (Iteration % CycleLength), 1);
	}

	// Which Worley family and which combining operation this iteration runs.
	//
	// Drawn from OperationSeed and the iteration index on the CPU, so the inner loop of the
	// propagation kernel carries no hashing at all. Two different hashes rather than two fields
	// of one, so family and operation do not march in lockstep -- with a single draw, iteration
	// 3 would always be "Chebyshev and smooth" for every seed, and the cycling would read as a
	// fixed five-step pattern rather than as variety.
	uint32 StrataIterationHash(const uint32 OperationSeed, const int32 Iteration, const uint32 Salt)
	{
		uint32 Value = OperationSeed * 0x9e3779b9u + static_cast<uint32>(Iteration) * 0x85ebca6bu + Salt;
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	constexpr int32 StrataFamilyCount = 5;
	constexpr int32 StrataOperationCount = 4;

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

// Box-reduces the scoped mask from composition resolution to the solve grid. See the note in
// MixtormatStrataCarver.usf: a bilinear fetch at an 8:1 ratio reads one texel in sixty-four, so
// a thin mask aliases or vanishes; an average over the footprint turns it into the coverage the
// solve actually wants.
class FMixtormatStrataCarverMaskReduceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverMaskReduceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverMaskReduceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, MaskFootprint)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FullResMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSolveMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverMaskReduceCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"MaskReduceCS",
	SF_Compute);

// Builds the Worley family fractals, the propagation cost and the initial distance state.
class FMixtormatStrataCarverSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, SeedThreshold)
		SHADER_PARAMETER(int32, WorleyCells)
		SHADER_PARAMETER(int32, SeedDetail)
		SHADER_PARAMETER(float, WorleyJitter)
		SHADER_PARAMETER(float, BandFrequency)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, CostAmount)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SeedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SeedRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFamilies)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutAux)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutDist)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"SeedCS",
	SF_Compute);

// One propagation step, ping-ponged.
class FMixtormatStrataCarverPropagateCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverPropagateCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverPropagateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, BandFrequency)
		SHADER_PARAMETER(float, StrataFrequency)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, StrataWarp)
		SHADER_PARAMETER(float, StepScale)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, PushAmount)
		SHADER_PARAMETER(float, PushDecay)
		SHADER_PARAMETER(int32, IterationStride)
		SHADER_PARAMETER(int32, IterationFamily)
		SHADER_PARAMETER(int32, IterationOp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PrevDist)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FamilyField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, Aux)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, NextDist)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverPropagateCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"PropagateCS",
	SF_Compute);

// Direct periodic layered-strata synthesis and height blend.
class FMixtormatStrataCarverResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, StrataFrequency)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, StrataWarp)
		SHADER_PARAMETER(float, StrataWarpDetail)
		SHADER_PARAMETER(int32, StrataLayers)
		SHADER_PARAMETER(float, StrataTilt)
		SHADER_PARAMETER(float, StrataTiltVariance)
		SHADER_PARAMETER(float, StrataRotation)
		SHADER_PARAMETER(float, StrataRotationVariance)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, BlendAmount)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Bias)
		SHADER_PARAMETER(float, RemapInMin)
		SHADER_PARAMETER(float, RemapInMax)
		SHADER_PARAMETER(float, RemapOutMin)
		SHADER_PARAMETER(float, RemapOutMax)
		SHADER_PARAMETER(float, CarveClampMin)
		SHADER_PARAMETER(float, CarveClampMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SolvedDist)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolveRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
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

	// 0 = build chunk geometry (one thread per cell), 1 = field (cached), 2 = combine.
	class FStage : SHADER_PERMUTATION_INT("ROCK_STAGE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Style)
		SHADER_PARAMETER(int32, Cells)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, FractureAmount)
		SHADER_PARAMETER(float, SlopeAmount)
		SHADER_PARAMETER(float, ChamferAmount)
		SHADER_PARAMETER(float, ChamferBias)
		SHADER_PARAMETER(float, FractureHeightBias)
		SHADER_PARAMETER(float, GapAmount)
		SHADER_PARAMETER(float, WarpAmount)
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
		SHADER_PARAMETER(int32, WarpScale)
		SHADER_PARAMETER(float, HeightClusters)
		SHADER_PARAMETER(float, Skew)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(int32, MaxLeaves)
		SHADER_PARAMETER(int32, CellsV)
		SHADER_PARAMETER(float, RowHeight)
		SHADER_PARAMETER(float, WallSlope)
		SHADER_PARAMETER(float, ChamferSlope)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRockLeaf>, OutLeaves)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutEdges)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, OutVertices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutLeafCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRockLeaf>, Leaves)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Edges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, Vertices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LeafCounts)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RockHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockTop)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockChamfer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRockWall)
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
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PebbleHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PebbleCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
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
	// 7 one axis of the direction blur.
	class FStage : SHADER_PERMUTATION_INT("FLOW_STAGE", 8);
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


namespace
{
	// One Strata Carver child.
	//
	// Takes the height currently standing as the layer's input and returns the carved one. The
	// caller chains them, so two carvers on one layer are two carves of one surface rather than
	// two competing for the same slot.
	FRDGTextureRef AddStrataCarverPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef SourceHeight)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FStrataCarverRenderData& Carver = Child.Generator.StrataCarver;
		const int32 LayerIndex = LayerCtx.LayerIndex;

		// Zero depth is an exact no-op; skip the resolve dispatch.
		if (Carver.Depth <= 0.0f)
		{
			return SourceHeight;
		}

		const FIntPoint SolveSize = StrataSolveResolution(Request.Resolution);

		// The scoped mask. Independent scope, so the chain starts from white rather than
		// inheriting whatever the layer mask happens to be at this row: a mask authored under a
		// generator describes where *this generator* acts, and folding the layer's own coverage
		// into it would make Mask Influence mean two different things depending on row order.
		const bool bHasScopedMask = HasScopedMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef ScopedMask = bHasScopedMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
			: LayerCtx.CombinedMask;

		// Region IDs published above this generator in the same layer, by the nearest producer.
		// Absent is the normal case and not an error -- a generator makes its own structure and
		// never needs one.
		FRDGTextureRef RegionIds =
			FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		const bool bHasRegionIds = RegionIds != nullptr;
		if (!bHasRegionIds)
		{
			RegionIds = Ctx.EmptyRegionIds;
		}

		// Direct layered-strata synthesis replaces the old recursive front solve.
		if (Carver.StrataLayers > 0)
		{
			FRDGTextureRef CarvedHeight = GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.StrataCarvedHeight"));
			FMixtormatStrataCarverResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverResolveCS::FParameters>();
			P->OutputSize = Request.Resolution;
			P->Seed = Carver.Seed;
			P->StrataFrequency = Carver.StrataFrequency;
			P->StrataAmount = Carver.StrataAmount;
			P->StrataWarp = Carver.StrataWarp;
			P->StrataWarpDetail = Carver.StrataWarpDetail;
			P->StrataLayers = Carver.StrataLayers;
			P->StrataTilt = Carver.StrataTilt;
			P->StrataTiltVariance = Carver.StrataTiltVariance;
			P->StrataRotation = Carver.StrataRotation;
			P->StrataRotationVariance = Carver.StrataRotationVariance;
			P->BlendMode = Carver.BlendMode;
			P->BlendAmount = Carver.BlendAmount;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->Depth = Carver.Depth;
			P->SourceHeight = SourceHeight;
			P->ResolveMask = ScopedMask;
			P->ResolveRegionIds = RegionIds;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(CarvedHeight);
			TShaderMapRef<FMixtormatStrataCarverResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.Layered.L%d.C%d", LayerIndex, Child.SourceChildIndex),
				Shader, P,
				FIntVector(FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8), 1));
			return CarvedHeight;
		}

		const FIntVector SolveGroups(
			FMath::DivideAndRoundUp(SolveSize.X, 8),
			FMath::DivideAndRoundUp(SolveSize.Y, 8),
			1);
		const FIntVector OutputGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		// RGBA16F for the four family fractals -- they are 0..1 and half is plenty. 32-bit for
		// the cost and the distance: cost reaches five figures where a mask confines the solve,
		// and the distance has to stay separable from MaxValue after a hundred accumulated
		// steps, which is exactly the case half floats lose.
		const FRDGTextureDesc FamilyDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc PairDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_G32R32F, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef FamilyField =
			GraphBuilder.CreateTexture(FamilyDesc, TEXT("Mixtormat.StrataFamilies"));
		FRDGTextureRef Aux =
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataAux"));
		FRDGTextureRef DistTargets[2] = {
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataDistA")),
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataDistB"))};

		// RDG rejects a pass that reads a transient nothing has written, and the very first
		// propagation reads whichever half the seed did not fill.
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DistTargets[1]), FVector4f(0.0f));

		// The mask, reduced onto the solve grid. Always allocated so the seed pass has a bound
		// resource, but only filled when there is a mask to reduce -- and cleared to white when
		// there is not, because an unwritten transient is an RDG error and a black one would
		// read as "masked out everywhere" and silently produce no carve at all.
		const FRDGTextureDesc SolveMaskDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_R16F, FClearValueBinding::White,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef SolveMask =
			GraphBuilder.CreateTexture(SolveMaskDesc, TEXT("Mixtormat.StrataSolveMask"));
		if (bHasScopedMask)
		{
			FMixtormatStrataCarverMaskReduceCS::FParameters* MP =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverMaskReduceCS::FParameters>();
			MP->SolveSize = SolveSize;
			MP->OutputSize = Request.Resolution;
			MP->MaskFootprint = FIntPoint(
				FMath::Max(Request.Resolution.X / FMath::Max(SolveSize.X, 1), 1),
				FMath::Max(Request.Resolution.Y / FMath::Max(SolveSize.Y, 1), 1));
			MP->FullResMask = ScopedMask;
			MP->OutSolveMask = GraphBuilder.CreateUAV(SolveMask);

			TShaderMapRef<FMixtormatStrataCarverMaskReduceCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.MaskReduce.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				MP,
				SolveGroups);
		}
		else
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SolveMask), FVector4f(1.0f));
		}

		{
			FMixtormatStrataCarverSeedCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverSeedCS::FParameters>();
			P->SolveSize = SolveSize;
			P->OutputSize = Request.Resolution;
			P->Seed = Carver.Seed;
			P->SeedThreshold = Carver.SeedThreshold;
			P->WorleyCells = Carver.WorleyCells;
			P->SeedDetail = Carver.SeedDetail;
			P->WorleyJitter = Carver.WorleyJitter;
			P->BandFrequency = Carver.BandFrequency;
			P->MaxValue = Carver.MaxValue;
			P->CostAmount = Carver.CostAmount;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->SeedMask = SolveMask;
			P->SeedRegionIds = RegionIds;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutFamilies = GraphBuilder.CreateUAV(FamilyField);
			P->OutAux = GraphBuilder.CreateUAV(Aux);
			P->OutDist = GraphBuilder.CreateUAV(DistTargets[0]);

			TShaderMapRef<FMixtormatStrataCarverSeedCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.Seed.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				P,
				SolveGroups);
		}

		int32 ReadIndex = 0;
		{
			TShaderMapRef<FMixtormatStrataCarverPropagateCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			for (int32 Iteration = 0; Iteration < Carver.Iterations; ++Iteration)
			{
				const int32 WriteIndex = 1 - ReadIndex;
				FMixtormatStrataCarverPropagateCS::FParameters* P =
					GraphBuilder.AllocParameters<FMixtormatStrataCarverPropagateCS::FParameters>();
				P->SolveSize = SolveSize;
				P->Seed = Carver.Seed;
				P->BandFrequency = Carver.BandFrequency;
				P->StrataFrequency = Carver.StrataFrequency;
				P->StrataAmount = Carver.StrataAmount;
				P->StrataWarp = Carver.StrataWarp;
				P->StepScale = Carver.StepScale;
				P->MaxValue = Carver.MaxValue;
				P->PushAmount = Carver.PushAmount;
				P->PushDecay = Carver.PushDecay;
				P->IterationStride = StrataJumpStride(Iteration, Carver.JumpStart);
				P->IterationFamily = static_cast<int32>(
					StrataIterationHash(Carver.OperationSeed, Iteration, 0x2545f491u)
					% static_cast<uint32>(StrataFamilyCount));
				P->IterationOp = static_cast<int32>(
					StrataIterationHash(Carver.OperationSeed, Iteration, 0xad90777du)
					% static_cast<uint32>(StrataOperationCount));
				P->PrevDist = DistTargets[ReadIndex];
				P->FamilyField = FamilyField;
				P->Aux = Aux;
				P->NextDist = GraphBuilder.CreateUAV(DistTargets[WriteIndex]);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.StrataCarver.Propagate.L%d.C%d.It%d",
						LayerIndex, Child.SourceChildIndex, Iteration),
					Shader,
					P,
					SolveGroups);

				ReadIndex = WriteIndex;
			}
		}

		FRDGTextureRef CarvedHeight =
			GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.StrataCarvedHeight"));
		{
			FMixtormatStrataCarverResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverResolveCS::FParameters>();
			P->OutputSize = Request.Resolution;
			P->Seed = Carver.Seed;
			P->StrataFrequency = Carver.StrataFrequency;
			P->StrataAmount = Carver.StrataAmount;
			P->StrataWarp = Carver.StrataWarp;
			P->MaxValue = Carver.MaxValue;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->Depth = Carver.Depth;
			P->Bias = Carver.Bias;
			P->RemapInMin = Carver.RemapInMin;
			P->RemapInMax = Carver.RemapInMax;
			P->RemapOutMin = Carver.RemapOutMin;
			P->RemapOutMax = Carver.RemapOutMax;
			P->CarveClampMin = Carver.ClampMin;
			P->CarveClampMax = Carver.ClampMax;
			P->SolvedDist = DistTargets[ReadIndex];
			P->SourceHeight = SourceHeight;
			P->ResolveMask = ScopedMask;
			P->ResolveRegionIds = RegionIds;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(CarvedHeight);

			TShaderMapRef<FMixtormatStrataCarverResolveCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.Resolve.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				P,
				OutputGroups);
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
	// The combine is separate and cheap: Amount and Height Scale mix the cached rock into the
	// layer's input height, so those two stay live without touching the field.
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
		// Style indexes the four-row table, so it is bounded to it (the shader does the same).
		const float Style = FMath::Clamp(Rock.Style, 0.0f, 3.0f);
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
		// Cx Cy, Sx Sy, TopConstant, Top, Radius, 3 chip planes (9) = 16 floats.
		float Floats[16];
		uint32 Uints[4];
	};
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

	bool HasActiveFlowTools(
		const FRenderRequest& Request,
		const int32 LayerIndex,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex)
	{
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (IsFlowToolChild(Candidate, OwnerSourceChildIndex)
				&& (!IsNeutralFlowTool(Candidate.Effect)
					|| IsPreviewingChild(Request, LayerIndex, Candidate.SourceChildIndex)))
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
		const FChildRenderData& RockChild,
		FRDGTextureRef BoundaryField,
		FRDGTextureRef RockField,
		FRDGTextureRef& InOutCoverage)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FIntPoint Size = Request.Resolution;
		// The same capped grid the strata solve uses; the resolve refines distance at full res.
		const FIntPoint SolveSize = StrataSolveResolution(Size);
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
			if (!IsFlowToolChild(FlowChild, RockChild.SourceChildIndex))
			{
				continue;
			}
			const FEffectRenderData& Flow = FlowChild.Effect;
			const int32 FlowIndex = FlowChild.SourceChildIndex;
			const bool bPreviewing = IsPreviewingChild(Request, LayerIndex, FlowIndex);
			const bool bNeutral = IsNeutralFlowTool(Flow);
			if (bNeutral && !bPreviewing)
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
			}
		}
		return Current;
	}

	FRDGTextureRef AddRockFormationPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef SourceHeight,
		const bool bFieldOnly = false)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRockFormationRenderData& Rock = Child.Generator.RockFormation;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const FRockLayout Layout = ResolveRockLayout(Rock);
		const auto FillParameters = [&Rock, &Layout, Size](FMixtormatRockFormationCS::FParameters* P)
		{
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
			P->SlopeAmount = Rock.Slope;
			P->ChamferAmount = Rock.Chamfer;
			P->ChamferBias = Rock.ChamferBias;
			P->FractureHeightBias = Rock.FractureHeightBias;
			P->GapAmount = Rock.Gap;
			P->WarpAmount = Rock.Warp;
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
			P->WarpScale = Rock.WarpScale;
			P->HeightClusters = Rock.HeightClusters;
			P->Skew = Rock.Skew;
			P->BlendMode = Rock.BlendMode;
			P->Amount = Rock.Amount;
			P->HeightScale = Rock.HeightScale;
		};

		// Fixed cache slots: height, top, chamfer, wall, signed boundary distance, IDs,
		// signed boundary distance + outline-sampled flag (RG32F).
		FRDGTextureRef Outputs[7] = {};
		// Produced once per layer: the ID phase may already have run it (see AddGeneratorFieldPasses).
		if (const TArray<FRDGTextureRef, TInlineAllocator<7>>* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Slot = 0; Slot < 7; ++Slot)
			{
				Outputs[Slot] = (*Memo)[Slot];
			}
		}
		else
		{
			FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
			const uint64 NodeKey = NodeCache && Rock.FieldKey != 0
				? MixtormatComposeHash::Combine(Rock.FieldKey, 0x526F636Bull)
				: 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit =
				NodeKey != 0 ? NodeCache->Find(NodeKey, Size)
					: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			if (Hit.IsValid())
			{
				static const TCHAR* const Names[7] = {
					TEXT("Mixtormat.Rock.Height"), TEXT("Mixtormat.Rock.Top"), TEXT("Mixtormat.Rock.Chamfer"),
					TEXT("Mixtormat.Rock.Wall"), TEXT("Mixtormat.Rock.EdgeDistance"), TEXT("Mixtormat.Rock.Ids"),
					TEXT("Mixtormat.Rock.BoundaryField")};
				for (int32 Slot = 0; Slot < 7; ++Slot)
				{
					Outputs[Slot] = Hit->Outputs[Slot].IsValid()
						? GraphBuilder.RegisterExternalTexture(Hit->Outputs[Slot], Names[Slot])
						: nullptr;
				}
			}
			if (!Outputs[0] || !Outputs[1] || !Outputs[2] || !Outputs[3] || !Outputs[4] || !Outputs[5] || !Outputs[6])
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

				// Build: each cell's chunk geometry, once, into buffers the field reads.
				const uint32 CellCount = static_cast<uint32>(Layout.CellsU) * static_cast<uint32>(Layout.CellsV);
				const uint32 LeafSlots = CellCount * static_cast<uint32>(Layout.MaxLeaves);
				FRDGBufferRef LeafBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRockLeafStride), LeafSlots), TEXT("Mixtormat.Rock.Leaves"));
				FRDGBufferRef EdgeBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), LeafSlots * RockMaxVertices), TEXT("Mixtormat.Rock.Edges"));
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
				P->Vertices = GraphBuilder.CreateSRV(VertexBuffer);
				P->LeafCounts = GraphBuilder.CreateSRV(CountBuffer);
				P->OutRockHeight = GraphBuilder.CreateUAV(Outputs[0]);
				P->OutRockTop = GraphBuilder.CreateUAV(Outputs[1]);
				P->OutRockChamfer = GraphBuilder.CreateUAV(Outputs[2]);
				P->OutRockWall = GraphBuilder.CreateUAV(Outputs[3]);
				P->OutRockEdgeDistance = GraphBuilder.CreateUAV(Outputs[4]);
				P->OutRockIds = GraphBuilder.CreateUAV(Outputs[5]);
				P->OutRockBoundaryField = GraphBuilder.CreateUAV(Outputs[6]);
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
					for (int32 Slot = 0; Slot < 7; ++Slot)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Slot], &Entry->Outputs[Slot]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}

			// Reusable outputs: chunk IDs for ID consumers and four scalar outputs for
			// Copy Output / published-source masks. Slot 4 is the new signed boundary
			// distance; slot 6 carries its validity without changing scalar mask reads.
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Outputs[5]);
			static const TCHAR* const MaskNames[4] = {
				TEXT("RockTop"), TEXT("RockChamfer"), TEXT("RockWall"), TEXT("RockEdgeDistance")};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FName OutputName(MaskNames[Index]);
				Ctx.PublishedMaskOutputs.Add(
					FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, OutputName}, Outputs[1 + Index]);
				if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, OutputName,
					LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					AddDebugPreviewMaskBlitPass(GraphBuilder, Outputs[1 + Index],
						Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
				}
			}
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
				LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(GraphBuilder, Outputs[5], nullptr,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}

			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, 7);
		}
		if (bFieldOnly)
		{
			return nullptr;
		}

		// Normalized first, so flow tools and the combine both see the 0..1 field. Scoped flow
		// tools then transform the rock's own height before the combine; they run even at Amount 0
		// while one of them is previewed, so its diagnostics are not blank.
		const bool bPreviewingFlow = IsPreviewingAnyFlowTool(Request, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex);
		FRDGTextureRef RockField = Outputs[0];
		if (Rock.bNormalize && (Rock.Amount != 0.0f || bPreviewingFlow))
		{
			RockField = AddNormalizeFieldPasses(GraphBuilder, Outputs[0], Size,
				Rock.RemapLow, Rock.RemapHigh, TEXT("Mixtormat.Rock.NormalizedHeight"));
		}
		if ((Rock.Amount != 0.0f || bPreviewingFlow)
			&& HasActiveFlowTools(Request, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
		{
			FRDGTextureRef NoCoverage = nullptr;
			RockField = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child, Outputs[6], RockField, NoCoverage);
		}

		// A neutral node passes its input through, like every other generator.
		if (Rock.Amount == 0.0f)
		{
			return SourceHeight;
		}

		FRDGTextureRef Combined = GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.Rock.LayerHeight"));
		{
			FMixtormatRockFormationCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatRockFormationCS::FStage>(2);
			TShaderMapRef<FMixtormatRockFormationCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatRockFormationCS::FParameters>();
			FillParameters(P);
			P->RockHeight = RockField;
			P->SourceHeight = SourceHeight;
			P->OutHeight = GraphBuilder.CreateUAV(Combined);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.RockFormation.Combine.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
		}
		return Combined;
	}

	// One Pebbles child. Same shape as Rock Formation: the scatter field is cached against its
	// settings and resolution, and Amount mixes it into the layer's height without re-running it.
	FRDGTextureRef AddPebblesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef SourceHeight,
		const bool bFieldOnly = false)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FPebblesRenderData& Pebbles = Child.Generator.Pebbles;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const auto FillParameters = [&Pebbles, Size](FMixtormatPebblesCS::FParameters* P)
		{
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
			P->Amount = Pebbles.Amount;
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
				? MixtormatComposeHash::Combine(Pebbles.FieldKey, 0x506562626C65ull)
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
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Outputs[4]);
			static const TCHAR* const MaskNames[3] = {
				TEXT("PebbleCoverage"), TEXT("PebbleEdgeDistance"), TEXT("PebbleRandom")};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FName OutputName(MaskNames[Index]);
				Ctx.PublishedMaskOutputs.Add(
					FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, OutputName}, Outputs[1 + Index]);
				if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask, OutputName,
					LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					AddDebugPreviewMaskBlitPass(GraphBuilder, Outputs[1 + Index],
						Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
				}
			}
			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
				LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(GraphBuilder, Outputs[4], nullptr,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Size);
			}

			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}
		if (bFieldOnly)
		{
			return nullptr;
		}

		// Scoped flow tools, as under Rock Formation: height and coverage move together.
		FRDGTextureRef PebbleField = Outputs[0];
		FRDGTextureRef PebbleCoverage = Outputs[1];
		if ((Pebbles.Amount != 0.0f || IsPreviewingAnyFlowTool(Request, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
			&& HasActiveFlowTools(Request, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
		{
			PebbleField = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child,
				PackScalarBoundary(Ctx, Outputs[2]), Outputs[0], PebbleCoverage);
		}

		if (Pebbles.Amount == 0.0f)
		{
			return SourceHeight;
		}

		FRDGTextureRef Combined = GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.Pebbles.LayerHeight"));
		{
			FMixtormatPebblesCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatPebblesCS::FStage>(1);
			TShaderMapRef<FMixtormatPebblesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatPebblesCS::FParameters>();
			FillParameters(P);
			P->PebbleHeight = PebbleField;
			P->PebbleCoverage = PebbleCoverage;
			P->SourceHeight = SourceHeight;
			P->OutHeight = GraphBuilder.CreateUAV(Combined);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Pebbles.Combine.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
		}
		return Combined;
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
			AddRockFormationPasses(Ctx, LayerCtx, Layer, Child, nullptr, true);
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::Pebbles)
		{
			AddPebblesPasses(Ctx, LayerCtx, Layer, Child, nullptr, true);
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
								|| Child.Generator.Type == EMixtormatGeneratorType::Pebbles);
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

	// Authored order. Each generator is handed what the previous one produced, so a second
	// carver cuts into the first one's grooves instead of re-cutting the original surface --
	// and because the chain is carried in LayerCtx.LayerInputHeight as well as in the local,
	// anything a later generator's own passes read sees the accumulated result too.
	//
	// A generator that is neutral returns its input unchanged rather than a copy of it (see the
	// Depth guard in AddStrataCarverPasses), so a disabled or zero-depth node in the middle of a
	// chain cannot reset what ran before it: the pointer simply passes through.
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
				Ctx, LayerCtx, Layer, Child, LayerCtx.LayerInputHeight);
			break;
		case EMixtormatGeneratorType::Fracture:
			// Fracture consumes ramp relief in the isolated structural stage.
			break;
		case EMixtormatGeneratorType::RockFormation:
			LayerCtx.LayerInputHeight = AddRockFormationPasses(
				Ctx, LayerCtx, Layer, Child, LayerCtx.LayerInputHeight);
			break;
		case EMixtormatGeneratorType::Pebbles:
			LayerCtx.LayerInputHeight = AddPebblesPasses(
				Ctx, LayerCtx, Layer, Child, LayerCtx.LayerInputHeight);
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
