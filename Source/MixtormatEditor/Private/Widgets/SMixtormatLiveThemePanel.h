// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;

// The token authoring panel.
//
// Rows are built once per registry entry and filtered with a per-row visibility lambda, so switching
// tab or typing in the search box costs a visibility pass rather than a rebuild. Four editor kinds
// share one row shell; which one a row uses comes from the registry type, not from its category name.
class SMixtormatLiveThemePanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLiveThemePanel) : _CanEdit(true) {}
		SLATE_ATTRIBUTE(bool, CanEdit)
		SLATE_EVENT(FSimpleDelegate, OnThemeChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void ChangeNumber(float Value, FName Name);
	void ChangeBool(bool Value, FName Name);
	void ChangeChoice(int32 Value, FName Name);
	void ChangeColor(FLinearColor Value, FName Name);
	FReply OpenColor(FName Name);
	FReply Save();
	FReply Load();
	FReply ResetAll();
	bool Matches(const FString& Name) const;
	bool HasMatches() const;
	void SelectTab(int32 Index);

	TAttribute<bool> CanEdit;
	FSimpleDelegate OnThemeChanged;
	// Strip slot index; INDEX_NONE means All. Search text survives tab changes and refreshes.
	int32 SelectedTab = INDEX_NONE;
	FString Filter;
	// Held on the widget, not in Construct: the search hint reads it on every tick of the text box,
	// long after the local that built the tabs is gone.
	TArray<FString> TabLabels;
	TSharedPtr<SScrollBox> TokenScroll;
	FString Status;
};
