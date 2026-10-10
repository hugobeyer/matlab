// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// One Sources shelf row: glyph, name, kind. Left click selects; right click opens the row's
// context menu. Paint comes from SMixtormatLayerSurface, so a source row reads like the rows
// above it without inheriting any of a layer row's layer semantics.
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
		// Right click. Unbound leaves the row without a menu rather than with a broken one.
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	FReply OpenContextMenu();

	// Copied out of the args: the enabled binding is read per paint, after Construct has returned.
	TAttribute<bool> Enabled;
	FSimpleDelegate OnSelected;
	TSharedPtr<SMenuAnchor> ContextAnchor;
	bool bHasContextMenu = false;
	bool bHovered = false;
};
