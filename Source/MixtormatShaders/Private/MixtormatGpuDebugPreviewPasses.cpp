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

namespace MixtormatGpuCompositor
{
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
		const FRDGTextureRef OutputDebug,
		const FIntPoint Resolution)
	{
		FMixtormatDebugPreviewRegionIdsCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatDebugPreviewRegionIdsCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->SourceIds = SourceIds;
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
