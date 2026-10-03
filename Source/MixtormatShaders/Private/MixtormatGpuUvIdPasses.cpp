// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuPatternPassesInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

// Region bounding boxes, and the per-region centre UV resolved from them.
class FMixtormatRegionBoundsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionBoundsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionBoundsCS, FGlobalShader);

	static constexpr uint32 StageInit = 0;
	static constexpr uint32 StageReduce = 1;
	static constexpr uint32 StageResolve = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, Axis)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionRootIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, RegionBounds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputCentreUV)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionBoundsCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"BoundsCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// UV From IDs: one generic centre solve per Region-ID producer, reused by every UV node that
	// resolves to that producer. The scratch reduction is one axis at a time to halve peak memory.
	//
	// Culled cleanly when nothing above publishes IDs. No output is registered, so the composite
	// falls back to the layer's ordinary placement -- the node does nothing rather than blackening
	// the layer or inventing a region 0 to key off.
	void AddUvIdPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		if (!Layer.bEnabled)
		{
			return;
		}
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint OutputSize = Ctx.Request.Resolution;
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);

		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::UvFromIds)
			{
				continue;
			}
			int32 ProducerIndex = INDEX_NONE;
			FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
				Layer, LayerCtx.RegionIdMaps, Child, ProducerIndex);
			if (!RegionIds)
			{
				// Deferred IDs are not ready before source sampling. Leave UV unavailable:
				// moving this solve later can cycle through masks that already read the source.
				continue;
			}

			FRDGTextureRef CentreUV = nullptr;
			for (const FRegionCentreCacheEntry& Entry : LayerCtx.RegionCentreCache)
			{
				if (Entry.SourceChildIndex == ProducerIndex)
				{
					CentreUV = Entry.CentreUV;
					break;
				}
			}

			if (!CentreUV)
			{
				TMap<FRDGTextureRef, FRDGTextureRef> RootCache;
				const FRDGTextureRef RegionRootIds = AddRegionIndexPasses(
					GraphBuilder, RegionIds, HasPixelRootIds(Layer, ProducerIndex), OutputSize, RootCache);
				const uint32 PixelCount =
					static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);

				// Two ints per possible root instead of four. X and Y are reduced in sequence
				// through the same scratch allocation. At 4K this is 128 MiB instead of 256 MiB.
				FRDGBufferRef RegionBounds = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), PixelCount * 2u),
					TEXT("Mixtormat.UvId.RegionBounds"));
				const FRDGBufferUAVRef RegionBoundsUAV = GraphBuilder.CreateUAV(RegionBounds);

				CentreUV = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						OutputSize,
						PF_G16R16F,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.UvId.CentreUV"));
				const FRDGTextureUAVRef CentreUAV = GraphBuilder.CreateUAV(CentreUV);

				TShaderMapRef<FMixtormatRegionBoundsCS> BoundsShader(
					GetGlobalShaderMap(GMaxRHIFeatureLevel));
				for (uint32 Axis = 0; Axis < 2; ++Axis)
				{
					for (uint32 Stage = FMixtormatRegionBoundsCS::StageInit;
						Stage <= FMixtormatRegionBoundsCS::StageResolve;
						++Stage)
					{
						FMixtormatRegionBoundsCS::FParameters* Parameters =
							GraphBuilder.AllocParameters<FMixtormatRegionBoundsCS::FParameters>();
						Parameters->OutputSize = OutputSize;
						Parameters->Stage = Stage;
						Parameters->Axis = Axis;
						Parameters->RegionIds = RegionIds;
						Parameters->RegionRootIds = RegionRootIds;
						Parameters->RegionBounds = RegionBoundsUAV;
						Parameters->OutputCentreUV = CentreUAV;
						// Default UAV barriers are intentional: each axis must finish its
						// reduction before resolve, and X resolve must finish before Y writes.
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.UvIds.L%d.P%d.Axis%u.Stage%u",
								LayerCtx.LayerIndex, ProducerIndex, Axis, Stage),
							BoundsShader, Parameters, Groups);
					}
				}

				FRegionCentreCacheEntry& Cache =
					LayerCtx.RegionCentreCache.AddDefaulted_GetRef();
				Cache.SourceChildIndex = ProducerIndex;
				Cache.CentreUV = CentreUV;
			}

			FUvIdPassOutput& Output = LayerCtx.UvIdOutputs.AddDefaulted_GetRef();
			Output.SourceChildIndex = Child.SourceChildIndex;
			Output.Ids = RegionIds;
			Output.CentreUV = CentreUV;
			Output.Settings = &Child.UvId;
			// Herringbone and Basketweave turn alternate pieces a quarter turn, and that basis is
			// a property of the lattice rather than something recoverable from a blob. Carried
			// through only when the producer this node actually resolved to is such a Pattern; any
			// other producer leaves it null, and the node says so rather than guessing.
			for (const FPatternIdPassOutput& PatternOutput : LayerCtx.PatternOutputs)
			{
				if (PatternOutput.SourceChildIndex == ProducerIndex
					&& PatternOutput.Settings
					&& HasIntrinsicPatternOrientation(*PatternOutput.Settings))
				{
					Output.Orientation = PatternOutput.Orientation;
					Output.bIntrinsicOrientation = true;
					break;
				}
			}
		}
	}
}
