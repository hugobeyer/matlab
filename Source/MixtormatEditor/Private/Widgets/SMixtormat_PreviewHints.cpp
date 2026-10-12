// Copyright 2026 Hugo Beyer. All Rights Reserved.

// The viewport's contextual hint strip: one compact line of key/action pairs.
// Its screen corner is configured by ContextLayout (typically top-right).
//
// Pairs reflect live actions, including W when a supported mask is selected,
// V when a diagnostic is active, Esc in quick controls, and the Plane flip.
// Visibility lambdas track selection and overlay changes without rebuilding.
//
// Hit-test-invisible like the light gizmo, so camera navigation passes through. The
// strip follows the H/Space master flag and collapses when nothing applies.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "Style/MixtormatRecipes.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace Mixtormat
{
	// The locator's target: a named compound widget so the eye can outline the strip
	// precisely instead of falling back to the whole preview.
	class SMixtormatHintStrip final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatHintStrip) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}
	};

	// The keycap plate: the preview plate recipe at rest, so the strip's keycaps are the
	// same surface the overlay buttons use rather than a second plate look.
	TSharedRef<SWidget> MakeHintKeycap(const FText& Key, const TAttribute<EVisibility>& Visible)
	{
		const FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const FTextBlockStyle KeycapStyle = FMixtormatTypography::MakeTextStyle(
			FMixtormatTypography::GetSpec(Resolved.Typography, EMixtormatTextRole::MenuShortcut),
			Resolved.Palette.Get(EMixtormatColorRole::Text));
		return SNew(SMixtormatSurfaceBox)
			.Visibility(Visible)
			.Recipe_Lambda([]()
			{
				return MakePreviewPlateRecipe(FMixtormatThemeStore::GetResolved());
			})
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ContextLayout.HintKeycapPadding))
			[
				SNew(STextBlock)
				.Text(Key)
				.TextStyle(&KeycapStyle)
			];
	}
}

TSharedRef<SWidget> SMixtormat::BuildPreviewHintStrip()
{
	const FTextBlockStyle ActionStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			FMixtormatThemeStore::GetResolved().Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
		FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	// Read live selection/preview state; do not advertise unavailable hotkeys.
	const auto bQuickControls = [this]() { return bQuickControlsOpen; };
	const auto bChannelView = [this]()
	{
		return !PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
			&& PreviewViewports[0]->GetChannelPreview() != EMixtormatChannelPreview::Material;
	};
	const auto bMaskAvailable = [this]()
	{
		return bHasWorkingMaterial && IsMaskOverlayTargetReady();
	};
	const auto bDiagnosticActive = [this, bChannelView]()
	{
		return bChannelView()
			|| DebugPreviewMode != EMixtormatDebugPreviewMode::None
			|| bMaskOverlayEnabled;
	};
	const auto bPlaneFlip = [this]()
	{
		return PreviewMesh == EMixtormatPreviewMesh::Plane;
	};
	const auto bNoMaterial = [this]() { return !bHasWorkingMaterial; };
	const auto bUvRotationAvailable = [this]() { return bHasWorkingMaterial; };
	const auto bAnyContext = [bQuickControls, bDiagnosticActive, bMaskAvailable, bPlaneFlip, bNoMaterial, bUvRotationAvailable]()
	{
		return bQuickControls() || bDiagnosticActive() || bMaskAvailable()
			|| bPlaneFlip() || bNoMaterial() || bUvRotationAvailable();
	};

	// One pair: optional keycap and action text, both with live visibility.
	// Action is an attribute so a mask toggle can say Show or Hide.
	const auto AddPair = [&ActionStyle](
		SHorizontalBox& Box,
		const FText& Key,
		const TAttribute<FText>& Action,
		const TAttribute<EVisibility>& Visible)
	{
		if (!Key.IsEmpty())
		{
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center)
				[
					Mixtormat::MakeHintKeycap(Key, Visible)
				];
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center)
				.Padding(FMixtormatThemeStore::GetResolved().ContextLayout.HintKeyActionGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Visibility(Visible)
					.Text(Action)
					.TextStyle(&ActionStyle)
				];
		}
		else
		{
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Visibility(Visible)
					.Text(Action)
					.TextStyle(&ActionStyle)
				];
		}
	};

	// Gap before every pair after the first, so a collapsed pair takes its gap with it.
	const auto AddGap = [](SHorizontalBox& Box, const TAttribute<EVisibility>& Visible)
	{
		Box.AddSlot().AutoWidth().VAlign(VAlign_Center)
			.Padding(FMixtormatThemeStore::GetResolved().ContextLayout.HintItemGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSpacer)
				.Visibility(Visible)
				.Size(FVector2D(2.0f, 1.0f))
			];
	};

	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// -- Quick controls open: the only pair on screen.
	const TAttribute<EVisibility> QuickControlsVis = TAttribute<EVisibility>::CreateLambda([bQuickControls]()
	{
		return bQuickControls() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddPair(*Row, LOCTEXT("HintEsc", "Esc"), LOCTEXT("HintCloseQuickControls", "Close quick controls"), QuickControlsVis);

	// -- 5 shares the marking-menu's document-wide 90-degree UV rotation toggle.
	const TAttribute<EVisibility> UvRotationVis = TAttribute<EVisibility>::CreateLambda(
		[bUvRotationAvailable, bQuickControls]()
	{
		return bUvRotationAvailable() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, UvRotationVis);
	AddPair(*Row, LOCTEXT("HintUVRotateKey", "5"),
		TAttribute<FText>::CreateLambda([this]()
		{
			return bGlobalUVRotation90
				? LOCTEXT("HintUVRotateOff", "Reset UV rotation")
				: LOCTEXT("HintUVRotateOn", "Rotate UVs 90°");
		}), UvRotationVis);

	// -- A supported selected mask can be toggled whether already visible or not.
	const TAttribute<EVisibility> MaskVis = TAttribute<EVisibility>::CreateLambda(
		[bMaskAvailable, bQuickControls]()
	{
		return bMaskAvailable() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, MaskVis);
	AddPair(*Row, LOCTEXT("HintW", "W"), TAttribute<FText>::CreateLambda([this]()
	{
		const FMixtormatChildAddress Selected = GetSelectedChildAddress();
		const bool bSameSource = bMaskOverlayEnabled
			&& Selected.OwnerType == MaskOverlaySource.OwnerType
			&& Selected.OwnerId == MaskOverlaySource.OwnerId
			&& Selected.ChildId == MaskOverlaySource.ChildId;
		return bSameSource
			? LOCTEXT("HintHideDiagnostic", "Hide diagnostic")
			: bMaskOverlayEnabled
				? LOCTEXT("HintTransferDiagnostic", "Preview selected source")
				: LOCTEXT("HintShowDiagnostic", "Show diagnostic");
	}), MaskVis);

	// -- V resets both the new mask tint and any remaining inspector diagnostics.
	const TAttribute<EVisibility> ResetVis = TAttribute<EVisibility>::CreateLambda(
		[bDiagnosticActive, bQuickControls]()
	{
		return bDiagnosticActive() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, ResetVis);
	AddPair(*Row, LOCTEXT("HintV", "V"), LOCTEXT("HintResetPreview", "Original material"), ResetVis);

	// -- Re-selecting Plane (2) flips the orientation, but not while a diagnostic is active.
	const TAttribute<EVisibility> PlaneVis = TAttribute<EVisibility>::CreateLambda(
		[bPlaneFlip, bDiagnosticActive, bQuickControls]()
	{
		return bPlaneFlip() && !bDiagnosticActive() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, PlaneVis);
	AddPair(*Row, LOCTEXT("HintPlane", "2"), LOCTEXT("HintPlaneFlip", "Flip plane orientation"), PlaneVis);

	// -- When no recipe exists, keep the Library-to-Layers nudge.
	const TAttribute<EVisibility> NoMaterialVis = TAttribute<EVisibility>::CreateLambda(
		[bNoMaterial, bPlaneFlip, bDiagnosticActive, bQuickControls]()
	{
		return bNoMaterial() && !bPlaneFlip() && !bDiagnosticActive() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, NoMaterialVis);
	AddPair(*Row, FText::GetEmpty(), LOCTEXT("HintNoMaterial", "Drag a surface from Library into Layers"), NoMaterialVis);

	TSharedRef<SWidget> Strip = MakePreviewCluster(Row);

	const TSharedRef<Mixtormat::SMixtormatHintStrip> StripWidget = SNew(Mixtormat::SMixtormatHintStrip)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this, bAnyContext]()
		{
			// Show only actions valid in the current context; plain material with no mask stays hint-free.
			return bPreviewOverlayUiVisible && bAnyContext()
				? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		}))
		[
			Strip
		];
	// Render opacity is a plain float on SWidget, read once here; the schema entry is
	// Reconstruct-mode, so editing Strip Opacity rebuilds the workspace and re-reads it.
	StripWidget->SetRenderOpacity(FMixtormatThemeStore::GetResolved().ContextLayout.HintStripOpacity);
	return StripWidget;
}

#undef LOCTEXT_NAMESPACE
