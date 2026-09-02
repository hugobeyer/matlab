#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/TextureRenderTarget2D.h"

class UMaterialInstanceDynamic;
struct FMaterialLabLayer;

enum class EMaterialLabDebugPreviewMode : uint8
{
	None,
	GeneratedFeature,
	HeightBlend,
	ContactAO,
	BorderNormal,
	LayerMask
};

struct FMaterialLabDebugPreviewSettings
{
	EMaterialLabDebugPreviewMode Mode = EMaterialLabDebugPreviewMode::None;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
};

class MATERIALLABSHADERS_API FMaterialLabGpuCompositor final
{
public:
	FMaterialLabGpuCompositor() = default;
	~FMaterialLabGpuCompositor() = default;

	bool Initialize(FIntPoint InResolution = FIntPoint(1024, 1024));
	bool RequestCompose(
		const TArray<FMaterialLabLayer>& Layers,
		FSimpleDelegate OnComplete = FSimpleDelegate(),
		FMaterialLabDebugPreviewSettings DebugSettings = FMaterialLabDebugPreviewSettings());
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
