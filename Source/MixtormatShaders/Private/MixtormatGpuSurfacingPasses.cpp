// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuSurfacingPasses.h"

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatGpuGeneratorCommon.h"
#include "Compositing/MixtormatComposeHash.h"
#include "MixtormatSurfacingTypes.h"
#include "MixtormatColorRampMath.h"
#include "MixtormatScalarRampMath.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "RHIStaticStates.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

DECLARE_GPU_STAT_NAMED(MixtormatSkin, TEXT("Mixtormat Skin"));

namespace MixtormatGpuCompositor
{
class FMixtormatMossCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMossCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMossCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("MOSS_STAGE", 14);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SimulationSize)
		SHADER_PARAMETER(uint32, GrowthIterations)
		SHADER_PARAMETER(uint32, IterationIndex)
		SHADER_PARAMETER(uint32, Preview)

		SHADER_PARAMETER(int32, HeightJump)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, HeightSeeds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GrowthInteriorDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, CushionInflation)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCushionInflation)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutHeightSeeds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthInteriorDistance)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, SeedMode)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, bNormalizeHeight)
		SHADER_PARAMETER(float, CavityAttraction)
		SHADER_PARAMETER(float, OcclusionInfluence)
		SHADER_PARAMETER(float, Moisture)
		SHADER_PARAMETER(float, SurfaceAnglePreference)
		SHADER_PARAMETER(float, SlopeAngle)
		SHADER_PARAMETER(float, Competition)
		SHADER_PARAMETER(float, ExposureTolerance)

		SHADER_PARAMETER(float, SeedMaskTiling)
		SHADER_PARAMETER(uint32, bSeedMaskInvert)
		SHADER_PARAMETER(uint32, HasSeedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SeedMask)
		SHADER_PARAMETER(int32, ColonyDensity)
		SHADER_PARAMETER(float, GrowthSpread)

		SHADER_PARAMETER(float, Coverage)
		SHADER_PARAMETER(float, Splatter)
		SHADER_PARAMETER(float, CushionSize)
		SHADER_PARAMETER(float, CushionHeight)

		SHADER_PARAMETER(float, MatureHeight)
		SHADER_PARAMETER(float, ClumpDensity)
		SHADER_PARAMETER(float, ShootDensity)
		SHADER_PARAMETER(float, ShootLength)
		SHADER_PARAMETER(float, FiberThickness)
		SHADER_PARAMETER(uint32, GrowthForm)
		SHADER_PARAMETER(float, LeafScale)
		SHADER_PARAMETER(float, Hydration)
		SHADER_PARAMETER(float, SurfaceWetness)
		SHADER_PARAMETER(float, WetDarkening)
		SHADER_PARAMETER(float, ColonyVariation)
		SHADER_PARAMETER(float, LeafVariation)
		SHADER_PARAMETER(float, TipColor)
		SHADER_PARAMETER(float, Damage)

		SHADER_PARAMETER(float, Softness)
		SHADER_PARAMETER(float, Plumpness)
		SHADER_PARAMETER(float, DryRoughness)
		SHADER_PARAMETER(float, WetRoughness)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(uint32, ColorRampCount)
		SHADER_PARAMETER(uint32, ColorRampInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, ColorRampPositions, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER_ARRAY(FVector4f, ColorRampColors, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GrowthIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMossColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMossRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMossNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutMossRoughness)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SubstrateHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScaledMossHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CanopyGate)
		SHADER_PARAMETER(uint32, HasCanopyGate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SubstrateAnalysis)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, Chemical)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GrowthState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ActivationHabitat)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GrowthCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GrowthAge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutChemical)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutGrowthState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutActivationHabitat)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HabitatColony)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MesoCushionHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSubstrateAnalysis)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutHabitatColony)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutMesoHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthAge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutGrowthIds)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatMossCS, "/Plugin/Mixtormat/Private/MixtormatMoss.usf", "MainCS", SF_Compute);

namespace
{
	class FMixtormatSkinAnalysisCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinAnalysisCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinAnalysisCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(float, CurvatureRadius)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SubstrateHeight)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutAnalysis)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_ANALYSIS"), 1);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinAnalysisCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "AnalysisCS", SF_Compute);

	class FMixtormatSkinFieldCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinFieldCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinFieldCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(FIntPoint, FinalOutputSize)
			SHADER_PARAMETER(float, MarbleDrift)
			SHADER_PARAMETER(float, WarpScale)
			SHADER_PARAMETER(float, PoreSize)
			SHADER_PARAMETER(int32, WarpLayers)
			SHADER_PARAMETER(float, DirectionVariation)
			SHADER_PARAMETER(float, Elongation)
			SHADER_PARAMETER(float, Cellular)
			SHADER_PARAMETER(float, ThicknessVariation)
			SHADER_PARAMETER(float, VascularAmount)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TissueState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SeedState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FoldState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MicroState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolvedHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMicro)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTissue)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSeed)
			SHADER_PARAMETER(uint32, GeneratorLayer)
			SHADER_PARAMETER(FVector2f, GeneratorUVScale)
			SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
			SHADER_PARAMETER(int32, GeneratorUVRotation)
			SHADER_PARAMETER(uint32, GeneratorUVFlipU)
			SHADER_PARAMETER(uint32, GeneratorUVFlipV)
			SHADER_PARAMETER(uint32, UsePreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SubstrateAnalysis)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(int32, TissueCells)
			SHADER_PARAMETER(int32, FoldCells)
			SHADER_PARAMETER(int32, DetailCells)
			SHADER_PARAMETER(int32, PoreCells)
			SHADER_PARAMETER(float, SurfaceTiling)
			SHADER_PARAMETER(int32, DetailTiling)
			SHADER_PARAMETER(float, VascularScale)
			SHADER_PARAMETER(float, VascularDensity)
			SHADER_PARAMETER(float, GravityBulge)
			SHADER_PARAMETER(float, GravityHold)
			SHADER_PARAMETER(float, GravityAngle)
			SHADER_PARAMETER(float, HeightScale)
			SHADER_PARAMETER(float, Fullness)
			SHADER_PARAMETER(float, Softness)
			SHADER_PARAMETER(float, Compression)
			SHADER_PARAMETER(float, Tension)
			SHADER_PARAMETER(float, FoldDepth)
			SHADER_PARAMETER(float, FoldWidth)
			SHADER_PARAMETER(float, Direction)
			SHADER_PARAMETER(float, Crossing)
			SHADER_PARAMETER(float, TissueCoupling)
			SHADER_PARAMETER(float, CurvatureStrength)
			SHADER_PARAMETER(float, PoreDepth)
			SHADER_PARAMETER(float, PoreVariation)
			SHADER_PARAMETER(float, Grain)
			SHADER_PARAMETER(float, PigmentVariation)
			SHADER_PARAMETER(float, RednessAmount)
			SHADER_PARAMETER(float, VeinAmount)
			SHADER_PARAMETER(float, VeinWidth)
			SHADER_PARAMETER(float, VeinDepth)
			SHADER_PARAMETER(float, VeinDensity)
			SHADER_PARAMETER(float, OilAmount)
			SHADER_PARAMETER(float, Dryness)
			SHADER_PARAMETER(float, Flaking)
			SHADER_PARAMETER(float, FinishVariation)
			SHADER_PARAMETER(float, CavityAO)
			SHADER_PARAMETER(float, CavityRoughness)
			SHADER_PARAMETER(float, IOR)
			SHADER_PARAMETER(float, FuzzAmount)
			SHADER_PARAMETER(float, FuzzRoughness)
			SHADER_PARAMETER(float, SSSAmount)
			SHADER_PARAMETER(FVector3f, FuzzColor)
			SHADER_PARAMETER(FVector3f, MeanFreePath)
			SHADER_PARAMETER(uint32, AlbedoCount)
			SHADER_PARAMETER(uint32, AlbedoInterpolation)
			SHADER_PARAMETER_ARRAY(FVector4f, AlbedoPositions, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER_ARRAY(FVector4f, AlbedoColors, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER(uint32, RoughnessCount)
			SHADER_PARAMETER(uint32, RoughnessInterpolation)
			SHADER_PARAMETER_ARRAY(FVector4f, RoughnessPoints, [FMixtormatScalarRamp::MaxPoints])
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutRAM)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutOptics)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFuzzColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSubsurface)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPores)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCompression)

		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_ANALYSIS"), 0);
			OutEnvironment.SetDefine(TEXT("SKIN_STAGE"), 2);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinFieldCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "FieldCS", SF_Compute);

	class FMixtormatSkinTissueCS final : public FMixtormatSkinFieldCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinTissueCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinTissueCS, FMixtormatSkinFieldCS);
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FMixtormatSkinFieldCS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_STAGE"), 0);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinTissueCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "TissueCS", SF_Compute);

	class FMixtormatSkinSeedCS final : public FMixtormatSkinFieldCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinSeedCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinSeedCS, FMixtormatSkinFieldCS);
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FMixtormatSkinFieldCS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_STAGE"), 1);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinSeedCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "SeedCS", SF_Compute);

	class FMixtormatSkinMicroCS final : public FMixtormatSkinFieldCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinMicroCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinMicroCS, FMixtormatSkinFieldCS);
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FMixtormatSkinFieldCS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_STAGE"), 3);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinMicroCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "MicroCS", SF_Compute);

	class FMixtormatSkinResolveCS final : public FMixtormatSkinFieldCS
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinResolveCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinResolveCS, FMixtormatSkinFieldCS);
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
			FShaderCompilerEnvironment& OutEnvironment)
		{
			FMixtormatSkinFieldCS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SKIN_STAGE"), 4);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinResolveCS,
		"/Plugin/Mixtormat/Private/MixtormatSkin.usf", "ResolveCS", SF_Compute);

	class FMixtormatSkinSolveCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSkinSolveCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSkinSolveCS, FGlobalShader);
		class FStage : SHADER_PERMUTATION_INT("SKIN_SOLVE_STAGE", 3);
		using FPermutationDomain = TShaderPermutationDomain<FStage>;
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(float, FoldRate)
			SHADER_PARAMETER(uint32, HasPreviousFold)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutPreparedSeed)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutPreparedTissue)
			SHADER_PARAMETER(float, Bending)
			SHADER_PARAMETER(float, Support)
			SHADER_PARAMETER(float, GravityHold)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TissueState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SeedState)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousFold)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFold)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSkinSolveCS,
		"/Plugin/Mixtormat/Private/MixtormatSkinSolve.usf", "MainCS", SF_Compute);

	class FMixtormatSurfacingApplyCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatSurfacingApplyCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatSurfacingApplyCS, FGlobalShader);
		class FStage : SHADER_PERMUTATION_INT("SURFACING_STAGE", 3);
		using FPermutationDomain = TShaderPermutationDomain<FStage>;
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(float, Amount)
			SHADER_PARAMETER(uint32, HasGate)
			SHADER_PARAMETER(uint32, HasParentGate)
			SHADER_PARAMETER(uint32, HasLocalGate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ParentGate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LocalGate)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGate)
			SHADER_PARAMETER(uint32, HasMaterialCoverage)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaterialCoverage)
			SHADER_PARAMETER(uint32, WriteHeight)
			SHADER_PARAMETER(uint32, WriteColor)
			SHADER_PARAMETER(uint32, WriteRoughness)
			SHADER_PARAMETER(uint32, WriteMetallic)
			SHADER_PARAMETER(uint32, WriteAO)
			SHADER_PARAMETER(uint32, WriteF0)
			SHADER_PARAMETER(uint32, WriteFuzz)
			SHADER_PARAMETER(uint32, WriteSSS)
			SHADER_PARAMETER(uint32, WriteNormal)
			SHADER_PARAMETER(uint32, HasPreviousNormal)
			SHADER_PARAMETER(float, NormalStrength)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DetailNormal)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutNormal)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SkinHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousOptics)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousFuzzColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousSubsurface)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SkinColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SkinRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SkinOptics)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SkinFuzzColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SkinSubsurface)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutRAM)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutOptics)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFuzzColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSubsurface)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BeforeHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AfterHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CompletedRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CompletedOptics)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RawPores)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RawCompression)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightDelta)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRoughness)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutAO)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutF0)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFuzz)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPores)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCompression)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatSurfacingApplyCS,
		"/Plugin/Mixtormat/Private/MixtormatSurfacingApply.usf", "MainCS", SF_Compute);

	FRDGTextureRef MakeSurfaceTexture(FMixtormatComposeContext& Ctx, EPixelFormat Format, const TCHAR* Name)
	{
		return Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Ctx.Request.Resolution, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	}

	void DispatchSurface(FMixtormatComposeContext& Ctx, FMixtormatSurfacingApplyCS::FParameters* P, int32 Stage)
	{
		FMixtormatSurfacingApplyCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatSurfacingApplyCS::FStage>(Stage);
		TShaderMapRef<FMixtormatSurfacingApplyCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		P->OutputSize = Ctx.Request.Resolution;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Surfacing.Resolve.S%d", Stage),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(P->OutputSize.X, 8), FMath::DivideAndRoundUp(P->OutputSize.Y, 8), 1));
	}

	FVector3f PhysicalRGB(const FLinearColor& Value, const FLinearColor& Default)
	{
		const auto Component = [](float V, float D)
		{
			return FMath::Max(FMath::IsFinite(V) ? V : D, 0.0f);
		};
		return FVector3f(Component(Value.R, Default.R), Component(Value.G, Default.G), Component(Value.B, Default.B));
	}
}

FRDGTextureRef AddMossPasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const FChildRenderData& Child, FRDGTextureRef RunningHeight,
	FRDGTextureRef BelowHeight, FGeneratorBundle* Bundle, FRDGTextureRef PreUV)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FMossRenderData& Data = Child.Moss;
	const FMixtormatMoss& Settings = Data.Settings;
	const FIntPoint Size = Ctx.Request.Resolution;
	constexpr int32 SlotCount = 8;
	FRDGTextureRef Outputs[SlotCount] = {};
	static const TCHAR* Names[SlotCount] = {
		TEXT("Mixtormat.Moss.Height"), TEXT("Mixtormat.Moss.Coverage"),
		TEXT("Mixtormat.Moss.Age"), TEXT("Mixtormat.Moss.Ids"),
		TEXT("Mixtormat.Moss.Color"), TEXT("Mixtormat.Moss.RAM"),
		TEXT("Mixtormat.Moss.Normal"), TEXT("Mixtormat.Moss.Roughness")};
	if (const auto* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
	{
		for (int32 Index = 0; Index < SlotCount; ++Index) { Outputs[Index] = (*Memo)[Index]; }
	}
	else
	{
		FMixtormatNodeCache* Cache = Ctx.Request.NodeCache.Get();
		const uint64 Key = Cache && !PreUV && Data.FieldKey
			? MixtormatComposeHash::Combine(Data.FieldKey, 0x4d6f737352444733ull) : 0;
		const auto Hit = Key ? Cache->Find(Key, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
		bool bComplete = Hit.IsValid();
		for (int32 Index = 0; bComplete && Index < SlotCount; ++Index)
		{
			bComplete = Hit->Outputs[Index].IsValid();
		}
		if (bComplete)
		{
			for (int32 Index = 0; Index < SlotCount; ++Index)
			{
				Outputs[Index] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[Index], Names[Index]);
			}
		}
		else
		{
			const EPixelFormat Formats[SlotCount] = {PF_R32_FLOAT, PF_R16F, PF_R16F, PF_R32_UINT,
				PF_FloatRGBA, PF_FloatRGBA, PF_FloatRGBA, PF_R16F};
			for (int32 Index = 0; Index < SlotCount; ++Index)
			{
				Outputs[Index] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Formats[Index],
					FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Names[Index]);
			}

			RDG_EVENT_SCOPE(GraphBuilder, "Mixtormat.Moss.Total.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex);
			// The lattice is independent of export pixel count; retain the tile's aspect ratio.
			const int32 Longest = FMath::Max(Size.X, Size.Y);
			const FIntPoint SimulationSize(
				FMath::Max(1, FMath::RoundToInt(double(Settings.SimulationResolution) * Size.X / Longest)),
				FMath::Max(1, FMath::RoundToInt(double(Settings.SimulationResolution) * Size.Y / Longest)));
			const auto MakeTexture = [&](FIntPoint Extent, EPixelFormat Format, const TCHAR* Name)
			{
				return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent, Format,
					FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
			};
			FRDGTextureRef HabitatColony = MakeTexture(Size, PF_FloatRGBA, TEXT("Mixtormat.Moss.HabitatColony"));
			FRDGTextureRef MesoHeight = MakeTexture(Size, PF_R32_FLOAT, TEXT("Mixtormat.Moss.MesoHeight"));
			FRDGTextureRef HeightSeeds[2] = {
				MakeTexture(SimulationSize, PF_G32R32F, TEXT("Mixtormat.Moss.HeightSeeds.Ping")),
				MakeTexture(SimulationSize, PF_G32R32F, TEXT("Mixtormat.Moss.HeightSeeds.Pong"))};
			FRDGTextureRef InteriorDistance = MakeTexture(SimulationSize, PF_R32_FLOAT, TEXT("Mixtormat.Moss.InteriorDistance"));
			FRDGTextureRef RawInflation = MakeTexture(SimulationSize, PF_G32R32F, TEXT("Mixtormat.Moss.Inflation.Raw"));
			FRDGTextureRef SmoothInflation = MakeTexture(SimulationSize, PF_G32R32F, TEXT("Mixtormat.Moss.Inflation.Smooth"));
			int32 CurrentHeightSeeds = 0;
			FRDGTextureRef SubstrateAlways = ResolveSubstrateHeight(Ctx, LayerCtx, RunningHeight, BelowHeight);

			// A separate cache retains chemicals across output remaps, previews and relief edits.
			// Resolution here is the request resolution, as required by the shared node-cache LRU.
			constexpr int32 SimulationSlotCount = 4;
			const uint64 SimulationKey = Cache && !PreUV ? Data.SimulationKey : 0;
			const auto SimulationHit = SimulationKey ? Cache->Find(SimulationKey, Size)
				: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			bool bSimulationComplete = SimulationHit.IsValid();
			for (int32 Index = 0; bSimulationComplete && Index < SimulationSlotCount; ++Index)
			{
				bSimulationComplete = SimulationHit->Outputs[Index].IsValid();
			}
			FRDGTextureRef Chemicals[2] = {};
			FRDGTextureRef States[2] = {};
			FRDGTextureRef SubstrateAnalysis = nullptr;
			FRDGTextureRef ActivationHabitat = nullptr;
			FRDGTextureRef Substrate = nullptr;
			FRDGTextureRef SeedMaskTexture = nullptr;
			if (bSimulationComplete)
			{
				Chemicals[0] = GraphBuilder.RegisterExternalTexture(SimulationHit->Outputs[0], TEXT("Mixtormat.Moss.Chemical.Cached"));
				States[0] = GraphBuilder.RegisterExternalTexture(SimulationHit->Outputs[1], TEXT("Mixtormat.Moss.ArrivalRoots.Cached"));
				SubstrateAnalysis = GraphBuilder.RegisterExternalTexture(SimulationHit->Outputs[2], TEXT("Mixtormat.Moss.SubstrateAnalysis.Cached"));
				ActivationHabitat = GraphBuilder.RegisterExternalTexture(SimulationHit->Outputs[3], TEXT("Mixtormat.Moss.Activation.Cached"));
			}
			else
			{
				Substrate = SubstrateAlways;
				SeedMaskTexture = Data.SeedMask.IsValid()
					? RegisterTexture(GraphBuilder, Ctx.RegisteredTextures, Data.SeedMask, TEXT("Mixtormat.Moss.SeedMask"))
					: Ctx.EmptyDriverSignal;
				SubstrateAnalysis = MakeTexture(SimulationSize, PF_FloatRGBA, TEXT("Mixtormat.Moss.SubstrateAnalysis"));
				ActivationHabitat = MakeTexture(SimulationSize, PF_FloatRGBA, TEXT("Mixtormat.Moss.Activation"));
				Chemicals[0] = MakeTexture(SimulationSize, PF_G16R16F, TEXT("Mixtormat.Moss.Chemical.Ping"));
				Chemicals[1] = MakeTexture(SimulationSize, PF_G16R16F, TEXT("Mixtormat.Moss.Chemical.Pong"));
				States[0] = MakeTexture(SimulationSize, PF_A32B32G32R32F, TEXT("Mixtormat.Moss.ArrivalRoots.Ping"));
				States[1] = MakeTexture(SimulationSize, PF_A32B32G32R32F, TEXT("Mixtormat.Moss.ArrivalRoots.Pong"));
			}
			int32 Current = 0;
			const FIntVector Pixels(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
			const FIntVector SimulationPixels(FMath::DivideAndRoundUp(SimulationSize.X, 8), FMath::DivideAndRoundUp(SimulationSize.Y, 8), 1);

			const auto DispatchStage = [&](int32 Stage, uint32 Iteration = 0u)
			{
				FMixtormatMossCS::FPermutationDomain Permutation;
				Permutation.Set<FMixtormatMossCS::FStage>(Stage);
				TShaderMapRef<FMixtormatMossCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
				auto* P = GraphBuilder.AllocParameters<FMixtormatMossCS::FParameters>();
				FillGeneratorPlacement(P, Layer, PreUV, Ctx.EmptyPatternUV);
				P->OutputSize = Size;
				P->SimulationSize = SimulationSize;
				P->GrowthIterations = static_cast<uint32>(Settings.GrowthIterations);
				P->IterationIndex = Iteration;
				P->Preview = static_cast<uint32>(Settings.Preview);

				P->Seed = static_cast<uint32>(Settings.Seed);
				P->SeedMode = static_cast<uint32>(Settings.SeedMode);
				// Shared signed-height assembly owns normalization/scale, once, downstream.
				P->HeightScale = 1.0f;
				P->bNormalizeHeight = 0u;
				P->CavityAttraction = Settings.CavityAttraction;
				P->OcclusionInfluence = Settings.OcclusionInfluence;
				P->Moisture = Settings.Moisture;
				P->SurfaceAnglePreference = Settings.SurfaceAnglePreference;
				P->SlopeAngle = Settings.SlopeAngle;
				P->Competition = Settings.Competition;
				P->ExposureTolerance = Settings.ExposureTolerance;

				P->SeedMaskTiling = static_cast<float>(Settings.SeedMaskTiling);
				P->bSeedMaskInvert = Settings.bSeedMaskInvert ? 1u : 0u;
				P->HasSeedMask = Data.SeedMask.IsValid() ? 1u : 0u;
				P->ColonyDensity = Settings.ColonyDensity;
				P->GrowthSpread = Settings.GrowthSpread;

				P->Coverage = Settings.Coverage;
				P->Splatter = Settings.Splatter;
				P->DryRoughness = Settings.DryRoughness;
				P->WetRoughness = Settings.WetRoughness;
				P->NormalStrength = Settings.NormalStrength;
				{
					const auto Ramp = MixtormatColorRampMath::PrepareGpuPayload(Settings.ColorRamp);
					P->ColorRampCount = Ramp.StopCount;
					P->ColorRampInterpolation = Ramp.Interpolation;
					for (int32 I = 0; I < FMixtormatColorRamp::MaxStops; ++I)
					{
						P->ColorRampPositions[I] = FVector4f(Ramp.Positions[I], 0.0f, 0.0f, 0.0f);
						P->ColorRampColors[I] = Ramp.Colors[I];
					}
				}

				P->HeightJump = static_cast<int32>(Iteration);
				P->CushionSize = Settings.CushionSize;
				P->CushionHeight = Settings.CushionHeight;

				P->MatureHeight = Settings.MatureHeight;
				P->ClumpDensity = Settings.ClumpDensity;
				P->ShootDensity = Settings.ShootDensity;
				P->ShootLength = Settings.ShootLength;
				P->FiberThickness = Settings.FiberThickness;
				P->GrowthForm = static_cast<uint32>(Settings.GrowthForm);
				P->LeafScale = Settings.LeafScale;
				P->Hydration = Settings.Hydration;
				P->SurfaceWetness = Settings.SurfaceWetness;
				P->WetDarkening = Settings.WetDarkening;
				P->ColonyVariation = Settings.ColonyVariation;
				P->LeafVariation = Settings.LeafVariation;
				P->TipColor = Settings.TipColor;
				P->Damage = Settings.Damage;

				P->Softness = Settings.Softness;
				P->Plumpness = Settings.Plumpness;

				P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				// Bind only this stage's reads/writes. No texture aliases an SRV and UAV in a step.
				if (Stage == 0)
				{
					P->SubstrateHeight = Substrate;
					P->OutSubstrateAnalysis = GraphBuilder.CreateUAV(SubstrateAnalysis);
				}
				else if (Stage == 1)
				{
					P->SubstrateAnalysis = SubstrateAnalysis;
					P->SeedMask = SeedMaskTexture;
					P->OutActivationHabitat = GraphBuilder.CreateUAV(ActivationHabitat);
					P->OutChemical = GraphBuilder.CreateUAV(Chemicals[Current]);
					P->OutGrowthState = GraphBuilder.CreateUAV(States[Current]);
				}
				else if (Stage == 4)
				{
					P->Chemical = Chemicals[Current];
					P->GrowthState = States[Current];
					P->ActivationHabitat = ActivationHabitat;
					P->SubstrateHeight = SubstrateAlways;
					P->OutChemical = GraphBuilder.CreateUAV(Chemicals[1 - Current]);
					P->OutGrowthState = GraphBuilder.CreateUAV(States[1 - Current]);
				}
				else if (Stage == 5)
				{
					P->Chemical = Chemicals[Current];
					P->GrowthState = States[Current];
					P->ActivationHabitat = ActivationHabitat;
					P->OutHabitatColony = GraphBuilder.CreateUAV(HabitatColony);
					P->OutGrowthCoverage = GraphBuilder.CreateUAV(Outputs[1]);
					P->OutGrowthAge = GraphBuilder.CreateUAV(Outputs[2]);
					P->OutGrowthIds = GraphBuilder.CreateUAV(Outputs[3]);
				}
				else if (Stage == 2)
				{
					P->CushionInflation = SmoothInflation;
					P->GrowthInteriorDistance = InteriorDistance;
					P->GrowthState = States[Current];
					P->SubstrateAnalysis = SubstrateAnalysis;
					P->HabitatColony = HabitatColony;
					P->OutMesoHeight = GraphBuilder.CreateUAV(MesoHeight);
				}
				else if (Stage == 3)
				{
					P->Chemical = Chemicals[Current];
					P->GrowthState = States[Current];
					P->ActivationHabitat = ActivationHabitat;
					P->GrowthCoverage = Outputs[1];
					P->GrowthAge = Outputs[2];
					P->HabitatColony = HabitatColony;
					P->MesoCushionHeight = MesoHeight;
					P->GrowthInteriorDistance = InteriorDistance;
					P->OutGrowthHeight = GraphBuilder.CreateUAV(Outputs[0]);
				}
				else if (Stage == 8)
				{
					P->Chemical = Chemicals[Current];
					P->GrowthState = States[Current];
					P->OutHeightSeeds = GraphBuilder.CreateUAV(HeightSeeds[CurrentHeightSeeds]);
				}
				else if (Stage == 9)
				{
					P->HeightSeeds = HeightSeeds[CurrentHeightSeeds];
					P->OutHeightSeeds = GraphBuilder.CreateUAV(HeightSeeds[1 - CurrentHeightSeeds]);
				}
				else if (Stage == 10)
				{
					P->Chemical = Chemicals[Current];
					P->GrowthState = States[Current];
					P->HeightSeeds = HeightSeeds[CurrentHeightSeeds];
					P->OutGrowthInteriorDistance = GraphBuilder.CreateUAV(InteriorDistance);
					P->OutCushionInflation = GraphBuilder.CreateUAV(RawInflation);
				}
				else if (Stage == 11)
				{
					P->CushionInflation = RawInflation;
					P->GrowthInteriorDistance = InteriorDistance;
					P->OutCushionInflation = GraphBuilder.CreateUAV(SmoothInflation);
				}
				else if (Stage == 12)
				{
					P->MesoCushionHeight = MesoHeight;
					P->GrowthState = States[Current];
					P->GrowthCoverage = Outputs[1];
					P->GrowthAge = Outputs[2];
					P->GrowthIds = Outputs[3];
					P->OutMossColor = GraphBuilder.CreateUAV(Outputs[4]);
					P->OutMossRAM = GraphBuilder.CreateUAV(Outputs[5]);
					P->OutMossNormal = GraphBuilder.CreateUAV(Outputs[6]);
					P->OutMossRoughness = GraphBuilder.CreateUAV(Outputs[7]);
				}

				ClearUnusedGraphResources(Shader, P);
				const bool bSimulationStage = Stage == 0 || Stage == 1 || Stage == 4 || (Stage >= 8 && Stage <= 11);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Moss.S%d.I%u.L%d.C%d",
					Stage, Iteration, LayerCtx.LayerIndex, Child.SourceChildIndex), Shader, P,
					bSimulationStage ? SimulationPixels : Pixels);
			};

			if (!bSimulationComplete)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Mixtormat.Moss.Simulation.%dx%d.%dSteps",
					SimulationSize.X, SimulationSize.Y, Settings.GrowthIterations);
				DispatchStage(0);
				DispatchStage(1);
				for (int32 Iteration = 0; Iteration < Settings.GrowthIterations; ++Iteration)
				{
					DispatchStage(4, static_cast<uint32>(Iteration));
					Current = 1 - Current;
				}
				if (SimulationKey)
				{
					auto Entry = MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = SimulationKey;
					Entry->Resolution = Size;
					const FRDGTextureRef Fields[SimulationSlotCount] = {
						Chemicals[Current], States[Current], SubstrateAnalysis, ActivationHabitat};
					for (int32 Index = 0; Index < SimulationSlotCount; ++Index)
					{
						GraphBuilder.QueueTextureExtraction(Fields[Index], &Entry->Outputs[Index]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Mixtormat.Moss.CushionEnvelope");
				DispatchStage(8);
				const int32 InitialJump = static_cast<int32>(FMath::RoundUpToPowerOfTwo(
					static_cast<uint32>(FMath::Max(SimulationSize.X, SimulationSize.Y)))) / 2;
				for (int32 Jump = InitialJump; Jump > 0; Jump /= 2)
				{
					DispatchStage(9, static_cast<uint32>(Jump));
					CurrentHeightSeeds = 1 - CurrentHeightSeeds;
				}
				DispatchStage(9, 1u);
				CurrentHeightSeeds = 1 - CurrentHeightSeeds;
				DispatchStage(10);
				DispatchStage(11);
			}
			DispatchStage(5);
			DispatchStage(2);
			DispatchStage(3);
			DispatchStage(12);

			if (Key)
			{
				auto Entry = MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
				Entry->Key = Key;
				Entry->Resolution = Size;
				for (int32 Index = 0; Index < SlotCount; ++Index)
				{
					GraphBuilder.QueueTextureExtraction(Outputs[Index], &Entry->Outputs[Index]);
				}
				Ctx.PendingNodeEntries.Add(Entry);
			}
		}
		LayerCtx.GeneratorFields.Add(Child.SourceChildIndex).Append(Outputs, SlotCount);
	}
	if (Bundle)
	{
		Bundle->Height = Outputs[0];
		Bundle->Coverage = Outputs[1];
		Bundle->RegionIds = Outputs[3];
		Bundle->RegisterNamedMask(FName(TEXT("GrowthCoverage")), Outputs[1], FGeneratorBundle::EFieldSemantic::CoverageAlias);
		Bundle->RegisterNamedMask(FName(TEXT("GrowthAge")), Outputs[2], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
	}
	return Outputs[0];
}

FRDGTextureRef AddMossCanopyResolvePass(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FChildRenderData& Child,
	FRDGTextureRef AnalysisDelta, FRDGTextureRef BelowHeight, FRDGTextureRef ScaledMossHeight,
	FRDGTextureRef GrowthCoverage, FRDGTextureRef Gate)
{
	const FMixtormatMoss& Mo = Child.Surfacing.Moss;
	const bool bRawPreview = Mo.Preview != EMixtormatMossPreview::Moss;
	if (!bRawPreview && Mo.HeightScale > 0.0f && Mo.CushionSize > 0.0f)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef CanopyHeight = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Moss.CanopyHeight"));
		FMixtormatMossCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatMossCS::FStage>(13);
		TShaderMapRef<FMixtormatMossCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatMossCS::FParameters>();
		P->OutputSize = Size;
		P->HeightScale = Mo.HeightScale;
		P->CushionSize = Mo.CushionSize;
		P->CushionHeight = Mo.CushionHeight;
		P->MatureHeight = Mo.MatureHeight;
		P->SubstrateHeight = ResolveSubstrateHeight(Ctx, LayerCtx, AnalysisDelta, BelowHeight);
		P->ScaledMossHeight = ScaledMossHeight;
		P->GrowthCoverage = GrowthCoverage;
		P->HasCanopyGate = Gate ? 1u : 0u;
		P->CanopyGate = Gate ? Gate : Ctx.EmptyDriverSignal;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		P->OutGrowthHeight = Ctx.GraphBuilder.CreateUAV(CanopyHeight);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Moss.CanopyResolve"),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return CanopyHeight;
	}
	return ScaledMossHeight;
}

FSurfacingFields AddSkinSurfacePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV)
{
	FSurfacingFields Result;
	const FIntPoint Size = Ctx.Request.Resolution;
	if (!Layer.bEnabled || Child.Type != EMixtormatLayerChildType::Surfacing
		|| !Child.Surfacing.bEnabled || Child.Surfacing.Type != EMixtormatSurfacingType::Skin
		|| !SubstrateHeight || Size.X <= 0 || Size.Y <= 0)
	{
		return Result;
	}
	// Do not skip zero Amount or write-disabled channels: raw diagnostics are independent of
	// composition. The parent must gate/short-circuit the combine, not modify these textures.
	const FMixtormatSkin& Skin = Child.Surfacing.Skin;
	const FMixtormatSkin Defaults;
	FRDGBuilder& Graph = Ctx.GraphBuilder;
	RDG_EVENT_SCOPE_STAT(Graph, MixtormatSkin, "Mixtormat.Skin");
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
	const auto Make = [&Graph, Size](const TCHAR* Name, EPixelFormat Format)
	{
		return Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	};

	FRDGTextureRef Analysis = Make(TEXT("Mixtormat.Skin.Analysis"), PF_A32B32G32R32F);
	{
		TShaderMapRef<FMixtormatSkinAnalysisCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = Graph.AllocParameters<FMixtormatSkinAnalysisCS::FParameters>();
		P->OutputSize = Size;
		P->CurvatureRadius = FMath::Max(FMath::IsFinite(Skin.CurvatureRadius)
			? Skin.CurvatureRadius : Defaults.CurvatureRadius, 0.0f);
		P->SubstrateHeight = SubstrateHeight;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		P->OutAnalysis = Graph.CreateUAV(Analysis);
		FComputeShaderUtils::AddPass(Graph,
			RDG_EVENT_NAME("Mixtormat.Skin.Analysis.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, P, Groups);
	}

	Result.Height = Make(TEXT("Mixtormat.Skin.Height"), PF_R32_FLOAT);
	// RGB channels retain full float range; never half-overflow physical MFP or authored HDR tint.
	Result.Color = Make(TEXT("Mixtormat.Skin.Color"), PF_A32B32G32R32F);
	Result.RAM = Make(TEXT("Mixtormat.Skin.RAM"), PF_FloatRGBA);
	Result.Optics = Make(TEXT("Mixtormat.Skin.Optics"), PF_FloatRGBA);
	Result.FuzzColor = Make(TEXT("Mixtormat.Skin.FuzzColor"), PF_A32B32G32R32F);
	Result.Subsurface = Make(TEXT("Mixtormat.Skin.Subsurface"), PF_A32B32G32R32F);
	Result.Pores = Make(TEXT("Mixtormat.Skin.Pores"), PF_R32_FLOAT);
	Result.Compression = Make(TEXT("Mixtormat.Skin.Compression"), PF_R32_FLOAT);


	TShaderMapRef<FMixtormatSkinFieldCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = Graph.AllocParameters<FMixtormatSkinFieldCS::FParameters>();
	P->OutputSize = Size;
	P->FinalOutputSize = Size;
	P->MarbleDrift = Skin.MarbleDrift;
	P->WarpScale = Skin.WarpScale;
	P->PoreSize = Skin.PoreSize;
	P->WarpLayers = Skin.WarpLayers;
	P->DirectionVariation = Skin.DirectionVariation;
	P->Elongation = Skin.Elongation;
	P->Cellular = Skin.Cellular;
	P->ThicknessVariation = Skin.ThicknessVariation;
	P->VascularAmount = Skin.VascularAmount;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	// Reuse lifted PreUV sampling but keep placement unwrapped in SkinPlacedUV for derivatives.
	// This flag controls a shared math helper; Skin remains a distinct Surfacing child category.
	P->GeneratorLayer = 0u;
	P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
	P->GeneratorUVOffset = Layer.UVOffset;
	P->GeneratorUVRotation = Layer.Rotation;
	P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
	P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
	P->UsePreGenerationUV = PreUV ? 1u : 0u;
	P->PreGenerationUV = PreUV ? PreUV : Ctx.EmptyPatternUV;
	P->SubstrateAnalysis = Analysis;
	P->Seed = static_cast<uint32>(Skin.Seed);
	P->TissueCells = Skin.TissueCells;
	P->FoldCells = Skin.FoldCells;
	P->DetailCells = Skin.DetailCells;
	P->PoreCells = Skin.PoreCells;
	// Fractional domain repeats cannot close on the destination tile.
	P->SurfaceTiling = FMath::Max(1.0f, FMath::RoundToFloat(
		FMath::IsFinite(Skin.SurfaceTiling) ? Skin.SurfaceTiling : Defaults.SurfaceTiling));
	P->DetailTiling = Skin.DetailTiling;
	P->VascularScale = Skin.VascularScale;
	P->VascularDensity = Skin.VascularDensity;
	P->GravityBulge = Skin.GravityBulge;
	P->GravityHold = Skin.GravityHold;
	P->GravityAngle = Skin.GravityAngle;
	P->HeightScale = Skin.HeightScale;
	P->Fullness = Skin.Fullness;
	P->Softness = Skin.Softness;
	P->Compression = Skin.Compression;
	P->Tension = Skin.Tension;
	P->FoldDepth = Skin.FoldDepth;
	P->FoldWidth = Skin.FoldWidth;
	P->Direction = FMath::Fmod(Skin.Direction, 360.0f);
	P->Crossing = Skin.Crossing;
	P->TissueCoupling = Skin.TissueCoupling;
	P->CurvatureStrength = Skin.CurvatureStrength;
	P->PoreDepth = Skin.PoreDepth;
	P->PoreVariation = Skin.PoreVariation;
	P->Grain = Skin.Grain;
	P->PigmentVariation = Skin.PigmentVariation;
	P->RednessAmount = Skin.RednessAmount;
	P->VeinAmount = Skin.VeinAmount;
	P->VeinWidth = Skin.VeinWidth;
	P->VeinDepth = Skin.VeinDepth;
	P->VeinDensity = Skin.VeinDensity;
	P->OilAmount = Skin.OilAmount;
	P->Dryness = Skin.Dryness;
	P->Flaking = Skin.Flaking;
	P->FinishVariation = Skin.FinishVariation;
	P->CavityAO = Skin.CavityAO;
	P->CavityRoughness = Skin.CavityRoughness;
	P->IOR = Skin.IOR;
	P->FuzzAmount = Skin.FuzzAmount;
	P->FuzzRoughness = Skin.FuzzRoughness;
	P->SSSAmount = Skin.SSSAmount;

	P->FuzzColor = PhysicalRGB(Skin.FuzzColor, Defaults.FuzzColor);
	P->MeanFreePath = PhysicalRGB(Skin.MeanFreePath, Defaults.MeanFreePath);

	// Canonical ramp preparation owns sanitization, ordering and spline coefficients.
	const auto Albedo = MixtormatColorRampMath::PrepareGpuPayload(Skin.AlbedoRamp);
	P->AlbedoCount = Albedo.StopCount;
	P->AlbedoInterpolation = Albedo.Interpolation;
	for (int32 I = 0; I < FMixtormatColorRamp::MaxStops; ++I)
	{
		P->AlbedoPositions[I] = FVector4f(Albedo.Positions[I], 0.0f, 0.0f, 0.0f);
		P->AlbedoColors[I] = Albedo.Colors[I];
	}
	const auto Roughness = MixtormatScalarRampMath::PrepareGpuPayload(Skin.RoughnessRamp);
	P->RoughnessCount = Roughness.PointCount;
	P->RoughnessInterpolation = Roughness.Interpolation;
	for (int32 I = 0; I < FMixtormatScalarRamp::MaxPoints; ++I)
	{
		P->RoughnessPoints[I] = Roughness.Points[I];
	}
	// Analytic support and folds retain output resolution. The solver stores only a correction;
	// a low mechanics grid must never become the raster resolution of the authored surface.
	const int32 MaxSolveSize = FMath::Clamp(Skin.SolverResolution,
		FMixtormatSkin::MinSolverResolution, FMixtormatSkin::MaxSolverResolution);
	const float SolveScale = FMath::Min(1.0f, float(MaxSolveSize) / float(FMath::Max(Size.X, Size.Y)));
	const FIntPoint SolveSize(FMath::Max(1, FMath::RoundToInt(Size.X * SolveScale)),
		FMath::Max(1, FMath::RoundToInt(Size.Y * SolveScale)));
	FRDGTextureRef Tissue = Make(TEXT("Mixtormat.Skin.Tissue"), PF_A32B32G32R32F);
	FRDGTextureRef Seed = Make(TEXT("Mixtormat.Skin.Seed"), PF_A32B32G32R32F);
	{
		TShaderMapRef<FMixtormatSkinTissueCS> TissueShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* TissueParameters = Graph.AllocParameters<FMixtormatSkinTissueCS::FParameters>();
		*TissueParameters = *P;
		TissueParameters->OutputSize = Size;
		TissueParameters->OutTissue = Graph.CreateUAV(Tissue);
		ClearUnusedGraphResources(TissueShader, TissueParameters);
		FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.Tissue"),
			TissueShader, TissueParameters, Groups);
	}
	P->TissueState = Tissue;
	{
		TShaderMapRef<FMixtormatSkinSeedCS> SeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* SeedParameters = Graph.AllocParameters<FMixtormatSkinSeedCS::FParameters>();
		*SeedParameters = *P;
		SeedParameters->OutputSize = Size;
		SeedParameters->OutSeed = Graph.CreateUAV(Seed);
		ClearUnusedGraphResources(SeedShader, SeedParameters);
		FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.Seed"),
			SeedShader, SeedParameters, Groups);
	}
	P->SeedState = Seed;
	FRDGTextureRef PreviousFold = nullptr;
	int32 RemainingIterations = FMath::Clamp(Skin.SolverIterations, 0, FMixtormatSkin::MaxSolverIterations);
	const FVector2f PlacementScale = P->GeneratorUVScale * P->SurfaceTiling;
	const float AuthoredRate = float(Skin.FoldCells)
		* FMath::Max(FMath::Abs(PlacementScale.X), FMath::Abs(PlacementScale.Y))
		/ FMath::Sqrt(FMath::Max(Skin.Elongation, 1.0f));
	const float Rate = FMath::Clamp(FMath::IsFinite(AuthoredRate) ? AuthoredRate : 1.0f,
		1.0f, float(FMath::Max(SolveSize.X, SolveSize.Y)));
	const int32 CoarseDimension = FMath::Clamp(FMath::RoundToInt(Rate * FMixtormatSkin::CoarseSamplesPerFold),
		FMixtormatSkin::MinCoarseSolverResolution, FMixtormatSkin::MaxCoarseSolverResolution);
	int32 LevelDimension = RemainingIterations > 0
		? FMath::Min(CoarseDimension, FMath::Max(SolveSize.X, SolveSize.Y))
		: FMath::Max(SolveSize.X, SolveSize.Y);
	for (int32 Level = 0; ; ++Level)
	{
		const float LevelScale = FMath::Min(1.0f, float(LevelDimension) / float(FMath::Max(SolveSize.X, SolveSize.Y)));
		const FIntPoint LevelSize(FMath::Max(1, FMath::RoundToInt(SolveSize.X * LevelScale)),
			FMath::Max(1, FMath::RoundToInt(SolveSize.Y * LevelScale)));
		const bool bFinest = LevelSize == SolveSize;
		const int32 LevelIterations = bFinest ? RemainingIterations : (RemainingIterations + 2) / 3;
		RemainingIterations -= LevelIterations;
		const FIntVector LevelGroups(FMath::DivideAndRoundUp(LevelSize.X, 8), FMath::DivideAndRoundUp(LevelSize.Y, 8), 1);
		const auto MakeLevel = [&Graph, LevelSize](const TCHAR* Name, EPixelFormat Format)
		{
			return Graph.CreateTexture(FRDGTextureDesc::Create2D(LevelSize, Format,
				FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef LevelSeed = MakeLevel(TEXT("Mixtormat.Skin.LevelSeed"), PF_A32B32G32R32F);
		FRDGTextureRef LevelTissue = MakeLevel(TEXT("Mixtormat.Skin.LevelTissue"), PF_A32B32G32R32F);
		{
			FMixtormatSkinSolveCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatSkinSolveCS::FStage>(0);
			TShaderMapRef<FMixtormatSkinSolveCS> PrepareShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* PrepareParameters = Graph.AllocParameters<FMixtormatSkinSolveCS::FParameters>();
			PrepareParameters->OutputSize = LevelSize;
			PrepareParameters->SeedState = Seed;
			PrepareParameters->TissueState = Tissue;
			PrepareParameters->LinearWrapSampler = P->LinearWrapSampler;
			PrepareParameters->OutPreparedSeed = Graph.CreateUAV(LevelSeed);
			PrepareParameters->OutPreparedTissue = Graph.CreateUAV(LevelTissue);
			ClearUnusedGraphResources(PrepareShader, PrepareParameters);
			FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.Prepare.L%d", Level),
				PrepareShader, PrepareParameters, LevelGroups);
		}
		// Spend the largest steps on a coarse grid, then prolongate only the correction.
		// This avoids the tiny h^4-limited progress of starting the entire budget at high resolution.
		for (int32 Iteration = 0; Iteration <= LevelIterations; ++Iteration)
		{
			FMixtormatSkinSolveCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatSkinSolveCS::FStage>(Iteration == 0 ? 1 : 2);
			TShaderMapRef<FMixtormatSkinSolveCS> SolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* SolveParameters = Graph.AllocParameters<FMixtormatSkinSolveCS::FParameters>();
			SolveParameters->OutputSize = LevelSize;
			SolveParameters->FoldRate = Rate;
			SolveParameters->HasPreviousFold = PreviousFold ? 1u : 0u;
			SolveParameters->LinearWrapSampler = P->LinearWrapSampler;
			SolveParameters->Bending = Skin.Bending;
			SolveParameters->Support = Skin.Support;
			SolveParameters->GravityHold = Skin.GravityHold;
			SolveParameters->TissueState = LevelTissue;
			SolveParameters->SeedState = LevelSeed;
			SolveParameters->PreviousFold = PreviousFold ? PreviousFold : SubstrateHeight;
			FRDGTextureRef NextFold = MakeLevel(TEXT("Mixtormat.Skin.FoldCorrection"), PF_R32_FLOAT);
			SolveParameters->OutFold = Graph.CreateUAV(NextFold);
			ClearUnusedGraphResources(SolveShader, SolveParameters);
			FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.Relax.L%d.I%d", Level, Iteration),
				SolveShader, SolveParameters, LevelGroups);
			PreviousFold = NextFold;
		}
		if (bFinest) { break; }
		LevelDimension = FMath::Min(LevelDimension * 2, FMath::Max(SolveSize.X, SolveSize.Y));
	}
	P->FoldState = PreviousFold;
	FRDGTextureRef Micro = Make(TEXT("Mixtormat.Skin.Micro"), PF_A32B32G32R32F);
	{
		TShaderMapRef<FMixtormatSkinMicroCS> MicroShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* MicroParameters = Graph.AllocParameters<FMixtormatSkinMicroCS::FParameters>();
		*MicroParameters = *P;
		MicroParameters->OutMicro = Graph.CreateUAV(Micro);
		ClearUnusedGraphResources(MicroShader, MicroParameters);
		FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.Micro"),
			MicroShader, MicroParameters, Groups);
	}
	P->MicroState = Micro;
	{
		TShaderMapRef<FMixtormatSkinResolveCS> ResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* ResolveParameters = Graph.AllocParameters<FMixtormatSkinResolveCS::FParameters>();
		*ResolveParameters = *P;
		ResolveParameters->OutHeight = Graph.CreateUAV(Result.Height);
		ClearUnusedGraphResources(ResolveShader, ResolveParameters);
		FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Mixtormat.Skin.ResolveHeight"),
			ResolveShader, ResolveParameters, Groups);
	}
	P->ResolvedHeight = Result.Height;
	P->OutColor = Graph.CreateUAV(Result.Color);
	P->OutRAM = Graph.CreateUAV(Result.RAM);
	P->OutOptics = Graph.CreateUAV(Result.Optics);
	P->OutFuzzColor = Graph.CreateUAV(Result.FuzzColor);
	P->OutSubsurface = Graph.CreateUAV(Result.Subsurface);
	P->OutPores = Graph.CreateUAV(Result.Pores);
	P->OutCompression = Graph.CreateUAV(Result.Compression);

	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(Graph,
		RDG_EVENT_NAME("Mixtormat.Skin.Fields.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
		Shader, P, Groups);
	return Result;
}

FRDGTextureRef AddSurfacingGatePass(FMixtormatComposeContext& Ctx, FRDGTextureRef ParentGate, FRDGTextureRef LocalGate)
{
	if (!ParentGate) { return LocalGate; }
	if (!LocalGate) { return ParentGate; }
	FRDGTextureRef Out = MakeSurfaceTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Surfacing.ScopeGate"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatSurfacingApplyCS::FParameters>();
	P->HasParentGate = 1u;
	P->HasLocalGate = 1u;
	P->ParentGate = ParentGate;
	P->LocalGate = LocalGate;
	P->OutGate = Ctx.GraphBuilder.CreateUAV(Out);
	DispatchSurface(Ctx, P, 0);
	return Out;
}

FRDGTextureRef ApplySurfacingFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FChildRenderData& Child, const FSurfacingFields& Fields, FRDGTextureRef Gate,
	FRDGTextureRef RunningHeight, float AncestorAmount)
{
	const bool bMoss = Child.Surfacing.Type == EMixtormatSurfacingType::Moss;
	const FMixtormatSkin& S = Child.Surfacing.Skin;
	const FMixtormatMoss& Mo = Child.Surfacing.Moss;
	const bool bRust = Child.Surfacing.Type == EMixtormatSurfacingType::Rust;
	const FMixtormatRust& Ru = Child.Surfacing.Rust;
	const bool bWood = Child.Surfacing.Type == EMixtormatSurfacingType::Wood;
	const FMixtormatWood& W = Child.Surfacing.Wood;
	const bool bWoodActive = W.Mode == EMixtormatWoodMode::FullWood
		|| (W.Finish != EMixtormatWoodFinish::Bare && W.FinishAmount > 0.0f);
	const float Amount = (bWood ? W.Amount : (bRust ? Ru.Amount : (bMoss ? Mo.Amount : S.Amount))) * AncestorAmount;
	const bool bRustActive = !bRust || Ru.Coverage > 0.0f;
	const bool bHeight = bWood ? (bWoodActive && W.bWriteHeight && W.HeightScale != 0.0f) : (bRust
		? (Ru.bWriteHeight && bRustActive && Ru.HeightScale != 0.0f && (Ru.PitDepth != 0.0f || Ru.Buildup != 0.0f))
		: (bMoss ? (Mo.bWriteHeight && Mo.HeightScale != 0.0f) : (S.bWriteHeight && S.HeightScale != 0.0f)));
	const bool wColor = bWood ? (bWoodActive && W.bWriteColor)
		: (bRust ? Ru.bWriteColor : (bMoss ? Mo.bWriteColor : S.bWriteColor)) && bRustActive;
	const bool wRough = bWood ? (bWoodActive && W.bWriteRoughness)
		: (bRust ? Ru.bWriteRoughness : (bMoss ? Mo.bWriteRoughness : S.bWriteRoughness)) && bRustActive;
	const bool wMetal = bWood ? (bWoodActive && W.bWriteMaterial) : bRust && Ru.bWriteMaterial && bRustActive;
	const bool wAO = !bWood && !bRust && !bMoss && S.bWriteAO;
	const bool wF0 = bWood ? (bWoodActive && W.bWriteMaterial)
		: (bRust ? Ru.bWriteMaterial : (!bMoss && S.bWriteF0)) && bRustActive;
	const bool wFuzz = bWood ? (bWoodActive && W.bWriteMaterial)
		: (bRust ? Ru.bWriteMaterial : (!bMoss && S.bWriteFuzz)) && bRustActive;
	const bool wSSS = bWood ? (bWoodActive && W.bWriteMaterial)
		: (bRust ? Ru.bWriteMaterial : (!bMoss && S.bWriteSSS)) && bRustActive;
	const bool wNormal = bMoss && Fields.DetailNormal && Mo.bWriteNormal && Mo.NormalStrength != 0.0f;
	const float NormalStrength = bMoss ? Mo.NormalStrength : 0.0f;
	if (!Fields.Height || Amount <= 0.0f
		|| !(bHeight || wColor || wRough || wMetal || wAO || wF0 || wFuzz || wSSS || wNormal))
	{
		return RunningHeight;
	}
	// Inputs must already represent the effective layer source (including Fill/overrides).
	// A zero gate pixel must not suddenly change those semantics when these flags become true.
	check(RunningHeight && LayerCtx.LayerInputBC && LayerCtx.LayerInputRAM && LayerCtx.LayerInputOptics
		&& LayerCtx.LayerInputFuzzColor && LayerCtx.LayerInputSubsurface);
	FRDGTextureRef H = MakeSurfaceTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Surfacing.AppliedHeight"));
	const auto Like = [&Ctx](FRDGTextureRef Source, const TCHAR* Name)
	{
		return MakeSurfaceTexture(Ctx, Source->Desc.Format, Name);
	};
	FRDGTextureRef C = Like(LayerCtx.LayerInputBC, TEXT("Mixtormat.Surfacing.AppliedColor"));
	FRDGTextureRef R = Like(LayerCtx.LayerInputRAM, TEXT("Mixtormat.Surfacing.AppliedRAM"));
	FRDGTextureRef O = Like(LayerCtx.LayerInputOptics, TEXT("Mixtormat.Surfacing.AppliedOptics"));
	FRDGTextureRef F = Like(LayerCtx.LayerInputFuzzColor, TEXT("Mixtormat.Surfacing.AppliedFuzzColor"));
	FRDGTextureRef M = Like(LayerCtx.LayerInputSubsurface, TEXT("Mixtormat.Surfacing.AppliedSubsurface"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatSurfacingApplyCS::FParameters>();
	P->Amount = Amount;
	P->HasGate = Gate ? 1u : 0u;
	P->Gate = Gate ? Gate : Ctx.EmptyDriverSignal;
	P->HasMaterialCoverage = Fields.MaterialCoverage ? 1u : 0u;
	P->MaterialCoverage = Fields.MaterialCoverage ? Fields.MaterialCoverage : Ctx.EmptyDriverSignal;
	P->WriteHeight = bHeight;
	P->WriteColor = wColor;
	P->WriteRoughness = wRough;
	P->WriteMetallic = wMetal;
	P->WriteAO = wAO;
	P->WriteF0 = wF0;
	P->WriteFuzz = wFuzz;
	P->WriteSSS = wSSS;
	P->WriteNormal = wNormal;
	P->HasPreviousNormal = LayerCtx.LayerInputN ? 1u : 0u;
	P->NormalStrength = NormalStrength;
	P->PreviousNormal = LayerCtx.LayerInputN ? LayerCtx.LayerInputN : Ctx.EmptyDriverSignal;
	P->DetailNormal = Fields.DetailNormal ? Fields.DetailNormal : Ctx.EmptyDriverSignal;
	FRDGTextureRef N = MakeSurfaceTexture(Ctx, PF_FloatRGBA, TEXT("Mixtormat.Surfacing.AppliedNormal"));
	P->OutNormal = Ctx.GraphBuilder.CreateUAV(N);
	P->RunningHeight = RunningHeight;
	P->SkinHeight = Fields.Height;
	P->PreviousColor = LayerCtx.LayerInputBC;
	P->PreviousRAM = LayerCtx.LayerInputRAM;
	P->PreviousOptics = LayerCtx.LayerInputOptics;
	P->PreviousFuzzColor = LayerCtx.LayerInputFuzzColor;
	P->PreviousSubsurface = LayerCtx.LayerInputSubsurface;
	P->SkinColor = Fields.Color;
	P->SkinRAM = Fields.RAM;
	P->SkinOptics = Fields.Optics;
	P->SkinFuzzColor = Fields.FuzzColor;
	P->SkinSubsurface = Fields.Subsurface;
	P->OutHeight = Ctx.GraphBuilder.CreateUAV(H);
	P->OutColor = Ctx.GraphBuilder.CreateUAV(C);
	P->OutRAM = Ctx.GraphBuilder.CreateUAV(R);
	P->OutOptics = Ctx.GraphBuilder.CreateUAV(O);
	P->OutFuzzColor = Ctx.GraphBuilder.CreateUAV(F);
	P->OutSubsurface = Ctx.GraphBuilder.CreateUAV(M);
	DispatchSurface(Ctx, P, 1);
	if (wColor) { LayerCtx.LayerInputBC = C; LayerCtx.bSurfacingColor = true; }
	if (wRough || wMetal || wAO || wF0) { LayerCtx.LayerInputRAM = R; }
	if (wF0 || wFuzz || wSSS) { LayerCtx.LayerInputOptics = O; }
	if (wFuzz) { LayerCtx.LayerInputFuzzColor = F; }
	if (wSSS) { LayerCtx.LayerInputSubsurface = M; }
	if (wNormal) { LayerCtx.LayerInputN = N; }
	LayerCtx.bSurfacingRoughness |= wRough;
	LayerCtx.bSurfacingAO |= wAO;
	LayerCtx.bSurfacingF0 |= wF0;
	LayerCtx.bSurfacingFuzz |= wFuzz;
	LayerCtx.bSurfacingSSS |= wSSS;
	return bHeight ? H : RunningHeight;
}

void PublishSurfacingFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const FChildRenderData& Child, const FSurfacingFields& Raw,
	FRDGTextureRef Gate, FRDGTextureRef BeforeHeight, FRDGTextureRef AfterHeight, float AncestorAmount)
{
	if (Child.Surfacing.Type == EMixtormatSurfacingType::Wood)
	{
		PublishWoodFields(Ctx, LayerCtx, Layer, Child, Raw, Gate, BeforeHeight, AfterHeight, AncestorAmount);
		return;
	}
	check(LayerCtx.LayerInputBC && LayerCtx.LayerInputRAM && LayerCtx.LayerInputOptics && LayerCtx.LayerInputSubsurface);
	FRDGTextureRef Scalars[7];
	const FName Names[] = {FName(TEXT("Height")), FName(TEXT("Roughness")), FName(TEXT("AO")),
		FName(TEXT("F0")), FName(TEXT("Fuzz")), FName(TEXT("Pores")), FName(TEXT("Compression"))};
	for (FRDGTextureRef& Scalar : Scalars) { Scalar = MakeSurfaceTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Surfacing.Diagnostic")); }
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatSurfacingApplyCS::FParameters>();
	const bool bRust = Child.Surfacing.Type == EMixtormatSurfacingType::Rust;
		P->Amount = (bRust ? Child.Surfacing.Rust.Amount : Child.Surfacing.Skin.Amount) * AncestorAmount;
	P->HasGate = Gate ? 1u : 0u;
	P->Gate = Gate ? Gate : Ctx.EmptyDriverSignal;
	P->BeforeHeight = BeforeHeight;
	P->AfterHeight = AfterHeight;
	P->CompletedRAM = LayerCtx.LayerInputRAM;
	P->CompletedOptics = LayerCtx.LayerInputOptics;
	P->RawPores = bRust ? Raw.MaterialCoverage : Raw.Pores;
	P->RawCompression = bRust ? Raw.MaterialCoverage : Raw.Compression;
	P->OutHeightDelta = Ctx.GraphBuilder.CreateUAV(Scalars[0]);
	P->OutRoughness = Ctx.GraphBuilder.CreateUAV(Scalars[1]);
	P->OutAO = Ctx.GraphBuilder.CreateUAV(Scalars[2]);
	P->OutF0 = Ctx.GraphBuilder.CreateUAV(Scalars[3]);
	P->OutFuzz = Ctx.GraphBuilder.CreateUAV(Scalars[4]);
	P->OutPores = Ctx.GraphBuilder.CreateUAV(Scalars[5]);
	P->OutCompression = Ctx.GraphBuilder.CreateUAV(Scalars[6]);
	DispatchSurface(Ctx, P, 2);
	const auto Publish = [&](FName Name, EMixtormatPublishedFieldKind Kind, FRDGTextureRef Texture)
	{
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name),
			FPublishedField{Kind, Texture, nullptr, nullptr, false});
		const auto PreviewKind = Kind == EMixtormatPublishedFieldKind::Color ? EMixtormatPreviewOutputKind::Color
			: Kind == EMixtormatPublishedFieldKind::ScalarSigned ? EMixtormatPreviewOutputKind::SignedDistance
			: EMixtormatPreviewOutputKind::Mask;
		if (Kind == EMixtormatPublishedFieldKind::Scalar01)
		{
			Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name), Texture);
		}
		if (IsChildOutputPreviewTarget(Ctx.Request, PreviewKind, Name, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
			if (Kind == EMixtormatPublishedFieldKind::Color) { AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
			else if (Kind == EMixtormatPublishedFieldKind::ScalarSigned) { AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
			else { AddMaskOverlayPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
		}
	};
	if (bRust)
	{
		Publish(FName(TEXT("Height")), EMixtormatPublishedFieldKind::ScalarSigned, Scalars[0]);
		Publish(FName(TEXT("Color")), EMixtormatPublishedFieldKind::Color, LayerCtx.LayerInputBC);
		Publish(FName(TEXT("OxideCoverage")), EMixtormatPublishedFieldKind::Scalar01, Scalars[5]);
		LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Scalars[0]);
		return;
	}
	for (int32 I = 0; I < 7; ++I)
	{
		Publish(Names[I], I == 0 ? EMixtormatPublishedFieldKind::ScalarSigned : EMixtormatPublishedFieldKind::Scalar01, Scalars[I]);
	}
	LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Scalars[0]);
	Publish(FName(TEXT("Color")), EMixtormatPublishedFieldKind::Color, LayerCtx.LayerInputBC);
	// This name is intentionally NOT Color. Runtime CanReferenceField rejects SSS consumption.
	Publish(FName(TEXT("SSS")), EMixtormatPublishedFieldKind::Color, LayerCtx.LayerInputSubsurface);
}
}
