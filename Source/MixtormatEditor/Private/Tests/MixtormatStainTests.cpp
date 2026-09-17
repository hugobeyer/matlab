// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

// Cover for Wet Stain, which solves on its own grid and resolves up.
//
// The solve is the one pass in the plugin whose result is supposed to be the same size at every
// output resolution, and the only way to say that is to run it at more than one and compare. The
// rest of these read the same debug preview to pin the three things a reduced-resolution solve is
// easiest to break while still compiling: which texels a Liquid Mask places liquid on, what
// tiling does to that mask, and which way gravity runs.
//
// Everything reads back through the Stain feature preview, which the resolve writes at
// composition resolution before the composite runs -- the composite is told to leave the target
// alone in this mode, so what comes back is the solve and nothing layered over it.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatStainTests
{
	// The fixture masks are authored once at this size and sampled by UV, so they say the same
	// thing whatever the composition or the solve is doing. That is the point: a resolution
	// comparison whose input changes with the resolution proves nothing.
	constexpr int32 MaskResolution = 512;

	// Enough liquid for the wet mask to reach a value a threshold can see. The shipped default of
	// 0.12 resolves to a few percent coverage, which is a fine stain and a terrible assertion.
	constexpr float TestSourceAmount = 1.0f;

	// Above this a texel counts as covered. Well clear of both the solve's noise floor and the
	// bilinear skirt the resolve leaves around an edge.
	constexpr float CoveredAbove = 0.15f;

	// A single-channel fixture, uncompressed and linear, painted by a predicate over UV. White
	// where the predicate holds and black everywhere else: a Liquid Mask is read as coverage, so
	// a hard edge is the honest fixture and a gradient would let a wrong tiling pass.
	template <typename PredicateType>
	UTexture2D* MakeMask(PredicateType&& Predicate)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(
			MaskResolution, MaskResolution, PF_B8G8R8A8);
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
		for (int32 Y = 0; Y < MaskResolution; ++Y)
		{
			const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(MaskResolution);
			for (int32 X = 0; X < MaskResolution; ++X)
			{
				const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(MaskResolution);
				const uint8 Value = Predicate(U, V) ? 255 : 0;
				Pixels[Y * MaskResolution + X] = FColor(Value, Value, Value, 255);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// Two flat Fill layers: one to be the surface underneath, one to carry the stain.
	//
	// Two rather than one because the stain reads the surface accumulated below it, and on the
	// bottom layer there is none -- SurfaceValid is off there and the auto source is unavailable.
	// Flat because these tests are about where liquid goes, not about what the geometry does to
	// it: with no normal and no height the surface-following term has nothing to bend, which
	// leaves gravity and the mask as the only things steering a run.
	TArray<FMixtormatLayer> MakeStainLayers(UTexture2D* SourceMask)
	{
		FMixtormatLayer Base;
		Base.Type = EMixtormatLayerType::Fill;
		Base.bEnabled = true;
		Base.bOverrideBaseColor = true;
		Base.BaseColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
		Base.bOverrideRoughness = true;
		Base.Roughness = 0.5f;

		FMixtormatLayer Stained;
		Stained.Type = EMixtormatLayerType::Fill;
		Stained.bEnabled = true;
		Stained.bOverrideBaseColor = true;
		Stained.BaseColor = FLinearColor(0.60f, 0.30f, 0.10f, 1.0f);
		Stained.bOverrideRoughness = true;
		Stained.Roughness = 0.8f;

		FMixtormatLayerChild& Child = Stained.Children.AddDefaulted_GetRef();
		Child.Type = EMixtormatLayerChildType::Effect;
		Child.Effect.ProceduralType = EMixtormatEffectType::Stain;
		Child.Effect.Strength = 1.0f;
		Child.Effect.StainMode = EMixtormatStainMode::Wet;
		Child.Effect.StainSourceAmount = TestSourceAmount;
		Child.Effect.StainIterations = 20;
		Child.Effect.StainGravity = -1.0f;
		Child.Effect.StainSourceMaskTexture =
			TSoftObjectPtr<UTexture2D>(FSoftObjectPath(SourceMask));
		Child.Effect.StainSourceMaskTiling = 1;

		// The auto source silenced outright. Every one of these weights reads the surface below
		// and adds liquid of its own, which would put wet texels outside the mask and make the
		// confinement and tiling assertions below say nothing about the mask at all.
		Child.Effect.StainConcavityWeight = 0.0f;
		Child.Effect.StainConvexityWeight = 0.0f;
		Child.Effect.StainOcclusionWeight = 0.0f;
		Child.Effect.StainHeightWeight = 0.0f;
		Child.Effect.StainSlopeWeight = 0.0f;

		// Surface following has nothing to follow on a flat fill, but pinning it at zero says so
		// deliberately rather than relying on the fixture staying flat.
		Child.Effect.StainSurfaceFollow = 0.0f;

		TArray<FMixtormatLayer> Layers;
		Layers.Add(Base);
		Layers.Add(Stained);
		return Layers;
	}

	FMixtormatDebugPreviewSettings StainDebug()
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::Stain;

		// The stained layer, not the base. The resolve gates its debug write on this index so two
		// stains on different layers cannot fight over one target.
		Debug.LayerIndex = 1;
		Debug.ChildIndex = 0;
		return Debug;
	}

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

	bool ReadTarget(
		UTextureRenderTarget2D* Target,
		const int32 Resolution,
		TArray<FLinearColor>& OutPixels)
	{
		if (!Target)
		{
			return false;
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		return Resource && Resource->ReadLinearColorPixels(OutPixels)
			&& OutPixels.Num() == Resolution * Resolution;
	}

	// Undo the preview ramp. It lerps two sRGB constants and converts the result to linear, and
	// the target holds the linear side, so recovering the value means going back through the
	// encode rather than interpolating the linear endpoints -- the conversion is per-channel and
	// nonlinear, and treating it as a straight line reads a third of the range as zero.
	float ToSrgbChannel(const float Linear)
	{
		return Linear <= 0.0031308f
			? Linear * 12.92f
			: 1.055f * FMath::Pow(FMath::Max(Linear, 0.0f), 1.0f / 2.4f) - 0.055f;
	}

	// Green is the channel that moves: the ramp runs it from 0.02 to 0.25 while red barely
	// changes, which is why sampling red says nothing.
	float Coverage(const FLinearColor& Pixel)
	{
		constexpr float Low = 0.02f;
		constexpr float High = 0.25f;
		return FMath::Clamp((ToSrgbChannel(Pixel.G) - Low) / (High - Low), 0.0f, 1.0f);
	}

	// Mean coverage over a column band, given in normalized UV so a caller reads the same band at
	// every resolution.
	float ColumnCoverage(
		const TArray<FLinearColor>& Pixels,
		const int32 Resolution,
		const float MinU,
		const float MaxU)
	{
		const int32 FirstX = FMath::Clamp(
			FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
		const int32 LastX = FMath::Clamp(
			FMath::CeilToInt(MaxU * Resolution) - 1, FirstX, Resolution - 1);
		double Total = 0.0;
		int32 Count = 0;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = FirstX; X <= LastX; ++X)
			{
				Total += Coverage(Pixels[Y * Resolution + X]);
				++Count;
			}
		}
		return Count > 0 ? static_cast<float>(Total / Count) : 0.0f;
	}

	float RowCoverage(
		const TArray<FLinearColor>& Pixels,
		const int32 Resolution,
		const float MinV,
		const float MaxV)
	{
		const int32 FirstY = FMath::Clamp(
			FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
		const int32 LastY = FMath::Clamp(
			FMath::CeilToInt(MaxV * Resolution) - 1, FirstY, Resolution - 1);
		double Total = 0.0;
		int32 Count = 0;
		for (int32 Y = FirstY; Y <= LastY; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				Total += Coverage(Pixels[Y * Resolution + X]);
				++Count;
			}
		}
		return Count > 0 ? static_cast<float>(Total / Count) : 0.0f;
	}

	// The furthest point the run reached, in UV. The scale-invariance claim is about distance
	// travelled, so this is the number the resolution comparison turns on.
	//
	// Furthest row rather than the span between the first and the last: the span includes the
	// source band, which is a fixed fraction of the image at every resolution and swamps the few
	// percent the run adds on the end. A metric the fixture dominates cannot fail, and a test
	// that cannot fail is worse than no test.
	float ReachV(const TArray<FLinearColor>& Pixels, const int32 Resolution)
	{
		for (int32 Y = Resolution - 1; Y >= 0; --Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (Coverage(Pixels[Y * Resolution + X]) > CoveredAbove)
				{
					return (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution);
				}
			}
		}
		return 0.0f;
	}

	// Coverage-weighted row centroid, in UV. Which way a run went, in one number.
	float CoverageCentroidV(const TArray<FLinearColor>& Pixels, const int32 Resolution)
	{
		double Weighted = 0.0;
		double Total = 0.0;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			const double V = (static_cast<double>(Y) + 0.5) / static_cast<double>(Resolution);
			for (int32 X = 0; X < Resolution; ++X)
			{
				const double Value = Coverage(Pixels[Y * Resolution + X]);
				Weighted += Value * V;
				Total += Value;
			}
		}
		return Total > 0.0 ? static_cast<float>(Weighted / Total) : 0.0f;
	}

	bool WithinRelative(const float A, const float B, const float Tolerance)
	{
		const float Scale = FMath::Max(FMath::Max(FMath::Abs(A), FMath::Abs(B)), 1.0e-4f);
		return FMath::Abs(A - B) / Scale <= Tolerance;
	}
}

// A Liquid Mask has to actually confine liquid. The solve is what decides which texels get any,
// and it now decides that on a coarser grid than the mask is sampled at, so this is the assertion
// that says the reduced-resolution source injection still lands where the mask says.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainSourceMaskTest,
	"Mixtormat.Stain.SourceMask",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainSourceMaskTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// A vertical stripe down the middle. Runs travel along the stripe, so the columns either side
	// of it stay dry unless something is placing liquid the mask did not ask for.
	TStrongObjectPtr<UTexture2D> Mask(MakeMask(
		[](const float U, const float) { return U >= 0.40f && U <= 0.60f; }));
	if (!TestNotNull(TEXT("Stripe Liquid Mask fixture exists"), Mask.Get()))
	{
		return false;
	}

	const TArray<FMixtormatLayer> Layers = MakeStainLayers(Mask.Get());
	if (!TestTrue(TEXT("Masked stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
	{
		return false;
	}

	const float Inside = ColumnCoverage(Pixels, Resolution, 0.42f, 0.58f);
	const float LeftOutside = ColumnCoverage(Pixels, Resolution, 0.00f, 0.15f);
	const float RightOutside = ColumnCoverage(Pixels, Resolution, 0.85f, 1.00f);

	TestTrue(
		FString::Printf(TEXT("Liquid Mask places liquid inside the stripe (%.3f)"), Inside),
		Inside > 0.20f);
	TestTrue(
		FString::Printf(
			TEXT("Liquid Mask keeps liquid off the left of the stripe (%.3f)"), LeftOutside),
		LeftOutside < 0.05f);
	TestTrue(
		FString::Printf(
			TEXT("Liquid Mask keeps liquid off the right of the stripe (%.3f)"), RightOutside),
		RightOutside < 0.05f);
	TestTrue(
		TEXT("Masked coverage stands well clear of the unmasked surface"),
		Inside > 4.0f * FMath::Max(LeftOutside, RightOutside));

	return true;
}

// Tiling multiplies the mask's UV before the frac, so raising it repeats the mask across the
// surface. The mask is sampled inside the solve, which now runs on a different grid from the
// composition, and a tiling read against the wrong grid is exactly the sort of thing that still
// produces a plausible-looking stain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainMaskTilingTest,
	"Mixtormat.Stain.MaskTiling",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainMaskTilingTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// White down the leftmost fifth only. At tiling 1 that is one band at the left edge; at
	// tiling 3 the same fifth repeats at [0, 0.067], [0.333, 0.400] and [0.667, 0.733].
	TStrongObjectPtr<UTexture2D> Mask(MakeMask(
		[](const float U, const float) { return U <= 0.20f; }));
	if (!TestNotNull(TEXT("Left-band Liquid Mask fixture exists"), Mask.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeStainLayers(Mask.Get());
	if (!TestTrue(TEXT("Untiled stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Untiled;
	if (!TestTrue(TEXT("Untiled stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Untiled)))
	{
		return false;
	}

	Layers[1].Children[0].Effect.StainSourceMaskTiling = 3;
	if (!TestTrue(TEXT("Tiled stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Tiled;
	if (!TestTrue(TEXT("Tiled stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Tiled)))
	{
		return false;
	}

	// Inside the second repeat at tiling 3, outside the single band at tiling 1.
	const float RepeatUntiled = ColumnCoverage(Untiled, Resolution, 0.35f, 0.39f);
	const float RepeatTiled = ColumnCoverage(Tiled, Resolution, 0.35f, 0.39f);

	// Between repeats at tiling 3, and outside the band at tiling 1, so dry in both.
	const float GapUntiled = ColumnCoverage(Untiled, Resolution, 0.23f, 0.27f);
	const float GapTiled = ColumnCoverage(Tiled, Resolution, 0.23f, 0.27f);

	// The original band, wet either way -- the first repeat still starts at the left edge.
	const float OriginUntiled = ColumnCoverage(Untiled, Resolution, 0.01f, 0.05f);
	const float OriginTiled = ColumnCoverage(Tiled, Resolution, 0.01f, 0.05f);

	TestTrue(
		FString::Printf(TEXT("Untiled mask leaves the repeat column dry (%.3f)"), RepeatUntiled),
		RepeatUntiled < 0.05f);
	TestTrue(
		FString::Printf(TEXT("Tiling 3 wets the repeat column (%.3f)"), RepeatTiled),
		RepeatTiled > 0.20f);
	TestTrue(
		FString::Printf(
			TEXT("Tiling leaves the gap between repeats dry (%.3f untiled, %.3f tiled)"),
			GapUntiled,
			GapTiled),
		GapUntiled < 0.05f && GapTiled < 0.05f);
	TestTrue(
		FString::Printf(
			TEXT("The band at the origin survives tiling (%.3f untiled, %.3f tiled)"),
			OriginUntiled,
			OriginTiled),
		OriginUntiled > 0.20f && OriginTiled > 0.20f);

	return true;
}

// Gravity decides which way a run leaves its source. Its sign is read in three separate places in
// the solve -- the initial flow direction, the per-iteration velocity clamp and the surface-flow
// fallback -- and all three are now evaluated on the solve grid, so this pins the direction the
// whole chain produces rather than any one of them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainGravityTest,
	"Mixtormat.Stain.Gravity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainGravityTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// A horizontal band across the middle. Liquid starts there and has room to run either way.
	TStrongObjectPtr<UTexture2D> Mask(MakeMask(
		[](const float, const float V) { return V >= 0.40f && V <= 0.50f; }));
	if (!TestNotNull(TEXT("Band Liquid Mask fixture exists"), Mask.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeStainLayers(Mask.Get());

	// The iteration ceiling, for reach. Water crosses at most one texel per iteration, so the
	// default twenty puts the far edge of a run about two percent of the image past its source --
	// a real run and a hopeless thing to sample against. Sixty-four gives the strips below
	// somewhere to sit that is unambiguously outside the band and unambiguously inside the reach.
	Layers[1].Children[0].Effect.StainIterations = 64;
	Layers[1].Children[0].Effect.StainGravity = -1.0f;
	if (!TestTrue(TEXT("Downward stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Downward;
	if (!TestTrue(TEXT("Downward stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Downward)))
	{
		return false;
	}

	Layers[1].Children[0].Effect.StainGravity = 1.0f;
	if (!TestTrue(TEXT("Upward stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Upward;
	if (!TestTrue(TEXT("Upward stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Upward)))
	{
		return false;
	}

	// V grows downward in the image, and a negative gravity runs that way. Both strips sit just
	// outside the band, on the two sides a run could leave by -- the question is which one it
	// took, and comparing one strip across the two settings answers that without depending on
	// exactly how far the liquid got.
	const float BelowDown = RowCoverage(Downward, Resolution, 0.51f, 0.545f);
	const float AboveDown = RowCoverage(Downward, Resolution, 0.355f, 0.39f);
	const float BelowUp = RowCoverage(Upward, Resolution, 0.51f, 0.545f);
	const float AboveUp = RowCoverage(Upward, Resolution, 0.355f, 0.39f);

	TestTrue(
		FString::Printf(
			TEXT("Negative gravity runs below the source band (%.3f below, %.3f above)"),
			BelowDown,
			AboveDown),
		BelowDown > AboveDown && BelowDown > 0.02f);
	TestTrue(
		FString::Printf(
			TEXT("Positive gravity runs above the source band (%.3f above, %.3f below)"),
			AboveUp,
			BelowUp),
		AboveUp > BelowUp && AboveUp > 0.02f);
	TestTrue(
		FString::Printf(
			TEXT("Gravity decides which side of the band is wet (%.3f vs %.3f below, ")
			TEXT("%.3f vs %.3f above)"),
			BelowDown,
			BelowUp,
			AboveUp,
			AboveDown),
		BelowDown > BelowUp && AboveUp > AboveDown);

	const float CentroidDown = CoverageCentroidV(Downward, Resolution);
	const float CentroidUp = CoverageCentroidV(Upward, Resolution);
	TestTrue(
		FString::Printf(
			TEXT("Reversing gravity reverses the run (%.3f down, %.3f up)"),
			CentroidDown,
			CentroidUp),
		CentroidDown > CentroidUp + 0.01f);

	return true;
}

// The dirt the liquid can dissolve, which is the channel of the precomputed surface half that is
// easiest to get wrong and hardest to notice.
//
// It used to be a SampleDirt(UV) on every iteration and is now answered once, into Surface.z. The
// trap is that with no Dirt Mask set, SampleDirt falls back to the Liquid Mask -- so a wiring
// mistake that put the source mask in that channel would behave identically in every test that
// does not set a Dirt Mask distinct from the Liquid Mask. This one does, and reads Deposit rather
// than Wet so what comes back is the dirt that settled rather than where the water went.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainDirtMaskTest,
	"Mixtormat.Stain.DirtMask",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainDirtMaskTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// Liquid over the left half, dirt over the left fifth only. The overlap is where dirt can be
	// picked up and deposited; the rest of the liquid band is wet and clean. The right half is
	// left wide deliberately -- the sampler wraps, so water leaks a little from the left edge
	// back around to the right one, and the dry probe has to sit clear of both that seam and the
	// liquid band's own edge.
	TStrongObjectPtr<UTexture2D> Liquid(MakeMask(
		[](const float U, const float) { return U <= 0.55f; }));
	TStrongObjectPtr<UTexture2D> Dirt(MakeMask(
		[](const float U, const float) { return U <= 0.20f; }));
	if (!TestNotNull(TEXT("Liquid Mask fixture exists"), Liquid.Get())
		|| !TestNotNull(TEXT("Dirt Mask fixture exists"), Dirt.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeStainLayers(Liquid.Get());
	FMixtormatLayerEffect& Effect = Layers[1].Children[0].Effect;
	Effect.StainMode = EMixtormatStainMode::Deposit;
	Effect.StainDirtMaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Dirt.Get()));
	Effect.StainDirtAmount = 1.0f;

	// Deposition wants standing water to dry out of, so give it time and a reason to.
	Effect.StainIterations = 64;
	Effect.StainDrying = 1.0f;

	// Spread off. It is a diffusion term, so it moves a vanishing amount of water an unbounded
	// distance -- and the deposit mask normalizes against the dirt that much liquid could have
	// dissolved, a small number, which turns that vanishing amount into a visible preview value
	// well outside the Liquid Mask. Real behaviour, and nothing this test is asking about.
	Effect.StainSpread = 0.0f;

	if (!TestTrue(TEXT("Deposit stain composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Deposit stain preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
	{
		return false;
	}

	// Inside both masks: wet and dirty, so deposit settles.
	const float Dirty = ColumnCoverage(Pixels, Resolution, 0.03f, 0.15f);

	// Inside the Liquid Mask but outside the Dirt Mask: wet and clean. If the dirt channel were
	// carrying the Liquid Mask instead, this band would deposit exactly as the one above does.
	const float Clean = ColumnCoverage(Pixels, Resolution, 0.30f, 0.50f);

	// Outside both, and clear of the wrap seam. Nothing to deposit because there is no liquid to
	// carry it.
	const float Outside = ColumnCoverage(Pixels, Resolution, 0.70f, 0.90f);

	TestTrue(
		FString::Printf(TEXT("Dirt deposits where the Dirt Mask allows (%.3f)"), Dirty),
		Dirty > 0.20f);
	TestTrue(
		FString::Printf(
			TEXT("Wet but undirtied liquid deposits far less (%.3f dirty, %.3f clean)"),
			Dirty,
			Clean),
		Clean < 0.25f * Dirty);
	TestTrue(
		FString::Printf(TEXT("No deposit outside the Liquid Mask (%.3f)"), Outside),
		Outside < 0.05f);

	return true;
}

// Surface Response at zero has to reach the solve as the neutral half it always was. The two
// couplings it drives are now folded into the precomputed surface half rather than evaluated per
// iteration, so this is the assertion that the fold kept the control wired to something.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainSurfaceResponseTest,
	"Mixtormat.Stain.SurfaceResponse",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainSurfaceResponseTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Mask(MakeMask(
		[](const float, const float V) { return V >= 0.10f && V <= 0.16f; }));
	if (!TestNotNull(TEXT("Top-band Liquid Mask fixture exists"), Mask.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeStainLayers(Mask.Get());
	Layers[1].Children[0].Effect.StainIterations = 64;

	// The base layer's roughness is 0.5, which is also the neutral both couplings fall back to, so
	// full response and no response ask for the same numbers here. Two runs that agree say the
	// control reaches the solve through the new texture; a run that comes back empty at one end
	// says the channel is unbound.
	Layers[1].Children[0].Effect.StainSurfaceResponse = 1.0f;
	if (!TestTrue(TEXT("Full Surface Response composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}
	TArray<FLinearColor> Full;
	if (!TestTrue(TEXT("Full Surface Response preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Full)))
	{
		return false;
	}

	Layers[1].Children[0].Effect.StainSurfaceResponse = 0.0f;
	if (!TestTrue(TEXT("Zero Surface Response composes"),
		ComposeAndWait(Compositor, Layers, StainDebug())))
	{
		return false;
	}
	TArray<FLinearColor> None;
	if (!TestTrue(TEXT("Zero Surface Response preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, None)))
	{
		return false;
	}

	const float FullRun = RowCoverage(Full, Resolution, 0.17f, 0.30f);
	const float NoneRun = RowCoverage(None, Resolution, 0.17f, 0.30f);

	TestTrue(
		FString::Printf(TEXT("Full Surface Response still runs (%.3f)"), FullRun),
		FullRun > 0.02f);
	TestTrue(
		FString::Printf(TEXT("Zero Surface Response still runs (%.3f)"), NoneRun),
		NoneRun > 0.02f);
	TestTrue(
		FString::Printf(
			TEXT("A neutral surface makes Surface Response a no-op (%.3f vs %.3f)"),
			FullRun,
			NoneRun),
		WithinRelative(FullRun, NoneRun, 0.10f));

	return true;
}

// The one this architecture exists for.
//
// Everything inside the solve is measured in texels, so before the solve was pinned to its own
// grid a run's reach in UV was the iteration count over the composition resolution -- a stain at
// 4K travelled a quarter as far as the same settings gave at 1K. With the solve fixed, 1K, 2K and
// 4K all run the same number of iterations across the same number of texels and the only thing
// that changes is how finely the resolve filters the answer back up.
//
// Bulk statistics with a loose tolerance, not per-pixel equality: the solve still reads the
// full-resolution surface below it, so the three differ by whatever the sampler does at three
// different input resolutions. The fixture is deliberately flat and the auto source is off, which
// keeps that difference small, but it is not zero and an exact comparison would be asserting on
// the sampler rather than on the solve.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatStainResolutionConsistencyTest,
	"Mixtormat.Stain.ResolutionConsistency",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatStainResolutionConsistencyTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatStainTests;
	(void)Parameters;

	// A band across the top, so the run has the whole image below it to travel through and the
	// extent below is free to differ if the solve is not scale-invariant.
	TStrongObjectPtr<UTexture2D> Mask(MakeMask(
		[](const float, const float V) { return V >= 0.10f && V <= 0.16f; }));
	if (!TestNotNull(TEXT("Top-band Liquid Mask fixture exists"), Mask.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeStainLayers(Mask.Get());

	// The iteration ceiling again, for the same reason the gravity test uses it: the run has to be
	// long enough that how long it is can be measured. At the default twenty it is about two
	// percent of the image, which is under the noise the three resolutions differ by anyway.
	Layers[1].Children[0].Effect.StainIterations = 64;

	const int32 Resolutions[] = {1024, 2048, 4096};
	constexpr int32 ResolutionCount = UE_ARRAY_COUNT(Resolutions);
	float RunCoverages[ResolutionCount] = {};
	float Reaches[ResolutionCount] = {};

	for (int32 Index = 0; Index < ResolutionCount; ++Index)
	{
		const int32 Resolution = Resolutions[Index];

		// A compositor per resolution, scoped so its targets are gone before the next one
		// allocates. At 4K they are not small.
		FMixtormatGpuCompositor Compositor;
		if (!TestTrue(
			FString::Printf(TEXT("Compositor initialises at %d"), Resolution),
			Compositor.Initialize(FIntPoint(Resolution, Resolution))))
		{
			return false;
		}
		if (!TestTrue(
			FString::Printf(TEXT("Stain composes at %d"), Resolution),
			ComposeAndWait(Compositor, Layers, StainDebug())))
		{
			return false;
		}

		TArray<FLinearColor> Pixels;
		if (!TestTrue(
			FString::Printf(TEXT("Stain preview reads back at %d"), Resolution),
			ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
		{
			return false;
		}

		// Strictly below the source band, which ends at 0.16. Everything in this strip is liquid
		// that travelled to get there, so it is the run and nothing else -- coverage measured
		// over the whole image would be mostly the band, which is the same size at every
		// resolution and would let a solve that barely moved pass.
		RunCoverages[Index] = RowCoverage(Pixels, Resolution, 0.17f, 0.30f);
		Reaches[Index] = ReachV(Pixels, Resolution);

		TestTrue(
			FString::Printf(
				TEXT("The stain actually ran at %d (run %.3f, reach %.3f)"),
				Resolution,
				RunCoverages[Index],
				Reaches[Index]),
			RunCoverages[Index] > 0.02f && Reaches[Index] > 0.18f);
	}

	// Fifteen percent. Tight enough that the old composition-resolution solve fails it outright --
	// at 4K it reached a quarter as far as the same settings gave at 1K -- and loose enough that
	// resampling a full-resolution surface at three different resolutions does not.
	constexpr float Tolerance = 0.15f;
	for (int32 Index = 1; Index < ResolutionCount; ++Index)
	{
		TestTrue(
			FString::Printf(
				TEXT("Run coverage matches between %d and %d (%.3f vs %.3f)"),
				Resolutions[0],
				Resolutions[Index],
				RunCoverages[0],
				RunCoverages[Index]),
			WithinRelative(RunCoverages[0], RunCoverages[Index], Tolerance));
		TestTrue(
			FString::Printf(
				TEXT("Run length matches between %d and %d (%.3f vs %.3f)"),
				Resolutions[0],
				Resolutions[Index],
				Reaches[0],
				Reaches[Index]),
			WithinRelative(Reaches[0], Reaches[Index], Tolerance));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
