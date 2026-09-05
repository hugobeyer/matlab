#include "UI/Containers/SMixtormatInspectorCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatInspectorCard::Construct(const FArguments& InArgs)
{
	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);

	const bool bHasTitle = !InArgs._Title.IsEmpty();
	const bool bHasAction = InArgs._HeaderAction.IsValid();
	if (bHasTitle || bHasAction)
	{
		TSharedRef<SHorizontalBox> TitleLine = SNew(SHorizontalBox);
		TitleLine->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			bHasTitle
				? StaticCastSharedRef<SWidget>(
					SNew(STextBlock)
					.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowCaption")))
					.Text(InArgs._Title.ToUpper()))
				: SNullWidget::NullWidget
		];
		if (bHasAction)
		{
			TitleLine->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				InArgs._HeaderAction.ToSharedRef()
			];
		}

		Stack->AddSlot()
		.AutoHeight()
		.Padding(MixtormatTokens::CardPadding, 0.0f, MixtormatTokens::CardPadding, MixtormatTokens::CardTitleGap)
		[
			TitleLine
		];
	}

	Stack->AddSlot()
	.AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Card")))
		// The vertical inset is the gap every run of rows gets under its heading, top and
		// bottom, so a card's first and last row are not flush against its edge.
		.Padding(FMargin(
			MixtormatTokens::CardPadding,
			MixtormatTokens::HeaderContentGap,
			MixtormatTokens::CardPadding,
			MixtormatTokens::HeaderContentGap))
		[
			InArgs._Content.Widget
		]
	];

	ChildSlot
	[
		Stack
	];
}
