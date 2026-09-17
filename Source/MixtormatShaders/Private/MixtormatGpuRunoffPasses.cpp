// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Runoff: the procedural streak, and the cheap answer to the class of look Wet Stain reaches by
// solving transport.
//
// Its own translation unit rather than joining MixtormatGpuSimulationPasses.cpp, where Stain and
// Peeling live. Those two are there because they are iterative ping-ponged solves and share the
// problems that come with that -- state textures, capture semantics, which half is read on which
// iteration. Runoff has none of those. Filing it beside them would suggest it does, which is the
// one thing worth not implying about it.
//
// Two passes, both full resolution, no state carried between them beyond a single prepared field.

class FMixtormatRunoffCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRunoffCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRunoffCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, UseLayerMask)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, StrataCount)
		SHADER_PARAMETER(float, GravityAngle)
		SHADER_PARAMETER(float, StreakRadius)
		SHADER_PARAMETER(float, StreakSoftness)
		SHADER_PARAMETER(float, SurfaceInfluence)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, WarpScale)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, LipStrength)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FeatureMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RunoffSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputSource)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRunoffCS,
	"/Plugin/Mixtormat/Private/MixtormatRunoff.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// A mask child, for the same reason Stain is one: Runoff resolves the shape of where the
	// runoff ran into the layer's accumulated mask, and the layer it masks supplies every channel.
	// A rust streak is a rust material masked by a runoff, not a tint painted on afterwards.
	//
	// It also has to run here rather than after the composite, because the surface it reads is
	// the one accumulated underneath the layer -- which is the surface the runoff would actually
	// run down.
	void AddRunoffMaskPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatRunoffCS> RunoffShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		// Strength 0 is the identity from the second mask child onward, the rule every other mask
		// node follows. The first child cannot skip: it establishes the chain with Initialize,
		// where Previous is zero rather than the read half's white clear, and skipping would
		// leave the layer fully visible rather than untouched by runoff.
		if (MaskPassIndex > 0 && Effect.RunoffStrength <= 0.0f)
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;
		const int32 LayerReadIndex = 1 - (LayerIndex & 1);

		// The prepared source: coverage in x, the fractal field in y.
		//
		// Full resolution, unlike Stain's fixed solve grid. Stain needs a fixed grid because its
		// transport measures distance in texels per iteration, so the solve resolution decides how
		// far a run reaches. Runoff measures distance in UV and derives its tap count from the UV
		// reach, so it is already the same size at every composition -- there is nothing a reduced
		// grid would fix, and it would cost an upsample and a band of filtered detail.
		const FRDGTextureDesc SourceDesc = FRDGTextureDesc::Create2D(
			Request.Resolution,
			PF_G16R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef RunoffSource =
			GraphBuilder.CreateTexture(SourceDesc, TEXT("Mixtormat.RunoffSource"));

		// One-by-one stand-ins for the slots a given pass does not write. RDG validates every
		// binding whether or not the shader stores through it.
		const FRDGTextureDesc TinySourceDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_G16R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc TinyMaskDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc TinyDebugDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef SourceReadDummy = GraphBuilder.CreateTexture(
			TinySourceDesc, TEXT("Mixtormat.RunoffSourceReadDummy"));
		FRDGTextureRef SourceWriteDummy = GraphBuilder.CreateTexture(
			TinySourceDesc, TEXT("Mixtormat.RunoffSourceWriteDummy"));
		FRDGTextureRef MaskWriteDummy = GraphBuilder.CreateTexture(
			TinyMaskDesc, TEXT("Mixtormat.RunoffMaskDummy"));
		FRDGTextureRef DebugWriteDummy = GraphBuilder.CreateTexture(
			TinyDebugDesc, TEXT("Mixtormat.RunoffDebugDummy"));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(SourceReadDummy), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(SourceWriteDummy), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(MaskWriteDummy), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(DebugWriteDummy), FVector4f(0.0f));

		// The feature-preview eye on the Runoff group. Gated on the selected layer the way the
		// composite gates its own debug write, so two runoffs on different layers cannot fight
		// over one target. Only the resolve binds the shared debug target.
		const bool bWriteRunoffDebug =
			Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::Runoff
			&& Request.DebugSettings.LayerIndex == LayerIndex;

		// Both passes share every value and view; only the mode and the bindings that go with it
		// change. Filled once.
		FMixtormatRunoffCS::FParameters* CommonParameters =
			GraphBuilder.AllocParameters<FMixtormatRunoffCS::FParameters>();
		{
			FMixtormatRunoffCS::FParameters* P = CommonParameters;
			P->OutputSize = Request.Resolution;
			P->Initialize = MaskPassIndex == 0 ? 1u : 0u;

			// No relief underneath on the bottom layer, so the surface response has nothing to
			// read and falls back to neutral.
			P->SurfaceValid = LayerIndex > 0 ? 1u : 0u;

			// Only from the second mask child onward. The read half is cleared to white, so
			// trusting it on the first child would source runoff over the whole surface instead
			// of falling back to the surface's own cavities and ledges.
			P->UseLayerMask = MaskPassIndex > 0 ? 1u : 0u;
			P->WriteDebug = 0u;

			// Replace. Runoff exposes no blend mode of its own, for the same reason Stain does
			// not: it is the shape of a run, and a run either covers a texel or it does not.
			P->BlendMode = static_cast<uint32>(EMixtormatMaskBlendMode::Replace);
			P->Seed = Effect.RunoffSeed;
			P->StrataCount = Effect.RunoffStrataCount;
			P->GravityAngle = Effect.RunoffGravityAngle;
			P->StreakRadius = Effect.RunoffStreakRadius;
			P->StreakSoftness = Effect.RunoffStreakSoftness;
			P->SurfaceInfluence = Effect.RunoffSurfaceInfluence;
			P->StrataAmount = Effect.RunoffStrataAmount;
			P->WarpScale = Effect.RunoffWarpScale;
			P->WarpAmount = Effect.RunoffWarpAmount;
			P->LipStrength = Effect.RunoffLipStrength;
			P->Strength = Effect.RunoffStrength;

			// The height accumulated below this layer, the same surface a generated mask reads.
			P->SourceHeight = HeightTargets[LayerReadIndex];
			P->PreviousMask = MaskTargets[MaskReadIndex];
			P->FeatureMask = FeatureMask;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

			// Overridden per pass, bound here so no pass can leave a slot empty.
			P->Mode = 0;
			P->RunoffSource = SourceReadDummy;
			P->OutputSource = GraphBuilder.CreateUAV(SourceWriteDummy);
			P->OutputMask = GraphBuilder.CreateUAV(MaskWriteDummy);
			P->OutputDebug = GraphBuilder.CreateUAV(DebugWriteDummy);
		}

		const FIntVector RunoffGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		// Pass one: surface response, fractal noise, source field. The one pass that writes the
		// source half, so the one pass that cannot also read it.
		FMixtormatRunoffCS::FParameters* Prepare =
			GraphBuilder.AllocParameters<FMixtormatRunoffCS::FParameters>();
		*Prepare = *CommonParameters;
		Prepare->Mode = 0;
		Prepare->RunoffSource = SourceReadDummy;
		Prepare->OutputSource = GraphBuilder.CreateUAV(RunoffSource);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Runoff.L%d.C%d.Prepare", LayerIndex, ChildIndex),
			RunoffShader,
			Prepare,
			RunoffGroups);

		// Pass two: the directional Gaussian, the strata, the warp and the lip, into the layer's
		// mask chain.
		FMixtormatRunoffCS::FParameters* Resolve =
			GraphBuilder.AllocParameters<FMixtormatRunoffCS::FParameters>();
		*Resolve = *CommonParameters;
		Resolve->Mode = 1;
		Resolve->WriteDebug = bWriteRunoffDebug ? 1u : 0u;
		Resolve->RunoffSource = RunoffSource;
		Resolve->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);
		Resolve->OutputDebug = GraphBuilder.CreateUAV(
			OutputDebug[Request.PublishedTargetIndex]);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Runoff.L%d.C%d.Resolve", LayerIndex, ChildIndex),
			RunoffShader,
			Resolve,
			RunoffGroups);

		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugRunoffSnapshot = GraphBuilder.CreateTexture(
				MaskDesc, TEXT("Mixtormat.DebugRunoffSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugRunoffSnapshot);
			DebugMask = DebugRunoffSnapshot;
		}
		++MaskPassIndex;
	}
}
