// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// A mask, effect or generated mask inside a layer.
//
// Shorter than a layer row and indented under it, with the same three fields -- name, kind, badge
// -- and a status dot instead of an eye. Selected children carry the tint from their right edge
// rather than their top, so a selected child never looks like a small layer.
//
// Like the layer row, the body of this one is the drag source and the right button is where its
// actions live. There is no grip and no overflow button: at 20px tall they would leave the name
// almost no width, and the child is reordered by dragging it rather than by aiming at a handle.
class SMixtormatLayerChildRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerChildRow)
		: _Connector(nullptr)
		, _bActive(true)
		, _bSelected(false)
		, _bInstanceSource(false)
		, _ExtraIndent(0.0f)
		, _bPreviewing(false)
	{}
		// A glyph saying what kind of child this is. A slot rather than a brush so the row does not
		// have to know how the glyph is tinted -- which mask it is, is a hover away, not in here.
		SLATE_NAMED_SLOT(FArguments, Icon)
		// Tree connector drawn before the glyph for a scoped child: a tee while more children of
		// the same owner follow, an elbow on the last one. Null for a top-level child.
		SLATE_ARGUMENT(const FSlateBrush*, Connector)
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(FText, Kind)
		SLATE_ATTRIBUTE(FText, Badge)
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_ATTRIBUTE(bool, bSelected)
		// This child is the source of the selected instance: it glows so the link is visible.
		SLATE_ATTRIBUTE(bool, bInstanceSource)
		SLATE_ARGUMENT(float, ExtraIndent)
		// The viewport is showing a debug or child-output preview taken from this child: the dot
		// turns the preview colour for as long as that holds.
		SLATE_ATTRIBUTE(bool, bPreviewing)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		// The dot is the child's enable toggle -- it already shows the state, so it takes the
		// click too rather than adding a checkbox the row has no room for.
		SLATE_EVENT(FSimpleDelegate, OnToggleActive)
		SLATE_EVENT(FPointerEventHandler, OnDragDetected)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
		// Shift + right button: the same published-output menu the full context menu nests under
		// "Outputs", opened directly. An accelerator only -- the full menu keeps its own entry.
		SLATE_EVENT(FOnGetContent, OnGetOutputMenu)
		// Whether this child publishes anything copyable. False leaves Shift + right button on the
		// normal menu rather than opening an empty popup.
		SLATE_ATTRIBUTE(bool, bHasOutputMenu)
		// Left click on the badge: the child's blend mode, when it has one.
		SLATE_EVENT(FOnGetContent, OnGetBadgeMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:

	// Which of the two menus the anchor should build for the open it is about to do: the full
	// context menu, or the published-output menu Shift + right button asked for.
	TSharedRef<SWidget> BuildContextMenuContent();

	TAttribute<bool> bSelected;
	TAttribute<bool> bInstanceSource;
	FSimpleDelegate OnSelected;
	FPointerEventHandler OnRowDragDetected;
	FOnGetContent OnGetContextMenu;
	FOnGetContent OnGetOutputMenu;
	TAttribute<bool> bHasOutputMenu;
	// Set for the duration of one open, in OnMouseButtonDown, before the anchor is opened.
	bool bOutputMenuOnly = false;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};
