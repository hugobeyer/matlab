// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FAdvancedPreviewScene;
class UMaterial;
class UMaterialInterface;
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
		UTextureRenderTarget2D* DiagnosticColor,
		EMixtormatPreviewOutputKind OutputKind);
	void Disable();
	UMaterialInstanceDynamic* GetBlendInstance() const { return bReady ? BlendInstance.Get() : nullptr; }

private:
	bool EnsureResources(UTextureRenderTarget2D* InitialField, UMaterialInterface* MasterInterface);
	FAdvancedPreviewScene& Scene;
	UStaticMeshComponent* UvProxy = nullptr;
	USceneCaptureComponent2D* UvCapture = nullptr;
	TStrongObjectPtr<UTextureRenderTarget2D> UvTarget;
	TStrongObjectPtr<UMaterial> UvMaterial;

	TStrongObjectPtr<UMaterialInstanceDynamic> UvInstance;
	TStrongObjectPtr<UMaterialInstanceDynamic> BlendInstance;
	// Built-in fallback for the artist-authored material; created only when no override loads.
	TStrongObjectPtr<UMaterial> DefaultPostMaterial;
	FIntPoint CapturedSize = FIntPoint::ZeroValue;
	bool bReady = false;
	// Latched after a failed initialization so a per-tick Update cannot rebuild the UV target
	// and re-run the material search for a state that cannot change on its own.
	bool bInitializationFailed = false;
};
