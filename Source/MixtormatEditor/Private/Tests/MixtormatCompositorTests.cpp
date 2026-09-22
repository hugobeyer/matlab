// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatSurface.h"
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

	// A packed RAMH map split down the middle: two flat roughness bands with a height step
	// between them, and nothing else. The cluster filter merges on roughness band *and* height
	// step, so a fixture with both channels agreeing is the one that says the two-channel
	// criterion ran rather than only half of it.
	//
	// The bands also have to survive the wrap. Column 0's left neighbour is column 255, which is
	// in the other band, so a correct kernel refuses that merge and the two halves stay separate
	// -- if wrapping were dropped or the bands compared wrongly, the whole image collapses to one
	// region and the assertions below see one colour.
	UTexture2D* MakeTwoBandRAMH()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(
			TestResolution, TestResolution, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}

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
				// R is roughness, A is height -- the two channels the segmentation reads.
				// G and B are AO and metallic and are never sampled by this pass.
				const bool bLeft = X < TestResolution / 2;
				Pixels[Y * TestResolution + X] =
					FColor(bLeft ? 51 : 204, 128, 0, bLeft ? 64 : 192);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// The filter reads the layer's packed map through its surface, so unlike every other fixture
	// here it needs a surface to hang the texture on. Transient and rooted by the caller: the
	// soft pointer resolves it by name out of the transient package for as long as it is alive.
	UMixtormatSurface* MakeSurfaceWithRAMH(UTexture2D* RAMH)
	{
		UMixtormatSurface* Surface = NewObject<UMixtormatSurface>(
			GetTransientPackage(), NAME_None, RF_Transient);
		if (Surface)
		{
			Surface->RoughnessAOMetallic = RAMH;
		}
		return Surface;
	}

	UMixtormatSurface* MakeFlowWarpSurface(
		UTexture2D* BaseColor,
		UTexture2D* Normal,
		UTexture2D* RAMH)
	{
		UMixtormatSurface* Surface = MakeSurfaceWithRAMH(RAMH);
		if (Surface)
		{
			Surface->BaseColor = BaseColor;
			Surface->Normal = Normal;
			Surface->bHasBlendHeight = true;
			Surface->BlendHeightProvenance = EMixtormatBlendHeightProvenance::AuthoredRAMH;
		}
		return Surface;
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
	FMixtormatClusterIdsTest,
	"Mixtormat.Compositor.ClusterIds",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatClusterIdsTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> RAMH(MakeTwoBandRAMH());
	if (!TestNotNull(TEXT("Two-band RAMH fixture exists"), RAMH.Get()))
	{
		return false;
	}
	TStrongObjectPtr<UMixtormatSurface> Surface(MakeSurfaceWithRAMH(RAMH.Get()));
	if (!TestNotNull(TEXT("Test surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Filter;
	Child.Filter.Threshold = 0.33f;
	Child.Filter.Offset = 0.0f;
	Child.Filter.HeightInfluence = 1.0f;

	FMixtormatLayer Layer = MakeLayerWithChild(Child);
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	// The filter publishes its own preview and the composite is told to leave the debug target
	// alone for this mode, so the debug read below is the kernel's output and nothing else.
	// LayerIndex/ChildIndex are resolved from ChildTarget by RequestComposeInternal itself; the
	// test only has to name the child by (LayerId, ChildId), the same way the editor does.
	FMixtormatDebugPreviewSettings Debug;
	Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
	Debug.ChildTarget.OwnerId = Layer.LayerId;
	Debug.ChildTarget.ChildId = Child.ChildId;
	Debug.ChildTarget.Kind = EMixtormatPreviewOutputKind::RegionIds;

	if (!TestTrue(TEXT("Cluster filter composes"), ComposeAndWait(Compositor, Layers, Debug)))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Debug target reads back"), ReadTarget(Compositor.GetDebugOutput(), Pixels)))
	{
		return false;
	}

	// Two interior samples, one per band, well away from the boundary. Roots are pixel indices
	// hashed to a colour, so two different regions collide only by accident of the hash.
	const int32 LeftIndex = (TestResolution / 2) * TestResolution + TestResolution / 4;
	const int32 RightIndex = (TestResolution / 2) * TestResolution + (3 * TestResolution) / 4;
	TestTrue(
		TEXT("The two bands segment into different regions"),
		!Pixels[LeftIndex].Equals(Pixels[RightIndex], 1.0e-4f));

	// The count is the binding assertion, and it brackets both failure modes at once. An unwritten
	// or unbound output leaves the single clear colour; a parents buffer that never got hooked
	// leaves every pixel its own root and one colour per pixel. Only a union-find that actually
	// ran lands between the two.
	TSet<uint32> DistinctRegions;
	for (const FLinearColor& Pixel : Pixels)
	{
		DistinctRegions.Add(Pixel.ToFColor(false).ToPackedRGBA());
	}
	TestTrue(
		FString::Printf(
			TEXT("Union-find produced regions rather than a fill or a per-pixel map (%d distinct)"),
			DistinctRegions.Num()),
		DistinctRegions.Num() >= 2 && DistinctRegions.Num() <= TestResolution);

	return true;
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
	Child.Craquelure.Scale = 6;
	Child.Craquelure.Density = 1.0f;
	Child.Craquelure.Iterations = 64;
	Child.Craquelure.Width = 0.08f;

	// Relief off for this one. The network is what is under test, and leaving the height alone
	// keeps a failure here from being ambiguous with a failure in the relief pass.
	Child.Craquelure.ReliefDepth = 0.0f;

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
	Child.Craquelure.Scale = 6;
	Child.Craquelure.Density = 1.0f;
	Child.Craquelure.Iterations = 64;

	// Mask muted, relief on. A node contributing nothing to the mask still has to carve, and
	// its normal is derived automatically from that carved height.
	Child.Craquelure.Weight = 0.0f;
	Child.Craquelure.ReliefDepth = 0.25f;
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
	Layers[0].Children[0].ColorId.Shaping.bInvert = true;
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
	FMixtormatBreakupIdentityTest,
	"Mixtormat.Compositor.BreakupIdentity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBreakupIdentityTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
		return false;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
		return false;

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;

	FMixtormatLayerChild BreakupChild;
	BreakupChild.Type = EMixtormatLayerChildType::Effect;
	BreakupChild.Effect.ProceduralType = EMixtormatEffectType::Breakup;
	BreakupChild.Effect.BreakupAmount = 0.0f;
	BreakupChild.Effect.BreakupRelief = -0.15f;
	BreakupChild.Effect.BreakupFold = 0.05f;
	BreakupChild.Effect.BreakupCrease = 0.02f;
	BreakupChild.Effect.BreakupMaskTexture =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
	Layer.Children.Add(BreakupChild);

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	if (!TestTrue(TEXT("Breakup at zero composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ZeroHeight;
	if (!TestTrue(TEXT("Breakup zero height reads"),
		ReadHeight(Compositor.GetHeightOutput(), ZeroHeight)))
		return false;

	Layers[0].Children.RemoveAt(0);
	if (!TestTrue(TEXT("Reference composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ReferenceHeight;
	TArray<FLinearColor> ReferenceBaseColor;
	if (!ReadHeight(Compositor.GetHeightOutput(), ReferenceHeight)
		|| !ReadTarget(Compositor.GetBaseColorOutput(), ReferenceBaseColor))
		return false;

	int32 IdentityDifferences = 0;
	for (int32 Index = 0; Index < ZeroHeight.Num(); ++Index)
		IdentityDifferences += ZeroHeight[Index].R == ReferenceHeight[Index].R ? 0 : 1;
	TestEqual(TEXT("Breakup Amount 0 is exact identity"), IdentityDifferences, 0);

	BreakupChild.Effect.BreakupAmount = 1.0f;
	Layers[0].Children.Add(BreakupChild);
	if (!TestTrue(TEXT("Active Breakup composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ActiveHeight;
	TArray<FLinearColor> ActiveBaseColor;
	if (!ReadHeight(Compositor.GetHeightOutput(), ActiveHeight)
		|| !ReadTarget(Compositor.GetBaseColorOutput(), ActiveBaseColor))
		return false;

	int32 HeightChanges = 0;
	int32 BaseColorChanges = 0;
	for (int32 Index = 0; Index < ActiveHeight.Num(); ++Index)
	{
		HeightChanges += FMath::Abs(ActiveHeight[Index].R - ReferenceHeight[Index].R) > 1.0e-4f ? 1 : 0;
		BaseColorChanges += ActiveBaseColor[Index].Equals(ReferenceBaseColor[Index], 1.0e-5f) ? 0 : 1;
	}
	TestTrue(TEXT("Active Breakup changes height"), HeightChanges > 0);
	TestEqual(TEXT("Breakup does not author base color"), BaseColorChanges, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupPublishedOutputsTest,
	"Mixtormat.Compositor.BreakupPublishedOutputs",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBreakupPublishedOutputsTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	// Breakup as a source rather than as a height effect. The three scalar maps are published
	// from the field pass rather than from the deferred carve, so none of this depends on Amount
	// -- a Breakup used purely as a structural generator still has to feed what reads it.
	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
		return false;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
		return false;

	const auto BuildLayers = [WhiteMask](const FName Output, const float GapWidth)
	{
		FMixtormatLayer Layer;
		Layer.Type = EMixtormatLayerType::Fill;

		FMixtormatLayerChild BreakupChild;
		BreakupChild.Type = EMixtormatLayerChildType::Effect;
		BreakupChild.Effect.ProceduralType = EMixtormatEffectType::Breakup;
		BreakupChild.Effect.BreakupAmount = 0.0f;
		BreakupChild.Effect.BreakupGapWidth = GapWidth;
		BreakupChild.Effect.BreakupMaskTexture =
			TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
		Layer.Children.Add(BreakupChild);

		FMixtormatLayerChild MaskChild;
		MaskChild.Type = EMixtormatLayerChildType::Mask;
		MaskChild.Mask.bEnabled = true;
		MaskChild.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		MaskChild.Mask.Weight = 1.0f;
		MaskChild.Mask.PublishedSourceLayerId = Layer.LayerId;
		MaskChild.Mask.PublishedSourceChildId = Layer.Children[0].ChildId;
		MaskChild.Mask.PublishedSourceOutput = Output;
		Layer.Children.Add(MaskChild);

		TArray<FMixtormatLayer> Layers;
		Layers.Add(Layer);
		return Layers;
	};

	FMixtormatDebugPreviewSettings MaskDebug = LayerMaskDebug();
	MaskDebug.ChildIndex = 1;

	// Edge. A boundary mask has to contain both a boundary and an interior; a flat result means
	// the output never resolved and the mask chain fell through to white.
	{
		TArray<FMixtormatLayer> Layers = BuildLayers(FName(TEXT("Edge")), 4.0f);
		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Breakup Edge composes"), ComposeAndWait(Compositor, Layers, MaskDebug))
			|| !TestTrue(TEXT("Breakup Edge reads"), ReadTarget(Compositor.GetDebugOutput(), Pixels)))
			return false;
		TestTrue(TEXT("Breakup publishes a structured Edge at Amount 0"),
			HasBothExtremes(Pixels, 0.1f, 0.6f));
	}

	// Pieces, and it must not be the same picture as Edge: the interior and its boundary are
	// different maps, and publishing one under both names would pass a laxer test than this.
	TArray<FLinearColor> PiecePixels;
	{
		TArray<FMixtormatLayer> Layers = BuildLayers(FName(TEXT("Pieces")), 4.0f);
		if (!TestTrue(TEXT("Breakup Pieces composes"), ComposeAndWait(Compositor, Layers, MaskDebug))
			|| !TestTrue(TEXT("Breakup Pieces reads"),
				ReadTarget(Compositor.GetDebugOutput(), PiecePixels)))
			return false;
		TestTrue(TEXT("Breakup publishes a structured Pieces at Amount 0"),
			HasBothExtremes(PiecePixels, 0.1f, 0.6f));
	}

	// Gap with the grout open, driven by the same GapWidth and per-piece jitter the carve uses.
	{
		TArray<FMixtormatLayer> Layers = BuildLayers(FName(TEXT("Gap")), 6.0f);
		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Breakup Gap composes"), ComposeAndWait(Compositor, Layers, MaskDebug))
			|| !TestTrue(TEXT("Breakup Gap reads"), ReadTarget(Compositor.GetDebugOutput(), Pixels)))
			return false;
		TestTrue(TEXT("Breakup publishes a structured Gap"), HasBothExtremes(Pixels, 0.1f, 0.6f));

		int32 GapPieceDifferences = 0;
		for (int32 Index = 0; Index < Pixels.Num() && Index < PiecePixels.Num(); ++Index)
		{
			GapPieceDifferences += FMath::IsNearlyEqual(
				DebugValue(Pixels[Index]), DebugValue(PiecePixels[Index]), 1.0e-3f) ? 0 : 1;
		}
		TestTrue(TEXT("Gap and Pieces are different maps"), GapPieceDifferences > 0);
	}

	// Gap with the grout closed. The explicit contract: GapWidth 0 is a zero mask, not a
	// hairline. Edge deliberately does not follow it -- a breakup with closed grout still has
	// edges -- and checking that here is what stops a globally zeroed output passing by accident.
	{
		TArray<FMixtormatLayer> Layers = BuildLayers(FName(TEXT("Gap")), 0.0f);
		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Breakup zero Gap composes"),
				ComposeAndWait(Compositor, Layers, MaskDebug))
			|| !TestTrue(TEXT("Breakup zero Gap reads"),
				ReadTarget(Compositor.GetDebugOutput(), Pixels)))
			return false;

		float MaximumGap = 0.0f;
		for (const FLinearColor& Pixel : Pixels)
		{
			MaximumGap = FMath::Max(MaximumGap, DebugValue(Pixel));
		}
		TestTrue(TEXT("GapWidth 0 publishes a zero mask"), MaximumGap <= 0.02f);

		TArray<FMixtormatLayer> EdgeLayers = BuildLayers(FName(TEXT("Edge")), 0.0f);
		TArray<FLinearColor> EdgePixels;
		if (!TestTrue(TEXT("Breakup Edge at zero gap composes"),
				ComposeAndWait(Compositor, EdgeLayers, MaskDebug))
			|| !TestTrue(TEXT("Breakup Edge at zero gap reads"),
				ReadTarget(Compositor.GetDebugOutput(), EdgePixels)))
			return false;
		TestTrue(TEXT("Edge survives a closed gap"), HasBothExtremes(EdgePixels, 0.1f, 0.6f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupRegionIdsTest,
	"Mixtormat.Compositor.BreakupRegionIds",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBreakupRegionIdsTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	// Breakup -> Random From IDs, the chain the piece IDs exist for. A consumer below Breakup has
	// to find its map through the ordinary nearest-producer rule, at Amount 0 like everything else.
	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
		return false;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
		return false;

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;

	FMixtormatLayerChild BreakupChild;
	BreakupChild.Type = EMixtormatLayerChildType::Effect;
	BreakupChild.Effect.ProceduralType = EMixtormatEffectType::Breakup;
	BreakupChild.Effect.BreakupAmount = 0.0f;
	BreakupChild.Effect.BreakupMaskTexture =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
	Layer.Children.Add(BreakupChild);

	FMixtormatLayerChild RandomChild;
	RandomChild.Type = EMixtormatLayerChildType::RandomId;
	RandomChild.RandomId.bEnabled = true;
	RandomChild.RandomId.BlendMode = EMixtormatMaskBlendMode::Replace;
	Layer.Children.Add(RandomChild);

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	FMixtormatDebugPreviewSettings MaskDebug = LayerMaskDebug();
	MaskDebug.ChildIndex = 1;

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Breakup to Random From IDs composes"),
			ComposeAndWait(Compositor, Layers, MaskDebug))
		|| !TestTrue(TEXT("Random From IDs reads"),
			ReadTarget(Compositor.GetDebugOutput(), Pixels)))
		return false;

	// Per-piece random values. Flat means the consumer found no ID map and fell through, which is
	// exactly the regression this covers.
	TestTrue(TEXT("Breakup piece IDs drive Random From IDs at Amount 0"),
		HasBothExtremes(Pixels, 0.15f, 0.6f));

	// Reseeding the breakup must reshuffle the pieces, or the IDs are not keyed to the field.
	Layers[0].Children[0].Effect.BreakupSeed += 17;
	TArray<FLinearColor> Reseeded;
	if (!TestTrue(TEXT("Reseeded Breakup composes"), ComposeAndWait(Compositor, Layers, MaskDebug))
		|| !TestTrue(TEXT("Reseeded reads"), ReadTarget(Compositor.GetDebugOutput(), Reseeded)))
		return false;

	int32 SeedDifferences = 0;
	for (int32 Index = 0; Index < Pixels.Num() && Index < Reseeded.Num(); ++Index)
	{
		SeedDifferences += FMath::IsNearlyEqual(
			DebugValue(Pixels[Index]), DebugValue(Reseeded[Index]), 1.0e-3f) ? 0 : 1;
	}
	TestTrue(TEXT("Breakup seed reshuffles the published IDs"), SeedDifferences > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupCombineIdsTest,
	"Mixtormat.Compositor.BreakupCombineIds",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBreakupCombineIdsTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
		return false;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
		return false;

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;

	// An older producer makes the silent-stale-map regression observable: Combine must consume
	// Breakup immediately above it, not this Pattern that happened to run in the early prepass.
	FMixtormatLayerChild PatternChild;
	PatternChild.Type = EMixtormatLayerChildType::PatternId;
	Layer.Children.Add(PatternChild);

	FMixtormatLayerChild BreakupChild;
	BreakupChild.Type = EMixtormatLayerChildType::Effect;
	BreakupChild.Effect.ProceduralType = EMixtormatEffectType::Breakup;
	BreakupChild.Effect.BreakupAmount = 0.0f;
	BreakupChild.Effect.BreakupGapWidth = 0.0f;
	BreakupChild.Effect.BreakupMaskTexture =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
	Layer.Children.Add(BreakupChild);

	FMixtormatLayerChild CombineChild;
	CombineChild.Type = EMixtormatLayerChildType::CombineId;
	CombineChild.CombineId.Amount = 0.0f; // Exact pass-through exposes which producer was selected.
	Layer.Children.Add(CombineChild);

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);
	const auto RegionPreview = [&Layer](const FMixtormatLayerChild& Child)
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
		Debug.ChildTarget.OwnerId = Layer.LayerId;
		Debug.ChildTarget.ChildId = Child.ChildId;
		Debug.ChildTarget.Kind = EMixtormatPreviewOutputKind::RegionIds;
		return Debug;
	};

	TArray<FLinearColor> BreakupPixels;
	if (!TestTrue(TEXT("Breakup preview composes"),
			ComposeAndWait(Compositor, Layers, RegionPreview(Layers[0].Children[1])))
		|| !TestTrue(TEXT("Breakup preview reads"),
			ReadTarget(Compositor.GetDebugOutput(), BreakupPixels)))
		return false;

	TArray<FLinearColor> CombinePixels;
	if (!TestTrue(TEXT("Breakup to Combine IDs composes"),
			ComposeAndWait(Compositor, Layers, RegionPreview(Layers[0].Children[2])))
		|| !TestTrue(TEXT("Combine IDs preview reads"),
			ReadTarget(Compositor.GetDebugOutput(), CombinePixels)))
		return false;

	int32 ComparedRegions = 0;
	int32 Differences = 0;
	for (int32 Index = 0; Index < BreakupPixels.Num() && Index < CombinePixels.Num(); ++Index)
	{
		// Breakup uses ID 0 for empty space; Combine correctly previews that as black.
		const FLinearColor& Combined = CombinePixels[Index];
		if (Combined.R <= 1.0e-5f && Combined.G <= 1.0e-5f && Combined.B <= 1.0e-5f)
		{
			continue;
		}
		++ComparedRegions;
		Differences += BreakupPixels[Index].Equals(Combined, 1.0e-5f) ? 0 : 1;
	}
	TestTrue(TEXT("Breakup IDs are structured"), HasBothExtremes(BreakupPixels, 0.1f, 0.6f));
	TestTrue(TEXT("Combine receives Breakup regions"), ComparedRegions > 0);
	TestEqual(TEXT("Combine consumes the nearest Breakup map"), Differences, 0);

	// Also cover Breakup as the only producer; removing Pattern must not make Combine unavailable.
	Layers[0].Children.RemoveAt(0);
	CombinePixels.Reset();
	if (!TestTrue(TEXT("Direct Breakup to Combine IDs composes"),
			ComposeAndWait(Compositor, Layers, RegionPreview(Layers[0].Children[1])))
		|| !TestTrue(TEXT("Direct Combine IDs preview reads"),
			ReadTarget(Compositor.GetDebugOutput(), CombinePixels)))
		return false;
	TestTrue(TEXT("Direct Breakup drives Combine IDs"),
		HasBothExtremes(CombinePixels, 0.1f, 0.6f));

	const auto CountPreviewColors = [](const TArray<FLinearColor>& Pixels)
	{
		TSet<FColor> Colors;
		for (const FLinearColor& Pixel : Pixels)
		{
			const FColor Color = Pixel.ToFColor(false);
			if (Color.R != 0 || Color.G != 0 || Color.B != 0)
			{
				Colors.Add(Color);
			}
		}
		return Colors.Num();
	};
	const int32 UncombinedColorCount = CountPreviewColors(CombinePixels);
	Layers[0].Children[1].CombineId.Amount = 1.0f;
	CombinePixels.Reset();
	if (!TestTrue(TEXT("Active Breakup Combine IDs composes"),
			ComposeAndWait(Compositor, Layers, RegionPreview(Layers[0].Children[1])))
		|| !TestTrue(TEXT("Active Combine IDs preview reads"),
			ReadTarget(Compositor.GetDebugOutput(), CombinePixels)))
		return false;
	TestTrue(TEXT("Combine merges full-width Breakup IDs"),
		CountPreviewColors(CombinePixels) < UncombinedColorCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBlurScopeTest,
	"Mixtormat.Compositor.BlurScope",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBlurScopeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}
	TStrongObjectPtr<UTexture2D> Coverage(MakeTwoToneIdMap(FColor::Black, FColor::White));
	TStrongObjectPtr<UTexture2D> White(MakeTwoToneIdMap(FColor::White, FColor::White));
	if (!TestNotNull(TEXT("Coverage exists"), Coverage.Get())
		|| !TestNotNull(TEXT("White scope exists"), White.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers;
	Layers.SetNum(2);
	for (FMixtormatLayer& Layer : Layers)
	{
		Layer.Type = EMixtormatLayerType::Fill;
		Layer.bOverrideBaseColor = true;
	}
	Layers[0].BaseColor = FLinearColor::Black;
	Layers[1].BaseColor = FLinearColor::White;
	FMixtormatLayerChild& Mask = Layers[1].Children.AddDefaulted_GetRef();
	Mask.Type = EMixtormatLayerChildType::Mask;
	Mask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Coverage.Get()));
	Mask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	FMixtormatLayerChild& Blur = Layers[1].Children.AddDefaulted_GetRef();
	Blur.Type = EMixtormatLayerChildType::Effect;
	Blur.Effect.ProceduralType = EMixtormatEffectType::LayerBlur;
	Blur.Effect.LayerBlurRadiusX = 8.0f;
	// Exercise the two-axis ping-pong path; this fixture is vertically uniform.
	Blur.Effect.LayerBlurRadiusY = 8.0f;
	const FGuid BlurId = Blur.ChildId;

	const int32 Outside = (TestResolution / 2) * TestResolution + TestResolution / 2 - 2;
	const int32 Inside = Outside + 3;
	// No scope, full Replace, and weighted Replace must all leave white independent of coverage.
	for (int32 ScopedCase = 0; ScopedCase < 3; ++ScopedCase)
	{
		if (ScopedCase == 1)
		{
			FMixtormatLayerChild& Scope = Layers[1].Children.AddDefaulted_GetRef();
			Scope.Type = EMixtormatLayerChildType::Mask;
			Scope.ScopeOwnerChildId = BlurId;
			Scope.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(White.Get()));
			Scope.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		}
		if (ScopedCase == 2)
		{
			Layers[1].Children[2].Mask.Weight = 0.5f;
		}
		for (const EMixtormatLayerBlurScope Scope :
			{EMixtormatLayerBlurScope::Layer, EMixtormatLayerBlurScope::Composite})
		{
			Layers[1].Children[1].Effect.LayerBlurScope = Scope;
			TArray<FLinearColor> Pixels;
			if (!TestTrue(TEXT("Blur composes"),
				ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings()))
				|| !TestTrue(TEXT("Blur reads back"),
					ReadTarget(Compositor.GetBaseColorOutput(), Pixels)))
			{
				return false;
			}
			TestTrue(FString::Printf(TEXT("Case %d: covered edge blurs"), ScopedCase),
				Pixels[Inside].R < 0.95f);
			TestTrue(FString::Printf(TEXT("Case %d: scope gates uncovered edge"), ScopedCase),
				Scope == EMixtormatLayerBlurScope::Layer
					? FMath::IsNearlyZero(Pixels[Outside].R, 0.01f)
					: Pixels[Outside].R > 0.1f);
		}
	}
	// A black scoped mask must suppress blur even in Whole Composite mode.
	Layers[1].Children[2].Mask.Weight = 1.0f;
	Layers[1].Children[2].Mask.Shaping.bInvert = true;
	TArray<FLinearColor> MaskedPixels;
	if (!TestTrue(TEXT("Black scoped blur composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings()))
		|| !TestTrue(TEXT("Black scoped blur reads back"),
			ReadTarget(Compositor.GetBaseColorOutput(), MaskedPixels)))
	{
		return false;
	}
	TestTrue(TEXT("Scoped mask excludes uncovered edge"),
		FMath::IsNearlyZero(MaskedPixels[Outside].R, 0.01f));
	TestTrue(TEXT("Scoped mask excludes covered edge"),
		FMath::IsNearlyEqual(MaskedPixels[Inside].R, 1.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatScopedGradeMaskTest,
	"Mixtormat.Compositor.ScopedGradeMask",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatScopedGradeMaskTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> ScopeMask(
		MakeTwoToneIdMap(FColor::Black, FColor::White));
	if (!TestNotNull(TEXT("Two-tone scoped mask exists"), ScopeMask.Get()))
	{
		return false;
	}

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;
	Layer.bOverrideBaseColor = true;
	Layer.BaseColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);

	FMixtormatLayerChild& ScopedGrade = Layer.Children.AddDefaulted_GetRef();
	ScopedGrade.Type = EMixtormatLayerChildType::Effect;
	ScopedGrade.Effect.ProceduralType = EMixtormatEffectType::Grade;
	ScopedGrade.Effect.GradeBrightness = 2.0f;
	const FGuid ScopedGradeId = ScopedGrade.ChildId;

	FMixtormatLayerChild& Mask = Layer.Children.AddDefaulted_GetRef();
	Mask.Type = EMixtormatLayerChildType::Mask;
	Mask.ScopeOwnerChildId = ScopedGradeId;
	Mask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(ScopeMask.Get()));

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);
	if (!TestTrue(TEXT("Scoped Grade composes without a layer mask"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ScopedPixels;
	if (!TestTrue(TEXT("Scoped Grade base color reads back"),
		ReadTarget(Compositor.GetBaseColorOutput(), ScopedPixels)))
	{
		return false;
	}

	const int32 Row = TestResolution / 2;
	const int32 LeftIndex = Row * TestResolution + TestResolution / 4;
	const int32 RightIndex = Row * TestResolution + (3 * TestResolution) / 4;
	TestTrue(TEXT("Scoped Grade leaves masked-out pixels unchanged"),
		FMath::IsNearlyEqual(ScopedPixels[LeftIndex].R, 0.25f, 0.01f));
	TestTrue(TEXT("Scoped Grade changes only masked-in pixels"),
		FMath::IsNearlyEqual(ScopedPixels[RightIndex].R, 0.50f, 0.01f));

	FMixtormatLayerChild& LaterGrade = Layers[0].Children.AddDefaulted_GetRef();
	LaterGrade.Type = EMixtormatLayerChildType::Effect;
	LaterGrade.Effect.ProceduralType = EMixtormatEffectType::Grade;
	LaterGrade.Effect.GradeBrightness = 0.5f;
	if (!TestTrue(TEXT("Later unscoped Grade composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> LaterPixels;
	if (!TestTrue(TEXT("Later Grade base color reads back"),
		ReadTarget(Compositor.GetBaseColorOutput(), LaterPixels)))
	{
		return false;
	}
	TestTrue(TEXT("Later sibling receives unchanged global mask on the left"),
		FMath::IsNearlyEqual(LaterPixels[LeftIndex].R, 0.125f, 0.01f));
	TestTrue(TEXT("Later sibling receives unchanged global mask on the right"),
		FMath::IsNearlyEqual(LaterPixels[RightIndex].R, 0.25f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatWornEdgesRoughnessTest,
	"Mixtormat.Compositor.WornEdgesRoughness",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatWornEdgesRoughnessTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> RAMH(MakeTwoBandRAMH());
	TStrongObjectPtr<UMixtormatSurface> Surface(MakeSurfaceWithRAMH(RAMH.Get()));
	if (!TestNotNull(TEXT("Worn Edges RAMH fixture exists"), RAMH.Get())
		|| !TestNotNull(TEXT("Worn Edges test surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	Layer.bOverrideBaseColor = true;
	Layer.BaseColor = FLinearColor(0.30f, 0.30f, 0.30f, 1.0f);
	Layer.bOverrideRoughness = true;
	Layer.Roughness = 0.5f;

	FMixtormatLayerChild& Cluster = Layer.Children.AddDefaulted_GetRef();
	Cluster.Type = EMixtormatLayerChildType::Filter;
	Cluster.Filter.Threshold = 0.33f;
	Cluster.Filter.HeightInfluence = 1.0f;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);
	if (!TestTrue(TEXT("Worn Edges reference composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ReferenceRAM;
	TArray<FLinearColor> ReferenceBaseColor;
	if (!TestTrue(TEXT("Reference RAM reads back"),
		ReadTarget(Compositor.GetRAMOutput(), ReferenceRAM))
		|| !TestTrue(TEXT("Reference base color reads back"),
			ReadTarget(Compositor.GetBaseColorOutput(), ReferenceBaseColor)))
	{
		return false;
	}

	FMixtormatLayerChild& Worn = Layers[0].Children.AddDefaulted_GetRef();
	Worn.Type = EMixtormatLayerChildType::Effect;
	Worn.Effect.ProceduralType = EMixtormatEffectType::WornEdges;
	Worn.Effect.EdgeWearRadius = 8;
	Worn.Effect.EdgeWearDirections = 8;
	Worn.Effect.EdgeWearStrength = 1.0f;
	Worn.Effect.EdgeWearRoughnessWeight = 0.0f;
	Worn.Effect.EdgeWearRoughnessOffset = 1.0f;
	if (!TestTrue(TEXT("Weight-zero Worn Edges composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ZeroWeightRAM;
	if (!TestTrue(TEXT("Weight-zero RAM reads back"),
		ReadTarget(Compositor.GetRAMOutput(), ZeroWeightRAM)))
	{
		return false;
	}
	int32 ZeroWeightDifferences = 0;
	for (int32 Index = 0; Index < ReferenceRAM.Num(); ++Index)
	{
		ZeroWeightDifferences += ZeroWeightRAM[Index].R == ReferenceRAM[Index].R ? 0 : 1;
	}
	TestEqual(TEXT("Worn roughness weight zero is the identity"), ZeroWeightDifferences, 0);

	Layers[0].Children.Last().Effect.EdgeWearRoughnessWeight = 1.0f;
	Layers[0].Children.Last().Effect.EdgeWearRoughnessOffset = 0.25f;
	if (!TestTrue(TEXT("Positive Worn roughness composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}
	TArray<FLinearColor> PositiveRAM;
	TArray<FLinearColor> PositiveBaseColor;
	if (!TestTrue(TEXT("Positive Worn RAM reads back"),
		ReadTarget(Compositor.GetRAMOutput(), PositiveRAM))
		|| !TestTrue(TEXT("Positive Worn base color reads back"),
			ReadTarget(Compositor.GetBaseColorOutput(), PositiveBaseColor)))
	{
		return false;
	}

	Layers[0].Children.Last().Effect.EdgeWearRoughnessOffset = -0.25f;
	if (!TestTrue(TEXT("Negative Worn roughness composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}
	TArray<FLinearColor> NegativeRAM;
	if (!TestTrue(TEXT("Negative Worn RAM reads back"),
		ReadTarget(Compositor.GetRAMOutput(), NegativeRAM)))
	{
		return false;
	}

	int32 RougherPixels = 0;
	int32 SmootherPixels = 0;
	int32 BaseColorDifferences = 0;
	bool bSignedDeltasMatch = true;
	for (int32 Index = 0; Index < ReferenceRAM.Num(); ++Index)
	{
		const float PositiveDelta = PositiveRAM[Index].R - ReferenceRAM[Index].R;
		const float NegativeDelta = NegativeRAM[Index].R - ReferenceRAM[Index].R;
		RougherPixels += PositiveDelta > 1.0e-4f ? 1 : 0;
		SmootherPixels += NegativeDelta < -1.0e-4f ? 1 : 0;
		if (FMath::Abs(PositiveDelta) > 1.0e-4f || FMath::Abs(NegativeDelta) > 1.0e-4f)
		{
			bSignedDeltasMatch &= FMath::IsNearlyEqual(PositiveDelta, -NegativeDelta, 2.0e-3f);
		}
		BaseColorDifferences += PositiveBaseColor[Index].Equals(
			ReferenceBaseColor[Index], 1.0e-5f) ? 0 : 1;
	}
	TestTrue(TEXT("Positive roughness offset roughens worn pixels"), RougherPixels > 0);
	TestTrue(TEXT("Negative roughness offset smooths worn pixels"), SmootherPixels > 0);
	TestTrue(TEXT("Signed roughness offsets use the same EdgeWearMask"), bSignedDeltasMatch);
	TestEqual(TEXT("Worn Edges leaves base color unchanged"), BaseColorDifferences, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatFlowWarpTest,
	"Mixtormat.Compositor.FlowWarp",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatFlowWarpTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> BaseColor(
		MakeTwoToneIdMap(FColor(51, 51, 51), FColor(204, 204, 204)));
	TStrongObjectPtr<UTexture2D> Normal(
		MakeTwoToneIdMap(FColor(64, 128, 237), FColor(192, 128, 237)));
	TStrongObjectPtr<UTexture2D> RAMH(
		MakeTwoToneIdMap(FColor(51, 128, 0, 51), FColor(204, 128, 0, 204)));
	TStrongObjectPtr<UMixtormatSurface> Surface(
		MakeFlowWarpSurface(BaseColor.Get(), Normal.Get(), RAMH.Get()));
	if (!TestNotNull(TEXT("Flow Warp base-color fixture exists"), BaseColor.Get())
		|| !TestNotNull(TEXT("Flow Warp normal fixture exists"), Normal.Get())
		|| !TestNotNull(TEXT("Flow Warp RAMH fixture exists"), RAMH.Get())
		|| !TestNotNull(TEXT("Flow Warp surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayer Layer;
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	FMixtormatLayerChild& Flow = Layer.Children.AddDefaulted_GetRef();
	Flow.Type = EMixtormatLayerChildType::Effect;
	Flow.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
	Flow.Effect.FlowWarpAmount = 0.0f;
	Flow.Effect.FlowWarpScale = 8;
	Flow.Effect.FlowWarpDirection = 23.0f;
	Flow.Effect.FlowWarpSeed = 17;
	const FGuid FlowId = Flow.ChildId;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);
	if (!TestTrue(TEXT("Zero Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ZeroBC;
	TArray<FLinearColor> ZeroN;
	TArray<FLinearColor> ZeroRAM;
	TArray<FLinearColor> ZeroHeight;
	if (!ReadTarget(Compositor.GetBaseColorOutput(), ZeroBC)
		|| !ReadTarget(Compositor.GetNormalOutput(), ZeroN)
		|| !ReadTarget(Compositor.GetRAMOutput(), ZeroRAM)
		|| !ReadHeight(Compositor.GetHeightOutput(), ZeroHeight))
	{
		AddError(TEXT("Zero Flow Warp outputs did not read back"));
		return false;
	}

	Layers[0].Children.RemoveAt(0);
	if (!TestTrue(TEXT("Flow Warp reference composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> ReferenceBC;
	TArray<FLinearColor> ReferenceN;
	TArray<FLinearColor> ReferenceRAM;
	TArray<FLinearColor> ReferenceHeight;
	if (!ReadTarget(Compositor.GetBaseColorOutput(), ReferenceBC)
		|| !ReadTarget(Compositor.GetNormalOutput(), ReferenceN)
		|| !ReadTarget(Compositor.GetRAMOutput(), ReferenceRAM)
		|| !ReadHeight(Compositor.GetHeightOutput(), ReferenceHeight))
	{
		AddError(TEXT("Flow Warp reference outputs did not read back"));
		return false;
	}

	int32 IdentityDifferences = 0;
	for (int32 Index = 0; Index < ReferenceBC.Num(); ++Index)
	{
		IdentityDifferences += ZeroBC[Index] == ReferenceBC[Index] ? 0 : 1;
		IdentityDifferences += ZeroN[Index] == ReferenceN[Index] ? 0 : 1;
		IdentityDifferences += ZeroRAM[Index] == ReferenceRAM[Index] ? 0 : 1;
		IdentityDifferences += ZeroHeight[Index] == ReferenceHeight[Index] ? 0 : 1;
	}
	TestEqual(TEXT("Amount zero is an exact identity"), IdentityDifferences, 0);

	Flow.Effect.FlowWarpAmount = 2.0f;
	Flow.Effect.FlowWarpHeightSlopeInfluence = 1.0f;
	Flow.Effect.FlowWarpMaskSlopeInfluence = 1.0f;
	Flow.Effect.FlowWarpDerivativeKernelX = 6.0f;
	Flow.Effect.FlowWarpDerivativeKernelY = 3.0f;
	Layers[0].Children.Add(Flow);
	if (!TestTrue(TEXT("Active Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> WarpedBC;
	TArray<FLinearColor> WarpedN;
	TArray<FLinearColor> WarpedRAM;
	TArray<FLinearColor> WarpedHeight;
	if (!ReadTarget(Compositor.GetBaseColorOutput(), WarpedBC)
		|| !ReadTarget(Compositor.GetNormalOutput(), WarpedN)
		|| !ReadTarget(Compositor.GetRAMOutput(), WarpedRAM)
		|| !ReadHeight(Compositor.GetHeightOutput(), WarpedHeight))
	{
		AddError(TEXT("Active Flow Warp outputs did not read back"));
		return false;
	}

	Layers[0].Children[0].Effect.FlowWarpHeightSlopeInfluence = 0.0f;
	if (!TestTrue(TEXT("Curl-only Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}
	TArray<FLinearColor> CurlOnlyBC;
	if (!ReadTarget(Compositor.GetBaseColorOutput(), CurlOnlyBC))
	{
		AddError(TEXT("Curl-only Flow Warp did not read back"));
		return false;
	}
	int32 HeightGuidanceDifferences = 0;
	for (int32 Index = 0; Index < WarpedBC.Num(); ++Index)
	{
		HeightGuidanceDifferences += WarpedBC[Index].Equals(CurlOnlyBC[Index], 1.0e-4f) ? 0 : 1;
	}
	TestTrue(TEXT("Self-height slope changes the flow field"), HeightGuidanceDifferences > 0);
	Layers[0].Children[0].Effect.FlowWarpHeightSlopeInfluence = 1.0f;

	int32 ChangedBC = 0;
	int32 ChangedN = 0;
	int32 ChangedRAM = 0;
	int32 ChangedHeight = 0;
	int32 MisalignedBands = 0;
	for (int32 Index = 0; Index < ReferenceBC.Num(); ++Index)
	{
		ChangedBC += WarpedBC[Index].Equals(ReferenceBC[Index], 1.0e-4f) ? 0 : 1;
		ChangedN += WarpedN[Index].Equals(ReferenceN[Index], 1.0e-4f) ? 0 : 1;
		ChangedRAM += WarpedRAM[Index].Equals(ReferenceRAM[Index], 1.0e-4f) ? 0 : 1;
		ChangedHeight += FMath::IsNearlyEqual(
			WarpedHeight[Index].R, ReferenceHeight[Index].R, 1.0e-4f) ? 0 : 1;
		const bool bBCBand = WarpedBC[Index].R >= 0.5f;
		const bool bRAMBand = WarpedRAM[Index].R >= 0.5f;
		const bool bHeightBand = WarpedHeight[Index].R >= 0.5f;
		MisalignedBands += bBCBand == bRAMBand && bBCBand == bHeightBand ? 0 : 1;
	}
	TestTrue(TEXT("Flow Warp displaces base color"), ChangedBC > 0);
	TestTrue(TEXT("Flow Warp transforms normals"), ChangedN > 0);
	TestTrue(TEXT("Flow Warp displaces RAM"), ChangedRAM > 0);
	TestTrue(TEXT("Flow Warp displaces height"), ChangedHeight > 0);
	TestEqual(TEXT("BC, RAM, and height boundaries remain aligned"), MisalignedBands, 0);

	Layers[0].Children[0].Effect.FlowWarpBlendMode = EMixtormatFlowWarpBlendMode::MaxHeight;
	if (!TestTrue(TEXT("Max-height Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}
	TArray<FLinearColor> MaxHeight;
	if (!ReadHeight(Compositor.GetHeightOutput(), MaxHeight))
	{
		AddError(TEXT("Max-height Flow Warp did not read back"));
		return false;
	}
	int32 LoweredByMax = 0;
	int32 RaisedByMax = 0;
	for (int32 Index = 0; Index < ReferenceHeight.Num(); ++Index)
	{
		LoweredByMax += MaxHeight[Index].R < ReferenceHeight[Index].R - 1.0e-4f ? 1 : 0;
		RaisedByMax += MaxHeight[Index].R > ReferenceHeight[Index].R + 1.0e-4f ? 1 : 0;
	}
	TestEqual(TEXT("Max Height never lowers the surface"), LoweredByMax, 0);
	TestTrue(TEXT("Max Height keeps raised warped samples"), RaisedByMax > 0);

	Layers[0].Children[0].Effect.FlowWarpBlendMode = EMixtormatFlowWarpBlendMode::MinHeight;
	if (!TestTrue(TEXT("Min-height Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}
	TArray<FLinearColor> MinHeight;
	if (!ReadHeight(Compositor.GetHeightOutput(), MinHeight))
	{
		AddError(TEXT("Min-height Flow Warp did not read back"));
		return false;
	}
	int32 RaisedByMin = 0;
	int32 LoweredByMin = 0;
	for (int32 Index = 0; Index < ReferenceHeight.Num(); ++Index)
	{
		RaisedByMin += MinHeight[Index].R > ReferenceHeight[Index].R + 1.0e-4f ? 1 : 0;
		LoweredByMin += MinHeight[Index].R < ReferenceHeight[Index].R - 1.0e-4f ? 1 : 0;
	}
	TestEqual(TEXT("Min Height never raises the surface"), RaisedByMin, 0);
	TestTrue(TEXT("Min Height keeps lowered warped samples"), LoweredByMin > 0);
	Layers[0].Children[0].Effect.FlowWarpBlendMode = EMixtormatFlowWarpBlendMode::Replace;

	TStrongObjectPtr<UTexture2D> ScopeMask(
		MakeTwoToneIdMap(FColor::Black, FColor::White));
	if (!TestNotNull(TEXT("Flow Warp scoped mask exists"), ScopeMask.Get()))
	{
		return false;
	}
	FMixtormatLayerChild& Mask = Layers[0].Children.AddDefaulted_GetRef();
	Mask.Type = EMixtormatLayerChildType::Mask;
	Mask.ScopeOwnerChildId = FlowId;
	Mask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(ScopeMask.Get()));
	if (!TestTrue(TEXT("Masked Flow Warp composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
	{
		return false;
	}

	TArray<FLinearColor> MaskedBC;
	if (!TestTrue(TEXT("Masked Flow Warp base color reads back"),
		ReadTarget(Compositor.GetBaseColorOutput(), MaskedBC)))
	{
		return false;
	}
	int32 UnmaskedDifferences = 0;
	int32 MaskedDifferences = 0;
	for (int32 Y = 0; Y < TestResolution; ++Y)
	{
		for (int32 X = 0; X < TestResolution; ++X)
		{
			const int32 Index = Y * TestResolution + X;
			if (X < TestResolution / 2 - 2)
			{
				UnmaskedDifferences += MaskedBC[Index].Equals(ReferenceBC[Index], 1.0e-5f) ? 0 : 1;
			}
			else if (X > TestResolution / 2 + 2)
			{
				MaskedDifferences += MaskedBC[Index].Equals(ReferenceBC[Index], 1.0e-4f) ? 0 : 1;
			}
		}
	}
	TestEqual(TEXT("Masked-out pixels remain unchanged"), UnmaskedDifferences, 0);
	TestTrue(TEXT("Masked-in pixels are displaced"), MaskedDifferences > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatScopedFeatureMaskDataTest,
	"Mixtormat.Compositor.ScopedFeatureMaskData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatScopedFeatureMaskDataTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FMixtormatLayer Layer;
	FMixtormatLayerChild& Owner = Layer.Children.AddDefaulted_GetRef();
	Owner.Type = EMixtormatLayerChildType::Effect;
	Owner.Effect.ProceduralType = EMixtormatEffectType::WornEdges;
	const FGuid OriginalOwnerId = Owner.ChildId;

	FMixtormatLayerChild& ScopedMask = Layer.Children.AddDefaulted_GetRef();
	ScopedMask.Type = EMixtormatLayerChildType::Mask;
	ScopedMask.ScopeOwnerChildId = OriginalOwnerId;
	TestEqual(TEXT("Scoped mask uses existing weight"), ScopedMask.Mask.Weight, 1.0f);
	TestEqual(TEXT("Scoped mask uses existing balance"), ScopedMask.Mask.Shaping.Balance, 0.5f);
	TestEqual(TEXT("Scoped mask uses existing contrast"), ScopedMask.Mask.Shaping.Contrast, 1.0f);
	TestEqual(TEXT("Scoped mask uses existing offset"), ScopedMask.Mask.Shaping.Offset, 0.0f);
	TestFalse(TEXT("Scoped mask invert defaults off"), ScopedMask.Mask.Shaping.bInvert);

	TestEqual(
		TEXT("Worn roughness weight defaults neutral"),
		Layer.Children[0].Effect.EdgeWearRoughnessWeight,
		0.0f);
	TestEqual(
		TEXT("Worn roughness offset defaults neutral"),
		Layer.Children[0].Effect.EdgeWearRoughnessOffset,
		0.0f);

	MixtormatParameterBinding::RegenerateLayerIdentity(Layer);
	TestTrue(TEXT("Duplicated owner receives a new identity"), Layer.Children[0].ChildId != OriginalOwnerId);
	TestEqual(
		TEXT("Duplicated scoped mask follows the duplicated owner"),
		Layer.Children[1].ScopeOwnerChildId,
		Layer.Children[0].ChildId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCompositionIdentityRemapTest,
	"Mixtormat.Compositor.CompositionIdentityRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatCompositionIdentityRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FMixtormatLayer> Layers;
	FMixtormatLayer& SourceLayer = Layers.AddDefaulted_GetRef();
	FMixtormatLayerChild& SourceChild = SourceLayer.Children.AddDefaulted_GetRef();
	const FGuid OldSourceLayerId = SourceLayer.LayerId;
	const FGuid OldSourceChildId = SourceChild.ChildId;

	FMixtormatLayer& DestinationLayer = Layers.AddDefaulted_GetRef();
	FMixtormatLayerChild& DestinationChild = DestinationLayer.Children.AddDefaulted_GetRef();
	DestinationChild.SourceLayerId = OldSourceLayerId;
	DestinationChild.SourceChildId = OldSourceChildId;
	DestinationChild.Mask.PublishedSourceLayerId = OldSourceLayerId;
	DestinationChild.Mask.PublishedSourceChildId = OldSourceChildId;
	FMixtormatParameterBinding& Binding = DestinationChild.ParameterBindings.AddDefaulted_GetRef();
	Binding.Reference.Source.LayerId = OldSourceLayerId;
	Binding.Reference.Source.ChildId = OldSourceChildId;
	Binding.Driver.SourceLayerId = OldSourceLayerId;
	Binding.Driver.SourceChildId = OldSourceChildId;

	MixtormatParameterBinding::RegenerateLayerIdentities(Layers);

	TestTrue(TEXT("Imported source layer receives a new identity"), Layers[0].LayerId != OldSourceLayerId);
	TestTrue(TEXT("Imported source child receives a new identity"), Layers[0].Children[0].ChildId != OldSourceChildId);
	TestEqual(TEXT("Instance follows imported layer"), Layers[1].Children[0].SourceLayerId, Layers[0].LayerId);
	TestEqual(TEXT("Instance follows imported child"), Layers[1].Children[0].SourceChildId, Layers[0].Children[0].ChildId);
	TestEqual(TEXT("Published mask follows imported layer"), Layers[1].Children[0].Mask.PublishedSourceLayerId, Layers[0].LayerId);
	TestEqual(TEXT("Published mask follows imported child"), Layers[1].Children[0].Mask.PublishedSourceChildId, Layers[0].Children[0].ChildId);
	TestEqual(TEXT("Reference follows imported layer"), Layers[1].Children[0].ParameterBindings[0].Reference.Source.LayerId, Layers[0].LayerId);
	TestEqual(TEXT("Driver follows imported child"), Layers[1].Children[0].ParameterBindings[0].Driver.SourceChildId, Layers[0].Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerHeightSmoothTest,
	"Mixtormat.Compositor.LayerHeightSmooth",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatLayerHeightSmoothTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!Compositor.Initialize(FIntPoint(TestResolution, TestResolution)))
	{
		AddError(TEXT("Compositor did not initialise"));
		return false;
	}
	TStrongObjectPtr<UTexture2D> RAMH(MakeTwoBandRAMH());
	TStrongObjectPtr<UMixtormatSurface> Surface(MakeSurfaceWithRAMH(RAMH.Get()));
	if (!RAMH.Get() || !Surface.Get())
	{
		AddError(TEXT("Height Smooth fixture was not created"));
		return false;
	}
	Surface->bHasBlendHeight = true;
	Surface->BlendHeightProvenance = EMixtormatBlendHeightProvenance::AuthoredRAMH;

	FMixtormatLayer Layer;
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	TArray<FMixtormatLayer> Layers = {Layer};
	TArray<FLinearColor> Sharp;
	if (!ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())
		|| !ReadHeight(Compositor.GetHeightOutput(), Sharp))
	{
		AddError(TEXT("Sharp height did not compose"));
		return false;
	}

	Layers[0].HeightSmooth = 8.0f;
	TArray<FLinearColor> Smooth;
	if (!ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())
		|| !ReadHeight(Compositor.GetHeightOutput(), Smooth))
	{
		AddError(TEXT("Smoothed height did not compose"));
		return false;
	}
	int32 ChangedBoundaryPixels = 0;
	int32 ChangedInteriorPixels = 0;
	for (int32 Y = 0; Y < TestResolution; ++Y)
	{
		for (int32 X = 0; X < TestResolution; ++X)
		{
			const int32 Index = Y * TestResolution + X;
			const bool bBoundary = FMath::Abs(X - TestResolution / 2) <= 8
				|| X <= 8 || X >= TestResolution - 9;
			const bool bChanged = !FMath::IsNearlyEqual(Sharp[Index].R, Smooth[Index].R, 1.0e-3f);
			ChangedBoundaryPixels += bBoundary && bChanged ? 1 : 0;
			ChangedInteriorPixels += !bBoundary && bChanged ? 1 : 0;
		}
	}
	TestTrue(TEXT("Height Smooth softens height boundaries"), ChangedBoundaryPixels > 0);
	TestEqual(TEXT("Height Smooth preserves flat interiors"), ChangedInteriorPixels, 0);
	return true;
}

#endif
