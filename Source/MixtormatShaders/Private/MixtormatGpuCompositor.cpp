// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositor.h"

#include "Compositing/MixtormatComposeHash.h"
#include "Compositing/MixtormatEffectGather.h"
#include "Compositing/MixtormatLayerGather.h"
#include "Compositing/MixtormatMaskGather.h"
#include "Compositing/MixtormatIdGather.h"
#include "Compositing/MixtormatGeneratorGather.h"
#include "MixtormatGpuCompositorInternal.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MixtormatChildScope.h"
#include "MixtormatEffect.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatMask.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatParameterDefinition.h"
#include "MixtormatReliefScaling.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

// Render commands retain the exact target generation, including isolated child outputs.
// UObject pins are released on the game thread; RDG/RHI retain GPU resources until queued work
// retires. Resource pointers are captured on the game thread but dereferenced only after their
// initialization commands on the render thread, so child creation never needs a flush.
FMixtormatComposeResources::~FMixtormatComposeResources()
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [KeepAlive = MoveTemp(Pins)]() mutable
		{
			KeepAlive.Reset();
		});
	}
}

DEFINE_LOG_CATEGORY(LogMixtormatComposition);

static TAutoConsoleVariable<int32> CVarMixtormatDisableComposeCache(
	TEXT("Mixtormat.DisableComposeCache"),
	0,
	TEXT("1 disables the layer-prefix and referenced-composition caches, so every composite runs ")
	TEXT("the whole stack. For comparing cached output against a full recomposite."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMixtormatComposeCacheBudgetMB(
	TEXT("Mixtormat.ComposeCacheBudgetMB"),
	512,
	TEXT("Upper bound, in MB, for kept layer-prefix snapshots per compositor. A snapshot larger ")
	TEXT("than this is not kept at all."),
	ECVF_Default);

// The ground every stack composites onto.
//
// Deliberately not a layer. It has no row, no selection, no children and no inspector -- it
// exists so that the bottom of the stack is an ordinary position rather than a privileged one.
// Before it, layer 0 seeded these buffers by replacing them, which meant the bottom layer
// ignored its own mask, feature influence and height blend; a layer therefore rendered
// differently depending on where it sat, and could not be freely dragged to the bottom.
//

class FMixtormatCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCompositeCS, FGlobalShader);

	// Has to agree with MIXTORMAT_MAX_REGION_PALETTE in MixtormatComposite.usf and with
	// FMixtormatHsvIdFilter::MaxPaletteColors. The ID gather clamps against that same
	// authored palette cap.
	static constexpr int32 MaxRegionPalette = FMixtormatHsvIdFilter::MaxPaletteColors;
	// Two driven scalars this step: slot 0 RoughnessInfluence, slot 1 HeightBlendAmount. Kept
	// small on purpose -- every slot is a texture binding on every composite dispatch.
	static constexpr int32 MaxScalarDrivers = 2;
	// Fixed shader slots keep masks independently sampleable. Additional grades are rejected by
	// the gather instead of falling back to grading the accumulated stack.
	static constexpr int32 MaxLayerGrades = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Enabled)
		SHADER_PARAMETER(uint32, HasMask)

		SHADER_PARAMETER(uint32, HasEffects)
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(uint32, OverrideRoughness)
		SHADER_PARAMETER(uint32, OverrideMetallic)
		SHADER_PARAMETER(uint32, CompositionMode)
		SHADER_PARAMETER(uint32, IsFill)
		SHADER_PARAMETER(uint32, IsGenerator)
		SHADER_PARAMETER(uint32, HasSurface)
		SHADER_PARAMETER(uint32, PreparedLayerMode)
		SHADER_PARAMETER(uint32, HasPackedHeight)
		SHADER_PARAMETER(uint32, HasSeparateHeight)
		SHADER_PARAMETER(uint32, LayerInputResolved)
		SHADER_PARAMETER(uint32, UseSourceF0)
		SHADER_PARAMETER(uint32, HasNormal)
		SHADER_PARAMETER(uint32, NormalOnly)
		SHADER_PARAMETER(uint32, OverrideNormal)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, HeightOp)
		SHADER_PARAMETER(uint32, HeightSource)
		SHADER_PARAMETER(uint32, InvertHeight)
		SHADER_PARAMETER(uint32, DirectHeightComparison)
		SHADER_PARAMETER(uint32, InvertHeightFeature)
		SHADER_PARAMETER(uint32, InvertAOFeature)
		SHADER_PARAMETER(uint32, InvertFeature)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(float, Opacity)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(int32, UVScaleX)
		SHADER_PARAMETER(int32, UVScaleY)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(FVector2f, UVOffset)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, NormalIntensity)
		SHADER_PARAMETER(float, HueShift)
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(float, Value)
		SHADER_PARAMETER(float, RoughnessBias)
		SHADER_PARAMETER(float, RoughnessContrast)
		SHADER_PARAMETER(float, RoughnessOffset)
		SHADER_PARAMETER(float, FillRoughness)
		SHADER_PARAMETER(float, FillMetallic)
		SHADER_PARAMETER(float, LayerF0)
		SHADER_PARAMETER(uint32, BaseColorBlendMode)
		SHADER_PARAMETER(float, BaseColorBlendAmount)
		SHADER_PARAMETER(float, BaseColorInfluence)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, AOInfluence)
		SHADER_PARAMETER(float, MetallicInfluence)
		SHADER_PARAMETER(float, F0Influence)
		SHADER_PARAMETER(float, NormalInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightBoost)
		SHADER_PARAMETER(float, HeightLevelOffset)
		SHADER_PARAMETER(float, HeightShape)
		SHADER_PARAMETER(float, HeightBlendAmount)
		SHADER_PARAMETER(float, HeightSoftness)
		SHADER_PARAMETER(float, HeightAmount)
		SHADER_PARAMETER(float, HeightThreshold)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER(float, HeightOffset)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, ConstantHeight)
		SHADER_PARAMETER(float, MaskHeightInfluence)
		SHADER_PARAMETER(float, HeightContactAOAmount)
		SHADER_PARAMETER(float, HeightContactAOWidth)
		SHADER_PARAMETER(float, HeightBorderLift)
		SHADER_PARAMETER(float, HeightBorderWidth)
		SHADER_PARAMETER(float, HeightBorderNormalStrength)
		SHADER_PARAMETER(uint32, SuppressReliefNormals)
		SHADER_PARAMETER(float, FeatureInfluence)
		SHADER_PARAMETER(float, FeatureBias)
		SHADER_PARAMETER(float, HeightFeatureInfluence)
		SHADER_PARAMETER(float, AOFeatureInfluence)
		SHADER_PARAMETER(int32, CurvatureRadius)
		SHADER_PARAMETER(float, CurvatureStrength)
		SHADER_PARAMETER(float, CurvaturePower)
		SHADER_PARAMETER(int32, CurvatureSmoothing)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousOccupancy)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ReferenceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerSourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, EffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, EffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerHeightMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BorderBaseHeight)
		SHADER_PARAMETER(float, HeightSmoothAmount)
		SHADER_PARAMETER(uint32, BorderSmoothValid)
		// Scalar Drivers. One slot per driven scalar, not per Driver -- and the slots hold
		// bindings, so two scalars naming one source point at the same texture and the layer
		// still costs at most one snapshot. Bound on every dispatch like RegionIds above.
		SHADER_PARAMETER_ARRAY(FVector4f, DriverParamsA, [MaxScalarDrivers])
		SHADER_PARAMETER_ARRAY(FVector4f, DriverParamsB, [MaxScalarDrivers])
		SHADER_PARAMETER_ARRAY(FVector4f, DriverParamsC, [MaxScalarDrivers])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DriverSignal0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DriverSignal1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DriverRegionIds0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DriverRegionIds1)
		SHADER_PARAMETER(uint32, GradeCount)
		SHADER_PARAMETER_ARRAY(FVector4f, GradeParamsA, [MaxLayerGrades])
		SHADER_PARAMETER_ARRAY(FVector4f, GradeParamsB, [MaxLayerGrades])
		SHADER_PARAMETER_ARRAY(FVector4f, GradeParamsC, [MaxLayerGrades])
		SHADER_PARAMETER_ARRAY(FVector4f, GradeParamsD, [MaxLayerGrades])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask4)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask5)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask6)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GradeMask7)
		// Per-region colour variation, read off a cluster filter's ID map in the same layer.
		// RegionIds is bound on every dispatch -- a 1x1 dummy when the layer has no cluster --
		// because RDG validates the binding whether RegionTintEnabled takes the branch or not.
		SHADER_PARAMETER(uint32, RegionTintEnabled)
		SHADER_PARAMETER(uint32, RegionSeed)
		SHADER_PARAMETER_ARRAY(FVector4f, RegionPalette, [MaxRegionPalette])
		SHADER_PARAMETER(int32, RegionPaletteCount)
		SHADER_PARAMETER(float, RegionMixMin)
		SHADER_PARAMETER(float, RegionMixMax)
		SHADER_PARAMETER(float, RegionHueMin)
		SHADER_PARAMETER(float, RegionHueMax)
		SHADER_PARAMETER(float, RegionSatMin)
		SHADER_PARAMETER(float, RegionSatMax)
		SHADER_PARAMETER(float, RegionValMin)
		SHADER_PARAMETER(float, RegionValMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER(uint32, RegionUVEnabled)
		SHADER_PARAMETER(uint32, RegionUVSeed)
		SHADER_PARAMETER(uint32, RegionUVOrthogonal)
		SHADER_PARAMETER(float, RegionUVRotationMin)
		SHADER_PARAMETER(float, RegionUVRotationMax)
		SHADER_PARAMETER(float, RegionUVScaleMin)
		SHADER_PARAMETER(float, RegionUVScaleMax)
		SHADER_PARAMETER(FVector2f, RegionUVOffset)
		SHADER_PARAMETER(uint32, RegionUVFlipU)
		SHADER_PARAMETER(uint32, RegionUVFlipV)
		SHADER_PARAMETER(uint32, RegionUVVariationEnabled)
		SHADER_PARAMETER(uint32, RegionUVIntrinsicOrientation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionUVIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RegionUVCentreField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RegionUVOrientationField)
		SHADER_PARAMETER(uint32, ReferencedUVEnabled)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, ReferencedUVField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DebugMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputOccupancy)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCompositeCS,
	"/Plugin/Mixtormat/Private/MixtormatComposite.usf",
	"MainCS",
	SF_Compute);

// Resolves an authored or referenced material into this layer's output-space tangent basis.
// Allocated only for a top-level Flow Warp, so ordinary layers retain the direct sample path.
class FMixtormatLayerInputCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatLayerInputCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatLayerInputCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, HasSurface)
		SHADER_PARAMETER(uint32, HasSeparateHeight)
		SHADER_PARAMETER(uint32, HasPackedHeight)
		SHADER_PARAMETER(float, ConstantHeight)
		SHADER_PARAMETER(uint32, LayerInputResolved)
		SHADER_PARAMETER(uint32, HasNormal)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(int32, UVScaleX)
		SHADER_PARAMETER(int32, UVScaleY)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(FVector2f, UVOffset)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, NormalIntensity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerSourceHeight)
		SHADER_PARAMETER(uint32, RegionUVEnabled)
		SHADER_PARAMETER(uint32, RegionUVSeed)
		SHADER_PARAMETER(uint32, RegionUVOrthogonal)
		SHADER_PARAMETER(float, RegionUVRotationMin)
		SHADER_PARAMETER(float, RegionUVRotationMax)
		SHADER_PARAMETER(float, RegionUVScaleMin)
		SHADER_PARAMETER(float, RegionUVScaleMax)
		SHADER_PARAMETER(FVector2f, RegionUVOffset)
		SHADER_PARAMETER(uint32, RegionUVFlipU)
		SHADER_PARAMETER(uint32, RegionUVFlipV)
		SHADER_PARAMETER(uint32, RegionUVVariationEnabled)
		SHADER_PARAMETER(uint32, RegionUVIntrinsicOrientation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionUVIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RegionUVCentreField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RegionUVOrientationField)
		SHADER_PARAMETER(uint32, ReferencedUVEnabled)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, ReferencedUVField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatLayerInputCS,
	"/Plugin/Mixtormat/Private/MixtormatComposite.usf",
	"ResolveLayerInputCS",
	SF_Compute);

// The layer's own resolved values, packed for a mask to read: albedo in RGB, roughness in alpha.
//
// Reads what FMixtormatLayerInputCS produced rather than the source maps, so the layer's UV
// transform, its pattern UV basis and any Flow Warp already applied come along for free and are
// not applied twice. Small on purpose -- the parameters here are exactly the layer-own finishing
// the composite does to albedo and roughness, and nothing else.
class FMixtormatLayerValuesCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatLayerValuesCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatLayerValuesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		// uint32, matching the globals these share with the composite -- the shader lerps with
		// them and HLSL converts, but the parameter type has to agree or the bind is rejected.
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(uint32, OverrideRoughness)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER(float, FillRoughness)
		SHADER_PARAMETER(float, HueShift)
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(float, Value)
		SHADER_PARAMETER(float, RoughnessBias)
		SHADER_PARAMETER(float, RoughnessContrast)
		SHADER_PARAMETER(float, RoughnessOffset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerRAM)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatLayerValuesCS,
	"/Plugin/Mixtormat/Private/MixtormatComposite.usf",
	"ResolveLayerValuesCS",
	SF_Compute);

class FMixtormatRotateOutputCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatRotateOutputCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatRotateOutputCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputDebug)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputRegionIdPick)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRegionIdPick)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatRotateOutputCS,
	"/Plugin/Mixtormat/Private/MixtormatRotateOutput.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Kept local to the owned GPU translation units until the internal typed-ref seam lands.
	TArray<TPair<int32, FRDGTextureRef>> GetRegionIdView(
		const FLayerRenderData& Layer,
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const FChildRenderData* Child);


	// The debug clear, in the same space the shaders write.
	//
	// The palette lives in MixtormatDebugColor.ush and is authored in sRGB. These two clears are
	// the only copies outside it, and they have to be converted the same way or an untouched
	// region of the preview sits at a different brightness from the ramp drawn over it -- which
	// reads as the debug view having two backgrounds.
	FLinearColor DebugClearColor()
	{
		// FLinearColor::FromSRGBColor would quantise through 8-bit first; these are authored as
		// floats and the low channel is small enough for that to round visibly.
		const auto ToLinear = [](const float C)
		{
			return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
		};
		return FLinearColor(ToLinear(0.08f), ToLinear(0.02f), ToLinear(0.12f), 1.0f);
	}

	static UTextureRenderTarget2D* CreateTarget(
		const FIntPoint Resolution,
		const FLinearColor ClearColor,
		const EPixelFormat Format = PF_FloatRGBA)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->ClearColor = ClearColor;
		Target->bCanCreateUAV = true;
		Target->bAutoGenerateMips = false;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->InitCustomFormat(Resolution.X, Resolution.Y, Format, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}


	// The per-region source placement the layer's texture reads through, resolved for one layer.
	//
	// Two producers of this binding, and the order between them is the whole point of Prompt 2's
	// split. `UV From IDs` is the architecture: it consumes whatever Region IDs sit above its own
	// row and publishes a transform, so the same node works after Pattern IDs, Cluster IDs or
	// ID Group. Pattern IDs' own UV block is the legacy path, kept live and unchanged so a
	// material authored before the split renders exactly as it did.
	//
	// Both paths compete in authored child order. Select one treatment rather than compounding
	// transforms; a later neutral Pattern does not shadow an applicable UV From IDs row.
	struct FRegionUVBinding
	{
		bool bEnabled = false;
		bool bVariation = false;
		bool bIntrinsicOrientation = false;
		bool bOrthogonal = true;
		bool bRandomFlipU = false;
		bool bRandomFlipV = false;
		uint32 Seed = 0;
		float RotationMin = 0.0f;
		float RotationMax = 0.0f;
		float ScaleMin = 1.0f;
		float ScaleMax = 1.0f;
		FVector2f Offset = FVector2f::ZeroVector;
		FRDGTextureRef Ids = nullptr;
		FRDGTextureRef Centre = nullptr;
		FRDGTextureRef Orientation = nullptr;
	};

	static FRegionUVBinding ResolveRegionUVBinding(const FMixtormatLayerPassContext& LayerCtx)
	{
		FRegionUVBinding Binding;

		// Stack order, not first-found: the last applicable row wins, which is the same rule every
		// other child follows.
		const FUvIdPassOutput* ActiveUvId = nullptr;
		for (const FUvIdPassOutput& Output : LayerCtx.UvIdOutputs)
		{
			if (Output.Settings && Output.CentreUV && Output.Ids
			&& (!ActiveUvId || Output.SourceChildIndex > ActiveUvId->SourceChildIndex))
			{
				ActiveUvId = &Output;
			}
		}
		const FPatternIdPassOutput* ActivePattern = nullptr;
		for (const FPatternIdPassOutput& Output : LayerCtx.PatternOutputs)
		{
			if (Output.Settings && Output.Ids && Output.UV
				&& (Output.Settings->bUVVariation || HasIntrinsicPatternOrientation(*Output.Settings))
				&& (!ActivePattern || Output.SourceChildIndex > ActivePattern->SourceChildIndex))
			{
				ActivePattern = &Output;
			}
		}
		if (ActiveUvId && (!ActivePattern
			|| ActiveUvId->SourceChildIndex > ActivePattern->SourceChildIndex))
		{
			const FUvIdRenderData& Uv = *ActiveUvId->Settings;
			Binding.bEnabled = true;
			// Always on for this node: unlike Pattern's block, its existence in the stack *is* the
			// request. A neutral rotation/scale still costs nothing but the sample it was going to
			// take anyway.
			Binding.bVariation = true;
			Binding.bIntrinsicOrientation = ActiveUvId->bIntrinsicOrientation;
			Binding.bOrthogonal = Uv.bOrthogonal;
			Binding.bRandomFlipU = Uv.bRandomFlipU;
			Binding.bRandomFlipV = Uv.bRandomFlipV;
			Binding.Seed = Uv.Seed;
			Binding.RotationMin = Uv.RotationMin;
			Binding.RotationMax = Uv.RotationMax;
			Binding.ScaleMin = Uv.ScaleMin;
			Binding.ScaleMax = Uv.ScaleMax;
			Binding.Offset = FVector2f(Uv.OffsetU, Uv.OffsetV);
			Binding.Ids = ActiveUvId->Ids;
			Binding.Centre = ActiveUvId->CentreUV;
			Binding.Orientation = ActiveUvId->Orientation;
			return Binding;
		}

		// Legacy: Pattern IDs' own UV block. The last Pattern row that needs source-space work
		// wins. Herringbone and Basketweave always need their intrinsic basis; other modes only
		// enter when random Pattern UV variation is enabled.
		if (!ActivePattern)
		{
			return Binding;
		}

		const FPatternIdRenderData& Pattern = *ActivePattern->Settings;
		Binding.bEnabled = true;
		Binding.bVariation = Pattern.bUVVariation;
		Binding.bIntrinsicOrientation = HasIntrinsicPatternOrientation(Pattern);
		Binding.bOrthogonal = Pattern.bOrthogonalUV;
		Binding.bRandomFlipU = Pattern.bRandomFlipU;
		Binding.bRandomFlipV = Pattern.bRandomFlipV;
		Binding.Seed = Pattern.Seed;
		Binding.RotationMin = Pattern.UVRotationMin;
		Binding.RotationMax = Pattern.UVRotationMax;
		Binding.ScaleMin = Pattern.UVScaleMin;
		Binding.ScaleMax = Pattern.UVScaleMax;
		// One authored scalar spread across both axes, which is exactly what the shader did with
		// it before this became a float2. Bit-identical for every existing material.
		Binding.Offset = FVector2f(Pattern.UVOffset, Pattern.UVOffset);
		Binding.Ids = ActivePattern->Ids;
		Binding.Centre = ActivePattern->UV;
		Binding.Orientation = ActivePattern->Orientation;
		return Binding;
	}

	template <typename TParameters>
	static void ApplyRegionUVParameters(
		TParameters& Parameters,
		const FRegionUVBinding& Binding,
		const FMixtormatComposeContext& Ctx)
	{
		Parameters.RegionUVEnabled = Binding.bEnabled ? 1u : 0u;
		Parameters.RegionUVSeed = Binding.Seed;
		Parameters.RegionUVOrthogonal = Binding.bOrthogonal ? 1u : 0u;
		Parameters.RegionUVRotationMin = Binding.RotationMin;
		Parameters.RegionUVRotationMax = Binding.RotationMax;
		Parameters.RegionUVScaleMin = Binding.ScaleMin;
		Parameters.RegionUVScaleMax = Binding.ScaleMax;
		Parameters.RegionUVOffset = Binding.Offset;
		Parameters.RegionUVFlipU = Binding.bRandomFlipU ? 1u : 0u;
		Parameters.RegionUVFlipV = Binding.bRandomFlipV ? 1u : 0u;
		Parameters.RegionUVVariationEnabled = Binding.bVariation ? 1u : 0u;
		Parameters.RegionUVIntrinsicOrientation = Binding.bIntrinsicOrientation ? 1u : 0u;
		Parameters.RegionUVIds = Binding.Ids ? Binding.Ids : Ctx.EmptyRegionIds;
		Parameters.RegionUVCentreField = Binding.Centre ? Binding.Centre : Ctx.EmptyPatternUV;
		Parameters.RegionUVOrientationField =
			Binding.Orientation ? Binding.Orientation : Ctx.EmptyPatternOrientation;
	}

	void AddLayerInputPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		if (LayerCtx.LayerInputBC)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		const FRegionUVBinding RegionUV = ResolveRegionUVBinding(LayerCtx);

		LayerCtx.LayerInputBC = GraphBuilder.CreateTexture(
			Ctx.OutputBC[0]->Desc, TEXT("Mixtormat.LayerInputBC"));
		LayerCtx.LayerInputN = GraphBuilder.CreateTexture(
			Ctx.OutputN[0]->Desc, TEXT("Mixtormat.LayerInputN"));
		LayerCtx.LayerInputRAM = GraphBuilder.CreateTexture(
			Ctx.OutputRAM[0]->Desc, TEXT("Mixtormat.LayerInputRAM"));
		LayerCtx.LayerInputHeight = GraphBuilder.CreateTexture(
			Ctx.OutputHeight[0]->Desc, TEXT("Mixtormat.LayerInputHeight"));

		FMixtormatLayerInputCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatLayerInputCS::FParameters>();
		Parameters->OutputSize = Request.Resolution;
		Parameters->HasSurface = Layer.bHasSurface ? 1u : 0u;
		Parameters->HasSeparateHeight = Layer.SourceOutputs.IsValid() ? 1u : 0u;
		Parameters->HasPackedHeight = Layer.bHasPackedHeight ? 1u : 0u;
		Parameters->ConstantHeight = Layer.ConstantHeight;
		Parameters->LayerInputResolved = 0u;
		Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
		Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
		Parameters->Tiling = Layer.Tiling;
		Parameters->UVScaleX = Layer.UVScaleX;
		Parameters->UVScaleY = Layer.UVScaleY;
		Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
		Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
		Parameters->UVOffset = Layer.UVOffset;
		Parameters->Rotation = Layer.Rotation;
		Parameters->NormalIntensity = Layer.NormalIntensity;
		Parameters->LayerBC = RegisterTexture(
			GraphBuilder, RegisteredTextures, Layer.BaseColor, TEXT("Mixtormat.LayerBC"));
		Parameters->LayerN = RegisterTexture(
			GraphBuilder, RegisteredTextures, Layer.Normal, TEXT("Mixtormat.LayerN"));
		Parameters->LayerRAM = RegisterTexture(
			GraphBuilder, RegisteredTextures, Layer.RAM, TEXT("Mixtormat.LayerRAM"));
		Parameters->LayerSourceHeight = Layer.Height.IsValid()
			? RegisterTexture(GraphBuilder, RegisteredTextures, Layer.Height,
				TEXT("Mixtormat.LayerSourceHeight"))
			: Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)];
		ApplyRegionUVParameters(*Parameters, RegionUV, Ctx);
		Parameters->ReferencedUVEnabled = LayerCtx.ReferencedUV ? 1u : 0u;
		Parameters->ReferencedUVField = LayerCtx.ReferencedUV ? LayerCtx.ReferencedUV : Ctx.EmptyPatternUV;
		Parameters->LinearWrapSampler = TStaticSamplerState<
			SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		Parameters->OutputBC = GraphBuilder.CreateUAV(LayerCtx.LayerInputBC);
		Parameters->OutputN = GraphBuilder.CreateUAV(LayerCtx.LayerInputN);
		Parameters->OutputRAM = GraphBuilder.CreateUAV(LayerCtx.LayerInputRAM);
		Parameters->OutputHeight = GraphBuilder.CreateUAV(LayerCtx.LayerInputHeight);

		TShaderMapRef<FMixtormatLayerInputCS> Shader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.LayerInput.Layer%d", LayerCtx.LayerIndex),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));
	}

	// The layer's own resolved values, as one texture a mask child can read.
	//
	// Idempotent and lazy, the same contract AddLayerInputPass has: several Layer Values masks on
	// one layer -- one on luminance, one on roughness, one scoped under an effect -- all share this
	// single dispatch, and a layer with none never pays for it.
	//
	// It chains onto AddLayerInputPass rather than re-reading the source maps. That pass may
	// already exist for a Flow Warp or a height smooth, in which case this costs one dispatch over
	// a texture that was going to be there anyway; and going through it is what makes the layer's
	// UV transform and pattern UV basis apply exactly once.
	//
	// **Cycle prevention, mechanically.** Nothing bound here is part of a mask chain. The inputs
	// are LayerInputBC/LayerInputRAM -- the layer's source maps resolved into output space -- and
	// the layer's own scalar parameters. The layer mask, the accumulated composite below, the
	// effect targets and the mask ping-pong halves are all absent from the parameter struct, so a
	// mask cannot reach its own output through this pass however it is scoped or ordered. That is
	// a property of what is bound, not of where the call happens to sit.
	void AddLayerValuesPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		if (LayerCtx.LayerValues)
		{
			return;
		}

		AddLayerInputPass(Ctx, LayerCtx, Layer);

		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		LayerCtx.LayerValues = GraphBuilder.CreateTexture(
			Ctx.OutputBC[0]->Desc, TEXT("Mixtormat.LayerValues"));

		FMixtormatLayerValuesCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatLayerValuesCS::FParameters>();
		Parameters->OutputSize = Request.Resolution;

		Parameters->OverrideBaseColor = Layer.bOverrideBaseColor ? 1u : 0u;
		Parameters->OverrideRoughness = Layer.bOverrideRoughness ? 1u : 0u;
		Parameters->FillColor = Layer.FillColor;
		Parameters->FillRoughness = Layer.FillRoughness;
		Parameters->HueShift = Layer.HueShift;
		Parameters->Saturation = Layer.Saturation;
		Parameters->Value = Layer.Value;
		Parameters->RoughnessBias = Layer.RoughnessBias;
		Parameters->RoughnessContrast = Layer.RoughnessContrast;
		Parameters->RoughnessOffset = Layer.RoughnessOffset;
		Parameters->LayerBC = LayerCtx.LayerInputBC;
		Parameters->LayerRAM = LayerCtx.LayerInputRAM;
		Parameters->LinearWrapSampler = TStaticSamplerState<
			SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		Parameters->OutputBC = GraphBuilder.CreateUAV(LayerCtx.LayerValues);

		TShaderMapRef<FMixtormatLayerValuesCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.LayerValues.Layer%d", LayerCtx.LayerIndex),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));
	}

	// One layer composited onto what the stack has accumulated below it.
	//
	// Everything the composite shader needs is gathered here: the layer's own parameters, the
	// two smoothing blurs that have to happen in a texture rather than per tap, the resolved
	// scalar Drivers, the HSV region tint applied at the albedo sample, and the Pattern UV
	// basis. It also takes the layer's Driver-signal snapshot, which can only be done here --
	// CombinedMask is final by this point and the mask halves are overwritten by the next layer.
	void AddLayerCompositePass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const uint32 PreparedLayerMode)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputBC = Ctx.OutputBC;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyRegionIds = Ctx.EmptyRegionIds;
		const FRDGTextureRef EmptyPatternUV = Ctx.EmptyPatternUV;
		const FRDGTextureRef EmptyPatternOrientation = Ctx.EmptyPatternOrientation;
		const FRDGTextureRef EmptyDriverSignal = Ctx.EmptyDriverSignal;
		TSet<FGuid>& DriverSnapshotDemand = Ctx.DriverSnapshotDemand;
		TMap<FGuid, FRDGTextureRef>& DriverSnapshots = Ctx.DriverSnapshots;
		TMap<int32, FRDGTextureRef>& HeightSnapshots = Ctx.HeightSnapshots;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& CombinedEffectData = LayerCtx.CombinedEffectData;
		FRDGTextureRef& CombinedEffectHeight = LayerCtx.CombinedEffectHeight;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		TShaderMapRef<FMixtormatCompositeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const int32 WriteIndex = LayerIndex & 1;
		const int32 ReadIndex = 1 - WriteIndex;
		FMixtormatCompositeCS::FParameters* Parameters =
			GraphBuilder.AllocParameters<FMixtormatCompositeCS::FParameters>();
		Parameters->OutputSize = Request.Resolution;
		Parameters->Enabled = Layer.bEnabled ? 1u : 0u;
		Parameters->HasMask = Layer.bHasMask ? 1u : 0u;

		Parameters->HasEffects = Layer.bHasEffects ? 1u : 0u;
		Parameters->OverrideBaseColor = Layer.bOverrideBaseColor ? 1u : 0u;
		Parameters->OverrideRoughness = Layer.bOverrideRoughness ? 1u : 0u;
		Parameters->OverrideMetallic = Layer.bOverrideMetallic ? 1u : 0u;
		Parameters->CompositionMode = Layer.bCoat ? 1u : 0u;
		Parameters->IsFill = Layer.bFill ? 1u : 0u;
		Parameters->IsGenerator = Layer.bGenerator ? 1u : 0u;
		Parameters->HasSurface = Layer.bHasSurface ? 1u : 0u;
		Parameters->PreparedLayerMode = PreparedLayerMode;
		// A generator wrote this layer's height, so it has one even with no RAMH (a fill).
		Parameters->HasPackedHeight = Layer.bHasPackedHeight || LayerCtx.bGeneratedHeight ? 1u : 0u;
		Parameters->HasSeparateHeight = Layer.SourceOutputs.IsValid() ? 1u : 0u;
		Parameters->LayerInputResolved = LayerCtx.LayerInputBC ? 1u : 0u;
		Parameters->UseSourceF0 = Layer.bUseSourceF0 ? 1u : 0u;
		Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
		Parameters->NormalOnly = Layer.bNormalOnly ? 1u : 0u;
		Parameters->OverrideNormal = Layer.bOverrideNormal ? 1u : 0u;
		Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
		Parameters->HeightOp = static_cast<uint32>(Layer.HeightBlend.Op);
		Parameters->HeightSoftness = Layer.HeightBlend.Softness;
		Parameters->HeightAmount = Layer.HeightBlend.Amount;
		Parameters->HeightSource = Layer.HeightSource;
		Parameters->InvertHeight = Layer.bInvertHeight ? 1u : 0u;
		Parameters->DirectHeightComparison = Layer.bDirectHeightComparison ? 1u : 0u;
		Parameters->InvertHeightFeature = Layer.bInvertHeightFeature ? 1u : 0u;
		Parameters->InvertAOFeature = Layer.bInvertAOFeature ? 1u : 0u;
		Parameters->InvertFeature = Layer.bInvertFeature ? 1u : 0u;
		Parameters->DebugMode = static_cast<uint32>(Request.DebugSettings.Mode);

		// Stain, Runoff and ClusterIds publish their own previews before this composite.
		// None has a case in the composite shader: exclude all three or DebugValue 0
		// would overwrite the selected child's preview with flat DebugLow.
		Parameters->WriteDebug =
			Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::None
			&& Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::Stain
			&& Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::Runoff
			&& Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::ChildOutput
			&& Request.DebugSettings.LayerIndex == LayerIndex ? 1u : 0u;
		Parameters->Opacity = Layer.Opacity;
		Parameters->Tiling = Layer.Tiling;
		Parameters->UVScaleX = Layer.UVScaleX;
		Parameters->UVScaleY = Layer.UVScaleY;
		Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
		Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
		Parameters->UVOffset = Layer.UVOffset;
		Parameters->Rotation = Layer.Rotation;
		Parameters->NormalIntensity = Layer.NormalIntensity;
		Parameters->HueShift = Layer.HueShift;
		Parameters->Saturation = Layer.Saturation;
		Parameters->Value = Layer.Value;
		Parameters->RoughnessBias = Layer.RoughnessBias;
		Parameters->RoughnessContrast = Layer.RoughnessContrast;
		Parameters->RoughnessOffset = Layer.RoughnessOffset;
		Parameters->FillRoughness = Layer.FillRoughness;
		Parameters->FillMetallic = Layer.FillMetallic;
		Parameters->LayerF0 = Layer.LayerF0;
		Parameters->HeightBoost = Layer.HeightBoost;
		Parameters->HeightLevelOffset = Layer.HeightLevelOffset;
		Parameters->HeightShape = Layer.HeightShape;
		Parameters->BaseColorBlendMode = static_cast<uint32>(Layer.BaseColorBlendMode);
		Parameters->BaseColorBlendAmount = Layer.BaseColorBlendAmount;
		Parameters->BaseColorInfluence = Layer.BaseColorInfluence;
		Parameters->RoughnessInfluence = Layer.RoughnessInfluence;
		Parameters->AOInfluence = Layer.AOInfluence;
		Parameters->MetallicInfluence = Layer.MetallicInfluence;
		Parameters->F0Influence = Layer.F0Influence;
		Parameters->NormalInfluence = Layer.NormalInfluence;
		Parameters->HeightInfluence = Layer.HeightInfluence;
		Parameters->HeightBlendAmount = Layer.HeightBlendAmount;
		Parameters->HeightThreshold = Layer.HeightBlend.Threshold;
		Parameters->HeightRange = Layer.HeightBlend.EdgeSoftness;
		Parameters->HeightContrast = Layer.HeightContrast;
		Parameters->HeightOffset = Layer.HeightBlend.BlendBias;
		Parameters->HeightBias = Layer.HeightBlend.BaseBias;
		Parameters->ConstantHeight = Layer.ConstantHeight;
		Parameters->MaskHeightInfluence = Layer.MaskHeightInfluence;
		Parameters->HeightContactAOAmount = Layer.HeightContactAOAmount;
		Parameters->HeightContactAOWidth = Layer.HeightContactAOWidth;
		Parameters->HeightBorderLift = Layer.HeightBorderLift;
		Parameters->HeightBorderWidth = Layer.HeightBorderWidth;
		Parameters->HeightBorderNormalStrength = BorderHeightDerivedNormalStrength;
		Parameters->SuppressReliefNormals = Ctx.Request.bFinalNormalFromHeight ? 1u : 0u;

		// Contact and border smoothing. The same separable Gaussian the mask smoothing
		// uses, run over the accumulated height the two fields are built from.
		//
		// It has to happen here rather than inside the composite, because a blur wants
		// the field already in a texture: evaluating the field per tap would cost four
		// texture reads each, and a kernel wide enough to matter would be dozens of
		// taps per pixel. Two separable passes over one texture is the same result for
		// a fraction of the work.
		//
		// Blurring the *source* rather than widening the derivative is the whole
		// point. A central difference taken further apart reaches further into the
		// noise instead of averaging it, which is why widening the measurement made
		// the stipple coarser rather than removing it.
		const bool bBorderActive =
			Layer.HeightBlend.Op == EMixtormatHeightOp::HeightBlend
			&& !Layer.bNormalOnly
			&& ((Layer.HeightContactAOAmount > 0.0f)
				|| FMath::Abs(Layer.HeightBorderLift) > 1.0e-4f);
		const bool bSmoothBorderField =
			bBorderActive && Layer.HeightBorderSmoothing > 1.0f;
		FRDGTextureRef BorderBaseHeight = HeightTargets[ReadIndex];
		if (bSmoothBorderField)
		{
			BorderBaseHeight = AddBorderHeightBlurPasses(Ctx, LayerCtx, Layer);
		}
		Parameters->BorderBaseHeight = BorderBaseHeight;
		Parameters->BorderSmoothValid = bSmoothBorderField ? 1u : 0u;
		Parameters->FeatureInfluence = Layer.FeatureInfluence;
		Parameters->FeatureBias = Layer.FeatureBias;
		Parameters->HeightFeatureInfluence = Layer.HeightFeatureInfluence;
		Parameters->AOFeatureInfluence = Layer.AOFeatureInfluence;
		Parameters->CurvatureRadius = Layer.CurvatureRadius;
		Parameters->CurvatureStrength = Layer.CurvatureStrength;
		Parameters->CurvaturePower = Layer.CurvaturePower;
		Parameters->CurvatureSmoothing = Layer.CurvatureSmoothing;
		Parameters->FillColor = Layer.FillColor;
		Parameters->PreviousBC = OutputBC[ReadIndex];
		Parameters->PreviousN = OutputN[ReadIndex];
		Parameters->PreviousRAM = OutputRAM[ReadIndex];
		Parameters->PreviousHeight = HeightTargets[ReadIndex];
		Parameters->PreviousOccupancy = LayerCtx.OccupancyTargets[ReadIndex];
		Parameters->ReferenceHeight = HeightTargets[ReadIndex];
		if (FRDGTextureRef* Snapshot = HeightSnapshots.Find(Layer.HeightReferenceLayerIndex))
		{
			Parameters->ReferenceHeight = *Snapshot;
		}
		Parameters->LayerBC = LayerCtx.LayerInputBC
			? LayerCtx.LayerInputBC
			: RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Layer.BaseColor,
				TEXT("Mixtormat.LayerBC"));
		Parameters->LayerN = LayerCtx.LayerInputN
			? LayerCtx.LayerInputN
			: RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Layer.Normal,
				TEXT("Mixtormat.LayerN"));
		Parameters->LayerRAM = LayerCtx.LayerInputRAM
			? LayerCtx.LayerInputRAM
			: RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Layer.RAM,
				TEXT("Mixtormat.LayerRAM"));
		Parameters->LayerSourceHeight = LayerCtx.LayerInputHeight
			? LayerCtx.LayerInputHeight
			: (Layer.Height.IsValid()
				? RegisterTexture(GraphBuilder, RegisteredTextures, Layer.Height,
					TEXT("Mixtormat.LayerSourceHeight"))
				: HeightTargets[ReadIndex]);
		Parameters->LayerMask = CombinedMask;


		// Rounding for the height field. A placement mask is a step, so the layer's
		// height falls from full to nothing across one texel and the layer reads as a
		// decal sitting on the surface. Blurring the mask and taking the height from
		// the blurred copy replaces that step with a ramp, and at a wide enough radius
		// the interior domes rather than merely softening at the rim.
		//
		// Its own pair of scratch targets, not the mask ping-pong halves: those are
		// the chain the next layer's mask children read and write, and blurring into
		// them would hand a later layer a mask nobody asked to smooth.
		FRDGTextureRef LayerHeightMask = CombinedMask;
		const bool bSmoothHeightMask =
			Layer.HeightSmoothRadius > 0.0f
			&& Layer.HeightSmoothAmount > 0.0f
			&& Layer.bHasMask
			&& !Layer.bNormalOnly;
		if (bSmoothHeightMask)
		{
			LayerHeightMask = AddHeightMaskBlurPasses(Ctx, LayerCtx, Layer);
		}
		Parameters->LayerHeightMask = LayerHeightMask;
		Parameters->HeightSmoothAmount =
			bSmoothHeightMask ? Layer.HeightSmoothAmount : 0.0f;
		Parameters->EffectData = CombinedEffectData;
		Parameters->EffectHeight = CombinedEffectHeight;
		Parameters->DebugMask = DebugMask;

		// Per-region colour variation. Applied here rather than in a pass of its own
		// because line-for-line this is the layer's colour-rewrite site already --
		// the same place HueShift/Saturation/Value are applied, and the ID map is
		// already in this pass's pixel space, so there is no second transform to get
		// wrong.
		//
		// One HSV filter per layer takes effect: the last enabled one that has a
		// cluster above it. Two of them do not compose into a single colour, they
		// each claim the whole albedo, so the later row wins rather than the two
		// silently averaging.
		const FHsvIdFilterRenderData* ActiveHsv = nullptr;
		FRDGTextureRef HsvRegionIds = nullptr;
		for (const FChildRenderData& Child : Layer.Children)
		{
			if (Child.Type != EMixtormatLayerChildType::HsvFilter)
			{
				continue;
			}
			if (FRDGTextureRef Ids = FindRegionIdsAbove(
				GetRegionIdView(Layer, RegionIdMaps, &Child), Child.SourceChildIndex))
			{
				ActiveHsv = &Child.HsvFilter;
				HsvRegionIds = Ids;
			}
		}
		Parameters->RegionTintEnabled = ActiveHsv != nullptr ? 1u : 0u;

		// CombinedMask is only a mask-chain texture once a mask child has written
		// one. Before that it is still the registered white UTexture2D the chain
		// started from -- BGRA8, at that asset's own size, not R16F at the composite
		// resolution. LayerMask gets away with binding it because the shader gates on
		// HasMask; a Driver signal is sampled unconditionally and a snapshot is
		// copied, so both have to check rather than assume.
		const bool bMaskIsSignalShaped =
			CombinedMask->Desc.Format == MaskDesc.Format
			&& CombinedMask->Desc.Extent == MaskDesc.Extent;

		// CombinedMask is final by here -- every mask-chain reassignment for
		// this layer has happened -- so this is the only place a snapshot can be
		// taken without capturing a half-built chain.
		if (bMaskIsSignalShaped
			&& DriverSnapshotDemand.Contains(Layer.LayerId)
			&& !DriverSnapshots.Contains(Layer.LayerId))
		{
			// Desc taken from the source, so the copy can never be handed two
			// incompatible descriptors.
			FRDGTextureDesc SnapshotDesc = CombinedMask->Desc;
			SnapshotDesc.Flags |= TexCreate_ShaderResource;
			FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
				SnapshotDesc,
				TEXT("Mixtormat.DriverSignalSnapshot"));
			AddCopyTexturePass(GraphBuilder, CombinedMask, Snapshot);
			DriverSnapshots.Add(Layer.LayerId, Snapshot);
		}

		// A source in this same layer reads the live mask and needs no copy at all.
		// One earlier in the stack reads its snapshot. One that has not composited
		// yet has none, so the Driver is switched off and the scalar keeps its value
		// -- the same rule an instance follows, and the reason nothing here needs a
		// dependency graph.
		FRDGTextureRef DriverSignals[FMixtormatCompositeCS::MaxScalarDrivers] =
			{ EmptyDriverSignal, EmptyDriverSignal };
		FRDGTextureRef DriverRegionSignals[FMixtormatCompositeCS::MaxScalarDrivers] =
			{ EmptyRegionIds, EmptyRegionIds };
		for (int32 SlotIndex = 0; SlotIndex < FMixtormatCompositeCS::MaxScalarDrivers; ++SlotIndex)
		{
			const FScalarDriverRenderData& Driver = Layer.ScalarDrivers[SlotIndex];
			FRDGTextureRef Signal = nullptr;
			FRDGTextureRef RegionSignal = nullptr;
			if (Driver.bEnabled && Driver.bRegionSource)
			{
				// The ID maps this layer produced, in this layer's own pixel space.
				// A named producer takes that producer's map; an unnamed one takes the
				// nearest above the composite, which is the same "nearest ID producer"
				// rule every other consumer follows. A region source in another layer
				// has no map here and stays unresolved rather than guessing.
				if (Driver.SourceLayerId == Layer.LayerId)
				{
					if (Driver.SourceChildIndex != INDEX_NONE)
					{
						for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
						{
							if (Entry.Key == Driver.SourceChildIndex)
							{
								RegionSignal = Entry.Value;
							}
						}
					}
					else
					{
						RegionSignal = FindRegionIdsAbove(
							GetRegionIdView(Layer, RegionIdMaps, nullptr), MAX_int32);
					}
				}
			}
			else if (Driver.bEnabled)
			{
				if (Driver.SourceLayerId == Layer.LayerId)
				{
					Signal = bMaskIsSignalShaped ? CombinedMask : nullptr;
				}
				else if (FRDGTextureRef* Found = DriverSnapshots.Find(Driver.SourceLayerId))
				{
					Signal = *Found;
				}
			}
			const bool bResolved = Driver.bRegionSource
				? RegionSignal != nullptr
				: Signal != nullptr;
			DriverSignals[SlotIndex] = Signal ? Signal : EmptyDriverSignal;
			DriverRegionSignals[SlotIndex] = RegionSignal ? RegionSignal : EmptyRegionIds;
			Parameters->DriverParamsC[SlotIndex] = FVector4f(
				bResolved && Driver.bRegionSource ? 1.0f : 0.0f,
				static_cast<float>(Driver.Seed),
				Driver.IdRandomMin,
				Driver.IdRandomMax);
			Parameters->DriverParamsA[SlotIndex] = FVector4f(
				bResolved ? 1.0f : 0.0f,
				Driver.bInvert ? 1.0f : 0.0f,
				Driver.InputMin,
				Driver.InputMax);
			Parameters->DriverParamsB[SlotIndex] = FVector4f(
				Driver.OutputMin,
				Driver.OutputMax,
				Driver.Amount,
				static_cast<float>(Driver.Combine));
		}
		Parameters->DriverSignal0 = DriverSignals[0];
		Parameters->DriverSignal1 = DriverSignals[1];
		Parameters->DriverRegionIds0 = DriverRegionSignals[0];
		Parameters->DriverRegionIds1 = DriverRegionSignals[1];

		FRDGTextureRef GradeMasks[FMixtormatCompositeCS::MaxLayerGrades];
		Parameters->GradeCount = FMath::Min(
			PendingGrades.Num(), FMixtormatCompositeCS::MaxLayerGrades);
		for (int32 GradeIndex = 0;
			GradeIndex < FMixtormatCompositeCS::MaxLayerGrades;
			++GradeIndex)
		{
			Parameters->GradeParamsA[GradeIndex] = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
			Parameters->GradeParamsB[GradeIndex] = FVector4f(0.18f, 1.0f, 0.0f, 0.0f);
			Parameters->GradeParamsC[GradeIndex] = FVector4f(1.0f, 0.0f, 1.0f, 0.0f);
			Parameters->GradeParamsD[GradeIndex] = FVector4f::Zero();
			GradeMasks[GradeIndex] = EmptyDriverSignal;
			if (PendingGrades.IsValidIndex(GradeIndex))
			{
				const FPendingEffect& Pending = PendingGrades[GradeIndex];
				const FEffectRenderData& Grade = *Pending.Effect;
				const bool bHasGradeMask = Layer.bHasMask || Pending.bHasScopedMask;
				Parameters->GradeParamsA[GradeIndex] = FVector4f(
					static_cast<float>(Grade.GradeTonemap), Grade.GradeTonemapStrength,
					Grade.GradeBrightness, Grade.GradeContrast);
				Parameters->GradeParamsB[GradeIndex] = FVector4f(
					Grade.GradeContrastPivot, Grade.GradeGamma, Grade.GradeAmount,
					Grade.GradeInputMin);
				Parameters->GradeParamsC[GradeIndex] = FVector4f(
					Grade.GradeInputMax, Grade.GradeOutputMin, Grade.GradeOutputMax,
					bHasGradeMask ? 1.0f : 0.0f);
				Parameters->GradeParamsD[GradeIndex] = FVector4f(
					Grade.GradeChannelBias.X, Grade.GradeChannelBias.Y,
					Grade.GradeChannelBias.Z,
					!Pending.bHasScopedMask && Grade.bGradeInvertMask ? 1.0f : 0.0f);
				GradeMasks[GradeIndex] = Pending.FeatureMask;
			}
		}
		Parameters->GradeMask0 = GradeMasks[0];
		Parameters->GradeMask1 = GradeMasks[1];
		Parameters->GradeMask2 = GradeMasks[2];
		Parameters->GradeMask3 = GradeMasks[3];
		Parameters->GradeMask4 = GradeMasks[4];
		Parameters->GradeMask5 = GradeMasks[5];
		Parameters->GradeMask6 = GradeMasks[6];
		Parameters->GradeMask7 = GradeMasks[7];
		Parameters->RegionIds = HsvRegionIds ? HsvRegionIds : EmptyRegionIds;
		Parameters->RegionSeed = ActiveHsv ? ActiveHsv->Seed : 0u;
		Parameters->RegionPaletteCount = ActiveHsv ? ActiveHsv->Palette.Num() : 0;
		for (int32 ColorIndex = 0; ColorIndex < FMixtormatCompositeCS::MaxRegionPalette; ++ColorIndex)
		{
			// The unused tail is filled rather than left alone, for the same reason
			// FMixtormatColorIdCS fills its own: a shader parameter array is not
			// zero initialised, and an uninitialised constant is the kind of thing
			// that only misbehaves on one driver.
			Parameters->RegionPalette[ColorIndex] =
				ActiveHsv && ActiveHsv->Palette.IsValidIndex(ColorIndex)
					? ActiveHsv->Palette[ColorIndex]
					: FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		}
		Parameters->RegionMixMin = ActiveHsv ? ActiveHsv->MixMin : 0.0f;
		Parameters->RegionMixMax = ActiveHsv ? ActiveHsv->MixMax : 0.0f;
		Parameters->RegionHueMin = ActiveHsv ? ActiveHsv->HueMin : 0.0f;
		Parameters->RegionHueMax = ActiveHsv ? ActiveHsv->HueMax : 0.0f;
		Parameters->RegionSatMin = ActiveHsv ? ActiveHsv->SatMin : 1.0f;
		Parameters->RegionSatMax = ActiveHsv ? ActiveHsv->SatMax : 1.0f;
		Parameters->RegionValMin = ActiveHsv ? ActiveHsv->ValMin : 1.0f;
		Parameters->RegionValMax = ActiveHsv ? ActiveHsv->ValMax : 1.0f;

		ApplyRegionUVParameters(*Parameters, ResolveRegionUVBinding(LayerCtx), Ctx);
		Parameters->ReferencedUVEnabled = !LayerCtx.LayerInputBC && LayerCtx.ReferencedUV ? 1u : 0u;
		Parameters->ReferencedUVField = LayerCtx.ReferencedUV ? LayerCtx.ReferencedUV : Ctx.EmptyPatternUV;

		Parameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		Parameters->OutputBC = GraphBuilder.CreateUAV(OutputBC[WriteIndex]);
		Parameters->OutputN = GraphBuilder.CreateUAV(OutputN[WriteIndex]);
		Parameters->OutputRAM = GraphBuilder.CreateUAV(OutputRAM[WriteIndex]);
		Parameters->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
		Parameters->OutputOccupancy = GraphBuilder.CreateUAV(LayerCtx.OccupancyTargets[WriteIndex]);
		// Only the selected layer writes debug output, and WriteDebug is already the shader's own
		// guard on that block. Binding the real target on every other layer would declare a UAV
		// dependency on the full-resolution debug target for a write that never happens, so those
		// bind the graph's 1x1 stand-in instead: valid, never touched, and off the real chain.
		Parameters->OutputDebug = Parameters->WriteDebug != 0
			? GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex])
			: GraphBuilder.CreateUAV(Ctx.EmptyDebugOutput);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Mixtormat.Composite.Layer%d", LayerIndex),
			Shader,
			Parameters,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));
	}

	void AddRotateOutputPass(
	FMixtormatComposeContext& Ctx,
	const int32 InputTargetIndex,
	const int32 OutputTargetIndex)
{
	FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
	const FRenderRequest& Request = Ctx.Request;
	TShaderMapRef<FMixtormatRotateOutputCS> RotateShader(
		GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FMixtormatRotateOutputCS::FParameters* Rotate =
		GraphBuilder.AllocParameters<FMixtormatRotateOutputCS::FParameters>();
	Rotate->OutputSize = Request.Resolution;
	Rotate->InputBC = Ctx.OutputBC[InputTargetIndex];
	Rotate->InputN = Ctx.OutputN[InputTargetIndex];
	Rotate->InputRAM = Ctx.OutputRAM[InputTargetIndex];
	Rotate->InputHeight = Ctx.OutputHeight[InputTargetIndex];
	Rotate->InputDebug = Ctx.OutputDebug[InputTargetIndex];
	Rotate->InputRegionIdPick = Ctx.OutputRegionIdPick[InputTargetIndex];
	Rotate->OutputBC = GraphBuilder.CreateUAV(Ctx.OutputBC[OutputTargetIndex]);
	Rotate->OutputN = GraphBuilder.CreateUAV(Ctx.OutputN[OutputTargetIndex]);
	Rotate->OutputRAM = GraphBuilder.CreateUAV(Ctx.OutputRAM[OutputTargetIndex]);
	Rotate->OutputHeight = GraphBuilder.CreateUAV(Ctx.OutputHeight[OutputTargetIndex]);
	Rotate->OutputDebug = GraphBuilder.CreateUAV(Ctx.OutputDebug[OutputTargetIndex]);
	Rotate->OutputRegionIdPick =
		GraphBuilder.CreateUAV(Ctx.OutputRegionIdPick[OutputTargetIndex]);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Mixtormat.RotateOutput90"),
		RotateShader,
		Rotate,
		FIntVector(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1));
}

}

FMixtormatGpuCompositor::FMixtormatGpuCompositor()
	: NetworkCache(MakeShared<FMixtormatNetworkCache, ESPMode::ThreadSafe>())
	, PrefixCache(MakeShared<FMixtormatPrefixCache, ESPMode::ThreadSafe>())
	, NodeCache(MakeShared<FMixtormatNodeCache, ESPMode::ThreadSafe>())
	, InFlight(MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false))
{
}

FMixtormatGpuCompositor::~FMixtormatGpuCompositor()
{
	// The entries hold pooled render targets, which have to be released on the render thread.
	// Dropping the last reference here would release them on whichever thread destroyed the
	// panel, so the contents go first and the shared pointer is left to expire on its own.
	if (NetworkCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatFlushNetworkCache)(
			[Cache = NetworkCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	NetworkCache.Reset();
	if (PrefixCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatFlushPrefixCache)(
			[Cache = PrefixCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	PrefixCache.Reset();
	if (NodeCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatFlushNodeCache)(
			[Cache = NodeCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	NodeCache.Reset();
}

bool FMixtormatGpuCompositor::IsComposeInFlight() const
{
	return InFlight.IsValid() && InFlight->load();
}

void FMixtormatGpuCompositor::ResetCaches()
{
	check(IsInGameThread());
	LastPrefixHashes.Reset();
	LastSnapshotLayer = INDEX_NONE;
	ReferenceCompositors.Reset();
	if (PrefixCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatResetPrefixCache)(
			[Cache = PrefixCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	if (NodeCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatResetNodeCache)(
			[Cache = NodeCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
}

bool FMixtormatGpuCompositor::Initialize(const FIntPoint InResolution)
{
	return InitializeTargets(InResolution, true);
}

bool FMixtormatGpuCompositor::InitializeTargets(
	const FIntPoint InResolution, const bool bWaitForResources)
{
	check(IsInGameThread());
	using namespace MixtormatGpuCompositor;
	if (InResolution.X <= 0 || InResolution.Y <= 0)
	{
		return false;
	}
	if (bInitialized && Resolution == InResolution)
	{
		return true;
	}

	Resolution = InResolution;
	for (FTargetSet& Set : Targets)
	{
		Set.BaseColor.Reset(CreateTarget(Resolution, FLinearColor::Black));
		Set.Normal.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.5f, 1.0f, 1.0f)));
		Set.RAM.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 1.0f, 0.0f, 0.04f)));
		Set.Height.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.0f, 0.0f, 0.0f), PF_R16F));
		Set.Debug.Reset(CreateTarget(Resolution, DebugClearColor()));
		// One channel of full float, cleared to the no-region sentinel. Not half: the ids are
		// pixel indices, so a 4096 composite reaches 16,777,215 and half float is exact only to
		// 2048. Not uint either -- a render target reads back as FLinearColor, and float32 holds
		// every id this tool can produce without loss.
		Set.RegionIdPick.Reset(CreateTarget(
			Resolution, FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f), PF_R32_FLOAT));
	}
	if (NetworkCache.IsValid())
	{
		// Entries are keyed on resolution, so stale ones could never be hit again. They would
		// still hold their pooled targets at the old size until six newer networks pushed them
		// out, which at 4K is a lot of memory to keep for nothing.
		ENQUEUE_RENDER_COMMAND(MixtormatResizeNetworkCache)(
			[Cache = NetworkCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	// Same reasoning for prefix snapshots, and referenced compositions own targets at the old size.
	ResetCaches();

	if (bWaitForResources)
	{
		FlushRenderingCommands();
	}
	PublishedTargetIndex = 0;
	bInitialized = true;
	return true;
}

bool FMixtormatGpuCompositor::RequestCompose(
	const TArray<FMixtormatLayer>& Layers,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings,
	const bool bRotateOutput90,
	const FSoftObjectPath& OwnerPath)
{
	return RequestCompose(Layers, TArray<FMixtormatLayerGroup>(), MoveTemp(OnComplete),
		DebugSettings, bRotateOutput90, OwnerPath);
}

bool FMixtormatGpuCompositor::RequestCompose(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings,
	const bool bRotateOutput90,
	const FSoftObjectPath& OwnerPath)
{
	check(IsInGameThread());
	FText ReferenceError;
	// Against the authored layers: groups add no SourceComposition of their own, and expansion
	// leaves every reference layer exactly where it was.
	if (!MixtormatCompositionReferences::Validate(Layers, OwnerPath, ReferenceError))
	{
		UE_LOG(LogMixtormatComposition, Warning, TEXT("%s"), *ReferenceError.ToString());
		return false;
	}
	TSet<const UMixtormatMaterial*> ActiveSources;
	return RequestComposeInternal(Layers, Groups, MoveTemp(OnComplete), DebugSettings,
		bRotateOutput90, ActiveSources);
}

bool FMixtormatGpuCompositor::RequestComposeInternal(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings,
	const bool bRotateOutput90,
	TSet<const UMixtormatMaterial*>& ActiveSources)
{
	using namespace MixtormatGpuCompositor;
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(Mixtormat_RequestCompose);

	// Groups become ordinary layers here and nowhere else. Below this point EffectiveLayers is the
	// only stack that exists -- resolving references against the authored array instead would look
	// up shared children by IDs that only exist in the expanded copy.
	TArray<FMixtormatLayer> ExpandedLayers;
	const bool bExpandGroups = MixtormatLayerGroups::RequiresExpansion(Layers, Groups);
	if (bExpandGroups)
	{
		MixtormatLayerGroups::BuildEffectiveLayers(Layers, Groups, ExpandedLayers);
	}
	const TArray<FMixtormatLayer>& EffectiveLayers = bExpandGroups ? ExpandedLayers : Layers;

	// ChildTarget names a child by (LayerId, ChildId); every pass below still addresses a child
	// by (LayerIndex, ChildIndex) the way LayerMask/ClusterIds always have, so resolve once here
	// rather than teach each pass file a second, GUID-based comparison.
	if (DebugSettings.Mode == EMixtormatDebugPreviewMode::ChildOutput)
	{
		DebugSettings.LayerIndex = INDEX_NONE;
		DebugSettings.ChildIndex = INDEX_NONE;
		for (int32 Index = 0; Index < EffectiveLayers.Num(); ++Index)
		{
			if (EffectiveLayers[Index].LayerId != DebugSettings.ChildTarget.OwnerId)
			{
				continue;
			}
			const FGuid TargetChildId = DebugSettings.ChildTarget.ChildId;
			if (!TargetChildId.IsValid())
			{
				if (EffectiveLayers[Index].Type != EMixtormatLayerType::Generator)
				{
					break;
				}
				DebugSettings.LayerIndex = Index;
				DebugSettings.ChildIndex = INDEX_NONE;
				break;
			}
			DebugSettings.LayerIndex = Index;
			DebugSettings.ChildIndex = EffectiveLayers[Index].Children.IndexOfByPredicate(
				[TargetChildId](const FMixtormatLayerChild& Candidate)
				{
					return Candidate.ChildId == TargetChildId;
				});
			break;
		}
	}

	if (!bInitialized && !Initialize())
	{
		return false;
	}
	// A quarter-turn swaps rectangular dimensions. The current compositor owns fixed-size output
	// targets, so reject that unsupported case rather than sampling outside either target.
	if (bRotateOutput90 && Resolution.X != Resolution.Y)
	{
		return false;
	}

	// Resolved once and kept. These are engine defaults that stand in for a missing map, so they
	// never change, and this runs on the game thread on every frame of a slider drag -- a package
	// lookup each time for two objects that were already resolved on the first composite.
	//
	// Strong pointers rather than weak: they are engine content that outlives the panel, and a
	// weak one would send us back through LoadObject the moment garbage collection ran with
	// nothing else referencing them.
	static TStrongObjectPtr<UTexture2D> CachedWhiteTexture;
	static TStrongObjectPtr<UTexture2D> CachedNormalTexture;
	if (!CachedWhiteTexture.IsValid())
	{
		CachedWhiteTexture.Reset(LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
	}
	if (!CachedNormalTexture.IsValid())
	{
		CachedNormalTexture.Reset(LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")));
	}

	UTexture2D* WhiteTexture = CachedWhiteTexture.Get();
	UTexture2D* NormalTexture = CachedNormalTexture.Get();
	if (!WhiteTexture || !NormalTexture)
	{
		return false;
	}

	FRenderRequest Request;
	Request.Resolution = Resolution;
	Request.DebugSettings = DebugSettings;
	Request.OnComplete = MoveTemp(OnComplete);
	Request.Targets = MakeShared<FMixtormatComposeResources, ESPMode::ThreadSafe>();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const auto CaptureTarget = [&Request](UTextureRenderTarget2D* Target)
		{
			Request.Targets->Pins.Emplace(Target);
			return Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		};
		Request.Targets->BaseColor[Index] = CaptureTarget(Targets[Index].BaseColor.Get());
		Request.Targets->Normal[Index] = CaptureTarget(Targets[Index].Normal.Get());
		Request.Targets->RAM[Index] = CaptureTarget(Targets[Index].RAM.Get());
		Request.Targets->Height[Index] = CaptureTarget(Targets[Index].Height.Get());
		Request.Targets->Debug[Index] = CaptureTarget(Targets[Index].Debug.Get());
		Request.Targets->RegionIdPick[Index] = CaptureTarget(Targets[Index].RegionIdPick.Get());
		if (!Request.Targets->BaseColor[Index] || !Request.Targets->Normal[Index]
			|| !Request.Targets->RAM[Index] || !Request.Targets->Height[Index]
			|| !Request.Targets->Debug[Index] || !Request.Targets->RegionIdPick[Index])
		{
			return false;
		}
	}

	const bool bUseComposeCache = CVarMixtormatDisableComposeCache.GetValueOnGameThread() == 0;
	// Layer-prefix and node caching; reference caching only needs bUseComposeCache.
	const bool bCacheLayers = bUseComposeCache && bCacheLayerResults;
	TSet<TObjectKey<UMixtormatMaterial>> TouchedReferences;
	TArray<uint64> PrefixHashes;
	PrefixHashes.Reserve(EffectiveLayers.Num());
	uint64 PrefixHash = MixtormatComposeHash::Combine(
		0x4D6978746F726D61ull,
		(static_cast<uint64>(static_cast<uint32>(Resolution.X)) << 32) | static_cast<uint32>(Resolution.Y));
	// Scheduler readiness, Pair/Min shader modes and configurable boundaries change cached results.
	PrefixHash = MixtormatComposeHash::Combine(PrefixHash, 0x526567696F6E5232ull);

	for (int32 LayerIndex = 0; LayerIndex < EffectiveLayers.Num(); ++LayerIndex)
	{
		FMixtormatLayer Layer = EffectiveLayers[LayerIndex];
		MixtormatParameterBinding::ApplyDirectReferences(
			FMixtormatBindingScope{EffectiveLayers, Groups}, Layer);
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		if (bCacheLayers)
		{
			// After direct references are applied, so a value borrowed from any other layer is part
			// of this layer's identity. Everything a layer reads from below is already covered by
			// chaining onto the prefix of the layers under it.
			TRACE_CPUPROFILER_EVENT_SCOPE(Mixtormat_HashLayer);
			MixtormatComposeHash::FHasher Hasher;
			Hasher.Struct(FMixtormatLayer::StaticStruct(), &Layer);
			PrefixHash = MixtormatComposeHash::Combine(PrefixHash, Hasher.Get());
			PrefixHashes.Add(PrefixHash);

			GatherLayerSourceCacheKey(Data, Layer);
		}
		const bool bReference = !Layer.SourceComposition.IsNull();
		if (bReference && Layer.bEnabled)
		{
			// Do not reinterpret malformed references as fills, surfaces or stale baked outputs.
			TStrongObjectPtr<UMixtormatMaterial> Source(ResolveLayerReference(Layer));
			if (IsInvalidLayerReference(Layer, Source, ActiveSources))
			{
				UE_LOG(LogMixtormatComposition, Warning,
					TEXT("Reference layer %d: missing source, invalid source contract, cycle or depth limit (32)."),
					LayerIndex);
				return false;
			}

			// One compositor per source asset, kept between composites. Source layer IDs, masks,
			// Drivers, direct references and ping-pong targets stay in the source's own graph, as
			// before; what changed is that an unchanged source is not recomposited, and its targets
			// are not reallocated, every frame. A second occurrence of the same source in one stack
			// reuses the first one's outputs -- same asset, same resolution, same pixels.
			if (bUseComposeCache)
			{
				uint64 SourceHash = 0;
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(Mixtormat_HashReference);
					MixtormatComposeHash::FHasher Hasher;
					Hasher.Object(Source.Get());
					SourceHash = Hasher.Get();
				}
				TouchedReferences.Add(TObjectKey<UMixtormatMaterial>(Source.Get()));
				TSharedPtr<FReferenceEntry>& Entry =
					ReferenceCompositors.FindOrAdd(TObjectKey<UMixtormatMaterial>(Source.Get()));
				if (!Entry.IsValid())
				{
					Entry = MakeShared<FReferenceEntry>();
					Entry->Compositor = MakeUnique<FMixtormatGpuCompositor>();
					Entry->Compositor->bCacheLayerResults = false;
				}
				if (!Entry->bValid || Entry->ContentHash != SourceHash
					|| !Entry->Compositor->PendingOutputs.IsValid())
				{
					Entry->bValid = false;
					ActiveSources.Add(Source.Get());
					const bool bComposed = Entry->Compositor->InitializeTargets(Resolution, false)
						&& Entry->Compositor->RequestComposeInternal(Source->Layers, Source->LayerGroups,
							FSimpleDelegate(), FMixtormatDebugPreviewSettings(), Source->bRotateUV90,
							ActiveSources);
					ActiveSources.Remove(Source.Get());
					if (!bComposed)
					{
						return false;
					}
					Entry->ContentHash = SourceHash;
					Entry->bValid = true;
				}
				Data.SourceOutputs = Entry->Compositor->PendingOutputs;
			}
			else
			{
				FMixtormatGpuCompositor SourceCompositor;
				ActiveSources.Add(Source.Get());
				const bool bComposed = SourceCompositor.InitializeTargets(Resolution, false)
					&& SourceCompositor.RequestComposeInternal(Source->Layers, Source->LayerGroups,
						FSimpleDelegate(), FMixtormatDebugPreviewSettings(), Source->bRotateUV90,
						ActiveSources);
				ActiveSources.Remove(Source.Get());
				if (!bComposed)
				{
					return false;
				}
				Data.SourceOutputs = SourceCompositor.PendingOutputs;
			}
			Data.bUseSourceF0 = !Layer.bOverrideIOR;
		}
		const UMixtormatSurface* Surface = nullptr;
		UTexture2D* LayerNormal = nullptr;
		if (!GatherLayerSource(Data, Layer, WhiteTexture, NormalTexture, Surface, LayerNormal))
		{
			return false;
		}
		TArray<int32> ScopeOwners;
		TArray<bool> DisabledGroupScopes;
		ScopeOwners.Init(INDEX_NONE, Layer.Children.Num());
		DisabledGroupScopes.Init(false, Layer.Children.Num());
		for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
		{
			const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, Index);
			if (OwnerIndex != INDEX_NONE)
			{
				ScopeOwners[Index] = OwnerIndex;
				const FMixtormatLayerChild& Parent = Layer.Children[OwnerIndex];
				DisabledGroupScopes[Index] = DisabledGroupScopes[OwnerIndex]
					|| (Parent.Type == EMixtormatLayerChildType::IdGroup && !Parent.IdGroup.bEnabled);
			}
		}
		const bool bGeneratorLayer = Layer.Type == EMixtormatLayerType::Generator;
		const uint64 PlacementKey = GatherLayerPlacementKey(Layer, bGeneratorLayer);
		for (int32 SourceChildIndex = 0; SourceChildIndex < Layer.Children.Num(); ++SourceChildIndex)
		{
			const FMixtormatLayerChild& LayerChild = Layer.Children[SourceChildIndex];
			if (SourceChildIndex < DisabledGroupScopes.Num() && DisabledGroupScopes[SourceChildIndex])
			{
				continue;
			}

			// Mask children still resolve on disabled layers so other layers can reference them.
			// Effects never contribute to that mask, and effect filters run after the disabled composite,
			// so capturing them would let a hidden layer modify the accumulated result.
			if (!Layer.bEnabled && LayerChild.Type == EMixtormatLayerChildType::Effect)
			{
				continue;
			}

			if (IsIdGatherChild(LayerChild.Type))
			{
				if (!GatherIdChild(Data, Layer, LayerChild, SourceChildIndex, LayerIndex,
					EffectiveLayers, Surface, WhiteTexture, bCacheLayers))
				{
					return false;
				}
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Generator)
			{
				GatherGeneratorChild(Data, Layer, LayerChild, SourceChildIndex,
					bGeneratorLayer, bCacheLayers, PlacementKey);
				continue;
			}

			if (IsMaskGatherChild(LayerChild.Type))
			{
				if (!GatherMaskChild(Data, Layer, LayerChild, SourceChildIndex, EffectiveLayers))
				{
					return false;
				}
				continue;
			}

			const FMixtormatLayerEffect& LayerEffect = LayerChild.Effect;
			if (!LayerEffect.bEnabled)
			{
				continue;
			}

			const UMixtormatEffect* EffectAsset = LayerEffect.Effect.LoadSynchronous();
			// Peeling is procedural-only. Asset-backed Peeling children are intentionally ignored.
			if (EffectAsset && EffectAsset->EffectType == EMixtormatEffectType::Peeling)
			{
				continue;
			}

			// Procedural effects carry no asset from which to read their type.
			const bool bProcedural =
				!EffectAsset
				&& (MixtormatEffectClassOf(LayerEffect.ProceduralType) == EMixtormatEffectClass::Filter
					|| LayerEffect.ProceduralType == EMixtormatEffectType::Peeling);
			if (!EffectAsset && !bProcedural)
			{
				continue;
			}
			const EMixtormatEffectType ResolvedType =
				EffectAsset ? EffectAsset->EffectType : LayerEffect.ProceduralType;
			if (ResolvedType == EMixtormatEffectType::Grade)
			{
				int32 GradeCount = 0;
				for (const FChildRenderData& Existing : Data.Children)
				{
					if (Existing.Type == EMixtormatLayerChildType::Effect
						&& Existing.Effect.Type == EMixtormatEffectType::Grade)
					{
						++GradeCount;
					}
				}
				if (GradeCount >= FMixtormatCompositeCS::MaxLayerGrades)
				{
					UE_LOG(LogMixtormatComposition, Warning,
						TEXT("Layer %d exceeds the supported maximum of %d Grade effects."),
						LayerIndex, FMixtormatCompositeCS::MaxLayerGrades);
					return false;
				}
			}
			FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
			ChildData.Type = EMixtormatLayerChildType::Effect;
			ChildData.SourceChildIndex = SourceChildIndex;
			if (LayerChild.ScopeOwnerChildId.IsValid())
			{
				const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
				bool bWarpableOwner = false;
				bool bFlowGeneratorOwner = false;
				if (OwnerIndex != INDEX_NONE)
				{
					const FMixtormatLayerChild& Owner = Layer.Children[OwnerIndex];
					bWarpableOwner = Owner.Type == EMixtormatLayerChildType::Mask;
					// Generator flow tools transform their owning Rock Formation's field. A
					// disabled owner produces no field, so its tools go with it.
					bFlowGeneratorOwner = Owner.Type == EMixtormatLayerChildType::Generator
						&& Layer.Type == EMixtormatLayerType::Generator
						&& MixtormatCanOwnGeneratorFlow(Owner.Generator.Type)
						&& Owner.Generator.bEnabled;
					if (Owner.Type == EMixtormatLayerChildType::Effect)
					{
						const UMixtormatEffect* OwnerAsset = Owner.Effect.Effect.LoadSynchronous();
						const EMixtormatEffectType OwnerType = OwnerAsset
							? OwnerAsset->EffectType
							: Owner.Effect.ProceduralType;
						bWarpableOwner =
							MixtormatEffectClassOf(OwnerType) == EMixtormatEffectClass::Surface;
					}
				}
				const bool bValidFlowWarpScope =
					(ResolvedType == EMixtormatEffectType::FlowWarp && bWarpableOwner)
					|| (MixtormatIsGeneratorFlowEffect(ResolvedType) && bFlowGeneratorOwner);
				if (!bValidFlowWarpScope)
				{
					Data.Children.RemoveAt(Data.Children.Num() - 1);
					continue;
				}
				ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
			}
			else if (MixtormatIsGeneratorFlowEffect(ResolvedType))
			{
				// A flow tool must sit under the module whose field it transforms.
				Data.Children.RemoveAt(Data.Children.Num() - 1);
				continue;
			}
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = ResolvedType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			// Shared enable/blend control across every effect family; not family-keyed, so
			// it keeps its literal bound instead of a definition entry.
			EffectData.Strength = LayerEffect.Strength;

			// Per-family gathers, one call each: authored values sanitized through the runtime
			// contract table, derived math local to the family (Compositing/MixtormatEffectGather).
			// Pure moves of the previously inline blocks, in this if-chain's order. The Filter
			// dispatch and the procedural-peel gather below stay inline until their own migration
			// (plan §3 Phase 7).
			if (ResolvedType == EMixtormatEffectType::Erosion)
			{
				GatherErosion(EffectData, LayerEffect);
			}

			if (ResolvedType == EMixtormatEffectType::Grade)
			{
				GatherGrade(EffectData, LayerEffect);
			}

			if (ResolvedType == EMixtormatEffectType::Breakup)
			{
				GatherBreakup(EffectData, LayerEffect);
			}


			if (ResolvedType == EMixtormatEffectType::LayerBlur)
			{
				GatherLayerBlur(EffectData, LayerEffect);
			}

			if (ResolvedType == EMixtormatEffectType::FlowWarp)
			{
				GatherFlowWarp(EffectData, LayerEffect);
			}

			if (MixtormatIsGeneratorFlowEffect(ResolvedType))
			{
				GatherGeneratorFlow(EffectData, LayerEffect);
			}

			if (ResolvedType == EMixtormatEffectType::WornEdges)
			{
				GatherWornEdges(EffectData, LayerEffect);
			}

			if (ResolvedType == EMixtormatEffectType::Stain)
			{
				GatherStain(EffectData, LayerEffect, Data.bHasMask);
			}

			if (ResolvedType == EMixtormatEffectType::Runoff)
			{
				GatherRunoff(EffectData, LayerEffect, Data.bHasMask);
			}

			// Filters and generator flow tools have no post-composite effect data. bHasEffects
			// stays clear for them, so the composite never samples an unwritten effect target.
			//
			// Gated on the class, not on the absence of an asset. Erosion got away with the
			// narrower test because nothing creates Erosion assets, but Grade is a valid
			// EffectType on UMixtormatEffect, so an authored Grade asset would fall through
			// into the peel branches below and trip exactly the failure above.
			if (MixtormatEffectClassOf(ResolvedType) == EMixtormatEffectClass::Filter
				|| MixtormatIsGeneratorFlowEffect(ResolvedType))
			{
				continue;
			}

			// Peeling is procedural-only. Its authored values are gathered through the same
			// contract sanitizer as the migrated effect families.
			GatherPeeling(EffectData, LayerEffect);
			Data.bHasEffects = true;
			continue;

		}

		// One gather step for every ID producer/consumer; Effect/Generator mask ownership
		// remains with its existing gather path above.
		for (FChildRenderData& Child : Data.Children)
		{
			if (IsIdGatherChild(Child.Type))
			{
				Child.ScopeOwnerSourceChildIndex = ScopeOwners[Child.SourceChildIndex];
			}
		}

		GatherLayerFields(Data, Layer, LayerIndex, Surface, LayerNormal);
	}

	const int32 CompositedTargetIndex = Request.Layers.IsEmpty()
		? 0
		: (Request.Layers.Num() - 1) & 1;
	Request.PublishedTargetIndex = CompositedTargetIndex;
	Request.bRotateOutput90 = bRotateOutput90;
	PublishedTargetIndex = bRotateOutput90
		? 1 - CompositedTargetIndex
		: CompositedTargetIndex;
	Request.NetworkCache = NetworkCache;
	Request.Targets->PublishedIndex = PublishedTargetIndex;
	PendingOutputs = Request.Targets;

	// Layer-prefix cache. The snapshot goes just below the lowest layer this request changed:
	// while a value on layer E is being dragged, layers 0..E-1 hash the same every frame, so they
	// are composited once and every later frame starts at E.
	// Sources no longer referenced by this stack release their compositor and its targets.
	if (bUseComposeCache)
	{
		for (auto It = ReferenceCompositors.CreateIterator(); It; ++It)
		{
			if (!TouchedReferences.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}
	else
	{
		ReferenceCompositors.Reset();
	}

	if (bCacheLayers && PrefixHashes.Num() == Request.Layers.Num() && PrefixCache.IsValid())
	{
		int32 FirstChanged = 0;
		while (FirstChanged < PrefixHashes.Num()
			&& LastPrefixHashes.IsValidIndex(FirstChanged)
			&& LastPrefixHashes[FirstChanged] == PrefixHashes[FirstChanged])
		{
			++FirstChanged;
		}
		if (FirstChanged < PrefixHashes.Num())
		{
			// Something at or above FirstChanged differs from the last request.
			LastSnapshotLayer = FirstChanged - 1;
		}
		else if (!PrefixHashes.IsValidIndex(LastSnapshotLayer))
		{
			// Nothing changed (a debug toggle, a re-request); keep the previous edit point.
			LastSnapshotLayer = INDEX_NONE;
		}
		// The top layer is never worth keeping: nothing composites above it.
		Request.SnapshotLayer = LastSnapshotLayer < PrefixHashes.Num() - 1 ? LastSnapshotLayer : INDEX_NONE;
		LastPrefixHashes = PrefixHashes;

		// A debug view writes from inside the layer it inspects, and the global debug modes from
		// whichever layer the settings name; neither may be skipped. Nor saved from: a debug
		// composite takes the same path for the surface today, but a snapshot is only ever taken
		// from a plain composite so that can never become an assumption.
		if (DebugSettings.Mode != EMixtormatDebugPreviewMode::None)
		{
			Request.CacheLayerLimit = DebugSettings.LayerIndex >= 0 ? DebugSettings.LayerIndex : 0;
			Request.SnapshotLayer = INDEX_NONE;
		}
		Request.PrefixHashes = MoveTemp(PrefixHashes);
		Request.PrefixCache = PrefixCache;
		Request.CacheBudgetBytes = static_cast<uint64>(
			FMath::Max(CVarMixtormatComposeCacheBudgetMB.GetValueOnGameThread(), 0)) * 1024ull * 1024ull;
	}
	else
	{
		LastPrefixHashes.Reset();
		LastSnapshotLayer = INDEX_NONE;
	}

	if (bCacheLayers)
	{
		Request.NodeCache = NodeCache;
	}
	Request.FinalAOAmount = FMath::IsFinite(FinalAOAmount) ? FinalAOAmount : 0.0f;
	Request.FinalAORadius = FMath::IsFinite(FinalAORadius) ? FinalAORadius : 8.0f;
	Request.bFinalNormalFromHeight = bFinalNormalFromHeight;
	Request.FinalNormalStrength = FMath::IsFinite(FinalNormalStrength) ? FinalNormalStrength : 1.0f;
	Request.bFinalAutoRemapHeight = bFinalAutoRemapHeight;
	Request.InFlight = InFlight;
	InFlight->store(true);
	EnqueueCompose(MoveTemp(Request));

	return true;
}

void FMixtormatGpuCompositor::BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const
{
	MaterialInstance.SetTextureParameterValue(TEXT("DA_BaseColor"), GetBaseColorOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("DA_Normal"), GetNormalOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("DA_RAMH"), GetRAMOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("DA_Height"), GetHeightOutput());
	MaterialInstance.SetScalarParameterValue(TEXT("DA_Tiling"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_RoughnessBias"), 0.5f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_RoughnessContrast"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_RoughnessOffset"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_NormalIntensity"), 1.0f);
	// DA_DielectricF0 / DA_UsePackedF0 intentionally not set: M_Mixtormat_Substrate has no such
	// parameters -- it derives F0 itself via MaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0
	// (BaseColor + Metallic), so there is nothing in the graph for these two names to bind to.
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetBaseColorOutput() const
{
	return Targets[PublishedTargetIndex].BaseColor.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetNormalOutput() const
{
	return Targets[PublishedTargetIndex].Normal.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetRAMOutput() const
{
	return Targets[PublishedTargetIndex].RAM.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetHeightOutput() const
{
	return Targets[PublishedTargetIndex].Height.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetRegionIdPickOutput() const
{
	return Targets[PublishedTargetIndex].RegionIdPick.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetDebugOutput() const
{
	return Targets[PublishedTargetIndex].Debug.Get();
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FMixtormatPrompt2RegionUVTest,
	"Mixtormat.Prompt2.RegionUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FMixtormatPrompt2RegionUVTest::GetTests(
	TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Name : {
		TEXT("LegacyOnly"), TEXT("LegacyThenUV"), TEXT("UVThenLaterLegacy"),
		TEXT("UVThenLaterUV"), TEXT("LaterNeutralPatternIgnored"),
		TEXT("NoProducerNoOp"), TEXT("RegionSources"), TEXT("LegacyIntrinsicOrientation") })
	{
		OutBeautifiedNames.Add(Name);
		OutTestCommands.Add(Name);
	}
}

bool FMixtormatPrompt2RegionUVTest::RunTest(const FString& Parameters)
{
	// Keep RDG resources and comparisons on the render thread; report only after the flush.
	TArray<FString> Failures;
	ENQUEUE_RENDER_COMMAND(MixtormatPrompt2RegionUV)(
		[Parameters, &Failures](FRHICommandListImmediate& RHICmdList)
		{
			using namespace MixtormatGpuCompositor;
			const auto Check = [&Failures](bool bCondition, const TCHAR* Message)
			{
				if (!bCondition)
				{
					Failures.Add(Message);
				}
			};
			FRDGBuilder GraphBuilder(RHICmdList);
			FRenderRequest Request;
			Request.Resolution = FIntPoint(4, 4);
			FMixtormatComposeContext Ctx(GraphBuilder, Request);
			FMixtormatLayerPassContext LayerCtx(Ctx);
			LayerCtx.BeginLayer(0);
			const auto Texture = [&GraphBuilder](EPixelFormat Format, const TCHAR* Name)
			{
				return GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
					FIntPoint(4, 4), Format, FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV), Name);
			};
			const FRDGTextureRef PatternIds = Texture(PF_R32_UINT, TEXT("Prompt2.PatternIds"));
			const FRDGTextureRef UvIds = Texture(PF_R32_UINT, TEXT("Prompt2.UvIds"));
			const FRDGTextureRef LaterIds = Texture(PF_R32_UINT, TEXT("Prompt2.LaterIds"));
			const FRDGTextureRef PatternUV = Texture(PF_G32R32F, TEXT("Prompt2.PatternUV"));
			const FRDGTextureRef CentreUV = Texture(PF_G32R32F, TEXT("Prompt2.CentreUV"));
			const FRDGTextureRef Orientation = Texture(PF_R32_FLOAT, TEXT("Prompt2.Orientation"));

			if (Parameters == TEXT("RegionSources"))
			{
				// Exercise the real publication/lookup helpers, not producer shader generation.
				PublishRegionIds(LayerCtx.RegionIdMaps, 6, LaterIds); // ID Group
				PublishRegionIds(LayerCtx.RegionIdMaps, 1, PatternIds); // Pattern
				PublishRegionIds(LayerCtx.RegionIdMaps, 3, UvIds); // Cluster
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 1) == nullptr,
					TEXT("A producer cannot source itself or a consumer above it"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 3) == PatternIds,
					TEXT("Pattern is the nearest earlier source"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 6) == UvIds,
					TEXT("Cluster is the nearest earlier source"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 7) == LaterIds,
					TEXT("ID Group is the nearest earlier source despite publication order"));
				GraphBuilder.Execute();
				return;
			}

			FPatternIdRenderData Pattern;
			Pattern.bUVVariation = true;
			Pattern.bOrthogonalUV = false;
			Pattern.bRandomFlipU = true;
			Pattern.Seed = 11;
			Pattern.UVRotationMin = -30.0f;
			Pattern.UVRotationMax = 60.0f;
			Pattern.UVScaleMin = 0.5f;
			Pattern.UVScaleMax = 2.0f;
			Pattern.UVOffset = 0.25f;
			FPatternIdRenderData NeutralPattern;
			FUvIdRenderData Uv;
			Uv.bOrthogonal = true;
			Uv.bRandomFlipV = true;
			Uv.Seed = 22;
			Uv.RotationMin = -90.0f;
			Uv.RotationMax = 180.0f;
			Uv.ScaleMin = 0.75f;
			Uv.ScaleMax = 1.5f;
			Uv.OffsetU = 0.1f;
			Uv.OffsetV = 0.3f;
			FUvIdRenderData LaterUv = Uv;
			LaterUv.Seed = 33;
			const auto AddPattern = [&](int32 Index, const FPatternIdRenderData& Settings,
				FRDGTextureRef Ids)
			{
				FPatternIdPassOutput& Output = LayerCtx.PatternOutputs.AddDefaulted_GetRef();
				Output.SourceChildIndex = Index;
				Output.Settings = &Settings;
				Output.Ids = Ids;
				Output.UV = PatternUV;
				Output.Orientation = Orientation;
			};
			const auto AddUv = [&](int32 Index, const FUvIdRenderData& Settings,
				FRDGTextureRef Ids)
			{
				FUvIdPassOutput& Output = LayerCtx.UvIdOutputs.AddDefaulted_GetRef();
				Output.SourceChildIndex = Index;
				Output.Settings = &Settings;
				Output.Ids = Ids;
				Output.CentreUV = CentreUV;
			};
			const bool bNoProducer = Parameters == TEXT("NoProducerNoOp");
			const bool bIntrinsic = Parameters == TEXT("LegacyIntrinsicOrientation");
			const bool bLegacy = Parameters == TEXT("LegacyOnly")
				|| Parameters == TEXT("UVThenLaterLegacy") || bIntrinsic;
			const bool bLaterUv = Parameters == TEXT("UVThenLaterUV");
			if (bNoProducer)
			{
				FLayerRenderData Layer;
				Layer.bEnabled = true;
				FChildRenderData& UvChild = Layer.Children.AddDefaulted_GetRef();
				UvChild.Type = EMixtormatLayerChildType::UvFromIds;
				UvChild.SourceChildIndex = 1;
				FChildRenderData& ReliefChild = Layer.Children.AddDefaulted_GetRef();
				ReliefChild.Type = EMixtormatLayerChildType::ReliefFromIds;
				ReliefChild.SourceChildIndex = 2;
				ReliefChild.ReliefId.HeightAmount = 0.5f;
				AddUvIdPasses(Ctx, LayerCtx, Layer);
				CollectPendingRampTilts(Ctx, LayerCtx, Layer);
				Check(LayerCtx.UvIdOutputs.IsEmpty(), TEXT("Missing IDs publish no UV output"));
				Check(LayerCtx.PendingRampTilts.IsEmpty(), TEXT("Missing IDs queue no relief"));
				Check(LayerCtx.RegionCentreCache.IsEmpty() && LayerCtx.RegionDistanceCache.IsEmpty(),
					TEXT("Missing IDs allocate no centre or distance fields"));
			}
			else if (bLegacy)
			{
				if (Parameters == TEXT("UVThenLaterLegacy"))
				{
					AddUv(3, Uv, UvIds);
				}
				if (bIntrinsic)
				{
					Pattern.bUVVariation = false;
					Pattern.PatternMode = EMixtormatPatternMode::Herringbone;
				}
				AddPattern(5, Pattern, PatternIds);
				AddPattern(1, Pattern, LaterIds); // Array order must not override child order.
			}
			else
			{
				AddPattern(1, Pattern, PatternIds);
				if (bLaterUv)
				{
					AddUv(5, LaterUv, LaterIds);
				}
				AddUv(3, Uv, UvIds);
				if (Parameters == TEXT("LaterNeutralPatternIgnored"))
				{
					AddPattern(7, NeutralPattern, LaterIds);
				}
			}

			// Each required input is independently absent on a later row of each producer type.
			for (int32 Missing = 0; Missing < 3; ++Missing)
			{
				AddPattern(10 + Missing, Pattern, PatternIds);
				FPatternIdPassOutput& P = LayerCtx.PatternOutputs.Last();
				AddUv(20 + Missing, Uv, UvIds);
				FUvIdPassOutput& U = LayerCtx.UvIdOutputs.Last();
				if (Missing == 0) { P.Settings = nullptr; U.Settings = nullptr; }
				if (Missing == 1) { P.Ids = nullptr; U.Ids = nullptr; }
				if (Missing == 2) { P.UV = nullptr; U.CentreUV = nullptr; }
			}
			const FRegionUVBinding Binding = ResolveRegionUVBinding(LayerCtx);
			Check(Binding.bEnabled == !bNoProducer, TEXT("Binding enabled only for a valid producer"));
			if (bNoProducer)
			{
				Check(!Binding.bVariation && !Binding.bIntrinsicOrientation,
					TEXT("No producer enables no UV treatment"));
				Check(!Binding.Ids && !Binding.Centre && !Binding.Orientation,
					TEXT("No producer binds no textures"));
				Check(Binding.ScaleMin == 1.0f && Binding.ScaleMax == 1.0f
					&& Binding.RotationMin == 0.0f && Binding.RotationMax == 0.0f
					&& Binding.Offset == FVector2f::ZeroVector,
					TEXT("No producer leaves an identity transform"));
			}
			else
			{
				Check(Binding.Ids == (bLegacy ? PatternIds : (bLaterUv ? LaterIds : UvIds)),
					TEXT("Highest valid SourceChildIndex wins across both output arrays"));
				Check(Binding.Centre == (bLegacy ? PatternUV : CentreUV), TEXT("Winner supplies centre"));
				Check(Binding.Orientation == (bLegacy ? Orientation : nullptr), TEXT("Winner supplies orientation"));
				Check(Binding.bVariation == !bIntrinsic && Binding.bIntrinsicOrientation == bIntrinsic,
					TEXT("Variation and intrinsic orientation remain independent"));
				Check(Binding.Seed == (bLegacy ? 11u : (bLaterUv ? 33u : 22u)), TEXT("Winner supplies seed"));
				Check(Binding.bOrthogonal == !bLegacy && Binding.bRandomFlipU == bLegacy
					&& Binding.bRandomFlipV == !bLegacy, TEXT("Winner supplies orthogonal and flip flags"));
				Check(Binding.RotationMin == (bLegacy ? -30.0f : -90.0f)
					&& Binding.RotationMax == (bLegacy ? 60.0f : 180.0f), TEXT("Winner supplies rotation"));
				Check(Binding.ScaleMin == (bLegacy ? 0.5f : 0.75f)
					&& Binding.ScaleMax == (bLegacy ? 2.0f : 1.5f), TEXT("Transforms are selected, not compounded"));
				Check(Binding.Offset == (bLegacy ? FVector2f(0.25f, 0.25f) : FVector2f(0.1f, 0.3f)),
					TEXT("Legacy scalar offset expands to both axes; UV offsets stay independent"));
			}
			GraphBuilder.Execute();
		});
	FlushRenderingCommands();
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return Failures.IsEmpty();
}

#endif // WITH_DEV_AUTOMATION_TESTS
