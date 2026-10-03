// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatInspectorCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatPalette.h"
#include "Rendering/DrawElements.h"
#include "UI/Primitives/MixtormatGradientPainter.h"
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
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupCardTitle")))
			.Text(UpperTitle)
			.AutoWrapText(false)
			.Clipping(EWidgetClipping::ClipToBounds)
		];
		Stack->AddSlot().AutoHeight()
		[
			SAssignNew(HeaderBox, SBox).MinDesiredHeight(
				MixtormatTokens::GroupCardTitleHeight + MixtormatTokens::GroupCardHeaderPaddingTop
					+ MixtormatTokens::GroupCardHeaderPaddingBottom)
			[
				SNew(SHorizontalBox)
				// Keep padding inside the proportional title/action slots.
				+ SHorizontalBox::Slot().FillWidth(MixtormatTokens::GroupCardTitleWidthRatio)
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					// The label must not contribute its unbounded text width to the card's desired size.
					.WidthOverride(0.0f)
					.HeightOverride(MixtormatTokens::GroupCardTitleHeight
						+ MixtormatTokens::GroupCardHeaderPaddingTop + MixtormatTokens::GroupCardHeaderPaddingBottom)
					.Padding(FMargin(MixtormatTokens::GroupCardHeaderPaddingLeft,
						MixtormatTokens::GroupCardHeaderPaddingTop,
						MixtormatTokens::GroupCardHeaderPaddingRight,
						MixtormatTokens::GroupCardHeaderPaddingBottom))
					.Clipping(EWidgetClipping::ClipToBounds)
					[ Label ]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f - MixtormatTokens::GroupCardTitleWidthRatio)
				[
					SNew(SBox)
					.HAlign(HAlign_Right).VAlign(VAlign_Center)
					.Padding(FMargin(MixtormatTokens::GroupCardHeaderPaddingLeft,
						MixtormatTokens::GroupCardHeaderPaddingTop,
						MixtormatTokens::GroupCardHeaderPaddingRight,
						MixtormatTokens::GroupCardHeaderPaddingBottom))
					.Clipping(EWidgetClipping::ClipToBounds)
					[ InArgs._HeaderAction.IsValid() ? InArgs._HeaderAction.ToSharedRef() : SNullWidget::NullWidget ]
				]
			]
		];
		Stack->AddSlot().AutoHeight()
		.Padding(MixtormatTokens::GroupCardHorizontalPadding, MixtormatTokens::GroupCardContentPaddingTop,
			MixtormatTokens::GroupCardHorizontalPadding, MixtormatTokens::GroupCardContentPaddingBottom)
		[ InArgs._Content.Widget ];
		ChildSlot
		.Padding(FMargin(MixtormatTokens::GroupCardOuterMarginLeft,
			MixtormatTokens::GroupCardOuterMarginTop,
			MixtormatTokens::GroupCardOuterMarginRight,
			MixtormatTokens::GroupCardOuterMarginBottom))
		[ Stack ];
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

	const FVector2f OuterSize(AllottedGeometry.GetLocalSize());
	const FVector2f Size(
		FMath::Max(0.0f, OuterSize.X - MixtormatTokens::GroupCardOuterMarginLeft
			- MixtormatTokens::GroupCardOuterMarginRight),
		FMath::Max(0.0f, OuterSize.Y - MixtormatTokens::GroupCardOuterMarginTop
			- MixtormatTokens::GroupCardOuterMarginBottom));
	const FGeometry CardGeometry = AllottedGeometry.MakeChild(Size,
		FSlateLayoutTransform(FVector2f(MixtormatTokens::GroupCardOuterMarginLeft,
			MixtormatTokens::GroupCardOuterMarginTop)));
	const float HeaderHeight = HeaderBox->GetCachedGeometry().GetLocalSize().Y;
	const float HeaderEnd = Size.Y > 0.0f ? FMath::Clamp(HeaderHeight / Size.Y, 0.0f, 1.0f) : 0.0f;
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor Background = MixtormatPalette::GroupCardBackground() * Tint;
	FLinearColor HeaderColor = Background;
	HeaderColor.A *= MixtormatTokens::GroupCardHeaderOpacity;
	FLinearColor BodyColor = Background;
	BodyColor.A *= MixtormatTokens::GroupCardBodyOpacity;
	const MixtormatGradient::FStop Stops[] = {
		{0.0f, HeaderColor}, {HeaderEnd, BodyColor}, {1.0f, BodyColor}
	};
	const float Radius = MixtormatTokens::CornerRadius;
	MixtormatGradient::Paint(OutDrawElements, LayerId, CardGeometry.ToPaintGeometry(),
		Size, Orient_Vertical, MakeArrayView(Stops), FVector4f(Radius, Radius, Radius, Radius));

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}
