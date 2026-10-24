// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorCommon.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

namespace MixtormatGpuCompositor
{

class FMixtormatBooleanStackCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBooleanStackCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBooleanStackCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Operation)
		SHADER_PARAMETER(int32, EdgeStyle)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(FVector2f, TargetOffset)
		SHADER_PARAMETER(FVector2f, SourceOffset)
		SHADER_PARAMETER(float, TargetHeightOffset)
		SHADER_PARAMETER(float, SourceHeightOffset)
		SHADER_PARAMETER(float, SourceScale)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, JoinWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TargetHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArea)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBoundary)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDifference)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
	{
		return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBooleanStackCS,
	"/Plugin/Mixtormat/Private/MixtormatBooleanStack.usf", "MainCS", SF_Compute);

class FMixtormatBooleanCarveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBooleanCarveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBooleanCarveCS, FGlobalShader);
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
		SHADER_PARAMETER(int32, Seed)
		SHADER_PARAMETER(int32, Count)
		SHADER_PARAMETER(int32, Operation)
		SHADER_PARAMETER(int32, Shape)
		SHADER_PARAMETER(int32, Sides)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, RadiusVariation)
		SHADER_PARAMETER(float, CenterDepth)
		SHADER_PARAMETER(float, DepthVariation)
		SHADER_PARAMETER(float, ReferenceHeight)
		SHADER_PARAMETER(float, DepthScale)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, Rotation)
		SHADER_PARAMETER(float, RotationVariation)
		SHADER_PARAMETER(float, ChamferFraction)
		SHADER_PARAMETER(float, ChamferVariation)
		SHADER_PARAMETER(float, HeightToUV)
		SHADER_PARAMETER(uint32, HasScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BelowHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightDelta)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCutDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryFront)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryBack)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGeometryMask)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
	{
		return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBooleanCarveCS,
	"/Plugin/Mixtormat/Private/MixtormatBooleanCarve.usf", "MainCS", SF_Compute);

void AddBooleanStackPasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Child, FRDGTextureRef TargetHeight,
	FRDGTextureRef SourceHeight, FGeneratorBundle* Bundle)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	const FBooleanStackRenderData& B = Child.Boolean;
	const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
	const auto Make = [&GraphBuilder, Size](const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	FRDGTextureRef Height = Make(TEXT("Mixtormat.Boolean.StackHeight"));
	FRDGTextureRef Area = Make(TEXT("Mixtormat.Boolean.AffectedArea"));
	FRDGTextureRef Boundary = Make(TEXT("Mixtormat.Boolean.Boundary"));
	FRDGTextureRef Difference = Make(TEXT("Mixtormat.Boolean.Difference"));
	auto* P = GraphBuilder.AllocParameters<FMixtormatBooleanStackCS::FParameters>();
	P->OutputSize = Size;
	P->Operation = B.Operation;
	P->EdgeStyle = B.EdgeStyle;
	P->HasMask = bHasMask ? 1u : 0u;
	P->TargetOffset = B.TargetOffset;
	P->SourceOffset = B.SourceOffset;
	P->TargetHeightOffset = B.TargetHeightOffset;
	P->SourceHeightOffset = B.SourceHeightOffset;
	P->SourceScale = B.SourceScale;
	P->Amount = B.Amount;
	P->JoinWidth = B.JoinWidth;
	P->TargetHeight = TargetHeight;
	P->SourceHeight = SourceHeight;
	P->ScopedMask = bHasMask
		? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true) : TargetHeight;
	P->OutHeight = GraphBuilder.CreateUAV(Height);
	P->OutArea = GraphBuilder.CreateUAV(Area);
	P->OutBoundary = GraphBuilder.CreateUAV(Boundary);
	P->OutDifference = GraphBuilder.CreateUAV(Difference);
	TShaderMapRef<FMixtormatBooleanStackCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.Boolean.Stack.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	Bundle->Height = Height;
	Bundle->RegisterNamedMask(FName(TEXT("FieldMask")), Area,
		FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
	Bundle->RegisterNamedMask(FName(TEXT("FieldBoundary")), Boundary,
		FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
	const FName DifferenceName(TEXT("FieldDifference"));
	Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, DifferenceName),
		FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Difference, nullptr, nullptr, false});
	if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
		DifferenceName, LayerCtx.LayerIndex, Child.SourceChildIndex))
	{
		AddDebugPreviewSignedHeightBlitPass(GraphBuilder, Difference,
			Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex], Size);
	}
}

void AddBooleanCarvePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FGeneratorPassInput& Child, FRDGTextureRef RunningHeight,
	FRDGTextureRef BelowHeight, FGeneratorBundle* Bundle)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	const FBooleanOpsRenderData& B = Child.Generator.BooleanOps;
	const auto Make = [&GraphBuilder, Size](const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	FRDGTextureRef Delta = Make(TEXT("Mixtormat.BooleanCarve.Delta"));
	FRDGTextureRef CutDepth = Make(TEXT("Mixtormat.BooleanCarve.CutDepth"));
	FRDGTextureRef Front = Make(TEXT("Mixtormat.BooleanCarve.Front"));
	FRDGTextureRef Back = Make(TEXT("Mixtormat.BooleanCarve.Back"));
	FRDGTextureRef Footprint = Make(TEXT("Mixtormat.BooleanCarve.GeometryMask"));
	const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
	FRDGTextureRef Scoped = bHasMask
		? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
		: RunningHeight;
	auto* P = GraphBuilder.AllocParameters<FMixtormatBooleanCarveCS::FParameters>();
	FillGeneratorPlacement(P, Layer, Child.PreUV, Ctx.EmptyPatternUV);
	P->OutputSize = Size;
	P->Seed = B.Seed;
	P->Count = B.Count;
	P->Operation = B.Operation;
	P->Shape = B.Shape;
	P->Sides = B.Sides;
	P->Radius = B.Radius;
	P->RadiusVariation = B.RadiusVariation;
	P->CenterDepth = B.CenterDepth;
	P->DepthVariation = B.DepthVariation;
	P->ReferenceHeight = B.ReferenceHeight;
	P->DepthScale = B.DepthScale;
	P->Irregularity = B.Irregularity;
	P->Rotation = B.Rotation;
	P->RotationVariation = B.RotationVariation;
	P->ChamferFraction = B.ChamferFraction;
	P->ChamferVariation = B.ChamferVariation;
	P->HeightToUV = B.HeightToUV;
	P->HasScopedMask = bHasMask ? 1u : 0u;
	P->RunningHeight = RunningHeight;
	P->BelowHeight = BelowHeight;
	P->ScopedMask = Scoped;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	P->OutHeightDelta = GraphBuilder.CreateUAV(Delta);
	P->OutCutDepth = GraphBuilder.CreateUAV(CutDepth);
	P->OutGeometryFront = GraphBuilder.CreateUAV(Front);
	P->OutGeometryBack = GraphBuilder.CreateUAV(Back);
	P->OutGeometryMask = GraphBuilder.CreateUAV(Footprint);
	TShaderMapRef<FMixtormatBooleanCarveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.BooleanCarve.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex),
		Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));

	Bundle->Height = Delta;
	Bundle->RegisterNamedMask(FName(TEXT("GeometryMask")), Footprint,
		FGeneratorBundle::EFieldSemantic::ContinuousAttribute);
	for (const TPair<FName, FRDGTextureRef>& Output : {
		TPair<FName, FRDGTextureRef>{FName(TEXT("CutDepth")), CutDepth},
		TPair<FName, FRDGTextureRef>{FName(TEXT("GeometryFront")), Front},
		TPair<FName, FRDGTextureRef>{FName(TEXT("GeometryBack")), Back}})
	{
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Output.Key),
			FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Output.Value, nullptr, nullptr, false});
		if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
			Output.Key, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
			AddDebugPreviewSignedHeightBlitPass(GraphBuilder, Output.Value, Debug, Size);
		}
	}
}

}
