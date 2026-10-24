// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorCommon.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

namespace MixtormatGpuCompositor
{
namespace
{
	constexpr int32 FeatherIterationsHardMax = 512;
	constexpr int32 DistanceSolveMaxSize = 1024;
}

int32 ResolveFeatherIterations(const int32 Requested)
{
	ensureMsgf(Requested <= FeatherIterationsHardMax,
		TEXT("Feather iterations (%d) exceeded hard maximum (%d); clamped. Value likely bypassed the UI."),
		Requested, FeatherIterationsHardMax);
	return FMath::Clamp(Requested, 0, FeatherIterationsHardMax);
}

FIntPoint DistanceSolveResolution(const FIntPoint Resolution)
{
	const int32 Longest = FMath::Max(Resolution.X, Resolution.Y);
	int32 Divisor = 1;
	while (Longest / Divisor > DistanceSolveMaxSize)
	{
		Divisor *= 2;
	}
	return FIntPoint(
		FMath::Max(Resolution.X / Divisor, 1),
		FMath::Max(Resolution.Y / Divisor, 1));
}

namespace FieldRange
{
	constexpr int32 GroupSize = 8;
	constexpr int32 GroupThreads = GroupSize * GroupSize;
	constexpr int32 PixelsPerThread = 4;
	constexpr int32 TileSize = GroupSize * PixelsPerThread;
	// Bound each image dispatch to roughly one megapixel, not a full 4K image. This is a work
	// cap, not a time guarantee; actual timeout headroom still needs profiling on target hardware.
	constexpr int32 MaxPixelsPerDispatch = 1024 * 1024;
	constexpr int32 MaxTileGroupsPerDispatch = MaxPixelsPerDispatch / (TileSize * TileSize);
}

// Measured-range normalization for generator height fields. See MixtormatFieldRange.usf.
class FMixtormatFieldRangeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFieldRangeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFieldRangeCS, FGlobalShader);

	// 0 image tiles, 1 normalize, 2 intermediate range reduction.
	class FStage : SHADER_PERMUTATION_INT("RANGE_STAGE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, GroupOffset)
		SHADER_PARAMETER(uint32, TileGroupCountX)
		SHADER_PARAMETER(uint32, RangeInputCount)
		SHADER_PARAMETER(float, OutLow)
		SHADER_PARAMETER(float, OutHigh)
		SHADER_PARAMETER(uint32, NormalizeMode)
		SHADER_PARAMETER(float, OutputScale)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(uint32, HasHeightGate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HeightGate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceField)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutRange)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Range)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RANGE_GROUP_SIZE"), FieldRange::GroupSize);
		OutEnvironment.SetDefine(TEXT("RANGE_GROUP_THREADS"), FieldRange::GroupThreads);
		OutEnvironment.SetDefine(TEXT("RANGE_PIXELS_PER_THREAD"), FieldRange::PixelsPerThread);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFieldRangeCS,
	"/Plugin/Mixtormat/Private/MixtormatFieldRange.usf",
	"MainCS",
	SF_Compute);

// Every tile owns a pair; successive buffers reduce those pairs until only one remains.
static FRDGBufferRef AddFieldRangePasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Field, const FIntPoint Size)
{
	const FIntPoint TileGroups(FMath::DivideAndRoundUp(Size.X, FieldRange::TileSize),
		FMath::DivideAndRoundUp(Size.Y, FieldRange::TileSize));
	int32 RangeCount = TileGroups.X * TileGroups.Y;
	FRDGBufferRef Ranges = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), RangeCount * 2), TEXT("Mixtormat.FieldRange.Tiles"));
	{
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(0);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		const int32 RowsPerDispatch = FMath::Max(FieldRange::MaxTileGroupsPerDispatch / TileGroups.X, 1);
		for (int32 FirstRow = 0; FirstRow < TileGroups.Y; FirstRow += RowsPerDispatch)
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
			P->OutputSize = Size;
			P->GroupOffset = FIntPoint(0, FirstRow);
			P->TileGroupCountX = TileGroups.X;
			P->SourceField = Field;
			P->OutRange = GraphBuilder.CreateUAV(Ranges);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.FieldRange.Tiles.Y%d", FirstRow),
				Shader, P, FIntVector(TileGroups.X, FMath::Min(RowsPerDispatch, TileGroups.Y - FirstRow), 1));
		}
	}
	for (int32 Level = 0; RangeCount > 1; ++Level)
	{
		const int32 ReducedCount = FMath::DivideAndRoundUp(RangeCount, FieldRange::GroupThreads);
		FRDGBufferRef Reduced = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), ReducedCount * 2), TEXT("Mixtormat.FieldRange.Reduced"));
		FMixtormatFieldRangeCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatFieldRangeCS::FStage>(2);
		TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		for (int32 FirstGroup = 0; FirstGroup < ReducedCount; FirstGroup += FieldRange::MaxTileGroupsPerDispatch)
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
			P->GroupOffset = FIntPoint(FirstGroup, 0);
			P->RangeInputCount = RangeCount;
			P->Range = GraphBuilder.CreateSRV(Ranges);
			P->OutRange = GraphBuilder.CreateUAV(Reduced);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.FieldRange.Reduce.L%d.G%d", Level, FirstGroup), Shader, P,
				FIntVector(FMath::Min(FieldRange::MaxTileGroupsPerDispatch, ReducedCount - FirstGroup), 1, 1));
		}
		Ranges = Reduced;
		RangeCount = ReducedCount;
	}
	return Ranges;
}

static void AddFieldRangeResolvePasses(FRDGBuilder& GraphBuilder,
	const FMixtormatFieldRangeCS::FParameters& Parameters, const TCHAR* Name)
{
	FMixtormatFieldRangeCS::FPermutationDomain Permutation;
	Permutation.Set<FMixtormatFieldRangeCS::FStage>(1);
	TShaderMapRef<FMixtormatFieldRangeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
	const FIntPoint Groups(FMath::DivideAndRoundUp(Parameters.OutputSize.X, FieldRange::GroupSize),
		FMath::DivideAndRoundUp(Parameters.OutputSize.Y, FieldRange::GroupSize));
	const int32 RowsPerDispatch = FMath::Max(
		FieldRange::MaxPixelsPerDispatch / FieldRange::GroupThreads / Groups.X, 1);
	for (int32 FirstRow = 0; FirstRow < Groups.Y; FirstRow += RowsPerDispatch)
	{
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		*P = Parameters;
		P->GroupOffset = FIntPoint(0, FirstRow);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("%s.Y%d", Name, FirstRow), Shader, P,
			FIntVector(Groups.X, FMath::Min(RowsPerDispatch, Groups.Y - FirstRow), 1));
	}
}

FRDGTextureRef AddNormalizeFieldPasses(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Field,
	const FIntPoint Size,
	const float OutLow,
	const float OutHigh,
	const TCHAR* Name)
{
	FRDGBufferRef RangeBuffer = AddFieldRangePasses(GraphBuilder, Field, Size);
	FRDGTextureRef Normalized = GraphBuilder.CreateTexture(Field->Desc, Name);
	{
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->OutLow = OutLow;
		P->OutHigh = OutHigh;
		P->NormalizeMode = 0u;
		P->OutputScale = 1.0f;
		P->HasHeightGate = 0u;
		P->HeightGate = Field;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Normalized);
		AddFieldRangeResolvePasses(GraphBuilder, *P, TEXT("Mixtormat.FieldRange.Normalize"));
	}
	return Normalized;
}

FRDGTextureRef AddSignedGeneratorHeightPasses(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef Field,
	const FIntPoint Size,
	const bool bNormalize,
	const float OutputScale,
	const float HeightBias,
	const bool bCenterNoise,
	FRDGTextureRef HeightGate,
	const TCHAR* Name)
{
	// Normalize off uses the raw signed field, so no min/max dispatch is needed.
	FRDGBufferRef RangeBuffer = nullptr;
	if (bNormalize)
	{
		RangeBuffer = AddFieldRangePasses(GraphBuilder, Field, Size);
	}
	else
	{
		RangeBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2), TEXT("Mixtormat.GeneratorSignedRange"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RangeBuffer), 0u);
	}
	FRDGTextureRef Signed = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	{
		auto* P = GraphBuilder.AllocParameters<FMixtormatFieldRangeCS::FParameters>();
		P->OutputSize = Size;
		P->OutLow = -1.0f;
		P->OutHigh = 1.0f;
		// Mode 3 centres Noise height; mode 1 preserves max-absolute normalization
		// for all other generators. Normalize-off leaves the raw signed height.
		P->NormalizeMode = bNormalize ? (bCenterNoise ? 3u : 1u) : 2u;
		P->OutputScale = FMath::IsFinite(OutputScale) ? OutputScale : 1.0f;
		P->HeightBias = FMath::IsFinite(HeightBias) ? HeightBias : 0.0f;
		P->HasHeightGate = HeightGate ? 1u : 0u;
		P->HeightGate = HeightGate ? HeightGate : Field;
		P->SourceField = Field;
		P->Range = GraphBuilder.CreateSRV(RangeBuffer);
		P->OutField = GraphBuilder.CreateUAV(Signed);
		AddFieldRangeResolvePasses(GraphBuilder, *P, TEXT("Mixtormat.GeneratorSigned.Resolve"));
	}
	return Signed;
}

class FMixtormatGeneratorBundleCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorBundleCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorBundleCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("BUNDLE_STAGE", 13);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, WarpedUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, IdField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VectorField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ModuleHeight)
		SHADER_PARAMETER(float, InvalidDistance)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutScalar)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutVector)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain Permutation(Parameters.PermutationId);
		return Permutation.Get<FStage>() != 8
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorBundleCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorBundle.usf", "MainCS", SF_Compute);

class FMixtormatScalarBoundaryPackCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatScalarBoundaryPackCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatScalarBoundaryPackCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutBoundary)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatScalarBoundaryPackCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowPreview.usf", "PackScalarBoundaryCS", SF_Compute);

void AddGeneratorModuleCombine(
	FMixtormatComposeContext& Ctx,
	FRDGTextureRef RunningHeight,
	const FGeneratorBundle& Module,
	const FChildRenderData& Child,
	FRDGTextureRef& OutHeight)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	OutHeight = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.RunningHeight"));

	FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
	Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(9);
	TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
	auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
	P->OutputSize = Size;
	P->RunningHeight = RunningHeight;
	P->ModuleHeight = Module.Height;
	P->OutScalar = GraphBuilder.CreateUAV(OutHeight);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.Combine"), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
}

FRDGTextureRef RemapBundleField(FMixtormatComposeContext& Ctx, FRDGTextureRef Field,
	FRDGTextureRef WarpedUV, const int32 Stage, FRDGTextureRef Boundary,
	FRDGTextureRef SourceIds, const float InvalidDistance)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Moved = GraphBuilder.CreateTexture(Field->Desc, TEXT("Mixtormat.Generator.MovedField"));
	FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
	Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(Stage);
	TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
	auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
	P->OutputSize = Size;
	P->WarpedUV = WarpedUV;
	P->IdField = SourceIds;
	P->InvalidDistance = InvalidDistance;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	if (Stage == 1)
	{
		P->IdField = Field;
		P->OutIds = GraphBuilder.CreateUAV(Moved);
	}
	else if (Stage == 2 || Stage == 3 || Stage == 4 || Stage == 5)
	{
		P->VectorField = Field;
		P->ScalarField = Field;
		P->BoundaryField = Field;
		P->OutVector = GraphBuilder.CreateUAV(Moved);
	}
	else
	{
		P->ScalarField = Field;
		P->BoundaryField = Boundary;
		P->OutScalar = GraphBuilder.CreateUAV(Moved);
	}
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.Remap%d", Stage),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Moved;
}

void RemapGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle, FRDGTextureRef WarpedUV)
{
	using ESemantic = FGeneratorBundle::EFieldSemantic;
	using EUnits = FGeneratorBundle::EFieldUnits;
	const FGeneratorBundle Source = Bundle;
	const FIntPoint Size = Ctx.Request.Resolution;
	const auto HasMapExtent = [Size](FRDGTextureRef Texture)
	{
		return !Texture || Texture->Desc.Extent == Size;
	};
	const auto HasScalarFormat = [](FRDGTextureRef Texture)
	{
		return Texture && (Texture->Desc.Format == PF_R16F || Texture->Desc.Format == PF_R32_FLOAT);
	};
	if (!ensureMsgf(WarpedUV && Size.X > 0 && Size.Y > 0 && HasMapExtent(WarpedUV)
		&& WarpedUV->Desc.Format == PF_G32R32F
		&& HasMapExtent(Source.RegionIds) && HasMapExtent(Source.BoundaryField)
		&& HasMapExtent(Source.CentreUV) && HasMapExtent(Source.Orientation)
		&& (!Source.RegionIds || Source.RegionIds->Desc.Format == PF_R32_UINT)
		&& (!Source.BoundaryField || Source.BoundaryField->Desc.Format == PF_G32R32F)
		&& (!Source.CentreUV || (Source.CentreUV->Desc.Format == PF_G32R32F
			&& Source.CentreDescriptor.Semantic == ESemantic::LegacyLocalCentre
			&& Source.CentreDescriptor.Units == EUnits::MapUV))
		&& (!Source.Orientation || (HasScalarFormat(Source.Orientation)
			&& Source.OrientationDescriptor.Semantic == ESemantic::LegacyLocalOrientation
			&& Source.OrientationDescriptor.Units == EUnits::Radians)),
		TEXT("Unsupported generator companion extent, format or fixed-slot policy")))
	{
		return;
	}
	for (const TPair<FName, FRDGTextureRef>& Mask : Source.NamedMasks)
	{
		const FGeneratorBundle::FFieldDescriptor* Descriptor = Source.NamedMaskDescriptors.Find(Mask.Key);
		bool bSupported = Descriptor && Mask.Value && HasMapExtent(Mask.Value);
		if (bSupported)
		{
			const bool bCoordinate = Descriptor->Semantic == ESemantic::LiftedCoordinateMap;
			bSupported = bCoordinate ? Mask.Value->Desc.Format == PF_G32R32F
				: HasScalarFormat(Mask.Value);
		}
		if (bSupported)
		{
			switch (Descriptor->Semantic)
			{
			case ESemantic::ContinuousAttribute:
				bSupported = Descriptor->Units == EUnits::Unitless || Descriptor->Units == EUnits::CrackCell;
				break;
			case ESemantic::RegionAttribute:
				bSupported = Source.RegionIds && Descriptor->Units == EUnits::Unitless;
				break;
			case ESemantic::UvDistance:
				bSupported = Source.BoundaryField && Descriptor->Units == EUnits::MapUV
					&& FMath::IsFinite(Descriptor->InvalidDistance);
				break;
			case ESemantic::CoverageAlias:
				bSupported = HasMapExtent(Source.Coverage) && HasScalarFormat(Source.Coverage)
					&& Descriptor->Units == EUnits::Unitless;
				break;
			case ESemantic::LiftedCoordinateMap:
				bSupported = Descriptor->Units == EUnits::MapUV;
				break;
			default:
				bSupported = false;
				break;
			}
		}
		if (!ensureMsgf(bSupported, TEXT("Unsupported generator companion policy: %s"), *Mask.Key.ToString()))
		{
			return;
		}
	}

	FGeneratorBundle Moved = Source;
	if (Source.RegionIds) { Moved.RegionIds = RemapBundleField(Ctx, Source.RegionIds, WarpedUV, 1); }
	if (Source.CentreUV) { Moved.CentreUV = RemapBundleField(Ctx, Source.CentreUV, WarpedUV, 5); }
	if (Source.Orientation) { Moved.Orientation = RemapBundleField(Ctx, Source.Orientation, WarpedUV, 6); }
	for (const TPair<FName, FRDGTextureRef>& Mask : Source.NamedMasks)
	{
		const FGeneratorBundle::FFieldDescriptor& Descriptor = Source.NamedMaskDescriptors.FindChecked(Mask.Key);
		int32 Stage = INDEX_NONE;
		switch (Descriptor.Semantic)
		{
		case ESemantic::ContinuousAttribute: Stage = 0; break;
		case ESemantic::RegionAttribute: Stage = 10; break;
		case ESemantic::UvDistance: Stage = 7; break;
		case ESemantic::LiftedCoordinateMap: Stage = 3; break;
		case ESemantic::CoverageAlias:
			Moved.NamedMasks[Mask.Key] = Source.Coverage;
			continue;
		default: checkNoEntry(); return;
		}
		Moved.NamedMasks[Mask.Key] = RemapBundleField(Ctx, Mask.Value, WarpedUV, Stage,
			Source.BoundaryField, Source.RegionIds, Descriptor.InvalidDistance);
	}
	if (Source.BoundaryField)
	{
		Moved.BoundaryField = RemapBundleField(Ctx, Source.BoundaryField, WarpedUV, 2);
	}
	Bundle = MoveTemp(Moved);
}

void RemapCompletedGeneratorBundle(FMixtormatComposeContext& Ctx, FGeneratorBundle& Bundle,
	FRDGTextureRef CompletedSignedHeight, FRDGTextureRef WarpedUV, const bool bFilterHeight)
{
	const FGeneratorBundle Source = Bundle;
	if (!CompletedSignedHeight) { return; }
	Bundle.Height = bFilterHeight
		? RemapBundleField(Ctx, CompletedSignedHeight, WarpedUV, 12)
		: RemapBundleField(Ctx, CompletedSignedHeight, WarpedUV, 0);
	if (Source.Coverage)
	{
		Bundle.Coverage = RemapBundleField(Ctx, Source.Coverage, WarpedUV, 0);
	}
	RemapGeneratorBundle(Ctx, Bundle, WarpedUV);
}

void RemapGeneratorModuleOutputs(FMixtormatComposeContext& Ctx,
	const FLayerRenderData& Layer, const FChildRenderData& Owner,
	FGeneratorBundle& Module, FRDGTextureRef Coordinates, const bool bFilterHeight)
{
	const FPublishedFieldKey ValueKey =
		PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("Value")));
	const FPublishedFieldKey GradientKey =
		PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("Gradient")));
	const FPublishedField* NoiseValue = Owner.Generator.Type == EMixtormatGeneratorType::Noise
		? Ctx.PublishedFieldOutputs.Find(ValueKey) : nullptr;
	const FPublishedField* NoiseGradient = Owner.Generator.Type == EMixtormatGeneratorType::Noise
		? Ctx.PublishedFieldOutputs.Find(GradientKey) : nullptr;
	const FPublishedField ValueSnapshot = NoiseValue ? *NoiseValue : FPublishedField{};
	const FPublishedField GradientSnapshot = NoiseGradient ? *NoiseGradient : FPublishedField{};
	RemapCompletedGeneratorBundle(Ctx, Module, Module.Height, Coordinates, bFilterHeight);
	if (ValueSnapshot.IsComplete())
	{
		Ctx.PublishedFieldOutputs.Add(ValueKey, FPublishedField{
			ValueSnapshot.Kind, RemapBundleField(Ctx, ValueSnapshot.Texture, Coordinates, 0),
			nullptr, nullptr, false});
	}
	if (Module.GradientDescriptor.Semantic == FGeneratorBundle::EFieldSemantic::SourceFrameVector
		&& Module.GradientDescriptor.Units == FGeneratorBundle::EFieldUnits::GeneratorDomain
		&& GradientSnapshot.Texture
		&& GradientSnapshot.Texture->Desc.Extent == Ctx.Request.Resolution
		&& GradientSnapshot.Texture->Desc.Format == PF_G32R32F)
	{
		Ctx.PublishedFieldOutputs.Add(GradientKey, FPublishedField{
			GradientSnapshot.Kind, RemapBundleField(Ctx, GradientSnapshot.Texture, Coordinates, 4),
			nullptr, nullptr, false});
	}
}

FRDGTextureRef PackScalarBoundary(FMixtormatComposeContext& Ctx, FRDGTextureRef Scalar)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Packed = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.PackedBoundary"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatScalarBoundaryPackCS::FParameters>();
	P->OutputSize = Size;
	P->ScalarBoundary = Scalar;
	P->OutBoundary = Ctx.GraphBuilder.CreateUAV(Packed);
	TShaderMapRef<FMixtormatScalarBoundaryPackCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.PackBoundary"),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
			FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Packed;
}

FRDGTextureRef ResolveSubstrateHeight(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	FRDGTextureRef RunningHeight, FRDGTextureRef BelowHeight)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;

	// RunningHeight starts at zero and contains generator deltas only. The layer's
	// texture height is added at final assembly, so it must also enter substrate analysis.
	const auto CombineHeight = [&](FRDGTextureRef Base, FRDGTextureRef Delta) -> FRDGTextureRef
	{
		if (!Base) { return Delta; }
		if (!Delta) { return Base; }
		FRDGTextureRef Combined = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Substrate.Combined"));
		FMixtormatGeneratorBundleCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatGeneratorBundleCS::FStage>(9);
		TShaderMapRef<FMixtormatGeneratorBundleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorBundleCS::FParameters>();
		P->OutputSize = Size;
		P->RunningHeight = Base;
		P->ModuleHeight = Delta;
		P->OutScalar = GraphBuilder.CreateUAV(Combined);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.SubstrateCombine"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Combined;
	};
	FRDGTextureRef Substrate = CombineHeight(BelowHeight, LayerCtx.LayerInputHeight);
	Substrate = CombineHeight(Substrate, RunningHeight);
	return Substrate ? Substrate : Ctx.OutputHeight[0];
}

}
