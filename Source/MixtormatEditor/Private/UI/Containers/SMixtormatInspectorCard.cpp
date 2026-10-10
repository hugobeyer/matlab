// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatInspectorCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Containers/MixtormatGroupCardPainter.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Rendering/DrawElements.h"
#include "Layout/ArrangedChildren.h"

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
			const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
			const FTextBlockStyle TitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
				Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::CardTitle),
				Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
			ChildSlot
			[
				SNew(STextBlock)
				.Font(TitleStyle.Font)
				.RenderOpacity(Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::CardTitle).Opacity)
				.ColorAndOpacity_Lambda([]() { return FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text)); })
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
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle CardTitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::CardTitle),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const Mixtormat::FMixtormatCardMetrics& Layout = Resolved.CardLayout;
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
			.Padding(0.0f, 0.0f, Layout.Gap, 0.0f)
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
			.Padding(Layout.Gap, 0.0f, 0.0f, 0.0f)
			[ InArgs._HeaderAction.ToSharedRef() ];
		}
		Stack->AddSlot().AutoHeight()
		.Padding(0.0f, Layout.HeaderMarginTop,
			0.0f, Layout.HeaderMarginBottom)
		[
			SAssignNew(HeaderBox, SBox)
			.MinDesiredHeight(Layout.HeaderHeight)
			.VAlign(VAlign_Center)
			.Padding(FMargin(Layout.HeaderLeft,
				Layout.HeaderTop,
				Layout.HeaderRight,
				Layout.HeaderBottom))
			[ Header ]
		];
		if (!InArgs._HeaderOnly)
		{
			Stack->AddSlot().AutoHeight()
			[
				SAssignNew(BodyBox, SBox)
				.Padding(FMargin(Layout.BodyHorizontal,
					Layout.BodyTop,
					Layout.BodyHorizontal,
					Layout.BodyBottom))
				[ InArgs._Content.Widget ]
			];
		}
		ChildSlot
		.Padding(FMargin(Layout.OuterLeft,
			Layout.OuterTop,
			Layout.OuterRight,
			Layout.OuterBottom))
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
			.Padding(0.0f, 0.0f, Layout.Gap, 0.0f)
			[
				// Same rule as the full card header at the top of this function: the caller owns
				// glyph size, hit padding, state and callbacks. Clamping the widget to a
				// glyph-sized square here would crop that padding back off, so the click target
				// would end up smaller than the header's own lead glyph and the two card layouts
				// would disagree about how wide the leading control is.
				InArgs._LeadingHeaderContent.ToSharedRef()
			];
		}
		TitleLine->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			bHasTitle
				? StaticCastSharedRef<SWidget>(
					SNew(STextBlock)
					.Font(CardTitleStyle.Font)
					.ColorAndOpacity(CardTitleStyle.ColorAndOpacity)
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
		.Padding(Layout.Padding, 0.0f, Layout.Padding, Layout.Gap)
		[
			TitleLine
		];
	}

	Stack->AddSlot()
	.AutoHeight()
	[
		SNew(SMixtormatSurfaceBox)
		.Recipe_Lambda([]()
		{
			const Mixtormat::FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
			// Compact cards keep their title outside a flat body; no group-header tail is added.
			Mixtormat::FMixtormatSurfaceRecipe Recipe = Mixtormat::MakeCardBodyRecipe(Theme, 1.0f, 0.0f);
			Recipe.Radius = Theme.Card.Radius;
			return Recipe;
		})
		.InheritWidgetStyle(true)
		// The vertical inset is the gap every run of rows gets under its heading, top and
		// bottom, so a card's first and last row are not flush against its edge.
		.Padding(FMargin(
			Layout.Padding,
			Layout.Gap,
			Layout.Padding,
			Layout.Gap))
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

	const Mixtormat::FMixtormatCardMetrics& Layout = FMixtormatThemeStore::GetResolved().CardLayout;
	const FVector2f OuterSize(AllottedGeometry.GetLocalSize());
	const FVector2f Size(
		FMath::Max(0.0f, OuterSize.X - Layout.OuterLeft - Layout.OuterRight),
		FMath::Max(0.0f, OuterSize.Y - Layout.OuterTop - Layout.OuterBottom));
	const FGeometry CardGeometry = AllottedGeometry.MakeChild(Size,
		FSlateLayoutTransform(FVector2f(Layout.OuterLeft, Layout.OuterTop)));
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return LayerId;
	}

	float HeaderTop = 0.0f;
	float HeaderHeight = 0.0f;
	float BodyTop = 0.0f;
	float BodyHeight = 0.0f;
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
			HeaderTop = Top;
			HeaderHeight = Child.Geometry.GetLocalSize().Y;
		}
		else if (Child.Widget == BodyBox)
		{
			BodyTop = Top;
			BodyHeight = Child.Geometry.GetLocalSize().Y;
		}
	}
	using namespace Mixtormat;
	const FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
	const FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	HeaderTop = FMath::Clamp(HeaderTop, 0.0f, Size.Y);
	const float HeaderEnd = FMath::Clamp(HeaderTop + HeaderHeight, HeaderTop, Size.Y);
	BodyTop = FMath::Clamp(BodyTop, HeaderEnd, Size.Y);
	const float BodyEnd = FMath::Clamp(BodyTop + BodyHeight, BodyTop, Size.Y);

	// Geometry-only adapter: the authored minimum defines the curve domain, while this frame's
	// arranged header defines the physical seam. Short bodies cap each mirrored tail at half-height.
	const float Reach = FMath::Max(Theme.Card.Reach, 0.0f);
	const float NominalHeight = FMath::Max(Theme.CardLayout.HeaderHeight, 1.0f);
	const float Seam = NominalHeight / (NominalHeight + Reach);
	const float TailFraction = BodyEnd > BodyTop ? FMath::Min(Reach / (BodyEnd - BodyTop), 0.5f) : 0.0f;
	const FMixtormatSurfaceRecipe Ground = MakeGroundRecipe();
	const FMixtormatSurfaceRecipe Header = MakeCardHeaderRecipe(Theme, Seam);
	const FMixtormatSurfaceRecipe Body = MakeCardBodyRecipe(Theme, Seam, TailFraction);
	FMixtormatSurfaceDrawStyle DrawStyle;
	DrawStyle.Tint = InWidgetStyle.GetColorAndOpacityTint();
	DrawStyle.Effects = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// Retain the existing rounded overflow clip around both the surface AND nested children.
	const int32 ClipCount = MixtormatGroupCard::PushRoundedClip(OutDrawElements, CardGeometry, Theme.Card.Radius);
	int32 SurfaceLayer = LayerId;
	const auto PaintBand = [&](const float Top, const float Bottom, const FMixtormatSurfaceRecipe& Recipe)
	{
		if (Bottom > Top)
		{
			const FGeometry Band = CardGeometry.MakeChild(FVector2f(Size.X, Bottom - Top),
				FSlateLayoutTransform(FVector2f(0.0f, Top)));
			SurfaceLayer = FMixtormatSurfacePainter::PaintSurface(
				OutDrawElements, SurfaceLayer, Band, Recipe, Palette, InWidgetStyle, DrawStyle);
		}
	};
	// Non-overlapping bands avoid applying inherited alpha twice over the header/body.
	PaintBand(0.0f, HeaderTop, Ground);
	PaintBand(HeaderTop, HeaderEnd, Header);
	PaintBand(HeaderEnd, BodyTop, Ground);
	PaintBand(BodyTop, BodyEnd, Body);
	PaintBand(BodyEnd, Size.Y, Ground);
	const int32 LastLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, SurfaceLayer + 1, InWidgetStyle, bParentEnabled);
	for (int32 Index = 0; Index < ClipCount; ++Index)
	{
		OutDrawElements.PopClip();
	}
	return LastLayer;
}
