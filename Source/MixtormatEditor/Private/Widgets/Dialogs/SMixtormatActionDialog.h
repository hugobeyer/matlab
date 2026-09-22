// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"


class SMixtormatActionDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatActionDialog) {}
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(FText, ConfirmLabel)
		SLATE_ARGUMENT(FText, CancelLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool WasConfirmed() const { return bConfirmed; }

private:
	FReply Confirm();
	FReply Cancel();
	void CloseWindow();

	bool bConfirmed = false;
};

bool ShowMixtormatActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& CancelLabel);

