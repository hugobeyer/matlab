// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "Compositing/MixtormatComposeHash.h"
#include "MixtormatGpuNoisePasses.h"
#include "MixtormatGpuNativeFlowPasses.h"
#include "MixtormatGpuGaussianBlur.h"
#include "MixtormatColorRampMath.h"
#include "MixtormatScalarRampMath.h"

#include "Algo/AnyOf.h"
#include "GlobalShader.h"
#include "Misc/ScopeExit.h"
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
//         -> AddGeneratorLayerPasses      <- here, on Generator layers only
//         -> mask children, effects, AddLayerCompositePass
//
// AddGeneratorLayerPasses is called once per Generator layer and walks its module children in
// authored order. Each module builds its own scalar height, then blends into the layer's
// running height with the shared HeightBlend; the result replaces LayerCtx.LayerInputHeight.

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
	constexpr int32 FeatherIterationsHardMax = 512;

	int32 ResolveFeatherIterations(const int32 Requested)
	{
		ensureMsgf(Requested <= FeatherIterationsHardMax,
			TEXT("Feather iterations (%d) exceeded hard maximum (%d); clamped. Value likely bypassed the UI."),
			Requested, FeatherIterationsHardMax);
		return FMath::Clamp(Requested, 0, FeatherIterationsHardMax);
	}

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


}


class FMixtormatBooleanStackCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBooleanStackCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBooleanStackCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Operation)
		SHADER_PARAMETER(int32, EdgeStyle)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(FVector2f, TargetOffset)
		SHADER_PARAMETER(FVector2f, SourceOffset)
		SHADER_PARAMETER(float, TargetHeightOffset)
		SHADER_PARAMETER(float, SourceHeightOffset)
		SHADER_PARAMETER(float, SourceScale)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, JoinWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TargetHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArea)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDifference)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
	{
		return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBooleanStackCS,
	"/Plugin/Mixtormat/Private/MixtormatBooleanStack.usf", "MainCS", SF_Compute);

class FMixtormatBooleanCarveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBooleanCarveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBooleanCarveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(uint32, GeneratorLayer)
        SHADER_PARAMETER(FVector2f, GeneratorUVScale)
        SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
        SHADER_PARAMETER(int32, GeneratorUVRotation)
        SHADER_PARAMETER(uint32, GeneratorUVFlipU)
        SHADER_PARAMETER(uint32, GeneratorUVFlipV)
        SHADER_PARAMETER(uint32, UsePreGenerationUV)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
        SHADER_PARAMETER(int32, Seed)
		SHADER_PARAMETER(int32, Count)
		SHADER_PARAMETER(int32, Operation)
		SHADER_PARAMETER(int32, Shape)
		SHADER_PARAMETER(int32, Sides)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, RadiusVariation)
		SHADER_PARAMETER(float, CenterDepth)
		SHADER_PARAMETER(float, DepthVariation)
		SHADER_PARAMETER(float, ReferenceHeight)
		SHADER_PARAMETER(float, DepthScale)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, Rotation)
		SHADER_PARAMETER(float, RotationVariation)
		SHADER_PARAMETER(float, ChamferFraction)
		SHADER_PARAMETER(float, ChamferVariation)
		SHADER_PARAMETER(float, HeightToUV)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BelowHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightDelta)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCutDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryFront)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryBack)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryMask)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
	{
		return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBooleanCarveCS,
	"/Plugin/Mixtormat/Private/MixtormatBooleanCarve.usf", "MainCS", SF_Compute);

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
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
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
		SHADER_PARAMETER(int32, MaxLeaves)
		SHADER_PARAMETER(uint32, FacetSlotsPerLeaf)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRockFacet>, OutFacets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRockFacet>, Facets)
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
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
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


class FMixtormatScalesCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatScalesCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatScalesCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("SCALES_STAGE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(FIntPoint, ScaleCount)
		SHADER_PARAMETER(FIntPoint, SearchReach)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, Shape)
		SHADER_PARAMETER(float, Stagger)
		SHADER_PARAMETER(float, Direction)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Length)
		SHADER_PARAMETER(float, TipRoundness)
		SHADER_PARAMETER(float, Taper)
		SHADER_PARAMETER(float, Overlap)
		SHADER_PARAMETER(float, TipLift)
		SHADER_PARAMETER(float, RootTuck)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Dome)
		SHADER_PARAMETER(float, Ridge)
		SHADER_PARAMETER(float, Bevel)
		SHADER_PARAMETER(float, SizeRandom)
		SHADER_PARAMETER(float, RotationRandom)
		SHADER_PARAMETER(float, HeightRandom)
		SHADER_PARAMETER(float, RowJitter)
		SHADER_PARAMETER(float, GrooveAmount)
		SHADER_PARAMETER(float, GrooveCount)
		SHADER_PARAMETER(float, GrooveDirection)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FScalePlate>, OutPlates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FScalePlate>, Plates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutScaleIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutScaleUV)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatScalesCS, "/Plugin/Mixtormat/Private/MixtormatScales.usf", "MainCS", SF_Compute);

class FMixtormatCliffStrataCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCliffStrataCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCliffStrataCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("CLIFF_STAGE", 10);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(uint32, BlockPlaneStride)
		SHADER_PARAMETER(uint32, ChamferMode)
		SHADER_PARAMETER(float, BoxChamferWidth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FCliffBlock>, OutBlocks)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutBlockPlanes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCliffBlock>, Blocks)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BlockPlanes)
		SHADER_PARAMETER(int32, CountX) SHADER_PARAMETER(int32, CountY) SHADER_PARAMETER(float, Density)
		SHADER_PARAMETER(float, SizeMin) SHADER_PARAMETER(float, SizeMax) SHADER_PARAMETER(float, SizeAspect)
		SHADER_PARAMETER(float, Jitter) SHADER_PARAMETER(float, FlowVariation) SHADER_PARAMETER(float, HeightMin) SHADER_PARAMETER(float, HeightMax)
		SHADER_PARAMETER(int32, Steps) SHADER_PARAMETER(float, Rotation) SHADER_PARAMETER(float, LeanX) SHADER_PARAMETER(float, LeanY)
		SHADER_PARAMETER(int32, FormationCells) SHADER_PARAMETER(float, FormationAmount) SHADER_PARAMETER(uint32, QuarterCopies)
		SHADER_PARAMETER(int32, QuarterYCount) SHADER_PARAMETER(float, QuarterFill) SHADER_PARAMETER(float, QuarterSize)
		SHADER_PARAMETER(float, QuarterHeight) SHADER_PARAMETER(float, QuarterJitterX) SHADER_PARAMETER(float, QuarterJitterY)
		SHADER_PARAMETER(int32, Sides) SHADER_PARAMETER(uint32, ShapeRandom)
		SHADER_PARAMETER(float, CameraYaw) SHADER_PARAMETER(float, CameraPitch) SHADER_PARAMETER(float, ViewScale)
		SHADER_PARAMETER(float, DepthMin) SHADER_PARAMETER(float, DepthMax)
		SHADER_PARAMETER(float, UnitDistance) SHADER_PARAMETER(float, UnitDistanceIdLerp) SHADER_PARAMETER(float, CarveDepth)
		SHADER_PARAMETER(float, CarveVoronoi) SHADER_PARAMETER(float, YBias) SHADER_PARAMETER(float, YBiasVoronoi)
		SHADER_PARAMETER(uint32, YBiasVoronoiInvert) SHADER_PARAMETER(float, NegativeYUnitDistanceTaper) SHADER_PARAMETER(uint32, Reverse)
		SHADER_PARAMETER(int32, Seed) SHADER_PARAMETER(int32, VoronoiCells) SHADER_PARAMETER(float, FlowVoronoi)
		SHADER_PARAMETER(float, ChamferWidth) SHADER_PARAMETER(float, ChamferIntensity) SHADER_PARAMETER(float, ChamferVoronoi)
		SHADER_PARAMETER(float, BlockCavityWidth) SHADER_PARAMETER(float, RowCavityWidth) SHADER_PARAMETER(float, CavityIntensity)
		SHADER_PARAMETER(float, CavityVoronoiThreshold) SHADER_PARAMETER(float, CavityVoronoiMaskGain)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RawHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, Flow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, BlockIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RowIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, VoronoiRaw)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, VoronoiSmooth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SweepIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, IdDistanceIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RowDistanceIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRawHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutFlow)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutBlockIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutRowIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutVoronoiRaw)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutVoronoiSmooth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSweep)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutIdDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRowDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBlockSeam)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRowSeam)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCavity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCoverage)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters){return IsFeatureLevelSupported(Parameters.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatCliffStrataCS,"/Plugin/Mixtormat/Private/MixtormatCliffStrata.usf","MainCS",SF_Compute);

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
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
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
		SHADER_PARAMETER(float, ChamferEdge)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackDelta)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCrackDelta)
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

// Root Network: space-colonization skeleton -> GPU segment buffers -> binned swept SDF.
// One permutation per stage; see MixtormatRootNetwork.usf for the stage table.
class FMixtormatRootNetworkCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRootNetworkCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRootNetworkCS, FGlobalShader);

	// 0 init, 1 assign, 2 extend, 3 segments, 4 scan, 5 scatter, 6 field (cached).
	class FStage : SHADER_PERMUTATION_INT("ROOT_STAGE", 7);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, Trunks)
		SHADER_PARAMETER(uint32, Attractors)
		SHADER_PARAMETER(uint32, StepIndex)
		SHADER_PARAMETER(float, StepLength)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(float, KillDistance)
		SHADER_PARAMETER(float, BranchSpread)
		SHADER_PARAMETER(float, DirectionInertia)
		SHADER_PARAMETER(float, CurlStrength)
		SHADER_PARAMETER(uint32, CurlScale)
		SHADER_PARAMETER(float, BaseRadius)
		SHADER_PARAMETER(float, TipTaper)
		SHADER_PARAMETER(float, JunctionSmooth)
		SHADER_PARAMETER(float, ProfileFlatness)
		SHADER_PARAMETER(float, WeaveAmount)
		SHADER_PARAMETER(float, KinkStrength)
		SHADER_PARAMETER(float, ProfileBias)
		SHADER_PARAMETER(float, BurialDepth)
		SHADER_PARAMETER(float, AzimuthAngle)
		SHADER_PARAMETER(float, AzimuthStrength)
		SHADER_PARAMETER(float, SwirlStrength)
		SHADER_PARAMETER(float, AxisRatio)
		SHADER_PARAMETER(uint32, UseRadiusCurve)
		SHADER_PARAMETER(uint32, RadiusCurveCount)
		SHADER_PARAMETER(uint32, RadiusCurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, RadiusCurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER(uint32, UseBurialCurve)
		SHADER_PARAMETER(uint32, BurialCurveCount)
		SHADER_PARAMETER(uint32, BurialCurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, BurialCurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER(float, RootUVScaleU)
		SHADER_PARAMETER(float, RootUVScaleV)
		SHADER_PARAMETER(float, RootUVOffsetU)
		SHADER_PARAMETER(float, RootUVOffsetV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootNode>, RWNodeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootSegment>, RWSegmentBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootAttractor>, RWAttractorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWStateBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWNodePullBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinCounts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinWriteHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinEntries)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRootSegment>, SegmentBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinEntries)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootEdgeDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootArcLength)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootAcross)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutRootUV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutRootIds)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRootNetworkCS,
	"/Plugin/Mixtormat/Private/MixtormatRootNetwork.usf",
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
		SHADER_PARAMETER(uint32, NormalizeMode)
		SHADER_PARAMETER(float, OutputScale)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(uint32, HasHeightGate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HeightGate)
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
		P->NormalizeMode = 0u;
		P->OutputScale = 1.0f;
		P->HasHeightGate = 0u;
		P->HeightGate = Field;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Normalized);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FieldRange.Normalize"), Shader, P, Groups);
	}
	return Normalized;
}

FRDGTextureRef AddSignedGeneratorHeightPasses(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Field,
	const FIntPoint Size,
	const bool bNormalize,
	const float OutputScale,
	const float HeightBias,
	const bool bCenterNoise,
	FRDGTextureRef HeightGate,
	const TCHAR* Name)
{
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
	// The reduce is only needed for the centring path. Normalize off uses the raw signed field, so
	// the min/max dispatch is skipped entirely.
	FRDGBufferRef RangeBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2), TEXT("Mixtormat.GeneratorSignedRange"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RangeBuffer), 0u);
	if (bNormalize)
	{
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(0);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->SourceField = Field;
		P->OutRange = GraphBuilder.CreateUAV(RangeBuffer);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.GeneratorSigned.Reduce"), Shader, P, Groups);
	}
	FRDGTextureRef Signed = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	{
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(1);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->OutLow = -1.0f;
		P->OutHigh = 1.0f;
		// Mode 3 centres Noise height; mode 1 preserves max-absolute normalization
		// for all other generators. Normalize-off leaves the raw signed height.
		P->NormalizeMode = bNormalize ? (bCenterNoise ? 3u : 1u) : 2u;
		P->OutputScale = FMath::IsFinite(OutputScale) ? OutputScale : 1.0f;
		P->HeightBias = FMath::IsFinite(HeightBias) ? HeightBias : 0.0f;
		P->HasHeightGate = HeightGate ? 1u : 0u;
		P->HeightGate = HeightGate ? HeightGate : Field;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Signed);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.GeneratorSigned.Resolve"), Shader, P, Groups);
	}
	return Signed;
}

// Generator flow tools scoped under a Rock Formation. One parameter struct for every stage;
class FMixtormatGeneratorBundleCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorBundleCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorBundleCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("BUNDLE_STAGE", 13);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, WarpedUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, IdField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VectorField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ModuleHeight)
		SHADER_PARAMETER(float, InvalidDistance)


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

class FMixtormatBehaviorFlowFeatherCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMixtormatBehaviorFlowFeatherCS);
    SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorFlowFeatherCS, FGlobalShader);
    class FStage : SHADER_PERMUTATION_INT("FLOW_FEATHER_STAGE", 5);
    using FPermutationDomain = TShaderPermutationDomain<FStage>;
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(float, Radius)
        SHADER_PARAMETER(float, Strength)
        SHADER_PARAMETER(float, DirectionBias)
        SHADER_PARAMETER(uint32, HasMask)
        SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
        SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceFlow)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GaussianFlow)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Validity)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskField)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlow)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArrival)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorFlowFeatherCS,
    "/Plugin/Mixtormat/Private/MixtormatBehaviorFlowFeather.usf", "MainCS", SF_Compute);

class FMixtormatBehaviorFeatherCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMixtormatBehaviorFeatherCS);
    SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorFeatherCS, FGlobalShader);
    class FStage : SHADER_PERMUTATION_INT("FEATHER_STAGE", 9);
    using FPermutationDomain = TShaderPermutationDomain<FStage>;
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(float, Radius)
        SHADER_PARAMETER(float, Strength)
        SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
        SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
        SHADER_PARAMETER(float, DirectionBias)
        SHADER_PARAMETER(uint32, Side)
        SHADER_PARAMETER(uint32, HasBoundary)
        SHADER_PARAMETER(uint32, HasMask)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, GaussianHeight)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskField)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, FeatherEnvelope)
        SHADER_PARAMETER(int32, FeatherJump)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArrival)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutFeatherEnvelope)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorFeatherCS,
    "/Plugin/Mixtormat/Private/MixtormatBehaviorFeather.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorHeightBlendCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightBlendCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightBlendCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Op)
		SHADER_PARAMETER(uint32, HasSource)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Scale)
		SHADER_PARAMETER(float, Softness)
		SHADER_PARAMETER(float, Threshold)
		SHADER_PARAMETER(float, EdgeSoftness)
		SHADER_PARAMETER(float, BaseBias)
		SHADER_PARAMETER(float, BlendBias)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightBlendCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightBlend.usf", "MainCS", SF_Compute);

// Generator-layer Height Curve / Height Remap sublayer: remaps the running signed height through
// the scalar ramp with signed normalization, signed input range, balance, contrast, offset,
// invert, and an Amount lerp against the original input.
class FMixtormatGeneratorHeightCurveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightCurveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightCurveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, InputMin)
		SHADER_PARAMETER(float, InputMax)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(uint32, bInvert)
		SHADER_PARAMETER(uint32, CurveCount)
		SHADER_PARAMETER(uint32, CurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, CurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightCurveCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightCurve.usf", "MainCS", SF_Compute);

// Generator-layer Height Color Ramp sublayer: maps the running signed height through a colour ramp.
class FMixtormatGeneratorHeightColorRampCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightColorRampCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightColorRampCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, StopCount)
		SHADER_PARAMETER(uint32, Interpolation)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, InputScale)
		SHADER_PARAMETER(float, InputOffset)
		SHADER_PARAMETER(uint32, MirrorRepeat)
		SHADER_PARAMETER_ARRAY(FVector4f, Positions, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER_ARRAY(FVector4f, Colors, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER(uint32, HasGate)
		SHADER_PARAMETER(uint32, HasPreviousColor)
		SHADER_PARAMETER(uint32, SourceMode)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, FlowModulation)
		SHADER_PARAMETER(FVector2f, FlowModulationDir)
		SHADER_PARAMETER(float, UVModulation)
		SHADER_PARAMETER(FVector2f, UVModulationDir)
		SHADER_PARAMETER(float, IdVariation)
		SHADER_PARAMETER(uint32, IdVariationSeed)
		SHADER_PARAMETER(uint32, HasModFlow)
		SHADER_PARAMETER(uint32, HasModUV)
		SHADER_PARAMETER(uint32, HasModIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourceUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightColorRampCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightColorRamp.usf", "MainCS", SF_Compute);


class FMixtormatBehaviorUvBlendCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorUvBlendCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorUvBlendCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourceCoordinates)
		// Per-field composition for the Direction socket. Amplitude scales this
		// socket's weight; Reverse flips the transported displacement.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorUvBlendCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "MainCS", SF_Compute);

class FMixtormatBehaviorModeMixCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorModeMixCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorModeMixCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, ModeMix)
		SHADER_PARAMETER(uint32, HasStandard)
		SHADER_PARAMETER(uint32, HasTraced)
		SHADER_PARAMETER(FVector4f, ModeMixDriverParamsA)
		SHADER_PARAMETER(FVector4f, ModeMixDriverParamsB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ModeMixDriverSignal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, StandardCoordinates)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, TracedCoordinates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorModeMixCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "ModeMixCS", SF_Compute);

class FMixtormatBehaviorHeightGradientCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorHeightGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorHeightGradientCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, GradientReach)
		SHADER_PARAMETER(uint32, SmoothHeightGradient)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		// Per-field composition for the Direction socket on the local-gradient path.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorHeightGradientCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "HeightGradientCS", SF_Compute);

class FMixtormatBehaviorSignedPushCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorSignedPushCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorSignedPushCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorHeight)
		// Per-field composition for the Height socket: amplitude, sign and the
		// signed-scalar combine. Gather admits Blend only where one was authored.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER(uint32, FieldBlend)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSignedHeight)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorSignedPushCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "SignedPushCS", SF_Compute);

class FMixtormatBehaviorSculptCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorSculptCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorSculptCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, TraceLength)
		SHADER_PARAMETER(int32, TraceSteps)
		SHADER_PARAMETER(float, Falloff)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, WorkingFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WorkingFlowValidity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_ARRAY(FVector4f, SculptDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, SculptDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SculptDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SculptDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSculptMask)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorSculptCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorSculpt.usf", "MainCS", SF_Compute);


class FMixtormatCanonicalFlowTraceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCanonicalFlowTraceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCanonicalFlowTraceCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, TraceLength)
		SHADER_PARAMETER(float, WarpStrength)
		SHADER_PARAMETER(int32, Steps)
		SHADER_PARAMETER_ARRAY(FVector4f, FlowDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, FlowDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FlowField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowValidity)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutWarpedUV)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatCanonicalFlowTraceCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowTrace.usf", "MainCS", SF_Compute);


class FMixtormatScalarBoundaryPackCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatScalarBoundaryPackCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatScalarBoundaryPackCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutBoundary)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatScalarBoundaryPackCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowPreview.usf", "PackScalarBoundaryCS", SF_Compute);

namespace
{
	// A module of a Generator layer: its payload and its child index in that layer. Publication
	// happens once per module in AddGeneratorLayerPasses, after flow.
	struct FGeneratorPassInput
	{
		const FGeneratorRenderData& Generator;
		int32 SourceChildIndex;
		FRDGTextureRef PreUV = nullptr;
	};

	// Shared Flow -> destination-UV binding for layer references and explicit generator inputs.
	// The producer's typed field stays intact; only the consumer's placement is traced.
	FRDGTextureRef AddReferencedFlowUVPass(FMixtormatComposeContext& Ctx,
		const FOutputReferenceRenderData& Reference, const FPublishedField& Field,
		const int32 LayerIndex, const int32 ChildIndex,
		const FScalarDriverRenderData* Drivers = nullptr, const bool bCanonical = false)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Coordinates = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.OutputReference.FlowUV"));
		auto* P = GraphBuilder.AllocParameters<FMixtormatCanonicalFlowTraceCS::FParameters>();
		P->OutputSize = Size;
		P->TraceLength = Reference.FlowTraceLength;
		P->WarpStrength = Reference.FlowAmount;
		P->Steps = FMath::Clamp(Reference.FlowSteps, 1, 64);
		FRDGTextureRef Signals[2] = {Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal};
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FScalarDriverRenderData* Driver = Drivers ? &Drivers[Slot] : nullptr;
			FRDGTextureRef* Snapshot = Driver && Driver->bEnabled
				? Ctx.DriverSnapshots.Find(Driver->SourceLayerId) : nullptr;
			const bool bResolved = Snapshot != nullptr;
			Signals[Slot] = bResolved ? *Snapshot : Ctx.EmptyDriverSignal;
			P->FlowDriverParamsA[Slot] = FVector4f(bResolved ? 1.0f : 0.0f,
				Driver && Driver->bInvert ? 1.0f : 0.0f,
				Driver ? Driver->InputMin : 0.0f, Driver ? Driver->InputMax : 1.0f);
			P->FlowDriverParamsB[Slot] = FVector4f(
				Driver ? Driver->OutputMin : 0.0f, Driver ? Driver->OutputMax : 1.0f,
				Driver ? Driver->Amount : 0.0f,
				Driver ? static_cast<float>(Driver->Combine) : 0.0f);
		}
		P->FlowDriverSignal0 = Signals[0];
		P->FlowDriverSignal1 = Signals[1];
		P->FlowField = Field.Texture;
		P->FlowValidity = Field.Validity ? Field.Validity : Ctx.EmptyDriverSignal;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		P->OutWarpedUV = GraphBuilder.CreateUAV(Coordinates);
		TShaderMapRef<FMixtormatCanonicalFlowTraceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.CanonicalFlow.Trace.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Coordinates;
	}

	// Every module sits in the Generator layer's UV placement, so the layer moves all of them.
	template<typename TParameters>
	void FillGeneratorPlacement(TParameters* P, const FLayerRenderData& Layer, FRDGTextureRef PreUV, FRDGTextureRef Fallback)
	{
		P->GeneratorLayer = 1u;
		P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
		P->GeneratorUVOffset = Layer.UVOffset;
		P->GeneratorUVRotation = Layer.Rotation;
		P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
		P->UsePreGenerationUV = PreUV ? 1u : 0u;
		P->PreGenerationUV = PreUV ? PreUV : Fallback;
	}


	void AddGeneratorModuleCombine(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef RunningHeight,
		const FGeneratorBundle& Module,
		const FChildRenderData& Child,
		FRDGTextureRef& OutHeight)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		OutHeight = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.RunningHeight"));

		FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(9);
		TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
		P->OutputSize = Size;
		P->RunningHeight = RunningHeight;
		P->ModuleHeight = Module.Height;
		P->OutScalar = GraphBuilder.CreateUAV(OutHeight);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.Combine"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	}

	// Generator-layer Height Blend sublayer. Source is the referenced module's signed height, or
	// the running height itself when nothing is referenced.
	FRDGTextureRef AddGeneratorHeightBlendPass(FMixtormatComposeContext& Ctx, FRDGTextureRef RunningHeight,
		FRDGTextureRef SourceHeight, const FGeneratorHeightBlendRenderData& Blend)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.HeightBlend"));
		TShaderMapRef<FMixtormatGeneratorHeightBlendCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightBlendCS::FParameters>();
		P->OutputSize = Size;
		P->Op = static_cast<uint32>(Blend.Op);
		P->HasSource = Blend.SourceChildIndex != INDEX_NONE ? 1u : 0u;
		P->Amount = Blend.Amount;
		P->Scale = Blend.Scale;
		P->Softness = Blend.Softness;
		P->Threshold = Blend.Threshold;
		P->EdgeSoftness = Blend.EdgeSoftness;
		P->BaseBias = Blend.BaseBias;
		P->BlendBias = Blend.BlendBias;
		P->RunningHeight = RunningHeight;
		P->SourceHeight = SourceHeight;
		P->OutHeight = GraphBuilder.CreateUAV(Out);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightBlend"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Out;
	}

	// Generator-layer Height Curve / Height Remap sublayer.
	FRDGTextureRef AddGeneratorHeightCurvePass(FMixtormatComposeContext& Ctx, FRDGTextureRef RunningHeight,
		const FGeneratorHeightCurveRenderData& Curve)
	{
		if (Curve.Amount == 0.0f)
		{
			return RunningHeight;
		}
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.HeightCurve"));
		TShaderMapRef<FMixtormatGeneratorHeightCurveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightCurveCS::FParameters>();
		P->OutputSize = Size;
		P->Amount = Curve.Amount;

		P->InputMin = Curve.InputMin;
		P->InputMax = Curve.InputMax;

		P->Offset = Curve.Offset;
		P->bInvert = Curve.bInvert;
		P->CurveCount = Curve.CurveCount;
		P->CurveInterpolation = Curve.CurveInterpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
		{
			P->CurvePoints[Index] = Curve.CurvePoints[Index];
		}
		P->RunningHeight = RunningHeight;
		P->SourceHeight = Curve.bNormalizeInput != 0
			? AddSignedGeneratorHeightPasses(GraphBuilder, RunningHeight, Size, true,
				1.0f, 0.0f, false, nullptr, TEXT("Mixtormat.Generator.HeightCurve.Normalized"))
			: RunningHeight;
		P->OutHeight = GraphBuilder.CreateUAV(Out);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightCurve"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Out;
	}

	// Generator-layer Height Color Ramp sublayer. SourceHeight is whichever height the module's
	// resolved source names; Gate is the scoped mask under it, or SourceHeight when there is
	// none (HasGate 0, the Strata Carver stand-in rule). The pass is a pure map.
	FRDGTextureRef AddGeneratorHeightColorRampPass(FMixtormatComposeContext& Ctx, FRDGTextureRef SourceHeight,
		FRDGTextureRef SourceFlow, FRDGTextureRef SourceUV, FRDGTextureRef SourceIds,
		const uint32 SourceMode, FRDGTextureRef Gate, FRDGTextureRef PreviousColor,
		const FGeneratorHeightColorRampRenderData& Ramp)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.HeightColorRamp"));
		TShaderMapRef<FMixtormatGeneratorHeightColorRampCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightColorRampCS::FParameters>();
		P->OutputSize = Size;
		P->StopCount = Ramp.StopCount;
		P->Interpolation = Ramp.Interpolation;
		P->Weight = Ramp.Weight;
		P->InputScale = Ramp.InputScale;
		P->InputOffset = Ramp.InputOffset;
		P->MirrorRepeat = Ramp.bMirrorRepeat ? 1u : 0u;
		for (int32 Index = 0; Index < FMixtormatColorRamp::MaxStops; ++Index)
		{
			P->Positions[Index] = FVector4f(Ramp.Positions[Index], 0.0f, 0.0f, 0.0f);
			P->Colors[Index] = Ramp.Colors[Index];
		}
		P->HasGate = Ramp.bHasGate;
		P->HasPreviousColor = PreviousColor != nullptr ? 1u : 0u;
		P->SourceMode = SourceMode;
		P->BlendMode = static_cast<uint32>(Ramp.BlendMode);
		P->FlowModulation = Ramp.FlowModulation;
		P->FlowModulationDir = Ramp.FlowModulationDir;
		P->UVModulation = Ramp.UVModulation;
		P->UVModulationDir = Ramp.UVModulationDir;
		P->IdVariation = Ramp.IdVariation;
		P->IdVariationSeed = Ramp.IdVariationSeed;
		P->HasModFlow = (SourceFlow != nullptr && FMath::Abs(Ramp.FlowModulation) > 1.0e-5f) ? 1u : 0u;
		P->HasModUV = (SourceUV != nullptr && FMath::Abs(Ramp.UVModulation) > 1.0e-5f) ? 1u : 0u;
		P->HasModIds = (SourceIds != nullptr && Ramp.IdVariation > 1.0e-5f) ? 1u : 0u;

		FRDGTextureRef NormalizedSource = SourceHeight;
		if (Ramp.bNormalizeInput && SourceHeight)
		{
			if (SourceMode == 0u)
			{
				NormalizedSource = AddSignedGeneratorHeightPasses(GraphBuilder, SourceHeight, Size, true,
					1.0f, 0.0f, false, nullptr, TEXT("Mixtormat.Generator.HeightColorRamp.Normalized"));
			}
			else if (SourceMode == 1u)
			{
				NormalizedSource = AddNormalizeFieldPasses(GraphBuilder, SourceHeight, Size, 0.0f, 1.0f,
					TEXT("Mixtormat.Generator.HeightColorRamp.Normalized01"));
			}
		}
		P->SourceHeight = NormalizedSource ? NormalizedSource : Gate;
		P->SourceFlow = SourceFlow ? SourceFlow : (PreviousColor ? PreviousColor : Ctx.OutputBC[0]);
		P->SourceUV = SourceUV ? SourceUV : Ctx.EmptyPatternUV;
		P->SourceIds = SourceIds ? SourceIds : Ctx.EmptyRegionIds;
		P->Gate = Gate;
		P->PreviousColor = PreviousColor ? PreviousColor : Ctx.OutputBC[0];
		P->OutColor = GraphBuilder.CreateUAV(Out);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightColorRamp"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Out;
	}

	FRDGTextureRef RemapBundleField(FMixtormatComposeContext& Ctx, FRDGTextureRef Field,
		FRDGTextureRef WarpedUV, const int32 Stage, FRDGTextureRef Boundary = nullptr,
		FRDGTextureRef SourceIds = nullptr, const float InvalidDistance = 0.0f)
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
		P->IdField = SourceIds;
		P->InvalidDistance = InvalidDistance;

		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		if (Stage == 1)
		{
			P->IdField = Field;
			P->OutIds = GraphBuilder.CreateUAV(Moved);
		}
		else if (Stage == 2 || Stage == 3 || Stage == 4 || Stage == 5 || Stage == 8)
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

	// Existing flow apply has already replaced Height/Coverage. Remap only companions
	// from one immutable producer revision; structural generation never calls this.
	void RemapGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle, FRDGTextureRef WarpedUV)
	{
		using ESemantic = FGeneratorBundle::EFieldSemantic;
		using EUnits = FGeneratorBundle::EFieldUnits;
		const FGeneratorBundle Source = Bundle;
		const FIntPoint Size = Ctx.Request.Resolution;
		const auto HasMapExtent = [Size](FRDGTextureRef Texture)
		{
			return !Texture || Texture->Desc.Extent == Size;
		};
		const auto HasScalarFormat = [](FRDGTextureRef Texture)
		{
			return Texture && (Texture->Desc.Format == PF_R16F || Texture->Desc.Format == PF_R32_FLOAT);
		};
		// Fixed slots have contracts too: storage alone cannot authorize an inverse or
		// reinterpret a generic Vector2/Noise output as an identity-winding map.
		if (!ensureMsgf(WarpedUV && Size.X > 0 && Size.Y > 0 && HasMapExtent(WarpedUV)
			&& WarpedUV->Desc.Format == PF_G32R32F
			&& HasMapExtent(Source.RegionIds) && HasMapExtent(Source.BoundaryField)
			&& HasMapExtent(Source.CentreUV) && HasMapExtent(Source.Orientation)
			&& (!Source.RegionIds || Source.RegionIds->Desc.Format == PF_R32_UINT)
			&& (!Source.BoundaryField || Source.BoundaryField->Desc.Format == PF_G32R32F)
			&& (!Source.CentreUV || (Source.CentreUV->Desc.Format == PF_G32R32F
				&& Source.CentreDescriptor.Semantic == ESemantic::LegacyLocalCentre
				&& Source.CentreDescriptor.Units == EUnits::MapUV))
			&& (!Source.Orientation || (HasScalarFormat(Source.Orientation)
				&& Source.OrientationDescriptor.Semantic == ESemantic::LegacyLocalOrientation
				&& Source.OrientationDescriptor.Units == EUnits::Radians)),
			TEXT("Unsupported generator companion extent, format or fixed-slot policy")))
		{
			return;
		}
		// Reject unsupported producers before scheduling any companion writes. In particular,
		// missing metadata must never implicitly enable Noise or generic Vector2 transport.
		for (const TPair<FName, FRDGTextureRef>& Mask : Source.NamedMasks)
		{
			const FGeneratorBundle::FFieldDescriptor* Descriptor = Source.NamedMaskDescriptors.Find(Mask.Key);
			bool bSupported = Descriptor && Mask.Value && HasMapExtent(Mask.Value);
			if (bSupported)
			{
				const bool bCoordinate = Descriptor->Semantic == ESemantic::LiftedCoordinateMap;
				bSupported = bCoordinate ? Mask.Value->Desc.Format == PF_G32R32F
					: HasScalarFormat(Mask.Value);
			}
			if (bSupported)
			{
				switch (Descriptor->Semantic)
				{
				case ESemantic::ContinuousAttribute:
					bSupported = Descriptor->Units == EUnits::Unitless || Descriptor->Units == EUnits::CrackCell;
					break;
				case ESemantic::RegionAttribute:
					bSupported = Source.RegionIds && Descriptor->Units == EUnits::Unitless;
					break;
				case ESemantic::BedCoordinate:
					bSupported = Source.RegionIds && Descriptor->Units == EUnits::BedFraction;
					break;
				case ESemantic::UvDistance:
					bSupported = Source.BoundaryField && Descriptor->Units == EUnits::MapUV
						&& FMath::IsFinite(Descriptor->InvalidDistance);
					break;
				case ESemantic::CoverageAlias:
					bSupported = HasMapExtent(Source.Coverage) && HasScalarFormat(Source.Coverage)
						&& Descriptor->Units == EUnits::Unitless;
					break;
				case ESemantic::LiftedCoordinateMap:
					bSupported = Descriptor->Units == EUnits::MapUV;
					break;
				default:
					bSupported = false;
					break;
				}
			}
			if (!ensureMsgf(bSupported, TEXT("Unsupported generator companion policy: %s"), *Mask.Key.ToString()))
			{
				return;
			}
		}

		FGeneratorBundle Moved = Source;
		if (Source.RegionIds) { Moved.RegionIds = RemapBundleField(Ctx, Source.RegionIds, WarpedUV, 1); }
		if (Source.CentreUV) { Moved.CentreUV = RemapBundleField(Ctx, Source.CentreUV, WarpedUV, 5); }
		if (Source.Orientation) { Moved.Orientation = RemapBundleField(Ctx, Source.Orientation, WarpedUV, 6); }
		for (const TPair<FName, FRDGTextureRef>& Mask : Source.NamedMasks)
		{
			const FGeneratorBundle::FFieldDescriptor& Descriptor = Source.NamedMaskDescriptors.FindChecked(Mask.Key);
			int32 Stage = INDEX_NONE;
			switch (Descriptor.Semantic)
			{
			case ESemantic::ContinuousAttribute: Stage = 0; break;
			case ESemantic::RegionAttribute: Stage = 10; break;
			case ESemantic::BedCoordinate: Stage = 11; break;
			case ESemantic::UvDistance: Stage = 7; break;
			case ESemantic::LiftedCoordinateMap: Stage = 3; break;
			case ESemantic::CoverageAlias:
				Moved.NamedMasks[Mask.Key] = Source.Coverage;
				continue;
			default: checkNoEntry(); return;
			}
			Moved.NamedMasks[Mask.Key] = RemapBundleField(Ctx, Mask.Value, WarpedUV, Stage,
				Source.BoundaryField, Source.RegionIds, Descriptor.InvalidDistance);
		}
		// Preserve a direct structural Strata boundary; only this later flow operation
		// pulls it back. All named-distance passes still read the old metric/validity.
		if (Source.BoundaryField)
		{
			Moved.BoundaryField = RemapBundleField(Ctx, Source.BoundaryField, WarpedUV, 2);
		}
		Bundle = MoveTemp(Moved);
	}

	// A completed-bundle operation owns every moved output. The signed height
	// supplied here may be native (V2 post-generation) or already normalized
	// (legacy structural pullback). Flow apply authors Height/Coverage separately.
	void RemapCompletedGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle,
		FRDGTextureRef CompletedSignedHeight, FRDGTextureRef WarpedUV,
		const bool bFilterHeight = false)
	{
		const FGeneratorBundle Source = Bundle;
		if (!CompletedSignedHeight) { return; }
		// Remap the caller's exact signed height snapshot, without introducing
		// any normalization here or silently converting its units.
		Bundle.Height = bFilterHeight
			? RemapBundleField(Ctx, CompletedSignedHeight, WarpedUV, 12)
			: RemapBundleField(Ctx, CompletedSignedHeight, WarpedUV, 0);
		if (Source.Coverage)
		{
			Bundle.Coverage = RemapBundleField(Ctx, Source.Coverage, WarpedUV, 0);
		}
		RemapGeneratorBundle(Ctx, Bundle, WarpedUV);
	}

	// Shared completed-bundle remap for both legacy Structural Warp and V2 Warp.
	// Generator-specific named outputs carry their own declared transport semantics.
	void RemapGeneratorModuleOutputs(FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer, const FChildRenderData& Owner,
		FGeneratorBundle& Module, FRDGTextureRef Coordinates,
		const bool bFilterHeight = false)
	{
		const FPublishedFieldKey ValueKey =
			PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("Value")));
		const FPublishedFieldKey GradientKey =
			PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("Gradient")));
		const FPublishedField* NoiseValue = Owner.Generator.Type == EMixtormatGeneratorType::Noise
			? Ctx.PublishedFieldOutputs.Find(ValueKey) : nullptr;
		const FPublishedField* NoiseGradient = Owner.Generator.Type == EMixtormatGeneratorType::Noise
			? Ctx.PublishedFieldOutputs.Find(GradientKey) : nullptr;
		const FPublishedField ValueSnapshot = NoiseValue ? *NoiseValue : FPublishedField{};
		const FPublishedField GradientSnapshot = NoiseGradient ? *NoiseGradient : FPublishedField{};
		RemapCompletedGeneratorBundle(Ctx, Module, Module.Height, Coordinates, bFilterHeight);
		if (ValueSnapshot.IsComplete())
		{
			Ctx.PublishedFieldOutputs.Add(ValueKey, FPublishedField{
				ValueSnapshot.Kind, RemapBundleField(Ctx, ValueSnapshot.Texture, Coordinates, 0),
				nullptr, nullptr, false});
		}
		if (Module.GradientDescriptor.Semantic == FGeneratorBundle::EFieldSemantic::SourceFrameVector
			&& Module.GradientDescriptor.Units == FGeneratorBundle::EFieldUnits::GeneratorDomain
			&& GradientSnapshot.Texture
			&& GradientSnapshot.Texture->Desc.Extent == Ctx.Request.Resolution
			&& GradientSnapshot.Texture->Desc.Format == PF_G32R32F)
		{
			Ctx.PublishedFieldOutputs.Add(GradientKey, FPublishedField{
				GradientSnapshot.Kind, RemapBundleField(Ctx, GradientSnapshot.Texture, Coordinates, 4),
				nullptr, nullptr, false});
		}
	}

	template<typename TParameters>
	void SetBehaviorScalarDrivers(FMixtormatComposeContext& Ctx,
		TParameters* P, const FScalarDriverRenderData* Drivers)
	{
		FRDGTextureRef Signals[2] = { Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal };
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FScalarDriverRenderData& Driver = Drivers[Slot];
			FRDGTextureRef Signal = nullptr;
			if (Driver.bEnabled)
			{
				if (FRDGTextureRef* Found = Ctx.DriverSnapshots.Find(Driver.SourceLayerId))
				{
					Signal = *Found;
				}
			}
			const bool bResolved = Signal != nullptr;
			Signals[Slot] = bResolved ? Signal : Ctx.EmptyDriverSignal;
			P->BehaviorDriverParamsA[Slot] = FVector4f(
				bResolved ? 1.0f : 0.0f, Driver.bInvert ? 1.0f : 0.0f,
				Driver.InputMin, Driver.InputMax);
			P->BehaviorDriverParamsB[Slot] = FVector4f(
				Driver.OutputMin, Driver.OutputMax, Driver.Amount,
				static_cast<float>(Driver.Combine));
		}
		P->BehaviorDriverSignal0 = Signals[0];
		P->BehaviorDriverSignal1 = Signals[1];
	}

	// A lifted UVMap carries displacement in destination texel space; interpolate
	// that displacement (not wrapped absolute coordinates) to preserve tile winding.
	FRDGTextureRef ScaleBehaviorUV(FMixtormatComposeContext& Ctx,
		FRDGTextureRef Source, const float Strength, FRDGTextureRef Mask,
		const bool bUseMask, FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex,
		const FScalarDriverRenderData* Drivers,
		const FBehaviorFieldComposition& Composition)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		// A neutral socket must not cost a deformation pass: identity amplitude and no reverse
		// reproduce the source map exactly, so only composition beyond the default
		// joins Strength, the masks and the drivers as reasons to run.
		const bool bCompositionNeutral = Composition.Amplitude == 1.0f
			&& Composition.bReversed == 0;
		if (Strength == 1.0f && !bUseMask && !bUseInfluence
			&& !Drivers[0].bEnabled && bCompositionNeutral) { return Source; }
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.WarpCoordinates"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorUvBlendCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Strength;
		P->FieldAmplitude = Composition.Amplitude;
		P->FieldReversed = Composition.bReversed;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceCoordinates = Source;
		SetBehaviorScalarDrivers(Ctx, P, Drivers);
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorUvBlendCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.WarpUV.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef MixBehaviorWarpCoordinates(FMixtormatComposeContext& Ctx,
		FRDGTextureRef Standard, FRDGTextureRef Traced,
		const FBehaviorRenderData& Behavior, const int32 LayerIndex, const int32 ChildIndex)
	{
		const float Mix = FMath::Clamp(Behavior.Flow.ModeMix, 0.0f, 1.0f);
		const FScalarDriverRenderData& Driver = Behavior.Flow.SettingsDrivers[8];
		if (!Driver.bEnabled)
		{
			if (Mix <= 0.0f) { return Standard; }
			if (Mix >= 1.0f) { return Traced; }
		}
		if (!Standard && !Traced) { return nullptr; }
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.ModeMixUV"));
		TShaderMapRef<FMixtormatBehaviorModeMixCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorModeMixCS::FParameters>();
		P->OutputSize = Size;
		P->ModeMix = Mix;
		P->HasStandard = Standard ? 1u : 0u;
		P->HasTraced = Traced ? 1u : 0u;
		P->StandardCoordinates = Standard ? Standard : (Traced ? Traced : Ctx.EmptyPatternUV);
		P->TracedCoordinates = Traced ? Traced : (Standard ? Standard : Ctx.EmptyPatternUV);
		FRDGTextureRef* Signal = Driver.bEnabled ? Ctx.DriverSnapshots.Find(Driver.SourceLayerId) : nullptr;
		const bool bResolved = Signal != nullptr;
		P->ModeMixDriverSignal = bResolved ? *Signal : Ctx.EmptyDriverSignal;
		P->ModeMixDriverParamsA = FVector4f(bResolved ? 1.0f : 0.0f,
			Driver.bInvert ? 1.0f : 0.0f, Driver.InputMin, Driver.InputMax);
		P->ModeMixDriverParamsB = FVector4f(Driver.OutputMin, Driver.OutputMax, Driver.Amount,
			static_cast<float>(Driver.Combine));
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.ModeMix.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef MakeBehaviorHeightGradientUV(FMixtormatComposeContext& Ctx,
		FRDGTextureRef SourceHeight, const float Strength, const float Reach,
		const bool bSmoothHeightGradient,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex,
		const FScalarDriverRenderData* Drivers,
		const FBehaviorFieldComposition& Composition)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.HeightGradientUV"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorHeightGradientCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Strength;
		P->FieldAmplitude = Composition.Amplitude;
		P->FieldReversed = Composition.bReversed;
		P->GradientReach = Reach;
		P->SmoothHeightGradient = bSmoothHeightGradient ? 1u : 0u;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceHeight = SourceHeight;
		SetBehaviorScalarDrivers(Ctx, P, Drivers);
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorHeightGradientCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.HeightGradient.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef AddBehaviorSignedPushPass(FMixtormatComposeContext& Ctx,
		FRDGTextureRef BaseHeight, FRDGTextureRef DeltaHeight, const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.SignedPush"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorSignedPushCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Behavior.Strength;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceHeight = BaseHeight;
		P->BehaviorHeight = DeltaHeight;
		P->FieldAmplitude = Behavior.HeightComposition.Amplitude;
		P->FieldReversed = Behavior.HeightComposition.bReversed;
		P->FieldBlend = Behavior.HeightComposition.Blend;
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		P->OutSignedHeight = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorSignedPushCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.Push.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}


	// One traced, signed-Height Sculpt operation. The former Carve enum remains a
	// serialized identifier until the editor handoff; no SDF or private Flow solve.
	FRDGTextureRef AddBehaviorSculptPass(FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer, FRDGTextureRef Height,
		FRDGTextureRef Flow, FRDGTextureRef FlowValidity,
		const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Behavior.Sculpt.Height"));
		FRDGTextureRef SculptMask = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R16F, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Behavior.Sculpt.Mask"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorSculptCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Behavior.Strength;
		P->Depth = Behavior.Flow.Depth;
		P->Width = FMath::Max(0.0f, Behavior.Flow.Width);
		P->TraceLength = FMath::Max(0.0f, Behavior.Flow.TraceLength);
		P->TraceSteps = FMath::Clamp(Behavior.Flow.TraceSteps, 1, 256);
		P->Falloff = FMath::Max(0.0f, Behavior.Flow.Falloff);
		P->UseMask = bUseMask ? 1u : 0u;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->SourceHeight = Height;
		P->WorkingFlow = Flow;
		P->WorkingFlowValidity = FlowValidity;
		P->ScopedMask = Mask ? Mask : Ctx.EmptyDriverSignal;
		P->InfluenceField = Influence ? Influence : Ctx.EmptyDriverSignal;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		// Traced length and signed Depth retain their independent parameter drivers.
		const int32 Slots[2] = {1, 3};
		FRDGTextureRef Signals[2] = {Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal};
		for (int32 I = 0; I < 2; ++I)
		{
			const FScalarDriverRenderData& Driver = Behavior.Flow.SettingsDrivers[Slots[I]];
			FRDGTextureRef* Snapshot = Driver.bEnabled
				? Ctx.DriverSnapshots.Find(Driver.SourceLayerId) : nullptr;
			const bool bResolved = Snapshot != nullptr;
			Signals[I] = bResolved ? *Snapshot : Ctx.EmptyDriverSignal;
			P->SculptDriverParamsA[I] = FVector4f(
				bResolved ? 1.0f : 0.0f, Driver.bInvert ? 1.0f : 0.0f,
				Driver.InputMin, Driver.InputMax);
			P->SculptDriverParamsB[I] = FVector4f(
				Driver.OutputMin, Driver.OutputMax, Driver.Amount,
				static_cast<float>(Driver.Combine));
		}
		P->SculptDriverSignal0 = Signals[0];
		P->SculptDriverSignal1 = Signals[1];
		P->OutHeight = Ctx.GraphBuilder.CreateUAV(Result);
		P->OutSculptMask = Ctx.GraphBuilder.CreateUAV(SculptMask);
		TShaderMapRef<FMixtormatBehaviorSculptCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.Sculpt.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, ChildIndex, FName(TEXT("CarveMask"))),
			FPublishedField{EMixtormatPublishedFieldKind::Scalar01, SculptMask, nullptr, nullptr, false});
		return Result;
	}

	// Shared, ordered post-generation operations consume the owning native bundle.
	FRDGTextureRef BuildGeneratorPreCoordinates(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Owner)
	{
		FRDGTextureRef Current = nullptr;
		const FIntPoint Size = Ctx.Request.Resolution;
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Behavior
				|| Child.Behavior.GeneratorChildIndex != Owner.SourceChildIndex
				|| Child.Behavior.Stage != EMixtormatBehaviorStage::PreGeneration
				|| Child.Behavior.Type != EMixtormatBehaviorType::Warp
				|| Child.Behavior.DirectionOrigin != EMixtormatBehaviorFieldOrigin::PublishedOutput) { continue; }
			const FOutputReferenceRenderData& Ref = Child.Behavior.Direction;
			const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Ref.Source);
			if (!Field || !Field->IsComplete() || Field->Kind != Ref.Kind
				|| Field->Texture->Desc.Extent != Size) { continue; }
			FRDGTextureRef Influence = LayerCtx.CombinedMask;
			const bool bInfluence = Child.Behavior.bHasInfluence;
			if (bInfluence)
			{
				const FPublishedField* I = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Influence.Source);
				if (!I || !I->IsComplete() || I->Kind != EMixtormatPublishedFieldKind::Scalar01
					|| I->Texture->Desc.Extent != Size) { continue; }
				Influence = I->Texture;
			}
			FRDGTextureRef UV = nullptr;
			if (Ref.Kind == EMixtormatPublishedFieldKind::UVMap) { UV = Field->Texture; }
			else if (Ref.Kind == EMixtormatPublishedFieldKind::Flow)
			{
				if ((Ref.FlowAmount == 0.0f && !Child.Behavior.FlowDrivers[0].bEnabled)
					|| (Ref.FlowTraceLength == 0.0f && !Child.Behavior.FlowDrivers[1].bEnabled)) { continue; }
				UV = AddReferencedFlowUVPass(Ctx, Ref, *Field, LayerCtx.LayerIndex,
					Child.SourceChildIndex, Child.Behavior.FlowDrivers);
			}
			if (!UV) { continue; }
			const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: LayerCtx.CombinedMask;
			UV = ScaleBehaviorUV(Ctx, UV, Child.Behavior.Strength, Gate, bMask, Influence,
				bInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
				Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
			Current = Current ? RemapBundleField(Ctx, Current, UV, 3) : UV;
			Ctx.PublishedFieldOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
				FPublishedField{EMixtormatPublishedFieldKind::UVMap, Current, nullptr, nullptr, false});
		}
		return Current;
	}

	FPublishedField AddFlowFeatherPasses(FMixtormatComposeContext& Ctx,
		const FPublishedField& Source, const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask, const bool bHasMask, const int32 LayerIndex, const int32 ChildIndex)
	{
		if (!Source.IsComplete() || Source.Kind != EMixtormatPublishedFieldKind::Flow
			|| (Behavior.Strength == 0.0f && !Behavior.ScalarDrivers[0].bEnabled)
			|| Behavior.FeatherRadius <= 0.0f)
		{
			return Source;
		}
		FRDGBuilder& Graph = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		if (Source.Texture->Desc.Extent != Size) { return Source; }
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8),
			FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const auto NewScalar = [&Graph, Size](const TCHAR* Name)
		{
			return Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, PF_R32_FLOAT,
				FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		const bool bEikonal = Behavior.FeatherMode == EMixtormatBehaviorFeatherMode::Eikonal;
		FRDGTextureRef Arrival[2] = {nullptr, nullptr};
		FRDGTextureRef GaussianFlow = nullptr;
		int32 Read = 0;
		const auto Dispatch = [&](int32 Stage, FRDGTextureRef Out, FRDGTextureRef InArrival)
		{
			FMixtormatBehaviorFlowFeatherCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatBehaviorFlowFeatherCS::FStage>(Stage);
			TShaderMapRef<FMixtormatBehaviorFlowFeatherCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = Graph.AllocParameters<FMixtormatBehaviorFlowFeatherCS::FParameters>();
			P->OutputSize = Size;
			P->Radius = Behavior.FeatherRadius;
			P->Strength = Behavior.Strength;
			P->DirectionBias = Behavior.FeatherDirectionBias;
			P->HasMask = bHasMask && Mask ? 1u : 0u;
			P->SourceFlow = Source.Texture;
			P->GaussianFlow = Stage == 0 ? GaussianFlow : nullptr;
			P->Validity = Source.Validity;
			P->MaskField = bHasMask && Mask ? Mask : Ctx.EmptyDriverSignal;
			P->Arrival = InArrival ? InArrival : Ctx.EmptyDriverSignal;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
			if (Stage == 0 || Stage == 3 || Stage == 4) { P->OutFlow = Graph.CreateUAV(Out); }
			else { P->OutArrival = Graph.CreateUAV(Out); }
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(Graph,
				RDG_EVENT_NAME("Mixtormat.FlowFeather.L%d.C%d.Stage%d", LayerIndex, ChildIndex, Stage),
				Shader, P, Groups);
		};
		if (bEikonal)
		{
			Arrival[0] = NewScalar(TEXT("Mixtormat.FlowFeather.ArrivalA"));
			Arrival[1] = NewScalar(TEXT("Mixtormat.FlowFeather.ArrivalB"));
			Dispatch(1, Arrival[0], nullptr);
			const int32 Iterations = ResolveFeatherIterations(Behavior.FeatherIterations);
			for (int32 Pass = 0; Pass < Iterations; ++Pass)
			{
				Dispatch(2, Arrival[1 - Read], Arrival[Read]);
				Read = 1 - Read;
			}
		}
		if (!bEikonal)
		{
			GaussianFlow = Graph.CreateTexture(FRDGTextureDesc::Create2D(
				Size, PF_A32B32G32R32F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.FlowFeather.GaussianX"));
			Dispatch(4, GaussianFlow, nullptr);
		}
		FRDGTextureRef OutFlow = Graph.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.FlowFeather.Filtered"));
		Dispatch(bEikonal ? 3 : 0, OutFlow, bEikonal ? Arrival[Read] : nullptr);
		return FPublishedField{EMixtormatPublishedFieldKind::Flow,
			OutFlow, OutFlow, Source.Validity, false};
	}

	FRDGTextureRef AddGeneratorFeatherPasses(FMixtormatComposeContext& Ctx,
		FRDGTextureRef Height, FRDGTextureRef Boundary,
		const FBehaviorRenderData& Behavior, FRDGTextureRef Mask, const bool bHasMask,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		if (!Height || Height->Desc.Format != PF_R32_FLOAT
			|| (Behavior.Strength == 0.0f && !Behavior.ScalarDrivers[0].bEnabled)
			|| Behavior.FeatherRadius <= 0.0f)
		{
			return Height;
		}
		FRDGBuilder& Graph = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const bool bBoundary = Boundary && Boundary->Desc.Extent == Size
			&& Boundary->Desc.Format == PF_G32R32F;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8),
			FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const auto Make = [&Graph, Size](const TCHAR* Name, EPixelFormat Format)
		{
			return Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, Format,
				FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		// Height Feather is Eikonal-only; Gaussian Blur is handled by the dedicated Blur module.
		constexpr bool bEikonal = true;
		FRDGTextureRef GaussianHeight = nullptr;
		const auto Dispatch = [&](const int32 Stage, FRDGTextureRef Output,
			FRDGTextureRef InputEnvelope, const int32 Jump)
		{
			FMixtormatBehaviorFeatherCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatBehaviorFeatherCS::FStage>(Stage);
			TShaderMapRef<FMixtormatBehaviorFeatherCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = Graph.AllocParameters<FMixtormatBehaviorFeatherCS::FParameters>();
			P->OutputSize = Size;
			P->Radius = Behavior.FeatherRadius;
			P->Strength = Behavior.Strength;
			SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
			P->DirectionBias = Behavior.FeatherDirectionBias;
			P->Side = static_cast<uint32>(Behavior.FeatherSide);
			P->HasBoundary = bBoundary ? 1u : 0u;
			P->HasMask = bHasMask && Mask ? 1u : 0u;
			P->SourceHeight = Height;
			P->GaussianHeight = Stage == 0 ? GaussianHeight : nullptr;
			P->BoundaryField = bBoundary ? Boundary : Ctx.EmptyPatternUV;
			P->MaskField = bHasMask && Mask ? Mask : Ctx.EmptyDriverSignal;
			P->Arrival = Ctx.EmptyDriverSignal;
			P->FeatherEnvelope = InputEnvelope ? InputEnvelope : Ctx.EmptyPatternUV;
			P->FeatherJump = Jump;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			if (Stage == 0 || Stage == 7) { P->OutHeight = Graph.CreateUAV(Output); }
			else { P->OutFeatherEnvelope = Graph.CreateUAV(Output); }
			ClearUnusedGraphResources(Shader, P);
			const FIntVector DispatchGroups = Stage == 4
				? FIntVector(FMath::DivideAndRoundUp(Size.Y, 64), 1, 1)
				: Stage == 5
					? FIntVector(FMath::DivideAndRoundUp(Size.X, 64), 1, 1)
					: Groups;
			FComputeShaderUtils::AddPass(Graph,
				RDG_EVENT_NAME("Mixtormat.Feather.L%d.C%d.Stage%d", LayerIndex, ChildIndex, Stage),
				Shader, P, DispatchGroups);
		};
		FRDGTextureRef Out = Make(TEXT("Mixtormat.Feather.NativeHeight"), PF_R32_FLOAT);
		if (!bEikonal)
		{
			GaussianHeight = Make(TEXT("Mixtormat.Feather.GaussianX"), PF_G32R32F);
			Dispatch(8, GaussianHeight, nullptr, 0);
			Dispatch(0, Out, nullptr, 0);
			return Out;
		}
		// A complete pair of cyclic line sweeps covers the entire wrapped tile.
		// Diagonal jump refinements improve the Euclidean slope without an O(radius)
		// Jacobi chain. The authored iteration budget controls refinement quality.
		FRDGTextureRef Envelope[2] = {
			Make(TEXT("Mixtormat.Feather.EnvelopeA"), PF_G32R32F),
			Make(TEXT("Mixtormat.Feather.EnvelopeB"), PF_G32R32F)
		};
		Dispatch(4, Envelope[0], nullptr, 0);
		Dispatch(5, Envelope[1], Envelope[0], 0);
		int32 Read = 1;
		const int32 Budget = ResolveFeatherIterations(Behavior.FeatherIterations);
		const int32 MaxJump = FMath::Max(1, FMath::Min(
			FMath::CeilToInt(Behavior.FeatherRadius), FMath::Max(Size.X, Size.Y) / 2));
		int32 Jump = 1;
		while (Jump <= MaxJump / 2) { Jump *= 2; }
		int32 Corrections = 0;
		for (; Jump >= 1 && Corrections < Budget; Jump /= 2, ++Corrections)
		{
			Dispatch(6, Envelope[1 - Read], Envelope[Read], Jump);
			Read = 1 - Read;
		}
		for (int32 I = 0; I < FMath::Min(Budget - Corrections, 2); ++I)
		{
			Dispatch(6, Envelope[1 - Read], Envelope[Read], 1);
			Read = 1 - Read;
		}
		Dispatch(7, Out, Envelope[Read], 0);
		return Out;
	}

	void ApplyGeneratorPostBehaviors(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Owner, FGeneratorBundle& Module, FRDGTextureRef PreviousRunningHeight)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef WorkingFlow = Ctx.GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, PF_FloatRGBA, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Generator.WorkingFlow"));
		AddClearUAVPass(Ctx.GraphBuilder, Ctx.GraphBuilder.CreateUAV(WorkingFlow), FLinearColor::Black);
		FRDGTextureRef WorkingFlowValidity = nullptr;
		// Intrinsic Noise supplies a unit downhill vector; other generators can
		// supply the same explicit unit-magnitude fallback from native height.
		// The original directional publication is never added to itself twice.
		const FPublishedField* Intrinsic = Ctx.PublishedFieldOutputs.Find(
			PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("FlowDirection"))));
		if (Intrinsic && Intrinsic->IsComplete()
			&& Intrinsic->Kind == EMixtormatPublishedFieldKind::Flow
			&& Intrinsic->Texture->Desc.Extent == Size)
		{
			WorkingFlow = Intrinsic->Texture;
			WorkingFlowValidity = Intrinsic->Validity;
		}
		const auto PublishFlowSnapshot = [&](const int32 ChildIndex)
		{
			if (!WorkingFlow || !WorkingFlowValidity) { return; }
			Ctx.PublishedFieldOutputs.Add(
				PublishedKey(Layer, ChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow),
				FPublishedField{EMixtormatPublishedFieldKind::Flow, WorkingFlow,
					WorkingFlow, WorkingFlowValidity, false});
		};
		ON_SCOPE_EXIT { PublishFlowSnapshot(Owner.SourceChildIndex); };
		for (const FChildRenderData& Child : Layer.Children)
		{
			const bool bScopedNoise = Child.Type == EMixtormatLayerChildType::Generator
				&& Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex
				&& Child.Generator.Type == EMixtormatGeneratorType::Noise;
			const bool bScopedBehavior = Child.Type == EMixtormatLayerChildType::Behavior
				&& Child.Behavior.GeneratorChildIndex == Owner.SourceChildIndex
				&& Child.Behavior.Stage == EMixtormatBehaviorStage::PostGeneration;
			bool bPublishWorkingFlow = true;
			// Capture after processing, including early continue paths for consumers such as Sculpt.
			// Existing RDG textures are immutable revisions; no diagnostic copy/allocation is needed.
			ON_SCOPE_EXIT
			{
				if (bPublishWorkingFlow && (bScopedNoise || bScopedBehavior))
								{
									PublishFlowSnapshot(Child.SourceChildIndex);
								}
			};
			if (bScopedNoise)
			{
				FMixtormatNoiseRenderData Noise;
				if (!MixtormatNoiseRenderStore().Find(
					FMixtormatNoiseRenderKey{Layer.LayerId, Child.SourceChildIndex}, Noise)
					|| (!Noise.bWriteFlow && !Noise.bWriteHeight)) { continue; }
				FRDGTextureRef ParentUV = nullptr;
				if (const FPublishedField* RootUVField = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("RootUV")))))
				{
					if (RootUVField->IsComplete() && RootUVField->Kind == EMixtormatPublishedFieldKind::UVMap)
					{
						ParentUV = RootUVField->Texture;
					}
				}
				if (!ParentUV)
				{
					if (const FPublishedField* WarpedField = Ctx.PublishedFieldOutputs.Find(
						PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("WarpedUV")))))
					{
						if (WarpedField->IsComplete() && WarpedField->Kind == EMixtormatPublishedFieldKind::UVMap)
						{
							ParentUV = WarpedField->Texture;
						}
					}
				}
				FGeneratorBundle ScopedBundle;
				AddNoisePasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, &ScopedBundle,
					ParentUV, Module.Height);
				// Scoped Noise generators are evaluated inside their parent's Behavior
				// chain, not in the outer generator loop. Apply their own Feather
				// children here before height composition and Flow consumption.
				for (const FChildRenderData& Filter : Layer.Children)
				{
					if (Filter.Type == EMixtormatLayerChildType::Blur
						&& Filter.ScopeOwnerSourceChildIndex == Child.SourceChildIndex
						&& Filter.SourceChildIndex > Child.SourceChildIndex)
					{
						if (Filter.Blur.BlursAnything() && ScopedBundle.Height)
						{
							const bool bFilterMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
							FRDGTextureRef FilterGate = bFilterMask
								? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
								: nullptr;
							MixtormatGaussianBlur::FSettings Settings;
							Settings.RadiusX = Filter.Blur.GetRadiusX();
							Settings.RadiusY = Filter.Blur.GetRadiusY();
							Settings.Amount = Filter.Blur.Amount;
							ScopedBundle.Height = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
								ScopedBundle.Height, Settings, MixtormatGaussianBlur::EField::Scalar, FilterGate);
						}
						continue;
					}
					if (Filter.Type != EMixtormatLayerChildType::Behavior
						|| Filter.Behavior.Type != EMixtormatBehaviorType::Feather
						|| Filter.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
						|| Filter.Behavior.GeneratorChildIndex != Child.SourceChildIndex
						|| Filter.SourceChildIndex <= Child.SourceChildIndex) { continue; }
					const bool bFilterMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
					FRDGTextureRef FilterGate = bFilterMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
						: nullptr;
					if (ScopedBundle.Height)
					{
						ScopedBundle.Height = AddGeneratorFeatherPasses(Ctx, ScopedBundle.Height,
							ScopedBundle.BoundaryField, Filter.Behavior, FilterGate, bFilterMask,
							LayerCtx.LayerIndex, Filter.SourceChildIndex);
					}
					for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
					{
						const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
						const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
						if (!Field || !Field->IsComplete()
							|| Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
						const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
							Filter.Behavior, FilterGate, bFilterMask,
							LayerCtx.LayerIndex, Filter.SourceChildIndex);
						Ctx.PublishedFieldOutputs.Add(Key, Filtered);
						// Match Noise's E choice: generated contribution when published, otherwise direction.
						if (Name == FName(TEXT("GeneratedFlow"))
							|| !Ctx.PublishedFieldOutputs.Contains(PublishedKey(Layer,
								Child.SourceChildIndex, FName(TEXT("GeneratedFlow")))))
						{
							Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
								Filter.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);

						}

					}
					const FPublishedFieldKey SnapshotKey = PublishedKey(Layer,
						Filter.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow);
					if (Ctx.PublishedFieldDemand.Contains(SnapshotKey))
					{
						FRDGTextureRef Snapshot = WorkingFlow;
						FRDGTextureRef SnapshotValidity = WorkingFlowValidity;
						if (Noise.bWriteFlow)
						{
							const FPublishedField* Contribution = Ctx.PublishedFieldOutputs.Find(
								PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GeneratedFlow"))));
							if (Contribution && Contribution->IsComplete())
							{
								const bool bScopedMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
								FRDGTextureRef ScopedMask = bScopedMask ? AddScopedFeatureMask(Ctx,
									LayerCtx, Layer, Child.SourceChildIndex, true) : nullptr;
								Snapshot = AddNoiseFlowComposePass(Ctx, WorkingFlow, Contribution->Texture,
									ScopedMask, Noise.FlowAdd, Noise.FlowMix, SnapshotValidity,
									TEXT("Mixtormat.Diagnostic.FlowFeatherSnapshot"));
							}
						}
						if (Snapshot && SnapshotValidity)
						{
							Ctx.PublishedFieldOutputs.Add(SnapshotKey,
								FPublishedField{EMixtormatPublishedFieldKind::Flow, Snapshot,
									Snapshot, SnapshotValidity, false});
						}
					}
				}
				if (ScopedBundle.Height && Module.Height)
				{
					const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef HeightMask = bMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: ((Owner.Generator.Type == EMixtormatGeneratorType::RootNetwork || Owner.Generator.Type == EMixtormatGeneratorType::Scales) ? Module.Coverage : nullptr);
					ScopedBundle.Height = AddSignedGeneratorHeightPasses(Ctx.GraphBuilder,
						ScopedBundle.Height, Size, Child.Generator.bNormalizeHeight,
						Child.Generator.HeightScale, Child.Generator.HeightBias, true, HeightMask,
						TEXT("Mixtormat.Generator.ScopedNoiseHeight"));
					AddGeneratorModuleCombine(Ctx, Module.Height, ScopedBundle, Child, Module.Height);
				}
				if (!Noise.bWriteFlow) { continue; }
				const FPublishedField* Generated = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GeneratedFlow"))));
				if (!Generated || !Generated->IsComplete()
					|| Generated->Kind != EMixtormatPublishedFieldKind::Flow
					|| Generated->Texture->Desc.Extent != Size) { continue; }
				const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Mask = bMasked ? AddScopedFeatureMask(Ctx, LayerCtx, Layer,
					Child.SourceChildIndex, true) : nullptr;
				WorkingFlow = AddNoiseFlowComposePass(Ctx, WorkingFlow, Generated->Texture, Mask,
					Noise.FlowAdd, Noise.FlowMix, WorkingFlowValidity,
					TEXT("Mixtormat.Generator.AccumulatedFlow"));

				continue;
			}

			if (Child.Type == EMixtormatLayerChildType::Blur)
			{
				const bool bDirect = Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex;
				const FChildRenderData* Parent = bDirect ? nullptr
					: Layer.Children.FindByPredicate([&](const FChildRenderData& Candidate)
					{
						return Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
							&& Candidate.SourceChildIndex < Child.SourceChildIndex
							&& (Candidate.Type == EMixtormatLayerChildType::Behavior
								|| Candidate.Type == EMixtormatLayerChildType::Generator);
					});
				if (!bDirect && !Parent) { continue; }
				if (Child.Blur.BlursAnything() && Module.Height)
				{
					const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef Gate = bMasked
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: nullptr;
					MixtormatGaussianBlur::FSettings Settings;
					Settings.RadiusX = Child.Blur.GetRadiusX();
					Settings.RadiusY = Child.Blur.GetRadiusY();
					Settings.Amount = Child.Blur.Amount;
					Module.Height = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
						Module.Height, Settings, MixtormatGaussianBlur::EField::Scalar, Gate);
				}
				continue;
			}

			if (Child.Type != EMixtormatLayerChildType::Behavior
				|| Child.Behavior.GeneratorChildIndex != Owner.SourceChildIndex
				|| Child.Behavior.Stage != EMixtormatBehaviorStage::PostGeneration) { continue; }
			if (Child.Behavior.Type == EMixtormatBehaviorType::Feather)
			{
				const FChildRenderData* FlowParent = Layer.Children.FindByPredicate(
					[&](const FChildRenderData& Candidate)
					{
						return Candidate.Type == EMixtormatLayerChildType::Behavior
							&& Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
							&& Candidate.SourceChildIndex < Child.SourceChildIndex
							&& Candidate.Behavior.Type == EMixtormatBehaviorType::FlowField
							&& Candidate.Behavior.GeneratorChildIndex == Owner.SourceChildIndex;
					});
				if (FlowParent)
				{
					const FPublishedFieldKey Key = PublishedKey(Layer,
						FlowParent->SourceChildIndex, FName(TEXT("FlowDirection")));
					const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
					if (Field && Field->IsComplete()
						&& Field->Kind == EMixtormatPublishedFieldKind::Flow)
					{
						const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
						FRDGTextureRef Gate = bMask
							? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
							: nullptr;
						const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
							Child.Behavior, Gate, bMask, LayerCtx.LayerIndex, Child.SourceChildIndex);
						Ctx.PublishedFieldOutputs.Add(Key, Filtered);
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Child.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
						bPublishWorkingFlow = false;
					}
					continue;
				}
				// A filter under a generator runs after preceding native-height operations.
				// A filter under Warp/Push/Carve/Deform acts immediately after its
				// parent and leaves IDs, boundary distance and named fields unchanged.
				const bool bDirect = Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex;
				const FChildRenderData* Parent = bDirect ? nullptr
					: Layer.Children.FindByPredicate([&](const FChildRenderData& Candidate)
					{
						return Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
							&& Candidate.SourceChildIndex < Child.SourceChildIndex
							&& Candidate.Type == EMixtormatLayerChildType::Behavior
							&& Candidate.Behavior.GeneratorChildIndex == Owner.SourceChildIndex
							&& (Candidate.Behavior.Type == EMixtormatBehaviorType::Warp
								|| Candidate.Behavior.Type == EMixtormatBehaviorType::Deform
								|| Candidate.Behavior.Type == EMixtormatBehaviorType::Push
								|| Candidate.Behavior.Type == EMixtormatBehaviorType::Carve);
					});
				if (!bDirect && !Parent) { continue; }
				const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bMasked
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: nullptr;
				Module.Height = AddGeneratorFeatherPasses(Ctx, Module.Height, Module.BoundaryField,
					Child.Behavior, Gate, bMasked, LayerCtx.LayerIndex, Child.SourceChildIndex);
				if (bDirect && Owner.Generator.Type == EMixtormatGeneratorType::Noise
					&& WorkingFlowValidity)
				{
					// Later siblings consume this snapshot; final Flow publication is separate.
					const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
						WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
					const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, FlowField,
						Child.Behavior, Gate, bMasked, LayerCtx.LayerIndex, Child.SourceChildIndex);
					WorkingFlow = Filtered.Texture;
					WorkingFlowValidity = Filtered.Validity;
				}
				continue;
			}
			if (Child.ScopeOwnerSourceChildIndex != Owner.SourceChildIndex)
			{
				continue;
			}
			if (Child.Behavior.Type != EMixtormatBehaviorType::Warp
				&& Child.Behavior.Type != EMixtormatBehaviorType::Push
				&& Child.Behavior.Type != EMixtormatBehaviorType::Carve
				&& Child.Behavior.Type != EMixtormatBehaviorType::Deform) { continue; }
			const FOutputReferenceRenderData& Ref = Child.Behavior.Direction;
			// A connected Influence that is unavailable must NOT turn into full strength.
			FRDGTextureRef Influence = LayerCtx.CombinedMask;
			const bool bUseInfluence = Child.Behavior.bHasInfluence;
			if (bUseInfluence)
			{
				const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Influence.Source);
				if (!Field || !Field->IsComplete()
					|| Field->Kind != EMixtormatPublishedFieldKind::Scalar01
					|| Field->Texture->Desc.Extent != Size
					|| (Field->Texture->Desc.Format != PF_R16F
						&& Field->Texture->Desc.Format != PF_R32_FLOAT))
				{
					continue;
				}
				Influence = Field->Texture;
			}
			if (Child.Behavior.Type == EMixtormatBehaviorType::Carve)
			{
				// Sculpt consumes the ordered working Flow, and gathers current Height.
				// The SDF remains available to separate masks/color/geometry consumers.
				if (!Module.Height || !WorkingFlowValidity) { continue; }
				const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bHasMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: LayerCtx.CombinedMask;
				Module.Height = AddBehaviorSculptPass(Ctx, Layer, Module.Height,
					WorkingFlow, WorkingFlowValidity, Child.Behavior, Gate, bHasMask,
					Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex);
				continue;
			}
			if (Child.Behavior.Type == EMixtormatBehaviorType::Push)
			{
				FRDGTextureRef Delta = nullptr;
				switch (Child.Behavior.HeightOrigin)
				{
				case EMixtormatBehaviorFieldOrigin::OwnNativeHeight:
					Delta = Module.Height;
					break;
				case EMixtormatBehaviorFieldOrigin::PreviousRunningHeight:
					Delta = PreviousRunningHeight;
					break;
				case EMixtormatBehaviorFieldOrigin::PublishedOutput:
				{
					const FPublishedField* HeightField = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Height.Source);
					if (HeightField && HeightField->IsComplete()
						&& HeightField->Kind == EMixtormatPublishedFieldKind::ScalarSigned
						&& HeightField->Texture->Desc.Extent == Size)
					{
						Delta = HeightField->Texture;
					}
					break;
				}
				default:
					break;
				}
				if (!Delta || !Module.Height || Delta->Desc.Format != PF_R32_FLOAT) { continue; }
				// Push is strictly signed Height; never transport its input through Flow.
				const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bHasMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: LayerCtx.CombinedMask;
				Module.Height = AddBehaviorSignedPushPass(Ctx, Module.Height, Delta,
					Child.Behavior, Gate, bHasMask, Influence, bUseInfluence,
					LayerCtx.LayerIndex, Child.SourceChildIndex);
				continue;
			}
			FRDGTextureRef Coordinates = nullptr;
			const bool bMixDriver = Child.Behavior.Flow.SettingsDrivers[8].bEnabled;
			const bool bWarp = Child.Behavior.Type == EMixtormatBehaviorType::Warp;
			const bool bStandardNeeded = !bWarp || Child.Behavior.Flow.ModeMix < 1.0f || bMixDriver;
			// Scoped Flow inherits automatically from preceding Flow siblings.
			// No direction-field picker is consulted for this execution path.
			if (bStandardNeeded && WorkingFlowValidity
				&& (Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::None
					|| (Child.Behavior.Type == EMixtormatBehaviorType::Deform
						&& Child.Behavior.bUseTracedFlow))
				&& (Child.Behavior.Type == EMixtormatBehaviorType::Warp
					|| Child.Behavior.Type == EMixtormatBehaviorType::Deform))
			{
				FOutputReferenceRenderData AutoFlow;
				AutoFlow.FlowAmount = 1.0f;
				AutoFlow.FlowTraceLength = Child.Behavior.bUseTracedFlow
					? Child.Behavior.Flow.TraceLength : Child.Behavior.Flow.StandardTraceLength;
				AutoFlow.FlowSteps = Child.Behavior.bUseTracedFlow
					? Child.Behavior.Flow.TraceSteps : Child.Behavior.Flow.StandardTraceSteps;
				const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
					WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
				Coordinates = AddReferencedFlowUVPass(Ctx, AutoFlow, FlowField,
					LayerCtx.LayerIndex, Child.SourceChildIndex, nullptr, true);
				const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bHasMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: LayerCtx.CombinedMask;
				Coordinates = ScaleBehaviorUV(Ctx, Coordinates, Child.Behavior.Strength,
					Gate, bHasMask, Influence, bUseInfluence,
					LayerCtx.LayerIndex, Child.SourceChildIndex,
					Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
			}
			else if (bStandardNeeded && Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight
				&& Module.Height && (Child.Behavior.GradientReach != 0.0f
					|| Child.Behavior.ScalarDrivers[1].bEnabled))
			{
				const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bHasMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: LayerCtx.CombinedMask;
				Coordinates = MakeBehaviorHeightGradientUV(Ctx, Module.Height,
					Child.Behavior.Strength, Child.Behavior.GradientReach,
					bWarp, Gate, bHasMask,
					Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
					Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
			}
			else if (bStandardNeeded && Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::PublishedOutput)
			{
				const FPublishedField* Published = Ctx.PublishedFieldOutputs.Find(Ref.Source);
				if (Published && Published->IsComplete() && Published->Kind == Ref.Kind
					&& Published->Texture->Desc.Extent == Size)
				{
				if (Ref.Kind == EMixtormatPublishedFieldKind::Flow)
				{
					// An active driver can produce displacement even when its authored
					// scalar is zero. Unavailable signals keep their authored fallback.
					if ((Ref.FlowAmount != 0.0f || Child.Behavior.FlowDrivers[0].bEnabled)
						&& (Ref.FlowTraceLength != 0.0f || Child.Behavior.FlowDrivers[1].bEnabled))
					{
						Coordinates = AddReferencedFlowUVPass(Ctx, Ref, *Published,
							LayerCtx.LayerIndex, Child.SourceChildIndex, Child.Behavior.FlowDrivers);
					}
				}
				else if (Ref.Kind == EMixtormatPublishedFieldKind::UVMap
					&& Published->Texture->Desc.Format == PF_G32R32F)
				{
					Coordinates = Published->Texture;
				}
				if (Coordinates)
				{
					// Strength is independent of authored/driver Flow Amount, and
					// always gates the final displacement toward the identity UV.
					const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef Gate = bHasMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: LayerCtx.CombinedMask;
					const float BlendStrength = Child.Behavior.Strength;
					Coordinates = ScaleBehaviorUV(Ctx, Coordinates, BlendStrength, Gate,
						bHasMask, Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
					Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
				}
				}
			}
			FRDGTextureRef TracedCoordinates = nullptr;
			if (bWarp && (Child.Behavior.Flow.ModeMix > 0.0f || bMixDriver)
				&& WorkingFlowValidity)
			{
				FOutputReferenceRenderData Trace;
				Trace.FlowAmount = 1.0f;
				Trace.FlowTraceLength = Child.Behavior.Flow.TraceLength;
				Trace.FlowSteps = Child.Behavior.Flow.TraceSteps;
				const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
					WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
				TracedCoordinates = AddReferencedFlowUVPass(Ctx, Trace, FlowField,
					LayerCtx.LayerIndex, Child.SourceChildIndex, nullptr, true);
				if (TracedCoordinates)
				{
					const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef Gate = bHasMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: LayerCtx.CombinedMask;
					TracedCoordinates = ScaleBehaviorUV(Ctx, TracedCoordinates,
						Child.Behavior.Strength, Gate, bHasMask, Influence, bUseInfluence,
						LayerCtx.LayerIndex, Child.SourceChildIndex,
						Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
				}
			}
			if (bWarp)
			{
				Coordinates = MixBehaviorWarpCoordinates(Ctx, Coordinates, TracedCoordinates,
					Child.Behavior, LayerCtx.LayerIndex, Child.SourceChildIndex);
				if (Coordinates)
				{
					Ctx.PublishedFieldOutputs.Add(
						PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
						FPublishedField{EMixtormatPublishedFieldKind::UVMap, Coordinates, nullptr, nullptr, false});
				}
			}
			if (Coordinates)
			{
				if (Child.Behavior.Type == EMixtormatBehaviorType::Deform)
				{
					// Unlike Warp, Deform moves relief only. Region IDs, coverage,
					// and named attributes retain their generator-domain placement.
					Module.Height = RemapBundleField(Ctx, Module.Height, Coordinates, 0);
				}
				else
				{
					RemapGeneratorModuleOutputs(Ctx, Layer, Owner, Module, Coordinates,
						bWarp);
					if (WorkingFlowValidity)
					{
						WorkingFlow = AddNoiseFlowTransportPass(Ctx, WorkingFlow,
							WorkingFlowValidity, Coordinates, WorkingFlowValidity);

					}
				}
			}
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
		// Beds lie across (sin, cos): rotation 0 gives horizontal bedding.
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

	// Combine the Boolean container's resolved target and source height fields.
	void AddBooleanStackPasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child, FRDGTextureRef TargetHeight,
		FRDGTextureRef SourceHeight, FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FBooleanStackRenderData& B = Child.Boolean;
		const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
		const auto Make = [&GraphBuilder, Size](const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, PF_R32_FLOAT, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef Height = Make(TEXT("Mixtormat.Boolean.StackHeight"));
		FRDGTextureRef Area = Make(TEXT("Mixtormat.Boolean.AffectedArea"));
		FRDGTextureRef Boundary = Make(TEXT("Mixtormat.Boolean.Boundary"));
		FRDGTextureRef Difference = Make(TEXT("Mixtormat.Boolean.Difference"));
		auto* P = GraphBuilder.AllocParameters<FMixtormatBooleanStackCS::FParameters>();
		P->OutputSize = Size;
		P->Operation = B.Operation;
		P->EdgeStyle = B.EdgeStyle;
		P->HasMask = bHasMask ? 1u : 0u;
		P->TargetOffset = B.TargetOffset;
		P->SourceOffset = B.SourceOffset;
		P->TargetHeightOffset = B.TargetHeightOffset;
		P->SourceHeightOffset = B.SourceHeightOffset;
		P->SourceScale = B.SourceScale;
		P->Amount = B.Amount;
		P->JoinWidth = B.JoinWidth;
		P->TargetHeight = TargetHeight;
		P->SourceHeight = SourceHeight;
		P->ScopedMask = bHasMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true) : TargetHeight;
		P->OutHeight = GraphBuilder.CreateUAV(Height);
		P->OutArea = GraphBuilder.CreateUAV(Area);
		P->OutBoundary = GraphBuilder.CreateUAV(Boundary);
		P->OutDifference = GraphBuilder.CreateUAV(Difference);
		TShaderMapRef<FMixtormatBooleanStackCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Boolean.Stack.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		Bundle->Height = Height;
		Bundle->RegisterNamedMask(FName(TEXT("FieldMask")), Area,
			FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		Bundle->RegisterNamedMask(FName(TEXT("FieldBoundary")), Boundary,
			FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		const FName DifferenceName(TEXT("FieldDifference"));
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, DifferenceName),
			FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Difference, nullptr, nullptr, false});
		if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
			DifferenceName, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewSignedHeightBlitPass(GraphBuilder, Difference,
				Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
		}
	}

	// Scatter Carve uses finite-volume evaluation. Its shader reads the
	// existing signed stack and produces the change needed by the Generator-layer
	// additive chain. Neither its input nor its delta is normalized.
	void AddBooleanCarvePasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child, FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight, FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FBooleanOpsRenderData& B = Child.Generator.BooleanOps;
		const auto Make = [&GraphBuilder, Size](const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, PF_R32_FLOAT, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef Delta = Make(TEXT("Mixtormat.BooleanCarve.Delta"));
		FRDGTextureRef CutDepth = Make(TEXT("Mixtormat.BooleanCarve.CutDepth"));
		FRDGTextureRef Front = Make(TEXT("Mixtormat.BooleanCarve.Front"));
		FRDGTextureRef Back = Make(TEXT("Mixtormat.BooleanCarve.Back"));
		FRDGTextureRef Footprint = Make(TEXT("Mixtormat.BooleanCarve.GeometryMask"));
		const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef Scoped = bHasMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
			: RunningHeight;
		auto* P = GraphBuilder.AllocParameters<FMixtormatBooleanCarveCS::FParameters>();
        FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
        P->OutputSize = Size;
		P->Seed = B.Seed;
		P->Count = B.Count;
		P->Operation = B.Operation;
		P->Shape = B.Shape;
		P->Sides = B.Sides;
		P->Radius = B.Radius;
		P->RadiusVariation = B.RadiusVariation;
		P->CenterDepth = B.CenterDepth;
		P->DepthVariation = B.DepthVariation;
		P->ReferenceHeight = B.ReferenceHeight;
		P->DepthScale = B.DepthScale;
		P->Irregularity = B.Irregularity;
		P->Rotation = B.Rotation;
		P->RotationVariation = B.RotationVariation;
		P->ChamferFraction = B.ChamferFraction;
		P->ChamferVariation = B.ChamferVariation;
		P->HeightToUV = B.HeightToUV;
		P->HasScopedMask = bHasMask ? 1u : 0u;
		P->RunningHeight = RunningHeight;
		P->BelowHeight = BelowHeight;
		P->ScopedMask = Scoped;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		P->OutHeightDelta = GraphBuilder.CreateUAV(Delta);
		P->OutCutDepth = GraphBuilder.CreateUAV(CutDepth);
		P->OutGeometryFront = GraphBuilder.CreateUAV(Front);
		P->OutGeometryBack = GraphBuilder.CreateUAV(Back);
		P->OutGeometryMask = GraphBuilder.CreateUAV(Footprint);
		TShaderMapRef<FMixtormatBooleanCarveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.BooleanCarve.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));

		Bundle->Height = Delta;
		Bundle->RegisterNamedMask(FName(TEXT("GeometryMask")), Footprint,
			FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		for (const TPair<FName, FRDGTextureRef>& Output : {
			TPair<FName, FRDGTextureRef>{FName(TEXT("CutDepth")), CutDepth},
			TPair<FName, FRDGTextureRef>{FName(TEXT("GeometryFront")), Front},
			TPair<FName, FRDGTextureRef>{FName(TEXT("GeometryBack")), Back}})
		{
			Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Output.Key),
				FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Output.Value, nullptr, nullptr, false});
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
				Output.Key, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
				AddDebugPreviewSignedHeightBlitPass(GraphBuilder, Output.Value, Debug, Size);
			}
		}
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
		// Splits and wall angle for cliff, layered, boulder, rubble.
		static const float Preset[4][2] = {
			{3.0f, 72.0f}, {2.0f, 72.0f}, {2.0f, 58.0f}, {1.0f, 60.0f}};
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
		const int32 Splits = FMath::Max(FMath::RoundToInt(Blend(0) * Rock.Fracture), 0);
		// Every internal BSP node has two children, so a tree with Splits internal nodes has
		// Splits + 1 leaves. Bounded by the per-cell buffer slot count, not by taste.
		Layout.MaxLeaves = FMath::Clamp(Splits + 1, 1, 256);
		Layout.WallSlope = FMath::Tan(FMath::DegreesToRadians(Blend(1)));
		Layout.ChamferSlope = FMath::Min(FMath::Tan(FMath::DegreesToRadians(42.0f)), Layout.WallSlope);
		return Layout;
	}

	// GPU mirror of FRockLeaf in MixtormatRockFormation.usf, for the buffer stride only.
	struct FRockLeafStride
	{
		// Cx Cy, Sx Sy, TopConstant, Radius, Rv, JagOffset, JagSize.
		float Floats[9];
		// Ck, Pk, ChipOn, EdgeCount, VertexCount, Id, SeamBits, FacetCount.
		uint32 Uints[8];
	};
	static_assert(sizeof(FRockLeafStride) == 68, "Rock leaf stride must match FRockLeaf in HLSL");
	static constexpr int32 RockMaxVertices = 24;
	struct FRockFacetStride
	{
		float Direction[2], Offset, Slope, JagSize;
		uint32 JagKey;
	};
	static_assert(sizeof(FRockFacetStride) == 24, "Rock facet stride must match FRockFacet in HLSL");

	FRDGTextureRef PackScalarBoundary(FMixtormatComposeContext& Ctx, FRDGTextureRef Scalar)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Packed = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.PackedBoundary"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatScalarBoundaryPackCS::FParameters>();
		P->OutputSize = Size;
		P->ScalarBoundary = Scalar;
		P->OutBoundary = Ctx.GraphBuilder.CreateUAV(Packed);
		TShaderMapRef<FMixtormatScalarBoundaryPackCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.PackBoundary"),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Packed;
	}

	FRDGTextureRef AddRockFormationPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRockFormationRenderData& Rock = Child.Generator.RockFormation;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const FRockLayout Layout = ResolveRockLayout(Rock);
		// Negative authored random values can add rounds; do not clamp away that behavior.
		const uint32 FacetRounds = static_cast<uint32>(FMath::Max(Rock.FacetIterations, 0));
		const uint32 MaxFacetRounds = FacetRounds + static_cast<uint32>(FMath::CeilToInt(
			static_cast<float>(FacetRounds) * FMath::Max(-Rock.FacetRandom, 0.0f)));
		// Sum of 3 + 2 * round over all rounds.
		const uint32 FacetSlotsPerLeaf = FMath::Max(MaxFacetRounds * (MaxFacetRounds + 2u), 1u);
		const auto FillParameters = [&Rock, &Layout, &Layer, &Child, &Ctx, Size, FacetSlotsPerLeaf](FMixtormatRockFormationCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
			P->OutputSize = Size;
			P->MaxLeaves = Layout.MaxLeaves;
			P->FacetSlotsPerLeaf = FacetSlotsPerLeaf;
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
		// Produced once per module.
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
			const uint64 NodeKey = NodeCache && !Child.PreUV && Rock.FieldKey != 0
				? MixtormatComposeHash::Combine(Rock.FieldKey, 0x526F636B0Aull)
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
				FRDGBufferRef FacetBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRockFacetStride), LeafSlots * FacetSlotsPerLeaf),
					TEXT("Mixtormat.Rock.Facets"));
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
					B->OutFacets = GraphBuilder.CreateUAV(FacetBuffer);
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
				P->Facets = GraphBuilder.CreateSRV(FacetBuffer);
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
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, RockSlotCount);
		}
		// Independent normalised gate, including field-only evaluation; never the flow-modified
		// layer height.
		if (Bundle)
		{

			Bundle->RegionIds = Outputs[5];
			Bundle->BoundaryField = Outputs[6];
			using ESemantic = FGeneratorBundle::EFieldSemantic;
			using EUnits = FGeneratorBundle::EFieldUnits;
			static const struct
			{
				const TCHAR* Name;
				int32 Slot;
				ESemantic Semantic;
				EUnits Units;
			} Fields[] = {
				{TEXT("RockTop"), 1, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockChamfer"), 2, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockWall"), 3, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockEdgeDistance"), 4, ESemantic::UvDistance, EUnits::MapUV},
				{TEXT("RockTopRamp"), 7, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockChamferRamp"), 8, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockWallRamp"), 9, ESemantic::ContinuousAttribute, EUnits::Unitless},
				// Intrinsic geometry attributes, not slope/height recomputed from flowed relief.
				{TEXT("RockHeight"), 10, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockSlope"), 11, ESemantic::ContinuousAttribute, EUnits::Unitless},
				{TEXT("RockGap"), 12, ESemantic::ContinuousAttribute, EUnits::Unitless}};
			for (const auto& Field : Fields)
			{
				// Rock's no-hit public distance is zero; BoundaryField retains its invalid bit.
				Bundle->RegisterNamedMask(FName(Field.Name), Outputs[Field.Slot], Field.Semantic, Field.Units);
			}
		}

		// Equalize the raw field to 0..1, then remap into Depth Min/Max. Cached masks stay raw.
		// Shared Normalize Height and Scale still run later on this result.
		FRDGBufferRef RangeBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2), TEXT("Mixtormat.Rock.DepthRange"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RangeBuffer), 0u);
		{
			FMixtormatFieldRangeCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatFieldRangeCS::FStage>(0);
			TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
			P->OutputSize = Size;
			P->SourceField = Outputs[0];
			P->OutRange = GraphBuilder.CreateUAV(RangeBuffer);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Rock.Depth.Reduce"), Shader, P, Groups);
		}
		FRDGTextureRef Height = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Rock.Depth"));
		{
			FMixtormatFieldRangeCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatFieldRangeCS::FStage>(1);
			TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
			P->OutputSize = Size;
			P->OutLow = Rock.DepthMin;
			P->OutHigh = Rock.DepthMax;
			P->NormalizeMode = 0u;
			P->OutputScale = 1.0f;
			P->HasHeightGate = 0u;
			P->HeightGate = Outputs[0];
			P->SourceField = Outputs[0];
			P->Range = GraphBuilder.CreateSRV(RangeBuffer);
			P->OutField = GraphBuilder.CreateUAV(Height);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Rock.Depth.Remap"), Shader, P, Groups);
		}
		if (Bundle) { Bundle->Height = Height; }
		return Height;
	}

	FRDGTextureRef AddCracksPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
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
		const auto Fill = [&Cracks, &Layer, &Child, &Ctx, Size, SolveSize](FMixtormatCracksCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
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
			P->ChamferEdge = Cracks.ChamferEdge;
		};
		const auto Make = [&GraphBuilder, Size](EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		// Field key deliberately omits chamfer, blend and flow settings.
		constexpr int32 SlotCount = 7;
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
			const uint64 NodeKey = NodeCache && !Child.PreUV && Cracks.FieldKey != 0
				? MixtormatComposeHash::Combine(Cracks.FieldKey, 0x437261636B74ull) : 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit = NodeKey != 0
				? NodeCache->Find(NodeKey, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			static const TCHAR* const Names[SlotCount] = {
				TEXT("Mixtormat.Cracks.Height"), TEXT("Mixtormat.Cracks.Mask"),
				TEXT("Mixtormat.Cracks.Distance"), TEXT("Mixtormat.Cracks.Ids"),
				TEXT("Mixtormat.Cracks.Random"), TEXT("Mixtormat.Cracks.Boundary"),
				TEXT("Mixtormat.Cracks.Delta")};
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
				// The chamfer remap reads the profile: retain field precision, not a quantized gate.
				Outputs[1] = Make(PF_R32_FLOAT, Names[1]);
				Outputs[2] = Make(PF_R32_FLOAT, Names[2]);
				Outputs[3] = Make(PF_R32_UINT, Names[3]);
				Outputs[4] = Make(PF_R16F, Names[4]);
				Outputs[5] = Make(PF_G32R32F, Names[5]);
				Outputs[6] = Make(PF_R32_FLOAT, Names[6]);
				FMixtormatCracksCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatCracksCS::FStage>(0);
				TShaderMapRef<FMixtormatCracksCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatCracksCS::FParameters>();
				Fill(P);
				P->OutCrackHeight = GraphBuilder.CreateUAV(Outputs[0]);
				P->OutCrackDelta = GraphBuilder.CreateUAV(Outputs[6]);
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
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}
		if (Bundle)
		{

			Bundle->RegionIds = Outputs[3];
			Bundle->BoundaryField = Outputs[5];
			Bundle->RegisterNamedMask(FName(TEXT("CrackMask")), Outputs[1],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			// Positive crack-cell attribute, not the separate negative-inside UV boundary.
			Bundle->RegisterNamedMask(FName(TEXT("CrackDistance")), Outputs[2],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute, FGeneratorBundle::EFieldUnits::CrackCell);
			Bundle->RegisterNamedMask(FName(TEXT("PieceRandom")), Outputs[4],
				FGeneratorBundle::EFieldSemantic::RegionAttribute);
		}

		FRDGTextureRef Field = Outputs[0];

		FRDGTextureRef ChamferCut = Make(PF_R16F, TEXT("Mixtormat.Cracks.ChamferCut"));
		FRDGTextureRef Chamfered = Make(PF_R32_FLOAT, TEXT("Mixtormat.Cracks.Chamfered"));
		// Neutral-speed arrival is independent of Width; chamfer only remaps the cached delta.
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
			P->CrackHeight = Field;
			P->CrackDelta = Outputs[6];
			P->CrackMask = Outputs[1];
			P->CrackDistance = Outputs[2];
			P->Arrival = Arrival[Read];
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

			P->OutHeight = GraphBuilder.CreateUAV(Chamfered);
			P->OutChamferCut = GraphBuilder.CreateUAV(ChamferCut);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Cracks.Chamfer.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, Groups);
		}
		Field = Chamfered;
		const FName CutName(TEXT("ChamferCut"));
		if (Bundle)
		{
			Bundle->RegisterNamedMask(CutName, ChamferCut, FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
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
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FPebblesRenderData& Pebbles = Child.Generator.Pebbles;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const auto FillParameters = [&Pebbles, &Layer, &Child, &Ctx, Size](FMixtormatPebblesCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
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
		};

		// Node-cache slots: height, coverage, edge distance, random, IDs.
		constexpr int32 SlotCount = 5;
		FRDGTextureRef Outputs[SlotCount] = {};
		// Produced once per module.
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
			const uint64 NodeKey = NodeCache && !Child.PreUV && Pebbles.FieldKey != 0
				? MixtormatComposeHash::Combine(Pebbles.FieldKey, 0x506562626C66ull)
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
			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Coverage = Outputs[1];
			Bundle->RegionIds = Outputs[4];
			Bundle->BoundaryField = PackScalarBoundary(Ctx, Outputs[2]);
			Bundle->RegisterNamedMask(FName(TEXT("PebbleCoverage")), Outputs[1],
				FGeneratorBundle::EFieldSemantic::CoverageAlias);
			Bundle->RegisterNamedMask(FName(TEXT("PebbleEdgeDistance")), Outputs[2],
				FGeneratorBundle::EFieldSemantic::UvDistance, FGeneratorBundle::EFieldUnits::MapUV, 1e9f);
			Bundle->RegisterNamedMask(FName(TEXT("PebbleRandom")), Outputs[3],
				FGeneratorBundle::EFieldSemantic::RegionAttribute);
		}

		FRDGTextureRef PebbleField = Outputs[0];

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

	FRDGTextureRef AddScalesPasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer, const FGeneratorPassInput& Child, FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FScalesRenderData& Data = Child.Generator.Scales;
		const FMixtormatScales& S = Data.Settings;
		const FIntPoint Size = Ctx.Request.Resolution;
		constexpr int32 SlotCount = 5;
		FRDGTextureRef O[SlotCount] = {};
		static const TCHAR* Names[SlotCount] = {TEXT("Mixtormat.Scales.Height"), TEXT("Mixtormat.Scales.Coverage"),
			TEXT("Mixtormat.Scales.EdgeDistance"), TEXT("Mixtormat.Scales.Ids"), TEXT("Mixtormat.Scales.UV")};
		if (const auto* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 I = 0; I < SlotCount; ++I) O[I] = (*Memo)[I];
		}
		else
		{
			FMixtormatNodeCache* Cache = Ctx.Request.NodeCache.Get();
			const uint64 Key = Cache && !Child.PreUV && Data.FieldKey
				? MixtormatComposeHash::Combine(Data.FieldKey, 0x5363616C657331ull) : 0;
			auto Hit = Key ? Cache->Find(Key, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			bool bComplete = Hit.IsValid();
			for (int32 I = 0; bComplete && I < SlotCount; ++I) bComplete = Hit->Outputs[I].IsValid();
			if (bComplete)
			{
				for (int32 I = 0; I < SlotCount; ++I) O[I] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[I], Names[I]);
			}
			else
			{
				const EPixelFormat Formats[SlotCount] = {PF_R32_FLOAT, PF_R16F, PF_R32_FLOAT, PF_R32_UINT, PF_G32R32F};
				for (int32 I = 0; I < SlotCount; ++I)
				{
					O[I] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Formats[I],
						FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Names[I]);
				}
				struct FScalePlateStride { float Anchor[2], Size[2], RotationCS[2], HeightOffset; };
				static_assert(sizeof(FScalePlateStride) == 28, "Scale plate stride must match HLSL");
				const uint32 PlateCount = static_cast<uint32>(S.CountX) * static_cast<uint32>(S.CountY);
				FRDGBufferRef Plates = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FScalePlateStride), PlateCount), TEXT("Mixtormat.Scales.Plates"));
				// Rotation-independent root-to-tip bound. Deliberately not a fixed neighbourhood:
				// typed oversize, negative taper, stagger and jitter must not clip distant plates.
				const float HalfWidth = .5f * S.Width / S.CountX * FMath::Max(1.0f, 1.0f - S.Taper);
				const float Length = FMath::Max(S.Length * (1.0f + S.Overlap), .001f) / S.CountY;
				const float Radius = FMath::Sqrt(HalfWidth * HalfWidth + Length * Length) * (1.0f + FMath::Abs(S.SizeRandom));
				const FIntPoint Reach(FMath::CeilToInt(Radius * S.CountX + FMath::Abs(S.Stagger) + FMath::Abs(S.RowJitter)) + 1,
					FMath::CeilToInt(Radius * S.CountY + FMath::Abs(S.RowJitter)) + 1);
				for (int32 Stage = 0; Stage < 2; ++Stage)
				{
					FMixtormatScalesCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatScalesCS::FStage>(Stage);
					TShaderMapRef<FMixtormatScalesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatScalesCS::FParameters>();
					FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
					P->OutputSize = Size; P->ScaleCount = FIntPoint(S.CountX, S.CountY); P->SearchReach = Reach;
					P->Seed = static_cast<uint32>(S.Seed); P->Shape = static_cast<uint32>(S.Shape);
					P->Stagger = S.Stagger; P->Direction = S.Direction; P->Width = S.Width; P->Length = S.Length;
					P->TipRoundness = S.TipRoundness; P->Taper = S.Taper; P->Overlap = S.Overlap;
					P->TipLift = S.TipLift; P->RootTuck = S.RootTuck; P->Thickness = S.Thickness;
					P->Dome = S.Dome; P->Ridge = S.Ridge; P->Bevel = S.Bevel;
					P->SizeRandom = S.SizeRandom; P->RotationRandom = S.RotationRandom;
					P->HeightRandom = S.HeightRandom; P->RowJitter = S.RowJitter;
					P->GrooveAmount = S.GrooveAmount; P->GrooveCount = S.GrooveCount; P->GrooveDirection = S.GrooveDirection;
					P->OutPlates = GraphBuilder.CreateUAV(Plates); P->Plates = GraphBuilder.CreateSRV(Plates);
					P->OutScaleHeight = GraphBuilder.CreateUAV(O[0]); P->OutScaleCoverage = GraphBuilder.CreateUAV(O[1]);
					P->OutScaleEdge = GraphBuilder.CreateUAV(O[2]); P->OutScaleIds = GraphBuilder.CreateUAV(O[3]);
					P->OutScaleUV = GraphBuilder.CreateUAV(O[4]);
					ClearUnusedGraphResources(Shader, P);
					const FIntVector Groups = Stage == 0 ? FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(PlateCount), 64), 1, 1)
						: FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Scales.S%d.L%d.C%d",
						Stage, LayerCtx.LayerIndex, Child.SourceChildIndex), Shader, P, Groups);
				}
				if (Key)
				{
					auto Entry = MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = Key; Entry->Resolution = Size;
					for (int32 I = 0; I < SlotCount; ++I) GraphBuilder.QueueTextureExtraction(O[I], &Entry->Outputs[I]);
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}
			LayerCtx.GeneratorFields.Add(Child.SourceChildIndex).Append(O, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Height = O[0]; Bundle->Coverage = O[1]; Bundle->RegionIds = O[3];
			Bundle->RegisterNamedMask(FName(TEXT("ScaleCoverage")), O[1], FGeneratorBundle::EFieldSemantic::CoverageAlias);
			// Local owner-edge measure only: never advertise it as a global signed boundary.
			Bundle->RegisterNamedMask(FName(TEXT("ScaleEdgeDistance")), O[2], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		}
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("ScaleUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, O[4], nullptr, nullptr, false});
		return O[0];
	}

	struct FCliffBlockStride
	{
		float Center[2], Extent[2], Lean[2], Height, RotationCS[2], CenterShift[2], FlowAxis[2];
		uint32 SideCount, Active;
	};
	static_assert(sizeof(FCliffBlockStride) == 60, "Cliff block stride must match FCliffBlock in HLSL");

	FRDGTextureRef AddCliffStrataPasses(FMixtormatComposeContext& Ctx,FMixtormatLayerPassContext& LayerCtx,const FLayerRenderData& Layer,const FGeneratorPassInput& Child,FGeneratorBundle* Bundle=nullptr)
	{
		FRDGBuilder& GraphBuilder=Ctx.GraphBuilder;const FCliffStrataRenderData& C=Child.Generator.CliffStrata;const FIntPoint Size=Ctx.Request.Resolution;
		const FIntVector PixelGroups(FMath::DivideAndRoundUp(Size.X,8),FMath::DivideAndRoundUp(Size.Y,8),1);
		const auto Make=[&](EPixelFormat F,const TCHAR* N){return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size,F,FClearValueBinding::Black,TexCreate_ShaderResource|TexCreate_UAV),N);};
		constexpr int32 SlotCount=10;FRDGTextureRef O[SlotCount]={};
		if(const TArray<FRDGTextureRef,TInlineAllocator<7>>* Memo=LayerCtx.GeneratorFields.Find(Child.SourceChildIndex)){for(int i=0;i<SlotCount;++i)O[i]=(*Memo)[i];}
		else{
			static const TCHAR* Names[SlotCount]={TEXT("Mixtormat.CliffStrata.RawHeight"),TEXT("Mixtormat.CliffStrata.Flow"),TEXT("Mixtormat.CliffStrata.BlockIds"),TEXT("Mixtormat.CliffStrata.RowIds"),TEXT("Mixtormat.CliffStrata.VoronoiRaw"),TEXT("Mixtormat.CliffStrata.FinalHeight"),TEXT("Mixtormat.CliffStrata.BlockSeam"),TEXT("Mixtormat.CliffStrata.RowSeam"),TEXT("Mixtormat.CliffStrata.Cavity"),TEXT("Mixtormat.CliffStrata.Coverage")};
			FMixtormatNodeCache* NodeCache=Ctx.Request.NodeCache.Get();const uint64 NodeKey=NodeCache&&!Child.PreUV&&C.FieldKey?MixtormatComposeHash::Combine(C.FieldKey,0x436C696666537473ull):0;
			auto Hit=NodeKey?NodeCache->Find(NodeKey,Size):TSharedPtr<FMixtormatNodeCacheEntry,ESPMode::ThreadSafe>();bool complete=Hit.IsValid();for(int i=0;complete&&i<SlotCount;++i)complete=Hit->Outputs[i].IsValid();
			if(complete){for(int i=0;i<SlotCount;++i)O[i]=GraphBuilder.RegisterExternalTexture(Hit->Outputs[i],Names[i]);}
			else{
				O[0]=Make(PF_R32_FLOAT,Names[0]);O[1]=Make(PF_G32R32F,Names[1]);O[2]=Make(PF_R32_UINT,Names[2]);O[3]=Make(PF_R32_UINT,Names[3]);O[4]=Make(PF_R32_FLOAT,Names[4]);O[5]=Make(PF_R32_FLOAT,Names[5]);O[6]=Make(PF_R16F,Names[6]);O[7]=Make(PF_R16F,Names[7]);O[8]=Make(PF_R16F,Names[8]);O[9]=Make(PF_R16F,Names[9]);
				FRDGTextureRef Smooth=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.VoronoiSmooth")),SweepA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.SweepA")),SweepB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.SweepB")),IdA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.IdA")),IdB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.IdB")),RowA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.RowA")),RowB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.RowB"));
				const uint32 BlockCount = static_cast<uint32>(C.CountX) * static_cast<uint32>(C.CountY)
					* (C.bQuarterCopies ? 16u * static_cast<uint32>(C.QuarterYCount) : 1u);
				const bool bBoxChamfer = C.ChamferMode == 1u && C.BoxChamferWidth > 0.0f;
				const uint32 BlockPlaneStride = static_cast<uint32>(C.bShapeRandom ? 8 : C.Sides) * (bBoxChamfer ? 4u : 1u);
				FRDGBufferRef Blocks = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FCliffBlockStride), BlockCount), TEXT("Mixtormat.CliffStrata.Blocks"));
				FRDGBufferRef Planes = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), BlockCount * BlockPlaneStride), TEXT("Mixtormat.CliffStrata.Planes"));
				auto Fill=[&](FMixtormatCliffStrataCS::FParameters* P){FillGeneratorPlacement(P,Layer,Child.PreUV,Ctx.EmptyPatternUV);P->BlockPlaneStride=BlockPlaneStride;P->ChamferMode=C.ChamferMode;P->BoxChamferWidth=C.BoxChamferWidth;P->OutBlocks=GraphBuilder.CreateUAV(Blocks);P->OutBlockPlanes=GraphBuilder.CreateUAV(Planes);P->Blocks=GraphBuilder.CreateSRV(Blocks);P->BlockPlanes=GraphBuilder.CreateSRV(Planes);P->OutputSize=Size;P->CountX=C.CountX;P->CountY=C.CountY;P->Density=C.Density;P->SizeMin=C.SizeMin;P->SizeMax=C.SizeMax;P->SizeAspect=C.SizeAspect;P->Jitter=C.Jitter;P->FlowVariation=C.FlowVariation;P->HeightMin=C.HeightMin;P->HeightMax=C.HeightMax;P->Steps=C.Steps;P->Rotation=C.Rotation;P->LeanX=C.LeanX;P->LeanY=C.LeanY;P->FormationCells=C.FormationCells;P->FormationAmount=C.FormationAmount;P->QuarterCopies=C.bQuarterCopies?1u:0u;P->QuarterYCount=C.QuarterYCount;P->QuarterFill=C.QuarterFill;P->QuarterSize=C.QuarterSize;P->QuarterHeight=C.QuarterHeight;P->QuarterJitterX=C.QuarterJitterX;P->QuarterJitterY=C.QuarterJitterY;P->Sides=C.Sides;P->ShapeRandom=C.bShapeRandom?1u:0u;P->CameraYaw=C.CameraYaw;P->CameraPitch=C.CameraPitch;P->ViewScale=C.ViewScale;P->DepthMin=C.DepthMin;P->DepthMax=C.DepthMax;P->UnitDistance=C.UnitDistance;P->UnitDistanceIdLerp=C.UnitDistanceIdLerp;P->CarveDepth=C.CarveDepth;P->CarveVoronoi=C.CarveVoronoi;P->YBias=C.YBias;P->YBiasVoronoi=C.YBiasVoronoi;P->YBiasVoronoiInvert=C.bYBiasVoronoiInvert?1u:0u;P->NegativeYUnitDistanceTaper=C.NegativeYUnitDistanceTaper;P->Reverse=C.bReverse?1u:0u;P->Seed=C.Seed;P->VoronoiCells=C.VoronoiCells;P->FlowVoronoi=C.FlowVoronoi;P->ChamferWidth=C.ChamferWidth;P->ChamferIntensity=C.ChamferIntensity;P->ChamferVoronoi=C.ChamferVoronoi;P->BlockCavityWidth=C.BlockCavityWidth;P->RowCavityWidth=C.RowCavityWidth;P->CavityIntensity=C.CavityIntensity;P->CavityVoronoiThreshold=C.CavityVoronoiThreshold;P->CavityVoronoiMaskGain=C.CavityVoronoiMaskGain;P->RawHeight=O[0];P->Flow=O[1];P->BlockIds=O[2];P->RowIds=O[3];P->VoronoiRaw=O[4];P->VoronoiSmooth=Smooth;P->SweepIn=SweepA;P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;};
				for(int Pass=0;Pass<10;++Pass){const int Stage=Pass==0?9:Pass-1;FMixtormatCliffStrataCS::FPermutationDomain Perm;Perm.Set<FMixtormatCliffStrataCS::FStage>(Stage);TShaderMapRef<FMixtormatCliffStrataCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel),Perm);auto* P=GraphBuilder.AllocParameters<FMixtormatCliffStrataCS::FParameters>();Fill(P);P->OutRawHeight=GraphBuilder.CreateUAV(O[0]);P->OutFlow=GraphBuilder.CreateUAV(O[1]);P->OutBlockIds=GraphBuilder.CreateUAV(O[2]);P->OutRowIds=GraphBuilder.CreateUAV(O[3]);P->OutVoronoiRaw=GraphBuilder.CreateUAV(O[4]);P->OutVoronoiSmooth=GraphBuilder.CreateUAV(Smooth);P->OutSweep=GraphBuilder.CreateUAV(Stage==3?SweepA:SweepB);P->OutIdDistance=GraphBuilder.CreateUAV((Stage==5||Stage==7)?IdA:IdB);P->OutRowDistance=GraphBuilder.CreateUAV((Stage==5||Stage==7)?RowA:RowB);P->OutHeight=GraphBuilder.CreateUAV(O[5]);P->OutBlockSeam=GraphBuilder.CreateUAV(O[6]);P->OutRowSeam=GraphBuilder.CreateUAV(O[7]);P->OutCavity=GraphBuilder.CreateUAV(O[8]);P->OutCoverage=GraphBuilder.CreateUAV(O[9]);if(Stage==4)P->SweepIn=SweepA;if(Stage==6){P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;}if(Stage==7){P->IdDistanceIn=IdB;P->RowDistanceIn=RowB;}if(Stage==8){P->SweepIn=SweepB;P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;}ClearUnusedGraphResources(Shader,P);const FIntVector Groups=Stage==9?FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(BlockCount),64),1,1):(Stage==3||Stage==6)?FIntVector(1,FMath::DivideAndRoundUp(Size.Y,8),1):((Stage==4||Stage==7)?FIntVector(FMath::DivideAndRoundUp(Size.X,8),1,1):PixelGroups);FComputeShaderUtils::AddPass(GraphBuilder,RDG_EVENT_NAME("Mixtormat.CliffStrata.S%d.L%d.C%d",Stage,LayerCtx.LayerIndex,Child.SourceChildIndex),Shader,P,Groups);}
				if(NodeKey){auto Entry=MakeShared<FMixtormatNodeCacheEntry,ESPMode::ThreadSafe>();Entry->Key=NodeKey;Entry->Resolution=Size;for(int i=0;i<SlotCount;++i)GraphBuilder.QueueTextureExtraction(O[i],&Entry->Outputs[i]);Ctx.PendingNodeEntries.Add(Entry);}
			}
			auto& Stored=LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);Stored.Append(O,SlotCount);
		}
		if (Bundle)
		{
			Bundle->Height = O[5]; Bundle->RegionIds = O[2]; Bundle->Coverage = O[9];
			Bundle->RegisterNamedMask(FName(TEXT("CliffBlockSeam")), O[6], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("CliffRowSeam")), O[7], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("CliffCavity")), O[8], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("CliffVoronoi")), O[4], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("CliffCoverage")), O[9], FGeneratorBundle::EFieldSemantic::CoverageAlias);
		}
		return O[5];
	}



}


namespace
{
	constexpr uint32 RootMaxNodes = 2048;
	constexpr uint32 RootMaxSegments = 2048;
	constexpr uint32 RootMaxAttractors = 4096;
	constexpr uint32 RootBinCount = 64 * 64;
	constexpr uint32 RootMaxBinEntries = 524288;
	constexpr uint32 RootStateSlots = 8;

	struct FRootNodeStride { float Pos[2]; float Dir[2]; uint32 Parent; uint32 BranchId; float Radius; float Arc; uint32 Generation; uint32 ChildCount; };
	struct FRootSegmentStride { float P[6]; float R[2]; float Arc[2]; uint32 BranchId; uint32 Generation; };
	struct FRootAttractorStride { float Pos[2]; uint32 Active; uint32 Nearest; float NearestDist; uint32 Pad[3]; };
	static_assert(sizeof(FRootNodeStride) == 40, "Root node stride must match FRootNode in HLSL");
	static_assert(sizeof(FRootSegmentStride) == 48, "Root segment stride must match FRootSegment in HLSL");
	static_assert(sizeof(FRootAttractorStride) == 32, "Root attractor stride must match FRootAttractor in HLSL");

	FRDGTextureRef AddRootNetworkPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRootNetworkRenderData& Root = Child.Generator.RootNetwork;
		const FIntPoint Size = Request.Resolution;
		const FIntVector PixelGroups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const uint32 AttractorCount = static_cast<uint32>(FMath::Clamp(Root.Attractors, 16, static_cast<int32>(RootMaxAttractors)));
		const int32 Iterations = FMath::Clamp(Root.Iterations, 1, 256);

		const auto FillParameters = [&Root, &Layer, &Child, &Ctx, Size, AttractorCount](FMixtormatRootNetworkCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
			P->OutputSize = Size;
			P->Seed = Root.Seed;
			P->Trunks = static_cast<uint32>(FMath::Clamp(Root.Trunks, 1, 128));
			P->Attractors = AttractorCount;
			P->StepIndex = 0u;
			P->StepLength = Root.StepLength;
			P->InfluenceRadius = Root.InfluenceRadius;
			P->KillDistance = Root.KillDistance;
			P->BranchSpread = Root.BranchSpread;
			P->DirectionInertia = Root.DirectionInertia;
			P->CurlStrength = Root.CurlStrength;
			P->CurlScale = static_cast<uint32>(FMath::Max(Root.CurlScale, 1));
			P->BaseRadius = Root.BaseRadius;
			P->TipTaper = Root.TipTaper;
			P->JunctionSmooth = Root.JunctionSmooth;
			P->ProfileFlatness = Root.ProfileFlatness;
			P->WeaveAmount = Root.WeaveAmount;
			P->KinkStrength = Root.Kink;
			P->ProfileBias = Root.ProfileBias;
			P->BurialDepth = Root.Burial;
			P->AzimuthAngle = Root.AzimuthAngle;
			P->AzimuthStrength = Root.AzimuthStrength;
			P->SwirlStrength = Root.SwirlStrength;
			P->AxisRatio = Root.AxisRatio;
			P->UseRadiusCurve = Root.bUseRadiusCurve ? 1u : 0u;
			P->RadiusCurveCount = Root.RadiusCurveCount;
			P->RadiusCurveInterpolation = Root.RadiusCurveInterpolation;
			for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
			{
				P->RadiusCurvePoints[Index] = Root.RadiusCurvePoints[Index];
			}
			P->UseBurialCurve = Root.bUseBurialCurve ? 1u : 0u;
			P->BurialCurveCount = Root.BurialCurveCount;
			P->BurialCurveInterpolation = Root.BurialCurveInterpolation;
			for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
			{
				P->BurialCurvePoints[Index] = Root.BurialCurvePoints[Index];
			}
			P->RootUVScaleU = Root.RootUVScaleU;
			P->RootUVScaleV = Root.RootUVScaleV;
			P->RootUVOffsetU = Root.RootUVOffsetU;
			P->RootUVOffsetV = Root.RootUVOffsetV;
		};

		constexpr int32 SlotCount = 7;
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
			// RootNet3: invalidate fields built with old sweep or without UV/Across outputs.
			const uint64 NodeKey = NodeCache && !Child.PreUV && Root.FieldKey != 0
				? MixtormatComposeHash::Combine(Root.FieldKey, 0x526F6F744E657437ull)
				: 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit =
				NodeKey != 0 ? NodeCache->Find(NodeKey, Size)
					: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			static const TCHAR* const Names[SlotCount] = {
				TEXT("Mixtormat.RootNetwork.Height"), TEXT("Mixtormat.RootNetwork.Coverage"),
				TEXT("Mixtormat.RootNetwork.EdgeDistance"), TEXT("Mixtormat.RootNetwork.ArcLength"),
				TEXT("Mixtormat.RootNetwork.Ids"), TEXT("Mixtormat.RootNetwork.UV"),
				TEXT("Mixtormat.RootNetwork.Across")};
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
				Outputs[5] = Make(PF_G32R32F, Names[5]);
				Outputs[6] = Make(PF_R16F, Names[6]);

				FRDGBufferRef NodeBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootNodeStride), RootMaxNodes),
					TEXT("Mixtormat.RootNetwork.Nodes"));
				FRDGBufferRef SegmentBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootSegmentStride), RootMaxSegments),
					TEXT("Mixtormat.RootNetwork.Segments"));
				FRDGBufferRef AttractorBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootAttractorStride), RootMaxAttractors),
					TEXT("Mixtormat.RootNetwork.Attractors"));
				FRDGBufferRef StateBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootStateSlots),
					TEXT("Mixtormat.RootNetwork.State"));
				FRDGBufferRef PullBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(int32), RootMaxNodes * 4),
					TEXT("Mixtormat.RootNetwork.NodePull"));
				FRDGBufferRef BinCounts = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinCounts"));
				FRDGBufferRef BinOffsets = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinOffsets"));
				FRDGBufferRef BinWriteHeads = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinWriteHeads"));
				FRDGBufferRef BinEntries = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootMaxBinEntries),
					TEXT("Mixtormat.RootNetwork.BinEntries"));

				const FRDGBufferUAVRef NodeUAV = GraphBuilder.CreateUAV(NodeBuffer);
				const FRDGBufferUAVRef SegmentUAV = GraphBuilder.CreateUAV(SegmentBuffer);
				const FRDGBufferUAVRef AttractorUAV = GraphBuilder.CreateUAV(AttractorBuffer);
				const FRDGBufferUAVRef StateUAV = GraphBuilder.CreateUAV(StateBuffer, PF_R32_UINT);
				const FRDGBufferUAVRef PullUAV = GraphBuilder.CreateUAV(PullBuffer, PF_R32_SINT);
				const FRDGBufferUAVRef BinCountsUAV = GraphBuilder.CreateUAV(BinCounts, PF_R32_UINT);
				const FRDGBufferUAVRef BinOffsetsUAV = GraphBuilder.CreateUAV(BinOffsets, PF_R32_UINT);
				const FRDGBufferUAVRef BinWriteHeadsUAV = GraphBuilder.CreateUAV(BinWriteHeads, PF_R32_UINT);
				const FRDGBufferUAVRef BinEntriesUAV = GraphBuilder.CreateUAV(BinEntries, PF_R32_UINT);

				const auto AddGrowthPass = [&](const int32 Stage, const uint32 Step, const FIntVector Groups)
				{
					FMixtormatRootNetworkCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatRootNetworkCS::FStage>(Stage);
					TShaderMapRef<FMixtormatRootNetworkCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatRootNetworkCS::FParameters>();
					FillParameters(P);
					P->StepIndex = Step;
					P->RWNodeBuffer = NodeUAV;
					P->RWSegmentBuffer = SegmentUAV;
					P->RWAttractorBuffer = AttractorUAV;
					P->RWStateBuffer = StateUAV;
					P->RWNodePullBuffer = PullUAV;
					P->RWBinCounts = BinCountsUAV;
					P->RWBinOffsets = BinOffsetsUAV;
					P->RWBinWriteHeads = BinWriteHeadsUAV;
					P->RWBinEntries = BinEntriesUAV;
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.RootNetwork.Stage%d.Step%u.L%d.C%d",
							Stage, Step, LayerCtx.LayerIndex, Child.SourceChildIndex),
						Shader, P, Groups);
				};

				const FIntVector Single(1, 1, 1);
				AddGrowthPass(0, 0u, FIntVector(FMath::DivideAndRoundUp(
					static_cast<int32>(FMath::Max(RootMaxNodes, AttractorCount)), 64), 1, 1));
				const FIntVector AttractorGroups(FMath::DivideAndRoundUp(static_cast<int32>(AttractorCount), 64), 1, 1);
				for (int32 Step = 0; Step < Iterations; ++Step)
				{
					AddGrowthPass(1, static_cast<uint32>(Step), AttractorGroups);
					AddGrowthPass(2, static_cast<uint32>(Step), Single);
				}
				AddGrowthPass(3, 0u, Single);
				AddGrowthPass(4, 0u, Single);
				AddGrowthPass(5, 0u, Single);

				{
					FMixtormatRootNetworkCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatRootNetworkCS::FStage>(6);
					TShaderMapRef<FMixtormatRootNetworkCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatRootNetworkCS::FParameters>();
					FillParameters(P);
					P->SegmentBuffer = GraphBuilder.CreateSRV(SegmentBuffer);
					P->BinCounts = GraphBuilder.CreateSRV(BinCounts, PF_R32_UINT);
					P->BinOffsets = GraphBuilder.CreateSRV(BinOffsets, PF_R32_UINT);
					P->BinEntries = GraphBuilder.CreateSRV(BinEntries, PF_R32_UINT);
					P->OutRootHeight = GraphBuilder.CreateUAV(Outputs[0]);
					P->OutRootCoverage = GraphBuilder.CreateUAV(Outputs[1]);
					P->OutRootEdgeDistance = GraphBuilder.CreateUAV(Outputs[2]);
					P->OutRootArcLength = GraphBuilder.CreateUAV(Outputs[3]);
					P->OutRootAcross = GraphBuilder.CreateUAV(Outputs[6]);
					P->OutRootUV = GraphBuilder.CreateUAV(Outputs[5]);
					P->OutRootIds = GraphBuilder.CreateUAV(Outputs[4]);
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.RootNetwork.Field.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
						Shader, P, PixelGroups);
				}

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

			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}

		if (Bundle)
		{
			Bundle->Height = Outputs[0];
			Bundle->Coverage = Outputs[1];
			Bundle->RegionIds = Outputs[4];
			Bundle->BoundaryField = PackScalarBoundary(Ctx, Outputs[2]);
			Bundle->RegisterNamedMask(FName(TEXT("RootCoverage")), Outputs[1],
				FGeneratorBundle::EFieldSemantic::CoverageAlias);
			Bundle->RegisterNamedMask(FName(TEXT("RootEdgeDistance")), Outputs[2],
				FGeneratorBundle::EFieldSemantic::UvDistance, FGeneratorBundle::EFieldUnits::MapUV, 1e9f);
			Bundle->RegisterNamedMask(FName(TEXT("RootArcLength")), Outputs[3],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("RootAcross")), Outputs[6],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		}
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Outputs[5], nullptr, nullptr, false});
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("RootUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Outputs[5], nullptr, nullptr, false});
		return Outputs[0];
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
		if (Reference.Kind == EMixtormatPublishedFieldKind::RegionIds)
		{
			AddRegionIdReferencePass(Ctx, LayerCtx, Layer, Child, false);
			continue;
		}
		const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Reference.Source);
		if (!Source || Source->Kind != Reference.Kind || !Source->IsComplete()) { continue; }
		// Copy the bundle before Add can reallocate the registry. No producer is reevaluated.
		const FPublishedField Field = *Source;
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, Reference.Source.Output), Field);

		if (Reference.Kind == EMixtormatPublishedFieldKind::UVMap)
		{
			LayerCtx.ReferencedUV = Field.Texture;
			continue;
		}
		if (Reference.Kind == EMixtormatPublishedFieldKind::Color)
		{
			// A colour field is copied whole; there is nothing to trace into destination UVs.
			// The destination layer's Base Color input resolves to this texture, so Fill / Generator
			// layers without a material surface still receive the referenced colour.
			LayerCtx.ReferencedColor = Field.Texture;
			// Reuse the colour blit preview so the reference previews identically to its source.
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::Color,
				Reference.Source.Output, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
				AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Field.Texture, Debug, Ctx.Request.Resolution);
			}
			continue;
		}
		// The generic scalar/vector kinds (Scalar01, ScalarSigned, SDF, Vector2) have no destination
		// consumer yet. The republish above already resolved the typed field under the destination
		// address; nothing further is traced into destination UVs. Stopping here keeps them from
		// silently falling into the Flow trace below -- a scalar is not a directional transport field.
		if (Reference.Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
		FRDGTextureRef Coordinates = AddReferencedFlowUVPass(
			Ctx, Reference, Field, LayerCtx.LayerIndex, Child.SourceChildIndex);
		LayerCtx.ReferencedUV = Coordinates;
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Coordinates, nullptr, nullptr, false});
	}
}

void AddGeneratorLayerPasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
{
	if (!Layer.bEnabled || !Layer.bGenerator) { return; }
	FGeneratorBundle& Bundle = LayerCtx.GeneratorBundle;
	Bundle = FGeneratorBundle();
	FRDGTextureRef GeneratedColor = nullptr;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
	FRDGTextureRef RunningHeight = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.SignedRunningHeight"));
	AddClearUAVPass(Ctx.GraphBuilder, Ctx.GraphBuilder.CreateUAV(RunningHeight), FLinearColor::Black);
	const FRDGTextureRef BooleanNeutralHeight = RunningHeight;
	bool bActualHeightAuthored = false;
	const FChildRenderData* PendingBoolean = nullptr;
	auto FinishBoolean = [&]()
	{
		if (!PendingBoolean) { return; }
		const FChildRenderData& Parent = *PendingBoolean;
		const FBooleanStackRenderData& B = Parent.Boolean;
		if (B.bComplete)
		{
			const FRDGTextureRef* Target = LayerCtx.GeneratorModuleHeights.Find(B.TargetChildIndex);
			const FRDGTextureRef* Source = LayerCtx.GeneratorModuleHeights.Find(B.SourceChildIndex);
			if (Target && *Target && Source && *Source)
			{
				FGeneratorBundle Combined;
				AddBooleanStackPasses(Ctx, LayerCtx, Layer, Parent, *Target, *Source, &Combined);
				LayerCtx.GeneratorModuleHeights.Add(Parent.SourceChildIndex, Combined.Height);
				Ctx.PublishedFieldOutputs.Add(
					PublishedKey(Layer, Parent.SourceChildIndex, FName(TEXT("Height"))),
					FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned,
						Combined.Height, nullptr, nullptr, false});
				for (const TPair<FName, FRDGTextureRef>& Named : Combined.NamedMasks)
				{
					Ctx.PublishedMaskOutputs.Add(
						PublishedKey(Layer, Parent.SourceChildIndex, Named.Key), Named.Value);
				}
				if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
					FName(TEXT("Height")), LayerCtx.LayerIndex, Parent.SourceChildIndex))
				{
					AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Combined.Height, Debug, Size);
				}
				AddGeneratorModuleCombine(Ctx, RunningHeight, Combined, Parent, RunningHeight);
				bActualHeightAuthored = true;
			}
		}
		PendingBoolean = nullptr;
	};

	TSet<int32> HeightBlendOperandIndices;
	for (int32 I = 0; I < Layer.Children.Num(); ++I)
	{
		const FChildRenderData& Child = Layer.Children[I];
		if (Child.Type == EMixtormatLayerChildType::HeightBlend)
		{
			const int32 OperandIdx = Child.HeightBlend.SourceChildIndex;
			if (OperandIdx != INDEX_NONE && OperandIdx < Child.SourceChildIndex)
			{
				bool bHasOtherHeightProducer = false;
				for (int32 J = 0; J < I; ++J)
				{
					const FChildRenderData& Prior = Layer.Children[J];
					if (Prior.SourceChildIndex != OperandIdx
						&& (Prior.Type == EMixtormatLayerChildType::Generator || Prior.Type == EMixtormatLayerChildType::Boolean))
					{
						bHasOtherHeightProducer = true;
						break;
					}
				}
				if (bHasOtherHeightProducer)
				{
					HeightBlendOperandIndices.Add(OperandIdx);
				}
			}
		}
	}

	// Modules compose in child order from signed zero; zero is the neutral generator height.
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (PendingBoolean && Child.SourceChildIndex > PendingBoolean->Boolean.LastChildIndex)
		{
			FinishBoolean();
		}
		if (Child.Type == EMixtormatLayerChildType::Boolean)
		{
			FinishBoolean();
			PendingBoolean = &Child;
			continue;
		}
		const bool bBooleanOperand = PendingBoolean
			&& Child.Type == EMixtormatLayerChildType::Generator
			&& Child.ScopeOwnerSourceChildIndex == PendingBoolean->SourceChildIndex
			&& (Child.SourceChildIndex == PendingBoolean->Boolean.TargetChildIndex
				|| Child.SourceChildIndex == PendingBoolean->Boolean.SourceChildIndex);
		const FRDGTextureRef OperandRunningHeight = bBooleanOperand ? BooleanNeutralHeight : RunningHeight;
		// Generator sublayers rewrite the running signed height in place (or publish colour).
		if (Child.Type == EMixtormatLayerChildType::HeightBlend)
		{
			// Resolve the source texture first; HasSource must reflect actual texture availability,
			// not merely a stored child index. A missing source falls back to the running height
			// for RDG validation, but the shader then uses its operation-specific neutral behaviour
			// rather than treating RunningHeight as a real operand (which would yield A+A, A-A, 0).
			FRDGTextureRef Source = RunningHeight;
			bool bHasSource = false;
			if (Child.HeightBlend.SourceChildIndex != INDEX_NONE)
			{
				if (const FRDGTextureRef* Found = LayerCtx.GeneratorModuleHeights.Find(Child.HeightBlend.SourceChildIndex))
				{
					Source = *Found;
					bHasSource = true;
				}
			}
			FGeneratorHeightBlendRenderData BlendData = Child.HeightBlend;
			BlendData.SourceChildIndex = bHasSource ? Child.HeightBlend.SourceChildIndex : INDEX_NONE;
			RunningHeight = AddGeneratorHeightBlendPass(Ctx, RunningHeight, Source, BlendData);
			if (bHasSource)
			{
				bActualHeightAuthored = true;
			}
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightCurve)
		{
			RunningHeight = AddGeneratorHeightCurvePass(Ctx, RunningHeight, Child.HeightCurve);
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightColorRamp)
		{
			FRDGTextureRef RampSource = RunningHeight;
			FRDGTextureRef RampFlow = nullptr;
			FRDGTextureRef RampUV = nullptr;
			FRDGTextureRef RampIds = nullptr;
			uint32 SourceMode = 0u;

			const int32 TargetChild = Child.HeightColorRamp.SourceChildIndex;
			const EMixtormatColorRampSource RampSourceEnum = static_cast<EMixtormatColorRampSource>(Child.HeightColorRamp.Source);
			switch (RampSourceEnum)
			{
			case EMixtormatColorRampSource::GeneratorRunning:
				RampSource = RunningHeight;
				SourceMode = 0u;
				break;
			case EMixtormatColorRampSource::ModuleRef:
				if (TargetChild != INDEX_NONE)
				{
					if (const FRDGTextureRef* Found = LayerCtx.GeneratorModuleHeights.Find(TargetChild))
					{
						RampSource = *Found;
					}
				}
				SourceMode = 0u;
				break;
			case EMixtormatColorRampSource::LayerHeight:
				RampSource = LayerCtx.LayerInputHeight ? LayerCtx.LayerInputHeight : RunningHeight;
				SourceMode = 0u;
				break;
			case EMixtormatColorRampSource::CompositeBelow:
				RampSource = Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)];
				SourceMode = 0u;
				break;
			case EMixtormatColorRampSource::FlowMagnitude:
			case EMixtormatColorRampSource::FlowAngle:
				SourceMode = (RampSourceEnum == EMixtormatColorRampSource::FlowMagnitude) ? 2u : 3u;
				break;
			case EMixtormatColorRampSource::NoiseValue:
				if (TargetChild != INDEX_NONE)
				{
					const FPublishedMaskKey Key = PublishedKey(Layer, TargetChild, FName(TEXT("Value")));
					if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key))
					{
						if (Field->Texture)
						{
							RampSource = Field->Texture;
						}
					}
					else if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(Key))
					{
						if (*MaskTex)
						{
							RampSource = *MaskTex;
						}
					}
				}
				SourceMode = 1u;
				break;
			case EMixtormatColorRampSource::ModuleCoverage:
			{
				SourceMode = 1u;
				if (TargetChild != INDEX_NONE)
				{
					static const FName CoverageNames[] = {
						FName(TEXT("RootCoverage")),
						FName(TEXT("PebbleCoverage")),
						FName(TEXT("RockTop")),
						FName(TEXT("CrackMask")),
						FName(TEXT("GeometryMask")),
						FName(TEXT("FieldMask"))
					};
					for (const FName& Name : CoverageNames)
					{
						if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
						{
							if (*MaskTex)
							{
								RampSource = *MaskTex;
								break;
							}
						}
						if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
						{
							if (Field->Texture)
							{
								RampSource = Field->Texture;
								break;
							}
						}
					}
				}
				if (RampSource == RunningHeight && LayerCtx.GeneratorBundle.Coverage)
				{
					RampSource = LayerCtx.GeneratorBundle.Coverage;
				}
				break;
			}
			case EMixtormatColorRampSource::ModuleEdgeDistance:
			{
				SourceMode = 0u;
				if (TargetChild != INDEX_NONE)
				{
					static const FName DistanceNames[] = {
						FName(TEXT("RootEdgeDistance")),
						FName(TEXT("PebbleEdgeDistance")),
						FName(TEXT("RockEdgeDistance")),
						FName(TEXT("FieldBoundary"))
					};
					for (const FName& Name : DistanceNames)
					{
						if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
						{
							if (Field->Texture)
							{
								RampSource = Field->Texture;
								break;
							}
						}
						if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
						{
							if (*MaskTex)
							{
								RampSource = *MaskTex;
								break;
							}
						}
					}
				}
				if (Child.HeightColorRamp.bNormalizeInput && RampSource)
				{
					RampSource = AddNormalizeFieldPasses(
						Ctx.GraphBuilder, RampSource, Size, 0.0f, 1.0f,
						TEXT("Mixtormat.Generator.HeightColorRamp.EdgeDistNormalized"));
					SourceMode = 1u;
				}
				break;
			}
			case EMixtormatColorRampSource::RootArcLength:
			{
				SourceMode = 1u;
				if (TargetChild != INDEX_NONE)
				{
					if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(
						PublishedKey(Layer, TargetChild, FName(TEXT("RootArcLength")))))
					{
						if (*MaskTex)
						{
							RampSource = *MaskTex;
						}
					}
				}
				break;
			}
			case EMixtormatColorRampSource::RootAcross:
			{
				SourceMode = 1u;
				if (TargetChild != INDEX_NONE)
				{
					if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(
						PublishedKey(Layer, TargetChild, FName(TEXT("RootAcross")))))
					{
						if (*MaskTex)
						{
							RampSource = *MaskTex;
						}
					}
				}
				break;
			}
			case EMixtormatColorRampSource::RegionRandom:
			{
				SourceMode = 4u;
				if (TargetChild != INDEX_NONE)
				{
					for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
					{
						if (Entry.Key == TargetChild && Entry.Value)
						{
							RampIds = Entry.Value;
							break;
						}
					}
				}
				if (!RampIds)
				{
					RampIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
				}
				break;
			}
			default:
				break;
			}

			// Flow resolution for Flow sources or inline Flow modulation:
			if (SourceMode == 2u || SourceMode == 3u || FMath::Abs(Child.HeightColorRamp.FlowModulation) > 1.0e-5f)
			{
				if (TargetChild != INDEX_NONE)
				{
					static const FName FlowNames[] = {
						MixtormatDiagnosticOutputs::AccumulatedFlow,
						FName(TEXT("GeneratedFlow")),
						FName(TEXT("FlowDirection"))
					};
					for (const FName& Name : FlowNames)
					{
						if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
							PublishedKey(Layer, TargetChild, Name)))
						{
							if (Field->Texture)
							{
								RampFlow = Field->Texture;
								break;
							}
						}
					}
				}
				if (!RampFlow)
				{
					for (int32 SearchIdx = Child.SourceChildIndex - 1; SearchIdx >= 0; --SearchIdx)
					{
						static const FName FlowNames[] = {
							MixtormatDiagnosticOutputs::AccumulatedFlow,
							FName(TEXT("GeneratedFlow")),
							FName(TEXT("FlowDirection"))
						};
						for (const FName& Name : FlowNames)
						{
							if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
								PublishedKey(Layer, SearchIdx, Name)))
							{
								if (Field->Texture)
								{
									RampFlow = Field->Texture;
									break;
								}
							}
						}
						if (RampFlow) { break; }
					}
				}
			}

			// UV resolution for inline UV modulation:
			if (FMath::Abs(Child.HeightColorRamp.UVModulation) > 1.0e-5f)
			{
				if (TargetChild != INDEX_NONE)
				{
					static const FName UVNames[] = {
						FName(TEXT("RootUV")),
						FName(TEXT("WarpedUV"))
					};
					for (const FName& Name : UVNames)
					{
						if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
							PublishedKey(Layer, TargetChild, Name)))
						{
							if (Field->Texture)
							{
								RampUV = Field->Texture;
								break;
							}
						}
					}
				}
				if (!RampUV)
				{
					for (int32 SearchIdx = Child.SourceChildIndex - 1; SearchIdx >= 0; --SearchIdx)
					{
						static const FName UVNames[] = {
							FName(TEXT("RootUV")),
							FName(TEXT("WarpedUV"))
						};
						for (const FName& Name : UVNames)
						{
							if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
								PublishedKey(Layer, SearchIdx, Name)))
							{
								if (Field->Texture)
								{
									RampUV = Field->Texture;
									break;
								}
							}
						}
						if (RampUV) { break; }
					}
				}
			}

			// IDs resolution for inline ID modulation:
			if (Child.HeightColorRamp.IdVariation > 1.0e-5f && !RampIds)
			{
				if (TargetChild != INDEX_NONE)
				{
					for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
					{
						if (Entry.Key == TargetChild && Entry.Value)
						{
							RampIds = Entry.Value;
							break;
						}
					}
				}
				if (!RampIds)
				{
					RampIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
				}
			}

			// A Mask scoped under this ramp gates where its colour shows, using the same independent
			// scope the generator flow tools use: the mask says where this module acts, not where
			// the layer is. The mask's own Weight dials how strongly it gates.
			const bool bHasGate = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef RampGate = bHasGate
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: RampSource;
			FGeneratorHeightColorRampRenderData RampData = Child.HeightColorRamp;
			RampData.bHasGate = bHasGate ? 1u : 0u;

			if (!GeneratedColor && !LayerCtx.LayerInputBC)
			{
				AddLayerInputPass(Ctx, LayerCtx, Layer);
			}

			FRDGTextureRef BaseColorForRamp = GeneratedColor
				? GeneratedColor
				: (LayerCtx.LayerInputBC
					? LayerCtx.LayerInputBC
					: (LayerCtx.LayerIndex != INDEX_NONE
						? Ctx.OutputBC[1 - (LayerCtx.LayerIndex & 1)]
						: nullptr));
			FRDGTextureRef Color = AddGeneratorHeightColorRampPass(
				Ctx, RampSource, RampFlow, RampUV, RampIds, SourceMode, RampGate, BaseColorForRamp, RampData);
			const FPublishedField ColorField{
				EMixtormatPublishedFieldKind::Color, Color, nullptr, nullptr, false};
			Ctx.PublishedFieldOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, Child.HeightColorRamp.OutputName),
				ColorField);
			if (ColorField.IsComplete()) { GeneratedColor = Color; }
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::Color,
				Child.HeightColorRamp.OutputName, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Color, Debug, Size);
			}
			continue;
		}
		if (Child.Type != EMixtormatLayerChildType::Generator) { continue; }
		if (Child.ScopeOwnerSourceChildIndex != INDEX_NONE && !bBooleanOperand) { continue; }
		// Only already-published maps may feed groups/references before this module.
		AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
		FGeneratorBundle Module;
		const FRDGTextureRef PreUV = BuildGeneratorPreCoordinates(Ctx, LayerCtx, Layer, Child);
		const FGeneratorPassInput Input{Child.Generator, Child.SourceChildIndex, PreUV};
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			break;
		case EMixtormatGeneratorType::RockFormation:
			AddRockFormationPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Pebbles:
			AddPebblesPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Cracks:
			AddCracksPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::CliffStrata:
			AddCliffStrataPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Scales:
			AddScalesPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::RootNetwork:
			AddRootNetworkPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::BooleanOps:
				AddBooleanCarvePasses(Ctx, LayerCtx, Layer, Input, RunningHeight,
					Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)], &Module);
				break;
		case EMixtormatGeneratorType::Noise:
			// One dispatch, no solve: the field producer publishes its value, gradient and (for
			// Worley) cell IDs, and leaves the signed height for the shared contract below.
			AddNoisePasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, &Module, PreUV, OperandRunningHeight);
			break;
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::BooleanOps)
		{
			// No normalize/flow/second mask pass: the signed delta is already scoped.
			if (Module.Height)
			{
				LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Module.Height);
				Ctx.PublishedFieldOutputs.Add(
                    PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Height"))),
                    FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Module.Height, nullptr, nullptr, false});
                if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
                    FName(TEXT("Height")), LayerCtx.LayerIndex, Child.SourceChildIndex))
                {
                    AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Module.Height, Debug, Size);
                }
                for (const auto& Named : Module.NamedMasks)
				{
					Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Named.Key), Named.Value);
				}
				AddGeneratorModuleCombine(Ctx, RunningHeight, Module, Child, RunningHeight);
				bActualHeightAuthored = true;
			}
			continue;
		}
		if (!Module.Height && Child.Generator.Type != EMixtormatGeneratorType::Noise) { continue; }
		// A Noise module with Write Height OFF produces no height: it contributes nothing to the
		// running height, runs no normalization, and its scoped Behaviors -- which all modify
		// relief -- have nothing to act on. Its Value/Gradient/FlowDirection/GeneratedFlow
		// publications and Region IDs still run below (P0 contract section 2).
		const bool bNoiseHeightless = Child.Generator.Type == EMixtormatGeneratorType::Noise
			&& !Module.Height;
		// Publish raw generator masks before any owned Behavior may consume them.
		for (const TPair<FName, FRDGTextureRef>& Mask : Module.NamedMasks)
		{
			Ctx.PublishedMaskOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, Mask.Key), Mask.Value);
		}
		// One authored-order pass processes producers and height operations on the native field.
		if (!bNoiseHeightless)
		{
			// Seed native Flow before the ordered Behavior chain, never from its
			// already-modified result. Noise's completed diagnostic is still published
			// separately after normalization; it is not composed back into working Flow.
			const bool bNeedsWorkingFlow = Algo::AnyOf(Layer.Children,
				[&Child](const FChildRenderData& Candidate)
				{
					if (Candidate.ScopeOwnerSourceChildIndex != Child.SourceChildIndex) { return false; }
					if (Candidate.Type == EMixtormatLayerChildType::Generator)
					{
						return Candidate.Generator.Type == EMixtormatGeneratorType::Noise;
					}
					if (Candidate.Type != EMixtormatLayerChildType::Behavior) { return false; }
					const FBehaviorRenderData& Behavior = Candidate.Behavior;
					if (Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::None
						&& (Behavior.Type == EMixtormatBehaviorType::Warp
							|| Behavior.Type == EMixtormatBehaviorType::Deform)) { return true; }
					// Stage 1 leaves Strata Carver's existing demand and execution unchanged.
					if (Child.Generator.Type == EMixtormatGeneratorType::StrataCarver
						|| Behavior.Stage != EMixtormatBehaviorStage::PostGeneration) { return false; }
					return Behavior.Type == EMixtormatBehaviorType::Carve
						|| (Behavior.Type == EMixtormatBehaviorType::Deform && Behavior.bUseTracedFlow)
						|| (Behavior.Type == EMixtormatBehaviorType::Warp
							&& (Behavior.Flow.ModeMix > 0.0f || Behavior.Flow.SettingsDrivers[8].bEnabled));
				});
			const bool bNativeFlowGenerator =
				Child.Generator.Type == EMixtormatGeneratorType::Cracks
				|| Child.Generator.Type == EMixtormatGeneratorType::RockFormation
				|| Child.Generator.Type == EMixtormatGeneratorType::Pebbles
				|| Child.Generator.Type == EMixtormatGeneratorType::CliffStrata
				|| Child.Generator.Type == EMixtormatGeneratorType::RootNetwork
								|| Child.Generator.Type == EMixtormatGeneratorType::Scales;
			// Native Flow is an available typed output, not an unconditional GPU job.
			// Geometry fields can be cached even when they are large; the derived Flow
			// textures are transient and currently have no node-cache slot. Demand only
			// when a consumer, explicit output reference, or selected preview needs them.
			const bool bFlowSnapshotDemanded = Ctx.PublishedFieldDemand.Contains(PublishedKey(
				Layer, Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow));
			bool bColorRampNeedsFlow = false;
			for (const FChildRenderData& OtherChild : Layer.Children)
			{
				if (OtherChild.Type == EMixtormatLayerChildType::HeightColorRamp)
				{
					const FGeneratorHeightColorRampRenderData& Ramp = OtherChild.HeightColorRamp;
					const bool bTargetMatch = (Ramp.SourceChildIndex == Child.SourceChildIndex) || (Ramp.SourceChildIndex == INDEX_NONE);
					const bool bUsesFlow = (Ramp.Source == static_cast<int32>(EMixtormatColorRampSource::FlowMagnitude))
						|| (Ramp.Source == static_cast<int32>(EMixtormatColorRampSource::FlowAngle))
						|| (FMath::Abs(Ramp.FlowModulation) > 1.0e-5f);
					if (bTargetMatch && bUsesFlow)
					{
						bColorRampNeedsFlow = true;
						break;
					}
				}
			}
			const bool bNativeFlowDemanded = bNeedsWorkingFlow || bFlowSnapshotDemanded || bColorRampNeedsFlow
				|| Ctx.PublishedFieldDemand.Contains(PublishedKey(
					Layer, Child.SourceChildIndex, FName(TEXT("FlowDirection"))))
				|| IsChildOutputPreviewTarget(Ctx.Request,
					EMixtormatPreviewOutputKind::FlowDirection,
					FName(TEXT("FlowDirection")), LayerCtx.LayerIndex, Child.SourceChildIndex);
			if (bNativeFlowGenerator && bNativeFlowDemanded)
			{
				// Geometry-derived native Flow before Behaviors; one seed, never twice.
				// Rock/Pebbles/Cracks use signed boundaries; Cliff uses native height.
				const bool bHasBoundary = Module.BoundaryField
					&& Module.BoundaryField->Desc.Format == PF_G32R32F;
				AddNativeFlowPass(Ctx, Layer,
					Child.SourceChildIndex, bHasBoundary ? Module.BoundaryField : Module.Height,
					bHasBoundary ? ENativeFlowSource::SignedBoundary : ENativeFlowSource::SignedHeight,
					bHasBoundary ? Module.RegionIds : nullptr, &Child.Generator.NativeFlow, Module.Height);

			}
			else if (bNeedsWorkingFlow || bFlowSnapshotDemanded || bColorRampNeedsFlow)
			{
				AddNativeFlowPass(Ctx, Layer, Child.SourceChildIndex, Module.Height);
			}
			ApplyGeneratorPostBehaviors(Ctx, LayerCtx, Layer, Child, Module, OperandRunningHeight);
			// Normalize the unmasked Noise native height, then apply its scoped gate to the
			// resolved signed result. Gating before centring turns masked zeros into negative height.
			// Other generators keep the historical zero-preserving max-abs contract.
			const FRDGTextureRef NoiseHeightGate =
				Child.Generator.Type == EMixtormatGeneratorType::Noise
				&& HasScopedGeneratorMasks(Layer, Child.SourceChildIndex)
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: nullptr;
			Module.Height = AddSignedGeneratorHeightPasses(
				Ctx.GraphBuilder, Module.Height, Size,
				Child.Generator.bNormalizeHeight, Child.Generator.HeightScale,
				Child.Generator.HeightBias,
				Child.Generator.Type == EMixtormatGeneratorType::Noise,
				NoiseHeightGate,
				TEXT("Mixtormat.Generator.SignedHeight"));

			if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
			{
				AddNativeFlowPass(Ctx, Layer, Child.SourceChildIndex, Module.Height);
			}
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
		{
			// Direct Feather owns this Noise module's typed Flow outputs as well
			// as its native height. This must run AFTER AddNativeFlowPass, which
			// replaces FlowDirection when Write Height is enabled. Otherwise
			// Flow preview and consumers bypass the child's filter silently.
			for (const FChildRenderData& Filter : Layer.Children)
			{
				if (Filter.Type != EMixtormatLayerChildType::Behavior
					|| Filter.Behavior.Type != EMixtormatBehaviorType::Feather
					|| Filter.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
					|| Filter.Behavior.GeneratorChildIndex != Child.SourceChildIndex) { continue; }
				const bool bMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
				FRDGTextureRef Gate = bMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
					: nullptr;
				for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
				{
					const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
					const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
					if (!Field || !Field->IsComplete()
						|| Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
					const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
						Filter.Behavior, Gate, bMask, LayerCtx.LayerIndex, Filter.SourceChildIndex);
					Ctx.PublishedFieldOutputs.Add(Key, Filtered);
					if (Name == FName(TEXT("GeneratedFlow"))
						|| !Ctx.PublishedFieldOutputs.Contains(PublishedKey(Layer,
							Child.SourceChildIndex, FName(TEXT("GeneratedFlow")))))
					{
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Filter.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
						if (bNoiseHeightless)
						{
							Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
								Filter.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
						}
					}
				}
			}
		}
		if (bNoiseHeightless)
		{
			FMixtormatNoiseRenderData Noise;
			if (MixtormatNoiseRenderStore().Find(
				FMixtormatNoiseRenderKey{Layer.LayerId, Child.SourceChildIndex}, Noise))
			{
				const FName ResultName = Noise.bWriteFlow
					? FName(TEXT("GeneratedFlow")) : FName(TEXT("FlowDirection"));
				if (const FPublishedField* Result = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, ResultName)))
				{
					if (Result->IsComplete())
					{
						const FPublishedField Snapshot = *Result;
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Snapshot);
					}
				}
			}
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
		{
			for (const FName Name : {FName(TEXT("Value")), FName(TEXT("Gradient")), FName(TEXT("FlowDirection")), FName(TEXT("GeneratedFlow"))})
			{
				const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, Name));
				if (!Field) { continue; }

				// Keep authored mask references to Value working, while Copy preserves its typed payload.
				if (Name == FName(TEXT("Value")))
				{
					Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name), Field->Texture);
				}
			}
		}
		// Retain this module's own signed height so a later Height Blend sublayer can reference it.
		// Height-less Noise retains nothing and combines nothing: zero contribution, no phantom
		// relief through Height Blend, no normalization side effects.
		if (Module.Height)
		{
			LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Module.Height);
			// Explicit inputs address the completed signed module result, never the running sum.
			Ctx.PublishedFieldOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Height"))),
				FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Module.Height, nullptr, nullptr, false});
		}

		// The only publication point: every module publishes under its own child index, after its
		// flow. The layer's default IDs are therefore the last module that produced any.
		if (Module.RegionIds)
		{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Module.RegionIds);
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::RegionIds,
				NAME_None, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(Ctx.GraphBuilder, Module.RegionIds, nullptr, Debug, Size);
			}
		}
		for (const TPair<FName, FRDGTextureRef>& Mask : Module.NamedMasks)
		{
			Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Mask.Key), Mask.Value);
			const bool bDistance = Mask.Key == FName(TEXT("RockEdgeDistance"))
				|| Mask.Key == FName(TEXT("PebbleEdgeDistance"))
				|| Mask.Key == FName(TEXT("RootEdgeDistance"));
			// Preserve legacy mask consumers while publishing the declared signed
			// UV-distance metric for typed Carve / Deposit consumers.
			if (bDistance && Mask.Value && Mask.Value->Desc.Format == PF_R32_FLOAT)
			{
				Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Mask.Key),
					FPublishedField{EMixtormatPublishedFieldKind::SDF, Mask.Value, nullptr, nullptr, false});
			}
			if (bDistance && IsChildOutputPreviewTarget(Ctx.Request,
				EMixtormatPreviewOutputKind::SignedDistance,
				Mask.Key, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewSignedDistanceBlitPass(Ctx.GraphBuilder, Mask.Value, (float)Size.X, Debug, Size);
			}
		}

		// Height-less Noise contributes nothing to the running height.
		if (Module.Height)
		{
			if (!bBooleanOperand && !HeightBlendOperandIndices.Contains(Child.SourceChildIndex))
			{
				AddGeneratorModuleCombine(Ctx, RunningHeight, Module, Child, RunningHeight);
				bActualHeightAuthored = true;
			}
		}
	}
	FinishBoolean();
	// Color Ramp only publishes Color. The layer owns whether the last valid ordered
	// Color result becomes this layer's Base Color input.
	if (GeneratedColor && Layer.bGeneratorAlbedo)
	{
		LayerCtx.LayerInputBC = GeneratedColor;
		LayerCtx.bGeneratorColor = true;
	}
	if (!RunningHeight || !bActualHeightAuthored) { return; }
	Bundle.Height = RunningHeight;
	if (LayerCtx.LayerInputHeight)
	{
		FGeneratorBundle DeltaBundle;
		DeltaBundle.Height = RunningHeight;
		FChildRenderData DummyChild;
		FRDGTextureRef CombinedLayerHeight = nullptr;
		AddGeneratorModuleCombine(
			Ctx, LayerCtx.LayerInputHeight, DeltaBundle, DummyChild, CombinedLayerHeight);
		LayerCtx.LayerInputHeight = CombinedLayerHeight;
	}
}

}
