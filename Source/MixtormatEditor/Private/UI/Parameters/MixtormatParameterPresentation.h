// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatParameterPresentation
{
	inline TSharedRef<SWidget> MakeCaption(const FText& Text)
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const FTextBlockStyle Style = Mixtormat::FMixtormatTypography::MakeTextStyle(
			Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Caption),
			Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
		return SNew(SBox)
			.Padding(FMargin(Resolved.MenuLayout.ItemInset, Resolved.MenuLayout.CaptionInsetAbove,
				Resolved.MenuLayout.ItemInset, Resolved.MenuLayout.CaptionInsetBelow))
			[
				SNew(STextBlock).Text(Text).Font(Style.Font).ColorAndOpacity(Style.ColorAndOpacity)
				.AutoWrapText(true)
				.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			];
	}

	// Keep the menu surface outside the scroll viewport so its border/shadow do not scroll.
	inline TSharedRef<SWidget> MakeScrollableContent(const TSharedRef<SWidget>& Content, const float Width)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.MaxDesiredHeight(MixtormatTokens::ParameterPopoverMaxHeight)
			[
				SNew(SScrollBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
				.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
				+ SScrollBox::Slot()
				[Content]
			];
	}

	// Form fields and multiline information must grow beyond the inspector's single-line row.
	inline TSharedRef<SWidget> MakeRow(const FText& Label, const TSharedRef<SWidget>& Content,
		const FText& ToolTip = FText::GetEmpty())
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const Mixtormat::FMixtormatControlMetrics& Layout = Resolved.ControlLayout;
		const FTextBlockStyle LabelStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
			Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
			Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
		const TSharedRef<SWidget> Row = SNew(SBox)
			.MinDesiredHeight(Layout.RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(Layout.DropdownLabelRatio).VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, Layout.RowLabelGap, 0.0f)
				[
					SNew(STextBlock).Text(Label)
					.Font(LabelStyle.Font).ColorAndOpacity(LabelStyle.ColorAndOpacity)
					.AutoWrapText(true)
					.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f - Layout.DropdownLabelRatio).VAlign(VAlign_Center)
				[Content]
			];
		if (!ToolTip.IsEmpty())
		{
			return SNew(SMixtormatHelp).Text(ToolTip)[Row];
		}
		return Row;
	}

	inline TSharedRef<SWidget> MakeInfoRow(const FText& Label, const FText& Value,
		const FText& ToolTip = FText::GetEmpty())
	{
		return MakeRow(Label,
			SNew(STextBlock).Text(Value)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.AutoWrapText(true)
			.OverflowPolicy(ETextOverflowPolicy::Clip)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping),
			ToolTip);
	}
}
