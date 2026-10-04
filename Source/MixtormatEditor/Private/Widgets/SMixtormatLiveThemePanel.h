// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class SScrollBox;

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
	void ChangeColor(FLinearColor Value, FName Name);
	FReply OpenColor(FName Name);
	FReply Save();
	FReply Load();
	FReply ResetAll();
	bool Matches(const FString& Name, const FString& Category) const;
	bool HasMatches(const FString& Category = FString()) const;
	void SelectCategory(const FString& Category);

	TAttribute<bool> CanEdit;
	FSimpleDelegate OnThemeChanged;
	// Empty selects All; search text survives category changes and live workspace refreshes.
	FString SelectedCategory;
	FString Filter;
	TSharedPtr<SScrollBox> TokenScroll;
	FString Status;
};
