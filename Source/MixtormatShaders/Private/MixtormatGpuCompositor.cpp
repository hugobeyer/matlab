#include "MixtormatGpuCompositor.h"

#include "MixtormatGpuCompositorInternal.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

// The ground every stack composites onto.
//
// Deliberately not a layer. It has no row, no selection, no children and no inspector -- it
// exists so that the bottom of the stack is an ordinary position rather than a privileged one.
// Before it, layer 0 seeded these buffers by replacing them, which meant the bottom layer
// ignored its own mask, feature influence and height blend; a layer therefore rendered
// differently depending on where it sat, and could not be freely dragged to the bottom.
//
// Values are the neutral read for each buffer, in that buffer's own encoding:
//
//   BaseColor  mid gray, so an uncovered or fully masked stack reads as unlit material
//              rather than as a hole. Alpha 1 -- the substrate is opaque by definition.
//   Normal     EncodeNormal(0, 0, 1) == n * 0.5 + 0.5, NOT (0, 0, 1). The buffer stores
//              encoded normals and the composite shader decodes what it reads, so a literal
//              (0, 0, 1) here decodes to (1, 1, ...) normalized -- a hard 45 degree tilt that
//              lights plausibly enough to survive review.
//   PackedRAM  roughness 0.5, AO 1 (unoccluded), metallic 0 (dielectric), specular 0.04.
//   Height     0.5, the midpoint every height comparison in the composite is written around.
//              Not 0: zero is the bottom of the range, not the absence of displacement, and
//              would bias every height blend against the layer above it.
namespace MixtormatSubstrate
{
	static const FVector4f BaseColor(0.5f, 0.5f, 0.5f, 1.0f);
	static const FVector4f Normal(0.5f, 0.5f, 1.0f, 1.0f);
	static const FVector4f PackedRAM(0.5f, 1.0f, 0.0f, 0.04f);
	static const FVector4f Height(0.5f, 0.0f, 0.0f, 0.0f);
}

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
		SHADER_PARAMETER(uint32, HasPackedHeight)
		SHADER_PARAMETER(uint32, HasNormal)
		SHADER_PARAMETER(uint32, NormalOnly)
		SHADER_PARAMETER(uint32, OverrideNormal)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, HeightBlendEnabled)
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
		SHADER_PARAMETER(uint32, PatternUVEnabled)
		SHADER_PARAMETER(uint32, PatternUVSeed)
		SHADER_PARAMETER(uint32, PatternUVOrthogonal)
		SHADER_PARAMETER(float, PatternUVRotationMin)
		SHADER_PARAMETER(float, PatternUVRotationMax)
		SHADER_PARAMETER(float, PatternUVScaleMin)
		SHADER_PARAMETER(float, PatternUVScaleMax)
		SHADER_PARAMETER(float, PatternUVOffset)
		SHADER_PARAMETER(uint32, PatternUVFlipU)
		SHADER_PARAMETER(uint32, PatternUVFlipV)
		SHADER_PARAMETER(uint32, PatternUVVariationEnabled)
		SHADER_PARAMETER(uint32, PatternIntrinsicOrientationEnabled)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PatternUVField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PatternOrientationField)
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

namespace MixtormatGpuCompositor
{
	static FTextureRHIRef GetTextureRHI(UTexture2D* Texture)
	{
		return Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
	}

	// The debug clear, in the same space the shaders write.
	//
	// The palette lives in MixtormatDebugColor.ush and is authored in sRGB. These two clears are
	// the only copies outside it, and they have to be converted the same way or an untouched
	// region of the preview sits at a different brightness from the ramp drawn over it -- which
	// reads as the debug view having two backgrounds.
	static FLinearColor DebugClearColor()
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

	static FTextureRHIRef GetTargetRHI(UTextureRenderTarget2D* Target)
	{
		return Target && Target->GameThread_GetRenderTargetResource()
			? Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture()
			: FTextureRHIRef();
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
		const FLayerRenderData& Layer)
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
		Parameters->HasPackedHeight = Layer.bHasPackedHeight ? 1u : 0u;
		Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
		Parameters->NormalOnly = Layer.bNormalOnly ? 1u : 0u;
		Parameters->OverrideNormal = Layer.bOverrideNormal ? 1u : 0u;
		Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
		Parameters->HeightBlendEnabled = Layer.bHeightBlendEnabled ? 1u : 0u;
		Parameters->HeightSource = Layer.HeightSource;
		Parameters->InvertHeight = Layer.bInvertHeight ? 1u : 0u;
		Parameters->DirectHeightComparison = Layer.bDirectHeightComparison ? 1u : 0u;
		Parameters->InvertHeightFeature = Layer.bInvertHeightFeature ? 1u : 0u;
		Parameters->InvertAOFeature = Layer.bInvertAOFeature ? 1u : 0u;
		Parameters->InvertFeature = Layer.bInvertFeature ? 1u : 0u;
		Parameters->DebugMode = static_cast<uint32>(Request.DebugSettings.Mode);

		// Stain and ClusterIds publish their own previews before this composite.
		// Neither has a case in the composite shader: exclude both or DebugValue 0
		// would overwrite the selected child's preview with flat DebugLow.
		Parameters->WriteDebug =
			Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::None
			&& Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::Stain
			&& Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::ClusterIds
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
		Parameters->HeightBorderNormalStrength = Layer.HeightBorderNormalStrength;

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
				|| (FMath::Abs(Layer.HeightBorderLift) > 1.0e-4f
					&& Layer.HeightBorderNormalStrength > 0.0f));
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
		Parameters->LayerBC = RegisterTexture(
			GraphBuilder,
			RegisteredTextures,
			Layer.BaseColor,
			TEXT("Mixtormat.LayerBC"));
		Parameters->LayerN = RegisterTexture(
			GraphBuilder,
			RegisteredTextures,
			Layer.Normal,
			TEXT("Mixtormat.LayerN"));
		Parameters->LayerRAM = RegisterTexture(
			GraphBuilder,
			RegisteredTextures,
			Layer.RAM,
			TEXT("Mixtormat.LayerRAM"));
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

		// The last Pattern row that needs source-space work wins. Herringbone and
		// Basketweave always need their intrinsic basis; other modes only enter when
		// random Pattern UV variation is enabled.
		const FPatternIdPassOutput* ActivePatternUV = nullptr;
		for (const FPatternIdPassOutput& PatternOutput : PatternOutputs)
		{
			if (PatternOutput.Settings
				&& (PatternOutput.Settings->bUVVariation
					|| HasIntrinsicPatternOrientation(*PatternOutput.Settings)))
			{
				ActivePatternUV = &PatternOutput;
			}
		}
		const FPatternIdRenderData* PatternUVSettings =
			ActivePatternUV ? ActivePatternUV->Settings : nullptr;
		Parameters->PatternUVEnabled = ActivePatternUV ? 1u : 0u;
		Parameters->PatternUVSeed = PatternUVSettings ? PatternUVSettings->Seed : 0u;
		Parameters->PatternUVOrthogonal =
			PatternUVSettings && PatternUVSettings->bOrthogonalUV ? 1u : 0u;
		Parameters->PatternUVRotationMin =
			PatternUVSettings ? PatternUVSettings->UVRotationMin : 0.0f;
		Parameters->PatternUVRotationMax =
			PatternUVSettings ? PatternUVSettings->UVRotationMax : 0.0f;
		Parameters->PatternUVScaleMin =
			PatternUVSettings ? PatternUVSettings->UVScaleMin : 1.0f;
		Parameters->PatternUVScaleMax =
			PatternUVSettings ? PatternUVSettings->UVScaleMax : 1.0f;
		Parameters->PatternUVOffset =
			PatternUVSettings ? PatternUVSettings->UVOffset : 0.0f;
		Parameters->PatternUVFlipU =
			PatternUVSettings && PatternUVSettings->bRandomFlipU ? 1u : 0u;
		Parameters->PatternUVFlipV =
			PatternUVSettings && PatternUVSettings->bRandomFlipV ? 1u : 0u;
		Parameters->PatternUVVariationEnabled =
			PatternUVSettings && PatternUVSettings->bUVVariation ? 1u : 0u;
		Parameters->PatternIntrinsicOrientationEnabled =
			PatternUVSettings && HasIntrinsicPatternOrientation(*PatternUVSettings) ? 1u : 0u;
		Parameters->PatternRegionIds =
			ActivePatternUV ? ActivePatternUV->Ids : EmptyRegionIds;
		Parameters->PatternUVField =
			ActivePatternUV ? ActivePatternUV->UV : EmptyPatternUV;
		Parameters->PatternOrientationField =
			ActivePatternUV ? ActivePatternUV->Orientation : EmptyPatternOrientation;

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

	FlushRenderingCommands();
	PublishedTargetIndex = 0;
	bInitialized = true;
	return true;
}

bool FMixtormatGpuCompositor::RequestCompose(
	const TArray<FMixtormatLayer>& Layers,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings)
{
	using namespace MixtormatGpuCompositor;
	check(IsInGameThread());
	if (!bInitialized && !Initialize())
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
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Request.OutputBC[Index] = GetTargetRHI(Targets[Index].BaseColor.Get());
		Request.OutputN[Index] = GetTargetRHI(Targets[Index].Normal.Get());
		Request.OutputRAM[Index] = GetTargetRHI(Targets[Index].RAM.Get());
		Request.OutputHeight[Index] = GetTargetRHI(Targets[Index].Height.Get());
		Request.OutputDebug[Index] = GetTargetRHI(Targets[Index].Debug.Get());
		if (!Request.OutputBC[Index].IsValid()
			|| !Request.OutputN[Index].IsValid()
			|| !Request.OutputRAM[Index].IsValid()
			|| !Request.OutputHeight[Index].IsValid()
			|| !Request.OutputDebug[Index].IsValid())
		{
			return false;
		}
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		FMixtormatLayer Layer = Layers[LayerIndex];
		MixtormatParameterBinding::ApplyDirectReferences(Layers, Layer);
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous();
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
				RampData.NormalStrength = FMath::IsFinite(Ramp.NormalStrength)
					? FMath::Max(Ramp.NormalStrength, 0.0f) : 8.0f;
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
				PatternData.NormalStrength = FMath::IsFinite(Pattern.NormalStrength)
					? FMath::Max(Pattern.NormalStrength, 0.0f) : 8.0f;
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

				UTexture2D* IdTexture = ColorIdMask.IdTexture.LoadSynchronous();

				// A node with no map or no colours selects nothing, and selecting nothing is not
				// the same as being the identity: it would blend a mask of zero. Dropping it
				// entirely is what an unconfigured node should do, and matches how a painted mask
				// with no texture behaves.
				if (!IdTexture || ColorIdMask.Colors.IsEmpty())
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::ColorId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FColorIdRenderData& IdData = ChildData.ColorId;
				IdData.IdTexture = GetTextureRHI(IdTexture);
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
				IdData.bInvert = ColorIdMask.bInvert;
				IdData.Tiling = FVector2f(
					static_cast<float>(FMath::Max(ColorIdMask.TilingX, 1)),
					static_cast<float>(FMath::Max(ColorIdMask.TilingY, 1)));
				IdData.UVOffset = FVector2f(ColorIdMask.UVOffsetX, ColorIdMask.UVOffsetY);
				IdData.bFlipU = ColorIdMask.bFlipU;
				IdData.bFlipV = ColorIdMask.bFlipV;
				IdData.Rotation = static_cast<int32>(ColorIdMask.Rotation);
				IdData.Balance = FMath::Clamp(ColorIdMask.Balance, 0.0f, 2.0f);
				IdData.Contrast = FMath::Clamp(ColorIdMask.Contrast, 0.0f, 10.0f);
				IdData.Offset = FMath::Clamp(ColorIdMask.Offset, -1.0f, 1.0f);
				Data.bHasMask = true;
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
					for (const FMixtormatLayer& SourceLayer : Layers)
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

				UTexture2D* MaskTexture = nullptr;
				if (!bPublishedSource)
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
						&& Layer.Children[OwnerIndex].Type == EMixtormatLayerChildType::Effect
						&& !Layer.Children[OwnerIndex].ScopeOwnerChildId.IsValid())
					{
						ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
					}
				}
				FMaskRenderData& MaskData = ChildData.Mask;
				if (bPublishedSource)
				{
					MaskData.PublishedSourceLayerId = MaskLayer.PublishedSourceLayerId;
					MaskData.PublishedSourceChildIndex = PublishedSourceChildIndex;
					MaskData.PublishedSourceOutput = MaskLayer.PublishedSourceOutput;
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
				GeneratedData.Balance = FMath::Max(GeneratedMask.Balance, 0.0f);
				GeneratedData.Contrast = FMath::Max(GeneratedMask.Contrast, 0.0f);
				GeneratedData.Offset = GeneratedMask.Offset;
				GeneratedData.bInvert = GeneratedMask.bInvert;
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
				CrackData.ReliefNormalStrength = FMath::Max(Craquelure.ReliefNormalStrength, 0.0f);
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

			// Procedural effects carry no source maps and so have no asset to read a type
			// from. Asset-backed effects are unchanged.
			// Every Filter is procedural -- none of them read source maps -- and Peeling is
			// the one Surface effect with a procedural path.
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
			FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
			ChildData.Type = EMixtormatLayerChildType::Effect;
			ChildData.SourceChildIndex = SourceChildIndex;
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = ResolvedType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			EffectData.Strength = FMath::Clamp(LayerEffect.Strength, 0.0f, 1.0f);
			if (ResolvedType == EMixtormatEffectType::Erosion)
			{
				// Values pass through unclamped. The inspector constrains the scrub range
				// visually, but a typed value outside it stays intact all the way to the
				// shader, which keeps its own epsilon guards at the division sites.
				EffectData.ErosionAmount = LayerEffect.ErosionAmount;
				EffectData.ErosionStrength = LayerEffect.ErosionStrength;
				EffectData.ErosionOctaves = FMath::Clamp(LayerEffect.ErosionOctaves, 1, 12);
				EffectData.ErosionPeriod = FMath::Clamp(LayerEffect.ErosionPeriod, 1, 1024);
				EffectData.ErosionGain = LayerEffect.ErosionGain;
				EffectData.ErosionDetail = LayerEffect.ErosionDetail;
				EffectData.ErosionGullyWeight = LayerEffect.ErosionGullyWeight;
				EffectData.ErosionNormalization = LayerEffect.ErosionNormalization;
				EffectData.ErosionRidgeRounding = LayerEffect.ErosionRidgeRounding;
				EffectData.ErosionCreaseRounding = LayerEffect.ErosionCreaseRounding;
				EffectData.ErosionSlopeOnset = LayerEffect.ErosionSlopeOnset;
				EffectData.ErosionFeatureOnset = LayerEffect.ErosionFeatureOnset;
				EffectData.ErosionAssumedSlope = LayerEffect.ErosionAssumedSlope;
				EffectData.ErosionAssumedSlopeAmount = LayerEffect.ErosionAssumedSlopeAmount;
				EffectData.ErosionNormalStrength = LayerEffect.ErosionNormalStrength;
				EffectData.ErosionSlopeRadius = FMath::Clamp(LayerEffect.ErosionSlopeRadius, 1, 32);
				EffectData.ErosionSlopeBlur = LayerEffect.ErosionSlopeBlur;
				EffectData.ErosionCurvatureMode = static_cast<int32>(LayerEffect.ErosionCurvatureMode);
				EffectData.ErosionCavityInfluence = LayerEffect.ErosionCavityInfluence;
				EffectData.ErosionCavityOffset = LayerEffect.ErosionCavityOffset;
				EffectData.ErosionCavityRemapMin = LayerEffect.ErosionCavityRemapMin;
				EffectData.ErosionCavityRemapMax = LayerEffect.ErosionCavityRemapMax;
				EffectData.ErosionHeightInfluence = LayerEffect.ErosionHeightInfluence;
				EffectData.ErosionHeightScale = LayerEffect.ErosionHeightScale;
				EffectData.ErosionMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.ErosionMaskTiling));
				EffectData.bErosionInvertMask = LayerEffect.bErosionInvertMask;
				{
					UTexture2D* PlacementMask = LayerEffect.ErosionMaskTexture.LoadSynchronous();
					if (!PlacementMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.ErosionMask.LoadSynchronous())
						{
							PlacementMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (PlacementMask)
					{
						EffectData.ErosionPlacementMask = GetTextureRHI(PlacementMask);
					}
				}

				EffectData.ErosionRoughnessAmount = LayerEffect.ErosionRoughnessAmount;
				EffectData.ErosionCarveDepth = LayerEffect.ErosionCarveDepth;
			}

			if (ResolvedType == EMixtormatEffectType::Grade)
			{
				EffectData.GradeAmount = LayerEffect.GradeAmount;
				EffectData.GradeTonemap = static_cast<int32>(LayerEffect.GradeTonemap);
				EffectData.GradeTonemapStrength =
					FMath::Clamp(LayerEffect.GradeTonemapStrength, 0.0f, 1.0f);
				EffectData.GradeBrightness = LayerEffect.GradeBrightness;
				EffectData.GradeContrast = LayerEffect.GradeContrast;
				EffectData.GradeContrastPivot = LayerEffect.GradeContrastPivot;
				EffectData.GradeGamma = LayerEffect.GradeGamma;
				EffectData.GradeInputMin = LayerEffect.GradeInputMin;
				EffectData.GradeInputMax = LayerEffect.GradeInputMax;
				EffectData.GradeOutputMin = LayerEffect.GradeOutputMin;
				EffectData.GradeOutputMax = LayerEffect.GradeOutputMax;
				EffectData.GradeChannelBias = FVector3f(
					LayerEffect.GradeBiasR, LayerEffect.GradeBiasG, LayerEffect.GradeBiasB);
				EffectData.bGradeInvertMask = LayerEffect.bGradeInvertMask;
			}

			if (ResolvedType == EMixtormatEffectType::Chipping)
			{
				EffectData.ChipAmount = LayerEffect.ChipAmount;
				EffectData.ChipGroutLevel = LayerEffect.ChipGroutLevel;
				EffectData.ChipGroutSoftness = LayerEffect.ChipGroutSoftness;
				EffectData.ChipSize = LayerEffect.ChipSize;
				EffectData.ChipDepth = LayerEffect.ChipDepth;
				EffectData.ChipIrregularity = LayerEffect.ChipIrregularity;
				EffectData.ChipIterations = FMath::Clamp(LayerEffect.ChipIterations, 1, 32);
				EffectData.ChipMaskEdge = LayerEffect.ChipMaskEdge;
				EffectData.ChipNormalStrength = LayerEffect.ChipNormalStrength;
				EffectData.ChipCavityInfluence = LayerEffect.ChipCavityInfluence;
				EffectData.ChipCavityOffset = LayerEffect.ChipCavityOffset;
				EffectData.ChipCavityRemapMin = LayerEffect.ChipCavityRemapMin;
				EffectData.ChipCavityRemapMax = LayerEffect.ChipCavityRemapMax;
				EffectData.ChipHeightInfluence = LayerEffect.ChipHeightInfluence;
				EffectData.ChipHeightScale = LayerEffect.ChipHeightScale;
				EffectData.ChipMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.ChipMaskTiling));
				EffectData.bChipInvertMask = LayerEffect.bChipInvertMask;
				{
					UTexture2D* PlacementMask = LayerEffect.ChipMaskTexture.LoadSynchronous();
					if (!PlacementMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.ChipMask.LoadSynchronous())
						{
							PlacementMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (PlacementMask)
					{
						EffectData.ChipPlacementMask = GetTextureRHI(PlacementMask);
					}
				}
				EffectData.ChipSeed = static_cast<uint32>(FMath::Max(LayerEffect.ChipSeed, 0));
				EffectData.ChipRoughnessAmount = LayerEffect.ChipRoughnessAmount;
			}


			if (ResolvedType == EMixtormatEffectType::FlowWarp)
			{
				EffectData.FlowWarpAmount = LayerEffect.FlowWarpAmount;
				EffectData.FlowWarpWeight = FMath::Clamp(LayerEffect.FlowWarpWeight, 0.0f, 1.0f);
				EffectData.FlowWarpScale = FMath::Clamp(LayerEffect.FlowWarpScale, 1, 128);
				EffectData.FlowWarpDirection = LayerEffect.FlowWarpDirection;
				EffectData.FlowWarpSeed = static_cast<uint32>(FMath::Max(LayerEffect.FlowWarpSeed, 0));
				EffectData.FlowWarpMaskSlopeInfluence =
					FMath::Max(LayerEffect.FlowWarpMaskSlopeInfluence, 0.0f);
				EffectData.FlowWarpHeightSlopeInfluence =
					FMath::Max(LayerEffect.FlowWarpHeightSlopeInfluence, 0.0f);
				EffectData.FlowWarpDerivativeKernel = FVector2f(
					FMath::Clamp(LayerEffect.FlowWarpDerivativeKernelX, 1.0f, 64.0f),
					FMath::Clamp(LayerEffect.FlowWarpDerivativeKernelY, 1.0f, 64.0f));
				EffectData.FlowWarpBlendMode = static_cast<uint32>(LayerEffect.FlowWarpBlendMode);
			}

			if (ResolvedType == EMixtormatEffectType::WornEdges)
			{
				EffectData.EdgeWearRadius = FMath::Clamp(LayerEffect.EdgeWearRadius, 1, 64);
				EffectData.EdgeWearSlope = LayerEffect.EdgeWearSlope;
				EffectData.EdgeWearStrength = LayerEffect.EdgeWearStrength;
				EffectData.EdgeWearFeather = LayerEffect.EdgeWearFeather;
				EffectData.EdgeWearDirections = FMath::Clamp(LayerEffect.EdgeWearDirections, 8, 32);
				EffectData.EdgeWearAngularAA = LayerEffect.EdgeWearAngularAA;
				EffectData.EdgeWearGravity = LayerEffect.EdgeWearGravity;
				EffectData.EdgeWearGravityAngle = LayerEffect.EdgeWearGravityAngle;
				EffectData.EdgeWearSeed = static_cast<uint32>(FMath::Max(LayerEffect.EdgeWearSeed, 0));
				EffectData.EdgeWearMacroScale = LayerEffect.EdgeWearMacroScale;
				EffectData.EdgeWearMacroAmount = LayerEffect.EdgeWearMacroAmount;
				EffectData.EdgeWearCellScale = LayerEffect.EdgeWearCellScale;
				EffectData.EdgeWearCellAmount = LayerEffect.EdgeWearCellAmount;
				EffectData.EdgeWearRidgeScale = LayerEffect.EdgeWearRidgeScale;
				EffectData.EdgeWearRidgeAmount = LayerEffect.EdgeWearRidgeAmount;
				EffectData.EdgeWearMicroScale = LayerEffect.EdgeWearMicroScale;
				EffectData.EdgeWearMicroAmount = LayerEffect.EdgeWearMicroAmount;
				EffectData.EdgeWearWarpScale = LayerEffect.EdgeWearWarpScale;
				EffectData.EdgeWearWarpAmount = LayerEffect.EdgeWearWarpAmount;
				EffectData.EdgeWearNoiseContrast = LayerEffect.EdgeWearNoiseContrast;
				EffectData.EdgeWearIdVariation = LayerEffect.EdgeWearIdVariation;
				EffectData.EdgeWearIdRadius = LayerEffect.EdgeWearIdRadius;
				EffectData.EdgeWearIdSlope = LayerEffect.EdgeWearIdSlope;
				EffectData.EdgeWearIdStrength = LayerEffect.EdgeWearIdStrength;
				EffectData.EdgeWearIdNoise = LayerEffect.EdgeWearIdNoise;
				EffectData.EdgeWearRoughnessWeight = FMath::Clamp(
					LayerEffect.EdgeWearRoughnessWeight, 0.0f, 1.0f);
				EffectData.EdgeWearRoughnessOffset = FMath::Clamp(
					LayerEffect.EdgeWearRoughnessOffset, -1.0f, 1.0f);
			}

			if (ResolvedType == EMixtormatEffectType::Stain)
			{
				EffectData.StainMode = static_cast<int32>(LayerEffect.StainMode);
				EffectData.StainSourceMaskTiling = FMath::Max(
					1.0f, static_cast<float>(LayerEffect.StainSourceMaskTiling));
				EffectData.StainDirtMaskTiling = FMath::Max(
					1.0f, static_cast<float>(LayerEffect.StainDirtMaskTiling));
				EffectData.bStainSourceMaskInvert = LayerEffect.bStainSourceMaskInvert;
				EffectData.bStainDirtMaskInvert = LayerEffect.bStainDirtMaskInvert;
				EffectData.StainIterations = FMath::Clamp(LayerEffect.StainIterations, 4, 64);
				EffectData.StainSeed = static_cast<uint32>(FMath::Max(LayerEffect.StainSeed, 1));
				EffectData.StainSourceAmount = LayerEffect.StainSourceAmount;
				EffectData.StainGravity = LayerEffect.StainGravity;
				EffectData.StainSurfaceFollow = LayerEffect.StainSurfaceFollow;
				EffectData.StainSpread = LayerEffect.StainSpread;
				EffectData.StainAccumulation = LayerEffect.StainAccumulation;
				EffectData.StainAbsorption = LayerEffect.StainAbsorption;
				EffectData.StainDrying = LayerEffect.StainDrying;
				EffectData.StainDirtAmount = LayerEffect.StainDirtAmount;
				EffectData.StainConcavityWeight = LayerEffect.StainConcavityWeight;
				EffectData.StainConvexityWeight = LayerEffect.StainConvexityWeight;
				EffectData.StainOcclusionWeight = LayerEffect.StainOcclusionWeight;
				EffectData.StainHeightWeight = LayerEffect.StainHeightWeight;
				EffectData.StainSourceHeightBias = LayerEffect.StainSourceHeightBias;
				EffectData.StainSlopeWeight = LayerEffect.StainSlopeWeight;
				EffectData.StainSurfaceResponse = LayerEffect.StainSurfaceResponse;

				const auto ResolveStainMask = [](const TSoftObjectPtr<UMixtormatMask>& MaskAssetRef,
					const TSoftObjectPtr<UTexture2D>& TextureRef) -> FTextureRHIRef
				{
					UTexture2D* Texture = TextureRef.LoadSynchronous();
					if (!Texture)
					{
						if (const UMixtormatMask* MaskAsset = MaskAssetRef.LoadSynchronous())
						{
							Texture = MaskAsset->MaskTexture.Get();
						}
					}
					return Texture ? GetTextureRHI(Texture) : FTextureRHIRef();
				};
				EffectData.StainSourceMask = ResolveStainMask(
					LayerEffect.StainSourceMask, LayerEffect.StainSourceMaskTexture);
				EffectData.StainDirtMask = ResolveStainMask(
					LayerEffect.StainDirtMask, LayerEffect.StainDirtMaskTexture);

				// Stain resolves into the layer's mask chain, so it makes the layer masked in
				// exactly the way an authored, generated, craquelure or colour-ID child does.
				// Without this the composite took HasMask 0, ignored the chain the stain had
				// just written, and the layer covered fully -- which is why a stain only
				// appeared to work once some other mask child was added in front of it.
				Data.bHasMask = true;
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

			// Peeling is procedural, full stop. It used to have a second path driven by an
			// authored map set on the effect asset -- a PDM plus coverage mask, height and SDF --
			// and that is gone; an effect asset now names a type and carries defaults, nothing
			// more. The bare scope is what is left of the branch that chose between them.
			{
				EffectData.bProceduralPeel = true;
				EffectData.PeelType = static_cast<int32>(LayerEffect.PeelType);
				EffectData.PeelMacroPeriod = LayerEffect.PeelMacroPeriod;
				EffectData.PeelMicroPeriod = LayerEffect.PeelMicroPeriod;
				EffectData.PeelRandomSeed = static_cast<uint32>(FMath::Max(LayerEffect.PeelRandomSeed, 1));
				EffectData.PeelSeedThreshold = LayerEffect.PeelSeedThreshold;
				EffectData.PeelSeedNoiseWeight = LayerEffect.PeelSeedNoiseWeight;
				EffectData.PeelSeedCurvatureWeight = LayerEffect.PeelSeedCurvatureWeight;
				EffectData.PeelSeedCurvatureBias = LayerEffect.PeelSeedCurvatureBias;
				EffectData.PeelSeedAOWeight = LayerEffect.PeelSeedAOWeight;
				EffectData.PeelSeedHeightWeight = LayerEffect.PeelSeedHeightWeight;
				EffectData.PeelSeedMaskWeight = LayerEffect.PeelSeedMaskWeight;
				EffectData.bPeelNormalizeSeedWeights = LayerEffect.bPeelNormalizeSeedWeights;
				EffectData.PeelCurvatureRadius = LayerEffect.PeelCurvatureRadius;
				EffectData.PeelGrowthStrength = LayerEffect.PeelGrowthStrength;
				EffectData.PeelAOStrength = LayerEffect.PeelAOStrength;
				EffectData.PeelEdgeSharpness = LayerEffect.PeelEdgeSharpness;
				EffectData.PeelLiftVariation = LayerEffect.PeelLiftVariation;
				EffectData.PeelSizeVariation = LayerEffect.PeelSizeVariation;
				EffectData.PeelClusterPeriod = LayerEffect.PeelClusterPeriod;
				EffectData.PeelSolveDivisor = LayerEffect.PeelSolveDivisor;
				EffectData.PeelMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.PeelMaskTiling));
				EffectData.bPeelMaskInvert = LayerEffect.bPeelMaskInvert;
				{
					// Direct texture wins over the asset, matching how mask children resolve.
					UTexture2D* OwnMask = LayerEffect.PeelMaskTexture.LoadSynchronous();
					if (!OwnMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.PeelMask.LoadSynchronous())
						{
							OwnMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (OwnMask)
					{
						EffectData.PeelOwnMask = GetTextureRHI(OwnMask);
					}
				}
				EffectData.PeelClusterAmount = LayerEffect.PeelClusterAmount;
				EffectData.PeelWarpPeriod = LayerEffect.PeelWarpPeriod;
				EffectData.PeelWarpAmount = LayerEffect.PeelWarpAmount;
				EffectData.PeelWarpSource = LayerEffect.PeelWarpSource;
				EffectData.Front = LayerEffect.Front;
				EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
				EffectData.MacroWarp = LayerEffect.MacroWarp;
				EffectData.MicroWarp = LayerEffect.MicroWarp;
				EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
				EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
				EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
				EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
				EffectData.PeelHeightAmount = LayerEffect.PeelHeightAmount;
				EffectData.bPeelHeightInvert = LayerEffect.bPeelHeightInvert;
				Data.bHasEffects = true;
				continue;
			}

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
		Data.NormalIntensity = Layer.NormalIntensity;
		Data.HueShift = FMath::Clamp(Layer.HueShift, -180.0f, 180.0f);
		Data.Saturation = FMath::Clamp(Layer.Saturation, 0.0f, 2.0f);
		Data.Value = FMath::Clamp(Layer.Value, 0.0f, 2.0f);
		Data.RoughnessBias = Layer.RoughnessBias;
		Data.RoughnessContrast = Layer.RoughnessContrast;
		Data.RoughnessOffset = Layer.RoughnessOffset;
		Data.FillRoughness = Layer.Roughness;
		Data.FillMetallic = Layer.Metallic;
		const float SourceIOR = Surface ? Surface->DefaultIOR : 1.5f;
		const float LayerIOR = FMath::Max(
			1.0f,
			(Layer.Type == EMixtormatLayerType::Fill || Layer.bOverrideIOR)
				? Layer.IOR
				: SourceIOR);
		Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
		Data.HeightBoost = FMath::Clamp(Layer.HeightBoost, 0.0f, 8.0f);
		Data.BaseColorBlendMode = Layer.BaseColorBlendMode;
		Data.BaseColorBlendAmount = FMath::Clamp(Layer.BaseColorBlendAmount, 0.0f, 1.0f);
		Data.BaseColorInfluence = FMath::Clamp(Layer.BaseColorInfluence, 0.0f, 1.0f);
		Data.RoughnessInfluence = FMath::Clamp(Layer.RoughnessInfluence, 0.0f, 1.0f);
		Data.AOInfluence = FMath::Clamp(Layer.AOInfluence, 0.0f, 1.0f);
		Data.MetallicInfluence = FMath::Clamp(Layer.MetallicInfluence, 0.0f, 1.0f);
		Data.F0Influence = FMath::Clamp(Layer.F0Influence, 0.0f, 1.0f);
		Data.NormalInfluence = FMath::Clamp(Layer.NormalInfluence, 0.0f, 1.0f);
		Data.HeightInfluence = FMath::Clamp(Layer.HeightInfluence, 0.0f, 1.0f);
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
		Data.HeightBorderNormalStrength = FMath::Max(Layer.HeightBorderNormalStrength, 0.0f);
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
		Data.bHasPackedHeight = Surface && Surface->bHasBlendHeight;
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
		Data.bHasSurface = Surface
			&& Surface->BaseColor
			&& Surface->Normal
			&& Surface->RoughnessAOMetallic;
		Data.bHasNormal = LayerNormal != nullptr;
		Data.bNormalOnly = bNormalOnly;
		Data.bOverrideNormal = Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override;
		Data.bFlipNormalY = Layer.bFlipNormalY;
	}

	PublishedTargetIndex = Request.Layers.IsEmpty()
		? 0
		: (Request.Layers.Num() - 1) & 1;
	Request.PublishedTargetIndex = PublishedTargetIndex;
	Request.NetworkCache = NetworkCache;

	ENQUEUE_RENDER_COMMAND(MixtormatComposite)(
		[Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FMixtormatComposeContext Ctx(GraphBuilder, Request);
			TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
			FRDGTextureRef* const OutputBC = Ctx.OutputBC;
			FRDGTextureRef* const OutputN = Ctx.OutputN;
			FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
			FRDGTextureRef* const OutputHeight = Ctx.OutputHeight;
			FRDGTextureRef* const OutputDebug = Ctx.OutputDebug;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				OutputBC[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputBC[Index],
					TEXT("Mixtormat.OutputBC"));
				OutputN[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputN[Index],
					TEXT("Mixtormat.OutputN"));
				OutputRAM[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputRAM[Index],
					TEXT("Mixtormat.OutputRAM"));
				OutputHeight[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputHeight[Index],
					TEXT("Mixtormat.OutputHeight"));
				OutputDebug[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputDebug[Index],
					TEXT("Mixtormat.OutputDebug"));
			}

			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]),
				FVector4f(DebugClearColor()));

			// Stand-in for the composite's RegionIds slot on every layer without a cluster
			// filter. RDG validates a binding whether the shader branches on it or not, so the
			// slot has to hold something real; one 1x1 texture for the whole graph is the
			// cheapest something there is. Cleared to the no-region sentinel, so if the tint
			// branch were ever entered against it the result is a pass-through rather than a
			// colour hashed out of uninitialised memory.
			FRDGTextureRef& EmptyRegionIds = Ctx.EmptyRegionIds;
			EmptyRegionIds = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_R32_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.EmptyRegionIds"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyRegionIds), 0xffffffffu);

			FRDGTextureRef& EmptyPatternUV = Ctx.EmptyPatternUV;
			EmptyPatternUV = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_G16R16F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.EmptyPatternUV"));
			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(EmptyPatternUV),
				FVector4f(0.5f, 0.5f, 0.0f, 0.0f));

			FRDGTextureRef& EmptyPatternOrientation = Ctx.EmptyPatternOrientation;
			EmptyPatternOrientation = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_R8_UINT,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.EmptyPatternOrientation"));
			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(EmptyPatternOrientation),
				0u);

			// No Driver on this layer, or one that could not resolve: the slot still needs a real
			// resource. Cleared to zero, which every Combine mode turns into a no-op once Amount
			// is applied -- and the shader's Enabled flag stops it being read at all.
			FRDGTextureRef& EmptyDriverSignal = Ctx.EmptyDriverSignal;
			EmptyDriverSignal = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_R16F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.EmptyDriverSignal"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyDriverSignal), FVector4f::Zero());

			// Demand-driven: only layers some other layer's Driver actually names get a snapshot,
			// because the mask pair is rotated across the whole graph and a layer's combined mask
			// is overwritten by the next layer that runs. Collected before the loop so layer N
			// already knows whether it has to be copied when its own mask is final.
			TSet<FGuid>& DriverSnapshotDemand = Ctx.DriverSnapshotDemand;
			for (const FLayerRenderData& DemandLayer : Request.Layers)
			{
				for (const FScalarDriverRenderData& Driver : DemandLayer.ScalarDrivers)
				{
					// Mask sources only. A region map is never snapshotted -- it is read in the
					// layer that produced it, in that layer's own pixel space.
					if (Driver.bEnabled
						&& !Driver.bRegionSource
						&& Driver.SourceLayerId != DemandLayer.LayerId)
					{
						DriverSnapshotDemand.Add(Driver.SourceLayerId);
					}
				}
			}
			TMap<FGuid, FRDGTextureRef>& DriverSnapshots = Ctx.DriverSnapshots;
			TMap<FPublishedMaskKey, FRDGTextureRef>& PublishedMaskOutputs = Ctx.PublishedMaskOutputs;

			if (Request.Layers.IsEmpty())
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[0]), MixtormatSubstrate::BaseColor);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[0]), MixtormatSubstrate::Normal);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[0]), MixtormatSubstrate::PackedRAM);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputHeight[0]), MixtormatSubstrate::Height);
			}
			else
			{
				FMixtormatLayerPassContext LayerCtx(Ctx);
				FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
				MaskDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
				const FRDGTextureRef MaskTargetsInit[2] =
				{
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskA")),
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskB"))
				};
				MaskTargets[0] = MaskTargetsInit[0];
				MaskTargets[1] = MaskTargetsInit[1];
				// Both halves cleared before anything reads either. The first mask child on a
				// layer binds the half it is not writing as PreviousMask and ignores the value --
				// Initialize makes it treat Previous as zero -- but RDG validates the binding
				// rather than the use, and a transient nothing has written trips
				// "has a read dependency on Mixtormat.MaskB, but it was never written to" on
				// every composite. The ensure fires once per session and is easy to never see;
				// the read itself was always harmless, and this makes the graph honest about it.
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[1]), FVector4f(0.0f));

				// The substrate, seeded into the half the bottom layer reads.
				//
				// Layer 0 composites onto this the way every other layer composites onto the
				// layer below it, which is the whole point: it used to seed these buffers
				// itself via Initialize, and a seeding layer ignores its own mask, feature
				// influence and height blend. That made position 0 a different thing to be,
				// so a layer could not be dragged to the bottom without changing what it did.
				//
				// Parity matters here and is easy to get backwards. WriteIndex is
				// LayerIndex & 1, so layer 0 writes half 0 and reads half 1 -- the substrate
				// belongs in half 1. Half 0 needs no seed; layer 0 overwrites it.
				FRDGTextureRef* const HeightTargets = OutputHeight;
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[1]), MixtormatSubstrate::BaseColor);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[1]), MixtormatSubstrate::Normal);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[1]), MixtormatSubstrate::PackedRAM);
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[0]),
					MixtormatSubstrate::Height);
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[1]),
					MixtormatSubstrate::Height);

				// Drainage and crest lines, produced by the erosion filter and consumed by
				// generated masks on later layers.
				//
				// Ping-ponged on the layer index like every other surface input a generated
				// mask reads, so a mask on layer N sees what was accumulated below layer N.
				// A single shared target would be read-after-write across layers with no
				// versioning, and a mask would see its own layer's ridge on some layers and
				// not others depending on child order.
				//
				// That means every layer has to write it, not only eroding ones: a layer that
				// left its slot alone would hand the next layer the ridge from two layers
				// back. Layers without erosion copy read to write below.
				FRDGTextureRef* const RidgeTargets = LayerCtx.RidgeTargets;
				RidgeTargets[0] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeA"));
				RidgeTargets[1] = GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeB"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[1]), FVector4f(0.0f));

				FRDGTextureDesc& EffectDesc = LayerCtx.EffectDesc;
				EffectDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef* const EffectTargets = LayerCtx.EffectTargets;
				EffectTargets[0] = GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectA"));
				EffectTargets[1] = GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectB"));

				// Cleared for the same reason the mask pair is: the first surface effect on a
				// layer binds the half it is not writing and ignores it, and RDG validates the
				// binding rather than the use. White, matching the desc's own clear value and the
				// neutral coverage the effect shader expects to read.
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[0]), FVector4f(1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[1]), FVector4f(1.0f));
				// Peel relief, signed around zero so stacked peels accumulate.
				FRDGTextureDesc& EffectHeightDesc = LayerCtx.EffectHeightDesc;
				EffectHeightDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef* const EffectHeightTargets = LayerCtx.EffectHeightTargets;
				EffectHeightTargets[0] =
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightA"));
				EffectHeightTargets[1] =
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightB"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[1]), FVector4f(0.0f));



				// Placeholders so the peel and peel-field parameter structs always have a
				// bound resource in slots the active mode does not use. Never read, never
				// written; a 1x1 keeps them free.
				const FRDGTextureDesc TinyRGDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				const FRDGTextureDesc TinyRGBADesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef& PeelNoiseDummy = LayerCtx.PeelNoiseDummy;
				PeelNoiseDummy = GraphBuilder.CreateTexture(TinyRGDesc, TEXT("Mixtormat.PeelNoiseDummy"));
				FRDGTextureRef& PeelFieldDummy = LayerCtx.PeelFieldDummy;
				PeelFieldDummy = GraphBuilder.CreateTexture(TinyRGBADesc, TEXT("Mixtormat.PeelFieldDummy"));

				// Bound wherever a peel input is absent -- the peel's own mask when the layer
				// uses its child mask, and the authored map slots on the procedural path. RDG
				// rejects a pass that reads a transient texture nothing has written, which the
				// log reported as an error on every procedural peel composite, so it is cleared
				// once here rather than left undefined.
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(PeelFieldDummy),
					FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(PeelNoiseDummy),
					FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

				TSet<int32>& RequiredHeightSnapshots = Ctx.RequiredHeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const int32 ReferenceIndex = Request.Layers[LayerIndex].HeightReferenceLayerIndex;
					if (ReferenceIndex >= 0 && ReferenceIndex < LayerIndex)
					{
						RequiredHeightSnapshots.Add(ReferenceIndex);
					}
				}
				TMap<int32, FRDGTextureRef>& HeightSnapshots = Ctx.HeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const FLayerRenderData& Layer = Request.Layers[LayerIndex];
					LayerCtx.BeginLayer(LayerIndex);

					TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
					TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
						LayerCtx.PatternOutputs;
					AddRegionProducerPasses(Ctx, LayerCtx, Layer);

					FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
					CombinedMask = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Mask,
						TEXT("Mixtormat.WhiteMask"));
					FRDGTextureRef& CombinedEffectData = LayerCtx.CombinedEffectData;
					CombinedEffectData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.DefaultEffectData"));
					FRDGTextureRef& CombinedEffectHeight = LayerCtx.CombinedEffectHeight;
					CombinedEffectHeight = EffectHeightTargets[0];
					FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
					DebugMask = CombinedMask;
					FPendingEffect& PendingErosion = LayerCtx.PendingErosion;
					FPendingEffect& PendingChipping = LayerCtx.PendingChipping;


					TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
						LayerCtx.PendingWornEdges;

					// Craquelure relief, deferred out of the child loop for the same reason
					// erosion and chipping are: the loop runs before the layer composites, so a
					// carve made here would be painted straight back over.
					//
					// The distance field is carried rather than looked up again later, because
					// the mask targets it was produced alongside are ping-ponged -- any later
					// mask child overwrites the slot, and relief would then read whichever child
					// happened to run last instead of its own network.
					TArray<FPendingCraquelureRelief, TInlineAllocator<2>>& PendingCraquelureReliefs =
						LayerCtx.PendingCraquelureReliefs;

					// Region relief and edge shading are deferred for exactly the reason
					// craquelure relief is: their fields exist before the composite, but the
					// surface they modify does not exist until after it.
					TArray<FPendingRampTilt, TInlineAllocator<2>>& PendingRampTilts =
						LayerCtx.PendingRampTilts;
					CollectPendingRampTilts(Ctx, LayerCtx, Layer);

					// An array where erosion keeps a single pointer. Two erosions on one layer
					// is nonsense, but a brightness grade and a separate tonemap grade is an
					// ordinary way to use an adjustment layer, and dropping all but the last
					// would read as a bug rather than as a contract.
					TArray<FPendingEffect, TInlineAllocator<2>>& PendingFlowWarps = LayerCtx.PendingFlowWarps;
					TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;
					int32& MaskPassIndex = LayerCtx.MaskPassIndex;
					int32& EffectPassIndex = LayerCtx.EffectPassIndex;


					for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
					{
						const FChildRenderData& Child = Layer.Children[ChildIndex];
						if (Child.Type == EMixtormatLayerChildType::Filter
							|| Child.Type == EMixtormatLayerChildType::PatternId
							|| Child.Type == EMixtormatLayerChildType::HsvFilter
							|| Child.Type == EMixtormatLayerChildType::RampId)
						{
							// All three are handled outside this loop -- the cluster in the
							// pre-mask phase, the HSV filter at the composite's albedo sample,
							// the ramp tilt after the composite. None may fall through to the
							// effect branch below.
							continue;
						}
						if (Child.Type == EMixtormatLayerChildType::Generated)
						{
							AddGeneratedMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::Craquelure)
						{
							AddCraquelureMaskPasses(Ctx, LayerCtx, Layer, Child, ChildIndex);
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::RandomId)
						{
							AddRandomIdMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::ColorId)
						{
							AddColorIdMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::Mask)
						{
							AddTextureMaskPass(Ctx, LayerCtx, Layer, Child, ChildIndex);
							continue;
						}

						const FEffectRenderData& Effect = Child.Effect;
						FRDGTextureRef FeatureMask =
							AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex);
						if (Effect.Type == EMixtormatEffectType::Erosion)
						{
							QueuePendingErosion(LayerCtx, Layer, Child, Effect, FeatureMask);
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Chipping)
						{
							QueuePendingChipping(LayerCtx, Layer, Child, Effect, FeatureMask);
							continue;
						}


						if (Effect.Type == EMixtormatEffectType::WornEdges)
						{
							QueuePendingWornEdges(LayerCtx, Child, Effect, FeatureMask);
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::FlowWarp)
						{
							QueuePendingFlowWarp(LayerCtx, Layer, Child, Effect, FeatureMask);
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Grade)
						{
							QueuePendingGrade(LayerCtx, Layer, Child, Effect, FeatureMask);
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Stain)
						{
							AddStainMaskPasses(Ctx, LayerCtx, Layer, Child, ChildIndex, Effect, FeatureMask);
							continue;
						}

						AddPeelingEffectPasses(Ctx, LayerCtx, Layer, Child, ChildIndex, Effect, FeatureMask);
					}

					AddLayerCompositePass(Ctx, LayerCtx, Layer);

					AddFlowWarpPasses(Ctx, LayerCtx, Layer);

					AddErosionPasses(Ctx, LayerCtx, Layer);

					AddRampReliefPasses(Ctx, LayerCtx, Layer);

					AddCraquelureReliefPasses(Ctx, LayerCtx, Layer);


					AddWornEdgesPasses(Ctx, LayerCtx, Layer);

					AddChippingPasses(Ctx, LayerCtx, Layer);

					AddGradePasses(Ctx, LayerCtx, Layer);

					// The half this layer wrote. Same parity the composite used; taken here because
					// a later layer's ReferenceHeight must see this layer's finished height, after
					// every filter above has had its turn at it.
					const int32 WriteIndex = LayerIndex & 1;
					if (RequiredHeightSnapshots.Contains(LayerIndex))
					{
						FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc,
							TEXT("Mixtormat.HeightSnapshot"));
						AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], Snapshot);
						HeightSnapshots.Add(LayerIndex, Snapshot);
					}
				}
			}

			GraphBuilder.SetTextureAccessFinal(
				OutputBC[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputN[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputRAM[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputHeight[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputDebug[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			if (Request.OnComplete.IsBound())
			{
				AsyncTask(
					ENamedThreads::GameThread,
					[OnComplete = Request.OnComplete]() mutable
					{
						OnComplete.ExecuteIfBound();
					});
			}
		});

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
	MaterialInstance.SetScalarParameterValue(TEXT("DA_RoughnessContrast"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_RoughnessOffset"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_NormalIntensity"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_DielectricF0"), 0.04f);
	MaterialInstance.SetScalarParameterValue(TEXT("DA_UsePackedF0"), 1.0f);
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

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetDebugOutput() const
{
	return Targets[PublishedTargetIndex].Debug.Get();
}
