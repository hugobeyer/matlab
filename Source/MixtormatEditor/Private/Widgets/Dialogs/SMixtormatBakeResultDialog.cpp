// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Dialogs/SMixtormatBakeResultDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatTypography.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormatBakeResultDialog::Construct(const FArguments& InArgs)
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle BodyStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	ChildSlot
	[
		SNew(SBorder)
		.Padding(Resolved.ControlLayout.DialogPadding)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Window")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.ScrollBarStyle(&Style.GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
				.ScrollBarThickness(FVector2D(Resolved.ShellLayout.ScrollbarThickness))
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
					.Text(InArgs._Message)
					.Font(BodyStyle.Font)
					.ColorAndOpacity(BodyStyle.ColorAndOpacity)
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("RebakeBakeResult", "Re-bake"), EMixtormatBakeResultAction::Rebake)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("ApplyBakeResult", "Apply to Selected Actors"), EMixtormatBakeResultAction::Apply)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DialogButtonGap, 0.0f, 0.0f, 0.0f)
				[
					MakeActionButton(LOCTEXT("OpenBakeResult", "Open Material Instance"), EMixtormatBakeResultAction::Open)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DialogButtonGap, 0.0f, 0.0f, 0.0f)
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
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle LabelStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	return SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
		.OnClicked_Lambda([this, InAction]()
		{
			Action = InAction;
			CloseWindow();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(LabelStyle.Font)
			.ColorAndOpacity(LabelStyle.ColorAndOpacity)
		];
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
		.ClientSize(FVector2D(
			FMixtormatThemeStore::GetResolved().ControlLayout.BakeResultDialogWidth,
			FMixtormatThemeStore::GetResolved().ControlLayout.BakeResultDialogHeight))
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
