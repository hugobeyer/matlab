// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

// Generic child-output preview: colours an already-built texture into OutputDebug after the fact,
// for a producer whose own kernel has no debug write of its own (Breakup's Region IDs/Gap/Edge/
// Pieces, Worn Edges' Wear, Pattern IDs' Gap). Cluster/Pattern/Combine Region IDs keep their own
// inline debug write instead of routing through here -- see MixtormatClusterIds.usf and
// MixtormatCombineIds.usf, which already do the same hash this file's region-ids kernel does.

class FMixtormatDebugPreviewMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDebugPreviewMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDebugPreviewMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatDebugPreviewMaskCS,
	"/Plugin/Mixtormat/Private/MixtormatDebugPreviewBlit.usf",
	"MainMaskCS",
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

namespace MixtormatGpuCompositor
{
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

	void AddDebugPreviewMaskBlitPass(
		FRDGBuilder& GraphBuilder,
		const FRDGTextureRef SourceMask,
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution)
	{
		FMixtormatDebugPreviewMaskCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatDebugPreviewMaskCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->SourceMask = SourceMask;
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug);

		TShaderMapRef<FMixtormatDebugPreviewMaskCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.DebugPreview.Mask"),
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
