// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatPreviewViewport.h"

#include "AssetViewerSettings.h"
#include "Components/BoxReflectionCaptureComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "Engine/TextureCube.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "MixtormatEditorModule.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMaterial.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
#include "Services/MixtormatPaths.h"
#include "Style/MixtormatThemeStore.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
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
	// Studio floor fade radii, in world units. Pushed from the subject's own size so the
	// floor dissolves at the same point relative to the object whatever the object is.
	const FName FloorFadeInParameter(TEXT("DA_FadeIn"));
	const FName FloorFadeOutParameter(TEXT("DA_FadeOut"));
	// Same texture parameter names FMixtormatGpuCompositor::BindOutputs already sets on the real
	// master-material instance, so the channel-preview material can be fed by that same call
	// rather than a second copy of the texture-fetch logic.
	const FName ChannelPreviewBaseColorParameter(TEXT("DA_BaseColor"));
	const FName ChannelPreviewNormalParameter(TEXT("DA_Normal"));
	const FName ChannelPreviewRamhParameter(TEXT("DA_RAMH"));
	const FName ChannelPreviewHeightParameter(TEXT("DA_Height"));
	const FName ChannelPreviewModeParameter(TEXT("DA_ChannelPreviewMode"));


	// Both transient preview materials displace the mesh the way the master does, so a debug or
	// channel view keeps the surface's shape instead of collapsing it flat. The tessellation switch
	// and the Nanite displacement scaling (magnitude, centre, fade) are copied from the master when
	// the material is built -- one source for those numbers, not a second set here -- and the input
	// is the composited height gated by the same DA_UseHeight and scaled by the same
	// DA_HeightAmount the master reads.
	void AddHeightDisplacement(
		UMaterial* Material,
		UMaterialExpressionTextureSampleParameter2D* HeightSample)
	{
		UMaterialInterface* MasterInterface = LoadObject<UMaterialInterface>(
			nullptr,
			*FMixtormatPaths::MasterMaterialObjectPath());
		const UMaterial* Master = MasterInterface ? MasterInterface->GetMaterial() : nullptr;
		if (!Master)
		{
			UE_LOG(LogMixtormat, Error,
				TEXT("Master material %s could not be loaded. Preview material built without displacement."),
				*FMixtormatPaths::MasterMaterialObjectPath());
			return;
		}
		Material->bEnableTessellation = Master->bEnableTessellation;
		Material->DisplacementScaling = Master->DisplacementScaling;
		Material->bEnableDisplacementFade = Master->bEnableDisplacementFade;
		Material->DisplacementFadeRange = Master->DisplacementFadeRange;
		Material->SetUsageByFlag(MATUSAGE_Nanite, true);

		const auto MakeScalar = [Material](const FName ParameterName, const float DefaultValue)
		{
			UMaterialExpressionScalarParameter* Parameter =
				NewObject<UMaterialExpressionScalarParameter>(Material);
			Parameter->SetParameterName(ParameterName);
			Parameter->ExpressionGUID = FGuid::NewGuid();
			Parameter->DefaultValue = DefaultValue;
			Material->GetExpressionCollection().AddExpression(Parameter);
			return Parameter;
		};
		UMaterialExpressionScalarParameter* UseHeight = MakeScalar(UseHeightParameter, 1.0f);
		UMaterialExpressionScalarParameter* HeightAmount = MakeScalar(HeightAmountParameter, 1.0f);

		// Output 1 is R: the height target is single-channel, and Connect copies that mask onto
		// the input (see WireInput in CreateChannelPreviewMaterial).
		UMaterialExpressionMultiply* Gated = NewObject<UMaterialExpressionMultiply>(Material);
		Gated->A.Connect(1, HeightSample);
		Gated->B.Connect(0, UseHeight);
		Material->GetExpressionCollection().AddExpression(Gated);
		UMaterialExpressionMultiply* Scaled = NewObject<UMaterialExpressionMultiply>(Material);
		Scaled->A.Connect(0, Gated);
		Scaled->B.Connect(0, HeightAmount);
		Material->GetExpressionCollection().AddExpression(Scaled);
		Material->GetEditorOnlyData()->Displacement.Connect(0, Scaled);
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

		// The debug colour is never the shape: the mesh displaces by the composited height,
		// bound alongside the debug texture on every compose.
		UMaterialExpressionTextureSampleParameter2D* HeightSample =
			NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		HeightSample->SetParameterName(ChannelPreviewHeightParameter);
		HeightSample->ExpressionGUID = FGuid::NewGuid();
		HeightSample->SamplerType = SAMPLERTYPE_Color;
		HeightSample->Texture = DebugTexture->Texture;
		Material->GetExpressionCollection().AddExpression(HeightSample);
		AddHeightDisplacement(Material, HeightSample);

		Material->PostEditChange();
		return Material;
	}

	// The V-key diagnostic cycle needs an Unlit look at a raw texture channel, and the real
	// master material's shading model is fixed Lit -- there is no per-instance switch for that
	// without reworking the production Substrate graph the bake path also depends on. So this
	// follows the same shortcut CreateDebugMaterial already takes above: a second small transient
	// material, built once in C++, never touching the saved asset.
	//
	// It takes the same DA_BaseColor / DA_Normal / DA_RAMH / DA_Height parameter names
	// FMixtormatGpuCompositor::BindOutputs already knows how to fill, so binding this material's
	// textures is that same call, not a second copy of the texture lookup.
	UMaterial* CreateChannelPreviewMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Material)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);

		UTexture2D* WhiteFallback = LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));

		auto MakeTextureParam = [Material, WhiteFallback](const FName ParameterName)
		{
			UMaterialExpressionTextureSampleParameter2D* Sample =
				NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
			Sample->SetParameterName(ParameterName);
			Sample->ExpressionGUID = FGuid::NewGuid();
			// Matches CreateDebugMaterial's fallback above: the sampler type is validated against
			// the default texture's own compression settings at compile time, and WhiteSquareTexture
			// is a Color-setting texture, so LinearColor here would fail that check. The bound
			// render targets carry their own sRGB flag regardless of this setting.
			Sample->SamplerType = SAMPLERTYPE_Color;
			Sample->Texture = WhiteFallback;
			Material->GetExpressionCollection().AddExpression(Sample);
			return Sample;
		};

		UMaterialExpressionTextureSampleParameter2D* BaseColorSample =
			MakeTextureParam(ChannelPreviewBaseColorParameter);
		UMaterialExpressionTextureSampleParameter2D* NormalSample =
			MakeTextureParam(ChannelPreviewNormalParameter);
		UMaterialExpressionTextureSampleParameter2D* RamhSample =
			MakeTextureParam(ChannelPreviewRamhParameter);
		UMaterialExpressionTextureSampleParameter2D* HeightSample =
			MakeTextureParam(ChannelPreviewHeightParameter);

		UMaterialExpressionScalarParameter* FuzzSample =
			NewObject<UMaterialExpressionScalarParameter>(Material);
		FuzzSample->SetParameterName(FuzzInfluenceParameter);
		FuzzSample->ExpressionGUID = FGuid::NewGuid();
		FuzzSample->DefaultValue = 0.0f;
		Material->GetExpressionCollection().AddExpression(FuzzSample);

		UMaterialExpressionScalarParameter* ModeParam =
			NewObject<UMaterialExpressionScalarParameter>(Material);
		ModeParam->SetParameterName(ChannelPreviewModeParameter);
		ModeParam->ExpressionGUID = FGuid::NewGuid();
		ModeParam->DefaultValue = static_cast<float>(EMixtormatChannelPreview::BaseColor);
		Material->GetExpressionCollection().AddExpression(ModeParam);

		// One source expression and output index per mode, matched to the fixed output layout
		// UMaterialExpressionTextureSample always declares: 0 = RGB, 1 = R, 2 = G, 3 = B, 4 = A.
		// Index 0 (Material) is never read -- that mode never binds this material at all.
		struct FChannelSource
		{
			UMaterialExpression* Expression = nullptr;
			int32 OutputIndex = 0;
		};
		FChannelSource Sources[9];
		Sources[static_cast<int32>(EMixtormatChannelPreview::BaseColor)] = { BaseColorSample, 0 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::Normal)] = { NormalSample, 0 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::Roughness)] = { RamhSample, 1 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::AO)] = { RamhSample, 2 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::Metallic)] = { RamhSample, 3 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::F0)] = { RamhSample, 4 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::Height)] = { HeightSample, 1 };
		Sources[static_cast<int32>(EMixtormatChannelPreview::Fuzz)] = { FuzzSample, 0 };

		// FExpressionInput::Connect, not a hand assignment of Expression/OutputIndex: the R/G/B/A
		// component mask lives on the *input* (Mask/MaskR/.../MaskA), copied over from the
		// source's FExpressionOutput entry only inside ConnectExpression. Setting OutputIndex
		// directly leaves the input unmasked, so every "single channel" pick would otherwise
		// silently come through as full RGB.
		auto WireInput = [](FExpressionInput& Input, const FChannelSource& Source)
		{
			Input.Connect(Source.OutputIndex, Source.Expression);
		};

		// A chain of If nodes, one per non-Material mode above the first, each testing
		// DA_ChannelPreviewMode against its own constant, built low-to-high so ALessThanB can
		// always point at the node just built -- a strictly backward-only reference, so the graph
		// stays acyclic. AGreaterThanB is wired to the node's OWN value rather than forward to the
		// next node: an earlier version pointed it at Chain[Mode + 1] to keep climbing, but that
		// made every adjacent pair reference each other (A's AGreaterThanB -> B, B's ALessThanB ->
		// A), which the material compiler rejects outright ("Expression is part of a cycle") and
		// silently substitutes the default material for every mode -- exactly the "nothing changes
		// when V is pressed" symptom. AGreaterThanB only needs a valid, harmless connection: it is
		// only ever reached for Mode > ConstB, and the outermost node (Chain[LastMode], wired to
		// EmissiveColor below) is the one actually evaluated first, so no interior node's
		// AGreaterThanB branch is reachable at runtime -- Mode never exceeds LastMode.
		const int32 FirstMode = static_cast<int32>(EMixtormatChannelPreview::BaseColor);
		const int32 LastMode = static_cast<int32>(EMixtormatChannelPreview::Fuzz);
		UMaterialExpressionIf* Chain[9] = {};
		FExpressionInput PreviousOutput;
		WireInput(PreviousOutput, Sources[FirstMode]);
		for (int32 Mode = FirstMode + 1; Mode <= LastMode; ++Mode)
		{
			UMaterialExpressionIf* Node = NewObject<UMaterialExpressionIf>(Material);
			Node->A.Connect(0, ModeParam);
			Node->ConstB = static_cast<float>(Mode);
			WireInput(Node->AEqualsB, Sources[Mode]);
			WireInput(Node->AGreaterThanB, Sources[Mode]);
			Node->ALessThanB = PreviousOutput;
			Material->GetExpressionCollection().AddExpression(Node);
			Chain[Mode] = Node;
			PreviousOutput.Connect(0, Node);
		}

		// The root is the node testing the *highest* mode: evaluating it first is what makes
		// every lower node's ALessThanB chain the correct fallback, down to Sources[FirstMode].
		//
		// Then one display step. The viewport encodes linear to sRGB on the way out, so a data
		// value written raw would show brighter than itself (0.5 roughness as 188, not 128). Data
		// channels therefore go through the inverse first -- the same exact curve the debug palette
		// uses (MixtormatDebugColor.ush) -- so every channel shows its value as the grey it is.
		// Base Color is a colour, not data: it is already linear and is left alone. The linear
		// branch is taken for anything at or below the knee, negatives included, so an unclamped
		// height never reaches pow with a negative base.
		UMaterialExpressionCustom* Display = NewObject<UMaterialExpressionCustom>(Material);
		Display->Description = TEXT("MixtormatChannelDisplay");
		Display->OutputType = CMOT_Float3;
		Display->Code = FString::Printf(TEXT(
			"float3 C = Value;\n"
			"if (abs(Mode - %d.0f) < 0.5f) { return C; }\n"
			"const float3 Low = C / 12.92f;\n"
			"const float3 High = pow((abs(C) + 0.055f) / 1.055f, 2.4f);\n"
			"return float3(\n"
			"\tC.r <= 0.04045f ? Low.r : High.r,\n"
			"\tC.g <= 0.04045f ? Low.g : High.g,\n"
			"\tC.b <= 0.04045f ? Low.b : High.b);"),
			static_cast<int32>(EMixtormatChannelPreview::BaseColor));
		Display->Inputs.Reset();
		FCustomInput& ValueInput = Display->Inputs.AddDefaulted_GetRef();
		ValueInput.InputName = TEXT("Value");
		ValueInput.Input.Connect(0, Chain[LastMode]);
		FCustomInput& ModeInput = Display->Inputs.AddDefaulted_GetRef();
		ModeInput.InputName = TEXT("Mode");
		ModeInput.Input.Connect(0, ModeParam);
		Material->GetExpressionCollection().AddExpression(Display);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Display);
		AddHeightDisplacement(Material, HeightSample);
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
		return FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::OverlayGround);
	}

	// A debug or channel view wants a literal read of the composited texture -- the studio look's
	// filmic tone curve compresses highlights and lifts blacks, its exposure bias darkens, and its
	// vignette and blue correction shift values by screen position and hue, all of which turn a
	// linear roughness/height/normal/mask value into visibly wrong numbers. Zeroing
	// ToneCurveAmount and ExpandGamut fully disables the curve (see
	// FPostProcessSettings::ToneCurveAmount) while leaving the ordinary linear-to-sRGB display
	// encode in place. Exposure goes manual with no physical camera and no bias, so a value is
	// shown as itself. Left alone in Material mode, so the shaded look is untouched.
	virtual void OverridePostProcessSettings(FSceneView& View) override
	{
		FPostProcessSettings Settings;
		// Force-disable inherited project/editor motion blur even if a profile or view extension
		// injects post settings after the viewport's MotionBlur show flag was cleared.
		Settings.bOverride_MotionBlurAmount = true;
		Settings.MotionBlurAmount = 0.0f;
		Settings.bOverride_MotionBlurMax = true;
		Settings.MotionBlurMax = 0.0f;

		if (Owner.CurrentPreviewQuality == EMixtormatPreviewQuality::Lumen)
		{
			Settings.bOverride_LumenFinalGatherQuality = true;
			Settings.LumenFinalGatherQuality = 0.5f;
		}
		if (Owner.IsUnlitPresentation())
		{
			Settings.bOverride_ToneCurveAmount = true;
			Settings.ToneCurveAmount = 0.0f;
			Settings.bOverride_ExpandGamut = true;
			Settings.ExpandGamut = 0.0f;
			Settings.bOverride_BlueCorrection = true;
			Settings.BlueCorrection = 0.0f;
			Settings.bOverride_VignetteIntensity = true;
			Settings.VignetteIntensity = 0.0f;
			Settings.bOverride_FilmGrainIntensity = true;
			Settings.FilmGrainIntensity = 0.0f;
			Settings.bOverride_BloomIntensity = true;
			Settings.BloomIntensity = 0.0f;
			Settings.bOverride_AutoExposureMethod = true;
			Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			Settings.AutoExposureApplyPhysicalCameraExposure = 0;
			Settings.bOverride_AutoExposureBias = true;
			Settings.AutoExposureBias = 0.0f;
		}
		View.OverridePostProcessSettings(Settings, 1.0f);
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
		// Bare Z only: Ctrl+Z is undo and must still reach the editor.
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::Z
			&& !IsCtrlPressed() && !IsAltPressed() && !IsShiftPressed())
		{
			Owner.ToggleDisplacement();
			return true;
		}
		// Temporary: cycles the raw-channel diagnostic view. No toolbar yet -- WorkingStatusText
		// is the only indication, same as every other viewport hotkey here.
		if (EventArgs.Event == IE_Pressed
			&& (EventArgs.Key == EKeys::U || EventArgs.Key == EKeys::M)
			&& !IsCtrlPressed() && !IsAltPressed() && !IsShiftPressed())
		{
			Owner.CycleModulePreview();
			return true;
		}
		if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::V)
		{
			// Shift+V jumps straight back to Material instead of cycling through every channel.
			if (IsShiftPressed())
			{
				Owner.ResetChannelPreview();
			}
			else
			{
				Owner.CycleChannelPreview();
			}
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
				// Negated: the light follows the drag instead of moving against it.
				Owner.RotateLighting(-Args.AmountDepressed, 0.0f);
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
	if (StudioReflectionCapture)
	{
		PreviewScene.RemoveComponent(StudioReflectionCapture);
	}
}

void SMixtormatPreviewViewport::Construct(const FArguments& InArgs)
{
	OnToggleOverlayUi = InArgs._OnToggleOverlayUi;
	OnToggleDisplacement = InArgs._OnToggleDisplacement;
	OnChannelPreviewChanged = InArgs._OnChannelPreviewChanged;
	OnCycleModulePreview = InArgs._OnCycleModulePreview;

	PreviewMeshComponent = NewObject<UStaticMeshComponent>();
	PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(PreviewMeshComponent, FTransform::Identity);

	// Added before the lighting preset runs, because SetStudioLighting is what hands it a cubemap.
	StudioReflectionCapture = NewObject<UBoxReflectionCaptureComponent>();
	StudioReflectionCapture->ReflectionSourceType = EReflectionSourceType::SpecifiedCubemap;
	StudioReflectionCapture->Brightness =
		MixtormatPreviewSceneSettings::ReflectionCaptureBrightness;
	PreviewScene.AddComponent(StudioReflectionCapture, FTransform::Identity);

	PreviewScene.SetFloorVisibility(true);
	PreviewScene.SetEnvironmentVisibility(false);
	SetStudioLighting(EMixtormatStudioLighting::Neutral);

	SEditorViewport::Construct(SEditorViewport::FArguments());
	SetPreviewMesh(EMixtormatPreviewMesh::Sphere, EMixtormatPlaneOrientation::Horizontal);
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

	ApplyPresentationState();
}

namespace
{
	// Floor on the gap between two drag-time composites, and the headroom over the measured
	// compose time. A cheap stack is held to the floor; a heavy one (erosion iterations, stain,
	// flow solve) backs off to a little longer than one compose takes, so the game thread never
	// queues behind the GPU.
	constexpr double MinDragComposeInterval = 0.05;
	constexpr double DragComposeHeadroom = 1.25;
	// Weight of the newest sample in the smoothed compose time.
	constexpr double ComposeTimeSmoothing = 0.3;
}

bool SMixtormatPreviewViewport::CanSubmitCompose(const bool bInteractive) const
{
	if (LayerCompositor.IsValid() && LayerCompositor->IsComposeInFlight())
	{
		return false;
	}
	if (!bInteractive)
	{
		return true;
	}
	const double Interval = FMath::Max(MinDragComposeInterval, SmoothedComposeSeconds * DragComposeHeadroom);
	return FPlatformTime::Seconds() - LastComposeSubmitTime >= Interval;
}

void SMixtormatPreviewViewport::SubmitCompose(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	const int32 Resolution,
	const FMixtormatDebugPreviewSettings& DebugSettings)
{
	LastComposeSubmitTime = FPlatformTime::Seconds();
	if (ComposeLayersWithDebug(Layers, Groups, Resolution, DebugSettings, false))
	{
		bMeasuringCompose = true;
		EnsureComposeTimer();
	}
}

void SMixtormatPreviewViewport::EnsureComposeTimer()
{
	if (!PendingComposeTimer.IsValid())
	{
		PendingComposeTimer = RegisterActiveTimer(0.0f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SMixtormatPreviewViewport::FlushPendingCompose));
	}
}

void SMixtormatPreviewViewport::SetPreviewLayers(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	const int32 Resolution,
	FMixtormatDebugPreviewSettings DebugSettings,
	const bool bInteractive)
{
	bDebugPreviewMode = DebugSettings.Mode;
	bDebugLayerIndex = DebugSettings.LayerIndex;
	bDebugChildIndex = DebugSettings.ChildIndex;

	// A slider drag asks for a composite as fast as the hand moves. While the last one is still
	// on the render thread, or (mid-drag) the interval sized to its cost has not passed, keep
	// only this newest request and submit it when the timer allows, instead of queueing frames
	// the user has already scrubbed past. A non-drag request replaces a waiting drag one, so the
	// final value of a scrub is never held back by the interval.
	if (!CanSubmitCompose(bInteractive))
	{
		PendingCompose = FPendingCompose{Layers, Groups, Resolution, DebugSettings, bInteractive};
		EnsureComposeTimer();
		return;
	}
	PendingCompose.Reset();
	SubmitCompose(Layers, Groups, Resolution, DebugSettings);
}

EActiveTimerReturnType SMixtormatPreviewViewport::FlushPendingCompose(
	const double CurrentTime, const float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	const bool bInFlight = LayerCompositor.IsValid() && LayerCompositor->IsComposeInFlight();
	if (bMeasuringCompose && !bInFlight)
	{
		const double Elapsed = FPlatformTime::Seconds() - LastComposeSubmitTime;
		SmoothedComposeSeconds = SmoothedComposeSeconds > 0.0
			? FMath::Lerp(SmoothedComposeSeconds, Elapsed, ComposeTimeSmoothing)
			: Elapsed;
		bMeasuringCompose = false;
	}
	if (!PendingCompose.IsSet())
	{
		if (bMeasuringCompose)
		{
			return EActiveTimerReturnType::Continue;
		}
		PendingComposeTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	if (!CanSubmitCompose(PendingCompose->bInteractive))
	{
		return EActiveTimerReturnType::Continue;
	}
	FPendingCompose Pending = MoveTemp(PendingCompose.GetValue());
	PendingCompose.Reset();
	SubmitCompose(Pending.Layers, Pending.Groups, Pending.Resolution, Pending.DebugSettings);
	return EActiveTimerReturnType::Continue;
}

bool SMixtormatPreviewViewport::ComposeLayersAtResolution(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	const int32 Resolution)
{
	FMixtormatDebugPreviewSettings DebugSettings;
	DebugSettings.Mode = bDebugPreviewMode;
	DebugSettings.LayerIndex = bDebugLayerIndex;
	DebugSettings.ChildIndex = bDebugChildIndex;
	// A newer request would overwrite what the caller is about to read back.
	PendingCompose.Reset();
	// The blocking wait would read as the cost of one preview compose and throttle the next drag.
	bMeasuringCompose = false;
	return ComposeLayersWithDebug(Layers, Groups, Resolution, DebugSettings, true);
}

bool SMixtormatPreviewViewport::ComposeLayersWithDebug(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups,
	const int32 Resolution,
	FMixtormatDebugPreviewSettings DebugSettings,
	const bool bWaitForCompletion)
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
		ApplyPresentationState();
	}

	if (!PreviewMaterialInstance.IsValid())
	{
		return false;
	}

	LayerCompositor->SetFinalSettings(
		FinalSettings.HeightAOAmount,
		FinalSettings.HeightAORadius,
		true,
		1.0f,
		FinalSettings.bAutoRemapHeight);
	if (!LayerCompositor->RequestCompose(
		Layers,
		Groups,
		FSimpleDelegate(),
		DebugSettings,
		bGlobalUVRotation90))
	{
		return false;
	}
	if (bWaitForCompletion)
	{
		FlushRenderingCommands();
	}
	if (bDebugPreview)
	{
		PreviewMaterialInstance->SetTextureParameterValue(
			MixtormatPreview::DebugTextureParameter,
			LayerCompositor->GetDebugOutput());
		PreviewMaterialInstance->SetTextureParameterValue(
			MixtormatPreview::ChannelPreviewHeightParameter,
			LayerCompositor->GetHeightOutput());
	}
	else
	{
		// Rebind the complete layer preview so removed overrides inherit the master again.
		PreviewMaterialInstance->ClearParameterValues();
		LayerCompositor->BindOutputs(*PreviewMaterialInstance.Get());
		const TOptional<FLinearColor> FuzzColor =
			MixtormatCompositionReferences::ComputeFuzzColor(Layers, Groups);
		if (FuzzColor.IsSet())
		{
			PreviewMaterialInstance->SetVectorParameterValue(TEXT("DA_FuzzColor"), FuzzColor.GetValue());
		}
		CompositedFuzzInfluence =
			MixtormatCompositionReferences::ComputeFuzzInfluence(Layers, Groups);
		PreviewMaterialInstance->SetScalarParameterValue(
			MixtormatPreview::FuzzInfluenceParameter,
			CompositedFuzzInfluence);
	}
	PreviewMaterialInstance->SetScalarParameterValue(
		MixtormatPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
	PreviewMaterialInstance->SetScalarParameterValue(
		MixtormatPreview::HeightAmountParameter,
		DisplacementAmount);
	InvalidateDisplacementShadows();
	// PreviewMaterialInstance above was just refreshed regardless of what the mesh currently
	// shows. If a diagnostic channel is active, reapply it too, both to pick up the new textures
	// and because the swap branch above may have just rebound the mesh to PreviewMaterialInstance
	// out from under it.
	if (ChannelPreview != EMixtormatChannelPreview::Material)
	{
		ApplyChannelPreview();
	}
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

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetCompositedDebug() const
{
	return LayerCompositor ? LayerCompositor->GetDebugOutput() : nullptr;
}

UTextureRenderTarget2D* SMixtormatPreviewViewport::GetRegionIdPick() const
{
	return LayerCompositor ? LayerCompositor->GetRegionIdPickOutput() : nullptr;
}

void SMixtormatPreviewViewport::SetPreviewScalarParameter(
	const FName ParameterName,
	const float Value)
{
	// The channel material displaces too, so it follows the same scalars while it is up.
	if (UMaterialInstanceDynamic* ChannelMID = ChannelPreviewMaterialInstance.Get())
	{
		ChannelMID->SetScalarParameterValue(ParameterName, Value);
	}
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
	UpdatePreviewMeshFloorClearance();
	UpdateStudioFloor();
	UpdateCamera();
}

void SMixtormatPreviewViewport::SetPreviewDisplacementAmount(const float Amount)
{
	DisplacementAmount = FMath::Clamp(Amount, 0.0f, 4.0f);
	SetPreviewScalarParameter(
		MixtormatPreview::HeightAmountParameter,
		DisplacementAmount);
	UpdatePreviewMeshFloorClearance();
	UpdateStudioFloor();
	UpdateCamera();
}

void SMixtormatPreviewViewport::SetGlobalUVRotation90(const bool bEnabled)
{
	bGlobalUVRotation90 = bEnabled;
}

void SMixtormatPreviewViewport::UpdatePreviewMeshFloorClearance()
{
	if (!PreviewMeshComponent || !PreviewMeshComponent->GetStaticMesh())
	{
		return;
	}

	PreviewMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	PreviewMeshComponent->UpdateBounds();
	const float PlaneDisplacementClearance = bDisplacementEnabled ? DisplacementAmount : 0.0f;
	// Horizontal Plane keeps its generous clearance so displacement has room. The vertical Plane
	// is meant to stand on the ground, so its post-rotation bounds are placed with Min.Z at 0 and
	// no extra clearance is added.
	const bool bVerticalPlane = CurrentPreviewMesh == EMixtormatPreviewMesh::Plane
		&& CurrentPlaneOrientation == EMixtormatPlaneOrientation::VerticalX;
	const float FloorClearance = bVerticalPlane
		? 0.0f
		: CurrentPreviewMesh == EMixtormatPreviewMesh::Plane
			? 2.0f + PlaneDisplacementClearance
			: 0.5f;
	const float HeightAboveFloor = -PreviewMeshComponent->Bounds.GetBox().Min.Z + FloorClearance;
	PreviewMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, HeightAboveFloor));
	PreviewMeshComponent->UpdateBounds();
	PreviewTarget = PreviewMeshComponent->Bounds.Origin;
}

void SMixtormatPreviewViewport::SetPreviewMesh(
	const EMixtormatPreviewMesh MeshType,
	const EMixtormatPlaneOrientation PlaneOrientation)
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	FString PluginMeshPath = FMixtormatPaths::SphereMeshObjectPath();
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere");
	FRotator MeshRotation = FRotator::ZeroRotator;
	// No engine fallback: the plugin cylinder is a specific authored asset, and standing in
	// /Engine/BasicShapes/Cylinder.Cylinder on a missing load would look plausible while being
	// silently wrong (different proportions/pivot), rather than surfacing the real problem.
	bool bAllowEngineFallback = true;

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
	case EMixtormatPreviewMesh::Cylinder:
		PluginMeshPath = FMixtormatPaths::CylinderMeshObjectPath();
		bAllowEngineFallback = false;
		break;
	case EMixtormatPreviewMesh::Sphere:
	default:
		break;
	}

	UStaticMesh* PreviewMesh = LoadObject<UStaticMesh>(nullptr, *PluginMeshPath);
	if (!PreviewMesh)
	{
		if (!bAllowEngineFallback)
		{
			UE_LOG(LogMixtormat, Error,
				TEXT("Required preview mesh %s could not be loaded. Preview mesh left unchanged."),
				*PluginMeshPath);
			return;
		}
		PreviewMesh = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath);
		if (MeshType == EMixtormatPreviewMesh::Plane)
		{
			MeshRotation = FRotator(90.0f, 0.0f, 0.0f);
		}
	}
	PreviewMeshComponent->SetStaticMesh(PreviewMesh);
	if (MeshType == EMixtormatPreviewMesh::Plane
		&& PlaneOrientation == EMixtormatPlaneOrientation::VerticalX)
	{
		// Stand the flat plane upright so its normal (the mesh's local +Z) faces world +X. A
		// positive pitch points local +Z at -X, so the quarter turn is negative. It is applied on
		// top of whatever rotation this mesh needed to lie flat, so the authored plugin asset
		// (flat at zero) and the engine fallback each reach +X from their own base rather than from
		// an assumed shared orientation.
		MeshRotation.Pitch -= 90.0f;
	}
	PreviewMeshComponent->SetRelativeRotation(MeshRotation);
	CurrentPreviewMesh = MeshType;
	CurrentPlaneOrientation = PlaneOrientation;
	UpdatePreviewMeshFloorClearance();

	UpdateStudioFloor();
	UpdateCamera();
}

// The floor runs to a fixed extent whatever the object's size, so something has to hide its
// edge. That used to be height fog, which cost the reflections their sky. Now the floor is scaled
// to the object and its material fades itself to the background colour in its own object space,
// so the edge is hidden by the floor rather than by something drawn in front of it.
void SMixtormatPreviewViewport::UpdateStudioFloor()
{
	if (!PreviewMeshComponent || !PreviewMeshComponent->GetStaticMesh())
	{
		return;
	}
	const UStaticMeshComponent* Floor = PreviewScene.GetFloorMeshComponent();
	if (!Floor || !Floor->GetStaticMesh())
	{
		return;
	}

	// The floor mesh's own unscaled half-size, so this does not depend on which plane the
	// preview scene profile happens to supply.
	const FVector FloorExtent = Floor->GetStaticMesh()->GetBounds().BoxExtent;
	const float FloorHalfSize = FMath::Max(FMath::Max(FloorExtent.X, FloorExtent.Y), 1.0f);

	const float MeshRadius = FMath::Max(PreviewMeshComponent->Bounds.SphereRadius, 0.5f);
	const float TargetRadius = MeshRadius * MixtormatPreviewSceneSettings::FloorRadiusInMeshRadii;
	const float Scale = TargetRadius / FloorHalfSize;
	PreviewScene.SetFloorMeshScale(FVector(Scale, Scale, 1.0f));

	if (!StudioFloorMaterial.IsValid())
	{
		UMaterialInterface* FloorMaster = LoadObject<UMaterialInterface>(
			nullptr,
			*FMixtormatPaths::StudioFloorMaterialObjectPath());
		if (FloorMaster)
		{
			StudioFloorMaterial.Reset(
				UMaterialInstanceDynamic::Create(FloorMaster, GetTransientPackage()));
		}
	}
	if (!StudioFloorMaterial.IsValid())
	{
		return;
	}
	// The material reads distance in world units, so the radii are pushed rather than baked --
	// the floor mesh is scaled above, but scaling a plane does not move where its own fade sits.
	StudioFloorMaterial->SetScalarParameterValue(
		MixtormatPreview::FloorFadeInParameter,
		MeshRadius * MixtormatPreviewSceneSettings::FloorFadeInInMeshRadii);
	StudioFloorMaterial->SetScalarParameterValue(
		MixtormatPreview::FloorFadeOutParameter,
		MeshRadius * MixtormatPreviewSceneSettings::FloorFadeOutInMeshRadii);
	// Re-handed every update, because UpdateScene puts the profile's own material back.
	PreviewScene.SetFloorMaterial(StudioFloorMaterial.Get());
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
	// UpdateScene resets the floor back to the profile's own scale, so this has to follow every
	// call rather than being set once.
	UpdateStudioFloor();

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
	if (StudioReflectionCapture)
	{
		// Same asset the skylight reads, so the traced and the captured reflections agree rather
		// than disagreeing wherever Lumen hands over to the reflection environment.
		StudioReflectionCapture->Cubemap = StudioEnvironmentCubemap.Get();
		StudioReflectionCapture->MarkDirtyForRecaptureOrUpload();
		StudioReflectionCapture->MarkRenderStateDirty();
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
	ApplyPresentationState();
}

void SMixtormatPreviewViewport::ApplyLightIntensities()
{
	PreviewScene.SetLightBrightness(BaseLightBrightness * LightIntensityScale);
	PreviewScene.SetSkyBrightness(
		BaseSkyBrightness
		* SkylightIntensityScale
		* MixtormatPreviewSceneSettings::CubemapReflectionBoost);
	if (StudioReflectionCapture)
	{
		StudioReflectionCapture->Brightness =
			MixtormatPreviewSceneSettings::ReflectionCaptureBrightness
			* SkylightIntensityScale;
		StudioReflectionCapture->MarkRenderStateDirty();
	}
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

bool SMixtormatPreviewViewport::IsUnlitPresentation() const
{
	return bUsingDebugPreview || ChannelPreview != EMixtormatChannelPreview::Material;
}

void SMixtormatPreviewViewport::ApplyPresentationState()
{
	const bool bUnlit = IsUnlitPresentation();
	if (PreviewScene.DirectionalLight)
	{
		PreviewScene.DirectionalLight->SetVisibility(!bUnlit);
	}
	if (PreviewScene.SkyLight)
	{
		PreviewScene.SkyLight->SetVisibility(!bUnlit);
	}
	if (StudioReflectionCapture)
	{
		StudioReflectionCapture->SetVisibility(!bUnlit);
	}
	// Direct, so the shared asset-viewer profile is not rewritten. The floor is not data, and an
	// emissive mesh would light it under Lumen, so it goes with the lights.
	PreviewScene.SetFloorVisibility(!bUnlit, true);
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}
	MixtormatPreviewSceneSettings::ConfigureQuality(
		PreviewViewportClient->EngineShowFlags,
		CurrentPreviewQuality);
	if (bUnlit)
	{
		MixtormatPreviewSceneSettings::ConfigureUnlit(PreviewViewportClient->EngineShowFlags);
	}
	PreviewViewportClient->Invalidate();
}

void SMixtormatPreviewViewport::SetPreviewQuality(const EMixtormatPreviewQuality Quality)
{
	CurrentPreviewQuality = Quality;
	ApplyPresentationState();
}

void SMixtormatPreviewViewport::SetPreviewAntiAliasing(const EMixtormatPreviewAntiAliasing AntiAliasing)
{
	if (!PreviewViewportClient.IsValid())
	{
		return;
	}

	// AntiAliasing is the master switch. With it enabled, TemporalAA selects TSR; clearing only
	// TemporalAA deliberately falls back to FXAA. Off therefore has to clear both flags.
	PreviewViewportClient->EngineShowFlags.SetAntiAliasing(
		AntiAliasing != EMixtormatPreviewAntiAliasing::Off);
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
	LastLightRotateTime = FPlatformTime::Seconds();
	LightingYaw = FMath::Fmod(LightingYaw + YawDelta * 0.35f + 360.0f, 360.0f);
	LightingPitch = FMath::Clamp(LightingPitch - PitchDelta * 0.25f, -89.0f, -1.0f);
	PreviewScene.SetLightDirection(FRotator(LightingPitch, LightingYaw, 0.0f));

	if (bUsingStudioEnvironment && StudioPreviewProfile && !FMath::IsNearlyZero(YawDelta))
	{
		EnvironmentYaw = FMath::Fmod(EnvironmentYaw - YawDelta * 0.22f + 360.0f, 360.0f);
		StudioPreviewProfile->LightingRigRotation = EnvironmentYaw;
		PreviewScene.UpdateScene(*StudioPreviewProfile, true, true, false, false);
		PreviewScene.SetEnvironmentVisibility(false, true);
		UpdateStudioFloor();
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
	UpdateStudioFloor();
	UpdateCamera();
}

void SMixtormatPreviewViewport::ToggleOverlayUi()
{
	OnToggleOverlayUi.ExecuteIfBound();
}

void SMixtormatPreviewViewport::ToggleDisplacement()
{
	// The owner holds the displacement state (its checkbox and the library preview read it), so
	// the hotkey asks it rather than flipping this viewport alone.
	OnToggleDisplacement.ExecuteIfBound();
}

void SMixtormatPreviewViewport::ResetChannelPreview()
{
	ChannelPreview = EMixtormatChannelPreview::Material;
	ApplyChannelPreview();
	OnChannelPreviewChanged.ExecuteIfBound();
}

void SMixtormatPreviewViewport::CycleModulePreview()
{
	OnCycleModulePreview.ExecuteIfBound();
}

void SMixtormatPreviewViewport::CycleChannelPreview()
{
	const uint8 NextMode = (static_cast<uint8>(ChannelPreview) + 1)
		% (static_cast<uint8>(EMixtormatChannelPreview::Fuzz) + 1);
	ChannelPreview = static_cast<EMixtormatChannelPreview>(NextMode);
	ApplyChannelPreview();
	OnChannelPreviewChanged.ExecuteIfBound();
}

void SMixtormatPreviewViewport::ApplyChannelPreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	if (ChannelPreview == EMixtormatChannelPreview::Material)
	{
		// Restore whatever the ordinary layer/debug pipeline last bound. That pipeline was never
		// touched while a diagnostic mode was active -- PreviewMaterialInstance kept receiving
		// every recompose in the background -- so this is an exact, up-to-date restore, not a
		// rebuild.
		PreviewMeshComponent->SetMaterial(0, PreviewMaterialInstance.Get());
		// Back in the material slot, so the strong hold is no longer needed.
		RetainedPreviewMaterialInstance.Reset();
		ApplyPresentationState();
		return;
	}

	// About to detach PreviewMaterialInstance from the mesh's material slot -- its only other
	// owner -- so it needs a strong reference of its own or it becomes GC-eligible the moment
	// this swap happens, leaving Material mode with nothing to restore later.
	RetainedPreviewMaterialInstance.Reset(PreviewMaterialInstance.Get());

	if (!ChannelPreviewMaterial)
	{
		ChannelPreviewMaterial.Reset(MixtormatPreview::CreateChannelPreviewMaterial());
	}
	if (!ChannelPreviewMaterial)
	{
		return;
	}
	if (!ChannelPreviewMaterialInstance.IsValid())
	{
		ChannelPreviewMaterialInstance = UMaterialInstanceDynamic::Create(
			ChannelPreviewMaterial.Get(), PreviewMeshComponent);
	}
	UMaterialInstanceDynamic* ChannelMID = ChannelPreviewMaterialInstance.Get();
	if (!ChannelMID)
	{
		return;
	}

	if (LayerCompositor)
	{
		// Same call the real preview material's textures come from -- one texture-fetch, two
		// consumers, so the diagnostic view can never drift from what BindOutputs considers
		// current.
		LayerCompositor->BindOutputs(*ChannelMID);
	}
	ChannelMID->SetScalarParameterValue(
		MixtormatPreview::FuzzInfluenceParameter,
		CompositedFuzzInfluence);
	ChannelMID->SetScalarParameterValue(
		MixtormatPreview::ChannelPreviewModeParameter,
		static_cast<float>(ChannelPreview));
	ChannelMID->SetScalarParameterValue(
		MixtormatPreview::UseHeightParameter,
		bDisplacementEnabled ? 1.0f : 0.0f);
	ChannelMID->SetScalarParameterValue(
		MixtormatPreview::HeightAmountParameter,
		DisplacementAmount);
	PreviewMeshComponent->SetMaterial(0, ChannelMID);
	ApplyPresentationState();
}

FText SMixtormatPreviewViewport::GetPreviewModeLabel() const
{
	// A channel view replaces the mesh's material outright, so it wins over a debug view that may
	// still be composing underneath it.
	if (ChannelPreview != EMixtormatChannelPreview::Material)
	{
		return FText::Format(
			NSLOCTEXT("SMixtormatPreviewViewport", "ChannelPreviewModeLabel", "{0}  (Shift+V for Material)"),
			FText::FromString(GetChannelPreviewLabel()));
	}
	if (!bUsingDebugPreview)
	{
		return NSLOCTEXT("SMixtormatPreviewViewport", "MaterialPreviewModeLabel", "Material");
	}
	switch (bDebugPreviewMode)
	{
	case EMixtormatDebugPreviewMode::GeneratedFeature:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugFeatureLabel", "Debug: Feature");
	case EMixtormatDebugPreviewMode::HeightBlend:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugHeightBlendLabel", "Debug: Height Blend");
	case EMixtormatDebugPreviewMode::ContactAO:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugContactAOLabel", "Debug: Contact AO");
	case EMixtormatDebugPreviewMode::BorderNormal:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugBorderNormalLabel", "Debug: Border Normal");
	case EMixtormatDebugPreviewMode::LayerMask:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugLayerMaskLabel", "Debug: Layer Mask");
	case EMixtormatDebugPreviewMode::Stain:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugStainLabel", "Debug: Stain");
	case EMixtormatDebugPreviewMode::Runoff:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugRunoffLabel", "Debug: Runoff");
	case EMixtormatDebugPreviewMode::ChildOutput:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugChildOutputLabel", "Debug: Child Output");
	case EMixtormatDebugPreviewMode::LayerUV:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugLayerUVLabel", "Debug: Layer UV");
	case EMixtormatDebugPreviewMode::None:
	default:
		return NSLOCTEXT("SMixtormatPreviewViewport", "DebugPreviewLabel", "Debug");
	}
}

FString SMixtormatPreviewViewport::GetChannelPreviewLabel() const
{
	switch (ChannelPreview)
	{
	case EMixtormatChannelPreview::BaseColor: return TEXT("Base Color");
	case EMixtormatChannelPreview::Normal: return TEXT("Normal");
	case EMixtormatChannelPreview::Roughness: return TEXT("Roughness");
	case EMixtormatChannelPreview::AO: return TEXT("AO");
	case EMixtormatChannelPreview::Metallic: return TEXT("Metallic");
	case EMixtormatChannelPreview::F0: return TEXT("F0 / Specular");
	case EMixtormatChannelPreview::Height: return TEXT("Height");
	case EMixtormatChannelPreview::Fuzz: return TEXT("Fuzz Influence");
	case EMixtormatChannelPreview::Material:
	default:
		return TEXT("Material");
	}
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
		GetCachedGeometry().GetLocalSize(),
		MixtormatPreviewCamera::ViewportFocusMargin);

	CameraDistance = FMath::Clamp(
		FitDistance,
		MixtormatPreviewCamera::DistanceMinimum,
		MixtormatPreviewCamera::DistanceMaximum);

	// The fog trails the camera, and it is keyed off distance.
	UpdateStudioFloor();
	UpdateCamera();
}

FQuat SMixtormatPreviewViewport::GetCameraRotation() const
{
	return FRotator(CameraPitch, CameraYaw, 0.0f).Quaternion();
}

FVector SMixtormatPreviewViewport::GetLightDirection() const
{
	return FRotator(LightingPitch, LightingYaw, 0.0f).Vector();
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
	SetPreviewQuality(EMixtormatPreviewQuality::Default);
	PreviewScene.SetLightDirection(FRotator(-35.0f, LightingYaw, 0.0f));
	UpdateCamera();
	return PreviewViewportClient.ToSharedRef();
}

