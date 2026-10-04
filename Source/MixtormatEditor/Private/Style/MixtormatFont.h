// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// One place decides the Mixtormat font family and how a weight resolves to a face.
//
// Every Mixtormat text style is built here rather than through FCoreStyle::GetDefaultFontStyle
// directly, so switching families is a single edit and not a sweep through dozens of call sites.
// Nothing outside Mixtormat's own style set is affected: editor and engine typography keep using
// whatever they were already using.
//
// Weight is a real face pair, not a numeric weight. The shipped family is a variable face, but
// Slate's default font set resolves `Regular` and `Bold` reliably while arbitrary variable
// instances are not dependable across platforms, so anything at or above the CSS 600 midpoint maps
// to Bold and everything below maps to Regular. A prototype value of 600 therefore renders as the
// Bold face, which is the intended weight; a value of 500 does the same, and that is the one
// unavoidable difference from the browser.
struct MixtormatFont final
{
	// The typeface name Slate should resolve for the currently selected family.
	static FName RequestedTypeface();

	// The typeface that is actually usable. Falls back to the engine default when the requested
	// family is not registered with Slate's font cache, so a missing font degrades to the previous
	// appearance instead of failing to paint text.
	static FName ResolvedTypeface();

	static FSlateFontInfo Make(bool bBold, const float Size);
	// Numeric CSS weight (400/600/...), mapped through the Regular/Bold pair described above.
	static FSlateFontInfo MakeFromWeight(const float CssWeight, const float Size);

	// Applies LetterSpacing authored in CSS pixels to a style whose size is known, converting to
	// Slate's 1/1000 em. Copying the number across opens a gap too small to see.
	static int32 TrackingToSlate(const float TrackingPx, const float SizePx);
};
