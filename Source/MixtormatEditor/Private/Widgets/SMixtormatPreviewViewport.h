#pragma once

#include "AdvancedPreviewScene.h"
#include "MixtormatGpuCompositor.h"
#include "SEditorViewport.h"
#include "UObject/StrongObjectPtr.h"

class FEditorViewportClient;
class FMixtormatPreviewViewportClient;
class UExponentialHeightFogComponent;
class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureCube;
class UTextureRenderTarget2D;
struct FMixtormatLayer;
struct FPreviewSceneProfile;

enum class EMixtormatPreviewMesh : uint8
{
	Sphere,
	Plane,
	Cube
};

enum class EMixtormatStudioLighting : uint8
{
	Neutral,
	Soft,
	Dramatic,
	Rim
};

enum class EMixtormatPreviewQuality : uint8
{
	Low,
	Medium,
	High
};

class SMixtormatPreviewViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SMixtormatPreviewViewport) {}
	SLATE_END_ARGS()

	SMixtormatPreviewViewport();
	virtual ~SMixtormatPreviewViewport() override;

	void Construct(const FArguments& InArgs);
	void SetPreviewMaterial(UMaterialInterface* Material);
	void SetPreviewLayers(
		const TArray<FMixtormatLayer>& Layers,
		int32 Resolution,
		FMixtormatDebugPreviewSettings DebugSettings = FMixtormatDebugPreviewSettings());
	void SetDebugPreview(FMixtormatDebugPreviewSettings DebugSettings);
	bool ComposeLayersAtResolution(const TArray<FMixtormatLayer>& Layers, int32 Resolution);
	void SetPreviewScalarParameter(FName ParameterName, float Value);
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void SetPreviewMesh(EMixtormatPreviewMesh MeshType);
	void SetStudioLighting(EMixtormatStudioLighting LightingPreset);
	void SetHdriLighting(UTextureCube* Cubemap);
	void SetPreviewQuality(EMixtormatPreviewQuality Quality);
	void SetCameraFov(float FovDegrees);
	void ResetCameraAndLighting();
	UTextureRenderTarget2D* GetCompositedBaseColor() const;
	UTextureRenderTarget2D* GetCompositedNormal() const;
	UTextureRenderTarget2D* GetCompositedRAM() const;
	UTextureRenderTarget2D* GetCompositedHeight() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	friend class FMixtormatPreviewViewportClient;

	void OrbitCamera(float YawDelta, float PitchDelta);
	void RotateLighting(float YawDelta);
	void ZoomCamera(float ZoomDelta);
	void UpdateCamera();
	void UpdateStudioFog();
	void UpdateHdriFillLight();
	void UpdateDebugLightVisibility();
	bool ComposeLayersWithDebug(
		const TArray<FMixtormatLayer>& Layers,
		int32 Resolution,
		FMixtormatDebugPreviewSettings DebugSettings);

	FAdvancedPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	UExponentialHeightFogComponent* StudioFogComponent = nullptr;
	TWeakObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;
	TStrongObjectPtr<UMaterial> DebugPreviewMaterial;
	TUniquePtr<FMixtormatGpuCompositor> LayerCompositor;
	TUniquePtr<FPreviewSceneProfile> DefaultPreviewProfile;
	TUniquePtr<FPreviewSceneProfile> HdriPreviewProfile;
	bool bUsingLayerPreview = false;
	bool bUsingDebugPreview = false;
	EMixtormatDebugPreviewMode bDebugPreviewMode = EMixtormatDebugPreviewMode::None;
	int32 bDebugLayerIndex = INDEX_NONE;
	int32 bDebugChildIndex = INDEX_NONE;
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
