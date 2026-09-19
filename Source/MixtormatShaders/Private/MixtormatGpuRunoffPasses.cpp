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
// It does now share one thing with Stain, though: a fixed analysis grid. See the note on
// RunoffAnalysisTarget below for why that replaced the full-resolution arrangement this file
// originally argued for.
//
// Four shaders rather than one with a Mode uniform. The mode arrangement made every pass bind
// every output, so each dispatch carried three one-by-one stand-in UAVs that nothing wrote --
// which meant the only thing keeping a full-resolution pixel coordinate from being stored into a
// one-by-one texture was a uniform branch nobody could see from the binding site. Four parameter
// structs, each holding exactly what its pass writes, removes the class of mistake rather than
// arguing about whether the branch holds. It also removes four clear passes per runoff child.

namespace
{
	// The grid the analysis, the source field and the directional solve all run on.
	//
	// Runoff measures every distance it has as a fraction of the texture -- the streak reach and
	// the cavity radii come in as reference texels over 1024 and are converted on the CPU, the
	// Gaussian sigma is a multiple of the reach, and the tap spacing is the reach over a constant
	// tap count. Nothing in the effect is counted in actual texels. So the fields being sampled
	// carry the same information at every composition size, and running the analysis at 4K sampled
	// them sixteen times more densely than at 1K without there being sixteen times as much to
	// find. It cost sixteen times as much, produced the same streak, and at 4K put enough work in
	// one submission to trip the driver's watchdog.
	//
	// This file used to argue the opposite, and the argument was wrong in a specific way worth
	// recording: it observed that Runoff needs no reduced grid for *correctness*, which is true,
	// and concluded there was nothing a reduced grid would fix. Cost is the thing it fixes.
	//
	// 1024 rather than half of whatever came in, for the same reason Stain's solve target is
	// 1024: one analysis size shared by 1K, 2K and 4K is what makes the three agree, and a
	// per-resolution fraction would make them differ again by a smaller factor. It is also
	// exactly the resolution the cavity radii are authored against -- the narrowest ring is two
	// reference texels, which is two actual texels here -- so the detector runs at the scale it
	// was tuned at instead of below it.
	//
	// Not exposed. Moving it would change the grain of every runoff in every material at once,
	// which is what Streak Radius and Warp Scale are already for.
	constexpr int32 RunoffAnalysisTarget = 1024;

	struct FMixtormatRunoffGrid
	{
		FIntPoint AnalysisResolution = FIntPoint::ZeroValue;
		int32 DownsampleTaps = 1;
		FIntVector AnalysisGroups = FIntVector::ZeroValue;
		FIntVector FullGroups = FIntVector::ZeroValue;
	};

	FMixtormatRunoffGrid MakeRunoffGrid(const FIntPoint FullResolution)
	{
		// One divisor from the longer side, so a non-square composition keeps its aspect on the
		// analysis grid rather than being squashed onto a square one. Clamped at one so a
		// composition already at or below the target analyses where it is rather than being
		// upsampled into, and at eight so an absurd resolution cannot reduce past usefulness.
		const int32 Divisor = FMath::Clamp(
			FMath::DivideAndRoundUp(
				FMath::Max(FullResolution.X, FullResolution.Y),
				RunoffAnalysisTarget),
			1,
			8);

		FMixtormatRunoffGrid Grid;
		Grid.AnalysisResolution = FIntPoint(
			FMath::Max(FullResolution.X / Divisor, 64),
			FMath::Max(FullResolution.Y / Divisor, 64));

		// One bilinear tap averages a two-by-two block, so a reduction by D wants D/2 taps per
		// axis. At D of one or two that is a single tap, which lands on a source texel centre and
		// on a source two-by-two midpoint respectively -- so 1K reads its height and mask through
		// the downsample completely unchanged.
		Grid.DownsampleTaps = FMath::Max(Divisor / 2, 1);

		Grid.AnalysisGroups = FIntVector(
			FMath::DivideAndRoundUp(Grid.AnalysisResolution.X, 8),
			FMath::DivideAndRoundUp(Grid.AnalysisResolution.Y, 8),
			1);
		Grid.FullGroups = FIntVector(
			FMath::DivideAndRoundUp(FullResolution.X, 8),
			FMath::DivideAndRoundUp(FullResolution.Y, 8),
			1);
		return Grid;
	}
}

// The composition's height and accumulated mask, box-filtered onto the analysis grid.
class FMixtormatRunoffDownsampleCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRunoffDownsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRunoffDownsampleCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SourceSize)
		SHADER_PARAMETER(int32, DownsampleTaps)
		SHADER_PARAMETER(uint32, UseLayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputAnalysis)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Surface response, fractal noise, source field. The one pass that writes the source.
class FMixtormatRunoffPrepareCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRunoffPrepareCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRunoffPrepareCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, GravityAngle)
		SHADER_PARAMETER(float, SurfaceInfluence)
		SHADER_PARAMETER(float, WarpScale)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, AnalysisSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputSource)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// The directional Gaussian, the strata, the warp and the lip.
class FMixtormatRunoffResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRunoffResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRunoffResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StrataCount)
		SHADER_PARAMETER(float, GravityAngle)
		SHADER_PARAMETER(float, StreakRadius)
		SHADER_PARAMETER(float, StreakSoftness)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, LipStrength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RunoffSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRunoff)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// Scope, the mask chain blend and Strength, at the composition's own resolution.
//
// The debug write is a permutation rather than a uniform branch, so the shared preview target is
// neither declared nor bound on the runoffs that are not being previewed. A branch would have left
// every runoff in the material holding a UAV binding on one full-resolution target, which is a
// false dependency between passes that have nothing to do with each other.
class FMixtormatRunoffApplyCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRunoffApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRunoffApplyCS, FGlobalShader);

	class FWriteDebugDim : SHADER_PERMUTATION_BOOL("MIXTORMAT_RUNOFF_WRITE_DEBUG");
	using FPermutationDomain = TShaderPermutationDomain<FWriteDebugDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolvedRunoff)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FeatureMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRunoffDownsampleCS,
	"/Plugin/Mixtormat/Private/MixtormatRunoff.usf",
	"DownsampleCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRunoffPrepareCS,
	"/Plugin/Mixtormat/Private/MixtormatRunoff.usf",
	"PrepareCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRunoffResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatRunoff.usf",
	"ResolveCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRunoffApplyCS,
	"/Plugin/Mixtormat/Private/MixtormatRunoff.usf",
	"ApplyCS",
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

		const FMixtormatRunoffGrid Grid = MakeRunoffGrid(Request.Resolution);

		// Only from the second mask child onward. The read half is cleared to white, so trusting
		// it on the first child would source runoff over the whole surface instead of falling
		// back to the surface's own cavities and ledges.
		const uint32 UseLayerMask = MaskPassIndex > 0 ? 1u : 0u;

		// The feature-preview eye on the Runoff group. Gated on the selected layer the way the
		// composite gates its own debug write, so two runoffs on different layers cannot fight
		// over one target. Only the apply pass touches the shared debug target, and only on the
		// permutation that declares it.
		const bool bWriteRunoffDebug =
			Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::Runoff
			&& Request.DebugSettings.LayerIndex == LayerIndex;

		// Three analysis-resolution intermediates. At 4K these are ten megabytes between them
		// where the single full-resolution source field they replace was sixty-seven, and the
		// twenty-six ring taps per texel now land inside a two-megabyte height rather than
		// scattering across a thirty-two-megabyte one.
		const FRDGTextureDesc AnalysisDesc = FRDGTextureDesc::Create2D(
			Grid.AnalysisResolution,
			PF_G16R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc ResolvedDesc = FRDGTextureDesc::Create2D(
			Grid.AnalysisResolution,
			PF_R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef AnalysisSource =
			GraphBuilder.CreateTexture(AnalysisDesc, TEXT("Mixtormat.RunoffAnalysis"));
		FRDGTextureRef RunoffSource =
			GraphBuilder.CreateTexture(AnalysisDesc, TEXT("Mixtormat.RunoffSource"));
		FRDGTextureRef ResolvedRunoff =
			GraphBuilder.CreateTexture(ResolvedDesc, TEXT("Mixtormat.RunoffResolved"));

		// Bilinear and wrapping, shared by all four passes. The wrap is not incidental: it is what
		// lets a streak that runs off the bottom edge arrive at the top, and what keeps the
		// analysis grid's rings reading across the seam rather than clamping at it.
		FRHISamplerState* const RunoffSampler =
			TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

		// Pass one: the height accumulated below this layer, and the layer's own mask chain,
		// box-filtered onto the analysis grid.
		{
			TShaderMapRef<FMixtormatRunoffDownsampleCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatRunoffDownsampleCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatRunoffDownsampleCS::FParameters>();
			P->OutputSize = Grid.AnalysisResolution;
			P->SourceSize = Request.Resolution;
			P->DownsampleTaps = Grid.DownsampleTaps;
			P->UseLayerMask = UseLayerMask;

			// The same surface a generated mask reads: whatever is composited below this layer.
			P->SourceHeight = HeightTargets[LayerReadIndex];
			P->PreviousMask = MaskTargets[MaskReadIndex];
			P->LinearWrapSampler = RunoffSampler;
			P->OutputAnalysis = GraphBuilder.CreateUAV(AnalysisSource);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.Runoff.L%d.C%d.Downsample", LayerIndex, ChildIndex),
				Shader,
				P,
				Grid.AnalysisGroups);
		}

		// Pass two: surface response, fractal noise, source field.
		{
			TShaderMapRef<FMixtormatRunoffPrepareCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatRunoffPrepareCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatRunoffPrepareCS::FParameters>();
			P->OutputSize = Grid.AnalysisResolution;

			// No relief underneath on the bottom layer, so the surface response has nothing to
			// read and falls back to neutral.
			P->SurfaceValid = LayerIndex > 0 ? 1u : 0u;
			P->Seed = Effect.RunoffSeed;
			P->GravityAngle = Effect.RunoffGravityAngle;
			P->SurfaceInfluence = Effect.RunoffSurfaceInfluence;
			P->WarpScale = Effect.RunoffWarpScale;
			P->AnalysisSource = AnalysisSource;
			P->LinearWrapSampler = RunoffSampler;
			P->OutputSource = GraphBuilder.CreateUAV(RunoffSource);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Runoff.L%d.C%d.Prepare", LayerIndex, ChildIndex),
				Shader,
				P,
				Grid.AnalysisGroups);
		}

		// Pass three: the directional Gaussian, the strata, the warp and the lip.
		{
			TShaderMapRef<FMixtormatRunoffResolveCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatRunoffResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatRunoffResolveCS::FParameters>();
			P->OutputSize = Grid.AnalysisResolution;
			P->StrataCount = Effect.RunoffStrataCount;
			P->GravityAngle = Effect.RunoffGravityAngle;
			P->StreakRadius = Effect.RunoffStreakRadius;
			P->StreakSoftness = Effect.RunoffStreakSoftness;
			P->StrataAmount = Effect.RunoffStrataAmount;
			P->WarpAmount = Effect.RunoffWarpAmount;
			P->LipStrength = Effect.RunoffLipStrength;
			P->RunoffSource = RunoffSource;
			P->LinearWrapSampler = RunoffSampler;
			P->OutputRunoff = GraphBuilder.CreateUAV(ResolvedRunoff);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Runoff.L%d.C%d.Resolve", LayerIndex, ChildIndex),
				Shader,
				P,
				Grid.AnalysisGroups);
		}

		// Pass four: scope, the mask chain blend and Strength, at the composition's resolution.
		{
			FMixtormatRunoffApplyCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatRunoffApplyCS::FWriteDebugDim>(bWriteRunoffDebug);
			TShaderMapRef<FMixtormatRunoffApplyCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);

			FMixtormatRunoffApplyCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatRunoffApplyCS::FParameters>();
			P->OutputSize = Request.Resolution;
			P->Initialize = MaskPassIndex == 0 ? 1u : 0u;

			// Replace. Runoff exposes no blend mode of its own, for the same reason Stain does
			// not: it is the shape of a run, and a run either covers a texel or it does not.
			P->BlendMode = static_cast<uint32>(EMixtormatMaskBlendMode::Replace);
			P->Strength = Effect.RunoffStrength;
			P->ResolvedRunoff = ResolvedRunoff;
			P->PreviousMask = MaskTargets[MaskReadIndex];
			P->FeatureMask = FeatureMask;
			P->LinearWrapSampler = RunoffSampler;
			P->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

			// Left null on the permutation that does not declare it. RDG only requires a binding
			// for what the compiled shader actually references, and the non-previewing
			// permutation references nothing here.
			P->OutputDebug = bWriteRunoffDebug
				? GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex])
				: nullptr;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Runoff.L%d.C%d.Apply", LayerIndex, ChildIndex),
				Shader,
				P,
				Grid.FullGroups);
		}

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
