// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

namespace MixtormatLightGizmo
{
	constexpr float Size = 240.0f;
	constexpr float ObjectRadius = 50.0f;
	constexpr float FootprintRadius = ObjectRadius * 1.25f;

	FTransform ObjectTransform(const FBoxSphereBounds& Bounds, const FQuat& CameraRotation);
	FTransform ArrowTransform(
		const FBoxSphereBounds& Bounds, const FQuat& CameraRotation, const FVector& LightDirection);
}

// A separate, cached preview scene; never adds primitives to the material preview world.
class SMixtormatLightGizmo final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLightGizmo) {}
		SLATE_ATTRIBUTE(FQuat, CameraRotation)
		SLATE_ATTRIBUTE(FVector, LightDirection)
	SLATE_END_ARGS()

	SMixtormatLightGizmo();
	virtual ~SMixtormatLightGizmo() override;
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, double CurrentTime, float DeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FPreviewScene Scene;
	UStaticMeshComponent* Object = nullptr;
	UStaticMeshComponent* Arrow = nullptr;
	USceneCaptureComponent2D* Capture = nullptr;
	TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget;
	FSlateBrush Brush;
	TAttribute<FQuat> CameraRotation;
	TAttribute<FVector> LightDirection;
	FQuat LastCameraRotation = FQuat::Identity;
	FVector LastLightDirection = FVector::ZeroVector;
	bool bCaptured = false;
};
