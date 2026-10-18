// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuDirectionalBlur.h"

#include "MixtormatBlur.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

class FMixtormatDirectionalBlurCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDirectionalBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDirectionalBlurCS, FGlobalShader);

	class FField : SHADER_PERMUTATION_INT("FIELD_TYPE", 5);
	class FGuided : SHADER_PERMUTATION_BOOL("FLOW_GUIDED");
	class FMasked : SHADER_PERMUTATION_BOOL("HAS_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FField, FGuided, FMasked>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FVector2f, AngleDirection)
		SHADER_PARAMETER(float, FlowInfluence)
		SHADER_PARAMETER(float, StepLength)
		SHADER_PARAMETER(int32, StepCount)
		SHADER_PARAMETER(uint32, Centered)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(FVector4f, ChannelWeights)
		SHADER_PARAMETER(uint32, ClampCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScalarSource)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VectorSource)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DirectionFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DirectionValidity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceValidity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ScalarOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VectorOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
	{
		return IsFeatureLevelSupported(Params.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Params,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Params, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BLUR_GROUP_SIZE"), MixtormatDirectionalBlur::GPU::ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("BLUR_MAX_STEPS"), MixtormatDirectionalBlur::GPU::MaxStepsPerSide);
		OutEnvironment.SetDefine(TEXT("BLUR_DIRECTION_EPSILON_SQUARED"),
			MixtormatDirectionalBlur::GPU::DirectionEpsilonSquared);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMixtormatDirectionalBlurCS,
	"/Plugin/Mixtormat/Private/MixtormatDirectionalBlur.usf", "MainCS", SF_Compute);

namespace MixtormatDirectionalBlur
{
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph, FRDGTextureRef Source, const FMixtormatBlur& Blur,
		const MixtormatGaussianBlur::FSettings& Settings, MixtormatGaussianBlur::EField Field,
		FRDGTextureRef Mask, FRDGTextureRef DirectionFlow, FRDGTextureRef DirectionValidity,
		FRDGTextureRef SourceValidity)
	{
		using MixtormatGaussianBlur::EField;
		if (!Source || !Blur.bEnabled) { return Source; }
		if (Blur.Mode == EMixtormatBlurMode::Gaussian)
		{
			return MixtormatGaussianBlur::AddPasses(Graph, Source, Settings, Field, Mask, SourceValidity);
		}
		if (Blur.Mode != EMixtormatBlurMode::Directional && Blur.Mode != EMixtormatBlurMode::Smear)
		{
			return Source;
		}
		if (!FMath::IsFinite(Settings.Amount) || Settings.Amount <= 0.0f
			|| !FMath::IsFinite(Blur.Length) || Blur.Length == 0.0f
			|| !FMath::IsFinite(Blur.Angle) || !FMath::IsFinite(Blur.FlowInfluence))
		{
			return Source;
		}
		if (Blur.DirectionSource != EMixtormatBlurDirectionSource::Angle
			&& Blur.DirectionSource != EMixtormatBlurDirectionSource::Flow) { return Source; }
		const FIntPoint Size = Source->Desc.Extent;
		const bool bGuided = Blur.DirectionSource == EMixtormatBlurDirectionSource::Flow;
		// Even influence zero needs the explicitly selected Flow to be available.
		if (bGuided && (!DirectionFlow || !DirectionValidity
			|| DirectionFlow->Desc.Extent != Size || DirectionValidity->Desc.Extent != Size))
		{
			return Source;
		}
		if (Field == EField::ValidFlow && (!SourceValidity || SourceValidity->Desc.Extent != Size))
		{
			return Source;
		}
		check(!Mask || Mask->Desc.Extent == Size);
		check(Field != EField::Scalar || Source->Desc.Format == PF_R32_FLOAT
			|| Source->Desc.Format == PF_R16F);

		const float Extent = FMath::Abs(Blur.Length);
		// Clamp before converting to int: finite authored lengths may exceed int32.
		const int32 Count = FMath::CeilToInt(FMath::Clamp(Extent / GPU::StepTexels,
			1.0f, static_cast<float>(GPU::MaxStepsPerSide)));
		const bool bCentered = Blur.Mode == EMixtormatBlurMode::Directional;
		const float TrailSign = Blur.Length > 0.0f ? -1.0f : 1.0f;
		const float Radians = FMath::DegreesToRadians(FMath::Fmod(Blur.Angle, 360.0f));
		FRDGTextureDesc Desc = Source->Desc;
		Desc.Flags |= TexCreate_ShaderResource | TexCreate_UAV;
		FRDGTextureRef Dest = Graph.CreateTexture(Desc, TEXT("Mixtormat.DirectionalBlur.Resolve"));

		FMixtormatDirectionalBlurCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatDirectionalBlurCS::FField>(static_cast<int32>(Field));
		Permutation.Set<FMixtormatDirectionalBlurCS::FGuided>(bGuided);
		Permutation.Set<FMixtormatDirectionalBlurCS::FMasked>(Mask != nullptr);
		TShaderMapRef<FMixtormatDirectionalBlurCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = Graph.AllocParameters<FMixtormatDirectionalBlurCS::FParameters>();
		P->OutputSize = Size;
		P->AngleDirection = FVector2f(FMath::Cos(Radians), FMath::Sin(Radians));
		P->FlowInfluence = FMath::Clamp(Blur.FlowInfluence, 0.0f, 1.0f);
		P->StepLength = (Extent / static_cast<float>(Count)) * (bCentered ? 1.0f : TrailSign);
		P->StepCount = Count;
		P->Centered = bCentered ? 1u : 0u;
		P->Amount = FMath::Clamp(Settings.Amount, 0.0f, 1.0f);
		P->ChannelWeights = Settings.ChannelWeights;
		P->ClampCoverage = Settings.bClampCoverage ? 1u : 0u;
		P->ScalarSource = Field == EField::Scalar ? Source : nullptr;
		P->VectorSource = Field != EField::Scalar ? Source : nullptr;
		P->MaskTexture = Mask;
		P->DirectionFlow = DirectionFlow;
		P->DirectionValidity = DirectionValidity;
		P->SourceValidity = SourceValidity;
		P->ScalarOutput = Field == EField::Scalar ? Graph.CreateUAV(Dest) : nullptr;
		P->VectorOutput = Field != EField::Scalar ? Graph.CreateUAV(Dest) : nullptr;
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Graph,
			RDG_EVENT_NAME("Mixtormat.DirectionalBlur.Mode%d.Type%d", static_cast<int32>(Blur.Mode),
				static_cast<int32>(Field)), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(Size.X, GPU::ThreadGroupSize),
				FMath::DivideAndRoundUp(Size.Y, GPU::ThreadGroupSize), 1));
		return Dest;
	}
}
