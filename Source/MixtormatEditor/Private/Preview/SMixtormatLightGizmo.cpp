// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Preview/SMixtormatLightGizmo.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "MixtormatEditorModule.h"
#include "Rendering/DrawElements.h"

namespace MixtormatLightGizmo
{
	FTransform ObjectTransform(const FBoxSphereBounds& Bounds, const FQuat& CameraRotation)
	{
		const double Scale = ObjectRadius / FMath::Max<double>(Bounds.SphereRadius, UE_SMALL_NUMBER);
		const FQuat Rotation = CameraRotation.Inverse();
		return FTransform(Rotation, -Rotation.RotateVector(Bounds.Origin * Scale), FVector(Scale));
	}

	FTransform ArrowTransform(
		const FBoxSphereBounds& Bounds, const FQuat& CameraRotation, const FVector& LightDirection)
	{
		// Keep the entire arrow inside 1.25 object radii, with a small gap above the surface.
		const double Radius = ObjectRadius * 0.115;
		const double Scale = Radius / FMath::Max<double>(Bounds.SphereRadius, UE_SMALL_NUMBER);
		const FVector Incoming = LightDirection.GetSafeNormal();
		// The light's +X is ray travel, but the authored arrow's forward is exactly +Y.
		const FQuat Rotation = CameraRotation.Inverse() * FRotationMatrix::MakeFromY(Incoming).ToQuat();
		const FVector Center = CameraRotation.UnrotateVector(-Incoming * (FootprintRadius - Radius));
		return FTransform(Rotation, Center - Rotation.RotateVector(Bounds.Origin * Scale), FVector(Scale));
	}
}

SMixtormatLightGizmo::SMixtormatLightGizmo()
	: Scene(FPreviewScene::ConstructionValues()
		.SetLightBrightness(4.0f)
		.SetSkyBrightness(1.0f))
{
}

SMixtormatLightGizmo::~SMixtormatLightGizmo()
{
	Brush.SetResourceObject(nullptr);
	if (Capture)
	{
		Scene.RemoveComponent(Capture);
	}
	if (Arrow)
	{
		Scene.RemoveComponent(Arrow);
	}
	if (Object)
	{
		Scene.RemoveComponent(Object);
	}
}

void SMixtormatLightGizmo::Construct(const FArguments& InArgs)
{
	CameraRotation = InArgs._CameraRotation;
	LightDirection = InArgs._LightDirection;
	SetClipping(EWidgetClipping::ClipToBounds);

	UStaticMesh* ObjectMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Mixtormat/Meshes/SM_Mixtormat_Gizmo_Obj.SM_Mixtormat_Gizmo_Obj"));
	UStaticMesh* ArrowMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Mixtormat/Meshes/SM_Mixtormat_Gizmo_Light_Arrow.SM_Mixtormat_Gizmo_Light_Arrow"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Mixtormat/Materials/M_Mixtormat_Gizmos.M_Mixtormat_Gizmos"));
	if (!ObjectMesh || !ArrowMesh || !Material)
	{
		UE_LOG(LogMixtormat, Warning, TEXT("Light gizmo requires both Mixtormat gizmo meshes and M_Mixtormat_Gizmos."));
		return;
	}

	const auto AddMesh = [this, Material](UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetTransientPackage());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		for (int32 Index = 0; Index < FMath::Max(1, Component->GetNumMaterials()); ++Index)
		{
			Component->SetMaterial(Index, Material);
		}
		Scene.AddComponent(Component, FTransform::Identity);
		return Component;
	};
	Object = AddMesh(ObjectMesh);
	Arrow = AddMesh(ArrowMesh);

	RenderTarget.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
	// SceneColorHDR stores inverse opacity: empty pixels must start at alpha one.
	RenderTarget->ClearColor = FLinearColor(0, 0, 0, 1);
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitCustomFormat(640, 640, PF_FloatRGBA, true);
	RenderTarget->UpdateResourceImmediate(true);
	Brush.SetResourceObject(RenderTarget.Get());
	Brush.ImageSize = FVector2D(MixtormatLightGizmo::Size);
	Brush.DrawAs = ESlateBrushDrawType::Image;

	Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = SCS_SceneColorHDR;
	Capture->TextureTarget = RenderTarget.Get();
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = MixtormatLightGizmo::FootprintRadius * 2.0f * 1.04f;
	Capture->PostProcessBlendWeight = 0.0f;
	Capture->ShowFlags.SetLighting(true);
	Capture->ShowFlags.SetDynamicShadows(false);
	Capture->ShowFlags.SetPostProcessing(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetEyeAdaptation(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetAntiAliasing(true);
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->ShowFlags.SetOnScreenDebug(false);
	Scene.AddComponent(Capture, FTransform(FVector(-MixtormatLightGizmo::ObjectRadius * 1.0f, 0, 0)));
}

void SMixtormatLightGizmo::Tick(const FGeometry& AllottedGeometry, double CurrentTime, float DeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
	if (!Capture)
	{
		return;
	}
	const FQuat Camera = CameraRotation.Get();
	const FVector Direction = LightDirection.Get();
	if (bCaptured && Camera.Equals(LastCameraRotation) && Direction.Equals(LastLightDirection))
	{
		return;
	}

	Object->SetWorldTransform(MixtormatLightGizmo::ObjectTransform(
		Object->GetStaticMesh()->GetBounds(), Camera));
	Arrow->SetWorldTransform(MixtormatLightGizmo::ArrowTransform(
		Arrow->GetStaticMesh()->GetBounds(), Camera, Direction));
	Scene.GetWorld()->SendAllEndOfFrameUpdates();
	Capture->CaptureScene();
	LastCameraRotation = Camera;
	LastLightDirection = Direction;
	bCaptured = true;
}

FVector2D SMixtormatLightGizmo::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	(void)LayoutScaleMultiplier;
	return FVector2D(MixtormatLightGizmo::Size);
}

int32 SMixtormatLightGizmo::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	(void)Args;
	(void)MyCullingRect;
	(void)bParentEnabled;
	if (bCaptured)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			&Brush,
			ESlateDrawEffect::InvertAlpha,
			InWidgetStyle.GetColorAndOpacityTint());
	}
	return LayerId;
}
