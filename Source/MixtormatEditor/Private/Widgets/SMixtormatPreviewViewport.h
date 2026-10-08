// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AdvancedPreviewScene.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "MixtormatGpuCompositor.h"
#include "MixtormatMaterial.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
#include "SEditorViewport.h"
#include "UObject/StrongObjectPtr.h"

class FEditorViewportClient;
class FMixtormatPreviewViewportClient;
class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UBoxReflectionCaptureComponent;
class UStaticMeshComponent;
class UTextureCube;
class UTextureRenderTarget2D;
struct FMixtormatLayer;
struct FPreviewSceneProfile;

enum class EMixtormatPreviewMesh : uint8
{
	Sphere,
	Plane,
	Cube,
	Cylinder
};

// The Plane mesh has a second preview mode: the same authored asset laid flat (Horizontal, its
// orientation at zero rotation, normal +Z) or stood upright so its normal faces +X. It is only
// meaningful for EMixtormatPreviewMesh::Plane; every other mesh ignores it.
enum class EMixtormatPlaneOrientation : uint8
{
	Horizontal,
	VerticalX
};

// Temporary V-key diagnostic cycle: a raw look at one composited output at a time, unlit, with
// no toolbar exposure yet. Material has to stay first and at value 0, with 0 doubling as "off"
// so Material means "untouched".
enum class EMixtormatChannelPreview : uint8
{
	Material,
	BaseColor,
	Normal,
	Roughness,
	AO,
	Metallic,
	F0,
	Height,
	Fuzz
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
	Default,
	Lumen
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
	Temporal,

	// Both AA show flags are disabled. Represented by neither viewport segment being active.
	Off
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


// Asked when Escape arrives in the viewport: returns true when the workspace actually dismissed
// something, so the client only consumes the key when it had an effect and the viewport's own
// Escape behaviour is untouched otherwise.
DECLARE_DELEGATE_RetVal(bool, FMixtormatDismissQuickControls);
DECLARE_DELEGATE_OneParam(FMixtormatCameraFovChanged, float);

class SMixtormatPreviewViewport final : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SMixtormatPreviewViewport) {}
		SLATE_EVENT(FSimpleDelegate, OnToggleOverlayUi)
		SLATE_EVENT(FSimpleDelegate, OnToggleDisplacement)
		SLATE_EVENT(FSimpleDelegate, OnChannelPreviewChanged)
		SLATE_EVENT(FSimpleDelegate, OnCycleModulePreview)
		// Bare Q in the viewport: the workspace opens its quick controls. Routed through the client
		// rather than a global preprocessor, so text entry and Slate's own focus navigation keep
		// their keys everywhere else.
		SLATE_EVENT(FSimpleDelegate, OnRequestQuickControls)
		// Escape in the viewport: the workspace closes them again.
		SLATE_EVENT(FMixtormatDismissQuickControls, OnDismissQuickControls)
		SLATE_EVENT(FMixtormatCameraFovChanged, OnCameraFovChanged)
	SLATE_END_ARGS()

	SMixtormatPreviewViewport();
	virtual ~SMixtormatPreviewViewport() override;

	void Construct(const FArguments& InArgs);
	void SetPreviewMaterial(UMaterialInterface* Material);
	// bInteractive marks a request made mid-scrub: it is rate-limited to the measured compose
	// cost and only the newest is kept. Anything else (typed value, toggle, the drag's final
	// value) is submitted as soon as the previous composite is out of flight.
	void SetPreviewLayers(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		int32 Resolution,
		FMixtormatDebugPreviewSettings DebugSettings = FMixtormatDebugPreviewSettings(),
		bool bInteractive = false);
	void SetDebugPreview(FMixtormatDebugPreviewSettings DebugSettings);
	bool ComposeLayersAtResolution(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		int32 Resolution);
	void SetPreviewScalarParameter(FName ParameterName, float Value);
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void SetFinalSettings(const FMixtormatFinalSettings& Settings) { FinalSettings = Settings; }
	void SetGlobalUVRotation90(bool bEnabled);

	// Multipliers on whatever the current lighting mode chose, not absolute brightnesses.
	//
	// Each preset already decides how bright its key and plugin-owned environment should be.
	// Multipliers keep 1.0 meaning "what this preset intended" while preserving user adjustments
	// when switching between lighting modes.
	void SetPreviewLightIntensity(float Scale);
	void SetPreviewSkylightIntensity(float Scale);
	void SetPreviewMesh(EMixtormatPreviewMesh MeshType, EMixtormatPlaneOrientation PlaneOrientation);
	void SetStudioLighting(EMixtormatStudioLighting LightingPreset);
	void SetPreviewQuality(EMixtormatPreviewQuality Quality);
	void SetPreviewAntiAliasing(EMixtormatPreviewAntiAliasing AntiAliasing);
	void SetPreviewScreenPercentage(int32 Percentage);
	void SetCameraFov(float FovDegrees);
	void ResetCameraAndLighting();
	void FocusCamera();
	// Called by the viewport client when bare Q arrives; the workspace decides what to do.
	void RequestQuickControls();
	// Called by the viewport client when Escape arrives; true when the workspace closed something.
	bool RequestDismissQuickControls();
	UTextureRenderTarget2D* GetCompositedBaseColor() const;
	UTextureRenderTarget2D* GetCompositedNormal() const;
	UTextureRenderTarget2D* GetCompositedRAM() const;
	UTextureRenderTarget2D* GetCompositedHeight() const;
	// The debug view the preview is currently showing, and the raw Region IDs behind it. The
	// second is what the Exact ID picker reads: one float per pixel, -1 where no region covers it.
	UTextureRenderTarget2D* GetCompositedDebug() const;
	UTextureRenderTarget2D* GetRegionIdPick() const;
	EMixtormatChannelPreview GetChannelPreview() const { return ChannelPreview; }
	FString GetChannelPreviewLabel() const;
	// What the viewport is showing right now: Material, a V-key channel (with the Shift+V hint),
	// or the debug view a preview eye turned on.
	FText GetPreviewModeLabel() const;
	void ResetChannelPreview();
	FQuat GetCameraRotation() const;
	FVector GetLightDirection() const;
	// True while the light is being rotated, and briefly after, so the gizmo can show only then.
	bool IsRotatingLighting() const { return FPlatformTime::Seconds() - LastLightRotateTime < 0.4; }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	friend class FMixtormatPreviewViewportClient;

	void OrbitCamera(float YawDelta, float PitchDelta);
	void RotateLighting(float YawDelta, float PitchDelta);
	void ZoomCamera(float ZoomDelta);
	void PanCamera(float DeltaX, float DeltaY);
	void HandleCameraWheel(float WheelDelta, bool bControlDown);
	void ToggleOverlayUi();
	void ToggleDisplacement();
	void CycleChannelPreview();
	void CycleModulePreview();
	void ApplyChannelPreview();
	void UpdateCamera();
	void UpdateStudioFloor();
	void UpdateStudioEnvironmentLighting();
	// A debug or channel view shows raw data on an unlit surface, so everything that lights,
	// reflects or grades the frame has to be off for it. One funnel, re-run after anything that
	// can bring that state back: UpdateScene (lighting preset, rotation), a quality change, and
	// every switch into or out of a data view.
	bool IsUnlitPresentation() const;
	void ApplyPresentationState();
	void UpdatePreviewMeshFloorClearance();
	void InvalidateDisplacementShadows();
	// bWaitForCompletion blocks the game thread until the composite has run. Only a caller that
	// reads the targets back straight away (bake) needs it; the live preview binds the targets
	// and lets the render thread catch up.
	bool ComposeLayersWithDebug(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		int32 Resolution,
		FMixtormatDebugPreviewSettings DebugSettings,
		bool bWaitForCompletion);
	// One timer both measures the composite in flight and releases the pending request.
	EActiveTimerReturnType FlushPendingCompose(double CurrentTime, float DeltaTime);
	bool CanSubmitCompose(bool bInteractive) const;
	// Submits a live-preview composite and starts timing it for the drag-time interval.
	void SubmitCompose(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		int32 Resolution,
		const FMixtormatDebugPreviewSettings& DebugSettings);
	void EnsureComposeTimer();

	// Latest preview request that arrived while the previous composite was still in flight or
	// the drag-time interval had not elapsed. Only the newest is kept: intermediate frames of a
	// drag are never worth rendering late.
	struct FPendingCompose
	{
		TArray<FMixtormatLayer> Layers;
		TArray<FMixtormatLayerGroup> Groups;
		int32 Resolution = 0;
		FMixtormatDebugPreviewSettings DebugSettings;
		bool bInteractive = false;
	};
	TOptional<FPendingCompose> PendingCompose;
	TSharedPtr<FActiveTimerHandle> PendingComposeTimer;

	// Game-thread wall time from submit to the first frame the composite is out of flight,
	// smoothed. It sets how often a drag may submit, so a heavy stack backs off by itself.
	double LastComposeSubmitTime = 0.0;
	double SmoothedComposeSeconds = 0.0;
	bool bMeasuringCompose = false;

	FAdvancedPreviewScene PreviewScene;
	TSharedPtr<FEditorViewportClient> PreviewViewportClient;
	FSimpleDelegate OnToggleOverlayUi;
	FSimpleDelegate OnToggleDisplacement;
	FSimpleDelegate OnChannelPreviewChanged;
	FSimpleDelegate OnCycleModulePreview;
	FSimpleDelegate OnRequestQuickControls;
	FMixtormatDismissQuickControls OnDismissQuickControls;
	FMixtormatCameraFovChanged OnCameraFovChanged;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	TWeakObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;
	TStrongObjectPtr<UMaterial> DebugPreviewMaterial;
	EMixtormatChannelPreview ChannelPreview = EMixtormatChannelPreview::Material;
	TStrongObjectPtr<UMaterial> ChannelPreviewMaterial;
	TWeakObjectPtr<UMaterialInstanceDynamic> ChannelPreviewMaterialInstance;
	// PreviewMaterialInstance above is a weak pointer whose only strong owner is normally
	// PreviewMeshComponent's own material slot. A diagnostic mode detaches it from that slot to
	// show ChannelPreviewMaterialInstance instead, which would otherwise leave it collectible --
	// this is the strong ref that keeps it alive until Material mode reclaims it.
	TStrongObjectPtr<UMaterialInstanceDynamic> RetainedPreviewMaterialInstance;
	TUniquePtr<FMixtormatGpuCompositor> LayerCompositor;
	TUniquePtr<FPreviewSceneProfile> StudioPreviewProfile;
	TStrongObjectPtr<UTextureCube> StudioEnvironmentCubemap;
	// The one specular source that does not depend on a trace succeeding. Re-pointed at the
	// preset's cubemap in SetStudioLighting, alongside the skylight that reads the same asset.
	UBoxReflectionCaptureComponent* StudioReflectionCapture = nullptr;
	// Owned here because FAdvancedPreviewScene::UpdateScene reassigns the floor's material
	// from the profile, so it has to be handed back after every one of those.
	TStrongObjectPtr<UMaterialInstanceDynamic> StudioFloorMaterial;
	bool bUsingLayerPreview = false;
	bool bUsingDebugPreview = false;
	EMixtormatDebugPreviewMode bDebugPreviewMode = EMixtormatDebugPreviewMode::None;
	int32 bDebugLayerIndex = INDEX_NONE;
	int32 bDebugChildIndex = INDEX_NONE;
	bool bUsingStudioEnvironment = false;
	EMixtormatPreviewMesh CurrentPreviewMesh = EMixtormatPreviewMesh::Sphere;
	EMixtormatPlaneOrientation CurrentPlaneOrientation = EMixtormatPlaneOrientation::Horizontal;
	EMixtormatPreviewQuality CurrentPreviewQuality = EMixtormatPreviewQuality::Default;
	bool bDisplacementEnabled = true;
	bool bGlobalUVRotation90 = false;
	float DisplacementAmount = 1.0f;
	FMixtormatFinalSettings FinalSettings;
	float CompositedFuzzInfluence = 0.0f;
	float CameraDistance = MixtormatPreviewCamera::DistanceDefault;
	float CameraYaw = MixtormatPreviewCamera::YawDefault;
	float CameraPitch = MixtormatPreviewCamera::PitchDefault;
	float CameraFov = MixtormatPreviewCamera::OverlayFovDefault;
	double LastLightRotateTime = -1000.0;
	float LightingYaw = -45.0f;
	float LightingPitch = -35.0f;

	// What the current mode asked for, before the user's multipliers. Held so a change to either
	// slider can be re-applied without re-running the whole preset, and so switching preset
	// keeps the multipliers rather than resetting them.
	float BaseLightBrightness = 2.0f;
	float BaseSkyBrightness = 0.45f;
	float LightIntensityScale = 0.8f;
	float SkylightIntensityScale = 0.1f;
	void ApplyLightIntensities();
	float EnvironmentYaw = 0.0f;
	FVector PreviewTarget = FVector(0.0f, 0.0f, 50.0f);
};
