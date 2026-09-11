#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Ordinary one- and few-pass effects, plus craquelure.
//
// Craquelure lives here whole -- network growth, jump-flood distance, the mask resolve and
// the deferred relief -- because splitting the mask half from the relief half would put one
// distance field and one cache across two files.

// Shared height -> normal reconciliation pass for structural effects.
// Effects author height; this pass derives only the normal contribution caused by the
// height delta and RNM-combines it with the normal that entered the effect.
class FMixtormatHeightDeltaNormalCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatHeightDeltaNormalCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatHeightDeltaNormalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, AOAmount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CurrentHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatHeightDeltaNormalCS,
	"/Plugin/Mixtormat/Private/MixtormatHeightDeltaNormal.usf",
	"MainCS",
	SF_Compute);

// Craquelure. A crack network on a cellular lattice, blended into the layer mask.
//
// Its own node rather than a signal on the generated mask: that node reads the surface below
// and early-returns when there is none, while this is generated from a lattice and means
// something on the bottom layer. The mask tail is shared through MixtormatMaskOps.ush rather
// than through a shared parameter struct, so this one carries no surface textures at all.
class FMixtormatCraquelureCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelure.usf",
	"MainCS",
	SF_Compute);

// Propagated craquelure. Three entry points in one file, one shader class each.
//
// Each class binds only the parameters its own entry point uses, rather than a shared struct
// covering all three. That is not tidiness: RDG rejects a pass that binds a transient nothing
// has written, so a seed pass carrying a PreviousState slot would fail on the first dispatch.
class FMixtormatCraquelureSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, SeedChance)
		SHADER_PARAMETER(float, SeedJitter)
		SHADER_PARAMETER(int32, NoiseCells)
		SHADER_PARAMETER(float, StressVariation)
		SHADER_PARAMETER(float, ToughnessVariation)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureGrowCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureGrowCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureGrowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Persistence)
		SHADER_PARAMETER(float, FlowStrength)
		SHADER_PARAMETER(float, StressGain)
		SHADER_PARAMETER(float, ToughnessCost)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, GrowthThreshold)
		SHADER_PARAMETER(float, MinAlignment)
		SHADER_PARAMETER(float, TurnResponse)
		SHADER_PARAMETER(int32, CollisionLimit)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousDirection)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Field)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureGrowCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"GrowCS",
	SF_Compute);

class FMixtormatCraquelureResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		// The warp is read here now rather than during growth, and the uniforms are declared at
		// file scope in a shader with three entry points -- so every struct that compiles
		// MixtormatCraquelureGrow.usf has to declare them, not just the pass that grows.
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"ResolveCS",
	SF_Compute);

// Distance to the nearest crack, by jump flooding. Three entry points, one class each, for the
// same reason the growth passes are split: RDG rejects a pass that binds a transient nothing has
// written, so a seed pass carrying a PreviousRecord slot would fail on its first dispatch.
class FMixtormatCraquelureDistanceSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureDistanceStepCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceStepCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceStepCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceStepCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"StepCS",
	SF_Compute);

class FMixtormatCraquelureDistanceResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"ResolveDistanceCS",
	SF_Compute);

// Craquelure relief. Reads the distance field rather than the mask: by the time the mask exists
// it has been through shaping, a blend mode and a weight lerp, and the distance the groove
// profile needs is gone.
class FMixtormatCraquelureReliefCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureReliefCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureReliefCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, NormalWeight)
		SHADER_PARAMETER(float, ReliefWidthPixels)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(float, Profile)
		SHADER_PARAMETER(float, GrooveVariation)
		SHADER_PARAMETER(float, ProfileVariation)
		SHADER_PARAMETER(float, WidthVariation)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureReliefCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureRelief.usf",
	"MainCS",
	SF_Compute);

class FMixtormatErosionCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatErosionCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatErosionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, NormalPass)
		SHADER_PARAMETER(int32, BlurPass)
		SHADER_PARAMETER(int32, BlurAxis)
		SHADER_PARAMETER(int32, ResamplePass)
		SHADER_PARAMETER(int32, ResampleRidge)
		SHADER_PARAMETER(float, BlurRadius)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(int32, Octaves)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, Gain)
		SHADER_PARAMETER(float, Detail)
		SHADER_PARAMETER(float, GullyWeight)
		SHADER_PARAMETER(float, Normalization)
		SHADER_PARAMETER(float, RidgeRounding)
		SHADER_PARAMETER(float, CreaseRounding)
		SHADER_PARAMETER(float, SlopeOnset)
		SHADER_PARAMETER(float, FeatureOnset)
		SHADER_PARAMETER(float, AssumedSlope)
		SHADER_PARAMETER(float, AssumedSlopeAmount)
		SHADER_PARAMETER(int32, SlopeRadius)
		SHADER_PARAMETER(int32, CurvatureMode)
		SHADER_PARAMETER(float, CavityInfluence)
		SHADER_PARAMETER(float, CavityOffset)
		SHADER_PARAMETER(float, CavityRemapMin)
		SHADER_PARAMETER(float, CavityRemapMax)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PlacementMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousRidge)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRidge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatErosionCS,
	"/Plugin/Mixtormat/Private/MixtormatErosion.usf",
	"MainCS",
	SF_Compute);

// Colour grade. The second Filter: it transforms the base colour composited up to its owning
// layer and is the identity at zero amount.
class FMixtormatGradeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGradeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGradeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(int32, TonemapMode)
		SHADER_PARAMETER(float, TonemapStrength)
		SHADER_PARAMETER(float, Brightness)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, ContrastPivot)
		SHADER_PARAMETER(float, Gamma)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, InputMin)
		SHADER_PARAMETER(float, InputMax)
		SHADER_PARAMETER(float, OutputMin)
		SHADER_PARAMETER(float, OutputMax)
		SHADER_PARAMETER(FVector3f, ChannelBias)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatGradeCS,
	"/Plugin/Mixtormat/Private/MixtormatGrade.usf",
	"MainCS",
	SF_Compute);

// Tileable curl-flow distortion. It transforms every composited material channel in one pass,
// before erosion/chipping, so later weathering reads the same warped height and normal field.
class FMixtormatFlowWarpCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFlowWarpCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFlowWarpCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, EffectWeight)
		SHADER_PARAMETER(int32, Scale)
		SHADER_PARAMETER(float, Direction)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, MaskSlopeInfluence)
		SHADER_PARAMETER(float, HeightSlopeInfluence)
		SHADER_PARAMETER(FVector2f, DerivativeKernel)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFlowWarpCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowWarp.usf",
	"MainCS",
	SF_Compute);

// Mask-weighted roughness for what erosion or chipping removed. Kept separate from the carving
// shaders so its texture slots are not bound on every dispatch that has no use for them.
class FMixtormatCarveShadeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCarveShadeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCarveShadeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, RoughnessAmount)
		SHADER_PARAMETER(float, CarveDepth)
		SHADER_PARAMETER(uint32, UseCoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CarvedHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCarveShadeCS,
	"/Plugin/Mixtormat/Private/MixtormatCarveShade.usf",
	"MainCS",
	SF_Compute);

// Min/max of a scalar texture, folded to 1x1 over a few passes. Chipping thresholds the
// composited height against this rather than against the nominal range of the format, which is
// what makes its Grout Level a position inside the content instead of an absolute value that
// lands on the clear colour.
class FMixtormatReduceMinMaxCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatReduceMinMaxCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatReduceMinMaxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, InputSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, FirstPass)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRange)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRange)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatReduceMinMaxCS,
	"/Plugin/Mixtormat/Private/MixtormatReduceMinMax.usf",
	"MainCS",
	SF_Compute);

// The fold factor the reduction shader uses. Declared here too because the pass count and the
// intermediate sizes are worked out on this side.
static constexpr int32 GMixtormatReduceFactor = 16;

// Chipping. A smooth height selection mixed with local cavity seeds chips, which grow inward
// over N iterations. One dispatch per iteration ping-pongs (core, tip, dirX, dirY); height stays
// read-only so the selection remains defined by the surface chipping received.
class FMixtormatChippingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatChippingCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatChippingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER(int32, NormalPass)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, GroutLevel)
		SHADER_PARAMETER(float, GroutSoftness)
		SHADER_PARAMETER(float, ChipAmount)
		SHADER_PARAMETER(float, ChipSize)
		SHADER_PARAMETER(float, ChipDepth)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, MaskEdge)
		SHADER_PARAMETER(float, CavityInfluence)
		SHADER_PARAMETER(float, CavityOffset)
		SHADER_PARAMETER(float, CavityRemapMin)
		SHADER_PARAMETER(float, CavityRemapMax)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HeightRange)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PlacementMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChipsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputChips)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatChippingCS,
	"/Plugin/Mixtormat/Private/MixtormatChipping.usf",
	"MainCS",
	SF_Compute);

// Worn Edges is a post-composite height filter. One shader layout serves the cheap ID-edge
// localization passes, the Houdini directional-MIN solve, and final-height normal regeneration.
class FMixtormatEdgeWearCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatEdgeWearCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatEdgeWearCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(int32, EdgeStep)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER(float, Slope)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(int32, Directions)
		SHADER_PARAMETER(float, AngularAA)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER(float, GravityAngle)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, MacroScale)
		SHADER_PARAMETER(float, MacroAmount)
		SHADER_PARAMETER(int32, CellScale)
		SHADER_PARAMETER(float, CellAmount)
		SHADER_PARAMETER(int32, RidgeScale)
		SHADER_PARAMETER(float, RidgeAmount)
		SHADER_PARAMETER(int32, MicroScale)
		SHADER_PARAMETER(float, MicroAmount)
		SHADER_PARAMETER(int32, WarpScale)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, NoiseContrast)
		SHADER_PARAMETER(float, IDVariation)
		SHADER_PARAMETER(float, IDRadius)
		SHADER_PARAMETER(float, IDSlope)
		SHADER_PARAMETER(float, IDStrength)
		SHADER_PARAMETER(float, IDNoise)
		SHADER_PARAMETER(uint32, HasPatternEdge)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WornHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousEdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, EdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WearMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputWearMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatEdgeWearCS,
	"/Plugin/Mixtormat/Private/MixtormatEdgeWear.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Shared height -> normal reconciliation for every structural effect that authors
	// height. Its own function rather than a lambda in the graph body because erosion, region
	// relief, craquelure relief, worn edges and chipping all end on it, and they no longer
	// live in one translation unit.
	void AddHeightDerivedNormalPass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef PreviousHeight,
		FRDGTextureRef CurrentHeight,
		FRDGTextureRef PreviousNormal,
		FRDGTextureRef PreviousRAM,
		FRDGTextureRef OutputNormal,
		FRDGTextureRef OutputRAM,
		const FIntPoint Resolution,
		const float NormalStrength,
		const float AOAmount,
		const TCHAR* DebugName)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		TShaderMapRef<FMixtormatHeightDeltaNormalCS> HeightDeltaNormalShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatHeightDeltaNormalCS::FParameters* P =
			GraphBuilder.AllocParameters<FMixtormatHeightDeltaNormalCS::FParameters>();
		P->OutputSize = Resolution;
		P->NormalStrength = NormalStrength;
		P->AOAmount = AOAmount;
		P->PreviousHeight = PreviousHeight;
		P->CurrentHeight = CurrentHeight;
		P->PreviousNormal = PreviousNormal;
		P->PreviousRAM = PreviousRAM;
		P->OutputNormal = GraphBuilder.CreateUAV(OutputNormal);
		P->OutputRAM = GraphBuilder.CreateUAV(OutputRAM);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.HeightDerivedSurface.%s", DebugName),
			HeightDeltaNormalShader,
			P,
			FIntVector(
				FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8),
				1));
	}

	// Craquelure, both modes: the lattice pass, and the propagated network with its growth
	// loop, jump-flood distance and kept-network cache. Relief is queued here and dispatched
	// after the composite by AddCraquelureReliefPasses -- the distance field is carried in the
	// pending entry because the mask targets it was produced alongside are ping-ponged.
	void AddCraquelureMaskPasses(
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
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
			LayerCtx.PendingCraquelureReliefs;
		TShaderMapRef<FMixtormatCraquelureDistanceResolveCS> CraqDistanceResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureDistanceSeedCS> CraqDistanceSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureDistanceStepCS> CraqDistanceStepShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureGrowCS> CraquelureGrowShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureResolveCS> CraquelureResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureSeedCS> CraquelureSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureCS> CraquelureShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FCraquelureRenderData& Crack = Child.Craquelure;

		// The same identity as the other mask children, and the one that
		// saves the most by far: a muted craquelure node was still growing
		// its whole network, up to a thousand full-resolution passes, to
		// produce a mask it then discarded. Relief reads the same network
		// through its own weights, so both halves have to be idle before
		// there is nothing left to compute.
		// Weight 0 makes the mask half the identity: every mask shader
		// ends on saturate(lerp(Previous, Result, Weight)), and the masks it
		// reads are already saturated, so the output is the input bit for
		// bit. Skipping is only exact from the second mask child onward --
		// the first establishes the chain with Initialize, where Previous is
		// zero rather than what the layer already had, and a skip there
		// would leave a different mask behind rather than the same one.
		if (MaskPassIndex > 0
			&& Crack.Weight == 0.0f
			&& Crack.ReliefDepth == 0.0f
			&& Crack.ReliefNormalStrength == 0.0f)
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;
		const FIntVector CrackGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		// (distance to the nearest crack in pixels, crack id). Both modes fill
		// it -- the lattice analytically, the propagated mode by flooding its
		// grown skeleton -- so the mask tail and relief read one field and
		// never learn which built it.
		//
		// Full float rather than half: the id is a lineage hash up to 2^24 and
		// has to stay exact, and a half would truncate it and silently merge
		// unrelated cracks into one variation value.
		const FRDGTextureDesc CraqDistanceDesc = FRDGTextureDesc::Create2D(
			Request.Resolution,
			PF_A32B32G32R32F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		// Propagated networks are looked up before anything is dispatched. On
		// a hit the seed, the growth loop and the whole jump flood are
		// skipped and the kept field is registered straight into this graph:
		// it is the same texture the miss would have produced, so everything
		// downstream is unchanged.
		//
		// Lattice mode is deliberately not cached. Its distance falls out of
		// the same single pass that writes its mask, so there is nothing to
		// skip -- the pass would have to run anyway.
		FRDGTextureRef CraqDistance = nullptr;
		bool bCraqNetworkCached = false;
		if (Crack.Mode == EMixtormatCraquelureMode::Propagated
			&& Request.NetworkCache.IsValid())
		{
			const TRefCountPtr<IPooledRenderTarget> Cached =
				Request.NetworkCache->Find(Crack.NetworkKey, Request.Resolution);
			if (Cached.IsValid())
			{
				CraqDistance = GraphBuilder.RegisterExternalTexture(
					Cached, TEXT("Mixtormat.CraqDistanceCached"));
				bCraqNetworkCached = true;
			}
		}
		if (CraqDistance == nullptr)
		{
			CraqDistance = GraphBuilder.CreateTexture(
				CraqDistanceDesc, TEXT("Mixtormat.CraqDistance"));
		}

		// Half-width of the groove in pixels. Cell units on both sides of the
		// conversion, so it means the same fraction of a cell at any
		// resolution -- and the cell count is the seed lattice in propagated
		// mode and the crack lattice in lattice mode, matching what Width
		// already divides by in each.
		const int32 CraqReliefCells = Crack.Mode == EMixtormatCraquelureMode::Propagated
			? FMath::Max(Crack.SeedCells, 1)
			: FMath::Max(Crack.Period, 1);
		const float CraqReliefWidthPixels =
			Crack.ReliefWidth * Request.Resolution.X / static_cast<float>(CraqReliefCells);

		// Queued whether or not either weight is live, so the branch that
		// decides is in one place; the dispatch below skips a pair of zeroes.
		auto QueueCraquelureRelief = [&]()
		{
			if (Crack.ReliefDepth <= 0.0f && Crack.ReliefNormalStrength <= 0.0f)
			{
				return;
			}
			FPendingCraquelureRelief& Relief = PendingCraquelureReliefs.AddDefaulted_GetRef();
			Relief.Distance = CraqDistance;
			Relief.HeightWeight = Crack.ReliefDepth;
			Relief.NormalWeight = Crack.ReliefNormalStrength;
			Relief.WidthPixels = CraqReliefWidthPixels;
			Relief.Variation = Crack.Variation;
			Relief.Warp = Crack.Warp;
			Relief.WarpPeriod = Crack.WarpPeriod;
			Relief.WarpSeed = Crack.WarpSeed;
			Relief.Profile = Crack.ReliefProfile;
			Relief.GrooveVariation = Crack.ReliefGrooveVariation;
			Relief.ProfileVariation = Crack.ReliefProfileVariation;
			Relief.WidthVariation = Crack.ReliefWidthVariation;
		};

		// Propagated mode grows a network over N iterations against its own
		// ping-ponged state, then resolves it into the layer mask. The
		// state and direction pair are meaningless to any other mask child,
		// so they are allocated here rather than routed through MaskTargets:
		// the node still consumes one MaskPassIndex and writes one R16F
		// target, exactly like every other mask child.
		if (Crack.Mode == EMixtormatCraquelureMode::Propagated)
		{
			// Everything from here to the store is the build, and it runs only
			// on a miss. A hit already holds the field it would produce, and
			// the mask tail below reads the two identically.
			if (!bCraqNetworkCached)
			{
				// State is (cracked, front, id, level) at full float. The id is
				// a lineage hash up to 2^24 and has to stay exact -- a half
				// would truncate it and silently merge unrelated cracks into
				// one, which the per-crack Variation would then show as a
				// single flat value across the whole network.
				const FRDGTextureDesc CraqStateDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_A32B32G32R32F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);

				// Direction only needs two channels and the field four, but both
				// are four here. A two-channel typed UAV is a binding shape
				// nothing else in this compositor uses, and it is the one thing
				// a standalone HLSL compile cannot check -- it validates the
				// shader in isolation, never the format against the
				// declaration. Four bytes a pixel to delete that failure mode.
				const FRDGTextureDesc CraqDirectionDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				const FRDGTextureDesc CraqFieldDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);

				FRDGTextureRef CraqState[2] = {
					GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateA")),
					GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateB"))};
				FRDGTextureRef CraqDirection[2] = {
					GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirA")),
					GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirB"))};
				FRDGTextureRef CraqField =
					GraphBuilder.CreateTexture(CraqFieldDesc, TEXT("Mixtormat.CraqField"));

				FMixtormatCraquelureSeedCS::FParameters* SeedParameters =
					GraphBuilder.AllocParameters<FMixtormatCraquelureSeedCS::FParameters>();
				SeedParameters->OutputSize = Request.Resolution;
				SeedParameters->Seed = Crack.Seed;
				SeedParameters->SeedCells = Crack.SeedCells;
				SeedParameters->SeedChance = Crack.SeedChance;
				SeedParameters->SeedJitter = Crack.SeedJitter;
				SeedParameters->NoiseCells = Crack.NoiseCells;
				SeedParameters->StressVariation = Crack.StressVariation;
				SeedParameters->ToughnessVariation = Crack.ToughnessVariation;
				SeedParameters->Warp = Crack.Warp;
				SeedParameters->WarpPeriod = Crack.WarpPeriod;
				SeedParameters->WarpSeed = Crack.WarpSeed;
				SeedParameters->OutputState = GraphBuilder.CreateUAV(CraqState[0]);
				SeedParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[0]);
				SeedParameters->OutputField = GraphBuilder.CreateUAV(CraqField);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Craquelure.Seed.Layer%d.Child%d", LayerIndex, ChildIndex),
					CraquelureSeedShader,
					SeedParameters,
					CrackGroups);

				// A crack advances one pixel per iteration, so the authored
				// count is a reach in pixels. Scaled against the same 1024
				// reference chipping uses, so a preview and an export grow the
				// same network rather than the same pixel count.
				//
				// The cap is a cost bound. It used to sit at 192, which quietly
				// made it the reach control rather than a guard on it: at a
				// Reach of 1024 the authored value was clamped to under a fifth
				// of itself at every resolution from 1K up, so most of the
				// slider did nothing at all. Raised to the top of the authored
				// range so Reach means what it says.
				//
				// The scaling still stops being honest above that: a 4K export
				// at maximum Reach clamps where the preview did not. Kept as a
				// bound rather than removed because each step is a
				// full-resolution pass doing roughly eighty texture loads per
				// pixel -- this is by some way the most expensive node in the
				// graph, and an unbounded count at 4K is minutes.
				const int32 GrowIterations = FMath::Clamp(
					FMath::RoundToInt(
						Crack.Iterations *
						FMath::Max(Request.Resolution.X, Request.Resolution.Y) / 1024.0f),
					1,
					1024);

				int32 StateIndex = 0;
				for (int32 GrowPass = 0; GrowPass < GrowIterations; ++GrowPass)
				{
					const int32 ReadState = StateIndex;
					const int32 WriteState = 1 - ReadState;

					FMixtormatCraquelureGrowCS::FParameters* GrowParameters =
						GraphBuilder.AllocParameters<FMixtormatCraquelureGrowCS::FParameters>();
					GrowParameters->OutputSize = Request.Resolution;
					GrowParameters->Seed = Crack.Seed;
					GrowParameters->Persistence = Crack.Persistence;
					GrowParameters->FlowStrength = Crack.FlowStrength;
					GrowParameters->StressGain = Crack.StressGain;
					GrowParameters->ToughnessCost = Crack.ToughnessCost;
					GrowParameters->Irregularity = Crack.Irregularity;
					GrowParameters->GrowthThreshold = Crack.GrowthThreshold;
					// Fixed rather than exposed: it only rejects steps a tip
					// would never take anyway, and the interesting control over
					// how straight a crack runs is Persistence.
					GrowParameters->MinAlignment = 0.05f;
					GrowParameters->TurnResponse = Crack.TurnResponse;
					GrowParameters->CollisionLimit = Crack.CollisionLimit;
					GrowParameters->Iteration = GrowPass;
					GrowParameters->PreviousState = CraqState[ReadState];
					GrowParameters->PreviousDirection = CraqDirection[ReadState];
					GrowParameters->Field = CraqField;
					GrowParameters->OutputState = GraphBuilder.CreateUAV(CraqState[WriteState]);
					GrowParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[WriteState]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Grow%d.Layer%d.Child%d",
							GrowPass, LayerIndex, ChildIndex),
						CraquelureGrowShader,
						GrowParameters,
						CrackGroups);

					StateIndex = WriteState;
				}

				// Distance to the grown skeleton, by jump flooding. Log2(N) passes
				// for any radius, where the resolve pass used to brute force a box
				// clamped to radius 8 -- quadratic in the width and a hard cap on
				// it. Relief needs the same field at radii far past what a box
				// could reach, so both read this now.
				{
					const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(
						Request.Resolution,
						PF_A32B32G32R32F,
						FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef Record[2] = {
						GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaA")),
						GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaB"))};

					FMixtormatCraquelureDistanceSeedCS::FParameters* JfaSeed =
						GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceSeedCS::FParameters>();
					JfaSeed->OutputSize = Request.Resolution;
					JfaSeed->CrackState = CraqState[StateIndex];
					JfaSeed->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Distance.Seed.Layer%d.Child%d",
							LayerIndex, ChildIndex),
						CraqDistanceSeedShader,
						JfaSeed,
						CrackGroups);

					int32 RecordIndex = 0;

					// Strides halve from half the padded extent down to 1, then one
					// more pass at 1 -- the JFA+1 variant. Plain jump flooding is
					// not exact: a seed can be lost when the record that would have
					// carried it was itself overwritten at a coarser stride. The
					// extra unit pass costs one dispatch and removes the islands
					// that error shows up as.
					const int32 FirstStep = FMath::Max(
						1,
						static_cast<int32>(FMath::RoundUpToPowerOfTwo(
							static_cast<uint32>(FMath::Max(
								Request.Resolution.X, Request.Resolution.Y)))) / 2);

					for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
					{
						const int32 ReadRecord = RecordIndex;
						const int32 WriteRecord = 1 - ReadRecord;

						FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
							GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
						JfaStep->OutputSize = Request.Resolution;
						JfaStep->StepSize = StepSize;
						JfaStep->PreviousRecord = Record[ReadRecord];
						JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.Craquelure.Distance.Step%d.Layer%d.Child%d",
								StepSize, LayerIndex, ChildIndex),
							CraqDistanceStepShader,
							JfaStep,
							CrackGroups);

						RecordIndex = WriteRecord;
					}

					{
						const int32 ReadRecord = RecordIndex;
						const int32 WriteRecord = 1 - ReadRecord;

						FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
							GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
						JfaStep->OutputSize = Request.Resolution;
						JfaStep->StepSize = 1;
						JfaStep->PreviousRecord = Record[ReadRecord];
						JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.Craquelure.Distance.StepFinal.Layer%d.Child%d",
								LayerIndex, ChildIndex),
							CraqDistanceStepShader,
							JfaStep,
							CrackGroups);

						RecordIndex = WriteRecord;
					}

					FMixtormatCraquelureDistanceResolveCS::FParameters* JfaResolve =
						GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceResolveCS::FParameters>();
					JfaResolve->OutputSize = Request.Resolution;
					JfaResolve->PreviousRecord = Record[RecordIndex];
					JfaResolve->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Distance.Resolve.Layer%d.Child%d",
							LayerIndex, ChildIndex),
						CraqDistanceResolveShader,
						JfaResolve,
						CrackGroups);
				}

				// Kept for the next composite. Converting promotes the transient to
				// a pooled target, which costs the memory of one full-resolution
				// RGBA32F per distinct network -- the trade this whole path makes.
				if (Request.NetworkCache.IsValid())
				{
					Request.NetworkCache->Store(
						Crack.NetworkKey,
						Request.Resolution,
						GraphBuilder.ConvertToExternalTexture(CraqDistance));
				}

			}

			QueueCraquelureRelief();

			FMixtormatCraquelureResolveCS::FParameters* ResolveParameters =
				GraphBuilder.AllocParameters<FMixtormatCraquelureResolveCS::FParameters>();
			ResolveParameters->OutputSize = Request.Resolution;
			ResolveParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
			ResolveParameters->SeedCells = Crack.SeedCells;
			ResolveParameters->Width = Crack.Width;
			ResolveParameters->Variation = Crack.Variation;
			ResolveParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);
			ResolveParameters->Invert = Crack.bInvert ? 1u : 0u;
			ResolveParameters->Weight = Crack.Weight;
			ResolveParameters->Balance = Crack.Balance;
			ResolveParameters->Contrast = Crack.Contrast;
			ResolveParameters->Offset = Crack.Offset;
			ResolveParameters->Warp = Crack.Warp;
			ResolveParameters->WarpPeriod = Crack.WarpPeriod;
			ResolveParameters->WarpSeed = Crack.WarpSeed;
			ResolveParameters->CrackDistance = CraqDistance;
			ResolveParameters->PreviousMask = MaskTargets[MaskReadIndex];
			ResolveParameters->LinearWrapSampler =
				TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			ResolveParameters->OutputMask =
				GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Craquelure.Resolve.Layer%d.Child%d", LayerIndex, ChildIndex),
				CraquelureResolveShader,
				ResolveParameters,
				CrackGroups);

			CombinedMask = MaskTargets[MaskWriteIndex];
			if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
				&& Request.DebugSettings.LayerIndex == LayerIndex
				&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
			{
				FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
					MaskDesc,
					TEXT("Mixtormat.DebugCraquelureSnapshot"));
				AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
				DebugMask = DebugCrackSnapshot;
			}
			++MaskPassIndex;
			return;
		}

		FMixtormatCraquelureCS::FParameters* CrackParameters =
			GraphBuilder.AllocParameters<FMixtormatCraquelureCS::FParameters>();
		CrackParameters->OutputSize = Request.Resolution;
		CrackParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
		CrackParameters->Period = Crack.Period;
		CrackParameters->Jitter = Crack.Jitter;
		CrackParameters->Width = Crack.Width;
		CrackParameters->Variation = Crack.Variation;
		CrackParameters->Seed = Crack.Seed;
		CrackParameters->Warp = Crack.Warp;
		CrackParameters->WarpPeriod = Crack.WarpPeriod;
		CrackParameters->WarpSeed = Crack.WarpSeed;
		CrackParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);
		CrackParameters->Invert = Crack.bInvert ? 1u : 0u;
		CrackParameters->Weight = Crack.Weight;
		CrackParameters->Balance = Crack.Balance;
		CrackParameters->Contrast = Crack.Contrast;
		CrackParameters->Offset = Crack.Offset;
		CrackParameters->PreviousMask = MaskTargets[MaskReadIndex];
		CrackParameters->LinearWrapSampler =
			TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		CrackParameters->OutputMask =
			GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);
		CrackParameters->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Craquelure.Layer%d.Child%d", LayerIndex, ChildIndex),
			CraquelureShader,
			CrackParameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));
		QueueCraquelureRelief();
		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
				MaskDesc,
				TEXT("Mixtormat.DebugCraquelureSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
			DebugMask = DebugCrackSnapshot;
		}
		++MaskPassIndex;
	}

	// Erosion is a post-layer filter: it carves what this layer actually composited, so the
	// child row only records it.
	void QueuePendingErosion(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FPendingEffect& PendingErosion = LayerCtx.PendingErosion;

		// Erosion is a post-layer filter: it carves what this layer actually
		// composited, not the height underneath it. Running it here would let
		// the layer paint straight back over the carve.
		PendingErosion.Effect = &Effect;
		PendingErosion.FeatureMask = FeatureMask;
		PendingErosion.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// Also a post-layer filter, and it runs after erosion.
	void QueuePendingChipping(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FPendingEffect& PendingChipping = LayerCtx.PendingChipping;

		// Also a post-layer filter. It runs after erosion rather than before:
		// chipping a surface that has already weathered is the order that
		// makes sense, and the reverse would have erosion smoothing chips it
		// never saw.
		PendingChipping.Effect = &Effect;
		PendingChipping.FeatureMask = FeatureMask;
		PendingChipping.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// Flow Warp runs first after the composite, before erosion and chipping analyze the
	// displaced surface.
	void QueuePendingFlowWarp(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingFlowWarps = LayerCtx.PendingFlowWarps;

		FPendingEffect& FlowWarp = PendingFlowWarps.AddDefaulted_GetRef();
		FlowWarp.Effect = &Effect;
		FlowWarp.FeatureMask = FeatureMask;
		FlowWarp.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// Grade is a post-layer filter over the accumulated base colour.
	void QueuePendingGrade(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;

		// Also a post-layer filter, for the same reason: it grades what the
		// stack has accumulated at this point, and running it inside the
		// child loop would grade a base colour the layer then overwrites.
		FPendingEffect& Grade = PendingGrades.AddDefaulted_GetRef();
		Grade.Effect = &Effect;
		Grade.FeatureMask = FeatureMask;
		Grade.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// Worn Edges needs an upstream Region ID producer; without one it is a deterministic
	// no-op rather than an invented ID system.
	void QueuePendingWornEdges(
		FMixtormatLayerPassContext& LayerCtx,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
			LayerCtx.PendingWornEdges;

		int32 ProducerChildIndex = INDEX_NONE;
		FRDGTextureRef WearRegionIds = nullptr;
		for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
		{
			if (Entry.Key < Child.SourceChildIndex)
			{
				ProducerChildIndex = Entry.Key;
				WearRegionIds = Entry.Value;
			}
		}
		if (!WearRegionIds)
		{
			// Task B contract: without an upstream Region ID producer Worn Edges
			// is a deterministic no-op rather than inventing an ID system.
			return;
		}

		FPendingWornEdges& Wear = PendingWornEdges.AddDefaulted_GetRef();
		Wear.Effect = &Effect;
		Wear.SourceChildIndex = Child.SourceChildIndex;
		Wear.FeatureMask = FeatureMask;
		Wear.RegionIds = WearRegionIds;
		for (const FPatternIdPassOutput& PatternOutput : PatternOutputs)
		{
			if (PatternOutput.SourceChildIndex != ProducerChildIndex)
			{
				continue;
			}
			// OutputEdge.x is signed pixels only in absolute mode. Relative mode
			// stores a cell fraction, so use the ID-derived band there rather than
			// comparing unlike units to Radius pixels.
			if (PatternOutput.Settings && !PatternOutput.Settings->bRelativeEdgeWidth)
			{
				Wear.PatternEdge = PatternOutput.Edge;
				Wear.bHasPatternEdge = PatternOutput.Edge != nullptr;
			}
			break;
		}
	}

	// Flow Warp runs first after the composite so erosion and chipping analyze the displaced
	// surface. Stacked warps compose in row order.
	void AddFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputBC = Ctx.OutputBC;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingFlowWarps = LayerCtx.PendingFlowWarps;
		TShaderMapRef<FMixtormatFlowWarpCS> FlowWarpShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Flow Warp runs first so erosion and chipping analyze the displaced surface,
		// not the coordinates it occupied before the warp. Stacked warps compose in
		// row order, each reading the channels written by the previous one.
		for (int32 FlowIndex = 0; FlowIndex < PendingFlowWarps.Num(); ++FlowIndex)
		{
			const FPendingEffect& PendingFlow = PendingFlowWarps[FlowIndex];
			const FEffectRenderData& Flow = *PendingFlow.Effect;
			if (FMath::IsNearlyZero(Flow.FlowWarpAmount)
				|| FMath::IsNearlyZero(Flow.FlowWarpWeight))
			{
				continue;
			}

			FRDGTextureRef WarpedBC = GraphBuilder.CreateTexture(
				OutputBC[WriteIndex]->Desc, TEXT("Mixtormat.FlowWarpBC"));
			FRDGTextureRef WarpedN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.FlowWarpN"));
			FRDGTextureRef WarpedRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.FlowWarpRAM"));
			FRDGTextureRef WarpedHeight = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.FlowWarpHeight"));

			FMixtormatFlowWarpCS::FParameters* FP =
				GraphBuilder.AllocParameters<FMixtormatFlowWarpCS::FParameters>();
			FP->OutputSize = Request.Resolution;
			FP->HasMask = (Layer.bHasMask || PendingFlow.bHasScopedMask) ? 1u : 0u;
			FP->Amount = Flow.FlowWarpAmount;
			FP->EffectWeight = Flow.FlowWarpWeight;
			FP->Scale = Flow.FlowWarpScale;
			FP->Direction = Flow.FlowWarpDirection;
			FP->Seed = Flow.FlowWarpSeed;
			FP->MaskSlopeInfluence = Flow.FlowWarpMaskSlopeInfluence;
			FP->HeightSlopeInfluence = Flow.FlowWarpHeightSlopeInfluence;
			FP->DerivativeKernel = Flow.FlowWarpDerivativeKernel;
			FP->BlendMode = Flow.FlowWarpBlendMode;
			FP->SourceBC = OutputBC[WriteIndex];
			FP->SourceN = OutputN[WriteIndex];
			FP->SourceRAM = OutputRAM[WriteIndex];
			FP->SourceHeight = HeightTargets[WriteIndex];
			FP->LayerMask = PendingFlow.FeatureMask;
			FP->LinearWrapSampler =
				TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			FP->OutputBC = GraphBuilder.CreateUAV(WarpedBC);
			FP->OutputN = GraphBuilder.CreateUAV(WarpedN);
			FP->OutputRAM = GraphBuilder.CreateUAV(WarpedRAM);
			FP->OutputHeight = GraphBuilder.CreateUAV(WarpedHeight);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.FlowWarp.Layer%d.%d", LayerIndex, FlowIndex),
				FlowWarpShader,
				FP,
				FIntVector(
					FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
					1));

			AddCopyTexturePass(GraphBuilder, WarpedBC, OutputBC[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WarpedN, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WarpedRAM, OutputRAM[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WarpedHeight, HeightTargets[WriteIndex]);
		}
	}

	// Erosion filters the layer output: it reads the height and normal this layer just
	// composited, carves the height, derives the normal change from what it removed, and writes
	// both back. Also the one place every layer -- eroding or not -- hands the next layer a
	// ridge, which is why the copy/clear above the filter is part of this and not optional.
	void AddErosionPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		FRDGTextureRef* const RidgeTargets = LayerCtx.RidgeTargets;
		const FRDGTextureRef PeelFieldDummy = LayerCtx.PeelFieldDummy;
		FPendingEffect& PendingErosion = LayerCtx.PendingErosion;
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatErosionCS> ErosionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Amount 0 is an exact identity in the erosion shader: Placement falls to
		// zero, the height comes back as it went in and the normal is copied
		// through. It was still costing the full filter -- six passes at twice the
		// composition resolution, so four times the pixels, plus the resample pair
		// -- which is the single most expensive thing a material could carry while
		// doing nothing at all. An erosion node parked at 0, or a mask that has
		// faded it out, now costs one clear.
		//
		// The clear is not an optimisation detail, it is what makes the skip exact:
		// at Amount 0 the filter writes a ridge of zero, so a layer that skipped it
		// and copied the previous ridge forward instead would hand the next layer a
		// different signal from the one it gets today.
		const bool bErosionActive =
			PendingErosion.Effect != nullptr && PendingErosion.Effect->ErosionAmount > 0.0f;

		// Every layer hands the next one a ridge, whether or not it erodes. A layer
		// that left the slot alone would pass on the ridge from two layers back,
		// which reads as the mask signal being correct on some layers and stale on
		// others. Eroding layers overwrite this below.
		if (!PendingErosion.Effect)
		{
			AddCopyTexturePass(
				GraphBuilder,
				RidgeTargets[1 - WriteIndex],
				RidgeTargets[WriteIndex]);
		}
		else if (!bErosionActive)
		{
			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(RidgeTargets[WriteIndex]),
				FVector4f(0.0f));
		}

		// Erosion filters the layer output: it reads the height and normal this
		// layer just composited, carves the height, derives the normal change from
		// what it removed, and writes both back.
		if (bErosionActive)
		{
			const FEffectRenderData& Ero = *PendingErosion.Effect;
			const bool bUseLegacyPlacementMask =
				!PendingErosion.bHasScopedMask && Ero.ErosionPlacementMask.IsValid();
			FRDGTextureRef ErosionPlacementMask = bUseLegacyPlacementMask
				? RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Ero.ErosionPlacementMask,
					TEXT("Mixtormat.ErosionPlacementMask"))
				: PeelFieldDummy;

			// Erosion runs at twice the composition resolution, capped at 4096,
			// then resamples back. Carving is high-frequency work: at composition
			// resolution the octave loop hits the two-pixels-per-cell floor with
			// passes still to run, so the finest gullies have nowhere to cut.
			// Above 4096 the cost stops buying visible detail.
			const FIntPoint EroRes(
				FMath::Min(Request.Resolution.X * 2, 4096),
				FMath::Min(Request.Resolution.Y * 2, 4096));
			const bool bResample = EroRes != Request.Resolution;

			// The height chain is R32F, not R16F like the rest of the compositor.
			// Every quantity this filter derives is a difference of two nearly
			// equal heights, and half floats do not survive that.
			//
			// A half around mid height has a ULP of 2^-11, about 4.9e-4. The slope
			// Sobel sums six taps and scales by Res/(8R) -- 128 at 2K with radius 2
			// -- so quantisation alone puts roughly 0.25 of noise on a slope the
			// repose gate thresholds at 0.30 with a 0.25 transition. The gate is
			// then close to a coin flip per pixel and it multiplies the carve, so
			// the height comes out dithered before the normal pass amplifies
			// anything. Slope Blur cannot help: the blur averages correctly and the
			// R16F write throws the result straight back to one ULP.
			//
			// The normal pass is the second victim: it differences the carve depth
			// between neighbours, and those differences are far smaller than the
			// carve itself, so they land on nought, one or two ULP -- a handful of
			// distinct slopes over the whole carve. The Hessian is the third, since
			// a second difference divided by StepUV squared multiplies its error by
			// about a million.
			//
			// EroGuide has to be R32F for the same reason as the rest: it is what
			// the slope and curvature stencils actually read.
			const FRDGTextureDesc EroDesc = FRDGTextureDesc::Create2D(
				EroRes,
				PF_R32_FLOAT,
				FClearValueBinding::White,
				TexCreate_ShaderResource | TexCreate_UAV);

			// The ridge map is a 0..1 signal that is never differenced, so it keeps
			// the cheaper format.
			const FRDGTextureDesc EroRidgeDesc = FRDGTextureDesc::Create2D(
				EroRes,
				PF_R16F,
				FClearValueBinding::White,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureDesc EroNormalDesc = OutputN[WriteIndex]->Desc;
			EroNormalDesc.Extent = EroRes;

			FRDGTextureRef SourceH = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionSrc"));
			FRDGTextureRef EroH[2] = {
				GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionA")),
				GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionB"))};
			FRDGTextureRef EroRidge = GraphBuilder.CreateTexture(EroRidgeDesc, TEXT("Mixtormat.ErosionRidge"));
			FRDGTextureRef EroGuide = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionGuide"));

			// The horizontal half of the separable slope blur. Allocated here with
			// the rest rather than per pass: it is another full erosion-resolution
			// R32F transient, 64MB at the 4096 cap.
			FRDGTextureRef EroGuideX = GraphBuilder.CreateTexture(
				EroDesc, TEXT("Mixtormat.ErosionGuideX"));
			FRDGTextureRef EroN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionN"));
			// The layer normal every carving pass reads, lifted to erosion resolution.
			FRDGTextureRef EroSrcN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionSrcN"));

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroRidge), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

			// Stands in at both ends of the ridge plumbing: the UAV slot on the
			// upsample, which must not be aimed at a composition-res target from an
			// erosion-res dispatch, and the SRV slot on every carving and blur pass,
			// which cannot read EroRidge because those passes write it.
			//
			// Cleared rather than left alone: RDG rejects a read of a transient
			// texture nothing has written, and it is now read as well as bound.
			FRDGTextureRef ResampleRidgeDummy = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_R16F,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.ErosionRidgeDummy"));
			AddClearUAVPass(
				GraphBuilder, GraphBuilder.CreateUAV(ResampleRidgeDummy), FVector4f(0.0f));

			// One dispatch moves height and normal together, in either direction.
			auto AddErosionResample = [&](
				FRDGTextureRef InH,
				FRDGTextureRef InN,
				FRDGTextureRef OutH,
				FRDGTextureRef OutN,
				FRDGTextureRef InRidge,
				FRDGTextureRef OutRidgeTarget,
				const FIntPoint DestRes,
				const TCHAR* DebugName)
			{
				// A ridge target only on the way down. On the way up the dispatch
				// runs at erosion resolution and the ridge slot holds the 1x1
				// dummy, so the shader's write is gated off rather than aimed at
				// a target it would overrun.
				const bool bCarryRidge = OutRidgeTarget != nullptr;

				FMixtormatErosionCS::FParameters* RP =
					GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
				RP->OutputSize = DestRes;
				RP->NormalPass = 0;
				RP->BlurPass = 0;
				RP->BlurAxis = 0;
				RP->ResamplePass = 1;
				RP->ResampleRidge = bCarryRidge ? 1 : 0;
				RP->PreviousRidge = InRidge;
				RP->PreviousHeight = InH;
				RP->SourceHeight = InH;
				RP->GuideHeight = InH;
				RP->LayerMask = PendingErosion.FeatureMask;
				RP->UsePlacementMask = bUseLegacyPlacementMask ? 1u : 0u;
				RP->PlacementMaskTiling = Ero.ErosionMaskTiling;
				RP->PlacementMaskTexture = ErosionPlacementMask;
				RP->PreviousNormal = InN;
				RP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				RP->OutputHeight = GraphBuilder.CreateUAV(OutH);
				RP->OutputRidge = GraphBuilder.CreateUAV(
					bCarryRidge ? OutRidgeTarget : ResampleRidgeDummy);
				RP->OutputNormal = GraphBuilder.CreateUAV(OutN);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.%s", LayerIndex, DebugName),
					ErosionShader,
					RP,
					FIntVector(
						FMath::DivideAndRoundUp(DestRes.X, 8),
						FMath::DivideAndRoundUp(DestRes.Y, 8),
						1));
			};

			if (bResample)
			{
				AddErosionResample(
					HeightTargets[WriteIndex], OutputN[WriteIndex],
					SourceH, EroSrcN,
					EroRidge, nullptr,
					EroRes, TEXT("Up"));
			}
			else
			{
				AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], SourceH);
				AddCopyTexturePass(GraphBuilder, OutputN[WriteIndex], EroSrcN);
			}

			// All octaves are evaluated together, so steering is analytical and no pass
			// can feed a masked boundary or quantized intermediate back into the next band.
			auto SetErosionParameters = [&](FMixtormatErosionCS::FParameters* Parameters)
			{
				Parameters->OutputSize = EroRes;
				Parameters->NormalPass = 0;
				Parameters->BlurPass = 0;
				Parameters->BlurAxis = 0;
				Parameters->ResamplePass = 0;
				Parameters->ResampleRidge = 0;
				Parameters->BlurRadius = Ero.ErosionSlopeBlur;
				Parameters->NormalStrength = Ero.ErosionNormalStrength;
				Parameters->Amount = Ero.ErosionAmount;
				Parameters->Strength = Ero.ErosionStrength;
				Parameters->Octaves = Ero.ErosionOctaves;
				Parameters->Period = Ero.ErosionPeriod;
				Parameters->Gain = Ero.ErosionGain;
				Parameters->Detail = Ero.ErosionDetail;
				Parameters->GullyWeight = Ero.ErosionGullyWeight;
				Parameters->Normalization = Ero.ErosionNormalization;
				Parameters->RidgeRounding = Ero.ErosionRidgeRounding;
				Parameters->CreaseRounding = Ero.ErosionCreaseRounding;
				Parameters->SlopeOnset = Ero.ErosionSlopeOnset;
				Parameters->FeatureOnset = Ero.ErosionFeatureOnset;
				Parameters->AssumedSlope = Ero.ErosionAssumedSlope;
				Parameters->AssumedSlopeAmount = Ero.ErosionAssumedSlopeAmount;
				Parameters->SlopeRadius = Ero.ErosionSlopeRadius;
				Parameters->CurvatureMode = Ero.ErosionCurvatureMode;
				Parameters->CavityInfluence = Ero.ErosionCavityInfluence;
				Parameters->CavityOffset = Ero.ErosionCavityOffset;
				Parameters->CavityRemapMin = Ero.ErosionCavityRemapMin;
				Parameters->CavityRemapMax = Ero.ErosionCavityRemapMax;
				Parameters->HeightInfluence = Ero.ErosionHeightInfluence;
				Parameters->HeightScale = Ero.ErosionHeightScale;
				Parameters->UsePlacementMask = bUseLegacyPlacementMask ? 1u : 0u;
				Parameters->PlacementMaskTiling = Ero.ErosionMaskTiling;
				Parameters->InvertMask =
					!PendingErosion.bHasScopedMask && Ero.bErosionInvertMask ? 1u : 0u;
				Parameters->Seed = 1u;
				Parameters->SourceHeight = SourceH;
				Parameters->PreviousNormal = EroSrcN;
				Parameters->LayerMask = PendingErosion.FeatureMask;
				Parameters->PlacementMaskTexture = ErosionPlacementMask;
				Parameters->PreviousRidge = ResampleRidgeDummy;
				Parameters->LinearWrapSampler =
					TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
				Parameters->OutputRidge = GraphBuilder.CreateUAV(EroRidge);
				Parameters->OutputNormal = GraphBuilder.CreateUAV(EroN);
			};

			const FIntVector ErosionGroups(
				FMath::DivideAndRoundUp(EroRes.X, 8),
				FMath::DivideAndRoundUp(EroRes.Y, 8),
				1);

			FRDGTextureRef Guidance = SourceH;
			if (Ero.ErosionSlopeBlur > 0.0f)
			{
				FMixtormatErosionCS::FParameters* BlurX =
					GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
				SetErosionParameters(BlurX);
				BlurX->BlurPass = 1;
				BlurX->BlurAxis = 0;
				BlurX->PreviousHeight = SourceH;
				BlurX->GuideHeight = SourceH;
				BlurX->OutputHeight = GraphBuilder.CreateUAV(EroGuideX);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.BlurX", LayerIndex),
					ErosionShader,
					BlurX,
					ErosionGroups);

				FMixtormatErosionCS::FParameters* BlurY =
					GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
				*BlurY = *BlurX;
				BlurY->BlurAxis = 1;
				BlurY->PreviousHeight = EroGuideX;
				BlurY->OutputHeight = GraphBuilder.CreateUAV(EroGuide);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.BlurY", LayerIndex),
					ErosionShader,
					BlurY,
					ErosionGroups);
				Guidance = EroGuide;
			}

			FMixtormatErosionCS::FParameters* ErosionParameters =
				GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
			SetErosionParameters(ErosionParameters);
			ErosionParameters->PreviousHeight = SourceH;
			ErosionParameters->GuideHeight = Guidance;
			ErosionParameters->OutputHeight = GraphBuilder.CreateUAV(EroH[0]);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Filter", LayerIndex),
				ErosionShader,
				ErosionParameters,
				ErosionGroups);

			FRDGTextureRef Result = EroH[0];
			FRDGTextureRef EroRAM = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					EroRes, PF_FloatRGBA, FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Erosion.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				SourceH,
				Result,
				EroSrcN,
				OutputRAM[WriteIndex],
				EroN,
				EroRAM,
				EroRes,
				Ero.ErosionNormalStrength,
				0.0f,
				TEXT("Erosion"));

			if (bResample)
			{
				AddErosionResample(
					Result, EroN,
					HeightTargets[WriteIndex], OutputN[WriteIndex],
					EroRidge, RidgeTargets[WriteIndex],
					Request.Resolution, TEXT("Down"));
			}
			else
			{
				AddCopyTexturePass(GraphBuilder, Result, HeightTargets[WriteIndex]);
				AddCopyTexturePass(GraphBuilder, EroN, OutputN[WriteIndex]);
				AddCopyTexturePass(GraphBuilder, EroRidge, RidgeTargets[WriteIndex]);
			}

			// Roughness is the only packed surface channel erosion changes. Skip the
			// full-resolution pass when its mask weight is neutral.
			if (Ero.ErosionRoughnessAmount != 0.0f)
			{
				// Through scratch and back rather than in place: RAM cannot be bound as
				// both SRV and UAV on the same dispatch.
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.ErosionShadeRAM"));

				FMixtormatCarveShadeCS::FParameters* SP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				SP->OutputSize = Request.Resolution;
				SP->RoughnessAmount = Ero.ErosionRoughnessAmount;
				SP->CarveDepth = Ero.ErosionCarveDepth;

				// Erosion recovers coverage from the height pair, so the coverage
				// slot is unread here. Bind an existing valid scalar texture.
				SP->UseCoverageTexture = 0;
				SP->CoverageTexture = SourceH;

				SP->SourceHeight = SourceH;
				SP->CarvedHeight = Result;
				SP->SourceRAM = OutputRAM[WriteIndex];
				SP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Roughness", LayerIndex),
					CarveShadeShader,
					SP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));

				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}
		}
	}

	// Craquelure relief: after erosion, before chipping, so chipping selects from a height
	// with the cracks already carved into it.
	void AddCraquelureReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
			LayerCtx.PendingCraquelureReliefs;
		TShaderMapRef<FMixtormatCraquelureReliefCS> CraquelureReliefShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Craquelure relief runs after erosion but before chipping. Chipping selects
		// from the current height and its cavity, so it must see cracks already carved
		// into the layer rather than the flat height that preceded them.
		for (int32 ReliefIndex = 0; ReliefIndex < PendingCraquelureReliefs.Num(); ++ReliefIndex)
		{
			const FPendingCraquelureRelief& Relief = PendingCraquelureReliefs[ReliefIndex];
			FRDGTextureRef ReliefH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefH"));
			FRDGTextureRef ReliefN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefN"));

			FMixtormatCraquelureReliefCS::FParameters* RelP =
				GraphBuilder.AllocParameters<FMixtormatCraquelureReliefCS::FParameters>();
			RelP->OutputSize = Request.Resolution;
			RelP->HeightWeight = Relief.HeightWeight;
			RelP->NormalWeight = 0.0f;
			RelP->ReliefWidthPixels = Relief.WidthPixels;
			RelP->Variation = Relief.Variation;
			RelP->Profile = Relief.Profile;
			RelP->GrooveVariation = Relief.GrooveVariation;
			RelP->ProfileVariation = Relief.ProfileVariation;
			RelP->WidthVariation = Relief.WidthVariation;
			RelP->Warp = Relief.Warp;
			RelP->WarpPeriod = Relief.WarpPeriod;
			RelP->WarpSeed = Relief.WarpSeed;
			RelP->CrackDistance = Relief.Distance;
			RelP->SourceHeight = HeightTargets[WriteIndex];
			RelP->PreviousNormal = OutputN[WriteIndex];
			RelP->OutputHeight = GraphBuilder.CreateUAV(ReliefH);
			RelP->OutputNormal = GraphBuilder.CreateUAV(ReliefN);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.Craquelure.Relief.L%d.%d", LayerIndex, ReliefIndex),
				CraquelureReliefShader,
				RelP,
				FIntVector(
					FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
					1));

			FRDGTextureRef ReliefRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Craquelure.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				HeightTargets[WriteIndex],
				ReliefH,
				OutputN[WriteIndex],
				OutputRAM[WriteIndex],
				ReliefN,
				ReliefRAM,
				Request.Resolution,
				Relief.NormalWeight,
				0.35f,
				TEXT("Craquelure"));
			AddCopyTexturePass(GraphBuilder, ReliefH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ReliefN, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ReliefRAM, OutputRAM[WriteIndex]);
		}
	}

	// Worn Edges: after Pattern/Ramp and craquelure relief so its input is the real structural
	// height, before Chipping so later damage sees the rounded surface. Publishes its wear
	// coverage as a named mask output.
	void AddWornEdgesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyPatternUV = Ctx.EmptyPatternUV;
		TMap<FPublishedMaskKey, FRDGTextureRef>& PublishedMaskOutputs = Ctx.PublishedMaskOutputs;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
			LayerCtx.PendingWornEdges;
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatEdgeWearCS> EdgeWearShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Worn Edges runs after Pattern/Ramp and craquelure relief so its input is the
		// actual structural + material height, and before Chipping so later damage sees
		// the rounded surface. Multiple Worn Edges nodes chain in child order.
		for (int32 WearIndex = 0; WearIndex < PendingWornEdges.Num(); ++WearIndex)
		{
			const FPendingWornEdges& PendingWear = PendingWornEdges[WearIndex];
			const FEffectRenderData& Wear = *PendingWear.Effect;
			if (Wear.EdgeWearStrength <= 0.0f)
			{
				continue;
			}

			const FIntVector WearGroups(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1);
			const FRDGTextureDesc WearScalarDesc = FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef WearSourceH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.SourceH"));
			FRDGTextureRef WornH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.Height"));
			FRDGTextureRef WornN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.Normal"));
			FRDGTextureRef EdgeWearMask = GraphBuilder.CreateTexture(
				WearScalarDesc, TEXT("Mixtormat.WornEdges.EdgeWearMask"));
			AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], WearSourceH);

			// One layout is shared by all four shader modes. Tiny distinct dummies keep
			// every reflected slot valid without ever binding one resource as both SRV
			// and UAV in the same pass.
			const FRDGTextureDesc TinyScalarDesc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1), PF_R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			const FRDGTextureDesc TinyNormalDesc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef ReadDummy = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.ReadDummy"));
			FRDGTextureRef WriteDummyA = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyA"));
			FRDGTextureRef WriteDummyB = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyB"));
			FRDGTextureRef WriteDummyC = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyC"));
			FRDGTextureRef NormalDummy = GraphBuilder.CreateTexture(TinyNormalDesc, TEXT("Mixtormat.WornEdges.NormalDummy"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ReadDummy), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyA), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyB), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyC), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(NormalDummy), FVector4f(0.0f));

			FRDGTextureRef FinalEdgeBand = ReadDummy;
			if (!PendingWear.bHasPatternEdge)
			{
				FRDGTextureRef Band[2] = {
					GraphBuilder.CreateTexture(WearScalarDesc, TEXT("Mixtormat.WornEdges.BandA")),
					GraphBuilder.CreateTexture(WearScalarDesc, TEXT("Mixtormat.WornEdges.BandB"))};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Band[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Band[1]), FVector4f(0.0f));

				FMixtormatEdgeWearCS::FParameters* SeedP =
					GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
				SeedP->OutputSize = Request.Resolution;
				SeedP->Mode = 0;
				SeedP->EdgeStep = 1;
				SeedP->Radius = Wear.EdgeWearRadius;
				SeedP->Slope = Wear.EdgeWearSlope;
				SeedP->Strength = Wear.EdgeWearStrength;
				SeedP->Feather = Wear.EdgeWearFeather;
				SeedP->Directions = Wear.EdgeWearDirections;
				SeedP->AngularAA = Wear.EdgeWearAngularAA;
				SeedP->Gravity = Wear.EdgeWearGravity;
				SeedP->GravityAngle = Wear.EdgeWearGravityAngle;
				SeedP->Seed = Wear.EdgeWearSeed;
				SeedP->MacroScale = Wear.EdgeWearMacroScale;
				SeedP->MacroAmount = Wear.EdgeWearMacroAmount;
				SeedP->CellScale = Wear.EdgeWearCellScale;
				SeedP->CellAmount = Wear.EdgeWearCellAmount;
				SeedP->RidgeScale = Wear.EdgeWearRidgeScale;
				SeedP->RidgeAmount = Wear.EdgeWearRidgeAmount;
				SeedP->MicroScale = Wear.EdgeWearMicroScale;
				SeedP->MicroAmount = Wear.EdgeWearMicroAmount;
				SeedP->WarpScale = Wear.EdgeWearWarpScale;
				SeedP->WarpAmount = Wear.EdgeWearWarpAmount;
				SeedP->NoiseContrast = Wear.EdgeWearNoiseContrast;
				SeedP->IDVariation = Wear.EdgeWearIdVariation;
				SeedP->IDRadius = Wear.EdgeWearIdRadius;
				SeedP->IDSlope = Wear.EdgeWearIdSlope;
				SeedP->IDStrength = Wear.EdgeWearIdStrength;
				SeedP->IDNoise = Wear.EdgeWearIdNoise;
				SeedP->HasPatternEdge = 0u;
				SeedP->SourceHeight = WearSourceH;
				SeedP->WornHeight = ReadDummy;
				SeedP->PreviousNormal = OutputN[WriteIndex];
				SeedP->RegionIds = PendingWear.RegionIds;
				SeedP->EdgeField = EmptyPatternUV;
				SeedP->PreviousEdgeBand = ReadDummy;
				SeedP->EdgeBand = ReadDummy;
				SeedP->LayerMask = PendingWear.FeatureMask;
				SeedP->WearMask = ReadDummy;
				SeedP->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SeedP->OutputHeight = GraphBuilder.CreateUAV(WriteDummyA);
				SeedP->OutputWearMask = GraphBuilder.CreateUAV(WriteDummyB);
				SeedP->OutputEdgeBand = GraphBuilder.CreateUAV(Band[0]);
				SeedP->OutputNormal = GraphBuilder.CreateUAV(NormalDummy);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.EdgeSeed", LayerIndex, WearIndex),
					EdgeWearShader,
					SeedP,
					WearGroups);

				const float MaxIdRadiusMul = FMath::Max(
					1.0f + FMath::Abs(Wear.EdgeWearIdRadius) * FMath::Clamp(Wear.EdgeWearIdVariation, 0.0f, 1.0f),
					0.15f);
				const int32 ConservativeRadius = FMath::Clamp(
					FMath::CeilToInt(static_cast<float>(Wear.EdgeWearRadius) * MaxIdRadiusMul * 1.80f),
					1,
					64);
				int32 BandIndex = 0;
				int32 Covered = 0;
				int32 DilationStep = 1;
				while (Covered < ConservativeRadius)
				{
					const int32 ThisStep = FMath::Min(DilationStep, ConservativeRadius - Covered);
					const int32 ReadBand = BandIndex;
					const int32 WriteBand = 1 - ReadBand;
					FMixtormatEdgeWearCS::FParameters* DilateP =
						GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
					*DilateP = *SeedP;
					DilateP->Mode = 1;
					DilateP->EdgeStep = ThisStep;
					DilateP->PreviousEdgeBand = Band[ReadBand];
					DilateP->OutputEdgeBand = GraphBuilder.CreateUAV(Band[WriteBand]);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.EdgeDilate%d", LayerIndex, WearIndex, Covered),
						EdgeWearShader,
						DilateP,
						WearGroups);
					BandIndex = WriteBand;
					Covered += ThisStep;
					DilationStep *= 2;
				}
				FinalEdgeBand = Band[BandIndex];
			}

			auto FillWearParameters = [&](FMixtormatEdgeWearCS::FParameters* P)
			{
				P->OutputSize = Request.Resolution;
				P->EdgeStep = 1;
				P->Radius = Wear.EdgeWearRadius;
				P->Slope = Wear.EdgeWearSlope;
				P->Strength = Wear.EdgeWearStrength;
				P->Feather = Wear.EdgeWearFeather;
				P->Directions = Wear.EdgeWearDirections;
				P->AngularAA = Wear.EdgeWearAngularAA;
				P->Gravity = Wear.EdgeWearGravity;
				P->GravityAngle = Wear.EdgeWearGravityAngle;
				P->Seed = Wear.EdgeWearSeed;
				P->MacroScale = Wear.EdgeWearMacroScale;
				P->MacroAmount = Wear.EdgeWearMacroAmount;
				P->CellScale = Wear.EdgeWearCellScale;
				P->CellAmount = Wear.EdgeWearCellAmount;
				P->RidgeScale = Wear.EdgeWearRidgeScale;
				P->RidgeAmount = Wear.EdgeWearRidgeAmount;
				P->MicroScale = Wear.EdgeWearMicroScale;
				P->MicroAmount = Wear.EdgeWearMicroAmount;
				P->WarpScale = Wear.EdgeWearWarpScale;
				P->WarpAmount = Wear.EdgeWearWarpAmount;
				P->NoiseContrast = Wear.EdgeWearNoiseContrast;
				P->IDVariation = Wear.EdgeWearIdVariation;
				P->IDRadius = Wear.EdgeWearIdRadius;
				P->IDSlope = Wear.EdgeWearIdSlope;
				P->IDStrength = Wear.EdgeWearIdStrength;
				P->IDNoise = Wear.EdgeWearIdNoise;
				P->HasPatternEdge = PendingWear.bHasPatternEdge ? 1u : 0u;
				P->SourceHeight = WearSourceH;
				P->WornHeight = ReadDummy;
				P->PreviousNormal = OutputN[WriteIndex];
				P->RegionIds = PendingWear.RegionIds;
				P->EdgeField = PendingWear.bHasPatternEdge ? PendingWear.PatternEdge : EmptyPatternUV;
				P->PreviousEdgeBand = ReadDummy;
				P->EdgeBand = FinalEdgeBand;
				P->LayerMask = PendingWear.FeatureMask;
				P->WearMask = ReadDummy;
				P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			};

			FMixtormatEdgeWearCS::FParameters* WearP =
				GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
			FillWearParameters(WearP);
			WearP->Mode = 2;
			WearP->OutputHeight = GraphBuilder.CreateUAV(WornH);
			WearP->OutputWearMask = GraphBuilder.CreateUAV(EdgeWearMask);
			WearP->OutputEdgeBand = GraphBuilder.CreateUAV(WriteDummyC);
			WearP->OutputNormal = GraphBuilder.CreateUAV(NormalDummy);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.Wear", LayerIndex, WearIndex),
				EdgeWearShader,
				WearP,
				WearGroups);

			PublishedMaskOutputs.Add(
				FPublishedMaskKey{
					Layer.LayerId,
					PendingWear.SourceChildIndex,
					FName(TEXT("Wear"))},
				EdgeWearMask);

			FRDGTextureRef WornRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				WearSourceH,
				WornH,
				OutputN[WriteIndex],
				OutputRAM[WriteIndex],
				WornN,
				WornRAM,
				Request.Resolution,
				8.0f,
				0.35f,
				TEXT("WornEdges"));
			AddCopyTexturePass(GraphBuilder, WornRAM, OutputRAM[WriteIndex]);

			// EdgeWearMask is the generated wear coverage, already gated by the
			// feature scope. It is the sole roughness mask; placement is not sampled
			// directly a second time here.
			const float RoughnessAmount =
				Wear.EdgeWearRoughnessWeight * Wear.EdgeWearRoughnessOffset;
			if (RoughnessAmount != 0.0f)
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc,
					TEXT("Mixtormat.WornEdges.RoughnessRAM"));
				FMixtormatCarveShadeCS::FParameters* ShadeP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				ShadeP->OutputSize = Request.Resolution;
				ShadeP->RoughnessAmount = RoughnessAmount;
				ShadeP->CarveDepth = 1.0f;
				ShadeP->UseCoverageTexture = 1u;
				ShadeP->CoverageTexture = EdgeWearMask;
				ShadeP->SourceHeight = WornH;
				ShadeP->CarvedHeight = WornH;
				ShadeP->SourceRAM = OutputRAM[WriteIndex];
				ShadeP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				ShadeP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME(
						"Mixtormat.WornEdges.L%d.%d.Roughness",
						LayerIndex, WearIndex),
					CarveShadeShader,
					ShadeP,
					WearGroups);
				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}

			AddCopyTexturePass(GraphBuilder, WornH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WornN, OutputN[WriteIndex]);
		}
	}

	// Chipping: a smooth height selection mixed with local cavity seeds chips, grown inward
	// over N ping-ponged iterations against a height held read-only for the whole loop.
	void AddChippingPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		const FRDGTextureRef PeelFieldDummy = LayerCtx.PeelFieldDummy;
		FPendingEffect& PendingChipping = LayerCtx.PendingChipping;
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatChippingCS> ChippingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatReduceMinMaxCS> ReduceMinMaxShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Chipping filters the layer output the same way erosion does, after both
		// erosion and craquelure have finished shaping the height it selects from.
		// Amount 0 seeds nothing, so it should also cost nothing rather than run
		// the iteration loop to produce an unchanged height.
		if (PendingChipping.Effect && PendingChipping.Effect->ChipAmount > 0.0f)
		{
			const FEffectRenderData& Chip = *PendingChipping.Effect;
			const bool bUseLegacyPlacementMask =
				!PendingChipping.bHasScopedMask && Chip.ChipPlacementMask.IsValid();
			FRDGTextureRef ChippingPlacementMask = bUseLegacyPlacementMask
				? RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Chip.ChipPlacementMask,
					TEXT("Mixtormat.ChippingPlacementMask"))
				: PeelFieldDummy;

			// A chip advances one pixel per iteration, so the authored count is a
			// reach in pixels. Scaled against a 1024 reference so a 512 preview and
			// a 2048 export show the same chip size rather than the same pixel
			// count -- otherwise the preview lies about the result.
			//
			// The obvious alternative, a dilated 3x3 gather at stride N, is cheaper
			// and wrong: at stride 2 the four pixel-parity classes never read each
			// other, so it produces four interleaved chip networks instead of one.
			//
			// This is the one filter whose dispatch count scales with output size.
			// At 4K with Iterations 24 the clamp binds at 96 full-resolution passes,
			// which is where a slow export will be coming from.
			const int32 ChipIterations = FMath::Clamp(
				FMath::RoundToInt(
					Chip.ChipIterations *
					FMath::Max(Request.Resolution.X, Request.Resolution.Y) / 1024.0f),
				1,
				96);

			// The state is (core, tip, dirX, dirY) at full float, not half, for the
			// reason the erosion height chain is R32F. Tip is a geometric decay --
			// multiplied by 0.72..0.99 every iteration, read back, re-multiplied --
			// and tested against a hard 0.001 cutoff, so half-float quantisation
			// near that cutoff turns a chip stopping into a per-pixel coin flip.
			// The stored direction is worse: it is renormalised every pass and fed
			// to a hard alignment test at dot > -0.10.
			const FRDGTextureDesc ChipStateDesc = FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_A32B32G32R32F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			const FRDGTextureDesc ChipMaskDesc = FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef ChipState[2] = {
				GraphBuilder.CreateTexture(ChipStateDesc, TEXT("Mixtormat.ChipStateA")),
				GraphBuilder.CreateTexture(ChipStateDesc, TEXT("Mixtormat.ChipStateB"))};

			// The chip mask ping-pongs for the same reason the state does: the
			// normal pass and the shade pass both read it, and a pass cannot write
			// the texture it is reading.
			FRDGTextureRef ChipMask[2] = {
				GraphBuilder.CreateTexture(ChipMaskDesc, TEXT("Mixtormat.ChipMaskA")),
				GraphBuilder.CreateTexture(ChipMaskDesc, TEXT("Mixtormat.ChipMaskB"))};

			// The height the layer composited, held aside. Every iteration reads
			// this rather than its own output, matching the read-only height bind in
			// the prototype: a chip must not be able to carve its own brick down
			// into grout and so stop itself.
			FRDGTextureRef ChipSourceH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.ChipSourceH"));
			AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], ChipSourceH);

			// The extent of that height, folded to a single texel. Every brick/grout
			// decision in the filter is taken on the height remapped through this
			// pair rather than on the composited value itself.
			//
			// Without it Grout Level is an absolute threshold on a target that is
			// cleared to 0.5, so at its own default it sits exactly on the clear
			// value, BrickMask comes out identically zero across the image, and the
			// filter -- every term of which is multiplied by that mask -- returns its
			// input unchanged. That is the whole reason chipping showed nothing.
			//
			// Folded on the GPU and consumed as a texture rather than read back:
			// this runs inside the same graph as the passes that use it, and a
			// readback here would stall the frame to move eight bytes.
			FRDGTextureRef ChipHeightRange = nullptr;
			{
				const FRDGTextureDesc RangeDesc1x1 = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_A32B32G32R32F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);

				FIntPoint ReduceSize = Request.Resolution;
				FRDGTextureRef ReduceSource = nullptr;
				int32 ReducePass = 0;
				while (ReduceSource == nullptr || ReduceSize != FIntPoint(1, 1))
				{
					const FIntPoint NextSize(
						FMath::DivideAndRoundUp(ReduceSize.X, GMixtormatReduceFactor),
						FMath::DivideAndRoundUp(ReduceSize.Y, GMixtormatReduceFactor));

					const FRDGTextureDesc StepDesc = FRDGTextureDesc::Create2D(
						NextSize,
						PF_A32B32G32R32F,
						FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef StepTarget = GraphBuilder.CreateTexture(
						StepDesc, TEXT("Mixtormat.ChipHeightRange"));

					FMixtormatReduceMinMaxCS::FParameters* RP =
						GraphBuilder.AllocParameters<FMixtormatReduceMinMaxCS::FParameters>();
					RP->InputSize = ReduceSize;
					RP->OutputSize = NextSize;
					RP->FirstPass = ReducePass == 0 ? 1 : 0;
					RP->SourceHeight = ChipSourceH;

					// Bound on every pass because the struct requires it and unread on
					// the first, where the chain has produced nothing yet. Aiming it at
					// the height would bind an R16F single-channel texture to a float4
					// slot; the 1x1 is the cheapest thing of the right shape, and RDG
					// rejects a transient nothing has written, so it is cleared.
					if (ReducePass == 0)
					{
						FRDGTextureRef RangeDummy = GraphBuilder.CreateTexture(
							RangeDesc1x1, TEXT("Mixtormat.ChipRangeDummy"));
						AddClearUAVPass(
							GraphBuilder,
							GraphBuilder.CreateUAV(RangeDummy),
							FVector4f(0.0f));
						RP->SourceRange = RangeDummy;
					}
					else
					{
						RP->SourceRange = ReduceSource;
					}
					RP->OutputRange = GraphBuilder.CreateUAV(StepTarget);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Chipping.L%d.HeightRange%d", LayerIndex, ReducePass),
						ReduceMinMaxShader,
						RP,
						FIntVector(
							FMath::DivideAndRoundUp(NextSize.X, 8),
							FMath::DivideAndRoundUp(NextSize.Y, 8),
							1));

					ReduceSource = StepTarget;
					ReduceSize = NextSize;
					++ReducePass;
				}
				ChipHeightRange = ReduceSource;
			}

			// Scratch for the normal pass, which reads the composited normal and
			// writes the same target.
			FRDGTextureRef ChipNormalScratch = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.ChipNormalScratch"));

			AddClearUAVPass(
				GraphBuilder, GraphBuilder.CreateUAV(ChipState[0]), FVector4f(0.0f));
			AddClearUAVPass(
				GraphBuilder, GraphBuilder.CreateUAV(ChipState[1]), FVector4f(0.0f));
			AddClearUAVPass(
				GraphBuilder, GraphBuilder.CreateUAV(ChipMask[0]), FVector4f(0.0f));
			AddClearUAVPass(
				GraphBuilder, GraphBuilder.CreateUAV(ChipMask[1]), FVector4f(0.0f));

			const FIntVector ChipGroups(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1);

			auto FillChipParameters = [&](FMixtormatChippingCS::FParameters* P)
			{
				P->OutputSize = Request.Resolution;
				P->GroutLevel = Chip.ChipGroutLevel;
				P->GroutSoftness = Chip.ChipGroutSoftness;
				P->ChipAmount = Chip.ChipAmount;
				P->ChipSize = Chip.ChipSize;
				P->ChipDepth = Chip.ChipDepth;
				P->Irregularity = Chip.ChipIrregularity;
				P->MaskEdge = Chip.ChipMaskEdge;
				P->NormalStrength = Chip.ChipNormalStrength;
				P->CavityInfluence = Chip.ChipCavityInfluence;
				P->CavityOffset = Chip.ChipCavityOffset;
				P->CavityRemapMin = Chip.ChipCavityRemapMin;
				P->CavityRemapMax = Chip.ChipCavityRemapMax;
				P->HeightInfluence = Chip.ChipHeightInfluence;
				P->HeightScale = Chip.ChipHeightScale;
				P->UsePlacementMask = bUseLegacyPlacementMask ? 1u : 0u;
				P->PlacementMaskTiling = Chip.ChipMaskTiling;
				P->InvertMask =
					!PendingChipping.bHasScopedMask && Chip.bChipInvertMask ? 1u : 0u;
				P->Seed = Chip.ChipSeed;
				P->SourceHeight = ChipSourceH;
				P->HeightRange = ChipHeightRange;
				P->LayerMask = PendingChipping.FeatureMask;
				P->PlacementMaskTexture = ChippingPlacementMask;
				P->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			};

			int32 ChipWrite = 0;
			for (int32 PassIndex = 0; PassIndex < ChipIterations; ++PassIndex)
			{
				ChipWrite = PassIndex & 1;
				const int32 ChipRead = 1 - ChipWrite;

				FMixtormatChippingCS::FParameters* CP =
					GraphBuilder.AllocParameters<FMixtormatChippingCS::FParameters>();
				FillChipParameters(CP);
				CP->Iteration = PassIndex;
				CP->NormalPass = 0;
				CP->PreviousState = ChipState[ChipRead];
				CP->ChipsTexture = ChipMask[ChipRead];
				CP->PreviousNormal = OutputN[WriteIndex];
				CP->OutputState = GraphBuilder.CreateUAV(ChipState[ChipWrite]);
				CP->OutputChips = GraphBuilder.CreateUAV(ChipMask[ChipWrite]);
				CP->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
				CP->OutputNormal = GraphBuilder.CreateUAV(ChipNormalScratch);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Chipping.L%d.P%d", LayerIndex, PassIndex),
					ChippingShader,
					CP,
					ChipGroups);
			}

			FRDGTextureRef FinalChips = ChipMask[ChipWrite];
			FRDGTextureRef SpareChips = ChipMask[1 - ChipWrite];

			// Height is authoritative. Derive the chip normal from the final
			// height delta instead of maintaining a parallel chip-normal solve.
			FRDGTextureRef ChipRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Chipping.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				ChipSourceH,
				HeightTargets[WriteIndex],
				OutputN[WriteIndex],
				OutputRAM[WriteIndex],
				ChipNormalScratch,
				ChipRAM,
				Request.Resolution,
				Chip.ChipNormalStrength,
				0.35f,
				TEXT("Chipping"));
			AddCopyTexturePass(GraphBuilder, ChipNormalScratch, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ChipRAM, OutputRAM[WriteIndex]);

			// Roughness is weighted by the resolved chip mask directly, so it remains
			// independent of chip depth and never touches base colour.
			if (Chip.ChipRoughnessAmount != 0.0f)
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.ChipShadeRAM"));

				FMixtormatCarveShadeCS::FParameters* SP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				SP->OutputSize = Request.Resolution;
				SP->RoughnessAmount = Chip.ChipRoughnessAmount;

				// The chip mask is already normalized coverage, so no depth divisor is used.
				SP->CarveDepth = 1.0f;
				SP->UseCoverageTexture = 1;
				SP->CoverageTexture = FinalChips;

				// Required by the erosion path, unread when coverage comes from a texture.
				SP->SourceHeight = ChipSourceH;
				SP->CarvedHeight = ChipSourceH;

				SP->SourceRAM = OutputRAM[WriteIndex];
				SP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Chipping.L%d.Roughness", LayerIndex),
					CarveShadeShader,
					SP,
					ChipGroups);

				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}
		}
	}

	// Grade runs last, over the finished weathered surface.
	void AddGradePasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputBC = Ctx.OutputBC;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;
		TShaderMapRef<FMixtormatGradeCS> GradeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Grade runs after erosion and chipping, so it grades the final weathered
		// surface rather than the one either filter was about to change.
		// That is the order the panel lists them in and the order a grade wants:
		// last, over the finished result. Stain is no longer in this list at all --
		// it resolves a mask inside the child loop, so the layer it masks has already
		// composited by the time a grade runs.
		for (int32 GradeIndex = 0; GradeIndex < PendingGrades.Num(); ++GradeIndex)
		{
			const FPendingEffect& PendingGrade = PendingGrades[GradeIndex];
			const FEffectRenderData& Grade = *PendingGrade.Effect;

			// The shader states its own Filter contract: at Amount 0 it returns
			// exactly what it read. Honour it here rather than paying a
			// full-resolution pass and a full-resolution copy to reproduce the input.
			if (Grade.GradeAmount == 0.0f)
			{
				continue;
			}

			// Through scratch and back, for the same reason the erosion shade pass
			// is: one texture cannot be SRV and UAV in the same dispatch. Stacked
			// grades chain through it, each reading what the last wrote.
			FRDGTextureRef GradedBC = GraphBuilder.CreateTexture(
				OutputBC[WriteIndex]->Desc, TEXT("Mixtormat.GradeBC"));

			FMixtormatGradeCS::FParameters* GP =
				GraphBuilder.AllocParameters<FMixtormatGradeCS::FParameters>();
			GP->OutputSize = Request.Resolution;
			GP->HasMask = (Layer.bHasMask || PendingGrade.bHasScopedMask) ? 1u : 0u;
			GP->InvertMask =
				!PendingGrade.bHasScopedMask && Grade.bGradeInvertMask ? 1u : 0u;
			GP->TonemapMode = Grade.GradeTonemap;
			GP->TonemapStrength = Grade.GradeTonemapStrength;
			GP->Brightness = Grade.GradeBrightness;
			GP->Contrast = Grade.GradeContrast;
			GP->ContrastPivot = Grade.GradeContrastPivot;
			GP->Gamma = Grade.GradeGamma;
			GP->Amount = Grade.GradeAmount;
			GP->InputMin = Grade.GradeInputMin;
			GP->InputMax = Grade.GradeInputMax;
			GP->OutputMin = Grade.GradeOutputMin;
			GP->OutputMax = Grade.GradeOutputMax;
			GP->ChannelBias = Grade.GradeChannelBias;
			GP->SourceColor = OutputBC[WriteIndex];

			// The layer's own accumulated child mask, which is what makes this an
			// adjustment layer rather than a whole-surface grade.
			GP->LayerMask = PendingGrade.FeatureMask;
			GP->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			GP->OutputColor = GraphBuilder.CreateUAV(GradedBC);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Grade.Layer%d.%d", LayerIndex, GradeIndex),
				GradeShader,
				GP,
				FIntVector(
					FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
					1));

			AddCopyTexturePass(GraphBuilder, GradedBC, OutputBC[WriteIndex]);
		}
	}

}
