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
	class FMixtormatStoneCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatStoneCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatStoneCS, FGlobalShader);
		class FStage : SHADER_PERMUTATION_INT("STONE_STAGE", 2);
		using FPermutationDomain = TShaderPermutationDomain<FStage>;
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, Family)
			SHADER_PARAMETER(uint32, Pattern)
			SHADER_PARAMETER(uint32, Finish)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(float, HeightScale)
			SHADER_PARAMETER(float, Scale)
			SHADER_PARAMETER(float, Direction)
			SHADER_PARAMETER(float, GrainSize)
			SHADER_PARAMETER(float, VeinFreq)
			SHADER_PARAMETER(float, VeinWidth)
			SHADER_PARAMETER(float, Sharpness)
			SHADER_PARAMETER(float, Secondary)
			SHADER_PARAMETER(float, Distort)
			SHADER_PARAMETER(float, WarpFreq)
			SHADER_PARAMETER(float, WarpPower)
			SHADER_PARAMETER(float, Grain)
			SHADER_PARAMETER(float, StripeFreq)
			SHADER_PARAMETER(float, StripeSoft)
			SHADER_PARAMETER(float, StripeTone)
			SHADER_PARAMETER(float, CellScale)
			SHADER_PARAMETER(float, CellJitter)
			SHADER_PARAMETER(float, CellWarp)
			SHADER_PARAMETER(float, EdgeWidth)
			SHADER_PARAMETER(float, EdgeDarken)
			SHADER_PARAMETER(float, Roughness)
			SHADER_PARAMETER(float, FinishAmount)
			SHADER_PARAMETER(float, Alteration)
			SHADER_PARAMETER(FVector4f, VeinColor)
			SHADER_PARAMETER(FVector4f, EdgeColor)
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
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			// Stage 0 only; stage 1 is FMixtormatStonePublishCS.
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
				&& Parameters.PermutationId == 0;
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatStoneCS, "/Plugin/Mixtormat/Private/MixtormatStone.usf", "MainCS", SF_Compute);

	// Separate class/struct for stage 1: sharing one struct across disjoint
	// stage declarations trips the D3D root-parameter minifier assert.
	class FMixtormatStonePublishCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatStonePublishCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatStonePublishCS, FGlobalShader);
		class FStage : SHADER_PERMUTATION_INT("STONE_STAGE", 2);
		using FPermutationDomain = TShaderPermutationDomain<FStage>;
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(float, Amount)
			SHADER_PARAMETER(uint32, HasGate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Gate)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BeforeHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AfterHeight)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CompletedRAM)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RawDiagnostics)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeightDelta)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutRoughness)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutVein)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutBand)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutEdge)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCells)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			// Stage 1 only; stage 0 is FMixtormatStoneCS.
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
				&& Parameters.PermutationId == 1;
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatStonePublishCS, "/Plugin/Mixtormat/Private/MixtormatStone.usf", "MainCS", SF_Compute);

	FRDGTextureRef StoneTexture(FMixtormatComposeContext& Ctx, EPixelFormat Format, const TCHAR* Name)
	{
		return Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Ctx.Request.Resolution, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	}

	void DispatchStone(FMixtormatComposeContext& Ctx, FMixtormatStoneCS::FParameters* P, int32 ChildIndex)
	{
		FMixtormatStoneCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatStoneCS::FStage>(0);
		TShaderMapRef<FMixtormatStoneCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		P->OutputSize = Ctx.Request.Resolution;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Stone.C%d", ChildIndex), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(P->OutputSize.X, 8), FMath::DivideAndRoundUp(P->OutputSize.Y, 8), 1));
	}

	void DispatchStonePublish(FMixtormatComposeContext& Ctx, FMixtormatStonePublishCS::FParameters* P, int32 ChildIndex)
	{
		FMixtormatStonePublishCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatStonePublishCS::FStage>(1);
		TShaderMapRef<FMixtormatStonePublishCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		P->OutputSize = Ctx.Request.Resolution;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Stone.Publish.C%d", ChildIndex), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(P->OutputSize.X, 8), FMath::DivideAndRoundUp(P->OutputSize.Y, 8), 1));
	}
}

FSurfacingFields AddStoneSurfacePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV)
{
	FSurfacingFields Result;
	if (!Layer.bEnabled || !Child.Surfacing.bEnabled || Child.Surfacing.Type != EMixtormatSurfacingType::Stone
		|| !SubstrateHeight) { return Result; }
	check(LayerCtx.LayerInputBC && LayerCtx.LayerInputRAM && LayerCtx.LayerInputOptics);
	const FMixtormatStone& S = Child.Surfacing.Stone;
	Result.Height = StoneTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Stone.Height"));
	Result.Color = StoneTexture(Ctx, PF_A32B32G32R32F, TEXT("Mixtormat.Stone.Color"));
	Result.RAM = StoneTexture(Ctx, PF_FloatRGBA, TEXT("Mixtormat.Stone.RAM"));
	Result.Optics = StoneTexture(Ctx, PF_FloatRGBA, TEXT("Mixtormat.Stone.Optics"));
	Result.StoneDiagnostics = StoneTexture(Ctx, PF_A32B32G32R32F, TEXT("Mixtormat.Stone.Diagnostics"));
	Result.FuzzColor = LayerCtx.LayerInputFuzzColor;
	Result.Subsurface = LayerCtx.LayerInputSubsurface;
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatStoneCS::FParameters>();
	P->Family = static_cast<uint32>(S.Family);
	P->Pattern = static_cast<uint32>(S.Pattern);
	P->Finish = static_cast<uint32>(S.Finish);
	P->Seed = static_cast<uint32>(S.Seed);
#define STONE_BIND(Member) P->Member = S.Member
	STONE_BIND(HeightScale); STONE_BIND(Scale); STONE_BIND(Direction); STONE_BIND(GrainSize);
	STONE_BIND(VeinFreq); STONE_BIND(VeinWidth); STONE_BIND(Sharpness); STONE_BIND(Secondary);
	STONE_BIND(Distort); STONE_BIND(WarpFreq); STONE_BIND(WarpPower); STONE_BIND(Grain);
	STONE_BIND(StripeFreq); STONE_BIND(StripeSoft); STONE_BIND(StripeTone); STONE_BIND(CellScale);
	STONE_BIND(CellJitter); STONE_BIND(CellWarp); STONE_BIND(EdgeWidth); STONE_BIND(EdgeDarken);
	STONE_BIND(Roughness); STONE_BIND(FinishAmount); STONE_BIND(Alteration);
#undef STONE_BIND
	P->VeinColor = FVector4f(S.VeinColor.R, S.VeinColor.G, S.VeinColor.B, 1.0f);
	P->EdgeColor = FVector4f(S.EdgeColor.R, S.EdgeColor.G, S.EdgeColor.B, 1.0f);
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
	const auto Palette = MixtormatColorRampMath::PrepareGpuPayload(S.ColorRamp);
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
	P->OutDiagnostics = Ctx.GraphBuilder.CreateUAV(Result.StoneDiagnostics);
	DispatchStone(Ctx, P, Child.SourceChildIndex);
	return Result;
}

void PublishStoneFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, const FChildRenderData& Child, const FSurfacingFields& Raw,
	FRDGTextureRef Gate, FRDGTextureRef BeforeHeight, FRDGTextureRef AfterHeight, float AncestorAmount)
{
	check(Raw.StoneDiagnostics && BeforeHeight && AfterHeight && LayerCtx.LayerInputRAM);
	FRDGTextureRef Scalars[6];
	const FName Names[] = {FName(TEXT("Height")), FName(TEXT("Roughness")), FName(TEXT("Vein")),
			FName(TEXT("Band")), FName(TEXT("Edge")), FName(TEXT("CellId"))};
	for (FRDGTextureRef& Scalar : Scalars) { Scalar = StoneTexture(Ctx, PF_R32_FLOAT, TEXT("Mixtormat.Stone.Diagnostic")); }
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatStonePublishCS::FParameters>();
	P->Amount = Child.Surfacing.Stone.Amount * AncestorAmount;
	P->HasGate = Gate ? 1u : 0u;
	P->Gate = Gate ? Gate : Ctx.EmptyDriverSignal;
	P->BeforeHeight = BeforeHeight;
	P->AfterHeight = AfterHeight;
	P->CompletedRAM = LayerCtx.LayerInputRAM;
	P->RawDiagnostics = Raw.StoneDiagnostics;
	P->OutHeightDelta = Ctx.GraphBuilder.CreateUAV(Scalars[0]);
	P->OutRoughness = Ctx.GraphBuilder.CreateUAV(Scalars[1]);
	P->OutVein = Ctx.GraphBuilder.CreateUAV(Scalars[2]);
	P->OutBand = Ctx.GraphBuilder.CreateUAV(Scalars[3]);
	P->OutEdge = Ctx.GraphBuilder.CreateUAV(Scalars[4]);
	P->OutCells = Ctx.GraphBuilder.CreateUAV(Scalars[5]);
	DispatchStonePublish(Ctx, P, Child.SourceChildIndex);
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
		Publish(Names[I], I == 0 ? EMixtormatPublishedFieldKind::ScalarSigned
			: (I == 5 ? EMixtormatPublishedFieldKind::RegionIds : EMixtormatPublishedFieldKind::Scalar01), Scalars[I]);
	}
	Publish(FName(TEXT("Color")), EMixtormatPublishedFieldKind::Color, LayerCtx.LayerInputBC);
	LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Scalars[0]);
}
}
