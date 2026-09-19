// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// The header of a layer group.
//
// Deliberately the same row as a layer, minus everything a group does not have: no thumbnail, no
// source, no composition badge -- a group composites nothing of its own, it only lends its
// children to the layers under it. What is left is the eye, a folder, the name and the chevron,
// at child-row height so a group reads as an enclosure around layers rather than as a bigger one.
//
// The member count is the one derived field, and it is subdued: it answers "how many did I just
// group" without competing with the name.
class SMixtormatLayerGroupRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerGroupRow)
		: _bEnabled(true)
		, _bExpanded(true)
		, _bSelected(false)
		, _MemberCount(0)
	{}
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ATTRIBUTE(bool, bExpanded)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_ATTRIBUTE(int32, MemberCount)

		SLATE_EVENT(FSimpleDelegate, OnToggleExpanded)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		SLATE_EVENT(FSimpleDelegate, OnToggleEnabled)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
		SLATE_EVENT(FOnTextCommitted, OnNameCommitted)
		// Left drag off the header moves the whole group. The owner builds the operation, so this
		// widget never learns what a group drag carries.
		SLATE_EVENT(FPointerEventHandler, OnDragDetected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// A group's name is undecorated, so the edit box starts from the same text the row shows.
	void BeginRename();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Double-click collapses, the way it does on a layer row. Rename is F2 and the context menu.
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FLinearColor GetBackgroundStart() const;
	FLinearColor GetBackgroundEnd() const;
	FSlateColor GetNameColor() const;
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitType);

	TAttribute<bool> bGroupEnabled;
	TAttribute<bool> bExpanded;
	TAttribute<bool> bSelected;
	FSimpleDelegate OnToggleExpanded;
	FSimpleDelegate OnSelected;
	FSimpleDelegate OnToggleEnabled;
	FPointerEventHandler OnRowDragDetected;
	TSharedPtr<SMenuAnchor> ContextAnchor;
	TAttribute<FText> Name;
	FOnTextCommitted OnNameCommitted;
	TSharedPtr<class SWidgetSwitcher> NameSwitcher;
	TSharedPtr<class SEditableTextBox> NameEditBox;
};
