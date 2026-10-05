// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatSegmentedControl.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatGroupButton.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// One cell. Kept local because it has no use outside the strip.
	class SMixtormatSegment final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatSegment) {}
			SLATE_ARGUMENT(FText, Text)
			SLATE_ARGUMENT(bool, ShowSeparator)
			SLATE_ATTRIBUTE(bool, bActive)
			SLATE_EVENT(FSimpleDelegate, OnChosen)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bActive = InArgs._bActive;
			OnChosen = InArgs._OnChosen;

			// One visual path. This used to branch on a UseGroupButtonVisuals flag and, when
			// false, hand-roll the segment as a gradient box over SegmentTop/Bottom/Shade -- a
			// second appearance for the same control that the prototype does not have:
			// components.css styles `.segmented` in the same rule as `.tabs`. Both branches were
			// therefore the same recipe and two painters.
			ChildSlot
			[
				SNew(SBox).MinDesiredHeight(FMixtormatThemeStore::GetResolved().Buttons.Height)
				[
					SNew(SMixtormatGroupButtonSurface)
					.Hovered_Lambda([this]() { return IsHovered(); })
					.Selected(bActive)
					.ShowSeparator(InArgs._ShowSeparator)
					[
						SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
							.ColorAndOpacity(this, &SMixtormatSegment::GetTextColor)
							.Text(InArgs._Text)
						]
					]
				]
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			OnChosen.ExecuteIfBound();
			return FReply::Handled();
		}

		virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}

	private:
		bool IsActive() const { return bActive.Get(false); }

		FSlateColor GetTextColor() const
		{
			// The plate owns the label's colour through the container's foreground, which resolves
			// the button text colour from the palette. Setting it here would override that with a
			// second, unrelated rule.
			return FSlateColor::UseForeground();
		}

		TAttribute<bool> bActive;
		FSimpleDelegate OnChosen;
	};
}

void SMixtormatSegmentedControl::Construct(const FArguments& InArgs)
{
	ActiveIndex = InArgs._ActiveIndex;

	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
	{
		const FMixtormatOnSegmentChosen Chosen = InArgs._OnChosen;
		Strip->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SMixtormatHelp)
			.Text(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
			[
				SNew(SMixtormatSegment)
				.ShowSeparator(Index + 1 < InArgs._Options.Num())
				.Text(InArgs._Options[Index])
				.bActive_Lambda([this, Index]() { return ActiveIndex.Get(0) == Index; })
				.OnChosen(FSimpleDelegate::CreateLambda([Chosen, Index]()
				{
					Chosen.ExecuteIfBound(Index);
				}))
			]
		];
	}

		// No wrapper. The strip is a row of buttons that join: each segment draws its own body, and
		// the separator tick between them comes from the button recipe rather than from a separate
		// seam element or a padding box around the whole strip.
		ChildSlot[Strip];
	}
