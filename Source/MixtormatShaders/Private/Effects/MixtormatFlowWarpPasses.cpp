// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Flow Warp has one field implementation and target-specific entry points. Keeping distinct
// parameter structs prevents a mask or effect contribution from pretending to be a surface.
class FMixtormatFlowWarpSurfaceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFlowWarpSurfaceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFlowWarpSurfaceCS, FGlobalShader);

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
	FMixtormatFlowWarpSurfaceCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowWarp.usf",
	"SurfaceCS",
	SF_Compute);

class FMixtormatFlowWarpMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFlowWarpMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFlowWarpMaskCS, FGlobalShader);

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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFlowWarpMaskCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowWarp.usf",
	"MaskCS",
	SF_Compute);

class FMixtormatFlowWarpEffectCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFlowWarpEffectCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFlowWarpEffectCS, FGlobalShader);

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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputEffectData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFlowWarpEffectCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowWarp.usf",
	"EffectCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	template <typename TParameters>
	void FillFlowWarpParameters(
		TParameters* Parameters,
		const FRenderRequest& Request,
		const FMixtormatLayerPassContext& LayerCtx,
		const FEffectRenderData& Flow,
		FRDGTextureRef FeatureMask)
	{
		const bool bHasMask = FeatureMask
			&& FeatureMask->Desc.Format == LayerCtx.MaskDesc.Format
			&& FeatureMask->Desc.Extent == LayerCtx.MaskDesc.Extent;
		Parameters->OutputSize = Request.Resolution;
		Parameters->HasMask = bHasMask ? 1u : 0u;
		Parameters->Amount = Flow.FlowWarpAmount;
		Parameters->EffectWeight = Flow.FlowWarpWeight;
		Parameters->Scale = Flow.FlowWarpScale;
		Parameters->Direction = Flow.FlowWarpDirection;
		Parameters->Seed = Flow.FlowWarpSeed;
		Parameters->MaskSlopeInfluence = Flow.FlowWarpMaskSlopeInfluence;
		Parameters->HeightSlopeInfluence = Flow.FlowWarpHeightSlopeInfluence;
		Parameters->DerivativeKernel = Flow.FlowWarpDerivativeKernel;
		Parameters->BlendMode = Flow.FlowWarpBlendMode;
		Parameters->LayerMask = FeatureMask;
		Parameters->LinearWrapSampler = TStaticSamplerState<
			SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
	}

	bool HasOwnedFlowWarps(
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex)
	{
		return Layer.Children.ContainsByPredicate(
			[OwnerSourceChildIndex](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Effect
					&& Candidate.Effect.Type == EMixtormatEffectType::FlowWarp
					&& Candidate.ScopeOwnerSourceChildIndex == OwnerSourceChildIndex;
			});
	}

	void AddLayerFlowWarpPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Flow,
		FRDGTextureRef FeatureMask)
	{
		if (Child.ScopeOwnerSourceChildIndex != INDEX_NONE
			|| FMath::IsNearlyZero(Flow.FlowWarpAmount)
			|| FMath::IsNearlyZero(Flow.FlowWarpWeight))
		{
			return;
		}

		AddLayerInputPass(Ctx, LayerCtx, Layer);
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		FRDGTextureRef WarpedBC = GraphBuilder.CreateTexture(
			LayerCtx.LayerInputBC->Desc, TEXT("Mixtormat.LayerFlowWarpBC"));
		FRDGTextureRef WarpedN = GraphBuilder.CreateTexture(
			LayerCtx.LayerInputN->Desc, TEXT("Mixtormat.LayerFlowWarpN"));
		FRDGTextureRef WarpedRAM = GraphBuilder.CreateTexture(
			LayerCtx.LayerInputRAM->Desc, TEXT("Mixtormat.LayerFlowWarpRAM"));
		FRDGTextureRef WarpedHeight = GraphBuilder.CreateTexture(
			LayerCtx.LayerInputHeight->Desc, TEXT("Mixtormat.LayerFlowWarpHeight"));
		FMixtormatFlowWarpSurfaceCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatFlowWarpSurfaceCS::FParameters>();
		FillFlowWarpParameters(Parameters, Ctx.Request, LayerCtx, Flow, FeatureMask);
		Parameters->SourceBC = LayerCtx.LayerInputBC;
		Parameters->SourceN = LayerCtx.LayerInputN;
		Parameters->SourceRAM = LayerCtx.LayerInputRAM;
		Parameters->SourceHeight = LayerCtx.LayerInputHeight;
		Parameters->OutputBC = GraphBuilder.CreateUAV(WarpedBC);
		Parameters->OutputN = GraphBuilder.CreateUAV(WarpedN);
		Parameters->OutputRAM = GraphBuilder.CreateUAV(WarpedRAM);
		Parameters->OutputHeight = GraphBuilder.CreateUAV(WarpedHeight);
		TShaderMapRef<FMixtormatFlowWarpSurfaceCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.FlowWarp.LayerInput.L%d.C%d",
				LayerCtx.LayerIndex,
				Child.SourceChildIndex),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Ctx.Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Ctx.Request.Resolution.Y, 8),
				1));
		LayerCtx.LayerInputBC = WarpedBC;
		LayerCtx.LayerInputN = WarpedN;
		LayerCtx.LayerInputRAM = WarpedRAM;
		LayerCtx.LayerInputHeight = WarpedHeight;
	}

	FRDGTextureRef AddOwnedMaskFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef SourceMask)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		TShaderMapRef<FMixtormatFlowWarpMaskCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRDGTextureRef CurrentMask = SourceMask;
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Effect
				|| Child.Effect.Type != EMixtormatEffectType::FlowWarp
				|| Child.ScopeOwnerSourceChildIndex != OwnerSourceChildIndex)
			{
				continue;
			}
			const FEffectRenderData& Flow = Child.Effect;
			if (FMath::IsNearlyZero(Flow.FlowWarpAmount)
				|| FMath::IsNearlyZero(Flow.FlowWarpWeight))
			{
				continue;
			}
			FRDGTextureRef FeatureMask =
				AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex);
			FRDGTextureRef WarpedMask = GraphBuilder.CreateTexture(
				CurrentMask->Desc, TEXT("Mixtormat.MaskFlowWarp"));
			FMixtormatFlowWarpMaskCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatFlowWarpMaskCS::FParameters>();
			FillFlowWarpParameters(Parameters, Ctx.Request, LayerCtx, Flow, FeatureMask);
			Parameters->SourceMask = CurrentMask;
			Parameters->SourceHeight = CurrentMask;
			Parameters->OutputMask = GraphBuilder.CreateUAV(WarpedMask);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.FlowWarp.Mask.L%d.Owner%d.C%d",
					LayerCtx.LayerIndex,
					OwnerSourceChildIndex,
					Child.SourceChildIndex),
				Shader,
				Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(Ctx.Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Ctx.Request.Resolution.Y, 8),
					1));
			CurrentMask = WarpedMask;
		}
		return CurrentMask;
	}

	void AddOwnedEffectFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef& EffectData,
		FRDGTextureRef& EffectHeight)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		TShaderMapRef<FMixtormatFlowWarpEffectCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Effect
				|| Child.Effect.Type != EMixtormatEffectType::FlowWarp
				|| Child.ScopeOwnerSourceChildIndex != OwnerSourceChildIndex)
			{
				continue;
			}
			const FEffectRenderData& Flow = Child.Effect;
			if (FMath::IsNearlyZero(Flow.FlowWarpAmount)
				|| FMath::IsNearlyZero(Flow.FlowWarpWeight))
			{
				continue;
			}
			FRDGTextureRef FeatureMask =
				AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex);
			FRDGTextureRef WarpedData = GraphBuilder.CreateTexture(
				EffectData->Desc, TEXT("Mixtormat.EffectFlowWarpData"));
			FRDGTextureRef WarpedHeight = GraphBuilder.CreateTexture(
				EffectHeight->Desc, TEXT("Mixtormat.EffectFlowWarpHeight"));
			FMixtormatFlowWarpEffectCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatFlowWarpEffectCS::FParameters>();
			FillFlowWarpParameters(Parameters, Ctx.Request, LayerCtx, Flow, FeatureMask);
			Parameters->SourceEffectData = EffectData;
			Parameters->SourceHeight = EffectHeight;
			Parameters->OutputEffectData = GraphBuilder.CreateUAV(WarpedData);
			Parameters->OutputHeight = GraphBuilder.CreateUAV(WarpedHeight);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.FlowWarp.Effect.L%d.Owner%d.C%d",
					LayerCtx.LayerIndex,
					OwnerSourceChildIndex,
					Child.SourceChildIndex),
				Shader,
				Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(Ctx.Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Ctx.Request.Resolution.Y, 8),
					1));
			EffectData = WarpedData;
			EffectHeight = WarpedHeight;
		}
	}

}
