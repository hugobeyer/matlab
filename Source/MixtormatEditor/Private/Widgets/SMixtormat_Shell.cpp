// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatTabStrip.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Controls/MixtormatShellSplitterStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"


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
	LeftTabIndex = PageIndex;
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
						return bLeftPanelCollapsed ? LOCTEXT("ShowLayers", "Show Layers") : LOCTEXT("HideLayers", "Hide Layers");
					})
					.ToolTipText(LOCTEXT("ToggleLeftPanelHint", "Collapse or expand the Layers / Library panel (L)."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::ToggleLeftPanelCollapsed)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text_Lambda([this]()
					{
						return bInspectorCollapsed ? LOCTEXT("ShowInspector", "Show Inspector") : LOCTEXT("HideInspector", "Hide Inspector");
					})
					.ToolTipText(LOCTEXT("ToggleInspectorHint", "Collapse or expand the Inspector panel (P)."))
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
			SNew(SSplitter)
			.Style(&MixtormatShell::GetSplitterStyle())
			.PhysicalSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterVisualWidth)
			.HitDetectionSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth)
			+ SSplitter::Slot()
						.Value_Lambda([this]() { return bLeftPanelCollapsed ? 0.01f : ShellLeftFraction; })
						.OnSlotResized_Lambda([this](float Value)
						{
							// Keep expanded widths separate from the temporary collapsed arrangement.
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed && !bInspectorCollapsed)
							{
								ShellLeftFraction = Value;
							}
						})
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return bLeftPanelCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
				[BuildLeftPanel()]
			]
			+ SSplitter::Slot()
						.Value_Lambda([this]()
						{
							return ShellCenterFraction
								+ (bLeftPanelCollapsed ? ShellLeftFraction - 0.01f : 0.0f)
								+ (bInspectorCollapsed ? ShellRightFraction - 0.01f : 0.0f);
						})
						.OnSlotResized_Lambda([this](float Value)
						{
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed && !bInspectorCollapsed)
							{
								ShellCenterFraction = Value;
							}
						})
			[
				SNew(SSplitter)
				.Style(&MixtormatShell::GetSplitterStyle())
				.Orientation(Orient_Vertical)
				.PhysicalSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterVisualWidth)
				.HitDetectionSplitterHandleSize(FMixtormatThemeStore::GetResolved().ShellLayout.SplitterHitWidth)
				+ SSplitter::Slot()
					.Value_Lambda([this]()
					{
						return bBottomLibraryCollapsed
							? 0.99f
							: FMath::Clamp(PreviewHeightFraction, 0.2f, 0.95f);
					})
					.OnSlotResized_Lambda([this](float Value)
					{
						// SSplitter reports every slot's computed value, including the ones it works
						// out for itself while arranging. Echoing those back fought the arrangement
						// and is what collapsed the gallery on a rebuild; only a settled layout -- or
						// a real drag, which is the only thing that changes the value afterwards --
						// is allowed to become the new remembered split.
						if (!bSuppressSplitWriteBack && !bBottomLibraryCollapsed)
						{
							PreviewHeightFraction = FMath::Clamp(Value, 0.2f, 0.95f);
						}
					})
					[BuildPreviewPanel()]
				+ SSplitter::Slot()
					// Derived, not stored: the two slots must always sum to 1, and two independent
					// values are what let them disagree.
					.Value_Lambda([this]()
					{
						return bBottomLibraryCollapsed
							? 0.01f
							: 1.0f - FMath::Clamp(PreviewHeightFraction, 0.2f, 0.95f);
					})
					.OnSlotResized_Lambda([this](float Value)
					{
						// Dragging the lower handle moves this slot; the split is remembered from the
						// preview side so the stored value stays the one the panel is authored in.
						if (!bSuppressSplitWriteBack && !bBottomLibraryCollapsed)
						{
							PreviewHeightFraction = FMath::Clamp(1.0f - Value, 0.2f, 0.95f);
						}
					})
								[
									SNew(SOverlay)
									+ SOverlay::Slot()
									[
										SNew(SBox)
										.Visibility_Lambda([this]()
										{
											return bBottomLibraryCollapsed
												? EVisibility::Collapsed
												: EVisibility::Visible;
										})
										[BuildBottomLibrary()]
									]
									+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
									[
										SNew(SBox)
										.WidthOverride(MixtormatTokens::BottomLibraryCollapseButtonWidth)
										.HeightOverride(MixtormatTokens::BottomLibraryCollapseButtonHeight)
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SAssignNew(BottomLibraryToggleButton, SButton)
											.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.BottomLibraryCollapseButton")))
											.ContentPadding(0.0f)
											.ToolTipText(LOCTEXT("ToggleBottomLibraryHint", "Collapse or expand the material and mask galleries (G)."))
											.OnClicked(this, &SMixtormat::ToggleBottomLibraryCollapsed)
											[
												SNew(SBox)
												.WidthOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
													static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)].GlyphSize)
												.HeightOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
													static_cast<uint8>(Mixtormat::EMixtormatIconRole::GalleryToolbar)].GlyphSize)
												.HAlign(HAlign_Center)
												.VAlign(VAlign_Center)
												[
													SNew(SImage)
													.Image_Lambda([this]()
													{
														return bBottomLibraryCollapsed
															? MixtormatIcons::ChevronRight()
															: MixtormatIcons::ChevronDown();
													})
												]
											]
										]
									]
								]
			]
+ SSplitter::Slot()
						.Value_Lambda([this]() { return bInspectorCollapsed ? 0.01f : ShellRightFraction; })
						.OnSlotResized_Lambda([this](float Value)
						{
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed && !bInspectorCollapsed)
							{
								ShellRightFraction = Value;
							}
						})
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return bInspectorCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
				[BuildInspectorPanel()]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLeftPanel()
{
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SMixtormatTabStrip)
				.StretchTabs(true)
				.Options({ LOCTEXT("LayersLeftTab", "LAYERS"), LOCTEXT("LibraryLeftTab", "LIBRARY") })
				.ToolTips({
					LOCTEXT("LayersLeftTabHint", "The layer stack: layers, their masks, effects and filters."),
					LOCTEXT("LibraryLeftTabHint", "Saved mixes and imported user surfaces.") })
				.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
				.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
			]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(LeftSwitcher, SWidgetSwitcher)
				.WidgetIndex(LeftTabIndex)
				+ SWidgetSwitcher::Slot()[BuildLayerStackPanel()]
				+ SWidgetSwitcher::Slot()[BuildUserLibraryPage()]
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

FReply SMixtormat::ToggleLeftPanelCollapsed()
{
	if (bIsBaking)
	{
		return FReply::Handled();
	}
	bLeftPanelCollapsed = !bLeftPanelCollapsed;
	return FReply::Handled();
}

FReply SMixtormat::ToggleInspectorCollapsed()
{
	if (bIsBaking)
	{
		return FReply::Handled();
	}
	bInspectorCollapsed = !bInspectorCollapsed;
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
