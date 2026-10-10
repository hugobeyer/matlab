// Copyright 2026 Hugo Beyer. All Rights Reserved.

// The viewport's contextual hint strip: one compact line of key/action pairs at the
// viewport's bottom-left. Blender-style "what can I do right here", scoped to what the
// preview viewport actually handles.
//
// Context-only: the plain material view shows nothing. Pairs appear only for the active
// context (quick controls open, a channel or debug view, the Plane's orientation flip, a
// missing material), each shown or hidden by a visibility lambda reading live workspace
// state, so the strip tracks context changes without a reconstruction.
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

	// The special contexts, evaluated live. The strip shows ONLY the active context's pairs:
	// nothing is on screen in the plain material view. Precedence: quick controls > channel
	// view > debug view > plane-orientation > no-material nudge.
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
	// Plane is the one mesh with a second state: pressing 2 again flips its orientation.
	const auto bPlaneFlip = [this]()
	{
		return PreviewMesh == EMixtormatPreviewMesh::Plane;
	};
	const auto bNoMaterial = [this]() { return !bHasWorkingMaterial; };
	const auto bAnyContext = [this]()
	{
		return bQuickControlsOpen
			|| (DebugPreviewMode != EMixtormatDebugPreviewMode::None)
			|| (!PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
				&& PreviewViewports[0]->GetChannelPreview() != EMixtormatChannelPreview::Material)
			|| PreviewMesh == EMixtormatPreviewMesh::Plane
			|| !bHasWorkingMaterial;
	};

	// One pair: optional keycap, action text, and when it is on screen. Visibility lives on
	// the children -- box-panel slots have no Visibility of their own. Action is an attribute
	// so a pair can name live state (the next channel's label).
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

	// -- Channel view active: name the NEXT channel, not "next channel".
	const TAttribute<EVisibility> ChannelVis = TAttribute<EVisibility>::CreateLambda([bChannelView, bQuickControls]()
	{
		return bChannelView() && !bQuickControls() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, ChannelVis);
	AddPair(*Row, LOCTEXT("HintV", "V"), TAttribute<FText>::CreateLambda([this]()
	{
		if (PreviewViewports.IsEmpty() || !PreviewViewports[0].IsValid())
		{
			return LOCTEXT("HintNextChannel", "Next channel");
		}
		const uint8 NextMode = (static_cast<uint8>(PreviewViewports[0]->GetChannelPreview()) + 1)
			% (static_cast<uint8>(EMixtormatChannelPreview::Fuzz) + 1);
		return FText::FromString(SMixtormatPreviewViewport::GetChannelPreviewLabel(
			static_cast<EMixtormatChannelPreview>(NextMode)));
	}), ChannelVis);
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

	// -- Plane selected: 2 again flips its orientation.
	const TAttribute<EVisibility> PlaneVis = TAttribute<EVisibility>::CreateLambda(
		[bPlaneFlip, bChannelView, bDebugView, bQuickControls]()
	{
		return bPlaneFlip() && !bChannelView() && !bDebugView() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, PlaneVis);
	AddPair(*Row, LOCTEXT("HintPlane", "2"), LOCTEXT("HintPlaneFlip", "Flip plane orientation"), PlaneVis);

	// -- No working material: a mouse-only nudge, the one thing a new user is stuck on.
	const TAttribute<EVisibility> NoMaterialVis = TAttribute<EVisibility>::CreateLambda(
		[bNoMaterial, bPlaneFlip, bChannelView, bDebugView, bQuickControls]()
	{
		return bNoMaterial() && !bPlaneFlip() && !bChannelView() && !bDebugView() && !bQuickControls()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	});
	AddGap(*Row, NoMaterialVis);
	AddPair(*Row, FText::GetEmpty(), LOCTEXT("HintNoMaterial", "Drag a surface from Library into Layers"), NoMaterialVis);

	TSharedRef<SWidget> Strip = MakePreviewCluster(Row);

	const TSharedRef<Mixtormat::SMixtormatHintStrip> StripWidget = SNew(Mixtormat::SMixtormatHintStrip)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this, bAnyContext]()
		{
			// Nothing applies -> nothing on screen. The plain material view is hint-free.
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
