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

// Preview camera. Named because each of these was written as a literal in four to six places --
// the member's initialiser, the reset, the slider's range, and the slider's restore-default --
// and they have to agree or the control lies about what it is restoring.
namespace MixtormatPreviewCamera
{
	// Narrower than a game camera on purpose. A material is judged on its surface rather than
	// its silhouette, and a long lens keeps the perspective divergence across the sample low
	// enough that tiling and normal detail read the same at the edges as at the centre.
	constexpr float FovDefault = 40.0f;
	constexpr float FovMinimum = 20.0f;
	constexpr float FovMaximum = 90.0f;

	constexpr float DistanceDefault = 225.0f;
	constexpr float DistanceMinimum = 75.0f;
	constexpr float DistanceMaximum = 400.0f;

	constexpr float YawDefault = 195.0f;
	constexpr float PitchDefault = -8.0f;

	// Slack left around the focused bounds so the mesh does not sit edge to edge in the frame.
	constexpr float FocusMargin = 1.15f;
}

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
	float CameraDistance = MixtormatPreviewCamera::DistanceDefault;
	float CameraYaw = MixtormatPreviewCamera::YawDefault;
	float CameraPitch = MixtormatPreviewCamera::PitchDefault;
	float CameraFov = MixtormatPreviewCamera::FovDefault;
	float LightingYaw = -45.0f;
	float HdriYaw = 0.0f;
	FVector PreviewTarget = FVector(0.0f, 0.0f, 50.0f);
};
