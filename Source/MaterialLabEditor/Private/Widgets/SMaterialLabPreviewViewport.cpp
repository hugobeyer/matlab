#include "Widgets/SMaterialLabPreviewViewport.h"

#include "AssetViewerSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "MaterialLabGpuCompositor.h"
#include "MaterialLabMaterial.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace MaterialLabPreview
{
	const FName UseHeightParameter(TEXT("ML_UseHeight"));
	const FName HeightAmountParameter(TEXT("ML_HeightAmount"));
	const FName DebugTextureParameter(TEXT("ML_DebugTexture"));

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

	void ConfigureLookdevProfile(FPreviewSceneProfile& Profile)
	{
		Profile.bPostProcessingEnabled = true;
		Profile.bEnableToneMapping = true;
		Profile.PostProcessingSettings.bOverride_AutoExposureMinBrightness = true;
		Profile.PostProcessingSettings.AutoExposureMinBrightness = 0.0f;
		Profile.PostProcessingSettings.bOverride_AutoExposureMaxBrightness = true;
		Profile.PostProcessingSettings.AutoExposureMaxBrightness = 0.0f;
		Profile.PostProcessingSettings.bOverride_AutoExposureBias = true;
		Profile.PostProcessingSettings.AutoExposureBias = -0.5f;
		Profile.PostProcessingSettings.bOverride_BloomIntensity = true;
		Profile.PostProcessingSettings.BloomIntensity = 0.0f;
	}
}

class FMaterialLabPreviewViewportClient final : public FEditorViewportClient
{
public:
	FMaterialLabPreviewViewportClient(
		FAdvancedPreviewScene& InPreviewScene,
		const TSharedRef<SEditorViewport>& InViewport,
		SMaterialLabPreviewViewport& InOwner)
		: FEditorViewportClient(nullptr, &InPreviewScene, InViewport)
		, Owner(InOwner)
	{
	}

	virtual FLinearColor GetBackgroundColor() const override
	{
		return FLinearColor(0.018f, 0.022f, 0.028f, 1.0f);
	}

	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
	{
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
				Owner.RotateLighting(Args.AmountDepressed);
				return true;
			}
			if (Args.Key == EKeys::MouseY)
			{
				return true;
			}
		}
		return FEditorViewportClient::InputAxis(Args);
	}

private:
	SMaterialLabPreviewViewport& Owner;
};

SMaterialLabPreviewViewport::SMaterialLabPreviewViewport()
	: PreviewScene(FPreviewScene::ConstructionValues())
{
}

SMaterialLabPreviewViewport::~SMaterialLabPreviewViewport()
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

void SMaterialLabPreviewViewport::Construct(const FArguments& InArgs)
{
	if (const FPreviewSceneProfile* CurrentProfile = PreviewScene.GetCurrentProfile())
	{
		DefaultPreviewProfile = MakeUnique<FPreviewSceneProfile>(*CurrentProfile);
		MaterialLabPreview::ConfigureLookdevProfile(*DefaultPreviewProfile);
	}

	PreviewMeshComponent = NewObject<UStaticMeshComponent>();
	PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);

	StudioFogComponent = NewObject<UExponentialHeightFogComponent>();
	StudioFogComponent->SetFogHeightFalloff(0.01f);
	StudioFogComponent->SetFogMaxOpacity(1.0f);
	StudioFogComponent->SetFogInscatteringColor(FLinearColor(0.006f, 0.008f, 0.012f));
	PreviewScene.AddComponent(StudioFogComponent, FTransform::Identity);

	PreviewScene.SetFloorVisibility(true);
	PreviewScene.SetEnvironmentVisibility(false);
	SetStudioLighting(EMaterialLabStudioLighting::Neutral);

	SEditorViewport::Construct(SEditorViewport::FArguments());
	SetPreviewMesh(EMaterialLabPreviewMesh::Sphere);
	SetPreviewMaterial(nullptr);
}

void SMaterialLabPreviewViewport::SetPreviewMaterial(UMaterialInterface* Material)
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
			MaterialLabPreview::UseHeightParameter,
			bDisplacementEnabled ? 1.0f : 0.0f);
		DynamicMaterial->SetScalarParameterValue(
			MaterialLabPreview::HeightAmountParameter,
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

void SMaterialLabPreviewViewport::SetPreviewLayers(
	const TArray<FMaterialLabLayer>& Layers,
	const int32 Resolution,
	FMaterialLabDebugPreviewSettings DebugSettings)
{
	bDebugPreviewMode = DebugSettings.Mode;
	bDebugLayerIndex = DebugSettings.LayerIndex;
	bDebugChildIndex = DebugSettings.ChildIndex;
	ComposeLayersWithDebug(Layers, Resolution, DebugSettings);
}

bool SMaterialLabPreviewViewport::ComposeLayersAtResolution(
	const TArray<FMaterialLabLayer>& Layers,
	const int32 Resolution)
{
	FMaterialLabDebugPreviewSettings DebugSettings;
	DebugSettings.Mode = bDebugPreviewMode;
	DebugSettings.LayerIndex = bDebugLayerIndex;
	DebugSettings.ChildIndex = bDebugChildIndex;
	return ComposeLayersWithDebug(Layers, Resolution, DebugSettings);
}

bool SMaterialLabPreviewViewport::ComposeLayersWithDebug(
	const TArray<FMaterialLabLayer>& Layers,
	const int32 Resolution,
	FMaterialLabDebugPreviewSettings DebugSettings)
{
	if (!LayerCompositor)
	{
		LayerCompositor = MakeUnique<FMaterialLabGpuCompositor>();
	}
	if (!LayerCompositor->Initialize(FIntPoint(Resolution, Resolution)))
	{
		return false;
	}

	const bool bDebugPreview = DebugSettings.Mode != EMaterialLabDebugPreviewMode::None;
	if (!bUsingLayerPreview
		|| !PreviewMaterialInstance.IsValid()
		|| bUsingDebugPreview != bDebugPreview)
	{
		UMaterialInterface* PreviewMaterial = nullptr;
		if (bDebugPreview)
		{
			if (!DebugPreviewMaterial.IsValid())
			{
				DebugPreviewMaterial.Reset(MaterialLabPreview::CreateDebugMaterial());
			}
			PreviewMaterial = DebugPreviewMaterial.Get();
		}
		else
		{
			PreviewMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/MaterialLab/Materials/M_MaterialLab_Substrate.M_MaterialLab_Substrate"));
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
			MaterialLabPreview::DebugTextureParameter,
			LayerCompositor->GetDebugOutput());
	}
	else
	{
		LayerCompositor->BindOutputs(*PreviewMaterialInstance.Get());
	}
	PreviewMaterialInstance->SetScalarParameterValue(
		MaterialLabPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
	PreviewMaterialInstance->SetScalarParameterValue(
		MaterialLabPreview::HeightAmountParameter,
		DisplacementAmount);
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
	return true;
}

UTextureRenderTarget2D* SMaterialLabPreviewViewport::GetCompositedBaseColor() const
{
	return LayerCompositor ? LayerCompositor->GetBaseColorOutput() : nullptr;
}

UTextureRenderTarget2D* SMaterialLabPreviewViewport::GetCompositedNormal() const
{
	return LayerCompositor ? LayerCompositor->GetNormalOutput() : nullptr;
}

UTextureRenderTarget2D* SMaterialLabPreviewViewport::GetCompositedRAM() const
{
	return LayerCompositor ? LayerCompositor->GetRAMOutput() : nullptr;
}

UTextureRenderTarget2D* SMaterialLabPreviewViewport::GetCompositedHeight() const
{
	return LayerCompositor ? LayerCompositor->GetHeightOutput() : nullptr;
}

void SMaterialLabPreviewViewport::SetPreviewScalarParameter(
	const FName ParameterName,
	const float Value)
{
	if (!PreviewMaterialInstance.IsValid())
	{
		return;
	}

	PreviewMaterialInstance->SetScalarParameterValue(ParameterName, Value);
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->MarkRenderStateDirty();
	}
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMaterialLabPreviewViewport::SetPreviewDisplacementEnabled(const bool bEnabled)
{
	bDisplacementEnabled = bEnabled;
	SetPreviewScalarParameter(
		MaterialLabPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
}

void SMaterialLabPreviewViewport::SetPreviewDisplacementAmount(const float Amount)
{
	DisplacementAmount = FMath::Clamp(Amount, 0.0f, 4.0f);
	SetPreviewScalarParameter(
		MaterialLabPreview::HeightAmountParameter,
		DisplacementAmount);
}

void SMaterialLabPreviewViewport::SetPreviewMesh(const EMaterialLabPreviewMesh MeshType)
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	const TCHAR* PluginMeshPath = TEXT("/MaterialLab/Meshes/SM_MaterialLab_Sphere.SM_MaterialLab_Sphere");
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere");
	FRotator MeshRotation = FRotator::ZeroRotator;

	switch (MeshType)
	{
	case EMaterialLabPreviewMesh::Plane:
		PluginMeshPath = TEXT("/MaterialLab/Meshes/SM_MaterialLab_Plane.SM_MaterialLab_Plane");
		FallbackMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
		break;
	case EMaterialLabPreviewMesh::Cube:
		PluginMeshPath = TEXT("/MaterialLab/Meshes/SM_MaterialLab_Cube.SM_MaterialLab_Cube");
		FallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
		break;
	case EMaterialLabPreviewMesh::Sphere:
	default:
		break;
	}

	UStaticMesh* PreviewMesh = LoadObject<UStaticMesh>(nullptr, PluginMeshPath);
	if (!PreviewMesh)
	{
		PreviewMesh = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath);
		if (MeshType == EMaterialLabPreviewMesh::Plane)
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

void SMaterialLabPreviewViewport::UpdateStudioFog()
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

void SMaterialLabPreviewViewport::SetStudioLighting(const EMaterialLabStudioLighting LightingPreset)
{
	bUsingHdri = false;
	HdriPreviewProfile.Reset();
	if (DefaultPreviewProfile)
	{
		PreviewScene.UpdateScene(*DefaultPreviewProfile, true, true, false, false);
	}
	PreviewScene.SetEnvironmentVisibility(false, true);
	PreviewScene.SetFloorVisibility(true, true);

	float LightBrightness = 2.0f;
	float SkyBrightness = 0.45f;
	float LightSourceAngle = 10.0f;
	FRotator LightRotation(-35.0f, -45.0f, 0.0f);

	switch (LightingPreset)
	{
	case EMaterialLabStudioLighting::Soft:
		LightBrightness = 1.25f;
		SkyBrightness = 0.65f;
		LightSourceAngle = 16.0f;
		LightRotation = FRotator(-28.0f, 30.0f, 0.0f);
		break;
	case EMaterialLabStudioLighting::Dramatic:
		LightBrightness = 3.0f;
		SkyBrightness = 0.15f;
		LightSourceAngle = 6.0f;
		LightRotation = FRotator(-48.0f, -65.0f, 0.0f);
		break;
	case EMaterialLabStudioLighting::Rim:
		LightBrightness = 2.5f;
		SkyBrightness = 0.2f;
		LightSourceAngle = 8.0f;
		LightRotation = FRotator(-22.0f, 145.0f, 0.0f);
		break;
	case EMaterialLabStudioLighting::Neutral:
	default:
		break;
	}

	LightingYaw = LightRotation.Yaw;
	PreviewScene.SetLightBrightness(LightBrightness);
	PreviewScene.SetSkyBrightness(SkyBrightness);
	PreviewScene.SetLightDirection(LightRotation);
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->SetLightSourceAngle(LightSourceAngle);
		PreviewScene.DirectionalLight->SetLightSourceSoftAngle(LightSourceAngle * 0.5f);
		PreviewScene.DirectionalLight->SetShadowBias(0.75f);
		PreviewScene.DirectionalLight->SetShadowSlopeBias(0.8f);
		PreviewScene.DirectionalLight->ShadowSharpen = 0.0f;
		PreviewScene.DirectionalLight->ContactShadowLength = 0.0f;
		PreviewScene.DirectionalLight->MarkRenderStateDirty();
	}
	UpdateDebugLightVisibility();
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMaterialLabPreviewViewport::SetHdriLighting(UTextureCube* Cubemap)
{
	if (!Cubemap)
	{
		return;
	}

	HdriPreviewProfile = DefaultPreviewProfile
		? MakeUnique<FPreviewSceneProfile>(*DefaultPreviewProfile)
		: MakeUnique<FPreviewSceneProfile>();
	HdriPreviewProfile->EnvironmentCubeMap = Cubemap;
	HdriPreviewProfile->EnvironmentCubeMapPath = Cubemap->GetPathName();
	HdriPreviewProfile->LightingRigRotation = HdriYaw;
	MaterialLabPreview::ConfigureLookdevProfile(*HdriPreviewProfile);
	HdriPreviewProfile->SkyLightIntensity = 0.55f;
	HdriPreviewProfile->DirectionalLightIntensity = 0.35f;
	bUsingHdri = true;

	PreviewScene.UpdateScene(*HdriPreviewProfile, true, true, false, true);
	PreviewScene.SetEnvironmentVisibility(false, true);
	UpdateHdriFillLight();
	PreviewScene.SetFloorVisibility(true, true);
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMaterialLabPreviewViewport::UpdateHdriFillLight()
{
	PreviewScene.SetLightBrightness(0.35f);
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->SetLightSourceAngle(24.0f);
		PreviewScene.DirectionalLight->SetLightSourceSoftAngle(12.0f);
		PreviewScene.DirectionalLight->SetShadowBias(0.75f);
		PreviewScene.DirectionalLight->SetShadowSlopeBias(0.8f);
		PreviewScene.DirectionalLight->ShadowSharpen = 0.0f;
		PreviewScene.DirectionalLight->ContactShadowLength = 0.0f;
		PreviewScene.DirectionalLight->MarkRenderStateDirty();
	}
	UpdateDebugLightVisibility();
}

void SMaterialLabPreviewViewport::UpdateDebugLightVisibility()
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

void SMaterialLabPreviewViewport::SetPreviewQuality(const EMaterialLabPreviewQuality Quality)
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	FEngineShowFlags& ShowFlags = PreviewViewportClient->EngineShowFlags;
	ShowFlags.SetDynamicShadows(true);
	switch (Quality)
	{
	case EMaterialLabPreviewQuality::Low:
		ShowFlags.SetLumenGlobalIllumination(false);
		ShowFlags.SetLumenReflections(false);
		ShowFlags.SetAmbientOcclusion(false);
		ShowFlags.SetScreenSpaceAO(false);
		ShowFlags.SetScreenSpaceReflections(false);
		break;
	case EMaterialLabPreviewQuality::High:
		ShowFlags.SetLumenGlobalIllumination(true);
		ShowFlags.SetLumenReflections(true);
		ShowFlags.SetAmbientOcclusion(true);
		ShowFlags.SetScreenSpaceAO(true);
		ShowFlags.SetScreenSpaceReflections(true);
		break;
	case EMaterialLabPreviewQuality::Medium:
	default:
		ShowFlags.SetLumenGlobalIllumination(false);
		ShowFlags.SetLumenReflections(false);
		ShowFlags.SetAmbientOcclusion(true);
		ShowFlags.SetScreenSpaceAO(true);
		ShowFlags.SetScreenSpaceReflections(true);
		break;
	}
	PreviewViewportClient->Invalidate();
}

void SMaterialLabPreviewViewport::OrbitCamera(const float YawDelta, const float PitchDelta)
{
	CameraYaw = FMath::Fmod(CameraYaw + YawDelta * 0.35f, 360.0f);
	CameraPitch = FMath::Clamp(CameraPitch + PitchDelta * 0.25f, -75.0f, 20.0f);
	UpdateCamera();
}

void SMaterialLabPreviewViewport::RotateLighting(const float YawDelta)
{
	if (bUsingHdri && HdriPreviewProfile)
	{
		HdriYaw = FMath::Fmod(HdriYaw - YawDelta * 0.22f + 360.0f, 360.0f);
		HdriPreviewProfile->LightingRigRotation = HdriYaw;
		PreviewScene.UpdateScene(*HdriPreviewProfile, true, true, false, false);
		PreviewScene.SetEnvironmentVisibility(false, true);
		UpdateHdriFillLight();
	}
	else
	{
		LightingYaw = FMath::Fmod(LightingYaw + YawDelta * 0.35f, 360.0f);
		PreviewScene.SetLightDirection(FRotator(-35.0f, LightingYaw, 0.0f));
	}
	if (PreviewViewportClient.IsValid())
	{
		PreviewViewportClient->Invalidate();
	}
}

void SMaterialLabPreviewViewport::ZoomCamera(const float ZoomDelta)
{
	CameraDistance = FMath::Clamp(CameraDistance - ZoomDelta * 6.0f, 75.0f, 400.0f);
	UpdateStudioFog();
	UpdateCamera();
}

void SMaterialLabPreviewViewport::SetCameraFov(const float FovDegrees)
{
	CameraFov = FMath::Clamp(FovDegrees, 20.0f, 90.0f);
	UpdateCamera();
}

void SMaterialLabPreviewViewport::ResetCameraAndLighting()
{
	CameraDistance = 225.0f;
	CameraYaw = 195.0f;
	CameraPitch = -8.0f;
	CameraFov = 50.0f;
	HdriYaw = 0.0f;
	SetStudioLighting(EMaterialLabStudioLighting::Neutral);
	UpdateStudioFog();
	UpdateCamera();
}

void SMaterialLabPreviewViewport::UpdateCamera()
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

TSharedRef<FEditorViewportClient> SMaterialLabPreviewViewport::MakeEditorViewportClient()
{
	PreviewViewportClient = MakeShared<FMaterialLabPreviewViewportClient>(PreviewScene, SharedThis(this), *this);
	PreviewViewportClient->SetViewMode(VMI_Lit);
	PreviewViewportClient->SetRealtime(true);
	PreviewViewportClient->EngineShowFlags.SetGrid(false);
	PreviewViewportClient->EngineShowFlags.SetSelectionOutline(false);
	SetPreviewQuality(EMaterialLabPreviewQuality::Medium);
	PreviewScene.SetLightDirection(FRotator(-35.0f, LightingYaw, 0.0f));
	UpdateCamera();
	return PreviewViewportClient.ToSharedRef();
}

