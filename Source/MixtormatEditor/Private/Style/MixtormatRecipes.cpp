// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatRecipes.h"

#include "Style/MixtormatResolvedStyle.h"

namespace Mixtormat
{
	namespace
	{
		// The well's ground and recess, shared by the well, the fill and the checked toggle.
		//
		// Built into an existing recipe rather than returned on its own so the three callers cannot
		// drift apart -- which is exactly what happened when the slider, the chip and the toggle
		// each assembled their own well.
		void AddWellSurface(FMixtormatSurfaceRecipe& Recipe, const FMixtormatTheme& Theme)
		{
			Recipe.Base = MakeColorRef(EMixtormatColorRole::Ground);

			FMixtormatPaintLayer Shade;
			Shade.Source = MakeColorRef(EMixtormatColorRole::Shade);
			Shade.Blend = Theme.Well.ShadeBlend;
			Shade.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical, Theme.Well.ShadeTop, Theme.Well.ShadeBottom, 2);
			Recipe.Layers.Add(Shade);
		}

		// The outlined variant's hover also lifts the ground, by adding the ground to itself. That
		// is not a restatement of the border change: the border says "this control is under the
		// pointer", the lift says "and its whole recess brightened". The prototype authors them as
		// separate numbers, so they stay separate layers.
		void AddHoverLift(FMixtormatSurfaceRecipe& Recipe, const FMixtormatTheme& Theme)
		{
			FMixtormatPaintLayer Lift;
			Lift.Source = MakeColorRef(EMixtormatColorRole::Ground);
			Lift.Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;
			Lift.Strength = Theme.Well.HoverLiftOpacity;
			Recipe.Layers.Add(Lift);
		}

		// The outline. Its ramp carries the overall intensity multiplied by the per-edge endpoint,
		// which is what the prototype authors -- one overall number times a per-edge one -- and what
		// a single flat border colour would throw away.
		FMixtormatBorderLayer MakeWellBorder(const FMixtormatTheme& Theme, const EMixtormatWellState State)
		{
			const bool bHover = State == EMixtormatWellState::Hover;

			FMixtormatColorRef Source = MakeColorRef(EMixtormatColorRole::Hairline);
			Source.Saturation = Theme.Well.BorderSaturation;

			const float Intensity = bHover ? Theme.Well.BorderHoverOpacity : Theme.Well.BorderOpacity;
			const float Top = bHover ? Theme.Well.BorderHoverTopOpacity : Theme.Well.BorderTopOpacity;
			const float Bottom = bHover ? Theme.Well.BorderHoverBottomOpacity : Theme.Well.BorderBottomOpacity;

			FMixtormatBorderLayer Border;
			Border.Source = Source;
			Border.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
			Border.Width = Theme.Well.BorderWidth;
			// A vertical ramp, so the painter samples the top edge at 0 and the bottom at 1.
			Border.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical, Intensity * Top, Intensity * Bottom, 2);
			Border.bTop = true;
			Border.bBottom = true;
			return Border;
		}
	}

	FMixtormatRamp MakeLinearRamp(const EMixtormatAxis Axis, const float Start, const float End, const int32 Samples)
	{
		FMixtormatRamp Ramp;
		Ramp.Axis = Axis;

		// Two stops minimum: a ramp with fewer describes a flat fill, and the painter would reject it.
		const int32 Count = FMath::Max(Samples, 2);
		Ramp.Points.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float T = static_cast<float>(Index) / static_cast<float>(Count - 1);
			Ramp.Points.Add({ T, FMath::Lerp(Start, End, T) });
		}
		return Ramp;
	}

	FMixtormatRamp MakeMidpointRamp(const EMixtormatAxis Axis, const float Start, const float Mid,
		const float MidPosition, const float End)
	{
		FMixtormatRamp Ramp;
		Ramp.Axis = Axis;
		Ramp.Points.Reserve(3);
		Ramp.Points.Add({ 0.0f, Start });
		Ramp.Points.Add({ FMath::Clamp(MidPosition, 0.0f, 1.0f), Mid });
		Ramp.Points.Add({ 1.0f, End });
		return Ramp;
	}

	FMixtormatRamp MakeFalloffRamp(const EMixtormatAxis Axis, const float Start, const float End,
		const float Power, const int32 Samples)
	{
		FMixtormatFalloff Falloff;
		Falloff.Start = Start;
		Falloff.End = End;
		Falloff.Power = Power;

		FMixtormatRamp Ramp;
		Ramp.Axis = Axis;

		const int32 Count = FMath::Max(Samples, 2);
		Ramp.Points.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float T = static_cast<float>(Index) / static_cast<float>(Count - 1);
			Ramp.Points.Add({ T, EvaluateFalloff(Falloff, T) });
		}
		return Ramp;
	}

	FMixtormatSurfaceRecipe MakeWellRecipe(const FMixtormatTheme& Theme, const EMixtormatWellState State)
	{
		FMixtormatSurfaceRecipe Recipe;
		AddWellSurface(Recipe, Theme);

		if (State == EMixtormatWellState::Hover)
		{
			AddHoverLift(Recipe, Theme);
		}

		Recipe.Radius = Theme.Well.Radius;
		Recipe.Borders.Add(MakeWellBorder(Theme, State));
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeFlatWellRecipe(const FMixtormatTheme& Theme)
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::Ground);
		Recipe.Radius = Theme.Well.Radius;
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeFillRecipe(const FMixtormatTheme& Theme, const EMixtormatFillState State)
	{
		FMixtormatSurfaceRecipe Recipe;
		AddWellSurface(Recipe, Theme);

		// The accent body, added over the well. Its weight comes from the ramp; the saturation from
		// the layer's own reference, because one accent is authored at three different saturations
		// across the fill's three states and a shared role could not carry that.
		FMixtormatColorRef Body = MakeColorRef(EMixtormatColorRole::Accent);

		float Top = Theme.Fill.Top;
		float Bottom = Theme.Fill.Bottom;
		switch (State)
		{
		case EMixtormatFillState::Hover:
			Top = Theme.Fill.HoverTop;
			Bottom = Theme.Fill.HoverBottom;
			Body.Saturation = Theme.Fill.HoverSaturation;
			break;

		case EMixtormatFillState::Active:
			Top = Theme.Fill.ActiveTop;
			Bottom = Theme.Fill.ActiveBottom;
			Body.Saturation = Theme.Fill.ActiveSaturation;
			break;

		case EMixtormatFillState::Disabled:
			// One weight at both ends, so a disabled fill is flat rather than merely paler -- it has
			// to read as unavailable, not as a lighter value of the same thing.
			Top = Theme.Fill.DisabledOpacity;
			Bottom = Theme.Fill.DisabledOpacity;
			Body.Saturation = Theme.Fill.DisabledSaturation;
			break;

		case EMixtormatFillState::Rest:
		default:
			Body.Saturation = Theme.Fill.Saturation;
			break;
		}

		FMixtormatPaintLayer BodyLayer;
		BodyLayer.Source = Body;
		BodyLayer.Blend = Theme.Fill.BodyBlend;
		// Through the authored power curve, not interpolated straight between its endpoints. The
		// exponent is 0.05, so the body holds near its top value and drops late.
		BodyLayer.OpacityRamp = MakeFalloffRamp(
			EMixtormatAxis::Vertical, Top, Bottom, Theme.Fill.FalloffPower, 6);
		Recipe.Layers.Add(BodyLayer);

		// The shade pass is a separate layer on the other axis with a midpoint, which is what makes
		// the fill read as eased rather than lit from one edge. Its blend is independent of the
		// body's, and that independence is the point: one is adding light, the other removing it.
		FMixtormatPaintLayer ShadeLayer;
		ShadeLayer.Source = MakeColorRef(EMixtormatColorRole::Shade);
		ShadeLayer.Blend = Theme.Fill.ShadeBlend;
		ShadeLayer.OpacityRamp = MakeMidpointRamp(
			EMixtormatAxis::Horizontal,
			Theme.Fill.ShadeStart, Theme.Fill.ShadeMid, Theme.Fill.ShadeMidPosition, Theme.Fill.ShadeEnd);
		Recipe.Layers.Add(ShadeLayer);

		// The fill sits inside the trough, so it carries the trough's radius rather than none --
		// the two surfaces meet at the ends and a mismatch there would be a visible notch.
		Recipe.Radius = Theme.Well.Radius;
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeCheckedToggleRecipe(const FMixtormatTheme& Theme, const EMixtormatWellState WellState,
		const EMixtormatFillState FillState)
	{
		FMixtormatSurfaceRecipe Recipe = MakeWellRecipe(Theme, WellState);

		// The fill layer, built from the same numbers as the slider's. There is no toggle-specific
		// fill: a toggle's fill is a slider fill, which is the point of §19.
		FMixtormatPaintLayer Body;
		Body.Source = MakeColorRef(EMixtormatColorRole::Accent);
		Body.Blend = Theme.Fill.BodyBlend;

		float Top = Theme.Fill.Top;
		float Bottom = Theme.Fill.Bottom;
		switch (FillState)
		{
		case EMixtormatFillState::Hover:
			Top = Theme.Fill.HoverTop;
			Bottom = Theme.Fill.HoverBottom;
			Body.Source.Saturation = Theme.Fill.HoverSaturation;
			break;

		case EMixtormatFillState::Active:
			Top = Theme.Fill.ActiveTop;
			Bottom = Theme.Fill.ActiveBottom;
			Body.Source.Saturation = Theme.Fill.ActiveSaturation;
			break;

		case EMixtormatFillState::Disabled:
			Top = Theme.Fill.DisabledOpacity;
			Bottom = Theme.Fill.DisabledOpacity;
			Body.Source.Saturation = Theme.Fill.DisabledSaturation;
			break;

		case EMixtormatFillState::Rest:
		default:
			Body.Source.Saturation = Theme.Fill.Saturation;
			break;
		}

		Body.OpacityRamp = MakeFalloffRamp(EMixtormatAxis::Vertical, Top, Bottom, Theme.Fill.FalloffPower, 6);
		Recipe.Layers.Add(Body);

		// No shade pass. The prototype's toggle fill carries only the body gradient, and adding the
		// fill's horizontal Multiply shade here would darken a 16px control across its short axis --
		// a gradient the design never authored for this surface.
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeButtonRecipe(const FMixtormatTheme& Theme, const EMixtormatButtonState State)
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::Ground);
		// Square. components.css gives the shared button `border-radius: 0`, and the theme has no
		// button radius because nothing in the prototype authors one.
		Recipe.Radius = 0.0f;

		float Top = Theme.Button.RestTop;
		float Bottom = Theme.Button.RestBottom;
		float HairlineOpacity = Theme.Button.HairlineOpacity;
		switch (State)
		{
		case EMixtormatButtonState::Hover:
			Top = Theme.Button.HoverTop;
			Bottom = Theme.Button.HoverBottom;
			HairlineOpacity = Theme.Button.HairlineHoverOpacity;
			break;

		case EMixtormatButtonState::Selected:
			Top = Theme.Button.SelectedTop;
			Bottom = Theme.Button.SelectedBottom;
			HairlineOpacity = Theme.Button.HairlineSelectedOpacity;
			break;

		case EMixtormatButtonState::Rest:
		default:
			break;
		}

		// The body: an Accent gradient over Ground, at the blend the design chose. One ramp per
		// state rather than a state modifier on one ramp, because the prototype authors different
		// top and bottom values per state -- these are not a scalar nudge of each other.
		FMixtormatColorRef Body = MakeColorRef(EMixtormatColorRole::Accent);
		Body.Saturation = Theme.Button.GradientSaturation;

		FMixtormatPaintLayer BodyLayer;
		BodyLayer.Source = Body;
		BodyLayer.Blend = Theme.Button.BodyBlend;
		BodyLayer.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical, Top, Bottom, 2);
		Recipe.Layers.Add(BodyLayer);

		// The hairline is a separate border with its OWN blend, deliberately: a button whose body is
		// Normal and whose top edge is Additive is a different thing from one where both are
		// Normal, and collapsing them would remove the only control the prototype gives over that.
		FMixtormatColorRef Hairline = MakeColorRef(EMixtormatColorRole::Accent);
		Hairline.Saturation = Theme.Button.HairlineSaturation;
		Hairline.Opacity = HairlineOpacity;

		FMixtormatBorderLayer Border;
		Border.Source = Hairline;
		Border.Blend = Theme.Button.HairlineBlend;
		Border.Width = Theme.Button.HairlineWidth;
		Border.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical, 1.0f, 1.0f, 2);
		Border.bTop = true;
		Recipe.Borders.Add(Border);

		return Recipe;
	}
}