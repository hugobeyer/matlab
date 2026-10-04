// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatHelp.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatHelp::Construct(const FArguments& InArgs)
{
	HelpText = InArgs._Text;
	HelpEnabled = InArgs._Enabled;
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
	const ISlateStyle& Style = FMixtormatStyle::Get();
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
	if (bHasTitle)
	{
		Lines->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.HelpTitle")))
			.Text(FText::FromString(DisplayText.Left(Newline).TrimEnd()))
			.AutoWrapText(true)
		];
	}
	Lines->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.HelpBody")))
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
			.Padding(FMargin(MixtormatTokens::HelpPadding))
			[
				Lines
			]
		];
}
