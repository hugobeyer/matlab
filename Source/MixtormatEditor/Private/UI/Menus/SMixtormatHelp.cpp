// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatHelp.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	void ClearNativeToolTips(const TSharedRef<SWidget>& Widget)
	{
		Widget->SetToolTipText(FText::GetEmpty());
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				ClearNativeToolTips(Children->GetChildAt(Index));
			}
		}
	}
}

void SMixtormatHelp::Construct(const FArguments& InArgs)
{
	HelpText = InArgs._Text;
	HelpEnabled = InArgs._Enabled;

	// This subtree has one help system. Remove any native Slate tooltip inherited by child widgets.
	ClearNativeToolTips(InArgs._Content.Widget);

	SMenuAnchor::Construct(
		SMenuAnchor::FArguments()
		.Placement(MenuPlacement_BelowAnchor)
		.Method(EPopupMethod::UseCurrentWindow)
		.UseApplicationMenuStack(false)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SMixtormatHelp::BuildHelpContent))
		[
			InArgs._Content.Widget
		]);
}

void SMixtormatHelp::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SMenuAnchor::OnMouseEnter(MyGeometry, MouseEvent);
	const uint64 Request = ++HelpRequest;
	if (!HelpEnabled.Get(true) || HelpText.Get(FText::GetEmpty()).IsEmpty())
	{
		return;
	}

	const TWeakPtr<SMixtormatHelp> WeakThis = SharedThis(this);
	RegisterActiveTimer(MixtormatTokens::HelpDelay,
		FWidgetActiveTimerDelegate::CreateLambda([WeakThis, Request](double, float)
		{
			if (const TSharedPtr<SMixtormatHelp> Help = WeakThis.Pin())
			{
				Help->OpenHelp(Request);
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void SMixtormatHelp::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Cancel logically: stale one-shot callbacks cannot reopen help after leave/re-entry.
	++HelpRequest;
	SetIsOpen(false, false);
	SMenuAnchor::OnMouseLeave(MouseEvent);
}

FReply SMixtormatHelp::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Cancel hover help before the child opens a dropdown or context menu, without consuming its click.
	++HelpRequest;
	SetIsOpen(false, false);
	return SMenuAnchor::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

void SMixtormatHelp::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SMenuAnchor::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (IsOpen() && !HelpEnabled.Get(true))
	{
		++HelpRequest;
		SetIsOpen(false, false);
	}
}

void SMixtormatHelp::OpenHelp(const uint64 Request)
{
	if (Request == HelpRequest && IsHovered() && HelpEnabled.Get(true)
		&& !HelpText.Get(FText::GetEmpty()).IsEmpty())
	{
		SetIsOpen(true, false);
	}
}

TSharedRef<SWidget> SMixtormatHelp::BuildHelpContent()
{
	const FText Text = HelpText.Get(FText::GetEmpty());
	const FString DisplayText = Text.ToString();
	int32 Newline = INDEX_NONE;
	const bool bHasTitle = DisplayText.FindChar(TEXT('\n'), Newline) && Newline > 0;
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle CaptionTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Caption),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const FTextBlockStyle BodyTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
	if (bHasTitle)
	{
		Lines->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(CaptionTextStyle.Font)
			.ColorAndOpacity(CaptionTextStyle.ColorAndOpacity)
			.Text(FText::FromString(DisplayText.Left(Newline).TrimEnd()))
			.AutoWrapText(true)
		];
	}
	Lines->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Font(BodyTextStyle.Font)
		.ColorAndOpacity(BodyTextStyle.ColorAndOpacity)
		.Text(bHasTitle ? FText::FromString(DisplayText.Mid(Newline + 1)) : Text)
		.AutoWrapText(true)
	];

	return SNew(SBox)
		.MaxDesiredWidth(MixtormatTokens::HelpMaxWidth)
		.Visibility_Lambda([TextAttribute = HelpText, EnabledAttribute = HelpEnabled]()
		{
			return EnabledAttribute.Get(true) && !TextAttribute.Get(FText::GetEmpty()).IsEmpty()
				? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatMenuPanel)
			.Visibility(EVisibility::HitTestInvisible)
			.Padding(FMargin(MixtormatTokens::HelpPadding))
			[
				Lines
			]
		];
}
