// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Dialogs/SMixtormatBakeSettingsDialog.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace MixtormatBakeDialog
{
	// The fixed picker's own option set, in display order.
	// Matches EMixtormatBakeResolution in UMixtormatEditorSettings.
	constexpr int32 ResolutionOptions[] = {512, 1024, 2048, 4096};

	template <int32 N>
	int32 ValueToIndex(const int32 (&Options)[N], const int32 Value, const int32 DefaultIndex)
	{
		for (int32 Index = 0; Index < N; ++Index)
		{
			if (Options[Index] == Value)
			{
				return Index;
			}
		}
		return DefaultIndex;
	}
}

void SMixtormatBakeSettingsDialog::Construct(const FArguments& InArgs)
{
	Settings = InArgs._InitialSettings;
	ChildSlot
	[
		SNew(SBorder)
		.Padding(MixtormatTokens::DialogPadding)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BakeDestinationLabel", "Destination Folder"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SAssignNew(DestinationTextBox, SEditableTextBox)
					.Text(FText::FromString(Settings.DestinationPath))
					.OnTextChanged_Lambda([this](const FText& Text)
					{
						Settings.DestinationPath = Text.ToString();
						ValidationText = FText::GetEmpty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &SMixtormatBakeSettingsDialog::BuildPathPicker)
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("BrowseBakeDestination", "Browse..."))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BakeBaseNameLabel", "Output Base Name"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(Settings.BaseName))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					Settings.BaseName = Text.ToString();
					ValidationText = FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BakeSettingsSectionLabel", "Bake Settings"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogSectionGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f)
				[
					SNew(SBox).WidthOverride(MixtormatTokens::BakeDialogSettingLabelWidth)
					[
						SNew(STextBlock).Text(LOCTEXT("BakeResolutionLabel", "Resolution"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SMixtormatSegmentedControl)
					.Options({LOCTEXT("BakeRes512", "512"), LOCTEXT("BakeRes1024", "1024"), LOCTEXT("BakeRes2048", "2048"), LOCTEXT("BakeRes4096", "4096")})
					.ActiveIndex_Lambda([this]()
					{
						return MixtormatBakeDialog::ValueToIndex(MixtormatBakeDialog::ResolutionOptions, Settings.Resolution, 2);
					})
					.OnChosen_Lambda([this](const int32 Index)
					{
						Settings.Resolution = MixtormatBakeDialog::ResolutionOptions[Index];
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BakeOutputPreviewLabel", "Generated Asset Names"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogSectionGap)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::FromString(FString::Join(
						FMixtormatBakeService::GetOutputAssetNames(Settings),
						TEXT("\n")));
				})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return ValidationText; })
				.ColorAndOpacity(MixtormatPalette::ErrorText())
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelBakeSettings", "Cancel"))
					.OnClicked(this, &SMixtormatBakeSettingsDialog::Cancel)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AcceptBakeSettings", "Bake"))
					.OnClicked(this, &SMixtormatBakeSettingsDialog::Accept)
				]
			]
		]
	];
}

TSharedRef<SWidget> SMixtormatBakeSettingsDialog::BuildPathPicker()
{
	FPathPickerConfig Config;
	Config.DefaultPath = Settings.DestinationPath;
	Config.OnPathSelected = FOnPathSelected::CreateSP(
		this,
		&SMixtormatBakeSettingsDialog::SelectPath);
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	return SNew(SBox)
		.WidthOverride(360.0f)
		.HeightOverride(420.0f)
		[ContentBrowserModule.Get().CreatePathPicker(Config)];
}

void SMixtormatBakeSettingsDialog::SelectPath(const FString& Path)
{
	Settings.DestinationPath = Path;
	DestinationTextBox->SetText(FText::FromString(Path));
	ValidationText = FText::GetEmpty();
	FSlateApplication::Get().DismissAllMenus();
}

FReply SMixtormatBakeSettingsDialog::Accept()
{
	Settings.DestinationPath.TrimStartAndEndInline();
	Settings.BaseName.TrimStartAndEndInline();
	while (Settings.DestinationPath.RemoveFromEnd(TEXT("/"))) {}
	FText Error;
	if (!FMixtormatBakeService::ValidateSettings(Settings, Error))
	{
		ValidationText = Error;
		return FReply::Handled();
	}
	bAccepted = true;
	CloseWindow();
	return FReply::Handled();
}

FReply SMixtormatBakeSettingsDialog::Cancel()
{
	CloseWindow();
	return FReply::Handled();
}

void SMixtormatBakeSettingsDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
