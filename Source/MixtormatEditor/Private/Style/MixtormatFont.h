// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// TEMPORARY BRIDGE -- delete at Stage 10.
//
// Legacy entry point forwarding to MixtormatTypography's native Unreal font backend.
//
// This shim exists only so the ~200 existing call sites keep compiling while the widgets are
// migrated one at a time (rewrite plan §83). It adds no behaviour of its own: every function is a
// forward or display-only backend label. Nothing new should call it -- call MixtormatTypography directly.
struct MixtormatFont final
{
	// Kept only because the live-theme registry still stores a font family index. Returns the
	// display-only backend label; this is not a Slate typeface name.
	static FName RequestedTypeface();

	// Deprecated alongside RequestedTypeface: the family is no longer selected by name, so there
	// is no "requested vs resolved" distinction left to make.
	static FName ResolvedTypeface();

	static FSlateFontInfo Make(bool bBold, const float Size);
	static FSlateFontInfo MakeFromWeight(const float CssWeight, const float Size);

	// Forwarded to MixtormatTypography::TrackingToSlate.
	static int32 TrackingToSlate(const float TrackingPx, const float SizePx);
};