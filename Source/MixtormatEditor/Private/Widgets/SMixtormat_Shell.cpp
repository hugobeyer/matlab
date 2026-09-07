#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Controls/SMixtormatTabStrip.h"


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

TSharedRef<SWidget> SMixtormat::BuildCompositionResolutionMenu()
{
	MixtormatMenu::FBuilder Menu;
	const int32 Resolutions[] = {1024, 2048, 4096};
	for (const int32 Resolution : Resolutions)
	{
		Menu.Item(
			FText::Format(
				LOCTEXT("CompositionResolutionOption", "{0}K ({1} × {1})"),
				FText::AsNumber(Resolution / 1024),
				FText::AsNumber(Resolution)),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Resolution]() { SetCompositionResolution(Resolution); }))
			.Checked(TAttribute<bool>::CreateLambda([this, Resolution]()
			{
				return CompositionResolution == Resolution;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildTopBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBox)
		.HeightOverride(MixtormatTokens::TopBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(8.0f, 3.0f))
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.TopBar")))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !UndoHistory.IsEmpty(); })
					.Text(LOCTEXT("UndoMaterialEditCompact", "Undo"))
					.ToolTipText(LOCTEXT("UndoMaterialEditHint", "Undo the last Mixtormat recipe edit (Ctrl+Z)."))
					.OnClicked(this, &SMixtormat::UndoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !RedoHistory.IsEmpty(); })
					.Text(LOCTEXT("RedoMaterialEditCompact", "Redo"))
					.ToolTipText(LOCTEXT("RedoMaterialEditHint", "Redo the last Mixtormat recipe edit (Ctrl+Y or Ctrl+Shift+Z)."))
					.OnClicked(this, &SMixtormat::RedoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(WorkingMaterialName); })
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
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text(LOCTEXT("NewMaterialTop", "NEW"))
					.ToolTipText(LOCTEXT("NewMaterialTopHint", "Start a new material workspace, confirming unsaved changes first."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::NewWorkingMaterial)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
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
								SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Folder")))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("LoadMaterialTop", "LOAD"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
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
								SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Save")))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "SAVE"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
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
								SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.SaveAs")))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAsTop", "SAVE AS..."))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Text(LOCTEXT("OpenLiveTheme", "UI STYLE"))
					.ToolTipText(LOCTEXT("OpenLiveThemeHint", "Developer popup: edit shared UI spacing, sizes, typography and colors live."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::OpenLiveThemePanel)
				]
				// Keeps PrimaryButton -- it is the one committing action up here and the accent is
				// deliberate -- but everything else about it now matches its neighbours: the
				// toolbar margin token rather than a hand-written 6/2, and the same icon-then-
				// label body that LOAD, SAVE and SAVE AS use, so the row scans as one set.
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
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
								SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Cube")))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("BakeMaterialTop", "BAKE"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ToolbarButtonMargin, 0.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.HasDownArrow(false)
					.ButtonContent()[SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Overflow")))]
					.OnGetMenuContent(this, &SMixtormat::BuildWorkflowMenu)
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
				.Orientation(Orient_Vertical)
				.PhysicalSplitterHandleSize(MixtormatTokens::SplitterHandleSize)
				.HitDetectionSplitterHandleSize(MixtormatTokens::SplitterHitSize)
				+ SSplitter::Slot()
								.Value_Lambda([this]() { return PreviewHeightFraction; })
								.OnSlotResized_Lambda([this](float Value) { PreviewHeightFraction = Value; })
								[BuildPreviewPanel()]
				+ SSplitter::Slot()
								.Value_Lambda([this]() { return LibraryHeightFraction; })
								.OnSlotResized_Lambda([this](float Value) { LibraryHeightFraction = Value; })
								[BuildBottomLibrary()]
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
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(MixtormatTokens::PanelPadding, MixtormatTokens::PanelPadding, MixtormatTokens::PanelPadding, 0.0f)
			[
				SNew(SMixtormatTabStrip)
				.Options({ LOCTEXT("LayersLeftTab", "LAYERS"), LOCTEXT("LibraryLeftTab", "LIBRARY") })
				.ToolTips({
					LOCTEXT("LayersLeftTabHint", "The layer stack: layers, their masks, effects and filters."),
					LOCTEXT("LibraryLeftTabHint", "Search and filter the surface library by category.") })
				.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
				.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
			]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(LeftSwitcher, SWidgetSwitcher)
				.WidgetIndex(LeftTabIndex)
				+ SWidgetSwitcher::Slot()[BuildLayerStackPanel()]
				+ SWidgetSwitcher::Slot()[BuildLibraryPage()]
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
			.Padding(FMargin(6.0f, 2.0f))
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.TopBar")))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(WorkingStatusText); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const FText QualityText = PreviewQuality == EMixtormatPreviewQuality::High
							? LOCTEXT("StatusQualityHigh", "High · Lumen")
							: PreviewQuality == EMixtormatPreviewQuality::Medium
								? LOCTEXT("StatusQualityMedium", "Medium")
								: LOCTEXT("StatusQualityLow", "Low");
						return FText::Format(LOCTEXT("RealtimeStatusDynamic", "Real-time Preview · {0} · SM6"), QualityText);
					})
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))
				]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("LayerStatus", "Layers {0}"), FText::AsNumber(WorkingLayers.Num())); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildWorkflowMenu()
{
	return SNew(SBorder)
		.Padding(6.0f)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
				.Text(LOCTEXT("NewMaterialMenu", "New Material"))
				.OnClicked(this, &SMixtormat::NewWorkingMaterial)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
				.Text(LOCTEXT("OpenMaterialMenu", "Open Material..."))
				.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
			]
		];
}

#undef LOCTEXT_NAMESPACE
