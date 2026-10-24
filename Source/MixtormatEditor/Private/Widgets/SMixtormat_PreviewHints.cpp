// Copyright 2026 Hugo Beyer. All Rights Reserved.

// The viewport's contextual hint strip: one compact line of key/action pairs.
// Its screen corner is configured by ContextLayout (typically top-right).
//
// Pairs reflect live actions, including W when a supported mask is selected,
// V for cycling, Shift+V when a diagnostic is active, Esc in quick controls, and the Plane flip.
// Visibility lambdas track selection and overlay changes without rebuilding.
//
// Hit-test-invisible like the light gizmo, so camera navigation passes through. The
// strip follows the H/Space master flag and collapses when nothing applies.

#include "Widgets/SMixtormat.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SMixtormatInternal.h"
#include "Widgets/MixtormatChildCapabilities.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "Style/MixtormatRecipes.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
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

void SMixtormat::OnSliderContextChanged(TSharedRef<SMixtormatSlider> Slider)
{
	const TSharedPtr<SMixtormatSlider> Current = HintSlider.Pin();
	if (Current.IsValid() && Current != Slider && Current->IsInteracting())
	{
		return;
	}
	if (!Slider->IsInteracting() && (!Slider->IsHovered() || !Slider->IsEnabled()))
	{
		if (Current == Slider) { HintSlider.Reset(); }
	}
	else
	{
		if (Current != Slider)
		{
			HintSliderHoveredAt = FSlateApplication::Get().GetCurrentTime();
			RegisterActiveTimer(MixtormatTokens::HelpDelay,
				FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
				{
					Invalidate(EInvalidateWidgetReason::Layout);
					return EActiveTimerReturnType::Stop;
				}));
		}
		HintSlider = Slider;
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

bool SMixtormat::IsHintInteractionActive() const
{
	const TSharedPtr<SMixtormatSlider> Slider = HintSlider.Pin();
	return Slider.IsValid() && Slider->IsInteracting();
}

bool SMixtormat::HasInteractionHint() const
{
	const TSharedPtr<SMixtormatSlider> Slider = HintSlider.Pin();
	return bQuickControlsOpen || (Slider.IsValid()
		&& (Slider->IsInteracting() || (Slider->IsHovered() && Slider->IsEnabled())));
}

void SMixtormat::SetQuickControlsHint(const EQuickControlsHint Hint, const bool bHovered)
{
	if (bHovered)
	{
		QuickControlsHint = Hint;
		QuickControlsHintHoveredAt = FSlateApplication::Get().GetCurrentTime();
	}
	else if (QuickControlsHint == Hint)
	{
		QuickControlsHint = EQuickControlsHint::None;
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

FText SMixtormat::GetInteractionHint() const
{
	const double Now = FSlateApplication::Get().GetCurrentTime();
	if (const TSharedPtr<SMixtormatSlider> Slider = HintSlider.Pin())
	{
		if (Slider->IsInteracting()) { return Slider->GetInteractionHint(); }
		if (Slider->IsHovered() && Slider->IsEnabled())
		{
			return Now - HintSliderHoveredAt >= MixtormatTokens::HelpDelay
				? Slider->GetInteractionHint() : FText::GetEmpty();
		}
	}
	if (!bQuickControlsOpen) { return FText::GetEmpty(); }
	if (bQuickControlsActionMenuOpen)
	{
		return LOCTEXT("HintQuickActionsMenu", "Choose an available action");
	}
	if (bQuickControlsFinalMenuOpen)
	{
		return LOCTEXT("HintQuickFinalMenu", "Adjust final composition");
	}
	if (Now - QuickControlsHintHoveredAt < MixtormatTokens::HelpDelay) { return FText::GetEmpty(); }
	switch (QuickControlsHint)
	{
	case EQuickControlsHint::Render:
		return LOCTEXT("HintQuickRender", "Choose AA and lighting quality · Drag: Render scale");
	case EQuickControlsHint::Composition:
		return LOCTEXT("HintQuickComposition", "Adjust AO, height remapping and displacement");
	case EQuickControlsHint::Lighting:
		return LOCTEXT("HintQuickLighting", "Click: Choose lighting · Reset: Camera and lighting");
	case EQuickControlsHint::Geometry:
		return LOCTEXT("HintQuickGeometry", "Click: Choose mesh · Selected plane: Flip");
	case EQuickControlsHint::Actions:
		return LOCTEXT("HintQuickActions", "Click: Open available actions");
	default:
		return FText::GetEmpty();
	}
}

void SMixtormat::ShowPreviewFeedback(const FText& Text)
{
	PreviewFeedbackText = Text;
	PreviewFeedbackStartedAt = FSlateApplication::Get().GetCurrentTime();
	PreviewFeedbackOpacity = 1.0f;
	if (!bPreviewFeedbackTimerActive)
	{
		bPreviewFeedbackTimerActive = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SMixtormat::TickPreviewFeedback));
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

EActiveTimerReturnType SMixtormat::TickPreviewFeedback(const double CurrentTime, const float DeltaTime)
{
	const double FadeTime = CurrentTime - PreviewFeedbackStartedAt - MixtormatTokens::PreviewFeedbackHoldSeconds;
	PreviewFeedbackOpacity = 1.0f - FMath::Clamp(
		static_cast<float>(FadeTime / MixtormatTokens::PreviewFeedbackFadeSeconds), 0.0f, 1.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (PreviewFeedbackOpacity <= 0.0f)
	{
		bPreviewFeedbackTimerActive = false;
		Invalidate(EInvalidateWidgetReason::Layout);
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewFeedback()
{
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle TextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	return SNew(SMixtormatSurfaceBox)
		.Visibility_Lambda([this]()
		{
			return bPreviewOverlayUiVisible && PreviewFeedbackOpacity > 0.0f && !IsHintInteractionActive()
				? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		})
		.RenderOpacity_Lambda([this]() { return PreviewFeedbackOpacity; })
		.Recipe_Lambda([]() { return Mixtormat::MakePreviewPlateRecipe(FMixtormatThemeStore::GetResolved()); })
		.Padding(FMargin(Resolved.ContextLayout.HintKeycapPadding))
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return PreviewFeedbackText; })
			.TextStyle(&TextStyle)
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SMixtormat::BuildPreviewHintStrip()
{
	const FTextBlockStyle ActionStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			FMixtormatThemeStore::GetResolved().Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
		FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	// Read live selection/preview state; do not advertise unavailable hotkeys.
	// An input owner takes precedence over selection hints, including outside Q.
		const auto bQuickControls = [this]() { return HasInteractionHint(); };
	const auto bChannelView = [this]()
	{
		return !PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
			&& PreviewViewports[0]->GetChannelPreview() != EMixtormatChannelPreview::Material;
	};
	const auto bMaskAvailable = [this]()
	{
		return bHasWorkingMaterial && (bMaskOverlayEnabled || IsMaskOverlayTargetReady());
	};
	const auto bCycleAvailable = [this]() { return bHasWorkingMaterial; };
	const auto bFlowAvailable = [this]()
	{
		return bHasWorkingMaterial && (bMaskOverlayEnabled || IsFlowOverlayTargetReady());
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
	const auto bAnyContext = [bQuickControls, bDiagnosticActive, bMaskAvailable, bFlowAvailable, bCycleAvailable, bPlaneFlip, bNoMaterial, bUvRotationAvailable]()
	{
		return bQuickControls() || bDiagnosticActive() || bMaskAvailable() || bFlowAvailable()
			|| bCycleAvailable() || bPlaneFlip() || bNoMaterial() || bUvRotationAvailable();
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

	// Entry owns Escape while typing; never advertise menu dismissal during an edit.
	const TAttribute<EVisibility> QuickControlsVis = TAttribute<EVisibility>::CreateLambda([this]()
	{
		return bQuickControlsOpen && !IsHintInteractionActive()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddPair(*Row, LOCTEXT("HintQuickCloseKeys", "Q / Esc"), LOCTEXT("HintQuickClose", "Close menu"), QuickControlsVis);
	const TAttribute<EVisibility> InteractionVis = TAttribute<EVisibility>::CreateLambda([this]()
	{
		return !GetInteractionHint().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, TAttribute<EVisibility>::CreateLambda([this]()
	{
		return bQuickControlsOpen && !IsHintInteractionActive() && !GetInteractionHint().IsEmpty()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}));
	AddPair(*Row, FText::GetEmpty(), TAttribute<FText>::CreateLambda([this]()
	{
		return GetInteractionHint();
	}), InteractionVis);

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
		const FMixtormatChildPreviewTarget Target = ResolveMaskOverlayTarget(
			Selected, NAME_None, EMixtormatPreviewOutputKind::Mask);
		if (bMaskOverlayEnabled && MaskOverlayOutputKind == EMixtormatPreviewOutputKind::Mask)
		{
			return LOCTEXT("HintHideDiagnostic", "Hide diagnostic");
		}
		if (Target.IsValid())
		{
			return bMaskOverlayEnabled
				? LOCTEXT("HintTransferDiagnostic", "Preview selected source")
				: LOCTEXT("HintShowDiagnostic", "Show diagnostic");
		}
		// A different diagnostic kind can switch only to a real selected mask output.
		return bMaskOverlayEnabled
			? LOCTEXT("HintDiagnosticActive", "Diagnostic active")
			: LOCTEXT("HintShowDiagnostic", "Show diagnostic");
	}), MaskVis);

	const TAttribute<EVisibility> FlowVis = TAttribute<EVisibility>::CreateLambda(
		[bFlowAvailable, bQuickControls]()
	{
		return bFlowAvailable() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, FlowVis);
	AddPair(*Row, LOCTEXT("HintE", "E"), TAttribute<FText>::CreateLambda([this]()
	{
		const FMixtormatChildAddress Selected = GetSelectedChildAddress();
		const FMixtormatChildPreviewTarget Target = ResolveMaskOverlayTarget(
			Selected, NAME_None, EMixtormatPreviewOutputKind::FlowDirection);
		if (bMaskOverlayEnabled && MaskOverlayOutputKind != EMixtormatPreviewOutputKind::Mask)
		{
			return LOCTEXT("HintHideDiagnostic", "Hide diagnostic");
		}
		if (Target.IsValid())
		{
			return bMaskOverlayEnabled
				? LOCTEXT("HintTransferFlow", "Preview selected Flow")
				: LOCTEXT("HintShowFlow", "Show raw Flow");
		}
		// A different diagnostic kind can switch only to a real selected Flow/UV output.
		return bMaskOverlayEnabled
			? LOCTEXT("HintDiagnosticActive", "Diagnostic active")
			: LOCTEXT("HintShowFlow", "Show raw Flow");
	}), FlowVis);

	const TAttribute<EVisibility> CycleVis = TAttribute<EVisibility>::CreateLambda(
		[bCycleAvailable, bQuickControls]()
	{
		return bCycleAvailable() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, CycleVis);
	AddPair(*Row, LOCTEXT("HintV", "V"), LOCTEXT("HintCyclePreview", "Cycle material channels"), CycleVis);

	// -- Shift+V resets PostFX, module diagnostics and material-channel views.
	const TAttribute<EVisibility> ResetVis = TAttribute<EVisibility>::CreateLambda(
		[bDiagnosticActive, bQuickControls]()
	{
		return bDiagnosticActive() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, ResetVis);
	AddPair(*Row, LOCTEXT("HintShiftV", "Shift+V"), LOCTEXT("HintResetPreview", "Original material"), ResetVis);

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

TSharedRef<SWidget> SMixtormat::BuildPreviewOutputStrip()
{
	SAssignNew(PreviewOutputStripHost, SBox)
		.Visibility_Lambda([this]()
		{
			if (!bPreviewOverlayUiVisible || bQuickControlsOpen)
			{
				return EVisibility::Collapsed;
			}
			const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
			return Child && !GetChildCapabilities(*Child).Outputs.IsEmpty()
				? EVisibility::SelfHitTestInvisible
				: EVisibility::Collapsed;
		});
	RebuildPreviewOutputStrip();
	return PreviewOutputStripHost.ToSharedRef();
}

void SMixtormat::RebuildPreviewOutputStrip()
{
	if (!PreviewOutputStripHost.IsValid())
	{
		return;
	}
	const FMixtormatChildAddress SelectedAddress = GetSelectedChildAddress();
	const FMixtormatLayerChild* Child = ResolveChildAt(SelectedAddress);
	if (!Child)
	{
		PreviewOutputStripHost->SetContent(SNullWidget::NullWidget);
		return;
	}
	const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Child);
	if (Caps.Outputs.IsEmpty())
	{
		PreviewOutputStripHost->SetContent(SNullWidget::NullWidget);
		return;
	}

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle HeaderStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			Resolved.Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const FTextBlockStyle ItemStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(
			Resolved.Typography, Mixtormat::EMixtormatTextRole::MenuShortcut),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	Column->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("PreviewOutputStripHeader", "OUTPUTS · {0}"),
				GetLayerChildName(*Child)))
			.TextStyle(&HeaderStyle)
		];

	for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
	{
		const bool bCopyable = Output.bCopyableAsMask || Output.bCopyableAsField;
		const bool bOverlay = Output.Kind == EMixtormatPreviewOutputKind::Mask
			|| Output.Kind == EMixtormatPreviewOutputKind::FlowDirection
			|| Output.Kind == EMixtormatPreviewOutputKind::WarpedUVGrid;
		const auto IsOutputPreviewActive = [this, SelectedAddress, Output, bOverlay]() -> bool
		{
			if (bOverlay)
			{
				const FMixtormatChildPreviewTarget Target = ResolveMaskOverlayTarget(
					SelectedAddress, Output.Name, Output.Kind);
				return bMaskOverlayEnabled
					&& SelectedAddress.OwnerType == MaskOverlaySource.OwnerType
					&& SelectedAddress.OwnerId == MaskOverlaySource.OwnerId
					&& SelectedAddress.ChildId == MaskOverlaySource.ChildId
					&& Target.IsValid()
					&& MaskOverlayOutputKind == Target.Kind
					&& MaskOverlayOutputName == Target.OutputName;
			}
			const FMixtormatChildPreviewTarget Target = ResolveChildPreviewTarget(
				Output.Name, Output.Kind, Output.PreviewGapMaskName);
			return !bMaskOverlayEnabled
				&& DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput
				&& Target.IsValid()
				&& ChildPreviewTarget == Target;
		};

		TSharedPtr<SButton> PreviewBtn;
		TSharedPtr<SButton> CopyBtn;
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		Row->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(PreviewBtn, SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.IsEnabled(Output.bPreviewable)
				.OnClicked_Lambda([this, Output, bOverlay]()
				{
					if (bOverlay)
					{
						return ToggleDiagnosticOverlayPreview(Output.Kind, Output.Name);
					}
					return ToggleChildOutputPreview(
						ResolveChildPreviewTarget(Output.Name, Output.Kind, Output.PreviewGapMaskName));
				})
				[
					SNew(SMixtormatSurfaceBox)
					.Padding(FMargin(Resolved.ContextLayout.HintKeycapPadding))
					.Recipe_Lambda([PreviewBtn, IsOutputPreviewActive]()
					{
						const bool bActive = IsOutputPreviewActive();
						const bool bHover = PreviewBtn.IsValid() && PreviewBtn->IsHovered();
						return Mixtormat::MakePreviewPlateRecipe(
							FMixtormatThemeStore::GetResolved(),
							bActive ? Mixtormat::EMixtormatPreviewPlateState::Checked
								: bHover ? Mixtormat::EMixtormatPreviewPlateState::Hover
									: Mixtormat::EMixtormatPreviewPlateState::Rest);
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(12.0f)
							.HeightOverride(12.0f)
							[
								SNew(SImage)
								.Image_Lambda([IsOutputPreviewActive]()
								{
									return IsOutputPreviewActive() ? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
								})
								.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(Output.Label)
							.TextStyle(&ItemStyle)
						]
					]
				]
			];

		if (bCopyable)
		{
			Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(CopyBtn, SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0.0f))
					.IsEnabled_Lambda([this, SelectedAddress, Output]()
					{
						return CanCopyChildOutput(SelectedAddress, Output.Name);
					})
					.OnClicked_Lambda([this, SelectedAddress, Output]()
					{
						CopyChildOutput(SelectedAddress, Output.Name);
						WorkingStatusText = FString::Printf(
							TEXT("Copied output: %s"), *Output.Label.ToString());
						return FReply::Handled();
					})
					[
						SNew(SMixtormatSurfaceBox)
						.Padding(FMargin(Resolved.ContextLayout.HintKeycapPadding))
						.Recipe_Lambda([CopyBtn]()
						{
							const bool bHover = CopyBtn.IsValid() && CopyBtn->IsHovered();
							return Mixtormat::MakePreviewPlateRecipe(
								FMixtormatThemeStore::GetResolved(),
								bHover ? Mixtormat::EMixtormatPreviewPlateState::Hover
									: Mixtormat::EMixtormatPreviewPlateState::Rest);
						})
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 3.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(11.0f)
								.HeightOverride(11.0f)
								[
									SNew(SImage)
									.Image(MixtormatIcons::Duplicate())
									.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("CopyOutputTagLabel", "Copy"))
								.TextStyle(&HeaderStyle)
							]
						]
					]
				];
		}

		Column->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				Row
			];
	}

	PreviewOutputStripHost->SetContent(MakePreviewCluster(Column));
}

#undef LOCTEXT_NAMESPACE
