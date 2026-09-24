// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Region-ID passes: the cluster segmentation and the procedural pattern lattice that
// produce ID maps, the Ramp-from-ID gradient built on one, the random-value mask that
// reads one, and the post-composite relief and edge shading those fields drive.

// One entry point and one complete parameter layout for every stage. In particular, do not
// split this into partial per-entry structs: UE validates all file-scope shader uniforms.
class FMixtormatClusterIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatClusterIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatClusterIdsCS, FGlobalShader);

	static constexpr uint32 UnionStart = 4;
	static constexpr uint32 UnionPasses = 12;
	static constexpr uint32 ResolveStage = UnionStart + UnionPasses;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, SourceMode)
		SHADER_PARAMETER(uint32, HasSeparateSourceHeight)
		SHADER_PARAMETER(FVector2f, SourceTiling)
		SHADER_PARAMETER(FVector2f, SourceOffset)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, Threshold)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAMH)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Statistics)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, GuideSignal)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Parents)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ClusterIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatClusterIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatClusterIds.usf",
	"MainCS",
	SF_Compute);

class FMixtormatIdGroupResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatIdGroupResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatIdGroupResolveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceAIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceBIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatIdGroupResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatIdGroup.usf", "ResolveCS", SF_Compute);

class FMixtormatIdGroupBoundaryCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatIdGroupBoundaryCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatIdGroupBoundaryCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, OutlineWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GroupIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputBoundary)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatIdGroupBoundaryCS,
	"/Plugin/Mixtormat/Private/MixtormatIdGroupBoundary.usf", "MainCS", SF_Compute);

// Random value per region. A mask, so it ends in the same PreviousMask/BlendMode/Weight tail
// every other mask child uses -- the only thing that makes it different is where the value
// comes from.
class FMixtormatRandomIdCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRandomIdCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRandomIdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, MinValue)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRandomIdCS,
	"/Plugin/Mixtormat/Private/MixtormatRandomId.usf",
	"MainCS",
	SF_Compute);

// Procedural lattice/Voronoi region producer. One dispatch writes IDs, feature-point UV,
// the shared ramp field, and an edge-distance field for bevel/shading.
class FMixtormatPatternIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPatternIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPatternIdsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, WriteOrientation)
		SHADER_PARAMETER(uint32, PatternMode)
		SHADER_PARAMETER(uint32, GridMode)
		SHADER_PARAMETER(int32, Rows)
		SHADER_PARAMETER(int32, Columns)
		SHADER_PARAMETER(float, RowOffset)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(uint32, SwapAxes)
		SHADER_PARAMETER(float, GapPixels)
		SHADER_PARAMETER(float, GapRandom)
		SHADER_PARAMETER(float, GapSlide)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, FeatherRandom)
		SHADER_PARAMETER(float, Rounding)
		SHADER_PARAMETER(uint32, EdgeRelative)
		SHADER_PARAMETER(float, FractureSizeVariation)
		SHADER_PARAMETER(float, FractureSecondaryAmount)
		SHADER_PARAMETER(int32, FractureSecondaryMin)
		SHADER_PARAMETER(int32, FractureSecondaryMax)
		SHADER_PARAMETER(float, FractureSecondaryRadius)
		SHADER_PARAMETER(float, FractureSecondaryJitter)
		SHADER_PARAMETER(float, FractureEdgeIrregularity)
		SHADER_PARAMETER(float, FractureEdgeScale)
		SHADER_PARAMETER(float, FractureEdgeDetail)
		SHADER_PARAMETER(float, FractureEdgeDetailScale)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputUV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputOrientation)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPatternIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatPatternIds.usf",
	"MainCS",
	SF_Compute);

class FMixtormatEdgeShadeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatEdgeShadeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatEdgeShadeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, BevelWidthPixels)
		SHADER_PARAMETER(float, BevelVariation)
		SHADER_PARAMETER(float, AOSpread)
		SHADER_PARAMETER(float, EdgeRoughness)
		SHADER_PARAMETER(float, EdgeRoughnessAmount)
		SHADER_PARAMETER(float, AOAmount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatEdgeShadeCS,
	"/Plugin/Mixtormat/Private/MixtormatEdgeShade.usf",
	"MainCS",
	SF_Compute);

// Per-region gradient. One entry point staged by Stage, so -- as with the cluster filter --
// every file-scope uniform is declared here whether a given stage reads it or not.
class FMixtormatRampIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRampIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRampIdsCS, FGlobalShader);

	static constexpr uint32 StageInit = 0;
	static constexpr uint32 StageBounds = 1;
	static constexpr uint32 StageResolve = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, RotateRandom)
		SHADER_PARAMETER(uint32, AngleStepping)
		SHADER_PARAMETER(float, AngleStepDegrees)
		SHADER_PARAMETER(float, IntensityRandom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, RegionBounds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRampIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIds.usf",
	"MainCS",
	SF_Compute);

// Pattern-only reduction: one maximum source height at each centre-pixel Region ID.
class FMixtormatPatternHeightMaxCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPatternHeightMaxCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPatternHeightMaxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputPatternHeightMax)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPatternHeightMaxCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIdRelief.usf",
	"PatternHeightMaxCS",
	SF_Compute);

// Fewer, larger regions from the ID map above. One MainCS with a Stage, like the cluster filter,
// so no entry point carries a partial parameter layout.
class FMixtormatCombineIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCombineIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCombineIdsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, PassIndex)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, CombineMode)
		SHADER_PARAMETER(uint32, UseHashedIds)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Parents)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RegionKeys)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCombineIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatCombineIds.usf",
	"MainCS",
	SF_Compute);

// The post-composite half: the gradient turned into a tilt in the composited height and normal.
class FMixtormatRampIdReliefCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRampIdReliefCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRampIdReliefCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, HeightAmount)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, UseEdge)
		SHADER_PARAMETER(float, CellHeightAmount)
		SHADER_PARAMETER(float, CellHeightRandom)
		SHADER_PARAMETER(float, BevelHeight)
		SHADER_PARAMETER(float, BevelWidthPixels)
		SHADER_PARAMETER(float, BevelWidthCells)
		SHADER_PARAMETER(uint32, BevelRelative)
		SHADER_PARAMETER(float, BevelVariation)
		SHADER_PARAMETER(float, BevelRoundness)
		SHADER_PARAMETER(float, BevelRoundnessRandom)
		SHADER_PARAMETER(float, BevelInsetPixels)
		SHADER_PARAMETER(float, GapHeight)
		SHADER_PARAMETER(float, FeatherGain)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RampField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternHeightMax)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRampIdReliefCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIdRelief.usf",
	"MainCS",
	SF_Compute);

// Generic region analysis, shared by UV From IDs and Relief From IDs.
//
// Five entry points and five parameter layouts, in the style of the craquelure distance file:
// each struct carries exactly what its own entry point references, which is what keeps a UAV and
// an SRV of the same texture out of one pass. Nothing here knows which node produced the IDs.

// Region bounding boxes, and the per-region centre UV resolved from them.
class FMixtormatRegionBoundsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionBoundsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionBoundsCS, FGlobalShader);

	static constexpr uint32 StageInit = 0;
	static constexpr uint32 StageReduce = 1;
	static constexpr uint32 StageResolve = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, Axis)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, RegionBounds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputCentreUV)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionBoundsCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"BoundsCS",
	SF_Compute);

// Jump-flood seed: the valid pixels that touch a boundary.
class FMixtormatRegionSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatRegionJumpCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionJumpCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionJumpCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionJumpCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"JumpCS",
	SF_Compute);

// Per-region reach, reduced at the root pixel.
class FMixtormatRegionExtentCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionExtentCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionExtentCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RegionExtent)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionExtentCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"ExtentCS",
	SF_Compute);

// The edge and ramp fields, in the exact layout Pattern IDs publishes them in -- which is what
// lets one relief pass serve a Pattern source and a Cluster source without branching.
class FMixtormatRegionResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, FieldSeed)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, FeatherRandom)
		SHADER_PARAMETER(uint32, RelativeWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionExtentIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"ResolveCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Produces one layer's ID map, and its debug preview alongside it.
	//
	// Returns the map rather than only writing the preview, because the two consumers -- the HSV
	// filter at the composite's albedo sample and the random-value mask in the chain -- both read
	// it, and both have to read the *same* one. Segmenting twice would cost 17 dispatches twice
	// and, worse, could disagree: a tint landing on different regions than the stain it is
	// supposed to share boundaries with is exactly the failure a shared map exists to prevent.
	static FRDGTextureRef AddClusterIdPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceRAMH,
		FRDGTextureRef SourceHeight,
		FRDGTextureRef SurfaceRAM,
		FRDGTextureRef SurfaceHeight,
		bool bHasSurfaceBelow,
		bool bWriteDebug,
		FRDGTextureRef OutputDebug,
		FIntPoint OutputSize,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		int32 LayerIndex)
	{
		// The bottom layer has nothing composited below it -- the substrate is flat, and
		// segmenting a flat surface returns one region covering everything. Fall back to the
		// layer's own map rather than hand back a map with no regions in it.
		const bool bFromComposite =
			Child.Filter.Source == EMixtormatClusterSource::CompositeBelow && bHasSurfaceBelow;
		// Deliberately graph-local. An RHI identity (even held strongly) cannot detect in-place
		// texture edits, reimports or streaming updates. Until the source exposes a content
		// revision, recompute selected previews each request rather than cache stale regions.
		// Any future persistent key must include that revision, retained source identity,
		// resolution, UV placement and these three filter controls -- not material grading.
		const uint32 PixelCount = static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);
		FRDGBufferRef Statistics = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4), TEXT("Mixtormat.Cluster.Statistics"));
		FRDGBufferRef GuideSignal = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), PixelCount), TEXT("Mixtormat.Cluster.GuideSignal"));
		FRDGBufferRef Parents = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PixelCount), TEXT("Mixtormat.Cluster.Parents"));
		// Sparse integer root indices, not colors or a compacted label map. No consumers yet.
		FRDGBufferRef ClusterIds = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PixelCount), TEXT("Mixtormat.Cluster.Ids"));
		const FRDGBufferUAVRef StatisticsUAV = GraphBuilder.CreateUAV(Statistics);
		const FRDGBufferUAVRef GuideSignalUAV = GraphBuilder.CreateUAV(GuideSignal);
		const FRDGBufferUAVRef ParentsUAV = GraphBuilder.CreateUAV(Parents);
		const FRDGBufferUAVRef ClusterIdsUAV = GraphBuilder.CreateUAV(ClusterIds);
		const FRDGTextureUAVRef DebugUAV = GraphBuilder.CreateUAV(OutputDebug);
		// R32_UINT and read with Load, never a sampler. Filtering an ID is meaningless -- halfway
		// between two regions is a third number naming neither -- and every consumer runs at this
		// resolution, so a pixel lookup is exact.
		FRDGTextureRef RegionIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Cluster.RegionIds"));
		const FRDGTextureUAVRef RegionIdsUAV = GraphBuilder.CreateUAV(RegionIds);
		TShaderMapRef<FMixtormatClusterIdsCS> ClusterShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8), FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		for (uint32 Stage = 0; Stage <= FMixtormatClusterIdsCS::ResolveStage; ++Stage)
		{
			FMixtormatClusterIdsCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatClusterIdsCS::FParameters>();
			Parameters->OutputSize = OutputSize;
			Parameters->Stage = Stage;
			Parameters->WriteDebug = bWriteDebug ? 1u : 0u;
			Parameters->SourceMode = bFromComposite ? 1u : 0u;
			Parameters->HasSeparateSourceHeight = Layer.SourceOutputs.IsValid() ? 1u : 0u;
			Parameters->SourceTiling = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
			Parameters->SourceOffset = Layer.UVOffset;
			Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
			Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
			Parameters->Rotation = Layer.Rotation;
			Parameters->Threshold = Child.Filter.Threshold;
			Parameters->Offset = Child.Filter.Offset;
			Parameters->HeightInfluence = Child.Filter.HeightInfluence;
			Parameters->SourceRAMH = SourceRAMH;
			Parameters->SourceHeight = SourceHeight;
			Parameters->SurfaceRAM = SurfaceRAM;
			Parameters->SurfaceHeight = SurfaceHeight;
			Parameters->LinearWrapSampler =
				TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			Parameters->Statistics = StatisticsUAV;
			Parameters->GuideSignal = GuideSignalUAV;
			Parameters->Parents = ParentsUAV;
			Parameters->ClusterIds = ClusterIdsUAV;
			Parameters->OutputIds = RegionIdsUAV;
			Parameters->OutputDebug = DebugUAV;
			// Keep the default UAV barriers: each dispatch must finish before the next stage
			// reads global statistics/parents. A group barrier cannot synchronize this algorithm.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.ClusterIds.Layer%d.Child%d.Stage%u", LayerIndex, Child.SourceChildIndex, Stage),
				ClusterShader, Parameters, Stage == 0 ? FIntVector(1, 1, 1) : Groups);
		}
		return RegionIds;
	}

	static FRDGTextureRef AddPatternIdPasses(
		FRDGBuilder& GraphBuilder,
		bool bWriteDebug,
		FRDGTextureRef OutputDebug,
		FIntPoint OutputSize,
		const FChildRenderData& Child,
		int32 LayerIndex,
		FRDGTextureRef EmptyPatternOrientation,
		FRDGTextureRef& OutUV,
		FRDGTextureRef& OutRamp,
		FRDGTextureRef& OutEdge,
		FRDGTextureRef& OutGap,
		FRDGTextureRef& OutOrientation)
	{
		const FRDGTextureDesc IdDesc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc Float2Desc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_G16R16F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef RegionIds =
			GraphBuilder.CreateTexture(IdDesc, TEXT("Mixtormat.Pattern.RegionIds"));
		OutUV = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.FeatureUV"));
		OutRamp = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.Ramp"));
		OutEdge = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.Edge"));
		OutGap = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Pattern.Gap"));
		const bool bWritesOrientation = HasIntrinsicPatternOrientation(Child.PatternId);
		OutOrientation = bWritesOrientation
			? GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					OutputSize,
					PF_R8_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Pattern.Orientation"))
			: EmptyPatternOrientation;

		FMixtormatPatternIdsCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatPatternIdsCS::FParameters>();
		Parameters->OutputSize = OutputSize;
		Parameters->WriteDebug = bWriteDebug ? 1u : 0u;
		Parameters->WriteOrientation = bWritesOrientation ? 1u : 0u;
		Parameters->PatternMode = static_cast<uint32>(Child.PatternId.PatternMode);
		Parameters->GridMode = static_cast<uint32>(Child.PatternId.GridMode);
		Parameters->Rows = Child.PatternId.Rows;
		Parameters->Columns = Child.PatternId.Columns;
		Parameters->RowOffset = Child.PatternId.RowOffset;
		Parameters->Jitter = Child.PatternId.Jitter;
		Parameters->SwapAxes = Child.PatternId.bSwapAxes ? 1u : 0u;
		Parameters->GapPixels = Child.PatternId.GapPixels;
		Parameters->GapRandom = Child.PatternId.GapRandom;
		Parameters->GapSlide = Child.PatternId.GapSlide;
		Parameters->Seed = Child.PatternId.Seed;
		Parameters->FeatherRandom = Child.PatternId.FeatherRandom;
		Parameters->Rounding = Child.PatternId.Rounding;
		Parameters->EdgeRelative = Child.PatternId.bRelativeEdgeWidth ? 1u : 0u;
		Parameters->Feather = Child.PatternId.Feather;
		Parameters->FractureSizeVariation = Child.PatternId.FractureSizeVariation;
		Parameters->FractureSecondaryAmount = Child.PatternId.FractureSecondaryAmount;
		Parameters->FractureSecondaryMin = Child.PatternId.FractureSecondaryMin;
		Parameters->FractureSecondaryMax = Child.PatternId.FractureSecondaryMax;
		Parameters->FractureSecondaryRadius = Child.PatternId.FractureSecondaryRadius;
		Parameters->FractureSecondaryJitter = Child.PatternId.FractureSecondaryJitter;
		Parameters->FractureEdgeIrregularity = Child.PatternId.FractureEdgeIrregularity;
		Parameters->FractureEdgeScale = Child.PatternId.FractureEdgeScale;
		Parameters->FractureEdgeDetail = Child.PatternId.FractureEdgeDetail;
		Parameters->FractureEdgeDetailScale = Child.PatternId.FractureEdgeDetailScale;
		Parameters->OutputIds = GraphBuilder.CreateUAV(RegionIds);
		Parameters->OutputUV = GraphBuilder.CreateUAV(OutUV);
		Parameters->OutputRamp = GraphBuilder.CreateUAV(OutRamp);
		Parameters->OutputEdge = GraphBuilder.CreateUAV(OutEdge);
		Parameters->OutputGap = GraphBuilder.CreateUAV(OutGap);
		Parameters->OutputOrientation = GraphBuilder.CreateUAV(OutOrientation);
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatPatternIdsCS> PatternShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.PatternIds.Layer%d.Child%d", LayerIndex, Child.SourceChildIndex),
			PatternShader,
			Parameters,
			Groups);
		return RegionIds;
	}

	// Builds one ramp filter's gradient field from an ID map.
	//
	// Three dispatches and one full-resolution int4 scratch buffer, so it is demand-culled the
	// same way the segmentation is: no tilt weight, no pass.
	static FRDGTextureRef AddRampIdPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef RegionIds,
		FIntPoint OutputSize,
		const FRampIdRenderData& Ramp,
		int32 LayerIndex,
		int32 ChildIndex)
	{
		const uint32 PixelCount = static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);
		// Four ints per pixel -- min x, max x, min y, max y -- strided in one buffer. Sized by
		// pixel rather than by region because an ID *is* a pixel index and nothing compacts them;
		// most of this is never touched, which is the price of keeping the root's position.
		FRDGBufferRef RegionBounds = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), PixelCount * 4u),
			TEXT("Mixtormat.Ramp.RegionBounds"));
		const FRDGBufferUAVRef RegionBoundsUAV = GraphBuilder.CreateUAV(RegionBounds);

		FRDGTextureRef RampField = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_G16R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Ramp.Field"));
		const FRDGTextureUAVRef RampFieldUAV = GraphBuilder.CreateUAV(RampField);

		TShaderMapRef<FMixtormatRampIdsCS> RampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8), FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		for (uint32 Stage = FMixtormatRampIdsCS::StageInit;
			Stage <= FMixtormatRampIdsCS::StageResolve;
			++Stage)
		{
			FMixtormatRampIdsCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatRampIdsCS::FParameters>();
			Parameters->OutputSize = OutputSize;
			Parameters->Stage = Stage;
			Parameters->Seed = Ramp.Seed;
			Parameters->RotateRandom = Ramp.bRotateRandom ? 1u : 0u;
			Parameters->AngleStepping = Ramp.bAngleStepping ? 1u : 0u;
			Parameters->AngleStepDegrees = Ramp.AngleStepDegrees;
			Parameters->IntensityRandom = Ramp.IntensityRandom;
			Parameters->RegionIds = RegionIds;
			Parameters->RegionBounds = RegionBoundsUAV;
			Parameters->OutputRamp = RampFieldUAV;

			// Default UAV barriers, deliberately: the bounds reduction has to have finished for
			// every pixel of a region before any pixel of it reads the box back.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.RampIds.Layer%d.Child%d.Stage%u", LayerIndex, ChildIndex, Stage),
				RampShader, Parameters, Groups);
		}
		return RampField;
	}

	// The cluster filter a consumer reads: the nearest enabled one above it in the child list.
	//
	// Above rather than anywhere in the layer, so two cluster filters at different thresholds can
	// coexist -- a coarse one with its own consumers, then a fine one with its own -- which is
	// how the micro/macro pairing in the design note is meant to be authored. Nothing above means
	// no map, and the consumer is culled rather than guessing.

	// The producer a From-IDs consumer resolves to: the nearest published map above its own row,
	// and the row that published it.
	//
	// The index matters as well as the texture. The region-distance cache is keyed on it, so two
	// consumers of one producer share a jump flood; and UV From IDs uses it to find out whether
	// the producer it landed on happens to be a Pattern with an intrinsic quarter-turn field.
	static FRDGTextureRef FindRegionIdsAboveWithIndex(
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const int32 ChildIndex,
		int32& OutProducerIndex)
	{
		FRDGTextureRef Found = nullptr;
		OutProducerIndex = INDEX_NONE;
		for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
		{
			if (Entry.Key < ChildIndex)
			{
				Found = Entry.Value;
				OutProducerIndex = Entry.Key;
			}
		}
		return Found;
	}

	// Distance to the nearest region boundary, by jump flooding, plus each region's reach.
	//
	// Both are functions of the ID map alone, so the result is cached against the producer's row
	// and reused by every Relief From IDs node reading that producer. Log2(N) passes and two
	// full-resolution textures is the same order as the cluster segmentation, and paying it twice
	// for two relief nodes over one pattern would be the obvious waste.
	static FRegionDistanceCacheEntry AddRegionDistancePasses(
		FRDGBuilder& GraphBuilder,
		FMixtormatLayerPassContext& LayerCtx,
		FRDGTextureRef RegionIds,
		const int32 ProducerIndex,
		const FIntPoint OutputSize,
		const int32 LayerIndex)
	{
		for (const FRegionDistanceCacheEntry& Entry : LayerCtx.RegionDistanceCache)
		{
			if (Entry.SourceChildIndex == ProducerIndex)
			{
				return Entry;
			}
		}

		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);

		// A boundary seed is only an integer pixel coordinate. Pack X/Y into one R32_UINT
		// (16 bits each) instead of carrying float4(x, y, valid, unused). At 4K this cuts
		// the JFA ping-pong pair from 512 MiB to 128 MiB with exact pixel coordinates.
		const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Record[2] = {
			GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.Region.JfaA")),
			GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.Region.JfaB"))};

		TShaderMapRef<FMixtormatRegionSeedCS> SeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionSeedCS::FParameters* SeedParameters =
			GraphBuilder.AllocParameters<FMixtormatRegionSeedCS::FParameters>();
		SeedParameters->OutputSize = OutputSize;
		SeedParameters->RegionIds = RegionIds;
		SeedParameters->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Seed.L%d.C%d", LayerIndex, ProducerIndex),
			SeedShader, SeedParameters, Groups);

		// Strides halve from half the padded extent down to 1, then one more pass at 1 -- the
		// JFA+1 variant, for the same reason the craquelure field uses it: plain jump flooding can
		// lose a seed whose carrier was overwritten at a coarser stride, and the extra unit pass
		// costs one dispatch and removes the islands that shows up as.
		TShaderMapRef<FMixtormatRegionJumpCS> JumpShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		int32 RecordIndex = 0;
		const int32 FirstStep = FMath::Max(
			1,
			static_cast<int32>(FMath::RoundUpToPowerOfTwo(
				static_cast<uint32>(FMath::Max(OutputSize.X, OutputSize.Y)))) / 2);
		for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
		{
			const int32 Read = RecordIndex;
			const int32 Write = 1 - Read;
			FMixtormatRegionJumpCS::FParameters* JumpParameters =
				GraphBuilder.AllocParameters<FMixtormatRegionJumpCS::FParameters>();
			JumpParameters->OutputSize = OutputSize;
			JumpParameters->StepSize = StepSize;
			JumpParameters->PreviousRecord = Record[Read];
			JumpParameters->OutputRecord = GraphBuilder.CreateUAV(Record[Write]);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.RegionFields.Jump%d.L%d.C%d", StepSize, LayerIndex, ProducerIndex),
				JumpShader, JumpParameters, Groups);
			RecordIndex = Write;
		}
		{
			const int32 Read = RecordIndex;
			const int32 Write = 1 - Read;
			FMixtormatRegionJumpCS::FParameters* JumpParameters =
				GraphBuilder.AllocParameters<FMixtormatRegionJumpCS::FParameters>();
			JumpParameters->OutputSize = OutputSize;
			JumpParameters->StepSize = 1;
			JumpParameters->PreviousRecord = Record[Read];
			JumpParameters->OutputRecord = GraphBuilder.CreateUAV(Record[Write]);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.RegionFields.JumpFinal.L%d.C%d", LayerIndex, ProducerIndex),
				JumpShader, JumpParameters, Groups);
			RecordIndex = Write;
		}

		FRDGTextureRef Extent = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Region.Extent"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Extent), 0u);

		TShaderMapRef<FMixtormatRegionExtentCS> ExtentShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionExtentCS::FParameters* ExtentParameters =
			GraphBuilder.AllocParameters<FMixtormatRegionExtentCS::FParameters>();
		ExtentParameters->OutputSize = OutputSize;
		ExtentParameters->RegionIds = RegionIds;
		ExtentParameters->PreviousRecord = Record[RecordIndex];
		ExtentParameters->RegionExtent = GraphBuilder.CreateUAV(Extent);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Extent.L%d.C%d", LayerIndex, ProducerIndex),
			ExtentShader, ExtentParameters, Groups);

		FRegionDistanceCacheEntry& Entry = LayerCtx.RegionDistanceCache.AddDefaulted_GetRef();
		Entry.SourceChildIndex = ProducerIndex;
		Entry.Record = Record[RecordIndex];
		Entry.Extent = Extent;
		// By value. The cache is inline-allocated, so a reference into it would dangle the moment
		// a second consumer of a different producer pushed its own entry.
		return Entry;
	}

	// One Relief From IDs node's edge and ramp fields. Everything above this is shared; only this
	// dispatch reads the node's own feather and width, which is why the cache above is worth
	// having at all.
	static void AddRegionReliefFieldPass(
		FRDGBuilder& GraphBuilder,
		const FRegionDistanceCacheEntry& Distance,
		FRDGTextureRef RegionIds,
		const FReliefIdRenderData& Relief,
		const FIntPoint OutputSize,
		const int32 LayerIndex,
		const int32 ChildIndex,
		FRDGTextureRef& OutEdge,
		FRDGTextureRef& OutRamp)
	{
		const FRDGTextureDesc Float2Desc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_G16R16F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		OutEdge = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Region.Edge"));
		OutRamp = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Region.Ramp"));

		TShaderMapRef<FMixtormatRegionResolveCS> ResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionResolveCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatRegionResolveCS::FParameters>();
		Parameters->OutputSize = OutputSize;
		Parameters->FieldSeed = Relief.Seed;
		Parameters->Feather = Relief.Feather;
		Parameters->FeatherRandom = Relief.FeatherRandom;
		Parameters->RelativeWidth = Relief.bRelativeWidth ? 1u : 0u;
		Parameters->RegionIds = RegionIds;
		Parameters->PreviousRecord = Distance.Record;
		Parameters->RegionExtentIn = Distance.Extent;
		Parameters->OutputEdge = GraphBuilder.CreateUAV(OutEdge);
		Parameters->OutputRamp = GraphBuilder.CreateUAV(OutRamp);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Resolve.L%d.C%d", LayerIndex, ChildIndex),
			ResolveShader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(OutputSize.X, 8),
				FMath::DivideAndRoundUp(OutputSize.Y, 8),
				1));
	}

	// UV From IDs: one generic centre solve per Region-ID producer, reused by every UV node that
	// resolves to that producer. The scratch reduction is one axis at a time to halve peak memory.
	//
	// Culled cleanly when nothing above publishes IDs. No output is registered, so the composite
	// falls back to the layer's ordinary placement -- the node does nothing rather than blackening
	// the layer or inventing a region 0 to key off.
	void AddUvIdPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		if (!Layer.bEnabled)
		{
			return;
		}
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint OutputSize = Ctx.Request.Resolution;
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);

		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::UvFromIds)
			{
				continue;
			}
			int32 ProducerIndex = INDEX_NONE;
			FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
				LayerCtx.RegionIdMaps, Child.SourceChildIndex, ProducerIndex);
			if (!RegionIds)
			{
				continue;
			}

			FRDGTextureRef CentreUV = nullptr;
			for (const FRegionCentreCacheEntry& Entry : LayerCtx.RegionCentreCache)
			{
				if (Entry.SourceChildIndex == ProducerIndex)
				{
					CentreUV = Entry.CentreUV;
					break;
				}
			}

			if (!CentreUV)
			{
				const uint32 PixelCount =
					static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);

				// Two ints per possible root instead of four. X and Y are reduced in sequence
				// through the same scratch allocation. At 4K this is 128 MiB instead of 256 MiB.
				FRDGBufferRef RegionBounds = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), PixelCount * 2u),
					TEXT("Mixtormat.UvId.RegionBounds"));
				const FRDGBufferUAVRef RegionBoundsUAV = GraphBuilder.CreateUAV(RegionBounds);

				CentreUV = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						OutputSize,
						PF_G16R16F,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.UvId.CentreUV"));
				const FRDGTextureUAVRef CentreUAV = GraphBuilder.CreateUAV(CentreUV);

				TShaderMapRef<FMixtormatRegionBoundsCS> BoundsShader(
					GetGlobalShaderMap(GMaxRHIFeatureLevel));
				for (uint32 Axis = 0; Axis < 2; ++Axis)
				{
					for (uint32 Stage = FMixtormatRegionBoundsCS::StageInit;
						Stage <= FMixtormatRegionBoundsCS::StageResolve;
						++Stage)
					{
						FMixtormatRegionBoundsCS::FParameters* Parameters =
							GraphBuilder.AllocParameters<FMixtormatRegionBoundsCS::FParameters>();
						Parameters->OutputSize = OutputSize;
						Parameters->Stage = Stage;
						Parameters->Axis = Axis;
						Parameters->RegionIds = RegionIds;
						Parameters->RegionBounds = RegionBoundsUAV;
						Parameters->OutputCentreUV = CentreUAV;
						// Default UAV barriers are intentional: each axis must finish its
						// reduction before resolve, and X resolve must finish before Y writes.
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.UvIds.L%d.P%d.Axis%u.Stage%u",
								LayerCtx.LayerIndex, ProducerIndex, Axis, Stage),
							BoundsShader, Parameters, Groups);
					}
				}

				FRegionCentreCacheEntry& Cache =
					LayerCtx.RegionCentreCache.AddDefaulted_GetRef();
				Cache.SourceChildIndex = ProducerIndex;
				Cache.CentreUV = CentreUV;
			}

			FUvIdPassOutput& Output = LayerCtx.UvIdOutputs.AddDefaulted_GetRef();
			Output.SourceChildIndex = Child.SourceChildIndex;
			Output.Ids = RegionIds;
			Output.CentreUV = CentreUV;
			Output.Settings = &Child.UvId;
			// Herringbone and Basketweave turn alternate pieces a quarter turn, and that basis is
			// a property of the lattice rather than something recoverable from a blob. Carried
			// through only when the producer this node actually resolved to is such a Pattern; any
			// other producer leaves it null, and the node says so rather than guessing.
			for (const FPatternIdPassOutput& PatternOutput : LayerCtx.PatternOutputs)
			{
				if (PatternOutput.SourceChildIndex == ProducerIndex
					&& PatternOutput.Settings
					&& HasIntrinsicPatternOrientation(*PatternOutput.Settings))
				{
					Output.Orientation = PatternOutput.Orientation;
					Output.bIntrinsicOrientation = true;
					break;
				}
			}
		}
	}

	// Random value per region, blended into the mask chain like any other mask child.
	void AddRandomIdMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatRandomIdCS> RandomIdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FRandomIdRenderData& RandomId = Child.RandomId;

		// Culled rather than defaulted when there is no cluster above it. A
		// mask with no ID map has no regions to vary, and emitting a flat
		// value would silently replace whatever the chain had accumulated.
		FRDGTextureRef RegionIds =
			FindRegionIdsAbove(RegionIdMaps, Child.SourceChildIndex);
		if (!RegionIds)
		{
			return;
		}

		// The same identity as every other mask child: at Weight 0 the tail
		// returns Previous unchanged, so skipping from the second child on
		// leaves exactly that behind.
		if (MaskPassIndex > 0 && RandomId.Weight == 0.0f)
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;

		FMixtormatRandomIdCS::FParameters* RandomParameters =
			GraphBuilder.AllocParameters<FMixtormatRandomIdCS::FParameters>();
		RandomParameters->OutputSize = Request.Resolution;
		RandomParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
		RandomParameters->Seed = RandomId.Seed;
		RandomParameters->MinValue = RandomId.MinValue;
		RandomParameters->MaxValue = RandomId.MaxValue;
		RandomParameters->BlendMode = static_cast<uint32>(RandomId.BlendMode);
		RandomParameters->Invert = RandomId.bInvert ? 1u : 0u;
		RandomParameters->Weight = RandomId.Weight;
		RandomParameters->Balance = RandomId.Balance;
		RandomParameters->Contrast = RandomId.Contrast;
		RandomParameters->Offset = RandomId.Offset;
		RandomParameters->PreviousMask = MaskTargets[MaskReadIndex];
		RandomParameters->RegionIds = RegionIds;
		RandomParameters->LinearWrapSampler =
			TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		RandomParameters->OutputMask =
			GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RandomId.Layer%d.Child%d", LayerIndex, ChildIndex),
			RandomIdShader,
			RandomParameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));

		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugRandomSnapshot = GraphBuilder.CreateTexture(
				MaskDesc,
				TEXT("Mixtormat.DebugRandomIdSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugRandomSnapshot);
			DebugMask = DebugRandomSnapshot;
		}
		++MaskPassIndex;
	}


	// Fewer, larger regions from the map above. Union-find keyed by ID value rather than by
	// pixel, so a region that is spatially disjoint still resolves through one root instead of
	// being split in two -- which would raise the region count, the opposite of the node's job.
	FRDGTextureRef AddCombineIdPasses(
		FMixtormatComposeContext& Ctx,
		const FRDGTextureRef SourceIds,
		const FCombineIdRenderData& Combine,
		const bool bUseHashedIds,
		const bool bWriteDebug,
		const FRDGTextureRef DebugTarget,
		const FIntPoint Resolution,
		const int32 LayerIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		TShaderMapRef<FMixtormatCombineIdsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FRDGTextureRef OutputIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				Resolution,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.CombineIds"));

		// Breakup IDs are full-width hashes, not pixel indices. Its table has at most
		// one distinct key per pixel and a load factor no greater than one half.
		const int32 ParentCount = Resolution.X * Resolution.Y * (bUseHashedIds ? 2 : 1);
		FRDGBufferRef Parents = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), ParentCount),
			TEXT("Mixtormat.CombineIds.Parents"));
		FRDGBufferRef RegionKeys = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), bUseHashedIds ? ParentCount : 1),
			TEXT("Mixtormat.CombineIds.RegionKeys"));

		const FIntVector Groups(
			FMath::DivideAndRoundUp(Resolution.X, 8),
			FMath::DivideAndRoundUp(Resolution.Y, 8),
			1);

		auto Dispatch = [&](const uint32 Stage, const uint32 PassIndex, const TCHAR* Name)
		{
			FMixtormatCombineIdsCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatCombineIdsCS::FParameters>();
			P->OutputSize = Resolution;
			P->Stage = Stage;
			P->PassIndex = PassIndex;
			P->Seed = Combine.Seed;
			P->Amount = Combine.Amount;
			P->CombineMode = Combine.bSubtract ? 1u : 0u;
			P->UseHashedIds = bUseHashedIds ? 1u : 0u;
			P->WriteDebug = bWriteDebug ? 1u : 0u;
			P->SourceIds = SourceIds;
			P->Parents = GraphBuilder.CreateUAV(Parents);
			P->RegionKeys = GraphBuilder.CreateUAV(RegionKeys);
			P->OutputIds = GraphBuilder.CreateUAV(OutputIds);
			P->OutputDebug = GraphBuilder.CreateUAV(DebugTarget);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.CombineIds.L%d.%s", LayerIndex, Name),
				Shader,
				P,
				Groups);
		};

		Dispatch(0u, 0u, TEXT("Init"));
		if (bUseHashedIds)
		{
			Dispatch(3u, 0u, TEXT("IndexRegions"));
		}

		// Each authored pass is several union dispatches, because one dispatch only propagates a
		// hook one link along the chain. The inner count matches the cluster filter's, which is
		// the same problem at the same resolution.
		const int32 Rounds = FMath::Clamp(Combine.Passes, 1, 8);
		for (int32 Round = 0; Round < Rounds; ++Round)
		{
			for (int32 Iteration = 0; Iteration < 12; ++Iteration)
			{
				Dispatch(1u, static_cast<uint32>(Round), TEXT("Union"));
			}
		}

		Dispatch(2u, 0u, TEXT("Resolve"));
		return OutputIds;
	}

	void AddCombineIdProducerPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child)
	{
		if (!Layer.bEnabled)
		{
			return;
		}
		for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
		{
			if (Entry.Key == Child.SourceChildIndex)
			{
				return; // Already scheduled in the pre-mask phase.
			}
		}

		// Resolve the authored producer first, not whichever map happens to exist yet.
		// A pending Breakup (or Combine) must shadow an older Pattern/Cluster map.
		int32 ProducerIndex = INDEX_NONE;
		bool bUseHashedIds = false;
		for (int32 Index = Layer.Children.Num() - 1; Index >= 0; --Index)
		{
			const FChildRenderData& Candidate = Layer.Children[Index];
			if (Candidate.SourceChildIndex >= Child.SourceChildIndex)
			{
				continue;
			}
			const bool bBreakup = Candidate.Type == EMixtormatLayerChildType::Effect
				&& Candidate.Effect.Type == EMixtormatEffectType::Breakup;
			if (Candidate.Type != EMixtormatLayerChildType::Filter
				&& Candidate.Type != EMixtormatLayerChildType::PatternId
				&& Candidate.Type != EMixtormatLayerChildType::CombineId
				&& Candidate.Type != EMixtormatLayerChildType::IdGroup && !bBreakup)
			{
				continue;
			}
			if (ProducerIndex == INDEX_NONE)
			{
				ProducerIndex = Candidate.SourceChildIndex;
			}
			if (Candidate.Type != EMixtormatLayerChildType::CombineId)
			{
				bUseHashedIds = bBreakup || Candidate.Type == EMixtormatLayerChildType::IdGroup;
				break;
			}
		}
		FRDGTextureRef Upstream = nullptr;
		for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
		{
			if (Entry.Key == ProducerIndex)
			{
				Upstream = Entry.Value;
				break;
			}
		}
		if (!Upstream)
		{
			return;
		}
		const FRenderRequest& Request = Ctx.Request;
		const bool bPreview = IsChildOutputPreviewTarget(
			Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
			LayerCtx.LayerIndex, Child.SourceChildIndex);
		PublishRegionIds(
			LayerCtx.RegionIdMaps, Child.SourceChildIndex,
			AddCombineIdPasses(
				Ctx, Upstream, Child.CombineId, bUseHashedIds, bPreview,
				Ctx.OutputDebug[Request.PublishedTargetIndex], Request.Resolution, LayerCtx.LayerIndex));
	}

	FRDGTextureRef AddIdGroupPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child)
	{
		FRDGTextureRef SourceA = nullptr;
		FRDGTextureRef SourceB = nullptr;
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (Candidate.Type != EMixtormatLayerChildType::PatternId
				|| Candidate.ScopeOwnerSourceChildIndex != Child.SourceChildIndex)
			{
				continue;
			}
			FRDGTextureRef ChildIds = nullptr;
			for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
			{
				if (Entry.Key == Candidate.SourceChildIndex)
				{
					ChildIds = Entry.Value;
					break;
				}
			}
			if (!SourceA)
			{
				SourceA = ChildIds;
			}
			else if (!SourceB)
			{
				SourceB = ChildIds;
			}
			else
			{
				return nullptr;
			}
		}
		if (!SourceA || !SourceB)
		{
			return nullptr;
		}

		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FIntVector Groups(
			FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		FRDGTextureRef GroupIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, PF_R32_UINT, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.IdGroup.Ids"));
		FRDGTextureRef Boundary = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, PF_R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.IdGroup.Boundary"));
		const bool bPreview = IsChildOutputPreviewTarget(
			Ctx.Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
			LayerCtx.LayerIndex, Child.SourceChildIndex);
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatIdGroupResolveCS::FParameters>();
			P->OutputSize = Size;
			P->Mode = static_cast<uint32>(Child.IdGroup.Mode);
			P->WriteDebug = bPreview ? 1u : 0u;
			P->SourceAIds = SourceA;
			P->SourceBIds = SourceB;
			P->OutputIds = GraphBuilder.CreateUAV(GroupIds);
			P->OutputDebug = GraphBuilder.CreateUAV(Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex]);
			TShaderMapRef<FMixtormatIdGroupResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.IdGroup.Resolve"), Shader, P, Groups);
		}
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatIdGroupBoundaryCS::FParameters>();
			P->OutputSize = Size;
			P->OutlineWidth = 1;
			P->GroupIds = GroupIds;
			P->OutputBoundary = GraphBuilder.CreateUAV(Boundary);
			TShaderMapRef<FMixtormatIdGroupBoundaryCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.IdGroup.Boundary"), Shader, P, Groups);
		}
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Boundary"))}, Boundary);
		if (IsChildOutputPreviewTarget(
			Ctx.Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Boundary")),
			LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, Boundary,
				Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
		}
		return GroupIds;
	}

	// Schedule independent producers early for generators. Breakup-dependent Combine
	// chains wait for the child loop; publication keeps maps sorted by SourceChildIndex.
	void AddRegionProducerPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyPatternOrientation = Ctx.EmptyPatternOrientation;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		if (Layer.bEnabled)
		{
			for (const FChildRenderData& Child : Layer.Children)
			{
				const bool bClusterProducer =
					Child.Type == EMixtormatLayerChildType::Filter;
				const bool bPatternProducer =
					Child.Type == EMixtormatLayerChildType::PatternId;
				const bool bCombineProducer =
					Child.Type == EMixtormatLayerChildType::CombineId;
				const bool bIdGroupProducer =
					Child.Type == EMixtormatLayerChildType::IdGroup;
				if (!bClusterProducer && !bPatternProducer && !bCombineProducer && !bIdGroupProducer)
				{
					continue;
				}

				if (bCombineProducer)
				{
					AddCombineIdProducerPass(Ctx, LayerCtx, Layer, Child);
					continue;
				}
				if (bIdGroupProducer)
				{
					// A parent precedes its nested children in the flat serialized array. Resolve it
					// after this loop, once both child ID maps have been produced.
					continue;
				}

				// Gated separately from whether the producer runs at all: ordinary
				// consumers must not overwrite a debug target owned by another layer.
				const bool bIsSelectedPreview = IsChildOutputPreviewTarget(
					Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
					LayerIndex, Child.SourceChildIndex);
				bool bWanted = bIsSelectedPreview
					|| (bPatternProducer && Child.ScopeOwnerSourceChildIndex != INDEX_NONE);

				if (bPatternProducer)
				{
					const FPatternIdRenderData& Pattern = Child.PatternId;
					bWanted = bWanted
						|| Pattern.bUVVariation
						|| HasIntrinsicPatternOrientation(Pattern)
						|| Pattern.HeightAmount > 0.0f
						|| Pattern.BevelHeight > 0.0f
						|| Pattern.EdgeRoughnessAmount > 0.0f
						|| Pattern.AOAmount > 0.0f;

					// A Gap instance mask is an explicit consumer even when every Pattern relief
					// control is neutral. Demand its producer so the published texture exists.
					if (!bWanted)
					{
						for (const FChildRenderData& Other : Layer.Children)
						{
							if (Other.Type == EMixtormatLayerChildType::Mask
								&& Other.Mask.PublishedSourceLayerId == Layer.LayerId
								&& Other.Mask.PublishedSourceChildIndex == Child.SourceChildIndex
								&& Other.Mask.PublishedSourceOutput == TEXT("Gap"))
							{
								bWanted = true;
								break;
							}
						}
					}

					// The Gap preview eye is a consumer too: without this, previewing Gap on a
					// Pattern that has no relief control active and no Gap instance mask silently
					// shows nothing, because the producer never ran to publish it.
					if (!bWanted)
					{
						bWanted = IsChildOutputPreviewTarget(
							Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Gap")),
							LayerIndex, Child.SourceChildIndex);
					}
				}

				// A scalar Driver is an ID consumer like any other, and it consumes at
				// the composite rather than from a row in the stack -- so it is not
				// visible to the child scan below and has to be asked for separately.
				// Without this a producer referenced only by a Driver is culled, and
				// the Driver then finds no map and silently disables itself.
				if (!bWanted)
				{
					for (const FScalarDriverRenderData& Driver : Layer.ScalarDrivers)
					{
						if (!Driver.bEnabled
							|| !Driver.bRegionSource
							|| Driver.SourceLayerId != Layer.LayerId)
						{
							continue;
						}
						if (Driver.SourceChildIndex != INDEX_NONE)
						{
							// Named producer: only that one is demanded.
							if (Driver.SourceChildIndex == Child.SourceChildIndex)
							{
								bWanted = true;
								break;
							}
							continue;
						}
						// Unnamed: the Driver reads the nearest map above the
						// composite, which is the last producer in the layer. Demand
						// this one only when nothing later would shadow it, so the
						// nearest-producer rule decides here exactly as it does at
						// resolve time.
						bool bLaterProducer = false;
						for (const FChildRenderData& Other : Layer.Children)
						{
							if (Other.SourceChildIndex > Child.SourceChildIndex
								&& (Other.Type == EMixtormatLayerChildType::Filter
									|| Other.Type == EMixtormatLayerChildType::PatternId
									|| Other.Type == EMixtormatLayerChildType::CombineId
									|| Other.Type == EMixtormatLayerChildType::IdGroup
									|| (Other.Type == EMixtormatLayerChildType::Effect
										&& Other.Effect.Type == EMixtormatEffectType::Breakup)))
							{
								bLaterProducer = true;
								break;
							}
						}
						if (!bLaterProducer)
						{
							bWanted = true;
							break;
						}
					}
				}

				if (!bWanted)
				{
					// Any ID consumer below this producer and above the next producer.
					// The next producer shadows this one for every later consumer.
					for (const FChildRenderData& Other : Layer.Children)
					{
						if (Other.SourceChildIndex <= Child.SourceChildIndex)
						{
							continue;
						}
						if (Other.Type == EMixtormatLayerChildType::CombineId)
						{
							// Consumes this producer, and shadows it for everything below.
							bWanted = true;
							break;
						}
						if (Other.Type == EMixtormatLayerChildType::Filter
							|| Other.Type == EMixtormatLayerChildType::PatternId)
						{
							break;
						}
						const bool bWornEdgesConsumer =
							Other.Type == EMixtormatLayerChildType::Effect
							&& Other.Effect.Type == EMixtormatEffectType::WornEdges;
						const bool bBreakupConsumer =
							Other.Type == EMixtormatLayerChildType::Effect
							&& Other.Effect.Type == EMixtormatEffectType::Breakup;
						const bool bFractureConsumer =
							Other.Type == EMixtormatLayerChildType::Generator
							&& Other.Generator.Type == EMixtormatGeneratorType::Fracture
							&& Other.Generator.Fracture.Source != EMixtormatFractureSource::Generated;
						if (Other.Type == EMixtormatLayerChildType::HsvFilter
							|| Other.Type == EMixtormatLayerChildType::RandomId
							|| Other.Type == EMixtormatLayerChildType::RampId
							|| Other.Type == EMixtormatLayerChildType::UvFromIds
							|| Other.Type == EMixtormatLayerChildType::ReliefFromIds
							|| bWornEdgesConsumer
							|| bBreakupConsumer
							|| bFractureConsumer)
						{
							bWanted = true;
							break;
						}
					}
				}
				if (!bWanted)
				{
					continue;
				}

				FRDGTextureRef RegionIds = nullptr;
				if (bClusterProducer)
				{
					// The same ping-pong slot the layer composite and generated masks
					// read: what every layer below this one has accumulated.
					const int32 SurfaceReadIndex = 1 - (LayerIndex & 1);
					RegionIds = AddClusterIdPasses(
						GraphBuilder,
						RegisterTexture(
							GraphBuilder,
							RegisteredTextures,
							Layer.RAM,
							TEXT("Mixtormat.Cluster.SourceRAMH")),
						Layer.Height.IsValid()
							? RegisterTexture(GraphBuilder, RegisteredTextures, Layer.Height,
								TEXT("Mixtormat.Cluster.SourceHeight"))
							: HeightTargets[SurfaceReadIndex],
						OutputRAM[SurfaceReadIndex],
						HeightTargets[SurfaceReadIndex],
						LayerIndex > 0,
						bIsSelectedPreview,
						OutputDebug[Request.PublishedTargetIndex],
						Request.Resolution,
						Layer,
						Child,
						LayerIndex);
				}
				else
				{
					FPatternIdPassOutput& PatternOutput =
						PatternOutputs.AddDefaulted_GetRef();
					PatternOutput.SourceChildIndex = Child.SourceChildIndex;
					PatternOutput.Settings = &Child.PatternId;
					RegionIds = AddPatternIdPasses(
						GraphBuilder,
						bIsSelectedPreview,
						OutputDebug[Request.PublishedTargetIndex],
						Request.Resolution,
						Child,
						LayerIndex,
						EmptyPatternOrientation,
						PatternOutput.UV,
						PatternOutput.Ramp,
						PatternOutput.Edge,
						PatternOutput.Gap,
						PatternOutput.Orientation);
					PatternOutput.Ids = RegionIds;
					Ctx.PublishedMaskOutputs.Add(
						FPublishedMaskKey{
							Layer.LayerId,
							Child.SourceChildIndex,
							FName(TEXT("Gap"))},
						PatternOutput.Gap);
					if (IsChildOutputPreviewTarget(
						Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Gap")),
						LayerIndex, Child.SourceChildIndex))
					{
						AddDebugPreviewMaskBlitPass(
							GraphBuilder, PatternOutput.Gap,
							OutputDebug[Request.PublishedTargetIndex], Request.Resolution);
					}
				}

				PublishRegionIds(RegionIdMaps, Child.SourceChildIndex, RegionIds);
			}

			for (const FChildRenderData& GroupChild : Layer.Children)
			{
				if (GroupChild.Type != EMixtormatLayerChildType::IdGroup)
				{
					continue;
				}
				FRDGTextureRef GroupIds = AddIdGroupPasses(Ctx, LayerCtx, Layer, GroupChild);
				if (!GroupIds)
				{
					continue;
				}
				RegionIdMaps.RemoveAll([&Layer, &GroupChild](const TPair<int32, FRDGTextureRef>& Entry)
				{
					return Layer.Children.ContainsByPredicate([&Entry, &GroupChild](
						const FChildRenderData& Candidate)
					{
						return Candidate.SourceChildIndex == Entry.Key
							&& Candidate.ScopeOwnerSourceChildIndex == GroupChild.SourceChildIndex;
					});
				});
				PublishRegionIds(RegionIdMaps, GroupChild.SourceChildIndex, GroupIds);
			}
		}
	}

	// Pattern and Ramp-from-ID relief, collected before the composite and dispatched after it:
	// their fields exist before the composite, the surface they modify does not.
	void CollectPendingRampTilts(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
			LayerCtx.PendingRampTilts;
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type == EMixtormatLayerChildType::PatternId)
			{
				const FPatternIdPassOutput* PatternOutput = nullptr;
				for (const FPatternIdPassOutput& Candidate : PatternOutputs)
				{
					if (Candidate.SourceChildIndex == Child.SourceChildIndex)
					{
						PatternOutput = &Candidate;
						break;
					}
				}
				if (!PatternOutput)
				{
					continue;
				}

				const FPatternIdRenderData& Pattern = Child.PatternId;
				const bool bNeedsRelief = Pattern.HeightAmount > 0.0f
					|| Pattern.BevelHeight != 0.0f
					|| Pattern.GapHeight != 0.0f;
				const bool bNeedsShade =
					Pattern.EdgeRoughnessAmount > 0.0f || Pattern.AOAmount > 0.0f;
				if (!bNeedsRelief && !bNeedsShade)
				{
					continue;
				}

				FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
				Tilt.Field = PatternOutput->Ramp;
				Tilt.EdgeField = PatternOutput->Edge;
				Tilt.RegionIds = PatternOutput->Ids;
				// Pattern has no tilt term: HeightAmount is its per-cell elevation
				// range and rides CellHeightAmount, leaving the relief pass's tilt
				// path -- which is Ramp From IDs' -- switched off.
				Tilt.HeightAmount = 0.0f;
				// Height is the elevation every cell gets; Height Random is how far
				// below it a cell may be drawn. Multiplied in the shader, not folded
				// together here, or Random 0 would zero the whole term instead of
				// leaving every cell at full Height.
				Tilt.CellHeightAmount = Pattern.HeightAmount;
				Tilt.CellHeightRandom = Pattern.HeightRandom;

				Tilt.bUseEdge = true;
				Tilt.BevelHeight = Pattern.BevelHeight;
				Tilt.BevelWidthPixels = Pattern.BevelWidthPixels;
				Tilt.BevelWidthCells = Pattern.BevelWidthCells;
				Tilt.bBevelRelative = Pattern.bRelativeEdgeWidth;
				Tilt.BevelVariation = Pattern.BevelVariation;
				Tilt.BevelRoundness = Pattern.BevelRoundness;
				Tilt.BevelRoundnessRandom = Pattern.BevelRoundnessRandom;
				Tilt.BevelInsetPixels = Pattern.BevelInsetPixels;
				Tilt.GapHeight = Pattern.GapHeight;
				Tilt.FeatherGain = Pattern.FeatherGain;
				Tilt.EdgeRoughness = Pattern.EdgeRoughness;
				Tilt.EdgeRoughnessAmount = Pattern.EdgeRoughnessAmount;
				Tilt.AOAmount = Pattern.AOAmount;
				Tilt.AOSpread = Pattern.AOSpread;
				continue;
			}

			if (Child.Type == EMixtormatLayerChildType::ReliefFromIds)
			{
				int32 ProducerIndex = INDEX_NONE;
				FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
					RegionIdMaps, Child.SourceChildIndex, ProducerIndex);
				if (!RegionIds)
				{
					// Nothing above publishes IDs. Culled rather than defaulted: a relief with no
					// regions has nothing to shape, and writing a flat field would still cost the
					// jump flood and still touch the composited height.
					continue;
				}

				const FReliefIdRenderData& Relief = Child.ReliefId;
				const bool bNeedsRelief = Relief.HeightAmount > 0.0f
					|| Relief.BevelHeight != 0.0f
					|| Relief.GapHeight != 0.0f;
				const bool bNeedsShade =
					Relief.EdgeRoughnessAmount > 0.0f || Relief.AOAmount > 0.0f;
				if (!bNeedsRelief && !bNeedsShade)
				{
					continue;
				}

				const FRegionDistanceCacheEntry Distance = AddRegionDistancePasses(
					GraphBuilder, LayerCtx, RegionIds, ProducerIndex,
					Request.Resolution, LayerIndex);
				FRDGTextureRef EdgeField = nullptr;
				FRDGTextureRef RampField = nullptr;
				AddRegionReliefFieldPass(
					GraphBuilder, Distance, RegionIds, Relief, Request.Resolution,
					LayerIndex, Child.SourceChildIndex, EdgeField, RampField);

				// From here down this is the Pattern relief path, unchanged. The fields carry the
				// same meaning in the same channels, so the pass that consumes them neither knows
				// nor needs to know which producer the regions came from.
				FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
				Tilt.Field = RampField;
				Tilt.EdgeField = EdgeField;
				Tilt.RegionIds = RegionIds;
				// No tilt term: HeightAmount here is a per-region elevation range and rides
				// CellHeightAmount, leaving the relief pass's tilt path -- which is Ramp From
				// IDs' -- switched off. Ramp and Relief compose rather than overlap.
				Tilt.HeightAmount = 0.0f;
				Tilt.CellHeightAmount = Relief.HeightAmount;
				Tilt.CellHeightRandom = Relief.HeightRandom;
				Tilt.bUseEdge = true;
				Tilt.BevelHeight = Relief.BevelHeight;
				Tilt.BevelWidthPixels = Relief.BevelWidthPixels;
				Tilt.BevelWidthCells = Relief.BevelWidthCells;
				Tilt.bBevelRelative = Relief.bRelativeWidth;
				Tilt.BevelVariation = Relief.BevelVariation;
				Tilt.BevelRoundness = Relief.Profile;
				Tilt.BevelRoundnessRandom = Relief.ProfileRandom;
				Tilt.BevelInsetPixels = Relief.BevelInsetPixels;
				Tilt.GapHeight = Relief.GapHeight;
				Tilt.FeatherGain = Relief.FeatherGain;
				Tilt.EdgeRoughness = Relief.EdgeRoughness;
				Tilt.EdgeRoughnessAmount = Relief.EdgeRoughnessAmount;
				Tilt.AOAmount = Relief.AOAmount;
				Tilt.AOSpread = Relief.AOSpread;
				continue;
			}

			if (Child.Type != EMixtormatLayerChildType::RampId)
			{
				continue;
			}

			FRDGTextureRef RegionIds =
				FindRegionIdsAbove(RegionIdMaps, Child.SourceChildIndex);
			if (!RegionIds)
			{
				continue;
			}
			const FRampIdRenderData& Ramp = Child.RampId;
			if (Ramp.HeightAmount <= 0.0f)
			{
				continue;
			}

			FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
			Tilt.Field = AddRampIdPasses(
				GraphBuilder,
				RegionIds,
				Request.Resolution,
				Ramp,
				LayerIndex,
				Child.SourceChildIndex);
			Tilt.EdgeField = Tilt.Field;
			Tilt.HeightAmount = Ramp.HeightAmount;
			Tilt.AOAmount = Ramp.AOAmount;
			Tilt.BlendMode = static_cast<uint32>(Ramp.BlendMode);
		}
	}

	// Region tilt and pattern edge shading, before craquelure relief: a crack carved into a
	// tile that has already settled is right, the reverse drags the groove's depth around.
	void AddRampReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyRegionIds = Ctx.EmptyRegionIds;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
			LayerCtx.PendingRampTilts;
		TShaderMapRef<FMixtormatEdgeShadeCS> EdgeShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatPatternHeightMaxCS> PatternHeightMaxShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatRampIdReliefCS> RampIdReliefShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// The region tilt runs before craquelure relief, and the order is not
		// arbitrary: a crack carved into a tile that has already settled is right,
		// whereas tilting a tile after its crack was carved drags the groove's depth
		// around with the slope.
		for (int32 TiltIndex = 0; TiltIndex < PendingRampTilts.Num(); ++TiltIndex)
		{
			const FPendingRampTilt& Tilt = PendingRampTilts[TiltIndex];
			const bool bNeedsRelief =
				Tilt.HeightAmount > 0.0f
				|| (Tilt.bUseEdge
					&& (Tilt.CellHeightAmount > 0.0f
						|| Tilt.BevelHeight != 0.0f
						|| Tilt.GapHeight != 0.0f));
			if (bNeedsRelief)
			{
				FRDGTextureRef TiltH = GraphBuilder.CreateTexture(
					HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.RampTiltH"));
				FRDGTextureRef TiltN = GraphBuilder.CreateTexture(
					OutputN[WriteIndex]->Desc, TEXT("Mixtormat.RampTiltN"));

				FRDGTextureRef PatternHeightMax = EmptyRegionIds;
				if (Tilt.bUseEdge && Tilt.CellHeightAmount > 0.0f && Tilt.RegionIds)
				{
					PatternHeightMax = GraphBuilder.CreateTexture(
						FRDGTextureDesc::Create2D(
							Request.Resolution,
							PF_R32_UINT,
							FClearValueBinding::None,
							TexCreate_ShaderResource | TexCreate_UAV),
						TEXT("Mixtormat.Pattern.HeightMax"));
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(PatternHeightMax),
						0u);

					FMixtormatPatternHeightMaxCS::FParameters* MaxP =
						GraphBuilder.AllocParameters<FMixtormatPatternHeightMaxCS::FParameters>();
					MaxP->OutputSize = Request.Resolution;
					MaxP->PatternRegionIds = Tilt.RegionIds;
					MaxP->SourceHeight = HeightTargets[WriteIndex];
					MaxP->OutputPatternHeightMax = GraphBuilder.CreateUAV(PatternHeightMax);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.PatternHeightMax.L%d.%d", LayerIndex, TiltIndex),
						PatternHeightMaxShader,
						MaxP,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));
				}

				FMixtormatRampIdReliefCS::FParameters* TiltP =
					GraphBuilder.AllocParameters<FMixtormatRampIdReliefCS::FParameters>();
				TiltP->OutputSize = Request.Resolution;
				TiltP->HeightAmount = Tilt.HeightAmount;
				TiltP->NormalStrength = HeightDerivedNormalStrength;
				TiltP->BlendMode = Tilt.BlendMode;
				TiltP->UseEdge = Tilt.bUseEdge ? 1u : 0u;
				TiltP->CellHeightAmount = Tilt.CellHeightAmount;
				TiltP->CellHeightRandom = Tilt.CellHeightRandom;
				TiltP->BevelHeight = Tilt.BevelHeight;
				TiltP->BevelWidthPixels = Tilt.BevelWidthPixels;
				TiltP->BevelWidthCells = Tilt.BevelWidthCells;
				TiltP->BevelRelative = Tilt.bBevelRelative ? 1u : 0u;
				TiltP->BevelVariation = Tilt.BevelVariation;
				TiltP->BevelRoundness = Tilt.BevelRoundness;
				TiltP->BevelRoundnessRandom = Tilt.BevelRoundnessRandom;
				TiltP->BevelInsetPixels = Tilt.BevelInsetPixels;
				TiltP->GapHeight = Tilt.GapHeight;
				TiltP->FeatherGain = Tilt.FeatherGain;
				TiltP->RampField = Tilt.Field;
				TiltP->EdgeField = Tilt.EdgeField ? Tilt.EdgeField : Tilt.Field;
				TiltP->SourceHeight = HeightTargets[WriteIndex];
				TiltP->PreviousNormal = OutputN[WriteIndex];
				TiltP->PatternRegionIds = Tilt.RegionIds ? Tilt.RegionIds : EmptyRegionIds;
				TiltP->PatternHeightMax = PatternHeightMax;
				TiltP->OutputHeight = GraphBuilder.CreateUAV(TiltH);
				TiltP->OutputNormal = GraphBuilder.CreateUAV(TiltN);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.RegionRelief.L%d.%d", LayerIndex, TiltIndex),
					RampIdReliefShader,
					TiltP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));

				AddHeightDerivedNormalPass(
					Ctx,
					HeightTargets[WriteIndex],
					TiltH,
					OutputN[WriteIndex],
					OutputRAM[WriteIndex],
					TiltN,
					nullptr,
					Request.Resolution,
					HeightDerivedNormalStrength,
					Tilt.AOAmount,
					false,
					TEXT("RegionRelief"));
				AddCopyTexturePass(GraphBuilder, TiltH, HeightTargets[WriteIndex]);
				AddCopyTexturePass(GraphBuilder, TiltN, OutputN[WriteIndex]);
			}

			if (Tilt.bUseEdge
				&& (Tilt.EdgeRoughnessAmount > 0.0f || Tilt.AOAmount > 0.0f))
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.PatternEdgeRAM"));
				FMixtormatEdgeShadeCS::FParameters* EdgeP =
					GraphBuilder.AllocParameters<FMixtormatEdgeShadeCS::FParameters>();
				EdgeP->OutputSize = Request.Resolution;
				EdgeP->BevelWidthPixels = Tilt.BevelWidthPixels;
				EdgeP->BevelVariation = Tilt.BevelVariation;
				EdgeP->AOSpread = Tilt.AOSpread;
				EdgeP->EdgeRoughness = Tilt.EdgeRoughness;
				EdgeP->EdgeRoughnessAmount = Tilt.EdgeRoughnessAmount;
				EdgeP->AOAmount = Tilt.AOAmount;
				EdgeP->EdgeField = Tilt.EdgeField;
				EdgeP->SourceRAM = OutputRAM[WriteIndex];
				EdgeP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.PatternEdgeShade.L%d.%d", LayerIndex, TiltIndex),
					EdgeShadeShader,
					EdgeP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));
				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}
		}
	}

}
