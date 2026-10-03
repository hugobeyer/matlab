// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "Async/Async.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"

// One stat per family, so `stat gpu` and ProfileGPU show where a composite's time goes.
DECLARE_GPU_STAT_NAMED(MixtormatCompose, TEXT("Mixtormat Compose"));
DECLARE_GPU_STAT_NAMED(MixtormatCacheRestore, TEXT("Mixtormat Cache Restore"));
DECLARE_GPU_STAT_NAMED(MixtormatCacheSave, TEXT("Mixtormat Cache Save"));
DECLARE_GPU_STAT_NAMED(MixtormatLayer, TEXT("Mixtormat Layer"));
DECLARE_GPU_STAT_NAMED(MixtormatRegionIds, TEXT("Mixtormat Region IDs"));
DECLARE_GPU_STAT_NAMED(MixtormatGenerators, TEXT("Mixtormat Generators"));
DECLARE_GPU_STAT_NAMED(MixtormatChildren, TEXT("Mixtormat Masks and Effects"));
DECLARE_GPU_STAT_NAMED(MixtormatComposite, TEXT("Mixtormat Composite"));
DECLARE_GPU_STAT_NAMED(MixtormatStructure, TEXT("Mixtormat Erosion Relief Breakup Wear"));
DECLARE_GPU_STAT_NAMED(MixtormatLayerBlur, TEXT("Mixtormat Layer Blur"));
DECLARE_GPU_STAT_NAMED(MixtormatFinalAO, TEXT("Mixtormat Final AO"));
DECLARE_GPU_STAT_NAMED(MixtormatFinalNormal, TEXT("Mixtormat Final Normal"));

// Normal from the finished height with the composited detail on top. See MixtormatFinalNormal.usf.
class FMixtormatFinalNormalCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFinalNormalCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFinalNormalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FinalHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DetailNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatFinalNormalCS, "/Plugin/Mixtormat/Private/MixtormatFinalNormal.usf", "MainCS", SF_Compute);

// AO from the finished height, once, after every layer. See MixtormatFinalAO.usf.
class FMixtormatFinalAOCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFinalAOCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFinalAOCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, RadiusPixels)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FinalHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatFinalAOCS, "/Plugin/Mixtormat/Private/MixtormatFinalAO.usf", "MainCS", SF_Compute);

// The ground every stack composites onto. Values are the neutral read for each buffer, in that
// buffer's own encoding: BaseColor is mid gray, Normal is encoded +Z, PackedRAM is roughness 0.5
// with AO 1 and metallic 0, and Height is the midpoint 0.5 used by height comparisons.
namespace MixtormatSubstrate
{
	static const FVector4f BaseColor(0.5f, 0.5f, 0.5f, 1.0f);
	static const FVector4f Normal(0.5f, 0.5f, 1.0f, 1.0f);
	static const FVector4f PackedRAM(0.5f, 1.0f, 0.0f, 0.04f);
	static const FVector4f Height(0.5f, 0.0f, 0.0f, 0.0f);
}

namespace MixtormatGpuCompositor
{
	// Kept local to the owned GPU translation units until the internal typed-ref seam lands.
	TArray<TPair<int32, FRDGTextureRef>> GetRegionIdView(
		const FLayerRenderData& Layer,
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const FChildRenderData* Child);

	// Legacy pass families resolve IDs from LayerCtx. Give them an isolated view, then
	// retain any IDs they publish without exposing another group's private producers.
	class FScopedRegionIdView
	{
	public:
		FScopedRegionIdView(FMixtormatLayerPassContext& InLayerCtx,
			const FLayerRenderData& Layer, const FChildRenderData* Child, const bool bEnabled)
			: LayerCtx(InLayerCtx), bActive(bEnabled)
		{
			if (bActive)
			{
				TArray<TPair<int32, FRDGTextureRef>> View = GetRegionIdView(Layer, LayerCtx.RegionIdMaps, Child);
				SavedMaps = MoveTemp(LayerCtx.RegionIdMaps);
				LayerCtx.RegionIdMaps = MoveTemp(View);
			}
		}

		~FScopedRegionIdView()
		{
			if (!bActive)
			{
				return;
			}
			for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
			{
				TPair<int32, FRDGTextureRef>* Saved = SavedMaps.FindByPredicate(
					[&Entry](const TPair<int32, FRDGTextureRef>& Map) { return Map.Key == Entry.Key; });
				if (Saved)
				{
					Saved->Value = Entry.Value;
				}
				else
				{
					PublishRegionIds(SavedMaps, Entry.Key, Entry.Value);
				}
			}
			LayerCtx.RegionIdMaps = MoveTemp(SavedMaps);
		}

	private:
		FMixtormatLayerPassContext& LayerCtx;
		TArray<TPair<int32, FRDGTextureRef>> SavedMaps;
		bool bActive;
	};

	// Copies what layers above LayerIndex can read from it and everything below it into targets
	// extracted at the end of the graph. Returns null -- keeping nothing -- when the stack cannot
	// be resumed exactly: over budget, or a published output that is really a ping-pong slot a
	// later layer overwrites, whose value at resume time the snapshot could not reproduce.
	static TSharedPtr<FMixtormatPrefixCache::FEntry, ESPMode::ThreadSafe> SavePrefixSnapshot(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const int32 LayerIndex,
		const int32 WriteIndex,
		const TMap<FGuid, int32>& LayerIndexById)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;

		const auto IsSharedSlot = [&Ctx, &LayerCtx](const FRDGTextureRef Texture)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (Texture == Ctx.OutputBC[Index] || Texture == Ctx.OutputN[Index]
					|| Texture == Ctx.OutputRAM[Index] || Texture == Ctx.OutputHeight[Index]
					|| Texture == Ctx.OutputDebug[Index] || Texture == Ctx.OutputRegionIdPick[Index]
					|| Texture == LayerCtx.MaskTargets[Index] || Texture == LayerCtx.RidgeTargets[Index]
					|| Texture == LayerCtx.OccupancyTargets[Index]
					|| Texture == LayerCtx.EffectTargets[Index] || Texture == LayerCtx.EffectHeightTargets[Index])
				{
					return true;
				}
			}
			return false;
		};
		const auto LayerOf = [&LayerIndexById](const FGuid& Id)
		{
			const int32* Found = LayerIndexById.Find(Id);
			return Found ? *Found : MAX_int32;
		};

		const auto TextureBytes = [](const FRDGTextureRef Texture)
		{
			return static_cast<uint64>(Texture->Desc.Extent.X) * static_cast<uint64>(Texture->Desc.Extent.Y)
				* static_cast<uint64>(GPixelFormats[Texture->Desc.Format].BlockBytes);
		};
		const FRDGTextureRef Accumulation[6] = {
			Ctx.OutputBC[WriteIndex], Ctx.OutputN[WriteIndex], Ctx.OutputRAM[WriteIndex],
			Ctx.OutputHeight[WriteIndex], LayerCtx.RidgeTargets[WriteIndex],
			LayerCtx.OccupancyTargets[WriteIndex]};
		uint64 Bytes = 0;
		for (const FRDGTextureRef Texture : Accumulation)
		{
			Bytes += TextureBytes(Texture);
		}

		TArray<TPair<FPublishedMaskKey, FRDGTextureRef>> Published;
		for (const TPair<FPublishedMaskKey, FRDGTextureRef>& Pair : Ctx.PublishedMaskOutputs)
		{
			if (LayerOf(Pair.Key.LayerId) > LayerIndex || !Pair.Value)
			{
				continue;
			}
			if (IsSharedSlot(Pair.Value))
			{
				UE_LOG(LogMixtormatComposition, Verbose,
					TEXT("Prefix cache: not saving layer %d -- published output '%s' is a shared ping-pong slot."),
					LayerIndex, *Pair.Key.Output.ToString());
				return nullptr;
			}
			Published.Add(Pair);
			Bytes += TextureBytes(Pair.Value);
		}
		TArray<TPair<FPublishedFieldKey, FPublishedField>> PublishedFields;
		TSet<FRDGTextureRef> KeptFieldTextures;
		for (const auto& Pair : Ctx.PublishedFieldOutputs)
		{
			if (LayerOf(Pair.Key.LayerId) > LayerIndex || !Pair.Value.IsComplete()) { continue; }
			const FRDGTextureRef Textures[] = {Pair.Value.Texture, Pair.Value.FlowSmooth, Pair.Value.Validity};
			for (FRDGTextureRef Texture : Textures)
			{
				if (!Texture) { continue; }
				if (IsSharedSlot(Texture)) { return nullptr; }
				if (!KeptFieldTextures.Contains(Texture))
				{
					KeptFieldTextures.Add(Texture);
					Bytes += TextureBytes(Texture);
				}
			}
			PublishedFields.Add(Pair);
		}
		if (Bytes > Request.CacheBudgetBytes)
		{
			UE_LOG(LogMixtormatComposition, Verbose,
				TEXT("Prefix cache: not saving layer %d -- %llu MB exceeds Mixtormat.ComposeCacheBudgetMB."),
				LayerIndex, Bytes / (1024ull * 1024ull));
			return nullptr;
		}

		RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatCacheSave, "Mixtormat.CacheSave.Layer%d", LayerIndex);
		TSharedPtr<FMixtormatPrefixCache::FEntry, ESPMode::ThreadSafe> Entry =
			MakeShared<FMixtormatPrefixCache::FEntry, ESPMode::ThreadSafe>();
		Entry->Key = Request.PrefixHashes[LayerIndex];
		Entry->LayerIndex = LayerIndex;
		Entry->Resolution = Request.Resolution;

		const auto Keep = [&GraphBuilder](const FRDGTextureRef Source, TRefCountPtr<IPooledRenderTarget>* Out, const TCHAR* Name)
		{
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				Source->Desc.Extent, Source->Desc.Format, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			const FRDGTextureRef Copy = GraphBuilder.CreateTexture(Desc, Name);
			AddCopyTexturePass(GraphBuilder, Source, Copy);
			GraphBuilder.QueueTextureExtraction(Copy, Out);
		};
		Keep(Accumulation[0], &Entry->BaseColor, TEXT("Mixtormat.CacheSave.BaseColor"));
		Keep(Accumulation[1], &Entry->Normal, TEXT("Mixtormat.CacheSave.Normal"));
		Keep(Accumulation[2], &Entry->RAM, TEXT("Mixtormat.CacheSave.RAM"));
		Keep(Accumulation[3], &Entry->Height, TEXT("Mixtormat.CacheSave.Height"));
		Keep(Accumulation[4], &Entry->Ridge, TEXT("Mixtormat.CacheSave.Ridge"));
		Keep(Accumulation[5], &Entry->Occupancy, TEXT("Mixtormat.CacheSave.Occupancy"));

		// The remaining snapshots are already dedicated copies nothing writes again, so they are
		// extracted as they are. Arrays are sized before any extraction pointer is taken.
		int32 HeightCount = 0;
		for (const TPair<int32, FRDGTextureRef>& Pair : Ctx.HeightSnapshots)
		{
			HeightCount += Pair.Key <= LayerIndex ? 1 : 0;
		}
		Entry->HeightSnapshots.Reserve(HeightCount);
		for (const TPair<int32, FRDGTextureRef>& Pair : Ctx.HeightSnapshots)
		{
			if (Pair.Key <= LayerIndex)
			{
				auto& Slot = Entry->HeightSnapshots.Emplace_GetRef(Pair.Key, TRefCountPtr<IPooledRenderTarget>());
				GraphBuilder.QueueTextureExtraction(Pair.Value, &Slot.Value);
			}
		}

		for (const FGuid& Demanded : Ctx.DriverSnapshotDemand)
		{
			if (LayerOf(Demanded) <= LayerIndex)
			{
				Entry->DriverDemandCovered.Add(Demanded);
			}
		}
		int32 DriverCount = 0;
		for (const TPair<FGuid, FRDGTextureRef>& Pair : Ctx.DriverSnapshots)
		{
			DriverCount += LayerOf(Pair.Key) <= LayerIndex ? 1 : 0;
		}
		Entry->DriverSnapshots.Reserve(DriverCount);
		for (const TPair<FGuid, FRDGTextureRef>& Pair : Ctx.DriverSnapshots)
		{
			if (LayerOf(Pair.Key) <= LayerIndex)
			{
				auto& Slot = Entry->DriverSnapshots.Emplace_GetRef(Pair.Key, TRefCountPtr<IPooledRenderTarget>());
				GraphBuilder.QueueTextureExtraction(Pair.Value, &Slot.Value);
			}
		}

		Entry->PublishedMasks.Reserve(Published.Num());
		for (const TPair<FPublishedMaskKey, FRDGTextureRef>& Pair : Published)
		{
			auto& Slot = Entry->PublishedMasks.Emplace_GetRef(Pair.Key, TRefCountPtr<IPooledRenderTarget>());
			GraphBuilder.QueueTextureExtraction(Pair.Value, &Slot.Value);
		}
		Entry->PublishedFields.Reserve(PublishedFields.Num());
		for (const auto& Pair : PublishedFields)
		{
			auto& Slot = Entry->PublishedFields.AddDefaulted_GetRef();
			Slot.Key = Pair.Key;
			Slot.Kind = Pair.Value.Kind;
			Slot.bHashedIds = Pair.Value.bHashedIds;
			GraphBuilder.QueueTextureExtraction(Pair.Value.Texture, &Slot.Texture);
			if (Pair.Value.FlowSmooth) { GraphBuilder.QueueTextureExtraction(Pair.Value.FlowSmooth, &Slot.FlowSmooth); }
			if (Pair.Value.Validity) { GraphBuilder.QueueTextureExtraction(Pair.Value.Validity, &Slot.Validity); }
		}
		for (const FPublishedFieldKey& Demand : Ctx.PublishedFieldDemand)
		{
			if (LayerOf(Demand.LayerId) <= LayerIndex) { Entry->FieldDemandCovered.Add(Demand); }
		}
		return Entry;
	}

	void EnqueueCompose(FRenderRequest&& Request)
	{
		ENQUEUE_RENDER_COMMAND(MixtormatComposite)(
			[Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList) mutable
			{
				// Every exit, including the failure returns below, ends the in-flight window.
				ON_SCOPE_EXIT
				{
					if (Request.InFlight.IsValid())
					{
						Request.InFlight->store(false);
					}
				};
				// Child commands were enqueued first on the same game thread. Their RDG graphs
				// finish submission and transition outputs to SRVs before this graph reads them.
				for (FLayerRenderData& Layer : Request.Layers)
				{
					if (!Layer.SourceOutputs.IsValid())
					{
						continue;
					}
					const FMixtormatComposeResources& Source = *Layer.SourceOutputs;
					if (!Source.bSucceeded)
					{
						UE_LOG(LogMixtormatComposition, Error, TEXT("Reference composition failed on the render thread."));
						return;
					}
					const int32 Index = Source.PublishedIndex;
					Layer.BaseColor = Source.BaseColor[Index]->GetRenderTargetTexture();
					Layer.Normal = Source.Normal[Index]->GetRenderTargetTexture();
					Layer.RAM = Source.RAM[Index]->GetRenderTargetTexture();
					Layer.Height = Source.Height[Index]->GetRenderTargetTexture();
				}
				for (int32 Index = 0; Index < 2; ++Index)
				{
					Request.OutputBC[Index] = Request.Targets->BaseColor[Index]->GetRenderTargetTexture();
					Request.OutputN[Index] = Request.Targets->Normal[Index]->GetRenderTargetTexture();
					Request.OutputRAM[Index] = Request.Targets->RAM[Index]->GetRenderTargetTexture();
					Request.OutputHeight[Index] = Request.Targets->Height[Index]->GetRenderTargetTexture();
					Request.OutputDebug[Index] = Request.Targets->Debug[Index]->GetRenderTargetTexture();
					Request.OutputRegionIdPick[Index] =
						Request.Targets->RegionIdPick[Index]->GetRenderTargetTexture();
					if (!Request.OutputBC[Index].IsValid() || !Request.OutputN[Index].IsValid()
						|| !Request.OutputRAM[Index].IsValid() || !Request.OutputHeight[Index].IsValid()
						|| !Request.OutputDebug[Index].IsValid()
						|| !Request.OutputRegionIdPick[Index].IsValid())
					{
						UE_LOG(LogMixtormatComposition, Error, TEXT("Composition target initialization failed."));
						return;
					}
				}
				FRDGBuilder GraphBuilder(RHICmdList);
				FMixtormatComposeContext Ctx(GraphBuilder, Request);
				// Filled during the graph and stored once it has executed, when the extracted
				// targets actually exist.
				TSharedPtr<FMixtormatPrefixCache::FEntry, ESPMode::ThreadSafe> PendingSnapshot;
				{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatCompose, "Mixtormat.Compose");
				TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
				FRDGTextureRef* const OutputBC = Ctx.OutputBC;
				FRDGTextureRef* const OutputN = Ctx.OutputN;
				FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
				FRDGTextureRef* const OutputHeight = Ctx.OutputHeight;
				FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
				FRDGTextureRef* const OutputRegionIdPick = Ctx.OutputRegionIdPick;
				for (int32 Index = 0; Index < 2; ++Index)
				{
					OutputBC[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputBC[Index],
						TEXT("Mixtormat.OutputBC"));
					OutputN[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputN[Index],
						TEXT("Mixtormat.OutputN"));
					OutputRAM[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputRAM[Index],
						TEXT("Mixtormat.OutputRAM"));
					OutputHeight[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputHeight[Index],
						TEXT("Mixtormat.OutputHeight"));
					OutputDebug[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputDebug[Index],
						TEXT("Mixtormat.OutputDebug"));
					OutputRegionIdPick[Index] = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Request.OutputRegionIdPick[Index],
						TEXT("Mixtormat.OutputRegionIdPick"));
				}

				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]),
					FVector4f(DebugClearColor()));
				// Cleared to the sentinel every composite, so a preview that is switched off leaves
				// nothing behind for the picker to read as a real id.
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(OutputRegionIdPick[Request.PublishedTargetIndex]),
					FVector4f(-1.0f, -1.0f, -1.0f, -1.0f));

				// Stand-in for the composite's RegionIds slot on every layer without a cluster
				// filter. RDG validates a binding whether the shader branches on it or not, so the
				// slot has to hold something real; one 1x1 texture for the whole graph is the
				// cheapest something there is. Cleared to the no-region sentinel, so if the tint
				// branch were ever entered against it the result is a pass-through rather than a
				// colour hashed out of uninitialised memory.
				FRDGTextureRef& EmptyRegionIds = Ctx.EmptyRegionIds;
				EmptyRegionIds = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1),
						PF_R32_UINT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.EmptyRegionIds"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyRegionIds), 0xffffffffu);

				FRDGTextureRef& EmptyPatternUV = Ctx.EmptyPatternUV;
				EmptyPatternUV = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1),
						PF_G16R16F,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.EmptyPatternUV"));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(EmptyPatternUV),
					FVector4f(0.5f, 0.5f, 0.0f, 0.0f));

				FRDGTextureRef& EmptyPatternOrientation = Ctx.EmptyPatternOrientation;
				EmptyPatternOrientation = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1),
						PF_R32_FLOAT,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.EmptyPatternOrientation"));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(EmptyPatternOrientation),
					0.0f);

				// No Driver on this layer, or one that could not resolve: the slot still needs a real
				// resource. Cleared to zero, which every Combine mode turns into a no-op once Amount
				// is applied -- and the shader's Enabled flag stops it being read at all.
				FRDGTextureRef& EmptyDriverSignal = Ctx.EmptyDriverSignal;
				EmptyDriverSignal = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1),
						PF_R16F,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.EmptyDriverSignal"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyDriverSignal), FVector4f::Zero());

				// The composite's debug slot on every layer that is not the selected debug layer.
				// RDG takes the dependency from the binding, not from the branch the shader takes,
				// so binding the real target there would put the whole layer stack into that
				// full-resolution texture's chain for a write that never happens. Cleared to zero so
				// a stray write would show up rather than read back as uninitialised memory.
				FRDGTextureRef& EmptyDebugOutput = Ctx.EmptyDebugOutput;
				EmptyDebugOutput = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1),
						PF_FloatRGBA,
						FClearValueBinding::None,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.EmptyDebugOutput"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyDebugOutput), FVector4f::Zero());

				// Demand-driven: only layers some other layer's Driver actually names get a snapshot,
				// because the mask pair is rotated across the whole graph and a layer's combined mask
				// is overwritten by the next layer that runs. Collected before the loop so layer N
				// already knows whether it has to be copied when its own mask is final.
				TSet<FGuid>& DriverSnapshotDemand = Ctx.DriverSnapshotDemand;
				for (const FLayerRenderData& DemandLayer : Request.Layers)
				{
					for (const FScalarDriverRenderData& Driver : DemandLayer.ScalarDrivers)
					{
						// Mask sources only. A region map is never snapshotted -- it is read in the
						// layer that produced it, in that layer's own pixel space.
						if (Driver.bEnabled
							&& !Driver.bRegionSource
							&& Driver.SourceLayerId != DemandLayer.LayerId)
						{
							DriverSnapshotDemand.Add(Driver.SourceLayerId);
						}
					}
				}
				TMap<FGuid, FRDGTextureRef>& DriverSnapshots = Ctx.DriverSnapshots;
				TMap<FPublishedMaskKey, FRDGTextureRef>& PublishedMaskOutputs = Ctx.PublishedMaskOutputs;

				if (Request.Layers.IsEmpty())
				{
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[0]), MixtormatSubstrate::BaseColor);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[0]), MixtormatSubstrate::Normal);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[0]), MixtormatSubstrate::PackedRAM);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputHeight[0]), MixtormatSubstrate::Height);
				}
				else
				{
					FMixtormatLayerPassContext LayerCtx(Ctx);
					FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
					MaskDesc = FRDGTextureDesc::Create2D(
						Request.Resolution,
						PF_R16F,
						FClearValueBinding::White,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
					const FRDGTextureRef MaskTargetsInit[2] =
					{
						GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskA")),
						GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskB"))
					};
					MaskTargets[0] = MaskTargetsInit[0];
					MaskTargets[1] = MaskTargetsInit[1];
					// Both halves cleared before anything reads either. The first mask child on a
					// layer binds the half it is not writing as PreviousMask and ignores the value --
					// Initialize makes it treat Previous as zero -- but RDG validates the binding
					// rather than the use, and a transient nothing has written trips
					// "has a read dependency on Mixtormat.MaskB, but it was never written to" on
					// every composite. The ensure fires once per session and is easy to never see;
					// the read itself was always harmless, and this makes the graph honest about it.
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[0]), FVector4f(0.0f));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[1]), FVector4f(0.0f));

					// The substrate, seeded into the half the bottom layer reads.
					//
					// Layer 0 composites onto this the way every other layer composites onto the
					// layer below it, which is the whole point: it used to seed these buffers
					// itself via Initialize, and a seeding layer ignores its own mask, feature
					// influence and height blend. That made position 0 a different thing to be,
					// so a layer could not be dragged to the bottom without changing what it did.
					//
					// Parity matters here and is easy to get backwards. WriteIndex is
					// LayerIndex & 1, so layer 0 writes half 0 and reads half 1 -- the substrate
					// belongs in half 1. Half 0 needs no seed; layer 0 overwrites it.
					FRDGTextureRef* const HeightTargets = OutputHeight;
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[1]), MixtormatSubstrate::BaseColor);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[1]), MixtormatSubstrate::Normal);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[1]), MixtormatSubstrate::PackedRAM);
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(HeightTargets[0]),
						MixtormatSubstrate::Height);
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(HeightTargets[1]),
						MixtormatSubstrate::Height);

					// Occupancy starts at zero: nothing stands on the substrate yet. Layer 0 reads
					// it like every other layer reads the one below, so the first layer is not a
					// special case -- it simply sees bare ground. Both halves cleared for the same
					// reason the mask pair is: RDG validates the binding rather than the use.
					FRDGTextureRef* const OccupancyTargets = LayerCtx.OccupancyTargets;
					OccupancyTargets[0] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.OccupancyA"));
					OccupancyTargets[1] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.OccupancyB"));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OccupancyTargets[0]), FVector4f(0.0f));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OccupancyTargets[1]), FVector4f(0.0f));

					// Drainage and crest lines, produced by the erosion filter and consumed by
					// generated masks on later layers.
					//
					// Ping-ponged on the layer index like every other surface input a generated
					// mask reads, so a mask on layer N sees what was accumulated below layer N.
					// A single shared target would be read-after-write across layers with no
					// versioning, and a mask would see its own layer's ridge on some layers and
					// not others depending on child order.
					//
					// That means every layer has to write it, not only eroding ones: a layer that
					// left its slot alone would hand the next layer the ridge from two layers
					// back. Layers without erosion copy read to write below.
					FRDGTextureRef* const RidgeTargets = LayerCtx.RidgeTargets;
					RidgeTargets[0] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeA"));
					RidgeTargets[1] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeB"));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[0]), FVector4f(0.0f));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[1]), FVector4f(0.0f));

					FRDGTextureDesc& EffectDesc = LayerCtx.EffectDesc;
					EffectDesc = FRDGTextureDesc::Create2D(
						Request.Resolution,
						PF_FloatRGBA,
						FClearValueBinding::White,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef* const EffectTargets = LayerCtx.EffectTargets;
					EffectTargets[0] = GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectA"));
					EffectTargets[1] = GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectB"));

					// Cleared for the same reason the mask pair is: the first surface effect on a
					// layer binds the half it is not writing and ignores it, and RDG validates the
					// binding rather than the use. White, matching the desc's own clear value and the
					// neutral coverage the effect shader expects to read.
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[0]), FVector4f(1.0f));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[1]), FVector4f(1.0f));
					// Peel relief, signed around zero so stacked peels accumulate.
					FRDGTextureDesc& EffectHeightDesc = LayerCtx.EffectHeightDesc;
					EffectHeightDesc = FRDGTextureDesc::Create2D(
						Request.Resolution,
						PF_R16F,
						FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef* const EffectHeightTargets = LayerCtx.EffectHeightTargets;
					EffectHeightTargets[0] =
						GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightA"));
					EffectHeightTargets[1] =
						GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightB"));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[0]), FVector4f(0.0f));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[1]), FVector4f(0.0f));

					// Placeholders so the peel and peel-field parameter structs always have a
					// bound resource in slots the active mode does not use. Never read, never
					// written; a 1x1 keeps them free.
					const FRDGTextureDesc TinyRGDesc = FRDGTextureDesc::Create2D(
						FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					const FRDGTextureDesc TinyRGBADesc = FRDGTextureDesc::Create2D(
						FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef& PeelNoiseDummy = LayerCtx.PeelNoiseDummy;
					PeelNoiseDummy = GraphBuilder.CreateTexture(TinyRGDesc, TEXT("Mixtormat.PeelNoiseDummy"));
					FRDGTextureRef& PeelFieldDummy = LayerCtx.PeelFieldDummy;
					PeelFieldDummy = GraphBuilder.CreateTexture(TinyRGBADesc, TEXT("Mixtormat.PeelFieldDummy"));

					// Bound wherever a peel input is absent -- the peel's own mask when the layer
					// uses its child mask, and the authored map slots on the procedural path. RDG
					// rejects a pass that reads a transient texture nothing has written, which the
					// log reported as an error on every procedural peel composite, so it is cleared
					// once here rather than left undefined.
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(PeelFieldDummy),
						FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(PeelNoiseDummy),
						FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

					TSet<int32>& RequiredHeightSnapshots = Ctx.RequiredHeightSnapshots;
					for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
					{
						const int32 ReferenceIndex = Request.Layers[LayerIndex].HeightReferenceLayerIndex;
						if (ReferenceIndex >= 0 && ReferenceIndex < LayerIndex)
						{
							RequiredHeightSnapshots.Add(ReferenceIndex);
						}
					}
					TMap<int32, FRDGTextureRef>& HeightSnapshots = Ctx.HeightSnapshots;

					// Layer of each Driver source, for checking a snapshot holds every Driver signal
					// the current stack asks for from below it.
					TMap<FGuid, int32> LayerIndexById;
					for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
					{
						const FLayerRenderData& Layer = Request.Layers[LayerIndex];
						LayerIndexById.Add(Layer.LayerId, LayerIndex);
						for (const FChildRenderData& Child : Layer.Children)
						{
							if (Child.Type == EMixtormatLayerChildType::OutputReference
								&& Child.OutputReference.Source.ChildIndex != INDEX_NONE)
							{
								// Includes scoped group inputs and local alias chains before prefix reuse.
								Ctx.PublishedFieldDemand.Add(Child.OutputReference.Source);
							}
							if (Child.Type == EMixtormatLayerChildType::BoundaryFromIds
								&& Child.BoundaryId.bExplicitSource)
							{
								Ctx.PublishedFieldDemand.Add(Child.BoundaryId.RegionIdsSource.Source);
							}
						}
					}

					// Resume from the deepest kept prefix. What a layer above K can read from below
					// it is the accumulation half K wrote, the ridge half K wrote, and the dedicated
					// snapshots -- restored here into exactly the slots the skipped layers would
					// have left them in.
					int32 FirstLayer = 0;
					const bool bPrefixCache = Request.PrefixCache.IsValid()
						&& Request.PrefixHashes.Num() == Request.Layers.Num();
					if (bPrefixCache)
					{
						const TSharedPtr<FMixtormatPrefixCache::FEntry, ESPMode::ThreadSafe> Resumed =
							Request.PrefixCache->FindDeepest(
								Request.PrefixHashes, Request.Resolution, Request.CacheLayerLimit,
								[&](const FMixtormatPrefixCache::FEntry& Entry)
								{
									for (const int32 Required : RequiredHeightSnapshots)
									{
										if (Required <= Entry.LayerIndex
											&& !Entry.HeightSnapshots.ContainsByPredicate(
												[Required](const TPair<int32, TRefCountPtr<IPooledRenderTarget>>& Pair)
												{
													return Pair.Key == Required;
												}))
										{
											return false;
										}
									}
									for (const FGuid& Demanded : DriverSnapshotDemand)
									{
										const int32* SourceIndex = LayerIndexById.Find(Demanded);
										if (SourceIndex && *SourceIndex <= Entry.LayerIndex
											&& !Entry.DriverDemandCovered.Contains(Demanded))
										{
											return false;
										}
									}
									for (const FPublishedFieldKey& Demand : Ctx.PublishedFieldDemand)
									{
										const int32* SourceIndex = LayerIndexById.Find(Demand.LayerId);
										if (SourceIndex && *SourceIndex <= Entry.LayerIndex
											&& !Entry.FieldDemandCovered.Contains(Demand)) { return false; }
									}
									return true;
								});
						UE_LOG(LogMixtormatComposition, Verbose,
							TEXT("Prefix cache: %s (snapshot layer %d, %d layers)."),
							Resumed.IsValid() ? TEXT("resumed") : TEXT("full composite"),
							Resumed.IsValid() ? Resumed->LayerIndex : INDEX_NONE,
							Request.Layers.Num());
						if (Resumed.IsValid())
						{
							RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatCacheRestore,
								"Mixtormat.CacheRestore.Layer%d", Resumed->LayerIndex);
							const int32 Half = Resumed->LayerIndex & 1;
							const auto Restore = [&GraphBuilder](
								const TRefCountPtr<IPooledRenderTarget>& Source, FRDGTextureRef Target, const TCHAR* Name)
							{
								AddCopyTexturePass(GraphBuilder, GraphBuilder.RegisterExternalTexture(Source, Name), Target);
							};
							Restore(Resumed->BaseColor, OutputBC[Half], TEXT("Mixtormat.Cache.BaseColor"));
							Restore(Resumed->Normal, OutputN[Half], TEXT("Mixtormat.Cache.Normal"));
							Restore(Resumed->RAM, OutputRAM[Half], TEXT("Mixtormat.Cache.RAM"));
							Restore(Resumed->Height, HeightTargets[Half], TEXT("Mixtormat.Cache.Height"));
							Restore(Resumed->Ridge, RidgeTargets[Half], TEXT("Mixtormat.Cache.Ridge"));
							Restore(Resumed->Occupancy, OccupancyTargets[Half], TEXT("Mixtormat.Cache.Occupancy"));
							for (const TPair<int32, TRefCountPtr<IPooledRenderTarget>>& Pair : Resumed->HeightSnapshots)
							{
								HeightSnapshots.Add(Pair.Key, GraphBuilder.RegisterExternalTexture(
									Pair.Value, TEXT("Mixtormat.Cache.HeightSnapshot")));
							}
							for (const TPair<FGuid, TRefCountPtr<IPooledRenderTarget>>& Pair : Resumed->DriverSnapshots)
							{
								DriverSnapshots.Add(Pair.Key, GraphBuilder.RegisterExternalTexture(
									Pair.Value, TEXT("Mixtormat.Cache.DriverSnapshot")));
							}
							for (const auto& Pair : Resumed->PublishedMasks)
							{
								PublishedMaskOutputs.Add(Pair.Key, GraphBuilder.RegisterExternalTexture(
									Pair.Value, TEXT("Mixtormat.Cache.PublishedMask")));
							}
							for (const auto& Slot : Resumed->PublishedFields)
							{
								FPublishedField Field;
								Field.Kind = Slot.Kind;
								Field.bHashedIds = Slot.bHashedIds;
								Field.Texture = GraphBuilder.RegisterExternalTexture(Slot.Texture, TEXT("Mixtormat.Cache.PublishedField"));
								if (Slot.FlowSmooth.IsValid())
								{
									Field.FlowSmooth = GraphBuilder.RegisterExternalTexture(Slot.FlowSmooth, TEXT("Mixtormat.Cache.FlowSmooth"));
								}
								if (Slot.Validity.IsValid())
								{
									Field.Validity = GraphBuilder.RegisterExternalTexture(Slot.Validity, TEXT("Mixtormat.Cache.FlowValidity"));
								}
								Ctx.PublishedFieldOutputs.Add(Slot.Key, Field);
							}
							FirstLayer = Resumed->LayerIndex + 1;
						}
					}

					for (int32 LayerIndex = FirstLayer; LayerIndex < Request.Layers.Num(); ++LayerIndex)
					{
						RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatLayer, "Mixtormat.Layer%d", LayerIndex);
						const FLayerRenderData& Layer = Request.Layers[LayerIndex];
						const bool bHasIdGroups = Layer.Children.ContainsByPredicate(
							[](const FChildRenderData& Child) { return Child.Type == EMixtormatLayerChildType::IdGroup; });
						LayerCtx.BeginLayer(LayerIndex);

						TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
						TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
							LayerCtx.PatternOutputs;
						FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
						CombinedMask = RegisterTexture(GraphBuilder, RegisteredTextures, Layer.Mask,
							TEXT("Mixtormat.WhiteMask"));
						FRDGTextureRef& CombinedEffectData = LayerCtx.CombinedEffectData;
						CombinedEffectData = RegisterTexture(GraphBuilder, RegisteredTextures, Layer.BaseColor,
							TEXT("Mixtormat.DefaultEffectData"));
						LayerCtx.CombinedEffectHeight = EffectHeightTargets[0];
						LayerCtx.DebugMask = CombinedMask;

						{
							RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatRegionIds, "Mixtormat.RegionIds");
							AddOutputReferencePasses(Ctx, LayerCtx, Layer);
							// Transitional child producers keep their established early-publication order.
							AddRegionProducerPasses(Ctx, LayerCtx, Layer);
							// A Generator layer builds its module chain here; each module publishes its own IDs and masks.
							if (Layer.bGenerator)
							{
								AddLayerInputPass(Ctx, LayerCtx, Layer);
								{
									RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatGenerators, "Mixtormat.Generators");
									AddGeneratorLayerPasses(Ctx, LayerCtx, Layer);
								}
								if (LayerCtx.GeneratorBundle.Height)
								{
									LayerCtx.LayerInputHeight = LayerCtx.GeneratorBundle.Height;
									LayerCtx.bGeneratedHeight = true;
								}
							}
							// Local references/groups may now consume evaluated generator modules.
							AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, MAX_int32, false);
							// UV From IDs may now consume the post-flow layer producer.
							AddUvIdPasses(Ctx, LayerCtx, Layer);
						}

						FRDGTextureRef& CombinedEffectHeight = LayerCtx.CombinedEffectHeight;
						CombinedEffectHeight = EffectHeightTargets[0];
						FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
						DebugMask = CombinedMask;
						AddLayerHeightSmoothPasses(Ctx, LayerCtx, Layer);
						if (Layer.bGenerator && LayerCtx.GeneratorBundle.Height)
						{
							const int32 BelowIndex = 1 - (LayerIndex & 1);
							FRDGTextureRef SourceHeight = Ctx.OutputHeight[BelowIndex];
							LayerCtx.bGeneratedHeight = true;
							FRDGTextureRef FormedNormal = GraphBuilder.CreateTexture(
								LayerCtx.LayerInputN->Desc, TEXT("Mixtormat.GeneratorLayerNormal"));
							AddHeightDerivedNormalPass(Ctx, SourceHeight, LayerCtx.LayerInputHeight,
								LayerCtx.LayerInputN, LayerCtx.LayerInputRAM, FormedNormal, nullptr,
								Request.Resolution, HeightDerivedNormalStrength, false,
								TEXT("GeneratorLayer"));
							LayerCtx.LayerInputN = FormedNormal;
						}

						FPendingEffect& PendingErosion = LayerCtx.PendingErosion;

						TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
							LayerCtx.PendingWornEdges;

						// Craquelure relief, deferred out of the child loop for the same reason
						// erosion and chipping are: the loop runs before the layer composites, so a
						// carve made here would be painted straight back over.
						//
						// The distance field is carried rather than looked up again later, because
						// the mask targets it was produced alongside are ping-ponged -- any later
						// mask child overwrites the slot, and relief would then read whichever child
						// happened to run last instead of its own network.
						TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
							LayerCtx.PendingCraquelureReliefs;

						// Region relief and edge shading are deferred for exactly the reason
						// craquelure relief is: their fields exist before the composite, but the
						// surface they modify does not exist until after it.
						TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
							LayerCtx.PendingRampTilts;

						// An array where erosion keeps a single pointer. Two erosions on one layer
						// is nonsense, but a brightness grade and a separate tonemap grade is an
						// ordinary way to use an adjustment layer, and dropping all but the last
						// would read as a bug rather than as a contract.
						TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;
						TArray<FPendingEffect, TInlineAllocator<2>>& PendingLayerBlurs =
							LayerCtx.PendingLayerBlurs;

						{
						RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatChildren, "Mixtormat.MasksAndEffects");
						for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
						{
							const FChildRenderData& Child = Layer.Children[ChildIndex];
							if (Child.Type == EMixtormatLayerChildType::IdGroup
								|| Child.Type == EMixtormatLayerChildType::OutputReference
								|| Child.Type == EMixtormatLayerChildType::CombineId)
							{
								// Finalize unavailable inputs at their owning row, with unfiltered maps.
								// Never execute a future producer to satisfy a local reference.
								AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex + 1, true);
								if (Child.Type == EMixtormatLayerChildType::OutputReference
									&& Child.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds)
								{
									// Non-group scopes are not visited by the group scheduler. Resolve their
									// own row too, so an unavailable alias cannot vanish from the ID chain.
									AddRegionIdReferencePass(Ctx, LayerCtx, Layer, Child, true);
								}
							}
							if (Child.Type == EMixtormatLayerChildType::BoundaryFromIds)
							{
								// Explicit same-owner addresses need the unfiltered maps; implicit
								// lookup applies the existing scope/order policy itself.
								AddBoundaryIdPass(Ctx, LayerCtx, Layer, Child);
								continue;
							}
							FScopedRegionIdView RegionView(LayerCtx, Layer, &Child, bHasIdGroups);
							if (Child.Type == EMixtormatLayerChildType::Generator)
							{
								// Already run, before this loop started. It has no mask to
								// contribute and no effect target to write, so falling through to
								// the effect branch below would read Child.Effect on a child that
								// never had one.
								continue;
							}
							if (Child.Type == EMixtormatLayerChildType::Filter
								|| Child.Type == EMixtormatLayerChildType::PatternId
								|| Child.Type == EMixtormatLayerChildType::HsvFilter
								|| Child.Type == EMixtormatLayerChildType::RampId
								|| Child.Type == EMixtormatLayerChildType::UvFromIds
								|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
								|| Child.Type == EMixtormatLayerChildType::IdGroup
								|| Child.Type == EMixtormatLayerChildType::OutputReference)
							{
								// All three are handled outside this loop -- the cluster in the
								// pre-mask phase, the HSV filter at the composite's albedo sample,
								// the ramp tilt after the composite. None may fall through to the
								// effect branch below.
								continue;
							}
							if (Child.Type == EMixtormatLayerChildType::CombineId)
							{
								// Breakup-dependent chains become available at their authored row.
								AddCombineIdProducerPass(Ctx, LayerCtx, Layer, Child);
								continue;
							}
							if (Child.Type == EMixtormatLayerChildType::Generated)
							{
								AddGeneratedMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
								continue;
							}

							if (Child.Type == EMixtormatLayerChildType::Craquelure)
							{
								AddCraquelureMaskPasses(Ctx, LayerCtx, Layer, Child, ChildIndex);
								continue;
							}

							if (Child.Type == EMixtormatLayerChildType::RandomId)
							{
								AddRandomIdMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
								continue;
							}

							if (Child.Type == EMixtormatLayerChildType::ColorId)
							{
								AddColorIdMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
								continue;
							}

							if (Child.Type == EMixtormatLayerChildType::Mask)
							{
								AddTextureMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
								continue;
							}

							const FEffectRenderData& Effect = Child.Effect;
							if (MixtormatIsGeneratorFlowEffect(Effect.Type))
							{
								// Already run inside its owning Rock Formation, before this loop
								// (AddGeneratorLayerPasses). Must not fall through to the peel default.
								continue;
							}
							FRDGTextureRef FeatureMask =
								AddScopedFeatureMask(
									Ctx, LayerCtx, Layer, Child.SourceChildIndex,
									Effect.Type == EMixtormatEffectType::LayerBlur
										|| Effect.Type == EMixtormatEffectType::Peeling);
							if (Effect.Type == EMixtormatEffectType::Erosion)
							{
								QueuePendingErosion(LayerCtx, Layer, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::Breakup)
							{
								QueuePendingBreakup(Ctx, LayerCtx, Layer, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::WornEdges)
							{
								QueuePendingWornEdges(LayerCtx, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::FlowWarp)
							{
								AddLayerFlowWarpPass(Ctx, LayerCtx, Layer, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::LayerBlur)
							{
								QueuePendingLayerBlur(LayerCtx, Layer, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::Grade)
							{
								QueuePendingGrade(LayerCtx, Layer, Child, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::Stain)
							{
								AddStainMaskPasses(Ctx, LayerCtx, Layer, Child, ChildIndex, Effect, FeatureMask);
								continue;
							}

							if (Effect.Type == EMixtormatEffectType::Runoff)
							{
								AddRunoffMaskPasses(Ctx, LayerCtx, Layer, Child, ChildIndex, Effect, FeatureMask);
								continue;
							}

							AddPeelingEffectPasses(Ctx, LayerCtx, Layer, Child, ChildIndex, Effect, FeatureMask);
						}

						}

						// All IDs, including Breakup-dependent Combine chains, now exist.
						CollectPendingRampTilts(Ctx, LayerCtx, Layer);

						// Structural operators consume local ramp relief, never the already-blended substrate.
						// Keep preparation stable as Amount crosses zero.
						const bool bPrepareStructure = !LayerCtx.PendingBreakups.IsEmpty();
						const int32 LocalWriteIndex = LayerIndex & 1;
						FRDGTextureRef SavedBC = Ctx.OutputBC[LocalWriteIndex];
						FRDGTextureRef SavedN = Ctx.OutputN[LocalWriteIndex];
						FRDGTextureRef SavedRAM = Ctx.OutputRAM[LocalWriteIndex];
						FRDGTextureRef SavedHeight = Ctx.OutputHeight[LocalWriteIndex];
						if (bPrepareStructure)
						{
							// Keep the read side intact for placement/height-reference evaluation.
							// Relief filters write only the isolated incoming material channels.
							Ctx.OutputBC[LocalWriteIndex] = GraphBuilder.CreateTexture(SavedBC->Desc, TEXT("Mixtormat.LocalBC"));
							Ctx.OutputN[LocalWriteIndex] = GraphBuilder.CreateTexture(SavedN->Desc, TEXT("Mixtormat.LocalN"));
							Ctx.OutputRAM[LocalWriteIndex] = GraphBuilder.CreateTexture(SavedRAM->Desc, TEXT("Mixtormat.LocalRAM"));
							Ctx.OutputHeight[LocalWriteIndex] = GraphBuilder.CreateTexture(SavedHeight->Desc, TEXT("Mixtormat.LocalHeight"));
						}
						{
							RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatComposite, "Mixtormat.Composite");
							AddLayerCompositePass(Ctx, LayerCtx, Layer, bPrepareStructure ? 1u : 0u);
						}

						{
							RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatStructure, "Mixtormat.Structure");
							AddErosionPasses(Ctx, LayerCtx, Layer);
							AddRampReliefPasses(Ctx, LayerCtx, Layer);
							AddCraquelureReliefPasses(Ctx, LayerCtx, Layer);

							// Breakup publishes structural IDs during child collection, then authors its relief here.
							// Worn Edges follows it so a Worn Edges row below Breakup can wear the actual generated
							// plate/flake boundaries instead of the pre-breakup surface.
							AddBreakupPasses(Ctx, LayerCtx, Layer);

							AddWornEdgesPasses(Ctx, LayerCtx, Layer);
						}

						if (bPrepareStructure)
						{
							LayerCtx.LayerInputBC = Ctx.OutputBC[LocalWriteIndex];
							LayerCtx.LayerInputN = Ctx.OutputN[LocalWriteIndex];
							LayerCtx.LayerInputRAM = Ctx.OutputRAM[LocalWriteIndex];
							LayerCtx.LayerInputHeight = Ctx.OutputHeight[LocalWriteIndex];
							Ctx.OutputBC[LocalWriteIndex] = SavedBC;
							Ctx.OutputN[LocalWriteIndex] = SavedN;
							Ctx.OutputRAM[LocalWriteIndex] = SavedRAM;
							Ctx.OutputHeight[LocalWriteIndex] = SavedHeight;
							AddLayerCompositePass(Ctx, LayerCtx, Layer, 2u);
						}

						// Grade is applied to the incoming layer color inside the composite shader.

						// Last of all: the blur softens the finished surface, so a grade or a relief pass
						// running after it would be sharpening detail the blur was asked to remove.
						{
							RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatLayerBlur, "Mixtormat.LayerBlur");
							AddLayerBlurPasses(Ctx, LayerCtx, Layer);
						}

						// The raw ids behind the Region IDs preview, for the Exact ID picker.
						//
						// Here rather than inside each producer: Cluster, Pattern and Combine write
						// their debug colour inline in their own kernels and Breakup goes through the
						// blit pass, so there is no one place a producer colours itself. There is one
						// place they all publish -- LayerCtx.RegionIdMaps -- and by the end of the
						// layer every one of them has. Keyed on the same child index the preview
						// already resolved, so the map read back is exactly the map on screen.
						if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::ChildOutput
							&& Request.DebugSettings.ChildTarget.Kind
								== EMixtormatPreviewOutputKind::RegionIds
							&& Request.DebugSettings.LayerIndex == LayerIndex)
						{
							for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
							{
								if (Entry.Key == Request.DebugSettings.ChildIndex)
								{
									AddRegionIdPickPass(
										GraphBuilder,
										Entry.Value,
										OutputRegionIdPick[Request.PublishedTargetIndex],
										Request.Resolution);
									break;
								}
							}
						}

						// The half this layer wrote. Same parity the composite used; taken here because
						// a later layer's ReferenceHeight must see this layer's finished height, after
						// every filter above has had its turn at it.
						const int32 WriteIndex = LayerIndex & 1;
						if (RequiredHeightSnapshots.Contains(LayerIndex))
						{
							FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
								HeightTargets[WriteIndex]->Desc,
								TEXT("Mixtormat.HeightSnapshot"));
							AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], Snapshot);
							HeightSnapshots.Add(LayerIndex, Snapshot);
						}

						PublishLayerRegionIdOutputs(Ctx, LayerCtx, Layer);
						if (bPrefixCache
							&& LayerIndex == Request.SnapshotLayer
							&& LayerIndex < Request.CacheLayerLimit
							&& !Request.PrefixCache->Contains(
								Request.PrefixHashes[LayerIndex], LayerIndex, Request.Resolution,
								Ctx.PublishedFieldDemand, LayerIndexById))
						{
							PendingSnapshot = SavePrefixSnapshot(
								Ctx, LayerCtx, LayerIndex, WriteIndex, LayerIndexById);
							UE_LOG(LogMixtormatComposition, Verbose,
								TEXT("Prefix cache: %s layer %d."),
								PendingSnapshot.IsValid() ? TEXT("saving") : TEXT("refused"), LayerIndex);
						}
					}
				}


				// Auto remap: the finished height onto 0..1 from its measured range, ahead of AO
				// and normals so both read the remapped surface.
				if (Request.bFinalAutoRemapHeight && !Request.Layers.IsEmpty())
				{
					const int32 Final = Request.PublishedTargetIndex;
					FRDGTextureRef Remapped = AddNormalizeFieldPasses(
						GraphBuilder, Ctx.OutputHeight[Final], Request.Resolution, 0.0f, 1.0f,
						TEXT("Mixtormat.FinalHeight.Remapped"));
					AddCopyTexturePass(GraphBuilder, Remapped, Ctx.OutputHeight[Final]);
				}

				// Final AO: from the finished height, into the finished AO channel. Last, so every
				// layer, effect and generator has shaped the height it reads.
				if (Request.FinalAOAmount != 0.0f && !Request.Layers.IsEmpty())
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatFinalAO, "Mixtormat.FinalAO");
					const int32 Final = Request.PublishedTargetIndex;
					FRDGTextureRef FinalRAM = Ctx.OutputRAM[Final];
					FRDGTextureRef Occluded = GraphBuilder.CreateTexture(
						FRDGTextureDesc::Create2D(Request.Resolution, FinalRAM->Desc.Format,
							FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
						TEXT("Mixtormat.FinalAO.RAM"));
					auto* P = GraphBuilder.AllocParameters<FMixtormatFinalAOCS::FParameters>();
					P->OutputSize = Request.Resolution;
					P->Amount = Request.FinalAOAmount;
					P->RadiusPixels = Request.FinalAORadius
						* static_cast<float>(FMath::Max(Request.Resolution.X, Request.Resolution.Y)) / 1024.0f;
					P->FinalHeight = Ctx.OutputHeight[Final];
					P->SourceRAM = FinalRAM;
					P->OutputRAM = GraphBuilder.CreateUAV(Occluded);
					TShaderMapRef<FMixtormatFinalAOCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FinalAO"), Shader, P,
						FIntVector(FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8), 1));
					AddCopyTexturePass(GraphBuilder, Occluded, FinalRAM);
				}

				// Final normal: slope from the finished height, the layers' authored normal
				// detail reoriented on top. Effects wrote height only (see bFinalNormalFromHeight).
				if (Request.bFinalNormalFromHeight && !Request.Layers.IsEmpty())
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, MixtormatFinalNormal, "Mixtormat.FinalNormal");
					const int32 Final = Request.PublishedTargetIndex;
					FRDGTextureRef FinalN = Ctx.OutputN[Final];
					FRDGTextureRef Rebuilt = GraphBuilder.CreateTexture(
						FRDGTextureDesc::Create2D(Request.Resolution, FinalN->Desc.Format,
							FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
						TEXT("Mixtormat.FinalNormal.N"));
					auto* P = GraphBuilder.AllocParameters<FMixtormatFinalNormalCS::FParameters>();
					P->OutputSize = Request.Resolution;
					P->Strength = FMath::Max(Request.FinalNormalStrength, 0.0f);
					P->FinalHeight = Ctx.OutputHeight[Final];
					P->DetailNormal = FinalN;
					P->OutputNormal = GraphBuilder.CreateUAV(Rebuilt);
					TShaderMapRef<FMixtormatFinalNormalCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FinalNormal"), Shader, P,
						FIntVector(FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8), 1));
					AddCopyTexturePass(GraphBuilder, Rebuilt, FinalN);
				}
				}

				int32 FinalTargetIndex = Request.PublishedTargetIndex;
				if (Request.bRotateOutput90)
				{
					FinalTargetIndex = 1 - Request.PublishedTargetIndex;
					AddRotateOutputPass(Ctx, Request.PublishedTargetIndex, FinalTargetIndex);
				}

				GraphBuilder.SetTextureAccessFinal(Ctx.OutputBC[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.SetTextureAccessFinal(Ctx.OutputN[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.SetTextureAccessFinal(Ctx.OutputRAM[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.SetTextureAccessFinal(Ctx.OutputHeight[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.SetTextureAccessFinal(Ctx.OutputDebug[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.SetTextureAccessFinal(
					Ctx.OutputRegionIdPick[FinalTargetIndex], ERHIAccess::SRVMask);
				GraphBuilder.Execute();
				Request.Targets->bSucceeded = true;
				if (PendingSnapshot.IsValid() && Request.PrefixCache.IsValid())
				{
					Request.PrefixCache->Store(PendingSnapshot, Request.CacheBudgetBytes);
				}
				if (Request.NodeCache.IsValid())
				{
					for (const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>& Entry : Ctx.PendingNodeEntries)
					{
						Request.NodeCache->Store(Entry);
					}
				}
				if (Request.OnComplete.IsBound())
				{
					AsyncTask(
						ENamedThreads::GameThread,
						[OnComplete = Request.OnComplete]() mutable
						{
							OnComplete.ExecuteIfBound();
						});
				}
			});
	}
}
