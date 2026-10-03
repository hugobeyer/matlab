// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"
#include "../MixtormatGpuMaskShaping.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Craquelure. A crack network on a cellular lattice, blended into the layer mask.
//
// Its own node rather than a signal on the generated mask: that node reads the surface below
// and early-returns when there is none, while this is generated from a lattice and means
// something on the bottom layer. Shaping/binding are shared with other mask producers,
// while this producer carries no surface textures at all.
class FMixtormatCraquelureCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER(uint32, BlendMode)
		MIXTORMAT_MASK_SHAPING_PARAMETERS
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelure.usf",
	"MainCS",
	SF_Compute);

// Propagated craquelure. Three entry points in one file, one shader class each.
//
// Each class binds only the parameters its own entry point uses, rather than a shared struct
// covering all three. That is not tidiness: RDG rejects a pass that binds a transient nothing
// has written, so a seed pass carrying a PreviousState slot would fail on the first dispatch.
class FMixtormatCraquelureSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, SeedChance)
		SHADER_PARAMETER(float, SeedJitter)
		SHADER_PARAMETER(int32, NoiseCells)
		SHADER_PARAMETER(float, StressVariation)
		SHADER_PARAMETER(float, ToughnessVariation)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureGrowCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureGrowCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureGrowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Persistence)
		SHADER_PARAMETER(float, FlowStrength)
		SHADER_PARAMETER(float, StressGain)
		SHADER_PARAMETER(float, ToughnessCost)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, GrowthThreshold)
		SHADER_PARAMETER(float, MinAlignment)
		SHADER_PARAMETER(float, TurnResponse)
		SHADER_PARAMETER(int32, CollisionLimit)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousDirection)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Field)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureGrowCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"GrowCS",
	SF_Compute);

class FMixtormatCraquelureResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, BlendMode)
		MIXTORMAT_MASK_SHAPING_PARAMETERS
		SHADER_PARAMETER(float, Weight)
		// The warp is read here now rather than during growth, and the uniforms are declared at
		// file scope in a shader with three entry points -- so every struct that compiles
		// MixtormatCraquelureGrow.usf has to declare them, not just the pass that grows.
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureGrow.usf",
	"ResolveCS",
	SF_Compute);

// Distance to the nearest crack, by jump flooding. Three entry points, one class each, for the
// same reason the growth passes are split: RDG rejects a pass that binds a transient nothing has
// written, so a seed pass carrying a PreviousRecord slot would fail on its first dispatch.
class FMixtormatCraquelureDistanceSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureDistanceStepCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceStepCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceStepCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceStepCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"StepCS",
	SF_Compute);

class FMixtormatCraquelureDistanceResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureDistance.usf",
	"ResolveDistanceCS",
	SF_Compute);

// Craquelure relief. Reads the distance field rather than the mask: by the time the mask exists
// it has been through shaping, a blend mode and a weight lerp, and the distance the groove
// profile needs is gone.
class FMixtormatCraquelureReliefCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureReliefCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureReliefCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, NormalWeight)
		SHADER_PARAMETER(float, ReliefWidthPixels)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(float, Profile)
		SHADER_PARAMETER(float, GrooveVariation)
		SHADER_PARAMETER(float, ProfileVariation)
		SHADER_PARAMETER(float, WidthVariation)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureReliefCS,
	"/Plugin/Mixtormat/Private/MixtormatCraquelureRelief.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Craquelure, both modes: the lattice pass, and the propagated network with its growth
	// loop, jump-flood distance and kept-network cache. Relief is queued here and dispatched
	// after the composite by AddCraquelureReliefPasses -- the distance field is carried in the
	// pending entry because the mask targets it was produced alongside are ping-ponged.
	void AddCraquelureMaskPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
			LayerCtx.PendingCraquelureReliefs;
		TShaderMapRef<FMixtormatCraquelureDistanceResolveCS> CraqDistanceResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureDistanceSeedCS> CraqDistanceSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureDistanceStepCS> CraqDistanceStepShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureGrowCS> CraquelureGrowShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureResolveCS> CraquelureResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureSeedCS> CraquelureSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCraquelureCS> CraquelureShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FCraquelureRenderData& Crack = Child.Craquelure;

		// The same identity as the other mask children, and the one that
		// saves the most by far: a muted craquelure node was still growing
		// its whole network, up to a thousand full-resolution passes, to
		// produce a mask it then discarded. Relief reads the same network
		// through its own weights, so both halves have to be idle before
		// there is nothing left to compute.
		// Weight 0 makes the mask half the identity: every mask shader
		// ends on saturate(lerp(Previous, Result, Weight)), and the masks it
		// reads are already saturated, so the output is the input bit for
		// bit. Skipping is only exact from the second mask child onward --
		// the first establishes the chain with Initialize, where Previous is
		// zero rather than what the layer already had, and a skip there
		// would leave a different mask behind rather than the same one.
		if (MaskPassIndex > 0
			&& Crack.Weight == 0.0f
			&& Crack.ReliefDepth == 0.0f)
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;
		const FIntVector CrackGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		// (distance to the nearest crack in pixels, crack id). Both modes fill
		// it -- the lattice analytically, the propagated mode by flooding its
		// grown skeleton -- so the mask tail and relief read one field and
		// never learn which built it.
		//
		// Full float rather than half: the id is a lineage hash up to 2^24 and
		// has to stay exact, and a half would truncate it and silently merge
		// unrelated cracks into one variation value.
		const FRDGTextureDesc CraqDistanceDesc = FRDGTextureDesc::Create2D(
			Request.Resolution,
			PF_A32B32G32R32F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		// Propagated networks are looked up before anything is dispatched. On
		// a hit the seed, the growth loop and the whole jump flood are
		// skipped and the kept field is registered straight into this graph:
		// it is the same texture the miss would have produced, so everything
		// downstream is unchanged.
		//
		// Lattice mode is deliberately not cached. Its distance falls out of
		// the same single pass that writes its mask, so there is nothing to
		// skip -- the pass would have to run anyway.
		FRDGTextureRef CraqDistance = nullptr;
		bool bCraqNetworkCached = false;
		if (Crack.Mode == EMixtormatCraquelureMode::Propagated
			&& Request.NetworkCache.IsValid())
		{
			const TRefCountPtr<IPooledRenderTarget> Cached =
				Request.NetworkCache->Find(Crack.NetworkKey, Request.Resolution);
			if (Cached.IsValid())
			{
				CraqDistance = GraphBuilder.RegisterExternalTexture(
					Cached, TEXT("Mixtormat.CraqDistanceCached"));
				bCraqNetworkCached = true;
			}
		}
		if (CraqDistance == nullptr)
		{
			CraqDistance = GraphBuilder.CreateTexture(
				CraqDistanceDesc, TEXT("Mixtormat.CraqDistance"));
		}

		// Half-width of the groove in pixels. Cell units on both sides of the
		// conversion, so it means the same fraction of a cell at any
		// resolution -- and the cell count is the seed lattice in propagated
		// mode and the crack lattice in lattice mode, matching what Width
		// already divides by in each.
		const int32 CraqReliefCells = Crack.Mode == EMixtormatCraquelureMode::Propagated
			? FMath::Max(Crack.SeedCells, 1)
			: FMath::Max(Crack.Period, 1);
		const float CraqReliefWidthPixels =
			Crack.ReliefWidth * Request.Resolution.X / static_cast<float>(CraqReliefCells);

		// Queued whether or not either weight is live, so the branch that
		// decides is in one place; the dispatch below skips a pair of zeroes.
		auto QueueCraquelureRelief = [&]()
		{
			if (Crack.ReliefDepth <= 0.0f)
			{
				return;
			}
			FPendingCraquelureRelief& Relief = PendingCraquelureReliefs.AddDefaulted_GetRef();
			Relief.Distance = CraqDistance;
			Relief.HeightWeight = Crack.ReliefDepth;
			Relief.WidthPixels = CraqReliefWidthPixels;
			Relief.Variation = Crack.Variation;
			Relief.Warp = Crack.Warp;
			Relief.WarpPeriod = Crack.WarpPeriod;
			Relief.WarpSeed = Crack.WarpSeed;
			Relief.Profile = Crack.ReliefProfile;
			Relief.GrooveVariation = Crack.ReliefGrooveVariation;
			Relief.ProfileVariation = Crack.ReliefProfileVariation;
			Relief.WidthVariation = Crack.ReliefWidthVariation;
		};

		// Propagated mode grows a network over N iterations against its own
		// ping-ponged state, then resolves it into the layer mask. The
		// state and direction pair are meaningless to any other mask child,
		// so they are allocated here rather than routed through MaskTargets:
		// the node still consumes one MaskPassIndex and writes one R16F
		// target, exactly like every other mask child.
		if (Crack.Mode == EMixtormatCraquelureMode::Propagated)
		{
			// Everything from here to the store is the build, and it runs only
			// on a miss. A hit already holds the field it would produce, and
			// the mask tail below reads the two identically.
			if (!bCraqNetworkCached)
			{
				// State is (cracked, front, id, level) at full float. The id is
				// a lineage hash up to 2^24 and has to stay exact -- a half
				// would truncate it and silently merge unrelated cracks into
				// one, which the per-crack Variation would then show as a
				// single flat value across the whole network.
				const FRDGTextureDesc CraqStateDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_A32B32G32R32F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);

				// Direction only needs two channels and the field four, but both
				// are four here. A two-channel typed UAV is a binding shape
				// nothing else in this compositor uses, and it is the one thing
				// a standalone HLSL compile cannot check -- it validates the
				// shader in isolation, never the format against the
				// declaration. Four bytes a pixel to delete that failure mode.
				const FRDGTextureDesc CraqDirectionDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				const FRDGTextureDesc CraqFieldDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);

				FRDGTextureRef CraqState[2] = {
					GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateA")),
					GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateB"))};
				FRDGTextureRef CraqDirection[2] = {
					GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirA")),
					GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirB"))};
				FRDGTextureRef CraqField =
					GraphBuilder.CreateTexture(CraqFieldDesc, TEXT("Mixtormat.CraqField"));

				FMixtormatCraquelureSeedCS::FParameters* SeedParameters =
					GraphBuilder.AllocParameters<FMixtormatCraquelureSeedCS::FParameters>();
				SeedParameters->OutputSize = Request.Resolution;
				SeedParameters->Seed = Crack.Seed;
				SeedParameters->SeedCells = Crack.SeedCells;
				SeedParameters->SeedChance = Crack.SeedChance;
				SeedParameters->SeedJitter = Crack.SeedJitter;
				SeedParameters->NoiseCells = Crack.NoiseCells;
				SeedParameters->StressVariation = Crack.StressVariation;
				SeedParameters->ToughnessVariation = Crack.ToughnessVariation;
				SeedParameters->Warp = Crack.Warp;
				SeedParameters->WarpPeriod = Crack.WarpPeriod;
				SeedParameters->WarpSeed = Crack.WarpSeed;
				SeedParameters->OutputState = GraphBuilder.CreateUAV(CraqState[0]);
				SeedParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[0]);
				SeedParameters->OutputField = GraphBuilder.CreateUAV(CraqField);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Craquelure.Seed.Layer%d.Child%d", LayerIndex, ChildIndex),
					CraquelureSeedShader,
					SeedParameters,
					CrackGroups);

				// A crack advances one pixel per iteration, so the authored
				// count is a reach in pixels. Scaled against the same 1024
				// reference chipping uses, so a preview and an export grow the
				// same network rather than the same pixel count.
				//
				// The cap is a cost bound. It used to sit at 192, which quietly
				// made it the reach control rather than a guard on it: at a
				// Reach of 1024 the authored value was clamped to under a fifth
				// of itself at every resolution from 1K up, so most of the
				// slider did nothing at all. Raised to the top of the authored
				// range so Reach means what it says.
				//
				// The scaling still stops being honest above that: a 4K export
				// at maximum Reach clamps where the preview did not. Kept as a
				// bound rather than removed because each step is a
				// full-resolution pass doing roughly eighty texture loads per
				// pixel -- this is by some way the most expensive node in the
				// graph, and an unbounded count at 4K is minutes.
				const int32 GrowIterations = FMath::Clamp(
					FMath::RoundToInt(
						Crack.Iterations *
						FMath::Max(Request.Resolution.X, Request.Resolution.Y) / 1024.0f),
					1,
					1024);

				int32 StateIndex = 0;
				for (int32 GrowPass = 0; GrowPass < GrowIterations; ++GrowPass)
				{
					const int32 ReadState = StateIndex;
					const int32 WriteState = 1 - ReadState;

					FMixtormatCraquelureGrowCS::FParameters* GrowParameters =
						GraphBuilder.AllocParameters<FMixtormatCraquelureGrowCS::FParameters>();
					GrowParameters->OutputSize = Request.Resolution;
					GrowParameters->Seed = Crack.Seed;
					GrowParameters->Persistence = Crack.Persistence;
					GrowParameters->FlowStrength = Crack.FlowStrength;
					GrowParameters->StressGain = Crack.StressGain;
					GrowParameters->ToughnessCost = Crack.ToughnessCost;
					GrowParameters->Irregularity = Crack.Irregularity;
					GrowParameters->GrowthThreshold = Crack.GrowthThreshold;
					// Fixed rather than exposed: it only rejects steps a tip
					// would never take anyway, and the interesting control over
					// how straight a crack runs is Persistence.
					GrowParameters->MinAlignment = 0.05f;
					GrowParameters->TurnResponse = Crack.TurnResponse;
					GrowParameters->CollisionLimit = Crack.CollisionLimit;
					GrowParameters->Iteration = GrowPass;
					GrowParameters->PreviousState = CraqState[ReadState];
					GrowParameters->PreviousDirection = CraqDirection[ReadState];
					GrowParameters->Field = CraqField;
					GrowParameters->OutputState = GraphBuilder.CreateUAV(CraqState[WriteState]);
					GrowParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[WriteState]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Grow%d.Layer%d.Child%d",
							GrowPass, LayerIndex, ChildIndex),
						CraquelureGrowShader,
						GrowParameters,
						CrackGroups);

					StateIndex = WriteState;
				}

				// Distance to the grown skeleton, by jump flooding. Log2(N) passes
				// for any radius, where the resolve pass used to brute force a box
				// clamped to radius 8 -- quadratic in the width and a hard cap on
				// it. Relief needs the same field at radii far past what a box
				// could reach, so both read this now.
				{
					const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(
						Request.Resolution,
						PF_A32B32G32R32F,
						FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef Record[2] = {
						GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaA")),
						GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaB"))};

					FMixtormatCraquelureDistanceSeedCS::FParameters* JfaSeed =
						GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceSeedCS::FParameters>();
					JfaSeed->OutputSize = Request.Resolution;
					JfaSeed->CrackState = CraqState[StateIndex];
					JfaSeed->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Distance.Seed.Layer%d.Child%d",
							LayerIndex, ChildIndex),
						CraqDistanceSeedShader,
						JfaSeed,
						CrackGroups);

					int32 RecordIndex = 0;

					// Strides halve from half the padded extent down to 1, then one
					// more pass at 1 -- the JFA+1 variant. Plain jump flooding is
					// not exact: a seed can be lost when the record that would have
					// carried it was itself overwritten at a coarser stride. The
					// extra unit pass costs one dispatch and removes the islands
					// that error shows up as.
					const int32 FirstStep = FMath::Max(
						1,
						static_cast<int32>(FMath::RoundUpToPowerOfTwo(
							static_cast<uint32>(FMath::Max(
								Request.Resolution.X, Request.Resolution.Y)))) / 2);

					for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
					{
						const int32 ReadRecord = RecordIndex;
						const int32 WriteRecord = 1 - ReadRecord;

						FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
							GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
						JfaStep->OutputSize = Request.Resolution;
						JfaStep->StepSize = StepSize;
						JfaStep->PreviousRecord = Record[ReadRecord];
						JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.Craquelure.Distance.Step%d.Layer%d.Child%d",
								StepSize, LayerIndex, ChildIndex),
							CraqDistanceStepShader,
							JfaStep,
							CrackGroups);

						RecordIndex = WriteRecord;
					}

					{
						const int32 ReadRecord = RecordIndex;
						const int32 WriteRecord = 1 - ReadRecord;

						FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
							GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
						JfaStep->OutputSize = Request.Resolution;
						JfaStep->StepSize = 1;
						JfaStep->PreviousRecord = Record[ReadRecord];
						JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.Craquelure.Distance.StepFinal.Layer%d.Child%d",
								LayerIndex, ChildIndex),
							CraqDistanceStepShader,
							JfaStep,
							CrackGroups);

						RecordIndex = WriteRecord;
					}

					FMixtormatCraquelureDistanceResolveCS::FParameters* JfaResolve =
						GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceResolveCS::FParameters>();
					JfaResolve->OutputSize = Request.Resolution;
					JfaResolve->PreviousRecord = Record[RecordIndex];
					JfaResolve->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME(
							"Mixtormat.Craquelure.Distance.Resolve.Layer%d.Child%d",
							LayerIndex, ChildIndex),
						CraqDistanceResolveShader,
						JfaResolve,
						CrackGroups);
				}

				// Kept for the next composite. Converting promotes the transient to
				// a pooled target, which costs the memory of one full-resolution
				// RGBA32F per distinct network -- the trade this whole path makes.
				if (Request.NetworkCache.IsValid())
				{
					Request.NetworkCache->Store(
						Crack.NetworkKey,
						Request.Resolution,
						GraphBuilder.ConvertToExternalTexture(CraqDistance));
				}

			}

			QueueCraquelureRelief();

			FMixtormatCraquelureResolveCS::FParameters* ResolveParameters =
				GraphBuilder.AllocParameters<FMixtormatCraquelureResolveCS::FParameters>();
			ResolveParameters->OutputSize = Request.Resolution;
			ResolveParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
			ResolveParameters->SeedCells = Crack.SeedCells;
			ResolveParameters->Width = Crack.Width;
			ResolveParameters->Variation = Crack.Variation;
			ResolveParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);

			ResolveParameters->Weight = Crack.Weight;

			ResolveParameters->Warp = Crack.Warp;
			ResolveParameters->WarpPeriod = Crack.WarpPeriod;
			ResolveParameters->WarpSeed = Crack.WarpSeed;
			ResolveParameters->CrackDistance = CraqDistance;
			ResolveParameters->PreviousMask = MaskTargets[MaskReadIndex];
			ResolveParameters->LinearWrapSampler =
				TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			ResolveParameters->OutputMask =
				GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

			AddMaskNodePass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Craquelure.Resolve.Layer%d.Child%d", LayerIndex, ChildIndex),
				CraquelureResolveShader,
				ResolveParameters,
				Crack,
				CrackGroups);

			CombinedMask = MaskTargets[MaskWriteIndex];
			if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
				&& Request.DebugSettings.LayerIndex == LayerIndex
				&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
			{
				FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
					MaskDesc,
					TEXT("Mixtormat.DebugCraquelureSnapshot"));
				AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
				DebugMask = DebugCrackSnapshot;
			}
			++MaskPassIndex;
			return;
		}

		FMixtormatCraquelureCS::FParameters* CrackParameters =
			GraphBuilder.AllocParameters<FMixtormatCraquelureCS::FParameters>();
		CrackParameters->OutputSize = Request.Resolution;
		CrackParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
		CrackParameters->Period = Crack.Period;
		CrackParameters->Jitter = Crack.Jitter;
		CrackParameters->Width = Crack.Width;
		CrackParameters->Variation = Crack.Variation;
		CrackParameters->Seed = Crack.Seed;
		CrackParameters->Warp = Crack.Warp;
		CrackParameters->WarpPeriod = Crack.WarpPeriod;
		CrackParameters->WarpSeed = Crack.WarpSeed;
		CrackParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);

		CrackParameters->Weight = Crack.Weight;

		CrackParameters->PreviousMask = MaskTargets[MaskReadIndex];
		CrackParameters->LinearWrapSampler =
			TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		CrackParameters->OutputMask =
			GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);
		CrackParameters->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);

		AddMaskNodePass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Craquelure.Layer%d.Child%d", LayerIndex, ChildIndex),
			CraquelureShader,
			CrackParameters,
			Crack,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));
		QueueCraquelureRelief();
		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
				MaskDesc,
				TEXT("Mixtormat.DebugCraquelureSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
			DebugMask = DebugCrackSnapshot;
		}
		++MaskPassIndex;
	}


	// Craquelure relief: after erosion, before chipping, so chipping selects from a height
	// with the cracks already carved into it.
	void AddCraquelureReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
			LayerCtx.PendingCraquelureReliefs;
		// A hidden layer still resolves its craquelure mask (other layers may reference it), but
		// its relief must not carve the surface accumulated below it.
		if (!Layer.bEnabled)
		{
			return;
		}
		TShaderMapRef<FMixtormatCraquelureReliefCS> CraquelureReliefShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Craquelure relief runs after erosion but before chipping. Chipping selects
		// from the current height and its cavity, so it must see cracks already carved
		// into the layer rather than the flat height that preceded them.
		for (int32 ReliefIndex = 0; ReliefIndex < PendingCraquelureReliefs.Num(); ++ReliefIndex)
		{
			const FPendingCraquelureRelief& Relief = PendingCraquelureReliefs[ReliefIndex];
			FRDGTextureRef ReliefH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefH"));
			FRDGTextureRef ReliefN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefN"));

			FMixtormatCraquelureReliefCS::FParameters* RelP =
				GraphBuilder.AllocParameters<FMixtormatCraquelureReliefCS::FParameters>();
			RelP->OutputSize = Request.Resolution;
			RelP->HeightWeight = Relief.HeightWeight;
			RelP->NormalWeight = 0.0f;
			RelP->ReliefWidthPixels = Relief.WidthPixels;
			RelP->Variation = Relief.Variation;
			RelP->Profile = Relief.Profile;
			RelP->GrooveVariation = Relief.GrooveVariation;
			RelP->ProfileVariation = Relief.ProfileVariation;
			RelP->WidthVariation = Relief.WidthVariation;
			RelP->Warp = Relief.Warp;
			RelP->WarpPeriod = Relief.WarpPeriod;
			RelP->WarpSeed = Relief.WarpSeed;
			RelP->CrackDistance = Relief.Distance;
			RelP->SourceHeight = HeightTargets[WriteIndex];
			RelP->PreviousNormal = OutputN[WriteIndex];
			RelP->OutputHeight = GraphBuilder.CreateUAV(ReliefH);
			RelP->OutputNormal = GraphBuilder.CreateUAV(ReliefN);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.Craquelure.Relief.L%d.%d", LayerIndex, ReliefIndex),
				CraquelureReliefShader,
				RelP,
				FIntVector(
					FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
					1));

			FRDGTextureRef ReliefRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Craquelure.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				HeightTargets[WriteIndex],
				ReliefH,
				OutputN[WriteIndex],
				OutputRAM[WriteIndex],
				ReliefN,
				ReliefRAM,
				Request.Resolution,
				HeightDerivedNormalStrength,
				true,
				TEXT("Craquelure"));
			AddCopyTexturePass(GraphBuilder, ReliefH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ReliefN, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ReliefRAM, OutputRAM[WriteIndex]);
		}
	}

}
