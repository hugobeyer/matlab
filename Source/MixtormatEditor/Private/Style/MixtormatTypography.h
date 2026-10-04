// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Style/MixtormatTheme.h"

namespace Mixtormat
{
	// Forward-declared inside its own namespace, because the parameter below is a reference to it.
	// Declared at global scope it would name a second, unrelated type and the definition in the
	// .cpp -- where the real one is visible -- would fail to match the declaration.
	struct FMixtormatResolvedTypography;
}

// Semantic typography backed by Unreal's shared native default composite font.
// UE 5.8 maps Regular/SemiBold/Bold to Regular/Medium/Bold; no plugin font lifetime is needed.
namespace Mixtormat
{
	struct FMixtormatTypography final
	{
		// The resolved font at a size and weight.
		//
		// bForceMonospaced maps to Slate's own bForceMonospaced, and letter spacing is converted
		// from CSS pixels centrally here.
		static FSlateFontInfo MakeFont(EMixtormatFontWeight Weight, float Size,
			float TrackingPx = 0.0f, bool bForceMonospaced = false);

		// The same, from an authored spec.
		static FSlateFontInfo MakeFont(const FMixtormatTextSpec& Spec);

		// A text block style for a semantic role, resolved from the current theme.
		//
		// `Color` is the palette colour; `Spec.Opacity` is applied on top of it, because the
		// prototype authors role opacity as a separate token from the colour it dims.
		static FTextBlockStyle MakeTextStyle(const FMixtormatTextSpec& Spec, const FLinearColor& Color);

		// The spec for a semantic role, read from a resolved style.
		//
		// Takes the typography by reference rather than reaching for a global: this stage has not
		// yet wired the resolved style into FMixtormatStyle, and inventing an accessor here would
		// mean a second path to the same data the moment that wiring lands.
		static FMixtormatTextSpec GetSpec(const FMixtormatResolvedTypography& Typography, EMixtormatTextRole Role);

		// Applies a spec's casing. Slate has no uppercase flag -- case is a property of the text,
		// not of the font -- so bUppercase is honoured here rather than being dropped, which is
		// what would have made it a dead control.
		static FText ApplyCase(const FText& In, const FMixtormatTextSpec& Spec);

		// CSS pixels to Slate's 1/1000 em, in one place.
		//
		// 1px of tracking at a 9px face is 111 in Slate units, not 1. Copying the raw number across
		// opens a gap too small to see and impossible to retune from the prototype.
		static int32 TrackingToSlate(float TrackingPx, float SizePx);

		// The nearest semantic weight to a CSS numeric weight (authored at 400, 600 and 700).
		// Native face selection happens only in MakeFont; these bands remain backend-independent.
		static EMixtormatFontWeight FromCssWeight(float CssWeight);
	};
}
