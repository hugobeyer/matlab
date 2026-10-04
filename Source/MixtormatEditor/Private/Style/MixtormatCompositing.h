// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Colour operations the visual contract performs in CSS but Slate has no name for.
//
// The prototype leans on four blend modes (tokens.css: `--surface-blend-mode`, `--well-blend-mode`,
// `--foldout-accent-blend-mode`, `--layer-group-blend-mode`). Slate exposes none of them, so each is
// resolved here as explicit arithmetic rather than stored as a token. That is deliberate: a token
// named `BlendMode` would be an enum value no painter reads, and the two non-trivial modes would
// quietly collapse into "paint a darker colour".
//
// Additive and Multiply are not new. Both already have working precedents in this codebase --
// `MixtormatPalette::GroupCardBackground` adds the source at alpha, and `SMixtormatGradientBox`'s
// second pass darkens with black at alpha so that src * (1 - a) survives. Those are re-expressed
// here so the semantics are stated once and a caller does not have to rediscover them.
//
// The important one is SoftLight. It is backdrop-dependent: the same source colour produces a
// different result over a dark panel than over a lit foldout, which is precisely why it cannot be
// faked with a recolour or approximated by multiply. Two surfaces use it (`components.css:177`,
// `--foldout-accent-blend-mode`; `:89`, `--layer-group-blend-mode`), and both need real compositing.
namespace MixtormatCompositing
{
	// The four blend operations the prototype authors as `*-blend-mode` tokens. Slate has no
	// equivalent, so the mode is stored as an enum and resolved by ApplyBlend rather than by a
	// string comparison at a paint site.
	//
	// Stored in MixtormatTokens as an int32 index so the live-theme registry can point at it
	// without casting an enum pointer; BlendModeOf below is the typed read.
	enum class EMixtormatBlendMode : int32
	{
		Normal = 0,
		Additive = 1,
		Multiply = 2,
		SoftLight = 3,
	};

	inline const TCHAR* BlendModeLabel(const EMixtormatBlendMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatBlendMode::Additive: return TEXT("Additive / Plus Lighter");
		case EMixtormatBlendMode::Multiply: return TEXT("Multiply");
		case EMixtormatBlendMode::SoftLight: return TEXT("Soft Light");
		case EMixtormatBlendMode::Normal:
		default: return TEXT("Normal");
		}
	}

	inline EMixtormatBlendMode BlendModeOf(const int32 Value)
	{
		return static_cast<EMixtormatBlendMode>(FMath::Clamp(Value, 0, 3));
	}
	// CSS `filter: saturate(x)`: push every channel away from (or toward) the colour's own luma.
	//
	// This is a property of the paint layer, not of the palette. One accent appears at three
	// authored saturations -- 0.7 as a slider fill, 1.5 on a group button, 1.5 on an active glow --
	// so a saturation applied inside `Accent()` would be wrong for at least two of the three. The
	// caller supplies the amount at the call site; the shared colour is never modified.
	//
	// Rec.709 luma, computed in linear space and applied to linear channels, with alpha untouched.
	// CSS specifies the operation in sRGB; matching that exactly would mean a per-channel transfer
	// round trip on a hot path for a difference these near-neutral surfaces do not show. This is
	// the standard approximation and the surfaces are tuned against screenshots, so treat the
	// numbers here as "visually equivalent", not "bit-identical to the browser".
	inline FLinearColor Saturate(const FLinearColor& Color, const float Amount)
	{
		if (Amount == 1.0f)
		{
			return Color;
		}
		const float Luma = 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
		FLinearColor Result;
		Result.R = FMath::Lerp(Luma, Color.R, Amount);
		Result.G = FMath::Lerp(Luma, Color.G, Amount);
		Result.B = FMath::Lerp(Luma, Color.B, Amount);
		Result.A = Color.A;
		return Result;
	}

	// CSS `plus-lighter`, and what tokens.css calls additive: the source is added to the ground at
	// its own alpha, so a surface above its background gets *brighter* rather than tinted toward a
	// fixed replacement colour. This is what `GroupCardBackground` and the rail-button plate do by
	// hand today.
	//
	// Alpha is deliberately preserved from the ground. An additive pass contributes light; it does
	// not change how opaque the surface underneath it is, and letting Source.A through would make a
	// low-alpha lift read as a hole punched in the panel.
	inline FLinearColor Additive(const FLinearColor& Ground, const FLinearColor& Source, const float Strength = 1.0f)
	{
		FLinearColor Result = Ground;
		Result.R = FMath::Clamp(Ground.R + Source.R * Source.A * Strength, 0.0f, 1.0f);
		Result.G = FMath::Clamp(Ground.G + Source.G * Source.A * Strength, 0.0f, 1.0f);
		Result.B = FMath::Clamp(Ground.B + Source.B * Source.A * Strength, 0.0f, 1.0f);
		Result.A = Ground.A;
		return Result;
	}

	// CSS `multiply`: the ground is scaled by the source. With a black source at alpha a this
	// reduces to ground * (1 - a), which is exactly the second pass `SMixtormatGradientBox` paints.
	//
	// Alpha is preserved from the ground, for the same reason as Additive: darkening is not the
	// same operation as making something transparent.
	inline FLinearColor Multiply(const FLinearColor& Ground, const FLinearColor& Source)
	{
		const FLinearColor Factor = FLinearColor(
			FMath::Lerp(1.0f, Source.R, Source.A),
			FMath::Lerp(1.0f, Source.G, Source.A),
			FMath::Lerp(1.0f, Source.B, Source.A),
			1.0f);
		FLinearColor Result = Ground;
		Result.R *= Factor.R;
		Result.G *= Factor.G;
		Result.B *= Factor.B;
		Result.A = Ground.A;
		return Result;
	}

	// The W3C soft-light separable blend, per Compositing and Blending Level 1 section 10.1.10:
	//
	//   if (Cs <= 0.5)  B(Cb, Cs) = Cb - (1 - 2*Cs) * Cb * (1 - Cb)
	//   else            B(Cb, Cs) = Cb + (2*Cs - 1) * (D(Cb) - Cb)
	//
	//   if (Cb <= 0.25) D(Cb) = ((16*Cb - 12) * Cb + 4) * Cb
	//   else            D(Cb) = sqrt(Cb)
	//
	// Cb is the backdrop (what is already on screen), Cs is the source (the colour being applied).
	//
	// D is piecewise, and the second branch is not a stylistic detail: D(0.8) is sqrt(0.8) = 0.894,
	// where the polynomial alone gives 3.712 -- more than double the top of the colour range. Using
	// the polynomial for all of Cb turns every light source on a light ground into a wildly
	// overbright result that has to be clamped back down, which destroys the soft-light character
	// and is not what a browser computes.
	//
	// The blend is then composited over the backdrop by Source.A, so a translucent source fades
	// toward the backdrop rather than toward black.
	//
	// This is the one operation in the file that genuinely needs both inputs. Multiply and additive
	// can each be reached by rearranging a single colour; soft-light cannot, which is why it could
	// not be satisfied by a "darker colour" literal. Sanity anchors, on a mid backdrop Cb = 0.5:
	//
	//   Cs = 0.0  ->  0.25   darkens
	//   Cs = 0.5  ->  0.50   identity (a mid-grey source changes nothing)
	//   Cs = 1.0  ->  0.707  lightens, capped at sqrt(Cb)
	//
	// A light source on a dark ground lightens it, a dark source on a dark ground darkens it, and
	// the mid-grey crossing is neutral -- none of which multiply or additive can express.
	//
	// Applied in linear space, consistent with Saturate above and with the values Slate hands us.
	// Strength scales the source's contribution without changing its direction.
	inline float SoftLightChannel(const float Backdrop, const float Source)
	{
		const float ClampedSource = FMath::Clamp(Source, 0.0f, 1.0f);
		float Blended;
		if (ClampedSource <= 0.5f)
		{
			Blended = Backdrop - (1.0f - 2.0f * ClampedSource) * Backdrop * (1.0f - Backdrop);
		}
		else
		{
			// Discriminant: the mirror of the darken branch, so a light source lifts the backdrop the
			// same amount a dark source would have dropped it.
			const float Discriminant = Backdrop <= 0.25f
				? ((16.0f * Backdrop - 12.0f) * Backdrop + 4.0f) * Backdrop
				: FMath::Sqrt(Backdrop);
			Blended = Backdrop + (2.0f * ClampedSource - 1.0f) * (Discriminant - Backdrop);
		}
		// The spec requires the mixing result to be clamped to the colour range.
		return FMath::Clamp(Blended, 0.0f, 1.0f);
	}

	inline FLinearColor SoftLight(const FLinearColor& Backdrop, const FLinearColor& Source, const float Strength = 1.0f)
	{
		const float Coverage = FMath::Clamp(Source.A * Strength, 0.0f, 1.0f);
		FLinearColor Result = Backdrop;
		Result.R = FMath::Lerp(Backdrop.R, SoftLightChannel(Backdrop.R, Source.R), Coverage);
		Result.G = FMath::Lerp(Backdrop.G, SoftLightChannel(Backdrop.G, Source.G), Coverage);
		Result.B = FMath::Lerp(Backdrop.B, SoftLightChannel(Backdrop.B, Source.B), Coverage);
		Result.A = Backdrop.A;
		return Result;
	}

	// CSS `normal`: standard source-over, src * alpha over dst * (1 - alpha).
	//
	// Every other mode above is expressed as "backdrop plus a contribution", because that is the
	// shape the visual contract needs -- a lift has to leave the surface beneath it as opaque as it
	// found it. Normal is the one mode whose contract *is* transparency, so it is written out
	// rather than approximated: a caller that picks Normal wants the two colours to combine the way
	// a browser would, including the alpha term.
	inline FLinearColor Normal(const FLinearColor& Backdrop, const FLinearColor& Source, const float Strength = 1.0f)
	{
		const float Alpha = FMath::Clamp(Source.A * Strength, 0.0f, 1.0f);
		FLinearColor Result = Backdrop;
		Result.R = FMath::Lerp(Backdrop.R, Source.R, Alpha);
		Result.G = FMath::Lerp(Backdrop.G, Source.G, Alpha);
		Result.B = FMath::Lerp(Backdrop.B, Source.B, Alpha);
		Result.A = FMath::Lerp(Backdrop.A, 1.0f, Alpha);
		return Result;
	}

	// The single dispatcher every paint layer uses, so no call site reimplements a blend.
	inline FLinearColor ApplyBlend(const EMixtormatBlendMode Mode, const FLinearColor& Backdrop,
		const FLinearColor& Source, const float Strength = 1.0f)
	{
		switch (Mode)
		{
		case EMixtormatBlendMode::Additive: return Additive(Backdrop, Source, Strength);
		case EMixtormatBlendMode::Multiply: return Multiply(Backdrop, Source);
		case EMixtormatBlendMode::SoftLight: return SoftLight(Backdrop, Source, Strength);
		case EMixtormatBlendMode::Normal:
		default: return Normal(Backdrop, Source, Strength);
		}
	}
}