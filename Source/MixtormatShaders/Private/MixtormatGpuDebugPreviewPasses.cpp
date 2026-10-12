// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

// Legacy IDs/SDF/colour previews remain here. Mask/scalar/Flow/Vector2 diagnostics
// export data only; their visualization belongs exclusively to viewport PostFX.

class FMixtormatDebugPreviewSignedDistanceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDebugPreviewSignedDistanceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDebugPreviewSignedDistanceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, DistanceToPixels)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceDistance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatDebugPreviewSignedDistanceCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainSignedDistanceCS",
	SF_Compute);

class FMixtormatDebugPreviewRegionIdsCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDebugPreviewRegionIdsCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDebugPreviewRegionIdsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceIds)
		SHADER_PARAMETER(uint32, SourceHasGapMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceGapMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatDebugPreviewRegionIdsCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainRegionIdsCS",
	SF_Compute);

class FMixtormatDebugPreviewColorCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDebugPreviewColorCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDebugPreviewColorCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatDebugPreviewColorCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainColorCS",
	SF_Compute);

class FMixtormatRegionIdPickCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRegionIdPickCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRegionIdPickCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PickSourceIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRegionIdPick)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRegionIdPickCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainRegionIdPickCS",
	SF_Compute);


class FMixtormatMaskOverlayCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMaskOverlayCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMaskOverlayCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, OverlaySourceIsSigned)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, OverlaySourceMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OverlayOutputColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatMaskOverlayCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainOverlayMaskCS",
	SF_Compute);

// Data-only export into a float32 target; legacy colour/debug blits are unchanged.
class FMixtormatRawFlowOverlayCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRawFlowOverlayCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRawFlowOverlayCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, RawFlowRotate90)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, RawFlowSource)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RawFlowOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatRawFlowOverlayCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf", "MainRawFlowCS", SF_Compute);

// Bilinear resample for diagnostic exports whose producer resolution differs from the
// composition's. Only engaged on a mismatch; matching extents keep the exact Load/store.
class FMixtormatDiagnosticResampleCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDiagnosticResampleCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDiagnosticResampleCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ResampleSource)
		SHADER_PARAMETER_SAMPLER(SamplerState, ResampleSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ResampleOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatDiagnosticResampleCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf", "MainDiagnosticResampleCS", SF_Compute);

namespace MixtormatGpuCompositor
{
	void AddRawFlowOverlayPass(FRDGBuilder& GraphBuilder, const FRDGTextureRef Source,
		const FRDGTextureRef Output, const FIntPoint Resolution, const bool bRotateOutput90)
	{
		auto* Parameters = GraphBuilder.AllocParameters<FMixtormatRawFlowOverlayCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->RawFlowRotate90 = bRotateOutput90 ? 1u : 0u;
		Parameters->RawFlowSource = Source;
		Parameters->RawFlowOutput = GraphBuilder.CreateUAV(Output);
		TShaderMapRef<FMixtormatRawFlowOverlayCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Diagnostic.RawFlow"),
			Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8), 1));
	}

	FRDGTextureRef AddDiagnosticResamplePass(FRDGBuilder& GraphBuilder, const FRDGTextureRef Source,
		const FIntPoint Resolution)
	{
		FRDGTextureRef Output = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Resolution, PF_A32B32G32R32F,
				FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Diagnostic.Resample"));
		auto* Parameters = GraphBuilder.AllocParameters<FMixtormatDiagnosticResampleCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->ResampleSource = Source;
		Parameters->ResampleSampler =
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->ResampleOutput = GraphBuilder.CreateUAV(Output);
		TShaderMapRef<FMixtormatDiagnosticResampleCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Diagnostic.Resample"),
			Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8), 1));
		return Output;
	}

	void AddMaskOverlayPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef Mask,
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution,
		const bool bSourceSigned)
	{
		if (!Mask || !OutputDebug)
		{
			return;
		}
		auto* Parameters = GraphBuilder.AllocParameters<FMixtormatMaskOverlayCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->OverlaySourceIsSigned = bSourceSigned ? 1u : 0u;
		Parameters->OverlaySourceMask = Mask;
		Parameters->OverlayOutputColor = GraphBuilder.CreateUAV(OutputDebug);
		TShaderMapRef<FMixtormatMaskOverlayCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder, RDG_EVENT_NAME("Mixtormat.Diagnostic.MaskOverlay"), Shader, Parameters,
			FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8), 1));
	}


	// One dispatch per composite, and only while a Region IDs preview is actually up: the editor
	// reads a single texel out of the result when the artist clicks, so the cost is one full-
	// resolution copy on a frame that was already rendering the preview.
	void AddRegionIdPickPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef SourceIds,
		const FRDGTextureRef OutputPick,
		const FIntPoint Resolution)
	{
		if (!SourceIds || !OutputPick)
		{
			return;
		}
		FMixtormatRegionIdPickCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatRegionIdPickCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->PickSourceIds = SourceIds;
		Parameters->OutputRegionIdPick = GraphBuilder.CreateUAV(OutputPick);

		TShaderMapRef<FMixtormatRegionIdPickCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.RegionIdPick"),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8),
				1));
	}


	void AddDebugPreviewColorBlitPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef SourceColor,
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution)
	{
		FMixtormatDebugPreviewColorCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatDebugPreviewColorCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->SourceColor = SourceColor;
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatDebugPreviewColorCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.DebugPreview.Color"),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8),
				1));
	}

	void AddDebugPreviewSignedDistanceBlitPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef SourceDistance,
		const float DistanceToPixels,
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution)
	{
		FMixtormatDebugPreviewSignedDistanceCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatDebugPreviewSignedDistanceCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->DistanceToPixels = DistanceToPixels;
		Parameters->SourceDistance = SourceDistance;
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatDebugPreviewSignedDistanceCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.DebugPreview.SignedDistance"),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8),
				1));
	}

	void AddDebugPreviewRegionIdsBlitPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef SourceIds,
		const FRDGTextureRef GapMask,
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution)
	{
		FMixtormatDebugPreviewRegionIdsCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatDebugPreviewRegionIdsCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->SourceIds = SourceIds;
		Parameters->SourceHasGapMask = GapMask ? 1u : 0u;
		// RDG requires every declared texture parameter bound to something, whether or not the
		// shader branches on it -- a 1x1 stand-in, cleared rather than left undefined, when there
		// is no gap mask to combine.
		FRDGTextureRef GapMaskOrDummy = GapMask;
		if (!GapMaskOrDummy)
		{
			GapMaskOrDummy = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_R16F, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.DebugPreview.EmptyGapMask"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GapMaskOrDummy), FVector4f(0.0f));
		}
		Parameters->SourceGapMask = GapMaskOrDummy;
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatDebugPreviewRegionIdsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.DebugPreview.RegionIds"),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8),
				1));
	}
}
