// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatScalarRampMath.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"

// Matches MixtormatMaskShaping.ush. All mask producers use the same binding contract.
#define MIXTORMAT_MASK_SHAPING_PARAMETERS \
	SHADER_PARAMETER(uint32, Invert) \
	SHADER_PARAMETER(float, Balance) \
	SHADER_PARAMETER(float, Contrast) \
	SHADER_PARAMETER(float, Offset) \
	SHADER_PARAMETER(FVector2f, MaskInputLevels) \
	SHADER_PARAMETER(uint32, MaskRawOutput) \
	SHADER_PARAMETER(uint32, ScalarRampCount) \
	SHADER_PARAMETER(uint32, ScalarRampInterpolation) \
	SHADER_PARAMETER_ARRAY(FVector4f, ScalarRampPoints, [FMixtormatScalarRamp::MaxPoints])

namespace MixtormatGpuCompositor
{
	template<typename TParameters>
	void BindMaskShaping(TParameters& Parameters, const FMixtormatMaskShaping& Shaping)
	{
		Parameters.Invert = Shaping.bInvert ? 1u : 0u;
		Parameters.Balance = Shaping.Balance;
		Parameters.Contrast = Shaping.Contrast;
		Parameters.Offset = Shaping.Offset;
		Parameters.MaskInputLevels = FVector2f(Shaping.InputMin, Shaping.InputMax);
		Parameters.MaskRawOutput = 0u;
		const MixtormatScalarRampMath::FGpuPayload Ramp =
			MixtormatScalarRampMath::PrepareGpuPayload(Shaping.CurveBias);
		Parameters.ScalarRampCount = Ramp.PointCount;
		Parameters.ScalarRampInterpolation = Ramp.Interpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
		{
			Parameters.ScalarRampPoints[Index] = Ramp.Points[Index];
		}
	}

	void AddNormalizedMaskShapingPass(
		FRDGBuilder& GraphBuilder,
		FIntPoint Resolution,
		FRDGTextureRef RawMask,
		FRDGTextureRef PreviousMask,
		FRDGTextureUAVRef OutputMask,
		const FMixtormatMaskShaping& Shaping,
		uint32 Initialize,
		uint32 BlendMode,
		float Weight);

	// Producers own only the raw field. Normalization and shaping are shared, before blending.
	// Producers with no valid source can skip normalization to preserve their identity behavior.
	template<typename TShader>
	void AddMaskNodePass(
		FRDGBuilder& GraphBuilder,
		FRDGEventName&& EventName,
		const TShaderMapRef<TShader>& Shader,
		typename TShader::FParameters* Parameters,
		const FMixtormatMaskShaping& Shaping,
		FIntVector Groups,
		bool bSkipNormalization = false)
	{
		BindMaskShaping(*Parameters, Shaping);
		if (!Shaping.bNormalizeInput || bSkipNormalization)
		{
			FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(EventName), Shader, Parameters, Groups);
			return;
		}

		FRDGTextureRef RawMask = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Parameters->OutputSize, PF_R32_FLOAT,
				FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Mask.Raw"));
		auto* RawParameters = GraphBuilder.AllocParameters<typename TShader::FParameters>();
		*RawParameters = *Parameters;
		RawParameters->MaskRawOutput = 1u;
		RawParameters->OutputMask = GraphBuilder.CreateUAV(RawMask);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(EventName), Shader, RawParameters, Groups);
		AddNormalizedMaskShapingPass(GraphBuilder, Parameters->OutputSize, RawMask,
			Parameters->PreviousMask, Parameters->OutputMask, Shaping,
			Parameters->Initialize, Parameters->BlendMode, Parameters->Weight);
	}
}
