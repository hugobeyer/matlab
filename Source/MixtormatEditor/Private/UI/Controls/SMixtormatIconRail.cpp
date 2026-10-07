// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

namespace
{
	// Resourceless, like the viewport rail buttons: the icon and its colour are the whole control,
	// so a plate brush here would be a second surface under the first. No padding either -- the
	// rail sizes each button itself, so the hover plate is exactly the button.
	const FCheckBoxStyle& GetRailToggleStyle()
	{
		static FCheckBoxStyle Style;
		Style = FCheckBoxStyle().SetCheckBoxType(ESlateCheckBoxType::ToggleButton);
		Style.SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource())
			.SetUncheckedPressedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
			.SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
			.SetUndeterminedImage(FSlateNoResource()).SetUndeterminedHoveredImage(FSlateNoResource())
			.SetUndeterminedPressedImage(FSlateNoResource()).SetBackgroundImage(FSlateNoResource())
			.SetBackgroundHoveredImage(FSlateNoResource()).SetBackgroundPressedImage(FSlateNoResource())
			.SetPadding(FMargin(0.0f));
		return Style;
	}
}

void SMixtormatIconRail::Construct(const FArguments& InArgs)
{
	ActiveIndex = InArgs._ActiveIndex;
	const Mixtormat::FMixtormatIconStyle& Role = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::PanelToolbar)];

	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
	for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
	{
		const TAttribute<bool> bActive = TAttribute<bool>::CreateLambda(
			[Active = ActiveIndex, Index]() { return Active.Get(0) == Index; });
		const FMixtormatOnSegmentChosen OnChosen = InArgs._OnChosen;
		Rail->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f,
					FMixtormatThemeStore::GetResolved().PreviewLayout.LeftRailButtonGap)
		[
			// The shared styled help, not Slate's default tooltip.
			SNew(SMixtormatHelp)
			.Text(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
			[
				SNew(SBox)
				.WidthOverride(Role.ButtonSize)
				.HeightOverride(Role.ButtonSize)
				[
					SNew(SCheckBox)
					.Style(&GetRailToggleStyle())
				.IsChecked_Lambda([bActive]()
				{
					return bActive.Get(false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				// Fires on any state change, like the tabs it replaces: clicking the page that is
				// already open is a no-op, not a way to leave nothing selected.
				.OnCheckStateChanged_Lambda([OnChosen, Index](ECheckBoxState) { OnChosen.ExecuteIfBound(Index); })
				[
					SNew(SBox)
					.WidthOverride(Role.GlyphSize)
					.HeightOverride(Role.GlyphSize)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(InArgs._Options[Index])
						.ColorAndOpacity_Lambda([bActive]()
						{
							const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
							return bActive.Get(false)
								? FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Accent))
								: FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
						})
					]
				]
			]
			]
			];
		}
		ChildSlot
	[
		Rail
	];
}
