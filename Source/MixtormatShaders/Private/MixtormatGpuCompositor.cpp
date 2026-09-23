// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositor.h"

#include "Compositing/MixtormatEffectGather.h"
#include "MixtormatGpuCompositorInternal.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
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

// The ground every stack composites onto.
//
// Deliberately not a layer. It has no row, no selection, no children and no inspector -- it
// exists so that the bottom of the stack is an ordinary position rather than a privileged one.
// Before it, layer 0 seeded these buffers by replacing them, which meant the bottom layer
// ignored its own mask, feature influence and height blend; a layer therefore rendered
// differently depending on where it sat, and could not be freely dragged to the bottom.
//

namespace MixtormatNetworkKey
{
	// FNV-1a over the raw bytes of whatever is fed in. The values are floats straight out of the
	// panel, so this hashes bit patterns rather than magnitudes -- which is what is wanted: two
	// settings that differ anywhere at all must miss, and a value that round-trips through the
	// UI unchanged must hit.
	inline uint64 Combine(const uint64 Hash, const void* Data, const int32 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		uint64 Result = Hash;
		for (int32 Index = 0; Index < Size; ++Index)
		{
			Result ^= static_cast<uint64>(Bytes[Index]);
			Result *= 1099511628211ull;
		}
		return Result;
	}

	template <typename T>
	inline uint64 Add(const uint64 Hash, const T& Value)
	{
		static_assert(TIsPODType<T>::Value, "Network key inputs are hashed as raw bytes.");
		return Combine(Hash, &Value, sizeof(T));
	}

	inline uint64 Seed()
	{
		return 14695981039346656037ull;
	}
}

class FMixtormatCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCompositeCS, FGlobalShader);

	// Has to agree with MIXTORMAT_MAX_REGION_PALETTE in MixtormatComposite.usf and with
	// FMixtormatHsvIdFilter::MaxPaletteColors. Three copies of one number, and the gather loop
	// clamps against this one.
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
		SHADER_PARAMETER(uint32, HeightBlendEnabled)
		SHADER_PARAMETER(uint32, SmoothHeightMerge)
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionUVOrientationField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DebugMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionUVOrientationField)
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
	// Combine IDs. Pattern IDs' own UV block is the legacy path, kept live and unchanged so a
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
		Parameters->HasSurface = Layer.bHasSurface ? 1u : 0u;
		Parameters->PreparedLayerMode = PreparedLayerMode;
		Parameters->HasPackedHeight = Layer.bHasPackedHeight ? 1u : 0u;
		Parameters->HasSeparateHeight = Layer.SourceOutputs.IsValid() ? 1u : 0u;
		Parameters->LayerInputResolved = LayerCtx.LayerInputBC ? 1u : 0u;
		Parameters->UseSourceF0 = Layer.bUseSourceF0 ? 1u : 0u;
		Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
		Parameters->NormalOnly = Layer.bNormalOnly ? 1u : 0u;
		Parameters->OverrideNormal = Layer.bOverrideNormal ? 1u : 0u;
		Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
		Parameters->HeightBlendEnabled = Layer.bHeightBlendEnabled ? 1u : 0u;
		Parameters->SmoothHeightMerge = Layer.bSmoothHeightMerge ? 1u : 0u;
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
		Parameters->HeightThreshold = Layer.HeightThreshold;
		Parameters->HeightRange = Layer.HeightRange;
		Parameters->HeightContrast = Layer.HeightContrast;
		Parameters->HeightOffset = Layer.HeightOffset;
		Parameters->HeightBias = Layer.HeightBias;
		Parameters->ConstantHeight = Layer.ConstantHeight;
		Parameters->MaskHeightInfluence = Layer.MaskHeightInfluence;
		Parameters->HeightContactAOAmount = Layer.HeightContactAOAmount;
		Parameters->HeightContactAOWidth = Layer.HeightContactAOWidth;
		Parameters->HeightBorderLift = Layer.HeightBorderLift;
		Parameters->HeightBorderWidth = Layer.HeightBorderWidth;
		Parameters->HeightBorderNormalStrength = BorderHeightDerivedNormalStrength;

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
			Layer.bHeightBlendEnabled
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
			if (FRDGTextureRef Ids = FindRegionIdsAbove(RegionIdMaps, Child.SourceChildIndex))
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
						RegionSignal = FindRegionIdsAbove(RegionIdMaps, MAX_int32);
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

		Parameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
		Parameters->OutputBC = GraphBuilder.CreateUAV(OutputBC[WriteIndex]);
		Parameters->OutputN = GraphBuilder.CreateUAV(OutputN[WriteIndex]);
		Parameters->OutputRAM = GraphBuilder.CreateUAV(OutputRAM[WriteIndex]);
		Parameters->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
		Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]);

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
			DebugSettings.LayerIndex = Index;
			const FGuid TargetChildId = DebugSettings.ChildTarget.ChildId;
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

	for (int32 LayerIndex = 0; LayerIndex < EffectiveLayers.Num(); ++LayerIndex)
	{
		FMixtormatLayer Layer = EffectiveLayers[LayerIndex];
		MixtormatParameterBinding::ApplyDirectReferences(
			FMixtormatBindingScope{EffectiveLayers, Groups}, Layer);
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		const bool bReference = !Layer.SourceComposition.IsNull();
		if (bReference && Layer.bEnabled)
		{
			// Do not reinterpret malformed references as fills, surfaces or stale baked outputs.
			TStrongObjectPtr<UMixtormatMaterial> Source(Layer.SourceComposition.LoadSynchronous());
			if (!Source.IsValid() || Layer.Type != EMixtormatLayerType::Material
				|| !Layer.SourceSurface.IsNull() || ActiveSources.Contains(Source.Get())
				|| ActiveSources.Num() >= 32)
			{
				UE_LOG(LogMixtormatComposition, Warning,
					TEXT("Reference layer %d: missing source, invalid source contract, cycle or depth limit (32)."),
					LayerIndex);
				return false;
			}

			// Each occurrence gets a fresh compositor, even for repeated DAG edges. Source layer
			// IDs, masks, Drivers, direct references and ping-pong targets stay in their own graph.
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
			Data.bUseSourceF0 = !Layer.bOverrideIOR;
		}
		const UMixtormatSurface* Surface = bReference ? nullptr : Layer.SourceSurface.LoadSynchronous();
		const bool bNormalOnly = Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail;
		UTexture2D* LayerBaseColor = Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture;
		UTexture2D* LayerNormal = Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture;
		if (bNormalOnly && Layer.NormalSourceType == EMixtormatNormalSourceType::Texture)
		{
			LayerNormal = Layer.NormalTexture.LoadSynchronous();
		}
		UTexture2D* LayerRAM = Surface && Surface->RoughnessAOMetallic
			? Surface->RoughnessAOMetallic.Get()
			: WhiteTexture;

		Data.BaseColor = GetTextureRHI(LayerBaseColor);
		Data.Normal = GetTextureRHI(LayerNormal ? LayerNormal : NormalTexture);
		Data.RAM = GetTextureRHI(LayerRAM);
		Data.Mask = GetTextureRHI(WhiteTexture);
		if (!Data.BaseColor.IsValid()
			|| !Data.Normal.IsValid()
			|| !Data.RAM.IsValid()
			|| !Data.Mask.IsValid())
		{
			return false;
		}
		Data.LayerId = Layer.LayerId;
		// Only a layer's combined mask is a usable signal this step. A child mask lives in the
		// rotating ping-pong pair and is gone by the composite; region IDs are not a scalar at
		// all. Both are refused here rather than approximated -- a Driver that cannot resolve
		// contributes nothing and the parameter keeps its authored value.
		{
			const FName DrivenScalars[2] = { TEXT("RoughnessInfluence"), TEXT("HeightBlendAmount") };
			for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
			{
				const FMixtormatParameterBinding* Binding = Layer.ParameterBindings.FindByPredicate(
					[&DrivenScalars, SlotIndex](const FMixtormatParameterBinding& Candidate)
					{
						return Candidate.DestinationOwner == EMixtormatParameterOwnerType::Layer
							&& Candidate.DestinationParameter == DrivenScalars[SlotIndex]
							&& Candidate.Driver.bEnabled
							&& (Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask
								|| Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds)
							&& Candidate.Driver.SourceLayerId.IsValid();
					});
				if (!Binding)
				{
					continue;
				}
				FScalarDriverRenderData& Driver = Data.ScalarDrivers[SlotIndex];
				Driver.bEnabled = true;
				Driver.bRegionSource =
					Binding->Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds;
				Driver.SourceLayerId = Binding->Driver.SourceLayerId;
				Driver.SourceChildIndex = Binding->Driver.SourceChildId.IsValid()
					? Layer.Children.IndexOfByPredicate(
						[Binding](const FMixtormatLayerChild& Candidate)
						{
							return Candidate.ChildId == Binding->Driver.SourceChildId;
						})
					: INDEX_NONE;
				Driver.Seed = static_cast<uint32>(Binding->Driver.Seed);
				Driver.IdRandomMin = Binding->Driver.IdRandomMin;
				Driver.IdRandomMax = Binding->Driver.IdRandomMax;
				Driver.bInvert = Binding->Driver.bInvert;
				Driver.InputMin = Binding->Driver.InputMin;
				Driver.InputMax = Binding->Driver.InputMax;
				Driver.OutputMin = Binding->Driver.OutputMin;
				Driver.OutputMax = Binding->Driver.OutputMax;
				Driver.Amount = Binding->Driver.Amount;
				Driver.Combine = static_cast<uint32>(Binding->Driver.Combine);
			}
		}
		Data.FillColor = FVector4f(
			Layer.BaseColor.R,
			Layer.BaseColor.G,
			Layer.BaseColor.B,
			Layer.BaseColor.A);
		for (int32 SourceChildIndex = 0; SourceChildIndex < Layer.Children.Num(); ++SourceChildIndex)
		{
			const FMixtormatLayerChild& LayerChild = Layer.Children[SourceChildIndex];

			// Mask children still resolve on disabled layers so other layers can reference them.
			// Effects never contribute to that mask, and effect filters run after the disabled composite,
			// so capturing them would let a hidden layer modify the accumulated result.
			if (!Layer.bEnabled && LayerChild.Type == EMixtormatLayerChildType::Effect)
			{
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::HsvFilter)
			{
				// Rewrites albedo at the composite's own sample, so it never reaches the mask
				// chain. An empty palette is still valid -- the jitter rows work alone.
				const FMixtormatHsvIdFilter& Hsv = LayerChild.HsvFilter;
				if (!Layer.bEnabled || !Hsv.bEnabled)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::HsvFilter;
				ChildData.SourceChildIndex = SourceChildIndex;
				FHsvIdFilterRenderData& HsvData = ChildData.HsvFilter;
				const int32 PaletteCount =
					FMath::Min(Hsv.Palette.Num(), FMixtormatCompositeCS::MaxRegionPalette);
				HsvData.Palette.Reserve(PaletteCount);
				for (int32 ColorIndex = 0; ColorIndex < PaletteCount; ++ColorIndex)
				{
					HsvData.Palette.Add(FVector4f(Hsv.Palette[ColorIndex]));
				}
				HsvData.MixMin = FMath::Clamp(Hsv.RampMixMin, 0.0f, 1.0f);
				HsvData.MixMax = FMath::Clamp(Hsv.RampMixMax, 0.0f, 1.0f);
				HsvData.HueMin = FMath::Clamp(Hsv.HueMin, -1.0f, 1.0f);
				HsvData.HueMax = FMath::Clamp(Hsv.HueMax, -1.0f, 1.0f);
				HsvData.SatMin = FMath::Clamp(Hsv.SaturationMin, 0.0f, 4.0f);
				HsvData.SatMax = FMath::Clamp(Hsv.SaturationMax, 0.0f, 4.0f);
				HsvData.ValMin = FMath::Clamp(Hsv.ValueMin, 0.0f, 4.0f);
				HsvData.ValMax = FMath::Clamp(Hsv.ValueMax, 0.0f, 4.0f);
				HsvData.Seed = static_cast<uint32>(FMath::Max(Hsv.Seed, 0));
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::RampId)
			{
				// Its pass runs after the composite, so a disabled layer must not gather it --
				// the same reason effects are skipped above.
				const FMixtormatRampIdFilter& Ramp = LayerChild.RampId;
				if (!Layer.bEnabled || !Ramp.bEnabled)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::RampId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FRampIdRenderData& RampData = ChildData.RampId;
				RampData.HeightAmount = FMath::IsFinite(Ramp.HeightAmount)
					? FMath::Max(Ramp.HeightAmount, 0.0f) : 0.05f;
				RampData.AOAmount = FMath::IsFinite(Ramp.AOAmount)
					? FMath::Clamp(Ramp.AOAmount, 0.0f, 1.0f) : 0.0f;
				RampData.IntensityRandom = FMath::Clamp(Ramp.IntensityRandom, 0.0f, 1.0f);
				RampData.BlendMode = Ramp.BlendMode;
				RampData.bRotateRandom = Ramp.bRotateRandom;
				RampData.bAngleStepping = Ramp.bAngleStepping;
				RampData.AngleStepDegrees = FMath::IsFinite(Ramp.AngleStepDegrees)
					? FMath::Max(Ramp.AngleStepDegrees, 0.01f) : 5.0f;
				RampData.Seed = static_cast<uint32>(FMath::Max(Ramp.Seed, 0));
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::UvFromIds)
			{
				// Changes the layer's own source read, so a disabled layer must not gather it --
				// the same reason effects and the ramp tilt are skipped above.
				const FMixtormatUvIdFilter& Uv = LayerChild.UvId;
				if (!Layer.bEnabled || !Uv.bEnabled)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::UvFromIds;
				ChildData.SourceChildIndex = SourceChildIndex;
				FUvIdRenderData& UvData = ChildData.UvId;
				UvData.bOrthogonal = Uv.bOrthogonal;
				UvData.RotationMin = FMath::IsFinite(Uv.RotationMin)
					? FMath::Clamp(Uv.RotationMin, -360.0f, 360.0f) : 0.0f;
				UvData.RotationMax = FMath::IsFinite(Uv.RotationMax)
					? FMath::Clamp(Uv.RotationMax, -360.0f, 360.0f) : 360.0f;
				UvData.ScaleMin = FMath::IsFinite(Uv.ScaleMin)
					? FMath::Clamp(Uv.ScaleMin, 0.05f, 8.0f) : 1.0f;
				UvData.ScaleMax = FMath::IsFinite(Uv.ScaleMax)
					? FMath::Clamp(Uv.ScaleMax, 0.05f, 8.0f) : 1.0f;
				UvData.OffsetU = FMath::IsFinite(Uv.OffsetU)
					? FMath::Clamp(Uv.OffsetU, 0.0f, 1.0f) : 0.0f;
				UvData.OffsetV = FMath::IsFinite(Uv.OffsetV)
					? FMath::Clamp(Uv.OffsetV, 0.0f, 1.0f) : 0.0f;
				UvData.bRandomFlipU = Uv.bRandomFlipU;
				UvData.bRandomFlipV = Uv.bRandomFlipV;
				UvData.Seed = static_cast<uint32>(FMath::Max(Uv.Seed, 0));
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::ReliefFromIds)
			{
				// Its pass runs after the composite, so a disabled layer must not gather it.
				const FMixtormatReliefIdFilter& Relief = LayerChild.ReliefId;
				if (!Layer.bEnabled || !Relief.bEnabled)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::ReliefFromIds;
				ChildData.SourceChildIndex = SourceChildIndex;
				FReliefIdRenderData& ReliefData = ChildData.ReliefId;
				// Finite-guarded rather than range-clamped wherever Pattern's equivalent is, and
				// bounded wherever Pattern's is bounded: these feed the same shader, so a value
				// that is safe there is safe here and one that is not is not.
				ReliefData.HeightAmount = FMath::IsFinite(Relief.HeightAmount)
					? FMath::Max(Relief.HeightAmount, 0.0f) : 0.0f;
				ReliefData.HeightRandom = FMath::IsFinite(Relief.HeightRandom)
					? FMath::Clamp(Relief.HeightRandom, 0.0f, 1.0f) : 1.0f;
				ReliefData.Profile = FMath::IsFinite(Relief.Profile)
					? FMath::Clamp(Relief.Profile, -1.0f, 1.0f) : 0.0f;
				ReliefData.ProfileRandom = FMath::IsFinite(Relief.ProfileRandom)
					? FMath::Clamp(Relief.ProfileRandom, 0.0f, 1.0f) : 0.0f;
				ReliefData.Feather = FMath::IsFinite(Relief.Feather)
					? FMath::Max(Relief.Feather, 0.0f) : 0.1f;
				ReliefData.FeatherRandom = FMath::IsFinite(Relief.FeatherRandom)
					? FMath::Clamp(Relief.FeatherRandom, 0.0f, 1.0f) : 0.0f;
				ReliefData.FeatherGain = FMath::IsFinite(Relief.FeatherGain)
					? FMath::Max(Relief.FeatherGain, 0.0f) : 0.0f;
				ReliefData.BevelHeight = FMath::IsFinite(Relief.BevelHeight)
					? Relief.BevelHeight : 0.0f;
				ReliefData.BevelWidthPixels = FMath::IsFinite(Relief.BevelWidthPixels)
					? FMath::Max(Relief.BevelWidthPixels, 0.0001f) : 4.0f;
				ReliefData.BevelWidthCells = FMath::IsFinite(Relief.BevelWidthCells)
					? FMath::Max(Relief.BevelWidthCells, 0.0001f) : 0.25f;
				ReliefData.bRelativeWidth = Relief.bRelativeWidth;
				ReliefData.BevelVariation = FMath::IsFinite(Relief.BevelVariation)
					? FMath::Clamp(Relief.BevelVariation, 0.0f, 1.0f) : 0.0f;
				ReliefData.BevelInsetPixels = FMath::IsFinite(Relief.BevelInsetPixels)
					? Relief.BevelInsetPixels : 0.0f;
				ReliefData.GapHeight = FMath::IsFinite(Relief.GapHeight)
					? Relief.GapHeight : 0.0f;
				ReliefData.EdgeRoughness = FMath::IsFinite(Relief.EdgeRoughness)
					? FMath::Clamp(Relief.EdgeRoughness, 0.0f, 1.0f) : 0.65f;
				ReliefData.EdgeRoughnessAmount = FMath::IsFinite(Relief.EdgeRoughnessAmount)
					? FMath::Clamp(Relief.EdgeRoughnessAmount, 0.0f, 1.0f) : 0.0f;
				ReliefData.AOAmount = FMath::IsFinite(Relief.AOAmount)
					? FMath::Clamp(Relief.AOAmount, 0.0f, 1.0f) : 0.0f;
				ReliefData.AOSpread = FMath::IsFinite(Relief.AOSpread)
					? FMath::Clamp(Relief.AOSpread, 1.0f, 8.0f) : 1.0f;
				ReliefData.Seed = static_cast<uint32>(FMath::Max(Relief.Seed, 0));
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::RandomId)
			{
				const FMixtormatRandomIdMask& RandomId = LayerChild.RandomId;
				if (!RandomId.bEnabled)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::RandomId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FRandomIdRenderData& RandomData = ChildData.RandomId;
				RandomData.MinValue = FMath::Clamp(RandomId.MinValue, 0.0f, 1.0f);
				RandomData.MaxValue = FMath::Clamp(RandomId.MaxValue, 0.0f, 1.0f);
				RandomData.Seed = static_cast<uint32>(FMath::Max(RandomId.Seed, 0));
				RandomData.BlendMode = RandomId.BlendMode;
				RandomData.Weight = FMath::Clamp(RandomId.Weight, 0.0f, 1.0f);
				RandomData.bInvert = RandomId.Shaping.bInvert;
				RandomData.Balance = FMath::Clamp(RandomId.Shaping.Balance, 0.0f, 1.0f);
				RandomData.Contrast = FMath::Clamp(RandomId.Shaping.Contrast, 0.0f, 10.0f);
				RandomData.Offset = FMath::Clamp(RandomId.Shaping.Offset, -1.0f, 1.0f);
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Generator)
			{
				// Rewrites the layer's own input height before the composite, so a disabled
				// layer must not gather it -- the same reason effects are skipped above. An
				// enabled generator on a hidden layer would carve a surface nobody can see and
				// still cost the whole solve.
				const FMixtormatGenerator& Generator = LayerChild.Generator;
				if (!Layer.bEnabled || !Generator.bEnabled)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Generator;
				ChildData.SourceChildIndex = SourceChildIndex;
				ChildData.Generator.Type = Generator.Type;

				switch (Generator.Type)
				{
				case EMixtormatGeneratorType::StrataCarver:
				{
					const FMixtormatStrataCarver& Carver = Generator.StrataCarver;
					FStrataCarverRenderData& Out = ChildData.Generator.StrataCarver;
					Out.Seed = static_cast<uint32>(FMath::Max(Carver.Seed, 0));
					Out.Depth = FMath::IsFinite(Carver.Depth)
						? FMath::Clamp(Carver.Depth, 0.0f, 1.0f) : 0.05f;
					// The same 1..64 the property and the slider carry, so a driver or a
					// binding cannot push the solve somewhere the UI says is impossible.
					// Nothing in the solver is keyed to this number -- the jump schedule is a
					// function of JumpStart alone -- so the ceiling is a policy, not a limit.
					Out.Iterations = FMath::Clamp(Carver.Iterations, 1, 64);
					Out.SeedThreshold = FMath::IsFinite(Carver.SeedThreshold)
						? FMath::Clamp(Carver.SeedThreshold, 0.0f, 1.0f) : 0.25f;
					Out.WorleyCells = FMath::Clamp(Carver.Scale, 1, 64);
					Out.SeedDetail = FMath::Clamp(Carver.SeedDetail, 1, 8);
					Out.StrataFrequency = FMath::IsFinite(Carver.StrataFrequency)
						? FMath::Clamp(Carver.StrataFrequency, 0.0f, 64.0f) : 4.0f;
					Out.StrataAmount = FMath::IsFinite(Carver.StrataAmount)
						? FMath::Clamp(Carver.StrataAmount, 0.0f, 16.0f) : 3.0f;
					Out.StrataWarp = FMath::IsFinite(Carver.StrataWarp)
						? FMath::Clamp(Carver.StrataWarp, 0.0f, 4.0f) : 0.54f;
					Out.PushAmount = FMath::IsFinite(Carver.PushAmount)
						? FMath::Clamp(Carver.PushAmount, 0.0f, 4.0f) : 0.5f;
					Out.MaskInfluence = FMath::IsFinite(Carver.MaskInfluence)
						? FMath::Clamp(Carver.MaskInfluence, 0.0f, 1.0f) : 1.0f;
					Out.IDInfluence = FMath::IsFinite(Carver.IDInfluence)
						? FMath::Clamp(Carver.IDInfluence, 0.0f, 1.0f) : 0.0f;

					Out.StepScale = FMath::IsFinite(Carver.StepScale)
						? FMath::Clamp(Carver.StepScale, 0.001f, 4.0f) : 0.3f;
					Out.JumpStart = FMath::Clamp(Carver.JumpStart, 1, 256);
					Out.MaxValue = FMath::IsFinite(Carver.MaxValue)
						? FMath::Max(Carver.MaxValue, 1.0f) : 256.0f;
					Out.WorleyJitter = FMath::IsFinite(Carver.WorleyJitter)
						? FMath::Clamp(Carver.WorleyJitter, 0.0f, 1.0f) : 1.0f;
					Out.BandFrequency = FMath::IsFinite(Carver.BandFrequency)
						? FMath::Clamp(Carver.BandFrequency, 0.0f, 16.0f) : 1.0f;
					Out.CostAmount = FMath::IsFinite(Carver.CostAmount)
						? FMath::Clamp(Carver.CostAmount, 0.0f, 32.0f) : 5.0f;
					Out.PushDecay = FMath::IsFinite(Carver.PushDecay)
						? FMath::Clamp(Carver.PushDecay, 0.0f, 1.0f) : 0.2f;
					Out.OperationSeed = static_cast<uint32>(FMath::Max(Carver.OperationSeed, 0));
					Out.Bias = FMath::IsFinite(Carver.Bias)
						? FMath::Clamp(Carver.Bias, 0.001f, 1.0f) : 0.68f;
					Out.RemapInMin = FMath::IsFinite(Carver.RemapInMin)
						? FMath::Clamp(Carver.RemapInMin, 0.0f, 1.0f) : 0.0f;
					Out.RemapInMax = FMath::IsFinite(Carver.RemapInMax)
						? FMath::Clamp(Carver.RemapInMax, 0.0f, 1.0f) : 1.0f;
					Out.RemapOutMin = FMath::IsFinite(Carver.RemapOutMin)
						? FMath::Clamp(Carver.RemapOutMin, 0.0f, 1.0f) : 0.0f;
					Out.RemapOutMax = FMath::IsFinite(Carver.RemapOutMax)
						? FMath::Clamp(Carver.RemapOutMax, 0.0f, 1.0f) : 1.0f;
					Out.ClampMin = FMath::IsFinite(Carver.ClampMin)
						? FMath::Clamp(Carver.ClampMin, 0.0f, 1.0f) : 0.0f;
					Out.ClampMax = FMath::IsFinite(Carver.ClampMax)
						? FMath::Clamp(Carver.ClampMax, 0.0f, 1.0f) : 1.0f;
					break;
				}
				case EMixtormatGeneratorType::Fracture:
				{
					const FMixtormatFracture& Fracture = Generator.Fracture;
					FFractureRenderData& Out = ChildData.Generator.Fracture;
					using namespace MixtormatParameterContracts;
					Out.Source = Fracture.FractureSource;
					Out.Seed = static_cast<uint32>(SanitizeInt32(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureSeed"), Fracture.FractureSeed));
					Out.Scale = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureScale"), Fracture.FractureScale);
					Out.Amount = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureAmount"), Fracture.FractureAmount);
					Out.Width = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureWidth"), Fracture.FractureWidth);
					Out.Depth = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureDepth"), Fracture.FractureDepth);
					Out.Profile = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureProfile"), Fracture.FractureProfile);
					Out.Chamfer = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureChamfer"), Fracture.FractureChamfer);
					Out.Variation = SanitizeFloat(
						EMixtormatParameterOwnerType::Generator, TEXT("FractureVariation"), Fracture.FractureVariation);
					break;
				}
				}
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::CombineId)
			{
				const FMixtormatCombineIdFilter& Combine = LayerChild.CombineId;
				if (!Layer.bEnabled || !Combine.bEnabled)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::CombineId;
				ChildData.SourceChildIndex = SourceChildIndex;
				ChildData.CombineId.Amount = FMath::IsFinite(Combine.Amount)
					? FMath::Clamp(Combine.Amount, 0.0f, 1.0f) : 0.0f;
				ChildData.CombineId.Seed = static_cast<uint32>(FMath::Max(Combine.Seed, 0));
				ChildData.CombineId.Passes = FMath::Clamp(Combine.Passes, 1, 8);
				ChildData.CombineId.bSubtract =
					Combine.Mode == EMixtormatIdCombineMode::Subtract;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::PatternId)
			{
				const FMixtormatPatternFilter& Pattern = LayerChild.PatternId;
				if (!Layer.bEnabled || !Pattern.bEnabled)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::PatternId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FPatternIdRenderData& PatternData = ChildData.PatternId;

				PatternData.PatternMode = Pattern.PatternMode;
				PatternData.GridMode = Pattern.GridMode;
				PatternData.Rows = FMath::Clamp(Pattern.Rows, 1, 256);
				PatternData.Columns = FMath::Clamp(Pattern.Columns, 1, 256);
				PatternData.RowOffset = FMath::IsFinite(Pattern.RowOffset)
					? FMath::Clamp(Pattern.RowOffset, 0.0f, 1.0f) : 0.0f;
				PatternData.Jitter = FMath::IsFinite(Pattern.Jitter)
					? FMath::Clamp(Pattern.Jitter, 0.0f, 1.0f) : 0.0f;
				PatternData.Rounding = FMath::IsFinite(Pattern.Rounding)
					? FMath::Max(Pattern.Rounding, 0.0f) : 0.0f;
				PatternData.bRelativeEdgeWidth = Pattern.bRelativeEdgeWidth;
				PatternData.bSwapAxes = Pattern.bSwapAxes;
				PatternData.GapPixels = FMath::IsFinite(Pattern.GapPixels)
					? FMath::Max(Pattern.GapPixels, 0.0f) : 0.0f;
				// Both are fractions of the piece's own half-gap, so they are genuinely bounded
				// rather than merely dragged -- past 1 a piece would invade its neighbour.
				PatternData.GapRandom = FMath::IsFinite(Pattern.GapRandom)
					? FMath::Clamp(Pattern.GapRandom, 0.0f, 1.0f) : 0.0f;
				PatternData.GapSlide = FMath::IsFinite(Pattern.GapSlide)
					? FMath::Clamp(Pattern.GapSlide, 0.0f, 1.0f) : 0.0f;
				PatternData.Seed = static_cast<uint32>(FMath::Max(Pattern.Seed, 0));

				PatternData.bUVVariation = Pattern.bUVVariation;
				PatternData.bOrthogonalUV = Pattern.bOrthogonalUV;
				PatternData.UVRotationMin = FMath::IsFinite(Pattern.UVRotationMin)
					? FMath::Clamp(Pattern.UVRotationMin, -360.0f, 360.0f) : 0.0f;
				PatternData.UVRotationMax = FMath::IsFinite(Pattern.UVRotationMax)
					? FMath::Clamp(Pattern.UVRotationMax, -360.0f, 360.0f) : 360.0f;
				PatternData.UVScaleMin = FMath::IsFinite(Pattern.UVScaleMin)
					? FMath::Clamp(Pattern.UVScaleMin, 0.05f, 8.0f) : 1.0f;
				PatternData.UVScaleMax = FMath::IsFinite(Pattern.UVScaleMax)
					? FMath::Clamp(Pattern.UVScaleMax, 0.05f, 8.0f) : 1.0f;
				PatternData.UVOffset = FMath::IsFinite(Pattern.UVOffset)
					? FMath::Clamp(Pattern.UVOffset, 0.0f, 1.0f) : 0.0f;
				PatternData.bRandomFlipU = Pattern.bRandomFlipU;
				PatternData.bRandomFlipV = Pattern.bRandomFlipV;

				PatternData.HeightAmount = FMath::IsFinite(Pattern.HeightAmount)
					? FMath::Max(Pattern.HeightAmount, 0.0f) : 0.0f;
				PatternData.Feather = FMath::IsFinite(Pattern.Feather)
					? FMath::Max(Pattern.Feather, 0.0f) : 0.15f;
				// Finite-guarded, not range-clamped. The editor sliders bound the drag; a typed
				// value goes through, because these are artistic amounts and the shader is what
				// actually has to be safe -- BoundedDelta stops any height clipping the surface,
				// and every divisor below is floored in the shader. A non-finite value is the one
				// thing that cannot pass: a NaN here poisons the whole composited height.
				PatternData.BevelHeight = FMath::IsFinite(Pattern.BevelHeight)
					? Pattern.BevelHeight : 0.0f;
				PatternData.BevelWidthPixels = FMath::IsFinite(Pattern.BevelWidthPixels)
					? FMath::Max(Pattern.BevelWidthPixels, 0.0001f) : 4.0f;
				PatternData.BevelWidthCells = FMath::IsFinite(Pattern.BevelWidthCells)
					? FMath::Max(Pattern.BevelWidthCells, 0.0001f) : 0.25f;
				PatternData.BevelVariation = FMath::IsFinite(Pattern.BevelVariation)
					? FMath::Clamp(Pattern.BevelVariation, 0.0f, 1.0f) : 0.0f;
				PatternData.BevelRoundness = FMath::IsFinite(Pattern.Profile)
					? FMath::Clamp(Pattern.Profile, -1.0f, 1.0f) : 0.0f;
				PatternData.BevelRoundnessRandom = FMath::IsFinite(Pattern.ProfileRandom)
					? FMath::Clamp(Pattern.ProfileRandom, 0.0f, 1.0f) : 0.0f;
				PatternData.HeightRandom = FMath::IsFinite(Pattern.HeightRandom)
					? FMath::Clamp(Pattern.HeightRandom, 0.0f, 1.0f) : 1.0f;
				PatternData.FeatherRandom = FMath::IsFinite(Pattern.FeatherRandom)
					? FMath::Clamp(Pattern.FeatherRandom, 0.0f, 1.0f) : 0.0f;
				// Floored, not range-clamped: an artistic amount, like the Bevel block above. The
				// shader normalises the curve by its own peak, so a large value cannot clip.
				PatternData.FeatherGain = FMath::IsFinite(Pattern.FeatherGain)
					? FMath::Max(Pattern.FeatherGain, 0.0f) : 0.0f;
				PatternData.BevelInsetPixels = FMath::IsFinite(Pattern.BevelInsetPixels)
					? Pattern.BevelInsetPixels : 0.0f;
				PatternData.GapHeight = FMath::IsFinite(Pattern.GapHeight)
					? Pattern.GapHeight : 0.0f;
				PatternData.EdgeRoughness = FMath::IsFinite(Pattern.EdgeRoughness)
					? FMath::Clamp(Pattern.EdgeRoughness, 0.0f, 1.0f) : 0.65f;
				PatternData.EdgeRoughnessAmount = FMath::IsFinite(Pattern.EdgeRoughnessAmount)
					? FMath::Clamp(Pattern.EdgeRoughnessAmount, 0.0f, 1.0f) : 0.0f;
				PatternData.AOAmount = FMath::IsFinite(Pattern.AOAmount)
					? FMath::Clamp(Pattern.AOAmount, 0.0f, 1.0f) : 0.0f;
				PatternData.AOSpread = FMath::IsFinite(Pattern.AOSpread)
					? FMath::Clamp(Pattern.AOSpread, 1.0f, 8.0f) : 2.0f;

				// Fracture Plates. Size Variation and the four Edge controls are artistic
				// amounts, so they are finite-guarded and not range-clamped -- a typed value
				// above the slider's range goes through, exactly as the Bevel block above does.
				// Secondary Amount is a probability and the child counts are indices, so those
				// are genuinely bounded rather than merely dragged.
				PatternData.FractureSizeVariation = FMath::IsFinite(Pattern.FractureSizeVariation)
					? FMath::Max(Pattern.FractureSizeVariation, 0.0f) : 0.3f;
				PatternData.FractureSecondaryAmount =
					FMath::IsFinite(Pattern.FractureSecondaryAmount)
						? FMath::Clamp(Pattern.FractureSecondaryAmount, 0.0f, 1.0f) : 0.5f;
				PatternData.FractureSecondaryMin =
					FMath::Clamp(Pattern.FractureSecondaryMin, 2, 8);
				PatternData.FractureSecondaryMax = FMath::Clamp(
					Pattern.FractureSecondaryMax, PatternData.FractureSecondaryMin, 8);
				PatternData.FractureSecondaryRadius =
					FMath::IsFinite(Pattern.FractureSecondaryRadius)
						? FMath::Max(Pattern.FractureSecondaryRadius, 0.0f) : 0.34f;
				PatternData.FractureSecondaryJitter =
					FMath::IsFinite(Pattern.FractureSecondaryJitter)
						? FMath::Max(Pattern.FractureSecondaryJitter, 0.0f) : 0.55f;
				PatternData.FractureEdgeIrregularity =
					FMath::IsFinite(Pattern.FractureEdgeIrregularity)
						? Pattern.FractureEdgeIrregularity : 8.0f;
				// Floored, not clamped: the scale is a divisor that sizes an integer lattice
				// period, and a period of zero has no meaning to floor it into.
				PatternData.FractureEdgeScale = FMath::IsFinite(Pattern.FractureEdgeScale)
					? FMath::Max(Pattern.FractureEdgeScale, 1.0f) : 96.0f;
				PatternData.FractureEdgeDetail = FMath::IsFinite(Pattern.FractureEdgeDetail)
					? Pattern.FractureEdgeDetail : 2.5f;
				PatternData.FractureEdgeDetailScale =
					FMath::IsFinite(Pattern.FractureEdgeDetailScale)
						? FMath::Max(Pattern.FractureEdgeDetailScale, 1.0f) : 24.0f;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Filter)
			{
				// A missing authored packed map is not a request to segment the white fallback.
				const FMixtormatClusterFilter& Filter = LayerChild.Filter;
				if (!Layer.bEnabled || !Filter.bEnabled || !Surface || !Surface->RoughnessAOMetallic)
				{
					continue;
				}
				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Filter;
				ChildData.SourceChildIndex = SourceChildIndex;
				ChildData.Filter.Source = Filter.Source;
				ChildData.Filter.Threshold = FMath::IsFinite(Filter.Threshold)
					? FMath::Clamp(Filter.Threshold, 0.0f, 1.0f) : 0.33f;
				ChildData.Filter.Offset = FMath::IsFinite(Filter.Offset) ? Filter.Offset : 0.0f;
				ChildData.Filter.HeightInfluence = FMath::IsFinite(Filter.HeightInfluence)
					? FMath::Max(Filter.HeightInfluence, 0.0f) : 1.0f;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::ColorId)
			{
				const FMixtormatColorIdMask& ColorIdMask = LayerChild.ColorId;
				if (!ColorIdMask.bEnabled)
				{
					continue;
				}

				const bool bExactId = ColorIdMask.Mode == EMixtormatColorIdMode::ExactId;
				UTexture2D* IdTexture = ColorIdMask.IdTexture.LoadSynchronous();

				// A node with no map or no colours selects nothing, and selecting nothing is not
				// the same as being the identity: it would blend a mask of zero. Dropping it
				// entirely is what an unconfigured node should do, and matches how a painted mask
				// with no texture behaves.
				//
				// Exact ID reads neither: it compares the Region IDs published above it, so an
				// unset map and an empty colour list are its normal state. Whether it has a
				// producer above it to read is decided in the pass, where the child list has
				// already been walked -- the same place Random From IDs decides it.
				if (!bExactId && (!IdTexture || ColorIdMask.Colors.IsEmpty()))
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::ColorId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FColorIdRenderData& IdData = ChildData.ColorId;
				IdData.Mode = ColorIdMask.Mode;
				IdData.ExactRegionId = static_cast<uint32>(FMath::Max(ColorIdMask.ExactRegionId, 0));
				// White in Exact ID, where the slot is never sampled. The shader parameter still
				// has to be bound, and the cached white texture is already resident.
				IdData.IdTexture = GetTextureRHI(IdTexture ? IdTexture : WhiteTexture);
				if (!IdData.IdTexture.IsValid())
				{
					return false;
				}

				// Truncated rather than reported: the array is what the shader can hold, and the
				// inspector does not offer to add past it, so this only trips on data authored
				// through Blueprint or a hand-edited asset.
				const int32 ColorCount =
					FMath::Min(ColorIdMask.Colors.Num(), FMixtormatColorIdMask::MaxColors);
				for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
				{
					const FLinearColor& Color = ColorIdMask.Colors[ColorIndex];
					IdData.Colors.Add(FVector4f(Color.R, Color.G, Color.B, 1.0f));
				}

				IdData.Tolerance = FMath::Clamp(ColorIdMask.Tolerance, 0.0f, 1.732f);
				IdData.Softness = FMath::Clamp(ColorIdMask.Softness, 0.0f, 0.5f);
				IdData.BlendMode = ColorIdMask.BlendMode;
				IdData.Weight = FMath::Clamp(ColorIdMask.Weight, 0.0f, 1.0f);
				IdData.bInvert = ColorIdMask.Shaping.bInvert;
				IdData.Tiling = FVector2f(
					static_cast<float>(FMath::Max(ColorIdMask.TilingX, 1)),
					static_cast<float>(FMath::Max(ColorIdMask.TilingY, 1)));
				IdData.UVOffset = FVector2f(ColorIdMask.UVOffsetX, ColorIdMask.UVOffsetY);
				IdData.bFlipU = ColorIdMask.bFlipU;
				IdData.bFlipV = ColorIdMask.bFlipV;
				IdData.Rotation = static_cast<int32>(ColorIdMask.Rotation);
				IdData.Balance = FMath::Clamp(ColorIdMask.Shaping.Balance, 0.0f, 1.0f);
				IdData.Contrast = FMath::Clamp(ColorIdMask.Shaping.Contrast, 0.0f, 10.0f);
				IdData.Offset = FMath::Clamp(ColorIdMask.Shaping.Offset, -1.0f, 1.0f);
				Data.bHasMask = true;
				continue;
			}

			// Consumed by the mask it is scoped to, never a node of its own down here. Skipped
			// explicitly so it cannot fall through to whatever handles an unrecognised type.
			if (LayerChild.Type == EMixtormatLayerChildType::Blur
				|| LayerChild.Type == EMixtormatLayerChildType::Curvature)
			{
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Mask)
			{
				const FMixtormatMaskLayer& MaskLayer = LayerChild.Mask;
				if (!MaskLayer.bEnabled)
				{
					continue;
				}

				const bool bPublishedSource = MaskLayer.HasPublishedSource();
				int32 PublishedSourceChildIndex = INDEX_NONE;
				if (bPublishedSource)
				{
					for (const FMixtormatLayer& SourceLayer : EffectiveLayers)
					{
						if (SourceLayer.LayerId != MaskLayer.PublishedSourceLayerId)
						{
							continue;
						}
						PublishedSourceChildIndex = SourceLayer.Children.IndexOfByPredicate(
							[&MaskLayer](const FMixtormatLayerChild& Candidate)
							{
								return Candidate.ChildId == MaskLayer.PublishedSourceChildId;
							});
						break;
					}
				}

				// A Layer Values mask reads the layer it sits on, so it needs no asset at all --
				// which is also why it cannot be dropped for the want of one the way a texture
				// mask is below.
				const bool bLayerValues = MaskLayer.UsesLayerValues();

				UTexture2D* MaskTexture = nullptr;
				if (!bPublishedSource && !bLayerValues)
				{
					MaskTexture = MaskLayer.MaskTexture.LoadSynchronous();
					if (!MaskTexture)
					{
						if (const UMixtormatMask* MaskAsset = MaskLayer.Mask.LoadSynchronous())
						{
							MaskTexture = MaskAsset->MaskTexture.Get();
						}
					}
					if (!MaskTexture)
					{
						continue;
					}
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Mask;
				ChildData.SourceChildIndex = SourceChildIndex;
				if (LayerChild.ScopeOwnerChildId.IsValid())
				{
					const int32 OwnerIndex = Layer.Children.IndexOfByPredicate(
						[&LayerChild](const FMixtormatLayerChild& Candidate)
						{
							return Candidate.ChildId == LayerChild.ScopeOwnerChildId;
						});
					if (Layer.Children.IsValidIndex(OwnerIndex)
						&& OwnerIndex < SourceChildIndex
					&& (Layer.Children[OwnerIndex].Type == EMixtormatLayerChildType::Effect
						|| Layer.Children[OwnerIndex].Type == EMixtormatLayerChildType::Generator))
					{
						ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
					}
					else
					{
						// A scoped mask whose owner cannot be resolved as a preceding Effect
						// is inactive. Never let it fall through as a layer-wide mask.
						Data.Children.RemoveAt(Data.Children.Num() - 1);
						continue;
					}
				}
				FMaskRenderData& MaskData = ChildData.Mask;
				if (bPublishedSource)
				{
					MaskData.PublishedSourceLayerId = MaskLayer.PublishedSourceLayerId;
					MaskData.PublishedSourceChildIndex = PublishedSourceChildIndex;
					MaskData.PublishedSourceOutput = MaskLayer.PublishedSourceOutput;
				}
				else if (bLayerValues)
				{
					MaskData.bLayerValues = true;
					MaskData.SourceChannel =
						static_cast<int32>(MaskLayer.LayerValueChannel);
				}
				else
				{
					MaskData.Texture = GetTextureRHI(MaskTexture);
					if (!MaskData.Texture.IsValid())
					{
						return false;
					}
				}
				MaskData.BlendMode = MaskLayer.BlendMode;
				MaskData.Weight = FMath::Clamp(MaskLayer.Weight, 0.0f, 1.0f);
				// Integer per axis: the shader wraps the read in a frac(), and a fractional
				// scale lands mid-cell at that wrap.
				MaskData.Tiling = FVector2f(
					static_cast<float>(FMath::Max(MaskLayer.TilingX, 1)),
					static_cast<float>(FMath::Max(MaskLayer.TilingY, 1)));
				MaskData.UVOffset = FVector2f(MaskLayer.UVOffsetX, MaskLayer.UVOffsetY);
				MaskData.bFlipU = MaskLayer.bFlipU;
				MaskData.bFlipV = MaskLayer.bFlipV;
				MaskData.Rotation = static_cast<int32>(MaskLayer.Rotation);
				MaskData.Balance = FMath::Clamp(MaskLayer.Shaping.Balance, 0.0f, 1.0f);
				MaskData.Contrast = FMath::Clamp(MaskLayer.Shaping.Contrast, 0.0f, 10.0f);
				MaskData.Offset = FMath::Clamp(MaskLayer.Shaping.Offset, -1.0f, 1.0f);
				MaskData.bInvert = MaskLayer.Shaping.bInvert;
				// Blur is a node in the recipe -- so it can be driven, published and instanced --
				// but a pair of numbers by the time the passes see it. Summed rather than maxed:
				// two blurs stacked on one mask should soften more than either alone, which is
				// what stacking them plainly means. Clamped to the shader's 32-tap unroll.
				for (const FMixtormatLayerChild& BlurChild : Layer.Children)
				{
					if (BlurChild.Type != EMixtormatLayerChildType::Blur
						|| BlurChild.ScopeOwnerChildId != LayerChild.ChildId
						|| !BlurChild.Blur.bEnabled)
					{
						continue;
					}
					MaskData.BlurRadiusX += FMath::Max(BlurChild.Blur.RadiusX, 0.0f);
					MaskData.BlurRadiusY += FMath::Max(BlurChild.Blur.RadiusY, 0.0f);
				}
				MaskData.BlurRadiusX = FMath::Min(MaskData.BlurRadiusX, 32.0f);
				MaskData.BlurRadiusY = FMath::Min(MaskData.BlurRadiusY, 32.0f);
				// Gathered in the order they appear, and kept as a list rather than reduced: each
				// one narrows what the one before it left, so two of them are not one of anything.
				for (const FMixtormatLayerChild& CurvatureChild : Layer.Children)
				{
					if (CurvatureChild.Type != EMixtormatLayerChildType::Curvature
						|| CurvatureChild.ScopeOwnerChildId != LayerChild.ChildId
						|| !CurvatureChild.Curvature.KeepsAnything())
					{
						continue;
					}
					MaskData.CurvatureFilters.Add(CurvatureChild.Curvature);
				}
				if (ChildData.ScopeOwnerSourceChildIndex == INDEX_NONE)
				{
					Data.bHasMask = true;
				}
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Generated)
			{
				const FMixtormatGeneratedMask& GeneratedMask = LayerChild.Generated;
				if (!GeneratedMask.bEnabled || !GeneratedMask.HasAnySignal())
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Generated;
				ChildData.SourceChildIndex = SourceChildIndex;
				FGeneratedMaskRenderData& GeneratedData = ChildData.Generated;
				GeneratedData.CurvatureWeight = GeneratedMask.CurvatureWeight;
				GeneratedData.CurvatureBias = FMath::Clamp(GeneratedMask.CurvatureBias, 0.0f, 1.0f);
				GeneratedData.CurvatureStrength = FMath::Max(GeneratedMask.CurvatureStrength, 0.0f);
				GeneratedData.CurvaturePower = FMath::Max(GeneratedMask.CurvaturePower, 0.001f);
				GeneratedData.DirectionWeight = GeneratedMask.DirectionWeight;
				GeneratedData.DirectionAngle = GeneratedMask.DirectionAngle;
				GeneratedData.DirectionBroadness = FMath::Max(GeneratedMask.DirectionBroadness, 0.001f);
				GeneratedData.AOWeight = GeneratedMask.AOWeight;
				GeneratedData.HeightWeight = GeneratedMask.HeightWeight;
				GeneratedData.HeightBias = GeneratedMask.HeightBias;
				GeneratedData.bNormalizeWeights = GeneratedMask.bNormalizeWeights;
				GeneratedData.Broadness = FMath::Clamp(GeneratedMask.Broadness, 1, 32);
				GeneratedData.Smoothing = FMath::Clamp(GeneratedMask.Smoothing, 1, 4);
				GeneratedData.Bias = FMath::Clamp(GeneratedMask.Bias, 0.001f, 0.999f);
				GeneratedData.WarpAmount = FMath::Max(GeneratedMask.WarpAmount, 0.0f);
				GeneratedData.WarpSource = FMath::Clamp(GeneratedMask.WarpSource, 0.0f, 1.0f);
				GeneratedData.WarpRadius = FMath::Clamp(GeneratedMask.WarpRadius, 1, 16);
				GeneratedData.BlendMode = GeneratedMask.BlendMode;
				GeneratedData.Weight = FMath::Max(GeneratedMask.Weight, 0.0f);
				GeneratedData.Balance = FMath::Max(GeneratedMask.Shaping.Balance, 0.0f);
				GeneratedData.Contrast = FMath::Max(GeneratedMask.Shaping.Contrast, 0.0f);
				GeneratedData.Offset = GeneratedMask.Shaping.Offset;
				GeneratedData.bInvert = GeneratedMask.Shaping.bInvert;
				GeneratedData.RidgeWeight = GeneratedMask.RidgeWeight;
				Data.bHasMask = true;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Craquelure)
			{
				const FMixtormatCraquelure& Craquelure = LayerChild.Craquelure;

				// No HasAnySignal() equivalent: this node has one signal and it is always on.
				// Weight 0 is how a craquelure node is muted, the same as any other mask.
				if (!Craquelure.bEnabled)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Craquelure;
				ChildData.SourceChildIndex = SourceChildIndex;
				FCraquelureRenderData& CrackData = ChildData.Craquelure;

				// The authored parameters are the ones a user thinks in; the growth kernel wants
				// the ones it was written against. This is where the one becomes the other, so the
				// shaders keep the maths they were tuned with and the consolidation costs nothing
				// at runtime.
				//
				// The clamps guard the lattice rather than taste: a period is a wrap modulus and a
				// non-positive one divides the hash by zero.
				const int32 CrackScale = FMath::Clamp(Craquelure.Scale, 1, 128);
				const float CrackJitter = FMath::Clamp(Craquelure.Jitter, 0.0f, 1.0f);

				// One Scale, read as the cell count by whichever mode is running.
				CrackData.Period = CrackScale;
				CrackData.SeedCells = CrackScale;
				CrackData.Jitter = CrackJitter;
				CrackData.SeedJitter = CrackJitter;

				CrackData.Seed = static_cast<uint32>(FMath::Max(Craquelure.Seed, 0));
				// The warp rides the network's seed rather than carrying its own. An offset, not the
				// same value, so reseeding moves both without the two fields ever sharing a hash.
				CrackData.WarpSeed = CrackData.Seed + 7919u;
				CrackData.WarpPeriod = FMath::Clamp(Craquelure.WarpScale, 1, 32);
				CrackData.Warp = FMath::Clamp(Craquelure.Warp, 0.0f, 1.0f);

				CrackData.Width = Craquelure.Width;
				CrackData.Variation = Craquelure.Variation;
				CrackData.BlendMode = Craquelure.BlendMode;
				CrackData.Weight = Craquelure.Weight;
				CrackData.bInvert = Craquelure.Shaping.bInvert;
				CrackData.Balance = FMath::Clamp(Craquelure.Shaping.Balance, 0.0f, 1.0f);
				CrackData.Contrast = Craquelure.Shaping.Contrast;
				CrackData.Offset = Craquelure.Shaping.Offset;

				CrackData.Mode = Craquelure.Mode;
				CrackData.ReliefDepth = FMath::Max(Craquelure.ReliefDepth, 0.0f);
				CrackData.ReliefWidth = FMath::Max(Craquelure.ReliefWidth, 0.002f);
				CrackData.ReliefProfile = FMath::Clamp(Craquelure.ReliefProfile, 0.05f, 8.0f);
				CrackData.ReliefGrooveVariation = FMath::Clamp(Craquelure.ReliefGrooveVariation, 0.0f, 1.0f);
				CrackData.ReliefProfileVariation = FMath::Clamp(Craquelure.ReliefProfileVariation, 0.0f, 1.0f);
				CrackData.ReliefWidthVariation = FMath::Clamp(Craquelure.ReliefWidthVariation, 0.0f, 1.0f);
				CrackData.Iterations = FMath::Clamp(Craquelure.Iterations, 1, 1024);
				CrackData.SeedChance = FMath::Clamp(Craquelure.Density, 0.0f, 1.0f);

				// Detail is a multiple of Scale, so the fields keep their size relative to the
				// pieces when Scale moves. As an absolute cell count it fought Scale on every drag.
				CrackData.NoiseCells = FMath::Clamp(
					FMath::RoundToInt(CrackScale * FMath::Clamp(Craquelure.Detail, 0.1f, 8.0f)),
					1,
					256);

				// Stress and toughness vary by the same amount, from independent noise. They were two
				// dials for the two ends of one balance.
				const float FieldContrast = FMath::Clamp(Craquelure.FieldContrast, 0.0f, 1.0f);
				CrackData.StressVariation = FieldContrast;
				CrackData.ToughnessVariation = FieldContrast;

				// Likewise the two weights on opposite signs of the same comparison.
				const float FractureBias = FMath::Clamp(Craquelure.FractureBias, 0.0f, 8.0f);
				CrackData.StressGain = FractureBias;
				CrackData.ToughnessCost = FractureBias;

				// Straightness drives both halves of holding a heading: how much alignment counts in
				// the score, and how fast the stored direction follows the step actually taken. They
				// run opposite ways -- a straighter crack weights alignment more and turns slower --
				// which is exactly why two dials for it were easy to set against each other. The
				// constants put the old defaults near 0.35.
				const float Straightness = FMath::Clamp(Craquelure.Straightness, 0.0f, 1.0f);
				CrackData.Persistence = Straightness * 6.0f;
				CrackData.TurnResponse = FMath::Clamp(1.0f - Straightness * 0.8f, 0.0f, 1.0f);

				CrackData.FlowStrength = FMath::Clamp(Craquelure.Flow, 0.0f, 1.0f);
				CrackData.Irregularity = FMath::Clamp(Craquelure.Roughness, 0.0f, 8.0f);
				CrackData.GrowthThreshold = Craquelure.GrowthThreshold;
				CrackData.CollisionLimit = FMath::Clamp(Craquelure.CollisionLimit, 1, 8);

				// Built from the clamped values rather than the authored ones, so two settings
				// the clamps map onto the same network share a cache entry -- and, more to the
				// point, so a key can never describe a network the shader would not produce.
				//
				// Mode is in the key because the two modes build entirely different fields from
				// overlapping parameters. Width, Variation, the blend tail and every relief
				// control are deliberately absent: they shape the field after it exists, and
				// including them would miss on exactly the sliders most likely to be dragged.
				{
					uint64 Key = MixtormatNetworkKey::Seed();
					const uint8 ModeByte = static_cast<uint8>(CrackData.Mode);
					Key = MixtormatNetworkKey::Add(Key, ModeByte);
					Key = MixtormatNetworkKey::Add(Key, CrackData.Seed);
					// Warp is deliberately absent. It bends where the finished network is read
					// from rather than how it grows, so the cached distance field stays valid
					// across a warp change -- which turns dragging the dial from a full regrow of
					// the most expensive node in the graph into one resolve pass.
					if (CrackData.Mode == EMixtormatCraquelureMode::Propagated)
					{
						Key = MixtormatNetworkKey::Add(Key, CrackData.Iterations);
						Key = MixtormatNetworkKey::Add(Key, CrackData.SeedCells);
						Key = MixtormatNetworkKey::Add(Key, CrackData.SeedChance);
						Key = MixtormatNetworkKey::Add(Key, CrackData.SeedJitter);
						Key = MixtormatNetworkKey::Add(Key, CrackData.NoiseCells);
						Key = MixtormatNetworkKey::Add(Key, CrackData.StressVariation);
						Key = MixtormatNetworkKey::Add(Key, CrackData.ToughnessVariation);
						Key = MixtormatNetworkKey::Add(Key, CrackData.Persistence);
						Key = MixtormatNetworkKey::Add(Key, CrackData.FlowStrength);
						Key = MixtormatNetworkKey::Add(Key, CrackData.StressGain);
						Key = MixtormatNetworkKey::Add(Key, CrackData.ToughnessCost);
						Key = MixtormatNetworkKey::Add(Key, CrackData.Irregularity);
						Key = MixtormatNetworkKey::Add(Key, CrackData.GrowthThreshold);
						Key = MixtormatNetworkKey::Add(Key, CrackData.TurnResponse);
						Key = MixtormatNetworkKey::Add(Key, CrackData.CollisionLimit);
					}
					else
					{
						Key = MixtormatNetworkKey::Add(Key, CrackData.Period);
						Key = MixtormatNetworkKey::Add(Key, CrackData.Jitter);
					}
					CrackData.NetworkKey = Key;
				}
				// Unconditional now that height and normal are their own weights rather than an
				// output mode. The node always contributes a mask; Weight 0 is how that half is
				// muted, exactly as on every other mask child.
				Data.bHasMask = true;
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
				const int32 OwnerIndex = Layer.Children.IndexOfByPredicate(
					[&LayerChild](const FMixtormatLayerChild& Candidate)
					{
						return Candidate.ChildId == LayerChild.ScopeOwnerChildId;
					});
				bool bWarpableOwner = false;
				if (Layer.Children.IsValidIndex(OwnerIndex) && OwnerIndex < SourceChildIndex)
				{
					const FMixtormatLayerChild& Owner = Layer.Children[OwnerIndex];
					bWarpableOwner = Owner.Type == EMixtormatLayerChildType::Mask;
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
					ResolvedType == EMixtormatEffectType::FlowWarp && bWarpableOwner;
				if (!bValidFlowWarpScope)
				{
					Data.Children.RemoveAt(Data.Children.Num() - 1);
					continue;
				}
				ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
			}
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = ResolvedType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			// Shared enable/blend control across every effect family; not family-keyed, so
			// it keeps its literal bound instead of a definition entry.
			EffectData.Strength = FMath::Clamp(LayerEffect.Strength, 0.0f, 1.0f);

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

			// Filters have nothing further to gather. bHasEffects is deliberately not set for
			// them: a Filter never writes the effect data target, so flagging it would make
			// the composite sample a buffer nothing wrote.
			//
			// Gated on the class, not on the absence of an asset. Erosion got away with the
			// narrower test because nothing creates Erosion assets, but Grade is a valid
			// EffectType on UMixtormatEffect, so an authored Grade asset would fall through
			// into the peel branches below and trip exactly the failure above.
			if (MixtormatEffectClassOf(ResolvedType) == EMixtormatEffectClass::Filter)
			{
				continue;
			}

			// Peeling is procedural-only. Its authored values are gathered through the same
			// contract sanitizer as the migrated effect families.
			GatherPeeling(EffectData, LayerEffect);
			Data.bHasEffects = true;
			continue;

		}

		Data.Opacity = Layer.Opacity;
		Data.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));

		// Integer, because the compositor wraps every source read in a frac() and a
		// fractional scale lands mid-cell at the wrap. Offset and flip are unclamped:
		// translating and mirroring a periodic function leave it periodic.
		Data.UVScaleX = FMath::Clamp(Layer.UVScaleX, 1, 16);
		Data.UVScaleY = FMath::Clamp(Layer.UVScaleY, 1, 16);
		Data.UVOffset = FVector2f(Layer.UVOffsetX, Layer.UVOffsetY);
		Data.Rotation = static_cast<int32>(Layer.Rotation);
		Data.bFlipU = Layer.bFlipU;
		Data.bFlipV = Layer.bFlipV;
		// Normal Strength is the authored normal map's own gain, and the only control that
		// steepens it. Height Booster is deliberately absent: it owns the height and the normals
		// derived from that height, and tying the two made a matching height+normal pair carry
		// the same bump twice. Pinning this to a neutral 1.0 removed that double count but left
		// no way to strengthen an authored map at all, since Normal Influence only attenuates.
		// See MixtormatReliefScaling.h.
		Data.NormalIntensity = MixtormatRelief::AuthoredNormalScale(
					MixtormatParameterContracts::SanitizeFloat(
						EMixtormatParameterOwnerType::Layer, TEXT("NormalIntensity"), Layer.NormalIntensity));
		Data.HueShift = FMath::Clamp(Layer.HueShift, -180.0f, 180.0f);
		Data.Saturation = FMath::Clamp(Layer.Saturation, 0.0f, 2.0f);
		Data.Value = FMath::Clamp(Layer.Value, 0.0f, 2.0f);
		Data.RoughnessBias = Layer.RoughnessBias;
		Data.RoughnessContrast = Layer.RoughnessContrast;
		Data.RoughnessOffset = Layer.RoughnessOffset;
		Data.FillRoughness = Layer.Roughness;
		Data.FillMetallic = Layer.Metallic;
		const float SourceIOR = Surface ? Surface->DefaultIOR : 1.5f;
		const float AuthoredIOR = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Layer, TEXT("IOR"), Layer.IOR);
		const float LayerIOR = FMath::Max(
			1.0f,
			(Layer.Type == EMixtormatLayerType::Fill || Layer.bOverrideIOR)
				? AuthoredIOR
				: SourceIOR);
		Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
		Data.HeightBoost = MixtormatRelief::HeightScale(
					MixtormatParameterContracts::SanitizeFloat(
						EMixtormatParameterOwnerType::Layer, TEXT("HeightBoost"), Layer.HeightBoost));
		Data.HeightLevelOffset = FMath::Clamp(Layer.HeightLevelOffset, -1.0f, 1.0f);
		Data.HeightShape = FMath::Clamp(Layer.HeightShape, -1.0f, 1.0f);
		Data.HeightSmooth = FMath::Clamp(Layer.HeightSmooth, 0.0f, 8.0f);
		Data.BaseColorBlendMode = Layer.BaseColorBlendMode;
		Data.BaseColorBlendAmount = FMath::Clamp(Layer.BaseColorBlendAmount, 0.0f, 1.0f);
		Data.BaseColorInfluence = FMath::Clamp(Layer.BaseColorInfluence, 0.0f, 1.0f);
		Data.RoughnessInfluence = FMath::Clamp(Layer.RoughnessInfluence, 0.0f, 1.0f);
		Data.AOInfluence = FMath::Clamp(Layer.AOInfluence, 0.0f, 1.0f);
		Data.MetallicInfluence = FMath::Clamp(Layer.MetallicInfluence, 0.0f, 1.0f);
		Data.F0Influence = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Layer, TEXT("F0Influence"), Layer.F0Influence);
		Data.NormalInfluence = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Layer, TEXT("NormalInfluence"), Layer.NormalInfluence);
		Data.HeightInfluence = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Layer, TEXT("HeightInfluence"), Layer.HeightInfluence);
		Data.HeightBlendAmount = FMath::Clamp(Layer.HeightBlendAmount, 0.0f, 4.0f);
		Data.HeightThreshold = FMath::Clamp(Layer.HeightThreshold, 0.0f, 1.0f);
		Data.HeightRange = FMath::Max(Layer.HeightRange, 1.0e-6f);
		Data.HeightContrast = FMath::Max(Layer.HeightContrast, 0.01f);
		Data.HeightOffset = FMath::Clamp(Layer.HeightOffset, -1.0f, 1.0f);
		Data.HeightBias = FMath::Clamp(Layer.HeightBias, -1.0f, 1.0f);
		Data.ConstantHeight = FMath::Clamp(Layer.ConstantHeight, 0.0f, 1.0f);
		Data.MaskHeightInfluence = FMath::Clamp(Layer.MaskHeightInfluence, 0.0f, 1.0f);
		Data.HeightContactAOAmount = FMath::Clamp(Layer.HeightContactAOAmount, 0.0f, 1.0f);
		Data.HeightContactAOWidth = FMath::Max(Layer.HeightContactAOWidth, 1.0e-4f);
		Data.HeightBorderLift = FMath::Clamp(Layer.HeightBorderLift, -1.0f, 1.0f);
		Data.HeightBorderWidth = FMath::Max(Layer.HeightBorderWidth, 1.0e-4f);
		Data.HeightSmoothRadius = FMath::Clamp(Layer.HeightSmoothRadius, 0.0f, 32.0f);
		Data.HeightSmoothAmount = FMath::Clamp(Layer.HeightSmoothAmount, 0.0f, 1.0f);
		Data.HeightBorderSmoothing = FMath::Clamp(Layer.HeightBorderSmoothing, 1.0f, 32.0f);
		Data.FeatureInfluence = FMath::Clamp(Layer.FeatureInfluence, 0.0f, 1.0f);
		Data.FeatureBias = FMath::Clamp(Layer.FeatureBias, 0.0f, 1.0f);
		Data.HeightFeatureInfluence = FMath::Clamp(Layer.HeightFeatureInfluence, 0.0f, 1.0f);
		Data.AOFeatureInfluence = FMath::Clamp(Layer.AOFeatureInfluence, 0.0f, 1.0f);
		Data.CurvatureRadius = Layer.CurvatureRadius;
		Data.CurvatureSmoothing = FMath::Clamp(Layer.CurvatureSmoothing, 1, 4);
		Data.CurvatureStrength = Layer.CurvatureStrength;
		Data.CurvaturePower = Layer.CurvaturePower;
		Data.bEnabled = Layer.bEnabled;
		Data.bHeightBlendEnabled = Layer.bHeightBlendEnabled;
		Data.bHasPackedHeight = Data.SourceOutputs.IsValid() || (Surface && Surface->bHasBlendHeight);
		Data.bInvertHeight = Layer.bInvertHeight;
		Data.bDirectHeightComparison = !bNormalOnly;
		Data.bInvertHeightFeature = Layer.bInvertHeightFeature;
		Data.bInvertAOFeature = Layer.bInvertAOFeature;
		Data.bInvertFeature = Layer.bInvertFeature;
		Data.HeightReferenceLayerIndex = Layer.HeightReferenceLayerIndex >= 0
			&& Layer.HeightReferenceLayerIndex < LayerIndex
			? Layer.HeightReferenceLayerIndex
			: INDEX_NONE;
		switch (Layer.HeightSource)
		{
		case EMixtormatHeightSource::Automatic:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : (Data.bHasMask ? 1u : 2u);
			break;
		case EMixtormatHeightSource::CombinedMask:
			Data.HeightSource = Data.bHasMask ? 1u : 2u;
			break;
		case EMixtormatHeightSource::Constant:
			Data.HeightSource = 2u;
			break;
		case EMixtormatHeightSource::RAMHAlpha:
		case EMixtormatHeightSource::LayerHeight:
		default:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : 2u;
			break;
		}
		Data.bOverrideBaseColor = Layer.bOverrideBaseColor;
		Data.bOverrideRoughness = Layer.bOverrideRoughness;
		Data.bOverrideMetallic = Layer.bOverrideMetallic;
		Data.bCoat = Layer.CompositionMode == EMixtormatCompositionMode::Coat;
		Data.bFill = Layer.Type == EMixtormatLayerType::Fill;
		Data.bHasSurface = Data.SourceOutputs.IsValid() || (Surface
			&& Surface->BaseColor
			&& Surface->Normal
			&& Surface->RoughnessAOMetallic);
		Data.bHasNormal = Data.SourceOutputs.IsValid() || LayerNormal != nullptr;
		Data.bNormalOnly = bNormalOnly;
		Data.bOverrideNormal = Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override;
		// The same two fields the layer badge reads for BLEND, so the word on the row and the
		// height arithmetic can never disagree. Coat is excluded: it keeps its own semantics.
		Data.bSmoothHeightMerge = !bNormalOnly
			&& Layer.CompositionMode == EMixtormatCompositionMode::Replace
			&& Layer.NormalBlendMode == EMixtormatNormalBlendMode::Combine;
		Data.bFlipNormalY = Layer.bFlipNormalY;
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
				PublishRegionIds(LayerCtx.RegionIdMaps, 6, LaterIds); // Combine
				PublishRegionIds(LayerCtx.RegionIdMaps, 1, PatternIds); // Pattern
				PublishRegionIds(LayerCtx.RegionIdMaps, 3, UvIds); // Cluster
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 1) == nullptr,
					TEXT("A producer cannot source itself or a consumer above it"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 3) == PatternIds,
					TEXT("Pattern is the nearest earlier source"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 6) == UvIds,
					TEXT("Cluster is the nearest earlier source"));
				Check(FindRegionIdsAbove(LayerCtx.RegionIdMaps, 7) == LaterIds,
					TEXT("Combine is the nearest earlier source despite publication order"));
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
