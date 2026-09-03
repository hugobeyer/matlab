#include "MixtormatGpuCompositor.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FMixtormatCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, Enabled)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, HasEffects)
		SHADER_PARAMETER(uint32, HasStain)
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(uint32, OverrideRoughness)
		SHADER_PARAMETER(uint32, OverrideMetallic)
		SHADER_PARAMETER(uint32, CompositionMode)
		SHADER_PARAMETER(uint32, IsFill)
		SHADER_PARAMETER(uint32, HasSurface)
		SHADER_PARAMETER(uint32, HasPackedHeight)
		SHADER_PARAMETER(uint32, HasNormal)
		SHADER_PARAMETER(uint32, NormalOnly)
		SHADER_PARAMETER(uint32, OverrideNormal)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, HeightBlendEnabled)
		SHADER_PARAMETER(uint32, HeightSource)
		SHADER_PARAMETER(uint32, InvertHeight)
		SHADER_PARAMETER(uint32, DirectHeightComparison)
		SHADER_PARAMETER(uint32, InvertHeightFeature)
		SHADER_PARAMETER(uint32, InvertAOFeature)
		SHADER_PARAMETER(uint32, InvertFeature)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(float, Opacity)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, NormalIntensity)
		SHADER_PARAMETER(float, HueShift)
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(float, Value)
		SHADER_PARAMETER(float, RoughnessBias)
		SHADER_PARAMETER(float, RoughnessContrast)
		SHADER_PARAMETER(float, RoughnessOffset)
		SHADER_PARAMETER(float, FillRoughness)
		SHADER_PARAMETER(float, FillMetallic)
		SHADER_PARAMETER(float, LayerF0)
		SHADER_PARAMETER(float, BaseColorInfluence)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, AOInfluence)
		SHADER_PARAMETER(float, MetallicInfluence)
		SHADER_PARAMETER(float, F0Influence)
		SHADER_PARAMETER(float, NormalInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightBlendAmount)
		SHADER_PARAMETER(float, HeightThreshold)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER(float, HeightOffset)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, ConstantHeight)
		SHADER_PARAMETER(float, MaskHeightInfluence)
		SHADER_PARAMETER(float, HeightContactAOAmount)
		SHADER_PARAMETER(float, HeightContactAOWidth)
		SHADER_PARAMETER(float, HeightBorderLift)
		SHADER_PARAMETER(float, HeightBorderWidth)
		SHADER_PARAMETER(float, HeightBorderNormalStrength)
		SHADER_PARAMETER(float, FeatureInfluence)
		SHADER_PARAMETER(float, FeatureBias)
		SHADER_PARAMETER(float, HeightFeatureInfluence)
		SHADER_PARAMETER(float, AOFeatureInfluence)
		SHADER_PARAMETER(int32, CurvatureRadius)
		SHADER_PARAMETER(float, CurvatureStrength)
		SHADER_PARAMETER(float, CurvaturePower)
		SHADER_PARAMETER(int32, CurvatureSmoothing)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ReferenceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, EffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, EffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, StainData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DebugMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCompositeCS,
	"/Plugin/MaterialLab/Private/MixtormatComposite.usf",
	"MainCS",
	SF_Compute);

class FMixtormatMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, IncomingMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatMaskCS,
	"/Plugin/MaterialLab/Private/MixtormatMask.usf",
	"MainCS",
	SF_Compute);

class FMixtormatGeneratedMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratedMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratedMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(float, CurvatureWeight)
		SHADER_PARAMETER(float, CurvatureBias)
		SHADER_PARAMETER(float, CurvatureStrength)
		SHADER_PARAMETER(float, CurvaturePower)
		SHADER_PARAMETER(float, DirectionWeight)
		SHADER_PARAMETER(float, DirectionAngle)
		SHADER_PARAMETER(float, DirectionBroadness)
		SHADER_PARAMETER(float, AOWeight)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(uint32, NormalizeWeights)
		SHADER_PARAMETER(int32, Broadness)
		SHADER_PARAMETER(int32, Smoothing)
		SHADER_PARAMETER(float, Bias)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, WarpSource)
		SHADER_PARAMETER(int32, WarpRadius)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatGeneratedMaskCS,
	"/Plugin/MaterialLab/Private/MixtormatGeneratedMask.usf",
	"MainCS",
	SF_Compute);

class FMixtormatErosionCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatErosionCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatErosionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Pass)
		SHADER_PARAMETER(int32, NormalPass)
		SHADER_PARAMETER(int32, BlurPass)
		SHADER_PARAMETER(int32, ResamplePass)
		SHADER_PARAMETER(float, BlurRadius)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, GullyLength)
		SHADER_PARAMETER(int32, LicSteps)
		SHADER_PARAMETER(float, Repose)
		SHADER_PARAMETER(float, ReposeSoftness)
		SHADER_PARAMETER(float, CavityBias)
		SHADER_PARAMETER(float, CavityScale)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(float, GullyWeight)
		SHADER_PARAMETER(float, BlendSoftness)
		SHADER_PARAMETER(float, Gain)
		SHADER_PARAMETER(float, DerivScale)
		SHADER_PARAMETER(int32, DerivMin)
		SHADER_PARAMETER(int32, DirectionMode)
		SHADER_PARAMETER(float, DirectionAngle)
		SHADER_PARAMETER(float, DirectionAmount)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideHeight)
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
	"/Plugin/MaterialLab/Private/MixtormatErosion.usf",
	"MainCS",
	SF_Compute);

class FMixtormatPeelingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPeelingCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPeelingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Front)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, MacroWarp)
		SHADER_PARAMETER(float, MicroWarp)
		SHADER_PARAMETER(float, MicroMorph)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Lift)
		SHADER_PARAMETER(float, DetailStrength)
		SHADER_PARAMETER(float, DistanceRange)
		SHADER_PARAMETER(float, SDFRange)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelSDF)
		SHADER_PARAMETER(uint32, ProceduralSource)
		SHADER_PARAMETER(float, ProceduralAOStrength)
		SHADER_PARAMETER(float, HeightAmount)
		SHADER_PARAMETER(float, HeightInvert)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelFieldA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelFieldB)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousEffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputEffectData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEffectHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPeelingCS,
	"/Plugin/MaterialLab/Private/MixtormatPeeling.usf",
	"MainCS",
	SF_Compute);

// Grows a peel front across the surface, replacing the authored PDM/MSK/H/SDF set.
// Seed and Solve run at reduced resolution; Resolve runs full and filters arrival up.
class FMixtormatPeelFieldCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPeelFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPeelFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, MaskWeight)
		SHADER_PARAMETER(float, PeelMaskTiling)
		SHADER_PARAMETER(uint32, PeelMaskInvert)
		SHADER_PARAMETER(uint32, UseOwnMask)
		SHADER_PARAMETER(float, SeedThreshold)
		SHADER_PARAMETER(int32, CurvatureRadius)
		SHADER_PARAMETER(int32, CurvatureSmoothing)
		SHADER_PARAMETER(float, CurvatureWeight)
		SHADER_PARAMETER(float, CurvatureBias)
		SHADER_PARAMETER(float, AOWeight)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(uint32, NormalizeWeights)
		SHADER_PARAMETER(float, GrowthStrength)
		SHADER_PARAMETER(float, SizeVariation)
		SHADER_PARAMETER(int32, FlakeCells)
		SHADER_PARAMETER(int32, PeelType)
		SHADER_PARAMETER(float, Front)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, MacroWarp)
		SHADER_PARAMETER(float, MicroWarp)
		SHADER_PARAMETER(float, MicroMorph)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Lift)
		SHADER_PARAMETER(float, DetailStrength)
		SHADER_PARAMETER(float, LiftVariation)
		SHADER_PARAMETER(float, EdgeSharpness)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelOwnMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreviousArrival)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GrowthField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputArrival)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGrowth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputFieldA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputFieldB)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPeelFieldCS,
	"/Plugin/MaterialLab/Private/MixtormatPeelField.usf",
	"MainCS",
	SF_Compute);

class FMixtormatStainCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStainCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStainCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(FVector4f, StainColor)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightWarp)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousStainData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AccumulatedHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputStainData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStainCS,
	"/Plugin/MaterialLab/Private/MixtormatStain.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	struct FMaskRenderData
	{
		FTextureRHIRef Texture;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		float Tiling = 1.0f;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		bool bInvert = false;
	};

	struct FEffectRenderData
	{
		EMixtormatEffectType Type = EMixtormatEffectType::Peeling;
		FTextureRHIRef PeelData;
		FTextureRHIRef Mask;
		FTextureRHIRef Height;
		FTextureRHIRef SDF;
		float Tiling = 1.0f;
		float Strength = 1.0f;
		float Front = 0.08f;
		float Width = 0.015f;
		float MacroWarp = 0.01f;
		float MicroWarp = 0.003f;
		float MicroMorph = 1.0f;
		float Thickness = 0.04f;
		float Lift = 0.04f;
		float DetailStrength = 0.02f;
		float DistanceRange = 1.0f;
		float SDFRange = 0.1f;
		float HeightRange = 0.1f;
		FLinearColor StainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);
		float StainRoughness = 0.2f;
		float StainHeightInfluence = 0.5f;
		float StainHeightWarp = 0.0f;
		float StainHeightBias = -1.0f;
		float StainHeightContrast = 1.0f;
		// Procedural peeling. bProceduralPeel selects the generated field over the
		// authored maps; the shaping values above are shared by both paths.
		bool bProceduralPeel = false;
		int32 PeelType = 0;
		int32 PeelMacroPeriod = 8;
		int32 PeelMicroPeriod = 32;
		uint32 PeelRandomSeed = 1;
		float PeelSeedThreshold = 0.62f;
		float PeelSeedNoiseWeight = 1.0f;
		float PeelSeedCurvatureWeight = 0.0f;
		float PeelSeedCurvatureBias = 1.0f;
		float PeelSeedAOWeight = 0.0f;
		float PeelSeedHeightWeight = 0.0f;
		float PeelSeedMaskWeight = 0.0f;
		bool bPeelNormalizeSeedWeights = true;
		int32 PeelCurvatureRadius = 2;
		float PeelGrowthStrength = 1.0f;
		float PeelAOStrength = 0.8f;
		float PeelEdgeSharpness = 1.0f;
		float PeelLiftVariation = 0.6f;
		float PeelSizeVariation = 0.5f;
		int32 PeelClusterPeriod = 4;
		int32 PeelSolveDivisor = 4;
		FTextureRHIRef PeelOwnMask;
		float PeelMaskTiling = 1.0f;
		bool bPeelMaskInvert = false;
		float PeelClusterAmount = 0.35f;
		int32 PeelWarpPeriod = 16;
		float PeelWarpAmount = 0.0f;
		float PeelWarpSource = 0.0f;
		float PeelHeightAmount = 1.0f;
		bool bPeelHeightInvert = false;

		float ErosionAmount = 1.0f;
		float ErosionRepose = 0.30f;
		float ErosionReposeSoftness = 0.25f;
		float ErosionNormalStrength = 8.0f;
		int32 ErosionSlopeRadius = 2;
		float ErosionSlopeBlur = 2.0f;
		float ErosionCavityBias = 0.0f;
		float ErosionCavityScale = 1.0f;
		float ErosionHeightInfluence = 0.0f;
		float ErosionHeightScale = 1.0f;
		float ErosionGullyWeight = 2.0f;
		float ErosionBlendSoftness = 0.0f;
		int32 ErosionDirectionMode = 0;
		float ErosionDirectionAngle = 90.0f;
		float ErosionDirectionAmount = 0.0f;
	};

	struct FGeneratedMaskRenderData
	{
		float CurvatureWeight = 0.0f;
		float CurvatureBias = 0.0f;
		float CurvatureStrength = 4.0f;
		float CurvaturePower = 1.0f;
		float DirectionWeight = 0.0f;
		float DirectionAngle = 90.0f;
		float DirectionBroadness = 1.0f;
		float AOWeight = 0.0f;
		float HeightWeight = 0.0f;
		float HeightBias = 0.0f;
		bool bNormalizeWeights = true;
		int32 Broadness = 2;
		int32 Smoothing = 2;
		float Bias = 0.5f;
		float WarpAmount = 0.0f;
		float WarpSource = 0.0f;
		int32 WarpRadius = 1;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Multiply;
		float Weight = 1.0f;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		bool bInvert = false;
	};

	struct FChildRenderData
	{
		EMixtormatLayerChildType Type = EMixtormatLayerChildType::Mask;
		int32 SourceChildIndex = INDEX_NONE;
		FMaskRenderData Mask;
		FEffectRenderData Effect;
		FGeneratedMaskRenderData Generated;
	};

	struct FLayerRenderData
	{
		FTextureRHIRef BaseColor;
		FTextureRHIRef Normal;
		FTextureRHIRef RAM;
		FTextureRHIRef Mask;
		TArray<FChildRenderData> Children;
		FVector4f FillColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		float Opacity = 1.0f;
		float Tiling = 1.0f;
		float NormalIntensity = 1.0f;
		float HueShift = 0.0f;
		float Saturation = 1.0f;
		float Value = 1.0f;
		float RoughnessBias = 0.5f;
		float RoughnessContrast = 1.0f;
		float RoughnessOffset = 0.0f;
		float FillRoughness = 0.5f;
		float FillMetallic = 0.0f;
		float LayerF0 = 0.04f;
		float BaseColorInfluence = 1.0f;
		float RoughnessInfluence = 1.0f;
		float AOInfluence = 1.0f;
		float MetallicInfluence = 1.0f;
		float F0Influence = 1.0f;
		float NormalInfluence = 1.0f;
		float HeightInfluence = 1.0f;
		float HeightBlendAmount = 1.0f;
		float HeightThreshold = 0.5f;
		float HeightRange = 0.1f;
		float HeightContrast = 1.0f;
		float HeightOffset = 0.0f;
		float HeightBias = 0.0f;
		float ConstantHeight = 0.5f;
		float MaskHeightInfluence = 0.0f;
		float HeightContactAOAmount = 0.0f;
		float HeightContactAOWidth = 0.05f;
		float HeightBorderLift = 0.0f;
		float HeightBorderWidth = 0.05f;
		float HeightBorderNormalStrength = 1.0f;
		float FeatureInfluence = 0.0f;
		float FeatureBias = 0.0f;
		float HeightFeatureInfluence = 0.0f;
		float AOFeatureInfluence = 0.0f;
		float CurvatureStrength = 1.0f;
		float CurvaturePower = 1.0f;
		int32 CurvatureSmoothing = 2;
		int32 CurvatureRadius = 1;
		bool bEnabled = true;
		bool bHasMask = false;
		bool bHasEffects = false;
		bool bHasStain = false;
		bool bOverrideBaseColor = false;
		bool bOverrideRoughness = false;
		bool bOverrideMetallic = false;
		bool bCoat = false;
		bool bFill = false;
		bool bHasSurface = false;
		bool bHasNormal = false;
		bool bNormalOnly = false;
		bool bOverrideNormal = false;
		bool bFlipNormalY = false;
		bool bHeightBlendEnabled = false;
		bool bHasPackedHeight = false;
		bool bInvertHeight = false;
		bool bDirectHeightComparison = false;
		bool bInvertHeightFeature = false;
		bool bInvertAOFeature = false;
		bool bInvertFeature = false;
		uint32 HeightSource = 2u;
		int32 HeightReferenceLayerIndex = INDEX_NONE;
	};

	struct FRenderRequest
	{
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TArray<FLayerRenderData> Layers;
		FTextureRHIRef OutputBC[2];
		FTextureRHIRef OutputN[2];
		FTextureRHIRef OutputRAM[2];
		FTextureRHIRef OutputHeight[2];
		FTextureRHIRef OutputDebug[2];
		FMixtormatDebugPreviewSettings DebugSettings;
		FSimpleDelegate OnComplete;
		int32 PublishedTargetIndex = 0;
	};

	FTextureRHIRef GetTextureRHI(UTexture2D* Texture)
	{
		return Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
	}

	UTextureRenderTarget2D* CreateTarget(
		const FIntPoint Resolution,
		const FLinearColor ClearColor,
		const EPixelFormat Format = PF_FloatRGBA)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->ClearColor = ClearColor;
		Target->bCanCreateUAV = true;
		Target->bAutoGenerateMips = false;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->InitCustomFormat(Resolution.X, Resolution.Y, Format, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	FTextureRHIRef GetTargetRHI(UTextureRenderTarget2D* Target)
	{
		return Target && Target->GameThread_GetRenderTargetResource()
			? Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture()
			: FTextureRHIRef();
	}

	FRDGTextureRef RegisterTexture(
		FRDGBuilder& GraphBuilder,
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures,
		const FTextureRHIRef& Texture,
		const TCHAR* Name)
	{
		FRHITexture* TextureRHI = Texture.GetReference();
		check(TextureRHI);
		if (const FRDGTextureRef* ExistingTexture = RegisteredTextures.Find(TextureRHI))
		{
			return *ExistingTexture;
		}

		FRDGTextureRef RegisteredTexture =
			GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, Name));
		RegisteredTextures.Add(TextureRHI, RegisteredTexture);
		return RegisteredTexture;
	}
}

bool FMixtormatGpuCompositor::Initialize(const FIntPoint InResolution)
{
	using namespace MixtormatGpuCompositor;
	if (InResolution.X <= 0 || InResolution.Y <= 0)
	{
		return false;
	}
	if (bInitialized && Resolution == InResolution)
	{
		return true;
	}

	Resolution = InResolution;
	for (FTargetSet& Set : Targets)
	{
		Set.BaseColor.Reset(CreateTarget(Resolution, FLinearColor::Black));
		Set.Normal.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.5f, 1.0f, 1.0f)));
		Set.RAM.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 1.0f, 0.0f, 0.04f)));
		Set.Height.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.0f, 0.0f, 0.0f), PF_R16F));
		Set.Debug.Reset(CreateTarget(Resolution, FLinearColor(0.08f, 0.02f, 0.12f, 1.0f)));
	}
	FlushRenderingCommands();
	PublishedTargetIndex = 0;
	bInitialized = true;
	return true;
}

bool FMixtormatGpuCompositor::RequestCompose(
	const TArray<FMixtormatLayer>& Layers,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings)
{
	using namespace MixtormatGpuCompositor;
	check(IsInGameThread());
	if (!bInitialized && !Initialize())
	{
		return false;
	}

	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture2D* NormalTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	if (!WhiteTexture || !NormalTexture)
	{
		return false;
	}

	FRenderRequest Request;
	Request.Resolution = Resolution;
	Request.DebugSettings = DebugSettings;
	Request.OnComplete = MoveTemp(OnComplete);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Request.OutputBC[Index] = GetTargetRHI(Targets[Index].BaseColor.Get());
		Request.OutputN[Index] = GetTargetRHI(Targets[Index].Normal.Get());
		Request.OutputRAM[Index] = GetTargetRHI(Targets[Index].RAM.Get());
		Request.OutputHeight[Index] = GetTargetRHI(Targets[Index].Height.Get());
		Request.OutputDebug[Index] = GetTargetRHI(Targets[Index].Debug.Get());
		if (!Request.OutputBC[Index].IsValid()
			|| !Request.OutputN[Index].IsValid()
			|| !Request.OutputRAM[Index].IsValid()
			|| !Request.OutputHeight[Index].IsValid()
			|| !Request.OutputDebug[Index].IsValid())
		{
			return false;
		}
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const FMixtormatLayer& Layer = Layers[LayerIndex];
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous();
		const bool bNormalOnly = Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail;
		UTexture2D* LayerBaseColor = Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture;
		UTexture2D* LayerNormal = Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture;
		if (bNormalOnly && Layer.NormalSourceType == EMixtormatNormalSourceType::Texture)
		{
			LayerNormal = Layer.NormalTexture.LoadSynchronous();
		}
		UTexture2D* LayerRAM = Surface && Surface->RoughnessAOMetallic
			? Surface->RoughnessAOMetallic.Get()
			: WhiteTexture;

		Data.BaseColor = GetTextureRHI(LayerBaseColor);
		Data.Normal = GetTextureRHI(LayerNormal ? LayerNormal : NormalTexture);
		Data.RAM = GetTextureRHI(LayerRAM);
		Data.Mask = GetTextureRHI(WhiteTexture);
		if (!Data.BaseColor.IsValid()
			|| !Data.Normal.IsValid()
			|| !Data.RAM.IsValid()
			|| !Data.Mask.IsValid())
		{
			return false;
		}
		Data.FillColor = FVector4f(
			Layer.BaseColor.R,
			Layer.BaseColor.G,
			Layer.BaseColor.B,
			Layer.BaseColor.A);
		for (int32 SourceChildIndex = 0; SourceChildIndex < Layer.Children.Num(); ++SourceChildIndex)
		{
			const FMixtormatLayerChild& LayerChild = Layer.Children[SourceChildIndex];
			if (LayerChild.Type == EMixtormatLayerChildType::Mask)
			{
				const FMixtormatMaskLayer& MaskLayer = LayerChild.Mask;
				if (!MaskLayer.bEnabled)
				{
					continue;
				}

				UTexture2D* MaskTexture = MaskLayer.MaskTexture.LoadSynchronous();
				if (!MaskTexture)
				{
					if (const UMixtormatMask* MaskAsset = MaskLayer.Mask.LoadSynchronous())
					{
						MaskTexture = MaskAsset->MaskTexture.Get();
					}
				}
				if (!MaskTexture)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Mask;
				ChildData.SourceChildIndex = SourceChildIndex;
				FMaskRenderData& MaskData = ChildData.Mask;
				MaskData.Texture = GetTextureRHI(MaskTexture);
				if (!MaskData.Texture.IsValid())
				{
					return false;
				}
				MaskData.BlendMode = MaskLayer.BlendMode;
				MaskData.Weight = FMath::Clamp(MaskLayer.Weight, 0.0f, 1.0f);
				MaskData.Tiling = FMath::Max(1.0f, static_cast<float>(MaskLayer.Tiling));
				MaskData.Balance = FMath::Clamp(MaskLayer.Balance, 0.0f, 2.0f);
				MaskData.Contrast = FMath::Clamp(MaskLayer.Contrast, 0.0f, 10.0f);
				MaskData.Offset = FMath::Clamp(MaskLayer.Offset, -1.0f, 1.0f);
				MaskData.bInvert = MaskLayer.bInvert;
				Data.bHasMask = true;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Generated)
			{
				const FMixtormatGeneratedMask& GeneratedMask = LayerChild.Generated;
				if (!GeneratedMask.bEnabled || !GeneratedMask.HasAnySignal())
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Generated;
				ChildData.SourceChildIndex = SourceChildIndex;
				FGeneratedMaskRenderData& GeneratedData = ChildData.Generated;
				GeneratedData.CurvatureWeight = GeneratedMask.CurvatureWeight;
				GeneratedData.CurvatureBias = FMath::Clamp(GeneratedMask.CurvatureBias, 0.0f, 1.0f);
				GeneratedData.CurvatureStrength = FMath::Max(GeneratedMask.CurvatureStrength, 0.0f);
				GeneratedData.CurvaturePower = FMath::Max(GeneratedMask.CurvaturePower, 0.001f);
				GeneratedData.DirectionWeight = GeneratedMask.DirectionWeight;
				GeneratedData.DirectionAngle = GeneratedMask.DirectionAngle;
				GeneratedData.DirectionBroadness = FMath::Max(GeneratedMask.DirectionBroadness, 0.001f);
				GeneratedData.AOWeight = GeneratedMask.AOWeight;
				GeneratedData.HeightWeight = GeneratedMask.HeightWeight;
				GeneratedData.HeightBias = GeneratedMask.HeightBias;
				GeneratedData.bNormalizeWeights = GeneratedMask.bNormalizeWeights;
				GeneratedData.Broadness = FMath::Clamp(GeneratedMask.Broadness, 1, 32);
				GeneratedData.Smoothing = FMath::Clamp(GeneratedMask.Smoothing, 1, 4);
				GeneratedData.Bias = FMath::Clamp(GeneratedMask.Bias, 0.001f, 0.999f);
				GeneratedData.WarpAmount = FMath::Max(GeneratedMask.WarpAmount, 0.0f);
				GeneratedData.WarpSource = FMath::Clamp(GeneratedMask.WarpSource, 0.0f, 1.0f);
				GeneratedData.WarpRadius = FMath::Clamp(GeneratedMask.WarpRadius, 1, 16);
				GeneratedData.BlendMode = GeneratedMask.BlendMode;
				GeneratedData.Weight = FMath::Max(GeneratedMask.Weight, 0.0f);
				GeneratedData.Balance = FMath::Max(GeneratedMask.Balance, 0.0f);
				GeneratedData.Contrast = FMath::Max(GeneratedMask.Contrast, 0.0f);
				GeneratedData.Offset = GeneratedMask.Offset;
				GeneratedData.bInvert = GeneratedMask.bInvert;
				Data.bHasMask = true;
				continue;
			}

			const FMixtormatLayerEffect& LayerEffect = LayerChild.Effect;
			if (!LayerEffect.bEnabled)
			{
				continue;
			}

			const UMixtormatEffect* EffectAsset = LayerEffect.Effect.LoadSynchronous();

			// Procedural effects carry no source maps and so have no asset to read a type
			// from. Asset-backed effects are unchanged.
			const bool bProcedural =
				!EffectAsset
				&& (LayerEffect.ProceduralType == EMixtormatEffectType::Erosion
					|| LayerEffect.ProceduralType == EMixtormatEffectType::Peeling);
			if (!EffectAsset && !bProcedural)
			{
				continue;
			}
			const EMixtormatEffectType ResolvedType =
				EffectAsset ? EffectAsset->EffectType : LayerEffect.ProceduralType;
			if (ResolvedType == EMixtormatEffectType::Peeling && EffectAsset
				&& (!EffectAsset->PeelData
					|| !EffectAsset->Mask
					|| !EffectAsset->Height
					|| !EffectAsset->SDF))
			{
				continue;
			}

			FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
			ChildData.Type = EMixtormatLayerChildType::Effect;
			ChildData.SourceChildIndex = SourceChildIndex;
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = ResolvedType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			EffectData.Strength = FMath::Clamp(LayerEffect.Strength, 0.0f, 1.0f);
			if (ResolvedType == EMixtormatEffectType::Erosion)
			{
				// Values pass through unclamped. The inspector constrains the scrub range
				// visually, but a typed value outside it stays intact all the way to the
				// shader, which keeps its own epsilon guards at the division sites.
				EffectData.ErosionAmount = LayerEffect.ErosionAmount;
				EffectData.ErosionRepose = LayerEffect.ErosionRepose;
				EffectData.ErosionReposeSoftness = LayerEffect.ErosionReposeSoftness;
				EffectData.ErosionNormalStrength = LayerEffect.ErosionNormalStrength;
				EffectData.ErosionSlopeRadius = LayerEffect.ErosionSlopeRadius;
				EffectData.ErosionSlopeBlur = LayerEffect.ErosionSlopeBlur;
				EffectData.ErosionCavityBias = LayerEffect.ErosionCavityBias;
				EffectData.ErosionCavityScale = LayerEffect.ErosionCavityScale;
				EffectData.ErosionHeightInfluence = LayerEffect.ErosionHeightInfluence;
				EffectData.ErosionHeightScale = LayerEffect.ErosionHeightScale;
				EffectData.ErosionGullyWeight = LayerEffect.ErosionGullyWeight;
				EffectData.ErosionBlendSoftness = LayerEffect.ErosionBlendSoftness;
				EffectData.ErosionDirectionMode =
					LayerEffect.ErosionDirectionMode == EMixtormatErosionDirectionMode::Lerp ? 1 : 0;
				EffectData.ErosionDirectionAngle = LayerEffect.ErosionDirectionAngle;
				EffectData.ErosionDirectionAmount = LayerEffect.ErosionDirectionAmount;
			}

			// Erosion is a Filter and has nothing further to gather. bHasEffects is
			// deliberately not set for it: a Filter never writes the effect data target,
			// so flagging it would make the composite sample a buffer nothing wrote.
			if (!EffectAsset && LayerEffect.ProceduralType == EMixtormatEffectType::Erosion)
			{
				continue;
			}

			// A procedural peel does write effect data, so it is gathered here — before the
			// asset-backed branches below, all of which dereference EffectAsset.
			if (!EffectAsset)
			{
				EffectData.bProceduralPeel = true;
				EffectData.PeelType = static_cast<int32>(LayerEffect.PeelType);
				EffectData.PeelMacroPeriod = LayerEffect.PeelMacroPeriod;
				EffectData.PeelMicroPeriod = LayerEffect.PeelMicroPeriod;
				EffectData.PeelRandomSeed = static_cast<uint32>(FMath::Max(LayerEffect.PeelRandomSeed, 1));
				EffectData.PeelSeedThreshold = LayerEffect.PeelSeedThreshold;
				EffectData.PeelSeedNoiseWeight = LayerEffect.PeelSeedNoiseWeight;
				EffectData.PeelSeedCurvatureWeight = LayerEffect.PeelSeedCurvatureWeight;
				EffectData.PeelSeedCurvatureBias = LayerEffect.PeelSeedCurvatureBias;
				EffectData.PeelSeedAOWeight = LayerEffect.PeelSeedAOWeight;
				EffectData.PeelSeedHeightWeight = LayerEffect.PeelSeedHeightWeight;
				EffectData.PeelSeedMaskWeight = LayerEffect.PeelSeedMaskWeight;
				EffectData.bPeelNormalizeSeedWeights = LayerEffect.bPeelNormalizeSeedWeights;
				EffectData.PeelCurvatureRadius = LayerEffect.PeelCurvatureRadius;
				EffectData.PeelGrowthStrength = LayerEffect.PeelGrowthStrength;
				EffectData.PeelAOStrength = LayerEffect.PeelAOStrength;
				EffectData.PeelEdgeSharpness = LayerEffect.PeelEdgeSharpness;
				EffectData.PeelLiftVariation = LayerEffect.PeelLiftVariation;
				EffectData.PeelSizeVariation = LayerEffect.PeelSizeVariation;
				EffectData.PeelClusterPeriod = LayerEffect.PeelClusterPeriod;
				EffectData.PeelSolveDivisor = LayerEffect.PeelSolveDivisor;
				EffectData.PeelMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.PeelMaskTiling));
				EffectData.bPeelMaskInvert = LayerEffect.bPeelMaskInvert;
				{
					// Direct texture wins over the asset, matching how mask children resolve.
					UTexture2D* OwnMask = LayerEffect.PeelMaskTexture.LoadSynchronous();
					if (!OwnMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.PeelMask.LoadSynchronous())
						{
							OwnMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (OwnMask)
					{
						EffectData.PeelOwnMask = GetTextureRHI(OwnMask);
					}
				}
				EffectData.PeelClusterAmount = LayerEffect.PeelClusterAmount;
				EffectData.PeelWarpPeriod = LayerEffect.PeelWarpPeriod;
				EffectData.PeelWarpAmount = LayerEffect.PeelWarpAmount;
				EffectData.PeelWarpSource = LayerEffect.PeelWarpSource;
				EffectData.Front = LayerEffect.Front;
				EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
				EffectData.MacroWarp = LayerEffect.MacroWarp;
				EffectData.MicroWarp = LayerEffect.MicroWarp;
				EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
				EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
				EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
				EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
				EffectData.PeelHeightAmount = LayerEffect.PeelHeightAmount;
				EffectData.bPeelHeightInvert = LayerEffect.bPeelHeightInvert;
				// No 8-bit round trip on a transient float target, so nothing to decode.
				EffectData.DistanceRange = 1.0f;
				EffectData.SDFRange = 1.0f;
				EffectData.HeightRange = 1.0f;
				Data.bHasEffects = true;
				continue;
			}
			if (EffectAsset->EffectType == EMixtormatEffectType::Stain)
			{
				EffectData.StainColor = LayerEffect.StainColor;
				EffectData.StainRoughness = FMath::Clamp(LayerEffect.StainRoughness, -1.0f, 1.0f);
				EffectData.StainHeightInfluence = FMath::Clamp(LayerEffect.StainHeightInfluence, 0.0f, 1.0f);
				EffectData.StainHeightWarp = FMath::Clamp(LayerEffect.StainHeightWarp, 0.0f, 1.0f);
				EffectData.StainHeightBias = FMath::Clamp(LayerEffect.StainHeightBias, -1.0f, 1.0f);
				EffectData.StainHeightContrast = FMath::Max(LayerEffect.StainHeightContrast, 0.01f);
				Data.bHasStain = true;
				continue;
			}


			EffectData.PeelData = GetTextureRHI(EffectAsset->PeelData.Get());
			EffectData.Mask = GetTextureRHI(EffectAsset->Mask.Get());
			EffectData.Height = GetTextureRHI(EffectAsset->Height.Get());
			EffectData.SDF = GetTextureRHI(EffectAsset->SDF.Get());
			if (!EffectData.PeelData.IsValid()
				|| !EffectData.Mask.IsValid()
				|| !EffectData.Height.IsValid()
				|| !EffectData.SDF.IsValid())
			{
				return false;
			}
			EffectData.Front = LayerEffect.Front;
			EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
			EffectData.MacroWarp = LayerEffect.MacroWarp;
			EffectData.MicroWarp = LayerEffect.MicroWarp;
			EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
			EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
			EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
			EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
			EffectData.PeelHeightAmount = LayerEffect.PeelHeightAmount;
			EffectData.bPeelHeightInvert = LayerEffect.bPeelHeightInvert;
			EffectData.DistanceRange = FMath::Max(EffectAsset->DistanceRange, 1.0e-6f);
			EffectData.SDFRange = FMath::Max(EffectAsset->SDFRange, 1.0e-6f);
			EffectData.HeightRange = FMath::Max(EffectAsset->HeightRange, 1.0e-6f);
			Data.bHasEffects = true;
		}

		Data.Opacity = Layer.Opacity;
		Data.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
		Data.NormalIntensity = Layer.NormalIntensity;
		Data.HueShift = FMath::Clamp(Layer.HueShift, -180.0f, 180.0f);
		Data.Saturation = FMath::Clamp(Layer.Saturation, 0.0f, 2.0f);
		Data.Value = FMath::Clamp(Layer.Value, 0.0f, 2.0f);
		Data.RoughnessBias = Layer.RoughnessBias;
		Data.RoughnessContrast = Layer.RoughnessContrast;
		Data.RoughnessOffset = Layer.RoughnessOffset;
		Data.FillRoughness = Layer.Roughness;
		Data.FillMetallic = Layer.Metallic;
		const float SourceIOR = Surface ? Surface->DefaultIOR : 1.5f;
		const float LayerIOR = FMath::Max(
			1.0f,
			Layer.bOverrideIOR ? Layer.IOR : SourceIOR);
		Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
		Data.BaseColorInfluence = FMath::Clamp(Layer.BaseColorInfluence, 0.0f, 1.0f);
		Data.RoughnessInfluence = FMath::Clamp(Layer.RoughnessInfluence, 0.0f, 1.0f);
		Data.AOInfluence = FMath::Clamp(Layer.AOInfluence, 0.0f, 1.0f);
		Data.MetallicInfluence = FMath::Clamp(Layer.MetallicInfluence, 0.0f, 1.0f);
		Data.F0Influence = FMath::Clamp(Layer.F0Influence, 0.0f, 1.0f);
		Data.NormalInfluence = FMath::Clamp(Layer.NormalInfluence, 0.0f, 1.0f);
		Data.HeightInfluence = FMath::Clamp(Layer.HeightInfluence, 0.0f, 1.0f);
		Data.HeightBlendAmount = FMath::Clamp(Layer.HeightBlendAmount, 0.0f, 4.0f);
		Data.HeightThreshold = FMath::Clamp(Layer.HeightThreshold, 0.0f, 1.0f);
		Data.HeightRange = FMath::Max(Layer.HeightRange, 1.0e-6f);
		Data.HeightContrast = FMath::Max(Layer.HeightContrast, 0.01f);
		Data.HeightOffset = FMath::Clamp(Layer.HeightOffset, -1.0f, 1.0f);
		Data.HeightBias = FMath::Clamp(Layer.HeightBias, -1.0f, 1.0f);
		Data.ConstantHeight = FMath::Clamp(Layer.ConstantHeight, 0.0f, 1.0f);
		Data.MaskHeightInfluence = FMath::Clamp(Layer.MaskHeightInfluence, 0.0f, 1.0f);
		Data.HeightContactAOAmount = FMath::Clamp(Layer.HeightContactAOAmount, 0.0f, 1.0f);
		Data.HeightContactAOWidth = FMath::Max(Layer.HeightContactAOWidth, 1.0e-4f);
		Data.HeightBorderLift = FMath::Clamp(Layer.HeightBorderLift, -1.0f, 1.0f);
		Data.HeightBorderWidth = FMath::Max(Layer.HeightBorderWidth, 1.0e-4f);
		Data.HeightBorderNormalStrength = FMath::Max(Layer.HeightBorderNormalStrength, 0.0f);
		Data.FeatureInfluence = FMath::Clamp(Layer.FeatureInfluence, 0.0f, 1.0f);
		Data.FeatureBias = FMath::Clamp(Layer.FeatureBias, 0.0f, 1.0f);
		Data.HeightFeatureInfluence = FMath::Clamp(Layer.HeightFeatureInfluence, 0.0f, 1.0f);
		Data.AOFeatureInfluence = FMath::Clamp(Layer.AOFeatureInfluence, 0.0f, 1.0f);
		Data.CurvatureRadius = Layer.CurvatureRadius;
		Data.CurvatureSmoothing = FMath::Clamp(Layer.CurvatureSmoothing, 1, 4);
		Data.CurvatureStrength = Layer.CurvatureStrength;
		Data.CurvaturePower = Layer.CurvaturePower;
		Data.bEnabled = Layer.bEnabled;
		Data.bHeightBlendEnabled = Layer.bHeightBlendEnabled;
		Data.bHasPackedHeight = Surface && Surface->bHasBlendHeight;
		Data.bInvertHeight = Layer.bInvertHeight;
		Data.bDirectHeightComparison = !bNormalOnly;
		Data.bInvertHeightFeature = Layer.bInvertHeightFeature;
		Data.bInvertAOFeature = Layer.bInvertAOFeature;
		Data.bInvertFeature = Layer.bInvertFeature;
		Data.HeightReferenceLayerIndex = Layer.HeightReferenceLayerIndex >= 0
			&& Layer.HeightReferenceLayerIndex < LayerIndex
			? Layer.HeightReferenceLayerIndex
			: INDEX_NONE;
		switch (Layer.HeightSource)
		{
		case EMixtormatHeightSource::Automatic:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : (Data.bHasMask ? 1u : 2u);
			break;
		case EMixtormatHeightSource::CombinedMask:
			Data.HeightSource = Data.bHasMask ? 1u : 2u;
			break;
		case EMixtormatHeightSource::Constant:
			Data.HeightSource = 2u;
			break;
		case EMixtormatHeightSource::RAMHAlpha:
		case EMixtormatHeightSource::LayerHeight:
		default:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : 2u;
			break;
		}
		Data.bOverrideBaseColor = Layer.bOverrideBaseColor;
		Data.bOverrideRoughness = Layer.bOverrideRoughness;
		Data.bOverrideMetallic = Layer.bOverrideMetallic;
		Data.bCoat = Layer.CompositionMode == EMixtormatCompositionMode::Coat;
		Data.bFill = Layer.Type == EMixtormatLayerType::Fill;
		Data.bHasSurface = Surface
			&& Surface->BaseColor
			&& Surface->Normal
			&& Surface->RoughnessAOMetallic;
		Data.bHasNormal = LayerNormal != nullptr;
		Data.bNormalOnly = bNormalOnly;
		Data.bOverrideNormal = Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override;
		Data.bFlipNormalY = Layer.bFlipNormalY;
	}

	PublishedTargetIndex = Request.Layers.IsEmpty()
		? 0
		: (Request.Layers.Num() - 1) & 1;
	Request.PublishedTargetIndex = PublishedTargetIndex;

	ENQUEUE_RENDER_COMMAND(MixtormatComposite)(
		[Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			TMap<FRHITexture*, FRDGTextureRef> RegisteredTextures;
			FRDGTextureRef OutputBC[2];
			FRDGTextureRef OutputN[2];
			FRDGTextureRef OutputRAM[2];
			FRDGTextureRef OutputHeight[2];
			FRDGTextureRef OutputDebug[2];
			for (int32 Index = 0; Index < 2; ++Index)
			{
				OutputBC[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputBC[Index],
					TEXT("Mixtormat.OutputBC"));
				OutputN[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputN[Index],
					TEXT("Mixtormat.OutputN"));
				OutputRAM[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputRAM[Index],
					TEXT("Mixtormat.OutputRAM"));
				OutputHeight[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputHeight[Index],
					TEXT("Mixtormat.OutputHeight"));
				OutputDebug[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputDebug[Index],
					TEXT("Mixtormat.OutputDebug"));
			}

			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]),
				FVector4f(0.08f, 0.02f, 0.12f, 1.0f));

			if (Request.Layers.IsEmpty())
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[0]), FVector4f(0.0f, 0.0f, 0.0f, 1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[0]), FVector4f(0.5f, 0.5f, 1.0f, 1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[0]), FVector4f(0.5f, 1.0f, 0.0f, 0.04f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputHeight[0]), FVector4f(0.5f, 0.0f, 0.0f, 0.0f));
			}
			else
			{
				const FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef MaskTargets[2] =
				{
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskA")),
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskB"))
				};
				FRDGTextureRef* HeightTargets = OutputHeight;
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[0]),
					FVector4f(0.5f, 0.0f, 0.0f, 0.0f));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[1]),
					FVector4f(0.5f, 0.0f, 0.0f, 0.0f));

				const FRDGTextureDesc EffectDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef EffectTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectB"))
				};
				// Peel relief, signed around zero so stacked peels accumulate.
				const FRDGTextureDesc EffectHeightDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef EffectHeightTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightA")),
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[1]), FVector4f(0.0f));
				FRDGTextureRef StainTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.StainA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.StainB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[0]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[1]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				TShaderMapRef<FMixtormatMaskCS> MaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatGeneratedMaskCS> GeneratedMaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatErosionCS> ErosionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatPeelingCS> PeelingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatPeelFieldCS> PeelFieldShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				// Placeholders so the peel and peel-field parameter structs always have a
				// bound resource in slots the active mode does not use. Never read, never
				// written; a 1x1 keeps them free.
				const FRDGTextureDesc TinyRGDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				const FRDGTextureDesc TinyRGBADesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef PeelNoiseDummy =
					GraphBuilder.CreateTexture(TinyRGDesc, TEXT("Mixtormat.PeelNoiseDummy"));
				FRDGTextureRef PeelFieldDummy =
					GraphBuilder.CreateTexture(TinyRGBADesc, TEXT("Mixtormat.PeelFieldDummy"));
				TShaderMapRef<FMixtormatStainCS> StainShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCompositeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TSet<int32> RequiredHeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const int32 ReferenceIndex = Request.Layers[LayerIndex].HeightReferenceLayerIndex;
					if (ReferenceIndex >= 0 && ReferenceIndex < LayerIndex)
					{
						RequiredHeightSnapshots.Add(ReferenceIndex);
					}
				}
				TMap<int32, FRDGTextureRef> HeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const FLayerRenderData& Layer = Request.Layers[LayerIndex];
					FRDGTextureRef CombinedMask = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Mask,
						TEXT("Mixtormat.WhiteMask"));
					FRDGTextureRef CombinedEffectData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.DefaultEffectData"));
					FRDGTextureRef CombinedEffectHeight = EffectHeightTargets[0];
					FRDGTextureRef CombinedStainData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.DefaultStainData"));
					FRDGTextureRef DebugMask = CombinedMask;
					// Height the owning layer composites against. An erosion filter replaces it
					// with its carved result so the reshaped height also drives the blend mask,
					// contact AO and border normals rather than only the displacement output.
					const FEffectRenderData* PendingErosion = nullptr;
					int32 MaskPassIndex = 0;
					int32 EffectPassIndex = 0;
					int32 StainPassIndex = 0;
					for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
					{
						const FChildRenderData& Child = Layer.Children[ChildIndex];
						if (Child.Type == EMixtormatLayerChildType::Generated)
						{
							// Generated masks read the surface accumulated below this layer,
							// which is the same ping-pong slot the layer composite reads.
							const int32 LayerReadIndex = 1 - (LayerIndex & 1);
							const FGeneratedMaskRenderData& Generated = Child.Generated;
							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							FMixtormatGeneratedMaskCS::FParameters* GeneratedParameters =
								GraphBuilder.AllocParameters<FMixtormatGeneratedMaskCS::FParameters>();
							GeneratedParameters->OutputSize = Request.Resolution;
							GeneratedParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							GeneratedParameters->SurfaceValid = LayerIndex > 0 ? 1u : 0u;
							GeneratedParameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
							GeneratedParameters->CurvatureWeight = Generated.CurvatureWeight;
							GeneratedParameters->CurvatureBias = Generated.CurvatureBias;
							GeneratedParameters->CurvatureStrength = Generated.CurvatureStrength;
							GeneratedParameters->CurvaturePower = Generated.CurvaturePower;
							GeneratedParameters->DirectionWeight = Generated.DirectionWeight;
							GeneratedParameters->DirectionAngle = Generated.DirectionAngle;
							GeneratedParameters->DirectionBroadness = Generated.DirectionBroadness;
							GeneratedParameters->AOWeight = Generated.AOWeight;
							GeneratedParameters->HeightWeight = Generated.HeightWeight;
							GeneratedParameters->HeightBias = Generated.HeightBias;
							GeneratedParameters->NormalizeWeights = Generated.bNormalizeWeights ? 1u : 0u;
							GeneratedParameters->Broadness = Generated.Broadness;
							GeneratedParameters->Smoothing = Generated.Smoothing;
							GeneratedParameters->Bias = Generated.Bias;
							GeneratedParameters->WarpAmount = Generated.WarpAmount;
							GeneratedParameters->WarpSource = Generated.WarpSource;
							GeneratedParameters->WarpRadius = Generated.WarpRadius;
							GeneratedParameters->BlendMode = static_cast<uint32>(Generated.BlendMode);
							GeneratedParameters->Invert = Generated.bInvert ? 1u : 0u;
							GeneratedParameters->Weight = Generated.Weight;
							GeneratedParameters->Balance = Generated.Balance;
							GeneratedParameters->Contrast = Generated.Contrast;
							GeneratedParameters->Offset = Generated.Offset;
							GeneratedParameters->PreviousMask = MaskTargets[MaskReadIndex];
							GeneratedParameters->SurfaceNormal = OutputN[LayerReadIndex];
							GeneratedParameters->SurfaceRAM = OutputRAM[LayerReadIndex];
							GeneratedParameters->SurfaceHeight = HeightTargets[LayerReadIndex];
							GeneratedParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							GeneratedParameters->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.GeneratedMask.Layer%d.Child%d", LayerIndex, ChildIndex),
								GeneratedMaskShader,
								GeneratedParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugGeneratedSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugGeneratedSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugGeneratedSnapshot);
								DebugMask = DebugGeneratedSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::Mask)
						{
							const FMaskRenderData& Mask = Child.Mask;
							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							FMixtormatMaskCS::FParameters* MaskParameters =
								GraphBuilder.AllocParameters<FMixtormatMaskCS::FParameters>();
							MaskParameters->OutputSize = Request.Resolution;
							MaskParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							MaskParameters->BlendMode = static_cast<uint32>(Mask.BlendMode);
							MaskParameters->Invert = Mask.bInvert ? 1u : 0u;
							MaskParameters->Weight = Mask.Weight;
							MaskParameters->Tiling = Mask.Tiling;
							MaskParameters->Balance = Mask.Balance;
							MaskParameters->Contrast = Mask.Contrast;
							MaskParameters->Offset = Mask.Offset;
							MaskParameters->PreviousMask = MaskTargets[MaskReadIndex];
							MaskParameters->IncomingMask = RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								Mask.Texture,
								TEXT("Mixtormat.IncomingMask"));
							MaskParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							MaskParameters->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Mask.Layer%d.Child%d", LayerIndex, ChildIndex),
								MaskShader,
								MaskParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugMaskSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugMaskSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugMaskSnapshot);
								DebugMask = DebugMaskSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						const FEffectRenderData& Effect = Child.Effect;
						if (Effect.Type == EMixtormatEffectType::Erosion)
						{
							// Erosion is a post-layer filter: it carves what this layer actually
							// composited, not the height underneath it. Running it here would let
							// the layer paint straight back over the carve.
							PendingErosion = &Effect;
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Stain)
						{
							const int32 StainWriteIndex = StainPassIndex & 1;
							const int32 StainReadIndex = 1 - StainWriteIndex;
							FMixtormatStainCS::FParameters* StainParameters =
								GraphBuilder.AllocParameters<FMixtormatStainCS::FParameters>();
							StainParameters->OutputSize = Request.Resolution;
							StainParameters->Initialize = StainPassIndex == 0 ? 1u : 0u;
							StainParameters->Strength = Effect.Strength;
							StainParameters->StainColor = FVector4f(
															Effect.StainColor.R,
															Effect.StainColor.G,
															Effect.StainColor.B,
															Effect.StainColor.A);
							StainParameters->RoughnessInfluence = Effect.StainRoughness;
							StainParameters->HeightInfluence = Effect.StainHeightInfluence;
							StainParameters->HeightWarp = Effect.StainHeightWarp;
							StainParameters->HeightBias = Effect.StainHeightBias;
							StainParameters->HeightContrast = Effect.StainHeightContrast;
							StainParameters->PreviousStainData = StainTargets[StainReadIndex];
							StainParameters->ChildMask = CombinedMask;
							StainParameters->AccumulatedHeight = HeightTargets[1 - (LayerIndex & 1)];
							StainParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							StainParameters->OutputStainData = GraphBuilder.CreateUAV(StainTargets[StainWriteIndex]);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Stain.Layer%d.Child%d", LayerIndex, ChildIndex),
								StainShader,
								StainParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedStainData = StainTargets[StainWriteIndex];
							++StainPassIndex;
							continue;
						}

						// A procedural peel builds its field first: seed, then a halving
						// chain of jump-flood steps, then one resolve into the same channel
						// layout the authored maps carry. The peel pass below is identical
						// either way apart from which source it reads.
						FRDGTextureRef PeelFieldA = PeelFieldDummy;
						FRDGTextureRef PeelFieldB = PeelFieldDummy;
						if (Effect.bProceduralPeel)
						{
							// Same accumulated state the generated mask reads: the surface
							// composited below this layer.
							const int32 PeelSurfaceIndex = 1 - (LayerIndex & 1);

							// The solve dominates cost, and halving the side both quarters
							// the texels and halves the passes the front needs to cross
							// them. Arrival is smooth enough to filter back up afterwards.
							const int32 SolveDivisor = FMath::Clamp(Effect.PeelSolveDivisor, 1, 8);
							const FIntPoint SolveRes(
								FMath::Max(Request.Resolution.X / SolveDivisor, 64),
								FMath::Max(Request.Resolution.Y / SolveDivisor, 64));

							const FRDGTextureDesc ArrivalDesc = FRDGTextureDesc::Create2D(
								SolveRes, PF_G32R32F, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);
							const FRDGTextureDesc GrowthDesc = FRDGTextureDesc::Create2D(
								SolveRes, PF_R16F, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);
							const FRDGTextureDesc FieldDesc = FRDGTextureDesc::Create2D(
								Request.Resolution, PF_FloatRGBA, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);

							FRDGTextureRef Arrival[2] = {
								GraphBuilder.CreateTexture(ArrivalDesc, TEXT("Mixtormat.PeelArrivalA")),
								GraphBuilder.CreateTexture(ArrivalDesc, TEXT("Mixtormat.PeelArrivalB"))};
							FRDGTextureRef PeelGrowth =
								GraphBuilder.CreateTexture(GrowthDesc, TEXT("Mixtormat.PeelGrowth"));
							FRDGTextureRef FieldA =
								GraphBuilder.CreateTexture(FieldDesc, TEXT("Mixtormat.PeelFieldA"));
							FRDGTextureRef FieldB =
								GraphBuilder.CreateTexture(FieldDesc, TEXT("Mixtormat.PeelFieldB"));

							auto AddPeelFieldPass = [&](
								const int32 ModeIndex,
								FRDGTextureRef InArrival,
								FRDGTextureRef OutArrival,
								const TCHAR* DebugName)
							{
								FMixtormatPeelFieldCS::FParameters* FP =
									GraphBuilder.AllocParameters<FMixtormatPeelFieldCS::FParameters>();
								FP->OutputSize = Request.Resolution;
								FP->SolveSize = SolveRes;
								FP->Mode = ModeIndex;
								FP->SurfaceValid = LayerIndex > 0 ? 1u : 0u;
								FP->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
								FP->Seed = Effect.PeelRandomSeed;
								FP->MaskWeight = Effect.PeelSeedMaskWeight;
								FP->PeelMaskTiling = Effect.PeelMaskTiling;
								FP->PeelMaskInvert = Effect.bPeelMaskInvert ? 1u : 0u;
								FP->UseOwnMask = Effect.PeelOwnMask.IsValid() ? 1u : 0u;
								FP->PeelOwnMask = Effect.PeelOwnMask.IsValid()
									? RegisterTexture(GraphBuilder, RegisteredTextures, Effect.PeelOwnMask, TEXT("Mixtormat.PeelOwnMask"))
									: PeelFieldDummy;
								FP->SeedThreshold = Effect.PeelSeedThreshold;
								FP->CurvatureRadius = Effect.PeelCurvatureRadius;
								FP->CurvatureSmoothing = 1;
								FP->CurvatureWeight = Effect.PeelSeedCurvatureWeight;
								FP->CurvatureBias = Effect.PeelSeedCurvatureBias;
								FP->AOWeight = Effect.PeelSeedAOWeight;
								FP->HeightWeight = Effect.PeelSeedHeightWeight;
								FP->NormalizeWeights = Effect.bPeelNormalizeSeedWeights ? 1u : 0u;
								FP->GrowthStrength = Effect.PeelGrowthStrength;
								FP->SizeVariation = Effect.PeelSizeVariation;
								FP->FlakeCells = Effect.PeelClusterPeriod;
								FP->PeelType = Effect.PeelType;
								FP->Front = Effect.Front;
								FP->Width = Effect.Width;
								FP->MacroWarp = Effect.MacroWarp;
								FP->MicroWarp = Effect.MicroWarp;
								FP->MicroMorph = Effect.MicroMorph;
								FP->Thickness = Effect.Thickness;
								FP->Lift = Effect.Lift;
								FP->DetailStrength = Effect.DetailStrength;
								FP->LiftVariation = Effect.PeelLiftVariation;
								FP->EdgeSharpness = Effect.PeelEdgeSharpness;
								FP->SurfaceNormal = OutputN[PeelSurfaceIndex];
								FP->SurfaceRAM = OutputRAM[PeelSurfaceIndex];
								FP->SurfaceHeight = HeightTargets[PeelSurfaceIndex];
								FP->ChildMask = CombinedMask;
								FP->PreviousArrival = InArrival;
								FP->GrowthField = PeelGrowth;
								FP->LinearWrapSampler =
									TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
								FP->OutputArrival = GraphBuilder.CreateUAV(OutArrival);
								FP->OutputGrowth = GraphBuilder.CreateUAV(PeelGrowth);
								FP->OutputFieldA = GraphBuilder.CreateUAV(FieldA);
								FP->OutputFieldB = GraphBuilder.CreateUAV(FieldB);

								const FIntPoint PassRes = ModeIndex == 2 ? Request.Resolution : SolveRes;
								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME(
										"Mixtormat.PeelField.L%d.C%d.%s",
										LayerIndex, ChildIndex, DebugName),
									PeelFieldShader,
									FP,
									FIntVector(
										FMath::DivideAndRoundUp(PassRes.X, 8),
										FMath::DivideAndRoundUp(PassRes.Y, 8),
										1));
							};

							AddPeelFieldPass(0, Arrival[1], Arrival[0], TEXT("Seed"));

							// The front only has to travel Front plus a few transition
							// widths, and advances about one texel per pass, so the count
							// is bounded by reach at the solve resolution.
							int32 Ping = 0;
							const float Reach =
								FMath::Abs(Effect.Front) + 4.0f * FMath::Abs(Effect.Width);
							const int32 Iterations = FMath::Clamp(
								FMath::CeilToInt(Reach * SolveRes.X), 1, 256);
							for (int32 Step = 0; Step < Iterations; ++Step)
							{
								AddPeelFieldPass(1, Arrival[Ping], Arrival[1 - Ping], TEXT("Solve"));
								Ping = 1 - Ping;
							}

							AddPeelFieldPass(2, Arrival[Ping], Arrival[1 - Ping], TEXT("Resolve"));

							PeelFieldA = FieldA;
							PeelFieldB = FieldB;
						}

						const int32 EffectWriteIndex = EffectPassIndex & 1;
						const int32 EffectReadIndex = 1 - EffectWriteIndex;
						FMixtormatPeelingCS::FParameters* EffectParameters =
							GraphBuilder.AllocParameters<FMixtormatPeelingCS::FParameters>();
						EffectParameters->OutputSize = Request.Resolution;
						EffectParameters->Initialize = EffectPassIndex == 0 ? 1u : 0u;
						EffectParameters->Tiling = Effect.Tiling;
						EffectParameters->Strength = Effect.Strength;
						EffectParameters->Front = Effect.Front;
						EffectParameters->Width = Effect.Width;
						EffectParameters->MacroWarp = Effect.MacroWarp;
						EffectParameters->MicroWarp = Effect.MicroWarp;
						EffectParameters->MicroMorph = Effect.MicroMorph;
						EffectParameters->Thickness = Effect.Thickness;
						EffectParameters->Lift = Effect.Lift;
						EffectParameters->DetailStrength = Effect.DetailStrength;
						EffectParameters->DistanceRange = Effect.DistanceRange;
						EffectParameters->SDFRange = Effect.SDFRange;
						EffectParameters->HeightRange = Effect.HeightRange;
						EffectParameters->PreviousEffectData = EffectTargets[EffectReadIndex];
						EffectParameters->ChildMask = CombinedMask;
						// A procedural peel has no source maps at all, and RegisterTexture
						// asserts on a null RHI texture, so those four slots take the
						// placeholder. The shader ignores them under ProceduralSource.
						if (Effect.bProceduralPeel)
						{
							EffectParameters->PeelData = PeelFieldDummy;
							EffectParameters->PeelMask = PeelFieldDummy;
							EffectParameters->PeelHeight = PeelFieldDummy;
							EffectParameters->PeelSDF = PeelFieldDummy;
						}
						else
						{
							EffectParameters->PeelData = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.PeelData, TEXT("Mixtormat.PeelData"));
							EffectParameters->PeelMask = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Mask, TEXT("Mixtormat.PeelMask"));
							EffectParameters->PeelHeight = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Height, TEXT("Mixtormat.PeelHeight"));
							EffectParameters->PeelSDF = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.SDF, TEXT("Mixtormat.PeelSDF"));
						}
						EffectParameters->ProceduralSource = Effect.bProceduralPeel ? 1u : 0u;
						EffectParameters->ProceduralAOStrength = Effect.PeelAOStrength;
						EffectParameters->HeightAmount = Effect.PeelHeightAmount;
						EffectParameters->HeightInvert = Effect.bPeelHeightInvert ? 1.0f : 0.0f;
						EffectParameters->PreviousEffectHeight = EffectHeightTargets[EffectReadIndex];
						EffectParameters->OutputEffectHeight = GraphBuilder.CreateUAV(EffectHeightTargets[EffectWriteIndex]);
						EffectParameters->PeelFieldA = PeelFieldA;
						EffectParameters->PeelFieldB = PeelFieldB;
						EffectParameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
						EffectParameters->PointWrapSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						EffectParameters->OutputEffectData = GraphBuilder.CreateUAV(EffectTargets[EffectWriteIndex]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Peeling.Layer%d.Child%d", LayerIndex, ChildIndex),
							PeelingShader,
							EffectParameters,
							FIntVector(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1));
						CombinedEffectData = EffectTargets[EffectWriteIndex];
						CombinedEffectHeight = EffectHeightTargets[EffectWriteIndex];
						++EffectPassIndex;
					}

					const int32 WriteIndex = LayerIndex & 1;
					const int32 ReadIndex = 1 - WriteIndex;
					FMixtormatCompositeCS::FParameters* Parameters =
						GraphBuilder.AllocParameters<FMixtormatCompositeCS::FParameters>();
					Parameters->OutputSize = Request.Resolution;
					Parameters->Initialize = LayerIndex == 0 ? 1u : 0u;
					Parameters->Enabled = Layer.bEnabled ? 1u : 0u;
					Parameters->HasMask = Layer.bHasMask ? 1u : 0u;
					Parameters->HasEffects = Layer.bHasEffects ? 1u : 0u;
					Parameters->HasStain = Layer.bHasStain ? 1u : 0u;
					Parameters->OverrideBaseColor = Layer.bOverrideBaseColor ? 1u : 0u;
					Parameters->OverrideRoughness = Layer.bOverrideRoughness ? 1u : 0u;
					Parameters->OverrideMetallic = Layer.bOverrideMetallic ? 1u : 0u;
					Parameters->CompositionMode = Layer.bCoat ? 1u : 0u;
					Parameters->IsFill = Layer.bFill ? 1u : 0u;
					Parameters->HasSurface = Layer.bHasSurface ? 1u : 0u;
					Parameters->HasPackedHeight = Layer.bHasPackedHeight ? 1u : 0u;
					Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
					Parameters->NormalOnly = Layer.bNormalOnly ? 1u : 0u;
					Parameters->OverrideNormal = Layer.bOverrideNormal ? 1u : 0u;
					Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
					Parameters->HeightBlendEnabled = Layer.bHeightBlendEnabled ? 1u : 0u;
					Parameters->HeightSource = Layer.HeightSource;
					Parameters->InvertHeight = Layer.bInvertHeight ? 1u : 0u;
					Parameters->DirectHeightComparison = Layer.bDirectHeightComparison ? 1u : 0u;
					Parameters->InvertHeightFeature = Layer.bInvertHeightFeature ? 1u : 0u;
					Parameters->InvertAOFeature = Layer.bInvertAOFeature ? 1u : 0u;
					Parameters->InvertFeature = Layer.bInvertFeature ? 1u : 0u;
					Parameters->DebugMode = static_cast<uint32>(Request.DebugSettings.Mode);
					Parameters->WriteDebug = Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::None
						&& Request.DebugSettings.LayerIndex == LayerIndex ? 1u : 0u;
					Parameters->Opacity = Layer.Opacity;
					Parameters->Tiling = Layer.Tiling;
					Parameters->NormalIntensity = Layer.NormalIntensity;
					Parameters->HueShift = Layer.HueShift;
					Parameters->Saturation = Layer.Saturation;
					Parameters->Value = Layer.Value;
					Parameters->RoughnessBias = Layer.RoughnessBias;
					Parameters->RoughnessContrast = Layer.RoughnessContrast;
					Parameters->RoughnessOffset = Layer.RoughnessOffset;
					Parameters->FillRoughness = Layer.FillRoughness;
					Parameters->FillMetallic = Layer.FillMetallic;
					Parameters->LayerF0 = Layer.LayerF0;
					Parameters->BaseColorInfluence = Layer.BaseColorInfluence;
					Parameters->RoughnessInfluence = Layer.RoughnessInfluence;
					Parameters->AOInfluence = Layer.AOInfluence;
					Parameters->MetallicInfluence = Layer.MetallicInfluence;
					Parameters->F0Influence = Layer.F0Influence;
					Parameters->NormalInfluence = Layer.NormalInfluence;
					Parameters->HeightInfluence = Layer.HeightInfluence;
					Parameters->HeightBlendAmount = Layer.HeightBlendAmount;
					Parameters->HeightThreshold = Layer.HeightThreshold;
					Parameters->HeightRange = Layer.HeightRange;
					Parameters->HeightContrast = Layer.HeightContrast;
					Parameters->HeightOffset = Layer.HeightOffset;
					Parameters->HeightBias = Layer.HeightBias;
					Parameters->ConstantHeight = Layer.ConstantHeight;
					Parameters->MaskHeightInfluence = Layer.MaskHeightInfluence;
					Parameters->HeightContactAOAmount = Layer.HeightContactAOAmount;
					Parameters->HeightContactAOWidth = Layer.HeightContactAOWidth;
					Parameters->HeightBorderLift = Layer.HeightBorderLift;
					Parameters->HeightBorderWidth = Layer.HeightBorderWidth;
					Parameters->HeightBorderNormalStrength = Layer.HeightBorderNormalStrength;
					Parameters->FeatureInfluence = Layer.FeatureInfluence;
					Parameters->FeatureBias = Layer.FeatureBias;
					Parameters->HeightFeatureInfluence = Layer.HeightFeatureInfluence;
					Parameters->AOFeatureInfluence = Layer.AOFeatureInfluence;
					Parameters->CurvatureRadius = Layer.CurvatureRadius;
					Parameters->CurvatureStrength = Layer.CurvatureStrength;
					Parameters->CurvaturePower = Layer.CurvaturePower;
					Parameters->CurvatureSmoothing = Layer.CurvatureSmoothing;
					Parameters->FillColor = Layer.FillColor;
					Parameters->PreviousBC = OutputBC[ReadIndex];
					Parameters->PreviousN = OutputN[ReadIndex];
					Parameters->PreviousRAM = OutputRAM[ReadIndex];
					Parameters->PreviousHeight = HeightTargets[ReadIndex];
					Parameters->ReferenceHeight = HeightTargets[ReadIndex];
					if (FRDGTextureRef* Snapshot = HeightSnapshots.Find(Layer.HeightReferenceLayerIndex))
					{
						Parameters->ReferenceHeight = *Snapshot;
					}
					Parameters->LayerBC = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.LayerBC"));
					Parameters->LayerN = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Normal,
						TEXT("Mixtormat.LayerN"));
					Parameters->LayerRAM = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.RAM,
						TEXT("Mixtormat.LayerRAM"));
					Parameters->LayerMask = CombinedMask;
					Parameters->EffectData = CombinedEffectData;
					Parameters->EffectHeight = CombinedEffectHeight;
					Parameters->StainData = CombinedStainData;
					Parameters->DebugMask = DebugMask;
					Parameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
					Parameters->OutputBC = GraphBuilder.CreateUAV(OutputBC[WriteIndex]);
					Parameters->OutputN = GraphBuilder.CreateUAV(OutputN[WriteIndex]);
					Parameters->OutputRAM = GraphBuilder.CreateUAV(OutputRAM[WriteIndex]);
					Parameters->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
					Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.Composite.Layer%d", LayerIndex),
						Shader,
						Parameters,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));

					// Erosion filters the layer output: it reads the height and normal this
					// layer just composited, carves the height, derives the normal change from
					// what it removed, and writes both back.
					if (PendingErosion)
					{
						const FEffectRenderData& Ero = *PendingErosion;

						// Erosion runs at twice the composition resolution, capped at 4096,
						// then resamples back. Carving is high-frequency work: at composition
						// resolution the octave loop hits the two-pixels-per-cell floor with
						// passes still to run, so the finest gullies have nowhere to cut.
						// Above 4096 the cost stops buying visible detail.
						const FIntPoint EroRes(
							FMath::Min(Request.Resolution.X * 2, 4096),
							FMath::Min(Request.Resolution.Y * 2, 4096));
						const bool bResample = EroRes != Request.Resolution;

						const FRDGTextureDesc EroDesc = FRDGTextureDesc::Create2D(
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
						FRDGTextureRef EroRidge = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionRidge"));
						FRDGTextureRef EroGuide = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionGuide"));
						FRDGTextureRef EroN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionN"));
						// The layer normal every carving pass reads, lifted to erosion resolution.
						FRDGTextureRef EroSrcN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionSrcN"));

						AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroRidge), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

						// The resample path never writes a ridge, but the parameter struct still
						// needs a bound UAV. Binding EroRidge would size-mismatch the downsample
						// dispatch, so give it a 1x1 target that is never written.
						FRDGTextureRef ResampleRidgeDummy = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(
								FIntPoint(1, 1),
								PF_R16F,
								FClearValueBinding::White,
								TexCreate_ShaderResource | TexCreate_UAV),
							TEXT("Mixtormat.ErosionResampleRidgeDummy"));

						// One dispatch moves height and normal together, in either direction.
						auto AddErosionResample = [&](
							FRDGTextureRef InH,
							FRDGTextureRef InN,
							FRDGTextureRef OutH,
							FRDGTextureRef OutN,
							const FIntPoint DestRes,
							const TCHAR* DebugName)
						{
							FMixtormatErosionCS::FParameters* RP =
								GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
							RP->OutputSize = DestRes;
							RP->ResamplePass = 1;
							RP->PreviousHeight = InH;
							RP->SourceHeight = InH;
							RP->GuideHeight = InH;
							RP->PreviousNormal = InN;
							RP->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							RP->OutputHeight = GraphBuilder.CreateUAV(OutH);
							RP->OutputRidge = GraphBuilder.CreateUAV(ResampleRidgeDummy);
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
								SourceH, EroSrcN, EroRes, TEXT("Up"));
						}
						else
						{
							AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], SourceH);
							AddCopyTexturePass(GraphBuilder, OutputN[WriteIndex], EroSrcN);
						}

						// Fixed pass count. Eight is where the octave loop stops adding shape
						// on the surfaces this filter is aimed at.
						const int32 Iterations = 8;

						// Cells across one UV repeat at the coarsest pass. Fixed so the field
						// always resolves to a whole number of cells and therefore tiles; it
						// doubles per octave, so it must stay integral.
						const int32 ErosionPeriodCells = 32;
						FRDGTextureRef Src = SourceH;
						FRDGTextureRef Result = SourceH;
						for (int32 PassIndex = 0; PassIndex <= Iterations; ++PassIndex)
						{
							const bool bNormalPass = PassIndex == Iterations;

							// Guidance blur before each carving pass, so the flow follows surface
							// shape rather than grain. Skipped entirely at zero.
							if (!bNormalPass && Ero.ErosionSlopeBlur > 0.0f)
							{
								FMixtormatErosionCS::FParameters* BP =
									GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
								BP->OutputSize = EroRes;
								BP->Pass = PassIndex;
								BP->NormalPass = 0;
								BP->BlurPass = 1;
								BP->ResamplePass = 0;
								BP->BlurRadius = Ero.ErosionSlopeBlur;
								BP->NormalStrength = Ero.ErosionNormalStrength;
								BP->Amount = Ero.ErosionAmount;
								BP->Strength = 1.0f;
								BP->Period = ErosionPeriodCells;
								BP->GullyLength = 1.5f;
								BP->LicSteps = 5;
								BP->Repose = Ero.ErosionRepose;
								BP->ReposeSoftness = Ero.ErosionReposeSoftness;
								BP->CavityBias = Ero.ErosionCavityBias;
								BP->CavityScale = Ero.ErosionCavityScale;
								BP->HeightInfluence = Ero.ErosionHeightInfluence;
								BP->HeightScale = Ero.ErosionHeightScale;
								BP->GullyWeight = Ero.ErosionGullyWeight;
								BP->BlendSoftness = Ero.ErosionBlendSoftness;
								BP->Gain = 0.5f;
								BP->DerivScale = 0.6f;
								BP->DerivMin = Ero.ErosionSlopeRadius;
								BP->DirectionMode = Ero.ErosionDirectionMode;
								BP->DirectionAngle = Ero.ErosionDirectionAngle;
								BP->DirectionAmount = Ero.ErosionDirectionAmount;
								BP->Seed = 1u;
								BP->PreviousHeight = Src;
								BP->SourceHeight = SourceH;
								BP->GuideHeight = Src;
								BP->PreviousNormal = EroSrcN;
								BP->LinearWrapSampler =
									TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
								BP->OutputHeight = GraphBuilder.CreateUAV(EroGuide);
								BP->OutputRidge = GraphBuilder.CreateUAV(EroRidge);
								BP->OutputNormal = GraphBuilder.CreateUAV(EroN);
								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Blur%d", LayerIndex, PassIndex),
									ErosionShader,
									BP,
									FIntVector(
										FMath::DivideAndRoundUp(EroRes.X, 8),
										FMath::DivideAndRoundUp(EroRes.Y, 8),
										1));
							}

							const int32 Slot = PassIndex & 1;
							FMixtormatErosionCS::FParameters* EP =
								GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
							EP->OutputSize = EroRes;
							EP->Pass = PassIndex;
							EP->NormalPass = bNormalPass ? 1 : 0;
							EP->BlurPass = 0;
							EP->ResamplePass = 0;
							EP->BlurRadius = Ero.ErosionSlopeBlur;
							EP->NormalStrength = Ero.ErosionNormalStrength;
							EP->Amount = Ero.ErosionAmount;
							EP->Strength = 1.0f;
							EP->Period = ErosionPeriodCells;
							EP->GullyLength = 1.5f;
							EP->LicSteps = 5;
							EP->Repose = Ero.ErosionRepose;
							EP->ReposeSoftness = Ero.ErosionReposeSoftness;
							EP->CavityBias = Ero.ErosionCavityBias;
							EP->CavityScale = Ero.ErosionCavityScale;
							EP->HeightInfluence = Ero.ErosionHeightInfluence;
							EP->HeightScale = Ero.ErosionHeightScale;
							EP->GullyWeight = Ero.ErosionGullyWeight;
							EP->BlendSoftness = Ero.ErosionBlendSoftness;
							EP->Gain = 0.5f;
							EP->DerivScale = 0.6f;
							EP->DerivMin = Ero.ErosionSlopeRadius;
							EP->DirectionMode = Ero.ErosionDirectionMode;
							EP->DirectionAngle = Ero.ErosionDirectionAngle;
							EP->DirectionAmount = Ero.ErosionDirectionAmount;
							EP->Seed = 1u;
							EP->PreviousHeight = Src;
							EP->SourceHeight = SourceH;
							EP->PreviousNormal = EroSrcN;
							EP->GuideHeight = Ero.ErosionSlopeBlur > 0.0f ? EroGuide : Src;
							EP->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							EP->OutputHeight = GraphBuilder.CreateUAV(EroH[Slot]);
							EP->OutputRidge = GraphBuilder.CreateUAV(EroRidge);
							EP->OutputNormal = GraphBuilder.CreateUAV(EroN);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Erosion.L%d.P%d", LayerIndex, PassIndex),
								ErosionShader,
								EP,
								FIntVector(
									FMath::DivideAndRoundUp(EroRes.X, 8),
									FMath::DivideAndRoundUp(EroRes.Y, 8),
									1));
							if (!bNormalPass)
							{
								Src = EroH[Slot];
								Result = EroH[Slot];
							}
						}

						if (bResample)
						{
							AddErosionResample(
								Result, EroN,
								HeightTargets[WriteIndex], OutputN[WriteIndex],
								Request.Resolution, TEXT("Down"));
						}
						else
						{
							AddCopyTexturePass(GraphBuilder, Result, HeightTargets[WriteIndex]);
							AddCopyTexturePass(GraphBuilder, EroN, OutputN[WriteIndex]);
						}
					}

					if (RequiredHeightSnapshots.Contains(LayerIndex))
					{
						FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc,
							TEXT("Mixtormat.HeightSnapshot"));
						AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], Snapshot);
						HeightSnapshots.Add(LayerIndex, Snapshot);
					}
				}
			}

			GraphBuilder.SetTextureAccessFinal(
				OutputBC[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputN[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputRAM[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputHeight[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputDebug[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			if (Request.OnComplete.IsBound())
			{
				AsyncTask(
					ENamedThreads::GameThread,
					[OnComplete = Request.OnComplete]() mutable
					{
						OnComplete.ExecuteIfBound();
					});
			}
		});

	return true;
}

void FMixtormatGpuCompositor::BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const
{
	MaterialInstance.SetTextureParameterValue(TEXT("ML_BaseColor"), GetBaseColorOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Normal"), GetNormalOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_RAM"), GetRAMOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Height"), GetHeightOutput());
	MaterialInstance.SetScalarParameterValue(TEXT("ML_Tiling"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessBias"), 0.5f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessContrast"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessOffset"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_NormalIntensity"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_DielectricF0"), 0.04f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_UsePackedF0"), 1.0f);
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetBaseColorOutput() const
{
	return Targets[PublishedTargetIndex].BaseColor.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetNormalOutput() const
{
	return Targets[PublishedTargetIndex].Normal.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetRAMOutput() const
{
	return Targets[PublishedTargetIndex].RAM.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetHeightOutput() const
{
	return Targets[PublishedTargetIndex].Height.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetDebugOutput() const
{
	return Targets[PublishedTargetIndex].Debug.Get();
}
