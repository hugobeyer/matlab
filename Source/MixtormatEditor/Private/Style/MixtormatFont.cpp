// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatFont.h"

#include "Styling/CoreStyle.h"
#include "Style/MixtormatDesignTokens.h"

namespace
{
	// Only families Mixtormat ships are selectable. This is deliberately not a system font
	// enumeration: a UI STYLE dropdown that lists fonts the plugin does not carry would offer
	// choices that silently render as something else.
	const TCHAR* const FamilyNames[] = { TEXT("Inter"), TEXT("Roboto") };
}

FName MixtormatFont::RequestedTypeface()
{
	const int32 Index = FMath::Clamp(MixtormatTokens::FontFamily, 0, UE_ARRAY_COUNT(FamilyNames) - 1);
	return FName(FamilyNames[Index]);
}

FName MixtormatFont::ResolvedTypeface()
{
	// Slate substitutes the engine default for a typeface name it cannot resolve, so an
	// unregistered family degrades to the previous appearance instead of failing to paint. The
	// shipped Inter face lives beside this module (Resources/Fonts) and is registered with the
	// font cache when the plugin initialises; until that registration succeeds this name resolves
	// to the default, which is why the fallback is silent rather than an error.
	return RequestedTypeface();
}

FSlateFontInfo MixtormatFont::Make(const bool bBold, const float Size)
{
	// Built from the engine default first so the result is always a valid FSlateFontInfo, then
	// pointed at the Mixtormat family. Starting from a valid style matters: a hand-constructed
	// FSlateFontInfo with no typeface resolves differently across platforms.
	FSlateFontInfo Result = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	Result.TypefaceFontName = ResolvedTypeface();
	return Result;
}

FSlateFontInfo MixtormatFont::MakeFromWeight(const float CssWeight, const float Size)
{
	// 600 is the prototype's value weight and 400 its label weight. The midpoint between them is
	// 500, which is the closest a two-face mapping can come to "this is a bold run".
	return Make(CssWeight >= 500.0f, Size);
}

int32 MixtormatFont::TrackingToSlate(const float TrackingPx, const float SizePx)
{
	if (SizePx <= UE_KINDA_SMALL_NUMBER)
	{
		return 0;
	}
	return FMath::RoundToInt(TrackingPx / SizePx * 1000.0f);
}
