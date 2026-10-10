// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "Compositing/MixtormatComposeHash.h"
#include "MixtormatGpuNoisePasses.h"
#include "MixtormatColorRampMath.h"
#include "MixtormatScalarRampMath.h"

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

// Geological beds with shared interfaces, hard shelves and joint-cut slabs. Writes bed IDs,
// position inside each bed and per-bed random alongside the signed height.
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
		SHADER_PARAMETER(float, LedgeWidth)
		SHADER_PARAMETER(float, HardnessContrast)
		SHADER_PARAMETER(float, SoftRecession)
		SHADER_PARAMETER(float, Bend)
		SHADER_PARAMETER(int32, BendScale)
		SHADER_PARAMETER(float, Breakup)
		SHADER_PARAMETER(int32, JointScale)
		SHADER_PARAMETER(float, JointWidth)
		SHADER_PARAMETER(float, HeightFollow)
		SHADER_PARAMETER(float, Lamination)
		SHADER_PARAMETER(float, CrossBedding)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER(uint32, HasHeightPush)
		SHADER_PARAMETER(uint32, HasStructuralWarp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, StructuralDisplacement)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HeightPushField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolveRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutBedIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBedPosition)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBedRandom)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutBoundaryField)
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


class FMixtormatCliffStrataCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCliffStrataCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCliffStrataCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("CLIFF_STAGE", 9);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
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
	const TCHAR* Name)
{
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
	// The reduce is only needed for the zero-preserving normalization. Normalize off uses the raw
	// signed field, so the min/max dispatch is skipped entirely.
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
		P->NormalizeMode = bNormalize ? 1u : 2u;
		P->OutputScale = FMath::IsFinite(OutputScale) ? OutputScale : 1.0f;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Signed);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.GeneratorSigned.Resolve"), Shader, P, Groups);
	}
	return Signed;
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
	// 7 one axis of the direction blur, 8 trace a published field into destination UVs,
	// 9 texture-space gravity with local height or boundary steering.
	class FStage : SHADER_PERMUTATION_INT("FLOW_STAGE", 10);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(int32, JumpStep)
		SHADER_PARAMETER(uint32, Source)
		SHADER_PARAMETER(float, Tangent)
		SHADER_PARAMETER(float, Angle)
		SHADER_PARAMETER(float, GravitySurfaceFollow)
		SHADER_PARAMETER(float, GravityDeflection)
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
	class FStage : SHADER_PERMUTATION_INT("BUNDLE_STAGE", 12);
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

// Generator-layer Height Blend sublayer: combines the running signed height with another module's.
class FMixtormatGeneratorHeightPushCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightPushCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightPushCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(uint32, HasPrevious)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousShift)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutShift)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightPushCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightPush.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorStructuralWarpCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorStructuralWarpCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorStructuralWarpCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, HasPreviousDisplacement)
		SHADER_PARAMETER(uint32, HasPreviousShift)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, WarpCoordinates)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreviousDisplacement)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousShift)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutDisplacement)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutShift)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorStructuralWarpCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorStructuralWarp.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorStructuralWarpCoordinateCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorStructuralWarpCoordinateCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorStructuralWarpCoordinateCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreviousDisplacement)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorStructuralWarpCoordinateCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorStructuralWarp.usf", "CoordinateCS", SF_Compute);

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
		SHADER_PARAMETER(uint32, bNormalizeInput)
		SHADER_PARAMETER(float, InputMin)
		SHADER_PARAMETER(float, InputMax)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(uint32, bInvert)
		SHADER_PARAMETER(uint32, CurveCount)
		SHADER_PARAMETER(uint32, CurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, CurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
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
		SHADER_PARAMETER_ARRAY(FVector4f, Positions, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER_ARRAY(FVector4f, Colors, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER(uint32, HasGate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightColorRampCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightColorRamp.usf", "MainCS", SF_Compute);

namespace
{
	// A module of a Generator layer: its payload and its child index in that layer. Publication
	// happens once per module in AddGeneratorLayerPasses, after flow.
	struct FGeneratorPassInput
	{
		const FGeneratorRenderData& Generator;
		int32 SourceChildIndex;
	};

	// Shared Flow -> destination-UV binding for layer references and explicit generator inputs.
	// The producer's typed field stays intact; only the consumer's placement is traced.
	FRDGTextureRef AddReferencedFlowUVPass(FMixtormatComposeContext& Ctx,
		const FOutputReferenceRenderData& Reference, const FPublishedField& Field,
		const int32 LayerIndex, const int32 ChildIndex)
	{
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
			RDG_EVENT_NAME("Mixtormat.OutputReference.Flow.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Coordinates;
	}

	// Every module sits in the Generator layer's UV placement, so the layer moves all of them.
	template<typename TParameters>
	void FillGeneratorPlacement(TParameters* P, const FLayerRenderData& Layer)
	{
		P->GeneratorLayer = 1u;
		P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
		P->GeneratorUVOffset = Layer.UVOffset;
		P->GeneratorUVRotation = Layer.Rotation;
		P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
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
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Generator.HeightCurve"));
		TShaderMapRef<FMixtormatGeneratorHeightCurveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightCurveCS::FParameters>();
		P->OutputSize = Size;
		P->Amount = Curve.Amount;
		P->bNormalizeInput = Curve.bNormalizeInput;
		P->InputMin = Curve.InputMin;
		P->InputMax = Curve.InputMax;
		P->Balance = Curve.Balance;
		P->Contrast = Curve.Contrast;
		P->Offset = Curve.Offset;
		P->bInvert = Curve.bInvert;
		P->CurveCount = Curve.CurveCount;
		P->CurveInterpolation = Curve.CurveInterpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
		{
			P->CurvePoints[Index] = Curve.CurvePoints[Index];
		}
		P->RunningHeight = RunningHeight;
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
		FRDGTextureRef Gate, const FGeneratorHeightColorRampRenderData& Ramp)
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
		for (int32 Index = 0; Index < FMixtormatColorRamp::MaxStops; ++Index)
		{
			P->Positions[Index] = FVector4f(Ramp.Positions[Index], 0.0f, 0.0f, 0.0f);
			P->Colors[Index] = Ramp.Colors[Index];
		}
		P->HasGate = Ramp.bHasGate;
		P->SourceHeight = SourceHeight;
		P->Gate = Gate;
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

	// A step-7 completed-bundle operation owns every moved output. It is intentionally separate
	// from flow apply, which has already authored Height/Coverage before companion remapping.
	void RemapCompletedGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle,
		FRDGTextureRef CompletedSignedHeight, FRDGTextureRef WarpedUV)
	{
		const FGeneratorBundle Source = Bundle;
		if (!CompletedSignedHeight) { return; }
		// Shared normalization/Height Scale already resolved Module.Height; remap that exact
		// published signed result rather than the bundle's pre-normalized native height.
		Bundle.Height = RemapBundleField(Ctx, CompletedSignedHeight, WarpedUV, 0);
		if (Source.Coverage)
		{
			Bundle.Coverage = RemapBundleField(Ctx, Source.Coverage, WarpedUV, 0);
		}
		RemapGeneratorBundle(Ctx, Bundle, WarpedUV);
	}

	FRDGTextureRef AddStructuralWarpCoordinates(FMixtormatComposeContext& Ctx, FRDGTextureRef Displacement,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Coordinates = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.StructuralWarp.Coordinates"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatGeneratorStructuralWarpCoordinateCS::FParameters>();
		P->OutputSize = Size;
		P->PreviousDisplacement = Displacement;
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Coordinates);
		TShaderMapRef<FMixtormatGeneratorStructuralWarpCoordinateCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.StructuralWarp.Coordinates.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Coordinates;
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

	// One Strata Carver child.
	//
	// Produces a signed geological relief field. Height Follow samples the upstream composite;
	// combination and normalization remain owned by the shared generator-layer pipeline.
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

		// No scoped mask and no upstream IDs yet: modules do not read each other until Region
		// inputs land, so the shader's gated paths stay off and these only satisfy the bindings.
		const bool bHasScopedMask = false;
		FRDGTextureRef ScopedMask = SourceHeight;
		const bool bHasRegionIds = false;
		FRDGTextureRef RegionIds = Ctx.EmptyRegionIds;

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
		const FRDGTextureRef* Displacement = LayerCtx.GeneratorStructuralDisplacements.Find(Child.SourceChildIndex);
		const bool bHasStructuralWarp = Displacement && *Displacement;
		FRDGTextureRef DirectBoundary = MakeField(PF_G32R32F, TEXT("Mixtormat.Strata.DirectBoundary"));

		{
			FMixtormatStrataCarverResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverResolveCS::FParameters>();
			FillGeneratorPlacement(P, Layer);
			P->BedWave = Lattice.Wave;
			P->BedPerp = Lattice.Perp;
			P->BedPeriod = Lattice.Period;
			P->OutputSize = Size;
			P->Seed = Carver.Seed;
			P->ThicknessVariation = Carver.ThicknessVariation;
			P->HeightVariation = Carver.HeightVariation;
			P->Verticality = Carver.Verticality;
			P->LedgeWidth = Carver.LedgeWidth;
			P->HardnessContrast = Carver.HardnessContrast;
			P->SoftRecession = Carver.SoftRecession;
			P->Bend = Carver.Bend;
			P->BendScale = Carver.BendScale;
			P->Breakup = Carver.Breakup;
			P->JointScale = Carver.JointScale;
			P->JointWidth = Carver.JointWidth;
			P->HeightFollow = Carver.HeightFollow;
			P->Lamination = Carver.Lamination;
			P->CrossBedding = Carver.CrossBedding;
			P->Depth = Carver.Depth;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			const FRDGTextureRef* Push = LayerCtx.GeneratorHeightPushFields.Find(Child.SourceChildIndex);
			P->HasHeightPush = Push && *Push ? 1u : 0u;
			P->HeightPushField = Push && *Push ? *Push : SourceHeight;
			P->HasStructuralWarp = bHasStructuralWarp ? 1u : 0u;
			P->StructuralDisplacement = bHasStructuralWarp ? *Displacement : Ctx.EmptyPatternUV;
			P->SourceHeight = SourceHeight;
			P->ResolveMask = ScopedMask;
			P->ResolveRegionIds = RegionIds;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(CarvedHeight);
			P->OutBedIds = GraphBuilder.CreateUAV(BedIds);
			P->OutBedPosition = GraphBuilder.CreateUAV(BedPosition);
			P->OutBedRandom = GraphBuilder.CreateUAV(BedRandom);
			P->OutBoundaryField = GraphBuilder.CreateUAV(DirectBoundary);
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

			Bundle->RegionIds = BedIds;
			Bundle->BoundaryField = bHasStructuralWarp ? DirectBoundary
				: RemapBundleField(Ctx, BedPosition, nullptr, 8);
			Bundle->RegisterNamedMask(FName(TEXT("StrataPosition")), BedPosition,
				FGeneratorBundle::EFieldSemantic::BedCoordinate, FGeneratorBundle::EFieldUnits::BedFraction);
			Bundle->RegisterNamedMask(FName(TEXT("StrataRandom")), BedRandom,
				FGeneratorBundle::EFieldSemantic::RegionAttribute);
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
		// Ck, Pk, ChipOn, EdgeCount, VertexCount, Id, SeamBits, Pad.
		uint32 Uints[8];
	};
	static_assert(sizeof(FRockLeafStride) == 68, "Rock leaf stride must match FRockLeaf in HLSL");
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
		case EMixtormatEffectType::GravityFlow:
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
			PublishedKey(Layer, ChildIndex, FName(TEXT("FlowDirection"))))
			|| Ctx.PublishedFieldDemand.Contains(
				PublishedKey(Layer, ChildIndex, FName(TEXT("WarpedUV"))));
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
	// InOutCoverage, when the owner has one (Pebbles), is transformed with the height.
	// Generator modules retain it for the named PebbleCoverage output, not height blending.
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
			const bool bGravity = Flow.Type == EMixtormatEffectType::GravityFlow;
			// Height-only producers must never bind a null boundary or invent an SDF.
			if (!BoundaryField && Flow.GeneratorFlowSource == static_cast<uint32>(EMixtormatGeneratorFlowSource::SignedDistance)) { continue; }
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
				P->GravitySurfaceFollow = Flow.GravityFlowSurfaceFollow;
				P->GravityDeflection = Flow.GravityFlowDeflection;
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
				P->Mode = bGravity ? 3u : (Flow.Type == EMixtormatEffectType::ShapeDeform ? 0u
					: (Flow.Type == EMixtormatEffectType::GeneratorFlow ? 1u : 2u));
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
				P->BoundaryField = BoundaryField ? BoundaryField : Ctx.EmptyPatternUV;
				P->FlowMask = Mask;
				P->HasCoverage = InOutCoverage ? 1u : 0u;
				P->GeneratorLayer = Bundle ? 1u : 0u;
				P->Coverage = InOutCoverage ? InOutCoverage : Current;
			};

			// Gravity + Height is evaluated directly at full resolution, including flat texels.
			FRDGTextureRef SeedData = Ctx.EmptyPatternUV;
			FRDGTextureRef JumpResult = Ctx.EmptyPatternUV;
			if (!bGravity || Flow.GeneratorFlowSource == static_cast<uint32>(EMixtormatGeneratorFlowSource::SignedDistance))
			{
				SeedData = MakeTexture(SolveSize, PF_A32B32G32R32F, TEXT("Mixtormat.GeneratorFlow.Seeds"));
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

				// Halving strides plus one extra stride-1 pass to repair the usual JFA misses.
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
				JumpResult = Jump[Read];
			}

			// Resolve at full resolution. Half precision holds a unit direction, a UV distance
			// under 1 and a 0..1 influence; WarpedUV below must stay 32F to resolve 4K texels.
			FRDGTextureRef FlowField = MakeTexture(Size, PF_FloatRGBA, TEXT("Mixtormat.GeneratorFlow.Field"));
			FRDGTextureRef Influence = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.Influence"));
			FRDGTextureRef Validity = MakeTexture(Size, PF_R16F, TEXT("Mixtormat.GeneratorFlow.Validity"));
			{
				TShaderMapRef<FMixtormatGeneratorFlowCS> Shader = StageShader(bGravity ? 9 : 2);
				auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
				Fill(P);
				P->SeedData = SeedData;
				P->JumpIn = JumpResult;
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
				PublishedKey(Layer, FlowIndex, FName(TEXT("FlowDirection"))),
				FPublishedField{EMixtormatPublishedFieldKind::Flow, FlowField, FlowSmooth, Validity, false});
			if (Flow.Type != EMixtormatEffectType::FlowCarve)
			{
				Ctx.PublishedFieldOutputs.Add(
					PublishedKey(Layer, FlowIndex, FName(TEXT("WarpedUV"))),
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
						// Noise's raw outputs are published separately from the shared generator bundle.
						for (const FName Name : {FName(TEXT("Value")), FName(TEXT("Gradient"))})
						{
							const FPublishedFieldKey Key = PublishedKey(Layer, OwnerSourceChildIndex, Name);
							const FPublishedField* Published = Ctx.PublishedFieldOutputs.Find(Key);
							if (!Published || !Published->IsComplete()) { continue; }
							FPublishedField Moved = *Published;
							Moved.Texture = RemapBundleField(Ctx, Moved.Texture, WarpedUV,
								Moved.Kind == EMixtormatPublishedFieldKind::Vector2 ? 4 : 0);
							Ctx.PublishedFieldOutputs.Add(Key, Moved);
						}
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
		FGeneratorBundle* Bundle = nullptr)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRockFormationRenderData& Rock = Child.Generator.RockFormation;
		const FIntPoint Size = Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);

		const FRockLayout Layout = ResolveRockLayout(Rock);
		const auto FillParameters = [&Rock, &Layout, &Layer, Size](FMixtormatRockFormationCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer);
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
			const uint64 NodeKey = NodeCache && Rock.FieldKey != 0
				? MixtormatComposeHash::Combine(Rock.FieldKey, 0x526F636B09ull)
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
		const auto Fill = [&Cracks, &Layer, &Child, Size, SolveSize](FMixtormatCracksCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer);
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
			const uint64 NodeKey = NodeCache && Cracks.FieldKey != 0
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

		const auto FillParameters = [&Pebbles, &Layer, Size](FMixtormatPebblesCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer);
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
			const uint64 NodeKey = NodeCache && Pebbles.FieldKey != 0
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

	FRDGTextureRef AddCliffStrataPasses(FMixtormatComposeContext& Ctx,FMixtormatLayerPassContext& LayerCtx,const FLayerRenderData& Layer,const FGeneratorPassInput& Child,FGeneratorBundle* Bundle=nullptr)
	{
		FRDGBuilder& GraphBuilder=Ctx.GraphBuilder;const FCliffStrataRenderData& C=Child.Generator.CliffStrata;const FIntPoint Size=Ctx.Request.Resolution;
		const FIntVector PixelGroups(FMath::DivideAndRoundUp(Size.X,8),FMath::DivideAndRoundUp(Size.Y,8),1);
		const auto Make=[&](EPixelFormat F,const TCHAR* N){return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size,F,FClearValueBinding::Black,TexCreate_ShaderResource|TexCreate_UAV),N);};
		constexpr int32 SlotCount=10;FRDGTextureRef O[SlotCount]={};
		if(const TArray<FRDGTextureRef,TInlineAllocator<7>>* Memo=LayerCtx.GeneratorFields.Find(Child.SourceChildIndex)){for(int i=0;i<SlotCount;++i)O[i]=(*Memo)[i];}
		else{
			static const TCHAR* Names[SlotCount]={TEXT("Mixtormat.CliffStrata.RawHeight"),TEXT("Mixtormat.CliffStrata.Flow"),TEXT("Mixtormat.CliffStrata.BlockIds"),TEXT("Mixtormat.CliffStrata.RowIds"),TEXT("Mixtormat.CliffStrata.VoronoiRaw"),TEXT("Mixtormat.CliffStrata.FinalHeight"),TEXT("Mixtormat.CliffStrata.BlockSeam"),TEXT("Mixtormat.CliffStrata.RowSeam"),TEXT("Mixtormat.CliffStrata.Cavity"),TEXT("Mixtormat.CliffStrata.Coverage")};
			FMixtormatNodeCache* NodeCache=Ctx.Request.NodeCache.Get();const uint64 NodeKey=NodeCache&&C.FieldKey?MixtormatComposeHash::Combine(C.FieldKey,0x436C696666537472ull):0;
			auto Hit=NodeKey?NodeCache->Find(NodeKey,Size):TSharedPtr<FMixtormatNodeCacheEntry,ESPMode::ThreadSafe>();bool complete=Hit.IsValid();for(int i=0;complete&&i<SlotCount;++i)complete=Hit->Outputs[i].IsValid();
			if(complete){for(int i=0;i<SlotCount;++i)O[i]=GraphBuilder.RegisterExternalTexture(Hit->Outputs[i],Names[i]);}
			else{
				O[0]=Make(PF_R32_FLOAT,Names[0]);O[1]=Make(PF_G32R32F,Names[1]);O[2]=Make(PF_R32_UINT,Names[2]);O[3]=Make(PF_R32_UINT,Names[3]);O[4]=Make(PF_R32_FLOAT,Names[4]);O[5]=Make(PF_R32_FLOAT,Names[5]);O[6]=Make(PF_R16F,Names[6]);O[7]=Make(PF_R16F,Names[7]);O[8]=Make(PF_R16F,Names[8]);O[9]=Make(PF_R16F,Names[9]);
				FRDGTextureRef Smooth=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.VoronoiSmooth")),SweepA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.SweepA")),SweepB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.SweepB")),IdA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.IdA")),IdB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.IdB")),RowA=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.RowA")),RowB=Make(PF_R32_FLOAT,TEXT("Mixtormat.CliffStrata.RowB"));
				auto Fill=[&](FMixtormatCliffStrataCS::FParameters* P){FillGeneratorPlacement(P,Layer);P->OutputSize=Size;P->CountX=C.CountX;P->CountY=C.CountY;P->Density=C.Density;P->SizeMin=C.SizeMin;P->SizeMax=C.SizeMax;P->SizeAspect=C.SizeAspect;P->Jitter=C.Jitter;P->FlowVariation=C.FlowVariation;P->HeightMin=C.HeightMin;P->HeightMax=C.HeightMax;P->Steps=C.Steps;P->Rotation=C.Rotation;P->LeanX=C.LeanX;P->LeanY=C.LeanY;P->FormationCells=C.FormationCells;P->FormationAmount=C.FormationAmount;P->QuarterCopies=C.bQuarterCopies?1u:0u;P->QuarterYCount=C.QuarterYCount;P->QuarterFill=C.QuarterFill;P->QuarterSize=C.QuarterSize;P->QuarterHeight=C.QuarterHeight;P->QuarterJitterX=C.QuarterJitterX;P->QuarterJitterY=C.QuarterJitterY;P->Sides=C.Sides;P->ShapeRandom=C.bShapeRandom?1u:0u;P->CameraYaw=C.CameraYaw;P->CameraPitch=C.CameraPitch;P->ViewScale=C.ViewScale;P->DepthMin=C.DepthMin;P->DepthMax=C.DepthMax;P->UnitDistance=C.UnitDistance;P->UnitDistanceIdLerp=C.UnitDistanceIdLerp;P->CarveDepth=C.CarveDepth;P->CarveVoronoi=C.CarveVoronoi;P->YBias=C.YBias;P->YBiasVoronoi=C.YBiasVoronoi;P->YBiasVoronoiInvert=C.bYBiasVoronoiInvert?1u:0u;P->NegativeYUnitDistanceTaper=C.NegativeYUnitDistanceTaper;P->Reverse=C.bReverse?1u:0u;P->Seed=C.Seed;P->VoronoiCells=C.VoronoiCells;P->FlowVoronoi=C.FlowVoronoi;P->ChamferWidth=C.ChamferWidth;P->ChamferIntensity=C.ChamferIntensity;P->ChamferVoronoi=C.ChamferVoronoi;P->BlockCavityWidth=C.BlockCavityWidth;P->RowCavityWidth=C.RowCavityWidth;P->CavityIntensity=C.CavityIntensity;P->CavityVoronoiThreshold=C.CavityVoronoiThreshold;P->CavityVoronoiMaskGain=C.CavityVoronoiMaskGain;P->RawHeight=O[0];P->Flow=O[1];P->BlockIds=O[2];P->RowIds=O[3];P->VoronoiRaw=O[4];P->VoronoiSmooth=Smooth;P->SweepIn=SweepA;P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;};
				for(int Stage=0;Stage<9;++Stage){FMixtormatCliffStrataCS::FPermutationDomain Perm;Perm.Set<FMixtormatCliffStrataCS::FStage>(Stage);TShaderMapRef<FMixtormatCliffStrataCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel),Perm);auto* P=GraphBuilder.AllocParameters<FMixtormatCliffStrataCS::FParameters>();Fill(P);P->OutRawHeight=GraphBuilder.CreateUAV(O[0]);P->OutFlow=GraphBuilder.CreateUAV(O[1]);P->OutBlockIds=GraphBuilder.CreateUAV(O[2]);P->OutRowIds=GraphBuilder.CreateUAV(O[3]);P->OutVoronoiRaw=GraphBuilder.CreateUAV(O[4]);P->OutVoronoiSmooth=GraphBuilder.CreateUAV(Smooth);P->OutSweep=GraphBuilder.CreateUAV(Stage==3?SweepA:SweepB);P->OutIdDistance=GraphBuilder.CreateUAV((Stage==5||Stage==7)?IdA:IdB);P->OutRowDistance=GraphBuilder.CreateUAV((Stage==5||Stage==7)?RowA:RowB);P->OutHeight=GraphBuilder.CreateUAV(O[5]);P->OutBlockSeam=GraphBuilder.CreateUAV(O[6]);P->OutRowSeam=GraphBuilder.CreateUAV(O[7]);P->OutCavity=GraphBuilder.CreateUAV(O[8]);P->OutCoverage=GraphBuilder.CreateUAV(O[9]);if(Stage==4)P->SweepIn=SweepA;if(Stage==6){P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;}if(Stage==7){P->IdDistanceIn=IdB;P->RowDistanceIn=RowB;}if(Stage==8){P->SweepIn=SweepB;P->IdDistanceIn=IdA;P->RowDistanceIn=RowA;}ClearUnusedGraphResources(Shader,P);const FIntVector Groups=(Stage==3||Stage==6)?FIntVector(1,Size.Y,1):((Stage==4||Stage==7)?FIntVector(Size.X,1,1):PixelGroups);FComputeShaderUtils::AddPass(GraphBuilder,RDG_EVENT_NAME("Mixtormat.CliffStrata.S%d.L%d.C%d",Stage,LayerCtx.LayerIndex,Child.SourceChildIndex),Shader,P,Groups);}
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

static void AddPublishedFieldPreview(FMixtormatComposeContext& Ctx,
	const FMixtormatLayerPassContext& LayerCtx, const int32 ChildIndex,
	const FName OutputName, const FPublishedField& Field)
{
	if (!Field.IsComplete()) { return; }
	const bool bScalar = Field.Kind == EMixtormatPublishedFieldKind::Scalar01
		|| Field.Kind == EMixtormatPublishedFieldKind::ScalarSigned;
	const bool bVector = Field.Kind == EMixtormatPublishedFieldKind::Vector2;
	const bool bFlow = Field.Kind == EMixtormatPublishedFieldKind::Flow;
	if (!bScalar && !bVector && !bFlow) { return; }
	const EMixtormatPreviewOutputKind PreviewKind = bScalar
		? EMixtormatPreviewOutputKind::Mask : EMixtormatPreviewOutputKind::FlowDirection;
	if (!IsChildOutputPreviewTarget(Ctx.Request, PreviewKind, OutputName, LayerCtx.LayerIndex, ChildIndex)) { return; }
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
	if (bScalar)
	{
		AddDebugPreviewScalarBlitPass(Ctx.GraphBuilder, Field.Texture,
			Field.Kind == EMixtormatPublishedFieldKind::ScalarSigned, Debug, Size);
	}
	else if (bVector)
	{
		AddDebugPreviewVectorBlitPass(Ctx.GraphBuilder, Field.Texture, Debug, Size);
	}
	else
	{
		FMixtormatGeneratorFlowCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorFlowCS::FStage>(4);
		TShaderMapRef<FMixtormatGeneratorFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatGeneratorFlowCS::FParameters>();
		P->OutputSize = Size;
		P->FlowField = Field.FlowSmooth;
		P->FlowValidity = Field.Validity;
		P->OutputDebug = Ctx.GraphBuilder.CreateUAV(Debug);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.PublishedFlow.Preview.C%d", ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
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
		AddPublishedFieldPreview(Ctx, LayerCtx, Child.SourceChildIndex, Reference.Source.Output, Field);
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

	// Modules compose in child order from signed zero; zero is the neutral generator height.
	for (const FChildRenderData& Child : Layer.Children)
	{
		// Structural input modules run at their own authored position, before a later target.
		if (Child.Type == EMixtormatLayerChildType::StructuralWarp)
		{
			const FGeneratorStructuralWarpRenderData& Warp = Child.StructuralWarp;
			const FPublishedField* FoundSource = Ctx.PublishedFieldOutputs.Find(Warp.Source.Source);
			if (Warp.TargetChildIndex == INDEX_NONE || !FoundSource || !FoundSource->IsComplete()
				|| FoundSource->Kind != Warp.Source.Kind || FoundSource->Texture->Desc.Extent != Size
				|| (Warp.Source.Kind != EMixtormatPublishedFieldKind::Flow
					&& Warp.Source.Kind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
			if (Warp.Source.Kind == EMixtormatPublishedFieldKind::Flow
				&& (Warp.Source.FlowAmount == 0.0f || Warp.Source.FlowTraceLength == 0.0f)) { continue; }
			const FPublishedField Source = *FoundSource;
			const FRDGTextureRef Coordinates = Warp.Source.Kind == EMixtormatPublishedFieldKind::Flow
				? AddReferencedFlowUVPass(Ctx, Warp.Source, Source, LayerCtx.LayerIndex, Child.SourceChildIndex)
				: Source.Texture;
			AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
			const bool bHasMask = HasScopedMasks(Layer, Child.SourceChildIndex);
			const FRDGTextureRef Mask = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true) : RunningHeight;
			const FRDGTextureRef* OldD = LayerCtx.GeneratorStructuralDisplacements.Find(Warp.TargetChildIndex);
			const FRDGTextureRef* OldB = LayerCtx.GeneratorHeightPushFields.Find(Warp.TargetChildIndex);
			const auto MakeState = [&](EPixelFormat Format, const TCHAR* Name)
			{
				return Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
					Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
			};
			FRDGTextureRef NewD = MakeState(PF_G32R32F, TEXT("Mixtormat.Generator.StructuralDisplacement"));
			FRDGTextureRef NewB = MakeState(PF_R32_FLOAT, TEXT("Mixtormat.Generator.WarpedBeddingShift"));
			auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatGeneratorStructuralWarpCS::FParameters>();
			P->OutputSize = Size;
			P->HasPreviousDisplacement = OldD && *OldD ? 1u : 0u;
			P->HasPreviousShift = OldB && *OldB ? 1u : 0u;
			P->HasMask = bHasMask ? 1u : 0u;
			P->WarpCoordinates = Coordinates;
			P->PreviousDisplacement = OldD && *OldD ? *OldD : Coordinates;
			P->PreviousShift = OldB && *OldB ? *OldB : RunningHeight;
			P->ScopedMask = Mask;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutDisplacement = Ctx.GraphBuilder.CreateUAV(NewD);
			P->OutShift = Ctx.GraphBuilder.CreateUAV(NewB);
			TShaderMapRef<FMixtormatGeneratorStructuralWarpCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StructuralWarp.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
			const bool bHadShift = OldB && *OldB;
			LayerCtx.GeneratorStructuralDisplacements.Add(Warp.TargetChildIndex, NewD);
			if (bHadShift) { LayerCtx.GeneratorHeightPushFields.Add(Warp.TargetChildIndex, NewB); }
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightPush)
		{
			const FGeneratorHeightPushRenderData& Push = Child.HeightPush;
			const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Push.Source.Source);
			if (Push.Amount == 0.0f || Push.TargetChildIndex == INDEX_NONE || !Source
				|| Source->Kind != EMixtormatPublishedFieldKind::ScalarSigned || !Source->IsComplete()
				|| Source->Texture->Desc.Extent != Size) { continue; }
			const FRDGTextureRef Height = Source->Texture;
			AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
			const bool bHasMask = HasScopedMasks(Layer, Child.SourceChildIndex);
			const FRDGTextureRef Mask = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true) : Height;
			const FRDGTextureRef* Found = LayerCtx.GeneratorHeightPushFields.Find(Push.TargetChildIndex);
			const FRDGTextureRef Previous = Found ? *Found : nullptr;
			FRDGTextureRef Shift = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Generator.HeightPush"));
			auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatGeneratorHeightPushCS::FParameters>();
			P->OutputSize = Size;
			P->Amount = Push.Amount;
			P->HasPrevious = Previous ? 1u : 0u;
			P->HasMask = bHasMask ? 1u : 0u;
			P->SourceHeight = Height;
			P->PreviousShift = Previous ? Previous : Height;
			P->ScopedMask = Mask;
			P->OutShift = Ctx.GraphBuilder.CreateUAV(Shift);
			TShaderMapRef<FMixtormatGeneratorHeightPushCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.HeightPush.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
				Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
			LayerCtx.GeneratorHeightPushFields.Add(Push.TargetChildIndex, Shift);
			continue;
		}
		// Generator-layer sublayers rewrite the running signed height in place (or publish colour).
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
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightCurve)
		{
			RunningHeight = AddGeneratorHeightCurvePass(Ctx, RunningHeight, Child.HeightCurve);
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightColorRamp)
		{
			// Resolve the height this ramp maps. The C++ side decides the source so the pass stays
			// a pure map: the running chain (default), an earlier module's published height, the
			// layer input, or the height accumulated below this layer. A ModuleRef that resolves
			// to nothing reads the running chain, which is the module's defined neutral.
			FRDGTextureRef RampSource = RunningHeight;
			switch (static_cast<EMixtormatColorRampSource>(Child.HeightColorRamp.Source))
			{
			case EMixtormatColorRampSource::ModuleRef:
				if (const FRDGTextureRef* Found = LayerCtx.GeneratorModuleHeights.Find(
					Child.HeightColorRamp.SourceChildIndex))
				{
					RampSource = *Found;
				}
				break;
			case EMixtormatColorRampSource::LayerHeight:
				RampSource = LayerCtx.LayerInputHeight;
				break;
			case EMixtormatColorRampSource::CompositeBelow:
				RampSource = Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)];
				break;
			default:
				break;
			}
			// A Mask scoped under this ramp gates where its colour shows, using the same independent
			// scope the generator flow tools use: the mask says where this module acts, not where
			// the layer is. The mask's own Weight dials how strongly it gates.
			const bool bHasGate = HasScopedMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef RampGate = bHasGate
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: RampSource;
			FGeneratorHeightColorRampRenderData RampData = Child.HeightColorRamp;
			RampData.bHasGate = bHasGate ? 1u : 0u;
			FRDGTextureRef Color = AddGeneratorHeightColorRampPass(Ctx, RampSource, RampGate, RampData);
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
		// Only already-published maps may feed groups/references before this module.
		AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
		FGeneratorInputFields& Inputs = LayerCtx.GeneratorInputs.Add(Child.SourceChildIndex);
		const auto ResolveInput = [&](const FGeneratorInputRenderData& Input, FPublishedField& Out)
		{
			if (!Input.bRequested || Input.Reference.Source.ChildIndex == INDEX_NONE) { return; }
			const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Input.Reference.Source);
			if (Source && Source->Kind == Input.Reference.Kind && Source->IsComplete()) { Out = *Source; }
		};
		ResolveInput(Child.Generator.HeightSource, Inputs.Height);
		ResolveInput(Child.Generator.WarpSource, Inputs.Warp);
		if (Inputs.Warp.IsComplete() && Inputs.Warp.Texture->Desc.Extent == Size)
		{
			if (Inputs.Warp.Kind == EMixtormatPublishedFieldKind::Flow)
			{
				Inputs.WarpUV = AddReferencedFlowUVPass(Ctx, Child.Generator.WarpSource.Reference,
					Inputs.Warp, LayerCtx.LayerIndex, Child.SourceChildIndex);
			}
			else if (Inputs.Warp.Kind == EMixtormatPublishedFieldKind::UVMap)
			{
				Inputs.WarpUV = Inputs.Warp.Texture;
			}
		}
		FGeneratorBundle Module;
		const FGeneratorPassInput Input{Child.Generator, Child.SourceChildIndex};
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			// Height Follow reads the composite below, never this layer's own height or IDs.
			AddStrataCarverPasses(Ctx, LayerCtx, Layer, Input,
				Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)], &Module);
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
		case EMixtormatGeneratorType::Noise:
			// One dispatch, no solve: the field producer publishes its value, gradient and (for
			// Worley) cell IDs, and leaves the signed height for the shared contract below.
			AddNoisePasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, &Module);
			break;
		}
		if (!Module.Height) { continue; }
		// Flow runs on the generator's native field, before the shared signed normalization: the
		// flow algorithm reads the field's own amplitude for its seed threshold and carve depth.
		if (HasActiveFlowTools(Ctx, LayerCtx.LayerIndex, Layer, Child.SourceChildIndex))
		{
			Module.Height = AddGeneratorFlowToolPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex,
				Module.BoundaryField, Module.Height, Module.Coverage, &Module);
		}
		// The shared signed output contract: zero-preserving max-absolute normalization to -1..1,
		// then Height Scale (which may exceed -1..1). Applied after flow.
		Module.Height = AddSignedGeneratorHeightPasses(
			Ctx.GraphBuilder, Module.Height, Size,
			Child.Generator.bNormalizeHeight, Child.Generator.HeightScale,
			TEXT("Mixtormat.Generator.SignedHeight"));

		// Strata regenerates in its structural frame. Every other supported target instead owns
		// one completed-bundle pullback after native generation, flow and signed normalization.
		if (Child.Generator.Type != EMixtormatGeneratorType::StrataCarver)
		{
			if (const FRDGTextureRef* Displacement = LayerCtx.GeneratorStructuralDisplacements.Find(
				Child.SourceChildIndex); Displacement && *Displacement)
			{
				const FRDGTextureRef Coordinates = AddStructuralWarpCoordinates(Ctx, *Displacement,
					LayerCtx.LayerIndex, Child.SourceChildIndex);
				const FPublishedFieldKey ValueKey = PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Value")));
				const FPublishedFieldKey GradientKey = PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Gradient")));
				const FPublishedField* NoiseValue = Child.Generator.Type == EMixtormatGeneratorType::Noise
					? Ctx.PublishedFieldOutputs.Find(ValueKey) : nullptr;
				const FPublishedField* NoiseGradient = Child.Generator.Type == EMixtormatGeneratorType::Noise
					? Ctx.PublishedFieldOutputs.Find(GradientKey) : nullptr;
				const FPublishedField ValueSnapshot = NoiseValue ? *NoiseValue : FPublishedField{};
				const FPublishedField GradientSnapshot = NoiseGradient ? *NoiseGradient : FPublishedField{};
				RemapCompletedGeneratorBundle(Ctx, Module, Module.Height, Coordinates);
				if (ValueSnapshot.IsComplete())
				{
					Ctx.PublishedFieldOutputs.Add(ValueKey, FPublishedField{
						ValueSnapshot.Kind, RemapBundleField(Ctx, ValueSnapshot.Texture, Coordinates, 0),
						nullptr, nullptr, false});
				}
				if (Module.GradientDescriptor.Semantic == FGeneratorBundle::EFieldSemantic::SourceFrameVector
					&& Module.GradientDescriptor.Units == FGeneratorBundle::EFieldUnits::GeneratorDomain
					&& GradientSnapshot.Texture && GradientSnapshot.Texture->Desc.Extent == Size
					&& GradientSnapshot.Texture->Desc.Format == PF_G32R32F)
				{
					// Noise Gradient is declared source-domain data: lattice families are analytic
					// covectors and Worley/Bars are directions. Preserve that public frame by
					// transport-sampling; do not silently apply either vector transform.
					Ctx.PublishedFieldOutputs.Add(GradientKey, FPublishedField{
						GradientSnapshot.Kind, RemapBundleField(Ctx, GradientSnapshot.Texture, Coordinates, 4),
						nullptr, nullptr, false});
				}
			}
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
		{
			AddNoiseFlowPass(Ctx, Layer, Child.SourceChildIndex, Module.Height);
			for (const FName Name : {FName(TEXT("Value")), FName(TEXT("Gradient")), FName(TEXT("FlowDirection"))})
			{
				const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, Name));
				if (!Field) { continue; }
				AddPublishedFieldPreview(Ctx, LayerCtx, Child.SourceChildIndex, Name, *Field);
				// Keep authored mask references to Value working, while Copy preserves its typed payload.
				if (Name == FName(TEXT("Value")))
				{
					Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name), Field->Texture);
				}
			}
		}
		// Retain this module's own signed height so a later Height Blend sublayer can reference it.
		LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Module.Height);
		// Explicit inputs address the completed signed module result, never the running sum.
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Height"))),
			FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Module.Height, nullptr, nullptr, false});

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
				|| Mask.Key == FName(TEXT("PebbleEdgeDistance"));
			if (IsChildOutputPreviewTarget(Ctx.Request,
				bDistance ? EMixtormatPreviewOutputKind::SignedDistance : EMixtormatPreviewOutputKind::Mask,
				Mask.Key, LayerCtx.LayerIndex, Child.SourceChildIndex))
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

		AddGeneratorModuleCombine(Ctx, RunningHeight, Module, Child, RunningHeight);
	}
	// Color Ramp only publishes Color. The Generator layer owns whether the last valid ordered
	// Color result becomes this layer's Base Color input. When nothing in the stack produced a
	// colour, bGeneratorColor stays false and the composite keeps the base colour below instead
	// of painting the resolved neutral input.
	if (Layer.bGeneratorAlbedo && GeneratedColor)
	{
		LayerCtx.LayerInputBC = GeneratedColor;
		LayerCtx.bGeneratorColor = true;
	}
	if (!RunningHeight) { return; }
	Bundle.Height = RunningHeight;
}

}
