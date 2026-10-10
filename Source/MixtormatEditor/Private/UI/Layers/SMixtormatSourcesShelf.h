// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;

// The Sources shelf: a collapsible header above layer actions with a compact,
// non-compositing array card for reusable generator outputs.
//
// A list, not a composition. Entries publish fields other operations consume; they never blend
// into one another, and their order here has no bearing on evaluation.
//
// Chrome only: it paints the foldout header bar -- chevron, title, and the whole bar as the click
// target -- and shows or hides its content slot. Both the body and the expanded flag belong to
// the caller, because the layer stack panel is rebuilt on a theme refresh and an open shelf has
// to survive that.
//
// Hover is read from the header button itself, not from this widget: the shelf also holds the
// expanded body, and a shelf-wide hover would keep the bar lit while the cursor works down there.
class SMixtormatSourcesShelf final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSourcesShelf)
		: _Expanded(false)
	{}
		// Read per frame, not copied: flipping the caller's flag has to move the chevron and the
		// body without rebuilding the panel.
		SLATE_ATTRIBUTE(bool, Expanded)

		// The whole bar toggles. The caller owns the state; the shelf never holds it.
		SLATE_EVENT(FSimpleDelegate, OnToggle)

		SLATE_ARGUMENT(FText, Title)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply ToggleExpanded();

	TAttribute<bool> Expanded;
	FSimpleDelegate OnToggle;
	// Downward reference only (parent holds a child), so a strong pointer is safe here; the hover
	// binding reads it back per paint.
	TSharedPtr<SButton> HeaderButton;
};
