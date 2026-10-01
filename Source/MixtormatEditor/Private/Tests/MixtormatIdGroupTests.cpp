// Copyright 2026 Hugo Beyer. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

namespace MixtormatIdGroupTests
{
	constexpr int32 Resolution = 256;
	constexpr uint32 InvalidId = 0xffffffffu;

	FMixtormatLayer MakeLayer()
	{
		FMixtormatLayer Layer;
		Layer.Type = EMixtormatLayerType::Fill;
		Layer.bEnabled = true;
		Layer.BaseColor = FLinearColor(0.5f, 0.5f, 0.5f);
		Layer.HeightSource = EMixtormatHeightSource::Constant;
		Layer.ConstantHeight = 0.35f;
		Layer.bHeightBlendEnabled = false;
		return Layer;
	}

	FMixtormatLayerChild Group(EMixtormatIdGroupMode Mode, FGuid Owner = FGuid())
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::IdGroup;
		Child.ScopeOwnerChildId = Owner;
		Child.IdGroup.Mode = Mode;
		return Child;
	}

	FMixtormatLayerChild Pattern(int32 Rows, int32 Columns, FGuid Owner = FGuid())
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::PatternId;
		Child.ScopeOwnerChildId = Owner;
		Child.PatternId.PatternMode = EMixtormatPatternMode::Grid;
		Child.PatternId.Rows = Rows;
		Child.PatternId.Columns = Columns;
		Child.PatternId.Jitter = 0.0f;
		Child.PatternId.GapPixels = 0.0f;
		Child.PatternId.GapHeight = 0.0f;
		Child.PatternId.HeightAmount = 0.0f;
		Child.PatternId.BevelHeight = 0.0f;
		Child.PatternId.EdgeRoughnessAmount = 0.0f;
		Child.PatternId.bUVVariation = false;
		return Child;
	}

	bool ReadTarget(UTextureRenderTarget2D* Target, TArray<FLinearColor>& Pixels)
	{
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		return Resource && Resource->ReadLinearColorPixels(Pixels)
			&& Pixels.Num() == Resolution * Resolution;
	}

	bool Submit(FMixtormatGpuCompositor& Compositor, const FMixtormatLayer& Layer,
		const FMixtormatDebugPreviewSettings& Debug)
	{
		Compositor.ResetCaches();
		TArray<FMixtormatLayer> Layers;
		Layers.Add(Layer);
		if (!Compositor.RequestCompose(Layers, FSimpleDelegate(), Debug))
		{
			return false;
		}
		FlushRenderingCommands();
		return true;
	}

	bool ReadIds(FMixtormatGpuCompositor& Compositor, const FMixtormatLayer& Layer,
		FGuid ChildId, TArray<FLinearColor>& Pixels)
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.Mode = EMixtormatDebugPreviewMode::ChildOutput;
		Debug.ChildTarget.OwnerId = Layer.LayerId;
		Debug.ChildTarget.ChildId = ChildId;
		Debug.ChildTarget.Kind = EMixtormatPreviewOutputKind::RegionIds;
		return Submit(Compositor, Layer, Debug) && ReadTarget(Compositor.GetRegionIdPickOutput(), Pixels);
	}

	// Independent uint oracle for the existing shader op. Never reconstruct a full-width ID
	// from its float picker readback: only original pixel-root Pattern IDs are read that way.
	uint32 Hash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	uint32 Fold(uint32 A, uint32 B, EMixtormatIdGroupMode Mode)
	{
		if (A == InvalidId) { return B; }
		if (B == InvalidId || A == B) { return A; }
		if (Mode == EMixtormatIdGroupMode::MaxId) { return FMath::Max(A, B); }
		const uint32 Result = Hash(Hash(FMath::Min(A, B) ^ 0x9e3779b9u)
			^ Hash(FMath::Max(A, B) ^ 0x85ebca6bu));
		return Result == InvalidId ? 0xfffffffeu : Result;
	}

	bool CheckIds(FAutomationTestBase& Test, const TCHAR* Label,
		const TArray<FLinearColor>& Actual, const TArray<uint32>& Expected)
	{
		if (!Test.TestEqual(FString::Printf(TEXT("%s pixel count"), Label), Actual.Num(), Expected.Num()))
		{
			return false;
		}
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			// Match the public picker's float32 conversion, not an inaccurate float-to-uint cast.
			const float Value = Expected[Index] == InvalidId ? -1.0f : static_cast<float>(Expected[Index]);
			Mismatches += Actual[Index].R != Value;
		}
		return Test.TestEqual(FString::Printf(TEXT("%s mismatched IDs"), Label), Mismatches, 0);
	}

	bool ReadConsumer(FMixtormatGpuCompositor& Compositor, const FMixtormatLayer& Layer,
		EMixtormatLayerChildType Type, FGuid ConsumerId, TArray<FLinearColor>& Pixels)
	{
		FMixtormatDebugPreviewSettings Debug;
		Debug.LayerIndex = 0;
		Debug.ChildIndex = Layer.Children.IndexOfByPredicate(
			[ConsumerId](const FMixtormatLayerChild& Child) { return Child.ChildId == ConsumerId; });
		if (Type == EMixtormatLayerChildType::ColorId || Type == EMixtormatLayerChildType::RandomId)
		{
			Debug.Mode = EMixtormatDebugPreviewMode::LayerMask;
		}
		else if (Type == EMixtormatLayerChildType::UvFromIds)
		{
			Debug.Mode = EMixtormatDebugPreviewMode::LayerUV;
		}
		if (!Submit(Compositor, Layer, Debug)) { return false; }
		UTextureRenderTarget2D* Target = Debug.Mode == EMixtormatDebugPreviewMode::None
			? Compositor.GetBaseColorOutput() : Compositor.GetDebugOutput();
		return ReadTarget(Target, Pixels);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
