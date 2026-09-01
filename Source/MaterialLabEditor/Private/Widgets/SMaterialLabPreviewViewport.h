#pragma once

#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"

class FEditorViewportClient;
class FMaterialLabGpuCompositor;
class FMaterialLabPreviewViewportClient;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureCube;
class UTextureRenderTarget2D;
struct FMaterialLabLayer;
struct FPreviewSceneProfile;

enum class EMaterialLabPreviewMesh : uint8
{
	Sphere,
	Plane,
	Cube
};

enum class EMaterialLabStudioLighting : uint8
{
	Neutral,
	Soft,
	Dramatic,
	Rim
};

enum class EMaterialLabPreviewQuality : uint8
{
	Low,
	Medium,
	High
};

class SMaterialLabPreviewViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabPreviewViewport) {}
	SLATE_END_ARGS()

	SMaterialLabPreviewViewport();
	virtual ~SMaterialLabPreviewViewport() override;

	void Construct(const FArguments& InArgs);
	void SetPreviewMaterial(UMaterialInterface* Material);
	void SetPreviewLayers(const TArray<FMaterialLabLayer>& Layers, int32 Resolution);
	bool ComposeLayersAtResolution(const TArray<FMaterialLabLayer>& Layers, int32 Resolution);
	void SetPreviewScalarParameter(FName ParameterName, float Value);
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void SetPreviewMesh(EMaterialLabPreviewMesh MeshType);
	void SetStudioLighting(EMaterialLabStudioLighting LightingPreset);
	void SetHdriLighting(UTextureCube* Cubemap);
	void SetPreviewQuality(EMaterialLabPreviewQuality Quality);
	void SetCameraFov(float FovDegrees);
	UTextureRenderTarget2D* GetCompositedBaseColor() const;
	UTextureRenderTarget2D* GetCompositedNormal() const;
	UTextureRenderTarget2D* GetCompositedRAM() const;
	UTextureRenderTarget2D* GetCompositedHeight() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	friend class FMaterialLabPreviewViewportClient;

	void OrbitCamera(float YawDelta, float PitchDelta);
	void RotateLighting(float YawDelta);
	void ZoomCamera(float ZoomDelta);
	void UpdateCamera();
	void UpdateStudioFog();

	FAdvancedPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	UExponentialHeightFogComponent* StudioFogComponent = nullptr;
	TWeakObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;
	TUniquePtr<FMaterialLabGpuCompositor> LayerCompositor;
	TUniquePtr<FPreviewSceneProfile> DefaultPreviewProfile;
	TUniquePtr<FPreviewSceneProfile> HdriPreviewProfile;
	bool bUsingLayerPreview = false;
	bool bUsingHdri = false;
	bool bDisplacementEnabled = false;
	float DisplacementAmount = 1.0f;
	float CameraDistance = 225.0f;
	float CameraYaw = 195.0f;
	float CameraPitch = -8.0f;
	float CameraFov = 50.0f;
	float LightingYaw = -45.0f;
	float HdriYaw = 0.0f;
	FVector PreviewTarget = FVector(0.0f, 0.0f, 50.0f);
};
