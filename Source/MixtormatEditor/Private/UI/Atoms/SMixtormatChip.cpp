#include "UI/Atoms/SMixtormatChip.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatChip::Construct(const FArguments& InArgs)
{
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);

	if (InArgs._LeadingContent.Widget != SNullWidget::NullWidget)
	{
		Content->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
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
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.Text(InArgs._Text)
	];

	Content->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(9.0f)
		.HeightOverride(9.0f)
		[
			SNew(SImage)
			.Image(MixtormatIcons::ChevronDown())
			.ColorAndOpacity(FSlateColor(MixtormatPalette::CaptionText()))
		]
	];

	ChildSlot
	[
		SNew(SComboButton)
		.ToolTipText(InArgs._ToolTip)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
		.HasDownArrow(false)
		.ContentPadding(FMargin(0.0f))
		.OnGetMenuContent(InArgs._OnGetMenuContent)
		.ButtonContent()
		[
			// The chip is a well like any other: darker than the body it sits in, no border.
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.Orientation(Orient_Vertical)
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(FMargin(6.0f, 0.0f, 4.0f, 0.0f))
			[
				SNew(SBox)
				.MinDesiredWidth(InArgs._MinWidth)
				.HeightOverride(MixtormatTokens::ChipHeight)
				[
					Content
				]
			]
		]
	];
}
