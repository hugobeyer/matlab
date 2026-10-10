// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

void SMixtormatIconRail::Construct(const FArguments& InArgs)
{
	ActiveIndex = InArgs._ActiveIndex;
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const Mixtormat::FMixtormatIconStyle& Role = Resolved.Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::NavigationRail)];
	const auto& Layout = Resolved.PreviewLayout;
	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
	for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
	{
		const TAttribute<bool> bActive = TAttribute<bool>::CreateLambda(
			[Active = ActiveIndex, Index]() { return Active.Get(0) == Index; });
		const FMixtormatOnSegmentChosen OnChosen = InArgs._OnChosen;
		const FText Label = InArgs._Labels.IsValidIndex(Index) ? InArgs._Labels[Index] : FText::GetEmpty();
		Rail->AddSlot().AutoHeight()
		.Padding(Layout.LeftRailInnerPadding, Layout.LeftRailInnerPadding,
			Layout.LeftRailInnerPadding, Layout.LeftRailButtonGap)
		[
			SNew(SMixtormatHelp)
			.Text(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
			[
				SNew(SBox)
				.WidthOverride(Layout.LeftRailButtonWidth)
				.HeightOverride(Layout.LeftRailButtonHeight)
				[
					SNew(SCheckBox)
					.Style(&FMixtormatStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.ViewportOverlayToggle")))
					.IsChecked_Lambda([bActive]()
					{
						return bActive.Get(false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([OnChosen, Index](ECheckBoxState)
					{
						OnChosen.ExecuteIfBound(Index);
					})
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(Role.GlyphSize)
							.HeightOverride(Role.GlyphSize)
							[
								SNew(SImage).Image(InArgs._Options[Index])
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						.Padding(0.0f, Layout.LeftRailLabelGap, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(Label)
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted))
						]
					]
				]
			]
		];
	}
	ChildSlot [ Rail ];
}

int32 SMixtormatIconRail::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const auto& Layout = Resolved.PreviewLayout;
	const FVector2f Size(Geometry.GetLocalSize());
	const float Radius = Layout.LeftRailCornerRadius;
	const float ShadowRadius = Layout.LeftRailShadowRadius;
	const float ShadowOffset = Layout.LeftRailShadowOffset;
	if (Layout.LeftRailShadowOpacity > 0.0f && Size.X > 0.0f && Size.Y > 0.0f)
	{
		FSlateRoundedBoxBrush Shadow(FLinearColor::Black, Radius + ShadowRadius);
		FSlateDrawElement::MakeBox(Elements, LayerId, Geometry.ToPaintGeometry(
			Size, FSlateLayoutTransform(FVector2f(ShadowOffset, ShadowOffset))),
			&Shadow, ESlateDrawEffect::None,
			FLinearColor(0.0f, 0.0f, 0.0f, Layout.LeftRailShadowOpacity));
	}
	if (Layout.LeftRailBorderThickness > 0.0f && Layout.LeftRailBorderOpacity > 0.0f)
	{
		FLinearColor Border = Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
		Border.A *= Layout.LeftRailBorderOpacity;
		FSlateRoundedBoxBrush Outline(FLinearColor::Transparent, Radius, Border, Layout.LeftRailBorderThickness);
		FSlateDrawElement::MakeBox(Elements, LayerId + 1, Geometry.ToPaintGeometry(),
			&Outline, ESlateDrawEffect::None, WidgetStyle.GetColorAndOpacityTint());
	}
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
		LayerId + 2, WidgetStyle, bParentEnabled);
}
