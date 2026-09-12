// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatTabStrip.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The underline under one stretch of the strip: a rule the width of whatever it is given,
	// drawn in the selected brush or the resting one.
	//
	// Its own helper because the strip draws it three times over -- once per tab and once for the
	// run of empty space past the last tab -- and those three had drifted apart when they were
	// written out by hand.
	TSharedRef<SWidget> MakeUnderline(const TAttribute<bool>& bSelected)
	{
		return SNew(SBox)
			.HeightOverride(MixtormatTokens::TabUnderlineThickness)
			[
				SNew(SBorder)
				.BorderImage_Lambda([bSelected]()
				{
					return FMixtormatStyle::Get().GetBrush(bSelected.Get(false)
						? TEXT("Mixtormat.TabUnderlineSelected")
						: TEXT("Mixtormat.TabUnderline"));
				})
			];
	}

	// One tab: a label over its own underline, inside a plate that reads as raised when resting
	// and as continuous with the panel below when selected.
	TSharedRef<SWidget> MakeTab(
		const FText& Label,
		const FText& ToolTip,
		const TAttribute<bool>& bSelected,
		const FSimpleDelegate& OnChosen)
	{
		const ISlateStyle& Style = FMixtormatStyle::Get();
		return SNew(SBox)
			.WidthOverride(MixtormatTokens::TabWidth)
			.HeightOverride(MixtormatTokens::TabHeight)
			[
				SNew(SBorder)
				.Padding(0.0f)
				.BorderImage_Lambda([bSelected]()
				{
					return FMixtormatStyle::Get().GetBrush(bSelected.Get(false)
						? TEXT("Mixtormat.TabActive")
						: TEXT("Mixtormat.TabInactive"));
				})
				[
					SNew(SCheckBox)
					.Style(&Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.TabToggle")))
					.ToolTipText(ToolTip)
					.IsChecked_Lambda([bSelected]()
					{
						return bSelected.Get(false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					// Fires on any state change rather than only on Checked: clicking the tab that
					// is already open should be a no-op, not a way to leave nothing selected.
					.OnCheckStateChanged_Lambda([OnChosen](ECheckBoxState) { OnChosen.ExecuteIfBound(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TabLabelBottomInset)
						[
							SNew(STextBlock)
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.SectionHeader")))
							.ColorAndOpacity_Lambda([bSelected]()
							{
								return bSelected.Get(false)
									? FSlateColor::UseForeground()
									: FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.55f));
							})
							.Text(Label)
							.Justification(ETextJustify::Center)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeUnderline(bSelected)
						]
					]
				]
			];
	}
}

void SMixtormatTabStrip::Construct(const FArguments& InArgs)
{
	ActiveIndex = InArgs._ActiveIndex;

	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
	{
		const TAttribute<bool> bSelected = TAttribute<bool>::CreateLambda(
			[Active = ActiveIndex, Index]() { return Active.Get(0) == Index; });

		const FMixtormatOnSegmentChosen OnChosen = InArgs._OnChosen;
		Strip->AddSlot().AutoWidth()
		[
			MakeTab(
				InArgs._Options[Index],
				InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty(),
				bSelected,
				FSimpleDelegate::CreateLambda([OnChosen, Index]() { OnChosen.ExecuteIfBound(Index); }))
		];
	}

	// The rule carries on past the last tab, always in the resting brush. That run of it is what
	// closes the top of the panel: without it the tabs float above an open edge, and the selected
	// tab has nothing to be joined to.
	Strip->AddSlot().FillWidth(1.0f).VAlign(VAlign_Bottom)
	[
		MakeUnderline(false)
	];

	ChildSlot[Strip];
}
