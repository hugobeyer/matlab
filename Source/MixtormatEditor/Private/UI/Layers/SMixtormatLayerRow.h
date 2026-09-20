// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// One layer in the stack.
//
// Three text fields, answering three different questions, which is what lets a stack of layers all
// named "Untitled" still be readable:
//
//   Name     what the user called it
//   Source   what it is made of      -- "Mat - Rust Orange", "FILL"
//   Badge    how it composites       -- BLEND / OVER / COAT / DETAIL
//   ColorBadge  how it blends colour -- MULT / SCREEN / ... , absent at Normal
//
// The badge is the only derived field: it is not typed, it is read from the layer's composition
// mode, and it is fixed-width so the badges form a scannable column down the right edge.
//
// The row is also the layer's own header: it carries the tint gradient and the additive hairline
// along its top edge, which is what encloses the masks and effects listed under it. There is no
// drag handle and no overflow button -- the body of the row is the drag source and the right
// button is where a layer's actions live, so nothing eats the width the name needs.
class SMixtormatLayerRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerRow)
		: _bEnabled(true)
		, _bExpanded(false)
		, _bSelected(false)
		, _bReference(false)
		, _bSolo(false)
		, _bCanDisable(true)
	{}
		SLATE_ATTRIBUTE(FText, Name)
		// What the edit box starts from, which is not what the row shows: Name is decorated --
		// a reference layer reads "Foo (ref)" and an unnamed one borrows its asset's name --
		// and committing that back would write the decoration into the layer.
		SLATE_ATTRIBUTE(FText, EditableName)
		SLATE_ATTRIBUTE(FText, Source)
		SLATE_ATTRIBUTE(FText, Badge)
		// A second, optional mark for a layer that also blends its colour. Collapsed when empty,
		// which is the usual case -- see MixtormatLayerBadges::ForColorBlendMode.
		SLATE_ATTRIBUTE(FText, ColorBadge)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ATTRIBUTE(bool, bExpanded)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_ATTRIBUTE(bool, bReference)
		// Soloed layers light the eye in the accent, so the one layer the preview is showing is
		// visible without a second control in the row.
		SLATE_ATTRIBUTE(bool, bSolo)
		// The base layer cannot be hidden. Its eye is drawn but inert, rather than absent, so the
		// column of eyes down the stack stays a straight line.
		SLATE_ARGUMENT(bool, bCanDisable)

		// A widget, not a brush: thumbnails come from FAssetThumbnail::MakeThumbnailWidget() and
		// a fill layer's swatch is an SColorBlock. Neither can be reduced to an FSlateBrush*.
		SLATE_NAMED_SLOT(FArguments, Thumbnail)

		SLATE_EVENT(FSimpleDelegate, OnToggleExpanded)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		// Plain click hides or shows the layer; ctrl or alt solos it.
		SLATE_EVENT(FSimpleDelegate, OnToggleEnabled)
		SLATE_EVENT(FSimpleDelegate, OnToggleSolo)
		// Left drag off the row body. The owner builds the drag operation, so this widget never
		// has to know what a layer drag carries.
		SLATE_EVENT(FPointerEventHandler, OnDragDetected)
		// Right button. The row selects itself first, so the menu always acts on what it opened on.
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
		// Enter or focus loss commits; Escape arrives as OnCleared and is dropped.
		SLATE_EVENT(FOnTextCommitted, OnNameCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Swaps the name for an edit box and puts the caret in it. Driven from F2 (or its F12 alias)
	// and the context menu; deliberately not from double-click, which opens and shuts the layer.
	void BeginRename();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Opens or shuts the layer, the same as the chevron. Rename is F2/F12 and the context menu, never
	// this -- a double-click that sometimes collapses and sometimes starts editing a name would
	// have to be guessed at every time.
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FLinearColor GetBackgroundStart() const;
	FLinearColor GetBackgroundEnd() const;
	const FSlateBrush* GetHairlineBrush() const;
	FSlateColor GetNameColor() const;
	void HandleEyeClicked(const FPointerEvent& MouseEvent);
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitType);

	TAttribute<bool> bLayerEnabled;
	TAttribute<bool> bExpanded;
	TAttribute<bool> bSelected;
	TAttribute<bool> bReference;
	FSimpleDelegate OnToggleExpanded;
	FSimpleDelegate OnSelected;
	FSimpleDelegate OnToggleEnabled;
	FSimpleDelegate OnToggleSolo;
	FPointerEventHandler OnRowDragDetected;
	TSharedPtr<SMenuAnchor> ContextAnchor;
	TAttribute<FText> EditableName;
	FOnTextCommitted OnNameCommitted;
	TSharedPtr<class SWidgetSwitcher> NameSwitcher;
	TSharedPtr<class SEditableTextBox> NameEditBox;
};
