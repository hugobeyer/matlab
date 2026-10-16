// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuGaussianBlur.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

class FMixtormatGaussianBlurCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGaussianBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGaussianBlurCS, FGlobalShader);

	class FField : SHADER_PERMUTATION_INT("FIELD_TYPE", 4);
	class FStage : SHADER_PERMUTATION_INT("STAGE", 3);
	class FMasked : SHADER_PERMUTATION_BOOL("HAS_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FField, FStage, FMasked>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(uint32, ClampCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarSource)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VectorSource)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceWeights)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarOriginal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VectorOriginal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowValidity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ScalarOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VectorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputWeights)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
	{
		const FPermutationDomain Permutation(Params.PermutationId);
		return IsFeatureLevelSupported(Params.Platform, ERHIFeatureLevel::SM5)
			&& (!Permutation.Get<FMasked>() || Permutation.Get<FStage>() == 2);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatGaussianBlurCS,
	"/Plugin/Mixtormat/Private/MixtormatGaussianBlur.usf", "MainCS", SF_Compute);

namespace MixtormatGaussianBlur
{
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph, FRDGTextureRef Source, const FSettings& Settings,
		EField Field, FRDGTextureRef Mask, FRDGTextureRef FlowValidity)
	{
		if (!Source) { return Source; }
		checkf(FMath::IsFinite(Settings.RadiusX) && FMath::IsFinite(Settings.RadiusY)
			&& FMath::IsFinite(Settings.Amount), TEXT("Gaussian settings must be finite"));
		if ((Settings.RadiusX <= 0.0f && Settings.RadiusY <= 0.0f) || Settings.Amount <= 0.0f)
		{
			return Source;
		}
		const FIntPoint Size = Source->Desc.Extent;
		check(!Mask || Mask->Desc.Extent == Size);
		checkf(Field != EField::ValidFlow || (FlowValidity && FlowValidity->Desc.Extent == Size),
			TEXT("Gaussian Flow requires a matching separate validity texture"));
		check(Field != EField::Scalar || Source->Desc.Format == PF_R32_FLOAT
			|| Source->Desc.Format == PF_R16F);

		const FRDGTextureDesc SumDesc = FRDGTextureDesc::Create2D(Size,
			PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc WeightDesc = FRDGTextureDesc::Create2D(Size,
			PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Current = Source;
		FRDGTextureRef Weights = nullptr;
		for (int32 Stage = 0; Stage < 3; ++Stage)
		{
			const bool bResolve = Stage == 2;
			FRDGTextureDesc DestDesc = bResolve ? Source->Desc : SumDesc;
			DestDesc.Flags |= TexCreate_ShaderResource | TexCreate_UAV;
			FRDGTextureRef Dest = Graph.CreateTexture(DestDesc,
				bResolve ? TEXT("Mixtormat.Gaussian.Resolve") :
					Stage == 0 ? TEXT("Mixtormat.Gaussian.X") : TEXT("Mixtormat.Gaussian.Y"));
			FRDGTextureRef NextWeights = bResolve ? nullptr :
				Graph.CreateTexture(WeightDesc, TEXT("Mixtormat.Gaussian.Weights"));
			FMixtormatGaussianBlurCS::FPermutationDomain Permutation;
			Permutation.Set<FMixtormatGaussianBlurCS::FField>(static_cast<int32>(Field));
			Permutation.Set<FMixtormatGaussianBlurCS::FStage>(Stage);
			Permutation.Set<FMixtormatGaussianBlurCS::FMasked>(bResolve && Mask != nullptr);
			TShaderMapRef<FMixtormatGaussianBlurCS> Shader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
			auto* P = Graph.AllocParameters<FMixtormatGaussianBlurCS::FParameters>();
			P->OutputSize = Size;
			P->Radius = FMath::Max(0.0f, Stage == 0 ? Settings.RadiusX : Settings.RadiusY);
			P->Amount = FMath::Clamp(Settings.Amount, 0.0f, 1.0f);
			P->ClampCoverage = Settings.bClampCoverage ? 1u : 0u;
			P->ScalarSource = Field == EField::Scalar && Stage == 0 ? Source : nullptr;
			P->VectorSource = Field != EField::Scalar || Stage != 0 ? Current : nullptr;
			P->SourceWeights = Weights;
			P->ScalarOriginal = Field == EField::Scalar ? Source : nullptr;
			P->VectorOriginal = Field != EField::Scalar ? Source : nullptr;
			P->MaskTexture = Mask;
			P->FlowValidity = FlowValidity;
			P->ScalarOutput = bResolve && Field == EField::Scalar ? Graph.CreateUAV(Dest) : nullptr;
			P->VectorOutput = !bResolve || Field != EField::Scalar ? Graph.CreateUAV(Dest) : nullptr;
			P->OutputWeights = NextWeights ? Graph.CreateUAV(NextWeights) : nullptr;
			ClearUnusedGraphResources(Shader, P);
			FComputeShaderUtils::AddPass(Graph,
				RDG_EVENT_NAME("Mixtormat.Gaussian.Stage%d.Type%d", Stage, static_cast<int32>(Field)),
				Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
					FMath::DivideAndRoundUp(Size.Y, 8), 1));
			Current = Dest;
			Weights = NextWeights;
		}
		return Current;
	}
}
