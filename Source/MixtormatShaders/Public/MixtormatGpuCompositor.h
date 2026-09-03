#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/TextureRenderTarget2D.h"

class UMaterialInstanceDynamic;
struct FMixtormatLayer;

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
	FMixtormatGpuCompositor() = default;
	~FMixtormatGpuCompositor() = default;

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
	FIntPoint Resolution = FIntPoint::ZeroValue;
	int32 PublishedTargetIndex = 0;
	bool bInitialized = false;
};
