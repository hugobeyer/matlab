// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "MixtormatEffectPassesPrivate.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

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
		SHADER_PARAMETER(uint32, WriteRAM)
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

class FMixtormatEffectMergeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatEffectMergeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatEffectMergeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousEffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputEffectData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatEffectMergeCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowWarp.usf",
	"EffectMergeCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCarveShadeCS,
	"/Plugin/Mixtormat/Private/MixtormatCarveShade.usf",
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
		const bool bWriteRAM,
		const TCHAR* DebugName)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		TShaderMapRef<FMixtormatHeightDeltaNormalCS> HeightDeltaNormalShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatHeightDeltaNormalCS::FParameters* P =
			GraphBuilder.AllocParameters<FMixtormatHeightDeltaNormalCS::FParameters>();
		P->OutputSize = Resolution;
		// With the final normal on, relief comes from the final height alone: a zero gradient
		// makes the reorientation return PreviousNormal unchanged, so every caller passes through.
		P->NormalStrength = Ctx.Request.bFinalNormalFromHeight ? 0.0f : NormalStrength;
		P->WriteRAM = bWriteRAM ? 1u : 0u;
		P->PreviousHeight = PreviousHeight;
		P->CurrentHeight = CurrentHeight;
		P->PreviousNormal = PreviousNormal;
		P->PreviousRAM = PreviousRAM;
		P->OutputNormal = GraphBuilder.CreateUAV(OutputNormal);
		if (!OutputRAM)
		{
			FRDGTextureDesc DummyDesc = PreviousRAM->Desc;
			DummyDesc.Extent = FIntPoint(1, 1);
			OutputRAM = GraphBuilder.CreateTexture(
				DummyDesc, TEXT("Mixtormat.HeightDerivedRAMDummy"));
		}
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

	void AddEffectContributionPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef EffectData,
		FRDGTextureRef EffectHeight)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const int32 WriteIndex = LayerCtx.EffectPassIndex & 1;
		const int32 ReadIndex = 1 - WriteIndex;
		FMixtormatEffectMergeCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatEffectMergeCS::FParameters>();
		Parameters->OutputSize = Ctx.Request.Resolution;
		Parameters->Initialize = LayerCtx.EffectPassIndex == 0 ? 1u : 0u;
		Parameters->PreviousEffectData = LayerCtx.EffectTargets[ReadIndex];
		Parameters->PreviousEffectHeight = LayerCtx.EffectHeightTargets[ReadIndex];
		Parameters->SourceEffectData = EffectData;
		Parameters->SourceHeight = EffectHeight;
		Parameters->OutputEffectData = GraphBuilder.CreateUAV(LayerCtx.EffectTargets[WriteIndex]);
		Parameters->OutputHeight = GraphBuilder.CreateUAV(LayerCtx.EffectHeightTargets[WriteIndex]);
		TShaderMapRef<FMixtormatEffectMergeCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.EffectMerge.L%d.Owner%d",
				LayerCtx.LayerIndex,
				OwnerSourceChildIndex),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Ctx.Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Ctx.Request.Resolution.Y, 8),
				1));
		LayerCtx.CombinedEffectData = LayerCtx.EffectTargets[WriteIndex];
		LayerCtx.CombinedEffectHeight = LayerCtx.EffectHeightTargets[WriteIndex];
		++LayerCtx.EffectPassIndex;
	}

}
