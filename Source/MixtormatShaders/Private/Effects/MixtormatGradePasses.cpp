// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

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

namespace MixtormatGpuCompositor
{
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
		// Grade runs after erosion and Breakup, so it grades the final weathered
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
