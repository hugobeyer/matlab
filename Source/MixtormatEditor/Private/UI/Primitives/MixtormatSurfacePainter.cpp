// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Primitives/MixtormatSurfacePainter.h"

#include "Rendering/DrawElements.h"
#include "Style/MixtormatCompositing.h"
#include "Styling/AppStyle.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

namespace Mixtormat
{
	namespace
	{
		// The colour a layer contributes at position T, already resolved, saturated and weighted.
		//
		// Order is the CSS one and the steps are not interchangeable: `filter: saturate()` applies to
		// the element *before* `mix-blend-mode` composites it, and the ramp's weight becomes the
		// source's alpha before the blend sees it. Saturating afterwards would tint the backdrop.
		FLinearColor LayerSourceAt(
			const FMixtormatPaintLayer& Layer,
			const FMixtormatResolvedPalette& Palette,
			const FMixtormatStateModifier& State,
			const float T)
		{
			// A state may replace a layer's source outright rather than nudging it. That is the
			// difference between "hover is brighter" and "hover is a different colour"; both are
			// legitimate, and which one a component uses is authored rather than guessed here.
			const FMixtormatColorRef& Ref =
				State.SourceOverride.IsSet() ? State.SourceOverride.GetValue() : Layer.Source;

			FLinearColor Source = ResolveColor(Palette, Ref);
			Source.A *= EvaluateRamp(Layer.OpacityRamp, T) * Layer.Strength * State.Strength * State.Opacity;
			return MixtormatCompositing::Saturate(Source, Ref.Saturation * State.Saturation);
		}

		MixtormatCompositing::EMixtormatBlendMode LayerBlend(const FMixtormatPaintLayer& Layer, const FMixtormatStateModifier& State)
		{
			return State.BlendOverride.IsSet() ? State.BlendOverride.GetValue() : Layer.Blend;
		}

		// The ramp whose authored positions define the surface: the first enabled layer that
		// actually varies along the axis. A layer with one stop, or none, varies nothing and does
		// not get to define the geometry of the whole surface.
		const FMixtormatRamp* FindDefiningRamp(const FMixtormatSurfaceRecipe& Recipe, const EMixtormatAxis Axis)
		{
			if (Axis == EMixtormatAxis::None)
			{
				return nullptr;
			}
			for (const FMixtormatPaintLayer& Layer : Recipe.Layers)
			{
				if (Layer.bEnabled && Layer.OpacityRamp.Axis == Axis && Layer.OpacityRamp.Points.Num() >= 2)
				{
					return &Layer.OpacityRamp;
				}
			}
			return nullptr;
		}

		// The body colour at a position along its axis, for a border to blend against.
		//
		// Indexed rather than interpolated: the samples are already spaced along the axis, and
		// interpolating a second time would bend the curve again for no gain.
		FLinearColor SampleBodyAt(const FMixtormatSurfaceSamples& Samples, const float T)
		{
			if (Samples.Colors.Num() == 0)
			{
				return FLinearColor::White;
			}
			if (Samples.Colors.Num() == 1)
			{
				return Samples.Colors[0];
			}
			const int32 Index = FMath::Clamp(
				FMath::RoundToInt(T * static_cast<float>(Samples.Colors.Num() - 1)),
				0, Samples.Colors.Num() - 1);
			return Samples.Colors[Index];
		}

		// EOrientation is an *unscoped* UENUM in SlateCore, so its values are spelled bare. This
		// matches MixtormatGradientPainter and MixtormatWell, which both use Orient_Vertical directly.
		EOrientation SlateOrientation(const EMixtormatAxis Axis)
		{
			// MixtormatGradient::Paint takes a CSS orientation and swaps it internally, because
			// Slate names a gradient after the direction of its bands rather than of its colour
			// change. Translating here keeps that single quirk in one place.
			return Axis == EMixtormatAxis::Horizontal ? Orient_Horizontal : Orient_Vertical;
		}
	}

	float EvaluateRamp(const FMixtormatRamp& Ramp, const float T)
	{
		const int32 Count = Ramp.Points.Num();

		// Nothing to ramp through. Returning 1 rather than 0 keeps a layer that forgot its ramp
		// visible at full strength, which is a failure that announces itself; a silently invisible
		// layer is the one that ships.
		if (Count == 0)
		{
			return 1.0f;
		}
		if (Count == 1)
		{
			return Ramp.Points[0].Value;
		}

		if (T <= Ramp.Points[0].Position)
		{
			return Ramp.Points[0].Value;
		}
		if (T >= Ramp.Points[Count - 1].Position)
		{
			return Ramp.Points[Count - 1].Value;
		}

		for (int32 Index = 0; Index < Count - 1; ++Index)
		{
			const FMixtormatRampPoint& From = Ramp.Points[Index];
			const FMixtormatRampPoint& To = Ramp.Points[Index + 1];

			const float Span = To.Position - From.Position;
			if (T < From.Position - UE_SMALL_NUMBER || T > To.Position + UE_SMALL_NUMBER)
			{
				continue;
			}
			if (Span <= UE_SMALL_NUMBER)
			{
				// Two points at the same position describe a step, not a ramp. Taking the later one
				// is what a gradient with two identical stops does.
				return To.Value;
			}
			return FMath::Lerp(From.Value, To.Value, FMath::Clamp((T - From.Position) / Span, 0.0f, 1.0f));
		}

		return Ramp.Points[Count - 1].Value;
	}

	float BorderEdgePosition(const EMixtormatAxis Axis, const EMixtormatBorderEdge Edge)
	{
		switch (Axis)
		{
		case EMixtormatAxis::Vertical:
			switch (Edge)
			{
			case EMixtormatBorderEdge::Top: return 0.0f;
			case EMixtormatBorderEdge::Bottom: return 1.0f;
			default: return 0.5f; // A side edge spans the full height; its centre is the middle.
			}

		case EMixtormatAxis::Horizontal:
			switch (Edge)
			{
			case EMixtormatBorderEdge::Left: return 0.0f;
			case EMixtormatBorderEdge::Right: return 1.0f;
			default: return 0.5f; // A top or bottom edge spans the full width.
			}

		case EMixtormatAxis::None:
		default:
			// Nothing varies along the surface, so every edge sees the same colour and the position
			// is arbitrary. Zero is as good as any and is the one the flat path already used.
			return 0.0f;
		}
	}

	int32 CompositeSurface(
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatResolvedPalette& Palette,
		const FMixtormatStateModifier& State,
		FMixtormatSurfaceSamples& OutSamples)
	{
		OutSamples.Reset();

		const FLinearColor Base = ResolveColor(Palette, Recipe.Base);

		// The axis is whichever ramp has the most authored points along a single direction. Picking
		// the busiest rather than the first means a surface whose first layer is a flat accent still
		// gets its body ramp sampled correctly instead of collapsing to one colour.
		EMixtormatAxis Axis = EMixtormatAxis::None;
		const FMixtormatRamp* DefiningRamp = nullptr;
		for (const FMixtormatPaintLayer& Layer : Recipe.Layers)
		{
			if (!Layer.bEnabled || Layer.OpacityRamp.Axis == EMixtormatAxis::None)
			{
				continue;
			}
			if (DefiningRamp == nullptr
				|| Layer.OpacityRamp.Points.Num() > DefiningRamp->Points.Num())
			{
				Axis = Layer.OpacityRamp.Axis;
				DefiningRamp = &Layer.OpacityRamp;
			}
		}

		// A ramp needs at least two points to be a ramp. One point (or none) is a constant layer, and
				// letting it define the axis would promote a flat surface into a gradient whose stops all
				// share one position -- a zero-length span that renders as a band rather than a fill.
				if (DefiningRamp != nullptr && DefiningRamp->Points.Num() < 2)
				{
					DefiningRamp = nullptr;
				}

				OutSamples.Axis = DefiningRamp != nullptr ? Axis : EMixtormatAxis::None;

		if (OutSamples.Axis == EMixtormatAxis::None)
		{
			// Flat. One colour, and the painter can draw a box -- the allocation-free path, and the
			// common case for a rest-state well or a border plate.
			OutSamples.Positions.Add(0.0f);
			OutSamples.Colors.Add(Base);
		}
		else
		{
			// The defining ramp's *authored positions*, not a uniform grid over 0..1. The fill's shade
			// pass places its midpoint at 63%, and resampling that onto an even grid would move it.
			//
			// Auto, not a spelled-out TArray: FMixtormatRamp::Points carries its own inline allocator,
			// and naming a different one here would not bind to it.
			const auto& Points = DefiningRamp->Points;

			const int32 Available = Points.Num();
			const int32 Count = FMath::Clamp(Available, 2, FMixtormatSurfaceSamples::MaxStops);
			OutSamples.Positions.Reserve(Count);
			OutSamples.Colors.Reserve(Count);

			for (int32 Index = 0; Index < Count; ++Index)
			{
				// Subsampling when the ramp is longer than MaxStops picks by index across the whole
				// ramp, so the shape of the curve survives rather than just its first Count points.
				const int32 Source = Available > Count
					? FMath::RoundToInt(static_cast<float>(Index) * static_cast<float>(Available - 1) / static_cast<float>(Count - 1))
					: Index;
				OutSamples.Positions.Add(FMath::Clamp(Points[FMath::Clamp(Source, 0, Available - 1)].Position, 0.0f, 1.0f));
			}
		}

		for (int32 StopIndex = 0; StopIndex < OutSamples.Positions.Num(); ++StopIndex)
		{
			const float T = OutSamples.Positions[StopIndex];
			FLinearColor Color = Base;

			for (const FMixtormatPaintLayer& Layer : Recipe.Layers)
			{
				if (!Layer.bEnabled)
				{
					continue;
				}
				Color = MixtormatCompositing::ApplyBlend(
					LayerBlend(Layer, State),
					Color,
					LayerSourceAt(Layer, Palette, State, T));
			}

			// The surface owns its rectangle. Every blend above preserves the backdrop's alpha, so
			// carrying it through would paint the base's translucency back over whatever the widget
			// put behind -- reintroducing exactly the composite the recipe already performed.
			Color.A = 1.0f;
			OutSamples.Colors.Add(Color);
		}

		return OutSamples.Colors.Num();
	}

	int32 FMixtormatSurfacePainter::PaintBody(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatSurfaceSamples& BodySamples)
	{
		const FVector2f Size = Geometry.GetLocalSize();
		if (Size.X <= 0.0f || Size.Y <= 0.0f || BodySamples.Colors.Num() == 0)
		{
			return LayerId;
		}

		const float Radius = FMath::Max(Recipe.Radius, 0.0f);
		const FVector4f Radii(Radius, Radius, Radius, Radius);
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");

		// The only path that allocates nothing at all.
		//
		// MakeBox has no corner-radius parameter in SlateCore 5.8 -- only MakeGradient carries radii
		// -- so a rounded surface cannot be a box without inventing an FSlateRoundedBoxBrush, which
		// allocates a UObject per paint and is banned outright. A rounded flat surface therefore
		// goes through MakeGradient as two identical stops, which is a uniform fill that happens to
		// know about its corners.
		if (Radius <= 0.0f && BodySamples.Colors.Num() == 1)
		{
			FSlateDrawElement::MakeBox(
				Elements, LayerId, Geometry.ToPaintGeometry(), White,
				ESlateDrawEffect::None, BodySamples.Colors[0]);
			return LayerId;
		}

		TArray<MixtormatGradient::FStop, TInlineAllocator<8>> Stops;
		Stops.Reserve(FMath::Max(BodySamples.Colors.Num(), 2));

		if (BodySamples.Colors.Num() == 1)
		{
			// Two stops of the same colour: a zero-length span, which is how a uniform fill is
			// expressed through an API that only speaks gradients.
			Stops.Add({ 0.0f, BodySamples.Colors[0] });
			Stops.Add({ 1.0f, BodySamples.Colors[0] });
		}
		else
		{
			for (int32 Index = 0; Index < BodySamples.Colors.Num(); ++Index)
			{
				Stops.Add({ BodySamples.Positions[Index], BodySamples.Colors[Index] });
			}
		}

		MixtormatGradient::Paint(
			Elements, LayerId, Geometry.ToPaintGeometry(), Size,
			SlateOrientation(BodySamples.Axis), Stops, Radii);
		return LayerId;
	}

	int32 FMixtormatSurfacePainter::PaintBorders(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatResolvedPalette& Palette,
		const FWidgetStyle& WidgetStyle,
		const FMixtormatSurfaceSamples& BodySamples)
	{
		(void)WidgetStyle;

		const FVector2f Size = Geometry.GetLocalSize();
		if (Size.X <= 0.0f || Size.Y <= 0.0f || Recipe.Borders.Num() == 0)
		{
			return LayerId;
		}

		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		const EMixtormatAxis Axis = BodySamples.Axis;

		for (const FMixtormatBorderLayer& Border : Recipe.Borders)
		{
			// A border wider than half the surface would fold inward and paint over itself, so it is
			// clamped to the largest width that still fits instead of drawn overlapping.
			const float Width = FMath::Clamp(Border.Width, 0.0f, FMath::Min(Size.X, Size.Y) * 0.5f);
			if (Width <= 0.0f)
			{
				continue;
			}

			const auto Edge = [&](const EMixtormatBorderEdge Which, const FVector2f& EdgeSize, const FVector2f& Offset)
			{
				const float T = BorderEdgePosition(Axis, Which);

				FLinearColor Source = ResolveColor(Palette, Border.Source);
				Source.A *= EvaluateRamp(Border.OpacityRamp, T);

				// Blended against the body that is already on screen, not against a re-derived
				// colour: with a rounded or ramped body the re-derived one would not match.
				const FLinearColor Color = MixtormatCompositing::ApplyBlend(
					Border.Blend, SampleBodyAt(BodySamples, T), Source);

				FSlateDrawElement::MakeBox(
					Elements, LayerId,
					Geometry.ToPaintGeometry(EdgeSize, FSlateLayoutTransform(Offset)),
					White, ESlateDrawEffect::None, Color);
			};

			// Drawn inward from each edge, so the border never lands outside the surface it outlines.
			if (Border.bTop)
			{
				Edge(EMixtormatBorderEdge::Top, FVector2f(Size.X, Width), FVector2f(0.0f, 0.0f));
			}
			if (Border.bBottom)
			{
				Edge(EMixtormatBorderEdge::Bottom, FVector2f(Size.X, Width), FVector2f(0.0f, Size.Y - Width));
			}
			if (Border.bLeft)
			{
				Edge(EMixtormatBorderEdge::Left, FVector2f(Width, Size.Y), FVector2f(0.0f, 0.0f));
			}
			if (Border.bRight)
			{
				Edge(EMixtormatBorderEdge::Right, FVector2f(Width, Size.Y), FVector2f(Size.X - Width, 0.0f));
			}
		}

		return LayerId;
	}

	int32 FMixtormatSurfacePainter::PaintSurfaceWithState(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatResolvedPalette& Palette,
		const FWidgetStyle& WidgetStyle,
		const FMixtormatStateModifier& State)
	{
		FMixtormatSurfaceSamples Samples;
		CompositeSurface(Recipe, Palette, State, Samples);

		const int32 NextLayer = PaintBody(Elements, LayerId, Geometry, Recipe, Samples);
		return PaintBorders(Elements, NextLayer, Geometry, Recipe, Palette, WidgetStyle, Samples);
	}

	int32 FMixtormatSurfacePainter::PaintSurface(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FMixtormatSurfaceRecipe& Recipe,
		const FMixtormatResolvedPalette& Palette,
		const FWidgetStyle& WidgetStyle)
	{
		return PaintSurfaceWithState(
			Elements, LayerId, Geometry, Recipe, Palette, WidgetStyle, FMixtormatStateModifier());
	}
}