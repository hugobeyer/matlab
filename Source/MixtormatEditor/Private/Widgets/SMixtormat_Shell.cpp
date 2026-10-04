// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatTabStrip.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "Brushes/SlateColorBrush.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"


namespace
{
	const FSplitterStyle& ShellSplitterStyle()
	{
		// SSplitter retains the style address; refresh this stable storage with each shell rebuild.
		static FSplitterStyle Style;
		Style.SetHandleNormalBrush(FSlateColorBrush(MixtormatPalette::FoldoutHairline()));
		Style.SetHandleHighlightBrush(FSlateColorBrush(MixtormatPalette::HairlineHover()));
		return Style;
	}

	using SMixtormatShellAction = SMixtormatGroupAction;
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
	const bool bHasDeveloperSources =
		!FMixtormatSurfaceImporter::EnumerateShippedSourceDirectories().IsEmpty();
	return SNew(SBox)
		.HeightOverride(MixtormatTokens::TopBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(MixtormatTokens::PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor_Lambda([]() { return MixtormatPalette::Ground(); })
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !UndoHistory.IsEmpty(); })
					.Text(LOCTEXT("UndoMaterialEditCompact", "Undo"))
					.ToolTipText(LOCTEXT("UndoMaterialEditHint", "Undo the last Mixtormat recipe edit (Ctrl+Z)."))
					.OnClicked(this, &SMixtormat::UndoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !RedoHistory.IsEmpty(); })
					.Text(LOCTEXT("RedoMaterialEditCompact", "Redo"))
					.ToolTipText(LOCTEXT("RedoMaterialEditHint", "Redo the last Mixtormat recipe edit (Ctrl+Y or Ctrl+Shift+Z)."))
					.OnClicked(this, &SMixtormat::RedoMaterialEdit)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(MixtormatTokens::PanelPadding, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(WorkingMaterialName); })
					.Clipping(EWidgetClipping::ClipToBounds)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]() { return bIsWorkingMaterialDirty ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text(LOCTEXT("WorkingMaterialEdited", "EDITED"))
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.ColorAndOpacity(FSlateColor(MixtormatPalette::Modified()))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("NewMaterialTopHint", "Start a new material workspace, confirming unsaved changes first."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::NewWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Add())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("NewMaterialTop", "NEW"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Folder())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("LoadMaterialTop", "LOAD"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Save())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "SAVE"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterialAs)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::SaveAs())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAsTop", "SAVE AS..."))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.Visibility(bHasDeveloperSources ? EVisibility::Visible : EVisibility::Collapsed)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text(LOCTEXT("OpenLiveTheme", "UI STYLE"))
					.ToolTipText(LOCTEXT("OpenLiveThemeHint", "Developer popup: edit shared UI spacing, sizes, typography and colors live."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::OpenLiveThemePanel)
				]
				// Bake is a peer toolbar action, so it uses the same tokenized button, spacing,
				// icon and label structure as New, Load, Save and Save As.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return WorkingMaterialAsset.IsValid() && bHasWorkingMaterial; })
					.ToolTipText(LOCTEXT("BakeMaterialHint", "Bake the current GPU-composited BC, Normal, and RAM outputs."))
					.OnClicked(this, &SMixtormat::BakeWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Cube())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("BakeMaterialTop", "BAKE"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("DocumentationTopHint", "Open Mixtormat documentation."))
					.OnClicked(this, &SMixtormat::OpenDocumentation)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Documentation())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("DocumentationTop", "DOCS"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatShellAction, false)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormat::OpenSettings)
					.ToolTipText(LOCTEXT("SettingsTopHint", "Open Mixtormat settings."))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::ToolbarIconSize)
							.HeightOverride(MixtormatTokens::ToolbarIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Settings())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SettingsTop", "SETTINGS"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]

			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildAuthoringPage()
{
	return SNew(SBorder)
		.Padding(0.0f)
		.IsEnabled_Lambda([this]() { return !bIsBaking; })
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Window")))
		[
			SNew(SSplitter)
			.Style(&ShellSplitterStyle())
			.PhysicalSplitterHandleSize(MixtormatTokens::SplitterHandleSize)
			.HitDetectionSplitterHandleSize(MixtormatTokens::SplitterHitSize)
			+ SSplitter::Slot()
						.Value_Lambda([this]() { return ShellLeftFraction; })
						.OnSlotResized_Lambda([this](float Value) { ShellLeftFraction = Value; })
			[
				BuildLeftPanel()
			]
			+ SSplitter::Slot()
						.Value_Lambda([this]() { return ShellCenterFraction; })
						.OnSlotResized_Lambda([this](float Value) { ShellCenterFraction = Value; })
			[
				SNew(SSplitter)
				.Style(&ShellSplitterStyle())
				.Orientation(Orient_Vertical)
				.PhysicalSplitterHandleSize(MixtormatTokens::SplitterHandleSize)
				.HitDetectionSplitterHandleSize(MixtormatTokens::SplitterHitSize)
				+ SSplitter::Slot()
								.Value_Lambda([this]() { return bBottomLibraryCollapsed ? 0.99f : PreviewHeightFraction; })
								.OnSlotResized_Lambda([this](float Value)
								{
									if (!bBottomLibraryCollapsed)
									{
										PreviewHeightFraction = Value;
									}
								})
								[BuildPreviewPanel()]
				+ SSplitter::Slot()
								.Value_Lambda([this]() { return bBottomLibraryCollapsed ? 0.01f : LibraryHeightFraction; })
								.OnSlotResized_Lambda([this](float Value)
								{
									if (!bBottomLibraryCollapsed)
									{
										LibraryHeightFraction = Value;
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
										.RenderTransform(FSlateRenderTransform(FVector2D(
											0.0f,
											-MixtormatTokens::SplitterHandleSize)))
										[
											SAssignNew(BottomLibraryToggleButton, SButton)
											.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.BottomLibraryCollapseButton")))
											.ContentPadding(0.0f)
											.ToolTipText(LOCTEXT("ToggleBottomLibraryHint", "Collapse or expand the material and mask galleries (G)."))
											.OnClicked(this, &SMixtormat::ToggleBottomLibraryCollapsed)
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
												[
													SNew(STextBlock)
													.Text(LOCTEXT("BottomLibraryToggleLabel", "Gallery"))
													.Visibility(EVisibility::HitTestInvisible)
												]
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
												[
													SNew(SImage)
													.Visibility(EVisibility::HitTestInvisible)
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
						.Value_Lambda([this]() { return ShellRightFraction; })
						.OnSlotResized_Lambda([this](float Value) { ShellRightFraction = Value; })
			[
				BuildInspectorPanel()
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLeftPanel()
{
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor_Lambda([]() { return MixtormatPalette::Ground(); })
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
	return SNew(SBox)
		.HeightOverride(MixtormatTokens::StatusBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(MixtormatTokens::PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor_Lambda([]() { return MixtormatPalette::Ground(); })
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(WorkingStatusText); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))]
					+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const FText QualityText = PreviewQuality == EMixtormatPreviewQuality::Lumen
								? LOCTEXT("StatusQualityLumen", "Lumen On")
								: LOCTEXT("StatusQualityDefault", "Default · Studio AO");
							return FText::Format(LOCTEXT("RealtimeStatusDynamic", "Real-time Preview · {0} · SM6"), QualityText);
						})
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))
					]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("LayerStatus", "Layers {0}"), FText::AsNumber(WorkingLayers.Num())); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))]
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
