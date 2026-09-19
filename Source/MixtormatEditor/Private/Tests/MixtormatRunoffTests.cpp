// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

// Cover for Runoff, the procedural streak.
//
// Runoff makes one claim that is worth a test suite rather than a look: that it produces a
// believable layered run *without* transporting anything, which means every property a solve gets
// for free has to be arranged deliberately here. A solve confines itself to its source because
// liquid can only be where it was carried; a smear has to be told. A solve is the same size at
// every resolution because it counts texels; a smear has to convert its reach into UV and be
// checked that it did. These tests are mostly about the places where "cheap" could have quietly
// meant "wrong".
//
// Everything reads back through the Runoff feature preview, which the resolve writes before the
// composite runs and which the composite is told to leave alone. What comes back is therefore the
// resolved runoff before Strength blends it into the mask chain -- the effect's own output, not
// the layer it ends up masking.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatRunoffTests
{
	// Fixture masks are authored once at this size and sampled by UV, so they say the same thing
	// whatever the composition is doing. A resolution comparison whose input changes with the
	// resolution proves nothing.
	constexpr int32 MaskResolution = 512;

	// Above this a texel counts as covered. Clear of the preview ramp's noise floor and of the
	// bilinear skirt around a mask edge.
	constexpr float CoveredAbove = 0.15f;

	// A single-channel fixture, uncompressed and linear, painted by a predicate over UV. A hard
	// edge rather than a gradient: a soft fixture would let a wrong tiling or a leaking scope
	// pass by blurring the boundary the assertion is looking at.
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
				const uint8 Value = static_cast<uint8>(
					FMath::Clamp(FMath::RoundToInt(Predicate(U, V) * 255.0f), 0, 255));
				Pixels[Y * MaskResolution + X] = FColor(Value, Value, Value, 255);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// Two flat Fill layers: one to be the surface underneath, one to carry the runoff.
	//
	// Two rather than one because Runoff reads the surface accumulated below it, and on the
	// bottom layer there is none -- SurfaceValid is off there and the surface response falls back
	// to neutral. Flat because most of these tests are about where the smear goes, not about what
	// relief does to it; the one test that wants relief builds it explicitly.
	//
	// The runoff layer carries two children in order: an authored mask that supplies the source,
	// then the Runoff itself. That order is the point -- Runoff sources from the layer's
	// accumulated mask chain, so a Runoff with nothing in front of it has only the surface to go
	// on. Putting the mask first is also what makes UseLayerMask true, which is the branch every
	// mask-driven assertion below depends on.
	TArray<FMixtormatLayer> MakeRunoffLayers(UTexture2D* SourceMask)
	{
		FMixtormatLayer Base;
		Base.Type = EMixtormatLayerType::Fill;
		Base.bEnabled = true;
		Base.bOverrideBaseColor = true;
		Base.BaseColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
		Base.bOverrideRoughness = true;
		Base.Roughness = 0.5f;

		FMixtormatLayer Streaked;
		Streaked.Type = EMixtormatLayerType::Fill;
		Streaked.bEnabled = true;
		Streaked.bOverrideBaseColor = true;
		Streaked.BaseColor = FLinearColor(0.60f, 0.30f, 0.10f, 1.0f);
		Streaked.bOverrideRoughness = true;
		Streaked.Roughness = 0.8f;

		FMixtormatLayerChild& Mask = Streaked.Children.AddDefaulted_GetRef();
		Mask.Type = EMixtormatLayerChildType::Mask;
		Mask.Mask.bEnabled = true;
		Mask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(SourceMask));
		Mask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		Mask.Mask.Weight = 1.0f;

		FMixtormatLayerChild& Child = Streaked.Children.AddDefaulted_GetRef();
		Child.Type = EMixtormatLayerChildType::Effect;
		Child.Effect.ProceduralType = EMixtormatEffectType::Runoff;
		Child.Effect.RunoffStrength = 1.0f;

		// The surface response silenced outright for the mask-driven tests. It reads the height
		// below and would add source of its own on any relief, which would put runoff outside the
		// fixture mask and make the confinement and tiling assertions say nothing about the mask.
		// The height test below turns it back on deliberately.
		Child.Effect.RunoffSurfaceInfluence = 0.0f;

		TArray<FMixtormatLayer> Layers;
		Layers.Add(Base);
		Layers.Add(Streaked);
		return Layers;
	}

	FMixtormatLayerEffect& RunoffOf(TArray<FMixtormatLayer>& Layers)
	{
		return Layers[1].Children[1].Effect;
	}

	FMixtormatDebugPreviewSettings RunoffDebug()
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::Runoff;

		// The streaked layer, not the base. The resolve gates its debug write on this index so
		// two runoffs on different layers cannot fight over one target.
		Debug.LayerIndex = 1;
		Debug.ChildIndex = 1;
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

	// Green is the channel that moves: the shared ramp runs it from 0.02 to 0.25 while red barely
	// changes, which is why sampling red says nothing.
	float Coverage(const FLinearColor& Pixel)
	{
		constexpr float Low = 0.02f;
		constexpr float High = 0.25f;
		return FMath::Clamp((ToSrgbChannel(Pixel.G) - Low) / (High - Low), 0.0f, 1.0f);
	}

	float At(const TArray<FLinearColor>& Pixels, const int32 Resolution, const float U, const float V)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(U * Resolution), 0, Resolution - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(V * Resolution), 0, Resolution - 1);
		return Coverage(Pixels[Y * Resolution + X]);
	}

	float MeanCoverage(const TArray<FLinearColor>& Pixels)
	{
		double Total = 0.0;
		for (const FLinearColor& Pixel : Pixels)
		{
			Total += Coverage(Pixel);
		}
		return Pixels.Num() > 0 ? static_cast<float>(Total / Pixels.Num()) : 0.0f;
	}

	// Mean coverage over a column band, in normalized UV so a caller reads the same band at every
	// resolution.
	float ColumnCoverage(
		const TArray<FLinearColor>& Pixels,
		const int32 Resolution,
		const float MinU,
		const float MaxU)
	{
		const int32 FirstX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
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
		const int32 FirstY = FMath::Clamp(FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
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

	// How far past a source band the runoff got, in UV, searching downstream of it.
	//
	// Measured from the band rather than over the whole image because the band itself is covered
	// at every setting and is a fixed fraction of the image at every resolution. A metric the
	// fixture dominates cannot fail, and a test that cannot fail is worse than no test.
	float ReachBelow(
		const TArray<FLinearColor>& Pixels,
		const int32 Resolution,
		const float BandBottomV)
	{
		const int32 FirstY = FMath::Clamp(FMath::CeilToInt(BandBottomV * Resolution), 0, Resolution - 1);
		for (int32 Y = Resolution - 1; Y >= FirstY; --Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				if (Coverage(Pixels[Y * Resolution + X]) > CoveredAbove)
				{
					return (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution)
						- BandBottomV;
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

	float MeanAbsoluteDifference(const TArray<FLinearColor>& A, const TArray<FLinearColor>& B)
	{
		if (A.Num() != B.Num() || A.IsEmpty())
		{
			return -1.0f;
		}
		double Total = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			Total += FMath::Abs(Coverage(A[Index]) - Coverage(B[Index]));
		}
		return static_cast<float>(Total / A.Num());
	}

	bool WithinRelative(const float A, const float B, const float Tolerance)
	{
		const float Scale = FMath::Max(FMath::Max(FMath::Abs(A), FMath::Abs(B)), 1.0e-4f);
		return FMath::Abs(A - B) / Scale <= Tolerance;
	}

	// A horizontal band across the top third. Runoff sources from it and runs down, which leaves
	// the rest of the image free to say how far it got.
	UTexture2D* MakeBandMask()
	{
		return MakeMask([](const float, const float V)
		{
			return V >= 0.20f && V <= 0.28f ? 1.0f : 0.0f;
		});
	}
	constexpr float BandBottomV = 0.28f;
}

// Defaults and ranges, on the CPU, with no GPU involved.
//
// The cheapest possible test and the one most likely to catch a real mistake: every number here
// is a value somebody typed twice -- once in the header's default, once in the inspector's slider
// -- and the two drifting apart is silent. Serialization, undo/redo and duplication all ride on
// these being UPROPERTYs, so a field missing its default is also a field that deserializes wrong.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffDefaultsTest,
	"Mixtormat.Runoff.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatRunoffDefaultsTest::RunTest(const FString&)
{
	const FMixtormatLayerEffect Effect;

	TestEqual(TEXT("Gravity Angle default"), Effect.RunoffGravityAngle, -90.0f);
	TestEqual(TEXT("Streak Radius default"), Effect.RunoffStreakRadius, 320.0f);
	TestEqual(TEXT("Streak Softness default"), Effect.RunoffStreakSoftness, 0.46f);
	TestEqual(TEXT("Surface Influence default"), Effect.RunoffSurfaceInfluence, 0.95f);
	TestEqual(TEXT("Strata Amount default"), Effect.RunoffStrataAmount, 0.75f);
	TestEqual(TEXT("Warp Scale default"), Effect.RunoffWarpScale, 18.0f);
	TestEqual(TEXT("Warp Amount default"), Effect.RunoffWarpAmount, 1.5f);
	TestEqual(TEXT("Lip Strength default"), Effect.RunoffLipStrength, 0.55f);
	TestEqual(TEXT("Strength default"), Effect.RunoffStrength, 0.25f);
	TestEqual(TEXT("Seed default"), Effect.RunoffSeed, 1);

	// Runoff is a Filter, like Stain: it resolves a mask rather than writing the effect data
	// target. Getting this wrong would make the composite sample a buffer nothing wrote.
	TestTrue(
		TEXT("Runoff classifies as a Filter"),
		MixtormatEffectClassOf(EMixtormatEffectType::Runoff) == EMixtormatEffectClass::Filter);

	// Appended, never renumbered: serialized recipes store this enum by value, so an existing
	// material with a Layer Blur child would silently become a Runoff if these moved.
	TestEqual(TEXT("Runoff is enum value 8"), static_cast<uint8>(EMixtormatEffectType::Runoff), static_cast<uint8>(8));
	TestEqual(TEXT("Layer Blur still 7"), static_cast<uint8>(EMixtormatEffectType::LayerBlur), static_cast<uint8>(7));
	TestEqual(TEXT("Stain still 1"), static_cast<uint8>(EMixtormatEffectType::Stain), static_cast<uint8>(1));
	return true;
}

// Zero strength is the identity, and an empty mask produces nothing.
//
// Two halves of the same claim: runoff appears only where something asked for it. The empty-mask
// half is the one that matters, because a smear that leaked would leak from an empty source too.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffNeutralTest,
	"Mixtormat.Runoff.Neutral",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffNeutralTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Empty(MakeMask([](float, float) { return 0.0f; }));
	if (!TestNotNull(TEXT("Empty mask fixture exists"), Empty.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Empty.Get());
	if (!TestTrue(TEXT("Empty-source runoff composes"),
		ComposeAndWait(Compositor, Layers, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> EmptyPixels;
	if (!TestTrue(TEXT("Empty-source preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, EmptyPixels)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("An empty mask produces no runoff (mean %.4f)"),
			MeanCoverage(EmptyPixels)),
		MeanCoverage(EmptyPixels) < 0.01f);

	// A full mask, by contrast, has to produce runoff essentially everywhere: there is nowhere
	// upstream that is not a source. Without this the empty-mask assertion above would also pass
	// on an effect that produced nothing at all.
	TStrongObjectPtr<UTexture2D> Full(MakeMask([](float, float) { return 1.0f; }));
	if (!TestNotNull(TEXT("Full mask fixture exists"), Full.Get()))
	{
		return false;
	}
	TArray<FMixtormatLayer> FullLayers = MakeRunoffLayers(Full.Get());

	// Breakup is on by default and would leave holes in a nominally solid result; this test is
	// about the source reaching everywhere, not about the grain.
	RunoffOf(FullLayers).RunoffWarpAmount = 0.0f;
	if (!TestTrue(TEXT("Full-source runoff composes"),
		ComposeAndWait(Compositor, FullLayers, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> FullPixels;
	if (!TestTrue(TEXT("Full-source preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, FullPixels)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("A full mask covers broadly (mean %.4f)"), MeanCoverage(FullPixels)),
		MeanCoverage(FullPixels) > 0.5f);

	return true;
}

// Which way runoff runs, and that it runs one way only.
//
// The assertion is relative -- the centroid at -90 sits downstream of the source and the centroid
// at +90 sits upstream of it -- so it tests the dial and the axis without hard-coding which end
// of V the viewport calls down. The sideways half is the stronger claim: at -90 the runoff must
// not have spread in U at all, which is what "no arbitrary sideways flow" has to mean to be a
// property rather than an intention.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffGravityTest,
	"Mixtormat.Runoff.Gravity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffGravityTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// A compact square source, so the centroid has somewhere to move to in both axes.
	TStrongObjectPtr<UTexture2D> Spot(MakeMask([](const float U, const float V)
	{
		return (U >= 0.45f && U <= 0.55f && V >= 0.45f && V <= 0.55f) ? 1.0f : 0.0f;
	}));
	if (!TestNotNull(TEXT("Spot mask fixture exists"), Spot.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Down = MakeRunoffLayers(Spot.Get());
	RunoffOf(Down).RunoffGravityAngle = -90.0f;
	if (!TestTrue(TEXT("Downward runoff composes"),
		ComposeAndWait(Compositor, Down, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> DownPixels;
	if (!TestTrue(TEXT("Downward preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, DownPixels)))
	{
		return false;
	}

	TArray<FMixtormatLayer> Up = MakeRunoffLayers(Spot.Get());
	RunoffOf(Up).RunoffGravityAngle = 90.0f;
	if (!TestTrue(TEXT("Upward runoff composes"),
		ComposeAndWait(Compositor, Up, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> UpPixels;
	if (!TestTrue(TEXT("Upward preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, UpPixels)))
	{
		return false;
	}

	const float DownCentroid = CoverageCentroidV(DownPixels, Resolution);
	const float UpCentroid = CoverageCentroidV(UpPixels, Resolution);
	TestTrue(
		FString::Printf(TEXT("Gravity reverses the run (down %.3f, up %.3f)"),
			DownCentroid, UpCentroid),
		DownCentroid > UpCentroid + 0.02f);

	// -90 is down: the runoff has to sit downstream of the source, not merely on the other side
	// of the +90 case. This is the half that pins the default to what an artist expects.
	TestTrue(
		FString::Printf(TEXT("-90 runs toward +V (centroid %.3f, source 0.50)"), DownCentroid),
		DownCentroid > 0.50f);

	// Strictly one-directional. The source spans U 0.45..0.55; outside a small bilinear margin
	// there must be nothing, at any V.
	const float OutsideColumns = FMath::Max(
		ColumnCoverage(DownPixels, Resolution, 0.0f, 0.42f),
		ColumnCoverage(DownPixels, Resolution, 0.58f, 1.0f));
	TestTrue(
		FString::Printf(TEXT("No sideways spread off the gravity axis (%.4f)"), OutsideColumns),
		OutsideColumns < 0.01f);

	return true;
}

// Mask intensity is a length control, not only an opacity.
//
// The one line in the resolve that makes the incoming mask feel like it is driving the effect:
// reach is the source value times the radius, evaluated per tap. A grey source must therefore
// produce a visibly shorter run than a white one from identical settings.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffMaskIntensityTest,
	"Mixtormat.Runoff.MaskIntensity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffMaskIntensityTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Strong(MakeMask([](const float, const float V)
	{
		return (V >= 0.20f && V <= 0.28f) ? 1.0f : 0.0f;
	}));
	TStrongObjectPtr<UTexture2D> Weak(MakeMask([](const float, const float V)
	{
		return (V >= 0.20f && V <= 0.28f) ? 0.35f : 0.0f;
	}));
	if (!TestNotNull(TEXT("Strong band exists"), Strong.Get())
		|| !TestNotNull(TEXT("Weak band exists"), Weak.Get()))
	{
		return false;
	}

	const auto ReachFor = [&](UTexture2D* Mask, float& OutReach) -> bool
	{
		TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Mask);

		// Warp off: it moves each stratum's endpoint, which is exactly the quantity being
		// measured. Leaving it on would compare reach plus a random offset against reach plus a
		// different random offset.
		RunoffOf(Layers).RunoffWarpAmount = 0.0f;
		if (!ComposeAndWait(Compositor, Layers, RunoffDebug()))
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		if (!ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels))
		{
			return false;
		}
		OutReach = ReachBelow(Pixels, Resolution, BandBottomV);
		return true;
	};

	float StrongReach = 0.0f;
	float WeakReach = 0.0f;
	if (!TestTrue(TEXT("Strong-source runoff composes"), ReachFor(Strong.Get(), StrongReach))
		|| !TestTrue(TEXT("Weak-source runoff composes"), ReachFor(Weak.Get(), WeakReach)))
	{
		return false;
	}

	TestTrue(
		FString::Printf(TEXT("A strong source runs at all (%.3f)"), StrongReach),
		StrongReach > 0.02f);
	TestTrue(
		FString::Printf(TEXT("A weak source runs shorter (strong %.3f, weak %.3f)"),
			StrongReach, WeakReach),
		WeakReach < StrongReach - 0.01f);

	return true;
}

// Streak Radius has to be the length control it says it is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffRadiusTest,
	"Mixtormat.Runoff.Radius",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffRadiusTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Band(MakeBandMask());
	if (!TestNotNull(TEXT("Band mask fixture exists"), Band.Get()))
	{
		return false;
	}

	const auto ReachFor = [&](const float Radius, float& OutReach) -> bool
	{
		TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Band.Get());
		RunoffOf(Layers).RunoffStreakRadius = Radius;
		RunoffOf(Layers).RunoffWarpAmount = 0.0f;
		if (!ComposeAndWait(Compositor, Layers, RunoffDebug()))
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		if (!ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels))
		{
			return false;
		}
		OutReach = ReachBelow(Pixels, Resolution, BandBottomV);
		return true;
	};

	float ShortReach = 0.0f;
	float LongReach = 0.0f;
	if (!TestTrue(TEXT("Short runoff composes"), ReachFor(64.0f, ShortReach))
		|| !TestTrue(TEXT("Long runoff composes"), ReachFor(400.0f, LongReach)))
	{
		return false;
	}

	TestTrue(
		FString::Printf(TEXT("A larger radius reaches further (short %.3f, long %.3f)"),
			ShortReach, LongReach),
		LongReach > ShortReach + 0.05f);

	return true;
}

// Height sources runoff on its own, with no mask contribution at all.
//
// Surface Influence at 1 and the layer's own mask chain left white: everything that appears is
// the cavity and ledge terms reading the step in the surface below. The assertion is that runoff
// appears near the step and not away from it -- a surface response that fired everywhere would be
// a constant, and a constant would make every other test in this file pass for the wrong reason.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffHeightSourceTest,
	"Mixtormat.Runoff.HeightSource",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffHeightSourceTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	// A raised shelf across the upper half of the base layer, published as height through the
	// layer's mask. Its lower edge is a drop, which is what the ledge term is built to find.
	TStrongObjectPtr<UTexture2D> Shelf(MakeMask([](const float, const float V)
	{
		return V <= 0.45f ? 1.0f : 0.0f;
	}));
	TStrongObjectPtr<UTexture2D> Full(MakeMask([](float, float) { return 1.0f; }));
	if (!TestNotNull(TEXT("Shelf fixture exists"), Shelf.Get())
		|| !TestNotNull(TEXT("Full fixture exists"), Full.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Full.Get());

	// The base layer publishes the shelf as height.
	FMixtormatLayerChild& ShelfMask = Layers[0].Children.AddDefaulted_GetRef();
	ShelfMask.Type = EMixtormatLayerChildType::Mask;
	ShelfMask.Mask.bEnabled = true;
	ShelfMask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Shelf.Get()));
	ShelfMask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	ShelfMask.Mask.Weight = 1.0f;
	Layers[0].bHeightBlendEnabled = true;
	Layers[0].HeightSource = EMixtormatHeightSource::CombinedMask;
	Layers[0].HeightInfluence = 1.0f;

	// The surface is the whole source now.
	RunoffOf(Layers).RunoffSurfaceInfluence = 1.0f;
	RunoffOf(Layers).RunoffWarpAmount = 0.0f;

	if (!TestTrue(TEXT("Height-sourced runoff composes"),
		ComposeAndWait(Compositor, Layers, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Height-sourced preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
	{
		return false;
	}

	// Just downstream of the drop, where runoff from the ledge lands.
	const float BelowStep = RowCoverage(Pixels, Resolution, 0.46f, 0.60f);

	// Well upstream of it, on the flat top of the shelf, where nothing should source.
	const float AboveStep = RowCoverage(Pixels, Resolution, 0.05f, 0.30f);

	TestTrue(
		FString::Printf(TEXT("The ledge sources runoff below it (%.4f)"), BelowStep),
		BelowStep > 0.05f);
	TestTrue(
		FString::Printf(TEXT("Flat ground well above the ledge does not (below %.4f, above %.4f)"),
			BelowStep, AboveStep),
		AboveStep < BelowStep * 0.5f);

	return true;
}

// The shaping controls have to do something, and the seed has to be deterministic.
//
// Grouped because they are the same shape of assertion -- change one input, confirm the output
// moved -- and because they share a compose. Weak on their own; their value is catching a control
// that was wired into the struct and never reached the shader, which is the standard way a new
// parameter fails.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffShapingTest,
	"Mixtormat.Runoff.Shaping",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffShapingTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Band(MakeBandMask());
	if (!TestNotNull(TEXT("Band mask fixture exists"), Band.Get()))
	{
		return false;
	}

	const auto Render = [&](
		const TFunctionRef<void(FMixtormatLayerEffect&)> Configure,
		TArray<FLinearColor>& OutPixels) -> bool
	{
		TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Band.Get());
		Configure(RunoffOf(Layers));
		return ComposeAndWait(Compositor, Layers, RunoffDebug())
			&& ReadTarget(Compositor.GetDebugOutput(), Resolution, OutPixels);
	};

	TArray<FLinearColor> Baseline;
	if (!TestTrue(TEXT("Baseline composes"),
		Render([](FMixtormatLayerEffect&) {}, Baseline)))
	{
		return false;
	}

	TArray<FLinearColor> FlatStrata;
	if (!TestTrue(TEXT("Flat strata composes"),
		Render([](FMixtormatLayerEffect& E) { E.RunoffStrataAmount = 0.0f; }, FlatStrata)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("Strata Amount changes the result (%.4f)"),
			MeanAbsoluteDifference(Baseline, FlatStrata)),
		MeanAbsoluteDifference(Baseline, FlatStrata) > 0.005f);

	TArray<FLinearColor> NoWarp;
	if (!TestTrue(TEXT("Unwarped composes"),
		Render([](FMixtormatLayerEffect& E) { E.RunoffWarpAmount = 0.0f; }, NoWarp)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("Warp Amount changes the result (%.4f)"),
			MeanAbsoluteDifference(Baseline, NoWarp)),
		MeanAbsoluteDifference(Baseline, NoWarp) > 0.005f);

	// The lip is a narrow deposit at the end of a run, so it shows up as extra coverage
	// downstream rather than as a change over the whole image. Compared against the same settings
	// with the lip off, which isolates it from the run underneath.
	TArray<FLinearColor> NoLip;
	TArray<FLinearColor> FullLip;
	if (!TestTrue(TEXT("Lip-free composes"),
		Render([](FMixtormatLayerEffect& E)
		{
			E.RunoffLipStrength = 0.0f;
			E.RunoffWarpAmount = 0.0f;
		}, NoLip))
		|| !TestTrue(TEXT("Full-lip composes"),
		Render([](FMixtormatLayerEffect& E)
		{
			E.RunoffLipStrength = 1.0f;
			E.RunoffWarpAmount = 0.0f;
		}, FullLip)))
	{
		return false;
	}
	const float NoLipTail = RowCoverage(NoLip, Resolution, 0.55f, 0.95f);
	const float FullLipTail = RowCoverage(FullLip, Resolution, 0.55f, 0.95f);
	TestTrue(
		FString::Printf(TEXT("Lip Strength deposits at the end of a run (off %.4f, on %.4f)"),
			NoLipTail, FullLipTail),
		FullLipTail > NoLipTail + 0.002f);

	// Same seed, same runoff. The determinism half.
	TArray<FLinearColor> SeedOneAgain;
	if (!TestTrue(TEXT("Repeat composes"),
		Render([](FMixtormatLayerEffect&) {}, SeedOneAgain)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("The same seed reproduces exactly (%.6f)"),
			MeanAbsoluteDifference(Baseline, SeedOneAgain)),
		MeanAbsoluteDifference(Baseline, SeedOneAgain) < 1.0e-4f);

	// A different seed is a different field, not a scaled one.
	TArray<FLinearColor> SeedTwo;
	if (!TestTrue(TEXT("Alternate seed composes"),
		Render([](FMixtormatLayerEffect& E) { E.RunoffSeed = 7; }, SeedTwo)))
	{
		return false;
	}
	TestTrue(
		FString::Printf(TEXT("A different seed changes the field (%.4f)"),
			MeanAbsoluteDifference(Baseline, SeedTwo)),
		MeanAbsoluteDifference(Baseline, SeedTwo) > 0.005f);

	return true;
}

// A scoped mask has to confine the runoff, and the wrap has to be seamless.
//
// Both are properties a transport solve gets structurally and a smear has to be given. Scope is
// applied to the resolved runoff rather than to its source deliberately: a scoped runoff is the
// same streak with the outside cut away, so the boundary is where the assertion looks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffScopeAndTilingTest,
	"Mixtormat.Runoff.ScopeAndTiling",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffScopeAndTilingTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 1024;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Band(MakeBandMask());

	// A left-half scope. Runoff sourced from the full-width band must appear only on the left.
	TStrongObjectPtr<UTexture2D> LeftHalf(MakeMask([](const float U, const float)
	{
		return U <= 0.5f ? 1.0f : 0.0f;
	}));
	if (!TestNotNull(TEXT("Band fixture exists"), Band.Get())
		|| !TestNotNull(TEXT("Scope fixture exists"), LeftHalf.Get()))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Band.Get());

	// A mask child scoped under the Runoff effect: the standard placement gate every Mixtormat
	// effect answers to.
	const FGuid RunoffChildId = Layers[1].Children[1].ChildId;
	FMixtormatLayerChild& Scope = Layers[1].Children.AddDefaulted_GetRef();
	Scope.Type = EMixtormatLayerChildType::Mask;
	Scope.Mask.bEnabled = true;
	Scope.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(LeftHalf.Get()));
	Scope.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	Scope.Mask.Weight = 1.0f;
	Scope.ScopeOwnerChildId = RunoffChildId;

	if (!TestTrue(TEXT("Scoped runoff composes"),
		ComposeAndWait(Compositor, Layers, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Scoped preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
	{
		return false;
	}

	const float Inside = ColumnCoverage(Pixels, Resolution, 0.05f, 0.45f);
	const float Outside = ColumnCoverage(Pixels, Resolution, 0.55f, 0.95f);
	TestTrue(
		FString::Printf(TEXT("Runoff exists inside the scope (%.4f)"), Inside),
		Inside > 0.05f);
	TestTrue(
		FString::Printf(TEXT("Runoff does not leak past the scope (in %.4f, out %.4f)"),
			Inside, Outside),
		Outside < 0.01f);

	// Tiling: a run that leaves one edge has to arrive at the other. Measured as the two columns
	// either side of the U seam agreeing -- the noise, the taps and the strata offsets all wrap,
	// so a discontinuity here means one of them does not.
	TArray<FMixtormatLayer> Wrapped = MakeRunoffLayers(Band.Get());
	if (!TestTrue(TEXT("Unscoped runoff composes"),
		ComposeAndWait(Compositor, Wrapped, RunoffDebug())))
	{
		return false;
	}
	TArray<FLinearColor> WrappedPixels;
	if (!TestTrue(TEXT("Unscoped preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, WrappedPixels)))
	{
		return false;
	}

	double SeamDelta = 0.0;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const float Left = Coverage(WrappedPixels[Y * Resolution + (Resolution - 1)]);
		const float Right = Coverage(WrappedPixels[Y * Resolution + 0]);
		SeamDelta += FMath::Abs(Left - Right);
	}
	const float MeanSeamDelta = static_cast<float>(SeamDelta / Resolution);

	// Against the difference between two arbitrary interior neighbours, so the bar is "the seam
	// is no worse than anywhere else" rather than an absolute number the grain would fail.
	double InteriorDelta = 0.0;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const float A = Coverage(WrappedPixels[Y * Resolution + Resolution / 2]);
		const float B = Coverage(WrappedPixels[Y * Resolution + Resolution / 2 + 1]);
		InteriorDelta += FMath::Abs(A - B);
	}
	const float MeanInteriorDelta = static_cast<float>(InteriorDelta / Resolution);

	TestTrue(
		FString::Printf(TEXT("The U seam is no worse than the interior (seam %.5f, interior %.5f)"),
			MeanSeamDelta, MeanInteriorDelta),
		MeanSeamDelta <= FMath::Max(MeanInteriorDelta * 3.0f, 0.01f));

	return true;
}

// The same runoff at 1K, 2K and 4K.
//
// This is the assertion the whole UV-reach design exists to satisfy, and the one that would have
// failed had Streak Radius stayed in texels: the run would have covered a quarter of the distance
// at 4K that it did at 1K. Reach is measured as a fraction of the image, so agreement between the
// three means the effect is the same physical size at all of them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffResolutionTest,
	"Mixtormat.Runoff.Resolution",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffResolutionTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;

	TStrongObjectPtr<UTexture2D> Band(MakeBandMask());
	if (!TestNotNull(TEXT("Band mask fixture exists"), Band.Get()))
	{
		return false;
	}

	const auto MeasureAt = [&](const int32 Resolution, float& OutReach, float& OutCentroid) -> bool
	{
		FMixtormatGpuCompositor Compositor;
		if (!Compositor.Initialize(FIntPoint(Resolution, Resolution)))
		{
			return false;
		}
		TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Band.Get());
		RunoffOf(Layers).RunoffWarpAmount = 0.0f;
		if (!ComposeAndWait(Compositor, Layers, RunoffDebug()))
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		if (!ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels))
		{
			return false;
		}
		OutReach = ReachBelow(Pixels, Resolution, BandBottomV);
		OutCentroid = CoverageCentroidV(Pixels, Resolution);
		return true;
	};

	float Reach1K = 0.0f, Centroid1K = 0.0f;
	float Reach2K = 0.0f, Centroid2K = 0.0f;
	float Reach4K = 0.0f, Centroid4K = 0.0f;
	if (!TestTrue(TEXT("1K composes"), MeasureAt(1024, Reach1K, Centroid1K))
		|| !TestTrue(TEXT("2K composes"), MeasureAt(2048, Reach2K, Centroid2K))
		|| !TestTrue(TEXT("4K composes"), MeasureAt(4096, Reach4K, Centroid4K)))
	{
		return false;
	}

	TestTrue(
		FString::Printf(TEXT("The run is not empty at 1K (%.3f)"), Reach1K),
		Reach1K > 0.02f);

	// Ten percent. The taps are spread over the reach rather than per texel, so the three
	// resolutions sample the same smear at different densities and the boundary lands on a
	// slightly different row; what must not happen is the reach scaling with the resolution.
	constexpr float Tolerance = 0.10f;
	TestTrue(
		FString::Printf(TEXT("Reach agrees 1K to 2K (%.3f vs %.3f)"), Reach1K, Reach2K),
		WithinRelative(Reach1K, Reach2K, Tolerance));
	TestTrue(
		FString::Printf(TEXT("Reach agrees 1K to 4K (%.3f vs %.3f)"), Reach1K, Reach4K),
		WithinRelative(Reach1K, Reach4K, Tolerance));
	TestTrue(
		FString::Printf(TEXT("Centroid agrees 1K to 4K (%.3f vs %.3f)"), Centroid1K, Centroid4K),
		WithinRelative(Centroid1K, Centroid4K, Tolerance));

	return true;
}

namespace MixtormatRunoffTests
{
	// The shelf fixture from the height-source test, as a helper, because the analysis-grid test
	// below needs the same relief. A raised shelf across the upper half of the base layer, with a
	// drop at its lower edge -- the feature both the cavity rings and the ledge term are built to
	// find, and therefore the part of Runoff whose answer depends on what resolution the height is
	// read at.
	TArray<FMixtormatLayer> MakeReliefRunoffLayers(UTexture2D* Shelf, UTexture2D* Full)
	{
		TArray<FMixtormatLayer> Layers = MakeRunoffLayers(Full);

		FMixtormatLayerChild& ShelfMask = Layers[0].Children.AddDefaulted_GetRef();
		ShelfMask.Type = EMixtormatLayerChildType::Mask;
		ShelfMask.Mask.bEnabled = true;
		ShelfMask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Shelf));
		ShelfMask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		ShelfMask.Mask.Weight = 1.0f;
		Layers[0].bHeightBlendEnabled = true;
		Layers[0].HeightSource = EMixtormatHeightSource::CombinedMask;
		Layers[0].HeightInfluence = 1.0f;

		// The surface is the whole source, so the cavity rings and the ledge decide the answer.
		Layers[1].Children[1].Effect.RunoffSurfaceInfluence = 1.0f;
		Layers[1].Children[1].Effect.RunoffWarpAmount = 0.0f;
		return Layers;
	}

	UTexture2D* MakeShelfMask()
	{
		return MakeMask([](const float, const float V)
		{
			return V <= 0.45f ? 1.0f : 0.0f;
		});
	}
}

// The surface response agrees between 1K and 4K, which is what the fixed analysis grid buys.
//
// The sibling Resolution test above measures reach and centroid, which are properties of the
// smear, and the smear was already resolution-independent because it works in UV. This one
// measures the part that was not: the multi-scale cavity detector, whose rings are authored in
// reference texels against 1024 and which used to be evaluated on whatever grid the composition
// happened to be. At 4K that read the height four times more finely than the radii were ever tuned
// for, so the same shelf produced a different, noisier response than it did at 1K -- and cost
// sixteen times as much to produce it, which is what made a 4K composition unsafe.
//
// Now the rings run on a grid pinned to 1024 at every composition size, so the response is the
// same answer rather than a similar one, and the profile down the streak should agree band for
// band. A regression that put the analysis back on the composition's own grid would show up here
// as the bands drifting apart, and -- on a material with more than one runoff in it -- as this
// test taking the GPU down with it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffAnalysisGridTest,
	"Mixtormat.Runoff.AnalysisGrid",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffAnalysisGridTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;

	TStrongObjectPtr<UTexture2D> Shelf(MakeShelfMask());
	TStrongObjectPtr<UTexture2D> Full(MakeMask([](float, float) { return 1.0f; }));
	if (!TestNotNull(TEXT("Shelf fixture exists"), Shelf.Get())
		|| !TestNotNull(TEXT("Full fixture exists"), Full.Get()))
	{
		return false;
	}

	// Bands in UV, so the same stretch of the streak is measured at every resolution. Each one
	// averages over at least a million texels at 4K, which is what makes the comparison a
	// statement about the field rather than about where one threshold happened to land.
	struct FBand
	{
		float MinV;
		float MaxV;
		const TCHAR* Name;
	};
	static const FBand Bands[] = {
		{0.05f, 0.30f, TEXT("above the shelf edge")},
		{0.46f, 0.60f, TEXT("just below the shelf edge")},
		{0.60f, 0.80f, TEXT("down the run")},
		{0.80f, 1.00f, TEXT("at the tail")},
	};
	constexpr int32 BandCount = UE_ARRAY_COUNT(Bands);

	const auto MeasureAt =
		[&](const int32 Resolution, float& OutMean, float (&OutBands)[BandCount]) -> bool
	{
		FMixtormatGpuCompositor Compositor;
		if (!Compositor.Initialize(FIntPoint(Resolution, Resolution)))
		{
			return false;
		}
		TArray<FMixtormatLayer> Layers = MakeReliefRunoffLayers(Shelf.Get(), Full.Get());
		if (!ComposeAndWait(Compositor, Layers, RunoffDebug()))
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		if (!ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels))
		{
			return false;
		}
		OutMean = MeanCoverage(Pixels);
		for (int32 Index = 0; Index < BandCount; ++Index)
		{
			OutBands[Index] =
				RowCoverage(Pixels, Resolution, Bands[Index].MinV, Bands[Index].MaxV);
		}
		return true;
	};

	float Mean1K = 0.0f;
	float Mean4K = 0.0f;
	float Bands1K[BandCount] = {};
	float Bands4K[BandCount] = {};
	if (!TestTrue(TEXT("1K composes with relief"), MeasureAt(1024, Mean1K, Bands1K))
		|| !TestTrue(TEXT("4K composes with relief"), MeasureAt(4096, Mean4K, Bands4K)))
	{
		return false;
	}

	// The ledge has to have sourced something, or the agreement below is agreement about nothing.
	TestTrue(
		FString::Printf(TEXT("The shelf sources runoff at 1K (mean %.4f)"), Mean1K),
		Mean1K > 0.01f);

	// The same ten percent the Resolution test uses. Not tighter, because the apply pass filters
	// the streak up off the analysis grid and the two resolutions still quantise the mask through
	// a 16-bit target at different densities; not looser, because with the rings pinned to one
	// grid there is no longer a mechanism that would move these by more than that.
	constexpr float Tolerance = 0.10f;
	TestTrue(
		FString::Printf(TEXT("Mean coverage agrees 1K to 4K (%.4f vs %.4f)"), Mean1K, Mean4K),
		WithinRelative(Mean1K, Mean4K, Tolerance));

	for (int32 Index = 0; Index < BandCount; ++Index)
	{
		TestTrue(
			FString::Printf(
				TEXT("Coverage agrees 1K to 4K %s (%.4f vs %.4f)"),
				Bands[Index].Name,
				Bands1K[Index],
				Bands4K[Index]),
			WithinRelative(Bands1K[Index], Bands4K[Index], Tolerance));
	}

	return true;
}

// Three runoffs on one layer at 4K, in one composition.
//
// This is the crash, as a test. The compositor builds the whole composition into a single render
// graph and executes it in one submission, so the driver's watchdog is looking at the total, not
// at any one pass -- which means the thing that made 4K unsafe was never a single dispatch being
// slow, it was a per-child cost that scaled with texel count multiplied by however many children
// the material had. Three is the smallest count that makes that multiplication visible, and the
// fixture deliberately turns the surface response on so the cavity rings run too.
//
// There is no timing assertion here and there should not be: the budget this guards is not a
// number of milliseconds on one machine, it is whether the work is proportional to the output at
// all. If a change puts the analysis back on the composition's own grid, this does not fail with a
// bad value -- it hangs the GPU, and that is the signal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatRunoffFourKBudgetTest,
	"Mixtormat.Runoff.FourKBudget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatRunoffFourKBudgetTest::RunTest(const FString&)
{
	using namespace MixtormatRunoffTests;
	constexpr int32 Resolution = 4096;

	TStrongObjectPtr<UTexture2D> Shelf(MakeShelfMask());
	TStrongObjectPtr<UTexture2D> Full(MakeMask([](float, float) { return 1.0f; }));
	if (!TestNotNull(TEXT("Shelf fixture exists"), Shelf.Get())
		|| !TestNotNull(TEXT("Full fixture exists"), Full.Get()))
	{
		return false;
	}

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises at 4K"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}

	TArray<FMixtormatLayer> Layers = MakeReliefRunoffLayers(Shelf.Get(), Full.Get());

	// Two more runoffs behind the first, at settings far enough apart that none of them is the
	// same dispatch as another: a long soft run, then a short sharp one. Each adds its own four
	// passes and its own analysis intermediates to the graph.
	{
		FMixtormatLayerChild& Second = Layers[1].Children.AddDefaulted_GetRef();
		Second.Type = EMixtormatLayerChildType::Effect;
		Second.Effect.ProceduralType = EMixtormatEffectType::Runoff;
		Second.Effect.RunoffStrength = 1.0f;
		Second.Effect.RunoffStreakRadius = 512.0f;
		Second.Effect.RunoffStreakSoftness = 0.9f;
		Second.Effect.RunoffSurfaceInfluence = 1.0f;
		Second.Effect.RunoffSeed = 7;

		FMixtormatLayerChild& Third = Layers[1].Children.AddDefaulted_GetRef();
		Third.Type = EMixtormatLayerChildType::Effect;
		Third.Effect.ProceduralType = EMixtormatEffectType::Runoff;
		Third.Effect.RunoffStrength = 1.0f;
		Third.Effect.RunoffStreakRadius = 48.0f;
		Third.Effect.RunoffStreakSoftness = 0.1f;
		Third.Effect.RunoffSurfaceInfluence = 0.5f;
		Third.Effect.RunoffSeed = 23;
	}

	if (!TestTrue(TEXT("Three runoffs compose at 4K"),
		ComposeAndWait(Compositor, Layers, RunoffDebug())))
	{
		return false;
	}

	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("The 4K preview reads back"),
		ReadTarget(Compositor.GetDebugOutput(), Resolution, Pixels)))
	{
		return false;
	}

	// Neither empty nor saturated. A wrong divisor, a wrong UV or an analysis grid addressed with
	// full-resolution coordinates would land on one of those two, and both would otherwise read as
	// a composition that completed successfully.
	const float Mean = MeanCoverage(Pixels);
	TestTrue(
		FString::Printf(TEXT("The 4K runoff is not empty (mean %.4f)"), Mean),
		Mean > 0.005f);
	TestTrue(
		FString::Printf(TEXT("The 4K runoff is not saturated (mean %.4f)"), Mean),
		Mean < 0.95f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
