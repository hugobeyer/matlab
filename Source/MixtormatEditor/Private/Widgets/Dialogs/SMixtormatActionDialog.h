// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

enum class EMixtormatActionDialogResult : uint8
{
	Cancel,
	Confirm,
	Alternate
};

class SMixtormatActionDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatActionDialog) {}
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(FText, ConfirmLabel)
		SLATE_ARGUMENT(FText, CancelLabel)
		SLATE_ARGUMENT(FText, AlternateLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool WasConfirmed() const { return Result == EMixtormatActionDialogResult::Confirm; }
	EMixtormatActionDialogResult GetResult() const { return Result; }

private:
	FReply Confirm();
	FReply Alternate();
	FReply Cancel();
	void CloseWindow();

	EMixtormatActionDialogResult Result = EMixtormatActionDialogResult::Cancel;
};

bool ShowMixtormatActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& CancelLabel);

EMixtormatActionDialogResult ShowMixtormatThreeActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& AlternateLabel,
	const FText& CancelLabel);
