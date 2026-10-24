// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuSurfacingPasses.h"
#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatGpuGeneratorCommon.h"
#include "MixtormatColorRampMath.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "RHIStaticStates.h"

namespace MixtormatGpuCompositor
{
namespace
{
	class FMixtormatWoodCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatWoodCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatWoodCS, FGlobalShader);
		class FStage : SHADER_PERMUTATION_INT("WOOD_STAGE", 2);
		using FPermutationDomain = TShaderPermutationDomain<FStage>;
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, Mode)
			SHADER_PARAMETER(uint32, Species)
			SHADER_PARAMETER(uint32, Cut)
			SHADER_PARAMETER(uint32, Figure)
			SHADER_PARAMETER(uint32, Prep)
			SHADER_PARAMETER(uint32, Finish)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(float, HeightScale)
			SHADER_PARAMETER(float, RingScale)
			SHADER_PARAMETER(float, LatewoodRatio)
			SHADER_PARAMETER(float, LatewoodSharpness)
			SHADER_PARAMETER(float, RingWobble)
			SHADER_PARAMETER(float, PoreDepth)
			SHADER_PARAMETER(float, PoreDensity)
			SHADER_PARAMETER(float, RayAmount)
			SHADER_PARAMETER(float, RayScale)
			SHADER_PARAMETER(float, PithOffset)
			SHADER_PARAMETER(float, CutTilt)
			SHADER_PARAMETER(float, FigureAmount)
			SHADER_PARAMETER(float, FigureScale)
			SHADER_PARAMETER(float, Chatoyance)
			SHADER_PARAMETER(float, PrepIntensity)
			SHADER_PARAMETER(float, Weathering)
			SHADER_PARAMETER(float, Roughness)
			SHADER_PARAMETER(float, FinishAmount)
			SHADER_PARAMETER(float, WetOut)
			SHADER_PARAMETER(float, TintAmount)
			SHADER_PARAMETER(FVector4f, FinishTint)
			SHADER_PARAMETER(float, FilmBuild)
			SHADER_PARAMETER(float, DepressionFill)
			SHADER_PARAMETER(float, Sheen)
			SHADER_PARAMETER(float, ApplicationTexture)
			SHADER_PARAMETER(float, ApplicationAngle)
			SHADER_PARAMETER(float, Wear)
			SHADER_PARAMETER(uint32, GeneratorLayer)
			SHADER_PARAMETER(FVector2f, GeneratorUVScale)
			SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
			SHADER_PARAMETER(int32, GeneratorUVRotation)
			SHADER_PARAMETER(uint32, GeneratorUVFlipU)
			SHADER_PARAMETER(uint32, GeneratorUVFlipV)
			SHADER_PARAMETER(uint32, UsePreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SubstrateHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputColor)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputOptics)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
			SHADER_PARAMETER(uint32, ColorCount)
			SHADER_PARAMETER(uint32, ColorInterpolation)
			SHADER_PARAMETER_ARRAY(FVector4f, ColorPositions, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER_ARRAY(FVector4f, ColorColors, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutRAM)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutOptics)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutDiagnostics)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlow)
			SHADER_PARAMETER(float, Amount)
			SHADER_PARAMETER(uint32, HasGate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BeforeHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AfterHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CompletedRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RawDiagnostics)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RawFlow)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightDelta)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRoughness)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutPores)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutLatewood)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRays)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCoat)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutPublishedFlow)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFlowValidity)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatWoodCS, "/Plugin/Mixtormat/Private/MixtormatWood.usf", "MainCS", SF_Compute);

	FRDGTextureRef WoodTexture(FMixtormatComposeContext& Ctx, EPixelFormat Format, const TCHAR* Name)
	{
		return Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Ctx.Request.Resolution, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	}

	void DispatchWood(FMixtormatComposeContext& Ctx, FMixtormatWoodCS::FParameters* P, int32 Stage, int32 ChildIndex)
	{
		FMixtormatWoodCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatWoodCS::FStage>(Stage);
		TShaderMapRef<FMixtormatWoodCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		P->OutputSize = Ctx.Request.Resolution;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Wood.Stage%d.C%d", Stage, ChildIndex), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(P->OutputSize.X, 8), FMath::DivideAndRoundUp(P->OutputSize.Y, 8), 1));
	}
}

FSurfacingFields AddWoodSurfacePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV)
{
	FSurfacingFields Result;
	if (!Layer.bEnabled || !Child.Surfacing.bEnabled || Child.Surfacing.Type != EMixtormatSurfacingType::Wood
		|| !SubstrateHeight) { return Result; }
	check(LayerCtx.LayerInputBC && LayerCtx.LayerInputRAM && LayerCtx.LayerInputOptics);
	const FMixtormatWood& W = Child.Surfacing.Wood;
	Result.Height = WoodTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Wood.Height"));
	Result.Color = WoodTexture(Ctx, PF_A32B32G32R32F, TEXT("Mixtormat.Wood.Color"));
	Result.RAM = WoodTexture(Ctx, PF_FloatRGBA, TEXT("Mixtormat.Wood.RAM"));
	Result.Optics = WoodTexture(Ctx, PF_FloatRGBA, TEXT("Mixtormat.Wood.Optics"));
	Result.WoodDiagnostics = WoodTexture(Ctx, PF_A32B32G32R32F, TEXT("Mixtormat.Wood.AnatomyCoat"));
	Result.WoodFlow = WoodTexture(Ctx, GetPublishedFieldFormat(EMixtormatPublishedFieldKind::Flow), TEXT("Mixtormat.Wood.FiberFlow"));
	Result.FuzzColor = LayerCtx.LayerInputFuzzColor;
	Result.Subsurface = LayerCtx.LayerInputSubsurface;
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatWoodCS::FParameters>();
	P->Mode = static_cast<uint32>(W.Mode);
	P->Species = static_cast<uint32>(W.Species);
	P->Cut = static_cast<uint32>(W.Cut);
	P->Figure = static_cast<uint32>(W.Figure);
	P->Prep = static_cast<uint32>(W.Prep);
	P->Finish = static_cast<uint32>(W.Finish);
	P->Seed = static_cast<uint32>(W.Seed);
#define WOOD_BIND(Member) P->Member = W.Member
	WOOD_BIND(HeightScale); WOOD_BIND(RingScale); WOOD_BIND(LatewoodRatio); WOOD_BIND(LatewoodSharpness);
	WOOD_BIND(RingWobble); WOOD_BIND(PoreDepth); WOOD_BIND(PoreDensity); WOOD_BIND(RayAmount); WOOD_BIND(RayScale);
	WOOD_BIND(PithOffset); WOOD_BIND(CutTilt); WOOD_BIND(FigureAmount); WOOD_BIND(FigureScale); WOOD_BIND(Chatoyance);
	WOOD_BIND(PrepIntensity); WOOD_BIND(Weathering); WOOD_BIND(Roughness); WOOD_BIND(FinishAmount); WOOD_BIND(WetOut);
	WOOD_BIND(TintAmount); WOOD_BIND(FilmBuild); WOOD_BIND(DepressionFill); WOOD_BIND(Sheen);
	WOOD_BIND(ApplicationTexture); WOOD_BIND(ApplicationAngle); WOOD_BIND(Wear);
#undef WOOD_BIND
	P->FinishTint = FVector4f(W.FinishTint.R, W.FinishTint.G, W.FinishTint.B, 1.0f);
	P->GeneratorLayer = 0u;
	P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
	P->GeneratorUVOffset = Layer.UVOffset;
	P->GeneratorUVRotation = Layer.Rotation;
	P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
	P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
	P->UsePreGenerationUV = PreUV ? 1u : 0u;
	P->PreGenerationUV = PreUV ? PreUV : Ctx.EmptyPatternUV;
	P->SubstrateHeight = SubstrateHeight;
	P->InputColor = LayerCtx.LayerInputBC;
	P->InputRAM = LayerCtx.LayerInputRAM;
	P->InputOptics = LayerCtx.LayerInputOptics;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	const auto Palette = MixtormatColorRampMath::PrepareGpuPayload(W.ColorRamp);
	P->ColorCount = Palette.StopCount;
	P->ColorInterpolation = Palette.Interpolation;
	for (int32 I = 0; I < FMixtormatColorRamp::MaxStops; ++I)
	{
		P->ColorPositions[I] = FVector4f(Palette.Positions[I], 0, 0, 0);
		P->ColorColors[I] = Palette.Colors[I];
	}
	P->OutHeight = Ctx.GraphBuilder.CreateUAV(Result.Height);
	P->OutColor = Ctx.GraphBuilder.CreateUAV(Result.Color);
	P->OutRAM = Ctx.GraphBuilder.CreateUAV(Result.RAM);
	P->OutOptics = Ctx.GraphBuilder.CreateUAV(Result.Optics);
	P->OutDiagnostics = Ctx.GraphBuilder.CreateUAV(Result.WoodDiagnostics);
	P->OutFlow = Ctx.GraphBuilder.CreateUAV(Result.WoodFlow);
	DispatchWood(Ctx, P, 0, Child.SourceChildIndex);
	return Result;
}

void PublishWoodFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const FChildRenderData& Child, const FSurfacingFields& Raw,
	FRDGTextureRef Gate, FRDGTextureRef BeforeHeight, FRDGTextureRef AfterHeight, float AncestorAmount)
{
	check(Raw.WoodDiagnostics && Raw.WoodFlow && BeforeHeight && AfterHeight && LayerCtx.LayerInputRAM);
	FRDGTextureRef Scalars[6];
	const FName Names[] = {TEXT("Height"), TEXT("Roughness"), TEXT("PoreMask"), TEXT("LatewoodMask"), TEXT("RayMask"), TEXT("CoatThickness")};
	for (FRDGTextureRef& Scalar : Scalars) { Scalar = WoodTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Wood.Diagnostic")); }
	FRDGTextureRef Flow = WoodTexture(Ctx, GetPublishedFieldFormat(EMixtormatPublishedFieldKind::Flow), TEXT("Mixtormat.Wood.PublishedFlow"));
	FRDGTextureRef Validity = WoodTexture(Ctx, PF_R16F, TEXT("Mixtormat.Wood.FlowValidity"));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatWoodCS::FParameters>();
	P->Amount = Child.Surfacing.Wood.Amount * AncestorAmount;
	P->HasGate = Gate ? 1u : 0u;
	P->Gate = Gate ? Gate : Ctx.EmptyDriverSignal;
	P->BeforeHeight = BeforeHeight;
	P->AfterHeight = AfterHeight;
	P->CompletedRAM = LayerCtx.LayerInputRAM;
	P->RawDiagnostics = Raw.WoodDiagnostics;
	P->RawFlow = Raw.WoodFlow;
	P->OutHeightDelta = Ctx.GraphBuilder.CreateUAV(Scalars[0]);
	P->OutRoughness = Ctx.GraphBuilder.CreateUAV(Scalars[1]);
	P->OutPores = Ctx.GraphBuilder.CreateUAV(Scalars[2]);
	P->OutLatewood = Ctx.GraphBuilder.CreateUAV(Scalars[3]);
	P->OutRays = Ctx.GraphBuilder.CreateUAV(Scalars[4]);
	P->OutCoat = Ctx.GraphBuilder.CreateUAV(Scalars[5]);
	P->OutPublishedFlow = Ctx.GraphBuilder.CreateUAV(Flow);
	P->OutFlowValidity = Ctx.GraphBuilder.CreateUAV(Validity);
	DispatchWood(Ctx, P, 1, Child.SourceChildIndex);
	const auto Publish = [&](FName Name, EMixtormatPublishedFieldKind Kind, FRDGTextureRef Texture)
	{
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name),
			FPublishedField{Kind, Texture, nullptr, nullptr, false});
		if (Kind == EMixtormatPublishedFieldKind::Scalar01)
		{
			Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name), Texture);
		}
		const auto PreviewKind = Kind == EMixtormatPublishedFieldKind::Color ? EMixtormatPreviewOutputKind::Color
			: Kind == EMixtormatPublishedFieldKind::ScalarSigned ? EMixtormatPreviewOutputKind::SignedDistance : EMixtormatPreviewOutputKind::Mask;
		if (IsChildOutputPreviewTarget(Ctx.Request, PreviewKind, Name, LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
			if (Kind == EMixtormatPublishedFieldKind::Color) { AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
			else if (Kind == EMixtormatPublishedFieldKind::ScalarSigned) { AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
			else { AddMaskOverlayPass(Ctx.GraphBuilder, Texture, Debug, Ctx.Request.Resolution); }
		}
	};
	for (int32 I = 0; I < 6; ++I)
	{
		Publish(Names[I], I == 0 ? EMixtormatPublishedFieldKind::ScalarSigned : EMixtormatPublishedFieldKind::Scalar01, Scalars[I]);
	}
	Publish(FName(TEXT("Color")), EMixtormatPublishedFieldKind::Color, LayerCtx.LayerInputBC);
	Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("FlowDirection"))),
		FPublishedField{EMixtormatPublishedFieldKind::Flow, Flow, Flow, Validity, false});
	LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Scalars[0]);
}
}
