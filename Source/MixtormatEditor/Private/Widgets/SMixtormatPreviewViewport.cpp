#include "Widgets/SMixtormatPreviewViewport.h"

#include "AssetViewerSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "Engine/TextureCube.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMaterial.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
#include "Services/MixtormatPaths.h"
#include "Style/MixtormatPalette.h"
#include "Materials/Material.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace MixtormatPreview
{
	const FName UseHeightParameter(TEXT("DA_UseHeight"));
	const FName HeightAmountParameter(TEXT("DA_HeightAmount"));
	const FName DebugTextureParameter(TEXT("DA_DebugTexture"));
	const FName FuzzInfluenceParameter(TEXT("DA_FuzzInfluence"));

	// Fuzz has no channel of its own to carry through the compositor -- DA_FuzzInfluence is a
	// Substrate Slab amount the master material reads once per material instance, not a per-pixel
	// value, so it is composited here on the CPU rather than baked as a texture. Later, more
	// influential layers blend over earlier ones by their own Opacity, the same way a layer's
	// Channel Influence rows blend a real per-pixel channel in the GPU composite.
	float ComputeFuzzInfluence(const TArray<FMixtormatLayer>& Layers)
	{
		float Result = 0.0f;
		for (const FMixtormatLayer& Layer : Layers)
		{
			if (!Layer.bEnabled || Layer.FuzzInfluence <= 0.0f)
			{
				continue;
			}
			const float Weight = FMath::Clamp(Layer.Opacity, 0.0f, 1.0f);
			const float Target = FMath::Clamp(Layer.FuzzInfluence, 0.0f, 1.0f);
			Result = FMath::Lerp(Result, Target, Weight);
		}
		return FMath::Clamp(Result, 0.0f, 1.0f);
	}

	UMaterial* CreateDebugMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Material)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		UMaterialExpressionTextureSampleParameter2D* DebugTexture =
			NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		DebugTexture->SetParameterName(DebugTextureParameter);
		DebugTexture->ExpressionGUID = FGuid::NewGuid();
		DebugTexture->SamplerType = SAMPLERTYPE_Color;
		DebugTexture->Texture = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		Material->GetExpressionCollection().AddExpression(DebugTexture);
		Material->GetEditorOnlyData()->EmissiveColor.Expression = DebugTexture;
		Material->PostEditChange();
		return Material;
	}

}

class FMixtormatPreviewViewportClient final : public FEditorViewportClient
{
public:
	FMixtormatPreviewViewportClient(
		FAdvancedPreviewScene& InPreviewScene,
		const TSharedRef<SEditorViewport>& InViewport,
		SMixtormatPreviewViewport& InOwner)
		: FEditorViewportClient(nullptr, &InPreviewScene, InViewport)
		, Owner(InOwner)
	{
	}

	virtual FLinearColor GetBackgroundColor() const override
	{
		return MixtormatPalette::PreviewBackground();
	}

	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
	{
		// F frames the mesh, the way it does everywhere else in the editor. Handled here rather
		// than through the viewport's command list because this client deliberately does not run
		// the standard editor camera -- there is no selection to focus, and the orbit is the
		// widget's own state -- so the engine's own F binding has nothing to act on.
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::F)
		{
			Owner.FocusCamera();
			return true;
		}
		if (EventArgs.Event == IE_Pressed
			&& (EventArgs.Key == EKeys::SpaceBar || EventArgs.Key == EKeys::H))
		{
			Owner.ToggleOverlayUi();
			return true;
		}
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::MouseScrollUp)
		{
			Owner.ZoomCamera(1.0f);
			return true;
		}
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::MouseScrollDown)
		{
			Owner.ZoomCamera(-1.0f);
			return true;
		}
		return FEditorViewportClient::InputKey(EventArgs);
	}

	virtual bool InputAxis(const FInputKeyEventArgs& Args) override
	{
		if (Args.Key == EKeys::MouseWheelAxis)
		{
			Owner.ZoomCamera(Args.AmountDepressed);
			return true;
		}
		if (Args.Viewport && Args.Viewport->KeyState(EKeys::LeftMouseButton))
		{
			if (Args.Key == EKeys::MouseX)
			{
				Owner.OrbitCamera(Args.AmountDepressed, 0.0f);
				return true;
			}
			if (Args.Key == EKeys::MouseY)
			{
				Owner.OrbitCamera(0.0f, Args.AmountDepressed);
				return true;
			}
		}
		if (Args.Viewport && Args.Viewport->KeyState(EKeys::RightMouseButton))
		{
			if (Args.Key == EKeys::MouseX)
			{
				Owner.RotateLighting(Args.AmountDepressed, 0.0f);
				return true;
			}
			if (Args.Key == EKeys::MouseY)
			{
				Owner.RotateLighting(0.0f, Args.AmountDepressed);
				return true;
			}
		}
		return FEditorViewportClient::InputAxis(Args);
	}

private:
	SMixtormatPreviewViewport& Owner;
};

SMixtormatPreviewViewport::SMixtormatPreviewViewport()
	: PreviewScene(FPreviewScene::ConstructionValues())
{
}

SMixtormatPreviewViewport::~SMixtormatPreviewViewport()
{
	if (PreviewMeshComponent)
	{
		PreviewScene.RemoveComponent(PreviewMeshComponent);
	}
	if (StudioFogComponent)
	{
		PreviewScene.RemoveComponent(StudioFogComponent);
	}
}

void SMixtormatPreviewViewport::Construct(const FArguments& InArgs)
{
	OnToggleOverlayUi = InArgs._OnToggleOverlayUi;


	PreviewMeshComponent = NewObject<UStaticMeshComponent>();
	PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);

	StudioFogComponent = NewObject<UExponentialHeightFogComponent>();
	StudioFogComponent->SetFogHeightFalloff(0.01f);
	StudioFogComponent->SetFogMaxOpacity(1.0f);
	StudioFogComponent->SetFogInscatteringColor(MixtormatPalette::PreviewFog());
	PreviewScene.AddComponent(StudioFogComponent, FTransform::Identity);

	PreviewScene.SetFloorVisibility(true);
	PreviewScene.SetEnvironmentVisibility(false);
	SetStudioLighting(EMixtormatStudioLighting::Neutral);

	SEditorViewport::Construct(SEditorViewport::FArguments());
	SetPreviewMesh(EMixtormatPreviewMesh::Sphere);
	SetPreviewMaterial(LoadObject<UMaterialInterface>(
		nullptr,
		*FMixtormatPaths::PreviewMaterialObjectPath()));
}

void SMixtormatPreviewViewport::SetPreviewMaterial(UMaterialInterface* Material)
{
	bUsingLayerPreview = false;
	bUsingDebugPreview = false;
	if (!PreviewMeshComponent)
	{
		return;
	}

	PreviewMaterialInstance.Reset();
	if (Material)
	{
		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(
			Material, PreviewMeshComponent);
		PreviewMaterialInstance = DynamicMaterial;
		DynamicMaterial->SetScalarParameterValue(
			MixtormatPreview::UseHeightParameter,
			bDisplacementEnabled ? 1.0f : 0.0f);
		DynamicMaterial->SetScalarParameterValue(
			MixtormatPreview::HeightAmountParameter,
			DisplacementAmount);
		PreviewMeshComponent->SetMaterial(0, DynamicMaterial);
	}
	else
	{
		PreviewMeshComponent->SetMaterial(0, nullptr);
		bUsingLayerPreview = false;
		bUsingDebugPreview = false;
	}

	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::SetPreviewLayers(
	const TArray<FMixtormatLayer>& Layers,
	const int32 Resolution,
	FMixtormatDebugPreviewSettings DebugSettings)
{
	bDebugPreviewMode = DebugSettings.Mode;
	bDebugLayerIndex = DebugSettings.LayerIndex;
	bDebugChildIndex = DebugSettings.ChildIndex;
	ComposeLayersWithDebug(Layers, Resolution, DebugSettings);
}

bool SMixtormatPreviewViewport::ComposeLayersAtResolution(
	const TArray<FMixtormatLayer>& Layers,
	const int32 Resolution)
{
	FMixtormatDebugPreviewSettings DebugSettings;
	DebugSettings.Mode = bDebugPreviewMode;
	DebugSettings.LayerIndex = bDebugLayerIndex;
	DebugSettings.ChildIndex = bDebugChildIndex;
	return ComposeLayersWithDebug(Layers, Resolution, DebugSettings);
}

bool SMixtormatPreviewViewport::ComposeLayersWithDebug(
	const TArray<FMixtormatLayer>& Layers,
	const int32 Resolution,
	FMixtormatDebugPreviewSettings DebugSettings)
{
	if (!LayerCompositor)
	{
		LayerCompositor = MakeUnique<FMixtormatGpuCompositor>();
	}
	if (!LayerCompositor->Initialize(FIntPoint(Resolution, Resolution)))
	{
		return false;
	}

	const bool bDebugPreview = DebugSettings.Mode != EMixtormatDebugPreviewMode::None;
	if (!bUsingLayerPreview
		|| !PreviewMaterialInstance.IsValid()
		|| bUsingDebugPreview != bDebugPreview)
	{
		UMaterialInterface* PreviewMaterial = nullptr;
		if (bDebugPreview)
		{
			if (!DebugPreviewMaterial.IsValid())
			{
				DebugPreviewMaterial.Reset(MixtormatPreview::CreateDebugMaterial());
			}
			PreviewMaterial = DebugPreviewMaterial.Get();
		}
		else
		{
			PreviewMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				*FMixtormatPaths::MasterMaterialObjectPath());
		}
		if (!PreviewMaterial)
		{
			return false;
		}
		SetPreviewMaterial(PreviewMaterial);
		bUsingLayerPreview = true;
		bUsingDebugPreview = bDebugPreview;
		UpdateDebugLightVisibility();
	}

	if (!PreviewMaterialInstance.IsValid())
	{
		return false;
	}

	if (!LayerCompositor->RequestCompose(Layers, FSimpleDelegate(), DebugSettings))
	{
		return false;
	}
	FlushRenderingCommands();
	if (bDebugPreview)
	{
		PreviewMaterialInstance->SetTextureParameterValue(
			MixtormatPreview::DebugTextureParameter,
			LayerCompositor->GetDebugOutput());
	}
	else
	{
		LayerCompositor->BindOutputs(*PreviewMaterialInstance.Get());
		PreviewMaterialInstance->SetScalarParameterValue(
			MixtormatPreview::FuzzInfluenceParameter,
			MixtormatPreview::ComputeFuzzInfluence(Layers));
	}
	PreviewMaterialInstance->SetScalarParameterValue(
		MixtormatPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
	PreviewMaterialInstance->SetScalarParameterValue(
		MixtormatPreview::HeightAmountParameter,
		DisplacementAmount);
	InvalidateDisplacementShadows();
	return true;
}

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetCompositedBaseColor() const
{
	return LayerCompositor ? LayerCompositor->GetBaseColorOutput() : nullptr;
}

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetCompositedNormal() const
{
	return LayerCompositor ? LayerCompositor->GetNormalOutput() : nullptr;
}

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetCompositedRAM() const
{
	return LayerCompositor ? LayerCompositor->GetRAMOutput() : nullptr;
}

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetCompositedHeight() const
{
	return LayerCompositor ? LayerCompositor->GetHeightOutput() : nullptr;
}

void SMixtormatPreviewViewport::SetPreviewScalarParameter(
	const FName ParameterName,
	const float Value)
{
	if (!PreviewMaterialInstance.IsValid())
	{
		return;
	}

	PreviewMaterialInstance->SetScalarParameterValue(ParameterName, Value);
	InvalidateDisplacementShadows();
}

void SMixtormatPreviewViewport::SetPreviewDisplacementEnabled(const bool bEnabled)
{
	bDisplacementEnabled = bEnabled;
	SetPreviewScalarParameter(
		MixtormatPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
}

void SMixtormatPreviewViewport::SetPreviewDisplacementAmount(const float Amount)
{
	DisplacementAmount = FMath::Clamp(Amount, 0.0f, 4.0f);
	SetPreviewScalarParameter(
		MixtormatPreview::HeightAmountParameter,
		DisplacementAmount);
}

void SMixtormatPreviewViewport::SetPreviewMesh(const EMixtormatPreviewMesh MeshType)
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	FString PluginMeshPath = FMixtormatPaths::SphereMeshObjectPath();
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere");
	FRotator MeshRotation = FRotator::ZeroRotator;

	switch (MeshType)
	{
	case EMixtormatPreviewMesh::Plane:
		PluginMeshPath = FMixtormatPaths::PlaneMeshObjectPath();
		FallbackMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
		break;
	case EMixtormatPreviewMesh::Cube:
		PluginMeshPath = FMixtormatPaths::CubeMeshObjectPath();
		FallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
		break;
	case EMixtormatPreviewMesh::Sphere:
	default:
		break;
	}

	UStaticMesh* PreviewMesh = LoadObject<UStaticMesh>(nullptr, *PluginMeshPath);
	if (!PreviewMesh)
	{
		PreviewMesh = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath);
		if (MeshType == EMixtormatPreviewMesh::Plane)
		{
			MeshRotation = FRotator(90.0f, 0.0f, 0.0f);
		}
	}
	PreviewMeshComponent->SetStaticMesh(PreviewMesh);
	PreviewMeshComponent->SetRelativeRotation(MeshRotation);
	PreviewMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	PreviewMeshComponent->UpdateBounds();

	const float FloorClearance = 0.5f;
	const float HeightAboveFloor = -PreviewMeshComponent->Bounds.GetBox().Min.Z + FloorClearance;
	PreviewMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, HeightAboveFloor));
	PreviewMeshComponent->UpdateBounds();
	PreviewTarget = PreviewMeshComponent->Bounds.Origin;

	UpdateStudioFog();
	UpdateCamera();
}

void SMixtormatPreviewViewport::UpdateStudioFog()
{
	if (!StudioFogComponent || !PreviewMeshComponent || !PreviewMeshComponent->GetStaticMesh())
	{
		return;
	}

	const float MeshRadius = FMath::Max(PreviewMeshComponent->Bounds.SphereRadius, 0.5f);
	const float MeshDiameter = MeshRadius * 2.0f;
	const float StrongFadeDistance = MeshDiameter * 2.0f;
	const float FogStartDistance = CameraDistance * 2.0f;

	// Fog density is measured per 1,000 Unreal units. Reach 95% opacity over the fade range.
	const float FogDensity = FMath::Clamp(
		-FMath::Loge(0.05f) * 1000.0f / StrongFadeDistance,
		0.001f,
		20.0f);

	StudioFogComponent->SetStartDistance(FogStartDistance);
	StudioFogComponent->SetFogDensity(FogDensity);
	StudioFogComponent->SetFogHeightFalloff(0.01f);
	StudioFogComponent->SetFogMaxOpacity(1.0f);
}

void SMixtormatPreviewViewport::SetStudioLighting(const EMixtormatStudioLighting LightingPreset)
{
	const FMixtormatStudioLightSettings LightSettings =
		MixtormatPreviewSceneSettings::GetStudioLighting(LightingPreset);
	const FString EnvironmentPath =
		MixtormatPreviewSceneSettings::GetStudioEnvironmentObjectPath(LightingPreset);
	StudioEnvironmentCubemap.Reset(LoadObject<UTextureCube>(nullptr, *EnvironmentPath));

	StudioPreviewProfile = MakeUnique<FPreviewSceneProfile>();
	MixtormatPreviewSceneSettings::ConfigureLookdevProfile(*StudioPreviewProfile);
	StudioPreviewProfile->EnvironmentCubeMap = StudioEnvironmentCubemap.Get();
	StudioPreviewProfile->EnvironmentCubeMapPath = EnvironmentPath;
	StudioPreviewProfile->LightingRigRotation = 0.0f;
	StudioPreviewProfile->SkyLightIntensity = LightSettings.SkyBrightness;
	StudioPreviewProfile->DirectionalLightIntensity = LightSettings.LightBrightness;
	PreviewScene.UpdateScene(*StudioPreviewProfile, true, true, false, true);
	PreviewScene.SetEnvironmentVisibility(false, true);
	PreviewScene.SetFloorVisibility(true, true);

	bUsingStudioEnvironment = StudioEnvironmentCubemap.IsValid();
	EnvironmentYaw = 0.0f;
	LightingYaw = LightSettings.LightRotation.Yaw;
	LightingPitch = LightSettings.LightRotation.Pitch;
	BaseLightBrightness = LightSettings.LightBrightness;
	BaseSkyBrightness = LightSettings.SkyBrightness;
	PreviewScene.SetLightDirection(FRotator(LightingPitch, LightingYaw, 0.0f));
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->SetLightSourceAngle(LightSettings.LightSourceAngle);
		PreviewScene.DirectionalLight->SetLightSourceSoftAngle(LightSettings.LightSourceAngle * 0.5f);
		PreviewScene.DirectionalLight->SetShadowBias(0.75f);
		PreviewScene.DirectionalLight->SetShadowSlopeBias(0.8f);
		PreviewScene.DirectionalLight->ShadowSharpen = 0.0f;
		PreviewScene.DirectionalLight->ContactShadowLength = 0.0f;
		PreviewScene.DirectionalLight->MarkRenderStateDirty();
	}
	UpdateStudioEnvironmentLighting();
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::UpdateStudioEnvironmentLighting()
{
	ApplyLightIntensities();
	UpdateDebugLightVisibility();
}

void SMixtormatPreviewViewport::ApplyLightIntensities()
{
	PreviewScene.SetLightBrightness(BaseLightBrightness * LightIntensityScale);
	PreviewScene.SetSkyBrightness(BaseSkyBrightness * SkylightIntensityScale);
}

void SMixtormatPreviewViewport::SetPreviewLightIntensity(const float Scale)
{
	LightIntensityScale = FMath::Clamp(Scale, 0.0f, 2.0f);
	ApplyLightIntensities();
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::SetPreviewSkylightIntensity(const float Scale)
{
	SkylightIntensityScale = FMath::Clamp(Scale, 0.0f, 2.0f);
	ApplyLightIntensities();
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::InvalidateDisplacementShadows()
{
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->MarkRenderDynamicDataDirty();
		PreviewMeshComponent->MarkRenderStateDirty();
	}
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->MarkRenderStateDirty();
	}
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::UpdateDebugLightVisibility()
{
	const bool bLightsVisible = !bUsingDebugPreview;
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->SetVisibility(bLightsVisible);
	}
	if (PreviewScene.SkyLight)
	{
		PreviewScene.SkyLight->SetVisibility(bLightsVisible);
	}
}

void SMixtormatPreviewViewport::SetPreviewQuality(const EMixtormatPreviewQuality Quality)
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	MixtormatPreviewSceneSettings::ConfigureQuality(
		PreviewViewportClient->EngineShowFlags,
		Quality);
	PreviewViewportClient->Invalidate();
}

void SMixtormatPreviewViewport::SetPreviewAntiAliasing(const EMixtormatPreviewAntiAliasing AntiAliasing)
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	// TemporalAA is the whole switch. The project default is TSR, and SetupAntiAliasingMethod
	// turns TSR into FXAA when this flag is clear -- so there is nothing to set for FXAA beyond
	// taking the history away, and no console variable is involved.
	PreviewViewportClient->EngineShowFlags.SetTemporalAA(
		AntiAliasing == EMixtormatPreviewAntiAliasing::Temporal);
	PreviewViewportClient->Invalidate();
}

void SMixtormatPreviewViewport::SetPreviewScreenPercentage(const int32 Percentage)
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	// Editor viewports ignore r.ScreenPercentage and the post-process volume by design, so the
	// preview fraction is the only way in. FEditorViewportClient clamps it to 25..200 itself.
	//
	// The previewing flag has to track the value rather than a mode: leave it false and anything
	// off 100 is silently ignored, which is how a scale control ends up appearing to do nothing.
	PreviewViewportClient->SetPreviewingScreenPercentage(
		Percentage != MixtormatPreviewScreenPercentage::Default);
	PreviewViewportClient->SetPreviewScreenPercentage(Percentage);
	PreviewViewportClient->Invalidate();
}

void SMixtormatPreviewViewport::OrbitCamera(const float YawDelta, const float PitchDelta)
{
	CameraYaw = FMath::Fmod(CameraYaw + YawDelta * 0.35f, 360.0f);
	CameraPitch = FMath::Clamp(CameraPitch + PitchDelta * 0.25f, -75.0f, 20.0f);
	UpdateCamera();
}

void SMixtormatPreviewViewport::RotateLighting(
	const float YawDelta,
	const float PitchDelta)
{
	LightingYaw = FMath::Fmod(LightingYaw + YawDelta * 0.35f + 360.0f, 360.0f);
	LightingPitch = FMath::Clamp(LightingPitch - PitchDelta * 0.25f, -89.0f, -1.0f);
	PreviewScene.SetLightDirection(FRotator(LightingPitch, LightingYaw, 0.0f));

	if (bUsingStudioEnvironment && StudioPreviewProfile && !FMath::IsNearlyZero(YawDelta))
	{
		EnvironmentYaw = FMath::Fmod(EnvironmentYaw - YawDelta * 0.22f + 360.0f, 360.0f);
		StudioPreviewProfile->LightingRigRotation = EnvironmentYaw;
		PreviewScene.UpdateScene(*StudioPreviewProfile, true, true, false, false);
		PreviewScene.SetEnvironmentVisibility(false, true);
		UpdateStudioEnvironmentLighting();
	}
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->MarkRenderStateDirty();
	}
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMixtormatPreviewViewport::ZoomCamera(const float ZoomDelta)
{
	CameraDistance = FMath::Clamp(
		CameraDistance - ZoomDelta * 6.0f,
		MixtormatPreviewCamera::DistanceMinimum,
		MixtormatPreviewCamera::DistanceMaximum);
	UpdateStudioFog();
	UpdateCamera();
}

void SMixtormatPreviewViewport::ToggleOverlayUi()
{
	OnToggleOverlayUi.ExecuteIfBound();
}

void SMixtormatPreviewViewport::SetCameraFov(const float FovDegrees)
{
	CameraFov = FMath::Clamp(
		FovDegrees,
		MixtormatPreviewCamera::FovMinimum,
		MixtormatPreviewCamera::FovMaximum);
	UpdateCamera();
}

void SMixtormatPreviewViewport::ResetCameraAndLighting()
{
	CameraDistance = MixtormatPreviewCamera::DistanceDefault;
	CameraYaw = MixtormatPreviewCamera::YawDefault;
	CameraPitch = MixtormatPreviewCamera::PitchDefault;
	CameraFov = MixtormatPreviewCamera::FovDefault;
	EnvironmentYaw = 0.0f;
	SetStudioLighting(EMixtormatStudioLighting::Neutral);
	FocusCamera();
}

void SMixtormatPreviewViewport::FocusCamera()
{
	if (!PreviewMeshComponent || !PreviewMeshComponent->GetStaticMesh())
	{
		return;
	}

	// Frame the mesh without touching the orbit. F answers "I have lost the object" or "I want
	// to see all of it", and re-aiming the camera as well would throw away the angle the user
	// chose to look at the surface from -- which is the whole point of a look-dev view. Reset
	// is the control that puts the orbit back.
	const FBoxSphereBounds Bounds = PreviewMeshComponent->Bounds;
	PreviewTarget = Bounds.Origin;

	const float FitDistance = MixtormatPreviewSceneSettings::CalculateFocusDistance(
		static_cast<float>(Bounds.SphereRadius),
		CameraFov,
		GetCachedGeometry().GetLocalSize());

	CameraDistance = FMath::Clamp(
		FitDistance,
		MixtormatPreviewCamera::DistanceMinimum,
		MixtormatPreviewCamera::DistanceMaximum);

	// The fog trails the camera, and it is keyed off distance.
	UpdateStudioFog();
	UpdateCamera();
}

void SMixtormatPreviewViewport::UpdateCamera()
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	PreviewViewportClient->ViewFOV = CameraFov;
	PreviewViewportClient->FOVAngle = CameraFov;
	const FVector ViewDirection = FRotator(CameraPitch, CameraYaw, 0.0f).Vector();
	const FVector CameraLocation = PreviewTarget - ViewDirection * CameraDistance;
	PreviewViewportClient->SetViewLocation(CameraLocation);
	PreviewViewportClient->SetViewRotation(ViewDirection.Rotation());
	PreviewViewportClient->Invalidate();
}

TSharedRef<FEditorViewportClient> SMixtormatPreviewViewport::MakeEditorViewportClient()
{
	PreviewViewportClient = MakeShared<FMixtormatPreviewViewportClient>(PreviewScene, SharedThis(this), *this);
	PreviewViewportClient->SetViewMode(VMI_Lit);
	PreviewViewportClient->SetRealtime(true);
	PreviewViewportClient->EngineShowFlags.SetGrid(false);
	PreviewViewportClient->EngineShowFlags.SetSelectionOutline(false);

	// Motion blur off. It defaults on in an editor viewport, and the engine only forces it off
	// for the bones debug view, so a preview inherits it. Every camera move here is an orbit or
	// a zoom the user made in order to look at the surface, and blurring the frame along that
	// movement smears exactly the high-frequency detail -- crack width, chip edges, grain --
	// that the move was made to inspect.
	PreviewViewportClient->EngineShowFlags.SetMotionBlur(false);
	SetPreviewQuality(EMixtormatPreviewQuality::Medium);
	PreviewScene.SetLightDirection(FRotator(-35.0f, LightingYaw, 0.0f));
	UpdateCamera();
	return PreviewViewportClient.ToSharedRef();
}

