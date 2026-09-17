// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

// Cover for the Layer Values mask source.
//
// Every one of these reads the composited base colour rather than a debug preview, because the
// claim being tested is not "a pass ran" but "the layer is masked by its own appearance" -- and
// the only place that is true or false is the composite. The arrangement is always the same: a
// black Fill underneath, the layer under test on top, Replace at Weight 1. The composite resolves
// that to lerp(black, LayerColour, Mask), so dividing a channel of the output by that channel of
// the layer's colour recovers the mask exactly.
//
// The fixtures are authored with SRGB off and linear bytes, so the value the shader samples is the
// value written here. That also makes these a check on the colour space: luminance is Rec. 709
// over linear RGB with no decode on the way in, and a stray sRGB conversion would move every
// expected number below well outside its tolerance.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatLayerValuesMaskTests
{
	constexpr int32 TestResolution = 256;

	// Loose enough for 8-bit fixtures and the composite's own blending, tight enough that the
	// channels below cannot be confused with one another -- the closest pair is 0.2 apart.
	constexpr float Tolerance = 0.03f;

	// The layer under test is this colour, and it is deliberately unequal in every channel: a
	// fixture with R == G == B would pass with the channel selector ignored entirely.
	//
	// Rec. 709 luminance of it is 0.2126*0.8 + 0.7152*0.5 + 0.0722*0.2 = 0.5421, which is distinct
	// from all three components and from the roughness below.
	const FLinearColor FillColor = FLinearColor(0.8f, 0.5f, 0.2f, 1.0f);
	constexpr float FillLuminance = 0.5421f;
	constexpr float FillRoughness = 0.35f;

	uint8 ToByte(const float Linear)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Linear * 255.0f), 0, 255));
	}

	// A texture painted by a predicate over UV, linear and uncompressed. SRGB is off so a byte is
	// the linear value the shader will sample; the importer's own sRGB flag on base colour is a
	// separate question, and either way this shader applies no conversion of its own.
	template <typename PredicateType>
	UTexture2D* MakeLinearTexture(PredicateType&& Predicate)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(
			TestResolution, TestResolution, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->Filter = TF_Bilinear;
		Texture->MipGenSettings = TMGS_NoMipmaps;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < TestResolution; ++Y)
		{
			const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(TestResolution);
			for (int32 X = 0; X < TestResolution; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(TestResolution);
				const FLinearColor Colour = Predicate(U, V);
				Pixels[Y * TestResolution + X] = FColor(
					ToByte(Colour.R), ToByte(Colour.G), ToByte(Colour.B), 255);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// Left half reddish, right half greenish, blue equal in both.
	//
	// The equal blue is what makes the mask readable: the layer's own albedo varies across the
	// image, so the composite output does too, and only the channel the layer is constant in
	// divides back out to the mask alone.
	//
	// Red and luminance also disagree about which half is brighter -- left is 0.8 red and 0.26
	// luminance, right is 0.1 red and 0.61 -- so a channel selection that silently did nothing
	// would put the bright half on the wrong side.
	const FLinearColor TwoToneLeft = FLinearColor(0.8f, 0.1f, 0.25f, 1.0f);
	const FLinearColor TwoToneRight = FLinearColor(0.1f, 0.8f, 0.25f, 1.0f);
	constexpr float TwoToneBlue = 0.25f;
	constexpr float TwoToneLeftLuminance = 0.2597f;
	constexpr float TwoToneRightLuminance = 0.6115f;

	UTexture2D* MakeTwoToneBaseColor()
	{
		return MakeLinearTexture([](const float U, const float)
		{
			return U < 0.5f ? TwoToneLeft : TwoToneRight;
		});
	}

	UTexture2D* MakeFlatNormal()
	{
		return MakeLinearTexture([](const float, const float)
		{
			return FLinearColor(0.5f, 0.5f, 1.0f, 1.0f);
		});
	}

	UTexture2D* MakeFlatRAM()
	{
		return MakeLinearTexture([](const float, const float)
		{
			return FLinearColor(0.5f, 1.0f, 0.0f, 1.0f);
		});
	}

	// A left/right split for the authored mask in the chain test: white left, black right.
	UTexture2D* MakeSplitMask()
	{
		return MakeLinearTexture([](const float U, const float)
		{
			return U < 0.5f ? FLinearColor::White : FLinearColor::Black;
		});
	}

	// bHasSurface wants all three maps, and without it the composite ignores the layer's source
	// entirely and the Layer Values pass reads the white stand-in instead.
	UMixtormatSurface* MakeSurface(UTexture2D* BaseColor, UTexture2D* Normal, UTexture2D* RAM)
	{
		UMixtormatSurface* Surface = NewObject<UMixtormatSurface>(
			GetTransientPackage(), NAME_None, RF_Transient);
		if (Surface)
		{
			Surface->BaseColor = BaseColor;
			Surface->Normal = Normal;
			Surface->RoughnessAOMetallic = RAM;
		}
		return Surface;
	}

	// Black, so the composite's lerp from what is underneath starts at zero and the output is the
	// masked layer colour and nothing else.
	FMixtormatLayer MakeBlackBase()
	{
		FMixtormatLayer Base;
		Base.Type = EMixtormatLayerType::Fill;
		Base.bEnabled = true;
		Base.bOverrideBaseColor = true;
		Base.BaseColor = FLinearColor::Black;
		Base.bOverrideRoughness = true;
		Base.Roughness = 0.5f;
		return Base;
	}

	FMixtormatLayerChild MakeLayerValuesMask(
		const EMixtormatLayerValueChannel Channel,
		const EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace)
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::Mask;
		Child.Mask.bEnabled = true;
		Child.Mask.Source = EMixtormatMaskSource::LayerValues;
		Child.Mask.LayerValueChannel = Channel;
		Child.Mask.BlendMode = BlendMode;
		Child.Mask.Weight = 1.0f;
		return Child;
	}

	bool ComposeAndWait(
		FMixtormatGpuCompositor& Compositor,
		const TArray<FMixtormatLayer>& Layers)
	{
		if (!Compositor.RequestCompose(Layers))
		{
			return false;
		}
		FlushRenderingCommands();
		return true;
	}

	bool ReadBaseColor(FMixtormatGpuCompositor& Compositor, TArray<FLinearColor>& OutPixels)
	{
		UTextureRenderTarget2D* Target = Compositor.GetBaseColorOutput();
		if (!Target)
		{
			return false;
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		return Resource && Resource->ReadLinearColorPixels(OutPixels)
			&& OutPixels.Num() == TestResolution * TestResolution;
	}

	int32 IndexAt(const float U, const float V)
	{
		const int32 X = FMath::Clamp(
			FMath::FloorToInt(U * TestResolution), 0, TestResolution - 1);
		const int32 Y = FMath::Clamp(
			FMath::FloorToInt(V * TestResolution), 0, TestResolution - 1);
		return Y * TestResolution + X;
	}

	// The composite resolved lerp(black, LayerColour, Mask), so one channel of the output over the
	// same channel of the layer's colour is the mask. Averaged over a patch rather than read from
	// a single texel, so a stray edge cannot decide the assertion.
	float MaskAt(
		const TArray<FLinearColor>& Pixels,
		const float U,
		const float V,
		const float LayerChannel,
		const int32 ChannelIndex)
	{
		double Total = 0.0;
		int32 Count = 0;
		for (int32 OffsetY = -4; OffsetY <= 4; ++OffsetY)
		{
			for (int32 OffsetX = -4; OffsetX <= 4; ++OffsetX)
			{
				const int32 Index = IndexAt(
					U + static_cast<float>(OffsetX) / TestResolution,
					V + static_cast<float>(OffsetY) / TestResolution);
				Total += Pixels[Index].Component(ChannelIndex);
				++Count;
			}
		}
		return Count > 0
			? static_cast<float>(Total / Count) / FMath::Max(LayerChannel, 1.0e-4f)
			: 0.0f;
	}
}

// The assertion the whole pass exists for.
//
// A Fill layer has no source maps, so the layer input a mask could read straight off is white --
// and a mask built on that would be a flat 1 that masks nothing, on every Fill layer in every
// material. What makes the mask mean anything here is that the fill colour is resolved into it
// first, which is exactly the difference this measures: 0.54, the luminance of the fill, and not
// 1.0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerValuesFillTest,
	"Mixtormat.Mask.LayerValuesFill",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerValuesFillTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatLayerValuesMaskTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	FMixtormatLayer Masked;
	Masked.Type = EMixtormatLayerType::Fill;
	Masked.bEnabled = true;
	Masked.bOverrideBaseColor = true;
	Masked.BaseColor = FillColor;
	Masked.Children.Add(MakeLayerValuesMask(EMixtormatLayerValueChannel::Luminance));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeBlackBase());
	Layers.Add(Masked);
	if (!TestTrue(TEXT("Layer Values mask composes on a Fill layer"),
		ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Base colour reads back"), ReadBaseColor(Compositor, Pixels)))
	{
		return false;
	}

	const float Mask = MaskAt(Pixels, 0.5f, 0.5f, FillColor.R, 0);
	TestTrue(
		FString::Printf(
			TEXT("Fill luminance reaches the mask (%.3f, expected %.3f)"),
			Mask,
			FillLuminance),
		FMath::IsNearlyEqual(Mask, FillLuminance, Tolerance));
	TestTrue(
		FString::Printf(
			TEXT("The mask is the fill colour, not the white layer input (%.3f)"), Mask),
		Mask < 0.9f);

	return true;
}

// Five channels, five different answers, on one fixture whose components are all different. A
// channel selector that was bound but ignored -- or one whose numbering drifted from the enum --
// lands on the wrong constant and every comparison here is 0.2 or more apart.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerValuesChannelsTest,
	"Mixtormat.Mask.LayerValuesChannels",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerValuesChannelsTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatLayerValuesMaskTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	FMixtormatLayer Masked;
	Masked.Type = EMixtormatLayerType::Fill;
	Masked.bEnabled = true;
	Masked.bOverrideBaseColor = true;
	Masked.BaseColor = FillColor;
	Masked.bOverrideRoughness = true;
	Masked.Roughness = FillRoughness;
	Masked.Children.Add(MakeLayerValuesMask(EMixtormatLayerValueChannel::Luminance));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeBlackBase());
	Layers.Add(Masked);

	struct FCase
	{
		EMixtormatLayerValueChannel Channel;
		float Expected;
		const TCHAR* Name;
	};
	const FCase Cases[] = {
		{EMixtormatLayerValueChannel::Luminance, FillLuminance, TEXT("Luminance")},
		{EMixtormatLayerValueChannel::Red, FillColor.R, TEXT("Red")},
		{EMixtormatLayerValueChannel::Green, FillColor.G, TEXT("Green")},
		{EMixtormatLayerValueChannel::Blue, FillColor.B, TEXT("Blue")},
		// The layer overrides roughness, so the fill value arrives unchanged: the composite
		// applies its bias/contrast/offset to the sampled roughness and then replaces it, and
		// this pass does the same thing in the same order.
		{EMixtormatLayerValueChannel::Roughness, FillRoughness, TEXT("Roughness")}};

	for (const FCase& Case : Cases)
	{
		Layers[1].Children[0].Mask.LayerValueChannel = Case.Channel;
		if (!TestTrue(
			FString::Printf(TEXT("%s channel composes"), Case.Name),
			ComposeAndWait(Compositor, Layers)))
		{
			return false;
		}

		TArray<FLinearColor> Pixels;
		if (!TestTrue(
			FString::Printf(TEXT("%s channel reads back"), Case.Name),
			ReadBaseColor(Compositor, Pixels)))
		{
			return false;
		}

		const float Mask = MaskAt(Pixels, 0.5f, 0.5f, FillColor.R, 0);
		TestTrue(
			FString::Printf(
				TEXT("%s resolves to its own value (%.3f, expected %.3f)"),
				Case.Name,
				Mask,
				Case.Expected),
			FMath::IsNearlyEqual(Mask, Case.Expected, Tolerance));
	}

	return true;
}

// The same thing on a Material layer, where the albedo comes from a map rather than a slider.
//
// Two halves, and the two channels disagree about which one is brighter, so this pins both that
// the mask follows the resolved albedo and that the channel control is actually steering which
// part of it -- swapping Luminance for Red has to swap which half of the layer shows.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerValuesMaterialTest,
	"Mixtormat.Mask.LayerValuesMaterial",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerValuesMaterialTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatLayerValuesMaskTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> BaseColor(MakeTwoToneBaseColor());
	TStrongObjectPtr<UTexture2D> Normal(MakeFlatNormal());
	TStrongObjectPtr<UTexture2D> RAM(MakeFlatRAM());
	if (!TestNotNull(TEXT("Two-tone base colour fixture exists"), BaseColor.Get())
		|| !TestNotNull(TEXT("Flat normal fixture exists"), Normal.Get())
		|| !TestNotNull(TEXT("Flat RAM fixture exists"), RAM.Get()))
	{
		return false;
	}
	TStrongObjectPtr<UMixtormatSurface> Surface(
		MakeSurface(BaseColor.Get(), Normal.Get(), RAM.Get()));
	if (!TestNotNull(TEXT("Test surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayer Masked;
	Masked.Type = EMixtormatLayerType::Material;
	Masked.bEnabled = true;
	Masked.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	Masked.Children.Add(MakeLayerValuesMask(EMixtormatLayerValueChannel::Luminance));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeBlackBase());
	Layers.Add(Masked);
	if (!TestTrue(TEXT("Luminance mask composes on a Material layer"),
		ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Luminance base colour reads back"), ReadBaseColor(Compositor, Pixels)))
	{
		return false;
	}

	// Blue is the channel the fixture holds equal across both halves, so it is the one that
	// divides back out to the mask rather than to the mask times a varying albedo.
	const float LuminanceLeft = MaskAt(Pixels, 0.25f, 0.5f, TwoToneBlue, 2);
	const float LuminanceRight = MaskAt(Pixels, 0.75f, 0.5f, TwoToneBlue, 2);

	TestTrue(
		FString::Printf(
			TEXT("Luminance follows the source albedo (%.3f left, expected %.3f)"),
			LuminanceLeft,
			TwoToneLeftLuminance),
		FMath::IsNearlyEqual(LuminanceLeft, TwoToneLeftLuminance, Tolerance));
	TestTrue(
		FString::Printf(
			TEXT("Luminance follows the source albedo (%.3f right, expected %.3f)"),
			LuminanceRight,
			TwoToneRightLuminance),
		FMath::IsNearlyEqual(LuminanceRight, TwoToneRightLuminance, Tolerance));

	Layers[1].Children[0].Mask.LayerValueChannel = EMixtormatLayerValueChannel::Red;
	if (!TestTrue(TEXT("Red mask composes on a Material layer"),
		ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Red base colour reads back"), ReadBaseColor(Compositor, Pixels)))
	{
		return false;
	}

	const float RedLeft = MaskAt(Pixels, 0.25f, 0.5f, TwoToneBlue, 2);
	const float RedRight = MaskAt(Pixels, 0.75f, 0.5f, TwoToneBlue, 2);

	TestTrue(
		FString::Printf(TEXT("Red reads the red channel (%.3f left, expected %.3f)"),
			RedLeft, TwoToneLeft.R),
		FMath::IsNearlyEqual(RedLeft, TwoToneLeft.R, Tolerance));
	TestTrue(
		FString::Printf(TEXT("Red reads the red channel (%.3f right, expected %.3f)"),
			RedRight, TwoToneRight.R),
		FMath::IsNearlyEqual(RedRight, TwoToneRight.R, Tolerance));

	// The two channels rank the halves oppositely, which no single fixed channel could produce.
	TestTrue(
		TEXT("Luminance and Red disagree about which half is brighter"),
		LuminanceLeft < LuminanceRight && RedLeft > RedRight);

	return true;
}

// The mask module's own controls, applied to this source.
//
// Inversion is Shaping.bInvert -- no second Invert was added for Layer Values -- and the Blur is
// the ordinary scoped Blur child. Both run in MixtormatMask.usf on whatever ResolveMaskSourceTexture
// returned, which is the claim: this is a source, not a new kind of mask.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerValuesShapingTest,
	"Mixtormat.Mask.LayerValuesShaping",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerValuesShapingTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatLayerValuesMaskTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> BaseColor(MakeTwoToneBaseColor());
	TStrongObjectPtr<UTexture2D> Normal(MakeFlatNormal());
	TStrongObjectPtr<UTexture2D> RAM(MakeFlatRAM());
	TStrongObjectPtr<UMixtormatSurface> Surface(
		MakeSurface(BaseColor.Get(), Normal.Get(), RAM.Get()));
	if (!TestNotNull(TEXT("Test surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayer Masked;
	Masked.Type = EMixtormatLayerType::Material;
	Masked.bEnabled = true;
	Masked.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	Masked.Children.Add(MakeLayerValuesMask(EMixtormatLayerValueChannel::Luminance));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeBlackBase());
	Layers.Add(Masked);
	if (!TestTrue(TEXT("Plain mask composes"), ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	TArray<FLinearColor> Plain;
	if (!TestTrue(TEXT("Plain mask reads back"), ReadBaseColor(Compositor, Plain)))
	{
		return false;
	}

	Layers[1].Children[0].Mask.Shaping.bInvert = true;
	if (!TestTrue(TEXT("Inverted mask composes"), ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	TArray<FLinearColor> Inverted;
	if (!TestTrue(TEXT("Inverted mask reads back"), ReadBaseColor(Compositor, Inverted)))
	{
		return false;
	}

	const float PlainLeft = MaskAt(Plain, 0.25f, 0.5f, TwoToneBlue, 2);
	const float InvertedLeft = MaskAt(Inverted, 0.25f, 0.5f, TwoToneBlue, 2);
	TestTrue(
		FString::Printf(
			TEXT("Shaping inversion inverts the layer value (%.3f plain, %.3f inverted)"),
			PlainLeft,
			InvertedLeft),
		FMath::IsNearlyEqual(InvertedLeft, 1.0f - PlainLeft, Tolerance));

	// A Blur scoped under the mask, which is the mask module's own filter node. On a hard-edged
	// source it has one visible consequence: the step between the two halves becomes a ramp.
	Layers[1].Children[0].Mask.Shaping.bInvert = false;
	FMixtormatLayerChild& Blur = Layers[1].Children.AddDefaulted_GetRef();
	Blur.Type = EMixtormatLayerChildType::Blur;
	Blur.ScopeOwnerChildId = Layers[1].Children[0].ChildId;
	Blur.Blur.bEnabled = true;
	Blur.Blur.RadiusX = 12.0f;
	Blur.Blur.RadiusY = 12.0f;
	if (!TestTrue(TEXT("Blurred mask composes"), ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	TArray<FLinearColor> Blurred;
	if (!TestTrue(TEXT("Blurred mask reads back"), ReadBaseColor(Compositor, Blurred)))
	{
		return false;
	}

	// Texels on the centre row sitting clear of both plateaus. A hard edge produces a couple from
	// bilinear filtering alone; a blur of this radius produces a band of them.
	const auto CountIntermediate = [](const TArray<FLinearColor>& Pixels)
	{
		const float Low = TwoToneLeftLuminance + 0.06f;
		const float High = TwoToneRightLuminance - 0.06f;
		int32 Count = 0;
		const int32 Row = TestResolution / 2;
		for (int32 X = 0; X < TestResolution; ++X)
		{
			const float Value = Pixels[Row * TestResolution + X].B / TwoToneBlue;
			Count += Value > Low && Value < High ? 1 : 0;
		}
		return Count;
	};

	const int32 PlainBand = CountIntermediate(Plain);
	const int32 BlurredBand = CountIntermediate(Blurred);
	TestTrue(
		FString::Printf(
			TEXT("A scoped Blur softens the layer-value edge (%d texels plain, %d blurred)"),
			PlainBand,
			BlurredBand),
		BlurredBand > PlainBand + 8);

	return true;
}

// Combination with the rest of the chain, and the independence that stands in for cycle safety.
//
// A Layer Values mask multiplied under an authored one has to give the product, and -- the part
// that matters -- the Layer Values factor has to be the same number it is on its own. It reads the
// layer's resolved input and nothing from a mask chain, so rearranging the chain around it cannot
// move it. If it could, that would be the feedback path this design exists to make impossible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerValuesChainTest,
	"Mixtormat.Mask.LayerValuesChain",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerValuesChainTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatLayerValuesMaskTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SplitMask(MakeSplitMask());
	if (!TestNotNull(TEXT("Split mask fixture exists"), SplitMask.Get()))
	{
		return false;
	}

	FMixtormatLayer Masked;
	Masked.Type = EMixtormatLayerType::Fill;
	Masked.bEnabled = true;
	Masked.bOverrideBaseColor = true;
	Masked.BaseColor = FillColor;
	Masked.Children.Add(MakeLayerValuesMask(EMixtormatLayerValueChannel::Luminance));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeBlackBase());
	Layers.Add(Masked);
	if (!TestTrue(TEXT("Layer Values alone composes"), ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	TArray<FLinearColor> Alone;
	if (!TestTrue(TEXT("Layer Values alone reads back"), ReadBaseColor(Compositor, Alone)))
	{
		return false;
	}
	const float AloneLeft = MaskAt(Alone, 0.25f, 0.5f, FillColor.R, 0);

	// An authored split mask ahead of it, and the Layer Values mask multiplied into that.
	FMixtormatLayerChild Authored;
	Authored.Type = EMixtormatLayerChildType::Mask;
	Authored.Mask.bEnabled = true;
	Authored.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(SplitMask.Get()));
	Authored.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	Authored.Mask.Weight = 1.0f;

	Layers[1].Children.Insert(Authored, 0);
	Layers[1].Children[1].Mask.BlendMode = EMixtormatMaskBlendMode::Multiply;
	if (!TestTrue(TEXT("Layer Values multiplied under an authored mask composes"),
		ComposeAndWait(Compositor, Layers)))
	{
		return false;
	}
	TArray<FLinearColor> Combined;
	if (!TestTrue(TEXT("Combined chain reads back"), ReadBaseColor(Compositor, Combined)))
	{
		return false;
	}

	const float CombinedLeft = MaskAt(Combined, 0.25f, 0.5f, FillColor.R, 0);
	const float CombinedRight = MaskAt(Combined, 0.75f, 0.5f, FillColor.R, 0);

	TestTrue(
		FString::Printf(
			TEXT("The chain multiplies where the authored mask allows (%.3f, expected %.3f)"),
			CombinedLeft,
			AloneLeft),
		FMath::IsNearlyEqual(CombinedLeft, AloneLeft, Tolerance));
	TestTrue(
		FString::Printf(
			TEXT("The chain closes where the authored mask is black (%.3f)"), CombinedRight),
		CombinedRight < Tolerance);

	// The independence claim, made directly: the authored mask changed from absent to a
	// half-covering split, and the Layer Values factor did not move. A source that could see the
	// chain would have.
	TestTrue(
		FString::Printf(
			TEXT("Layer Values is unchanged by the chain around it (%.3f alone, %.3f in chain)"),
			AloneLeft,
			CombinedLeft),
		FMath::IsNearlyEqual(AloneLeft, CombinedLeft, Tolerance));

	// A Flow Warp between two Layer Values masks. Flow Warp rewrites LayerInputBC, and the layer's
	// values are resolved once and cached, so both consumers here read the same snapshot -- taken
	// at whichever of them resolved first -- rather than one seeing warped albedo and the other
	// not. That is the intended contract of a per-layer cache; what this asserts is the part that
	// could actually break, which is that a producer landing between two consumers of a shared
	// idempotent resource still builds a valid graph.
	{
		FMixtormatLayerChild& Warp = Layers[1].Children.AddDefaulted_GetRef();
		Warp.Type = EMixtormatLayerChildType::Effect;
		Warp.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
		Warp.Effect.Strength = 1.0f;

		Layers[1].Children.Add(
			MakeLayerValuesMask(
				EMixtormatLayerValueChannel::Green, EMixtormatMaskBlendMode::Multiply));
		TestTrue(
			TEXT("Layer Values masks either side of a Flow Warp compose"),
			ComposeAndWait(Compositor, Layers));
	}

	// And once more scoped under an effect, which routes through AddScopedFeatureMask rather than
	// the layer chain. Composing at all is the assertion: that path resolves its source through
	// the same choke point, so a missing branch there is a graph-build failure, not a wrong pixel.
	FMixtormatLayerChild& Effect = Layers[1].Children.AddDefaulted_GetRef();
	Effect.Type = EMixtormatLayerChildType::Effect;
	Effect.Effect.ProceduralType = EMixtormatEffectType::Grade;
	Effect.Effect.Strength = 1.0f;

	FMixtormatLayerChild Scoped = MakeLayerValuesMask(EMixtormatLayerValueChannel::Roughness);
	Scoped.ScopeOwnerChildId = Effect.ChildId;
	Layers[1].Children.Add(Scoped);
	TestTrue(
		TEXT("A Layer Values mask scoped under an effect composes"),
		ComposeAndWait(Compositor, Layers));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
