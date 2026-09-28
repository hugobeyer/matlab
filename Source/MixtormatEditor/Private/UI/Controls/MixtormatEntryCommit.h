// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"

class IInputProcessor;
class SWidget;
struct FGeometry;
struct FKeyEvent;

// The one set of commit rules for every typed field in Mixtormat (slider entry, layer and group
// rename, ...):
//
//     accept: Enter, Tab, focus loss (clicking elsewhere, leaving the window), left click anywhere
//     cancel: Escape, right click anywhere
//
// Slate reports Escape and a window deactivation alike as ETextCommit::OnCleared, so the commit
// type cannot tell them apart; this tracks the cancel itself. Usage: Begin() when the field takes
// focus, route the field's OnKeyDownHandler to HandleKeyDown, and at the top of OnTextCommitted
// call Finish(), which ends the session and says whether to discard.
class FMixtormatEntryCommit final : public TSharedFromThis<FMixtormatEntryCommit>
{
public:
	~FMixtormatEntryCommit();

	// Starts watching clicks app-wide. OnClosedWithoutCommit runs when a cancel or accept found
	// the field already unfocused, so no commit will arrive to close it.
	void Begin(const TSharedRef<SWidget>& EntryWidget, TFunction<void()> OnClosedWithoutCommit = nullptr);

	// Ends the session. True when the commit that just arrived must be discarded.
	bool Finish();

	bool IsActive() const { return Watcher.IsValid(); }

	FReply HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	void Cancel();
	void Accept();
	bool IsOverEntry(const FVector2D& ScreenPosition) const;

private:
	void StopWatching();
	void CloseIfNoCommitArrived();

	TWeakPtr<SWidget> Entry;
	TSharedPtr<IInputProcessor> Watcher;
	TFunction<void()> ClosedWithoutCommit;
	bool bCancel = false;
};
