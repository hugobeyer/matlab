// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// One Sources shelf row: glyph, name, kind. Left click selects; right click opens the row's
// context menu. Its background uses the shared menu-row recipe inside the Sources
// card, without adopting material-layer appearance or composition semantics.
//
// All content is attribute-bound by SourceId, so a parameter edit elsewhere (an enabled toggle in
// the inspector, a rename) updates the row without a list rebuild. The list is rebuilt only when
// rows are added or removed.
class SMixtormatSourceRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSourceRow) {}
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(FText, Kind)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		SLATE_EVENT(FOnTextCommitted, OnNameCommitted)
		SLATE_EVENT(FSimpleDelegate, OnToggleEnabled)
		// Right click. Unbound leaves the row without a menu rather than with a broken one.
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void BeginRename();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	FReply OpenContextMenu();
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitType);
	void ToggleEnabled();

	// Copied out of the args: the enabled binding is read per paint, after Construct has returned.
	TAttribute<bool> Enabled;
	FSimpleDelegate OnSelected;
	FSimpleDelegate OnToggleEnabled;
	FOnTextCommitted OnNameCommitted;
	TAttribute<FText> EditableName;
	TSharedPtr<class SWidgetSwitcher> NameSwitcher;
	TSharedPtr<class SEditableTextBox> NameEditBox;
	TSharedPtr<class FMixtormatEntryCommit> NameEntry;
	TSharedPtr<SMenuAnchor> ContextAnchor;
	bool bHasContextMenu = false;
	bool bHovered = false;
};
