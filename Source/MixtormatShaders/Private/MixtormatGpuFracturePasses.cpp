// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

namespace MixtormatGpuCompositor
{
class FMixtormatFractureTopologyCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFractureTopologyCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFractureTopologyCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, FractureSource)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER(uint32, FractureSeed)
		SHADER_PARAMETER(float, FractureScale)
		SHADER_PARAMETER(float, FractureAmount)
		SHADER_PARAMETER(float, FractureWidthPixels)
		SHADER_PARAMETER(float, FractureDepth)
		SHADER_PARAMETER(float, FractureVariation)
		SHADER_PARAMETER(float, LayerTiling)
		SHADER_PARAMETER(FIntPoint, LayerUVScale)
		SHADER_PARAMETER(FVector2f, LayerUVOffset)
		SHADER_PARAMETER(uint32, LayerFlipU)
		SHADER_PARAMETER(uint32, LayerFlipV)
		SHADER_PARAMETER(int32, LayerRotation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, OutPieceIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutSourcePosition)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatFractureTopologyCS,
	"/Plugin/Mixtormat/Private/MixtormatFracture.usf", "MainCS", SF_Compute);

class FMixtormatFractureSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFractureSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFractureSeedCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, FractureWidthPixels)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, PieceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourcePosition)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBoundary)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatFractureSeedCS,
	"/Plugin/Mixtormat/Private/MixtormatFractureDistance.usf", "SeedCS", SF_Compute);

class FMixtormatFractureStepCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFractureStepCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFractureStepCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, PieceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatFractureStepCS,
	"/Plugin/Mixtormat/Private/MixtormatFractureDistance.usf", "StepCS", SF_Compute);

class FMixtormatFractureFieldCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFractureFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFractureFieldCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, FractureWidthPixels)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, PieceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BoundaryRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutField)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatFractureFieldCS,
	"/Plugin/Mixtormat/Private/MixtormatFractureField.usf", "MainCS", SF_Compute);

class FMixtormatFractureResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFractureResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFractureResolveCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, FractureSeed)
		SHADER_PARAMETER(float, FractureScale)
		SHADER_PARAMETER(float, FractureAmount)
		SHADER_PARAMETER(float, FractureWidthPixels)
		SHADER_PARAMETER(float, FractureDepth)
		SHADER_PARAMETER(float, FractureProfile)
		SHADER_PARAMETER(float, FractureChamfer)
		SHADER_PARAMETER(float, FractureVariation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, PieceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourcePosition)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BoundaryRecord)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BoundarySurface)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ShapeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFractureMask)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatFractureResolveCS,
	"/Plugin/Mixtormat/Private/MixtormatFractureResolve.usf", "MainCS", SF_Compute);

namespace
{
	FRDGTextureRef AddFracturePass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef SourceHeight)
	{
		const FFractureRenderData& Fracture = Child.Generator.Fracture;
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FIntPoint Size = Ctx.Request.Resolution;
		const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1);
		const auto MakeTexture = [&](EPixelFormat Format, const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
				Size, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
		};
		FRDGTextureRef OutputMask = MakeTexture(PF_R16F, TEXT("Mixtormat.Fracture.Mask"));
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Fracture"))}, OutputMask);
		FRDGTextureRef RegionIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		if (Fracture.Amount <= 0.0f || Fracture.Depth <= 0.0f
			|| (!RegionIds && Fracture.Source == EMixtormatFractureSource::RegionIds))
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputMask), FVector4f(0.0f));
			return SourceHeight;
		}

		const float WidthPixels = FMath::Lerp(1.5f, 64.0f, Fracture.Width)
			* static_cast<float>(FMath::Min(Size.X, Size.Y)) / 1024.0f;
		FRDGTextureRef PieceIds = MakeTexture(PF_R32G32_UINT, TEXT("Mixtormat.Fracture.Pieces"));
		FRDGTextureRef SourcePosition = MakeTexture(PF_G32R32F, TEXT("Mixtormat.Fracture.SourcePosition"));
		FRDGTextureRef Boundary = MakeTexture(PF_A32B32G32R32F, TEXT("Mixtormat.Fracture.Boundary"));
		FRDGTextureRef Record[2] = {
			MakeTexture(PF_A32B32G32R32F, TEXT("Mixtormat.Fracture.DistanceA")),
			MakeTexture(PF_A32B32G32R32F, TEXT("Mixtormat.Fracture.DistanceB"))};
		FRDGTextureRef OutputHeight = GraphBuilder.CreateTexture(SourceHeight->Desc, TEXT("Mixtormat.Fracture.Height"));
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFractureTopologyCS::FParameters>();
			P->OutputSize = Size;
			P->FractureSource = static_cast<uint32>(Fracture.Source);
			P->HasRegionIds = RegionIds ? 1u : 0u;
			P->FractureSeed = Fracture.Seed;
			P->FractureScale = Fracture.Scale;
			P->FractureAmount = Fracture.Amount;
			P->FractureWidthPixels = WidthPixels;
			P->FractureDepth = Fracture.Depth;
			P->FractureVariation = Fracture.Variation;
			P->LayerTiling = Layer.Tiling;
			P->LayerUVScale = FIntPoint(Layer.UVScaleX, Layer.UVScaleY);
			P->LayerUVOffset = Layer.UVOffset;
			P->LayerFlipU = Layer.bFlipU ? 1u : 0u;
			P->LayerFlipV = Layer.bFlipV ? 1u : 0u;
			P->LayerRotation = Layer.Rotation;
			P->RegionIds = RegionIds ? RegionIds : Ctx.EmptyRegionIds;
			P->SourceHeight = SourceHeight;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutPieceIds = GraphBuilder.CreateUAV(PieceIds);
			P->OutSourcePosition = GraphBuilder.CreateUAV(SourcePosition);
			TShaderMapRef<FMixtormatFractureTopologyCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Fracture.Footprints"), Shader, P, Groups);
		}
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFractureSeedCS::FParameters>();
			P->OutputSize = Size;
			P->FractureWidthPixels = WidthPixels;
			P->PieceIds = PieceIds;
			P->SourcePosition = SourcePosition;
			P->SourceHeight = SourceHeight;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
			P->OutputBoundary = GraphBuilder.CreateUAV(Boundary);
			TShaderMapRef<FMixtormatFractureSeedCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Fracture.Boundaries"), Shader, P, Groups);
		}
		int32 ReadIndex = 0;
		const auto AddDistanceStep = [&](int32 StepSize)
		{
			const int32 WriteIndex = 1 - ReadIndex;
			auto* P = GraphBuilder.AllocParameters<FMixtormatFractureStepCS::FParameters>();
			P->OutputSize = Size;
			P->StepSize = StepSize;
			P->PieceIds = PieceIds;
			P->PreviousRecord = Record[ReadIndex];
			P->OutputRecord = GraphBuilder.CreateUAV(Record[WriteIndex]);
			TShaderMapRef<FMixtormatFractureStepCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Fracture.Redistance%d", StepSize), Shader, P, Groups);
			ReadIndex = WriteIndex;
		};
		const int32 FirstStep = FMath::Max(1, static_cast<int32>(
			FMath::RoundUpToPowerOfTwo(static_cast<uint32>(FMath::Max(Size.X, Size.Y)))) / 2);
		for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
		{
			AddDistanceStep(StepSize);
		}
		AddDistanceStep(1);
		FRDGTextureRef ShapeField = MakeTexture(PF_R32_FLOAT, TEXT("Mixtormat.Fracture.ShapeField"));
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFractureFieldCS::FParameters>();
			P->OutputSize = Size;
			P->FractureWidthPixels = WidthPixels;
			P->PieceIds = PieceIds;
			P->BoundaryRecord = Record[ReadIndex];
			P->OutField = GraphBuilder.CreateUAV(ShapeField);
			TShaderMapRef<FMixtormatFractureFieldCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Fracture.ShapeField"), Shader, P, Groups);
		}
		{
			auto* P = GraphBuilder.AllocParameters<FMixtormatFractureResolveCS::FParameters>();
			P->OutputSize = Size;
			P->FractureSeed = Fracture.Seed;
			P->FractureScale = Fracture.Scale;
			P->FractureAmount = Fracture.Amount;
			P->FractureWidthPixels = WidthPixels;
			P->FractureDepth = Fracture.Depth;
			P->FractureProfile = Fracture.Profile;
			P->FractureChamfer = Fracture.Chamfer;
			P->FractureVariation = Fracture.Variation;
			P->PieceIds = PieceIds;
			P->SourcePosition = SourcePosition;
			P->BoundaryRecord = Record[ReadIndex];
			P->BoundarySurface = Boundary;
			P->ShapeField = ShapeField;
			P->SourceHeight = SourceHeight;
			P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutHeight = GraphBuilder.CreateUAV(OutputHeight);
			P->OutFractureMask = GraphBuilder.CreateUAV(OutputMask);
			TShaderMapRef<FMixtormatFractureResolveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Fracture.FaceIntersection"), Shader, P, Groups);
		}
		return OutputHeight;
	}
}

void AddFracturePasses(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer)
{
	const int32 Index = LayerCtx.LayerIndex & 1;
	FRDGTextureRef SourceHeight = Ctx.OutputHeight[Index];
	FRDGTextureRef Height = SourceHeight;
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type == EMixtormatLayerChildType::Generator
			&& Child.Generator.Type == EMixtormatGeneratorType::Fracture)
		{
			Height = AddFracturePass(Ctx, LayerCtx, Layer, Child, Height);
		}
	}
	if (Height == SourceHeight)
	{
		return;
	}
	FRDGTextureRef Normal = Ctx.GraphBuilder.CreateTexture(Ctx.OutputN[Index]->Desc, TEXT("Mixtormat.Fracture.Normal"));
	AddHeightDerivedNormalPass(Ctx, SourceHeight, Height, Ctx.OutputN[Index], Ctx.OutputRAM[Index],
		Normal, nullptr, Ctx.Request.Resolution, HeightDerivedNormalStrength, 0.0f, false, TEXT("Fracture"));
	AddCopyTexturePass(Ctx.GraphBuilder, Height, Ctx.OutputHeight[Index]);
	AddCopyTexturePass(Ctx.GraphBuilder, Normal, Ctx.OutputN[Index]);
}
}
