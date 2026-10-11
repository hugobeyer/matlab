// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuNativeFlowPasses.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

namespace MixtormatGpuCompositor
{
namespace
{
	class FMixtormatNativeFlowCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatNativeFlowCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatNativeFlowCS, FGlobalShader);

		class FPackedBoundary : SHADER_PERMUTATION_BOOL("MIXTORMAT_NATIVE_FLOW_PACKED_BOUNDARY");
		using FPermutationDomain = TShaderPermutationDomain<FPackedBoundary>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SignedField)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
			SHADER_PARAMETER(uint32, HasBoundaryRegions)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, BoundaryRegionIds)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlow)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutFlowValidity)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FMixtormatNativeFlowCS,
		"/Plugin/Mixtormat/Private/MixtormatNativeFlow.usf", "MainCS", SF_Compute);
}

FPublishedField AddNativeFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	const int32 SourceChildIndex, FRDGTextureRef Source, const ENativeFlowSource SourceKind,
	FRDGTextureRef BoundaryRegionIds)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	const bool bSignedHeight = SourceKind == ENativeFlowSource::SignedHeight;
	const bool bSignedBoundary = SourceKind == ENativeFlowSource::SignedBoundary;
	const bool bPackedBoundary = bSignedBoundary && Source && Source->Desc.Format == PF_G32R32F;
	if (!Source || Source->Desc.Extent != Size || (!bSignedHeight && !bSignedBoundary)
		|| (Source->Desc.Format != PF_R32_FLOAT && !bPackedBoundary)
		|| (BoundaryRegionIds && (!bPackedBoundary || BoundaryRegionIds->Desc.Extent != Size
			|| BoundaryRegionIds->Desc.Format != PF_R32_UINT)))
	{
		return FPublishedField{EMixtormatPublishedFieldKind::Flow};
	}

	FRDGTextureRef Flow = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, GetPublishedFieldFormat(EMixtormatPublishedFieldKind::Flow),
		FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.NativeFlow.Flow"));
	FRDGTextureRef Validity = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.NativeFlow.Validity"));
	FMixtormatNativeFlowCS::FPermutationDomain Permutation;
	Permutation.Set<FMixtormatNativeFlowCS::FPackedBoundary>(bPackedBoundary);
	TShaderMapRef<FMixtormatNativeFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatNativeFlowCS::FParameters>();
	P->OutputSize = Size;
	P->SignedField = bPackedBoundary ? nullptr : Source;
	P->BoundaryField = bPackedBoundary ? Source : nullptr;
	P->HasBoundaryRegions = BoundaryRegionIds ? 1u : 0u;
	P->BoundaryRegionIds = bPackedBoundary
		? (BoundaryRegionIds ? BoundaryRegionIds : Ctx.EmptyRegionIds) : nullptr;
	P->OutFlow = Ctx.GraphBuilder.CreateUAV(Flow);
	P->OutFlowValidity = Ctx.GraphBuilder.CreateUAV(Validity);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.NativeFlow.C%d", SourceChildIndex), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
	const FPublishedField Field{EMixtormatPublishedFieldKind::Flow, Flow, Flow, Validity, false};
	Ctx.PublishedFieldOutputs.Add(
		PublishedKey(Layer, SourceChildIndex, FName(TEXT("FlowDirection"))), Field);
	return Field;
}
}
