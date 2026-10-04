// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatSegmentedControl.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatGroupButton.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
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
			SLATE_ARGUMENT(bool, UseGroupButtonVisuals)
			SLATE_ARGUMENT(bool, ShowSeparator)
			SLATE_ATTRIBUTE(bool, bActive)
			SLATE_EVENT(FSimpleDelegate, OnChosen)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bActive = InArgs._bActive;
			OnChosen = InArgs._OnChosen;
			bUseGroupButtonVisuals = InArgs._UseGroupButtonVisuals;

			if (bUseGroupButtonVisuals)
			{
				ChildSlot
				[
					SNew(SBox).MinDesiredHeight(MixtormatTokens::GroupButtonHeight)
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
				return;
			}

			ChildSlot
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::SegmentHeight)
				[
					SNew(SMixtormatGradientBox)
					.StartColor(this, &SMixtormatSegment::GetTop)
					.EndColor(this, &SMixtormatSegment::GetBottom)
					.MultiplyStart(this, &SMixtormatSegment::GetMultiply)
					.Orientation(Orient_Vertical)
					.CornerRadius(MixtormatTokens::CornerRadiusInner)
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.BadgeText")))
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

		FLinearColor GetTop() const
		{
			return IsActive() ? MixtormatPalette::SegmentTop() : FLinearColor::Transparent;
		}
		FLinearColor GetBottom() const
		{
			return IsActive() ? MixtormatPalette::SegmentBottom() : FLinearColor::Transparent;
		}
		FLinearColor GetMultiply() const
		{
			// A segment is only 16px wide, so it takes a lighter darkening pass than a full row.
			return IsActive() ? MixtormatPalette::SegmentShade() : FLinearColor::Transparent;
		}
		FSlateColor GetTextColor() const
		{
			if (bUseGroupButtonVisuals)
			{
				return FSlateColor::UseForeground();
			}
			if (IsActive())
			{
				return FSlateColor(MixtormatPalette::SegmentActiveText());
			}
			return FSlateColor(IsHovered() ? MixtormatPalette::RowText() : MixtormatPalette::BadgeText());
		}

		bool bUseGroupButtonVisuals = false;
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
		// A hairline between cells only -- never before the first, never around the strip.
		if (Index > 0 && !InArgs._UseGroupButtonVisuals)
		{
			Strip->AddSlot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::SegmentSeamWidth)
				[
					SNew(SImage)
					.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.SegmentSeam")))
				]
			];
		}

		const FMixtormatOnSegmentChosen Chosen = InArgs._OnChosen;
		Strip->AddSlot()
		.FillWidth(1.0f)
		[
			SNew(SMixtormatSegment)
			.UseGroupButtonVisuals(InArgs._UseGroupButtonVisuals)
			.ShowSeparator(Index + 1 < InArgs._Options.Num())
			.Text(InArgs._Options[Index])
			.ToolTipText(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
			.bActive_Lambda([this, Index]() { return ActiveIndex.Get(0) == Index; })
			.OnChosen(FSimpleDelegate::CreateLambda([Chosen, Index]()
			{
				Chosen.ExecuteIfBound(Index);
			}))
		];
	}

	if (InArgs._UseGroupButtonVisuals)
	{
		ChildSlot[Strip];
		return;
	}

	ChildSlot
	[
		SNew(SMixtormatGradientBox)
		.StartColor(MixtormatPalette::WellTop())
		.EndColor(FLinearColor::Transparent)
		.Orientation(Orient_Vertical)
		.CornerRadius(MixtormatTokens::CornerRadius)
		.Padding(FMargin(MixtormatTokens::SegmentSeamWidth))
		[
			Strip
		]
	];
}
