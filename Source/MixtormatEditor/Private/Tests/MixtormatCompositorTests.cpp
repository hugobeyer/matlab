#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

// End-to-end cover for the generated mask nodes and the height filters.
//
// These exist because nothing else in the plugin catches an RDG binding mistake. A compute pass
// with a texture bound to the wrong slot, a transient nothing wrote, or a cached external
// resource registered into a graph that never produces it all compile perfectly and only fail
// when the graph is built -- which, until now, meant the first time someone opened the panel and
// looked at it.
//
// Everything here runs at 256 so a full stack costs a few milliseconds, and reads back through
// the debug target, which the compositor already fills with a chosen child's mask.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatCompositorTests
{
	constexpr int32 TestResolution = 256;

	// An ID map: left half one colour, right half another, with no gradient between them. Point
	// sampling is the whole contract of the colour id node, so the fixture has to be a hard edge
	// -- a gradient would pass under bilinear filtering too and prove nothing.
	UTexture2D* MakeTwoToneIdMap(const FColor Left, const FColor Right)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(
			TestResolution, TestResolution, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

		// Uncompressed and linear, the same import settings the node's tooltip asks for. Anything
		// else moves the colours out from under the comparison the shader is about to make.
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->Filter = TF_Nearest;
		Texture->MipGenSettings = TMGS_NoMipmaps;

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < TestResolution; ++Y)
		{
			for (int32 X = 0; X < TestResolution; ++X)
			{
				Pixels[Y * TestResolution + X] = X < TestResolution / 2 ? Left : Right;
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// One material layer with a single child, which is all any of these tests need: the child is
	// the thing under test and the layer is just somewhere to hang it.
	FMixtormatLayer MakeLayerWithChild(const FMixtormatLayerChild& Child)
	{
		FMixtormatLayer Layer;
		Layer.DisplayName = FText::FromString(TEXT("Test"));
		Layer.bEnabled = true;
		Layer.Children.Add(Child);
		return Layer;
	}

	// Composes and waits. RequestCompose enqueues onto the render thread and returns immediately,
	// so without the flush the read below races the passes that fill the targets.
	bool ComposeAndWait(
		FMixtormatGpuCompositor& Compositor,
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatDebugPreviewSettings& Debug)
	{
		if (!Compositor.RequestCompose(Layers, FSimpleDelegate(), Debug))
		{
			return false;
		}
		FlushRenderingCommands();
		return true;
	}

	// Linear rather than FColor. The debug target is PF_FloatRGBA and the height target is R16F;
	// ReadPixels quantises the first and ReadFloat16Pixels refuses the second outright, since it
	// insists on a four-channel half format.
	bool ReadTarget(UTextureRenderTarget2D* Target, TArray<FLinearColor>& OutPixels)
	{
		if (!Target)
		{
			return false;
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		return Resource && Resource->ReadLinearColorPixels(OutPixels)
			&& OutPixels.Num() == TestResolution * TestResolution;
	}

	// The debug target does not hold the mask, it holds the mask ramped between two dark colours
	// so it reads as a preview rather than as data. Undo that here: green runs from 0.02 to 0.25
	// across the range, and red barely moves at all, which is why sampling red says nothing.
	float DebugValue(const FLinearColor& Pixel)
	{
		constexpr float Low = 0.02f;
		constexpr float High = 0.25f;
		return FMath::Clamp((Pixel.G - Low) / (High - Low), 0.0f, 1.0f);
	}

	// A generated network is meant to be structure, not a flat fill. Both extremes have to be
	// present or something upstream produced a constant -- which is exactly what an unbound
	// texture or a starved growth pass looks like.
	bool HasBothExtremes(const TArray<FLinearColor>& Pixels, const float DarkBelow, const float BrightAbove)
	{
		bool bDark = false;
		bool bBright = false;
		for (const FLinearColor& Pixel : Pixels)
		{
			const float Value = DebugValue(Pixel);
			bDark |= Value <= DarkBelow;
			bBright |= Value >= BrightAbove;
			if (bDark && bBright)
			{
				return true;
			}
		}
		return false;
	}

	bool ReadHeight(UTextureRenderTarget2D* Target, TArray<FLinearColor>& OutPixels)
	{
		return ReadTarget(Target, OutPixels);
	}

	FMixtormatDebugPreviewSettings LayerMaskDebug()
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::LayerMask;
		Debug.LayerIndex = 0;
		Debug.ChildIndex = 0;
		return Debug;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCraquelureNetworkTest,
	"Mixtormat.Compositor.CraquelureNetwork",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatCraquelureNetworkTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Craquelure;
	Child.Craquelure.Mode = EMixtormatCraquelureMode::Propagated;
	Child.Craquelure.SeedCells = 6;
	Child.Craquelure.SeedChance = 1.0f;
	Child.Craquelure.Iterations = 64;
	Child.Craquelure.Width = 0.08f;

	// Relief off for this one. The network is what is under test, and leaving the height alone
	// keeps a failure here from being ambiguous with a failure in the relief pass.
	Child.Craquelure.ReliefDepth = 0.0f;
	Child.Craquelure.ReliefNormalStrength = 0.0f;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeLayerWithChild(Child));

	if (!TestTrue(TEXT("Propagated craquelure composes"),
		ComposeAndWait(Compositor, Layers, LayerMaskDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Debug target reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Pixels)))
	{
		return false;
	}

	// The seed, the growth loop, the jump flood and the resolve all have to have run and bound
	// correctly to get a network out. A uniform result is what any of them failing looks like.
	TestTrue(TEXT("Grown network has cracks and cells"), HasBothExtremes(Pixels, 0.15f, 0.85f));

	// Composed a second time with the same parameters, which takes the cache-hit path: the
	// network is registered from a pooled target instead of grown. The result has to be the same
	// field, or the cache is answering with something the miss would not have produced.
	if (!TestTrue(TEXT("Cached craquelure composes"),
		ComposeAndWait(Compositor, Layers, LayerMaskDebug())))
	{
		return false;
	}

	TArray<FLinearColor> CachedPixels;
	if (!TestTrue(TEXT("Cached debug target reads back"),
		ReadTarget(Compositor.GetDebugOutput(), CachedPixels)))
	{
		return false;
	}

	int32 Differences = 0;
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		Differences += Pixels[Index].G == CachedPixels[Index].G ? 0 : 1;
	}
	TestEqual(TEXT("Cached network matches the grown one"), Differences, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCraquelureReliefTest,
	"Mixtormat.Compositor.CraquelureRelief",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatCraquelureReliefTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Craquelure;
	Child.Craquelure.Mode = EMixtormatCraquelureMode::Propagated;
	Child.Craquelure.SeedCells = 6;
	Child.Craquelure.SeedChance = 1.0f;
	Child.Craquelure.Iterations = 64;

	// Mask muted, relief on. The two halves are independent weights now, and this is the
	// combination that proves it: a node contributing nothing to the mask still has to carve.
	Child.Craquelure.Weight = 0.0f;
	Child.Craquelure.ReliefDepth = 0.25f;
	Child.Craquelure.ReliefNormalStrength = 8.0f;
	Child.Craquelure.ReliefWidth = 0.08f;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeLayerWithChild(Child));

	if (!TestTrue(TEXT("Relief composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	UTextureRenderTarget2D* HeightTarget = Compositor.GetHeightOutput();
	if (!TestNotNull(TEXT("Height output exists"), HeightTarget))
	{
		return false;
	}

	// Linear, not ReadFloat16Pixels: that path asserts the target is four-channel half, and the
	// height chain is R16F. The values still arrive unquantised, which is what the test wants --
	// the grooves are a quarter of the range but the ceiling check is exact.
	TArray<FLinearColor> HeightPixels;
	if (!TestTrue(TEXT("Height target reads back"), ReadHeight(HeightTarget, HeightPixels)))
	{
		return false;
	}

	float Minimum = TNumericLimits<float>::Max();
	float Maximum = TNumericLimits<float>::Lowest();
	for (const FLinearColor& Pixel : HeightPixels)
	{
		Minimum = FMath::Min(Minimum, Pixel.R);
		Maximum = FMath::Max(Maximum, Pixel.R);
	}

	// The composite clears height to 0.5 and this layer adds none of its own, so anything below
	// that is the groove and nothing should be above it: relief subtracts under a minimum, and
	// this is the invariant that says so.
	TestTrue(TEXT("Relief cuts the height"), Minimum < 0.45f);
	TestTrue(TEXT("Relief never raises the height"), Maximum <= 0.5f + KINDA_SMALL_NUMBER);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatColorIdMaskTest,
	"Mixtormat.Compositor.ColorIdMask",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatColorIdMaskTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	// Rooted for the length of the test. The layer holds it through a soft pointer, which will
	// not keep a transient object alive on its own.
	TStrongObjectPtr<UTexture2D> IdMap(
		MakeTwoToneIdMap(FColor(255, 0, 0, 255), FColor(0, 0, 255, 255)));
	if (!TestTrue(TEXT("ID map created"), IdMap.IsValid()))
	{
		return false;
	}

	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::ColorId;
	Child.ColorId.IdTexture = TSoftObjectPtr<UTexture2D>(IdMap.Get());
	Child.ColorId.Colors.Add(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
	Child.ColorId.Tolerance = 0.10f;
	Child.ColorId.Softness = 0.02f;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(MakeLayerWithChild(Child));

	if (!TestTrue(TEXT("Colour id composes"),
		ComposeAndWait(Compositor, Layers, LayerMaskDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Debug target reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Pixels)))
	{
		return false;
	}

	// Sampled well inside each half rather than at the seam, so the softness band is not what
	// decides the result. The left half is the selected colour and the right is not.
	const int32 Row = TestResolution / 2;
	const float Selected = DebugValue(Pixels[Row * TestResolution + TestResolution / 4]);
	const float Rejected = DebugValue(Pixels[Row * TestResolution + (TestResolution * 3) / 4]);

	TestTrue(TEXT("Selected id is masked in"), Selected >= 0.9f);
	TestTrue(TEXT("Unselected id is masked out"), Rejected <= 0.1f);

	// Inverted, the same node has to answer the other way round. This is the mask tail running
	// on the selection, which is the half of the node that is shared with every other mask child.
	Layers[0].Children[0].ColorId.bInvert = true;
	if (!TestTrue(TEXT("Inverted colour id composes"),
		ComposeAndWait(Compositor, Layers, LayerMaskDebug())))
	{
		return false;
	}

	TArray<FLinearColor> InvertedPixels;
	if (!TestTrue(TEXT("Inverted debug target reads back"),
		ReadTarget(Compositor.GetDebugOutput(), InvertedPixels)))
	{
		return false;
	}

	TestTrue(
		TEXT("Inverted selection excludes the chosen id"),
		DebugValue(InvertedPixels[Row * TestResolution + TestResolution / 4]) <= 0.1f);
	TestTrue(
		TEXT("Inverted selection includes the rest"),
		DebugValue(InvertedPixels[Row * TestResolution + (TestResolution * 3) / 4]) >= 0.9f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatChippingIdentityTest,
	"Mixtormat.Compositor.ChippingIdentity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatChippingIdentityTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	// Craquelure relief carves a surface for chipping to find. Chipping thresholds the height it
	// is handed against that height's own extent now, so it needs something with a range at all
	// -- on the flat clear value there are no bricks and correctly nothing happens.
	FMixtormatLayerChild CrackChild;
	CrackChild.Type = EMixtormatLayerChildType::Craquelure;
	CrackChild.Craquelure.Mode = EMixtormatCraquelureMode::Lattice;
	CrackChild.Craquelure.Period = 8;
	CrackChild.Craquelure.Jitter = 0.0f;
	CrackChild.Craquelure.Weight = 0.0f;
	CrackChild.Craquelure.ReliefDepth = 0.30f;
	CrackChild.Craquelure.ReliefWidth = 0.30f;
	CrackChild.Craquelure.ReliefNormalStrength = 0.0f;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Chipping placement mask exists"), WhiteMask))
	{
		return false;
	}

	FMixtormatLayerChild ChipChild;
	ChipChild.Type = EMixtormatLayerChildType::Effect;
	ChipChild.Effect.ProceduralType = EMixtormatEffectType::Chipping;
	ChipChild.Effect.ChipAmount = 0.0f;
	ChipChild.Effect.ChipDepth = 0.1f;
	ChipChild.Effect.ChipMaskTexture =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));

	FMixtormatLayer Layer = MakeLayerWithChild(CrackChild);
	Layer.Children.Add(ChipChild);

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	// Amount 0 seeds nothing, so the height has to come back exactly as the relief left it. This
	// is the Filter contract, and it is the assertion the height normalisation could most easily
	// have broken: the reduction runs on every composite whether or not anything is carved.
	if (!TestTrue(TEXT("Chipping at zero composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> WithChipping;
	if (!TestTrue(TEXT("Height reads back"),
		ReadHeight(Compositor.GetHeightOutput(), WithChipping)))
	{
		return false;
	}

	Layers[0].Children.RemoveAt(1);
	if (!TestTrue(TEXT("Reference composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> WithoutChipping;
	if (!TestTrue(TEXT("Reference height reads back"),
		ReadHeight(Compositor.GetHeightOutput(), WithoutChipping)))
	{
		return false;
	}

	if (!TestEqual(TEXT("Both reads are the same size"),
		WithChipping.Num(), WithoutChipping.Num()))
	{
		return false;
	}

	int32 Differences = 0;
	float Minimum = TNumericLimits<float>::Max();
	float Maximum = TNumericLimits<float>::Lowest();
	for (int32 Index = 0; Index < WithChipping.Num(); ++Index)
	{
		const float Value = WithoutChipping[Index].R;
		Minimum = FMath::Min(Minimum, Value);
		Maximum = FMath::Max(Maximum, Value);
		Differences += WithChipping[Index].R == Value ? 0 : 1;
	}

	// The fixture is only meaningful if the relief actually produced a range for chipping to have
	// thresholded. Without this the identity check would pass on a flat image for the wrong
	// reason.
	TestTrue(TEXT("Relief produced a height range"), Maximum - Minimum > 0.05f);
	TestEqual(TEXT("Chipping at Amount 0 is the identity"), Differences, 0);

	ChipChild.Effect.ChipAmount = 1.0f;
	Layers[0].Children.Add(ChipChild);
	if (!TestTrue(TEXT("Active chipping composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ActiveChipping;
	if (!TestTrue(TEXT("Active chipping height reads back"),
		ReadHeight(Compositor.GetHeightOutput(), ActiveChipping)))
	{
		return false;
	}

	int32 CarvedPixels = 0;
	for (int32 Index = 0; Index < ActiveChipping.Num(); ++Index)
	{
		CarvedPixels += ActiveChipping[Index].R < WithoutChipping[Index].R - 1.0e-4f ? 1 : 0;
	}
	TestTrue(TEXT("Active chipping carves the height"), CarvedPixels > 0);

	return true;
}

#endif
