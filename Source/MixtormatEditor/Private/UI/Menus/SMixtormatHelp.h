// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SMenuAnchor.h"

// A hover-only help host. The child retains input and focus ownership.
class SMixtormatHelp final : public SMenuAnchor
{
public:
	SLATE_BEGIN_ARGS(SMixtormatHelp)
		: _Enabled(true)
	{}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(bool, Enabled)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	// For custom widgets that own their pointer events and cannot be wrapped by SMenuAnchor.
	static TSharedRef<IToolTip> MakeStyledToolTip(const TAttribute<FText>& Text);
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

private:
	void OpenHelp(uint64 Request);
	TSharedRef<SWidget> BuildHelpContent();

	TAttribute<FText> HelpText;
	TAttribute<bool> HelpEnabled;
	uint64 HelpRequest = 0;
};
