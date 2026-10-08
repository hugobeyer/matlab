// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatIconRail.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Controls/MixtormatShellSplitterStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"


namespace
{
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
	// Layers is the only page that can pop out; choosing it returns the retained stack home.
	if (PageIndex == 0 && LeftPanelPlacement != ELeftPanelPlacement::Docked)
	{
		LeftPanelPlacement = ELeftPanelPlacement::Docked;
		ApplyLeftPanelPlacement();
	}
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
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text_Lambda([this]()
					{
						switch (LeftPanelPlacement)
						{
						case ELeftPanelPlacement::Docked: return LOCTEXT("LayersHome", "Layers: Home");
						case ELeftPanelPlacement::Overlay: return LOCTEXT("LayersPoppedOut", "Layers: Popped Out");
						default: return LOCTEXT("LayersHidden", "Layers: Hidden");
						}
					})
					.ToolTipText(LOCTEXT("ToggleLeftPanelHint", "Cycle Layers: Home → Popped Out → Hidden → Home (L). Click LAYERS to return it home."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::ToggleLeftPanelCollapsed)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text_Lambda([this]()
					{
						switch (InspectorPlacement)
												{
												case EInspectorPlacement::Docked: return LOCTEXT("InspectorDocked", "Inspector: Docked");
												case EInspectorPlacement::Overlay: return LOCTEXT("InspectorOverlay", "Inspector: Overlay");
												default: return LOCTEXT("InspectorHidden", "Inspector: Hidden");
												}
					})
					.ToolTipText(LOCTEXT("ToggleInspectorHint", "Cycle Inspector placement: Docked → Overlay → Hidden → Docked (P)."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::ToggleInspectorCollapsed)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f)
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
	if (LeftPanelOverlayHost.IsValid()) LeftPanelOverlayHost->SetContent(SNullWidget::NullWidget);
	InspectorPanel = BuildInspectorPanel();
	LeftPanel = BuildFloatingLayerStack();
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
		.Padding(FMargin(
			FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerSideInset,
			FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInset,
			FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerSideInset,
			FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInset))
		[
			SNew(SBox)
			.HeightOverride_Lambda([this]()
			{
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
						return bBottomLibraryCollapsed ? EVisibility::Collapsed : EVisibility::Visible;
					})
					[BuildBottomLibrary()]
				]
				+ SOverlay::Slot()
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.BottomLibraryCollapseButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT("RestoreGalleryHint", "Open the material and mask gallery (G)."))
					.Visibility_Lambda([this]()
					{
						return bBottomLibraryCollapsed ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.OnClicked(this, &SMixtormat::ToggleBottomLibraryCollapsed)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SBox).HeightOverride(MixtormatTokens::HairlineThickness)
							[
								SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
								.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							.Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.HeaderGap, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)].GlyphSize)
							.HeightOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)].GlyphSize)
							[
								SNew(SImage).Image(MixtormatIcons::Library())
								.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SBox).HeightOverride(MixtormatTokens::HairlineThickness)
							[
								SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
								.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)))
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
	// The rail and selected page form the normal left workspace column; only Layers can pop out.
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				[
					SNew(SMixtormatIconRail)
					.Options({
						MixtormatIcons::Layers(),
						MixtormatIcons::Library(),
						MixtormatIcons::Global() })
					.ToolTips({
						LOCTEXT("LayersRailHint", "The layer stack: layers, their masks, effects and filters."),
						LOCTEXT("LibraryRailHint", "Saved mixes and imported user surfaces."),
						LOCTEXT("GlobalRailHint", "Document-wide variables and preview settings.") })
					.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
					.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SBox)
				[
					SAssignNew(LeftSwitcher, SWidgetSwitcher)
					.WidgetIndex(LeftTabIndex)
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(LeftPanelDockHost, SBox)
						[LeftPanelPlacement == ELeftPanelPlacement::Docked
							? LeftPanel.ToSharedRef() : SNullWidget::NullWidget]
					]
					+ SWidgetSwitcher::Slot()[BuildUserLibraryPage()]
					+ SWidgetSwitcher::Slot()[BuildGlobalPage()]
				]
				]
			];
	}

TSharedRef<SWidget> SMixtormat::BuildFloatingLayerStack()
{
	// The same grab margin starts a home pop-out or moves the floating stack; resize grips remain floating-only.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			// Docked, the column keeps the ground colour it always had. Floating, it takes the
			// inspector overlay's square, borderless, translucent surface -- the same tokens.
			.BorderBackgroundColor_Lambda([this]()
			{
				FLinearColor Background = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell);
				Background.A *= FMixtormatThemeStore::GetResolved().PreviewLayout.LeftOverlaySurfaceOpacity;
				return Background;
			})
			[
				SNew(SVerticalBox)
				// The home grab area uses Slate drag detection, so an ordinary click does not pop it out.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(LeftPanelOverlay.Header, SBox)
					.HeightOverride(MixtormatTokens::OverlayPanelGrabMargin)
					.ToolTipText(LOCTEXT("LayersGrabHint", "Drag to pop Layers out or move it. Click LAYERS or drag back to the rail to return it home."))
					[
						SNew(SBorder)
						.Visibility(EVisibility::Visible)
						.Padding(0.0f)
						.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.BorderBackgroundColor(FLinearColor::Transparent)
						.Cursor(EMouseCursor::GrabHand)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					BuildLayerStackPanel()
				]
			]
		]
		// Overlay only; docked resizing remains the splitter's responsibility.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 0, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 1, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 2, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 3, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		// The two side edges: width only, so the height stays auto and re-measures at the new width.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 4, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
		[MixtormatOverlay::MakeResizeGrip(LeftPanelOverlay, 5, LOCTEXT("ResizeLeftPanelOverlayHint", "Drag to resize the Layers panel."))]
		// The way back to auto-fit after a corner drag has frozen the height (D23): bottom-centre,
		// the edge the height is about.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		[MakeOverlayFitButton(LeftPanelOverlay, LeftPanel)];
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
	{
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewVisibility", "VISIBILITY"));
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
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewRender", "RENDER"));
		AddSliderRow(Card, BuildPreviewRenderControls());
	}
	{
		// Presets and the light sliders are one feature: what the surface is lit by.
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewLighting", "LIGHTING"));
		AddGroupToggle(Card, LOCTEXT("PreviewLightGizmo", "Light Gizmo"),
			LOCTEXT("PreviewLightGizmoHint", "Show the lighting direction gizmo while rotating lighting with RMB."),
			bPreviewLightGizmoVisible);
		AddSliderRow(Card, BuildPreviewLightingControls(EPreviewControlLayout::Inline));
		AddSliderRow(Card, BuildPreviewSceneControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewGeometry", "GEOMETRY"));
		AddSliderRow(Card, BuildPreviewGeometryControls(EPreviewControlLayout::Inline));
	}
	{
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewCamera", "CAMERA"));
		AddSliderRow(Card, BuildPreviewCameraControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddCard(PreviewSection, LOCTEXT("GlobalPreviewOutput", "OUTPUT"));
		AddSliderRow(Card, BuildPreviewOutputControls());
	}

	return SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		+ SScrollBox::Slot()
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.GroupOuterGap, 0.0f))
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
	bBottomLibraryCollapsed = !bBottomLibraryCollapsed;
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
