#pragma once

#include "CoreMinimal.h"

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
	void Paint(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2f& Size,
		EOrientation CssOrientation,
		TArrayView<const FStop> Stops,
		float CornerRadius);
}
