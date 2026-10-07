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
	// Detach the old tree before creating the single replacement inspector on theme refresh.
	if (InspectorDockHost.IsValid()) InspectorDockHost->SetContent(SNullWidget::NullWidget);
	if (InspectorOverlayHost.IsValid()) InspectorOverlayHost->SetContent(SNullWidget::NullWidget);
	InspectorPanel = BuildInspectorPanel();
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
							// Keep expanded widths separate from the temporary collapsed arrangement. The
							// inspector column is not one of those: while it is away its share is carried by
							// the centre slot, so this value is still a fraction of the full width and the
							// Layers column stays resizable in every placement.
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed)
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
							// While the inspector column is away the centre slot carries its share, so the
							// splitter still divides the full width and a drag on the left handle reports
							// values in the units the expanded layout stores.
							return ShellCenterFraction
								+ (bLeftPanelCollapsed ? ShellLeftFraction - 0.01f : 0.0f)
								+ (bInspectorCollapsed ? ShellRightFraction : 0.0f);
						})
						.OnSlotResized_Lambda([this](float Value)
						{
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed)
							{
								// Hand the inspector's share back before storing, so the column returns
								// to its own width rather than the borrowed one.
								ShellCenterFraction = bInspectorCollapsed
									? FMath::Max(0.0f, Value - ShellRightFraction)
									: Value;
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
						// Zero, not a sliver: a collapsed slot is skipped by the splitter, so this keeps
						// the left and centre coefficients summing to one while the column is away.
						.Value_Lambda([this]() { return bInspectorCollapsed ? 0.0f : ShellRightFraction; })
						.OnSlotResized_Lambda([this](float Value)
						{
							if (!bSuppressSplitWriteBack && !bLeftPanelCollapsed && !bInspectorCollapsed)
							{
								ShellRightFraction = Value;
							}
						})
			[
				SAssignNew(InspectorDockHost, SBox)
				.Visibility_Lambda([this]() { return bInspectorCollapsed ? EVisibility::Collapsed : EVisibility::Visible; })
				[InspectorPlacement == EInspectorPlacement::Overlay ? SNullWidget::NullWidget : InspectorPanel.ToSharedRef()]
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
				.Options({ LOCTEXT("LayersLeftTab", "LAYERS"), LOCTEXT("LibraryLeftTab", "LIBRARY"), LOCTEXT("GlobalLeftTab", "GLOBAL") })
				.ToolTips({
					LOCTEXT("LayersLeftTabHint", "The layer stack: layers, their masks, effects and filters."),
					LOCTEXT("LibraryLeftTabHint", "Saved mixes and imported user surfaces."),
					LOCTEXT("GlobalLeftTabHint", "Document-wide variables and settings.") })
				.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
				.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
			]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(LeftSwitcher, SWidgetSwitcher)
				.WidgetIndex(LeftTabIndex)
				+ SWidgetSwitcher::Slot()[BuildLayerStackPanel()]
				+ SWidgetSwitcher::Slot()[BuildUserLibraryPage()]
				+ SWidgetSwitcher::Slot()[BuildGlobalPage()]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildGlobalPage()
{
	return SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		+ SScrollBox::Slot()
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.GroupOuterGap, 0.0f))
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
					Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(0.5f)))
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
	switch (InspectorPlacement)
		{
		case EInspectorPlacement::Docked: InspectorPlacement = EInspectorPlacement::Overlay; break;
		case EInspectorPlacement::Overlay: InspectorPlacement = EInspectorPlacement::Hidden; break;
		case EInspectorPlacement::Hidden: InspectorPlacement = EInspectorPlacement::Docked; break;
		}
		bInspectorCollapsed = InspectorPlacement != EInspectorPlacement::Docked;
		if (InspectorPlacement == EInspectorPlacement::Overlay)
		{
			// First entry places it flush right and full height, where the docked column was; after
			// that the user's own geometry stands, re-clamped in case the panel has shrunk since.
			EnsureInspectorOverlayPlaced();
			ClampInspectorOverlay();
		}
		// Remove the old parent first; no rebuild means scroll and expansion state stay intact.
		InspectorDockHost->SetContent(SNullWidget::NullWidget);
		InspectorOverlayHost->SetContent(SNullWidget::NullWidget);
		(InspectorPlacement == EInspectorPlacement::Overlay ? InspectorOverlayHost : InspectorDockHost)
			->SetContent(InspectorPanel.ToSharedRef());
		return FReply::Handled();
	}

	void SMixtormat::EnsureInspectorOverlayPlaced()
	{
		if (bInspectorOverlayPlaced)
		{
			return;
		}
		// The viewport fills the preview panel, so its size is the space the overlay floats in. Read
		// once: after this the user's own corner stands, and the next P press re-clamps it.
		const FVector2D Bounds = GetInspectorOverlayBounds();
		InspectorOverlaySize = FVector2D(
			MixtormatTokens::InspectorWidth,
			Bounds.Y > 0.0f ? Bounds.Y : MixtormatTokens::InspectorWidth);
		InspectorOverlayPosition = FVector2D(FMath::Max(0.0f, Bounds.X - InspectorOverlaySize.X), 0.0f);
		bInspectorOverlayPlaced = true;
	}

	FVector2D SMixtormat::GetInspectorOverlayBounds() const
	{
		return PreviewViewports.IsValidIndex(0) && PreviewViewports[0].IsValid()
			? PreviewViewports[0]->GetCachedGeometry().GetLocalSize()
			: FVector2D::ZeroVector;
	}

	void SMixtormat::ClampInspectorOverlay()
	{
		const FVector2D Bounds = GetInspectorOverlayBounds();
		if (Bounds.X <= 0.0f || Bounds.Y <= 0.0f)
		{
			return;
		}
		InspectorOverlaySize = FVector2D(
			FMath::Clamp(InspectorOverlaySize.X, MixtormatTokens::InspectorOverlayMinWidth,
				FMath::Max(MixtormatTokens::InspectorOverlayMinWidth, Bounds.X)),
			FMath::Clamp(InspectorOverlaySize.Y, MixtormatTokens::InspectorOverlayMinHeight,
				FMath::Max(MixtormatTokens::InspectorOverlayMinHeight, Bounds.Y)));
		InspectorOverlayPosition = FVector2D(
			FMath::Clamp(InspectorOverlayPosition.X, 0.0f, FMath::Max(0.0f, Bounds.X - InspectorOverlaySize.X)),
			FMath::Clamp(InspectorOverlayPosition.Y, 0.0f, FMath::Max(0.0f, Bounds.Y - InspectorOverlaySize.Y)));
	}

	FReply SMixtormat::BeginInspectorOverlayInteraction(const FVector2D& ScreenPosition, const bool bResize)
	{
		bInspectorOverlayResizing = bResize;
		bInspectorOverlayDragging = !bResize;
		InspectorOverlayDragOrigin = ScreenPosition;
		InspectorOverlayPositionAtDragStart = InspectorOverlayPosition;
		InspectorOverlaySizeAtDragStart = InspectorOverlaySize;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	void SMixtormat::UpdateInspectorOverlayInteraction(const FVector2D& ScreenPosition)
	{
		const FVector2D Delta = ScreenPosition - InspectorOverlayDragOrigin;
		if (bInspectorOverlayResizing)
		{
			// The grip is the bottom-left corner, so the right edge stays where the user put it.
			const FVector2D Size = InspectorOverlaySizeAtDragStart + FVector2D(-Delta.X, Delta.Y);
			InspectorOverlaySize = FVector2D(
				FMath::Max(Size.X, MixtormatTokens::InspectorOverlayMinWidth),
				FMath::Max(Size.Y, MixtormatTokens::InspectorOverlayMinHeight));
			InspectorOverlayPosition.X = InspectorOverlayPositionAtDragStart.X
				+ (InspectorOverlaySizeAtDragStart.X - InspectorOverlaySize.X);
		}
		else
		{
			InspectorOverlayPosition = InspectorOverlayPositionAtDragStart + Delta;
		}
		ClampInspectorOverlay();
	}

	FReply SMixtormat::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (InspectorPlacement == EInspectorPlacement::Overlay && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			const FVector2D ScreenPosition = MouseEvent.GetScreenSpacePosition();
			// The grip wins where the two overlap: it sits in the corner the header also covers.
			if (const TSharedPtr<SWidget> Grip = InspectorResizeGrip.Pin();
				Grip.IsValid() && Grip->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				return BeginInspectorOverlayInteraction(ScreenPosition, true);
			}
			if (const TSharedPtr<SWidget> Header = InspectorIdentityRow.Pin();
				Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				return BeginInspectorOverlayInteraction(ScreenPosition, false);
			}
		}
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	FReply SMixtormat::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!bInspectorOverlayDragging && !bInspectorOverlayResizing)
		{
			return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
		}
		UpdateInspectorOverlayInteraction(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}

	FReply SMixtormat::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!bInspectorOverlayDragging && !bInspectorOverlayResizing)
		{
			return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
		}
		bInspectorOverlayDragging = false;
		bInspectorOverlayResizing = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	void SMixtormat::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
	{
		// Alt-tab or a modal mid-drag: drop the interaction rather than follow a mouse that is gone.
		bInspectorOverlayDragging = false;
		bInspectorOverlayResizing = false;
		SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	}

	FCursorReply SMixtormat::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		if (InspectorPlacement == EInspectorPlacement::Overlay)
		{
			const FVector2D ScreenPosition = CursorEvent.GetScreenSpacePosition();
			if (const TSharedPtr<SWidget> Grip = InspectorResizeGrip.Pin();
				Grip.IsValid() && Grip->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
			}
			if (const TSharedPtr<SWidget> Header = InspectorIdentityRow.Pin();
				Header.IsValid() && Header->GetCachedGeometry().IsUnderLocation(ScreenPosition))
			{
				return FCursorReply::Cursor(EMouseCursor::GrabHand);
			}
		}
		return SCompoundWidget::OnCursorQuery(MyGeometry, CursorEvent);
	}


#undef LOCTEXT_NAMESPACE
