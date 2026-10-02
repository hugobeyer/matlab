// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// The layer-level counterpart to the mask blur: separable, wrap-sampled, one axis per dispatch,
// but over a float4 surface target rather than a single-channel mask.
class FMixtormatLayerBlurCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatLayerBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatLayerBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Axis)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(uint32, UseLayerCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerCoverage)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(uint32, RenormalizeXYZ)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatLayerBlurCS,
	"/Plugin/Mixtormat/Private/MixtormatLayerBlur.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Grade is a post-layer filter over the accumulated base colour.
	void QueuePendingLayerBlur(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingLayerBlurs = LayerCtx.PendingLayerBlurs;

		// Deferred for the same reason a grade is: it softens what the stack has accumulated, and
		// running it inside the child loop would blur a surface the layer then overwrites.
		FPendingEffect& LayerBlur = PendingLayerBlurs.AddDefaulted_GetRef();
		LayerBlur.Effect = &Effect;
		LayerBlur.FeatureMask = FeatureMask;
		LayerBlur.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// Last of the filters, so it softens the finished surface rather than one that erosion, a
	// ramp relief or a grade was still about to change.
	void AddLayerBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingLayerBlurs = LayerCtx.PendingLayerBlurs;
		TShaderMapRef<FMixtormatLayerBlurCS> BlurShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		for (int32 BlurIndex = 0; BlurIndex < PendingLayerBlurs.Num(); ++BlurIndex)
		{
			const FPendingEffect& Pending = PendingLayerBlurs[BlurIndex];
			const FEffectRenderData& Blur = *Pending.Effect;

			// The shader's own contract at Amount 0 is to return what it read, so honour it here
			// rather than paying full-resolution passes to reproduce the input.
			const bool bAxis[2] = {Blur.LayerBlurRadiusX > 0.0f, Blur.LayerBlurRadiusY > 0.0f};
			if (Blur.LayerBlurAmount <= 0.0f || (!bAxis[0] && !bAxis[1]))
			{
				continue;
			}

			// Layer scope keeps the layer's own coverage in the blend so nothing outside this layer
			// moves; Composite scope drops it and softens everything the stack has reached. A scoped
			// mask gates either, which is what turns the second one into a lens blur over a region.
			const bool bLayerScope =
				Blur.LayerBlurScope == static_cast<uint32>(EMixtormatLayerBlurScope::Layer);
			const bool bUseMask = Pending.bHasScopedMask;

			// Every surface target the layer writes. Height is optional because softening it changes
			// what the surface is -- displacement, the height blend, derived normals -- rather than
			// only how it looks.
			FRDGTextureRef Targets[4] = {
				Ctx.OutputBC[WriteIndex],
				Ctx.OutputN[WriteIndex],
				Ctx.OutputRAM[WriteIndex],
				Blur.bLayerBlurHeight ? Ctx.OutputHeight[WriteIndex] : nullptr};
			const TCHAR* TargetNames[4] = {
				TEXT("Mixtormat.LayerBlurBC"),
				TEXT("Mixtormat.LayerBlurN"),
				TEXT("Mixtormat.LayerBlurRAM"),
				TEXT("Mixtormat.LayerBlurHeight")};

			for (int32 TargetIndex = 0; TargetIndex < 4; ++TargetIndex)
			{
				FRDGTextureRef Target = Targets[TargetIndex];
				if (!Target)
				{
					continue;
				}
				FRDGTextureRef Source = Target;
				const int32 LastAxis = bAxis[1] ? 1 : 0;
				for (int32 BlurAxis = 0; BlurAxis < 2; ++BlurAxis)
				{
					if (!bAxis[BlurAxis])
					{
						continue;
					}
					// With both axes active, the first writes scratch and the second writes
					// straight back to Target. This removes both full-image copy passes.
					const bool bCanWriteTarget = BlurAxis == LastAxis && Source != Target;
					FRDGTextureRef Destination = bCanWriteTarget
						? Target
						: GraphBuilder.CreateTexture(Target->Desc, TargetNames[TargetIndex]);
					FMixtormatLayerBlurCS::FParameters* BP =
						GraphBuilder.AllocParameters<FMixtormatLayerBlurCS::FParameters>();
					BP->OutputSize = Request.Resolution;
					BP->Axis = BlurAxis;
					BP->Radius = BlurAxis == 0 ? Blur.LayerBlurRadiusX : Blur.LayerBlurRadiusY;
					BP->UseLayerCoverage = bLayerScope && Layer.bHasMask ? 1u : 0u;
					BP->LayerCoverage = LayerCtx.CombinedMask;
					BP->UseMask = bUseMask ? 1u : 0u;
					BP->InvertMask = 0u;
					BP->Amount = Blur.LayerBlurAmount;
					BP->RenormalizeXYZ = TargetIndex == 1 ? 1u : 0u;
					BP->SourceTexture = Source;
					BP->LayerMask = Pending.FeatureMask;
					BP->LinearWrapSampler =
						TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
					BP->OutputTexture = GraphBuilder.CreateUAV(Destination);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.LayerBlur.Layer%d.%d.Target%d.Axis%d",
							LayerIndex, BlurIndex, TargetIndex, BlurAxis),
						BlurShader,
						BP,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));
					Source = Destination;
				}
				if (Source != Target)
				{
					AddCopyTexturePass(GraphBuilder, Source, Target);
				}
			}
		}
	}

}
