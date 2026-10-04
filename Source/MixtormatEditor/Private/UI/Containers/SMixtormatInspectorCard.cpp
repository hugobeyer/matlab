// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatInspectorCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatPalette.h"
#include "Rendering/DrawElements.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UI/Rows/SMixtormatRow.h"

namespace
{
	// Resolve inherited disabled state at paint time without Slate's additional disabled shader.
	class SMixtormatGroupCardTitle final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatGroupCardTitle) {}
			SLATE_ATTRIBUTE(FText, Text)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				SNew(STextBlock)
				// Copy the font; no style-set pointer survives a theme refresh.
				.Font(FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupCardTitle")).Font)
				.ColorAndOpacity_Lambda([]() { return FSlateColor(MixtormatPalette::GroupCardTitleText()); })
				.Text(InArgs._Text)
				.AutoWrapText(false)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Clipping(EWidgetClipping::ClipToBounds)
			];
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
			const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
			const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			FWidgetStyle TitleStyle = Style;
			TitleStyle.BlendColorAndOpacityTint(FLinearColor(1.0f, 1.0f, 1.0f,
				ShouldBeEnabled(bParentEnabled) ? MixtormatTokens::GroupCardTitleOpacity
					: MixtormatTokens::GroupCardTitleDisabledOpacity));
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
				LayerId, TitleStyle, true);
		}
	};
}

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
		CardStack = Stack;
		TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
		if (InArgs._LeadingHeaderContent.IsValid())
		{
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, MixtormatTokens::GroupCardLeadingGap, 0.0f)
			[
				// The caller owns glyph size, hit padding, state and callbacks. Do not squeeze
				// an interactive widget into a glyph-sized box or dim all of its states here.
				InArgs._LeadingHeaderContent.ToSharedRef()
			];
		}
		Header->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SBox)
			// Like CSS min-width:0: long titles consume allotted space, not desired width.
			.WidthOverride(0.0f)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SMixtormatGroupCardTitle)
				.Text(UpperTitle)
			]
		];
		if (InArgs._HeaderAction.IsValid())
		{
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
			.Padding(MixtormatTokens::GroupCardLeadingGap, 0.0f, 0.0f, 0.0f)
			[ InArgs._HeaderAction.ToSharedRef() ];
		}
		Stack->AddSlot().AutoHeight()
		.Padding(0.0f, MixtormatTokens::GroupCardHeaderMarginTop,
			0.0f, MixtormatTokens::GroupCardHeaderMarginBottom)
		[
			SAssignNew(HeaderBox, SBox)
			.MinDesiredHeight(MixtormatTokens::GroupCardTitleHeight)
			.VAlign(VAlign_Center)
			.Padding(FMargin(MixtormatTokens::GroupCardHeaderPaddingLeft,
				MixtormatTokens::GroupCardHeaderPaddingTop,
				MixtormatTokens::GroupCardHeaderPaddingRight,
				MixtormatTokens::GroupCardHeaderPaddingBottom))
			[ Header ]
		];
		Stack->AddSlot().AutoHeight()
		[
			SAssignNew(BodyBox, SBox)
			.Padding(FMargin(MixtormatTokens::GroupCardHorizontalPadding,
				MixtormatTokens::GroupCardContentPaddingTop,
				MixtormatTokens::GroupCardHorizontalPadding,
				MixtormatTokens::GroupCardContentPaddingBottom))
			[ InArgs._Content.Widget ]
		];
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
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return LayerId;
	}

	MixtormatGroupCard::FSurface Surface;
	Surface.Size = Size;
	// Arrange this frame's slots explicitly: cached geometry is from the previous paint and
	// gives a stale seam on first paint, resize or a change in action/content desired height.
	FArrangedChildren Arranged(EVisibility::Visible);
	CardStack->ArrangeChildren(CardGeometry, Arranged);
	for (int32 Index = 0; Index < Arranged.Num(); ++Index)
	{
		const FArrangedWidget& Child = Arranged[Index];
		const float Top = CardGeometry.AbsoluteToLocal(
			Child.Geometry.LocalToAbsolute(FVector2D::ZeroVector)).Y;
		if (Child.Widget == HeaderBox)
		{
			Surface.HeaderTop = Top;
			Surface.HeaderHeight = Child.Geometry.GetLocalSize().Y;
		}
		else if (Child.Widget == BodyBox)
		{
			Surface.BodyTop = Top;
			Surface.BodyHeight = Child.Geometry.GetLocalSize().Y;
		}
	}
	Surface.AuthoredHeaderHeight = MixtormatTokens::GroupCardTitleHeight;
	Surface.Radius = MixtormatTokens::GroupCardRadius;
	Surface.Reach = MixtormatTokens::GroupCardGradientReach;
	Surface.Power = MixtormatTokens::GroupCardFalloffPower;
	Surface.HeaderOpacity = MixtormatTokens::GroupCardHeaderOpacity;
	Surface.BodyOpacity = MixtormatTokens::GroupCardBodyOpacity;
	Surface.HeaderSaturation = MixtormatTokens::GroupCardHeaderSaturation;
	Surface.BodySaturation = MixtormatTokens::GroupCardBodySaturation;

	Surface.Ground = MixtormatPalette::Ground();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const int32 ClipCount = MixtormatGroupCard::PushRoundedClip(OutDrawElements, CardGeometry, Surface.Radius);
	SurfacePainter.Paint(OutDrawElements, LayerId, CardGeometry, Surface,
		InWidgetStyle.GetColorAndOpacityTint(), Effect);
	const int32 LastLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
	for (int32 Index = 0; Index < ClipCount; ++Index)
	{
		OutDrawElements.PopClip();
	}
	return LastLayer;
}
