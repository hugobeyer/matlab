// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatInspectorCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatPalette.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UI/Rows/SMixtormatRow.h"

void SMixtormatInspectorCard::Construct(const FArguments& InArgs)
{
	bCompactLayout = InArgs._CompactLayout;
	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);
	const TAttribute<FText> Title = InArgs._Title;
	const TAttribute<FText> UpperTitle = TAttribute<FText>::CreateLambda([Title]()
	{
		const FText Value = Title.Get(FText::GetEmpty()).ToUpper();
		const FString Text = Value.ToString();
		return Text.Contains(TEXT("\n")) || Text.Contains(TEXT("\r"))
			? FText::FromString(Text.Replace(TEXT("\r\n"), TEXT(" "))
				.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")))
			: Value;
	});

	if (!bCompactLayout)
	{
		TSharedRef<SHorizontalBox> Label = SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::ClipToBounds);
		if (InArgs._LeadingHeaderContent.IsValid())
		{
			Label->AddSlot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, MixtormatTokens::GroupCardLeadingGap, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::GroupCardLeadingIconSize)
				.HeightOverride(MixtormatTokens::GroupCardLeadingIconSize)
				[ InArgs._LeadingHeaderContent.ToSharedRef() ]
			];
		}
		Label->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.CardTitle")))
			.Text(UpperTitle)
			.AutoWrapText(false)
			.Clipping(EWidgetClipping::ClipToBounds)
		];
		Stack->AddSlot().AutoHeight()
		[
			SNew(SBox).MinDesiredHeight(FMath::Max(
								MixtormatTokens::GroupCardTitleHeight, MixtormatTokens::GroupCardTitleDropDepth))
			[
				SNew(SHorizontalBox)
				// Keep padding inside the proportional slots so layout and paint share the midpoint.
				+ SHorizontalBox::Slot().FillWidth(MixtormatTokens::GroupCardTitleWidthRatio)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					// The label must not contribute its unbounded text width to the card's desired size.
					.WidthOverride(0.0f)
					.HeightOverride(MixtormatTokens::GroupCardTitleHeight)
					.Padding(FMargin(MixtormatTokens::GroupCardHorizontalPadding, 0.0f))
					.Clipping(EWidgetClipping::ClipToBounds)
					[ Label ]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f - MixtormatTokens::GroupCardTitleWidthRatio)
				[
					SNew(SBox)
					.HAlign(HAlign_Right).VAlign(VAlign_Center)
					.Padding(FMargin(MixtormatTokens::GroupCardHorizontalPadding,
						MixtormatTokens::GroupCardTitleDropDepth,
						MixtormatTokens::GroupCardHorizontalPadding, 0.0f))
					.Clipping(EWidgetClipping::ClipToBounds)
					[ InArgs._HeaderAction.IsValid() ? InArgs._HeaderAction.ToSharedRef() : SNullWidget::NullWidget ]
				]
			]
		];
		Stack->AddSlot().AutoHeight()
		.Padding(MixtormatTokens::GroupCardHorizontalPadding, MixtormatTokens::HeaderContentGap,
			MixtormatTokens::GroupCardHorizontalPadding, MixtormatTokens::HeaderContentGap)
		[ InArgs._Content.Widget ];
		ChildSlot [ Stack ];
		return;
	}

	const bool bHasTitle = Title.IsBound() || !Title.Get(FText::GetEmpty()).IsEmpty();
	const bool bHasAction = InArgs._HeaderAction.IsValid();
	const bool bHasLeading = InArgs._LeadingHeaderContent.IsValid();
	if (bHasTitle || bHasAction || bHasLeading)
	{
		TSharedRef<SHorizontalBox> TitleLine = SNew(SHorizontalBox);
		if (bHasLeading)
		{
			TitleLine->AddSlot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, MixtormatTokens::GroupCardLeadingGap, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::GroupCardLeadingIconSize)
				.HeightOverride(MixtormatTokens::GroupCardLeadingIconSize)
				[ InArgs._LeadingHeaderContent.ToSharedRef() ]
			];
		}
		TitleLine->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			bHasTitle
				? StaticCastSharedRef<SWidget>(
					SNew(STextBlock)
					.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.CardTitle")))
					.Justification_Lambda([]() { return MixtormatRow::JustifyFor(MixtormatTokens::SubgroupHeaderAlign); })
					.Text(UpperTitle))
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

		if (bHasTitle)
		{
			Stack->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::CardTitleGap)
			[
				MixtormatRow::MakeInspectorHairline(TAttribute<bool>::CreateLambda([]()
				{
					return MixtormatTokens::InspectorHairlineAboveSubgroups >= 0.5f;
				}))
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

int32 SMixtormatInspectorCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (bCompactLayout)
	{
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const float TitleWidth = Size.X * MixtormatTokens::GroupCardTitleWidthRatio;
	const float Drop = FMath::Clamp(MixtormatTokens::GroupCardTitleDropDepth, 0.0f, Size.Y);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FSlateBrush* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FLinearColor Background = MixtormatPalette::GroupCardBackground() * Tint;
	// Non-overlapping rectangles keep even translucent themes continuous at the step.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(TitleWidth, Drop), FSlateLayoutTransform()),
		Brush, Effect, Background);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2f(Size.X, Size.Y - Drop),
			FSlateLayoutTransform(FVector2f(0.0f, Drop))), Brush, Effect, Background);

	if (MixtormatTokens::InspectorHairlineAboveSubgroups >= 0.5f
		&& MixtormatTokens::InspectorHairlineThickness > 0.0f)
	{
		const float Inset = FMath::Clamp(MixtormatTokens::InspectorHairlineInset, 0.0f,
			FMath::Min(TitleWidth, Size.X - TitleWidth));
		const TArray<FVector2f> Edge = {
			FVector2f(Inset, 0.0f), FVector2f(TitleWidth, 0.0f),
			FVector2f(TitleWidth, Drop), FVector2f(Size.X - Inset, Drop)
		};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(), Edge, Effect,
			MixtormatPalette::InspectorHairline() * Tint, false,
			MixtormatTokens::InspectorHairlineThickness);

		const float HalfThickness = FMath::Min(MixtormatTokens::InspectorHairlineThickness * 0.5f,
			FMath::Min(Size.X, Size.Y) * 0.5f);
		const TArray<FVector2f> LeftEdge = {
			FVector2f(HalfThickness, HalfThickness),
			FVector2f(HalfThickness, Size.Y - HalfThickness)
		};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(), LeftEdge, Effect,
			MixtormatPalette::InspectorHairline() * Tint, false,
			MixtormatTokens::InspectorHairlineThickness);
		const TArray<FVector2f> LowerEdge = {
			FVector2f(Size.X - HalfThickness, FMath::Min(Drop + HalfThickness, Size.Y - HalfThickness)),
			FVector2f(Size.X - HalfThickness, Size.Y - HalfThickness),
			FVector2f(HalfThickness, Size.Y - HalfThickness)
		};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(), LowerEdge, Effect,
			MixtormatPalette::Shadow() * Tint, false,
			MixtormatTokens::InspectorHairlineThickness);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}
