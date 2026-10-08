// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// The fixed-width mark that says how a row composites.
//
// A layer's Height Op, a mask's blend mode abbreviated, an effect's type. It is
// deliberately a fixed width rather than hugging its text: the badges then form a scannable column
// down the right edge of the stack, and the word can change without the column moving.
//
// One widget for the layer stack, the child rows and the group headers, so the three can never
// drift apart.
//
// Painted as a small well: a vertical ramp with a hairline along the top. When OnGetMenuContent is
// bound, a left click opens that menu (the row's blend-mode choices) and does not select the row.
class SMixtormatBadge final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBadge) : _bAutoWidth(false) {}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		// Connection badges size to their label within the caller's width budget.
		SLATE_ARGUMENT(bool, bAutoWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	FLinearColor GetTop() const;
	FLinearColor GetBottom() const;

	FOnGetContent OnGetMenuContent;
	TSharedPtr<SMenuAnchor> MenuAnchor;
};
