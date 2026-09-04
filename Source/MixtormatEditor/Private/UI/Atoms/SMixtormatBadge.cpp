#include "UI/Atoms/SMixtormatBadge.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatBadge::Construct(const FArguments& InArgs)
{
	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(MixtormatTokens::BadgeWidth)
		.HeightOverride(MixtormatTokens::BadgeHeight)
		[
			SNew(SBorder)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Badge")))
			.Padding(FMargin(MixtormatTokens::BadgeTextInset, 0.0f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.BadgeText")))
				.Text(InArgs._Text)
			]
		]
	];
}
