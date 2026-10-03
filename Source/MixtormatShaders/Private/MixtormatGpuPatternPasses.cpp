// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuPatternPassesInternal.h"
#include "Compositing/MixtormatComposeHash.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"


// Pattern/Region-ID producers, publication, shared ID analysis and random-value masks.

// One entry point and one complete parameter layout for every stage. In particular, do not
// split this into partial per-entry structs: UE validates all file-scope shader uniforms.
class FMixtormatClusterIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatClusterIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatClusterIdsCS, FGlobalShader);

	static constexpr uint32 UnionStart = 4;
	static constexpr uint32 UnionPasses = 12;
	static constexpr uint32 ResolveStage = UnionStart + UnionPasses;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, SourceMode)
		SHADER_PARAMETER(uint32, HasSeparateSourceHeight)
		SHADER_PARAMETER(FVector2f, SourceTiling)
		SHADER_PARAMETER(FVector2f, SourceOffset)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, Threshold)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAMH)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Statistics)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, GuideSignal)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Parents)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ClusterIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatClusterIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatClusterIds.usf",
	"MainCS",
	SF_Compute);

class FMixtormatIdGroupResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatIdGroupResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatIdGroupResolveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceAIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceBIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatIdGroupResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatIdGroup.usf", "ResolveCS", SF_Compute);

class FMixtormatIdGroupBoundaryCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatIdGroupBoundaryCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatIdGroupBoundaryCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, OutlineWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GroupIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputBoundary)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatIdGroupBoundaryCS,
	"/Plugin/Mixtormat/Private/MixtormatIdGroupBoundary.usf", "MainCS", SF_Compute);

// Random value per region. A mask, so it ends in the same PreviousMask/BlendMode/Weight tail
// every other mask child uses -- the only thing that makes it different is where the value
// comes from.
class FMixtormatRandomIdCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRandomIdCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRandomIdCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, MinValue)
		SHADER_PARAMETER(float, MaxValue)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRandomIdCS,
	"/Plugin/Mixtormat/Private/MixtormatRandomId.usf",
	"MainCS",
	SF_Compute);

// Procedural lattice/Voronoi region producer. One dispatch writes IDs, feature-point UV,
// the shared ramp field, and an edge-distance field for bevel/shading.
class FMixtormatPatternIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPatternIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPatternIdsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(uint32, WriteOrientation)
		SHADER_PARAMETER(uint32, PatternMode)
		SHADER_PARAMETER(uint32, GridMode)
		SHADER_PARAMETER(int32, Rows)
		SHADER_PARAMETER(int32, Columns)
		SHADER_PARAMETER(float, RowOffset)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(uint32, SwapAxes)
		SHADER_PARAMETER(float, GapPixels)
		SHADER_PARAMETER(float, GapRandom)
		SHADER_PARAMETER(float, GapSlide)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, FeatherRandom)
		SHADER_PARAMETER(float, Rounding)
		SHADER_PARAMETER(uint32, EdgeRelative)
		SHADER_PARAMETER(float, FractureSizeVariation)
		SHADER_PARAMETER(float, FractureSecondaryAmount)
		SHADER_PARAMETER(int32, FractureSecondaryMin)
		SHADER_PARAMETER(int32, FractureSecondaryMax)
		SHADER_PARAMETER(float, FractureSecondaryRadius)
		SHADER_PARAMETER(float, FractureSecondaryJitter)
		SHADER_PARAMETER(float, FractureEdgeIrregularity)
		SHADER_PARAMETER(float, FractureEdgeScale)
		SHADER_PARAMETER(float, FractureEdgeDetail)
		SHADER_PARAMETER(float, FractureEdgeDetailScale)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputUV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputOrientation)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPatternIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatPatternIds.usf",
	"MainCS",
	SF_Compute);



// Bounded analysis roots for compact labels and full-width identities.
class FMixtormatRegionIndexCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionIndexCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionIndexCS, FGlobalShader);

	static constexpr uint32 StageInit = 0;
	static constexpr uint32 StageRegister = 1;
	static constexpr uint32 StageResolve = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, IndexStage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RegionIndexKeys)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RegionIndexRoots)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRootIds)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionIndexCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"IndexCS",
	SF_Compute);


namespace MixtormatGpuCompositor
{
	// Produces one layer's ID map, and its debug preview alongside it.
	//
	// Returns the map rather than only writing the preview, because the two consumers -- the HSV
	// filter at the composite's albedo sample and the random-value mask in the chain -- both read
	// it, and both have to read the *same* one. Segmenting twice would cost 17 dispatches twice
	// and, worse, could disagree: a tint landing on different regions than the stain it is
	// supposed to share boundaries with is exactly the failure a shared map exists to prevent.
	static FRDGTextureRef AddClusterIdPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceRAMH,
		FRDGTextureRef SourceHeight,
		FRDGTextureRef SurfaceRAM,
		FRDGTextureRef SurfaceHeight,
		bool bHasSurfaceBelow,
		bool bWriteDebug,
		FRDGTextureRef OutputDebug,
		FIntPoint OutputSize,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		int32 LayerIndex)
	{
		// The bottom layer has nothing composited below it -- the substrate is flat, and
		// segmenting a flat surface returns one region covering everything. Fall back to the
		// layer's own map rather than hand back a map with no regions in it.
		const bool bFromComposite =
			Child.Filter.Source == EMixtormatClusterSource::CompositeBelow && bHasSurfaceBelow;
		// Deliberately graph-local. An RHI identity (even held strongly) cannot detect in-place
		// texture edits, reimports or streaming updates. Until the source exposes a content
		// revision, recompute selected previews each request rather than cache stale regions.
		// Any future persistent key must include that revision, retained source identity,
		// resolution, UV placement and these three filter controls -- not material grading.
		const uint32 PixelCount = static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);
		FRDGBufferRef Statistics = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4), TEXT("Mixtormat.Cluster.Statistics"));
		FRDGBufferRef GuideSignal = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), PixelCount), TEXT("Mixtormat.Cluster.GuideSignal"));
		FRDGBufferRef Parents = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PixelCount), TEXT("Mixtormat.Cluster.Parents"));
		// Sparse integer root indices, not colors or a compacted label map. No consumers yet.
		FRDGBufferRef ClusterIds = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PixelCount), TEXT("Mixtormat.Cluster.Ids"));
		const FRDGBufferUAVRef StatisticsUAV = GraphBuilder.CreateUAV(Statistics);
		const FRDGBufferUAVRef GuideSignalUAV = GraphBuilder.CreateUAV(GuideSignal);
		const FRDGBufferUAVRef ParentsUAV = GraphBuilder.CreateUAV(Parents);
		const FRDGBufferUAVRef ClusterIdsUAV = GraphBuilder.CreateUAV(ClusterIds);
		const FRDGTextureUAVRef DebugUAV = GraphBuilder.CreateUAV(OutputDebug);
		// R32_UINT and read with Load, never a sampler. Filtering an ID is meaningless -- halfway
		// between two regions is a third number naming neither -- and every consumer runs at this
		// resolution, so a pixel lookup is exact.
		FRDGTextureRef RegionIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Cluster.RegionIds"));
		const FRDGTextureUAVRef RegionIdsUAV = GraphBuilder.CreateUAV(RegionIds);
		TShaderMapRef<FMixtormatClusterIdsCS> ClusterShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8), FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		for (uint32 Stage = 0; Stage <= FMixtormatClusterIdsCS::ResolveStage; ++Stage)
		{
			FMixtormatClusterIdsCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatClusterIdsCS::FParameters>();
			Parameters->OutputSize = OutputSize;
			Parameters->Stage = Stage;
			Parameters->WriteDebug = bWriteDebug ? 1u : 0u;
			Parameters->SourceMode = bFromComposite ? 1u : 0u;
			Parameters->HasSeparateSourceHeight = Layer.SourceOutputs.IsValid() ? 1u : 0u;
			Parameters->SourceTiling = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
			Parameters->SourceOffset = Layer.UVOffset;
			Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
			Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
			Parameters->Rotation = Layer.Rotation;
			Parameters->Threshold = Child.Filter.Threshold;
			Parameters->Offset = Child.Filter.Offset;
			Parameters->HeightInfluence = Child.Filter.HeightInfluence;
			Parameters->SourceRAMH = SourceRAMH;
			Parameters->SourceHeight = SourceHeight;
			Parameters->SurfaceRAM = SurfaceRAM;
			Parameters->SurfaceHeight = SurfaceHeight;
			Parameters->LinearWrapSampler =
				TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			Parameters->Statistics = StatisticsUAV;
			Parameters->GuideSignal = GuideSignalUAV;
			Parameters->Parents = ParentsUAV;
			Parameters->ClusterIds = ClusterIdsUAV;
			Parameters->OutputIds = RegionIdsUAV;
			Parameters->OutputDebug = DebugUAV;
			// Keep the default UAV barriers: each dispatch must finish before the next stage
			// reads global statistics/parents. A group barrier cannot synchronize this algorithm.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.ClusterIds.Layer%d.Child%d.Stage%u", LayerIndex, Child.SourceChildIndex, Stage),
				ClusterShader, Parameters, Stage == 0 ? FIntVector(1, 1, 1) : Groups);
		}
		return RegionIds;
	}

	static FRDGTextureRef AddPatternIdPasses(
		FRDGBuilder& GraphBuilder,
		bool bWriteDebug,
		FRDGTextureRef OutputDebug,
		FIntPoint OutputSize,
		const FChildRenderData& Child,
		int32 LayerIndex,
		FRDGTextureRef EmptyPatternOrientation,
		FRDGTextureRef& OutUV,
		FRDGTextureRef& OutRamp,
		FRDGTextureRef& OutEdge,
		FRDGTextureRef& OutGap,
		FRDGTextureRef& OutOrientation)
	{
		const FRDGTextureDesc IdDesc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc Float2Desc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_G16R16F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef RegionIds =
			GraphBuilder.CreateTexture(IdDesc, TEXT("Mixtormat.Pattern.RegionIds"));
		OutUV = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.FeatureUV"));
		OutRamp = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.Ramp"));
		OutEdge = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Pattern.Edge"));
		OutGap = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Pattern.Gap"));
		const bool bWritesOrientation = HasIntrinsicPatternOrientation(Child.PatternId);
		OutOrientation = bWritesOrientation
			? GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					OutputSize,
					PF_R32_FLOAT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Pattern.Orientation"))
			: EmptyPatternOrientation;

		FMixtormatPatternIdsCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatPatternIdsCS::FParameters>();
		Parameters->OutputSize = OutputSize;
		Parameters->WriteDebug = bWriteDebug ? 1u : 0u;
		Parameters->WriteOrientation = bWritesOrientation ? 1u : 0u;
		Parameters->PatternMode = static_cast<uint32>(Child.PatternId.PatternMode);
		Parameters->GridMode = static_cast<uint32>(Child.PatternId.GridMode);
		Parameters->Rows = Child.PatternId.Rows;
		Parameters->Columns = Child.PatternId.Columns;
		Parameters->RowOffset = Child.PatternId.RowOffset;
		Parameters->Jitter = Child.PatternId.Jitter;
		Parameters->SwapAxes = Child.PatternId.bSwapAxes ? 1u : 0u;
		Parameters->GapPixels = Child.PatternId.GapPixels;
		Parameters->GapRandom = Child.PatternId.GapRandom;
		Parameters->GapSlide = Child.PatternId.GapSlide;
		Parameters->Seed = Child.PatternId.Seed;
		Parameters->FeatherRandom = Child.PatternId.FeatherRandom;
		Parameters->Rounding = Child.PatternId.Rounding;
		Parameters->EdgeRelative = Child.PatternId.bRelativeEdgeWidth ? 1u : 0u;
		Parameters->Feather = Child.PatternId.Feather;
		Parameters->FractureSizeVariation = Child.PatternId.FractureSizeVariation;
		Parameters->FractureSecondaryAmount = Child.PatternId.FractureSecondaryAmount;
		Parameters->FractureSecondaryMin = Child.PatternId.FractureSecondaryMin;
		Parameters->FractureSecondaryMax = Child.PatternId.FractureSecondaryMax;
		Parameters->FractureSecondaryRadius = Child.PatternId.FractureSecondaryRadius;
		Parameters->FractureSecondaryJitter = Child.PatternId.FractureSecondaryJitter;
		Parameters->FractureEdgeIrregularity = Child.PatternId.FractureEdgeIrregularity;
		Parameters->FractureEdgeScale = Child.PatternId.FractureEdgeScale;
		Parameters->FractureEdgeDetail = Child.PatternId.FractureEdgeDetail;
		Parameters->FractureEdgeDetailScale = Child.PatternId.FractureEdgeDetailScale;
		Parameters->OutputIds = GraphBuilder.CreateUAV(RegionIds);
		Parameters->OutputUV = GraphBuilder.CreateUAV(OutUV);
		Parameters->OutputRamp = GraphBuilder.CreateUAV(OutRamp);
		Parameters->OutputEdge = GraphBuilder.CreateUAV(OutEdge);
		Parameters->OutputGap = GraphBuilder.CreateUAV(OutGap);
		Parameters->OutputOrientation = GraphBuilder.CreateUAV(OutOrientation);
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatPatternIdsCS> PatternShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.PatternIds.Layer%d.Child%d", LayerIndex, Child.SourceChildIndex),
			PatternShader,
			Parameters,
			Groups);
		return RegionIds;
	}


	// Only these local producers guarantee that an identity is also a pixel-root address.
	// Output references are indexed conservatively: a non-hashed map may use compact labels.
	bool HasPixelRootIds(const FLayerRenderData& Layer, int32 ProducerIndex)
	{
		const FChildRenderData* Producer = Layer.Children.FindByPredicate(
			[ProducerIndex](const FChildRenderData& Candidate)
			{
				return Candidate.SourceChildIndex == ProducerIndex;
			});

		return Producer && (Producer->Type == EMixtormatLayerChildType::PatternId
			|| (Producer->Type == EMixtormatLayerChildType::Filter
				&& (!Producer->Filter.bSurfaceIds || Producer->Filter.bSplitIslands)));
	}

	FRDGTextureRef AddRegionIndexPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef RegionIds,
		const bool bPixelRootIds,
		const FIntPoint OutputSize,
		TMap<FRDGTextureRef, FRDGTextureRef>& RootCache)
	{
		if (bPixelRootIds)
		{
			return RegionIds;
		}
		if (const FRDGTextureRef* Cached = RootCache.Find(RegionIds))
		{
			return *Cached;
		}
		const uint32 PixelCount = static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);
		const uint32 Capacity = PixelCount * 2u;
		FRDGBufferRef Keys = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Capacity),
			TEXT("Mixtormat.Region.IndexKeys"));
		FRDGBufferRef Roots = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Capacity),
			TEXT("Mixtormat.Region.IndexRoots"));
		FRDGTextureRef RootIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(OutputSize, PF_R32_UINT, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Region.RootIds"));
		const FRDGBufferUAVRef KeysUAV = GraphBuilder.CreateUAV(Keys);
		const FRDGBufferUAVRef RootsUAV = GraphBuilder.CreateUAV(Roots);
		const FRDGTextureUAVRef RootIdsUAV = GraphBuilder.CreateUAV(RootIds);
		TShaderMapRef<FMixtormatRegionIndexCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8), FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		for (uint32 Stage = FMixtormatRegionIndexCS::StageInit;
			Stage <= FMixtormatRegionIndexCS::StageResolve; ++Stage)
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatRegionIndexCS::FParameters>();
			P->OutputSize = OutputSize;
			P->IndexStage = Stage;
			P->RegionIds = RegionIds;
			P->RegionIndexKeys = KeysUAV;
			P->RegionIndexRoots = RootsUAV;
			P->OutputRootIds = RootIdsUAV;
			// Default UAV barriers separate initialization, registration and root publication.
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.RegionFields.Index.Stage%u", Stage), Shader, P, Groups);
		}
		RootCache.Add(RegionIds, RootIds);
		return RootIds;
	}


	// The cluster filter a consumer reads: the nearest enabled one above it in the child list.
	//
	// Above rather than anywhere in the layer, so two cluster filters at different thresholds can
	// coexist -- a coarse one with its own consumers, then a fine one with its own -- which is
	// how the micro/macro pairing in the design note is meant to be authored. Nothing above means
	// no map, and the consumer is culled rather than guessing.

	// Keep private producer maps for previews and nested groups, but expose only the
	// owning group's final output to its consumers.
	TArray<TPair<int32, FRDGTextureRef>> GetRegionIdView(
		const FLayerRenderData& Layer,
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const FChildRenderData* Child)
	{
		TArray<TPair<int32, FRDGTextureRef>> View;
		const int32 ScopeOwner = Child ? Child->ScopeOwnerSourceChildIndex : INDEX_NONE;
		const bool bGroupConsumer = Child && ScopeOwner != INDEX_NONE
			&& Layer.Children.ContainsByPredicate([ScopeOwner](const FChildRenderData& Candidate)
			{
				return Candidate.SourceChildIndex == ScopeOwner
					&& Candidate.Type == EMixtormatLayerChildType::IdGroup;
			});
		for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
		{
			if (bGroupConsumer)
			{
				if (Entry.Key == ScopeOwner)
				{
					View.Add(Entry);
				}
				continue;
			}
			const FChildRenderData* Producer = Layer.Children.FindByPredicate(
				[&Entry](const FChildRenderData& Candidate)
				{
					return Candidate.SourceChildIndex == Entry.Key;
				});
			if (Producer && Producer->ScopeOwnerSourceChildIndex == ScopeOwner)
			{
				View.Add(Entry);
			}
		}
		return View;
	}

	// Resolve the authored producer before looking for its published map. A deferred or
	// unavailable producer shadows older maps; publication order must not change the source.
	//
	// UV From IDs also needs the producer index to identify Pattern's intrinsic orientation.
	// Distance records instead share by source texture identity, resolution and seed policy.
	FRDGTextureRef FindRegionIdsAboveWithIndex(
		const FLayerRenderData& Layer,
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const FChildRenderData& Child,
		int32& OutProducerIndex)
	{
		OutProducerIndex = INDEX_NONE;
		const int32 ScopeOwner = Child.ScopeOwnerSourceChildIndex;
		const bool bGroupConsumer = ScopeOwner != INDEX_NONE
			&& Layer.Children.ContainsByPredicate([ScopeOwner](const FChildRenderData& Candidate)
			{
				return Candidate.SourceChildIndex == ScopeOwner
					&& Candidate.Type == EMixtormatLayerChildType::IdGroup;
			});
		if (bGroupConsumer)
		{
			// Nested consumers read only their immediate owning group's final output.
			if (ScopeOwner < Child.SourceChildIndex)
			{
				OutProducerIndex = ScopeOwner;
			}
		}
		else
		{
			for (const FChildRenderData& Candidate : Layer.Children)
			{
				if (Candidate.SourceChildIndex >= Child.SourceChildIndex
					|| Candidate.ScopeOwnerSourceChildIndex != ScopeOwner)
				{
					continue;
				}
				const bool bProducesIds = Candidate.Type == EMixtormatLayerChildType::Filter
					|| Candidate.Type == EMixtormatLayerChildType::PatternId

					|| Candidate.Type == EMixtormatLayerChildType::IdGroup
					|| Candidate.Type == EMixtormatLayerChildType::Generator
					|| (Candidate.Type == EMixtormatLayerChildType::OutputReference
						&& Candidate.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds)
					|| (Candidate.Type == EMixtormatLayerChildType::Effect
						&& Candidate.Effect.Type == EMixtormatEffectType::Breakup);
				if (bProducesIds && Candidate.SourceChildIndex > OutProducerIndex)
				{
					OutProducerIndex = Candidate.SourceChildIndex;
				}
			}
		}
		if (OutProducerIndex != INDEX_NONE)
		{
			for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
			{
				if (Entry.Key == OutProducerIndex)
				{
					return Entry.Value;
				}
			}
		}
		return nullptr;
	}


	// Random value per region, blended into the mask chain like any other mask child.
	void AddRandomIdMaskPass(
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
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatRandomIdCS> RandomIdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FRandomIdRenderData& RandomId = Child.RandomId;

		// Culled rather than defaulted when there is no cluster above it. A
		// mask with no ID map has no regions to vary, and emitting a flat
		// value would silently replace whatever the chain had accumulated.
		int32 ProducerIndex = INDEX_NONE;
		FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
			Layer, RegionIdMaps, Child, ProducerIndex);
		if (!RegionIds)
		{
			return;
		}

		// The same identity as every other mask child: at Weight 0 the tail
		// returns Previous unchanged, so skipping from the second child on
		// leaves exactly that behind.
		if (MaskPassIndex > 0 && RandomId.Weight == 0.0f)
		{
			return;
		}

		const int32 MaskWriteIndex = MaskPassIndex & 1;
		const int32 MaskReadIndex = 1 - MaskWriteIndex;

		FMixtormatRandomIdCS::FParameters* RandomParameters =
			GraphBuilder.AllocParameters<FMixtormatRandomIdCS::FParameters>();
		RandomParameters->OutputSize = Request.Resolution;
		RandomParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
		RandomParameters->Seed = RandomId.Seed;
		RandomParameters->MinValue = RandomId.MinValue;
		RandomParameters->MaxValue = RandomId.MaxValue;
		RandomParameters->BlendMode = static_cast<uint32>(RandomId.BlendMode);
		RandomParameters->Invert = RandomId.bInvert ? 1u : 0u;
		RandomParameters->Weight = RandomId.Weight;
		RandomParameters->Balance = RandomId.Balance;
		RandomParameters->Contrast = RandomId.Contrast;
		RandomParameters->Offset = RandomId.Offset;
		RandomParameters->PreviousMask = MaskTargets[MaskReadIndex];
		RandomParameters->RegionIds = RegionIds;
		RandomParameters->LinearWrapSampler =
			TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		RandomParameters->OutputMask =
			GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RandomId.Layer%d.Child%d", LayerIndex, ChildIndex),
			RandomIdShader,
			RandomParameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));

		CombinedMask = MaskTargets[MaskWriteIndex];
		if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
		{
			FRDGTextureRef DebugRandomSnapshot = GraphBuilder.CreateTexture(
				MaskDesc,
				TEXT("Mixtormat.DebugRandomIdSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, DebugRandomSnapshot);
			DebugMask = DebugRandomSnapshot;
		}
		++MaskPassIndex;
	}



	void PublishLayerRegionIdOutputs(FMixtormatComposeContext& Ctx,
		const FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
	{
		for (const auto& Map : LayerCtx.RegionIdMaps)
		{
			if (!Map.Value) { continue; }
			const FChildRenderData* Child = Layer.Children.FindByPredicate([&Map](const FChildRenderData& Candidate)
			{
				return Candidate.SourceChildIndex == Map.Key;
			});
			if (!Child) { continue; }
			bool bHashed = Child->Type == EMixtormatLayerChildType::Generator
				|| Child->Type == EMixtormatLayerChildType::IdGroup
				|| (Child->Type == EMixtormatLayerChildType::Effect && Child->Effect.Type == EMixtormatEffectType::Breakup);

			if (Child->Type == EMixtormatLayerChildType::OutputReference)
			{
				// Preserve the resolved alias metadata, including invalid and local references.
				// Its local source may sort after a group output during layer-end publication.
				const FPublishedField* Alias = Ctx.PublishedFieldOutputs.Find(
					FPublishedFieldKey{Layer.LayerId, Map.Key, FName(TEXT("RegionIds"))});
				bHashed = !Alias || Alias->bHashedIds;
			}
			Ctx.PublishedFieldOutputs.Add(
				FPublishedFieldKey{Layer.LayerId, Map.Key, FName(TEXT("RegionIds"))},
				FPublishedField{EMixtormatPublishedFieldKind::RegionIds, Map.Value, nullptr, nullptr, bHashed});
		}
	}


	bool AddRegionIdReferencePass(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child, const bool bFinalizeUnavailable)
	{
		if (!Layer.bEnabled || Child.Type != EMixtormatLayerChildType::OutputReference
			|| Child.OutputReference.Kind != EMixtormatPublishedFieldKind::RegionIds) { return false; }
		if (LayerCtx.RegionIdMaps.ContainsByPredicate([&](const TPair<int32, FRDGTextureRef>& Map)
		{
			return Map.Key == Child.SourceChildIndex && Map.Value
				&& Map.Value->Desc.Format == PF_R32_UINT && Map.Value->Desc.Extent == Ctx.Request.Resolution;
		})) { return true; }
		const FPublishedFieldKey& Address = Child.OutputReference.Source;
		FPublishedField Field;
		Field.Kind = EMixtormatPublishedFieldKind::RegionIds;
		const bool bLocal = Address.LayerId == Layer.LayerId;
		const bool bValidAddress = Address.ChildIndex >= 0
			&& Address.Output == FName(TEXT("RegionIds"))
			&& (!bLocal || Address.ChildIndex < Child.SourceChildIndex);
		if (bValidAddress && bLocal && Address.ChildIndex < Child.SourceChildIndex)
		{
			const TPair<int32, FRDGTextureRef>* Source = LayerCtx.RegionIdMaps.FindByPredicate(
				[&](const TPair<int32, FRDGTextureRef>& Map) { return Map.Key == Address.ChildIndex; });
			if (Source) { Field.Texture = Source->Value; }
			// Local aliases may carry full-width IDs. Never treat them as direct pixel addresses.
			Field.bHashedIds = true;
		}
		else if (bValidAddress && !bLocal)
		{
			const int32 SourceLayer = Ctx.Request.Layers.IndexOfByPredicate(
				[&](const FLayerRenderData& Candidate) { return Candidate.LayerId == Address.LayerId; });
			if (SourceLayer >= 0 && SourceLayer < LayerCtx.LayerIndex)
			{
				const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Address);
				if (Source && Source->Kind == EMixtormatPublishedFieldKind::RegionIds) { Field = *Source; }
			}
		}
		if (!Field.IsComplete() || Field.Texture->Desc.Extent != Ctx.Request.Resolution)
		{
			// A valid local source may still be waiting for its generator or nested group.
			if (bValidAddress && bLocal && !bFinalizeUnavailable) { return false; }
			Field.Texture = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Ctx.Request.Resolution, PF_R32_UINT, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.OutputReference.InvalidIds"));
			AddClearUAVPass(Ctx.GraphBuilder, Ctx.GraphBuilder.CreateUAV(Field.Texture), 0xffffffffu);
			Field.bHashedIds = true;
		}
		PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Field.Texture);
		Ctx.PublishedFieldOutputs.Add(
			FPublishedFieldKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("RegionIds"))}, Field);
		if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::RegionIds,
			NAME_None, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewRegionIdsBlitPass(Ctx.GraphBuilder, Field.Texture, nullptr,
				Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Ctx.Request.Resolution);
		}
		return true;
	}

	FRDGTextureRef AddIdGroupPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FIntVector Groups(
			FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const FRDGTextureDesc IdDesc = FRDGTextureDesc::Create2D(
			Size, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef GroupIds = nullptr;
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (Candidate.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
				|| (Candidate.Type != EMixtormatLayerChildType::PatternId
					&& Candidate.Type != EMixtormatLayerChildType::Filter

					&& Candidate.Type != EMixtormatLayerChildType::IdGroup
					&& !(Candidate.Type == EMixtormatLayerChildType::OutputReference
						&& Candidate.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds)))
			{
				continue;
			}
			const TPair<int32, FRDGTextureRef>* Entry = LayerCtx.RegionIdMaps.FindByPredicate(
				[&Candidate](const TPair<int32, FRDGTextureRef>& Map)
				{
					return Map.Key == Candidate.SourceChildIndex;
				});
			if (!Entry || !Entry->Value || Entry->Value->Desc.Format != PF_R32_UINT
				|| Entry->Value->Desc.Extent != Size)
			{
				// Unavailable producer maps contribute no IDs.
				continue;
			}
			if (!GroupIds)
			{
				GroupIds = Entry->Value; // One producer is an exact passthrough.
				continue;
			}

			const FRDGTextureRef Folded = GraphBuilder.CreateTexture(IdDesc, TEXT("Mixtormat.IdGroup.Ids"));
			auto* P = GraphBuilder.AllocParameters<FMixtormatIdGroupResolveCS::FParameters>();
			P->OutputSize = Size;
			P->Mode = static_cast<uint32>(Child.IdGroup.Mode);
			P->WriteDebug = 0u;
			P->SourceAIds = GroupIds;
			P->SourceBIds = Entry->Value;
			P->OutputIds = GraphBuilder.CreateUAV(Folded);
			P->OutputDebug = GraphBuilder.CreateUAV(Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex]);
			TShaderMapRef<FMixtormatIdGroupResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.IdGroup.Resolve"), Shader, P, Groups);
			GroupIds = Folded;
		}
		if (!GroupIds)
		{
			// Full-size invalid IDs, not the 1x1 binding dummy: consumers use integer Load.
			GroupIds = GraphBuilder.CreateTexture(IdDesc, TEXT("Mixtormat.IdGroup.Empty"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GroupIds), 0xffffffffu);
		}
		if (IsChildOutputPreviewTarget(
			Ctx.Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
			LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewRegionIdsBlitPass(GraphBuilder, GroupIds, nullptr,
				Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
		}
		FRDGTextureRef Boundary = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, PF_R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.IdGroup.Boundary"));
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatIdGroupBoundaryCS::FParameters>();
			P->OutputSize = Size;
			P->OutlineWidth = FMath::Clamp(Child.IdGroup.BoundaryWidth, 1, 16);
			P->GroupIds = GroupIds;
			P->OutputBoundary = GraphBuilder.CreateUAV(Boundary);
			TShaderMapRef<FMixtormatIdGroupBoundaryCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.IdGroup.Boundary"), Shader, P, Groups);
		}
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Boundary"))}, Boundary);
		if (IsChildOutputPreviewTarget(
			Ctx.Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Boundary")),
			LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, Boundary,
				Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
		}
		return GroupIds;
	}

	void AddReadyRegionIdPasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const int32 BeforeChildIndex, const bool bFinalizeUnavailable)
	{
		if (!Layer.bEnabled) { return; }
		const auto IsPublished = [&](const int32 Index)
		{
			return LayerCtx.RegionIdMaps.ContainsByPredicate(
				[&](const TPair<int32, FRDGTextureRef>& Map)
				{
					return Map.Key == Index && Map.Value && Map.Value->Desc.Format == PF_R32_UINT
						&& Map.Value->Desc.Extent == Ctx.Request.Resolution;
				});
		};
		TSet<int32> ActiveScopes;
		TFunction<bool(int32, int32)> ResolveScope;
		ResolveScope = [&](const int32 ScopeOwner, const int32 Depth)
		{
			if (Depth > 128 || ActiveScopes.Contains(ScopeOwner)) { return false; }
			ActiveScopes.Add(ScopeOwner);
			bool bReady = true;
			for (const FChildRenderData& Child : Layer.Children)
			{
				if (Child.ScopeOwnerSourceChildIndex != ScopeOwner
					|| (ScopeOwner == INDEX_NONE && Child.SourceChildIndex >= BeforeChildIndex)) { continue; }
				if (Child.Type == EMixtormatLayerChildType::OutputReference
					&& Child.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds)
				{
					if (!AddRegionIdReferencePass(Ctx, LayerCtx, Layer, Child, bFinalizeUnavailable)) { bReady = false; }
				}
				else if (Child.Type == EMixtormatLayerChildType::IdGroup && !IsPublished(Child.SourceChildIndex))
				{
					if (ResolveScope(Child.SourceChildIndex, Depth + 1))
					{
						PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex,
							AddIdGroupPasses(Ctx, LayerCtx, Layer, Child));
					}
					else { bReady = false; }
				}

				else if ((Child.Type == EMixtormatLayerChildType::PatternId
					|| Child.Type == EMixtormatLayerChildType::Filter)
					&& !IsPublished(Child.SourceChildIndex) && !bFinalizeUnavailable)
				{
					bReady = false;
				}
			}
			ActiveScopes.Remove(ScopeOwner);
			return bReady;
		};
		ResolveScope(INDEX_NONE, 0);
	}

	// Schedule independent producers early for generators. ID Groups resolve in scope order;
	// publication keeps maps sorted by SourceChildIndex.
	void AddRegionProducerPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyPatternOrientation = Ctx.EmptyPatternOrientation;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		if (Layer.bEnabled)
		{
			for (const FChildRenderData& Child : Layer.Children)
			{
				const bool bClusterProducer =
					Child.Type == EMixtormatLayerChildType::Filter;
				const bool bPatternProducer =
					Child.Type == EMixtormatLayerChildType::PatternId;

				const bool bIdGroupProducer =
					Child.Type == EMixtormatLayerChildType::IdGroup;
				if (!bClusterProducer && !bPatternProducer && !bIdGroupProducer)
				{
					continue;
				}

				if (bIdGroupProducer)
				{
					// Resolved in scope order after the independent producers below.
					continue;
				}

				// Gated separately from whether the producer runs at all: ordinary
				// consumers must not overwrite a debug target owned by another layer.
				const bool bIsSelectedPreview = IsChildOutputPreviewTarget(
					Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
					LayerIndex, Child.SourceChildIndex);
				bool bWanted = bIsSelectedPreview
					|| Child.ScopeOwnerSourceChildIndex != INDEX_NONE
					|| Ctx.PublishedFieldDemand.Contains(
						FPublishedFieldKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("RegionIds"))});

				if (bPatternProducer)
				{
					const FPatternIdRenderData& Pattern = Child.PatternId;
					bWanted = bWanted
						|| Pattern.bUVVariation
						|| HasIntrinsicPatternOrientation(Pattern)
						|| Pattern.HeightAmount > 0.0f
						|| Pattern.BevelHeight > 0.0f
						|| Pattern.EdgeRoughnessAmount > 0.0f;

					// A Gap instance mask is an explicit consumer even when every Pattern relief
					// control is neutral. Demand its producer so the published texture exists.
					if (!bWanted)
					{
						for (const FChildRenderData& Other : Layer.Children)
						{
							if (Other.Type == EMixtormatLayerChildType::Mask
								&& Other.Mask.PublishedSourceLayerId == Layer.LayerId
								&& Other.Mask.PublishedSourceChildIndex == Child.SourceChildIndex
								&& Other.Mask.PublishedSourceOutput == TEXT("Gap"))
							{
								bWanted = true;
								break;
							}
						}
					}

					// The Gap preview eye is a consumer too: without this, previewing Gap on a
					// Pattern that has no relief control active and no Gap instance mask silently
					// shows nothing, because the producer never ran to publish it.
					if (!bWanted)
					{
						bWanted = IsChildOutputPreviewTarget(
							Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Gap")),
							LayerIndex, Child.SourceChildIndex);
					}
				}

				// A scalar Driver is an ID consumer like any other, and it consumes at
				// the composite rather than from a row in the stack -- so it is not
				// visible to the child scan below and has to be asked for separately.
				// Without this a producer referenced only by a Driver is culled, and
				// the Driver then finds no map and silently disables itself.
				if (!bWanted)
				{
					for (const FScalarDriverRenderData& Driver : Layer.ScalarDrivers)
					{
						if (!Driver.bEnabled
							|| !Driver.bRegionSource
							|| Driver.SourceLayerId != Layer.LayerId)
						{
							continue;
						}
						if (Driver.SourceChildIndex != INDEX_NONE)
						{
							// Named producer: only that one is demanded.
							if (Driver.SourceChildIndex == Child.SourceChildIndex)
							{
								bWanted = true;
								break;
							}
							continue;
						}
						// Unnamed: the Driver reads the nearest map above the
						// composite, which is the last producer in the layer. Demand
						// this one only when nothing later would shadow it, so the
						// nearest-producer rule decides here exactly as it does at
						// resolve time.
						bool bLaterProducer = false;
						for (const FChildRenderData& Other : Layer.Children)
						{
							if (Other.SourceChildIndex > Child.SourceChildIndex
								&& Other.ScopeOwnerSourceChildIndex == Child.ScopeOwnerSourceChildIndex
								&& (Other.Type == EMixtormatLayerChildType::Filter
									|| Other.Type == EMixtormatLayerChildType::PatternId

										|| Other.Type == EMixtormatLayerChildType::IdGroup
									|| Other.Type == EMixtormatLayerChildType::Generator
									|| (Other.Type == EMixtormatLayerChildType::Effect
										&& Other.Effect.Type == EMixtormatEffectType::Breakup)))
							{
								bLaterProducer = true;
								break;
							}
						}
						if (!bLaterProducer)
						{
							bWanted = true;
							break;
						}
					}
				}

				if (!bWanted)
				{
					// Any ID consumer below this producer and above the next producer.
					// The next producer shadows this one for every later consumer.
					for (const FChildRenderData& Other : Layer.Children)
					{
						if (Other.SourceChildIndex <= Child.SourceChildIndex
							|| Other.ScopeOwnerSourceChildIndex != Child.ScopeOwnerSourceChildIndex)
						{
							continue;
						}
						if (Other.Type == EMixtormatLayerChildType::Generator
							&& Other.Generator.Type == EMixtormatGeneratorType::StrataCarver)
						{
							// Strata consumes this map before publishing its own.
							bWanted = true;
							break;
						}
						if (Other.Type == EMixtormatLayerChildType::Filter
							|| Other.Type == EMixtormatLayerChildType::PatternId
							|| Other.Type == EMixtormatLayerChildType::IdGroup
							|| Other.Type == EMixtormatLayerChildType::Generator)
						{
							break;
						}
						const bool bWornEdgesConsumer =
							Other.Type == EMixtormatLayerChildType::Effect
							&& Other.Effect.Type == EMixtormatEffectType::WornEdges;
						const bool bBreakupConsumer =
							Other.Type == EMixtormatLayerChildType::Effect
							&& Other.Effect.Type == EMixtormatEffectType::Breakup;
						if (Other.Type == EMixtormatLayerChildType::HsvFilter
							|| (Other.Type == EMixtormatLayerChildType::ColorId
								&& Other.ColorId.Mode == EMixtormatColorIdMode::ExactId)
							|| Other.Type == EMixtormatLayerChildType::RandomId
							|| Other.Type == EMixtormatLayerChildType::RampId
							|| Other.Type == EMixtormatLayerChildType::UvFromIds
							|| Other.Type == EMixtormatLayerChildType::ReliefFromIds
							|| Other.Type == EMixtormatLayerChildType::BoundaryFromIds
							|| bWornEdgesConsumer
							|| bBreakupConsumer)
						{
							bWanted = true;
							break;
						}
					}
				}
				if (!bWanted)
				{
					continue;
				}

				FRDGTextureRef RegionIds = nullptr;

				// Node cache. Bypassed for the previewed child: its kernel also writes the debug
				// view, which a cached map cannot reproduce.
				FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
				uint64 NodeKey = 0;
				if (NodeCache && Child.CacheKey != 0 && !bIsSelectedPreview)
				{
					if (bClusterProducer)
					{
						// Surface/Cluster IDs read the layer's own maps and, with Composite Below or as
						// the height fallback, the accumulated surface under this layer -- exactly what
						// the prefix hash of the layer below identifies. Folded in whenever there is a
						// layer below, so the key never depends on which of those a mode samples.
						const bool bHasBelow = LayerIndex > 0;
						if (Layer.SourceCacheKey != 0
							&& (!bHasBelow || Request.PrefixHashes.IsValidIndex(LayerIndex - 1)))
						{
							NodeKey = MixtormatComposeHash::Combine(
								MixtormatComposeHash::Combine(Child.CacheKey, Layer.SourceCacheKey),
								bHasBelow ? Request.PrefixHashes[LayerIndex - 1] : 0x436C75ull);
						}
					}
					else
					{
						// Pattern IDs read nothing but their own settings.
						NodeKey = MixtormatComposeHash::Combine(Child.CacheKey, 0x5061747465726Eull);
					}
				}
				const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> NodeHit =
					NodeKey != 0 ? NodeCache->Find(NodeKey, Request.Resolution)
						: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
				const auto RegisterCached = [&GraphBuilder](
					const TRefCountPtr<IPooledRenderTarget>& Target, const TCHAR* Name) -> FRDGTextureRef
				{
					return Target.IsValid() ? GraphBuilder.RegisterExternalTexture(Target, Name) : nullptr;
				};
				const auto KeepNode = [&](const std::initializer_list<FRDGTextureRef> Outputs)
				{
					if (NodeKey == 0)
					{
						return;
					}
					TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Entry =
						MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = NodeKey;
					Entry->Resolution = Request.Resolution;
					int32 Slot = 0;
					for (const FRDGTextureRef Output : Outputs)
					{
						if (Output && Slot < static_cast<int32>(UE_ARRAY_COUNT(Entry->Outputs)))
						{
							GraphBuilder.QueueTextureExtraction(Output, &Entry->Outputs[Slot]);
						}
						++Slot;
					}
					Ctx.PendingNodeEntries.Add(Entry);
				};

				if (NodeHit.IsValid())
				{
					UE_LOG(LogMixtormatComposition, VeryVerbose,
						TEXT("Node cache hit: layer %d child %d."), LayerIndex, Child.SourceChildIndex);
				}
				if (bClusterProducer && NodeHit.IsValid())
				{
					RegionIds = RegisterCached(NodeHit->Outputs[0], TEXT("Mixtormat.NodeCache.RegionIds"));
				}
				else if (bClusterProducer && Child.Filter.bSurfaceIds)
				{
					RegionIds = AddSurfaceIdPasses(Ctx, Layer, Child, LayerIndex, bIsSelectedPreview);
					KeepNode({RegionIds});
				}
				else if (bClusterProducer)
				{
					// The same ping-pong slot the layer composite and generated masks
					// read: what every layer below this one has accumulated.
					const int32 SurfaceReadIndex = 1 - (LayerIndex & 1);
					RegionIds = AddClusterIdPasses(
						GraphBuilder,
						RegisterTexture(
							GraphBuilder,
							RegisteredTextures,
							Layer.RAM,
							TEXT("Mixtormat.Cluster.SourceRAMH")),
						Layer.Height.IsValid()
							? RegisterTexture(GraphBuilder, RegisteredTextures, Layer.Height,
								TEXT("Mixtormat.Cluster.SourceHeight"))
							: HeightTargets[SurfaceReadIndex],
						OutputRAM[SurfaceReadIndex],
						HeightTargets[SurfaceReadIndex],
						LayerIndex > 0,
						bIsSelectedPreview,
						OutputDebug[Request.PublishedTargetIndex],
						Request.Resolution,
						Layer,
						Child,
						LayerIndex);
					KeepNode({RegionIds});
				}
				else
				{
					FPatternIdPassOutput& PatternOutput =
						PatternOutputs.AddDefaulted_GetRef();
					PatternOutput.SourceChildIndex = Child.SourceChildIndex;
					PatternOutput.Settings = &Child.PatternId;
					if (NodeHit.IsValid())
					{
						RegionIds = RegisterCached(NodeHit->Outputs[0], TEXT("Mixtormat.NodeCache.PatternIds"));
						PatternOutput.UV = RegisterCached(NodeHit->Outputs[1], TEXT("Mixtormat.NodeCache.PatternUV"));
						PatternOutput.Ramp = RegisterCached(NodeHit->Outputs[2], TEXT("Mixtormat.NodeCache.PatternRamp"));
						PatternOutput.Edge = RegisterCached(NodeHit->Outputs[3], TEXT("Mixtormat.NodeCache.PatternEdge"));
						PatternOutput.Gap = RegisterCached(NodeHit->Outputs[4], TEXT("Mixtormat.NodeCache.PatternGap"));
						PatternOutput.Orientation = NodeHit->Outputs[5].IsValid()
							? RegisterCached(NodeHit->Outputs[5], TEXT("Mixtormat.NodeCache.PatternOrientation"))
							: EmptyPatternOrientation;
					}
					else
					{
						RegionIds = AddPatternIdPasses(
							GraphBuilder,
							bIsSelectedPreview,
							OutputDebug[Request.PublishedTargetIndex],
							Request.Resolution,
							Child,
							LayerIndex,
							EmptyPatternOrientation,
							PatternOutput.UV,
							PatternOutput.Ramp,
							PatternOutput.Edge,
							PatternOutput.Gap,
							PatternOutput.Orientation);
						// The shared 1x1 stand-in is not this node's output; a hit restores it.
						KeepNode({RegionIds, PatternOutput.UV, PatternOutput.Ramp, PatternOutput.Edge,
							PatternOutput.Gap,
							PatternOutput.Orientation != EmptyPatternOrientation ? PatternOutput.Orientation : nullptr});
					}
					PatternOutput.Ids = RegionIds;
					Ctx.PublishedMaskOutputs.Add(
						FPublishedMaskKey{
							Layer.LayerId,
							Child.SourceChildIndex,
							FName(TEXT("Gap"))},
						PatternOutput.Gap);
					if (IsChildOutputPreviewTarget(
						Request, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Gap")),
						LayerIndex, Child.SourceChildIndex))
					{
						AddDebugPreviewMaskBlitPass(
							GraphBuilder, PatternOutput.Gap,
							OutputDebug[Request.PublishedTargetIndex], Request.Resolution);
					}
				}

				PublishRegionIds(RegionIdMaps, Child.SourceChildIndex, RegionIds);
			}

			// Only resolve groups whose references already have their exact source maps.
			// Generator-dependent groups are retried after module publication, not frozen empty.
			AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, MAX_int32, false);
		}
	}


}
