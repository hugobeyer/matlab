// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Dialogs/SMixtormatBakeResultDialog.h"

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

void SMixtormatBakeResultDialog::Construct(const FArguments& InArgs)
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
					MakeActionButton(LOCTEXT("CloseBakeResult", "Close"), EMixtormatBakeResultAction::Close)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("RebakeBakeResult", "Re-bake"), EMixtormatBakeResultAction::Rebake)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("ApplyBakeResult", "Apply to Selected Actors"), EMixtormatBakeResultAction::Apply)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("OpenBakeResult", "Open Material Instance"), EMixtormatBakeResultAction::Open)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("RevealBakeResult", "Reveal Outputs"), EMixtormatBakeResultAction::Reveal)
				]
			]
		]
	];
}

TSharedRef<SWidget> SMixtormatBakeResultDialog::MakeActionButton(
	const FText& Label,
	const EMixtormatBakeResultAction InAction)
{
	return SNew(SButton)
		.Text(Label)
		.OnClicked_Lambda([this, InAction]()
		{
			Action = InAction;
			CloseWindow();
			return FReply::Handled();
		});
}

void SMixtormatBakeResultDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

EMixtormatBakeResultAction ShowMixtormatBakeResultDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Message)
{
	TSharedPtr<SMixtormatBakeResultDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("BakeResultTitle", "Bake Complete"))
		.ClientSize(FVector2D(760.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMixtormatBakeResultDialog)
			.Message(Message)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->GetAction();
}

#undef LOCTEXT_NAMESPACE
