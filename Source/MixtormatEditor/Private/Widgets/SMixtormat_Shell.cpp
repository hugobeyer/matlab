// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Gallery/SMixtormatGalleryTab.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatIconRail.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Controls/MixtormatShellSplitterStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"


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
			const float Opacity = FMath::Clamp(L.LeftRailFadeOpacity, 0.0f, 1.0f);
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
			FSlateDrawElement::MakeGradient(Elements, Layer, G.ToPaintGeometry(), Stops, Orient_Horizontal);
			return Layer + 1;
		}
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
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("DocumentationTopHint", "Open Mixtormat documentation."))
					.OnClicked(this, &SMixtormat::OpenDocumentation)
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
			.HitDetectionSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth)

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
				const float Bottom = bBottomLibraryCollapsed ? 0.0f : Gallery.DrawerInset;
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
			return GalleryDrawerHeight > 0.0f
				? GalleryDrawerHeight
				: FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInitialHeight;
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
	// One switch per viewport group: the H/Space master flag still hides everything, and these hide
	// one group each. Session state on the retained workspace, so a theme rebuild keeps them and a
	// hidden group keeps its hotkeys.
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
	// The settings are the same builders the viewport overlay uses, so the two views cannot drift:
	// a control added to a strip appears here, and both write the same state. Cards, because a flat
	// list of every preview control reads as a wall; each card is one feature, and the icon buttons
	// run inline inside it rather than as a column of full-width bars.
	TSharedRef<SVerticalBox> PreviewSection = SNew(SVerticalBox);
	// Reuse the Inspector's Card builder; page-level gaps remain independent
	// of preview-overlay button spacing or marking-menu geometry.
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
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewVisibility", "VISIBILITY"));
		AddGroupToggle(Card, LOCTEXT("PreviewGroupRender", "Render strip"),
			LOCTEXT("PreviewGroupRenderHint", "Render scale and the Final popup on the viewport."),
			bPreviewGroupRenderVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupLighting", "Lighting"),
			LOCTEXT("PreviewGroupLightingHint", "Studio presets, the camera and lighting reset, and the light and skylight sliders."),
			bPreviewGroupLightingVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupGeometry", "Geometry"),
			LOCTEXT("PreviewGroupGeometryHint", "The preview mesh buttons and the UV 90° toggle."),
			bPreviewGroupGeometryVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupCamera", "Camera"),
			LOCTEXT("PreviewGroupCameraHint", "The preview mode label and the FOV slider."),
			bPreviewGroupCameraVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupOutput", "Output"),
			LOCTEXT("PreviewGroupOutputHint", "Composition resolution and the clear-debug control."),
			bPreviewGroupOutputVisible);
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewRender", "RENDER"));
		AddSliderRow(Card, BuildPreviewRenderControls());
	}
	{
		// Presets and the light sliders are one feature: what the surface is lit by.
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewLighting", "LIGHTING"));
		AddGroupToggle(Card, LOCTEXT("PreviewLightGizmo", "Light Gizmo"),
			LOCTEXT("PreviewLightGizmoHint", "Show the lighting direction gizmo while rotating lighting with RMB."),
			bPreviewLightGizmoVisible);
		AddSliderRow(Card, BuildPreviewLightingControls(EPreviewControlLayout::Inline));
		AddSliderRow(Card, BuildPreviewSceneControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewGeometry", "GEOMETRY"));
		AddSliderRow(Card, BuildPreviewGeometryControls(EPreviewControlLayout::Inline));
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewCamera", "CAMERA"));
		AddSliderRow(Card, BuildPreviewCameraControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewOutput", "OUTPUT"));
		AddSliderRow(Card, BuildPreviewOutputControls());
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
				.Title(LOCTEXT("GlobalHeading", "GLOBAL"))
				.InitiallyExpanded(true)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return bHasWorkingMaterial
							? LOCTEXT("GlobalEmpty", "No global variables yet.")
							: LOCTEXT("GlobalNoMaterial", "Create or open a material to add global variables.");
					})
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
						Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(MixtormatTokens::EmptyStateOpacity)))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("GlobalPreviewHeading", "PREVIEW / VIEWPORT"))
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

FReply SMixtormat::ToggleBottomLibraryCollapsed()
{
	if (!bGalleryDrawerAnimating)
	{
		GalleryDrawerAnimatedHeight = bBottomLibraryCollapsed
			? FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerCollapsedHeight
			: (GalleryDrawerHeight > 0.0f
				? GalleryDrawerHeight
				: FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInitialHeight);
	}
	bBottomLibraryCollapsed = !bBottomLibraryCollapsed;
	bGalleryPointerInside = !bBottomLibraryCollapsed;

	if (!bGalleryDrawerAnimating)
	{
		bGalleryDrawerAnimating = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, const float DeltaTime)
		{
			const Mixtormat::FMixtormatGalleryMetrics& Layout = FMixtormatThemeStore::GetResolved().GalleryLayout;
			const float TargetHeight = bBottomLibraryCollapsed
				? Layout.DrawerCollapsedHeight
				: (GalleryDrawerHeight > 0.0f ? GalleryDrawerHeight : Layout.DrawerInitialHeight);
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
