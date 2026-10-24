// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Dialogs/SMixtormatBakeSettingsDialog.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"

#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
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
	RefreshDiscoveredParameters();
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle LabelStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const FTextBlockStyle MutedLabelStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const FTextBlockStyle CaptionStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Caption),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const FTextBlockStyle BodyStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const FTextBlockStyle ErrorStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Error));

	ChildSlot
	[
		SNew(SBorder)
		.Padding(Resolved.ControlLayout.DialogPadding)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Window")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.ScrollBarStyle(&Style.GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
				.ScrollBarThickness(FVector2D(Resolved.ShellLayout.ScrollbarThickness))
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)

					// CARD 1: OUTPUT DESTINATION & NAMING
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
					[
						SNew(SMixtormatInspectorCard)
						.Title(LOCTEXT("BakeOutputSectionTitle", "OUTPUT DESTINATION & NAMING"))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("BakeDestinationLabel", "Destination Folder"))
									.Font(MutedLabelStyle.Font)
									.ColorAndOpacity(MutedLabelStyle.ColorAndOpacity)
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f)
									[
										SNew(SMixtormatTextFieldGradient)
										[
											SNew(SBox)
											.MinDesiredHeight(FMixtormatThemeStore::GetTheme().TextField.MinHeight)
											.MinDesiredWidth(FMixtormatThemeStore::GetTheme().TextField.MinWidth)
											[
												SAssignNew(DestinationTextBox, SEditableTextBox)
												.Style(&Style.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
												.Text(FText::FromString(Settings.DestinationPath))
												.OnTextChanged_Lambda([this](const FText& Text)
												{
													Settings.DestinationPath = Text.ToString();
													ValidationText = FText::GetEmpty();
												})
											]
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f, 0.0f, 0.0f)
									[
										SNew(SComboButton)
										.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
										.OnGetMenuContent(this, &SMixtormatBakeSettingsDialog::BuildPathPicker)
										.ButtonContent()
										[
											SNew(SHorizontalBox)
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f)
											[
												SNew(SBox)
												.WidthOverride(MixtormatTokens::BakeDialogBrowseGlyphSize)
												.HeightOverride(MixtormatTokens::BakeDialogBrowseGlyphSize)
												[
													SNew(SImage)
													.Image(MixtormatIcons::Folder())
													.ColorAndOpacity(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text))
												]
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
											[
												SNew(STextBlock)
												.Text(LOCTEXT("BrowseBakeDestination", "Browse..."))
												.Font(LabelStyle.Font)
												.ColorAndOpacity(LabelStyle.ColorAndOpacity)
												.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
											]
										]
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("BakeBaseNameLabel", "Output Base Name"))
									.Font(MutedLabelStyle.Font)
									.ColorAndOpacity(MutedLabelStyle.ColorAndOpacity)
									.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SMixtormatTextFieldGradient)
									[
										SNew(SBox)
										.MinDesiredHeight(FMixtormatThemeStore::GetTheme().TextField.MinHeight)
										.MinDesiredWidth(FMixtormatThemeStore::GetTheme().TextField.MinWidth)
										[
											SNew(SEditableTextBox)
											.Style(&Style.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
											.Text(FText::FromString(Settings.BaseName))
											.OnTextChanged_Lambda([this](const FText& Text)
											{
												Settings.BaseName = Text.ToString();
												ValidationText = FText::GetEmpty();
											})
										]
									]
								]
							]
						]
					]

					// CARD 2: BAKE RESOLUTION
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
					[
						SNew(SMixtormatInspectorCard)
						.Title(LOCTEXT("BakeSettingsSectionLabel", "RESOLUTION"))
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

					// CARD 3: MATERIAL PARENT & BINDINGS
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
					[
						SNew(SMixtormatInspectorCard)
						.Title(LOCTEXT("BakeMaterialParentTitle", "MATERIAL PARENT"))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
							[
								SNew(SMixtormatSegmentedControl)
								.Options({LOCTEXT("ParentModeDefault", "Mixtormat Default"), LOCTEXT("ParentModeCustom", "Custom Material")})
								.ActiveIndex_Lambda([this]()
								{
									return Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Custom ? 1 : 0;
								})
								.OnChosen_Lambda([this](const int32 Index)
								{
									Settings.MaterialSettings.ParentMode = Index == 1
										? EMixtormatBakeMaterialParentMode::Custom
										: EMixtormatBakeMaterialParentMode::Default;
									if (Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Custom && DiscoveredParameters.IsEmpty())
									{
										RefreshDiscoveredParameters();
									}
									ValidationText = FText::GetEmpty();
								})
							]

							// Default Mode Note
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox)
								.Visibility_Lambda([this]()
								{
									return Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Default
										? EVisibility::Visible : EVisibility::Collapsed;
								})
								[
									SNew(STextBlock)
									.Text(LOCTEXT("DefaultMasterInfo", "Bakes to standard Substrate master with DA_BaseColor, DA_Normal, DA_RAMH, and DA_Height parameters."))
									.Font(BodyStyle.Font)
									.ColorAndOpacity(BodyStyle.ColorAndOpacity)
									.AutoWrapText(true)
								]
							]

							// Custom Material Controls
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SVerticalBox)
								.Visibility_Lambda([this]()
								{
									return Settings.MaterialSettings.ParentMode == EMixtormatBakeMaterialParentMode::Custom
										? EVisibility::Visible : EVisibility::Collapsed;
								})
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("CustomMasterLabel", "Master Material"))
										.Font(MutedLabelStyle.Font)
										.ColorAndOpacity(MutedLabelStyle.ColorAndOpacity)
										.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
									]
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SObjectPropertyEntryBox)
										.AllowedClass(UMaterialInterface::StaticClass())
										.DisplayThumbnail(false)
										.AllowClear(true)
										.ObjectPath_Lambda([this]()
										{
											return Settings.MaterialSettings.CustomMaster.ToString();
										})
										.OnObjectChanged(this, &SMixtormatBakeSettingsDialog::OnCustomMasterChanged)
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogMappingCaptionGap)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("TextureMappingsCaption", "TEXTURE PARAMETER MAPPINGS"))
									.Font(CaptionStyle.Font)
									.ColorAndOpacity(CaptionStyle.ColorAndOpacity)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
								[
									BuildParameterMappingDropdown(Settings.MaterialSettings.BaseColorMapping, LOCTEXT("MapBC", "Base Color"))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
								[
									BuildParameterMappingDropdown(Settings.MaterialSettings.NormalMapping, LOCTEXT("MapNormal", "Normal"))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogFieldTopMargin)
								[
									BuildParameterMappingDropdown(Settings.MaterialSettings.RAMMapping, LOCTEXT("MapRAM", "Packed RAM"))
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									BuildParameterMappingDropdown(Settings.MaterialSettings.HeightMapping, LOCTEXT("MapHeight", "Height"))
								]
							]
						]
					]

					// CARD 4: GENERATED ASSETS SUMMARY
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SMixtormatInspectorCard)
						.Title(LOCTEXT("BakeOutputPreviewLabel", "GENERATED ASSETS SUMMARY"))
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								const TArray<FString> Names = FMixtormatBakeService::GetOutputAssetNames(Settings);
								if (Names.Num() != 10)
								{
									return FText::FromString(FString::Join(Names, TEXT("\n")));
								}
								return FText::Format(
									LOCTEXT("BakeOpticalAssetSummaryFmt",
										"{0}  (Base Color · sRGB)\n"
										"{1}  (Normal Map · Tangent)\n"
										"{2}  (Packed RAM · Roughness/AO/Metallic/F0)\n"
										"{3}  (Height · 16-bit Grayscale)\n"
										"{4}  (Material Instance)\n"
										"{5}  (Surface RAMH · Height in Alpha)\n"
										"{6}  (Mixtormat Surface Asset)\n"
																				"{7}  (Optics · F0/Fuzz/Roughness/SSS · Linear Float)\n"
																				"{8}  (Fuzz Tint · Linear Float RGB)\n"
																				"{9}  (Subsurface MFP · Centimeters · HDR Float)"),
									FText::FromString(Names[0]),
									FText::FromString(Names[1]),
									FText::FromString(Names[2]),
									FText::FromString(Names[3]),
									FText::FromString(Names[4]),
									FText::FromString(Names[5]),
									FText::FromString(Names[6]),
																		FText::FromString(Names[7]),
																		FText::FromString(Names[8]),
																		FText::FromString(Names[9]));
							})
							.Font(BodyStyle.Font)
							.ColorAndOpacity(BodyStyle.ColorAndOpacity)
							.AutoWrapText(true)
							.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
							.LineHeightPercentage(Resolved.ControlLayout.BakeDialogSummaryLineHeight)
						]
					]
				]
			]

			// VALIDATION ERROR
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogValidationTopMargin, 0.0f, MixtormatTokens::BakeDialogValidationBottomMargin)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return ValidationText; })
				.Font(ErrorStyle.Font)
				.ColorAndOpacity(ErrorStyle.ColorAndOpacity)
				.OverflowPolicy(ETextOverflowPolicy::Clip)
				.AutoWrapText(true)
			]

			// FOOTER ACTIONS
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, MixtormatTokens::DialogActionsTopMargin, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormatBakeSettingsDialog::Cancel)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CancelBakeSettings", "Cancel"))
						.Font(LabelStyle.Font)
						.ColorAndOpacity(LabelStyle.ColorAndOpacity)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(Resolved.ControlLayout.DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
					.OnClicked(this, &SMixtormatBakeSettingsDialog::Accept)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AcceptBakeSettings", "Bake"))
						.Font(LabelStyle.Font)
						.ColorAndOpacity(LabelStyle.ColorAndOpacity)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
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
		.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.BakeDialogPathPickerWidth)
		.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.BakeDialogPathPickerHeight)
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

void SMixtormatBakeSettingsDialog::OnCustomMasterChanged(const FAssetData& AssetData)
{
	Settings.MaterialSettings.CustomMaster = TSoftObjectPtr<UMaterialInterface>(AssetData.ToSoftObjectPath());
	RefreshDiscoveredParameters();
	ValidationText = FText::GetEmpty();
}

void SMixtormatBakeSettingsDialog::RefreshDiscoveredParameters()
{
	DiscoveredParameters.Empty();
	if (Settings.MaterialSettings.CustomMaster.IsNull())
	{
		return;
	}

	UMaterialInterface* Master = Settings.MaterialSettings.CustomMaster.LoadSynchronous();
	if (!Master)
	{
		return;
	}

	DiscoveredParameters = FMixtormatBakeService::DiscoverTextureParameters(Master);

	auto AutoMatchOrValidate = [this](FMixtormatBakeMaterialMapping& Mapping, const TArray<FString>& CandidateKeywords)
	{
		bool bFound = false;
		if (Mapping.IsAssigned())
		{
			for (const FMixtormatDiscoveredTextureParameter& Param : DiscoveredParameters)
			{
				if (Param.ParameterInfo == Mapping.ParameterInfo)
				{
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			Mapping.SetUnassigned();
			for (const FString& Keyword : CandidateKeywords)
			{
				for (const FMixtormatDiscoveredTextureParameter& Param : DiscoveredParameters)
				{
					if (Param.ParameterInfo.Name.ToString().Contains(Keyword, ESearchCase::IgnoreCase))
					{
						Mapping.ParameterInfo = Param.ParameterInfo;
						return;
					}
				}
			}
		}
	};

	AutoMatchOrValidate(Settings.MaterialSettings.BaseColorMapping, {TEXT("BaseColor"), TEXT("Albedo"), TEXT("Diffuse"), TEXT("BC")});
	AutoMatchOrValidate(Settings.MaterialSettings.NormalMapping, {TEXT("Normal"), TEXT("Norm"), TEXT("Nrm")});
	AutoMatchOrValidate(Settings.MaterialSettings.RAMMapping, {TEXT("RAMH"), TEXT("RAM"), TEXT("ORM"), TEXT("Masks"), TEXT("Roughness")});
	AutoMatchOrValidate(Settings.MaterialSettings.HeightMapping, {TEXT("Height"), TEXT("Displacement"), TEXT("Disp")});
}

FText SMixtormatBakeSettingsDialog::GetParameterDisplayLabel(const FMixtormatBakeMaterialMapping& Mapping) const
{
	if (!Mapping.IsAssigned())
	{
		return LOCTEXT("UnassignedParamLabel", "[Unassigned]");
	}
	if (Mapping.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
	{
		return FText::FromName(Mapping.ParameterInfo.Name);
	}
	if (Mapping.ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter)
	{
		return FText::Format(LOCTEXT("LayerParamLabelFmt", "{0} (Layer {1})"), FText::FromName(Mapping.ParameterInfo.Name), FText::AsNumber(Mapping.ParameterInfo.Index));
	}
	return FText::Format(LOCTEXT("BlendParamLabelFmt", "{0} (Blend {1})"), FText::FromName(Mapping.ParameterInfo.Name), FText::AsNumber(Mapping.ParameterInfo.Index));
}

TSharedRef<SWidget> SMixtormatBakeSettingsDialog::BuildParameterMappingDropdown(
	FMixtormatBakeMaterialMapping& Mapping,
	const FText& OutputLabel)
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle LabelStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::BakeDialogMappingLabelGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::BakeDialogSettingLabelWidth)
			[
				SNew(STextBlock)
				.Text(OutputLabel)
				.Font(LabelStyle.Font)
				.ColorAndOpacity(LabelStyle.ColorAndOpacity)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
			.OnGetMenuContent_Lambda([this, &Mapping]()
			{
				return BuildParameterMenu(&Mapping);
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this, &Mapping]()
				{
					return GetParameterDisplayLabel(Mapping);
				})
				.Font(LabelStyle.Font)
				.ColorAndOpacity(LabelStyle.ColorAndOpacity)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		];
}

TSharedRef<SWidget> SMixtormatBakeSettingsDialog::BuildParameterMenu(FMixtormatBakeMaterialMapping* TargetMapping)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("UnassignedOption", "[Unassigned]"),
		LOCTEXT("UnassignedOptionTip", "Leave this parameter at its inherited master default."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, TargetMapping]()
			{
				TargetMapping->SetUnassigned();
				ValidationText = FText::GetEmpty();
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([TargetMapping]()
			{
				return !TargetMapping->IsAssigned();
			})
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);

	if (!DiscoveredParameters.IsEmpty())
	{
		MenuBuilder.AddSeparator();
		for (const FMixtormatDiscoveredTextureParameter& Param : DiscoveredParameters)
		{
			FText Label = GetParameterDisplayLabel(FMixtormatBakeMaterialMapping{Param.ParameterInfo});
			FText ToolTip = Param.bHasKnownSamplerType
				? FText::Format(LOCTEXT("ParamSamplerTipFmt", "Parameter: {0}\nSampler Type: {1}"), FText::FromName(Param.ParameterInfo.Name), FText::AsNumber(static_cast<int32>(Param.SamplerType)))
				: FText::FromName(Param.ParameterInfo.Name);

			MenuBuilder.AddMenuEntry(
				Label,
				ToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, TargetMapping, Param]()
					{
						TargetMapping->ParameterInfo = Param.ParameterInfo;
						ValidationText = FText::GetEmpty();
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([TargetMapping, Param]()
					{
						return TargetMapping->ParameterInfo == Param.ParameterInfo;
					})
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}
	}

	return MenuBuilder.MakeWidget();
}

void SMixtormatBakeSettingsDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
