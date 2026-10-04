// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatFont.h"

#include "Style/MixtormatTheme.h"
#include "Style/MixtormatTypography.h"

// TEMPORARY BRIDGE -- delete at Stage 10. See MixtormatFont.h.

FName MixtormatFont::RequestedTypeface()
{
	// Display-only backend label; the legacy family token does not select a font.
	return FName(TEXT("Unreal Default"));
}

FName MixtormatFont::ResolvedTypeface()
{
	// The typeface name no longer selects the face. FSlateFontInfo resolves the family through
	// the composite font, and MixtormatTypography selects the native face inside it. This returns
	// the family name purely so the UI STYLE font readout and any legacy call site keep working.
	return RequestedTypeface();
}

FSlateFontInfo MixtormatFont::Make(const bool bBold, const float Size)
{
	return Mixtormat::FMixtormatTypography::MakeFont(
		bBold ? Mixtormat::EMixtormatFontWeight::Bold : Mixtormat::EMixtormatFontWeight::Regular,
		Size);
}

FSlateFontInfo MixtormatFont::MakeFromWeight(const float CssWeight, const float Size)
{
	// Forwards the real CSS weight rather than a bold flag. This used to split at 500, which mapped
	// the prototype's --value-weight: 600 onto Bold and rendered it at 700.
	return Mixtormat::FMixtormatTypography::MakeFont(
		Mixtormat::FMixtormatTypography::FromCssWeight(CssWeight), Size);
}

int32 MixtormatFont::TrackingToSlate(const float TrackingPx, const float SizePx)
{
	return Mixtormat::FMixtormatTypography::TrackingToSlate(TrackingPx, SizePx);
}