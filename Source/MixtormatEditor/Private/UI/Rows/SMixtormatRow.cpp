// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Rows/SMixtormatRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatPalette.h"
#include "Styling/CoreStyle.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Atoms/SMixtormatToggle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatRow
{

TSharedRef<SWidget> Make(
	const FText& Label,
	const TSharedRef<SWidget>& TrailingContent,
	const TAttribute<FText>& ToolTip)
{
	// The label takes the slack but sits at its right end, so it reads up against the control it
	// names instead of across a gap from it. Every caller here is a label in front of a chip,
	// dropdown or toggle, and a label pinned to the far left left the pairing to be inferred
	// from vertical position alone.
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::RowLabelGap, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			TrailingContent
		];

	TSharedRef<SBox> Sized = SNew(SBox)
		.HeightOverride(MixtormatTokens::RowHeight)
		[
			Row
		];
	if (ToolTip.IsSet())
	{
		Sized->SetToolTipText(ToolTip);
	}
	return Sized;
}

TSharedRef<SWidget> MakeDropdown(
	const FText& Label,
	const TSharedRef<SWidget>& Control,
	const TAttribute<FText>& ToolTip)
{
	TSharedRef<SBox> Sized = SNew(SBox)
		.HeightOverride_Lambda([]() { return FOptionalSize(MixtormatTokens::RowHeight); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(TAttribute<float>::CreateLambda([]() { return MixtormatTokens::DropdownLabelRatio; }))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, MixtormatTokens::RowLabelGap, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
				.Text(Label)
				.Justification(ETextJustify::Left)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(TAttribute<float>::CreateLambda([]() { return 1.0f - MixtormatTokens::DropdownLabelRatio; }))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				Control
			]
		];
	if (ToolTip.IsSet())
	{
		Sized->SetToolTipText(ToolTip);
	}
	return Sized;
}

TSharedRef<SWidget> MakeTrailing(
	const FText& Label,
	const TSharedRef<SWidget>& TrailingContent,
	const TAttribute<FText>& ToolTip)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		// The spacer takes the slack, so label and control stay together at the right edge
		// however wide the panel gets.
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::RowLabelGap, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			TrailingContent
		];

	TSharedRef<SBox> Sized = SNew(SBox)
		.HeightOverride(MixtormatTokens::RowHeight)
		[
			Row
		];
	if (ToolTip.IsSet())
	{
		Sized->SetToolTipText(ToolTip);
	}
	return Sized;
}

TSharedRef<SWidget> MakePair(const TSharedRef<SWidget>& Left, const TSharedRef<SWidget>& Right)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)[Left]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SSpacer).Size(FVector2D(MixtormatTokens::RowGap * 2.0f, 0.0f))]
		+ SHorizontalBox::Slot().FillWidth(1.0f)[Right];
}

ETextJustify::Type JustifyFor(const float Align)
{
	return Align >= 1.5f ? ETextJustify::Right : (Align >= 0.5f ? ETextJustify::Center : ETextJustify::Left);
}

TSharedRef<SWidget> MakeInspectorHairline(const TAttribute<bool>& bShown)
{
	return SNew(SBox)
		.Visibility_Lambda([bShown]()
		{
			return bShown.Get(true) && MixtormatTokens::InspectorHairlineThickness > 0.0f
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		.HeightOverride_Lambda([]() { return FOptionalSize(MixtormatTokens::InspectorHairlineThickness); })
		.Padding_Lambda([]() { return FMargin(MixtormatTokens::InspectorHairlineInset, 0.0f); })
		[
			SNew(SImage)
			.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.ColorAndOpacity_Lambda([]() { return FSlateColor(MixtormatPalette::InspectorHairline()); })
		];
}

TSharedRef<SWidget> MakeCaption(const FText& Caption)
{
	// A subgroup header with its own top spacing, but no hairline; foldout headers own those.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Padding(FMargin(
				1.0f,
				MixtormatTokens::CaptionHeightAbove,
				0.0f,
				MixtormatTokens::CaptionHeightBelow))
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowCaption")))
				.Justification_Lambda([]() { return JustifyFor(MixtormatTokens::SubgroupHeaderAlign); })
				.Text(Caption.ToUpper())
			]
		];
}

TSharedRef<SWidget> MakeHairline()
{
	return SNew(SBox)
		.HeightOverride(MixtormatTokens::HairlineThickness)
		.Padding(FMargin(0.0f, MixtormatTokens::HairlineMargin))
		[
			SNew(SImage)
			.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Hairline")))
		];
}

TSharedRef<SWidget> MakeCheckbox(
	const TAttribute<ECheckBoxState>& IsChecked,
	const FOnCheckStateChanged& OnStateChanged,
	const TAttribute<FText>& ToolTip)
{
	return SNew(SMixtormatToggle)
		.ToolTip(ToolTip)
		.IsChecked(IsChecked)
		.OnCheckStateChanged(OnStateChanged);
}

// Delegates rather than rebuilds. This assembled its own SComboButton with a plain button
// background, which is why a dropdown in a row and a dropdown anywhere else were visibly
// different controls -- the chip has the well gradient and this did not.
TSharedRef<SWidget> MakeChip(
	const TAttribute<FText>& Text,
	const FOnGetContent& OnGetMenuContent,
	const TSharedPtr<SWidget>& LeadingContent,
	const TAttribute<FText>& ToolTip,
	const float MinWidth)
{
	// The chip ignores a null leading slot, so there is no branch here: an absent thumbnail is
	// SNullWidget rather than a differently-constructed chip.
	return SNew(SMixtormatChip)
		.MinWidth(MinWidth)
		.Text(Text)
		.ToolTip(ToolTip)
		.OnGetMenuContent(OnGetMenuContent)
		.LeadingContent()
		[
			LeadingContent.IsValid() ? LeadingContent.ToSharedRef() : SNullWidget::NullWidget
		];
}

}
