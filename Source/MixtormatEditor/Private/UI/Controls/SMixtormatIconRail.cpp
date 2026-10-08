// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"


#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"


void SMixtormatIconRail::Construct(const FArguments& InArgs)
{
	ActiveIndex = InArgs._ActiveIndex;
	const Mixtormat::FMixtormatIconStyle& Role = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::NavigationRail)];

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
				.WidthOverride(FMath::Max(Role.ButtonSize, Role.HitSize))
				.HeightOverride(FMath::Max(Role.ButtonSize, Role.HitSize))
				[
					SNew(SCheckBox)
					.Style(&FMixtormatStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.ViewportOverlayToggle")))
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
