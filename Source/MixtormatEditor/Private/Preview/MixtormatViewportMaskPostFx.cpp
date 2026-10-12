// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Preview/MixtormatViewportMaskPostFx.h"
#include "MixtormatGpuCompositor.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AdvancedPreviewScene.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"

namespace MixtormatPreview
{
	// Existing master-displacement copy in SMixtormatPreviewViewport.cpp. The UV
	// proxy deliberately uses the *same* helper, not a parallel WPO contract.
	void AddHeightDisplacement(
		UMaterial* Material,
		UMaterialExpressionTextureSampleParameter2D* HeightSample);
}

namespace
{
	const FName UvBufferName(TEXT("DA_DiagnosticUvBuffer"));
	const FName FieldName(TEXT("DA_DiagnosticField"));
	const FName FlowName(TEXT("DA_DiagnosticFlow"));
	const FName ModeName(TEXT("DA_DiagnosticMode"));
	// Material parameter contract; values must match the documented artist-authored graph.
	enum class EDiagnosticDataMode : uint8 { Coverage = 0, RelativeFlow = 1, AbsoluteUV = 2, Scalar = 3 };
	const FName OpacityName(TEXT("DA_DiagnosticOpacity"));
	const FName HeightName(TEXT("DA_Height"));
	const FName UseHeightName(TEXT("DA_UseHeight"));
	const FName HeightAmountName(TEXT("DA_HeightAmount"));

	static UMaterialInterface* LoadDiagnosticPostMaterial()
	{
		// Try a few common local-only asset locations.
		static const TCHAR* CandidatePaths[] =
		{
			TEXT("/Game/M_MixtormatDiagnosticPost.M_MixtormatDiagnosticPost"),
			TEXT("/Game/Materials/M_MixtormatDiagnosticPost.M_MixtormatDiagnosticPost"),
			TEXT("/Mixtormat/M_MixtormatDiagnosticPost.M_MixtormatDiagnosticPost"),
			TEXT("/Mixtormat/Materials/M_MixtormatDiagnosticPost.M_MixtormatDiagnosticPost")
		};

		for (const TCHAR* Path : CandidatePaths)
		{
			if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path))
			{
				UE_LOG(LogTemp, Log,
					TEXT("Mixtormat: loaded diagnostic post material: %s"), Path);
				return Mat;
			}
		}

		UE_LOG(LogTemp, Warning,
			TEXT("Mixtormat: could not load M_MixtormatDiagnosticPost. ")
			TEXT("Expected one of: /Game/M_MixtormatDiagnosticPost, ")
			TEXT("/Game/Materials/M_MixtormatDiagnosticPost, ")
			TEXT("/Mixtormat/M_MixtormatDiagnosticPost, ")
			TEXT("/Mixtormat/Materials/M_MixtormatDiagnosticPost"));
		return nullptr;
	}

	template<typename T>
	T* AddNode(UMaterial* Material)
	{
		T* Node = NewObject<T>(Material);
		Material->GetExpressionCollection().AddExpression(Node);
		return Node;
	}

	UMaterialExpressionTextureSampleParameter2D* AddSample(
		UMaterial* Material, const FName Parameter, UTextureRenderTarget2D* Default)
	{
		auto* Sample = AddNode<UMaterialExpressionTextureSampleParameter2D>(Material);
		Sample->SetParameterName(Parameter);
		Sample->ExpressionGUID = FGuid::NewGuid();
		// The dynamically created render targets are exposed to UE's material
		// compiler as Color textures. LinearColor rejects them even though
		// their float data is intended to remain unmodified.
		Sample->SamplerType = SAMPLERTYPE_Color;
		Sample->Texture = Default;
		return Sample;
	}

	UMaterial* MakeUvMaterial(UTextureRenderTarget2D* HeightDefault)
	{
		UMaterial* Material = NewObject<UMaterial>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);

		auto* UV = AddNode<UMaterialExpressionTextureCoordinate>(Material);
		auto* One = AddNode<UMaterialExpressionConstant>(Material);
		One->R = 1.0f;
		auto* WithValidity = AddNode<UMaterialExpressionAppendVector>(Material);
		WithValidity->A.Connect(0, UV);
		WithValidity->B.Connect(0, One);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, WithValidity);

		auto* Height = AddSample(Material, HeightName, HeightDefault);
		MixtormatPreview::AddHeightDisplacement(Material, Height);
		Material->PostEditChange();
		return Material;
	}

}

FMixtormatViewportMaskPostFx::FMixtormatViewportMaskPostFx(
	FAdvancedPreviewScene& InScene)
	: Scene(InScene)
{
}

FMixtormatViewportMaskPostFx::~FMixtormatViewportMaskPostFx()
{
	if (UvCapture)
	{
		Scene.RemoveComponent(UvCapture);
	}
	if (UvProxy)
	{
		Scene.RemoveComponent(UvProxy);
	}
}

bool FMixtormatViewportMaskPostFx::EnsureResources(
	UTextureRenderTarget2D* InitialField)
{
	if (BlendInstance.IsValid())
	{
		return true;
	}
	if (!InitialField)
	{
		return false;
	}

	UvTarget.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
	UvTarget->RenderTargetFormat = RTF_RGBA32f;
	UvTarget->ClearColor = FLinearColor::Black;
	// These are pixel-aligned coordinates plus a binary mesh-validity flag.
	// Bilinear filtering mixes foreground validity with the black background,
	// creating a cyan fringe when the post material multiplies by channel B.
	UvTarget->Filter = TF_Nearest;
	UvTarget->InitAutoFormat(16, 16);
	UvTarget->UpdateResourceImmediate(true);
	CapturedSize = FIntPoint(16, 16);

	UvMaterial.Reset(MakeUvMaterial(InitialField));

	UMaterialInterface* LoadedPostMaterial = LoadDiagnosticPostMaterial();
	if (!UvMaterial.IsValid() || !LoadedPostMaterial)
	{
		return false;
	}
	UvProxy = NewObject<UStaticMeshComponent>();
	UvProxy->SetMobility(EComponentMobility::Movable);
	UvProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UvProxy->SetCastShadow(false);
	UvProxy->SetVisibleInSceneCaptureOnly(true);
	Scene.AddComponent(UvProxy, FTransform::Identity);
	UvInstance.Reset(UMaterialInstanceDynamic::Create(UvMaterial.Get(), UvProxy));
	UvProxy->SetMaterial(0, UvInstance.Get());

	UvCapture = NewObject<USceneCaptureComponent2D>();
	UvCapture->SetMobility(EComponentMobility::Movable);
	UvCapture->CaptureSource = SCS_SceneColorHDR;
	UvCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	UvCapture->TextureTarget = UvTarget.Get();
	UvCapture->bCaptureEveryFrame = false;
	UvCapture->bCaptureOnMovement = false;
	UvCapture->ShowFlags.SetPostProcessing(false);
	Scene.AddComponent(UvCapture, FTransform::Identity);
	UvCapture->ShowOnlyComponent(UvProxy);

	BlendInstance.Reset(UMaterialInstanceDynamic::Create(
		LoadedPostMaterial, GetTransientPackage()));
	return true;
}

void FMixtormatViewportMaskPostFx::Disable()
{
	bReady = false;
	if (UvCapture)
	{
		UvCapture->bCaptureEveryFrame = false;
	}
}

void FMixtormatViewportMaskPostFx::Update(
	const UStaticMeshComponent& PreviewMesh,
	UTextureRenderTarget2D* MaskField,
	UTextureRenderTarget2D* HeightField,
	const FIntPoint ViewSize,
	const FVector& CameraLocation,
	const FRotator& CameraRotation,
	const float FovDegrees,
	const bool bDisplace,
	const float DisplacementAmount,
	const float OverlayOpacity,
	UTextureRenderTarget2D* FlowField,
	const EMixtormatPreviewOutputKind OutputKind,
	const bool bScalarDiagnostic)
{
	if (!MaskField || !HeightField || !PreviewMesh.GetStaticMesh()
		|| ViewSize.X <= 0 || ViewSize.Y <= 0 || !EnsureResources(MaskField))
	{
		Disable();
		return;
	}
	if (CapturedSize != ViewSize)
	{
		UvTarget->ResizeTarget(ViewSize.X, ViewSize.Y);
		CapturedSize = ViewSize;
	}
	if (UvProxy->GetStaticMesh() != PreviewMesh.GetStaticMesh())
	{
		UvProxy->SetStaticMesh(PreviewMesh.GetStaticMesh());
	}
	if (!UvProxy->GetComponentTransform().Equals(PreviewMesh.GetComponentTransform()))
	{
		UvProxy->SetWorldTransform(PreviewMesh.GetComponentTransform());
	}

	UvInstance->SetTextureParameterValue(HeightName, HeightField);
	UvInstance->SetScalarParameterValue(UseHeightName, bDisplace ? 1.0f : 0.0f);
	UvInstance->SetScalarParameterValue(HeightAmountName, DisplacementAmount);
	UvCapture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
	UvCapture->FOVAngle = FovDegrees;
	BlendInstance->SetTextureParameterValue(UvBufferName, UvTarget.Get());
	BlendInstance->SetTextureParameterValue(FieldName, MaskField);
	BlendInstance->SetTextureParameterValue(FlowName, FlowField);
	const EDiagnosticDataMode Mode = OutputKind == EMixtormatPreviewOutputKind::WarpedUVGrid
		? EDiagnosticDataMode::AbsoluteUV : OutputKind == EMixtormatPreviewOutputKind::FlowDirection
			? EDiagnosticDataMode::RelativeFlow : bScalarDiagnostic
				? EDiagnosticDataMode::Scalar : EDiagnosticDataMode::Coverage;
	BlendInstance->SetScalarParameterValue(ModeName, static_cast<float>(Mode));
	BlendInstance->SetScalarParameterValue(OpacityName, OverlayOpacity);
	// PreviewScene worlds are not guaranteed to tick capture components on their
	// own. Submit one view-matched UV capture per *active viewport* Slate tick.
	UvCapture->CaptureScene();
	bReady = true;
}
