// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorSublayerPasses.h"
#include "MixtormatGpuGeneratorCommon.h"
#include "MixtormatGpuGaussianBlur.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

namespace MixtormatGpuCompositor
{

class FMixtormatGeneratorHeightBlendCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightBlendCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightBlendCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Op)
		SHADER_PARAMETER(uint32, HasSource)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Scale)
		SHADER_PARAMETER(float, Softness)
		SHADER_PARAMETER(float, Threshold)
		SHADER_PARAMETER(float, EdgeSoftness)
		SHADER_PARAMETER(float, BaseBias)
		SHADER_PARAMETER(float, BlendBias)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightBlendCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightBlend.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorHeightCurveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightCurveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightCurveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, InputMin)
		SHADER_PARAMETER(float, InputMax)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(uint32, bInvert)
		SHADER_PARAMETER(uint32, CurveCount)
		SHADER_PARAMETER(uint32, CurveInterpolation)
		SHADER_PARAMETER_ARRAY(FVector4f, CurvePoints, [FMixtormatScalarRamp::MaxPoints])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RunningHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightCurveCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightCurve.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorHeightColorRampCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorHeightColorRampCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorHeightColorRampCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, StopCount)
		SHADER_PARAMETER(uint32, Interpolation)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, InputScale)
		SHADER_PARAMETER(float, InputOffset)
		SHADER_PARAMETER(uint32, MirrorRepeat)
		SHADER_PARAMETER_ARRAY(FVector4f, Positions, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER_ARRAY(FVector4f, Colors, [FMixtormatColorRamp::MaxStops])
		SHADER_PARAMETER(uint32, MapOnly)
		SHADER_PARAMETER(uint32, HasGate)
		SHADER_PARAMETER(uint32, HasPreviousColor)
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER(uint32, SourceMode)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, FlowModulation)
		SHADER_PARAMETER(uint32, SignedFlowModulation)
		SHADER_PARAMETER(FVector2f, FlowModulationDir)
		SHADER_PARAMETER(float, UVModulation)
		SHADER_PARAMETER(FVector2f, UVModulationDir)
		SHADER_PARAMETER(float, IdVariation)
		SHADER_PARAMETER(uint32, IdVariationSeed)
		SHADER_PARAMETER(uint32, HasModFlow)
		SHADER_PARAMETER(uint32, HasModUV)
		SHADER_PARAMETER(uint32, HasModIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourceUV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorHeightColorRampCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightColorRamp.usf", "MainCS", SF_Compute);

class FMixtormatGeneratorColorCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratorColorCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratorColorCompositeCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(uint32, HasGate)
		SHADER_PARAMETER(uint32, HasPreviousColor)
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MappedColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGeneratorColorCompositeCS,
	"/Plugin/Mixtormat/Private/MixtormatGeneratorHeightColorRamp.usf", "BlendCS", SF_Compute);

FRDGTextureRef AddGeneratorHeightBlendPass(FMixtormatComposeContext& Ctx, FRDGTextureRef RunningHeight,
	FRDGTextureRef SourceHeight, const FGeneratorHeightBlendRenderData& Blend)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.HeightBlend"));
	TShaderMapRef<FMixtormatGeneratorHeightBlendCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightBlendCS::FParameters>();
	P->OutputSize = Size;
	P->Op = static_cast<uint32>(Blend.Op);
	P->HasSource = Blend.SourceChildIndex != INDEX_NONE ? 1u : 0u;
	P->Amount = Blend.Amount;
	P->Scale = Blend.Scale;
	P->Softness = Blend.Softness;
	P->Threshold = Blend.Threshold;
	P->EdgeSoftness = Blend.EdgeSoftness;
	P->BaseBias = Blend.BaseBias;
	P->BlendBias = Blend.BlendBias;
	P->RunningHeight = RunningHeight;
	P->SourceHeight = SourceHeight;
	P->OutHeight = GraphBuilder.CreateUAV(Out);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightBlend"), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Out;
}

FRDGTextureRef AddGeneratorHeightCurvePass(FMixtormatComposeContext& Ctx, FRDGTextureRef RunningHeight,
	const FGeneratorHeightCurveRenderData& Curve)
{
	if (Curve.Amount == 0.0f)
	{
		return RunningHeight;
	}
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.HeightCurve"));
	TShaderMapRef<FMixtormatGeneratorHeightCurveCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightCurveCS::FParameters>();
	P->OutputSize = Size;
	P->Amount = Curve.Amount;
	P->InputMin = Curve.InputMin;
	P->InputMax = Curve.InputMax;
	P->Offset = Curve.Offset;
	P->bInvert = Curve.bInvert;
	P->CurveCount = Curve.CurveCount;
	P->CurveInterpolation = Curve.CurveInterpolation;
	for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
	{
		P->CurvePoints[Index] = Curve.CurvePoints[Index];
	}
	P->RunningHeight = RunningHeight;
	P->SourceHeight = Curve.bNormalizeInput != 0
		? AddSignedGeneratorHeightPasses(GraphBuilder, RunningHeight, Size, true,
			1.0f, 0.0f, false, nullptr, TEXT("Mixtormat.Generator.HeightCurve.Normalized"))
		: RunningHeight;
	P->OutHeight = GraphBuilder.CreateUAV(Out);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightCurve"), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Out;
}

FRDGTextureRef AddGeneratorHeightColorRampPass(FMixtormatComposeContext& Ctx, FRDGTextureRef SourceHeight,
	FRDGTextureRef SourceFlow, FRDGTextureRef SourceUV, FRDGTextureRef SourceIds,
	const uint32 SourceMode, FRDGTextureRef Gate, FRDGTextureRef PreviousColor,
	const FGeneratorHeightColorRampRenderData& Ramp,
	const bool bOverrideBaseColor, const FVector4f& FillColor, const bool bMapOnly)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Out = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.HeightColorRamp"));
	TShaderMapRef<FMixtormatGeneratorHeightColorRampCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = GraphBuilder.AllocParameters<FMixtormatGeneratorHeightColorRampCS::FParameters>();
	P->OutputSize = Size;
	P->StopCount = Ramp.StopCount;
	P->Interpolation = Ramp.Interpolation;
	P->Weight = Ramp.Weight;
	P->InputScale = Ramp.InputScale;
	P->InputOffset = Ramp.InputOffset;
	P->MirrorRepeat = Ramp.bMirrorRepeat ? 1u : 0u;
	for (int32 Index = 0; Index < FMixtormatColorRamp::MaxStops; ++Index)
	{
		P->Positions[Index] = FVector4f(Ramp.Positions[Index], 0.0f, 0.0f, 0.0f);
		P->Colors[Index] = Ramp.Colors[Index];
	}
	P->MapOnly = bMapOnly ? 1u : 0u;
	P->HasGate = Ramp.bHasGate;
	P->HasPreviousColor = PreviousColor != nullptr || bOverrideBaseColor ? 1u : 0u;
	P->OverrideBaseColor = bOverrideBaseColor ? 1u : 0u;
	P->FillColor = FillColor;
	P->SourceMode = SourceMode;
	P->BlendMode = static_cast<uint32>(Ramp.BlendMode);
	P->FlowModulation = Ramp.FlowModulation;
	P->SignedFlowModulation = Ramp.bSignedFlowModulation ? 1u : 0u;
	P->FlowModulationDir = Ramp.FlowModulationDir;
	P->UVModulation = Ramp.UVModulation;
	P->UVModulationDir = Ramp.UVModulationDir;
	P->IdVariation = Ramp.IdVariation;
	P->IdVariationSeed = Ramp.IdVariationSeed;
	P->HasModFlow = (SourceFlow != nullptr && FMath::Abs(Ramp.FlowModulation) > 1.0e-5f) ? 1u : 0u;
	P->HasModUV = (SourceUV != nullptr && FMath::Abs(Ramp.UVModulation) > 1.0e-5f) ? 1u : 0u;
	P->HasModIds = (SourceIds != nullptr && Ramp.IdVariation > 1.0e-5f) ? 1u : 0u;

	FRDGTextureRef NormalizedSource = SourceHeight;
	if (Ramp.bNormalizeInput && SourceHeight)
	{
		if (SourceMode == 0u)
		{
			NormalizedSource = AddSignedGeneratorHeightPasses(GraphBuilder, SourceHeight, Size, true,
				1.0f, 0.0f, false, nullptr, TEXT("Mixtormat.Generator.HeightColorRamp.Normalized"));
		}
		else if (SourceMode == 1u)
		{
			NormalizedSource = AddNormalizeFieldPasses(GraphBuilder, SourceHeight, Size, 0.0f, 1.0f,
				TEXT("Mixtormat.Generator.HeightColorRamp.Normalized01"));
		}
	}
	P->SourceHeight = NormalizedSource ? NormalizedSource : Gate;
	P->SourceFlow = SourceFlow ? SourceFlow : (PreviousColor ? PreviousColor : Ctx.OutputBC[0]);
	P->SourceUV = SourceUV ? SourceUV : Ctx.EmptyPatternUV;
	P->SourceIds = SourceIds ? SourceIds : Ctx.EmptyRegionIds;
	P->Gate = Gate;
	P->PreviousColor = PreviousColor ? PreviousColor : Ctx.OutputBC[0];
	P->OutColor = GraphBuilder.CreateUAV(Out);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.HeightColorRamp"), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Out;
}

FRDGTextureRef AddGeneratorColorCompositePass(FMixtormatComposeContext& Ctx,
	FRDGTextureRef MappedColor, FRDGTextureRef Gate, FRDGTextureRef PreviousColor,
	const FGeneratorHeightColorRampRenderData& Ramp,
	const bool bOverrideBaseColor, const FVector4f& FillColor)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Out = Ctx.GraphBuilder.CreateTexture(MappedColor->Desc,
		TEXT("Mixtormat.Generator.ColorComposite"));
	TShaderMapRef<FMixtormatGeneratorColorCompositeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatGeneratorColorCompositeCS::FParameters>();
	P->OutputSize = Size;
	P->Weight = Ramp.Weight;
	P->HasGate = Ramp.bHasGate;
	P->HasPreviousColor = PreviousColor != nullptr || bOverrideBaseColor ? 1u : 0u;
	P->OverrideBaseColor = bOverrideBaseColor ? 1u : 0u;
	P->FillColor = FillColor;
	P->BlendMode = static_cast<uint32>(Ramp.BlendMode);
	P->Gate = Gate;
	P->PreviousColor = PreviousColor ? PreviousColor : Ctx.OutputBC[0];
	P->MappedColor = MappedColor;
	P->OutColor = Ctx.GraphBuilder.CreateUAV(Out);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Generator.ColorComposite"),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Out;
}

void FlushPendingGeneratorColorRamps(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer,
	TArray<FPendingColorRamp>& PendingColorRamps)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
	FRDGTextureRef GeneratedColor = nullptr;
	for (int32 RampIndex = PendingColorRamps.Num() - 1; RampIndex >= 0; --RampIndex)
	{
		const FPendingColorRamp& Pending = PendingColorRamps[RampIndex];
		const FChildRenderData& Child = *Pending.Child;
		FGeneratorHeightColorRampRenderData RampData = Child.HeightColorRamp;
		RampData.bHasGate = Pending.bHasGate ? 1u : 0u;
		FRDGTextureRef BaseColorForRamp = GeneratedColor ? GeneratedColor
			: (LayerCtx.LayerInputBC ? LayerCtx.LayerInputBC
				: (LayerCtx.LayerIndex != INDEX_NONE ? Ctx.OutputBC[1 - (LayerCtx.LayerIndex & 1)] : nullptr));
		const bool bOverrideRampBaseColor = !GeneratedColor && !LayerCtx.bGeneratorColor
			&& !LayerCtx.bSurfacingColor && !Layer.bFillPassThrough
			&& Layer.bOverrideBaseColor && !LayerCtx.ReferencedColor;
		FRDGTextureRef Color = Pending.FilteredColor
			? AddGeneratorColorCompositePass(Ctx, Pending.FilteredColor, Pending.Gate,
				BaseColorForRamp, RampData, bOverrideRampBaseColor, Layer.FillColor)
			: AddGeneratorHeightColorRampPass(Ctx, Pending.SourceHeight, Pending.SourceFlow,
				Pending.SourceUV, Pending.SourceIds, Pending.SourceMode, Pending.Gate,
				BaseColorForRamp, RampData, bOverrideRampBaseColor, Layer.FillColor);
		const FPublishedField ColorField{EMixtormatPublishedFieldKind::Color, Color, nullptr, nullptr, false};
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Child.HeightColorRamp.OutputName), ColorField);
		if (ColorField.IsComplete()) { GeneratedColor = Color; }
		if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::Color,
			Child.HeightColorRamp.OutputName, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Color, Debug, Size);
		}
	}
	if (GeneratedColor && Layer.bGeneratorAlbedo)
	{
		LayerCtx.LayerInputBC = GeneratedColor;
		LayerCtx.bGeneratorColor = true;
	}
	PendingColorRamps.Reset();
}

void CaptureGeneratorColorRamp(
	FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer,
	const FChildRenderData& Child,
	FRDGTextureRef RunningHeight,
	const int32 ScopeOwnerIndex,
	FRDGTextureRef ScopeHeight,
	TSet<int32>& CapturedColorRamps,
	TArray<FPendingColorRamp>& PendingColorRamps)
{
	if (CapturedColorRamps.Contains(Child.SourceChildIndex)) { return; }
	CapturedColorRamps.Add(Child.SourceChildIndex);
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef RampSource = RunningHeight;
	FRDGTextureRef RampFlow = nullptr;
	FRDGTextureRef RampUV = nullptr;
	FRDGTextureRef RampIds = nullptr;
	uint32 SourceMode = 0u;

	const int32 TargetChild = Child.HeightColorRamp.SourceChildIndex;
	const EMixtormatColorRampSource RampSourceEnum = static_cast<EMixtormatColorRampSource>(Child.HeightColorRamp.Source);
	switch (RampSourceEnum)
	{
	case EMixtormatColorRampSource::GeneratorRunning:
		RampSource = RunningHeight;
		SourceMode = 0u;
		break;
	case EMixtormatColorRampSource::ModuleRef:
		if (TargetChild == ScopeOwnerIndex && ScopeHeight)
		{
			RampSource = ScopeHeight;
		}
		else if (TargetChild != INDEX_NONE)
		{
			if (const FRDGTextureRef* Found = LayerCtx.GeneratorModuleHeights.Find(TargetChild))
			{
				RampSource = *Found;
			}
		}
		SourceMode = 0u;
		break;
	case EMixtormatColorRampSource::LayerHeight:
		RampSource = LayerCtx.LayerInputHeight ? LayerCtx.LayerInputHeight : RunningHeight;
		SourceMode = 0u;
		break;
	case EMixtormatColorRampSource::CompositeBelow:
		RampSource = Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)];
		SourceMode = 0u;
		break;
	case EMixtormatColorRampSource::FlowMagnitude:
	case EMixtormatColorRampSource::FlowAngle:
		SourceMode = (RampSourceEnum == EMixtormatColorRampSource::FlowMagnitude) ? 2u : 3u;
		break;
	case EMixtormatColorRampSource::NoiseValue:
		if (TargetChild != INDEX_NONE)
		{
			const FPublishedMaskKey Key = PublishedKey(Layer, TargetChild, FName(TEXT("Value")));
			if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key))
			{
				if (Field->Texture)
				{
					RampSource = Field->Texture;
				}
			}
			else if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(Key))
			{
				if (*MaskTex)
				{
					RampSource = *MaskTex;
				}
			}
		}
		SourceMode = 1u;
		break;
	case EMixtormatColorRampSource::ModuleCoverage:
	{
		SourceMode = 1u;
		if (TargetChild != INDEX_NONE)
		{
			static const FName CoverageNames[] = {
				FName(TEXT("RootCoverage")),
				FName(TEXT("PebbleCoverage")),
				FName(TEXT("RockTop")),
				FName(TEXT("CrackMask")),
				FName(TEXT("GeometryMask")),
				FName(TEXT("FieldMask"))
			};
			for (const FName& Name : CoverageNames)
			{
				if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
				{
					if (*MaskTex)
					{
						RampSource = *MaskTex;
						break;
					}
				}
				if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
				{
					if (Field->Texture)
					{
						RampSource = Field->Texture;
						break;
					}
				}
			}
		}
		if (RampSource == RunningHeight && LayerCtx.GeneratorBundle.Coverage)
		{
			RampSource = LayerCtx.GeneratorBundle.Coverage;
		}
		break;
	}
	case EMixtormatColorRampSource::ModuleEdgeDistance:
	{
		SourceMode = 0u;
		if (TargetChild != INDEX_NONE)
		{
			static const FName DistanceNames[] = {
				FName(TEXT("RootEdgeDistance")),
				FName(TEXT("PebbleEdgeDistance")),
				FName(TEXT("RockEdgeDistance")),
				FName(TEXT("FieldBoundary"))
			};
			for (const FName& Name : DistanceNames)
			{
				if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
				{
					if (Field->Texture)
					{
						RampSource = Field->Texture;
						break;
					}
				}
				if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(PublishedKey(Layer, TargetChild, Name)))
				{
					if (*MaskTex)
					{
						RampSource = *MaskTex;
						break;
					}
				}
			}
		}
		if (Child.HeightColorRamp.bNormalizeInput && RampSource)
		{
			RampSource = AddNormalizeFieldPasses(
				Ctx.GraphBuilder, RampSource, Size, 0.0f, 1.0f,
				TEXT("Mixtormat.Generator.HeightColorRamp.EdgeDistNormalized"));
			SourceMode = 1u;
		}
		break;
	}
	case EMixtormatColorRampSource::RootArcLength:
	{
		SourceMode = 1u;
		if (TargetChild != INDEX_NONE)
		{
			if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(
				PublishedKey(Layer, TargetChild, FName(TEXT("RootArcLength")))))
			{
				if (*MaskTex)
				{
					RampSource = *MaskTex;
				}
			}
		}
		break;
	}
	case EMixtormatColorRampSource::RootAcross:
	{
		SourceMode = 1u;
		if (TargetChild != INDEX_NONE)
		{
			if (const FRDGTextureRef* MaskTex = Ctx.PublishedMaskOutputs.Find(
				PublishedKey(Layer, TargetChild, FName(TEXT("RootAcross")))))
			{
				if (*MaskTex)
				{
					RampSource = *MaskTex;
				}
			}
		}
		break;
	}
	case EMixtormatColorRampSource::RegionRandom:
	{
		SourceMode = 4u;
		if (TargetChild != INDEX_NONE)
		{
			for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
			{
				if (Entry.Key == TargetChild && Entry.Value)
				{
					RampIds = Entry.Value;
					break;
				}
			}
		}
		if (!RampIds)
		{
			RampIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		}
		break;
	}
	default:
		break;
	}

	if (SourceMode == 2u || SourceMode == 3u || FMath::Abs(Child.HeightColorRamp.FlowModulation) > 1.0e-5f)
	{
		if (TargetChild != INDEX_NONE)
		{
			static const FName FlowNames[] = {
				MixtormatDiagnosticOutputs::AccumulatedFlow,
				FName(TEXT("GeneratedFlow")),
				FName(TEXT("FlowDirection"))
			};
			for (const FName& Name : FlowNames)
			{
				if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, TargetChild, Name)))
				{
					if (Field->Texture)
					{
						RampFlow = Field->Texture;
						break;
					}
				}
			}
		}
		if (!RampFlow)
		{
			for (int32 SearchIdx = Child.SourceChildIndex - 1; SearchIdx >= 0; --SearchIdx)
			{
				static const FName FlowNames[] = {
					MixtormatDiagnosticOutputs::AccumulatedFlow,
					FName(TEXT("GeneratedFlow")),
					FName(TEXT("FlowDirection"))
				};
				for (const FName& Name : FlowNames)
				{
					if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
						PublishedKey(Layer, SearchIdx, Name)))
					{
						if (Field->Texture)
						{
							RampFlow = Field->Texture;
							break;
						}
					}
				}
				if (RampFlow) { break; }
			}
		}
	}

	if (FMath::Abs(Child.HeightColorRamp.UVModulation) > 1.0e-5f)
	{
		if (TargetChild != INDEX_NONE)
		{
			static const FName UVNames[] = {
				FName(TEXT("RootUV")),
				FName(TEXT("WarpedUV"))
			};
			for (const FName& Name : UVNames)
			{
				if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, TargetChild, Name)))
				{
					if (Field->Texture)
					{
						RampUV = Field->Texture;
						break;
					}
				}
			}
		}
		if (!RampUV)
		{
			for (int32 SearchIdx = Child.SourceChildIndex - 1; SearchIdx >= 0; --SearchIdx)
			{
				static const FName UVNames[] = {
					FName(TEXT("RootUV")),
					FName(TEXT("WarpedUV"))
				};
				for (const FName& Name : UVNames)
				{
					if (const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
						PublishedKey(Layer, SearchIdx, Name)))
					{
						if (Field->Texture)
						{
							RampUV = Field->Texture;
							break;
						}
					}
				}
				if (RampUV) { break; }
			}
		}
	}

	if (Child.HeightColorRamp.IdVariation > 1.0e-5f && !RampIds)
	{
		if (TargetChild != INDEX_NONE)
		{
			for (const TPair<int32, FRDGTextureRef>& Entry : LayerCtx.RegionIdMaps)
			{
				if (Entry.Key == TargetChild && Entry.Value)
				{
					RampIds = Entry.Value;
					break;
				}
			}
		}
		if (!RampIds)
		{
			RampIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		}
	}

	const bool bHasGate = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
	FRDGTextureRef RampGate = bHasGate
		? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
		: RampSource;
	if (!LayerCtx.LayerInputBC)
	{
		AddLayerInputPass(Ctx, LayerCtx, Layer);
	}

	FPendingColorRamp& Pending = PendingColorRamps.AddDefaulted_GetRef();
	Pending.Child = &Child;
	Pending.SourceHeight = RampSource;
	Pending.SourceFlow = RampFlow;
	Pending.SourceUV = RampUV;
	Pending.SourceIds = RampIds;
	Pending.Gate = RampGate;
	Pending.SourceMode = SourceMode;
	Pending.bHasGate = bHasGate;
	for (const FChildRenderData& Filter : Layer.Children)
	{
		if (Filter.Type != EMixtormatLayerChildType::Blur
			|| Filter.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
			|| !Filter.Blur.BlursAnything()) { continue; }
		if (!Pending.FilteredColor)
		{
			FGeneratorHeightColorRampRenderData RampData = Child.HeightColorRamp;
			RampData.bHasGate = 0u;
			Pending.FilteredColor = AddGeneratorHeightColorRampPass(Ctx, RampSource, RampFlow,
				RampUV, RampIds, SourceMode, RampGate, LayerCtx.LayerInputBC, RampData,
				false, Layer.FillColor, true);
		}
		MixtormatGaussianBlur::FSettings Settings;
		Settings.RadiusX = Filter.Blur.GetRadiusX();
		Settings.RadiusY = Filter.Blur.GetRadiusY();
		Settings.Amount = Filter.Blur.Amount;
		Pending.FilteredColor = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Filter.Blur,
			Pending.FilteredColor, Settings, MixtormatGaussianBlur::EField::ColorAlpha);
	}
}

}
