// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// The GENERATORS category.
//
// A generator is not an effect and not a mask. Effects -- Erosion, Breakup, Worn Edges, Grade --
// are filters over a layer that has already been composited, which is why every one of them is
// queued during the child loop and dispatched after AddLayerCompositePass. Masks produce 0..1
// coverage and join the mask chain. A generator does neither: it rewrites the layer's *input*
// height before anything has read it, so the layer composites as though the carved surface were
// the one that had been authored.
//
// That single property is the whole category, and everything here exists to preserve it:
//
//     Layer source height
//         -> AddLayerInputPass            resolves it into output space
//         -> AddLayerHeightSmoothPasses   optional pre-smooth
//         -> AddGeneratorPasses           <- here
//         -> mask children, effects, AddLayerCompositePass
//
// AddGeneratorPasses is called once per layer from the layer loop and walks that layer's
// generator children in authored order, each one carving the height the previous one produced.
// It reassigns LayerCtx.LayerInputHeight and LayerCtx.LayerInputN in place -- exactly what
// AddLayerHeightSmoothPasses does one line above it -- and returns. Nothing downstream needs to
// know a generator ran.

namespace MixtormatGpuCompositor
{
namespace
{
	// The grid the distance solve runs on.
	//
	// A propagated distance is a large-scale quantity: the field it produces is smooth at the
	// scale of the cells that seeded it, and solving it at 4K would cost sixteen times the work
	// of solving it at 1K for a result that is visually identical after the remap. Sixteen taps
	// per pixel per iteration at 128 iterations is 34 billion texture loads at 4K and 537 million
	// at 512 -- the difference between a composite that takes minutes and one that takes a
	// fraction of a second.
	//
	// The cap is on the grid, not on a divisor, so the same material solves at the same physical
	// scale at every export resolution. 512 and 1024 and 4096 all solve at 512 and produce the
	// same strata; only the resolve is finer. That is the property an artist actually needs --
	// a preview at 1K that predicts the 4K bake.
	//
	// What the coarse grid costs is edge crispness in the bands, and the bands are analytic:
	// ResolveCS re-evaluates the same strata phase at full resolution and folds it back in, so
	// the detail that matters comes back without the solve paying for it. The same trade the
	// peel front and the wet stain already make.
	constexpr int32 StrataSolveMaxSize = 1024;

	// Divisor rather than a straight clamp, so a non-square tile keeps its aspect and the wrap
	// stays exact on both axes. Rounded up to a power of two because every composition
	// resolution in this plugin is one, and a non-power-of-two divisor would put the solve grid
	// off the texel lattice the mask and ID maps are sampled on.
	FIntPoint StrataSolveResolution(const FIntPoint Resolution)
	{
		const int32 Longest = FMath::Max(Resolution.X, Resolution.Y);
		int32 Divisor = 1;
		while (Longest / Divisor > StrataSolveMaxSize)
		{
			Divisor *= 2;
		}
		return FIntPoint(
			FMath::Max(Resolution.X / Divisor, 1),
			FMath::Max(Resolution.Y / Divisor, 1));
	}

	// The jump schedule.
	//
	// Strides halve from JumpStart to 1 and then the schedule restarts, so a long iteration
	// budget alternates reach and relaxation instead of spending everything after the fifth
	// iteration crawling one texel at a time. The wide passes carry a front across the tile; the
	// stride-1 passes are where the recursive strata push accumulates and the bands form.
	//
	// The schedule is a function of JumpStart alone. Nothing here is keyed to the iteration
	// count, which is what lets Iterations be 1 or 64 or 128 without a special case: 1 runs the
	// widest jump only, 64 runs the cycle a dozen times, and neither is a different algorithm.
	int32 StrataJumpStride(const int32 Iteration, const int32 JumpStart)
	{
		const int32 Start = FMath::Max(JumpStart, 1);
		int32 Halvings = 0;
		while ((Start >> Halvings) > 1)
		{
			++Halvings;
		}
		const int32 CycleLength = Halvings + 1;
		return FMath::Max(Start >> (Iteration % CycleLength), 1);
	}

	// Which Worley family and which combining operation this iteration runs.
	//
	// Drawn from OperationSeed and the iteration index on the CPU, so the inner loop of the
	// propagation kernel carries no hashing at all. Two different hashes rather than two fields
	// of one, so family and operation do not march in lockstep -- with a single draw, iteration
	// 3 would always be "Chebyshev and smooth" for every seed, and the cycling would read as a
	// fixed five-step pattern rather than as variety.
	uint32 StrataIterationHash(const uint32 OperationSeed, const int32 Iteration, const uint32 Salt)
	{
		uint32 Value = OperationSeed * 0x9e3779b9u + static_cast<uint32>(Iteration) * 0x85ebca6bu + Salt;
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	constexpr int32 StrataFamilyCount = 5;
	constexpr int32 StrataOperationCount = 4;

	// True when this generator has at least one enabled mask scoped beneath it.
	//
	// Asked before the scoped mask is resolved, because "there is no mask" and "there is a mask
	// that happens to be white" are different: the first must leave Mask Influence completely
	// inert whatever it is set to, and the second must honour it. Collapsing the two would make
	// a generator with no mask behave as though Mask Influence were always 1, which is the same
	// picture only by accident.
	bool HasScopedMasks(const FLayerRenderData& Layer, const int32 OwnerSourceChildIndex)
	{
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type == EMixtormatLayerChildType::Mask
				&& Child.ScopeOwnerSourceChildIndex == OwnerSourceChildIndex
				&& Child.Mask.Weight != 0.0f)
			{
				return true;
			}
		}
		return false;
	}
}

// Box-reduces the scoped mask from composition resolution to the solve grid. See the note in
// MixtormatStrataCarver.usf: a bilinear fetch at an 8:1 ratio reads one texel in sixty-four, so
// a thin mask aliases or vanishes; an average over the footprint turns it into the coverage the
// solve actually wants.
class FMixtormatStrataCarverMaskReduceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverMaskReduceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverMaskReduceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, MaskFootprint)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FullResMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSolveMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverMaskReduceCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"MaskReduceCS",
	SF_Compute);

// Builds the Worley family fractals, the propagation cost and the initial distance state.
class FMixtormatStrataCarverSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, SeedThreshold)
		SHADER_PARAMETER(int32, WorleyCells)
		SHADER_PARAMETER(int32, SeedDetail)
		SHADER_PARAMETER(float, WorleyJitter)
		SHADER_PARAMETER(float, BandFrequency)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, CostAmount)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SeedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SeedRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFamilies)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutAux)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutDist)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"SeedCS",
	SF_Compute);

// One propagation step, ping-ponged.
class FMixtormatStrataCarverPropagateCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverPropagateCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverPropagateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, BandFrequency)
		SHADER_PARAMETER(float, StrataFrequency)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, StrataWarp)
		SHADER_PARAMETER(float, StepScale)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, PushAmount)
		SHADER_PARAMETER(float, PushDecay)
		SHADER_PARAMETER(int32, IterationStride)
		SHADER_PARAMETER(int32, IterationFamily)
		SHADER_PARAMETER(int32, IterationOp)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PrevDist)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FamilyField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, Aux)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, NextDist)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverPropagateCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"PropagateCS",
	SF_Compute);

// Raw distance -> display remap -> carve -> height. Everything cosmetic is here and only here.
class FMixtormatStrataCarverResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStrataCarverResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStrataCarverResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, StrataFrequency)
		SHADER_PARAMETER(float, StrataAmount)
		SHADER_PARAMETER(float, StrataWarp)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(float, MaskInfluence)
		SHADER_PARAMETER(float, IDInfluence)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Bias)
		SHADER_PARAMETER(float, RemapInMin)
		SHADER_PARAMETER(float, RemapInMax)
		SHADER_PARAMETER(float, RemapOutMin)
		SHADER_PARAMETER(float, RemapOutMax)
		SHADER_PARAMETER(float, CarveClampMin)
		SHADER_PARAMETER(float, CarveClampMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SolvedDist)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ResolveRegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStrataCarverResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatStrataCarver.usf",
	"ResolveCS",
	SF_Compute);

namespace
{
	// One Strata Carver child.
	//
	// Takes the height currently standing as the layer's input and returns the carved one. The
	// caller chains them, so two carvers on one layer are two carves of one surface rather than
	// two competing for the same slot.
	FRDGTextureRef AddStrataCarverPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef SourceHeight)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FStrataCarverRenderData& Carver = Child.Generator.StrataCarver;
		const int32 LayerIndex = LayerCtx.LayerIndex;

		// Depth 0 is the off switch and it is exact: the resolve writes
		// SourceHeight - Carve * 0, which is SourceHeight bit for bit. Skipping here rather than
		// running the whole solve to reproduce the input is worth up to 128 dispatches.
		if (Carver.Depth <= 0.0f)
		{
			return SourceHeight;
		}

		const FIntPoint SolveSize = StrataSolveResolution(Request.Resolution);

		// The scoped mask. Independent scope, so the chain starts from white rather than
		// inheriting whatever the layer mask happens to be at this row: a mask authored under a
		// generator describes where *this generator* acts, and folding the layer's own coverage
		// into it would make Mask Influence mean two different things depending on row order.
		const bool bHasScopedMask = HasScopedMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef ScopedMask = bHasScopedMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
			: LayerCtx.CombinedMask;

		// Region IDs published above this generator in the same layer, by the nearest producer.
		// Absent is the normal case and not an error -- a generator makes its own structure and
		// never needs one.
		FRDGTextureRef RegionIds =
			FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		const bool bHasRegionIds = RegionIds != nullptr;
		if (!bHasRegionIds)
		{
			RegionIds = Ctx.EmptyRegionIds;
		}

		const FIntVector SolveGroups(
			FMath::DivideAndRoundUp(SolveSize.X, 8),
			FMath::DivideAndRoundUp(SolveSize.Y, 8),
			1);
		const FIntVector OutputGroups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		// RGBA16F for the four family fractals -- they are 0..1 and half is plenty. 32-bit for
		// the cost and the distance: cost reaches five figures where a mask confines the solve,
		// and the distance has to stay separable from MaxValue after a hundred accumulated
		// steps, which is exactly the case half floats lose.
		const FRDGTextureDesc FamilyDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc PairDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_G32R32F, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef FamilyField =
			GraphBuilder.CreateTexture(FamilyDesc, TEXT("Mixtormat.StrataFamilies"));
		FRDGTextureRef Aux =
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataAux"));
		FRDGTextureRef DistTargets[2] = {
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataDistA")),
			GraphBuilder.CreateTexture(PairDesc, TEXT("Mixtormat.StrataDistB"))};

		// RDG rejects a pass that reads a transient nothing has written, and the very first
		// propagation reads whichever half the seed did not fill.
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DistTargets[1]), FVector4f(0.0f));

		// The mask, reduced onto the solve grid. Always allocated so the seed pass has a bound
		// resource, but only filled when there is a mask to reduce -- and cleared to white when
		// there is not, because an unwritten transient is an RDG error and a black one would
		// read as "masked out everywhere" and silently produce no carve at all.
		const FRDGTextureDesc SolveMaskDesc = FRDGTextureDesc::Create2D(
			SolveSize, PF_R16F, FClearValueBinding::White,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef SolveMask =
			GraphBuilder.CreateTexture(SolveMaskDesc, TEXT("Mixtormat.StrataSolveMask"));
		if (bHasScopedMask)
		{
			FMixtormatStrataCarverMaskReduceCS::FParameters* MP =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverMaskReduceCS::FParameters>();
			MP->SolveSize = SolveSize;
			MP->OutputSize = Request.Resolution;
			MP->MaskFootprint = FIntPoint(
				FMath::Max(Request.Resolution.X / FMath::Max(SolveSize.X, 1), 1),
				FMath::Max(Request.Resolution.Y / FMath::Max(SolveSize.Y, 1), 1));
			MP->FullResMask = ScopedMask;
			MP->OutSolveMask = GraphBuilder.CreateUAV(SolveMask);

			TShaderMapRef<FMixtormatStrataCarverMaskReduceCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.MaskReduce.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				MP,
				SolveGroups);
		}
		else
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SolveMask), FVector4f(1.0f));
		}

		{
			FMixtormatStrataCarverSeedCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverSeedCS::FParameters>();
			P->SolveSize = SolveSize;
			P->OutputSize = Request.Resolution;
			P->Seed = Carver.Seed;
			P->SeedThreshold = Carver.SeedThreshold;
			P->WorleyCells = Carver.WorleyCells;
			P->SeedDetail = Carver.SeedDetail;
			P->WorleyJitter = Carver.WorleyJitter;
			P->BandFrequency = Carver.BandFrequency;
			P->MaxValue = Carver.MaxValue;
			P->CostAmount = Carver.CostAmount;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->SeedMask = SolveMask;
			P->SeedRegionIds = RegionIds;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutFamilies = GraphBuilder.CreateUAV(FamilyField);
			P->OutAux = GraphBuilder.CreateUAV(Aux);
			P->OutDist = GraphBuilder.CreateUAV(DistTargets[0]);

			TShaderMapRef<FMixtormatStrataCarverSeedCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.Seed.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				P,
				SolveGroups);
		}

		int32 ReadIndex = 0;
		{
			TShaderMapRef<FMixtormatStrataCarverPropagateCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			for (int32 Iteration = 0; Iteration < Carver.Iterations; ++Iteration)
			{
				const int32 WriteIndex = 1 - ReadIndex;
				FMixtormatStrataCarverPropagateCS::FParameters* P =
					GraphBuilder.AllocParameters<FMixtormatStrataCarverPropagateCS::FParameters>();
				P->SolveSize = SolveSize;
				P->Seed = Carver.Seed;
				P->BandFrequency = Carver.BandFrequency;
				P->StrataFrequency = Carver.StrataFrequency;
				P->StrataAmount = Carver.StrataAmount;
				P->StrataWarp = Carver.StrataWarp;
				P->StepScale = Carver.StepScale;
				P->MaxValue = Carver.MaxValue;
				P->PushAmount = Carver.PushAmount;
				P->PushDecay = Carver.PushDecay;
				P->IterationStride = StrataJumpStride(Iteration, Carver.JumpStart);
				P->IterationFamily = static_cast<int32>(
					StrataIterationHash(Carver.OperationSeed, Iteration, 0x2545f491u)
					% static_cast<uint32>(StrataFamilyCount));
				P->IterationOp = static_cast<int32>(
					StrataIterationHash(Carver.OperationSeed, Iteration, 0xad90777du)
					% static_cast<uint32>(StrataOperationCount));
				P->PrevDist = DistTargets[ReadIndex];
				P->FamilyField = FamilyField;
				P->Aux = Aux;
				P->NextDist = GraphBuilder.CreateUAV(DistTargets[WriteIndex]);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.StrataCarver.Propagate.L%d.C%d.It%d",
						LayerIndex, Child.SourceChildIndex, Iteration),
					Shader,
					P,
					SolveGroups);

				ReadIndex = WriteIndex;
			}
		}

		FRDGTextureRef CarvedHeight =
			GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.StrataCarvedHeight"));
		{
			FMixtormatStrataCarverResolveCS::FParameters* P =
				GraphBuilder.AllocParameters<FMixtormatStrataCarverResolveCS::FParameters>();
			P->OutputSize = Request.Resolution;
			P->Seed = Carver.Seed;
			P->StrataFrequency = Carver.StrataFrequency;
			P->StrataAmount = Carver.StrataAmount;
			P->StrataWarp = Carver.StrataWarp;
			P->MaxValue = Carver.MaxValue;
			P->MaskInfluence = Carver.MaskInfluence;
			P->IDInfluence = Carver.IDInfluence;
			P->HasScopedMask = bHasScopedMask ? 1u : 0u;
			P->HasRegionIds = bHasRegionIds ? 1u : 0u;
			P->Depth = Carver.Depth;
			P->Bias = Carver.Bias;
			P->RemapInMin = Carver.RemapInMin;
			P->RemapInMax = Carver.RemapInMax;
			P->RemapOutMin = Carver.RemapOutMin;
			P->RemapOutMax = Carver.RemapOutMax;
			P->CarveClampMin = Carver.ClampMin;
			P->CarveClampMax = Carver.ClampMax;
			P->SolvedDist = DistTargets[ReadIndex];
			P->SourceHeight = SourceHeight;
			P->ResolveMask = ScopedMask;
			P->ResolveRegionIds = RegionIds;
			P->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(CarvedHeight);

			TShaderMapRef<FMixtormatStrataCarverResolveCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.StrataCarver.Resolve.L%d.C%d",
					LayerIndex, Child.SourceChildIndex),
				Shader,
				P,
				OutputGroups);
		}

		return CarvedHeight;
	}
}

void AddGeneratorPasses(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer)
{
	const bool bHasGenerator = Layer.Children.ContainsByPredicate(
		[](const FChildRenderData& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Generator;
		});
	if (!bHasGenerator)
	{
		return;
	}

	// The layer's own maps, resolved into output space. Idempotent -- if a height smooth or a
	// flow warp already asked for this pass it costs nothing, and going through it is what makes
	// the layer's UV transform and pattern UV basis apply exactly once, to the surface the
	// generator is about to carve rather than twice to the carve itself.
	AddLayerInputPass(Ctx, LayerCtx, Layer);

	// The height as it stood before any generator ran. Kept for the normal pass at the end: the
	// relief normal is one delta across the whole chain rather than one per generator, which is
	// both cheaper and more correct -- two carves of the same groove compose into one slope, and
	// RNM-combining each of them separately would tilt the normal twice for a surface that only
	// moved once.
	FRDGTextureRef SourceHeight = LayerCtx.LayerInputHeight;

	// Authored order. Each generator is handed what the previous one produced, so a second
	// carver cuts into the first one's grooves instead of re-cutting the original surface --
	// and because the chain is carried in LayerCtx.LayerInputHeight as well as in the local,
	// anything a later generator's own passes read sees the accumulated result too.
	//
	// A generator that is neutral returns its input unchanged rather than a copy of it (see the
	// Depth guard in AddStrataCarverPasses), so a disabled or zero-depth node in the middle of a
	// chain cannot reset what ran before it: the pointer simply passes through.
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::Generator)
		{
			continue;
		}
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			LayerCtx.LayerInputHeight = AddStrataCarverPasses(
				Ctx, LayerCtx, Layer, Child, LayerCtx.LayerInputHeight);
			break;
		}
	}

	FRDGTextureRef CarvedHeight = LayerCtx.LayerInputHeight;
	if (CarvedHeight == SourceHeight)
	{
		// Every generator on this layer was neutral. Nothing carved, so nothing to re-derive --
		// and skipping is not merely an optimisation: running the delta pass over an unchanged
		// height would still rewrite LayerInputN through the RNM combine, which is not the
		// identity in the last bits.
		return;
	}

	// The normal, through the shared convention and nothing else.
	//
	// AddHeightDerivedNormalPass is the one height->normal in the plugin -- erosion, craquelure
	// relief, region relief and worn edges all go through it -- and it takes the *delta* between
	// the height before and after, derives a relief normal from it and RNM-combines that onto the
	// normal that was already there. A private Sobel here with its own strength would be a fourth
	// convention, and the header at MixtormatHeightNormal.ush documents what three of those cost
	// the last time: the same height field differentiated in worlds three orders of magnitude
	// apart.
	//
	// HeightDerivedNormalStrength, which is what every other caller of this pass uses --
	// erosion, craquelure relief and region relief all pass it, and passing anything else here
	// would be the private convention this node is not allowed to have. It is not the neutral
	// gain: MixtormatHeightDeltaNormal carries its own Sobel rather than going through
	// MixtormatHeightNormal.ush, and its /8 is cancelled by this 8 to land on the pixel-space
	// gradient the header names as this pass's convention. ReliefNormalStrength is the neutral
	// gain for the *other* path -- the shaders that include MixtormatHeightNormal.ush, where the
	// pixel-to-slope conversion has already happened -- and using it here would make the same
	// carve depth read eight times shallower than an erosion of the same depth.
	//
	// A second target rather than writing LayerInputN in place -- RDG will not let one pass read
	// and write the same resource, and the composite reads whichever one this leaves behind.
	FRDGTextureRef CarvedNormal = Ctx.GraphBuilder.CreateTexture(
		LayerCtx.LayerInputN->Desc, TEXT("Mixtormat.StrataCarvedNormal"));
	AddHeightDerivedNormalPass(
		Ctx,
		SourceHeight,
		CarvedHeight,
		LayerCtx.LayerInputN,
		LayerCtx.LayerInputRAM,
		CarvedNormal,
		nullptr,
		Ctx.Request.Resolution,
		HeightDerivedNormalStrength,
		0.0f,
		false,
		TEXT("StrataCarver"));
	LayerCtx.LayerInputN = CarvedNormal;
}

}
