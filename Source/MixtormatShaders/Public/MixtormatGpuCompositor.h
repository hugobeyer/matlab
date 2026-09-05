#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/TextureRenderTarget2D.h"

class UMaterialInstanceDynamic;
struct FMixtormatLayer;

// Cache of generated networks that survive between composites. Defined in the compositor
// translation unit and only ever touched on the render thread; the compositor holds it so that
// the lifetime is tied to the panel rather than to the module, and hands a shared reference to
// each render command so a composite still in flight keeps it alive.
struct FMixtormatNetworkCache;

enum class EMixtormatDebugPreviewMode : uint8
{
	None,
	GeneratedFeature,
	HeightBlend,
	ContactAO,
	BorderNormal,
	LayerMask
};

struct FMixtormatDebugPreviewSettings
{
	EMixtormatDebugPreviewMode Mode = EMixtormatDebugPreviewMode::None;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
};

class MIXTORMATSHADERS_API FMixtormatGpuCompositor final
{
public:
	FMixtormatGpuCompositor();
	~FMixtormatGpuCompositor();

	bool Initialize(FIntPoint InResolution = FIntPoint(1024, 1024));
	bool RequestCompose(
		const TArray<FMixtormatLayer>& Layers,
		FSimpleDelegate OnComplete = FSimpleDelegate(),
		FMixtormatDebugPreviewSettings DebugSettings = FMixtormatDebugPreviewSettings());
	void BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const;

	bool IsInitialized() const { return bInitialized; }
	FIntPoint GetResolution() const { return Resolution; }
	UTextureRenderTarget2D* GetBaseColorOutput() const;
	UTextureRenderTarget2D* GetNormalOutput() const;
	UTextureRenderTarget2D* GetRAMOutput() const;
	UTextureRenderTarget2D* GetHeightOutput() const;
	UTextureRenderTarget2D* GetDebugOutput() const;

private:
	struct FTargetSet
	{
		TStrongObjectPtr<UTextureRenderTarget2D> BaseColor;
		TStrongObjectPtr<UTextureRenderTarget2D> Normal;
		TStrongObjectPtr<UTextureRenderTarget2D> RAM;
		TStrongObjectPtr<UTextureRenderTarget2D> Height;
		TStrongObjectPtr<UTextureRenderTarget2D> Debug;
	};

	FTargetSet Targets[2];

	// Craquelure networks keyed on the parameters that shape them. Growing one is by a wide
	// margin the most expensive thing in the graph, and almost nothing a user touches while
	// tuning actually changes it, so it is kept rather than regrown every frame of a drag.
	TSharedPtr<FMixtormatNetworkCache, ESPMode::ThreadSafe> NetworkCache;

	FIntPoint Resolution = FIntPoint::ZeroValue;
	int32 PublishedTargetIndex = 0;
	bool bInitialized = false;
};
