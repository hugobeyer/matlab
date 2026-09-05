#include "UI/Rows/SMixtormatRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
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
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
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

TSharedRef<SWidget> MakeCaption(const FText& Caption)
{
	return SNew(SBox)
		.Padding(FMargin(
			1.0f,
			MixtormatTokens::CaptionHeightAbove,
			0.0f,
			MixtormatTokens::CaptionHeightBelow))
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowCaption")))
			.Text(Caption.ToUpper())
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
	const TAttribute<FText>& ToolTip)
{
	// The chip ignores a null leading slot, so there is no branch here: an absent thumbnail is
	// SNullWidget rather than a differently-constructed chip.
	return SNew(SMixtormatChip)
		.Text(Text)
		.ToolTip(ToolTip)
		.OnGetMenuContent(OnGetMenuContent)
		.LeadingContent()
		[
			LeadingContent.IsValid() ? LeadingContent.ToSharedRef() : SNullWidget::NullWidget
		];
}

}
