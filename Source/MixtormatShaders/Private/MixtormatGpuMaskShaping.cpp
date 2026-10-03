// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuMaskShaping.h"

#include "GlobalShader.h"

class FMixtormatMaskShapingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMaskShapingCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMaskShapingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(float, Weight)
		MIXTORMAT_MASK_SHAPING_PARAMETERS
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatMaskShapingCS,
	"/Plugin/Mixtormat/Private/MixtormatMaskShaping.usf", "MainCS", SF_Compute);

namespace MixtormatGpuCompositor
{
	void AddNormalizedMaskShapingPass(
		FRDGBuilder& GraphBuilder,
		FIntPoint Resolution,
		FRDGTextureRef RawMask,
		FRDGTextureRef PreviousMask,
		FRDGTextureUAVRef OutputMask,
		const FMixtormatMaskShaping& Shaping,
		uint32 Initialize,
		uint32 BlendMode,
		float Weight)
	{
		// Reuse measured-field reduction: a constant field maps to zero, without CPU readback.
		FRDGTextureRef Normalized = AddNormalizeFieldPasses(GraphBuilder, RawMask, Resolution,
			0.0f, 1.0f, TEXT("Mixtormat.Mask.Normalized"));
		auto* Parameters = GraphBuilder.AllocParameters<FMixtormatMaskShapingCS::FParameters>();
		Parameters->OutputSize = Resolution;
		Parameters->Initialize = Initialize;
		Parameters->BlendMode = BlendMode;
		Parameters->Weight = Weight;
		BindMaskShaping(*Parameters, Shaping);
		Parameters->SourceMask = Normalized;
		Parameters->PreviousMask = PreviousMask;
		Parameters->OutputMask = OutputMask;
		TShaderMapRef<FMixtormatMaskShapingCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mixtormat.Mask.NormalizeShape"),
			Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(Resolution.X, 8),
				FMath::DivideAndRoundUp(Resolution.Y, 8), 1));
	}
}
