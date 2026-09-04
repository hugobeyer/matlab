#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

// Window chrome: top bar, page routing, splitters, status bar.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::ShowPage(const int32 PageIndex)
{
	if (MainSwitcher.IsValid())
	{
		MainSwitcher->SetActiveWidgetIndex(PageIndex);
	}
	return FReply::Handled();
}

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
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const int32 Resolutions[] = {1024, 2048, 4096};
	for (const int32 Resolution : Resolutions)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(FText::Format(
				LOCTEXT("CompositionResolutionOption", "{0}K ({1} × {1})"),
				FText::AsNumber(Resolution / 1024),
				FText::AsNumber(Resolution)))
			.OnClicked(this, &SMixtormat::SetCompositionResolution, Resolution)
		];
	}
	return SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
		[Menu];
}

TSharedRef<SWidget> SMixtormat::BuildTopBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBox)
		.HeightOverride(MixtormatUI::TopBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(8.0f, 3.0f))
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.TopBar")))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SImage)
					.Image(Style.GetBrush(TEXT("Mixtormat.Brand.Logo")))
					.ToolTipText(LOCTEXT("BrandTooltip", "Mixtormat"))
				]
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Save")))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "SAVE"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMixtormat::SaveWorkingMaterialAs)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.SaveAs")))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveAsTop", "SAVE AS..."))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
					.IsEnabled_Lambda([this]() { return WorkingMaterialAsset.IsValid() && bHasWorkingMaterial; })
					.Text(LOCTEXT("BakeMaterialTop", "BAKE"))
					.ToolTipText(LOCTEXT("BakeMaterialHint", "Bake the current GPU-composited BC, Normal, and RAM outputs."))
					.OnClicked(this, &SMixtormat::BakeWorkingMaterial)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
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
			.PhysicalSplitterHandleSize(MixtormatUI::SplitterHandleSize)
			.HitDetectionSplitterHandleSize(MixtormatUI::SplitterHitSize)
			+ SSplitter::Slot().Value(0.19f)
			[
				BuildLeftPanel()
			]
			+ SSplitter::Slot().Value(0.60f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)
				.PhysicalSplitterHandleSize(MixtormatUI::SplitterHandleSize)
				.HitDetectionSplitterHandleSize(MixtormatUI::SplitterHitSize)
				+ SSplitter::Slot().Value(0.64f)[BuildPreviewPanel()]
				+ SSplitter::Slot().Value(0.36f)[BuildBottomLibrary()]
			]
			+ SSplitter::Slot().Value(0.21f)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.0f, 4.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(88.0f)
					[
						SNew(SBorder)
						.Padding(0.0f)
						.BorderImage_Lambda([this]() { return FMixtormatStyle::Get().GetBrush(LeftTabIndex == 0 ? TEXT("Mixtormat.InsetPanel") : TEXT("Mixtormat.SectionBar")); })
						[
					SNew(SCheckBox)
					.Style(&Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.TabToggle")))
					.IsChecked_Lambda([this]() { return LeftTabIndex == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ShowLeftPage(0); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("LayersLeftTab", "LAYERS")).Justification(ETextJustify::Center)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[SNew(SBox).HeightOverride(2.0f)[SNew(SBorder).BorderImage_Lambda([this]() { return FMixtormatStyle::Get().GetBrush(LeftTabIndex == 0 ? TEXT("Mixtormat.TabUnderlineSelected") : TEXT("Mixtormat.TabUnderline")); })]]
					]
				]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(88.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage_Lambda([this]() { return FMixtormatStyle::Get().GetBrush(LeftTabIndex == 1 ? TEXT("Mixtormat.InsetPanel") : TEXT("Mixtormat.SectionBar")); })
					[
					SNew(SCheckBox)
					.Style(&Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.TabToggle")))
					.IsChecked_Lambda([this]() { return LeftTabIndex == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ShowLeftPage(1); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("LibraryLeftTab", "LIBRARY")).Justification(ETextJustify::Center)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[SNew(SBox).HeightOverride(2.0f)[SNew(SBorder).BorderImage_Lambda([this]() { return FMixtormatStyle::Get().GetBrush(LeftTabIndex == 1 ? TEXT("Mixtormat.TabUnderlineSelected") : TEXT("Mixtormat.TabUnderline")); })]]
					]
				]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HeightOverride(1.0f)
				[SNew(SBorder).BorderImage(Style.GetBrush(TEXT("Mixtormat.TabUnderline")))]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(LeftSwitcher, SWidgetSwitcher)
				.WidgetIndex(0)
				+ SWidgetSwitcher::Slot()[BuildLayerStackPanel()]
				+ SWidgetSwitcher::Slot()[BuildLibraryPage()]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildStatusBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBox)
		.HeightOverride(MixtormatUI::StatusBarHeight)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("AuthoringMenu", "Material Authoring"), 0)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("MixerMenu", "Mixer (Legacy)"), 1)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("PresetsMenu", "Presets"), 2)]
		];
}

TSharedRef<SWidget> SMixtormat::BuildNavButton(const FText& Label, const int32 PageIndex)
{
	return SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
		.OnClicked_Lambda([this, PageIndex]() { return ShowPage(PageIndex); })
		[SNew(STextBlock).Text(Label)];
}

TSharedRef<SWidget> SMixtormat::BuildWorkspacePage(const FText& Heading, const FText& Description)
{
	return SNew(SBorder)
		.Padding(MixtormatUI::PanelPadding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Heading).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 14.0f)[SNew(STextBlock).Text(Description).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("WetnessLayer", "Wetness · 100%"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)[SNew(SButton).Text(LOCTEXT("RustLayer", "Rust · 72%"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("BaseLayer", "Base Steel · 100%"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)
						[
							SNew(SButton)
							.IsEnabled(false)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")))]
								+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
								[SNew(STextBlock).Text(LOCTEXT("AddLayer", "Add Layer"))]
							]
						]
		];
}

TSharedRef<SWidget> SMixtormat::BuildPresetsPage()
{
	return SNew(SBorder)
		.Padding(24.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("PresetsHeading", "LOOK PRESETS")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(STextBlock).Text(LOCTEXT("PresetsDescription", "Saved Mixtormat looks will appear here for editor and runtime use.")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f)[SNew(SButton).Text(LOCTEXT("CreatePreset", "Create Preset from Current Look")).IsEnabled(false)]
		];
}

#undef LOCTEXT_NAMESPACE
