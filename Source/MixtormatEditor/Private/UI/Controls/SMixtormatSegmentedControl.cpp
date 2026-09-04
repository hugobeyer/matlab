#include "UI/Controls/SMixtormatSegmentedControl.h"

#include "Style/MixtormatDesignTokens.h"
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
			SLATE_ATTRIBUTE(bool, bActive)
			SLATE_EVENT(FSimpleDelegate, OnChosen)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bActive = InArgs._bActive;
			OnChosen = InArgs._OnChosen;

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
			return IsActive() ? MixtormatPalette::Hex(0x000000, MixtormatTokens::SegmentShadeAlpha) : FLinearColor::Transparent;
		}
		FSlateColor GetTextColor() const
		{
			if (IsActive())
			{
				return FSlateColor(MixtormatPalette::Hex(0xE8F0F8));
			}
			return FSlateColor(IsHovered() ? MixtormatPalette::RowText() : MixtormatPalette::BadgeText());
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
		// A hairline between cells only -- never before the first, never around the strip.
		if (Index > 0)
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
			.Text(InArgs._Options[Index])
			.ToolTipText(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
			.bActive_Lambda([this, Index]() { return ActiveIndex.Get(0) == Index; })
			.OnChosen(FSimpleDelegate::CreateLambda([Chosen, Index]()
			{
				Chosen.ExecuteIfBound(Index);
			}))
		];
	}

	ChildSlot
	[
		SNew(SMixtormatGradientBox)
		.StartColor(MixtormatPalette::WellTop())
		.EndColor(MixtormatPalette::Hex(0x000000, 0.0f))
		.Orientation(Orient_Vertical)
		.CornerRadius(MixtormatTokens::CornerRadius)
		.Padding(FMargin(MixtormatTokens::SegmentSeamWidth))
		[
			Strip
		]
	];
}
