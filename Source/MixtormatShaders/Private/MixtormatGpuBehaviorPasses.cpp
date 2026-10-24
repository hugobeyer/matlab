// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuBehaviorPasses.h"
#include "MixtormatGpuGeneratorCommon.h"
#include "MixtormatGpuNoisePasses.h"
#include "MixtormatGpuGaussianBlur.h"

#include "GlobalShader.h"
#include "Misc/ScopeExit.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

namespace MixtormatGpuCompositor
{

class FMixtormatBehaviorFlowFeatherCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorFlowFeatherCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorFlowFeatherCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("FLOW_FEATHER_STAGE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, DirectionBias)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, GaussianFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Validity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutFlow)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArrival)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorFlowFeatherCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorFlowFeather.usf", "MainCS", SF_Compute);

class FMixtormatBehaviorFeatherCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorFeatherCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorFeatherCS, FGlobalShader);
	class FStage : SHADER_PERMUTATION_INT("FEATHER_STAGE", 9);
	using FPermutationDomain = TShaderPermutationDomain<FStage>;
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER(float, DirectionBias)
		SHADER_PARAMETER(uint32, Side)
		SHADER_PARAMETER(uint32, SideDriverEnabled)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SideDriverSignal)
		SHADER_PARAMETER(uint32, HasBoundary)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, GaussianHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BoundaryField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, MaskField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, Arrival)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, FeatherEnvelope)
		SHADER_PARAMETER(int32, FeatherJump)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutArrival)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutFeatherEnvelope)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorFeatherCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorFeather.usf", "MainCS", SF_Compute);

class FMixtormatBehaviorUvBlendCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorUvBlendCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorUvBlendCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SourceCoordinates)
		// Per-field composition for the Direction socket. Amplitude scales this
		// socket's weight; Reverse flips the transported displacement.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorUvBlendCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "MainCS", SF_Compute);

class FMixtormatBehaviorModeMixCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorModeMixCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorModeMixCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, ModeMix)
		SHADER_PARAMETER(uint32, HasStandard)
		SHADER_PARAMETER(uint32, HasTraced)
		SHADER_PARAMETER(FVector4f, ModeMixDriverParamsA)
		SHADER_PARAMETER(FVector4f, ModeMixDriverParamsB)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ModeMixDriverSignal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, StandardCoordinates)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, TracedCoordinates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorModeMixCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "ModeMixCS", SF_Compute);

class FMixtormatBehaviorHeightGradientCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorHeightGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorHeightGradientCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, GradientReach)
		SHADER_PARAMETER(uint32, SmoothHeightGradient)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		// Per-field composition for the Direction socket on the local-gradient path.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCoordinates)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorHeightGradientCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "HeightGradientCS", SF_Compute);

class FMixtormatBehaviorSignedPushCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorSignedPushCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorSignedPushCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorHeight)
		// Per-field composition for the Height socket: amplitude, sign and the
		// signed-scalar combine. Gather admits Blend only where one was authored.
		SHADER_PARAMETER(float, FieldAmplitude)
		SHADER_PARAMETER(uint32, FieldReversed)
		SHADER_PARAMETER(uint32, FieldBlend)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSignedHeight)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorSignedPushCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorWarp.usf", "SignedPushCS", SF_Compute);

class FMixtormatBehaviorSculptCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBehaviorSculptCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBehaviorSculptCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, TraceLength)
		SHADER_PARAMETER(int32, TraceSteps)
		SHADER_PARAMETER(float, Falloff)
		SHADER_PARAMETER(uint32, UseMask)
		SHADER_PARAMETER(uint32, UseInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, WorkingFlow)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WorkingFlowValidity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ScopedMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InfluenceField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, BehaviorDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BehaviorDriverSignal1)
		SHADER_PARAMETER_ARRAY(FVector4f, SculptDriverParamsA, [2])
		SHADER_PARAMETER_ARRAY(FVector4f, SculptDriverParamsB, [2])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SculptDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SculptDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutSculptMask)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatBehaviorSculptCS,
	"/Plugin/Mixtormat/Private/MixtormatBehaviorSculpt.usf", "MainCS", SF_Compute);

class FMixtormatCanonicalFlowTraceCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCanonicalFlowTraceCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCanonicalFlowTraceCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, TraceLength)
		SHADER_PARAMETER(float, WarpStrength)
		SHADER_PARAMETER(int32, Steps)
		SHADER_PARAMETER_ARRAY(FVector4f, FlowDriverParamsA, [3])
		SHADER_PARAMETER_ARRAY(FVector4f, FlowDriverParamsB, [3])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowDriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowDriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowDriverSignal2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FlowField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FlowValidity)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutWarpedUV)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMixtormatCanonicalFlowTraceCS,
	"/Plugin/Mixtormat/Private/MixtormatFlowTrace.usf", "MainCS", SF_Compute);

FRDGTextureRef AddReferencedFlowUVPass(FMixtormatComposeContext& Ctx,
	const FOutputReferenceRenderData& Reference, const FPublishedField& Field,
	const int32 LayerIndex, const int32 ChildIndex,
	const FScalarDriverRenderData* Drivers, const bool bCanonical)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Coordinates = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.OutputReference.FlowUV"));
	auto* P = GraphBuilder.AllocParameters<FMixtormatCanonicalFlowTraceCS::FParameters>();
	P->OutputSize = Size;
	P->TraceLength = Reference.FlowTraceLength;
	P->WarpStrength = Reference.FlowAmount;
	P->Steps = FMath::Clamp(Reference.FlowSteps, 1, 64);
	FRDGTextureRef Signals[3] = {Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal};
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const FScalarDriverRenderData* Driver = Drivers ? &Drivers[Slot] : nullptr;
		FRDGTextureRef* Snapshot = Driver && Driver->bEnabled
			? Ctx.DriverSnapshots.Find(Driver->SourceLayerId) : nullptr;
		const bool bResolved = Snapshot != nullptr;
		Signals[Slot] = bResolved ? AddDriverTransferPass(Ctx, *Driver, *Snapshot) : Ctx.EmptyDriverSignal;
		P->FlowDriverParamsA[Slot] = FVector4f(Driver ? DriverEnableValue(*Driver, bResolved) : 0.0f,
			Driver && Driver->bInvert ? 1.0f : 0.0f,
			Driver ? Driver->InputMin : 0.0f, Driver ? Driver->InputMax : 1.0f);
		P->FlowDriverParamsB[Slot] = FVector4f(
			Driver ? Driver->OutputMin : 0.0f, Driver ? Driver->OutputMax : 1.0f,
			Driver ? Driver->Amount : 0.0f,
			Driver ? static_cast<float>(Driver->Combine) : 0.0f);
	}
	P->FlowDriverSignal0 = Signals[0];
	P->FlowDriverSignal1 = Signals[1];
	P->FlowDriverSignal2 = Signals[2];
	P->FlowField = Field.Texture;
	P->FlowValidity = Field.Validity ? Field.Validity : Ctx.EmptyDriverSignal;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	P->OutWarpedUV = GraphBuilder.CreateUAV(Coordinates);
	TShaderMapRef<FMixtormatCanonicalFlowTraceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.CanonicalFlow.Trace.L%d.C%d", LayerIndex, ChildIndex),
		Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
			FMath::DivideAndRoundUp(Size.Y, 8), 1));
	return Coordinates;
}

namespace
{
	template<typename TParameters>
	void SetBehaviorScalarDrivers(FMixtormatComposeContext& Ctx,
		TParameters* P, const FScalarDriverRenderData* Drivers)
	{
		FRDGTextureRef Signals[2] = { Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal };
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FScalarDriverRenderData& Driver = Drivers[Slot];
			FRDGTextureRef Signal = nullptr;
			if (Driver.bEnabled)
			{
				if (FRDGTextureRef* Found = Ctx.DriverSnapshots.Find(Driver.SourceLayerId))
				{
					Signal = *Found;
				}
			}
			const bool bResolved = Signal != nullptr;
			Signals[Slot] = bResolved ? AddDriverTransferPass(Ctx, Driver, Signal) : Ctx.EmptyDriverSignal;
			P->BehaviorDriverParamsA[Slot] = FVector4f(
				DriverEnableValue(Driver, bResolved), Driver.bInvert ? 1.0f : 0.0f,
				Driver.InputMin, Driver.InputMax);
			P->BehaviorDriverParamsB[Slot] = FVector4f(
				Driver.OutputMin, Driver.OutputMax, Driver.Amount,
				static_cast<float>(Driver.Combine));
		}
		P->BehaviorDriverSignal0 = Signals[0];
		P->BehaviorDriverSignal1 = Signals[1];
	}

	FRDGTextureRef ScaleBehaviorUV(FMixtormatComposeContext& Ctx,
		FRDGTextureRef Source, const float Strength, FRDGTextureRef Mask,
		const bool bUseMask, FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex,
		const FScalarDriverRenderData* Drivers,
		const FBehaviorFieldComposition& Composition)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		const bool bCompositionNeutral = Composition.Amplitude == 1.0f
			&& Composition.bReversed == 0;
		if (Strength == 1.0f && !bUseMask && !bUseInfluence
			&& !Drivers[0].bEnabled && bCompositionNeutral) { return Source; }
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.WarpCoordinates"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorUvBlendCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Strength;
		P->FieldAmplitude = Composition.Amplitude;
		P->FieldReversed = Composition.bReversed;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceCoordinates = Source;
		SetBehaviorScalarDrivers(Ctx, P, Drivers);
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorUvBlendCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.WarpUV.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef MixBehaviorWarpCoordinates(FMixtormatComposeContext& Ctx,
		FRDGTextureRef Standard, FRDGTextureRef Traced,
		const FBehaviorRenderData& Behavior, const int32 LayerIndex, const int32 ChildIndex)
	{
		const float Mix = FMath::Clamp(Behavior.Flow.ModeMix, 0.0f, 1.0f);
		const FScalarDriverRenderData& Driver = Behavior.Flow.SettingsDrivers[8];
		if (!Driver.bEnabled)
		{
			if (Mix <= 0.0f) { return Standard; }
			if (Mix >= 1.0f) { return Traced; }
		}
		if (!Standard && !Traced) { return nullptr; }
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.ModeMixUV"));
		TShaderMapRef<FMixtormatBehaviorModeMixCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorModeMixCS::FParameters>();
		P->OutputSize = Size;
		P->ModeMix = Mix;
		P->HasStandard = Standard ? 1u : 0u;
		P->HasTraced = Traced ? 1u : 0u;
		P->StandardCoordinates = Standard ? Standard : (Traced ? Traced : Ctx.EmptyPatternUV);
		P->TracedCoordinates = Traced ? Traced : (Standard ? Standard : Ctx.EmptyPatternUV);
		FRDGTextureRef* Signal = Driver.bEnabled ? Ctx.DriverSnapshots.Find(Driver.SourceLayerId) : nullptr;
		const bool bResolved = Signal != nullptr;
		P->ModeMixDriverSignal = bResolved ? AddDriverTransferPass(Ctx, Driver, *Signal) : Ctx.EmptyDriverSignal;
		P->ModeMixDriverParamsA = FVector4f(DriverEnableValue(Driver, bResolved),
			Driver.bInvert ? 1.0f : 0.0f, Driver.InputMin, Driver.InputMax);
		P->ModeMixDriverParamsB = FVector4f(Driver.OutputMin, Driver.OutputMax, Driver.Amount,
			static_cast<float>(Driver.Combine));
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.ModeMix.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef MakeBehaviorHeightGradientUV(FMixtormatComposeContext& Ctx,
		FRDGTextureRef SourceHeight, const float Strength, const float Reach,
		const bool bSmoothHeightGradient,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex,
		const FScalarDriverRenderData* Drivers,
		const FBehaviorFieldComposition& Composition)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.HeightGradientUV"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorHeightGradientCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Strength;
		P->FieldAmplitude = Composition.Amplitude;
		P->FieldReversed = Composition.bReversed;
		P->GradientReach = Reach;
		P->SmoothHeightGradient = bSmoothHeightGradient ? 1u : 0u;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceHeight = SourceHeight;
		SetBehaviorScalarDrivers(Ctx, P, Drivers);
		P->OutCoordinates = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorHeightGradientCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.HeightGradient.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef AddBehaviorSignedPushPass(FMixtormatComposeContext& Ctx,
		FRDGTextureRef BaseHeight, FRDGTextureRef DeltaHeight, const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		AddBehaviorMaskDiagnosticPass(Ctx, LayerIndex, ChildIndex, Mask, bUseMask, Influence, bUseInfluence);
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Behavior.SignedPush"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorSignedPushCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Behavior.Strength;
		P->UseMask = bUseMask ? 1u : 0u;
		P->ScopedMask = Mask;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->InfluenceField = Influence;
		P->SourceHeight = BaseHeight;
		P->BehaviorHeight = DeltaHeight;
		P->FieldAmplitude = Behavior.HeightComposition.Amplitude;
		P->FieldReversed = Behavior.HeightComposition.bReversed;
		P->FieldBlend = Behavior.HeightComposition.Blend;
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		P->OutSignedHeight = Ctx.GraphBuilder.CreateUAV(Result);
		TShaderMapRef<FMixtormatBehaviorSignedPushCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.Push.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		return Result;
	}

	FRDGTextureRef AddBehaviorSculptPass(FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer, FRDGTextureRef Height,
		FRDGTextureRef Flow, FRDGTextureRef FlowValidity,
		const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask, const bool bUseMask,
		FRDGTextureRef Influence, const bool bUseInfluence,
		const int32 LayerIndex, const int32 ChildIndex)
	{
		const FIntPoint Size = Ctx.Request.Resolution;
		FRDGTextureRef Result = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R32_FLOAT, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Behavior.Sculpt.Height"));
		FRDGTextureRef SculptMask = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_R16F, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Behavior.Sculpt.Mask"));
		auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatBehaviorSculptCS::FParameters>();
		P->OutputSize = Size;
		P->Strength = Behavior.Strength;
		P->Depth = Behavior.Flow.Depth;
		P->Width = FMath::Max(0.0f, Behavior.Flow.Width);
		P->TraceLength = FMath::Max(0.0f, Behavior.Flow.TraceLength);
		P->TraceSteps = FMath::Clamp(Behavior.Flow.TraceSteps, 1, 256);
		P->Falloff = FMath::Max(0.0f, Behavior.Flow.Falloff);
		P->UseMask = bUseMask ? 1u : 0u;
		P->UseInfluence = bUseInfluence ? 1u : 0u;
		P->SourceHeight = Height;
		P->WorkingFlow = Flow;
		P->WorkingFlowValidity = FlowValidity;
		P->ScopedMask = Mask ? Mask : Ctx.EmptyDriverSignal;
		P->InfluenceField = Influence ? Influence : Ctx.EmptyDriverSignal;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		const int32 Slots[2] = {1, 3};
		FRDGTextureRef Signals[2] = {Ctx.EmptyDriverSignal, Ctx.EmptyDriverSignal};
		for (int32 I = 0; I < 2; ++I)
		{
			const FScalarDriverRenderData& Driver = Behavior.Flow.SettingsDrivers[Slots[I]];
			FRDGTextureRef* Snapshot = Driver.bEnabled
				? Ctx.DriverSnapshots.Find(Driver.SourceLayerId) : nullptr;
			const bool bResolved = Snapshot != nullptr;
			Signals[I] = bResolved ? AddDriverTransferPass(Ctx, Driver, *Snapshot) : Ctx.EmptyDriverSignal;
			P->SculptDriverParamsA[I] = FVector4f(
				DriverEnableValue(Driver, bResolved), Driver.bInvert ? 1.0f : 0.0f,
				Driver.InputMin, Driver.InputMax);
			P->SculptDriverParamsB[I] = FVector4f(
				Driver.OutputMin, Driver.OutputMax, Driver.Amount,
				static_cast<float>(Driver.Combine));
		}
		P->SculptDriverSignal0 = Signals[0];
		P->SculptDriverSignal1 = Signals[1];
		P->OutHeight = Ctx.GraphBuilder.CreateUAV(Result);
		P->OutSculptMask = Ctx.GraphBuilder.CreateUAV(SculptMask);
		TShaderMapRef<FMixtormatBehaviorSculptCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Behavior.Sculpt.L%d.C%d", LayerIndex, ChildIndex),
			Shader, P, FIntVector(FMath::DivideAndRoundUp(Size.X, 8),
				FMath::DivideAndRoundUp(Size.Y, 8), 1));
		Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, ChildIndex, FName(TEXT("CarveMask"))),
			FPublishedField{EMixtormatPublishedFieldKind::Scalar01, SculptMask, nullptr, nullptr, false});
		return Result;
	}
}

FRDGTextureRef BuildGeneratorPreCoordinates(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Owner)
{
	FRDGTextureRef Current = nullptr;
	const FIntPoint Size = Ctx.Request.Resolution;
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::Behavior
			|| Child.Behavior.GeneratorChildIndex != Owner.SourceChildIndex
			|| Child.Behavior.Stage != EMixtormatBehaviorStage::PreGeneration
			|| Child.Behavior.Type != EMixtormatBehaviorType::Warp
			|| Child.Behavior.DirectionOrigin != EMixtormatBehaviorFieldOrigin::PublishedOutput) { continue; }
		const FOutputReferenceRenderData& Ref = Child.Behavior.Direction;
		const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Ref.Source);
		if (!Field || !Field->IsComplete() || Field->Kind != Ref.Kind
			|| Field->Texture->Desc.Extent != Size) { continue; }
		FRDGTextureRef Influence = LayerCtx.CombinedMask;
		const bool bInfluence = Child.Behavior.bHasInfluence;
		if (bInfluence)
		{
			const FPublishedField* I = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Influence.Source);
			if (!I || !I->IsComplete() || I->Kind != EMixtormatPublishedFieldKind::Scalar01
				|| I->Texture->Desc.Extent != Size) { continue; }
			Influence = I->Texture;
		}
		FRDGTextureRef UV = nullptr;
		if (Ref.Kind == EMixtormatPublishedFieldKind::UVMap) { UV = Field->Texture; }
		else if (Ref.Kind == EMixtormatPublishedFieldKind::Flow)
		{
			if ((Ref.FlowAmount == 0.0f && !Child.Behavior.FlowDrivers[0].bEnabled)
				|| (Ref.FlowTraceLength == 0.0f && !Child.Behavior.FlowDrivers[1].bEnabled)) { continue; }
			UV = AddReferencedFlowUVPass(Ctx, Ref, *Field, LayerCtx.LayerIndex,
				Child.SourceChildIndex, Child.Behavior.FlowDrivers);
		}
		if (!UV) { continue; }
		const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
		FRDGTextureRef Gate = bMask
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
			: LayerCtx.CombinedMask;
		UV = ScaleBehaviorUV(Ctx, UV, Child.Behavior.Strength, Gate, bMask, Influence,
			bInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
			Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
		Current = Current ? RemapBundleField(Ctx, Current, UV, 3) : UV;
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Current, nullptr, nullptr, false});
	}
	return Current;
}

FPublishedField AddFlowFeatherPasses(FMixtormatComposeContext& Ctx,
	const FPublishedField& Source, const FBehaviorRenderData& Behavior,
	FRDGTextureRef Mask, const bool bHasMask, const int32 LayerIndex, const int32 ChildIndex)
{
	if (!Source.IsComplete() || Source.Kind != EMixtormatPublishedFieldKind::Flow
		|| (Behavior.Strength == 0.0f && !Behavior.ScalarDrivers[0].bEnabled)
		|| Behavior.FeatherRadius <= 0.0f)
	{
		return Source;
	}
	FRDGBuilder& Graph = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	if (Source.Texture->Desc.Extent != Size) { return Source; }
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8),
		FMath::DivideAndRoundUp(Size.Y, 8), 1);
	const auto NewScalar = [&Graph, Size](const TCHAR* Name)
	{
		return Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, PF_R32_FLOAT,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	const bool bEikonal = Behavior.FeatherMode == EMixtormatBehaviorFeatherMode::Eikonal;
	FRDGTextureRef Arrival[2] = {nullptr, nullptr};
	FRDGTextureRef GaussianFlow = nullptr;
	int32 Read = 0;
	const auto Dispatch = [&](int32 Stage, FRDGTextureRef Out, FRDGTextureRef InArrival)
	{
		FMixtormatBehaviorFlowFeatherCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatBehaviorFlowFeatherCS::FStage>(Stage);
		TShaderMapRef<FMixtormatBehaviorFlowFeatherCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = Graph.AllocParameters<FMixtormatBehaviorFlowFeatherCS::FParameters>();
		P->OutputSize = Size;
		P->Radius = Behavior.FeatherRadius;
		P->Strength = Behavior.Strength;
		P->DirectionBias = Behavior.FeatherDirectionBias;
		P->HasMask = bHasMask && Mask ? 1u : 0u;
		P->SourceFlow = Source.Texture;
		P->GaussianFlow = Stage == 0 ? GaussianFlow : nullptr;
		P->Validity = Source.Validity;
		P->MaskField = bHasMask && Mask ? Mask : Ctx.EmptyDriverSignal;
		P->Arrival = InArrival ? InArrival : Ctx.EmptyDriverSignal;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		if (Stage == 0 || Stage == 3 || Stage == 4) { P->OutFlow = Graph.CreateUAV(Out); }
		else { P->OutArrival = Graph.CreateUAV(Out); }
		ClearUnusedGraphResources(Shader, P);
		FComputeShaderUtils::AddPass(Graph,
			RDG_EVENT_NAME("Mixtormat.FlowFeather.L%d.C%d.Stage%d", LayerIndex, ChildIndex, Stage),
			Shader, P, Groups);
	};
	if (bEikonal)
	{
		Arrival[0] = NewScalar(TEXT("Mixtormat.FlowFeather.ArrivalA"));
		Arrival[1] = NewScalar(TEXT("Mixtormat.FlowFeather.ArrivalB"));
		Dispatch(1, Arrival[0], nullptr);
		const int32 Iterations = ResolveFeatherIterations(Behavior.FeatherIterations);
		for (int32 Pass = 0; Pass < Iterations; ++Pass)
		{
			Dispatch(2, Arrival[1 - Read], Arrival[Read]);
			Read = 1 - Read;
		}
	}
	if (!bEikonal)
	{
		GaussianFlow = Graph.CreateTexture(FRDGTextureDesc::Create2D(
			Size, PF_A32B32G32R32F, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.FlowFeather.GaussianX"));
		Dispatch(4, GaussianFlow, nullptr);
	}
	FRDGTextureRef OutFlow = Graph.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_FloatRGBA, FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.FlowFeather.Filtered"));
	Dispatch(bEikonal ? 3 : 0, OutFlow, bEikonal ? Arrival[Read] : nullptr);
	return FPublishedField{EMixtormatPublishedFieldKind::Flow,
		OutFlow, OutFlow, Source.Validity, false};
}

FRDGTextureRef AddGeneratorFeatherPasses(FMixtormatComposeContext& Ctx,
	FRDGTextureRef Height, FRDGTextureRef Boundary,
	const FBehaviorRenderData& Behavior, FRDGTextureRef Mask, const bool bHasMask,
	const int32 LayerIndex, const int32 ChildIndex)
{
	if (!Height || Height->Desc.Format != PF_R32_FLOAT
		|| (Behavior.Strength == 0.0f && !Behavior.ScalarDrivers[0].bEnabled)
		|| Behavior.FeatherRadius <= 0.0f)
	{
		return Height;
	}
	FRDGBuilder& Graph = Ctx.GraphBuilder;
	const FIntPoint Size = Ctx.Request.Resolution;
	const bool bBoundary = Boundary && Boundary->Desc.Extent == Size
		&& Boundary->Desc.Format == PF_G32R32F;
	const FIntVector Groups(FMath::DivideAndRoundUp(Size.X, 8),
		FMath::DivideAndRoundUp(Size.Y, 8), 1);
	const auto Make = [&Graph, Size](const TCHAR* Name, EPixelFormat Format)
	{
		return Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	constexpr bool bEikonal = true;
	FRDGTextureRef GaussianHeight = nullptr;
	const auto Dispatch = [&](const int32 Stage, FRDGTextureRef Output,
		FRDGTextureRef InputEnvelope, const int32 Jump)
	{
		FMixtormatBehaviorFeatherCS::FPermutationDomain Permutation;
		Permutation.Set<FMixtormatBehaviorFeatherCS::FStage>(Stage);
		TShaderMapRef<FMixtormatBehaviorFeatherCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), Permutation);
		auto* P = Graph.AllocParameters<FMixtormatBehaviorFeatherCS::FParameters>();
		P->OutputSize = Size;
		P->Radius = Behavior.FeatherRadius;
		P->Strength = Behavior.Strength;
		SetBehaviorScalarDrivers(Ctx, P, Behavior.ScalarDrivers);
		P->DirectionBias = Behavior.FeatherDirectionBias;
		P->Side = static_cast<uint32>(Behavior.FeatherSide);
		const auto& SideDriver = Behavior.FeatherSideDriver;
		FRDGTextureRef* SideSignal = SideDriver.bEnabled ? Ctx.DriverSnapshots.Find(SideDriver.SourceLayerId) : nullptr;
		P->SideDriverEnabled = SideSignal ? 1u : 0u;
		P->SideDriverSignal = SideSignal ? AddDriverTransferPass(Ctx, SideDriver, *SideSignal) : Ctx.EmptyDriverSignal;
		P->HasBoundary = bBoundary ? 1u : 0u;
		P->HasMask = bHasMask && Mask ? 1u : 0u;
		P->SourceHeight = Height;
		P->GaussianHeight = Stage == 0 ? GaussianHeight : nullptr;
		P->BoundaryField = bBoundary ? Boundary : Ctx.EmptyPatternUV;
		P->MaskField = bHasMask && Mask ? Mask : Ctx.EmptyDriverSignal;
		P->Arrival = Ctx.EmptyDriverSignal;
		P->FeatherEnvelope = InputEnvelope ? InputEnvelope : Ctx.EmptyPatternUV;
		P->FeatherJump = Jump;
		P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		if (Stage == 0 || Stage == 7) { P->OutHeight = Graph.CreateUAV(Output); }
		else { P->OutFeatherEnvelope = Graph.CreateUAV(Output); }
		ClearUnusedGraphResources(Shader, P);
		const FIntVector DispatchGroups = Stage == 4
			? FIntVector(FMath::DivideAndRoundUp(Size.Y, 64), 1, 1)
			: Stage == 5
				? FIntVector(FMath::DivideAndRoundUp(Size.X, 64), 1, 1)
				: Groups;
		FComputeShaderUtils::AddPass(Graph,
			RDG_EVENT_NAME("Mixtormat.Feather.L%d.C%d.Stage%d", LayerIndex, ChildIndex, Stage),
			Shader, P, DispatchGroups);
	};
	FRDGTextureRef Out = Make(TEXT("Mixtormat.Feather.NativeHeight"), PF_R32_FLOAT);
	if (!bEikonal)
	{
		GaussianHeight = Make(TEXT("Mixtormat.Feather.GaussianX"), PF_G32R32F);
		Dispatch(8, GaussianHeight, nullptr, 0);
		Dispatch(0, Out, nullptr, 0);
		return Out;
	}
	FRDGTextureRef Envelope[2] = {
		Make(TEXT("Mixtormat.Feather.EnvelopeA"), PF_G32R32F),
		Make(TEXT("Mixtormat.Feather.EnvelopeB"), PF_G32R32F)
	};
	Dispatch(4, Envelope[0], nullptr, 0);
	Dispatch(5, Envelope[1], Envelope[0], 0);
	int32 Read = 1;
	const int32 Budget = ResolveFeatherIterations(Behavior.FeatherIterations);
	const int32 MaxJump = FMath::Max(1, FMath::Min(
		FMath::CeilToInt(Behavior.FeatherRadius), FMath::Max(Size.X, Size.Y) / 2));
	int32 Jump = 1;
	while (Jump <= MaxJump / 2) { Jump *= 2; }
	int32 Corrections = 0;
	for (; Jump >= 1 && Corrections < Budget; Jump /= 2, ++Corrections)
	{
		Dispatch(6, Envelope[1 - Read], Envelope[Read], Jump);
		Read = 1 - Read;
	}
	for (int32 I = 0; I < FMath::Min(Budget - Corrections, 2); ++I)
	{
		Dispatch(6, Envelope[1 - Read], Envelope[Read], 1);
		Read = 1 - Read;
	}
	Dispatch(7, Out, Envelope[Read], 0);
	return Out;
}

void ApplyGeneratorPostBehaviors(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Owner, FGeneratorBundle& Module, FRDGTextureRef PreviousRunningHeight,
	TSet<int32>* CompletedScopes)
{
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef WorkingFlow = Ctx.GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Size, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("Mixtormat.Generator.WorkingFlow"));
	AddClearUAVPass(Ctx.GraphBuilder, Ctx.GraphBuilder.CreateUAV(WorkingFlow), FLinearColor::Black);
	FRDGTextureRef WorkingFlowValidity = nullptr;
	const FPublishedField* Intrinsic = Ctx.PublishedFieldOutputs.Find(
		PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("FlowDirection"))));
	if (Intrinsic && Intrinsic->IsComplete()
		&& Intrinsic->Kind == EMixtormatPublishedFieldKind::Flow
		&& Intrinsic->Texture->Desc.Extent == Size)
	{
		WorkingFlow = Intrinsic->Texture;
		WorkingFlowValidity = Intrinsic->Validity;
	}
	const auto PublishFlowSnapshot = [&](const int32 ChildIndex)
	{
		if (!WorkingFlow || !WorkingFlowValidity) { return; }
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, ChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow),
			FPublishedField{EMixtormatPublishedFieldKind::Flow, WorkingFlow,
				WorkingFlow, WorkingFlowValidity, false});
	};
	ON_SCOPE_EXIT { PublishFlowSnapshot(Owner.SourceChildIndex); };
	for (const FChildRenderData& Child : Layer.Children)
	{
		const bool bScopedNoise = Child.Type == EMixtormatLayerChildType::Generator
			&& Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex
			&& Child.Generator.Type == EMixtormatGeneratorType::Noise;
		const bool bScopedBehavior = Child.Type == EMixtormatLayerChildType::Behavior
			&& Child.Behavior.GeneratorChildIndex == Owner.SourceChildIndex
			&& Child.Behavior.Stage == EMixtormatBehaviorStage::PostGeneration;
		bool bPublishWorkingFlow = true;
		ON_SCOPE_EXIT
		{
			if (bPublishWorkingFlow && (bScopedNoise || bScopedBehavior))
			{
				PublishFlowSnapshot(Child.SourceChildIndex);
			}
		};
		if (bScopedNoise)
		{
			FMixtormatNoiseRenderData Noise;
			if (!MixtormatNoiseRenderStore().Find(
				FMixtormatNoiseRenderKey{Layer.LayerId, Child.SourceChildIndex}, Noise)) { continue; }
			if (CompletedScopes) { CompletedScopes->Add(Child.SourceChildIndex); }
			if (!Noise.bWriteFlow && !Noise.bWriteHeight) { continue; }
			FRDGTextureRef ParentUV = nullptr;
			if (const FPublishedField* RootUVField = Ctx.PublishedFieldOutputs.Find(
				PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("RootUV")))))
			{
				if (RootUVField->IsComplete() && RootUVField->Kind == EMixtormatPublishedFieldKind::UVMap)
				{
					ParentUV = RootUVField->Texture;
				}
			}
			if (!ParentUV)
			{
				if (const FPublishedField* WarpedField = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Owner.SourceChildIndex, FName(TEXT("WarpedUV")))))
				{
					if (WarpedField->IsComplete() && WarpedField->Kind == EMixtormatPublishedFieldKind::UVMap)
					{
						ParentUV = WarpedField->Texture;
					}
				}
			}
			FGeneratorBundle ScopedBundle;
			AddNoisePasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, &ScopedBundle,
				ParentUV, Module.Height);
			for (const FChildRenderData& Filter : Layer.Children)
			{
				if (Filter.Type == EMixtormatLayerChildType::Blur
					&& Filter.ScopeOwnerSourceChildIndex == Child.SourceChildIndex
					&& Filter.SourceChildIndex > Child.SourceChildIndex)
				{
					if (Filter.Blur.BlursAnything() && Filter.Blur.Target != EMixtormatBlurTarget::Flow && ScopedBundle.Height)
					{
						const bool bFilterMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
						FRDGTextureRef FilterGate = bFilterMask
							? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
							: nullptr;
						MixtormatGaussianBlur::FSettings Settings;
						Settings.RadiusX = Filter.Blur.GetRadiusX();
						Settings.RadiusY = Filter.Blur.GetRadiusY();
						Settings.Amount = Filter.Blur.Amount;
						ScopedBundle.Height = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Filter.Blur,
							ScopedBundle.Height, Settings, MixtormatGaussianBlur::EField::Scalar, FilterGate);
					}
					if (Filter.Blur.BlursAnything() && Filter.Blur.Target != EMixtormatBlurTarget::Height)
					{
						for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
						{
							const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
							const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
							if (!Field || !Field->IsComplete() || Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
							const FPublishedField Source = *Field;
							const FPublishedField Filtered = AddOwnedFlowBlurPass(Ctx, LayerCtx, Layer, Filter.Blur, Source);
							Ctx.PublishedFieldOutputs.Add(Key, Filtered);
						}
					}
					continue;
				}
				if (Filter.Type != EMixtormatLayerChildType::Behavior
					|| Filter.Behavior.Type != EMixtormatBehaviorType::Feather
					|| Filter.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
					|| Filter.Behavior.GeneratorChildIndex != Child.SourceChildIndex
					|| Filter.SourceChildIndex <= Child.SourceChildIndex) { continue; }
				const bool bFilterMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
				FRDGTextureRef FilterGate = bFilterMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
					: nullptr;
				if (ScopedBundle.Height)
				{
					ScopedBundle.Height = AddGeneratorFeatherPasses(Ctx, ScopedBundle.Height,
						ScopedBundle.BoundaryField, Filter.Behavior, FilterGate, bFilterMask,
						LayerCtx.LayerIndex, Filter.SourceChildIndex);
				}
				for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
				{
					const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
					const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
					if (!Field || !Field->IsComplete()
						|| Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
					const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
						Filter.Behavior, FilterGate, bFilterMask,
						LayerCtx.LayerIndex, Filter.SourceChildIndex);
					Ctx.PublishedFieldOutputs.Add(Key, Filtered);
					if (Name == FName(TEXT("GeneratedFlow"))
						|| !Ctx.PublishedFieldOutputs.Contains(PublishedKey(Layer,
							Child.SourceChildIndex, FName(TEXT("GeneratedFlow")))))
					{
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Filter.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
					}
				}
				const FPublishedFieldKey SnapshotKey = PublishedKey(Layer,
					Filter.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow);
				if (Ctx.PublishedFieldDemand.Contains(SnapshotKey))
				{
					FRDGTextureRef Snapshot = WorkingFlow;
					FRDGTextureRef SnapshotValidity = WorkingFlowValidity;
					if (Noise.bWriteFlow)
					{
						const FPublishedField* Contribution = Ctx.PublishedFieldOutputs.Find(
							PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GeneratedFlow"))));
						if (Contribution && Contribution->IsComplete())
						{
							const bool bScopedMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
							FRDGTextureRef ScopedMask = bScopedMask ? AddScopedFeatureMask(Ctx,
								LayerCtx, Layer, Child.SourceChildIndex, true) : nullptr;
							Snapshot = AddNoiseFlowComposePass(Ctx, WorkingFlow, Contribution->Texture,
								ScopedMask, Noise.FlowAdd, Noise.FlowMix, SnapshotValidity,
								TEXT("Mixtormat.Diagnostic.FlowFeatherSnapshot"));
						}
					}
					if (Snapshot && SnapshotValidity)
					{
						Ctx.PublishedFieldOutputs.Add(SnapshotKey,
							FPublishedField{EMixtormatPublishedFieldKind::Flow, Snapshot,
								Snapshot, SnapshotValidity, false});
					}
				}
			}
			if (ScopedBundle.Height && Module.Height)
			{
				const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef HeightMask = bMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: ((Owner.Generator.Type == EMixtormatGeneratorType::RootNetwork || Owner.Generator.Type == EMixtormatGeneratorType::Scales) ? Module.Coverage : nullptr);
				ScopedBundle.Height = AddSignedGeneratorHeightPasses(Ctx.GraphBuilder,
					ScopedBundle.Height, Size, Child.Generator.bNormalizeHeight,
					Child.Generator.HeightScale, Child.Generator.HeightBias, true, HeightMask,
					TEXT("Mixtormat.Generator.ScopedNoiseHeight"));
				AddGeneratorModuleCombine(Ctx, Module.Height, ScopedBundle, Child, Module.Height);
			}
			if (!Noise.bWriteFlow) { continue; }
			const FPublishedField* Generated = Ctx.PublishedFieldOutputs.Find(
				PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GeneratedFlow"))));
			if (!Generated || !Generated->IsComplete()
				|| Generated->Kind != EMixtormatPublishedFieldKind::Flow
				|| Generated->Texture->Desc.Extent != Size) { continue; }
			const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Mask = bMasked ? AddScopedFeatureMask(Ctx, LayerCtx, Layer,
				Child.SourceChildIndex, true) : nullptr;
			WorkingFlow = AddNoiseFlowComposePass(Ctx, WorkingFlow, Generated->Texture, Mask,
				Noise.FlowAdd, Noise.FlowMix, WorkingFlowValidity,
				TEXT("Mixtormat.Generator.AccumulatedFlow"));
			continue;
		}

		if (Child.Type == EMixtormatLayerChildType::Blur)
		{
			const bool bDirect = Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex;
			const FChildRenderData* Parent = bDirect ? nullptr
				: Layer.Children.FindByPredicate([&](const FChildRenderData& Candidate)
				{
					return Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
						&& Candidate.SourceChildIndex < Child.SourceChildIndex
						&& Candidate.Type == EMixtormatLayerChildType::Behavior
						&& Candidate.Behavior.GeneratorChildIndex == Owner.SourceChildIndex
						&& Candidate.Behavior.Stage == EMixtormatBehaviorStage::PostGeneration;
				});
			if (!bDirect && !Parent) { continue; }
			if (Child.Blur.BlursAnything() && Child.Blur.Target != EMixtormatBlurTarget::Height)
			{
				const int32 FlowOwnerIndex = bDirect ? Owner.SourceChildIndex : Parent->SourceChildIndex;
				const FPublishedFieldKey FlowKey = PublishedKey(Layer, FlowOwnerIndex, FName(TEXT("FlowDirection")));
				const FPublishedField* Published = bDirect ? nullptr : Ctx.PublishedFieldOutputs.Find(FlowKey);
				const FPublishedField Source = Published ? *Published
					: FPublishedField{EMixtormatPublishedFieldKind::Flow, WorkingFlow,
						WorkingFlow, WorkingFlowValidity, false};
				if (bDirect || Published)
				{
					const FPublishedField Filtered = AddOwnedFlowBlurPass(Ctx, LayerCtx, Layer, Child.Blur, Source);
					if (Filtered.IsComplete())
					{
						if (bDirect)
						{
							WorkingFlow = Filtered.Texture;
							WorkingFlowValidity = Filtered.Validity;
						}
						Ctx.PublishedFieldOutputs.Add(FlowKey, Filtered);
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex,
							FName(TEXT("FlowDirection"))), Filtered);
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex,
							MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
					}
				}
			}
			if (Child.Blur.BlursAnything() && Child.Blur.Target != EMixtormatBlurTarget::Flow && Module.Height)
			{
				const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bMasked
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: nullptr;
				MixtormatGaussianBlur::FSettings Settings;
				Settings.RadiusX = Child.Blur.GetRadiusX();
				Settings.RadiusY = Child.Blur.GetRadiusY();
				Settings.Amount = Child.Blur.Amount;
				Module.Height = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Child.Blur,
					Module.Height, Settings, MixtormatGaussianBlur::EField::Scalar, Gate);
			}
			continue;
		}

		if (Child.Type != EMixtormatLayerChildType::Behavior
			|| Child.Behavior.GeneratorChildIndex != Owner.SourceChildIndex
			|| Child.Behavior.Stage != EMixtormatBehaviorStage::PostGeneration) { continue; }
		if (Child.Behavior.Type == EMixtormatBehaviorType::Feather)
		{
			const FChildRenderData* FlowParent = Layer.Children.FindByPredicate(
				[&](const FChildRenderData& Candidate)
				{
					return Candidate.Type == EMixtormatLayerChildType::Behavior
						&& Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
						&& Candidate.SourceChildIndex < Child.SourceChildIndex
						&& Candidate.Behavior.Type == EMixtormatBehaviorType::FlowField
						&& Candidate.Behavior.GeneratorChildIndex == Owner.SourceChildIndex;
				});
			if (FlowParent)
			{
				const FPublishedFieldKey Key = PublishedKey(Layer,
					FlowParent->SourceChildIndex, FName(TEXT("FlowDirection")));
				const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
				if (Field && Field->IsComplete()
					&& Field->Kind == EMixtormatPublishedFieldKind::Flow)
				{
					const bool bMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef Gate = bMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: nullptr;
					const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
						Child.Behavior, Gate, bMask, LayerCtx.LayerIndex, Child.SourceChildIndex);
					Ctx.PublishedFieldOutputs.Add(Key, Filtered);
					Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
						Child.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
					Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
						Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
					bPublishWorkingFlow = false;
				}
				continue;
			}
			const bool bDirect = Child.ScopeOwnerSourceChildIndex == Owner.SourceChildIndex;
			const FChildRenderData* Parent = bDirect ? nullptr
				: Layer.Children.FindByPredicate([&](const FChildRenderData& Candidate)
				{
					return Candidate.SourceChildIndex == Child.ScopeOwnerSourceChildIndex
						&& Candidate.SourceChildIndex < Child.SourceChildIndex
						&& Candidate.Type == EMixtormatLayerChildType::Behavior
						&& Candidate.Behavior.GeneratorChildIndex == Owner.SourceChildIndex
						&& (Candidate.Behavior.Type == EMixtormatBehaviorType::Warp
							|| Candidate.Behavior.Type == EMixtormatBehaviorType::Deform
							|| Candidate.Behavior.Type == EMixtormatBehaviorType::Push
							|| Candidate.Behavior.Type == EMixtormatBehaviorType::Carve);
				});
			if (!bDirect && !Parent) { continue; }
			const bool bMasked = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bMasked
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: nullptr;
			Module.Height = AddGeneratorFeatherPasses(Ctx, Module.Height, Module.BoundaryField,
				Child.Behavior, Gate, bMasked, LayerCtx.LayerIndex, Child.SourceChildIndex);
			if (bDirect && Owner.Generator.Type == EMixtormatGeneratorType::Noise
				&& WorkingFlowValidity)
			{
				const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
					WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
				const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, FlowField,
					Child.Behavior, Gate, bMasked, LayerCtx.LayerIndex, Child.SourceChildIndex);
				WorkingFlow = Filtered.Texture;
				WorkingFlowValidity = Filtered.Validity;
			}
			continue;
		}
		if (Child.ScopeOwnerSourceChildIndex != Owner.SourceChildIndex)
		{
			continue;
		}
		if (Child.Behavior.Type != EMixtormatBehaviorType::Warp
			&& Child.Behavior.Type != EMixtormatBehaviorType::Push
			&& Child.Behavior.Type != EMixtormatBehaviorType::Carve
			&& Child.Behavior.Type != EMixtormatBehaviorType::Deform) { continue; }
		const FOutputReferenceRenderData& Ref = Child.Behavior.Direction;
		FRDGTextureRef Influence = LayerCtx.CombinedMask;
		const bool bUseInfluence = Child.Behavior.bHasInfluence;
		if (bUseInfluence)
		{
			const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Influence.Source);
			if (!Field || !Field->IsComplete()
				|| Field->Kind != EMixtormatPublishedFieldKind::Scalar01
				|| Field->Texture->Desc.Extent != Size
				|| (Field->Texture->Desc.Format != PF_R16F
					&& Field->Texture->Desc.Format != PF_R32_FLOAT))
			{
				continue;
			}
			Influence = Field->Texture;
		}
		if (Child.Behavior.Type == EMixtormatBehaviorType::Carve)
		{
			if (!Module.Height || !WorkingFlowValidity) { continue; }
			const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: LayerCtx.CombinedMask;
			Module.Height = AddBehaviorSculptPass(Ctx, Layer, Module.Height,
				WorkingFlow, WorkingFlowValidity, Child.Behavior, Gate, bHasMask,
				Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex);
			continue;
		}
		if (Child.Behavior.Type == EMixtormatBehaviorType::Push)
		{
			FRDGTextureRef Delta = nullptr;
			switch (Child.Behavior.HeightOrigin)
			{
			case EMixtormatBehaviorFieldOrigin::OwnNativeHeight:
				Delta = Module.Height;
				break;
			case EMixtormatBehaviorFieldOrigin::PreviousRunningHeight:
				Delta = PreviousRunningHeight;
				break;
			case EMixtormatBehaviorFieldOrigin::PublishedOutput:
			{
				const FPublishedField* HeightField = Ctx.PublishedFieldOutputs.Find(Child.Behavior.Height.Source);
				if (HeightField && HeightField->IsComplete()
					&& HeightField->Kind == EMixtormatPublishedFieldKind::ScalarSigned
					&& HeightField->Texture->Desc.Extent == Size)
				{
					Delta = HeightField->Texture;
				}
				break;
			}
			default:
				break;
			}
			if (!Delta || !Module.Height || Delta->Desc.Format != PF_R32_FLOAT) { continue; }
			const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: LayerCtx.CombinedMask;
			Module.Height = AddBehaviorSignedPushPass(Ctx, Module.Height, Delta,
				Child.Behavior, Gate, bHasMask, Influence, bUseInfluence,
				LayerCtx.LayerIndex, Child.SourceChildIndex);
			continue;
		}
		FRDGTextureRef Coordinates = nullptr;
		const bool bMixDriver = Child.Behavior.Flow.SettingsDrivers[8].bEnabled;
		const bool bWarp = Child.Behavior.Type == EMixtormatBehaviorType::Warp;
		const bool bStandardNeeded = !bWarp || Child.Behavior.Flow.ModeMix < 1.0f || bMixDriver;
		if (bStandardNeeded && WorkingFlowValidity
			&& (Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::None
				|| (Child.Behavior.Type == EMixtormatBehaviorType::Deform
					&& Child.Behavior.bUseTracedFlow))
			&& (Child.Behavior.Type == EMixtormatBehaviorType::Warp
				|| Child.Behavior.Type == EMixtormatBehaviorType::Deform))
		{
			FOutputReferenceRenderData AutoFlow;
			AutoFlow.FlowAmount = 1.0f;
			AutoFlow.FlowTraceLength = Child.Behavior.bUseTracedFlow
				? Child.Behavior.Flow.TraceLength : Child.Behavior.Flow.StandardTraceLength;
			AutoFlow.FlowSteps = Child.Behavior.bUseTracedFlow
				? Child.Behavior.Flow.TraceSteps : Child.Behavior.Flow.StandardTraceSteps;
			const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
				WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
			Coordinates = AddReferencedFlowUVPass(Ctx, AutoFlow, FlowField,
				LayerCtx.LayerIndex, Child.SourceChildIndex, nullptr, true);
			const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: LayerCtx.CombinedMask;
			Coordinates = ScaleBehaviorUV(Ctx, Coordinates, Child.Behavior.Strength,
				Gate, bHasMask, Influence, bUseInfluence,
				LayerCtx.LayerIndex, Child.SourceChildIndex,
				Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
		}
		else if (bStandardNeeded && Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight
			&& Module.Height && (Child.Behavior.GradientReach != 0.0f
				|| Child.Behavior.ScalarDrivers[1].bEnabled))
		{
			const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
			FRDGTextureRef Gate = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
				: LayerCtx.CombinedMask;
			Coordinates = MakeBehaviorHeightGradientUV(Ctx, Module.Height,
				Child.Behavior.Strength, Child.Behavior.GradientReach,
				bWarp, Gate, bHasMask,
				Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
				Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
		}
		else if (bStandardNeeded && Child.Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::PublishedOutput)
		{
			const FPublishedField* Published = Ctx.PublishedFieldOutputs.Find(Ref.Source);
			if (Published && Published->IsComplete() && Published->Kind == Ref.Kind
				&& Published->Texture->Desc.Extent == Size)
			{
				if (Ref.Kind == EMixtormatPublishedFieldKind::Flow)
				{
					if ((Ref.FlowAmount != 0.0f || Child.Behavior.FlowDrivers[0].bEnabled)
						&& (Ref.FlowTraceLength != 0.0f || Child.Behavior.FlowDrivers[1].bEnabled))
					{
						Coordinates = AddReferencedFlowUVPass(Ctx, Ref, *Published,
							LayerCtx.LayerIndex, Child.SourceChildIndex, Child.Behavior.FlowDrivers);
					}
				}
				else if (Ref.Kind == EMixtormatPublishedFieldKind::UVMap
					&& Published->Texture->Desc.Format == PF_G32R32F)
				{
					Coordinates = Published->Texture;
				}
				if (Coordinates)
				{
					const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
					FRDGTextureRef Gate = bHasMask
						? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
						: LayerCtx.CombinedMask;
					const float BlendStrength = Child.Behavior.Strength;
					Coordinates = ScaleBehaviorUV(Ctx, Coordinates, BlendStrength, Gate,
						bHasMask, Influence, bUseInfluence, LayerCtx.LayerIndex, Child.SourceChildIndex,
						Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
				}
			}
		}
		FRDGTextureRef TracedCoordinates = nullptr;
		if (bWarp && (Child.Behavior.Flow.ModeMix > 0.0f || bMixDriver)
			&& WorkingFlowValidity)
		{
			FOutputReferenceRenderData Trace;
			Trace.FlowAmount = 1.0f;
			Trace.FlowTraceLength = Child.Behavior.Flow.TraceLength;
			Trace.FlowSteps = Child.Behavior.Flow.TraceSteps;
			const FPublishedField FlowField{EMixtormatPublishedFieldKind::Flow,
				WorkingFlow, WorkingFlow, WorkingFlowValidity, false};
			TracedCoordinates = AddReferencedFlowUVPass(Ctx, Trace, FlowField,
				LayerCtx.LayerIndex, Child.SourceChildIndex, nullptr, true);
			if (TracedCoordinates)
			{
				const bool bHasMask = HasScopedGeneratorMasks(Layer, Child.SourceChildIndex);
				FRDGTextureRef Gate = bHasMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: LayerCtx.CombinedMask;
				TracedCoordinates = ScaleBehaviorUV(Ctx, TracedCoordinates,
					Child.Behavior.Strength, Gate, bHasMask, Influence, bUseInfluence,
					LayerCtx.LayerIndex, Child.SourceChildIndex,
					Child.Behavior.ScalarDrivers, Child.Behavior.DirectionComposition);
			}
		}
		if (bWarp)
		{
			Coordinates = MixBehaviorWarpCoordinates(Ctx, Coordinates, TracedCoordinates,
				Child.Behavior, LayerCtx.LayerIndex, Child.SourceChildIndex);
			if (Coordinates)
			{
				Ctx.PublishedFieldOutputs.Add(
					PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
					FPublishedField{EMixtormatPublishedFieldKind::UVMap, Coordinates, nullptr, nullptr, false});
			}
		}
		if (Coordinates)
		{
			if (Child.Behavior.Type == EMixtormatBehaviorType::Deform)
			{
				Module.Height = RemapBundleField(Ctx, Module.Height, Coordinates, 0);
			}
			else
			{
				RemapGeneratorModuleOutputs(Ctx, Layer, Owner, Module, Coordinates, bWarp);
				if (WorkingFlowValidity)
				{
					WorkingFlow = AddNoiseFlowTransportPass(Ctx, WorkingFlow,
						WorkingFlowValidity, Coordinates, WorkingFlowValidity);
				}
			}
		}
	}
}

void AddOutputReferencePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
{
	if (!Layer.bEnabled) { return; }
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (Child.Type != EMixtormatLayerChildType::OutputReference) { continue; }
		const FOutputReferenceRenderData& Reference = Child.OutputReference;
		if (Reference.Kind == EMixtormatPublishedFieldKind::RegionIds)
		{
			AddRegionIdReferencePass(Ctx, LayerCtx, Layer, Child, false);
			continue;
		}
		const FPublishedField* Source = Ctx.PublishedFieldOutputs.Find(Reference.Source);
		if (!Source || Source->Kind != Reference.Kind || !Source->IsComplete()) { continue; }
		const FPublishedField Field = *Source;
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, Reference.Source.Output), Field);

		if (Reference.Kind == EMixtormatPublishedFieldKind::UVMap)
		{
			LayerCtx.ReferencedUV = Field.Texture;
			continue;
		}
		if (Reference.Kind == EMixtormatPublishedFieldKind::Color)
		{
			LayerCtx.ReferencedColor = Field.Texture;
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::Color,
				Reference.Source.Output, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
				AddDebugPreviewColorBlitPass(Ctx.GraphBuilder, Field.Texture, Debug, Ctx.Request.Resolution);
			}
			continue;
		}
		if (Reference.Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
		FRDGTextureRef Coordinates = AddReferencedFlowUVPass(
			Ctx, Reference, Field, LayerCtx.LayerIndex, Child.SourceChildIndex);
		LayerCtx.ReferencedUV = Coordinates;
		Ctx.PublishedFieldOutputs.Add(
			PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("WarpedUV"))),
			FPublishedField{EMixtormatPublishedFieldKind::UVMap, Coordinates, nullptr, nullptr, false});
	}
}

}
