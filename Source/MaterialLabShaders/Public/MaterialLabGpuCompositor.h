#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/TextureRenderTarget2D.h"

class UMaterialInstanceDynamic;
struct FMaterialLabLayer;

class MATERIALLABSHADERS_API FMaterialLabGpuCompositor final
{
public:
	FMaterialLabGpuCompositor() = default;
	~FMaterialLabGpuCompositor() = default;

	bool Initialize(FIntPoint InResolution = FIntPoint(1024, 1024));
	bool RequestCompose(
		const TArray<FMaterialLabLayer>& Layers,
		FSimpleDelegate OnComplete = FSimpleDelegate());
	void BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const;

	bool IsInitialized() const { return bInitialized; }
	FIntPoint GetResolution() const { return Resolution; }
	UTextureRenderTarget2D* GetBaseColorOutput() const;
	UTextureRenderTarget2D* GetNormalOutput() const;
	UTextureRenderTarget2D* GetRAMOutput() const;
	UTextureRenderTarget2D* GetHeightOutput() const;

private:
	struct FTargetSet
	{
		TStrongObjectPtr<UTextureRenderTarget2D> BaseColor;
		TStrongObjectPtr<UTextureRenderTarget2D> Normal;
		TStrongObjectPtr<UTextureRenderTarget2D> RAM;
		TStrongObjectPtr<UTextureRenderTarget2D> Height;
	};

	FTargetSet Targets[2];
	FIntPoint Resolution = FIntPoint::ZeroValue;
	int32 PublishedTargetIndex = 0;
	bool bInitialized = false;
};
