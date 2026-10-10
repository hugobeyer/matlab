// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatRecipes.h"

#include "Style/MixtormatResolvedStyle.h"

namespace Mixtormat
{
	namespace
	{
		// Match luminance before tinting so group/reference hues preserve the row's state contrast.
				FLinearColor TintTowards(const FLinearColor& Base, const FLinearColor& Accent, float Strength)
				{
					const float Luminance = Accent.GetLuminance();
					if (Accent.A <= 0.0f || Strength <= 0.0f || Luminance <= KINDA_SMALL_NUMBER)
					{
						return Base;
					}
					const float Scale = Base.GetLuminance() / Luminance;
					return FMath::Lerp(Base,
						FLinearColor(Accent.R * Scale, Accent.G * Scale, Accent.B * Scale, Base.A),
						FMath::Clamp(Strength, 0.0f, 1.0f));
				}

				FMixtormatColorRef LocalLayerSource(const FLinearColor& Color, float Saturation)
				{
					FMixtormatColorRef Ref;
					Ref.LocalColor = Color;
					Ref.Saturation = Saturation;
					return Ref;
				}

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

	FMixtormatSurfaceRecipe MakeLayerBodyRecipe(
			const FMixtormatTheme& Theme, const FMixtormatLayerRecipeContext& Context)
		{
			const FMixtormatLayerTheme& L = Theme.Layer;
			const bool bChild = Context.Kind == EMixtormatLayerKind::Child;
			FLinearColor Start = Theme.Palette.Panel;
			FLinearColor End = L.RowBottom;
			if (bChild)
			{
				Start = Context.bSelected ? L.ChildSelectedLeft : L.ChildLeft;
				End = Context.bSelected ? L.ChildSelectedRight : L.ChildRight;
			}
			else if (Context.bSelected)
			{
				Start = L.SelectedTop;
				End = L.SelectedBottom;
			}
			else if (Context.bHovered)
			{
				Start = L.HoverTop;
				End = L.HoverBottom;
			}
			if (!Context.bVisible)
			{
				Start = L.HiddenTop;
				End = L.HiddenEnd;
			}
			if (Context.Kind == EMixtormatLayerKind::Group && Context.bVisible)
			{
				const float Tint = Context.bSelected ? L.GroupTintSelectedStrength : L.GroupTintStrength;
				Start = TintTowards(Start, Context.GroupTint, Tint);
				End = TintTowards(End, Context.GroupTint, Tint);
			}
			if (Context.bReference)
			{
				const float Tint = !Context.bVisible ? L.ReferenceHiddenTint
					: Context.bSelected ? L.ReferenceSelectedTint
					: Context.bHovered ? L.ReferenceHoverTint : L.ReferenceRestTint;
				Start = FMath::Lerp(Theme.Palette.Panel, Theme.Palette.Modified, Tint);
			}
			const bool bInstanceSource = bChild && Context.bInstanceSource && !Context.bSelected;
			if (bInstanceSource)
			{
				Start = Theme.Palette.Accent;
				End = Theme.Palette.Accent;
			}
			const float Saturation = bChild
				? (Context.bSelected ? L.ChildSelectedSaturation : Context.bHovered ? L.ChildHoverSaturation : L.ChildSaturation)
				: (Context.bSelected ? L.SelectedSaturation : Context.bHovered ? L.HoverSaturation : L.RestSaturation);
			const float Right = bInstanceSource ? L.InstanceSourceRightTint : bChild
				? (Context.bSelected ? L.ChildSelectedStrength : Context.bHovered ? L.ChildHoverStrength : L.ChildStrength)
				: (Context.bSelected ? L.SelectedStrength : Context.bHovered ? L.HoverStrength : L.RestStrength);
			const float Left = bInstanceSource ? L.InstanceSourceLeftTint : bChild
				? (Context.bSelected ? L.ChildSelectedLeftOpacity : Context.bHovered ? L.ChildHoverLeftOpacity : L.ChildLeftOpacity)
				: Right;
			FMixtormatSurfaceRecipe Recipe;
			Recipe.Base = MakeColorRef(EMixtormatColorRole::Ground);
			Recipe.Radius = L.Radius;
			FMixtormatPaintLayer Body;
			Body.Source = LocalLayerSource(Start, Saturation);
			Body.SourceEnd = LocalLayerSource(End, Saturation);
			Body.Blend = L.Blend;
			Body.OpacityRamp = MakeLinearRamp(bChild ? EMixtormatAxis::Horizontal : EMixtormatAxis::Vertical, Left, Right);
			Recipe.Layers.Add(Body);
			return Recipe;
		}

		FMixtormatSurfaceRecipe MakeLayerGroupCrossRecipe(
			const FMixtormatTheme& Theme, const FMixtormatLayerRecipeContext& Context)
		{
			FMixtormatSurfaceRecipe Recipe;
			FMixtormatPaintLayer Cross;
			const float Tint = Context.bSelected ? Theme.Layer.GroupTintSelectedStrength : Theme.Layer.GroupTintStrength;
			Cross.Source = LocalLayerSource(TintTowards(Theme.Layer.Cross, Context.GroupTint, Tint), Theme.Layer.GroupSaturation);
			Cross.Blend = Theme.Layer.GroupBlend;
			Cross.Strength = Theme.Layer.GroupStrength;
			Cross.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Horizontal, 1.0f, 0.0f);
			Cross.bEnabled = Context.Kind == EMixtormatLayerKind::Group && Context.bVisible;
			Recipe.Layers.Add(Cross);
			return Recipe;
		}

		FMixtormatSurfaceRecipe MakeLayerGlowRecipe(const FMixtormatTheme& Theme, float ReachFraction)
		{
			FMixtormatSurfaceRecipe Recipe;
			FMixtormatPaintLayer Glow;
			Glow.Source = Theme.Layer.ActiveGlow.Source;
			Glow.Source.Saturation = Theme.Layer.ActiveGlow.Saturation;
			Glow.Strength = Theme.Layer.ActiveGlow.Opacity;
			Glow.Blend = Theme.Layer.ActiveGlow.Blend;
			const float Reach = FMath::Clamp(ReachFraction, 0.0f, 1.0f);
			Glow.bEnabled = Reach > 0.0f;
			Glow.OpacityRamp.Axis = EMixtormatAxis::Vertical;
			Glow.OpacityRamp.Points.Add({ 0.0f, 1.0f });
			Glow.OpacityRamp.Points.Add({ Reach, 0.0f });
			if (Reach < 1.0f) { Glow.OpacityRamp.Points.Add({ 1.0f, 0.0f }); }
			Recipe.Layers.Add(Glow);
			return Recipe;
		}

		FMixtormatSurfaceRecipe MakeLayerHairlineRecipe(
			const FMixtormatTheme& Theme, const bool bSelected, const EMixtormatLayerKind Kind)
		{
			FMixtormatSurfaceRecipe Recipe;
			FMixtormatPaintLayer Hairline;
			Hairline.Source = MakeColorRef(bSelected ? EMixtormatColorRole::Accent : EMixtormatColorRole::Hairline);
			Hairline.Source.Saturation = bSelected ? Theme.Layer.ActiveGlow.Saturation : 1.0f;
			Hairline.Strength = bSelected
				? (Kind == EMixtormatLayerKind::Child
					? Theme.Layer.ChildActiveHairlineOpacity : Theme.Layer.ActiveHairlineOpacity)
				: Kind == EMixtormatLayerKind::Group ? Theme.Layer.GroupHairlineOpacity
				: Kind == EMixtormatLayerKind::Child ? Theme.Layer.ChildHairlineOpacity
				: Theme.Layer.HairlineOpacity;
			Hairline.Blend = bSelected ? MixtormatCompositing::EMixtormatBlendMode::Additive : MixtormatCompositing::EMixtormatBlendMode::Normal;
			Hairline.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Horizontal, 1.0f, 1.0f, 2);
			Recipe.Layers.Add(Hairline);
			return Recipe;
		}

		FMixtormatSurfaceRecipe MakeGroundRecipe()
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::Ground);
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakePreviewClusterRecipe(const FMixtormatTheme& Theme)
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.bTranslucent = true;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::OverlayGround);
		Recipe.Radius = Theme.Well.Radius;
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeFoldoutRecipe(const FMixtormatTheme& Theme,
		const bool bHovered, const bool bEnabled)
	{
		FMixtormatSurfaceRecipe Recipe = MakeGroundRecipe();
		Recipe.Radius = Theme.FoldoutLayout.Radius;
		const FMixtormatFoldoutTheme& Foldout = Theme.Foldout;
		const float Saturation = bHovered ? Foldout.HoverSaturation : Foldout.LiftSaturation;
		const FMixtormatFalloff& Falloff = Foldout.LiftFalloff;

		FMixtormatPaintLayer Lift;
		Lift.Source.LocalColor = bHovered ? Foldout.HoverTint : Foldout.LiftTint;
		Lift.Source.Opacity = bHovered ? Foldout.HoverTintOpacity : Foldout.LiftOpacity;
		Lift.Source.Saturation = Saturation;
		Lift.Blend = Foldout.LiftBlend;
		Lift.OpacityRamp = MakeFalloffRamp(EMixtormatAxis::Vertical,
			Falloff.Start, Falloff.End, Falloff.Power, Falloff.Samples);
		Recipe.Layers.Add(Lift);

		FMixtormatPaintLayer Accent;
		Accent.Source = MakeColorRef(EMixtormatColorRole::Accent);
		Accent.Source.Opacity = bHovered ? Foldout.AccentHoverOpacity : Foldout.AccentOpacity;
		Accent.Source.Saturation = Saturation;
		Accent.Blend = Foldout.AccentBlend;
		Accent.OpacityRamp = Lift.OpacityRamp;
		Recipe.Layers.Add(Accent);

		if (bEnabled)
		{
			FMixtormatBorderLayer Hairline;
			Hairline.Source = MakeColorRef(EMixtormatColorRole::Hairline);
			if (bHovered)
			{
				Hairline.Source.LocalColor = Foldout.HairlineHoverTint;
			}
			Hairline.Source.Opacity = bHovered ? Foldout.HairlineHoverOpacity : Foldout.HairlineOpacity;
			Hairline.Source.Saturation = bHovered ? Foldout.HairlineHoverSaturation : Foldout.HairlineSaturation;
			Hairline.Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;
			Hairline.bTop = true;
			Recipe.Borders.Add(Hairline);
		}
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeGalleryTabRecipe(const FMixtormatTheme& Theme, const bool bHovered)
	{
		// The collapsed gallery's restore tab: the foldout header anatomy (Ground -> lift -> Accent,
		// enabled-only top hairline) with only the top corners rounded, because the tab's bottom
		// edge sits flush above the status bar like a drawer handle pointing up.
		FMixtormatSurfaceRecipe Recipe = MakeFoldoutRecipe(Theme, bHovered, true);
		const float R = FMath::Max(Theme.FoldoutLayout.Radius, 0.0f);
		Recipe.CornerRadii = FVector4f(R, R, 0.0f, 0.0f);
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeCardHeaderRecipe(const FMixtormatTheme& Theme, const float Seam)
	{
		FMixtormatSurfaceRecipe Recipe = MakeGroundRecipe();
		FMixtormatPaintLayer Header;
		Header.Source = MakeColorRef(EMixtormatColorRole::Ground);
		Header.Source.Saturation = Theme.Card.HeaderSaturation;
		Header.Blend = Theme.Card.Blend;
		Header.OpacityRamp.Axis = EMixtormatAxis::Vertical;

		FMixtormatFalloff Falloff;
		Falloff.Start = Theme.Card.HeaderOpacity;
		Falloff.End = Theme.Card.BodyOpacity;
		Falloff.Power = Theme.Card.FalloffPower;
		const float ClampedSeam = FMath::Clamp(Seam, 0.0f, 1.0f);
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 5.0f;
			Header.OpacityRamp.Points.Add({ T, EvaluateFalloff(Falloff, T * ClampedSeam) });
		}
		Recipe.Layers.Add(Header);
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeCardBodyRecipe(const FMixtormatTheme& Theme,
		const float Seam, const float TailFraction)
	{
		FMixtormatSurfaceRecipe Recipe = MakeGroundRecipe();
		FMixtormatPaintLayer Body;
		Body.Source = MakeColorRef(EMixtormatColorRole::Ground);
		Body.Source.Saturation = Theme.Card.BodySaturation;
		Body.Blend = Theme.Card.Blend;
		if (TailFraction <= 0.0f)
		{
			Body.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical,
				Theme.Card.BodyOpacity, Theme.Card.BodyOpacity, 2);
		}
		else
		{
			FMixtormatFalloff Falloff;
			Falloff.Start = Theme.Card.HeaderOpacity;
			Falloff.End = Theme.Card.BodyOpacity;
			Falloff.Power = Theme.Card.FalloffPower;
			const float ClampedSeam = FMath::Clamp(Seam, 0.0f, 1.0f);
			const float Tail = FMath::Min(TailFraction, 0.5f);
			Body.OpacityRamp.Axis = EMixtormatAxis::Vertical;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const float T = static_cast<float>(Index) / 5.0f;
				Body.OpacityRamp.Points.Add({ T * Tail,
					EvaluateFalloff(Falloff, ClampedSeam + T * (1.0f - ClampedSeam)) });
			}
			// Reuse the sampled values so the bottom is exactly the reversed body tail.
			for (int32 Index = 5; Index >= 0; --Index)
			{
				const FMixtormatRampPoint Stop = Body.OpacityRamp.Points[Index];
				const float Position = 1.0f - Stop.Position;
				if (Position > Body.OpacityRamp.Points.Last().Position)
				{
					Body.OpacityRamp.Points.Add({ Position, Stop.Value });
				}
			}
		}
		Recipe.Layers.Add(Body);
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeMenuPanelRecipe(const FMixtormatTheme& Theme, const float MenuHeight)
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::MenuGround);
		Recipe.Radius = Theme.Menu.CornerRadius;

		// Stops are placed in panel coordinates, but their values are sampled over the fixed
		// physical lip. A short panel gets only the prefix of that 25px treatment.
		const float LipHeight = FMath::Max(Theme.MenuLayout.LipHeight, UE_SMALL_NUMBER);
		const float AvailableHeight = FMath::Clamp(MenuHeight, 0.0f, LipHeight);
		const float PanelHeight = FMath::Max(MenuHeight, UE_SMALL_NUMBER);
		const float SampleFraction = AvailableHeight / LipHeight;
		const float PanelFraction = AvailableHeight / PanelHeight;
		FMixtormatRamp LipRamp;
		LipRamp.Axis = EMixtormatAxis::Vertical;
		constexpr int32 Samples = 6;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float T = static_cast<float>(Index) / (Samples - 1);
			LipRamp.Points.Add({ T * PanelFraction, 1.0f - T * SampleFraction });
		}
		if (PanelFraction < 1.0f)
		{
			LipRamp.Points.Add({ 1.0f, 0.0f });
		}

		FMixtormatColorRef LipSource;
		LipSource.LocalColor = Theme.Menu.LipSource;
		FMixtormatPaintLayer Lip;
		Lip.Source = LipSource;
		Lip.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		Lip.OpacityRamp = LipRamp;
		Recipe.Layers.Add(Lip);

		FMixtormatPaintLayer Tint;
		Tint.Source = MakeColorRef(EMixtormatColorRole::Accent);
		Tint.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		Tint.Strength = Theme.Menu.LipTintOpacity;
		Tint.OpacityRamp = LipRamp;
		Recipe.Layers.Add(Tint);

		FMixtormatBorderLayer Border;
		Border.Source = MakeColorRef(EMixtormatColorRole::Hairline);
		Border.Width = 1.0f;
		Border.OpacityRamp = MakeLinearRamp(EMixtormatAxis::None, Theme.Menu.BorderOpacity, Theme.Menu.BorderOpacity, 2);
		Border.bTop = Border.bBottom = Border.bLeft = Border.bRight = true;
		Recipe.Borders.Add(Border);
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeMenuRowRecipe(const FMixtormatTheme& Theme, const EMixtormatMenuRowState State)
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::MenuGround);
		Recipe.Radius = Theme.Menu.CornerRadius;
		FMixtormatColorRef Source;
		float Opacity = 0.0f;
		switch (State)
		{
		case EMixtormatMenuRowState::Hover:
			Source = MakeColorRef(EMixtormatColorRole::Panel);
			Opacity = Theme.Menu.ItemHoverOpacity;
			break;
		case EMixtormatMenuRowState::Checked:
			Source = MakeColorRef(EMixtormatColorRole::Panel);
			Opacity = Theme.Menu.ItemCheckedOpacity;
			break;
		case EMixtormatMenuRowState::Disabled:
			break;
		case EMixtormatMenuRowState::Destructive:
			break;
		case EMixtormatMenuRowState::DestructiveHover:
			Source.LocalColor = Theme.Menu.DestructiveHover;
			Opacity = Theme.Menu.ItemHoverOpacity;
			break;
		default:
			break;
		}
		if (Opacity > 0.0f)
		{
			FMixtormatPaintLayer Layer;
			Layer.Source = Source;
			Layer.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
			Layer.Strength = Opacity;
			Recipe.Layers.Add(Layer);
		}
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeGalleryTileRecipe(
		const FMixtormatResolvedStyle& Style, const EMixtormatGalleryTileState State)
	{
		const FMixtormatResolvedGalleryStyle& Gallery = Style.Gallery;
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::ThumbnailGround);
		Recipe.Radius = Gallery.CornerRadius;

		if (State == EMixtormatGalleryTileState::Hover
			|| State == EMixtormatGalleryTileState::SelectedHover)
		{
			FMixtormatPaintLayer Lift;
			Lift.Source = MakeColorRef(EMixtormatColorRole::Accent);
			Lift.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
			Lift.Strength = Gallery.HoverLiftOpacity;
			Recipe.Layers.Add(Lift);
		}

		FMixtormatBorderLayer Border;
		Border.Source = MakeColorRef(EMixtormatColorRole::Hairline);
		Border.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		Border.Width = Gallery.BorderWidth;
		Border.OpacityRamp = MakeLinearRamp(EMixtormatAxis::None, Gallery.BorderOpacity, Gallery.BorderOpacity, 2);
		Border.bTop = Border.bBottom = Border.bLeft = Border.bRight = true;
		Recipe.Borders.Add(Border);

		if (State == EMixtormatGalleryTileState::Selected
			|| State == EMixtormatGalleryTileState::SelectedHover)
		{
			FMixtormatBorderLayer Selected = Border;
			Selected.Source = MakeColorRef(EMixtormatColorRole::Accent);
			Selected.Width = Gallery.SelectedEdgeWidth;
			Selected.OpacityRamp = MakeLinearRamp(EMixtormatAxis::None,
				Gallery.SelectedEdgeOpacity, Gallery.SelectedEdgeOpacity, 2);
			Recipe.Borders.Add(Selected);
		}
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeGalleryCaptionRecipe()
	{
		FMixtormatSurfaceRecipe Recipe;
		Recipe.Base = MakeColorRef(EMixtormatColorRole::Shade);
		return Recipe;
	}

	// The translucent plate behind a viewport overlay control.
//
// The prototype's `rgb(var(--overlay-plate-rgb) / var(--overlay-plate-opacity))` is a plate
	// drawn ON the rendered viewport, not a ground: the viewport has to show through it. That is why
	// this is the one surface that is allowed to keep its alpha (see FMixtormatSurfaceRecipe::
	// bTranslucent), and why its base is the plate colour itself.
	//
	// `--overlay-bottom-rgb` / `--overlay-ground-opacity` are a different surface -- the gradient
	// behind a whole overlay cluster -- and are not mixed in here. Hover and press add the accent to
	// the plate at their authored strengths, which is the CSS color-mix over the plate colour.
	FMixtormatSurfaceRecipe MakePreviewPlateRecipe(
		const FMixtormatResolvedStyle& Style, const EMixtormatPreviewPlateState State)
	{
		const FMixtormatResolvedPreviewStyle& Preview = Style.Preview;

		FMixtormatSurfaceRecipe Recipe;
		Recipe.bTranslucent = true;
		Recipe.Base.LocalColor = Preview.OverlayPlate;
		Recipe.Base.Opacity = Preview.OverlayPlateOpacity;

		float AccentOpacity = 0.0f;
		switch (State)
		{
		case EMixtormatPreviewPlateState::Hover:
			AccentOpacity = Preview.HoverAccent;
			break;
		case EMixtormatPreviewPlateState::Pressed:
			AccentOpacity = Preview.PressAccent;
			break;
		case EMixtormatPreviewPlateState::Checked:
			// The prototype's checked plate is 60% of the hover contribution.
			AccentOpacity = Preview.HoverAccent * 0.6f;
			break;
		default:
			break;
		}
		if (AccentOpacity > 0.0f)
		{
			FMixtormatPaintLayer Accent;
			Accent.Source = MakeColorRef(EMixtormatColorRole::Accent);
			Accent.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
			Accent.Strength = AccentOpacity;
			Recipe.Layers.Add(Accent);
		}
		return Recipe;
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

	FMixtormatSurfaceRecipe MakeNavigationRailTabRecipe(
		const FMixtormatTheme& Theme, const EMixtormatButtonState State,
		const int32 TabIndex, const int32 TabCount)
	{
		FMixtormatSurfaceRecipe Recipe = MakeButtonRecipe(Theme, State, false);
		const FMixtormatPreviewMetrics& Layout = Theme.PreviewLayout;
		const int32 Count = FMath::Max(TabCount, 1);
		const int32 Index = FMath::Clamp(TabIndex, 0, Count - 1);
		// Only the outside corners may round. Every shared seam stays square,
		// including when an older theme still stores a nonzero corner radius.
		const float R = FMath::Max(Layout.LeftRailCornerRadius, 0.0f);
		Recipe.CornerRadii = FVector4f(
			Index == 0 ? R : 0.0f, Index == 0 ? R : 0.0f,
			Index == Count - 1 ? R : 0.0f, Index == Count - 1 ? R : 0.0f);

		// The old drop shadow extended beyond the widget and into layer rows.
		// Shade is now an in-bounds, shared vertical ramp across every tab.
		if (Layout.LeftRailShadowOpacity > 0.0f)
		{
			FMixtormatPaintLayer Shade;
			Shade.Source = MakeColorRef(EMixtormatColorRole::Shade);
			Shade.Blend = Theme.Well.ShadeBlend;
			Shade.Strength = FMath::Clamp(Layout.LeftRailShadowOpacity, 0.0f, 1.0f);
			Shade.OpacityRamp.Axis = EMixtormatAxis::Vertical;
			constexpr int32 Samples = 8;

			const float Bias = FMath::Max(Layout.LeftRailShadeBias, 0.1f);
			Shade.OpacityRamp.Points.Reserve(Samples);
			for (int32 Point = 0; Point < Samples; ++Point)
			{
				const float LocalT = static_cast<float>(Point) / static_cast<float>(Samples - 1);
				const float GlobalT = (static_cast<float>(Index) + LocalT) / static_cast<float>(Count);
				Shade.OpacityRamp.Points.Add({ LocalT, FMath::Pow(GlobalT, Bias) });
			}
			Recipe.Layers.Add(Shade);
		}

		// Keep the shared button hairline blend but expose its thickness and strength
		// through the existing Navigation Rail tokens.
		for (FMixtormatBorderLayer& Border : Recipe.Borders)
		{
			Border.Width *= Layout.LeftRailBorderThickness;
			Border.Source.Opacity *= Layout.LeftRailBorderOpacity;
		}
		return Recipe;
	}

	FMixtormatSurfaceRecipe MakeButtonRecipe(const FMixtormatTheme& Theme, const EMixtormatButtonState State,
			const bool bShowSeparator)
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

		// The separator is a second border on the right edge, carrying the SAME authored saturation and
		// blend as the hairline but its own opacity -- which is exactly how the prototype authors it:
		// both edges live in one filtered blend layer, and only the weight differs. Its EdgeFraction
		// shortens it to the authored height and centres it, so it reads as a tick between two actions
		// rather than as a column divider.
		if (bShowSeparator)
		{
			FMixtormatColorRef Separator = MakeColorRef(EMixtormatColorRole::Text);
			Separator.Saturation = Theme.Button.HairlineSaturation;
			Separator.Opacity = Theme.Button.SeparatorOpacity;

			FMixtormatBorderLayer Tick;
			Tick.Source = Separator;
			Tick.Blend = Theme.Button.HairlineBlend;
			Tick.Width = Theme.Button.SeparatorWidth;
			Tick.EdgeFraction = Theme.Button.Height > 0.0f
				? FMath::Clamp(Theme.Button.SeparatorHeight / Theme.Button.Height, 0.0f, 1.0f)
				: 1.0f;
			Tick.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Vertical, 1.0f, 1.0f, 2);
			Tick.bRight = true;
			Recipe.Borders.Add(Tick);
		}

		return Recipe;
	}

	FLinearColor MakeButtonTextColor(const FMixtormatResolvedPalette& Palette, const FMixtormatTheme& Theme,
			const EMixtormatButtonState State)
	{
		// Selected reads as Accent; every other state is the shared Text role at the button's own
		// authored opacity, so the label is dimmed by one number rather than by a second grey.
		const bool bSelected = State == EMixtormatButtonState::Selected;
		FLinearColor Color = Palette.Get(
			bSelected ? EMixtormatColorRole::Accent : EMixtormatColorRole::Text);
		Color.A = (bSelected || State == EMixtormatButtonState::Hover) ? 1.0f : Theme.Button.TextOpacity;
		return Color;
	}
}