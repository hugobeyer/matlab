// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatGroupButton.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

// SButton remains responsible for input, focus, sounds and activation.
class SMixtormatGroupAction : public SButton
{
public:
	void Construct(const SButton::FArguments& InArgs, const bool bShowSeparator = true, const float MinHeightOverride = -1.0f)
	{
		ButtonStyle = MixtormatGroupButton::MakeButtonStyle(*InArgs._ButtonStyle);
		const FTextBlockStyle& TextStyle = FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText"));
		TSharedRef<SWidget> Content = InArgs._Content.Widget;
		if (Content == SNullWidget::NullWidget)
		{
			// Boxed and centred, not a bare text block. The surface gives every action the same
			// MinDesiredHeight, but a bare STextBlock is arranged top-aligned inside that box while an
			// icon+label row is centred by its slots -- so a text-only action sat visibly higher than
			// its neighbours. This is the same wrapper SMixtormatSegment already uses.
			Content = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(InArgs._Text).TextStyle(&TextStyle)
					.ColorAndOpacity(FSlateColor::UseForeground())
			];
		}
		SButton::FArguments Args = InArgs;
		Args.ButtonStyle(&ButtonStyle).ContentPadding(0.0f).TextStyle(&TextStyle)
			.ToolTipText(FText::GetEmpty());
		Args
		[
			SNew(SMixtormatHelp)
			.Text(InArgs._ToolTipText)
			.Enabled_Lambda([this]() { return IsEnabled(); })
			[
				SNew(SBox).MinDesiredHeight(MinHeightOverride >= 0.0f ? MinHeightOverride : FMixtormatThemeStore::GetResolved().Buttons.Height)
		.Padding(FMargin(0.0f, 0.0f, 1.0f, 0.0f))
				[
					SNew(SMixtormatGroupButtonSurface)
					.Hovered_Lambda([this]() { return IsHovered(); })
					.Pressed_Lambda([this]() { return IsPressed(); })
					.ShowSeparator(bShowSeparator)
					.Padding(FMargin(FMixtormatThemeStore::GetResolved().Buttons.HorizontalPadding, 0.0f))
					[Content]
				]
			]
		];
		SButton::Construct(Args);
		if (InArgs._ToolTipText.IsSet())
		{
			// Slate may apply inherited widget arguments before this Construct runs.
			SetToolTipText(FText::GetEmpty());
		}
	}

private:
	// SButton retains this address, not a copy of the adapter.
	FButtonStyle ButtonStyle;
};
