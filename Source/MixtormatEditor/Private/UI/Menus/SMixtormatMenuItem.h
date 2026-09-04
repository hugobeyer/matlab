#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;
struct FSlateBrush;

// One row in a popover.
//
// The design's rule for a menu is that the thing under the cursor looks identical tool-wide: a
// hovered row carries the same fill a slider does, down to the multiply pass across it. That is
// why this is a widget rather than an FMenuBuilder entry -- a multibox row can be given our fonts
// and colours, but its hover is a brush, and a brush cannot hold the two-axis gradient the rest of
// the tool hovers with.
//
// Anatomy, fixed so nothing shifts between rows:
//   [ icon | check ]  label  ......  shortcut  [ submenu chevron ]
//
// Rows with no icon keep the gutter, so their labels stay on the same left edge as rows that have
// one -- a menu whose text steps in and out by eleven pixels reads as two menus.
class SMixtormatMenuItem final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatMenuItem)
		: _Icon(nullptr)
		, _bChecked(false)
		, _bEnabled(true)
		, _bDestructive(false)
	{}
		SLATE_ATTRIBUTE(FText, Label)
		// Printed quietly on the right. Never a control -- it teaches the shortcut, it does not
		// offer it.
		SLATE_ATTRIBUTE(FText, Shortcut)
		SLATE_ARGUMENT(const FSlateBrush*, Icon)

		// A ticked row shows a check in the icon gutter, so a checkable row and an icon row are
		// never both competing for that space.
		SLATE_ATTRIBUTE(bool, bChecked)
		SLATE_ATTRIBUTE(bool, bEnabled)

		// Destructive rows keep the hover's shape and change only its hue: the gesture reads the
		// same, the consequence does not.
		SLATE_ARGUMENT(bool, bDestructive)

		SLATE_EVENT(FSimpleDelegate, OnActivate)

		// Bound for a row that opens a submenu instead of acting. The chevron appears, the row
		// stops dismissing the menu, and hovering is enough to open it.
		SLATE_EVENT(FOnGetContent, OnGetSubMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	FLinearColor GetFillTop() const;
	FLinearColor GetFillBottom() const;
	FLinearColor GetShadeStart() const;
	FLinearColor GetShadeMid() const;
	FLinearColor GetShadeEnd() const;
	FSlateColor GetLabelColor() const;
	bool IsRowEnabled() const;

	TAttribute<bool> bChecked;
	TAttribute<bool> bRowEnabled;
	bool bDestructive = false;
	FSimpleDelegate OnActivate;
	TSharedPtr<SMenuAnchor> SubMenuAnchor;
};
