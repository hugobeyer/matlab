// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorCommon.h"
#include "Compositing/MixtormatComposeHash.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

class FMixtormatMyceliumCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMyceliumCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMyceliumCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("MYCELIUM_STAGE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	struct FHyphalSegment
	{
		float P0[2];
		float P1[2];
		float Radius0;
		float Radius1;
		uint32 Generation;
		uint32 ColonyId;
		float Age;
		float Pad;
	};
	static_assert(sizeof(FHyphalSegment) == 40, "FHyphalSegment stride");
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, bNormalizeHeight)
		SHADER_PARAMETER(int32, ColonyCount)
		SHADER_PARAMETER(float, GrowthExtent)
		SHADER_PARAMETER(float, Coverage)
		SHADER_PARAMETER(float, NutrientAttraction)
		SHADER_PARAMETER(float, BranchingFrequency)
		SHADER_PARAMETER(float, DirectionalPersistence)
		SHADER_PARAMETER(float, NetworkConnectivity)
		SHADER_PARAMETER(float, SecondaryBranchDensity)
		SHADER_PARAMETER(float, MicrofilamentDensity)
		SHADER_PARAMETER(float, FilamentWidth)
		SHADER_PARAMETER(float, Taper)
		SHADER_PARAMETER(uint32, BinResolution)
		SHADER_PARAMETER(uint32, SegmentCapacity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SubstrateHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FHyphalSegment>, Segments)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SegmentCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, BinHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, BinEntries)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGrowthAge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutGrowthIds)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatMyceliumCS, "/Plugin/Mixtormat/Private/MixtormatMycelium.usf", "MainCS", SF_Compute);

class FMixtormatScalesCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatScalesCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatScalesCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("SCALES_STAGE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(FIntPoint, ScaleCount)
		SHADER_PARAMETER(FIntPoint, SearchReach)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, Shape)
		SHADER_PARAMETER(float, Stagger)
		SHADER_PARAMETER(float, Direction)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Length)
		SHADER_PARAMETER(float, TipRoundness)
		SHADER_PARAMETER(float, Taper)
		SHADER_PARAMETER(float, Overlap)
		SHADER_PARAMETER(float, TipLift)
		SHADER_PARAMETER(float, RootTuck)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Dome)
		SHADER_PARAMETER(float, Ridge)
		SHADER_PARAMETER(float, Bevel)
		SHADER_PARAMETER(float, SizeRandom)
		SHADER_PARAMETER(float, RotationRandom)
		SHADER_PARAMETER(float, HeightRandom)
		SHADER_PARAMETER(float, RowJitter)
		SHADER_PARAMETER(float, GrooveAmount)
		SHADER_PARAMETER(float, GrooveCount)
		SHADER_PARAMETER(float, GrooveDirection)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FScalePlate>, OutPlates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FScalePlate>, Plates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScaleEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutScaleIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutScaleUV)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatScalesCS, "/Plugin/Mixtormat/Private/MixtormatScales.usf", "MainCS", SF_Compute);

// Root Network: space-colonization skeleton -> GPU segment buffers -> binned swept SDF.
// One permutation per stage; see MixtormatRootNetwork.usf for the stage table.
class FMixtormatRootNetworkCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRootNetworkCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRootNetworkCS, FGlobalShader);

	// 0 init, 1 assign, 2 extend, 3 segments, 4 scan, 5 scatter, 6 field (cached).
	class FStage : SHADER_PERMUTATION_INT("ROOT_STAGE", 7);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, GeneratorLayer)
		SHADER_PARAMETER(FVector2f, GeneratorUVScale)
		SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
		SHADER_PARAMETER(int32, GeneratorUVRotation)
		SHADER_PARAMETER(uint32, GeneratorUVFlipU)
		SHADER_PARAMETER(uint32, GeneratorUVFlipV)
		SHADER_PARAMETER(uint32, UsePreGenerationUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, Trunks)
		SHADER_PARAMETER(uint32, Attractors)
		SHADER_PARAMETER(uint32, StepIndex)
		SHADER_PARAMETER(float, StepLength)
		SHADER_PARAMETER(float, InfluenceRadius)
		SHADER_PARAMETER(float, KillDistance)
		SHADER_PARAMETER(float, BranchSpread)
		SHADER_PARAMETER(float, DirectionInertia)
		SHADER_PARAMETER(float, CurlStrength)
		SHADER_PARAMETER(uint32, CurlScale)
		SHADER_PARAMETER(float, BaseRadius)
		SHADER_PARAMETER(float, TipTaper)
		SHADER_PARAMETER(float, JunctionSmooth)
		SHADER_PARAMETER(float, ProfileFlatness)
		SHADER_PARAMETER(float, WeaveAmount)
		SHADER_PARAMETER(float, KinkStrength)
		SHADER_PARAMETER(float, ProfileBias)
		SHADER_PARAMETER(float, BurialDepth)
		SHADER_PARAMETER(float, AzimuthAngle)
		SHADER_PARAMETER(float, AzimuthStrength)
		SHADER_PARAMETER(float, SwirlStrength)
		SHADER_PARAMETER(float, AxisRatio)
		SHADER_PARAMETER(uint32, UseRadiusCurve)
		SHADER_PARAMETER(uint32, RadiusCurveCount)
		SHADER_PARAMETER(uint32, RadiusCurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, RadiusCurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER(uint32, UseBurialCurve)
		SHADER_PARAMETER(uint32, BurialCurveCount)
		SHADER_PARAMETER(uint32, BurialCurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, BurialCurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER(float, RootUVScaleU)
		SHADER_PARAMETER(float, RootUVScaleV)
		SHADER_PARAMETER(float, RootUVOffsetU)
		SHADER_PARAMETER(float, RootUVOffsetV)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootNode>, RWNodeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootSegment>, RWSegmentBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FRootAttractor>, RWAttractorBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWStateBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, RWNodePullBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinCounts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinWriteHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBinEntries)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRootSegment>, SegmentBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinCounts)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BinEntries)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootCoverage)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootEdgeDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootArcLength)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRootAcross)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutRootUV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutRootIds)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRootNetworkCS,
	"/Plugin/Mixtormat/Private/MixtormatRootNetwork.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	FRDGTextureRef AddMyceliumPasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer, const FGeneratorPassInput& Child, FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight, FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FMyceliumRenderData& Data = Child.Generator.Mycelium;
		const FMixtormatMycelium& Settings = Data.Settings;
		const FIntPoint Size = Ctx.Request.Resolution;
		constexpr int32 SlotCount = 4;
		FRDGTextureRef Outputs[SlotCount] = {};
		static const TCHAR* Names[SlotCount] = {
			TEXT("Mixtormat.Mycelium.Height"), TEXT("Mixtormat.Mycelium.Coverage"),
			TEXT("Mixtormat.Mycelium.Age"), TEXT("Mixtormat.Mycelium.Ids")};
		if (const auto* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Index = 0; Index < SlotCount; ++Index) { Outputs[Index] = (*Memo)[Index]; }
		}
		else
		{
			FRDGTextureRef Substrate = ResolveSubstrateHeight(Ctx, LayerCtx, RunningHeight, BelowHeight);

			FMixtormatNodeCache* Cache = Ctx.Request.NodeCache.Get();
			const uint64 Key = Cache && !Child.PreUV && Data.FieldKey
				? MixtormatComposeHash::Combine(Data.FieldKey, 0x4d7963654e6f6432ull) : 0;
			const auto Hit = Key ? Cache->Find(Key, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			bool bComplete = Hit.IsValid();
			for (int32 Index = 0; bComplete && Index < SlotCount; ++Index)
			{
				bComplete = Hit->Outputs[Index].IsValid();
			}
			if (bComplete)
			{
				for (int32 Index = 0; Index < SlotCount; ++Index)
				{
					Outputs[Index] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[Index], Names[Index]);
				}
			}
			else
			{
				const EPixelFormat Formats[SlotCount] = {PF_R32_FLOAT, PF_R16F, PF_R16F, PF_R32_UINT};
				for (int32 Index = 0; Index < SlotCount; ++Index)
				{
					Outputs[Index] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Formats[Index],
						FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Names[Index]);
				}

				constexpr uint32 SegmentCapacity = 4096;
				constexpr uint32 BinResolution = 32;

				FRDGBufferRef Segments = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FMixtormatMyceliumCS::FHyphalSegment), SegmentCapacity),
					TEXT("Mixtormat.Mycelium.Segments"));
				FRDGBufferRef SegmentCounter = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1),
					TEXT("Mixtormat.Mycelium.Counter"));
				FRDGBufferRef BinHeads = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BinResolution * BinResolution),
					TEXT("Mixtormat.Mycelium.BinHeads"));
				FRDGBufferRef BinEntries = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, SegmentCapacity),
					TEXT("Mixtormat.Mycelium.BinEntries"));

				const auto DispatchStage = [&](int32 Stage, const FIntVector& Groups)
				{
					FMixtormatMyceliumCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatMyceliumCS::FStage>(Stage);
					TShaderMapRef<FMixtormatMyceliumCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatMyceliumCS::FParameters>();
					FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
					P->OutputSize = Size;
					P->Seed = static_cast<uint32>(Settings.Seed);
					P->HeightScale = Settings.HeightScale;
					P->bNormalizeHeight = Settings.bNormalizeHeight ? 1u : 0u;
					P->ColonyCount = Settings.ColonyCount;
					P->GrowthExtent = Settings.GrowthExtent;
					P->Coverage = Settings.Coverage;
					P->NutrientAttraction = Settings.NutrientAttraction;
					P->BranchingFrequency = Settings.BranchingFrequency;
					P->DirectionalPersistence = Settings.DirectionalPersistence;
					P->NetworkConnectivity = Settings.NetworkConnectivity;
					P->SecondaryBranchDensity = Settings.SecondaryBranchDensity;
					P->MicrofilamentDensity = Settings.MicrofilamentDensity;
					P->FilamentWidth = Settings.FilamentWidth;
					P->Taper = Settings.Taper;
					P->BinResolution = BinResolution;
					P->SegmentCapacity = SegmentCapacity;

					P->SubstrateHeight = Substrate;
					P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

					P->Segments = GraphBuilder.CreateUAV(Segments);
					P->SegmentCounter = GraphBuilder.CreateUAV(SegmentCounter, PF_R32_UINT);
					P->BinHeads = GraphBuilder.CreateUAV(BinHeads, PF_R32_UINT);
					P->BinEntries = GraphBuilder.CreateUAV(BinEntries);

					P->OutGrowthHeight = GraphBuilder.CreateUAV(Outputs[0]);
					P->OutGrowthCoverage = GraphBuilder.CreateUAV(Outputs[1]);
					P->OutGrowthAge = GraphBuilder.CreateUAV(Outputs[2]);
					P->OutGrowthIds = GraphBuilder.CreateUAV(Outputs[3]);

					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Mycelium.S%d.L%d.C%d",
						Stage, LayerCtx.LayerIndex, Child.SourceChildIndex), Shader, P, Groups);
				};

				// Stage 0: Clear bins & counter
				DispatchStage(0, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(BinResolution * BinResolution), 64), 1, 1));
				// Stage 1: Growth Solver
				const int32 NumTrunks = FMath::Clamp(Settings.ColonyCount, 1, 16) * 6;
				DispatchStage(1, FIntVector(FMath::DivideAndRoundUp(NumTrunks, 32), 1, 1));
				// Stage 2: Spatial Binning
				DispatchStage(2, FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(SegmentCapacity), 64), 1, 1));
				// Stage 3: Rasterization & Microfilaments
				const FIntVector Pixels(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
				DispatchStage(3, Pixels);

				if (Key)
				{
					auto Entry = MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = Key;
					Entry->Resolution = Size;
					for (int32 Index = 0; Index < SlotCount; ++Index)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Index], &Entry->Outputs[Index]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}
			LayerCtx.GeneratorFields.Add(Child.SourceChildIndex).Append(Outputs, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Height = Outputs[0];
			Bundle->Coverage = Outputs[1];
			Bundle->RegionIds = Outputs[3];
			Bundle->RegisterNamedMask(FName(TEXT("GrowthCoverage")), Outputs[1], FGeneratorBundle::EFieldSemantic::CoverageAlias);
			Bundle->RegisterNamedMask(FName(TEXT("GrowthAge")), Outputs[2], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		}
		return Outputs[0];
	}

	FRDGTextureRef AddScalesPasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer, const FGeneratorPassInput& Child, FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FScalesRenderData& Data = Child.Generator.Scales;
		const FMixtormatScales& S = Data.Settings;
		const FIntPoint Size = Ctx.Request.Resolution;
		constexpr int32 SlotCount = 5;
		FRDGTextureRef O[SlotCount] = {};
		static const TCHAR* Names[SlotCount] = {TEXT("Mixtormat.Scales.Height"), TEXT("Mixtormat.Scales.Coverage"),
			TEXT("Mixtormat.Scales.EdgeDistance"), TEXT("Mixtormat.Scales.Ids"), TEXT("Mixtormat.Scales.UV")};
		if (const auto* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 I = 0; I < SlotCount; ++I) O[I] = (*Memo)[I];
		}
		else
		{
			FMixtormatNodeCache* Cache = Ctx.Request.NodeCache.Get();
			const uint64 Key = Cache && !Child.PreUV && Data.FieldKey
				? MixtormatComposeHash::Combine(Data.FieldKey, 0x5363616C657331ull) : 0;
			auto Hit = Key ? Cache->Find(Key, Size) : TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			bool bComplete = Hit.IsValid();
			for (int32 I = 0; bComplete && I < SlotCount; ++I) bComplete = Hit->Outputs[I].IsValid();
			if (bComplete)
			{
				for (int32 I = 0; I < SlotCount; ++I) O[I] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[I], Names[I]);
			}
			else
			{
				const EPixelFormat Formats[SlotCount] = {PF_R32_FLOAT, PF_R16F, PF_R32_FLOAT, PF_R32_UINT, PF_G32R32F};
				for (int32 I = 0; I < SlotCount; ++I)
				{
					O[I] = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Formats[I],
						FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Names[I]);
				}
				struct FScalePlateStride { float Anchor[2], Size[2], RotationCS[2], HeightOffset; };
				static_assert(sizeof(FScalePlateStride) == 28, "Scale plate stride must match HLSL");
				const uint32 PlateCount = static_cast<uint32>(S.CountX) * static_cast<uint32>(S.CountY);
				FRDGBufferRef Plates = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FScalePlateStride), PlateCount), TEXT("Mixtormat.Scales.Plates"));
				// Rotation-independent root-to-tip bound. Deliberately not a fixed neighbourhood:
				// typed oversize, negative taper, stagger and jitter must not clip distant plates.
				const float HalfWidth = .5f * S.Width / S.CountX * FMath::Max(1.0f, 1.0f - S.Taper);
				const float Length = FMath::Max(S.Length * (1.0f + S.Overlap), .001f) / S.CountY;
				const float Radius = FMath::Sqrt(HalfWidth * HalfWidth + Length * Length) * (1.0f + FMath::Abs(S.SizeRandom));
				const FIntPoint Reach(FMath::CeilToInt(Radius * S.CountX + FMath::Abs(S.Stagger) + FMath::Abs(S.RowJitter)) + 1,
					FMath::CeilToInt(Radius * S.CountY + FMath::Abs(S.RowJitter)) + 1);
				for (int32 Stage = 0; Stage < 2; ++Stage)
				{
					FMixtormatScalesCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatScalesCS::FStage>(Stage);
					TShaderMapRef<FMixtormatScalesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatScalesCS::FParameters>();
					FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
					P->OutputSize = Size; P->ScaleCount = FIntPoint(S.CountX, S.CountY); P->SearchReach = Reach;
					P->Seed = static_cast<uint32>(S.Seed); P->Shape = static_cast<uint32>(S.Shape);
					P->Stagger = S.Stagger; P->Direction = S.Direction; P->Width = S.Width; P->Length = S.Length;
					P->TipRoundness = S.TipRoundness; P->Taper = S.Taper; P->Overlap = S.Overlap;
					P->TipLift = S.TipLift; P->RootTuck = S.RootTuck; P->Thickness = S.Thickness;
					P->Dome = S.Dome; P->Ridge = S.Ridge; P->Bevel = S.Bevel;
					P->SizeRandom = S.SizeRandom; P->RotationRandom = S.RotationRandom;
					P->HeightRandom = S.HeightRandom; P->RowJitter = S.RowJitter;
					P->GrooveAmount = S.GrooveAmount; P->GrooveCount = S.GrooveCount; P->GrooveDirection = S.GrooveDirection;
					P->OutPlates = GraphBuilder.CreateUAV(Plates); P->Plates = GraphBuilder.CreateSRV(Plates);
					P->OutScaleHeight = GraphBuilder.CreateUAV(O[0]); P->OutScaleCoverage = GraphBuilder.CreateUAV(O[1]);
					P->OutScaleEdge = GraphBuilder.CreateUAV(O[2]); P->OutScaleIds = GraphBuilder.CreateUAV(O[3]);
					P->OutScaleUV = GraphBuilder.CreateUAV(O[4]);
					ClearUnusedGraphResources(Shader, P);
					const FIntVector Groups = Stage == 0 ? FIntVector(FMath::DivideAndRoundUp(static_cast<int32>(PlateCount), 64), 1, 1)
						: FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Scales.S%d.L%d.C%d",
						Stage, LayerCtx.LayerIndex, Child.SourceChildIndex), Shader, P, Groups);
				}
				if (Key)
				{
					auto Entry = MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = Key; Entry->Resolution = Size;
					for (int32 I = 0; I < SlotCount; ++I) GraphBuilder.QueueTextureExtraction(O[I], &Entry->Outputs[I]);
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}
			LayerCtx.GeneratorFields.Add(Child.SourceChildIndex).Append(O, SlotCount);
		}
		if (Bundle)
		{
			Bundle->Height = O[0]; Bundle->Coverage = O[1]; Bundle->RegionIds = O[3];
			Bundle->RegisterNamedMask(FName(TEXT("ScaleCoverage")), O[1], FGeneratorBundle::EFieldSemantic::CoverageAlias);
			// Local owner-edge measure only: never advertise it as a global signed boundary.
			Bundle->RegisterNamedMask(FName(TEXT("ScaleEdgeDistance")), O[2], FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		}
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("ScaleUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, O[4], nullptr, nullptr, false});
		return O[0];
	}

namespace
{
	constexpr uint32 RootMaxNodes = 2048;
	constexpr uint32 RootMaxSegments = 2048;
	constexpr uint32 RootMaxAttractors = 4096;
	constexpr uint32 RootBinCount = 64 * 64;
	constexpr uint32 RootMaxBinEntries = 524288;
	constexpr uint32 RootStateSlots = 8;

	struct FRootNodeStride { float Pos[2]; float Dir[2]; uint32 Parent; uint32 BranchId; float Radius; float Arc; uint32 Generation; uint32 ChildCount; };
	struct FRootSegmentStride { float P[6]; float R[2]; float Arc[2]; uint32 BranchId; uint32 Generation; };
	struct FRootAttractorStride { float Pos[2]; uint32 Active; uint32 Nearest; float NearestDist; uint32 Pad[3]; };
	static_assert(sizeof(FRootNodeStride) == 40, "Root node stride must match FRootNode in HLSL");
	static_assert(sizeof(FRootSegmentStride) == 48, "Root segment stride must match FRootSegment in HLSL");
	static_assert(sizeof(FRootAttractorStride) == 32, "Root attractor stride must match FRootAttractor in HLSL");
}

	FRDGTextureRef AddRootNetworkPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const FRootNetworkRenderData& Root = Child.Generator.RootNetwork;
		const FIntPoint Size = Request.Resolution;
		const FIntVector PixelGroups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const uint32 AttractorCount = static_cast<uint32>(FMath::Clamp(Root.Attractors, 16, static_cast<int32>(RootMaxAttractors)));
		const int32 Iterations = FMath::Clamp(Root.Iterations, 1, 256);

		const auto FillParameters = [&Root, &Layer, &Child, &Ctx, Size, AttractorCount](FMixtormatRootNetworkCS::FParameters* P)
		{
			FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
			P->OutputSize = Size;
			P->Seed = Root.Seed;
			P->Trunks = static_cast<uint32>(FMath::Clamp(Root.Trunks, 1, 128));
			P->Attractors = AttractorCount;
			P->StepIndex = 0u;
			P->StepLength = Root.StepLength;
			P->InfluenceRadius = Root.InfluenceRadius;
			P->KillDistance = Root.KillDistance;
			P->BranchSpread = Root.BranchSpread;
			P->DirectionInertia = Root.DirectionInertia;
			P->CurlStrength = Root.CurlStrength;
			P->CurlScale = static_cast<uint32>(FMath::Max(Root.CurlScale, 1));
			P->BaseRadius = Root.BaseRadius;
			P->TipTaper = Root.TipTaper;
			P->JunctionSmooth = Root.JunctionSmooth;
			P->ProfileFlatness = Root.ProfileFlatness;
			P->WeaveAmount = Root.WeaveAmount;
			P->KinkStrength = Root.Kink;
			P->ProfileBias = Root.ProfileBias;
			P->BurialDepth = Root.Burial;
			P->AzimuthAngle = Root.AzimuthAngle;
			P->AzimuthStrength = Root.AzimuthStrength;
			P->SwirlStrength = Root.SwirlStrength;
			P->AxisRatio = Root.AxisRatio;
			P->UseRadiusCurve = Root.bUseRadiusCurve ? 1u : 0u;
			P->RadiusCurveCount = Root.RadiusCurveCount;
			P->RadiusCurveInterpolation = Root.RadiusCurveInterpolation;
			for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
			{
				P->RadiusCurvePoints[Index] = Root.RadiusCurvePoints[Index];
			}
			P->UseBurialCurve = Root.bUseBurialCurve ? 1u : 0u;
			P->BurialCurveCount = Root.BurialCurveCount;
			P->BurialCurveInterpolation = Root.BurialCurveInterpolation;
			for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
			{
				P->BurialCurvePoints[Index] = Root.BurialCurvePoints[Index];
			}
			P->RootUVScaleU = Root.RootUVScaleU;
			P->RootUVScaleV = Root.RootUVScaleV;
			P->RootUVOffsetU = Root.RootUVOffsetU;
			P->RootUVOffsetV = Root.RootUVOffsetV;
		};

		constexpr int32 SlotCount = 7;
		FRDGTextureRef Outputs[SlotCount] = {};
		if (const TArray<FRDGTextureRef, TInlineAllocator<7>>* Memo = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex))
		{
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				Outputs[Slot] = (*Memo)[Slot];
			}
		}
		else
		{
			FMixtormatNodeCache* const NodeCache = Request.NodeCache.Get();
			// RootNet3: invalidate fields built with old sweep or without UV/Across outputs.
			const uint64 NodeKey = NodeCache && !Child.PreUV && Root.FieldKey != 0
				? MixtormatComposeHash::Combine(Root.FieldKey, 0x526F6F744E657437ull)
				: 0;
			const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Hit =
				NodeKey != 0 ? NodeCache->Find(NodeKey, Size)
					: TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
			static const TCHAR* const Names[SlotCount] = {
				TEXT("Mixtormat.RootNetwork.Height"), TEXT("Mixtormat.RootNetwork.Coverage"),
				TEXT("Mixtormat.RootNetwork.EdgeDistance"), TEXT("Mixtormat.RootNetwork.ArcLength"),
				TEXT("Mixtormat.RootNetwork.Ids"), TEXT("Mixtormat.RootNetwork.UV"),
				TEXT("Mixtormat.RootNetwork.Across")};
			bool bComplete = Hit.IsValid();
			for (int32 Slot = 0; bComplete && Slot < SlotCount; ++Slot)
			{
				bComplete = Hit->Outputs[Slot].IsValid();
			}
			if (bComplete)
			{
				for (int32 Slot = 0; Slot < SlotCount; ++Slot)
				{
					Outputs[Slot] = GraphBuilder.RegisterExternalTexture(Hit->Outputs[Slot], Names[Slot]);
				}
			}
			else
			{
				const auto Make = [&GraphBuilder, Size](const EPixelFormat Format, const TCHAR* Name)
				{
					return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
						Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
				};
				Outputs[0] = Make(PF_R32_FLOAT, Names[0]);
				Outputs[1] = Make(PF_R16F, Names[1]);
				Outputs[2] = Make(PF_R32_FLOAT, Names[2]);
				Outputs[3] = Make(PF_R16F, Names[3]);
				Outputs[4] = Make(PF_R32_UINT, Names[4]);
				Outputs[5] = Make(PF_G32R32F, Names[5]);
				Outputs[6] = Make(PF_R16F, Names[6]);

				FRDGBufferRef NodeBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootNodeStride), RootMaxNodes),
					TEXT("Mixtormat.RootNetwork.Nodes"));
				FRDGBufferRef SegmentBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootSegmentStride), RootMaxSegments),
					TEXT("Mixtormat.RootNetwork.Segments"));
				FRDGBufferRef AttractorBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateStructuredDesc(sizeof(FRootAttractorStride), RootMaxAttractors),
					TEXT("Mixtormat.RootNetwork.Attractors"));
				FRDGBufferRef StateBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootStateSlots),
					TEXT("Mixtormat.RootNetwork.State"));
				FRDGBufferRef PullBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(int32), RootMaxNodes * 4),
					TEXT("Mixtormat.RootNetwork.NodePull"));
				FRDGBufferRef BinCounts = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinCounts"));
				FRDGBufferRef BinOffsets = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinOffsets"));
				FRDGBufferRef BinWriteHeads = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootBinCount),
					TEXT("Mixtormat.RootNetwork.BinWriteHeads"));
				FRDGBufferRef BinEntries = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), RootMaxBinEntries),
					TEXT("Mixtormat.RootNetwork.BinEntries"));

				const FRDGBufferUAVRef NodeUAV = GraphBuilder.CreateUAV(NodeBuffer);
				const FRDGBufferUAVRef SegmentUAV = GraphBuilder.CreateUAV(SegmentBuffer);
				const FRDGBufferUAVRef AttractorUAV = GraphBuilder.CreateUAV(AttractorBuffer);
				const FRDGBufferUAVRef StateUAV = GraphBuilder.CreateUAV(StateBuffer, PF_R32_UINT);
				const FRDGBufferUAVRef PullUAV = GraphBuilder.CreateUAV(PullBuffer, PF_R32_SINT);
				const FRDGBufferUAVRef BinCountsUAV = GraphBuilder.CreateUAV(BinCounts, PF_R32_UINT);
				const FRDGBufferUAVRef BinOffsetsUAV = GraphBuilder.CreateUAV(BinOffsets, PF_R32_UINT);
				const FRDGBufferUAVRef BinWriteHeadsUAV = GraphBuilder.CreateUAV(BinWriteHeads, PF_R32_UINT);
				const FRDGBufferUAVRef BinEntriesUAV = GraphBuilder.CreateUAV(BinEntries, PF_R32_UINT);

				const auto AddGrowthPass = [&](const int32 Stage, const uint32 Step, const FIntVector Groups)
				{
					FMixtormatRootNetworkCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatRootNetworkCS::FStage>(Stage);
					TShaderMapRef<FMixtormatRootNetworkCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatRootNetworkCS::FParameters>();
					FillParameters(P);
					P->StepIndex = Step;
					P->RWNodeBuffer = NodeUAV;
					P->RWSegmentBuffer = SegmentUAV;
					P->RWAttractorBuffer = AttractorUAV;
					P->RWStateBuffer = StateUAV;
					P->RWNodePullBuffer = PullUAV;
					P->RWBinCounts = BinCountsUAV;
					P->RWBinOffsets = BinOffsetsUAV;
					P->RWBinWriteHeads = BinWriteHeadsUAV;
					P->RWBinEntries = BinEntriesUAV;
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.RootNetwork.Stage%d.Step%u.L%d.C%d",
							Stage, Step, LayerCtx.LayerIndex, Child.SourceChildIndex),
						Shader, P, Groups);
				};

				const FIntVector Single(1, 1, 1);
				AddGrowthPass(0, 0u, FIntVector(FMath::DivideAndRoundUp(
					static_cast<int32>(FMath::Max(RootMaxNodes, AttractorCount)), 64), 1, 1));
				const FIntVector AttractorGroups(FMath::DivideAndRoundUp(static_cast<int32>(AttractorCount), 64), 1, 1);
				for (int32 Step = 0; Step < Iterations; ++Step)
				{
					AddGrowthPass(1, static_cast<uint32>(Step), AttractorGroups);
					AddGrowthPass(2, static_cast<uint32>(Step), Single);
				}
				AddGrowthPass(3, 0u, Single);
				AddGrowthPass(4, 0u, Single);
				AddGrowthPass(5, 0u, Single);

				{
					FMixtormatRootNetworkCS::FPermutationDomain Permutation;
					Permutation.Set<FMixtormatRootNetworkCS::FStage>(6);
					TShaderMapRef<FMixtormatRootNetworkCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
					auto* P = GraphBuilder.AllocParameters<FMixtormatRootNetworkCS::FParameters>();
					FillParameters(P);
					P->SegmentBuffer = GraphBuilder.CreateSRV(SegmentBuffer);
					P->BinCounts = GraphBuilder.CreateSRV(BinCounts, PF_R32_UINT);
					P->BinOffsets = GraphBuilder.CreateSRV(BinOffsets, PF_R32_UINT);
					P->BinEntries = GraphBuilder.CreateSRV(BinEntries, PF_R32_UINT);
					P->OutRootHeight = GraphBuilder.CreateUAV(Outputs[0]);
					P->OutRootCoverage = GraphBuilder.CreateUAV(Outputs[1]);
					P->OutRootEdgeDistance = GraphBuilder.CreateUAV(Outputs[2]);
					P->OutRootArcLength = GraphBuilder.CreateUAV(Outputs[3]);
					P->OutRootAcross = GraphBuilder.CreateUAV(Outputs[6]);
					P->OutRootUV = GraphBuilder.CreateUAV(Outputs[5]);
					P->OutRootIds = GraphBuilder.CreateUAV(Outputs[4]);
					ClearUnusedGraphResources(Shader, P);
					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.RootNetwork.Field.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
						Shader, P, PixelGroups);
				}

				if (NodeKey != 0)
				{
					TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Entry =
						MakeShared<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>();
					Entry->Key = NodeKey;
					Entry->Resolution = Size;
					for (int32 Slot = 0; Slot < SlotCount; ++Slot)
					{
						GraphBuilder.QueueTextureExtraction(Outputs[Slot], &Entry->Outputs[Slot]);
					}
					Ctx.PendingNodeEntries.Add(Entry);
				}
			}

			TArray<FRDGTextureRef, TInlineAllocator<7>>& Stored = LayerCtx.GeneratorFields.Add(Child.SourceChildIndex);
			Stored.Append(Outputs, SlotCount);
		}

		if (Bundle)
		{
			Bundle->Height = Outputs[0];
			Bundle->Coverage = Outputs[1];
			Bundle->RegionIds = Outputs[4];
			Bundle->BoundaryField = PackScalarBoundary(Ctx, Outputs[2]);
			Bundle->RegisterNamedMask(FName(TEXT("RootCoverage")), Outputs[1],
				FGeneratorBundle::EFieldSemantic::CoverageAlias);
			Bundle->RegisterNamedMask(FName(TEXT("RootEdgeDistance")), Outputs[2],
				FGeneratorBundle::EFieldSemantic::UvDistance, FGeneratorBundle::EFieldUnits::MapUV, 1e9f);
			Bundle->RegisterNamedMask(FName(TEXT("RootArcLength")), Outputs[3],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
			Bundle->RegisterNamedMask(FName(TEXT("RootAcross")), Outputs[6],
				FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
		}
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Outputs[5], nullptr, nullptr, false});
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("RootUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Outputs[5], nullptr, nullptr, false});
		return Outputs[0];
	}
}
