// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MixtormatGroupParameterTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnMixtormatGroupControlCommit, const FMixtormatGroupControl&, FText&);

// A draft editor: closing the popup without committing never changes the document.
class SMixtormatGroupControlPopover final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGroupControlPopover)
		: _bEditing(false), _bTypeLocked(false), _bLabelOnly(false) {}
		SLATE_ARGUMENT(FMixtormatGroupControl, Control)
		SLATE_ARGUMENT(bool, bEditing)
		SLATE_ARGUMENT(bool, bTypeLocked)
		SLATE_ARGUMENT(bool, bLabelOnly)
		SLATE_EVENT(FOnMixtormatGroupControlCommit, OnCommit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildTypeMenu();
	void ChangeType(EMixtormatParameterValueType Type);
	void RebuildValueFields();
	FReply Commit();
	FReply Cancel();

	FMixtormatGroupControl Draft;
	bool bEditing = false;
	bool bTypeLocked = false;
	bool bLabelOnly = false;
	FText Error;
	TSharedPtr<SVerticalBox> ValueFields;
	FOnMixtormatGroupControlCommit OnCommit;
};
