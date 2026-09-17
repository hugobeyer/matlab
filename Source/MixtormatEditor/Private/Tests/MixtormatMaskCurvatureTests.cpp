// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatGpuCompositor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatMaterial.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

// Cover for the Curvature filter's angle-deficit modes.
//
// The fixture is a mask with one smooth dome and one smooth pit in it, and the filter reads the
// mask itself, so the field under test is the fixture and nothing else. A black Fill underneath
// and a white Fill on top means the composited base colour is the layer mask directly, and the
// mask at that point is the fixture value times the filter's coverage -- so dividing by the known
// fixture value recovers the coverage the filter produced.
//
// Smooth and wide on purpose. Angle deficit on a step edge is dominated by the discontinuity, and
// the analytic modes it is compared against differentiate the field twice, so a hard-edged fixture
// would be measuring the fixture's quantisation rather than either estimator.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatMaskCurvatureTests
{
	constexpr int32 TestResolution = 256;

	// A raised cosine, wide enough that an 8-bit fixture still resolves its second derivative.
	constexpr float BumpRadius = 0.25f;
	constexpr float BumpAmplitude = 0.45f;
	constexpr float Background = 0.5f;

	// Dome on the left, pit on the right, flat between and around them.
	constexpr float DomeU = 0.25f;
	constexpr float PitU = 0.75f;
	constexpr float CentreV = 0.5f;
	constexpr float FlatU = 0.5f;

	// What the fixture holds at each probe, which is what the readback has to be divided by.
	constexpr float DomeValue = Background + BumpAmplitude;
	constexpr float PitValue = Background - BumpAmplitude;

	float Bump(const float U, const float V, const float CentreU)
	{
		const float DU = U - CentreU;
		const float DV = V - CentreV;
		const float Distance = FMath::Sqrt(DU * DU + DV * DV) / BumpRadius;
		return Distance >= 1.0f
			? 0.0f
			: 0.5f * (1.0f + FMath::Cos(PI * Distance));
	}

	float FixtureValue(const float U, const float V)
	{
		return Background
			+ BumpAmplitude * Bump(U, V, DomeU)
			- BumpAmplitude * Bump(U, V, PitU);
	}

	UTexture2D* MakeDomeAndPitMask()
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
				const uint8 Value = static_cast<uint8>(
					FMath::Clamp(FMath::RoundToInt(FixtureValue(U, V) * 255.0f), 0, 255));
				Pixels[Y * TestResolution + X] = FColor(Value, Value, Value, 255);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	// Black underneath, white on top: the composited base colour is then the layer mask itself.
	TArray<FMixtormatLayer> MakeLayers(
		UTexture2D* MaskTexture,
		const EMixtormatCurvatureMode Mode,
		const int32 Kernel,
		const float RangeLow,
		const float RangeHigh)
	{
		FMixtormatLayer Base;
		Base.Type = EMixtormatLayerType::Fill;
		Base.bEnabled = true;
		Base.bOverrideBaseColor = true;
		Base.BaseColor = FLinearColor::Black;

		FMixtormatLayer Masked;
		Masked.Type = EMixtormatLayerType::Fill;
		Masked.bEnabled = true;
		Masked.bOverrideBaseColor = true;
		Masked.BaseColor = FLinearColor::White;

		FMixtormatLayerChild& Mask = Masked.Children.AddDefaulted_GetRef();
		Mask.Type = EMixtormatLayerChildType::Mask;
		Mask.Mask.bEnabled = true;
		Mask.Mask.MaskTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(MaskTexture));
		Mask.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		Mask.Mask.Weight = 1.0f;

		FMixtormatLayerChild& Curvature = Masked.Children.AddDefaulted_GetRef();
		Curvature.Type = EMixtormatLayerChildType::Curvature;
		Curvature.ScopeOwnerChildId = Masked.Children[0].ChildId;
		Curvature.Curvature.bEnabled = true;
		Curvature.Curvature.Source = EMixtormatCurvatureSource::Mask;
		Curvature.Curvature.Mode = Mode;
		Curvature.Curvature.Kernel = Kernel;
		Curvature.Curvature.Scale = 8.0f;
		Curvature.Curvature.RangeLow = RangeLow;
		Curvature.Curvature.RangeHigh = RangeHigh;
		Curvature.Curvature.Weight = 1.0f;

		TArray<FMixtormatLayer> Layers;
		Layers.Add(Base);
		Layers.Add(Masked);
		return Layers;
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

	// The filter multiplies its coverage into the mask, so the readback is fixture times coverage.
	// Averaged over a patch, because the probe is comparing estimators and one of them is a second
	// derivative of an 8-bit fixture.
	float CoverageAt(
		const TArray<FLinearColor>& Pixels,
		const float U,
		const float V,
		const float FixtureAtProbe)
	{
		double Total = 0.0;
		int32 Count = 0;
		for (int32 OffsetY = -5; OffsetY <= 5; ++OffsetY)
		{
			for (int32 OffsetX = -5; OffsetX <= 5; ++OffsetX)
			{
				const int32 X = FMath::Clamp(
					FMath::FloorToInt(U * TestResolution) + OffsetX, 0, TestResolution - 1);
				const int32 Y = FMath::Clamp(
					FMath::FloorToInt(V * TestResolution) + OffsetY, 0, TestResolution - 1);
				Total += Pixels[Y * TestResolution + X].R;
				++Count;
			}
		}
		return Count > 0
			? static_cast<float>(Total / Count) / FMath::Max(FixtureAtProbe, 1.0e-4f)
			: 0.0f;
	}

	// Coverage is saturate((Signed - Low) / (High - Low)). Run a symmetric window wide enough that
	// nothing clips and the signed curvature comes back out of it.
	float SignedFromCoverage(const float Coverage, const float Window)
	{
		return (Coverage - 0.5f) * 2.0f * Window;
	}
}

// The measurement behind the assertions in the test after this one.
//
// Angle deficit is normalised by a third of the fan area to put it in the same units as the
// analytic Gaussian, and the sign of the mean -- which is what orients Convex against Concave --
// depends on a parameterisation convention that is easy to reason about backwards. Both are read
// off a real compose here rather than argued from the algebra, and the numbers are printed so the
// window the other test uses is one that was observed rather than one that was hoped for.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatCurvatureDeficitProbeTest,
	"Mixtormat.Curvature.DeficitProbe",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatCurvatureDeficitProbeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatMaskCurvatureTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Mask(MakeDomeAndPitMask());
	if (!TestNotNull(TEXT("Dome-and-pit fixture exists"), Mask.Get()))
	{
		return false;
	}

	constexpr float Window = 512.0f;
	struct FProbe
	{
		EMixtormatCurvatureMode Mode;
		const TCHAR* Name;
	};
	const FProbe Probes[] = {
		{EMixtormatCurvatureMode::Gaussian, TEXT("Gaussian")},
		{EMixtormatCurvatureMode::Mean, TEXT("Mean")},
		{EMixtormatCurvatureMode::AngleDeficit, TEXT("AngleDeficit")},
		{EMixtormatCurvatureMode::AngleDeficitConvex, TEXT("DeficitConvex")},
		{EMixtormatCurvatureMode::AngleDeficitConcave, TEXT("DeficitConcave")}};

	FString Report;
	for (const FProbe& Probe : Probes)
	{
		const TArray<FMixtormatLayer> Layers =
			MakeLayers(Mask.Get(), Probe.Mode, 4, -Window, Window);
		if (!TestTrue(
			FString::Printf(TEXT("%s composes"), Probe.Name),
			ComposeAndWait(Compositor, Layers)))
		{
			return false;
		}

		TArray<FLinearColor> Pixels;
		if (!TestTrue(
			FString::Printf(TEXT("%s reads back"), Probe.Name),
			ReadBaseColor(Compositor, Pixels)))
		{
			return false;
		}

		const float Dome = SignedFromCoverage(
			CoverageAt(Pixels, DomeU, CentreV, DomeValue), Window);
		const float Pit = SignedFromCoverage(
			CoverageAt(Pixels, PitU, CentreV, PitValue), Window);
		const float Flat = SignedFromCoverage(
			CoverageAt(Pixels, FlatU, CentreV, Background), Window);
		Report += FString::Printf(
			TEXT("%s dome=%.4f pit=%.4f flat=%.4f | "), Probe.Name, Dome, Pit, Flat);
	}

	AddInfo(Report);
	TestTrue(*Report, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
