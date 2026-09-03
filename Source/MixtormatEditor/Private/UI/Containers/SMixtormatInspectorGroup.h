#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A collapsible section: a 22px header bar over a body of rows.
//
// The header carries a translucent blue tint at its top lip, settling to the body's flat panel
// colour by its base, with an additive hairline along that top edge -- light catching a raised
// lip rather than a border drawn on it. The whole bar is the hover surface and the whole bar is
// the click target; nothing sits on top of it with its own hover.
//
// Header anatomy is fixed so it never moves between groups:
//   chevron - title - state - custom actions - reset
class SMixtormatInspectorGroup final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatInspectorGroup)
		: _InitiallyExpanded(false)
	{}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(bool, InitiallyExpanded)

		// Short state shown before the actions -- a changed count, a badge. Empty hides it, so a
		// collapsed group still reports itself without being opened.
		SLATE_ATTRIBUTE(FText, StateText)
		SLATE_ATTRIBUTE(FSlateColor, StateColor)

		// Anything the group wants in its header: a preview eye, a toggle. An argument rather than
		// a named slot, so a caller can pass a widget it built conditionally.
		SLATE_ARGUMENT(TSharedPtr<SWidget>, HeaderAction)

		// Bound: a reset control appears last and right-aligned, in the same place in every group.
		SLATE_EVENT(FSimpleDelegate, OnReset)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsExpanded() const { return bExpanded; }

private:
	FReply ToggleExpanded();
	const FSlateBrush* GetHeaderBrush() const;
	FLinearColor GetHeaderTint() const;

	bool bExpanded = false;
};
