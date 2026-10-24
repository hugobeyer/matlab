// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorCommon.h"
#include "Compositing/MixtormatComposeHash.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

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

namespace MixtormatGpuCompositor
{
namespace
{
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
}

	FRDGTextureRef AddRockFormationPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle)
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
		FRDGTextureRef Height = AddNormalizeFieldPasses(GraphBuilder, Outputs[0], Size, Rock.DepthMin, Rock.DepthMax, TEXT("Mixtormat.Rock.Depth"));
		if (Bundle) { Bundle->Height = Height; }
		return Height;
	}

	FRDGTextureRef AddCracksPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle)
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

	FRDGTextureRef AddPebblesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle)
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

	struct FCliffBlockStride
	{
		float Center[2], Extent[2], Lean[2], Height, RotationCS[2], CenterShift[2], FlowAxis[2];
		uint32 SideCount, Active;
	};
	static_assert(sizeof(FCliffBlockStride) == 60, "Cliff block stride must match FCliffBlock in HLSL");

	FRDGTextureRef AddCliffStrataPasses(FMixtormatComposeContext& Ctx,FMixtormatLayerPassContext& LayerCtx,const FLayerRenderData& Layer,const FGeneratorPassInput& Child,FGeneratorBundle* Bundle)
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
