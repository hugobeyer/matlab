#include "MaterialLabGpuCompositor.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MaterialLabEffect.h"
#include "MaterialLabMask.h"
#include "MaterialLabMaterial.h"
#include "MaterialLabSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FMaterialLabCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMaterialLabCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialLabCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, Enabled)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, HasEffects)
		SHADER_PARAMETER(uint32, HasStain)
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
		SHADER_PARAMETER(float, BaseColorInfluence)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, AOInfluence)
		SHADER_PARAMETER(float, MetallicInfluence)
		SHADER_PARAMETER(float, F0Influence)
		SHADER_PARAMETER(float, NormalInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, StainData)
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
	FMaterialLabCompositeCS,
	"/Plugin/MaterialLab/Private/MaterialLabComposite.usf",
	"MainCS",
	SF_Compute);

class FMaterialLabMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMaterialLabMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialLabMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, IncomingMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMaterialLabMaskCS,
	"/Plugin/MaterialLab/Private/MaterialLabMask.usf",
	"MainCS",
	SF_Compute);

class FMaterialLabPeelingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMaterialLabPeelingCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialLabPeelingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Front)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, MacroWarp)
		SHADER_PARAMETER(float, MicroWarp)
		SHADER_PARAMETER(float, MicroMorph)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Lift)
		SHADER_PARAMETER(float, DetailStrength)
		SHADER_PARAMETER(float, DistanceRange)
		SHADER_PARAMETER(float, SDFRange)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelSDF)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputEffectData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMaterialLabPeelingCS,
	"/Plugin/MaterialLab/Private/MaterialLabPeeling.usf",
	"MainCS",
	SF_Compute);

class FMaterialLabStainCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMaterialLabStainCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialLabStainCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(FVector4f, StainColor)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightWarp)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousStainData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AccumulatedHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputStainData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMaterialLabStainCS,
	"/Plugin/MaterialLab/Private/MaterialLabStain.usf",
	"MainCS",
	SF_Compute);

namespace MaterialLabGpuCompositor
{
	struct FMaskRenderData
	{
		FTextureRHIRef Texture;
		EMaterialLabMaskBlendMode BlendMode = EMaterialLabMaskBlendMode::Replace;
		float Weight = 1.0f;
		float Tiling = 1.0f;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		bool bInvert = false;
	};

	struct FEffectRenderData
	{
		EMaterialLabEffectType Type = EMaterialLabEffectType::Peeling;
		FTextureRHIRef PeelData;
		FTextureRHIRef Mask;
		FTextureRHIRef Height;
		FTextureRHIRef SDF;
		float Tiling = 1.0f;
		float Strength = 1.0f;
		float Front = 0.08f;
		float Width = 0.015f;
		float MacroWarp = 0.01f;
		float MicroWarp = 0.003f;
		float MicroMorph = 1.0f;
		float Thickness = 0.04f;
		float Lift = 0.04f;
		float DetailStrength = 0.02f;
		float DistanceRange = 1.0f;
		float SDFRange = 0.1f;
		float HeightRange = 0.1f;
		FLinearColor StainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);
		float StainRoughness = 0.2f;
		float StainHeightInfluence = 0.5f;
		float StainHeightWarp = 0.0f;
		float StainHeightBias = -1.0f;
		float StainHeightContrast = 1.0f;
	};

	struct FChildRenderData
	{
		EMaterialLabLayerChildType Type = EMaterialLabLayerChildType::Mask;
		int32 SourceChildIndex = INDEX_NONE;
		FMaskRenderData Mask;
		FEffectRenderData Effect;
	};

	struct FLayerRenderData
	{
		FTextureRHIRef BaseColor;
		FTextureRHIRef Normal;
		FTextureRHIRef RAM;
		FTextureRHIRef Mask;
		TArray<FChildRenderData> Children;
		FVector4f FillColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		float Opacity = 1.0f;
		float Tiling = 1.0f;
		float NormalIntensity = 1.0f;
		float HueShift = 0.0f;
		float Saturation = 1.0f;
		float Value = 1.0f;
		float RoughnessBias = 0.5f;
		float RoughnessContrast = 1.0f;
		float RoughnessOffset = 0.0f;
		float FillRoughness = 0.5f;
		float FillMetallic = 0.0f;
		float LayerF0 = 0.04f;
		float BaseColorInfluence = 1.0f;
		float RoughnessInfluence = 1.0f;
		float AOInfluence = 1.0f;
		float MetallicInfluence = 1.0f;
		float F0Influence = 1.0f;
		float NormalInfluence = 1.0f;
		float HeightInfluence = 1.0f;
		float HeightBlendAmount = 1.0f;
		float HeightThreshold = 0.5f;
		float HeightRange = 0.1f;
		float HeightContrast = 1.0f;
		float HeightOffset = 0.0f;
		float HeightBias = 0.0f;
		float ConstantHeight = 0.5f;
		float MaskHeightInfluence = 0.0f;
		float HeightContactAOAmount = 0.0f;
		float HeightContactAOWidth = 0.05f;
		float HeightBorderLift = 0.0f;
		float HeightBorderWidth = 0.05f;
		float HeightBorderNormalStrength = 1.0f;
		float FeatureInfluence = 0.0f;
		float FeatureBias = 0.0f;
		float HeightFeatureInfluence = 0.0f;
		float AOFeatureInfluence = 0.0f;
		float CurvatureStrength = 1.0f;
		float CurvaturePower = 1.0f;
		int32 CurvatureRadius = 1;
		bool bEnabled = true;
		bool bHasMask = false;
		bool bHasEffects = false;
		bool bHasStain = false;
		bool bOverrideBaseColor = false;
		bool bOverrideRoughness = false;
		bool bOverrideMetallic = false;
		bool bCoat = false;
		bool bFill = false;
		bool bHasSurface = false;
		bool bHasNormal = false;
		bool bNormalOnly = false;
		bool bOverrideNormal = false;
		bool bFlipNormalY = false;
		bool bHeightBlendEnabled = false;
		bool bHasPackedHeight = false;
		bool bInvertHeight = false;
		bool bDirectHeightComparison = false;
		bool bInvertHeightFeature = false;
		bool bInvertAOFeature = false;
		bool bInvertFeature = false;
		uint32 HeightSource = 2u;
		int32 HeightReferenceLayerIndex = INDEX_NONE;
	};

	struct FRenderRequest
	{
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TArray<FLayerRenderData> Layers;
		FTextureRHIRef OutputBC[2];
		FTextureRHIRef OutputN[2];
		FTextureRHIRef OutputRAM[2];
		FTextureRHIRef OutputHeight[2];
		FTextureRHIRef OutputDebug[2];
		FMaterialLabDebugPreviewSettings DebugSettings;
		FSimpleDelegate OnComplete;
		int32 PublishedTargetIndex = 0;
	};

	FTextureRHIRef GetTextureRHI(UTexture2D* Texture)
	{
		return Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
	}

	UTextureRenderTarget2D* CreateTarget(
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

	FTextureRHIRef GetTargetRHI(UTextureRenderTarget2D* Target)
	{
		return Target && Target->GameThread_GetRenderTargetResource()
			? Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture()
			: FTextureRHIRef();
	}

	FRDGTextureRef RegisterTexture(
		FRDGBuilder& GraphBuilder,
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures,
		const FTextureRHIRef& Texture,
		const TCHAR* Name)
	{
		FRHITexture* TextureRHI = Texture.GetReference();
		check(TextureRHI);
		if (const FRDGTextureRef* ExistingTexture = RegisteredTextures.Find(TextureRHI))
		{
			return *ExistingTexture;
		}

		FRDGTextureRef RegisteredTexture =
			GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, Name));
		RegisteredTextures.Add(TextureRHI, RegisteredTexture);
		return RegisteredTexture;
	}
}

bool FMaterialLabGpuCompositor::Initialize(const FIntPoint InResolution)
{
	using namespace MaterialLabGpuCompositor;
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
		Set.Debug.Reset(CreateTarget(Resolution, FLinearColor(0.22f, 0.055f, 0.065f, 1.0f)));
	}
	FlushRenderingCommands();
	PublishedTargetIndex = 0;
	bInitialized = true;
	return true;
}

bool FMaterialLabGpuCompositor::RequestCompose(
	const TArray<FMaterialLabLayer>& Layers,
	FSimpleDelegate OnComplete,
	FMaterialLabDebugPreviewSettings DebugSettings)
{
	using namespace MaterialLabGpuCompositor;
	check(IsInGameThread());
	if (!bInitialized && !Initialize())
	{
		return false;
	}

	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture2D* NormalTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
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
		const FMaterialLabLayer& Layer = Layers[LayerIndex];
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		const UMaterialLabSurface* Surface = Layer.SourceSurface.LoadSynchronous();
		const bool bNormalOnly = Layer.ChannelMode == EMaterialLabLayerChannelMode::NormalDetail;
		UTexture2D* LayerBaseColor = Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture;
		UTexture2D* LayerNormal = Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture;
		if (bNormalOnly && Layer.NormalSourceType == EMaterialLabNormalSourceType::Texture)
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
		Data.FillColor = FVector4f(
			Layer.BaseColor.R,
			Layer.BaseColor.G,
			Layer.BaseColor.B,
			Layer.BaseColor.A);
		for (int32 SourceChildIndex = 0; SourceChildIndex < Layer.Children.Num(); ++SourceChildIndex)
		{
			const FMaterialLabLayerChild& LayerChild = Layer.Children[SourceChildIndex];
			if (LayerChild.Type == EMaterialLabLayerChildType::Mask)
			{
				const FMaterialLabMaskLayer& MaskLayer = LayerChild.Mask;
				if (!MaskLayer.bEnabled)
				{
					continue;
				}

				UTexture2D* MaskTexture = MaskLayer.MaskTexture.LoadSynchronous();
				if (!MaskTexture)
				{
					if (const UMaterialLabMask* MaskAsset = MaskLayer.Mask.LoadSynchronous())
					{
						MaskTexture = MaskAsset->MaskTexture.Get();
					}
				}
				if (!MaskTexture)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMaterialLabLayerChildType::Mask;
				ChildData.SourceChildIndex = SourceChildIndex;
				FMaskRenderData& MaskData = ChildData.Mask;
				MaskData.Texture = GetTextureRHI(MaskTexture);
				if (!MaskData.Texture.IsValid())
				{
					return false;
				}
				MaskData.BlendMode = MaskLayer.BlendMode;
				MaskData.Weight = FMath::Clamp(MaskLayer.Weight, 0.0f, 1.0f);
				MaskData.Tiling = FMath::Max(1.0f, static_cast<float>(MaskLayer.Tiling));
				MaskData.Balance = FMath::Clamp(MaskLayer.Balance, 0.0f, 2.0f);
				MaskData.Contrast = FMath::Clamp(MaskLayer.Contrast, 0.0f, 10.0f);
				MaskData.Offset = FMath::Clamp(MaskLayer.Offset, -1.0f, 1.0f);
				MaskData.bInvert = MaskLayer.bInvert;
				Data.bHasMask = true;
				continue;
			}

			const FMaterialLabLayerEffect& LayerEffect = LayerChild.Effect;
			if (!LayerEffect.bEnabled)
			{
				continue;
			}

			const UMaterialLabEffect* EffectAsset = LayerEffect.Effect.LoadSynchronous();
			if (!EffectAsset)
			{
				continue;
			}
			if (EffectAsset->EffectType == EMaterialLabEffectType::Peeling
				&& (!EffectAsset->PeelData
					|| !EffectAsset->Mask
					|| !EffectAsset->Height
					|| !EffectAsset->SDF))
			{
				continue;
			}

			FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
			ChildData.Type = EMaterialLabLayerChildType::Effect;
			ChildData.SourceChildIndex = SourceChildIndex;
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = EffectAsset->EffectType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			EffectData.Strength = FMath::Clamp(LayerEffect.Strength, 0.0f, 1.0f);
			if (EffectAsset->EffectType == EMaterialLabEffectType::Stain)
			{
				EffectData.StainColor = LayerEffect.StainColor;
				EffectData.StainRoughness = FMath::Clamp(LayerEffect.StainRoughness, -1.0f, 1.0f);
				EffectData.StainHeightInfluence = FMath::Clamp(LayerEffect.StainHeightInfluence, 0.0f, 1.0f);
				EffectData.StainHeightWarp = FMath::Clamp(LayerEffect.StainHeightWarp, 0.0f, 1.0f);
				EffectData.StainHeightBias = FMath::Clamp(LayerEffect.StainHeightBias, -1.0f, 1.0f);
				EffectData.StainHeightContrast = FMath::Max(LayerEffect.StainHeightContrast, 0.01f);
				Data.bHasStain = true;
				continue;
			}

			EffectData.PeelData = GetTextureRHI(EffectAsset->PeelData.Get());
			EffectData.Mask = GetTextureRHI(EffectAsset->Mask.Get());
			EffectData.Height = GetTextureRHI(EffectAsset->Height.Get());
			EffectData.SDF = GetTextureRHI(EffectAsset->SDF.Get());
			if (!EffectData.PeelData.IsValid()
				|| !EffectData.Mask.IsValid()
				|| !EffectData.Height.IsValid()
				|| !EffectData.SDF.IsValid())
			{
				return false;
			}
			EffectData.Front = LayerEffect.Front;
			EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
			EffectData.MacroWarp = LayerEffect.MacroWarp;
			EffectData.MicroWarp = LayerEffect.MicroWarp;
			EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
			EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
			EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
			EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
			EffectData.DistanceRange = FMath::Max(EffectAsset->DistanceRange, 1.0e-6f);
			EffectData.SDFRange = FMath::Max(EffectAsset->SDFRange, 1.0e-6f);
			EffectData.HeightRange = FMath::Max(EffectAsset->HeightRange, 1.0e-6f);
			Data.bHasEffects = true;
		}

		Data.Opacity = Layer.Opacity;
		Data.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
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
			Layer.bOverrideIOR ? Layer.IOR : SourceIOR);
		Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
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
		Data.FeatureInfluence = FMath::Clamp(Layer.FeatureInfluence, 0.0f, 1.0f);
		Data.FeatureBias = FMath::Clamp(Layer.FeatureBias, 0.0f, 1.0f);
		Data.HeightFeatureInfluence = FMath::Clamp(Layer.HeightFeatureInfluence, 0.0f, 1.0f);
		Data.AOFeatureInfluence = FMath::Clamp(Layer.AOFeatureInfluence, 0.0f, 1.0f);
		Data.CurvatureRadius = Layer.CurvatureRadius;
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
		case EMaterialLabHeightSource::Automatic:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : (Data.bHasMask ? 1u : 2u);
			break;
		case EMaterialLabHeightSource::CombinedMask:
			Data.HeightSource = Data.bHasMask ? 1u : 2u;
			break;
		case EMaterialLabHeightSource::Constant:
			Data.HeightSource = 2u;
			break;
		case EMaterialLabHeightSource::RAMHAlpha:
		case EMaterialLabHeightSource::LayerHeight:
		default:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : 2u;
			break;
		}
		Data.bOverrideBaseColor = Layer.bOverrideBaseColor;
		Data.bOverrideRoughness = Layer.bOverrideRoughness;
		Data.bOverrideMetallic = Layer.bOverrideMetallic;
		Data.bCoat = Layer.CompositionMode == EMaterialLabCompositionMode::Coat;
		Data.bFill = Layer.Type == EMaterialLabLayerType::Fill;
		Data.bHasSurface = Surface
			&& Surface->BaseColor
			&& Surface->Normal
			&& Surface->RoughnessAOMetallic;
		Data.bHasNormal = LayerNormal != nullptr;
		Data.bNormalOnly = bNormalOnly;
		Data.bOverrideNormal = Layer.NormalBlendMode == EMaterialLabNormalBlendMode::Override;
		Data.bFlipNormalY = Layer.bFlipNormalY;
	}

	PublishedTargetIndex = Request.Layers.IsEmpty()
		? 0
		: (Request.Layers.Num() - 1) & 1;
	Request.PublishedTargetIndex = PublishedTargetIndex;

	ENQUEUE_RENDER_COMMAND(MaterialLabComposite)(
		[Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			TMap<FRHITexture*, FRDGTextureRef> RegisteredTextures;
			FRDGTextureRef OutputBC[2];
			FRDGTextureRef OutputN[2];
			FRDGTextureRef OutputRAM[2];
			FRDGTextureRef OutputHeight[2];
			FRDGTextureRef OutputDebug[2];
			for (int32 Index = 0; Index < 2; ++Index)
			{
				OutputBC[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputBC[Index],
					TEXT("MaterialLab.OutputBC"));
				OutputN[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputN[Index],
					TEXT("MaterialLab.OutputN"));
				OutputRAM[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputRAM[Index],
					TEXT("MaterialLab.OutputRAM"));
				OutputHeight[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputHeight[Index],
					TEXT("MaterialLab.OutputHeight"));
				OutputDebug[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputDebug[Index],
					TEXT("MaterialLab.OutputDebug"));
			}

			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]),
				FVector4f(0.22f, 0.055f, 0.065f, 1.0f));

			if (Request.Layers.IsEmpty())
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[0]), FVector4f(0.0f, 0.0f, 0.0f, 1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[0]), FVector4f(0.5f, 0.5f, 1.0f, 1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[0]), FVector4f(0.5f, 1.0f, 0.0f, 0.04f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputHeight[0]), FVector4f(0.5f, 0.0f, 0.0f, 0.0f));
			}
			else
			{
				const FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef MaskTargets[2] =
				{
					GraphBuilder.CreateTexture(MaskDesc, TEXT("MaterialLab.MaskA")),
					GraphBuilder.CreateTexture(MaskDesc, TEXT("MaterialLab.MaskB"))
				};
				FRDGTextureRef* HeightTargets = OutputHeight;
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[0]),
					FVector4f(0.5f, 0.0f, 0.0f, 0.0f));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[1]),
					FVector4f(0.5f, 0.0f, 0.0f, 0.0f));

				const FRDGTextureDesc EffectDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef EffectTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("MaterialLab.EffectA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("MaterialLab.EffectB"))
				};
				FRDGTextureRef StainTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("MaterialLab.StainA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("MaterialLab.StainB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[0]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[1]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				TShaderMapRef<FMaterialLabMaskCS> MaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMaterialLabPeelingCS> PeelingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMaterialLabStainCS> StainShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMaterialLabCompositeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TSet<int32> RequiredHeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const int32 ReferenceIndex = Request.Layers[LayerIndex].HeightReferenceLayerIndex;
					if (ReferenceIndex >= 0 && ReferenceIndex < LayerIndex)
					{
						RequiredHeightSnapshots.Add(ReferenceIndex);
					}
				}
				TMap<int32, FRDGTextureRef> HeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const FLayerRenderData& Layer = Request.Layers[LayerIndex];
					FRDGTextureRef CombinedMask = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Mask,
						TEXT("MaterialLab.WhiteMask"));
					FRDGTextureRef CombinedEffectData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("MaterialLab.DefaultEffectData"));
					FRDGTextureRef CombinedStainData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("MaterialLab.DefaultStainData"));
					FRDGTextureRef DebugMask = CombinedMask;
					int32 MaskPassIndex = 0;
					int32 EffectPassIndex = 0;
					int32 StainPassIndex = 0;
					for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
					{
						const FChildRenderData& Child = Layer.Children[ChildIndex];
						if (Child.Type == EMaterialLabLayerChildType::Mask)
						{
							const FMaskRenderData& Mask = Child.Mask;
							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							FMaterialLabMaskCS::FParameters* MaskParameters =
								GraphBuilder.AllocParameters<FMaterialLabMaskCS::FParameters>();
							MaskParameters->OutputSize = Request.Resolution;
							MaskParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							MaskParameters->BlendMode = static_cast<uint32>(Mask.BlendMode);
							MaskParameters->Invert = Mask.bInvert ? 1u : 0u;
							MaskParameters->Weight = Mask.Weight;
							MaskParameters->Tiling = Mask.Tiling;
							MaskParameters->Balance = Mask.Balance;
							MaskParameters->Contrast = Mask.Contrast;
							MaskParameters->Offset = Mask.Offset;
							MaskParameters->PreviousMask = MaskTargets[MaskReadIndex];
							MaskParameters->IncomingMask = RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								Mask.Texture,
								TEXT("MaterialLab.IncomingMask"));
							MaskParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							MaskParameters->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("MaterialLab.Mask.Layer%d.Child%d", LayerIndex, ChildIndex),
								MaskShader,
								MaskParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMaterialLabDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugMaskSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("MaterialLab.DebugMaskSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugMaskSnapshot);
								DebugMask = DebugMaskSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						const FEffectRenderData& Effect = Child.Effect;
						if (Effect.Type == EMaterialLabEffectType::Stain)
						{
							const int32 StainWriteIndex = StainPassIndex & 1;
							const int32 StainReadIndex = 1 - StainWriteIndex;
							FMaterialLabStainCS::FParameters* StainParameters =
								GraphBuilder.AllocParameters<FMaterialLabStainCS::FParameters>();
							StainParameters->OutputSize = Request.Resolution;
							StainParameters->Initialize = StainPassIndex == 0 ? 1u : 0u;
							StainParameters->Strength = Effect.Strength;
							StainParameters->StainColor = FVector4f(
															Effect.StainColor.R,
															Effect.StainColor.G,
															Effect.StainColor.B,
															Effect.StainColor.A);
							StainParameters->RoughnessInfluence = Effect.StainRoughness;
							StainParameters->HeightInfluence = Effect.StainHeightInfluence;
							StainParameters->HeightWarp = Effect.StainHeightWarp;
							StainParameters->HeightBias = Effect.StainHeightBias;
							StainParameters->HeightContrast = Effect.StainHeightContrast;
							StainParameters->PreviousStainData = StainTargets[StainReadIndex];
							StainParameters->ChildMask = CombinedMask;
							StainParameters->AccumulatedHeight = HeightTargets[1 - (LayerIndex & 1)];
							StainParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							StainParameters->OutputStainData = GraphBuilder.CreateUAV(StainTargets[StainWriteIndex]);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("MaterialLab.Stain.Layer%d.Child%d", LayerIndex, ChildIndex),
								StainShader,
								StainParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedStainData = StainTargets[StainWriteIndex];
							++StainPassIndex;
							continue;
						}

						const int32 EffectWriteIndex = EffectPassIndex & 1;
						const int32 EffectReadIndex = 1 - EffectWriteIndex;
						FMaterialLabPeelingCS::FParameters* EffectParameters =
							GraphBuilder.AllocParameters<FMaterialLabPeelingCS::FParameters>();
						EffectParameters->OutputSize = Request.Resolution;
						EffectParameters->Initialize = EffectPassIndex == 0 ? 1u : 0u;
						EffectParameters->Tiling = Effect.Tiling;
						EffectParameters->Strength = Effect.Strength;
						EffectParameters->Front = Effect.Front;
						EffectParameters->Width = Effect.Width;
						EffectParameters->MacroWarp = Effect.MacroWarp;
						EffectParameters->MicroWarp = Effect.MicroWarp;
						EffectParameters->MicroMorph = Effect.MicroMorph;
						EffectParameters->Thickness = Effect.Thickness;
						EffectParameters->Lift = Effect.Lift;
						EffectParameters->DetailStrength = Effect.DetailStrength;
						EffectParameters->DistanceRange = Effect.DistanceRange;
						EffectParameters->SDFRange = Effect.SDFRange;
						EffectParameters->HeightRange = Effect.HeightRange;
						EffectParameters->PreviousEffectData = EffectTargets[EffectReadIndex];
						EffectParameters->ChildMask = CombinedMask;
						EffectParameters->PeelData = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.PeelData, TEXT("MaterialLab.PeelData"));
						EffectParameters->PeelMask = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Mask, TEXT("MaterialLab.PeelMask"));
						EffectParameters->PeelHeight = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Height, TEXT("MaterialLab.PeelHeight"));
						EffectParameters->PeelSDF = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.SDF, TEXT("MaterialLab.PeelSDF"));
						EffectParameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
						EffectParameters->PointWrapSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						EffectParameters->OutputEffectData = GraphBuilder.CreateUAV(EffectTargets[EffectWriteIndex]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("MaterialLab.Peeling.Layer%d.Child%d", LayerIndex, ChildIndex),
							PeelingShader,
							EffectParameters,
							FIntVector(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1));
						CombinedEffectData = EffectTargets[EffectWriteIndex];
						++EffectPassIndex;
					}

					const int32 WriteIndex = LayerIndex & 1;
					const int32 ReadIndex = 1 - WriteIndex;
					FMaterialLabCompositeCS::FParameters* Parameters =
						GraphBuilder.AllocParameters<FMaterialLabCompositeCS::FParameters>();
					Parameters->OutputSize = Request.Resolution;
					Parameters->Initialize = LayerIndex == 0 ? 1u : 0u;
					Parameters->Enabled = Layer.bEnabled ? 1u : 0u;
					Parameters->HasMask = Layer.bHasMask ? 1u : 0u;
					Parameters->HasEffects = Layer.bHasEffects ? 1u : 0u;
					Parameters->HasStain = Layer.bHasStain ? 1u : 0u;
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
					Parameters->WriteDebug = Request.DebugSettings.Mode != EMaterialLabDebugPreviewMode::None
						&& Request.DebugSettings.LayerIndex == LayerIndex ? 1u : 0u;
					Parameters->Opacity = Layer.Opacity;
					Parameters->Tiling = Layer.Tiling;
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
					Parameters->FeatureInfluence = Layer.FeatureInfluence;
					Parameters->FeatureBias = Layer.FeatureBias;
					Parameters->HeightFeatureInfluence = Layer.HeightFeatureInfluence;
					Parameters->AOFeatureInfluence = Layer.AOFeatureInfluence;
					Parameters->CurvatureRadius = Layer.CurvatureRadius;
					Parameters->CurvatureStrength = Layer.CurvatureStrength;
					Parameters->CurvaturePower = Layer.CurvaturePower;
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
						TEXT("MaterialLab.LayerBC"));
					Parameters->LayerN = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Normal,
						TEXT("MaterialLab.LayerN"));
					Parameters->LayerRAM = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.RAM,
						TEXT("MaterialLab.LayerRAM"));
					Parameters->LayerMask = CombinedMask;
					Parameters->EffectData = CombinedEffectData;
					Parameters->StainData = CombinedStainData;
					Parameters->DebugMask = DebugMask;
					Parameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
					Parameters->OutputBC = GraphBuilder.CreateUAV(OutputBC[WriteIndex]);
					Parameters->OutputN = GraphBuilder.CreateUAV(OutputN[WriteIndex]);
					Parameters->OutputRAM = GraphBuilder.CreateUAV(OutputRAM[WriteIndex]);
					Parameters->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
					Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("MaterialLab.Composite.Layer%d", LayerIndex),
						Shader,
						Parameters,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));

					if (RequiredHeightSnapshots.Contains(LayerIndex))
					{
						FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc,
							TEXT("MaterialLab.HeightSnapshot"));
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

void FMaterialLabGpuCompositor::BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const
{
	MaterialInstance.SetTextureParameterValue(TEXT("ML_BaseColor"), GetBaseColorOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Normal"), GetNormalOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_RAM"), GetRAMOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Height"), GetHeightOutput());
	MaterialInstance.SetScalarParameterValue(TEXT("ML_Tiling"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessBias"), 0.5f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessContrast"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessOffset"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_NormalIntensity"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_DielectricF0"), 0.04f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_UsePackedF0"), 1.0f);
}

UTextureRenderTarget2D* FMaterialLabGpuCompositor::GetBaseColorOutput() const
{
	return Targets[PublishedTargetIndex].BaseColor.Get();
}

UTextureRenderTarget2D* FMaterialLabGpuCompositor::GetNormalOutput() const
{
	return Targets[PublishedTargetIndex].Normal.Get();
}

UTextureRenderTarget2D* FMaterialLabGpuCompositor::GetRAMOutput() const
{
	return Targets[PublishedTargetIndex].RAM.Get();
}

UTextureRenderTarget2D* FMaterialLabGpuCompositor::GetHeightOutput() const
{
	return Targets[PublishedTargetIndex].Height.Get();
}

UTextureRenderTarget2D* FMaterialLabGpuCompositor::GetDebugOutput() const
{
	return Targets[PublishedTargetIndex].Debug.Get();
}
