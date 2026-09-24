// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatChildOutputPreviewTests
{
	constexpr int32 Resolution = 128;

	bool ComposeAndWait(
		FMixtormatGpuCompositor& Compositor,
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		const FMixtormatDebugPreviewSettings& Debug)
	{
		if (!Compositor.RequestCompose(Layers, Groups, FSimpleDelegate(), Debug))
		{
			return false;
		}
		FlushRenderingCommands();
		return true;
	}

	bool ReadDebug(FMixtormatGpuCompositor& Compositor, TArray<FLinearColor>& OutPixels)
	{
		UTextureRenderTarget2D* Target = Compositor.GetDebugOutput();
		FTextureRenderTargetResource* Resource =
			Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		return Resource && Resource->ReadLinearColorPixels(OutPixels)
			&& OutPixels.Num() == Resolution * Resolution;
	}

	FMixtormatDebugPreviewSettings ChildOutput(
		const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& Child,
		const EMixtormatPreviewOutputKind Kind,
		const FName OutputName = NAME_None,
		const FName GapMaskName = NAME_None)
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
		Debug.ChildTarget.OwnerId = Layer.LayerId;
		Debug.ChildTarget.ChildId = Child.ChildId;
		Debug.ChildTarget.Kind = Kind;
		Debug.ChildTarget.OutputName = OutputName;
		Debug.ChildTarget.GapMaskName = GapMaskName;
		return Debug;
	}

	float MaskValue(const FLinearColor& Pixel)
	{
		constexpr float Low = 0.02f;
		constexpr float High = 0.25f;
		return FMath::Clamp((Pixel.G - Low) / (High - Low), 0.0f, 1.0f);
	}

	bool HasMaskRange(const TArray<FLinearColor>& Pixels)
	{
		bool bDark = false;
		bool bBright = false;
		for (const FLinearColor& Pixel : Pixels)
		{
			const float Value = MaskValue(Pixel);
			bDark |= Value <= 0.1f;
			bBright |= Value >= 0.6f;
		}
		return bDark && bBright;
	}

	int32 CountBlackPixels(const TArray<FLinearColor>& Pixels)
	{
		int32 Count = 0;
		for (const FLinearColor& Pixel : Pixels)
		{
			if (Pixel.R <= 1.0e-4f && Pixel.G <= 1.0e-4f && Pixel.B <= 1.0e-4f)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountRegionColors(const TArray<FLinearColor>& Pixels)
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
	}

	bool IsUniformClear(const TArray<FLinearColor>& Pixels)
	{
		if (Pixels.IsEmpty())
		{
			return false;
		}
		const FLinearColor Clear = Pixels[0];
		return Pixels.ContainsByPredicate([Clear](const FLinearColor& Pixel)
		{
			return !Pixel.Equals(Clear, 1.0e-5f);
		}) == false;
	}

	UTexture2D* MakeTwoBandRAMH()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8);
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
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const bool bLeft = X < Resolution / 2;
				Pixels[Y * Resolution + X] =
					FColor(bLeft ? 48 : 208, 128, 0, bLeft ? 48 : 208);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		FlushRenderingCommands();
		return Texture;
	}

	UMixtormatSurface* MakeSurface(UTexture2D* RAMH)
	{
		UMixtormatSurface* Surface = NewObject<UMixtormatSurface>(
			GetTransientPackage(), NAME_None, RF_Transient);
		if (Surface)
		{
			Surface->RoughnessAOMetallic = RAMH;
		}
		return Surface;
	}

	FMixtormatLayerChild MakePattern()
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::PatternId;
		Child.PatternId.Rows = 8;
		Child.PatternId.Columns = 8;
		Child.PatternId.GapPixels = 4.0f;
		return Child;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatChildOutputPreviewGateTest,
	"Mixtormat.Compositor.ChildOutputPreviewGate",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatChildOutputPreviewGateTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatChildOutputPreviewTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}
	const TArray<FMixtormatLayerGroup> NoGroups;

	// Generated Mask uses the existing LayerMask mode, but still exercises the generic debug blit.
	{
		FMixtormatLayer Lower;
		Lower.Type = EMixtormatLayerType::Fill;
		Lower.ConstantHeight = 0.85f;
		FMixtormatLayer Upper;
		Upper.Type = EMixtormatLayerType::Fill;
		FMixtormatLayerChild& Generated = Upper.Children.AddDefaulted_GetRef();
		Generated.Type = EMixtormatLayerChildType::Generated;
		Generated.Generated.HeightWeight = 1.0f;
		Generated.Generated.BlendMode = EMixtormatMaskBlendMode::Replace;

		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::LayerMask;
		Debug.LayerIndex = 1;
		Debug.ChildIndex = 0;
		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Generated Mask preview composes"),
				ComposeAndWait(Compositor, {Lower, Upper}, NoGroups, Debug))
			|| !TestTrue(TEXT("Generated Mask preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Generated Mask preview publishes coverage"),
			Pixels.ContainsByPredicate([](const FLinearColor& Pixel) { return MaskValue(Pixel) > 0.5f; }));
	}

	// Pattern IDs must contain coloured cells and pure-black grout.
	FMixtormatLayer PatternLayer;
	PatternLayer.Type = EMixtormatLayerType::Fill;
	PatternLayer.Children.Add(MakePattern());
	TArray<FLinearColor> PatternPixels;
	{
		const FMixtormatDebugPreviewSettings Debug = ChildOutput(
			PatternLayer, PatternLayer.Children[0], EMixtormatPreviewOutputKind::RegionIds);
		if (!TestTrue(TEXT("Pattern IDs preview composes"),
				ComposeAndWait(Compositor, {PatternLayer}, NoGroups, Debug))
			|| !TestTrue(TEXT("Pattern IDs preview reads"), ReadDebug(Compositor, PatternPixels)))
		{
			return false;
		}
		TestTrue(TEXT("Pattern IDs publishes several region colours"), CountRegionColors(PatternPixels) > 4);
		TestTrue(TEXT("Pattern gaps preview as black"), CountBlackPixels(PatternPixels) > 0);
	}

	// Combine IDs with Pattern upstream.
	{
		FMixtormatLayer Layer = PatternLayer;
		// Close grout so neighbouring IDs share boundaries and Amount 1 can coarsen them.
		Layer.Children[0].PatternId.GapPixels = 0.0f;
		FMixtormatLayerChild& Combine = Layer.Children.AddDefaulted_GetRef();
		Combine.Type = EMixtormatLayerChildType::CombineId;
		Combine.CombineId.Amount = 1.0f;
		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Pattern to Combine IDs preview composes"),
				ComposeAndWait(Compositor, {Layer}, NoGroups,
					ChildOutput(Layer, Combine, EMixtormatPreviewOutputKind::RegionIds)))
			|| !TestTrue(TEXT("Pattern Combine IDs preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Pattern Combine IDs keeps valid regions"), CountRegionColors(Pixels) > 0);
		TestTrue(TEXT("Pattern Combine IDs coarsens the source"),
			CountRegionColors(Pixels) < CountRegionColors(PatternPixels));
	}

	// Combine IDs with Cluster upstream.
	{
		TStrongObjectPtr<UTexture2D> RAMH(MakeTwoBandRAMH());
		TStrongObjectPtr<UMixtormatSurface> Surface(MakeSurface(RAMH.Get()));
		if (!TestNotNull(TEXT("Cluster fixture texture exists"), RAMH.Get())
			|| !TestNotNull(TEXT("Cluster fixture surface exists"), Surface.Get()))
		{
			return false;
		}

		FMixtormatLayer Layer;
		Layer.Type = EMixtormatLayerType::Fill;
		Layer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(FSoftObjectPath(Surface.Get()));
		FMixtormatLayerChild& Cluster = Layer.Children.AddDefaulted_GetRef();
		Cluster.Type = EMixtormatLayerChildType::Filter;
		Cluster.Filter.Threshold = 0.33f;
		Cluster.Filter.HeightInfluence = 1.0f;
		FMixtormatLayerChild& Combine = Layer.Children.AddDefaulted_GetRef();
		Combine.Type = EMixtormatLayerChildType::CombineId;
		Combine.CombineId.Amount = 0.0f;

		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Cluster to Combine IDs preview composes"),
				ComposeAndWait(Compositor, {Layer}, NoGroups,
					ChildOutput(Layer, Combine, EMixtormatPreviewOutputKind::RegionIds)))
			|| !TestTrue(TEXT("Cluster Combine IDs preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Cluster Combine IDs publishes both source regions"), CountRegionColors(Pixels) >= 2);
	}

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
	{
		return false;
	}

	// Breakup Region IDs and every named scalar output.
	{
		FMixtormatLayer Layer;
		Layer.Type = EMixtormatLayerType::Fill;
		FMixtormatLayerChild& Breakup = Layer.Children.AddDefaulted_GetRef();
		Breakup.Type = EMixtormatLayerChildType::Effect;
		Breakup.Effect.ProceduralType = EMixtormatEffectType::Breakup;
		Breakup.Effect.BreakupAmount = 0.0f;
		Breakup.Effect.BreakupGapWidth = 6.0f;
		Breakup.Effect.BreakupMaskTexture =
			TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));

		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Breakup Region IDs preview composes"),
				ComposeAndWait(Compositor, {Layer}, NoGroups,
					ChildOutput(Layer, Breakup, EMixtormatPreviewOutputKind::RegionIds,
						NAME_None, FName(TEXT("Gap")))))
			|| !TestTrue(TEXT("Breakup Region IDs preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Breakup Region IDs contains pieces"), CountRegionColors(Pixels) > 1);
		TestTrue(TEXT("Breakup Region IDs blackens gaps"), CountBlackPixels(Pixels) > 0);

		for (const FName Output : {FName(TEXT("Gap")), FName(TEXT("Edge")), FName(TEXT("Pieces"))})
		{
			Pixels.Reset();
			if (!TestTrue(*FString::Printf(TEXT("Breakup %s preview composes"), *Output.ToString()),
					ComposeAndWait(Compositor, {Layer}, NoGroups,
						ChildOutput(Layer, Breakup, EMixtormatPreviewOutputKind::Mask, Output)))
				|| !TestTrue(*FString::Printf(TEXT("Breakup %s preview reads"), *Output.ToString()),
					ReadDebug(Compositor, Pixels)))
			{
				return false;
			}
			TestTrue(*FString::Printf(TEXT("Breakup %s publishes structured coverage"), *Output.ToString()),
				HasMaskRange(Pixels));
		}
	}

	// Worn Edges publishes Wear after its deferred surface pass.
	{
		FMixtormatLayer Layer;
		Layer.Type = EMixtormatLayerType::Fill;
		Layer.Children.Add(MakePattern());
		FMixtormatLayerChild& Worn = Layer.Children.AddDefaulted_GetRef();
		Worn.Type = EMixtormatLayerChildType::Effect;
		Worn.Effect.ProceduralType = EMixtormatEffectType::WornEdges;
		Worn.Effect.EdgeWearRadius = 8;
		Worn.Effect.EdgeWearDirections = 8;
		Worn.Effect.EdgeWearStrength = 1.0f;

		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Worn Wear preview composes"),
				ComposeAndWait(Compositor, {Layer}, NoGroups,
					ChildOutput(Layer, Worn, EMixtormatPreviewOutputKind::Mask, FName(TEXT("Wear")))))
			|| !TestTrue(TEXT("Worn Wear preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Worn Wear publishes structured coverage"), HasMaskRange(Pixels));
	}

	// A group-authored child must be addressed by its per-member effective ID.
	{
		TArray<FMixtormatLayer> Layers;
		FMixtormatLayer& Layer = Layers.AddDefaulted_GetRef();
		Layer.Type = EMixtormatLayerType::Fill;
		TArray<FMixtormatLayerGroup> Groups;
		FMixtormatLayerGroup& Group = Groups.AddDefaulted_GetRef();
		Layer.GroupId = Group.GroupId;
		Group.Children.Add(MakePattern());

		const FGuid AuthoredChildId = Group.Children[0].ChildId;
		const FGuid EffectiveChildId = MixtormatLayerGroups::MakeEffectiveChildId(
			Group.GroupId, AuthoredChildId, Layer.LayerId);
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
		Debug.ChildTarget.OwnerId = Layer.LayerId;
		Debug.ChildTarget.ChildId = EffectiveChildId;
		Debug.ChildTarget.Kind = EMixtormatPreviewOutputKind::RegionIds;

		TArray<FLinearColor> Pixels;
		if (!TestTrue(TEXT("Group-authored preview composes with effective ID"),
				ComposeAndWait(Compositor, Layers, Groups, Debug))
			|| !TestTrue(TEXT("Group-authored preview reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Effective group child ID resolves to preview output"), CountRegionColors(Pixels) > 4);

		// Removing the authored child leaves the old target stale. The next compose must clear the
		// debug target instead of displaying the previous frame under a dead eye selection.
		Groups[0].Children.Reset();
		Pixels.Reset();
		if (!TestTrue(TEXT("Stale child-output target composes safely"),
				ComposeAndWait(Compositor, Layers, Groups, Debug))
			|| !TestTrue(TEXT("Stale child-output target reads"), ReadDebug(Compositor, Pixels)))
		{
			return false;
		}
		TestTrue(TEXT("Invalidated child-output preview clears stale pixels"), IsUniformClear(Pixels));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatHierarchicalIdGroupTest,
	"Mixtormat.Compositor.HierarchicalIdGroup",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatHierarchicalIdGroupTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatChildOutputPreviewTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(Resolution, Resolution))))
	{
		return false;
	}
	const TArray<FMixtormatLayerGroup> NoGroups;
	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;
	FMixtormatLayerChild& Group = Layer.Children.AddDefaulted_GetRef();
	Group.Type = EMixtormatLayerChildType::IdGroup;
	const FGuid GroupId = Group.ChildId;

	FMixtormatLayerChild& PatternA = Layer.Children.AddDefaulted_GetRef();
	PatternA.Type = EMixtormatLayerChildType::PatternId;
	PatternA.ScopeOwnerChildId = GroupId;
	PatternA.PatternId.Rows = 1;
	PatternA.PatternId.Columns = 1;
	PatternA.PatternId.GapPixels = 0.0f;

	FMixtormatLayerChild& PatternB = Layer.Children.AddDefaulted_GetRef();
	PatternB.Type = EMixtormatLayerChildType::PatternId;
	PatternB.ScopeOwnerChildId = GroupId;
	PatternB.PatternId.Rows = 1;
	PatternB.PatternId.Columns = 2;
	PatternB.PatternId.GapPixels = 0.0f;

	const auto ReadPreview = [&Compositor, &NoGroups](
		const FMixtormatLayer& InLayer,
		const FMixtormatLayerChild& Child,
		const EMixtormatPreviewOutputKind Kind,
		TArray<FLinearColor>& Pixels,
		const FName Output = NAME_None)
	{
		return ComposeAndWait(Compositor, {InLayer}, NoGroups, ChildOutput(InLayer, Child, Kind, Output))
			&& ReadDebug(Compositor, Pixels);
	};

	TArray<FLinearColor> A;
	TArray<FLinearColor> B;
	TArray<FLinearColor> Difference;
	if (!TestTrue(TEXT("First nested Pattern previews"), ReadPreview(Layer, Layer.Children[1],
			EMixtormatPreviewOutputKind::RegionIds, A))
		|| !TestTrue(TEXT("Second nested Pattern previews"), ReadPreview(Layer, Layer.Children[2],
			EMixtormatPreviewOutputKind::RegionIds, B))
		|| !TestTrue(TEXT("Difference ID Group previews"), ReadPreview(Layer, Layer.Children[0],
			EMixtormatPreviewOutputKind::RegionIds, Difference)))
	{
		return false;
	}

	int32 DifferentOverlapPixels = 0;
	int32 NewDifferenceIds = 0;
	for (int32 Index = 0; Index < Difference.Num(); ++Index)
	{
		if (!A[Index].Equals(B[Index], 1.0e-5f))
		{
			++DifferentOverlapPixels;
			NewDifferenceIds += !Difference[Index].Equals(A[Index], 1.0e-5f)
				&& !Difference[Index].Equals(B[Index], 1.0e-5f) ? 1 : 0;
		}
	}
	TestTrue(TEXT("Nested patterns overlap with different IDs"), DifferentOverlapPixels > 0);
	TestEqual(TEXT("Difference creates an extra ID for every differing overlap"),
		NewDifferenceIds, DifferentOverlapPixels);

	Layer.Children[0].IdGroup.Mode = EMixtormatIdGroupMode::MaxId;
	TArray<FLinearColor> MaxIds;
	if (!TestTrue(TEXT("Max ID Group previews"), ReadPreview(Layer, Layer.Children[0],
		EMixtormatPreviewOutputKind::RegionIds, MaxIds)))
	{
		return false;
	}
	int32 NonSourcePixels = 0;
	for (int32 Index = 0; Index < MaxIds.Num(); ++Index)
	{
		NonSourcePixels += !MaxIds[Index].Equals(A[Index], 1.0e-5f)
			&& !MaxIds[Index].Equals(B[Index], 1.0e-5f) ? 1 : 0;
	}
	TestEqual(TEXT("Max ID always routes one child ID"), NonSourcePixels, 0);

	TArray<FLinearColor> Boundary;
	TestTrue(TEXT("ID Group Boundary previews"), ReadPreview(Layer, Layer.Children[0],
		EMixtormatPreviewOutputKind::Mask, Boundary, FName(TEXT("Boundary"))));
	TestTrue(TEXT("ID Group publishes a structured Boundary"), HasMaskRange(Boundary));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
