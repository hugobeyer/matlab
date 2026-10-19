// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Gallery/SMixtormatGalleryTab.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatIconRail.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Menus/SMixtormatPopupAnchor.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Controls/MixtormatShellSplitterStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "InputCoreTypes.h"


namespace
{
	// Horizontal shadow under the rail buttons, above the Layers page; not a per-tab shade.
	class SMixtormatRailFade final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatRailFade) {} SLATE_END_ARGS()
		void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
		FVector2D ComputeDesiredSize(float) const override
		{
			const auto& L = FMixtormatThemeStore::GetResolved().PreviewLayout;
			return FVector2D(L.LeftRailButtonWidth + L.LeftRailFadeExtension, 1.0f);
		}
		int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const auto& L = FMixtormatThemeStore::GetResolved().PreviewLayout;
			const float Width = G.GetLocalSize().X;
			const float Edge = FMath::Min(FMath::Max(L.LeftRailButtonWidth, 0.0f), Width);
			const float Opacity = FMath::Clamp(L.LeftRailFadeOpacity * L.LeftRailShadowOpacity, 0.0f, 1.0f);
			if (Width <= Edge || Opacity <= 0.0f) return Layer;
			FLinearColor Shade = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shade);
			Shade.A *= Opacity * Style.GetColorAndOpacityTint().A;
			TArray<FSlateGradientStop> Stops;
			// A shadow *beneath* the rail: full strength at its outer edge, then
			// transparent over the Layers page. The buttons paint on top of this pass.
			Stops.Add(FSlateGradientStop(FVector2D(0.0f, 0.0f), Shade));
			Stops.Add(FSlateGradientStop(FVector2D(Edge, 0.0f), Shade));
			Shade.A = 0.0f;
			Stops.Add(FSlateGradientStop(FVector2D(Width, 0.0f), Shade));
			FSlateDrawElement::MakeGradient(Elements, Layer, G.ToPaintGeometry(), Stops, Orient_Vertical);
			return Layer + 1;
		}
	};


	DECLARE_DELEGATE_OneParam(FOnShellGripMoved, float);

	// Hit-testable only on the actual divider. It sits above the layer hierarchy so
	// row mouse capture cannot steal a resize, while the visible splitter stays 1px.
	class SMixtormatShellGrip final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatShellGrip) {}
			SLATE_ATTRIBUTE(float, HostWidth)
			SLATE_EVENT(FSimpleDelegate, OnGrabbed)
			SLATE_EVENT(FOnShellGripMoved, OnMoved)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			HostWidth = Args._HostWidth;
			OnGrabbed = Args._OnGrabbed;
			OnMoved = Args._OnMoved;
			SetCursor(EMouseCursor::ResizeLeftRight);
		}

		FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth, 1.0f);
		}

		int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
			FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			return LayerId;
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			PressX = Event.GetScreenSpacePosition().X;
			OnGrabbed.ExecuteIfBound();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!HasMouseCapture())
			{
				return FReply::Unhandled();
			}
			const float WidthInScreenPixels = HostWidth.Get(1.0f)
				* Geometry.GetAccumulatedLayoutTransform().GetScale();
			if (WidthInScreenPixels > UE_SMALL_NUMBER)
			{
				OnMoved.ExecuteIfBound(
					(Event.GetScreenSpacePosition().X - PressX) / WidthInScreenPixels);
			}
			return FReply::Handled();
		}

		FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
		{
			return HasMouseCapture() && Event.GetEffectingButton() == EKeys::LeftMouseButton
				? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
		}

	private:
		TAttribute<float> HostWidth;
		FSimpleDelegate OnGrabbed;
		FOnShellGripMoved OnMoved;
		float PressX = 0.0f;
	};

	using SMixtormatShellAction = SMixtormatGroupAction;

	float TopBarActionHeight()
	{
		const Mixtormat::FMixtormatShellMetrics& S = FMixtormatThemeStore::GetResolved().ShellLayout;
		return FMath::Max(1.0f, S.TopBarHeight - S.TopBarActionInset * 2.0f);
	}

	float TopBarIconSize()
	{
		return FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::TopBar)].GlyphSize;
	}

	FSlateColor TopBarIconTint()
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const Mixtormat::FMixtormatIconStyle& Icon = Resolved.Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::TopBar)];
		return FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)
			.CopyWithNewOpacity(Icon.RestOpacity));
	}
}

// Window chrome: top bar, page routing, splitters, status bar.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::ShowLeftPage(const int32 PageIndex)
{
	LeftTabIndex = PageIndex;
	if (PageIndex != 0)
	{
		LastNonLayersPage = PageIndex;
	}
	if (LeftSwitcher.IsValid())
	{
		LeftSwitcher->SetActiveWidgetIndex(PageIndex);
	}
	return FReply::Handled();
}

FReply SMixtormat::OpenDocumentation()
{
	FString LaunchError;
	FPlatformProcess::LaunchURL(
		TEXT("https://hugobeyer.github.io/mixtormat/"),
		nullptr,
		&LaunchError);
	if (!LaunchError.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("DocumentationLaunchFailed", "Could not open Mixtormat documentation:\n{0}"),
				FText::FromString(LaunchError)));
	}
	return FReply::Handled();
}

FReply SMixtormat::OpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Editor", "Plugins", "Mixtormat");
	}
	return FReply::Handled();
}


TSharedRef<SWidget> SMixtormat::BuildTopBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle TopBarTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::TopBar),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const bool bHasDeveloperSources =
		!FMixtormatSurfaceImporter::EnumerateShippedSourceDirectories().IsEmpty();
	return SNew(SBox)
		.HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.TopBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !UndoHistory.IsEmpty(); })
					.Text(LOCTEXT("UndoMaterialEditCompact", "Undo"))
					.ToolTipText(LOCTEXT("UndoMaterialEditHint", "Undo the last Mixtormat recipe edit (Ctrl+Z)."))
					.OnClicked(this, &SMixtormat::UndoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !RedoHistory.IsEmpty(); })
					.Text(LOCTEXT("RedoMaterialEditCompact", "Redo"))
					.ToolTipText(LOCTEXT("RedoMaterialEditHint", "Redo the last Mixtormat recipe edit (Ctrl+Y or Ctrl+Shift+Z)."))
					.OnClicked(this, &SMixtormat::RedoMaterialEdit)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(WorkingMaterialName); })
					.Clipping(EWidgetClipping::ClipToBounds)
					.Font(TopBarTextStyle.Font)
					.ColorAndOpacity(TopBarTextStyle.ColorAndOpacity)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("NewMaterialTopHint", "Start a new material workspace, confirming unsaved changes first."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::NewWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::Add()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("NewMaterialTop", "NEW"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::Folder()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("LoadMaterialTop", "LOAD"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::Save()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "SAVE"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterialAs)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::SaveAs()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAsTop", "SAVE AS..."))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.Visibility(bHasDeveloperSources ? EVisibility::Visible : EVisibility::Collapsed)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text(LOCTEXT("OpenLiveTheme", "UI STYLE"))
					.ToolTipText(LOCTEXT("OpenLiveThemeHint", "Developer popup: edit shared UI spacing, sizes, typography and colors live."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::OpenThemePanel)
				]
				// Bake is a peer toolbar action, so it uses the same tokenized button, spacing,
				// icon and label structure as New, Load, Save and Save As.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return WorkingMaterialAsset.IsValid() && bHasWorkingMaterial; })
					.ToolTipText(LOCTEXT("BakeMaterialHint", "Bake the current GPU-composited BC, Normal, and RAM outputs."))
					.OnClicked(this, &SMixtormat::BakeWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::Cube()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("BakeMaterialTop", "BAKE"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SAssignNew(DocsMenuAnchor, SMixtormatPopupAnchor)
					.Placement(MenuPlacement_BelowRightAnchor)
					.OnGetMenuContent_Lambda([this]()
					{
						MixtormatMenu::FBuilder Menu;
						Menu.Caption(LOCTEXT("DocsMenuCaption", "Help & Reference"))
							.Item(
								LOCTEXT("DocsMenuWelcome", "Welcome & Quick Guide"),
								MixtormatIcons::Documentation(),
								FSimpleDelegate::CreateSP(this, &SMixtormat::ShowWelcomeCard))
							.Item(
								LOCTEXT("DocsMenuOnline", "Online Documentation"),
								MixtormatIcons::Globe(),
								FSimpleDelegate::CreateLambda([]()
								{
									FString LaunchError;
									FPlatformProcess::LaunchURL(
										TEXT("https://hugobeyer.github.io/mixtormat/"),
										nullptr,
										&LaunchError);
								}));
						return Menu.Build();
					})
					[
						SNew(SMixtormatShellAction, true, TopBarActionHeight())
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
						.ToolTipText(LOCTEXT("DocumentationTopHint", "Open documentation and help."))
						.OnClicked_Lambda([this]()
						{
							if (DocsMenuAnchor.IsValid())
							{
								DocsMenuAnchor->SetIsOpen(!DocsMenuAnchor->IsOpen());
							}
							return FReply::Handled();
						})
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(TopBarIconSize())
								.HeightOverride(TopBarIconSize())
								[
									SNew(SImage).Image(MixtormatIcons::Documentation()).ColorAndOpacity(TopBarIconTint())
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(LOCTEXT("DocumentationTop", "DOCS"))
								.Font(TopBarTextStyle.Font)
								.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, false, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormat::OpenSettings)
					.ToolTipText(LOCTEXT("SettingsTopHint", "Open Mixtormat settings."))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[
								SNew(SImage).Image(MixtormatIcons::Settings()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SettingsTop", "SETTINGS"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]

			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildAuthoringPage()
{
	// Detach the old tree before creating the single replacement panels on theme refresh.
	if (InspectorDockHost.IsValid()) InspectorDockHost->SetContent(SNullWidget::NullWidget);
	if (InspectorOverlayHost.IsValid()) InspectorOverlayHost->SetContent(SNullWidget::NullWidget);
	if (LeftPanelDockHost.IsValid()) LeftPanelDockHost->SetContent(SNullWidget::NullWidget);
	InspectorPanel = BuildInspectorPanel();
	LeftPanel = BuildLayerStackPanel();
	// Every rebuild runs a full layout pass, and that pass reports slot values back through
	// OnSlotResized. Mute write-back until the layout has settled, then release it on the next tick
	// -- one-shot, not a running timer -- so a LiveTheme refresh cannot overwrite the user's split.
	const TSharedRef<float> LeftStart = MakeShared<float>(ShellLeftFraction);
	const TSharedRef<float> CenterStart = MakeShared<float>(ShellCenterFraction);
	const TSharedRef<float> RightStart = MakeShared<float>(ShellRightFraction);
	bSuppressSplitWriteBack = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		bSuppressSplitWriteBack = false;
		return EActiveTimerReturnType::Stop;
	}));
	return SNew(SBorder)
		.Padding(0.0f)
		.IsEnabled_Lambda([this]() { return !bIsBaking; })
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Shell.Ground)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SSplitter)
				.Style(&MixtormatShell::GetSplitterStyle())
			.PhysicalSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterVisualWidth)
			.HitDetectionSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterVisualWidth)

			+ SSplitter::Slot()
				.Value_Lambda([this]() { return ShellLeftFraction; })
				.OnSlotResized_Lambda([this](float Value)
				{
					if (!bSuppressSplitWriteBack)
					{
						ShellLeftFraction = Value;
					}
				})
			[
				BuildLeftColumn()
			]
			+ SSplitter::Slot()
						.Value_Lambda([this]()
						{
							// While the inspector column is away the centre slot carries its share, so the
							// splitter still divides the full width and a drag on the left handle reports
							// values in the units the expanded layout stores.
							return ShellCenterFraction + (bInspectorCollapsed ? ShellRightFraction : 0.0f);
						})
						.OnSlotResized_Lambda([this](float Value)
						{
							if (bSuppressSplitWriteBack)
							{
								return;
							}
							// Hand every borrowed share back before storing, so the centre column returns to
							// its own width rather than the one it was carrying for an absent panel -- the
							// same pattern for the left panel as for the inspector.
							if (bInspectorCollapsed)
							{
								Value = FMath::Max(0.0f, Value - ShellRightFraction);
							}

							ShellCenterFraction = Value;
						})
			[
				BuildPreviewPanel()
			]
+ SSplitter::Slot()
						// Zero, not a sliver: a collapsed slot is skipped by the splitter, so this keeps
						// the left and centre coefficients summing to one while the column is away.
						.Value_Lambda([this]() { return bInspectorCollapsed ? 0.0f : ShellRightFraction; })
						.OnSlotResized_Lambda([this](float Value)
						{
							// The right slot's value is a fraction of the full width in every placement, so the
							// docked inspector stays resizable while the left panel floats or is hidden.
							if (!bSuppressSplitWriteBack && !bInspectorCollapsed)
							{
								ShellRightFraction = Value;
							}
						})
			[
				SAssignNew(InspectorDockHost, SBox)
				.Visibility_Lambda([this]() { return bInspectorCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
				[InspectorPlacement == EInspectorPlacement::Overlay ? SNullWidget::NullWidget : InspectorPanel.ToSharedRef()]
			]
		]

		// Invisible resize grips are on top of the hierarchy and viewport.
		// Their hit width is centered on the physical splitter line, on BOTH sides.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.Padding_Lambda([this]()
			{
				const float HitWidth = FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth;
				if (!LeftSwitcher.IsValid()) return FMargin(0.0f);
				const FGeometry& Left = LeftSwitcher->GetCachedGeometry();
				const FVector2D Edge = Left.LocalToAbsolute(FVector2D(Left.GetLocalSize().X, 0.0f));
				const float X = GetCachedGeometry().AbsoluteToLocal(Edge).X;
				return FMargin(FMath::Max(0.0f, X - HitWidth * 0.5f), 0.0f, 0.0f, 0.0f);
			})
			[
				SNew(SBox)
				.WidthOverride_Lambda([]()
				{
					return FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth;
				})
				[
					SNew(SMixtormatShellGrip)
					.HostWidth_Lambda([this]() { return GetCachedGeometry().GetLocalSize().X; })
					.OnGrabbed_Lambda([this, LeftStart, CenterStart]() { *LeftStart = ShellLeftFraction; *CenterStart = ShellCenterFraction; })
					.OnMoved_Lambda([this, LeftStart, CenterStart](float Delta)
					{
						const float Width = FMath::Max(GetCachedGeometry().GetLocalSize().X, 1.0f);
						const float MinLeft = (FMixtormatThemeStore::GetResolved().PreviewLayout.LeftRailContentInset
							+ FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth) / Width;
						const float MinCenter = FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth / Width;
						const float Pair = *LeftStart + *CenterStart;
						ShellLeftFraction = FMath::Clamp(*LeftStart + Delta, MinLeft, FMath::Max(MinLeft, Pair - MinCenter));
						ShellCenterFraction = Pair - ShellLeftFraction;
						Invalidate(EInvalidateWidgetReason::Layout);
					})
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return bInspectorCollapsed ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
			.Padding_Lambda([this]()
			{
				const float HitWidth = FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth;
				if (!InspectorDockHost.IsValid()) return FMargin(0.0f);
				const FVector2D Edge = InspectorDockHost->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector);
				const float X = GetCachedGeometry().AbsoluteToLocal(Edge).X;
				return FMargin(FMath::Max(0.0f, X - HitWidth * 0.5f), 0.0f, 0.0f, 0.0f);
			})
			[
				SNew(SBox)
				.WidthOverride_Lambda([]()
				{
					return FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth;
				})
				[
					SNew(SMixtormatShellGrip)
					.HostWidth_Lambda([this]() { return GetCachedGeometry().GetLocalSize().X; })
					.OnGrabbed_Lambda([this, CenterStart, RightStart]() { *CenterStart = ShellCenterFraction; *RightStart = ShellRightFraction; })
					.OnMoved_Lambda([this, CenterStart, RightStart](float Delta)
					{
						const float Width = FMath::Max(GetCachedGeometry().GetLocalSize().X, 1.0f);
						const float MinFraction = FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth / Width;
						const float Pair = *CenterStart + *RightStart;
						ShellCenterFraction = FMath::Clamp(*CenterStart + Delta, MinFraction,
							FMath::Max(MinFraction, Pair - MinFraction));
						ShellRightFraction = Pair - ShellCenterFraction;
						Invalidate(EInvalidateWidgetReason::Layout);
					})
				]
			]
		]

		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			// An FOverlaySlot takes a literal padding only, so the collapse-dependent bottom inset
			// lives on this wrapper box instead.
			SNew(SBox)
			.Padding_Lambda([this]()
			{
				const Mixtormat::FMixtormatGalleryMetrics& Gallery =
					FMixtormatThemeStore::GetResolved().GalleryLayout;
				// Collapsed, the tab sits flush with the workspace bottom -- directly above the status
				// bar, as a drawer handle should; expanded, the drawer keeps its floating inset.
				const float Bottom = 0.0f;
				return FMargin(Gallery.DrawerSideInset, Gallery.DrawerInset, Gallery.DrawerSideInset, Bottom);
			})
			[
				SAssignNew(GalleryDrawerHost, SBox)
			.HeightOverride_Lambda([this]()
			{
				if (bGalleryDrawerAnimating)
				{
					return GalleryDrawerAnimatedHeight;
				}
				if (bBottomLibraryCollapsed)
				{
					return FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerCollapsedHeight;
				}
				return GetGalleryOpenHeight();
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return bBottomLibraryCollapsed && !bGalleryDrawerAnimating
							? EVisibility::Collapsed : EVisibility::Visible;
					})
					[BuildBottomLibrary()]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center)
				[
					// A centred handle, not a bar: the collapsed drawer is a fixed-width tab above the
					// status bar, so it stops competing with the layer column's bottom edge.
					SNew(SBox)
					.WidthOverride(MixtormatTokens::GalleryTabWidth)
					.Visibility_Lambda([this]()
					{
						return bBottomLibraryCollapsed && !bGalleryDrawerAnimating
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						// The action returns FReply; the tab's delegate takes void.
						SNew(SMixtormatHelp)
						.Text(LOCTEXT("RestoreGalleryStyledHint", "Open the material and mask gallery (G)."))
						[
							SNew(SMixtormatGalleryTab)
							.OnActivated(FSimpleDelegate::CreateLambda([this]() { ToggleBottomLibraryCollapsed(); }))
						]
					]
				]
			]
			]
		]
	];
}

TSharedRef<SWidget> SMixtormat::BuildLeftColumn()
{
	// One continuous page surface. The rail overlays the page's leading inset
	// instead of reserving a separate horizontal column beside it.
	const Mixtormat::FMixtormatPreviewMetrics& Layout =
		FMixtormatThemeStore::GetResolved().PreviewLayout;
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				// Only page content is inset; the rail overlays the same dark column.
				SNew(SBox)
				.Padding(FMargin(Layout.LeftRailContentInset, 0.0f, 0.0f, 0.0f))
				[
					SAssignNew(LeftSwitcher, SWidgetSwitcher)
					.WidgetIndex(LeftTabIndex)
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(LeftPanelDockHost, SBox)
						[LeftPanel.ToSharedRef()]
					]
					+ SWidgetSwitcher::Slot()[BuildUserLibraryPage()]
					+ SWidgetSwitcher::Slot()[BuildGlobalPage()]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.Visibility(EVisibility::HitTestInvisible)
				.WidthOverride(Layout.LeftRailButtonWidth + Layout.LeftRailFadeExtension)
				[SNew(SMixtormatRailFade)]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.WidthOverride(Layout.LeftRailButtonWidth)
				[
					SNew(SMixtormatIconRail)
					.Options({
						MixtormatIcons::Layers(),
						MixtormatIcons::Library(),
						MixtormatIcons::Global() })
					.Labels({
						LOCTEXT("LayersRailLabel", "Layers"),
						LOCTEXT("LibraryRailLabel", "Library"),
						LOCTEXT("GlobalRailLabel", "Global") })
					.ToolTips({
						LOCTEXT("LayersRailHint", "The layer stack: layers, their masks, effects and filters."),
						LOCTEXT("LibraryRailHint", "Saved mixes and imported user surfaces."),
						LOCTEXT("GlobalRailHint", "Document-wide variables and preview settings.") })
					.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
					.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildGlobalPage()
{
	const auto AddGroupToggle = [this](const TSharedRef<SVerticalBox>& Panel, const FText& Label,
		const FText& ToolTip, bool& bFlag)
	{
		AddSliderRow(Panel, MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([&bFlag]()
				{
					return bFlag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([&bFlag](const ECheckBoxState State)
				{
					bFlag = State == ECheckBoxState::Checked;
				}),
				ToolTip)));
	};
	const auto AddGlobalCard = [this](const TSharedRef<SVerticalBox>& Panel, const FText& Title)
	{
		if (Panel->GetChildren()->Num() > 0)
		{
			Panel->AddSlot().AutoHeight()
			[
				SNew(SBox).HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.GlobalCardGap)
			];
		}
		return AddCard(Panel, Title);
	};
	const auto Final = [this]() -> FMixtormatFinalSettings*
	{
		return bHasWorkingMaterial ? &WorkingFinalSettings : nullptr;
	};

	TSharedRef<SVerticalBox> DocumentSection = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(DocumentSection, LOCTEXT("GlobalDocOutputCard", "OUTPUT & RESOLUTION"));
		AddSliderRow(Card, MixtormatRow::Make(
			LOCTEXT("GlobalResolutionLabel", "Resolution"),
			MakeCompositionResolutionControl(),
			LOCTEXT("GlobalResolutionHint", "Working composition and bake resolution.")));
		AddSliderRow(Card, MixtormatRow::Make(
			LOCTEXT("GlobalRotateUV90Label", "Rotate Output 90°"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					return bGlobalUVRotation90 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
				{
					if (bHasWorkingMaterial)
					{
						SetGlobalUVRotation90(State == ECheckBoxState::Checked);
					}
				}),
				LOCTEXT("GlobalRotateUV90Hint", "Rotate the entire composited material 90 degrees (Shortcut: 5)."))));
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(DocumentSection, LOCTEXT("GlobalFinalReliefCard", "FINAL RELIEF & AO"));
		AddSliderRow(Card, MakeMemberSlider<FMixtormatFinalSettings>(
			LOCTEXT("GlobalFinalNormalStrength", "Normal Strength"),
			Final, &FMixtormatFinalSettings::HeightNormalStrength, 0.0, 4.0, 1.0, 0.01,
			LOCTEXT("GlobalFinalNormalStrengthHint", "Strength of the final height-derived normal pass across the entire material.")));
		AddSliderRow(Card, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatFinalSettings>(
				LOCTEXT("GlobalFinalAOAmount", "AO Amount"),
				Final, &FMixtormatFinalSettings::HeightAOAmount, 0.0, 1.0, 0.5, 0.01,
				LOCTEXT("GlobalFinalAOAmountHint", "Ambient occlusion derived from the final composited height.")),
			MakeMemberSlider<FMixtormatFinalSettings>(
				LOCTEXT("GlobalFinalAORadius", "AO Radius"),
				Final, &FMixtormatFinalSettings::HeightAORadius, 1.0, 64.0, 8.0, 0.5,
				LOCTEXT("GlobalFinalAORadiusHint", "Occlusion reach in pixels at 1024 resolution."))));
		AddSliderRow(Card, MakeMemberToggle<FMixtormatFinalSettings>(
			LOCTEXT("GlobalFinalAutoRemapHeight", "Auto Remap Height"),
			Final, &FMixtormatFinalSettings::bAutoRemapHeight,
			LOCTEXT("GlobalFinalAutoRemapHeightHint", "Remap the finished height to 0..1 from its measured min/max before final AO and normals.")));
	}

	TSharedRef<SVerticalBox> PreviewSection = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewGeometry", "GEOMETRY & DISPLACEMENT"));
		const TArray<FText> MeshOptions = {
			LOCTEXT("MeshOptionSphere", "SPHERE"),
			LOCTEXT("MeshOptionPlane", "PLANE"),
			LOCTEXT("MeshOptionCube", "CUBE"),
			LOCTEXT("MeshOptionCylinder", "CYL")};
		AddSliderRow(Card, SNew(SMixtormatSegmentedControl)
			.Options(MeshOptions)
			.ActiveIndex_Lambda([this]() { return static_cast<int32>(PreviewMesh); })
			.OnChosen_Lambda([this](const int32 Index)
			{
				SetPreviewMesh(static_cast<EMixtormatPreviewMesh>(FMath::Clamp(Index, 0, 3)));
			}));
		AddSliderRow(Card, BuildPreviewDisplacementControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewLighting", "LIGHTING & CAMERA"));
		AddSliderRow(Card, BuildPreviewLightingControls(EPreviewControlLayout::Inline));
		AddSliderRow(Card, BuildPreviewSceneControls());
		AddSliderRow(Card, BuildPreviewCameraControls());
		AddGroupToggle(Card, LOCTEXT("PreviewLightGizmo", "Show Light Gizmo on RMB"),
			LOCTEXT("PreviewLightGizmoHint", "Show the lighting direction gizmo while rotating lighting with RMB."),
			bPreviewLightGizmoVisible);
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewRender", "VIEWPORT QUALITY"));
		AddSliderRow(Card, BuildPreviewRenderControls());
	}

	return SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		+ SScrollBox::Slot()
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.GlobalPagePadding, 0.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("GlobalHeading", "FINAL COMPOSITE"))
				.InitiallyExpanded(true)
				[
					SNew(SBox)
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					[
						DocumentSection
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("GlobalPreviewHeading", "VIEWPORT & LIGHTING"))
				.InitiallyExpanded(true)
				[PreviewSection]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildStatusBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FTextBlockStyle& MutedStyle =
		Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText"));

	return SNew(SBox)
		.HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.StatusBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(
				FMixtormatThemeStore::GetResolved().Palette.Get(
					Mixtormat::EMixtormatColorRole::Ground))
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(WorkingStatusText);
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const FText QualityText =
							PreviewQuality == EMixtormatPreviewQuality::Lumen
								? LOCTEXT("StatusQualityLumen", "Lumen On")
								: LOCTEXT("StatusQualityDefault", "Default · Studio AO");

						return FText::Format(
							LOCTEXT(
								"RealtimeStatusDynamic",
								"Real-time Preview · {0} · SM6"),
							QualityText);
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::Format(
							LOCTEXT("LayerStatus", "Layers {0}"),
							FText::AsNumber(WorkingLayers.Num()));
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]
			]
		];
}

float SMixtormat::GetGalleryOpenHeight() const
{
	if (bGalleryCursorPeek && GalleryPeekHeight > 0.0f)
	{
		return GalleryPeekHeight;
	}
	const Mixtormat::FMixtormatGalleryMetrics& Layout =
		FMixtormatThemeStore::GetResolved().GalleryLayout;
	return GalleryDrawerHeight > 0.0f ? GalleryDrawerHeight : Layout.DrawerInitialHeight;
}

float SMixtormat::GetGalleryPeekHeightAtCursor() const
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const Mixtormat::FMixtormatGalleryMetrics& Gallery = Resolved.GalleryLayout;
	const FGeometry& Geometry = GetCachedGeometry();
	const FVector2D Local = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const float FromBottom = Geometry.GetLocalSize().Y - Local.Y;
	const float PaneHeader = Resolved.Buttons.Height + Gallery.TilePadding * 2.0f;
	const float Available = Geometry.GetLocalSize().Y;
	const float Minimum = FMath::Min(Available, Gallery.DrawerPeekMinHeight);
	const float Maximum = FMath::Max(Minimum, FMath::Min(Available, Gallery.DrawerPeekMaxHeight));
	const float Target = FromBottom + Gallery.DrawerPeekDistance;
	const float Averaged = FMath::Lerp(Gallery.DrawerInitialHeight, Target,
		FMath::Clamp(Gallery.DrawerPeekLerp, 0.0f, 1.0f));
	return FMath::Clamp(Averaged, Minimum, Maximum);
}

void SMixtormat::PersistGalleryPinState()
{
	UMixtormatEditorSettings* Settings = GetMutableDefault<UMixtormatEditorSettings>();
	Settings->bGalleryDrawerPinned = bGalleryPinned;
	Settings->GalleryDrawerHeight = GalleryDrawerHeight;
	Settings->SaveConfig();
}

FReply SMixtormat::ToggleBottomLibraryCollapsed()
{
	const bool bOpening = bBottomLibraryCollapsed;
	if (bOpening && !bGalleryPinned)
	{
		GalleryPeekHeight = GetGalleryPeekHeightAtCursor();
		bGalleryCursorPeek = true;
	}
	else if (!bOpening)
	{
		bGalleryCursorPeek = false;
	}

	if (!bGalleryDrawerAnimating)
	{
		GalleryDrawerAnimatedHeight = bOpening
			? FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerCollapsedHeight
			: GetGalleryOpenHeight();
	}
	bBottomLibraryCollapsed = !bBottomLibraryCollapsed;
	bGalleryPointerInside = !bBottomLibraryCollapsed;

	if (!bGalleryDrawerAnimating)
	{
		bGalleryDrawerAnimating = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, const float DeltaTime)
		{
			const float TargetHeight = bBottomLibraryCollapsed
				? FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerCollapsedHeight
				: GetGalleryOpenHeight();
			GalleryDrawerAnimatedHeight = FMath::FInterpTo(
				GalleryDrawerAnimatedHeight, TargetHeight, DeltaTime, 14.0f);
			if (FMath::Abs(GalleryDrawerAnimatedHeight - TargetHeight) <= 0.5f)
			{
				GalleryDrawerAnimatedHeight = TargetHeight;
				bGalleryDrawerAnimating = false;
				Invalidate(EInvalidateWidgetReason::Layout);
				return EActiveTimerReturnType::Stop;
			}
			Invalidate(EInvalidateWidgetReason::Layout);
			return EActiveTimerReturnType::Continue;
		}));
	}
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
