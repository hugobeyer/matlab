// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatFont.h"

#include "Style/MixtormatTheme.h"
#include "Style/MixtormatTypography.h"

// TEMPORARY BRIDGE -- delete at Stage 10. See MixtormatFont.h.

FName MixtormatFont::RequestedTypeface()
{
	// The shipped family is Inter and nothing else, so the token index is clamped rather than
	// mapped through a list. The old build of this file also accepted "Roboto"; offering a face
	// the plugin does not ship produced a dropdown that changed nothing on screen.
	return FName(TEXT("Inter"));
}

FName MixtormatFont::ResolvedTypeface()
{
	// The typeface name no longer selects the face. FSlateFontInfo resolves the family through
	// the composite font, and MixtormatTypography selects Regular or Bold inside it. This returns
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