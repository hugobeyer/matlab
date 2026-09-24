// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

class FMixtormatSurfaceIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatSurfaceIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatSurfaceIdsCS, FGlobalShader);

	class FStage : SHADER_PERMUTATION_INT("SURFACE_ID_STAGE", 8);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FVector2f, SourceTiling)
		SHADER_PARAMETER(FVector2f, SourceOffset)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(uint32, SourceMode)
		SHADER_PARAMETER(uint32, HasSeparateHeight)
		SHADER_PARAMETER(uint32, PrimaryFeature)
		SHADER_PARAMETER(uint32, SecondaryFeature)
		SHADER_PARAMETER(float, FeatureMix)
		SHADER_PARAMETER(int32, FormScale)
		SHADER_PARAMETER(int32, GuideBlur)
		SHADER_PARAMETER(uint32, MaxIds)
		SHADER_PARAMETER(int32, EdgeClose)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAMH)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, FeatureGuide)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, DilatedGuide)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, InputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputGuide)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Statistics)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CompactIds)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatSurfaceIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatSurfaceIds.usf", "MainCS", SF_Compute);

namespace MixtormatGpuCompositor
{
	FRDGTextureRef AddSurfaceIdPasses(
		FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 LayerIndex,
		const bool bWriteDebug)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FClusterFilterRenderData& Settings = Child.Filter;
		const int32 ReadIndex = 1 - (LayerIndex & 1);
		const bool bComposite = Settings.Source == EMixtormatClusterSource::CompositeBelow && LayerIndex > 0;
		const auto SourceTexture = [&Ctx](const FTextureRHIRef& Texture, const TCHAR* Name)
		{
			return RegisterTexture(Ctx.GraphBuilder, Ctx.RegisteredTextures, Texture, Name);
		};
		FRDGTextureRef RAM = bComposite ? Ctx.OutputRAM[ReadIndex]
			: SourceTexture(Layer.RAM, TEXT("Mixtormat.SurfaceIds.SourceRAMH"));
		FRDGTextureRef Height = bComposite ? Ctx.OutputHeight[ReadIndex]
			: (Layer.Height.IsValid() ? SourceTexture(Layer.Height, TEXT("Mixtormat.SurfaceIds.SourceHeight"))
				: Ctx.OutputHeight[ReadIndex]);
		FRDGTextureRef Normal = bComposite ? Ctx.OutputN[ReadIndex]
			: SourceTexture(Layer.Normal, TEXT("Mixtormat.SurfaceIds.SourceNormal"));
		FRDGTextureRef Color = bComposite ? Ctx.OutputBC[ReadIndex]
			: SourceTexture(Layer.BaseColor, TEXT("Mixtormat.SurfaceIds.SourceColor"));

		const auto Texture = [&GraphBuilder, Size](EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Format,
				FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef RawSamples = Texture(PF_FloatRGBA, TEXT("Mixtormat.SurfaceIds.Horizontal"));
		FRDGTextureRef SmoothedSamples = Settings.GuideBlur > 0
			? Texture(PF_FloatRGBA, TEXT("Mixtormat.SurfaceIds.Smoothed")) : RawSamples;
		FRDGTextureRef Guide = Texture(PF_G32R32F, TEXT("Mixtormat.SurfaceIds.Features"));
		FRDGTextureRef Dilated = Settings.EdgeClose > 0
			? Texture(PF_G32R32F, TEXT("Mixtormat.SurfaceIds.Dilated")) : Guide;
		FRDGTextureRef Quantized = Texture(PF_R32_UINT, TEXT("Mixtormat.SurfaceIds.Bands"));
		FRDGTextureRef Cleaned = Texture(PF_R32_UINT, TEXT("Mixtormat.SurfaceIds.Cleaned"));
		FRDGTextureRef Result = Texture(PF_R32_UINT, TEXT("Mixtormat.SurfaceIds.RegionIds"));
		FRDGBufferRef Statistics = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 12), TEXT("Mixtormat.SurfaceIds.Statistics"));
		FRDGBufferRef CompactIds = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 256), TEXT("Mixtormat.SurfaceIds.CompactIds"));
		const FRDGBufferUAVRef StatisticsUAV = GraphBuilder.CreateUAV(Statistics);
		AddClearUAVPass(GraphBuilder, StatisticsUAV, 0u);

		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		for (int32 Stage = 0; Stage < 8; ++Stage)
		{
			if ((Stage == 1 && Settings.GuideBlur == 0) || (Stage == 3 && Settings.EdgeClose == 0))
			{
				continue;
			}
			FMixtormatSurfaceIdsCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatSurfaceIdsCS::FStage>(Stage);
			TShaderMapRef<FMixtormatSurfaceIdsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* Parameters = GraphBuilder.AllocParameters<FMixtormatSurfaceIdsCS::FParameters>();
			Parameters->OutputSize = Size;
			Parameters->SourceTiling = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
			Parameters->SourceOffset = Layer.UVOffset;
			Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
			Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
			Parameters->Rotation = Layer.Rotation;
			Parameters->SourceMode = bComposite ? 1u : 0u;
			Parameters->HasSeparateHeight = bComposite || Layer.SourceOutputs.IsValid() ? 1u : 0u;
			// At either endpoint only sample the selected feature, including its required maps.
			Parameters->PrimaryFeature = static_cast<uint32>(Settings.FeatureMix >= 1.0f
				? Settings.SecondaryFeature : Settings.PrimaryFeature);
			Parameters->SecondaryFeature = static_cast<uint32>(Settings.SecondaryFeature);
			Parameters->FeatureMix = Settings.FeatureMix >= 1.0f ? 0.0f : Settings.FeatureMix;
			Parameters->FormScale = Settings.FormScale;
			Parameters->GuideBlur = Settings.GuideBlur;
			Parameters->MaxIds = static_cast<uint32>(Settings.MaxIds);
			Parameters->EdgeClose = Settings.EdgeClose;
			Parameters->WriteDebug = bWriteDebug ? 1u : 0u;
			Parameters->SourceRAMH = RAM;
			Parameters->SourceHeight = Height;
			Parameters->SourceNormal = Normal;
			Parameters->SourceColor = Color;
			Parameters->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			Parameters->InputSamples = Stage == 1 ? RawSamples : SmoothedSamples;
			Parameters->FeatureGuide = Guide;
			Parameters->DilatedGuide = Dilated;
			Parameters->InputIds = Stage == 5 ? Quantized : Cleaned;
			Parameters->OutputSamples = GraphBuilder.CreateUAV(Stage == 0 ? RawSamples : SmoothedSamples);
			Parameters->OutputGuide = GraphBuilder.CreateUAV(Stage == 2 ? Guide : Dilated);
			Parameters->OutputIds = GraphBuilder.CreateUAV(Stage == 4 ? Quantized : (Stage == 5 ? Cleaned : Result));
			Parameters->OutputDebug = GraphBuilder.CreateUAV(Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex]);
			Parameters->Statistics = StatisticsUAV;
			Parameters->CompactIds = GraphBuilder.CreateUAV(CompactIds);
			// Stage permutations strip unused bindings. Clear them so RDG tracks only real
			// dependencies, never an unwritten future texture or a spurious read/write alias.
			ClearUnusedGraphResources(Shader, Parameters);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.SurfaceIds.Layer%d.Child%d.Stage%d", LayerIndex, Child.SourceChildIndex, Stage),
				Shader, Parameters, Stage == 6 ? FIntVector(1, 1, 1) : Groups);
		}
		return Result;
	}
}
