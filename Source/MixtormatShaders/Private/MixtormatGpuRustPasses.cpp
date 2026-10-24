// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGpuSurfacingPasses.h"
#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatColorRampMath.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "RHIStaticStates.h"

namespace MixtormatGpuCompositor
{
namespace
{
	class FMixtormatRustCS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FMixtormatRustCS);
		SHADER_USE_PARAMETER_STRUCT(FMixtormatRustCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, OutputSize)
			SHADER_PARAMETER(uint32, Seed)
			SHADER_PARAMETER(uint32, Structure)
			SHADER_PARAMETER(float, ClusterPeriod)
			SHADER_PARAMETER(float, Coverage)
			SHADER_PARAMETER(float, PitDepth)
			SHADER_PARAMETER(float, Buildup)
			SHADER_PARAMETER(float, Flaking)
			SHADER_PARAMETER(float, SurfaceBias)
			SHADER_PARAMETER(float, Variation)
			SHADER_PARAMETER(float, HeightScale)
			SHADER_PARAMETER(float, Roughness)
			SHADER_PARAMETER(uint32, GeneratorLayer)
			SHADER_PARAMETER(FVector2f, GeneratorUVScale)
			SHADER_PARAMETER(FVector2f, GeneratorUVOffset)
			SHADER_PARAMETER(int32, GeneratorUVRotation)
			SHADER_PARAMETER(uint32, GeneratorUVFlipU)
			SHADER_PARAMETER(uint32, GeneratorUVFlipV)
			SHADER_PARAMETER(uint32, UsePreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreGenerationUV)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SubstrateHeight)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
			SHADER_PARAMETER(uint32, ColorCount)
			SHADER_PARAMETER(uint32, ColorInterpolation)
			SHADER_PARAMETER_ARRAY(FVector4f, ColorPositions, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER_ARRAY(FVector4f, ColorColors, [FMixtormatColorRamp::MaxStops])
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutCoverage)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutRAM)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutOptics)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FMixtormatRustCS, "/Plugin/Mixtormat/Private/MixtormatRust.usf", "MainCS", SF_Compute);
}

FSurfacingFields AddRustSurfacePasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
	const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV)
{
	FSurfacingFields Result;
	if (!Layer.bEnabled || !Child.Surfacing.bEnabled || Child.Surfacing.Type != EMixtormatSurfacingType::Rust
		|| !SubstrateHeight) { return Result; }
	const FMixtormatRust& R = Child.Surfacing.Rust;
	const FIntPoint Size = Ctx.Request.Resolution;
	const auto Make = [&](EPixelFormat Format, const TCHAR* Name)
	{
		return Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Format,
			FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	};
	Result.Height = Make(PF_R32_FLOAT, TEXT("Mixtormat.Rust.Height"));
	Result.MaterialCoverage = Make(PF_R32_FLOAT, TEXT("Mixtormat.Rust.OxideCoverage"));
	Result.Color = Make(PF_A32B32G32R32F, TEXT("Mixtormat.Rust.Color"));
	Result.RAM = Make(PF_FloatRGBA, TEXT("Mixtormat.Rust.RAM"));
	Result.Optics = Make(PF_FloatRGBA, TEXT("Mixtormat.Rust.Optics"));
	// Disabled optical lobes need no extra textures. Apply preserves underlying MFP/tint;
	// the Rust material flag clears only their weights under visible oxide.
	Result.FuzzColor = LayerCtx.LayerInputFuzzColor;
	Result.Subsurface = LayerCtx.LayerInputSubsurface;
	TShaderMapRef<FMixtormatRustCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	auto* P = Ctx.GraphBuilder.AllocParameters<FMixtormatRustCS::FParameters>();
	P->OutputSize = Size;
	P->Seed = static_cast<uint32>(R.Seed);
	P->Structure = static_cast<uint32>(R.Structure);
	// Preserve the binding value. Only protect division; unresolvable noise octaves
		// are filtered in the shared Noise lattice, not clamped back to a larger feature.
		P->ClusterPeriod = FMath::Max(1.0f, 24.0f / FMath::Max(FMath::Abs(R.FeatureSize), UE_SMALL_NUMBER));
	P->Coverage = R.Coverage;
	P->PitDepth = R.PitDepth;
	P->Buildup = R.Buildup;
	P->Flaking = R.Flaking;
	P->SurfaceBias = R.SurfaceBias;
	P->Variation = R.Variation;
	P->HeightScale = R.HeightScale;
	P->Roughness = R.Roughness;
	P->GeneratorLayer = 0u;
	P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
	P->GeneratorUVOffset = Layer.UVOffset;
	P->GeneratorUVRotation = Layer.Rotation;
	P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
	P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
	P->UsePreGenerationUV = PreUV ? 1u : 0u;
	P->PreGenerationUV = PreUV ? PreUV : Ctx.EmptyPatternUV;
	P->SubstrateHeight = SubstrateHeight;
	P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	const auto Palette = MixtormatColorRampMath::PrepareGpuPayload(R.ColorRamp);
	P->ColorCount = Palette.StopCount;
	P->ColorInterpolation = Palette.Interpolation;
	for (int32 I = 0; I < FMixtormatColorRamp::MaxStops; ++I)
	{
		P->ColorPositions[I] = FVector4f(Palette.Positions[I], 0, 0, 0);
		P->ColorColors[I] = Palette.Colors[I];
	}
	P->OutHeight = Ctx.GraphBuilder.CreateUAV(Result.Height);
	P->OutCoverage = Ctx.GraphBuilder.CreateUAV(Result.MaterialCoverage);
	P->OutColor = Ctx.GraphBuilder.CreateUAV(Result.Color);
	P->OutRAM = Ctx.GraphBuilder.CreateUAV(Result.RAM);
	P->OutOptics = Ctx.GraphBuilder.CreateUAV(Result.Optics);
	ClearUnusedGraphResources(Shader, P);
	FComputeShaderUtils::AddPass(Ctx.GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.Rust.L%d.C%d", LayerCtx.LayerIndex, Child.SourceChildIndex), Shader, P,
		FIntVector(FMath::DivideAndRoundUp(Size.X,8), FMath::DivideAndRoundUp(Size.Y,8), 1));
	return Result;
}
}
