// Copyright 2026 Hugo Beyer. All Rights Reserved.

// The viewport's contextual hint strip: one compact line of key/action pairs at the
// viewport's bottom-left. Blender-style "what can I do right here", scoped to what the
// preview viewport actually handles.
//
// Every pair is built once and shown or hidden by a visibility lambda reading live
// workspace state, so the strip tracks context changes without a reconstruction.
// First-match-wins is expressed as "special pairs visible only in their state, default
// pairs visible only when no special state is", which keeps at most five pairs on screen.
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
	TSharedRef<SWidget> MakeHintKeycap(const FText& Key)
	{
		const FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const FTextBlockStyle KeycapStyle = FMixtormatTypography::MakeTextStyle(
			FMixtormatTypography::GetSpec(Resolved.Typography, EMixtormatTextRole::MenuShortcut),
			Resolved.Palette.Get(EMixtormatColorRole::Text));
		return SNew(SMixtormatSurfaceBox)
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
	const Mixtormat::FMixtormatContextMetrics& Context = FMixtormatThemeStore::GetResolved().ContextLayout;
	const FTextBlockStyle ActionStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			FMixtormatThemeStore::GetResolved().Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
		FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	// The special contexts, evaluated live. Precedence is explicit per pair: quick controls
	// win over everything (the overlay is on screen), then a channel view, then a debug
	// view, then a selected module -- the same precedence GetPreviewModeLabel uses.
	const auto bQuickControls = [this]() { return bQuickControlsOpen; };
	const auto bChannelView = [this]()
	{
		return !PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
			&& PreviewViewports[0]->GetChannelPreview() != EMixtormatChannelPreview::Material;
	};
	const auto bDebugView = [this]()
	{
		return DebugPreviewMode != EMixtormatDebugPreviewMode::None;
	};
	const auto bModuleSelected = [this]() { return bHasSelectedLayer; };
	const auto bNoneSpecial = [this]()
	{
		return !bQuickControlsOpen
			&& !(DebugPreviewMode != EMixtormatDebugPreviewMode::None)
			&& !(!PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
				&& PreviewViewports[0]->GetChannelPreview() != EMixtormatChannelPreview::Material)
			&& !bHasSelectedLayer;
	};

	// One pair: optional keycap, action text, and when it is on screen.
	const auto AddPair = [&Context, &ActionStyle](
		SHorizontalBox& Box,
		const FText& Key,
		const FText& Action,
		const TAttribute<EVisibility>& Visible)
	{
		if (!Key.IsEmpty())
		{
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center).Visibility(Visible)
				[
					Mixtormat::MakeHintKeycap(Key)
				];
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center).Visibility(Visible)
				.Padding(Context.HintKeyActionGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Action)
					.TextStyle(&ActionStyle)
				];
		}
		else
		{
			Box.AddSlot().AutoWidth().VAlign(VAlign_Center).Visibility(Visible)
				[
					SNew(STextBlock)
					.Text(Action)
					.TextStyle(&ActionStyle)
				];
		}
	};

	// Gap before every pair after the first, so a collapsed pair takes its gap with it.
	const auto AddGap = [&Context](SHorizontalBox& Box, const TAttribute<EVisibility>& Visible)
	{
		Box.AddSlot().AutoWidth().VAlign(VAlign_Center).Visibility(Visible)
			.Padding(Context.HintItemGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSpacer).Size(FVector2D(2.0f, 1.0f))
			];
	};

	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// -- Quick controls open: the only pair on screen.
	const TAttribute<EVisibility> QuickControlsVis = TAttribute<EVisibility>::CreateLambda([bQuickControls]()
	{
		return bQuickControls() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddPair(*Row, LOCTEXT("HintEsc", "Esc"), LOCTEXT("HintCloseQuickControls", "Close quick controls"), QuickControlsVis);

	// -- Channel view active, unless quick controls are open above it.
	const TAttribute<EVisibility> ChannelVis = TAttribute<EVisibility>::CreateLambda([bChannelView, bQuickControls]()
	{
		return bChannelView() && !bQuickControls() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, ChannelVis);
	AddPair(*Row, LOCTEXT("HintV", "V"), LOCTEXT("HintNextChannel", "Next channel"), ChannelVis);
	AddGap(*Row, ChannelVis);
	AddPair(*Row, LOCTEXT("HintShiftV", "Shift+V"), LOCTEXT("HintMaterial", "Material"), ChannelVis);

	// -- Debug or child preview active: mouse-only, so no keycap. Sits under the channel view.
	const TAttribute<EVisibility> DebugVis = TAttribute<EVisibility>::CreateLambda([bDebugView, bChannelView, bQuickControls]()
	{
		return bDebugView() && !bChannelView() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, DebugVis);
	AddPair(*Row, FText::GetEmpty(), LOCTEXT("HintClearDebug", "Clear button returns to the composite"), DebugVis);

	// -- A module is selected, under every view context.
	const TAttribute<EVisibility> ModuleVis = TAttribute<EVisibility>::CreateLambda(
		[bModuleSelected, bDebugView, bChannelView, bQuickControls]()
	{
		return bModuleSelected() && !bDebugView() && !bChannelView() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, ModuleVis);
	AddPair(*Row, LOCTEXT("HintM", "M"), LOCTEXT("HintCycleModule", "Cycle module preview"), ModuleVis);

	// -- Default camera and viewport keys, on screen only when no special context leads.
	// Five pairs: the compactness contract. U/M and Z are discoverable through their
	// GLOBAL rows; these five are the ones with no other on-screen readout.
	const TAttribute<EVisibility> DefaultVis = TAttribute<EVisibility>::CreateLambda([bNoneSpecial]()
	{
		return bNoneSpecial() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, DefaultVis);
	AddPair(*Row, LOCTEXT("HintLMB", "LMB"), LOCTEXT("HintOrbit", "Orbit"), DefaultVis);
	AddGap(*Row, DefaultVis);
	AddPair(*Row, LOCTEXT("HintWheel", "Wheel"), LOCTEXT("HintZoom", "Zoom"), DefaultVis);
	AddGap(*Row, DefaultVis);
	AddPair(*Row, LOCTEXT("HintMesh", "1-4"), LOCTEXT("HintMeshAction", "Mesh"), DefaultVis);
	AddGap(*Row, DefaultVis);
	AddPair(*Row, LOCTEXT("HintQ", "Q"), LOCTEXT("HintQuickControls", "Quick controls"), DefaultVis);
	AddGap(*Row, DefaultVis);
	AddPair(*Row, LOCTEXT("HintH", "H"), LOCTEXT("HintHideUi", "Hide UI"), DefaultVis);

	TSharedRef<SWidget> Strip = MakePreviewCluster(Row);

	const TSharedRef<Mixtormat::SMixtormatHintStrip> StripWidget = SNew(Mixtormat::SMixtormatHintStrip)
		[
			Strip
		];
	StripWidget->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return bPreviewOverlayUiVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}));
	StripWidget->SetRenderOpacity(TAttribute<float>::CreateLambda([]()
	{
		return FMixtormatThemeStore::GetResolved().ContextLayout.HintStripOpacity;
	}));
	return StripWidget;
}

#undef LOCTEXT_NAMESPACE
