#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

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

		// Right-clicking the group. Leave it unbound for the standard menu -- reset, and expand or
		// collapse every group at once -- which the group can build for itself because it already
		// holds its own reset delegate and can see the others through the registry. Bind it only
		// when a group has actions of its own to add.
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	SMixtormatInspectorGroup();
	virtual ~SMixtormatInspectorGroup() override;

	void Construct(const FArguments& InArgs);

	bool IsExpanded() const { return bExpanded; }
	void SetExpanded(bool bInExpanded) { bExpanded = bInExpanded; }

	// Every live group, so "expand all" and "collapse all" can reach them.
	//
	// A registry rather than a list the inspector owns: groups are created in fifteen places by
	// plain SNew, several of them conditionally, and threading a container through all of them to
	// implement two menu entries would put the plumbing in every call site instead of here. The
	// entries are weak and pruned on access, so a group that has been rebuilt leaves nothing
	// behind.
	static TArray<TSharedRef<SMixtormatInspectorGroup>> GetLiveGroups();
	static void SetAllExpanded(bool bInExpanded);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FReply ToggleExpanded();
	FLinearColor GetHeaderTint() const;
	TSharedRef<SWidget> BuildDefaultContextMenu();

	bool bExpanded = false;
	FSimpleDelegate OnReset;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};
