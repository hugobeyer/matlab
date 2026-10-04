// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatBadge.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "Styling/CoreStyle.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatBadge::Construct(const FArguments& InArgs)
{
	OnGetMenuContent = InArgs._OnGetMenuContent;
	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle BadgeTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Badge),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	ChildSlot
	[
		SAssignNew(MenuAnchor, SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(OnGetMenuContent)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::BadgeWidth)
			.HeightOverride(MixtormatTokens::BadgeHeight)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SMixtormatGradientBox)
					.StartColor(this, &SMixtormatBadge::GetTop)
					.EndColor(this, &SMixtormatBadge::GetBottom)
					.Orientation(Orient_Vertical)
					.CornerRadius(MixtormatTokens::BadgeCornerRadius)
				]
				// The lip along the top edge, like the layer rows carry.
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				.Padding(FMargin(MixtormatTokens::BadgeCornerRadius, 0.0f))
				[
					SNew(SBox)
					.HeightOverride(MixtormatTokens::HairlineThickness)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.ColorAndOpacity(FSlateColor(MixtormatPalette::BadgeHairline()))
					]
				]
				+ SOverlay::Slot()
				.Padding(FMargin(MixtormatTokens::BadgeTextInset, 0.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(BadgeTextStyle.Font)
					.ColorAndOpacity(BadgeTextStyle.ColorAndOpacity)
					.Text(InArgs._Text)
				]
			]
		]
	];
}

FLinearColor SMixtormatBadge::GetTop() const
{
	const bool bLive = OnGetMenuContent.IsBound() && (IsHovered() || (MenuAnchor.IsValid() && MenuAnchor->IsOpen()));
	return bLive ? MixtormatPalette::BadgeTopHover() : MixtormatPalette::BadgeTop();
}

FLinearColor SMixtormatBadge::GetBottom() const
{
	const bool bLive = OnGetMenuContent.IsBound() && (IsHovered() || (MenuAnchor.IsValid() && MenuAnchor->IsOpen()));
	return bLive ? MixtormatPalette::BadgeBottomHover() : MixtormatPalette::BadgeBottom();
}

FReply SMixtormatBadge::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (!OnGetMenuContent.IsBound() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	if (MenuAnchor.IsValid())
	{
		MenuAnchor->SetIsOpen(!MenuAnchor->IsOpen());
	}
	// Handled, so the row behind neither selects nor starts a drag.
	return FReply::Handled();
}

FReply SMixtormatBadge::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FCursorReply SMixtormatBadge::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return OnGetMenuContent.IsBound() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}
