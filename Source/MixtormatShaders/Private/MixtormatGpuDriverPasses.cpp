// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatScalarRampMath.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

namespace MixtormatGpuCompositor
{
	class FMixtormatDriverTransferCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatDriverTransferCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatDriverTransferCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, UseRegion)
			SHADER_PARAMETER(uint32, RegionSeed)
			SHADER_PARAMETER(FVector2f, RegionRange)
			SHADER_PARAMETER(FVector4f, InputMapping)
			SHADER_PARAMETER(FVector2f, CurveDomain)
			SHADER_PARAMETER(uint32, CurveCount)
			SHADER_PARAMETER(uint32, CurveInterpolation)
			SHADER_PARAMETER_ARRAY(FVector4f, CurvePoints, [FMixtormatScalarRamp::MaxPoints])
			SHADER_PARAMETER(uint32, EnumBinCount)
			SHADER_PARAMETER_ARRAY(FVector4f, EnumBins, [FMixtormatParameterDriver::MaxEnumBins])
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceSignal)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceRegionIds)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSignal)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{ return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
			Environment.SetDefine(TEXT("MIXTORMAT_DRIVER_MAX_ENUM_BINS"), FMixtormatParameterDriver::MaxEnumBins);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatDriverTransferCS, "/Plugin/Mixtormat/Private/MixtormatDriverTransfer.usf", "MainCS", SF_Compute);

	FRDGTextureRef AddDriverTransferPass(FMixtormatComposeContext& Ctx, const FScalarDriverRenderData& Driver,
		FRDGTextureRef Signal, FRDGTextureRef RegionIds)
	{
		if (!Driver.NeedsSignalTransform()) { return Signal; }
		if ((!Driver.bRegionSource && !Signal) || (Driver.bRegionSource && !RegionIds)) { return nullptr; }
		const FDriverTransferKey Key{&Driver, Signal, RegionIds};
		if (const FRDGTextureRef* Existing = Ctx.DriverTransferSignals.Find(Key)) { return *Existing; }
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Ctx.Request.Resolution,
			PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Driver.Transfer"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatDriverTransferCS::FParameters>();
		P->OutputSize = Ctx.Request.Resolution;
		P->UseRegion = Driver.bRegionSource ? 1u : 0u;
		P->RegionSeed = Driver.Seed;
		P->RegionRange = FVector2f(Driver.IdRandomMin, Driver.IdRandomMax);
		P->InputMapping = FVector4f(Driver.bInvert ? 1.0f : 0.0f, Driver.InputMin, Driver.InputMax, 0.0f);
		P->CurveDomain = FVector2f(Driver.TransferCurve.DomainMin, Driver.TransferCurve.DomainMax);
		const auto Curve = MixtormatScalarRampMath::PrepareGpuPayload(Driver.TransferCurve);
		P->CurveCount = Driver.bUseTransferCurve ? Curve.PointCount : 0u;
		P->CurveInterpolation = Curve.Interpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index) { P->CurvePoints[Index] = Curve.Points[Index]; }
		P->EnumBinCount = Driver.bEnumBins ? static_cast<uint32>(Driver.EnumBins.Num()) : 0u;
		for (int32 Index = 0; Index < FMixtormatParameterDriver::MaxEnumBins; ++Index)
		{
			P->EnumBins[Index] = Driver.EnumBins.IsValidIndex(Index)
				? FVector4f(Driver.EnumBins[Index].UpperBound, static_cast<float>(Driver.EnumBins[Index].Value), 0.0f, 0.0f) : FVector4f::Zero();
		}
		P->SourceSignal = Signal ? Signal : Ctx.EmptyDriverSignal;
		P->SourceRegionIds = RegionIds ? RegionIds : Ctx.EmptyRegionIds;
		P->OutSignal = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatDriverTransferCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder, RDG_EVENT_NAME("Mixtormat.Driver.Transfer"), Shader, P,
			FIntVector(FMath::DivideAndRoundUp(P->OutputSize.X, 8), FMath::DivideAndRoundUp(P->OutputSize.Y, 8), 1));
		Ctx.DriverTransferSignals.Add(Key, Result);
		return Result;
	}
}
