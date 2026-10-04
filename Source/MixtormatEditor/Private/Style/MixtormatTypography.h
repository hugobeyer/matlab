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

// Mixtormat owns its typeface. The shipped Inter faces live in the plugin's Resources/Fonts and
// are loaded from disk by Slate, not borrowed from the engine's default font with its name swapped.
//
// The difference is not cosmetic, and it is worth being exact about what it is. The old approach
// built every style from FCoreStyle::GetDefaultFontStyle and then overwrote TypefaceFontName with
// "Inter". Slate cannot resolve a typeface name that no composite font provides, so it fell back
// to Roboto -- meaning the font family dropdown changed nothing on screen, while reporting that it
// had. Here the composite font genuinely points at the file, so the glyphs are Inter's.
//
// Verified against UE 5.8 SlateCore rather than assumed:
//
//   FSlateFontInfo::GetCompositeFont() returns CompositeFont.Get() directly when FontObject is not
//   an IFontProviderInterface, so no registration with FSlateFontCache is required and none is
//   performed. There is no AddFont-style entry point to call.
//
//   A typeface entry is a *file*. SlateCore 5.8 contains no variation-axis support at all -- no
//   FT_Set_Var_Design_Coordinates, no weight axis, nothing in Public or Private -- so a variable
//   font cannot be asked for a named instance. Inter.ttf *is* variable (fvar/gvar/avar/HVAR), which
//   is exactly why a second typeface entry cannot substitute for a weight axis.
//
//   So each weight is a static instance generated from that variable file with
//   fontTools.varLib.instancer: Inter-Regular.ttf (400), Inter-SemiBold.ttf (600),
//   Inter-Bold.ttf (700). SemiBold is not decoration -- tokens.css authors --value-weight: 600,
//   and a Regular/Bold-only pair had to collapse it to Bold.
//
//   FStandaloneCompositeFont is the ownership type for exactly this case: it derives FCompositeFont
//   and FGCObject so that a non-UObject font's bulk data stays referenced. It is held in a
//   function-local static for the lifetime of the module.
//
// Consequences, stated rather than hidden:
//
//   Each weight is only real if its file is present. IsWeightAvailable() reports which genuinely
//   resolve, and the authoring UI builds its weight list from that, so a dropdown never offers a
//   weight that renders identically to the one below it.
//
//   Glyphs are never scaled to correct apparent metrics. A stretched face is a font bug, not a
//   layout one, and the fix belongs in line height / slot VAlign / padding.

namespace Mixtormat
{
	struct FMixtormatTypography final
	{
		// Resolves the shipped font file and builds the composite font. Idempotent, and safe to
		// call from any module that needs a Mixtormat font. Returns false when the file is missing
		// or unreadable, in which case every Make() below falls back to the engine default and the
		// reason has already been logged once.
		static bool Initialize();

		// Releases the composite font. Only needed if the module shuts down before Slate does.
		static void Shutdown();

		// True when the shipped font resolved. False means every font below is the engine default.
		static bool IsAvailable();

		// Which weights genuinely resolve to a distinct face. The authoring UI must build its
		// weight list from this rather than from EMixtormatFontWeight, so that no option is dead.
		static bool IsWeightAvailable(EMixtormatFontWeight Weight);

		// The resolved font at a size and weight.
		//
		// bForceMonospaced maps to Slate's own bForceMonospaced, and letter spacing is converted
		// from CSS pixels centrally here -- no caller writes a raw Slate LetterSpacing.
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

		// Absolute path of the shipped Inter-Regular.ttf, or empty if the plugin directory could not
		// be resolved. Exposed for diagnostics and for the UI STYLE font readout. The heavier faces
		// live beside it under their own names.
		static FString GetFontFilePath();

		// The nearest shipped face to a CSS numeric weight.
		//
		// The prototype's authored weights are 400, 600 and 700, and those are the bands' centres,
		// so an authored value lands on the face it asked for rather than on a midpoint between
		// two. The old two-face mapping used 500 as the split, which turned every 600 into Bold.
		static EMixtormatFontWeight FromCssWeight(float CssWeight);
	};
}