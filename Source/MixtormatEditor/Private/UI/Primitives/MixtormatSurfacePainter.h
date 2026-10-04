// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatResolvedStyle.h"
#include "Style/MixtormatTheme.h"
#include "Styling/SlateTypes.h"


// One painter for every Mixtormat surface.
//
// A recipe describes how a surface is built; this turns that into draw elements. It is the only
// place that knows a layer becomes a colour, a border becomes a box, and a radius becomes four
// corner radii -- so a component that needs a new surface writes a recipe, not a painter.
//
// What it deliberately does NOT do:
//
//   Decide anything. Which colour role a layer uses, how strong it is, which blend it needs -- all
//   of that is authored in FMixtormatTheme and resolved before this point. The painter receives a
//   recipe and a palette and does the arithmetic; a widget that wanted a different look changes
//   the recipe, never this file.
//
//   Look anything up by name. §37 of the rewrite plan forbids registry lookups in hot paint paths,
//   and the resolution that remains -- turning an EMixtormatColorRole into a colour -- is an array
//   index plus a few multiplies. There is no string, no map, no JSON and no FName here.
//
//   Scale glyphs, round corners to whole pixels, or "tidy" a size. Everything drawn lands exactly
//   where the recipe's geometry puts it.
//
// Clipping: the painter never draws outside the geometry it is handed -- borders are drawn inward
// and the body fills the local rect exactly. Clipping the surface to a *larger* widget is the
// caller's, via SetClipping on the widget: an FSlateDrawElement cannot clip itself, so claiming
// otherwise here would be a promise the API cannot keep.
namespace Mixtormat
{
	// Which edge of a surface a border occupies. Named rather than passed as two booleans, because
	// "top or bottom" cannot distinguish left from right, and the position a border samples its
	// ramp at depends on that.
	enum class EMixtormatBorderEdge : uint8
	{
		Top,
		Bottom,
		Left,
		Right,
	};

	// Stop positions and composited colours for one surface. Inline-allocated so a caller can hold
	// this on the stack without touching the heap.
	struct FMixtormatSurfaceSamples
	{
		// Beyond this a ramp costs a second draw element's worth of work for a difference that is
		// not visible on a surface this size, and a longer gradient is more opportunity for one
		// stop to be wrong. A ramp longer than this is subsampled, not rejected.
		static constexpr int32 MaxStops = 16;

		// The axis these samples run along, or None when the surface is flat. Carried here rather
		// than re-derived by the painter: the painter needs it to pick a gradient direction, and
		// re-deriving it would be a second implementation of the same rule.
		EMixtormatAxis Axis = EMixtormatAxis::None;

		TArray<float, TInlineAllocator<MaxStops>> Positions;
		TArray<FLinearColor, TInlineAllocator<MaxStops>> Colors;

		void Reset()
		{
			Positions.Reset();
			Colors.Reset();
			Axis = EMixtormatAxis::None;
		}
	};

	// The pure half: recipe + palette + state -> flat or ramped opaque colours.
	//
	// Split out from PaintSurface so the compositing can be exercised without Slate, a renderer or a
	// window -- and so there is exactly one implementation of it (hard rule 3.3). PaintSurface is
	// this plus a draw call.
	//
	// Returns the number of colours written. One colour means the surface is flat and the painter
	// will draw a box rather than a gradient. Never zero for a non-empty recipe.
	int32 CompositeSurface(
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatResolvedPalette& Palette,
		const FMixtormatStateModifier& State,
		FMixtormatSurfaceSamples& OutSamples);

	// A ramp's value at position T. Piecewise-linear between authored points, clamped at the ends.
	// An empty ramp is 1.0 and a single-point ramp is that point's value -- a ramp with nothing to
	// ramp through must not silently become no-opaque.
	float EvaluateRamp(const FMixtormatRamp& Ramp, const float T);

	// Where a border edge sits along the surface axis, so a vertical border ramp becomes a brighter
	// top edge over a dimmer one. Exposed because it is the rule a caller has to author against and
	// it is not obvious from the recipe.
	//
	//   Vertical axis    top 0, bottom 1, left/right 0.5 (the edge spans the full height)
	//   Horizontal axis  left 0, right 1, top/bottom 0.5 (the edge spans the full width)
	//   No axis          0, because there is nothing to vary along
	float BorderEdgePosition(EMixtormatAxis Axis, EMixtormatBorderEdge Edge);

	// Applied only when drawing; compositing and border math use untinted samples.
	struct FMixtormatSurfaceDrawStyle
	{
		FLinearColor Tint = FLinearColor::White;
		ESlateDrawEffect Effects = ESlateDrawEffect::None;
	};

	struct FMixtormatSurfacePainter final
	{
		// Paints a surface from a recipe: body, then borders. Returns the layer id actually used, so
		// a caller painting several things in a fixed order (§63) can carry it forward rather than
		// assuming it did not move.
		//
		// Before draw-time tint, the result is opaque: the recipe's Base is the
		// backdrop every layer composites against, and a surface that owns its rectangle does not
		// also blend with whatever the widget happened to put behind it. A translucent surface is a
		// different decision and belongs to a caller that owns its backdrop.
		static int32 PaintSurface(
			FSlateWindowElementList& Elements,
			int32 LayerId,
			const FGeometry& Geometry,
			const FMixtormatSurfaceRecipe& Recipe,
			const FMixtormatResolvedPalette& Palette,
			const FWidgetStyle& WidgetStyle,
			const FMixtormatSurfaceDrawStyle& DrawStyle = FMixtormatSurfaceDrawStyle());

		// As above, with a state modifier applied. This is how hover works: the same recipe with
		// different numbers, never a second recipe. §16 forbids duplicating a surface per state and
		// this is what makes obeying that cheap rather than merely correct.
		static int32 PaintSurfaceWithState(
			FSlateWindowElementList& Elements,
			int32 LayerId,
			const FGeometry& Geometry,
			const FMixtormatSurfaceRecipe& Recipe,
			const FMixtormatResolvedPalette& Palette,
			const FWidgetStyle& WidgetStyle,
			const FMixtormatStateModifier& State,
			const FMixtormatSurfaceDrawStyle& DrawStyle = FMixtormatSurfaceDrawStyle());

		// Body only. Exposed for a caller that must interleave something between body and hairline --
		// the layer row's paint order in §63 puts the selected glow there.
		static int32 PaintBody(
			FSlateWindowElementList& Elements,
			int32 LayerId,
			const FGeometry& Geometry,
			const FMixtormatSurfaceRecipe& Recipe,
			const FMixtormatSurfaceSamples& BodySamples,
			const FMixtormatSurfaceDrawStyle& DrawStyle = FMixtormatSurfaceDrawStyle());

		// The same, for a sub-rect of a widget rather than the whole of it.
		//
		// This is not a convenience: a slider's fill and a toggle's checked inset are both the same
		// recipe drawn into a smaller box, and FGeometry cannot express an offset at all. An earlier
		// version clipped a full-size paint instead, which collapsed to the right width and no
		// height -- so the primitive the painter takes is the one Slate's draw calls actually
		// consume, and the FGeometry overload forwards to it.
		static int32 PaintBody(
			FSlateWindowElementList& Elements,
			int32 LayerId,
			const FPaintGeometry& PaintGeometry,
			const FMixtormatSurfaceRecipe& Recipe,
			const FMixtormatSurfaceSamples& BodySamples,
			const FMixtormatSurfaceDrawStyle& DrawStyle = FMixtormatSurfaceDrawStyle());

		// Borders only, on their own layer. A blended border has to composite against the body it
		// was drawn over, so this takes the body's samples rather than re-deriving a colour that
		// would not match what is already on screen.
		//
		// FGeometry, not FPaintGeometry: each edge is an offset sub-box, and only FGeometry can
		// produce one. A surface whose borders live inside a sub-rect draws that sub-rect's own
		// FGeometry rather than asking for an offset form here.
		static int32 PaintBorders(
			FSlateWindowElementList& Elements,
			int32 LayerId,
			const FGeometry& Geometry,
			const FMixtormatSurfaceRecipe& Recipe,
			const FMixtormatResolvedPalette& Palette,
			const FWidgetStyle& WidgetStyle,
			const FMixtormatSurfaceSamples& BodySamples,
			const FMixtormatSurfaceDrawStyle& DrawStyle = FMixtormatSurfaceDrawStyle());
	};
}