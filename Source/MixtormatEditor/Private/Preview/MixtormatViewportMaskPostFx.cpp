// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Preview/MixtormatViewportMaskPostFx.h"
#include "MixtormatEditorModule.h"
#include "MixtormatGpuCompositor.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AdvancedPreviewScene.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionSubtract.h"
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
	const FName ColorName(TEXT("DA_DiagnosticColor"));
	const FName ModeName(TEXT("DA_DiagnosticMode"));
	// Material parameter contract; values must match the documented artist-authored graph.
	enum class EDiagnosticDataMode : uint8 { Mask = 0, Flow = 1, ReservedIds = 3 };
	const FName OpacityName(TEXT("DA_DiagnosticOpacity"));
	const FName HeightName(TEXT("DA_Height"));
	const FName UseHeightName(TEXT("DA_UseHeight"));
	const FName HeightAmountName(TEXT("DA_HeightAmount"));

	static UMaterialInterface* LoadDiagnosticPostMaterial()
	{
		// Artist-authored override, tried first. The built-in default below removes any
		// dependency on these assets existing: a miss is normal, not an error.
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
				UE_LOG(LogMixtormat, Log,
					TEXT("Mixtormat: using artist-authored diagnostic post material: %s"), Path);
				return Mat;
			}
		}

		UE_LOG(LogMixtormat, Log,
			TEXT("Mixtormat: no artist-authored M_MixtormatDiagnosticPost found; using the built-in default. ")
			TEXT("Searched: /Game/M_MixtormatDiagnosticPost, /Game/Materials/M_MixtormatDiagnosticPost, ")
			TEXT("/Mixtormat/M_MixtormatDiagnosticPost, /Mixtormat/Materials/M_MixtormatDiagnosticPost"));
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

	// Self-contained fallback for the artist-authored material: the same parameter contract
	// (DA_DiagnosticUvBuffer / DA_DiagnosticField / DA_DiagnosticColor / DA_DiagnosticMode /
	// DA_DiagnosticOpacity, PostProcessInput0 compositing) built from material expressions, so
	// W/E work with no /Game/ asset present. Modes 0/1 both composite generated color:
	// mask/scalar coverage in mode 0, Flow/UV in mode 1. Raw fields stay bound for overrides.
	UMaterial* MakeDefaultPostMaterial(UTextureRenderTarget2D* FieldDefault)
	{
		UMaterial* Material = NewObject<UMaterial>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_PostProcess;
		Material->BlendableLocation = BL_SceneColorAfterTonemapping;
		Material->BlendableOutputAlpha = true;
		Material->SetShadingModel(MSM_Unlit);

		// Output 0 of ScreenPosition is the viewport UV in 5.8; it has no mapping property.
		auto* ScreenUV = AddNode<UMaterialExpressionScreenPosition>(Material);

		auto* UvSample = AddSample(Material, UvBufferName, nullptr);
		UvSample->Coordinates.Connect(0, ScreenUV);

		auto* FieldSample = AddSample(Material, FieldName, FieldDefault);
		FieldSample->Coordinates.Connect(0, UvSample);

		auto* ColorSample = AddSample(Material, ColorName, FieldDefault);
		// Use the target's trilinear wrap sampler and implicit mesh-UV derivatives.
		// A forced mip-zero sample would defeat the color mip chain.
		ColorSample->SamplerSource = SSM_FromTextureAsset;
		ColorSample->Coordinates.Connect(0, UvSample);

		auto* Mode = AddNode<UMaterialExpressionScalarParameter>(Material);
		Mode->ParameterName = ModeName;
		Mode->DefaultValue = 0.0f;

		// Only Mask (0) and Flow (1) are emitted by the current PostFX path.
		auto* FlowWeight = AddNode<UMaterialExpressionClamp>(Material);
		FlowWeight->Input.Connect(0, Mode);

		// Both diagnostic modes supply premultiplied RGB and independent coverage A.
		auto* ColorRgb = AddNode<UMaterialExpressionComponentMask>(Material);
		ColorRgb->Input.Connect(0, ColorSample);
		ColorRgb->R = true;
		ColorRgb->G = true;
		ColorRgb->B = true;
		ColorRgb->A = false;
		auto* ColorCoverage = AddNode<UMaterialExpressionComponentMask>(Material);
		ColorCoverage->Input.Connect(0, ColorSample);
		ColorCoverage->R = false;
		ColorCoverage->G = false;
		ColorCoverage->B = false;
		ColorCoverage->A = true;
		auto* Zero = AddNode<UMaterialExpressionConstant>(Material);
		Zero->R = 0.0f;
		// Mask coverage is already in Color.a; only Flow uses Field.R as availability.
		auto* Coverage = AddNode<UMaterialExpressionComponentMask>(Material);
		Coverage->Input.Connect(0, FieldSample);
		Coverage->R = true;
		Coverage->G = false;
		Coverage->B = false;
		Coverage->A = false;
		auto* Validity = AddNode<UMaterialExpressionComponentMask>(Material);
		Validity->Input.Connect(0, UvSample);
		Validity->R = false;
		Validity->G = false;
		Validity->B = true;
		Validity->A = false;
		auto* Half = AddNode<UMaterialExpressionConstant>(Material);
		Half->R = 0.5f;
		auto* One = AddNode<UMaterialExpressionConstant>(Material);
		One->R = 1.0f;
		auto* ValidityGate = AddNode<UMaterialExpressionIf>(Material);
		ValidityGate->A.Connect(0, Validity);
		ValidityGate->B.Connect(0, Half);
		ValidityGate->AGreaterThanB.Connect(0, One);
		ValidityGate->AEqualsB.Connect(0, One);
		ValidityGate->ALessThanB.Connect(0, Zero);
		auto* Opacity = AddNode<UMaterialExpressionScalarParameter>(Material);
		Opacity->ParameterName = OpacityName;
		Opacity->DefaultValue = 0.55f;
		auto* Availability = AddNode<UMaterialExpressionLinearInterpolate>(Material);
		Availability->A.Connect(0, One);
		Availability->B.Connect(0, Coverage);
		Availability->Alpha.Connect(0, FlowWeight);
		auto* CoverageGated = AddNode<UMaterialExpressionMultiply>(Material);
		CoverageGated->A.Connect(0, Availability);
		CoverageGated->B.Connect(0, ValidityGate);
		auto* Alpha = AddNode<UMaterialExpressionMultiply>(Material);
		Alpha->A.Connect(0, CoverageGated);
		Alpha->B.Connect(0, Opacity);

		auto* SceneColor = AddNode<UMaterialExpressionSceneTexture>(Material);
		SceneColor->SceneTextureId = PPI_PostProcessInput0;
		SceneColor->Coordinates.Connect(0, ScreenUV);
		auto* FinalAlpha = AddNode<UMaterialExpressionMultiply>(Material);
		FinalAlpha->A.Connect(0, Alpha);
		FinalAlpha->B.Connect(0, ColorCoverage);
		auto* Background = AddNode<UMaterialExpressionLinearInterpolate>(Material);
		Background->A.Connect(0, SceneColor);
		Background->B.Connect(0, Zero);
		Background->Alpha.Connect(0, FinalAlpha);
		auto* Foreground = AddNode<UMaterialExpressionMultiply>(Material);
		Foreground->A.Connect(0, ColorRgb);
		Foreground->B.Connect(0, Alpha);
		auto* Final = AddNode<UMaterialExpressionAdd>(Material);
		Final->A.Connect(0, Background);
		Final->B.Connect(0, Foreground);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Final);
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
	// A failed initialization is latched: retrying it every tick would rebuild the UV target
	// and re-run the material search for a state that cannot change on its own.
	if (bInitializationFailed || !InitialField)
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

	// The artist-authored material stays the first choice; the built-in default removes the
	// dependency on a /Game/ asset that may not exist in this project.
	UMaterialInterface* LoadedPostMaterial = LoadDiagnosticPostMaterial();
	if (!LoadedPostMaterial)
	{
		DefaultPostMaterial.Reset(MakeDefaultPostMaterial(InitialField));
		LoadedPostMaterial = DefaultPostMaterial.Get();
	}
	if (!UvMaterial.IsValid() || !LoadedPostMaterial)
	{
		bInitializationFailed = true;
		UE_LOG(LogMixtormat, Warning,
			TEXT("Mixtormat: diagnostic PostFX initialization failed; W/E overlays are unavailable."));
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
	UTextureRenderTarget2D* DiagnosticColor,
	const EMixtormatPreviewOutputKind OutputKind)
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
	BlendInstance->SetTextureParameterValue(ColorName, DiagnosticColor);
	const bool bFlow = OutputKind == EMixtormatPreviewOutputKind::FlowDirection
		|| OutputKind == EMixtormatPreviewOutputKind::WarpedUVGrid;
	const EDiagnosticDataMode Mode = bFlow ? EDiagnosticDataMode::Flow : EDiagnosticDataMode::Mask;
	BlendInstance->SetScalarParameterValue(ModeName, static_cast<float>(Mode));
	BlendInstance->SetScalarParameterValue(OpacityName, OverlayOpacity);
	// PreviewScene worlds are not guaranteed to tick capture components on their
	// own. The viewport client invokes this after camera maintenance each viewport tick;
	// UV mapping refreshes independently of Slate invalidation and generator recomposition.
	UvCapture->CaptureScene();
	bReady = true;
}
