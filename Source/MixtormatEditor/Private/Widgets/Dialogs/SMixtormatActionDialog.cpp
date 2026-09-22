// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Dialogs/SMixtormatActionDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormatActionDialog::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(MixtormatTokens::DialogPadding)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
					.Text(InArgs._Message)
					.AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, MixtormatTokens::DialogActionsTopMargin, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(InArgs._CancelLabel)
					.OnClicked(this, &SMixtormatActionDialog::Cancel)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(InArgs._ConfirmLabel)
					.OnClicked(this, &SMixtormatActionDialog::Confirm)
				]
			]
		]
	];
}

FReply SMixtormatActionDialog::Confirm()
{
	bConfirmed = true;
	CloseWindow();
	return FReply::Handled();
}


FReply SMixtormatActionDialog::Cancel()
{
	CloseWindow();
	return FReply::Handled();
}

void SMixtormatActionDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

bool ShowMixtormatActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& CancelLabel)
{
	TSharedPtr<SMixtormatActionDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(MixtormatTokens::ActionDialogWidth, MixtormatTokens::ActionDialogHeight))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMixtormatActionDialog)
			.Message(Message)
			.ConfirmLabel(ConfirmLabel)
			.CancelLabel(CancelLabel)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->WasConfirmed();
}


#undef LOCTEXT_NAMESPACE
