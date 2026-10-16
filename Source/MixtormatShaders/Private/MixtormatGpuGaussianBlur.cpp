// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuGaussianBlur.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

class FMixtormatGaussianBlurCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGaussianBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGaussianBlurCS, FGlobalShader);

	class FField : SHADER_PERMUTATION_INT("FIELD_TYPE", 4);
	class FStage : SHADER_PERMUTATION_INT("STAGE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FField, FStage>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Axis)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, AngleRadians)
		SHADER_PARAMETER(float, Anisotropy)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, HasDirectionFlow)
		SHADER_PARAMETER(uint32, ClampCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, OriginalTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ParentFlowTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, WrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
	{
		return IsFeatureLevelSupported(Params.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGaussianBlurCS,
	"/Plugin/Mixtormat/Private/MixtormatGaussianBlur.usf", "MainCS", SF_Compute);

namespace MixtormatGaussianBlur
{
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph, FRDGTextureRef Source, const FSettings& Settings,
		EField Field, FRDGTextureRef Mask, FRDGTextureRef ParentFlow)
	{
		if (!Source || Settings.Radius <= 0.0f || Settings.Amount <= 0.0f)
			return Source;
		const FIntPoint Size = Source->Desc.Extent;
		FRDGTextureRef Current = Source;
		for (int32 Stage = 0; Stage < 3; ++Stage)
		{
			const bool bResolve = Stage == 2;
			FRDGTextureRef Dest = Graph.CreateTexture(Source->Desc,
				bResolve ? TEXT("Mixtormat.Gaussian.Resolve") :
					Stage == 0 ? TEXT("Mixtormat.Gaussian.AxisA") : TEXT("Mixtormat.Gaussian.AxisB"));
			FMixtormatGaussianBlurCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatGaussianBlurCS::FField>(static_cast<int32>(Field));
			Permutation.Set<FMixtormatGaussianBlurCS::FStage>(Stage);
			TShaderMapRef<FMixtormatGaussianBlurCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = Graph.AllocParameters<FMixtormatGaussianBlurCS::FParameters>();
			P->OutputSize = Size;
			P->Axis = Stage;
			P->Radius = Settings.Radius;
			P->Amount = Settings.Amount;
			P->AngleRadians = FMath::DegreesToRadians(Settings.AngleDegrees);
			P->Anisotropy = Settings.Anisotropy;
			P->HasMask = Mask ? 1u : 0u;
			P->HasDirectionFlow = ParentFlow ? 1u : 0u;
			P->ClampCoverage = Settings.bClampCoverage ? 1u : 0u;
			P->SourceTexture = Current;
			P->OriginalTexture = Source;
			P->MaskTexture = Mask;
			P->ParentFlowTexture = ParentFlow;
			P->WrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			P->OutputTexture = Graph.CreateUAV(Dest);
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(Graph,
				RDG_EVENT_NAME("Mixtormat.Gaussian.Stage%d.Type%d", Stage, static_cast<int32>(Field)),
				Shader, P,
				FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
					FMath::DivideAndRoundUp(Size.Y, 8), 1));
			Current = Dest;
		}
		return Current;
	}
}
