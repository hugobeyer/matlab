// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

enum class EMixtormatBakeResultAction : uint8
{
	Close,
	Reveal,
	Open,
	Apply,
	Rebake
};

class SMixtormatBakeResultDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBakeResultDialog) {}
		SLATE_ARGUMENT(FText, Message)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	EMixtormatBakeResultAction GetAction() const { return Action; }

private:
	TSharedRef<SWidget> MakeActionButton(const FText& Label, EMixtormatBakeResultAction InAction);
	void CloseWindow();

	EMixtormatBakeResultAction Action = EMixtormatBakeResultAction::Close;
};

EMixtormatBakeResultAction ShowMixtormatBakeResultDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Message);
