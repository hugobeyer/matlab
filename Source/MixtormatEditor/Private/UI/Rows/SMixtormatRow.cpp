#include "UI/Rows/SMixtormatRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
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
	return SNew(SCheckBox)
		.ToolTipText(ToolTip)
		.IsChecked(IsChecked)
		.OnCheckStateChanged(OnStateChanged);
}

TSharedRef<SWidget> MakeChip(
	const TAttribute<FText>& Text,
	const FOnGetContent& OnGetMenuContent,
	const TSharedPtr<SWidget>& LeadingContent,
	const TAttribute<FText>& ToolTip)
{
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	if (LeadingContent.IsValid())
	{
		Content->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::RowLabelGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ChipThumbnailSize)
			.HeightOverride(MixtormatTokens::ChipThumbnailSize)
			[
				LeadingContent.ToSharedRef()
			]
		];
	}
	Content->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.Text(Text)
	];

	return SNew(SComboButton)
		.ToolTipText(ToolTip)
		.ContentPadding(FMargin(MixtormatTokens::RowLabelGap, 0.0f))
		.OnGetMenuContent(OnGetMenuContent)
		.ButtonContent()
		[
			SNew(SBox)
			.MinDesiredWidth(MixtormatTokens::RowFieldMinWidth)
			.HeightOverride(MixtormatTokens::ChipHeight)
			[
				Content
			]
		];
}

}
