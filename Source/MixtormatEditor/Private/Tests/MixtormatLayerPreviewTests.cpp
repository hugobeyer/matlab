#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatEffect.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMaterial.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatGpuCompositorTest,
	"Mixtormat.Editor.GpuCompositor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGpuCompositorTest::RunTest(const FString& Parameters)
{
	FMixtormatGpuCompositor Compositor;
	TestTrue(TEXT("Compositor render targets initialize"), Compositor.Initialize(FIntPoint(32, 32)));

	const auto ReadFirstPixel = [this](
		UTextureRenderTarget2D* Target,
		const TCHAR* Label,
		FColor& OutPixel)
	{
		TestNotNull(Label, Target);
		if (!Target || !Target->GameThread_GetRenderTargetResource())
		{
			return false;
		}

		TArray<FColor> Pixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		const bool bRead = Target->GameThread_GetRenderTargetResource()->ReadPixels(
			Pixels,
			ReadFlags);
		TestTrue(Label, bRead && !Pixels.IsEmpty());
		if (!bRead || Pixels.IsEmpty())
		{
			return false;
		}
		OutPixel = Pixels[0];
		return true;
	};

	FMixtormatLayer LegacyLayer;
	LegacyLayer.Masks.AddDefaulted();
	LegacyLayer.Effects.AddDefaulted();
	LegacyLayer.MigrateLegacyChildren();
	TestEqual(TEXT("Legacy child arrays migrate once"), LegacyLayer.Children.Num(), 2);
	TestTrue(
		TEXT("Legacy masks migrate before effects"),
		LegacyLayer.Children[0].Type == EMixtormatLayerChildType::Mask
			&& LegacyLayer.Children[1].Type == EMixtormatLayerChildType::Effect);
	TestTrue(
		TEXT("Legacy arrays are cleared after migration"),
		LegacyLayer.Masks.IsEmpty() && LegacyLayer.Effects.IsEmpty());

	FMixtormatLayer BaseLayer;
	BaseLayer.Type = EMixtormatLayerType::Fill;
	BaseLayer.DisplayName = FText::FromString(TEXT("Red Fill"));
	BaseLayer.bOverrideBaseColor = true;
	BaseLayer.bOverrideRoughness = true;
	BaseLayer.bOverrideMetallic = true;
	BaseLayer.BaseColor = FLinearColor::Red;
	BaseLayer.Roughness = 0.25f;
	BaseLayer.Metallic = 1.0f;
	TestFalse(TEXT("Legacy layers keep height blending disabled"), BaseLayer.bHeightBlendEnabled);
	TestEqual(TEXT("Contact AO is opt-in"), BaseLayer.HeightContactAOAmount, 0.0f);
	TestEqual(TEXT("Border Lift is opt-in"), BaseLayer.HeightBorderLift, 0.0f);
	TestEqual(TEXT("Height mask strength defaults full"), BaseLayer.HeightBlendAmount, 1.0f);
	TestTrue(
		TEXT("New layers default to Layer Height"),
		BaseLayer.HeightSource == EMixtormatHeightSource::LayerHeight);
	TestEqual(TEXT("Height mask influence is opt-in"), BaseLayer.HeightFeatureInfluence, 0.0f);
	TestEqual(TEXT("AO mask influence is opt-in"), BaseLayer.AOFeatureInfluence, 0.0f);
	TestFalse(TEXT("Height mask inversion defaults off"), BaseLayer.bInvertHeightFeature);
	TestFalse(TEXT("AO mask inversion defaults off"), BaseLayer.bInvertAOFeature);
	TestFalse(TEXT("Generated feature inversion defaults off"), BaseLayer.bInvertFeature);
	TestEqual(TEXT("Hue Shift defaults neutral"), BaseLayer.HueShift, 0.0f);
	TestEqual(TEXT("Saturation defaults neutral"), BaseLayer.Saturation, 1.0f);
	TestEqual(TEXT("Value defaults neutral"), BaseLayer.Value, 1.0f);
	TestEqual(TEXT("Base Color influence defaults full"), BaseLayer.BaseColorInfluence, 1.0f);
	TestEqual(TEXT("Roughness influence defaults full"), BaseLayer.RoughnessInfluence, 1.0f);
	TestEqual(TEXT("AO influence defaults full"), BaseLayer.AOInfluence, 1.0f);
	TestEqual(TEXT("Metallic influence defaults full"), BaseLayer.MetallicInfluence, 1.0f);
	TestEqual(TEXT("F0 influence defaults full"), BaseLayer.F0Influence, 1.0f);
	TestEqual(TEXT("Normal influence defaults full"), BaseLayer.NormalInfluence, 1.0f);
	TestEqual(TEXT("Height influence defaults full"), BaseLayer.HeightInfluence, 1.0f);

	TArray<FMixtormatLayer> Layers = { BaseLayer };
	TestTrue(TEXT("Compositor accepts a base layer"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();

	FColor Pixel;
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Base Color output can be read"), Pixel))
	{
		TestTrue(TEXT("Fill Base Color reaches the GPU output"), Pixel.R > Pixel.G);
	}
	if (ReadFirstPixel(Compositor.GetNormalOutput(), TEXT("Normal output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Fill emits a neutral tangent-space normal"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2
				&& FMath::Abs(static_cast<int32>(Pixel.G) - 128) <= 2
				&& Pixel.B >= 253);
	}
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Metallic RAM output can be read"), Pixel))
	{
		TestTrue(TEXT("Metallic is written to RAM blue"), Pixel.B >= 253);
		TestTrue(
			TEXT("Default dielectric F0 is written to RAM alpha"),
			FMath::Abs(static_cast<int32>(Pixel.A) - 10) <= 2);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Height output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Default constant height is published"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2);
	}

	Layers[0].HueShift = 120.0f;
	TestTrue(TEXT("Compositor accepts per-layer HSV adjustment"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("HSV output can be read"), Pixel))
	{
		TestTrue(TEXT("A 120 degree hue shift turns red toward green"), Pixel.G > Pixel.R && Pixel.G > Pixel.B);
	}
	Layers[0].HueShift = 0.0f;

	FMixtormatLayer TopLayer = BaseLayer;
	TopLayer.DisplayName = FText::FromString(TEXT("Blue Fill"));
	TopLayer.BaseColor = FLinearColor::Blue;
	Layers.Add(TopLayer);
	TestTrue(TEXT("Compositor accepts multiple ordered layers"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Multiple-layer output can be read"), Pixel))
	{
		TestTrue(TEXT("The later layer is composited on top"), Pixel.B > Pixel.R);
	}
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Upper metallic output can be read"), Pixel))
	{
		TestTrue(TEXT("A metallic replacement layer remains metallic"), Pixel.B >= 253);
	}

	Layers[1].BaseColorInfluence = 0.0f;
	Layers[1].RoughnessInfluence = 1.0f;
	Layers[1].AOInfluence = 0.0f;
	Layers[1].MetallicInfluence = 1.0f;
	Layers[1].F0Influence = 0.0f;
	Layers[1].NormalInfluence = 0.0f;
	Layers[1].HeightInfluence = 0.0f;
	Layers[1].Roughness = 0.75f;
	Layers[1].Metallic = 0.0f;
	Layers[1].bOverrideIOR = true;
	Layers[1].IOR = 2.0f;
	Layers[1].ConstantHeight = 0.9f;
	TestTrue(TEXT("Compositor accepts a Roughness/Metallic-only Fill"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Channel-isolated Base Color can be read"), Pixel))
	{
		TestTrue(TEXT("Zero Base Color influence preserves the lower color"), Pixel.R > Pixel.B);
	}
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Channel-isolated RAM can be read"), Pixel))
	{
		TestTrue(
			TEXT("Roughness/Metallic-only Fill changes RAM R/B and preserves AO/F0"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 191) <= 2
				&& Pixel.G >= 253
				&& Pixel.B <= 2
				&& FMath::Abs(static_cast<int32>(Pixel.A) - 10) <= 2);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Channel-isolated Height can be read"), Pixel))
	{
		TestTrue(
			TEXT("Zero Height influence preserves the lower height"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2);
	}

	UTexture2D* DetailNormalTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	TestNotNull(TEXT("Engine normal-influence test texture exists"), DetailNormalTexture);
	if (DetailNormalTexture)
	{
		Layers[1].Type = EMixtormatLayerType::Material;
		Layers[1].ChannelMode = EMixtormatLayerChannelMode::NormalDetail;
		Layers[1].NormalSourceType = EMixtormatNormalSourceType::Texture;
		Layers[1].NormalTexture = DetailNormalTexture;
		Layers[1].NormalInfluence = 1.0f;
		TestTrue(TEXT("Compositor accepts a full-influence normal detail"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetNormalOutput(), TEXT("Full-influence Normal can be read"), Pixel))
		{
			TestTrue(TEXT("The test normal differs from the neutral lower normal"), Pixel.R > 160 && Pixel.G > 160);
		}

		Layers[1].NormalInfluence = 0.0f;
		TestTrue(TEXT("Compositor accepts zero normal influence"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetNormalOutput(), TEXT("Zero-influence Normal can be read"), Pixel))
		{
			TestTrue(
				TEXT("Zero Normal influence preserves the neutral lower normal"),
				FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2
					&& FMath::Abs(static_cast<int32>(Pixel.G) - 128) <= 2
					&& Pixel.B >= 253);
		}
	}

	Layers[1] = TopLayer;

	UTexture2D* PeelTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UMixtormatEffect* PeelingAsset = NewObject<UMixtormatEffect>(GetTransientPackage());
	TestNotNull(TEXT("Engine test peeling texture exists"), PeelTexture);
	TestNotNull(TEXT("Transient peeling asset exists"), PeelingAsset);
	if (PeelTexture && PeelingAsset)
	{
		PeelingAsset->PeelData = PeelTexture;
		PeelingAsset->Mask = PeelTexture;
		PeelingAsset->Height = PeelTexture;
		PeelingAsset->SDF = PeelTexture;
		FMixtormatLayerChild& PeelingChild = Layers[1].Children.AddDefaulted_GetRef();
		PeelingChild.Type = EMixtormatLayerChildType::Effect;
		FMixtormatLayerEffect& Peeling = PeelingChild.Effect;
		Peeling.Effect = PeelingAsset;
		Peeling.Front = 2.0f;
		Peeling.Strength = 1.0f;
		TestTrue(TEXT("Compositor accepts a child peeling effect"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Peeling output can be read"), Pixel))
		{
			TestTrue(TEXT("Full peeling reveals the accumulated lower layer"), Pixel.R > Pixel.B);
		}
		Layers[1].Children.Reset();
	}

	Layers[1].Metallic = 0.0f;
	Layers[1].CompositionMode = EMixtormatCompositionMode::Coat;
	TestTrue(TEXT("Compositor accepts a dielectric coat"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Coat metallic output can be read"), Pixel))
	{
		TestTrue(TEXT("A coat preserves the substrate metallic value"), Pixel.B >= 253);
	}
	Layers[1].Metallic = 1.0f;
	Layers[1].CompositionMode = EMixtormatCompositionMode::Replace;
	Layers[1].bOverrideIOR = true;
	Layers[1].IOR = 2.0f;
	TestTrue(TEXT("Compositor accepts a layer IOR override"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Layer F0 output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Layer IOR is converted to F0 in RAM alpha"),
			FMath::Abs(static_cast<int32>(Pixel.A) - 28) <= 2);
	}

	Layers[1].bEnabled = false;
	TestTrue(TEXT("Compositor accepts a disabled layer"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Disabled-layer output can be read"), Pixel))
	{
		TestTrue(TEXT("A disabled layer preserves the previous result"), Pixel.R > Pixel.B);
	}

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	TestNotNull(TEXT("Engine test mask exists"), WhiteMask);
	if (WhiteMask)
	{
		Layers[1].bEnabled = true;
		FMixtormatLayerChild& MaskChild = Layers[1].Children.AddDefaulted_GetRef();
		MaskChild.Type = EMixtormatLayerChildType::Mask;
		FMixtormatMaskLayer& Mask = MaskChild.Mask;
		TestEqual(TEXT("Mask offset defaults neutral"), Mask.Offset, 0.0f);
		Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
		Mask.bInvert = true;
		TestTrue(TEXT("Compositor accepts a texture mask"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Masked output can be read"), Pixel))
		{
			TestTrue(TEXT("An inverted white mask rejects the upper layer"), Pixel.R > Pixel.B);
		}
		Mask.bInvert = false;
		Mask.Balance = 1.0f;
		TestTrue(TEXT("Compositor accepts full dark mask balance"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Dark-balanced mask output can be read"), Pixel))
		{
			TestTrue(TEXT("Maximum mask balance rejects the upper layer"), Pixel.R > Pixel.B);
		}

		Mask.Balance = 0.0f;
		TestTrue(TEXT("Compositor accepts full light mask balance"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Light-balanced mask output can be read"), Pixel))
		{
			TestTrue(TEXT("Minimum mask balance restores the upper layer"), Pixel.B > Pixel.R);
		}

		Mask.Balance = 0.5f;
		Mask.Offset = -1.0f;
		TestTrue(TEXT("Compositor accepts mask offset"), Compositor.RequestCompose(Layers));
		FlushRenderingCommands();
		if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Offset mask output can be read"), Pixel))
		{
			TestTrue(TEXT("Negative mask offset rejects the upper layer"), Pixel.R > Pixel.B);
		}
		Mask.Offset = 0.0f;

		Layers[1].Children.Reset();

		if (PeelingAsset && PeelingAsset->PeelData)
		{
			FMixtormatLayerChild& EffectFirst = Layers[1].Children.AddDefaulted_GetRef();
			EffectFirst.Type = EMixtormatLayerChildType::Effect;
			EffectFirst.Effect.Effect = PeelingAsset;
			EffectFirst.Effect.Front = 2.0f;
			FMixtormatLayerChild& HalfMask = Layers[1].Children.AddDefaulted_GetRef();
			HalfMask.Type = EMixtormatLayerChildType::Mask;
			HalfMask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
			HalfMask.Mask.Weight = 0.5f;

			uint8 EffectThenMaskBlue = 0;
			TestTrue(TEXT("Compositor accepts Effect then Mask ordering"), Compositor.RequestCompose(Layers));
			FlushRenderingCommands();
			if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Effect-first output can be read"), Pixel))
			{
				EffectThenMaskBlue = Pixel.B;
			}

			Layers[1].Children.Swap(0, 1);
			TestTrue(TEXT("Compositor accepts Mask then Effect ordering"), Compositor.RequestCompose(Layers));
			FlushRenderingCommands();
			if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Mask-first output can be read"), Pixel))
			{
				TestTrue(
					TEXT("A preceding Mask changes Effect evaluation"),
					Pixel.B > EffectThenMaskBlue);
			}
			Layers[1].Children.Reset();
		}
	}

	Layers[0].ConstantHeight = 0.8f;
	Layers[1].bHeightBlendEnabled = true;
	Layers[1].HeightSource = EMixtormatHeightSource::Constant;
	Layers[1].ConstantHeight = 0.2f;
	Layers[1].HeightRange = 0.01f;
	Layers[1].HeightBias = 0.0f;
	Layers[1].HeightOffset = 0.0f;
	TestTrue(TEXT("Compositor accepts constant height blending"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Height-rejected output can be read"), Pixel))
	{
		TestTrue(TEXT("Lower height rejects the upper layer"), Pixel.R > Pixel.B);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Rejected height output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Rejected upper height preserves the lower height"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 204) <= 2);
	}

	Layers[1].bInvertHeight = true;
	TestTrue(TEXT("Compositor accepts inverted base height"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Inverted-base output can be read"), Pixel))
	{
		TestTrue(TEXT("Inverting the lower height restores the upper layer"), Pixel.B > Pixel.R);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Inverted-base height can be read"), Pixel))
	{
		TestTrue(
			TEXT("Accumulated height uses the inverted lower height"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 51) <= 2);
	}
	Layers[1].bInvertHeight = false;

	Layers[1].HeightOffset = 1.0f;
	TestTrue(TEXT("Compositor accepts blend height bias"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Height-biased output can be read"), Pixel))
	{
		TestTrue(TEXT("Positive blend height bias restores the upper layer"), Pixel.B > Pixel.R);
	}
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Height-blended RAM output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Height blending keeps dielectric F0 in RAM alpha"),
			FMath::Abs(static_cast<int32>(Pixel.A) - 28) <= 2);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Height-biased output height can be read"), Pixel))
	{
		TestTrue(
			TEXT("Accepted biased height is clamped in the height output"),
			Pixel.R >= 253);
	}
	Layers[1].bHeightBlendEnabled = false;
	Layers[1].HeightBias = 0.0f;
	Layers[1].HeightOffset = 0.0f;

	Layers[0].ConstantHeight = 0.6f;
	Layers[1].bHeightBlendEnabled = true;
	Layers[1].HeightSource = EMixtormatHeightSource::LayerHeight;
	Layers[1].ConstantHeight = 0.4f;
	if (PeelTexture)
	{
		FMixtormatLayerChild& HeightMask = Layers[1].Children.AddDefaulted_GetRef();
		HeightMask.Type = EMixtormatLayerChildType::Mask;
		HeightMask.Mask.MaskTexture = PeelTexture;
		HeightMask.Mask.Weight = 0.8f;
	}
	Layers[1].HeightBlendAmount = 0.5f;
	Layers[1].HeightThreshold = 0.5f;
	Layers[1].HeightRange = 0.1f;
	Layers[1].HeightBias = 0.0f;
	Layers[1].HeightOffset = 0.0f;
	Layers[1].Metallic = 0.0f;
	TestTrue(TEXT("Compositor accepts RAMH-style height-mask blending"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Height-mask rejected output can be read"), Pixel))
	{
		TestTrue(TEXT("Weak mask strength rejects the upper layer"), Pixel.R > Pixel.B);
	}
	Layers[1].HeightOffset = 0.4f;
	TestTrue(TEXT("Compositor accepts blend height bias"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Blend-biased output can be read"), Pixel))
	{
		TestTrue(TEXT("Blend bias restores the upper layer"), Pixel.B > Pixel.R);
	}
	Layers[1].HeightOffset = 0.0f;
	Layers[1].HeightBlendAmount = 1.0f;
	Layers[1].HeightBias = 0.4f;
	TestTrue(TEXT("Compositor accepts base height bias"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Base-biased output can be read"), Pixel))
	{
		TestTrue(TEXT("Base bias protects the lower layer"), Pixel.R > Pixel.B);
	}
	Layers[1].HeightBias = 0.0f;
	TestTrue(TEXT("Compositor accepts full height-mask strength"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Height-mask accepted output can be read"), Pixel))
	{
		TestTrue(TEXT("Height blend mask drives Base Color"), Pixel.B > Pixel.R);
	}
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Height-masked RAM output can be read"), Pixel))
	{
		TestTrue(TEXT("Height blend mask drives Metallic"), Pixel.B <= 2);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Height-masked composite height can be read"), Pixel))
	{
		TestTrue(
			TEXT("Height blend mask carries the accepted blend height"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 102) <= 2);
	}
	Layers[1].bHeightBlendEnabled = false;
	Layers[1].HeightBlendAmount = 1.0f;
	Layers[1].HeightBias = 0.0f;
	Layers[1].HeightOffset = 0.0f;
	Layers[1].Metallic = 1.0f;
	Layers[1].HeightRange = 0.1f;
	Layers[1].Children.Reset();

	TArray<FMixtormatLayer> ReferenceLayers;
	FMixtormatLayer ReferenceBase = BaseLayer;
	ReferenceBase.ConstantHeight = 0.8f;
	ReferenceLayers.Add(ReferenceBase);
	FMixtormatLayer ReferenceMiddle = BaseLayer;
	ReferenceMiddle.BaseColor = FLinearColor::Green;
	ReferenceMiddle.ConstantHeight = 0.1f;
	ReferenceLayers.Add(ReferenceMiddle);
	FMixtormatLayer ReferenceTop = BaseLayer;
	ReferenceTop.BaseColor = FLinearColor::Blue;
	ReferenceTop.bHeightBlendEnabled = true;
	ReferenceTop.HeightSource = EMixtormatHeightSource::Constant;
	ReferenceTop.ConstantHeight = 0.5f;
	ReferenceTop.HeightRange = 0.01f;
	ReferenceLayers.Add(ReferenceTop);
	TestTrue(TEXT("Compositor accepts previous-composite height comparison"), Compositor.RequestCompose(ReferenceLayers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Previous-composite comparison can be read"), Pixel))
	{
		TestTrue(TEXT("Current height beats the immediate previous composite"), Pixel.B > Pixel.G);
	}
	ReferenceLayers[2].HeightReferenceLayerIndex = 0;
	TestTrue(TEXT("Compositor accepts serialized legacy height references"), Compositor.RequestCompose(ReferenceLayers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Accumulated-height comparison can be read"), Pixel))
	{
		TestTrue(TEXT("Current blending always uses the immediate accumulated height"), Pixel.B > Pixel.G);
	}

	Layers[1].FeatureInfluence = 1.0f;
	Layers[1].FeatureBias = 0.0f;
	Layers[1].CurvatureRadius = 2;
	Layers[1].CurvatureStrength = 1.0f;
	Layers[1].CurvaturePower = 1.0f;
	TestTrue(TEXT("Compositor accepts normal-derived feature masking"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Feature-mask output can be read"), Pixel))
	{
		TestTrue(TEXT("Flat underlying normals produce no cavity mask"), Pixel.R > Pixel.B);
	}
	Layers[1].bInvertFeature = true;
	TestTrue(TEXT("Compositor accepts inverted normal-derived feature masking"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Inverted feature-mask output can be read"), Pixel))
	{
		TestTrue(TEXT("Inverting a flat cavity mask restores the upper layer"), Pixel.B > Pixel.R);
	}
	FMixtormatDebugPreviewSettings DebugSettings;
	DebugSettings.Mode = EMixtormatDebugPreviewMode::GeneratedFeature;
	DebugSettings.LayerIndex = 1;
	TestTrue(
		TEXT("Compositor accepts generated-feature debug preview"),
		Compositor.RequestCompose(Layers, FSimpleDelegate(), DebugSettings));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetDebugOutput(), TEXT("Generated-feature debug output can be read"), Pixel))
	{
		TestTrue(TEXT("Active feature coverage uses the cyan debug color"), Pixel.G > Pixel.R && Pixel.B > Pixel.R);
	}
	Layers[1].bInvertFeature = false;
	Layers[1].FeatureInfluence = 0.0f;
	Layers[0].ConstantHeight = 1.0f;
	Layers[1].HeightFeatureInfluence = 1.0f;
	Layers[1].bInvertHeightFeature = true;
	TestTrue(TEXT("Compositor accepts inverted height-derived masking"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("Height-derived mask output can be read"), Pixel))
	{
		TestTrue(TEXT("Inverted high underlying height rejects the upper layer"), Pixel.R > Pixel.B);
	}
	Layers[1].HeightFeatureInfluence = 0.0f;
	Layers[1].bInvertHeightFeature = false;
	Layers[1].AOFeatureInfluence = 1.0f;
	Layers[1].bInvertAOFeature = true;
	TestTrue(TEXT("Compositor accepts inverted AO-derived masking"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetBaseColorOutput(), TEXT("AO-derived mask output can be read"), Pixel))
	{
		TestTrue(TEXT("Inverted unoccluded AO rejects the upper layer"), Pixel.R > Pixel.B);
	}

	Layers.Reset();
	TestTrue(TEXT("Compositor accepts an empty recipe"), Compositor.RequestCompose(Layers));
	FlushRenderingCommands();
	if (ReadFirstPixel(Compositor.GetRAMOutput(), TEXT("Neutral RAM output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Empty recipes emit neutral RAM"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2
				&& Pixel.G >= 253
				&& Pixel.B <= 2
				&& FMath::Abs(static_cast<int32>(Pixel.A) - 10) <= 2);
	}
	if (ReadFirstPixel(Compositor.GetHeightOutput(), TEXT("Neutral height output can be read"), Pixel))
	{
		TestTrue(
			TEXT("Empty recipes emit neutral height"),
			FMath::Abs(static_cast<int32>(Pixel.R) - 128) <= 2);
	}
	return true;
}

#endif
