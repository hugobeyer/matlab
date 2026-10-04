// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatChip.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "UI/Primitives/SMixtormatWellBox.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatChip::Construct(const FArguments& InArgs)
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle ValueTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlValue),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const Mixtormat::FMixtormatControlMetrics& Layout = Resolved.ControlLayout;
	const float MinWidth = InArgs._MinWidth > 0.0f ? InArgs._MinWidth : Layout.RowFieldMinWidth;
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);

	if (InArgs._LeadingContent.Widget != SNullWidget::NullWidget)
	{
		Content->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::ChipGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ChipThumbnailSize)
			.HeightOverride(MixtormatTokens::ChipThumbnailSize)
			[
				InArgs._LeadingContent.Widget
			]
		];
	}

	Content->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Font(ValueTextStyle.Font)
		.ColorAndOpacity(ValueTextStyle.ColorAndOpacity)
		.Text(InArgs._Text)
		.AutoWrapText(false)
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		.Clipping(EWidgetClipping::ClipToBounds)
	];

	Content->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(MixtormatTokens::ChipGap, 0.0f, 0.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize)
		.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize)
		[
			SNew(SImage)
			.Image(MixtormatIcons::ChevronDown())
			.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
		]
	];

	ChildSlot
	[
		SNew(SMixtormatHelp)
		.Text(InArgs._ToolTip)
		[
		SAssignNew(ComboButton, SComboButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
		.Method(EPopupMethod::UseCurrentWindow)
		.HasDownArrow(false)
		.ContentPadding(FMargin(0.0f))
		.OnGetMenuContent(InArgs._OnGetMenuContent)
		.ButtonContent()
		[
			// The chip is a well like any other: the shared well painter, so it cannot drift from the
			// slider trough or the toggle. Lifts to hover while hovered or open, like the trough.
			SNew(SMixtormatWellBox)
			.IsHovered_Lambda([this]()
			{
				return IsHovered() || (ComboButton.IsValid() && ComboButton->IsOpen());
			})
			[
				SNew(SMixtormatGradientBox)
				.Padding(FMargin(MixtormatTokens::ChipTextInset, 0.0f, MixtormatTokens::ChipGap, 0.0f))
				[
					SNew(SBox)
					.MinDesiredWidth(MinWidth)
					.HeightOverride(MixtormatTokens::ChipHeight)
					[
						Content
					]
				]
			]
		]
		]
	];
}
