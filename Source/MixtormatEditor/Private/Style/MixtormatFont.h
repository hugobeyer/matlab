// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// TEMPORARY BRIDGE -- delete at Stage 10.
//
// This was the old typography entry point. It faked Inter by taking the engine's default font and
// overwriting TypefaceFontName, which does not work: Slate resolves a typeface name inside a
// composite font, and "Inter" exists in none of them, so the result was Roboto wearing Inter's
// name. MixtormatTypography::MakeFont now builds a real file-backed composite font.
//
// This shim exists only so the ~200 existing call sites keep compiling while the widgets are
// migrated one at a time (rewrite plan §83). It adds no behaviour of its own: every function is a
// forward. Nothing new should call it -- call MixtormatTypography directly.
struct MixtormatFont final
{
	// Kept only because the live-theme registry still stores a font family index. Returns the
	// name Slate is asked for; note that this name is NOT what selects the face any more.
	static FName RequestedTypeface();

	// Deprecated alongside RequestedTypeface: the family is no longer selected by name, so there
	// is no "requested vs resolved" distinction left to make.
	static FName ResolvedTypeface();

	static FSlateFontInfo Make(bool bBold, const float Size);
	static FSlateFontInfo MakeFromWeight(const float CssWeight, const float Size);

	// Forwarded to MixtormatTypography::TrackingToSlate.
	static int32 TrackingToSlate(const float TrackingPx, const float SizePx);
};