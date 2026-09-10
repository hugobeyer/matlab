#pragma once

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AdvancedPreviewScene.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "MixtormatGpuCompositor.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
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
	Rim,
	Workshop
};

enum class EMixtormatPreviewQuality : uint8
{
	Low,
	Medium,
	High
};

// Anti-aliasing for the preview, chosen per viewport rather than for the editor.
//
// Driven through this viewport's show flags rather than r.AntiAliasingMethod, which is a global
// and would drag every other editor viewport along with it. FSceneView::SetupAntiAliasingMethod
// reads the project default -- TSR -- and then downgrades it: clearing TemporalAA turns TSR into
// FXAA. That is one flag and no console variable.
//
// MSAA is deliberately absent. It exists only on the desktop forward renderer, this project is
// deferred -- Lumen, Substrate, virtual shadow maps -- and it anti-aliases triangle coverage
// anyway. Everything worth looking at here is texture and shader detail on a near-flat mesh,
// which is the aliasing MSAA does not touch. Supersampling is the answer to that, which is what
// the third option is.
enum class EMixtormatPreviewAntiAliasing : uint8
{
	// Single frame, no history. Softer, but a crack one pixel wide stays where it is instead of
	// swimming, which is what you want while judging a mask.
	Fxaa,

	// The project default. Resolves thin detail best when the image is still, but it accumulates
	// over frames, so hairline features shimmer while the history reconverges after a camera
	// move or a recomposite.
	Temporal
};

// Render scale, kept separate from the method above rather than folded into it as a
// "supersampled" mode.
//
// The two are orthogonal and the intents differ at each end: above 100 the extra samples are
// real and fix shading aliasing rather than hiding it, which is what FXAA at 150 is for; below
// 100 the point is fill rate on an expensive graph, which has nothing to do with which resolve
// runs afterwards. Folding them together would have made the scale unreachable except at one
// value.
namespace MixtormatPreviewScreenPercentage
{
	constexpr int32 Minimum = 75;
	constexpr int32 Maximum = 200;
	constexpr int32 Default = 100;
}


class SMixtormatPreviewViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SMixtormatPreviewViewport) {}
		SLATE_EVENT(FSimpleDelegate, OnToggleOverlayUi)
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

	// Multipliers on whatever the current lighting mode chose, not absolute brightnesses.
	//
	// Each preset already decides how bright its key and plugin-owned environment should be.
	// Multipliers keep 1.0 meaning "what this preset intended" while preserving user adjustments
	// when switching between lighting modes.
	void SetPreviewLightIntensity(float Scale);
	void SetPreviewSkylightIntensity(float Scale);
	void SetPreviewMesh(EMixtormatPreviewMesh MeshType);
	void SetStudioLighting(EMixtormatStudioLighting LightingPreset);
	void SetPreviewQuality(EMixtormatPreviewQuality Quality);
	void SetPreviewAntiAliasing(EMixtormatPreviewAntiAliasing AntiAliasing);
	void SetPreviewScreenPercentage(int32 Percentage);
	void SetCameraFov(float FovDegrees);
	void ResetCameraAndLighting();
	void FocusCamera();
	UTextureRenderTarget2D* GetCompositedBaseColor() const;
	UTextureRenderTarget2D* GetCompositedNormal() const;
	UTextureRenderTarget2D* GetCompositedRAM() const;
	UTextureRenderTarget2D* GetCompositedHeight() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	friend class FMixtormatPreviewViewportClient;

	void OrbitCamera(float YawDelta, float PitchDelta);
	void RotateLighting(float YawDelta, float PitchDelta);
	void ZoomCamera(float ZoomDelta);
	void ToggleOverlayUi();
	void UpdateCamera();
	void UpdateStudioFog();
	void UpdateStudioEnvironmentLighting();
	void UpdateDebugLightVisibility();
	void InvalidateDisplacementShadows();
	bool ComposeLayersWithDebug(
		const TArray<FMixtormatLayer>& Layers,
		int32 Resolution,
		FMixtormatDebugPreviewSettings DebugSettings);

	FAdvancedPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	FSimpleDelegate OnToggleOverlayUi;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	UExponentialHeightFogComponent* StudioFogComponent = nullptr;
	TWeakObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;
	TStrongObjectPtr<UMaterial> DebugPreviewMaterial;
	TUniquePtr<FMixtormatGpuCompositor> LayerCompositor;
	TUniquePtr<FPreviewSceneProfile> StudioPreviewProfile;
	TStrongObjectPtr<UTextureCube> StudioEnvironmentCubemap;
	bool bUsingLayerPreview = false;
	bool bUsingDebugPreview = false;
	EMixtormatDebugPreviewMode bDebugPreviewMode = EMixtormatDebugPreviewMode::None;
	int32 bDebugLayerIndex = INDEX_NONE;
	int32 bDebugChildIndex = INDEX_NONE;
	bool bUsingStudioEnvironment = false;
	bool bDisplacementEnabled = false;
	float DisplacementAmount = 1.0f;
	float CameraDistance = MixtormatPreviewCamera::DistanceDefault;
	float CameraYaw = MixtormatPreviewCamera::YawDefault;
	float CameraPitch = MixtormatPreviewCamera::PitchDefault;
	float CameraFov = MixtormatPreviewCamera::FovDefault;
	float LightingYaw = -45.0f;
	float LightingPitch = -35.0f;

	// What the current mode asked for, before the user's multipliers. Held so a change to either
	// slider can be re-applied without re-running the whole preset, and so switching preset
	// keeps the multipliers rather than resetting them.
	float BaseLightBrightness = 2.0f;
	float BaseSkyBrightness = 0.45f;
	float LightIntensityScale = 1.0f;
	float SkylightIntensityScale = 1.0f;
	void ApplyLightIntensities();
	float EnvironmentYaw = 0.0f;
	FVector PreviewTarget = FVector(0.0f, 0.0f, 50.0f);
};
