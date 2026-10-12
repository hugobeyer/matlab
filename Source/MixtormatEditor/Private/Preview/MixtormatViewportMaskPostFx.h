// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FAdvancedPreviewScene;
class UMaterial;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
enum class EMixtormatPreviewOutputKind : uint8;

// A viewport-only after-tonemapping mask blend. Its capture proxy carries the
// identical mesh, transform, height field and displacement controls as the lit
// preview. The capture writes authored mesh UVs + a coverage bit into HDR pixels.
// Neither the scene post process nor the proxy changes the authored PBR material.
class FMixtormatViewportMaskPostFx final
{
public:
	explicit FMixtormatViewportMaskPostFx(FAdvancedPreviewScene& InScene);
	~FMixtormatViewportMaskPostFx();

	// Called for W mask or E raw Flow diagnostics; no capture/PP cost while off.
	void Update(
		const UStaticMeshComponent& PreviewMesh,
		UTextureRenderTarget2D* MaskField,
		UTextureRenderTarget2D* HeightField,
		FIntPoint ViewSize,
		const FVector& CameraLocation,
		const FRotator& CameraRotation,
		float FovDegrees,
		bool bDisplace,
		float DisplacementAmount,
		float OverlayOpacity,
		UTextureRenderTarget2D* FlowField,
		EMixtormatPreviewOutputKind OutputKind,
		bool bScalarDiagnostic);
	void Disable();
	UMaterialInstanceDynamic* GetBlendInstance() const { return bReady ? BlendInstance.Get() : nullptr; }

private:
	bool EnsureResources(UTextureRenderTarget2D* InitialField);
	FAdvancedPreviewScene& Scene;
	UStaticMeshComponent* UvProxy = nullptr;
	USceneCaptureComponent2D* UvCapture = nullptr;
	TStrongObjectPtr<UTextureRenderTarget2D> UvTarget;
	TStrongObjectPtr<UMaterial> UvMaterial;

	TStrongObjectPtr<UMaterialInstanceDynamic> UvInstance;
	TStrongObjectPtr<UMaterialInstanceDynamic> BlendInstance;
	FIntPoint CapturedSize = FIntPoint::ZeroValue;
	bool bReady = false;
};
