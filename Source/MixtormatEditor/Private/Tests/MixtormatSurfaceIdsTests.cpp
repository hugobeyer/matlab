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

// Cover for Surface IDs: which maps a guide selection requires, and one end-to-end compose.
//
// The compose fixture is two flat height bands with a hard edge. Output and source are both 256,
// so every pixel centre lands on a texel centre and the bilinear sampler returns exact texel
// values -- no interpolated heights at the seam to occupy a third band. Two occupied bands must
// compact to exactly two IDs; the compaction pass is what the count checks.

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatSurfaceIdsTests
{
	constexpr int32 TestResolution = 256;

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
				// Only A (height) differs between the halves; R/G/B are constant, so a Height guide
				// sees two values and nothing else.
				const bool bLeft = X < TestResolution / 2;
				Pixels[Y * TestResolution + X] = FColor(128, 128, 0, bLeft ? 64 : 192);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UMixtormatSurface* MakeSurface(UTexture2D* RAMH, UTexture2D* BaseColor, UTexture2D* Normal)
	{
		UMixtormatSurface* Surface = NewObject<UMixtormatSurface>(
			GetTransientPackage(), NAME_None, RF_Transient);
		if (Surface)
		{
			Surface->RoughnessAOMetallic = RAMH;
			Surface->BaseColor = BaseColor;
			Surface->Normal = Normal;
		}
		return Surface;
	}

	FMixtormatClusterFilter MakeSurfaceIds(
		const EMixtormatSurfaceIdFeature Primary,
		const EMixtormatSurfaceIdFeature Secondary,
		const float Mix)
	{
		FMixtormatClusterFilter Filter;
		Filter.bSurfaceIds = true;
		Filter.PrimaryFeature = Primary;
		Filter.SecondaryFeature = Secondary;
		Filter.FeatureMix = Mix;
		return Filter;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceIdsInputGatingTest,
	"Mixtormat.SurfaceIds.InputGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatSurfaceIdsInputGatingTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfaceIdsTests;
	using EF = EMixtormatSurfaceIdFeature;
	(void)Parameters;

	// Presence is all CanSampleSurface checks, so tiny placeholder textures are enough.
	TStrongObjectPtr<UTexture2D> Placeholder(UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8));
	if (!TestNotNull(TEXT("Placeholder texture exists"), Placeholder.Get()))
	{
		return false;
	}
	UTexture2D* Tex = Placeholder.Get();
	TStrongObjectPtr<UMixtormatSurface> RamOnly(MakeSurface(Tex, nullptr, nullptr));
	TStrongObjectPtr<UMixtormatSurface> ColorOnly(MakeSurface(nullptr, Tex, nullptr));
	TStrongObjectPtr<UMixtormatSurface> NormalOnly(MakeSurface(nullptr, nullptr, Tex));

	// Legacy Cluster keeps its original requirement: the packed RAMH map.
	FMixtormatClusterFilter Legacy;
	TestFalse(TEXT("A new filter defaults to the legacy Cluster algorithm"), Legacy.bSurfaceIds);
	TestTrue(TEXT("Legacy Cluster samples a RAMH surface"), Legacy.CanSampleSurface(RamOnly.Get()));
	TestFalse(TEXT("Legacy Cluster refuses a surface without RAMH"), Legacy.CanSampleSurface(ColorOnly.Get()));
	TestFalse(TEXT("No surface is never sampleable"), Legacy.CanSampleSurface(nullptr));

	// Each guide requires its own map.
	TestTrue(TEXT("Height reads RAMH"),
		MakeSurfaceIds(EF::Height, EF::Height, 0.0f).CanSampleSurface(RamOnly.Get()));
	TestTrue(TEXT("Curvature reads RAMH height"),
		MakeSurfaceIds(EF::Curvature, EF::Height, 0.0f).CanSampleSurface(RamOnly.Get()));
	TestTrue(TEXT("Normal Flatness reads Normal"),
		MakeSurfaceIds(EF::NormalFlatness, EF::Height, 0.0f).CanSampleSurface(NormalOnly.Get()));
	TestFalse(TEXT("Normal Flatness refuses a surface without Normal"),
		MakeSurfaceIds(EF::NormalFlatness, EF::Height, 0.0f).CanSampleSurface(RamOnly.Get()));
	TestTrue(TEXT("Luminance reads BaseColor"),
		MakeSurfaceIds(EF::Luminance, EF::Height, 0.0f).CanSampleSurface(ColorOnly.Get()));
	TestFalse(TEXT("Luminance refuses a surface without BaseColor"),
		MakeSurfaceIds(EF::Luminance, EF::Height, 0.0f).CanSampleSurface(NormalOnly.Get()));

	// At an endpoint only the active guide counts; between them both do.
	TestTrue(TEXT("Mix 0 ignores the secondary guide's map"),
		MakeSurfaceIds(EF::Luminance, EF::NormalFlatness, 0.0f).CanSampleSurface(ColorOnly.Get()));
	TestTrue(TEXT("Mix 1 ignores the primary guide's map"),
		MakeSurfaceIds(EF::Luminance, EF::NormalFlatness, 1.0f).CanSampleSurface(NormalOnly.Get()));
	TestFalse(TEXT("Mix 1 requires the secondary guide's map"),
		MakeSurfaceIds(EF::Luminance, EF::NormalFlatness, 1.0f).CanSampleSurface(ColorOnly.Get()));
	TestFalse(TEXT("A partial mix requires both maps"),
		MakeSurfaceIds(EF::Luminance, EF::NormalFlatness, 0.5f).CanSampleSurface(ColorOnly.Get()));
	TestTrue(TEXT("A non-finite mix is treated as 0"),
		MakeSurfaceIds(EF::Luminance, EF::NormalFlatness, std::numeric_limits<float>::quiet_NaN()).CanSampleSurface(ColorOnly.Get()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatSurfaceIdsComposeTest,
	"Mixtormat.SurfaceIds.Compose",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatSurfaceIdsComposeTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatSurfaceIdsTests;
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
	TStrongObjectPtr<UMixtormatSurface> Surface(MakeSurface(RAMH.Get(), nullptr, nullptr));
	if (!TestNotNull(TEXT("Test surface exists"), Surface.Get()))
	{
		return false;
	}

	FMixtormatLayerChild Child;
	Child.Type = EMixtormatLayerChildType::Filter;
	Child.Filter = MakeSurfaceIds(
		EMixtormatSurfaceIdFeature::Height, EMixtormatSurfaceIdFeature::Roughness, 0.0f);
	Child.Filter.MaxIds = 8;
	// No smoothing or closing: the fixture's seam must stay a clean two-band edge.
	Child.Filter.GuideBlur = 0;
	Child.Filter.EdgeClose = 0;

	FMixtormatLayer Layer;
	Layer.DisplayName = FText::FromString(TEXT("Test"));
	Layer.bEnabled = true;
	Layer.Children.Add(Child);
	Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	FMixtormatDebugPreviewSettings Debug;
	Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
	Debug.ChildTarget.OwnerId = Layer.LayerId;
	Debug.ChildTarget.ChildId = Child.ChildId;
	Debug.ChildTarget.Kind = EMixtormatPreviewOutputKind::RegionIds;

	if (!TestTrue(TEXT("Surface IDs compose"),
		Compositor.RequestCompose(Layers, FSimpleDelegate(), Debug)))
	{
		return false;
	}
	FlushRenderingCommands();

	UTextureRenderTarget2D* DebugTarget = Compositor.GetDebugOutput();
	FTextureRenderTargetResource* Resource =
		DebugTarget ? DebugTarget->GameThread_GetRenderTargetResource() : nullptr;
	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Debug target reads back"),
		Resource && Resource->ReadLinearColorPixels(Pixels)
			&& Pixels.Num() == TestResolution * TestResolution))
	{
		return false;
	}

	const int32 LeftIndex = (TestResolution / 2) * TestResolution + TestResolution / 4;
	const int32 RightIndex = (TestResolution / 2) * TestResolution + (3 * TestResolution) / 4;
	TestTrue(
		TEXT("The two height bands receive different IDs"),
		!Pixels[LeftIndex].Equals(Pixels[RightIndex], 1.0e-4f));

	// Exactly two: one means the pass never wrote (a clear) or normalization collapsed the range;
	// more means interpolated seam values or an uncompacted band leaked through.
	TSet<uint32> DistinctIds;
	for (const FLinearColor& Pixel : Pixels)
	{
		DistinctIds.Add(Pixel.ToFColor(false).ToPackedRGBA());
	}
	TestEqual(TEXT("Two occupied bands compact to exactly two IDs"), DistinctIds.Num(), 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
