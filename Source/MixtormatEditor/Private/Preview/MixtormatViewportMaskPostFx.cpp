// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Preview/MixtormatViewportMaskPostFx.h"

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
	const FName OpacityName(TEXT("DA_DiagnosticOpacity"));
	const FName HeightName(TEXT("DA_Height"));
	const FName UseHeightName(TEXT("DA_UseHeight"));
	const FName HeightAmountName(TEXT("DA_HeightAmount"));

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

	UMaterial* MakePostMaterial(
		UTextureRenderTarget2D* UvDefault, UTextureRenderTarget2D* FieldDefault)
	{
		UMaterial* Material = NewObject<UMaterial>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		Material->bIsBlendable = false;

		auto* SceneColor = AddNode<UMaterialExpressionSceneTexture>(Material);
		SceneColor->SceneTextureId = PPI_PostProcessInput0;
		// PostProcessInput0 is RGBA; the cyan tint is RGB. Lerp requires
		// matching vector arity, so drop scene alpha before the blend.
		auto* SceneRgb = AddNode<UMaterialExpressionComponentMask>(Material);
		SceneRgb->R = true;
		SceneRgb->G = true;
		SceneRgb->B = true;
		SceneRgb->Input.Connect(0, SceneColor);
		auto* ScreenUV = AddNode<UMaterialExpressionScreenPosition>(Material);
		auto* UVSample = AddSample(Material, UvBufferName, UvDefault);
		UVSample->Coordinates.Connect(0, ScreenUV);

		auto* MaterialUV = AddNode<UMaterialExpressionComponentMask>(Material);
		MaterialUV->R = true;
		MaterialUV->G = true;
		MaterialUV->Input.Connect(0, UVSample);

		auto* Coverage = AddSample(Material, FieldName, FieldDefault);
		Coverage->Coordinates.Connect(0, MaterialUV);
		auto* FieldR = AddNode<UMaterialExpressionComponentMask>(Material);
		FieldR->R = true;
		FieldR->Input.Connect(0, Coverage);

		// UV capture clears to zero outside the subject. Validity lives in B,
		// so the post pass never colors the floor/background with the edge texel.
		auto* Validity = AddNode<UMaterialExpressionComponentMask>(Material);
		Validity->B = true;
		Validity->Input.Connect(0, UVSample);
		auto* Gated = AddNode<UMaterialExpressionMultiply>(Material);
		Gated->A.Connect(0, FieldR);
		Gated->B.Connect(0, Validity);

		auto* Opacity = AddNode<UMaterialExpressionScalarParameter>(Material);
		Opacity->SetParameterName(OpacityName);
		Opacity->ExpressionGUID = FGuid::NewGuid();
		Opacity->DefaultValue = 0.55f;
		auto* Alpha = AddNode<UMaterialExpressionMultiply>(Material);
		Alpha->A.Connect(0, Gated);
		Alpha->B.Connect(0, Opacity);
		auto* Cyan = AddNode<UMaterialExpressionConstant3Vector>(Material);
		Cyan->Constant = FLinearColor(0.35f, 0.78f, 0.87f);
		auto* Blend = AddNode<UMaterialExpressionLinearInterpolate>(Material);
		Blend->A.Connect(0, SceneRgb);
		Blend->B.Connect(0, Cyan);
		Blend->Alpha.Connect(0, Alpha);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Blend);
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
	UvTarget->InitAutoFormat(16, 16);
	UvTarget->UpdateResourceImmediate(true);
	CapturedSize = FIntPoint(16, 16);

	UvMaterial.Reset(MakeUvMaterial(InitialField));
	BlendMaterial.Reset(MakePostMaterial(UvTarget.Get(), InitialField));
	if (!UvMaterial.IsValid() || !BlendMaterial.IsValid())
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
		BlendMaterial.Get(), UvCapture));
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
	const float OverlayOpacity)
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
	BlendInstance->SetScalarParameterValue(OpacityName, OverlayOpacity);
	// PreviewScene worlds are not guaranteed to tick capture components on their
	// own. Submit one view-matched UV capture per *active viewport* Slate tick.
	UvCapture->CaptureScene();
	bReady = true;
}
