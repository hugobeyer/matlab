// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

class FSlateWindowElementList;
struct FPaintGeometry;

// Painting a CSS gradient with Slate, correctly.
//
// Two corrections live here, and both of them were silently wrong before:
//
//   Slate names a gradient after the direction of its *bands*, not the direction its colour
//   changes. ElementBatcher reads Position.X when the type is Orient_Vertical and Position.Y
//   otherwise, so Slate's "vertical" is a left-to-right ramp. Callers here speak CSS -- Vertical
//   means top to bottom -- and the axis is translated at the draw call.
//
//   CSS interpolates between stops in sRGB. Slate interpolates vertex colours in linear space,
//   which traces a different curve between the same endpoints: lighter through the middle, and
//   reading as an eased ramp where the design asks for a steady one. Each span is sampled here in
//   sRGB, so the spans Slate finally interpolates are too short for the difference to show.
//
// It is a free function rather than a widget because two very different things need it: the
// gradient box that wraps content, and the slider, which is a leaf widget hand-painting a fill at
// a geometry it computes itself.
namespace MixtormatGradient
{
	// A stop, as CSS writes one: a position along the axis in 0..1, and a colour.
	struct FStop
	{
		float Position = 0.0f;
		FLinearColor Color = FLinearColor::Transparent;
	};

	// Interpolate the way CSS does, on the sRGB values rather than the linear ones.
	FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& B, float T);

	// Draw one gradient across Size, in CSS orientation, sampled in sRGB.
	//
	// Stops must be ordered by position. Fewer than two draws nothing -- a one-stop gradient is a
	// flat fill, and the caller has a brush for that.
	// CornerRadii is Slate's own order: top-left, top-right, bottom-right, bottom-left. A group
	// header rounds its top pair and its body rounds its bottom pair, so the two stack into one
	// rounded block with a flat seam; a single uniform radius cannot express that and left a
	// notch where the header met the body.
	void Paint(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2f& Size,
		EOrientation CssOrientation,
		TArrayView<const FStop> Stops,
		const FVector4f& CornerRadii,
		ESlateDrawEffect DrawEffects = ESlateDrawEffect::None);

	// The shared power-curve falloff, ported from the prototype's falloff.js.
	//
	//   value(t) = Start + (End - Start) * pow(t, max(Power, 0.01)),   t in [0, 1]
	//
	// For a decreasing fade, Power < 1 falls away early, 1 is linear, and Power > 1 holds the start
	// value longer. The clamp matches the prototype's Math.max(exponent, .01): a zero or negative
	// exponent is undefined at t = 0, and the prototype's answer is to pin it just above zero.
	//
	// This reproduces the *curve*, not the JavaScript. falloff.js samples six stops because a CSS
	// gradient interpolates between stops in the compositor; here the stops are fed to
	// MixtormatGradient::Paint, which samples each span again at GradientSamplesPerSpan. Choosing a
	// sample count here is therefore choosing how finely to describe the curve, not how finely Slate
	// will render it -- the two numbers are unrelated and must not be conflated.
	float FalloffValue(float T, float Start, float End, float Power);

	// Append `SampleCount` stops spanning PositionStart..PositionEnd, each carrying Value's colour
	// at the falloff opacity for its own position.
	//
	// Appended rather than returned so the caller owns the storage and can size the allocator: this
	// runs per paint on surfaces that will later animate, and a returned TArray would heap on every
	// frame. `Color` is the source; only its alpha varies along the ramp, which is what every use of
	// it in falloff.js does -- an accent lift, a fill body, a card body.
	//
	// SampleCount is clamped to at least 2, because a one-stop gradient is a flat fill that Paint
	// refuses to draw and that no falloff describes.
	void AppendFalloffStops(
		TArray<FStop, TInlineAllocator<16>>& OutStops,
		const FLinearColor& Color,
		float StartOpacity,
		float EndOpacity,
		float Power,
		float PositionStart = 0.0f,
		float PositionEnd = 1.0f,
		int32 SampleCount = 8);
}
