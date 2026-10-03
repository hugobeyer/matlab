// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuPatternPassesInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

class FMixtormatEdgeShadeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatEdgeShadeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatEdgeShadeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, BevelWidthPixels)
		SHADER_PARAMETER(float, BevelVariation)
		SHADER_PARAMETER(float, EdgeRoughness)
		SHADER_PARAMETER(float, EdgeRoughnessAmount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatEdgeShadeCS,
	"/Plugin/Mixtormat/Private/MixtormatEdgeShade.usf",
	"MainCS",
	SF_Compute);

// Per-region gradient. One entry point staged by Stage, so -- as with the cluster filter --
// every file-scope uniform is declared here whether a given stage reads it or not.
class FMixtormatRampIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRampIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRampIdsCS, FGlobalShader);

	static constexpr uint32 StageInit = 0;
	static constexpr uint32 StageBounds = 1;
	static constexpr uint32 StageResolve = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Stage)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, RotateRandom)
		SHADER_PARAMETER(uint32, AngleStepping)
		SHADER_PARAMETER(float, AngleStepDegrees)
		SHADER_PARAMETER(float, IntensityRandom)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionRootIds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, RegionBounds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRampIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIds.usf",
	"MainCS",
	SF_Compute);

// Pattern-only reduction: one maximum source height at each centre-pixel Region ID.
class FMixtormatPatternHeightMaxCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPatternHeightMaxCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPatternHeightMaxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputPatternHeightMax)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPatternHeightMaxCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIdRelief.usf",
	"PatternHeightMaxCS",
	SF_Compute);

// The post-composite half: the gradient turned into a tilt in the composited height and normal.
class FMixtormatRampIdReliefCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRampIdReliefCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRampIdReliefCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, HeightAmount)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, UseEdge)
		SHADER_PARAMETER(float, CellHeightAmount)
		SHADER_PARAMETER(float, CellHeightRandom)
		SHADER_PARAMETER(float, BevelHeight)
		SHADER_PARAMETER(float, BevelWidthPixels)
		SHADER_PARAMETER(float, BevelWidthCells)
		SHADER_PARAMETER(uint32, BevelRelative)
		SHADER_PARAMETER(float, BevelVariation)
		SHADER_PARAMETER(float, BevelRoundness)
		SHADER_PARAMETER(float, BevelRoundnessRandom)
		SHADER_PARAMETER(float, BevelInsetPixels)
		SHADER_PARAMETER(float, GapHeight)
		SHADER_PARAMETER(float, FeatherGain)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RampField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternHeightMax)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRampIdReliefCS,
	"/Plugin/Mixtormat/Private/MixtormatRampIdRelief.usf",
	"MainCS",
	SF_Compute);

// Jump-flood seed: the valid pixels that touch a boundary.
class FMixtormatRegionSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, SeedPolicy)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatBoundaryResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBoundaryResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBoundaryResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, BoundaryWidthPixels)
		SHADER_PARAMETER(float, BoundarySoftness)
		SHADER_PARAMETER(float, GapWidthPixels)
		SHADER_PARAMETER(float, GapSoftness)
		SHADER_PARAMETER(float, GapBiasPixels)
		SHADER_PARAMETER(float, DistanceRangePixels)
		SHADER_PARAMETER(uint32, InvertDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatBoundaryResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf", "BoundaryResolveCS", SF_Compute);

class FMixtormatRegionJumpCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionJumpCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionJumpCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionJumpCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"JumpCS",
	SF_Compute);

// Per-region reach, reduced at the root pixel.
class FMixtormatRegionExtentCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionExtentCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionExtentCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionRootIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RegionExtent)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionExtentCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"ExtentCS",
	SF_Compute);

// The edge and ramp fields, in the exact layout Pattern IDs publishes them in -- which is what
// lets one relief pass serve a Pattern source and a Cluster source without branching.
class FMixtormatRegionResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, FieldSeed)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(float, FeatherRandom)
		SHADER_PARAMETER(uint32, RelativeWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionExtentIn)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionRootIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputRamp)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatRegionFields.usf",
	"ResolveCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Builds one ramp filter's gradient field from an ID map.
	//
	// Three dispatches and one full-resolution int4 scratch buffer, so it is demand-culled the
	// same way the segmentation is: no tilt weight, no pass.
	static FRDGTextureRef AddRampIdPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef RegionIds,
		FRDGTextureRef RegionRootIds,
		FIntPoint OutputSize,
		const FRampIdRenderData& Ramp,
		int32 LayerIndex,
		int32 ChildIndex)
	{
		const uint32 PixelCount = static_cast<uint32>(OutputSize.X) * static_cast<uint32>(OutputSize.Y);
		// Four ints per pixel -- min x, max x, min y, max y -- strided in one buffer. Sized by
		// pixel rather than by raw identity: RegionRootIds supplies bounded analysis addresses.
		FRDGBufferRef RegionBounds = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), PixelCount * 4u),
			TEXT("Mixtormat.Ramp.RegionBounds"));
		const FRDGBufferUAVRef RegionBoundsUAV = GraphBuilder.CreateUAV(RegionBounds);

		FRDGTextureRef RampField = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_G16R16F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Ramp.Field"));
		const FRDGTextureUAVRef RampFieldUAV = GraphBuilder.CreateUAV(RampField);

		TShaderMapRef<FMixtormatRampIdsCS> RampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8), FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		for (uint32 Stage = FMixtormatRampIdsCS::StageInit;
			Stage <= FMixtormatRampIdsCS::StageResolve;
			++Stage)
		{
			FMixtormatRampIdsCS::FParameters* Parameters =
				GraphBuilder.AllocParameters<FMixtormatRampIdsCS::FParameters>();
			Parameters->OutputSize = OutputSize;
			Parameters->Stage = Stage;
			Parameters->Seed = Ramp.Seed;
			Parameters->RotateRandom = Ramp.bRotateRandom ? 1u : 0u;
			Parameters->AngleStepping = Ramp.bAngleStepping ? 1u : 0u;
			Parameters->AngleStepDegrees = Ramp.AngleStepDegrees;
			Parameters->IntensityRandom = Ramp.IntensityRandom;
			Parameters->RegionIds = RegionIds;
			Parameters->RegionRootIds = RegionRootIds;
			Parameters->RegionBounds = RegionBoundsUAV;
			Parameters->OutputRamp = RampFieldUAV;

			// Default UAV barriers, deliberately: the bounds reduction has to have finished for
			// every pixel of a region before any pixel of it reads the box back.
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.RampIds.Layer%d.Child%d.Stage%u", LayerIndex, ChildIndex, Stage),
				RampShader, Parameters, Groups);
		}
		return RampField;
	}

	// Distance to the nearest region boundary, by jump flooding, plus each region's reach.
	//
	// Records are cached graph-wide by source texture, resolution and seed policy, so consumers
	// of the same map and policy share the solve. Log2(N) passes and two
	// full-resolution textures is the same order as the cluster segmentation, and paying it twice
	// for two relief nodes over one pattern would be the obvious waste.
	static FRDGTextureRef AddRegionDistanceRecordPasses(
		FRDGBuilder& GraphBuilder,
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef RegionIds,
		const ERegionSeedPolicy Policy,
		const int32 ProducerIndex,
		const FIntPoint OutputSize,
		const int32 LayerIndex)
	{
		for (const FRegionDistanceRecord& Entry : Ctx.RegionDistanceRecords)
		{
			if (Entry.Source == RegionIds && Entry.Resolution == OutputSize && Entry.Policy == Policy)
			{
				return Entry.Record;
			}
		}

		const FIntVector Groups(
			FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8),
			1);

		// A boundary seed is only an integer pixel coordinate. Pack X/Y into one R32_UINT
		// (16 bits each) instead of carrying float4(x, y, valid, unused). At 4K this cuts
		// the JFA ping-pong pair from 512 MiB to 128 MiB with exact pixel coordinates.
		const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_R32_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Record[2] = {
			GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.Region.JfaA")),
			GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.Region.JfaB"))};

		TShaderMapRef<FMixtormatRegionSeedCS> SeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionSeedCS::FParameters* SeedParameters =
			GraphBuilder.AllocParameters<FMixtormatRegionSeedCS::FParameters>();
		SeedParameters->OutputSize = OutputSize;
		SeedParameters->RegionIds = RegionIds;
		SeedParameters->SeedPolicy = static_cast<uint32>(Policy);
		SeedParameters->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Seed.L%d.C%d", LayerIndex, ProducerIndex),
			SeedShader, SeedParameters, Groups);

		// Strides halve from half the padded extent down to 1, then one more pass at 1 -- the
		// JFA+1 variant, for the same reason the craquelure field uses it: plain jump flooding can
		// lose a seed whose carrier was overwritten at a coarser stride, and the extra unit pass
		// costs one dispatch and removes the islands that shows up as.
		TShaderMapRef<FMixtormatRegionJumpCS> JumpShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		int32 RecordIndex = 0;
		const int32 FirstStep = FMath::Max(
			1,
			static_cast<int32>(FMath::RoundUpToPowerOfTwo(
				static_cast<uint32>(FMath::Max(OutputSize.X, OutputSize.Y)))) / 2);
		for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
		{
			const int32 Read = RecordIndex;
			const int32 Write = 1 - Read;
			FMixtormatRegionJumpCS::FParameters* JumpParameters =
				GraphBuilder.AllocParameters<FMixtormatRegionJumpCS::FParameters>();
			JumpParameters->OutputSize = OutputSize;
			JumpParameters->StepSize = StepSize;
			JumpParameters->PreviousRecord = Record[Read];
			JumpParameters->OutputRecord = GraphBuilder.CreateUAV(Record[Write]);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.RegionFields.Jump%d.L%d.C%d", StepSize, LayerIndex, ProducerIndex),
				JumpShader, JumpParameters, Groups);
			RecordIndex = Write;
		}
		{
			const int32 Read = RecordIndex;
			const int32 Write = 1 - Read;
			FMixtormatRegionJumpCS::FParameters* JumpParameters =
				GraphBuilder.AllocParameters<FMixtormatRegionJumpCS::FParameters>();
			JumpParameters->OutputSize = OutputSize;
			JumpParameters->StepSize = 1;
			JumpParameters->PreviousRecord = Record[Read];
			JumpParameters->OutputRecord = GraphBuilder.CreateUAV(Record[Write]);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.RegionFields.JumpFinal.L%d.C%d", LayerIndex, ProducerIndex),
				JumpShader, JumpParameters, Groups);
			RecordIndex = Write;
		}

		Ctx.RegionDistanceRecords.Add(FRegionDistanceRecord{RegionIds, OutputSize, Policy, Record[RecordIndex]});
		return Record[RecordIndex];
	}

	// Relief alone requires per-region reach. Boundary never indexes IDs or allocates extent.
	static FRegionDistanceCacheEntry AddRegionDistancePasses(
		FRDGBuilder& GraphBuilder,
		FMixtormatLayerPassContext& LayerCtx,
		FRDGTextureRef RegionIds,
		FRDGTextureRef RegionRootIds,
		const int32 ProducerIndex,
		const FIntPoint OutputSize,
		const int32 LayerIndex)
	{
		for (const FRegionDistanceCacheEntry& Entry : LayerCtx.RegionDistanceCache)
		{
			if (Entry.Source == RegionIds && Entry.RootIds == RegionRootIds && Entry.Resolution == OutputSize)
			{
				return Entry;
			}
		}
		const FRDGTextureRef Record = AddRegionDistanceRecordPasses(GraphBuilder, LayerCtx.Ctx,
			RegionIds, ERegionSeedPolicy::LegacyFour, ProducerIndex, OutputSize, LayerIndex);
		const FIntVector Groups(FMath::DivideAndRoundUp(OutputSize.X, 8),
			FMath::DivideAndRoundUp(OutputSize.Y, 8), 1);
		FRDGTextureRef Extent = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				OutputSize,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Region.Extent"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Extent), 0u);

		TShaderMapRef<FMixtormatRegionExtentCS> ExtentShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionExtentCS::FParameters* ExtentParameters =
			GraphBuilder.AllocParameters<FMixtormatRegionExtentCS::FParameters>();
		ExtentParameters->OutputSize = OutputSize;
		ExtentParameters->RegionIds = RegionIds;
		ExtentParameters->RegionRootIds = RegionRootIds;
		ExtentParameters->PreviousRecord = Record;
		ExtentParameters->RegionExtent = GraphBuilder.CreateUAV(Extent);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Extent.L%d.C%d", LayerIndex, ProducerIndex),
			ExtentShader, ExtentParameters, Groups);

		FRegionDistanceCacheEntry& Entry = LayerCtx.RegionDistanceCache.AddDefaulted_GetRef();
		Entry.SourceChildIndex = ProducerIndex;
		Entry.Source = RegionIds;
		Entry.RootIds = RegionRootIds;
		Entry.Resolution = OutputSize;
		Entry.Record = Record;
		Entry.Extent = Extent;
		// By value. The cache is inline-allocated, so a reference into it would dangle the moment
		// a second consumer of a different producer pushed its own entry.
		return Entry;
	}

	// Resolution depends on published texture type/address, never on the source producer's enum.
	static FRDGTextureRef ResolveBoundaryRegionIds(
		const FMixtormatComposeContext& Ctx, const FMixtormatLayerPassContext& LayerCtx,
		const FChildRenderData& Child)
	{
		const auto IsValid = [&Ctx](FRDGTextureRef Texture)
		{
			return Texture && Texture->Desc.Format == PF_R32_UINT
				&& Texture->Desc.Extent == Ctx.Request.Resolution;
		};
		const FBoundaryIdRenderData& Boundary = Child.BoundaryId;
		if (Boundary.bExplicitSource)
		{
			const FPublishedFieldKey& Address = Boundary.RegionIdsSource.Source;
			if (Boundary.RegionIdsSource.Kind != EMixtormatPublishedFieldKind::RegionIds
				|| Address.Output != FName(TEXT("RegionIds"))) { return nullptr; }
			if (Ctx.Request.Layers.IsValidIndex(LayerCtx.LayerIndex)
				&& Address.LayerId == Ctx.Request.Layers[LayerCtx.LayerIndex].LayerId)
			{
				// Current-layer IDs are not published to the graph registry until layer end.
				for (const auto& Map : LayerCtx.RegionIdMaps)
				{
					if (Map.Key == Address.ChildIndex && Map.Key < Child.SourceChildIndex)
					{
						return IsValid(Map.Value) ? Map.Value : nullptr;
					}
				}
				return nullptr;
			}
			const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Address);
			return Boundary.RegionIdsSource.Kind == EMixtormatPublishedFieldKind::RegionIds
				&& Field && Field->Kind == EMixtormatPublishedFieldKind::RegionIds
				&& Field->IsComplete() && IsValid(Field->Texture) ? Field->Texture : nullptr;
		}
		if (!Ctx.Request.Layers.IsValidIndex(LayerCtx.LayerIndex)) { return nullptr; }
		int32 ProducerIndex = INDEX_NONE;
		const FRDGTextureRef Source = FindRegionIdsAboveWithIndex(
			Ctx.Request.Layers[LayerCtx.LayerIndex], LayerCtx.RegionIdMaps, Child, ProducerIndex);
		return IsValid(Source) ? Source : nullptr;
	}

	void AddBoundaryIdPass(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child)
	{
		if (!Layer.bEnabled) { return; }
		const FRDGTextureRef RegionIds = ResolveBoundaryRegionIds(Ctx, LayerCtx, Child);
		if (!RegionIds) { return; } // Unresolved explicit sources never fall back.
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FRDGTextureRef Record = AddRegionDistanceRecordPasses(GraphBuilder, Ctx, RegionIds,
			ERegionSeedPolicy::ValidOnlyEight, Child.SourceChildIndex, Size, LayerCtx.LayerIndex);
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, PF_R32_FLOAT,
			FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Outputs[] = {
			GraphBuilder.CreateTexture(Desc, TEXT("Mixtormat.BoundaryId.Boundary")),
			GraphBuilder.CreateTexture(Desc, TEXT("Mixtormat.BoundaryId.Gap")),
			GraphBuilder.CreateTexture(Desc, TEXT("Mixtormat.BoundaryId.Distance"))};
		const FBoundaryIdRenderData& Boundary = Child.BoundaryId;
		TShaderMapRef<FMixtormatBoundaryResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* Parameters = GraphBuilder.AllocParameters<FMixtormatBoundaryResolveCS::FParameters>();
		Parameters->OutputSize = Size;
		Parameters->BoundaryWidthPixels = Boundary.WidthPixels;
		Parameters->BoundarySoftness = Boundary.Softness;
		Parameters->GapWidthPixels = Boundary.GapWidthPixels;
		Parameters->GapSoftness = Boundary.GapSoftness;
		Parameters->GapBiasPixels = Boundary.GapBiasPixels;
		Parameters->DistanceRangePixels = Boundary.DistanceRangePixels;
		Parameters->InvertDistance = Boundary.bInvertDistance ? 1u : 0u;
		Parameters->RegionIds = RegionIds;
		Parameters->PreviousRecord = Record;
		Parameters->OutputBoundary = GraphBuilder.CreateUAV(Outputs[0]);
		Parameters->OutputGap = GraphBuilder.CreateUAV(Outputs[1]);
		Parameters->OutputDistance = GraphBuilder.CreateUAV(Outputs[2]);
		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.BoundaryId.Resolve.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
			Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		const FName Names[] = {FName(TEXT("Boundary")), FName(TEXT("Gap")), FName(TEXT("Distance"))};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Outputs); ++Index)
		{
			Ctx.PublishedMaskOutputs.Add(
				FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, Names[Index]}, Outputs[Index]);
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::Mask,
				Names[Index], LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewMaskBlitPass(GraphBuilder, Outputs[Index],
					Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
			}
		}
	}


	// One Relief From IDs node's edge and ramp fields. Everything above this is shared; only this
	// dispatch reads the node's own feather and width, which is why the cache above is worth
	// having at all.
	static void AddRegionReliefFieldPass(
		FRDGBuilder& GraphBuilder,
		const FRegionDistanceCacheEntry& Distance,
		FRDGTextureRef RegionIds,
		FRDGTextureRef RegionRootIds,
		const FReliefIdRenderData& Relief,
		const FIntPoint OutputSize,
		const int32 LayerIndex,
		const int32 ChildIndex,
		FRDGTextureRef& OutEdge,
		FRDGTextureRef& OutRamp)
	{
		const FRDGTextureDesc Float2Desc = FRDGTextureDesc::Create2D(
			OutputSize,
			PF_G16R16F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		OutEdge = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Region.Edge"));
		OutRamp = GraphBuilder.CreateTexture(Float2Desc, TEXT("Mixtormat.Region.Ramp"));

		TShaderMapRef<FMixtormatRegionResolveCS> ResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatRegionResolveCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatRegionResolveCS::FParameters>();
		Parameters->OutputSize = OutputSize;
		Parameters->FieldSeed = Relief.Seed;
		Parameters->Feather = Relief.Feather;
		Parameters->FeatherRandom = Relief.FeatherRandom;
		Parameters->RelativeWidth = Relief.bRelativeWidth ? 1u : 0u;
		Parameters->RegionIds = RegionIds;
		Parameters->RegionRootIds = RegionRootIds;
		Parameters->PreviousRecord = Distance.Record;
		Parameters->RegionExtentIn = Distance.Extent;
		Parameters->OutputEdge = GraphBuilder.CreateUAV(OutEdge);
		Parameters->OutputRamp = GraphBuilder.CreateUAV(OutRamp);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionFields.Resolve.L%d.C%d", LayerIndex, ChildIndex),
			ResolveShader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(OutputSize.X, 8),
				FMath::DivideAndRoundUp(OutputSize.Y, 8),
				1));
	}

	// Pattern and Ramp-from-ID relief, collected before the composite and dispatched after it:
	// their fields exist before the composite, the surface they modify does not.
	void CollectPendingRampTilts(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
			LayerCtx.PendingRampTilts;
		TMap<FRDGTextureRef, FRDGTextureRef> RootCache;
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type == EMixtormatLayerChildType::PatternId)
			{
				const FPatternIdPassOutput* PatternOutput = nullptr;
				for (const FPatternIdPassOutput& Candidate : PatternOutputs)
				{
					if (Candidate.SourceChildIndex == Child.SourceChildIndex)
					{
						PatternOutput = &Candidate;
						break;
					}
				}
				if (!PatternOutput)
				{
					continue;
				}

				const FPatternIdRenderData& Pattern = Child.PatternId;
				const bool bNeedsRelief = Pattern.HeightAmount > 0.0f
					|| Pattern.BevelHeight != 0.0f
					|| Pattern.GapHeight != 0.0f;
				const bool bNeedsShade =
					Pattern.EdgeRoughnessAmount > 0.0f;
				if (!bNeedsRelief && !bNeedsShade)
				{
					continue;
				}

				FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
				Tilt.Field = PatternOutput->Ramp;
				Tilt.EdgeField = PatternOutput->Edge;
				Tilt.RegionIds = PatternOutput->Ids;
				// Pattern has no tilt term: HeightAmount is its per-cell elevation
				// range and rides CellHeightAmount, leaving the relief pass's tilt
				// path -- which is Ramp From IDs' -- switched off.
				Tilt.HeightAmount = 0.0f;
				// Height is the elevation every cell gets; Height Random is how far
				// below it a cell may be drawn. Multiplied in the shader, not folded
				// together here, or Random 0 would zero the whole term instead of
				// leaving every cell at full Height.
				Tilt.CellHeightAmount = Pattern.HeightAmount;
				Tilt.CellHeightRandom = Pattern.HeightRandom;

				Tilt.bUseEdge = true;
				Tilt.BevelHeight = Pattern.BevelHeight;
				Tilt.BevelWidthPixels = Pattern.BevelWidthPixels;
				Tilt.BevelWidthCells = Pattern.BevelWidthCells;
				Tilt.bBevelRelative = Pattern.bRelativeEdgeWidth;
				Tilt.BevelVariation = Pattern.BevelVariation;
				Tilt.BevelRoundness = Pattern.BevelRoundness;
				Tilt.BevelRoundnessRandom = Pattern.BevelRoundnessRandom;
				Tilt.BevelInsetPixels = Pattern.BevelInsetPixels;
				Tilt.GapHeight = Pattern.GapHeight;
				Tilt.FeatherGain = Pattern.FeatherGain;
				Tilt.EdgeRoughness = Pattern.EdgeRoughness;
				Tilt.EdgeRoughnessAmount = Pattern.EdgeRoughnessAmount;
				continue;
			}

			if (Child.Type == EMixtormatLayerChildType::ReliefFromIds)
			{
				int32 ProducerIndex = INDEX_NONE;
				FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
					Layer, RegionIdMaps, Child, ProducerIndex);
				if (!RegionIds)
				{
					// Nothing above publishes IDs. Culled rather than defaulted: a relief with no
					// regions has nothing to shape, and writing a flat field would still cost the
					// jump flood and still touch the composited height.
					continue;
				}

				const FReliefIdRenderData& Relief = Child.ReliefId;
				const bool bNeedsRelief = Relief.HeightAmount > 0.0f
					|| Relief.BevelHeight != 0.0f
					|| Relief.GapHeight != 0.0f;
				const bool bNeedsShade =
					Relief.EdgeRoughnessAmount > 0.0f;
				if (!bNeedsRelief && !bNeedsShade)
				{
					continue;
				}

				const FRDGTextureRef RegionRootIds = AddRegionIndexPasses(
					GraphBuilder, RegionIds, HasPixelRootIds(Layer, ProducerIndex),
					Request.Resolution, RootCache);
				const FRegionDistanceCacheEntry Distance = AddRegionDistancePasses(
					GraphBuilder, LayerCtx, RegionIds, RegionRootIds, ProducerIndex,
					Request.Resolution, LayerIndex);
				FRDGTextureRef EdgeField = nullptr;
				FRDGTextureRef RampField = nullptr;
				AddRegionReliefFieldPass(
					GraphBuilder, Distance, RegionIds, RegionRootIds, Relief, Request.Resolution,
					LayerIndex, Child.SourceChildIndex, EdgeField, RampField);

				// From here down this is the Pattern relief path, unchanged. The fields carry the
				// same meaning in the same channels, so the pass that consumes them neither knows
				// nor needs to know which producer the regions came from.
				FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
				Tilt.Field = RampField;
				Tilt.EdgeField = EdgeField;
				Tilt.RegionIds = RegionIds;
				// No tilt term: HeightAmount here is a per-region elevation range and rides
				// CellHeightAmount, leaving the relief pass's tilt path -- which is Ramp From
				// IDs' -- switched off. Ramp and Relief compose rather than overlap.
				Tilt.HeightAmount = 0.0f;
				Tilt.CellHeightAmount = Relief.HeightAmount;
				Tilt.CellHeightRandom = Relief.HeightRandom;
				Tilt.bUseEdge = true;
				Tilt.BevelHeight = Relief.BevelHeight;
				Tilt.BevelWidthPixels = Relief.BevelWidthPixels;
				Tilt.BevelWidthCells = Relief.BevelWidthCells;
				Tilt.bBevelRelative = Relief.bRelativeWidth;
				Tilt.BevelVariation = Relief.BevelVariation;
				Tilt.BevelRoundness = Relief.Profile;
				Tilt.BevelRoundnessRandom = Relief.ProfileRandom;
				Tilt.BevelInsetPixels = Relief.BevelInsetPixels;
				Tilt.GapHeight = Relief.GapHeight;
				Tilt.FeatherGain = Relief.FeatherGain;
				Tilt.EdgeRoughness = Relief.EdgeRoughness;
				Tilt.EdgeRoughnessAmount = Relief.EdgeRoughnessAmount;
				continue;
			}

			if (Child.Type != EMixtormatLayerChildType::RampId)
			{
				continue;
			}

			int32 ProducerIndex = INDEX_NONE;
			FRDGTextureRef RegionIds = FindRegionIdsAboveWithIndex(
				Layer, RegionIdMaps, Child, ProducerIndex);
			if (!RegionIds)
			{
				continue;
			}
			const FRampIdRenderData& Ramp = Child.RampId;
			// The preview builds the gradient even at zero strength: it shows what the node would
			// lay down, which is what an artist needs before turning the height up.
			const bool bPreviewRamp = IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask,
				FName(TEXT("Ramp")), LayerIndex, Child.SourceChildIndex);
			if (Ramp.HeightAmount <= 0.0f && !bPreviewRamp)
			{
				continue;
			}

			const FRDGTextureRef RampField = AddRampIdPasses(
				GraphBuilder,
				RegionIds,
				AddRegionIndexPasses(GraphBuilder, RegionIds, HasPixelRootIds(Layer, ProducerIndex),
					Request.Resolution, RootCache),
				Request.Resolution,
				Ramp,
				LayerIndex,
				Child.SourceChildIndex);
			if (bPreviewRamp)
			{
				// The gradient is the field's first channel, which is what the mask blit reads.
				AddDebugPreviewMaskBlitPass(GraphBuilder, RampField,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Request.Resolution);
			}
			if (Ramp.HeightAmount <= 0.0f)
			{
				continue;
			}

			FPendingRampTilt& Tilt = PendingRampTilts.AddDefaulted_GetRef();
			Tilt.Field = RampField;
			Tilt.EdgeField = Tilt.Field;
			Tilt.HeightAmount = Ramp.HeightAmount;
			Tilt.BlendMode = static_cast<uint32>(Ramp.BlendMode);
		}
	}

	// Region tilt and pattern edge shading, before craquelure relief: a crack carved into a
	// tile that has already settled is right, the reverse drags the groove's depth around.
	void AddRampReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyRegionIds = Ctx.EmptyRegionIds;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
			LayerCtx.PendingRampTilts;
		TShaderMapRef<FMixtormatEdgeShadeCS> EdgeShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatPatternHeightMaxCS> PatternHeightMaxShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatRampIdReliefCS> RampIdReliefShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TMap<FRDGTextureRef, FRDGTextureRef> RootCache;
		// The region tilt runs before craquelure relief, and the order is not
		// arbitrary: a crack carved into a tile that has already settled is right,
		// whereas tilting a tile after its crack was carved drags the groove's depth
		// around with the slope.
		for (int32 TiltIndex = 0; TiltIndex < PendingRampTilts.Num(); ++TiltIndex)
		{
			const FPendingRampTilt& Tilt = PendingRampTilts[TiltIndex];
			const bool bNeedsRelief =
				Tilt.HeightAmount > 0.0f
				|| (Tilt.bUseEdge
					&& (Tilt.CellHeightAmount > 0.0f
						|| Tilt.BevelHeight != 0.0f
						|| Tilt.GapHeight != 0.0f));
			if (bNeedsRelief)
			{
				FRDGTextureRef TiltH = GraphBuilder.CreateTexture(
					HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.RampTiltH"));
				FRDGTextureRef TiltN = GraphBuilder.CreateTexture(
					OutputN[WriteIndex]->Desc, TEXT("Mixtormat.RampTiltN"));

				FRDGTextureRef PatternHeightMax = EmptyRegionIds;
				FRDGTextureRef HeightRootIds = EmptyRegionIds;
				if (Tilt.bUseEdge && Tilt.CellHeightAmount > 0.0f && Tilt.RegionIds)
				{
					const bool bPatternPixelRoots = LayerCtx.PatternOutputs.ContainsByPredicate(
						[&Tilt](const FPatternIdPassOutput& Output) { return Output.Ids == Tilt.RegionIds; });
					HeightRootIds = AddRegionIndexPasses(GraphBuilder, Tilt.RegionIds,
						bPatternPixelRoots, Request.Resolution, RootCache);
					PatternHeightMax = GraphBuilder.CreateTexture(
						FRDGTextureDesc::Create2D(
							Request.Resolution,
							PF_R32_UINT,
							FClearValueBinding::None,
							TexCreate_ShaderResource | TexCreate_UAV),
						TEXT("Mixtormat.Pattern.HeightMax"));
					AddClearUAVPass(
						GraphBuilder,
						GraphBuilder.CreateUAV(PatternHeightMax),
						0u);

					FMixtormatPatternHeightMaxCS::FParameters* MaxP =
						GraphBuilder.AllocParameters<FMixtormatPatternHeightMaxCS::FParameters>();
					MaxP->OutputSize = Request.Resolution;
					// This shader uses IDs only as addresses; random draws remain in the raw-ID fields.
					MaxP->PatternRegionIds = HeightRootIds;
					MaxP->SourceHeight = HeightTargets[WriteIndex];
					MaxP->OutputPatternHeightMax = GraphBuilder.CreateUAV(PatternHeightMax);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.PatternHeightMax.L%d.%d", LayerIndex, TiltIndex),
						PatternHeightMaxShader,
						MaxP,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));
				}

				FMixtormatRampIdReliefCS::FParameters* TiltP =
					GraphBuilder.AllocParameters<FMixtormatRampIdReliefCS::FParameters>();
				TiltP->OutputSize = Request.Resolution;
				TiltP->HeightAmount = Tilt.HeightAmount;
				TiltP->NormalStrength = HeightDerivedNormalStrength;
				TiltP->BlendMode = Tilt.BlendMode;
				TiltP->UseEdge = Tilt.bUseEdge ? 1u : 0u;
				TiltP->CellHeightAmount = Tilt.CellHeightAmount;
				TiltP->CellHeightRandom = Tilt.CellHeightRandom;
				TiltP->BevelHeight = Tilt.BevelHeight;
				TiltP->BevelWidthPixels = Tilt.BevelWidthPixels;
				TiltP->BevelWidthCells = Tilt.BevelWidthCells;
				TiltP->BevelRelative = Tilt.bBevelRelative ? 1u : 0u;
				TiltP->BevelVariation = Tilt.BevelVariation;
				TiltP->BevelRoundness = Tilt.BevelRoundness;
				TiltP->BevelRoundnessRandom = Tilt.BevelRoundnessRandom;
				TiltP->BevelInsetPixels = Tilt.BevelInsetPixels;
				TiltP->GapHeight = Tilt.GapHeight;
				TiltP->FeatherGain = Tilt.FeatherGain;
				TiltP->RampField = Tilt.Field;
				TiltP->EdgeField = Tilt.EdgeField ? Tilt.EdgeField : Tilt.Field;
				TiltP->SourceHeight = HeightTargets[WriteIndex];
				TiltP->PreviousNormal = OutputN[WriteIndex];
				TiltP->PatternRegionIds = HeightRootIds;
				TiltP->PatternHeightMax = PatternHeightMax;
				TiltP->OutputHeight = GraphBuilder.CreateUAV(TiltH);
				TiltP->OutputNormal = GraphBuilder.CreateUAV(TiltN);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.RegionRelief.L%d.%d", LayerIndex, TiltIndex),
					RampIdReliefShader,
					TiltP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));

				AddHeightDerivedNormalPass(
					Ctx,
					HeightTargets[WriteIndex],
					TiltH,
					OutputN[WriteIndex],
					OutputRAM[WriteIndex],
					TiltN,
					nullptr,
					Request.Resolution,
					HeightDerivedNormalStrength,
					false,
					TEXT("RegionRelief"));
				AddCopyTexturePass(GraphBuilder, TiltH, HeightTargets[WriteIndex]);
				AddCopyTexturePass(GraphBuilder, TiltN, OutputN[WriteIndex]);
			}

			if (Tilt.bUseEdge
				&& Tilt.EdgeRoughnessAmount > 0.0f)
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.PatternEdgeRAM"));
				FMixtormatEdgeShadeCS::FParameters* EdgeP =
					GraphBuilder.AllocParameters<FMixtormatEdgeShadeCS::FParameters>();
				EdgeP->OutputSize = Request.Resolution;
				// The edge field is a region fraction in Relative mode, so the width must be too.
				EdgeP->BevelWidthPixels = Tilt.bBevelRelative ? Tilt.BevelWidthCells : Tilt.BevelWidthPixels;
				EdgeP->BevelVariation = Tilt.BevelVariation;
				EdgeP->EdgeRoughness = Tilt.EdgeRoughness;
				EdgeP->EdgeRoughnessAmount = Tilt.EdgeRoughnessAmount;
				EdgeP->EdgeField = Tilt.EdgeField;
				EdgeP->SourceRAM = OutputRAM[WriteIndex];
				EdgeP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.PatternEdgeShade.L%d.%d", LayerIndex, TiltIndex),
					EdgeShadeShader,
					EdgeP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));
				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}
		}
	}
}
