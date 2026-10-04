// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatResolvedStyle.h"
#include "Style/MixtormatTheme.h"

// Owns the theme and the resolved style derived from it.
// 
// This is the answer to "where does a widget get its colours from" -- one object, one accessor.
// Before this existed the only way to reach a resolved value was to rebuild the whole chain, which
// is why nothing had tried.
//
// Lifecycle (§66):
//
//     SetTheme -> validate -> resolve -> rebuild resolved -> notify
//
// Validation runs before resolution on purpose: resolution assumes legal input, so a clamp applied
// after it would leave a colour that no longer matches the number the author typed.
//
// Deliberately not here yet: JSON persistence and the UI STYLE panel. Those arrive at Stage 9 and
// will call SetTheme; adding a file format now would mean shipping a schema nothing edits.
//
// Lives at global scope, beside FMixtormatStyle, rather than inside namespace Mixtormat: it is a
// process-level singleton in the same way FMixtormatStyle is, and its types are therefore spelled
// with the Mixtormat:: qualifier.
class FMixtormatThemeStore final
{
public:
	// The resolved style. Safe to call from anywhere at any time; the theme is initialised to its
	// authored defaults on first use rather than at module load, so nothing depends on module order.
	static const Mixtormat::FMixtormatResolvedStyle& GetResolved();

	// The editable theme. Read-only to callers -- a change goes through SetTheme so it cannot skip
	// validation or leave the resolved style stale.
	static const Mixtormat::FMixtormatTheme& GetTheme();

	// Replaces the theme, validates, resolves, and notifies. The only way to change the look.
	static void SetTheme(Mixtormat::FMixtormatTheme InTheme);

	// Re-resolves the current theme and re-notifies, without changing it. For an edit that touched
	// a live-theme value rather than a theme field.
	static void Refresh();

	// Restores the authored defaults.
	static void ResetToDefaults();

	// Issues raised by the last ValidateTheme, for the authoring UI to show. Empty when the last
	// change was clean.
	static const TArray<FText>& GetValidationIssues();

	// True when the last change altered something a widget has to be rebuilt for, as opposed to
	// something a repaint covers. Read with ConsumeRebuildRequest, which clears it -- a flag that
	// stayed set would rebuild every widget on the next unrelated edit.
	//
	// For now every change requests a rebuild: no recipe is wired into a live widget yet, so nothing
	// can be known to survive a pure invalidation. Stage 5 shrinks this as widgets adopt recipes.
	static bool ConsumeRebuildRequest();

	// Startup load result for UI status display.
	enum class EStartupLoadResult : uint8
	{
		LoadedSavedTheme,
		UsingCompiledDefaults,
		SavedThemeInvalid
	};

	// Get the startup load result (valid after EnsureInitialised).
	static EStartupLoadResult GetStartupLoadResult();

private:
	// Idempotent, and separate from Refresh so the lazy path cannot recurse.
	static void EnsureInitialised();
	
	// Loads saved theme from disk if valid, otherwise uses compiled defaults.
	static void LoadSavedThemeOrDefaults();
};