// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatTypography.h"

#include "Style/MixtormatResolvedStyle.h"
#include "Styling/CoreStyle.h"

namespace Mixtormat
{
	namespace
	{
		// UE 5.8's default composite defines Regular, Medium and Bold, but no SemiBold.
		// Medium keeps the authored middle weight distinct without synthesizing a face.
		const TCHAR* EngineTypefaceFor(const EMixtormatFontWeight Weight)
		{
			switch (Weight)
			{
			case EMixtormatFontWeight::SemiBold: return TEXT("Medium");
			case EMixtormatFontWeight::Bold: return TEXT("Bold");
			case EMixtormatFontWeight::Regular:
			default: return TEXT("Regular");
			}
		}
	}

	EMixtormatFontWeight FMixtormatTypography::FromCssWeight(const float CssWeight)
	{
		// Preserve the authored 400/600/700 bands independently of the native face mapping.
		if (CssWeight <= 500.0f)
		{
			return EMixtormatFontWeight::Regular;
		}
		if (CssWeight <= 650.0f)
		{
			return EMixtormatFontWeight::SemiBold;
		}
		return EMixtormatFontWeight::Bold;
	}

	int32 FMixtormatTypography::TrackingToSlate(const float TrackingPx, const float SizePx)
	{
		if (SizePx <= UE_KINDA_SMALL_NUMBER)
		{
			return 0;
		}
		return FMath::RoundToInt(TrackingPx / SizePx * 1000.0f);
	}

	FSlateFontInfo FMixtormatTypography::MakeFont(const EMixtormatFontWeight Weight, const float Size,
		const float TrackingPx, const bool bForceMonospaced)
	{
		// The same shared composite used by Starship's DEFAULT_FONT and small editor labels.
		FSlateFontInfo Result = FCoreStyle::GetDefaultFontStyle(EngineTypefaceFor(Weight), Size);
		Result.LetterSpacing = TrackingToSlate(TrackingPx, Size);
		Result.bForceMonospaced = bForceMonospaced;

		return Result;
	}

	FSlateFontInfo FMixtormatTypography::MakeFont(const FMixtormatTextSpec& Spec)
	{
		return MakeFont(Spec.Weight, Spec.Size, Spec.TrackingPx, Spec.bMonospacedNumbers);
	}

	FTextBlockStyle FMixtormatTypography::MakeTextStyle(const FMixtormatTextSpec& Spec, const FLinearColor& Color)
	{
		FTextBlockStyle Style;
		Style.SetFont(MakeFont(Spec));

		// The role's opacity multiplies the palette colour rather than replacing its alpha, so a
		// role can be dimmed without a second colour being authored for the dimmed case.
		Style.SetColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, Color.A * Spec.Opacity));

		return Style;
	}

	FMixtormatTextSpec FMixtormatTypography::GetSpec(const FMixtormatResolvedTypography& Typography,
		const EMixtormatTextRole Role)
	{
		const uint8 Index = static_cast<uint8>(Role);
		if (Index >= FMixtormatResolvedTypography::RoleCount)
		{
			return FMixtormatTextSpec();
		}
		return Typography.Roles[Index];
	}

}

#undef LOCTEXT_NAMESPACE
