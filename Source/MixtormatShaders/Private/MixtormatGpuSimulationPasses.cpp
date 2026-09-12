// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// The two iterative solves.
//
// Wet Stain ping-pongs a pair of state textures over StainIterations and resolves into the
// layer mask; Procedural Peeling solves an eikonal front at reduced resolution and resolves
// it up. Both own their state transients outright: nothing outside these functions reads
// them, and the capture semantics -- which half is read on which iteration, and what the
// resolve binds -- are exactly as they were in the single-file compositor.

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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
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
	"/Plugin/Mixtormat/Private/MixtormatPeeling.usf",
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

		SHADER_PARAMETER(int32, MacroPeriod)
		SHADER_PARAMETER(int32, MicroPeriod)
		SHADER_PARAMETER(float, NoiseWeight)
		SHADER_PARAMETER(float, SizeVariation)
		SHADER_PARAMETER(int32, FlakeCells)
		SHADER_PARAMETER(float, ClusterAmount)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, WarpSource)

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
	"/Plugin/Mixtormat/Private/MixtormatPeelField.usf",
	"MainCS",
	SF_Compute);

class FMixtormatStainCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStainCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStainCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SurfaceSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(int32, StainMode)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, UseSourceMask)
		SHADER_PARAMETER(uint32, UseLayerMask)
		SHADER_PARAMETER(uint32, UseDirtMask)
		SHADER_PARAMETER(uint32, InvertSourceMask)
		SHADER_PARAMETER(uint32, InvertDirtMask)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, SourceMaskTiling)
		SHADER_PARAMETER(float, DirtMaskTiling)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, SourceAmount)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER(float, SurfaceFollow)
		SHADER_PARAMETER(float, Spread)
		SHADER_PARAMETER(float, Accumulation)
		SHADER_PARAMETER(float, Absorption)
		SHADER_PARAMETER(float, Drying)
		SHADER_PARAMETER(float, DirtAmount)
		SHADER_PARAMETER(float, ConcavityWeight)
		SHADER_PARAMETER(float, ConvexityWeight)
		SHADER_PARAMETER(float, OcclusionWeight)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, SlopeWeight)
		SHADER_PARAMETER(float, SurfaceResponse)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousStateA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousStateB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FeatureMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DirtMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputStateA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputStateB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStainCS,
	"/Plugin/Mixtormat/Private/MixtormatStain.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Wet Stain: an iterative solve whose state ping-pongs across StainIterations, resolved
	// into the layer's mask chain. A mask child rather than a post-layer filter, because the
	// surface it reads is the one accumulated underneath the layer.
	void AddStainMaskPasses(
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
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatStainCS> StainShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		// A mask child, not a post-layer filter. Stain resolves the shape of
		// where liquid ran into the layer's accumulated mask, and the layer it
		// masks supplies every channel -- a rust streak is a rust material
		// masked by a stain, not a tint the filter paints on afterwards.
		//
		// That is also why it belongs here rather than after the composite:
		// the surface it reads is the one accumulated underneath the layer,
		// which is the surface the liquid would actually run over.

		// Weight 0 is the identity from the second mask child onward, the same
		// rule every other mask node follows. The first child cannot skip: it
		// establishes the chain with Initialize, where Previous is zero rather
		// than the half's white clear, and skipping would leave the layer
		// fully visible instead of unstained.
		if (MaskPassIndex > 0
			&& (Effect.Strength <= 0.0f || Effect.StainSourceAmount <= 0.0f))
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;
		const int32 LayerReadIndex = 1 - (LayerIndex & 1);

		const FRDGTextureDesc StateDesc = FRDGTextureDesc::Create2D(
			Request.Resolution,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef StateA[2] = {
			GraphBuilder.CreateTexture(StateDesc, TEXT("Mixtormat.StainStateA0")),
			GraphBuilder.CreateTexture(StateDesc, TEXT("Mixtormat.StainStateA1"))};
		FRDGTextureRef StateB[2] = {
			GraphBuilder.CreateTexture(StateDesc, TEXT("Mixtormat.StainStateB0")),
			GraphBuilder.CreateTexture(StateDesc, TEXT("Mixtormat.StainStateB1"))};

		// One-by-one stand-ins for the slots a given pass does not write. RDG
		// validates every binding whether or not the shader stores through it.
		const FRDGTextureDesc TinyStateDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc TinyMaskDesc = FRDGTextureDesc::Create2D(
			FIntPoint(1, 1),
			PF_R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef StateReadDummy = GraphBuilder.CreateTexture(
			TinyStateDesc, TEXT("Mixtormat.StainReadDummy"));
		FRDGTextureRef StateWriteDummyA = GraphBuilder.CreateTexture(
			TinyStateDesc, TEXT("Mixtormat.StainWriteDummyA"));
		FRDGTextureRef StateWriteDummyB = GraphBuilder.CreateTexture(
			TinyStateDesc, TEXT("Mixtormat.StainWriteDummyB"));
		FRDGTextureRef StainMaskDummy = GraphBuilder.CreateTexture(
			TinyMaskDesc, TEXT("Mixtormat.StainMaskDummy"));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(StateReadDummy), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(StateWriteDummyA), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(StateWriteDummyB), FVector4f(0.0f));
		AddClearUAVPass(
			GraphBuilder, GraphBuilder.CreateUAV(StainMaskDummy), FVector4f(0.0f));

		// The feature-preview eye on the Stain group. Gated on the selected
		// layer the way the composite gates its own debug write, so two stains
		// on different layers cannot fight over one target. Only the resolve
		// binds the shared debug target; the solve passes take a dummy.
		const bool bWriteStainDebug =
			Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::Stain
			&& Request.DebugSettings.LayerIndex == LayerIndex;

		FRDGTextureRef StainSourceMask = Effect.StainSourceMask.IsValid()
			? RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Effect.StainSourceMask,
				TEXT("Mixtormat.StainSourceMask"))
			: MaskTargets[MaskReadIndex];
		FRDGTextureRef StainDirtMask = Effect.StainDirtMask.IsValid()
			? RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Effect.StainDirtMask,
				TEXT("Mixtormat.StainDirtMask"))
			: StainSourceMask;

		auto FillStainParameters = [&](FMixtormatStainCS::FParameters* P)
		{
			P->OutputSize = Request.Resolution;
			P->SurfaceSize = Request.Resolution;
			P->StainMode = Effect.StainMode;
			P->Seed = Effect.StainSeed;
			P->UseSourceMask = Effect.StainSourceMask.IsValid() ? 1u : 0u;

			// Only from the second mask child onward. The read half is cleared
			// to white, so trusting it on the first child would source liquid
			// over the whole surface instead of falling back to curvature.
			P->UseLayerMask = MaskPassIndex > 0 ? 1u : 0u;
			P->UseDirtMask = Effect.StainDirtMask.IsValid() ? 1u : 0u;
			P->InvertSourceMask = Effect.bStainSourceMaskInvert ? 1u : 0u;
			P->InvertDirtMask = Effect.bStainDirtMaskInvert ? 1u : 0u;
			P->WriteDebug = 0u;
			P->Initialize = MaskPassIndex == 0 ? 1u : 0u;
			P->SurfaceValid = LayerIndex > 0 ? 1u : 0u;

			// Replace. Stain exposes no blend mode of its own: it is the shape
			// of a run, and a run either covers a texel or it does not.
			P->BlendMode = static_cast<uint32>(EMixtormatMaskBlendMode::Replace);
			P->SourceMaskTiling = Effect.StainSourceMaskTiling;
			P->DirtMaskTiling = Effect.StainDirtMaskTiling;
			P->Strength = Effect.Strength;
			P->SourceAmount = Effect.StainSourceAmount;
			P->Gravity = Effect.StainGravity;
			P->SurfaceFollow = Effect.StainSurfaceFollow;
			P->Spread = Effect.StainSpread;
			P->Accumulation = Effect.StainAccumulation;
			P->Absorption = Effect.StainAbsorption;
			P->Drying = Effect.StainDrying;
			P->DirtAmount = Effect.StainDirtAmount;
			P->ConcavityWeight = Effect.StainConcavityWeight;
			P->ConvexityWeight = Effect.StainConvexityWeight;
			P->OcclusionWeight = Effect.StainOcclusionWeight;
			P->HeightWeight = Effect.StainHeightWeight;
			P->HeightBias = Effect.StainSourceHeightBias;
			P->SlopeWeight = Effect.StainSlopeWeight;
			P->SurfaceResponse = Effect.StainSurfaceResponse;

			// The surface accumulated below this layer, the same one the
			// generated mask reads.
			P->SourceNormal = OutputN[LayerReadIndex];
			P->SourceRAM = OutputRAM[LayerReadIndex];
			P->SourceHeight = HeightTargets[LayerReadIndex];
			P->PreviousMask = MaskTargets[MaskReadIndex];
			P->FeatureMask = FeatureMask;
			P->SourceMask = StainSourceMask;
			P->DirtMask = StainDirtMask;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		};

		const FIntVector StainGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);
		FMixtormatStainCS::FParameters* Init =
			GraphBuilder.AllocParameters<FMixtormatStainCS::FParameters>();
		FillStainParameters(Init);
		Init->Mode = 0;
		Init->Iteration = 0;
		Init->PreviousStateA = StateReadDummy;
		Init->PreviousStateB = StateReadDummy;
		Init->OutputStateA = GraphBuilder.CreateUAV(StateA[0]);
		Init->OutputStateB = GraphBuilder.CreateUAV(StateB[0]);
		Init->OutputMask = GraphBuilder.CreateUAV(StainMaskDummy);
		Init->OutputDebug = GraphBuilder.CreateUAV(StateWriteDummyA);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.Stain.L%d.C%d.Initialize", LayerIndex, ChildIndex),
			StainShader,
			Init,
			StainGroups);

		int32 StateReadIndex = 0;
		for (int32 Iteration = 0; Iteration < Effect.StainIterations; ++Iteration)
		{
			const int32 StateWriteIndex = 1 - StateReadIndex;
			FMixtormatStainCS::FParameters* Step =
				GraphBuilder.AllocParameters<FMixtormatStainCS::FParameters>();
			FillStainParameters(Step);
			Step->Mode = 1;
			Step->Iteration = Iteration + 1;
			Step->PreviousStateA = StateA[StateReadIndex];
			Step->PreviousStateB = StateB[StateReadIndex];
			Step->OutputStateA = GraphBuilder.CreateUAV(StateA[StateWriteIndex]);
			Step->OutputStateB = GraphBuilder.CreateUAV(StateB[StateWriteIndex]);
			Step->OutputMask = GraphBuilder.CreateUAV(StainMaskDummy);
			Step->OutputDebug = GraphBuilder.CreateUAV(StateWriteDummyA);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.Stain.L%d.C%d.Step%d",
					LayerIndex,
					ChildIndex,
					Iteration),
				StainShader,
				Step,
				StainGroups);
			StateReadIndex = StateWriteIndex;
		}

		FMixtormatStainCS::FParameters* Resolve =
			GraphBuilder.AllocParameters<FMixtormatStainCS::FParameters>();
		FillStainParameters(Resolve);
		Resolve->Mode = 2;
		Resolve->Iteration = Effect.StainIterations;
		Resolve->WriteDebug = bWriteStainDebug ? 1u : 0u;
		Resolve->PreviousStateA = StateA[StateReadIndex];
		Resolve->PreviousStateB = StateB[StateReadIndex];
		Resolve->OutputStateA = GraphBuilder.CreateUAV(StateWriteDummyA);
		Resolve->OutputStateB = GraphBuilder.CreateUAV(StateWriteDummyB);
		Resolve->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);
		Resolve->OutputDebug = GraphBuilder.CreateUAV(
			OutputDebug[Request.PublishedTargetIndex]);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.Stain.L%d.C%d.Resolve", LayerIndex, ChildIndex),
			StainShader,
			Resolve,
			StainGroups);

		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugStainSnapshot = GraphBuilder.CreateTexture(
				MaskDesc, TEXT("Mixtormat.DebugStainSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugStainSnapshot);
			DebugMask = DebugStainSnapshot;
		}
		++MaskPassIndex;
	}

	// Peeling, and the procedural peel field that can replace its authored maps.
	//
	// Reached by fall-through in the child dispatcher rather than by a positive test, exactly
	// as before: every effect type the dispatcher names above it has already returned, and
	// Peeling is what EMixtormatEffectType has left.
	//
	// The field is an eikonal solve at reduced resolution -- seed, a chain of ping-ponged
	// solve steps, then one full-resolution resolve that filters arrival back up. The two
	// arrival halves and the growth field are graph transients owned by this pass alone.
	void AddPeelingEffectPasses(
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
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		FRDGTextureRef* const EffectTargets = LayerCtx.EffectTargets;
		FRDGTextureRef* const EffectHeightTargets = LayerCtx.EffectHeightTargets;
		const FRDGTextureRef PeelFieldDummy = LayerCtx.PeelFieldDummy;
		FRDGTextureRef& CombinedEffectData = LayerCtx.CombinedEffectData;
		FRDGTextureRef& CombinedEffectHeight = LayerCtx.CombinedEffectHeight;
		int32& EffectPassIndex = LayerCtx.EffectPassIndex;
		TShaderMapRef<FMixtormatPeelFieldCS> PeelFieldShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatPeelingCS> PeelingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// A procedural peel builds its field first: seed the mask's threshold
		// contour, then a chain of eikonal solve steps, then one resolve into
		// the same channel layout the authored maps carry. The peel pass below
		// is identical either way apart from which source it reads.
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
			const int32 SolveDivisor = FMath::Clamp(Effect.PeelSolveDivisor, 1, 32);
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

				FP->MacroPeriod = FMath::Clamp(Effect.PeelMacroPeriod, 1, 256);
				FP->MicroPeriod = FMath::Clamp(Effect.PeelMicroPeriod, 1, 512);
				FP->NoiseWeight = Effect.PeelSeedNoiseWeight;
				FP->SizeVariation = Effect.PeelSizeVariation;
				FP->FlakeCells = FMath::Clamp(Effect.PeelClusterPeriod, 1, 128);
				FP->ClusterAmount = Effect.PeelClusterAmount;
				FP->WarpPeriod = FMath::Clamp(Effect.PeelWarpPeriod, 1, 256);
				FP->WarpAmount = Effect.PeelWarpAmount;
				FP->WarpSource = Effect.PeelWarpSource;

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
				FP->ChildMask = FeatureMask;
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
			// is bounded by reach at the solve resolution. Seeding the
			// contour rather than the interior does not raise it: inward
			// and outward propagation leave the same band on the same pass,
			// and the reach each side needs is still the same.
			int32 Ping = 0;
			const float CurlLength =
				FMath::Abs(Effect.Width)
				* FMath::Lerp(
					2.0f,
					10.0f,
					FMath::Clamp(Effect.MicroMorph, 0.0f, 1.0f));

			const float Reach =
				FMath::Abs(Effect.Front)
				+ FMath::Max(
					4.0f * FMath::Abs(Effect.Width),
					2.25f * CurlLength);
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
		EffectParameters->PreviousEffectData = EffectTargets[EffectReadIndex];
		EffectParameters->ChildMask = FeatureMask;
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

}
