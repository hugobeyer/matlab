// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuSurfacingPasses.h"

#include "MixtormatGpuCompositorInternal.h"
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
			SHADER_PARAMETER(int32, DetailTiling)
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
			SHADER_PARAMETER(uint32, WriteHeight)
			SHADER_PARAMETER(uint32, WriteColor)
			SHADER_PARAMETER(uint32, WriteRoughness)
			SHADER_PARAMETER(uint32, WriteAO)
			SHADER_PARAMETER(uint32, WriteF0)
			SHADER_PARAMETER(uint32, WriteFuzz)
			SHADER_PARAMETER(uint32, WriteSSS)
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
	P->DetailTiling = Skin.DetailTiling;
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
	const FVector2f PlacementScale = P->GeneratorUVScale;
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
	const FMixtormatSkin& S = Child.Surfacing.Skin;
	const float Amount = S.Amount * AncestorAmount;
	const bool bHeight = S.bWriteHeight && S.HeightScale != 0.0f;
	if (!Fields.Height || Amount <= 0.0f
		|| !(bHeight || S.bWriteColor || S.bWriteRoughness || S.bWriteAO || S.bWriteF0 || S.bWriteFuzz || S.bWriteSSS))
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
	P->WriteHeight = bHeight;
	P->WriteColor = S.bWriteColor;
	P->WriteRoughness = S.bWriteRoughness;
	P->WriteAO = S.bWriteAO;
	P->WriteF0 = S.bWriteF0;
	P->WriteFuzz = S.bWriteFuzz;
	P->WriteSSS = S.bWriteSSS;
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
	if (S.bWriteColor) { LayerCtx.LayerInputBC = C; LayerCtx.bSurfacingColor = true; }
	if (S.bWriteRoughness || S.bWriteAO || S.bWriteF0) { LayerCtx.LayerInputRAM = R; }
	if (S.bWriteF0 || S.bWriteFuzz || S.bWriteSSS) { LayerCtx.LayerInputOptics = O; }
	if (S.bWriteFuzz) { LayerCtx.LayerInputFuzzColor = F; }
	if (S.bWriteSSS) { LayerCtx.LayerInputSubsurface = M; }
	LayerCtx.bSurfacingRoughness |= S.bWriteRoughness;
	LayerCtx.bSurfacingAO |= S.bWriteAO;
	LayerCtx.bSurfacingF0 |= S.bWriteF0;
	LayerCtx.bSurfacingFuzz |= S.bWriteFuzz;
	LayerCtx.bSurfacingSSS |= S.bWriteSSS;
	return bHeight ? H : RunningHeight;
}

void PublishSurfacingFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const FChildRenderData& Child, const FSurfacingFields& Raw,
	FRDGTextureRef Gate, FRDGTextureRef BeforeHeight, FRDGTextureRef AfterHeight, float AncestorAmount)
{
	check(LayerCtx.LayerInputBC && LayerCtx.LayerInputRAM && LayerCtx.LayerInputOptics && LayerCtx.LayerInputSubsurface);
	FRDGTextureRef Scalars[7];
	const FName Names[] = {FName(TEXT("Height")), FName(TEXT("Roughness")), FName(TEXT("AO")),
		FName(TEXT("F0")), FName(TEXT("Fuzz")), FName(TEXT("Pores")), FName(TEXT("Compression"))};
	for (FRDGTextureRef& Scalar : Scalars) { Scalar = MakeSurfaceTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Surfacing.Diagnostic")); }
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatSurfacingApplyCS::FParameters>();
	P->Amount = Child.Surfacing.Skin.Amount * AncestorAmount;
	P->HasGate = Gate ? 1u : 0u;
	P->Gate = Gate ? Gate : Ctx.EmptyDriverSignal;
	P->BeforeHeight = BeforeHeight;
	P->AfterHeight = AfterHeight;
	P->CompletedRAM = LayerCtx.LayerInputRAM;
	P->CompletedOptics = LayerCtx.LayerInputOptics;
	P->RawPores = Raw.Pores;
	P->RawCompression = Raw.Compression;
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
