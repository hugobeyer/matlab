// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatResolvedStyle.h"

namespace Mixtormat
{
	namespace
	{
		// CSS applies `filter: saturate()` to a whole composited layer. A layer made of gradient
		// stops is saturated by saturating each stop, which is the only reading that keeps the
		// filter's meaning identical between a flat fill and a ramp.
		void SaturateRamp(FMixtormatResolvedRamp& Ramp, const float Amount)
		{
			for (FLinearColor& Stop : Ramp.Colors)
			{
				Stop = MixtormatCompositing::Saturate(Stop, Amount);
			}
		}
	}

	FLinearColor ResolveColor(const FMixtormatResolvedPalette& Palette, const FMixtormatColorRef& Ref)
	{
		FLinearColor Color = Palette.Get(Ref.Role);

		// Multiplier first: it is a tint of the role, so the saturation filter below should see the
		// tinted colour rather than the untouched role.
		Color.R *= Ref.Multiplier.R;
		Color.G *= Ref.Multiplier.G;
		Color.B *= Ref.Multiplier.B;

		Color.A *= Ref.Opacity;

		// Saturation last. CSS specifies it as a paint-time filter on the composited source, so
		// applying it to the palette entry instead would make one saturation setting fight another
		// wherever roles are shared.
		Color = MixtormatCompositing::Saturate(Color, Ref.Saturation);

		return Color;
	}

	float EvaluateFalloff(const FMixtormatFalloff& Falloff, const float T)
	{
		// The prototype pins the exponent just above zero rather than rejecting it: at t = 0 a
		// zero exponent is undefined, and the browser-side answer is to clamp.
		const float Exponent = FMath::Max(Falloff.Power, 0.01f);
		const float ClampedT = FMath::Clamp(T, 0.0f, 1.0f);
		return Falloff.Start + (Falloff.End - Falloff.Start) * FMath::Pow(ClampedT, Exponent);
	}

	void BuildFalloffRamp(const FMixtormatFalloff& Falloff, const EMixtormatAxis Axis, const FLinearColor& Source,
		const float Strength, FMixtormatResolvedRamp& OutRamp)
	{
		const int32 Samples = FMath::Max(Falloff.Samples, 2);

		OutRamp.Axis = Axis;
		OutRamp.Colors.Reset(Samples);
		OutRamp.Colors.Reserve(Samples);

		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float T = Samples > 1 ? static_cast<float>(Index) / static_cast<float>(Samples - 1) : 0.0f;
			FLinearColor Stop = Source;
			Stop.A *= EvaluateFalloff(Falloff, T) * Strength;
			OutRamp.Colors.Add(Stop);
		}
	}

	void ResolveTheme(const FMixtormatTheme& Theme, FMixtormatResolvedStyle& OutStyle)
	{
		// ---- Palette -------------------------------------------------------------------
		OutStyle.Palette.Set(EMixtormatColorRole::Ground, Theme.Palette.Ground);
		OutStyle.Palette.Set(EMixtormatColorRole::Shell, Theme.Palette.Shell);
		OutStyle.Palette.Set(EMixtormatColorRole::Panel, Theme.Palette.Panel);
		OutStyle.Palette.Set(EMixtormatColorRole::Text, Theme.Palette.Text);
		OutStyle.Palette.Set(EMixtormatColorRole::TextMuted, Theme.Palette.TextMuted);
		OutStyle.Palette.Set(EMixtormatColorRole::Accent, Theme.Palette.Accent);
		OutStyle.Palette.Set(EMixtormatColorRole::Modified, Theme.Palette.Modified);
		OutStyle.Palette.Set(EMixtormatColorRole::Warning, Theme.Palette.Warning);
		OutStyle.Palette.Set(EMixtormatColorRole::Error, Theme.Palette.Error);
		OutStyle.Palette.Set(EMixtormatColorRole::Shade, Theme.Palette.Shade);
		OutStyle.Palette.Set(EMixtormatColorRole::Hairline, Theme.Palette.Hairline);
		OutStyle.Palette.Set(EMixtormatColorRole::MenuGround, Theme.Palette.MenuGround);
		OutStyle.Palette.Set(EMixtormatColorRole::ThumbnailGround, Theme.Palette.ThumbnailGround);
		OutStyle.Palette.Set(EMixtormatColorRole::OverlayGround, Theme.Palette.OverlayGround);

		const FMixtormatResolvedPalette& P = OutStyle.Palette;

		// ---- Well ---------------------------------------------------------------------
		// Ground under a black multiply falloff. The two endpoint opacities scale the recipe's
		// authored top/bottom shade, so the ramp and the border below cannot disagree about how
		// dark the top edge is.
		{
			FMixtormatResolvedWellStyle& Well = OutStyle.Well;
			Well.Base = P.Get(EMixtormatColorRole::Ground);
			Well.Radius = Theme.Well.Radius;

			FMixtormatFalloff ShadeFalloff;
			ShadeFalloff.Start = Theme.Well.ShadeTop;
			ShadeFalloff.End = Theme.Well.ShadeBottom;
			ShadeFalloff.Power = 1.0f;
			ShadeFalloff.Samples = 2;
			BuildFalloffRamp(ShadeFalloff, EMixtormatAxis::Vertical,
				P.Get(EMixtormatColorRole::Shade), 1.0f, Well.Shade);

			// The border is its own colour at its own opacity, saturated. The per-edge
			// falloff is folded into a ramp here rather than left for the painter, because a single
			// flat colour cannot carry a vertical fade and the endpoints would otherwise become
			// controls nothing reads.
			auto BorderRamp = [&P, &Theme](const float HoverOpacity, const float TopOpacity,
				const float BottomOpacity, FMixtormatResolvedRamp& Out)
			{
				FMixtormatColorRef Ref = MakeColorRef(EMixtormatColorRole::Hairline);
				Ref.Opacity = HoverOpacity;
				Ref.Saturation = Theme.Well.BorderSaturation;

				FLinearColor Top = ResolveColor(P, Ref);
				FLinearColor Bottom = Top;
				// The endpoint opacities are relative to the state's overall intensity, not
				// absolute -- that is what --well-border-opacity is for.
				Top.A *= TopOpacity;
				Bottom.A *= BottomOpacity;

				Out.Axis = EMixtormatAxis::Vertical;
				Out.Colors.Reset(2);
				Out.Colors.Reserve(2);
				Out.Colors.Add(Top);
				Out.Colors.Add(Bottom);
			};

			BorderRamp(Theme.Well.BorderOpacity, Theme.Well.BorderTopOpacity,
				Theme.Well.BorderBottomOpacity, Well.Border);
			BorderRamp(Theme.Well.BorderHoverOpacity, Theme.Well.BorderHoverTopOpacity,
				Theme.Well.BorderHoverBottomOpacity, Well.BorderHover);

			Well.BorderWidth = Theme.Well.BorderWidth;
		}

		// ---- Fill ---------------------------------------------------------------------
		// The accent body added over the well, then a black horizontal shade with a midpoint.
		{
			FMixtormatResolvedFillStyle& Fill = OutStyle.Fill;
			const FLinearColor Accent = P.Get(EMixtormatColorRole::Accent);

			FMixtormatFalloff RestFalloff;
			RestFalloff.Start = Theme.Fill.Top;
			RestFalloff.End = Theme.Fill.Bottom;
			RestFalloff.Power = Theme.Fill.FalloffPower;
			RestFalloff.Samples = 6;
			BuildFalloffRamp(RestFalloff, EMixtormatAxis::Vertical, Accent, Theme.Fill.Saturation, Fill.Body);

			FMixtormatFalloff HoverFalloff = RestFalloff;
			HoverFalloff.Start = Theme.Fill.HoverTop;
			HoverFalloff.End = Theme.Fill.HoverBottom;
			BuildFalloffRamp(HoverFalloff, EMixtormatAxis::Vertical, Accent, Theme.Fill.HoverSaturation, Fill.BodyHover);

			FMixtormatFalloff ActiveFalloff = RestFalloff;
			ActiveFalloff.Start = Theme.Fill.ActiveTop;
			ActiveFalloff.End = Theme.Fill.ActiveBottom;
			BuildFalloffRamp(ActiveFalloff, EMixtormatAxis::Vertical, Accent, Theme.Fill.ActiveSaturation, Fill.BodyActive);

			// The shade pass is black at three weights along a horizontal axis. Mid is the eased
			// low point; keeping it as its own stop is what stops the fill reading as lit from
			// one edge. Built inline rather than through the falloff sampler because start/mid/end
			// is a three-point curve, not a falloff, and routing it through one would have meant
			// a second sampling path for a single gradient.
			const FLinearColor Shade = P.Get(EMixtormatColorRole::Shade);
			FLinearColor ShadeStart = Shade;
			FLinearColor ShadeMid = Shade;
			FLinearColor ShadeEnd = Shade;
			ShadeStart.A *= Theme.Fill.ShadeStart;
			ShadeMid.A *= Theme.Fill.ShadeMid;
			ShadeEnd.A *= Theme.Fill.ShadeEnd;

			Fill.Shade.Axis = EMixtormatAxis::Horizontal;
			Fill.Shade.Colors.Reset(3);
			Fill.Shade.Colors.Reserve(3);
			Fill.Shade.Colors.Add(ShadeStart);
			Fill.Shade.Colors.Add(ShadeMid);
			Fill.Shade.Colors.Add(ShadeEnd);
			Fill.ShadeMidPosition = Theme.Fill.ShadeMidPosition;

			Fill.FalloffPower = Theme.Fill.FalloffPower;
		}

		// ---- Toggle -------------------------------------------------------------------
		// Deliberately no surface of its own: a toggle is the well recipe plus the fill recipe,
		// which is the first proof that the two compose without either knowing about the other.
		{
			FMixtormatResolvedToggleStyle& Toggle = OutStyle.Toggles;
			Toggle.Well = OutStyle.Well;
			Toggle.Fill = OutStyle.Fill;
			Toggle.Size = Theme.Toggle.Size;
			Toggle.FillInset = Theme.Toggle.FillInset;
			Toggle.DisabledShadeTop = Theme.Toggle.DisabledShadeTop;
			Toggle.DisabledShadeBottom = Theme.Toggle.DisabledShadeBottom;
		}

		// ---- Foldout ------------------------------------------------------------------
		// Ground, an additive lift that decays to nothing at the body seam, a Soft Light accent
		// over the same domain, then a saturated hairline.
		{
			FMixtormatResolvedFoldoutStyle& Foldout = OutStyle.Foldouts;
			Foldout.Base = P.Get(EMixtormatColorRole::Ground);

			// The lift is the foldout's own tint, added over Ground and decayed to nothing at the body
			// seam, so the header dissolves into the body it opens rather than stopping at a hard
			// edge. Its source and its weight are both independently authored (--header-tint-rgb at
			// --header-tint-opacity), which is why they arrive as a colour and a separate number
			// rather than as one premultiplied value.
			FMixtormatResolvedRamp BaseLift;
			{
				FMixtormatFalloff Falloff = Theme.Foldout.LiftFalloff;
				BaseLift.Axis = EMixtormatAxis::Vertical;
				BaseLift.Colors.Reset(FMath::Max(Falloff.Samples, 2));
				BaseLift.Colors.Reserve(FMath::Max(Falloff.Samples, 2));

				for (int32 Index = 0; Index < FMath::Max(Falloff.Samples, 2); ++Index)
				{
					const float T = static_cast<float>(Index) / static_cast<float>(FMath::Max(Falloff.Samples, 2) - 1);
					FLinearColor Stop = Theme.Foldout.LiftTint;
					Stop.A *= EvaluateFalloff(Falloff, T) * Theme.Foldout.LiftOpacity;
					BaseLift.Colors.Add(Stop);
				}
			}
			// Saturating a gradient means saturating each of its stops: CSS applies the filter to
			// the composited layer, and the stops are what that layer is made of.
			SaturateRamp(BaseLift, Theme.Foldout.LiftSaturation);
			Foldout.Lift = BaseLift;

			// Hover lifts with the accent-tinted hover colour rather than a brighter grey: the
			// hovered header is the same lip catching the same light the controls catch.
			{
				FMixtormatFalloff Falloff = Theme.Foldout.LiftFalloff;
				const int32 Samples = FMath::Max(Falloff.Samples, 2);
				Foldout.LiftHover.Axis = EMixtormatAxis::Vertical;
				Foldout.LiftHover.Colors.Reset(Samples);
				Foldout.LiftHover.Colors.Reserve(Samples);

				for (int32 Index = 0; Index < Samples; ++Index)
				{
					const float T = static_cast<float>(Index) / static_cast<float>(Samples - 1);
					FLinearColor Stop = Theme.Foldout.HoverTint;
					Stop.A *= EvaluateFalloff(Falloff, T) * Theme.Foldout.HoverTintOpacity;
					Foldout.LiftHover.Colors.Add(Stop);
				}
				SaturateRamp(Foldout.LiftHover, Theme.Foldout.HoverSaturation);
			}

			// The accent pass darkens and saturates the header rather than adding light, which is
			// why it is Soft Light and not Additive: the header should read as tinted, not lit.
			const FLinearColor Accent = P.Get(EMixtormatColorRole::Accent);
			FMixtormatFalloff AccentFalloff = Theme.Foldout.LiftFalloff;
			AccentFalloff.Start = Theme.Foldout.AccentOpacity;
			AccentFalloff.End = 0.0f;
			BuildFalloffRamp(AccentFalloff, EMixtormatAxis::Vertical, Accent, 1.0f, Foldout.Accent);

			FMixtormatFalloff AccentHoverFalloff = AccentFalloff;
			AccentHoverFalloff.Start = Theme.Foldout.AccentHoverOpacity;
			BuildFalloffRamp(AccentHoverFalloff, EMixtormatAxis::Vertical, Accent, 1.0f, Foldout.AccentHover);

			FMixtormatColorRef HairlineRef = MakeColorRef(EMixtormatColorRole::Hairline);
			HairlineRef.Saturation = Theme.Foldout.HairlineSaturation;
			HairlineRef.Opacity = Theme.Foldout.HairlineOpacity;
			Foldout.Hairline = ResolveColor(P, HairlineRef);
			HairlineRef.Saturation = Theme.Foldout.HairlineHoverSaturation;
			HairlineRef.Opacity = Theme.Foldout.HairlineHoverOpacity;
			Foldout.HairlineHover = ResolveColor(P, HairlineRef);

			Foldout.HairlineWidth = 1.0f;
		}

		// ---- Card ---------------------------------------------------------------------
		{
			FMixtormatResolvedCardStyle& Card = OutStyle.Cards;
			Card.Base = P.Get(EMixtormatColorRole::Ground);
			Card.HeaderHeight = Theme.CardLayout.HeaderHeight;
			Card.Reach = Theme.Card.Reach;
			Card.FalloffPower = Theme.Card.FalloffPower;
			Card.Radius = Theme.Card.Radius;

			// Header and body in one ramp: the body weight is held from the end of the header's
			// decay onward, so the seam is a continuation rather than a join.
			const FLinearColor Accent = P.Get(EMixtormatColorRole::Accent);
			Card.Gradient.Axis = EMixtormatAxis::Vertical;
			Card.Gradient.Colors.Reset(6);
			Card.Gradient.Colors.Reserve(6);
			const float Power = FMath::Max(Theme.Card.FalloffPower, 0.01f);
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const float T = static_cast<float>(Index) / 5.0f;
				// The header's own decay, sampled over its height, then held at the body weight.
				const float HeaderT = T;
				const float Decay = Theme.Card.HeaderOpacity * FMath::Pow(1.0f - HeaderT, Power);
				const float Weight = FMath::Max(Decay, Theme.Card.BodyOpacity);
				FLinearColor Stop = Accent;
				Stop.A *= Weight;
				Stop = MixtormatCompositing::Saturate(Stop, Theme.Card.BodySaturation);
				Card.Gradient.Colors.Add(Stop);
			}
		}

		// ---- Layer ---------------------------------------------------------------------
		{
			FMixtormatResolvedLayerStyle& Layer = OutStyle.Layers;
			Layer.Base = P.Get(EMixtormatColorRole::Ground);

			const FLinearColor Panel = P.Get(EMixtormatColorRole::Panel);

			auto RowRamp = [&Panel](const float Saturation, const float Strength, FMixtormatResolvedRamp& Out)
			{
				FMixtormatFalloff Falloff;
				Falloff.Start = Strength;
				Falloff.End = Strength;
				Falloff.Power = 1.0f;
				Falloff.Samples = 2;
				BuildFalloffRamp(Falloff, EMixtormatAxis::Vertical, Panel, Saturation, Out);
			};

			RowRamp(Theme.Layer.RestSaturation, Theme.Layer.RestStrength, Layer.Row);
			RowRamp(Theme.Layer.HoverSaturation, Theme.Layer.HoverStrength, Layer.RowHover);
			RowRamp(Theme.Layer.SelectedSaturation, Theme.Layer.SelectedStrength, Layer.RowSelected);

			// Child rows ramp left to right instead, which is what makes a child read as a child
			// without needing its own colour.
			auto ChildRamp = [&Panel](const float Saturation, const float Strength, FMixtormatResolvedRamp& Out)
			{
				FMixtormatFalloff Falloff;
				Falloff.Start = Strength;
				Falloff.End = Strength;
				Falloff.Power = 1.0f;
				Falloff.Samples = 2;
				BuildFalloffRamp(Falloff, EMixtormatAxis::Horizontal, Panel, Saturation, Out);
			};

			ChildRamp(Theme.Layer.ChildSaturation, Theme.Layer.ChildStrength, Layer.Child);
			ChildRamp(Theme.Layer.ChildHoverSaturation, Theme.Layer.ChildHoverStrength, Layer.ChildHover);
			ChildRamp(Theme.Layer.ChildSelectedSaturation, Theme.Layer.ChildSelectedStrength, Layer.ChildSelected);

			// The group cross pass: a mid-grey lift on the left edge. A group is the only row
			// painted on two axes, and the second one is what says so.
			FLinearColor Cross = Panel;
			Cross.A *= Theme.Layer.GroupStrength;
			Layer.GroupCross = MixtormatCompositing::Saturate(Cross, Theme.Layer.GroupSaturation);
			Layer.GroupCrossStrength = Theme.Layer.GroupStrength;

			FMixtormatColorRef GlowRef = Theme.Layer.ActiveGlow.Source;
			GlowRef.Opacity = Theme.Layer.ActiveGlow.Opacity;
			GlowRef.Saturation = Theme.Layer.ActiveGlow.Saturation;
			Layer.Glow = ResolveColor(P, GlowRef);
			Layer.GlowOpacity = Theme.Layer.ActiveGlow.Opacity;
			Layer.GlowReach = Theme.Layer.ActiveGlow.Reach;
			Layer.GlowSaturation = Theme.Layer.ActiveGlow.Saturation;

			FMixtormatColorRef HairlineRef = MakeColorRef(EMixtormatColorRole::Hairline);
			HairlineRef.Opacity = Theme.Layer.ActiveHairlineOpacity;
			Layer.GlowHairline = ResolveColor(P, HairlineRef);
			Layer.GlowHairlineWidth = Theme.Layer.ActiveHairlineWidth;

			// Resolved here, painted by the hierarchy painter -- never as part of the row surface,
			// or it would be saturated and multiplied by the row gradient along with everything else.
			FMixtormatColorRef RailRef = Theme.LayerHierarchy.Source;
			RailRef.Opacity = Theme.LayerHierarchy.Opacity;
			Layer.HierarchyRail = ResolveColor(P, RailRef);
			Layer.HierarchyWidth = Theme.LayerHierarchy.Width;
			Layer.HierarchyOpacity = Theme.LayerHierarchy.Opacity;
		}

		// ---- Button --------------------------------------------------------------------
		// Body and hairline resolve independently, which is the whole point of having two blend
		// modes here rather than one.
		{
			FMixtormatResolvedButtonStyle& Button = OutStyle.Buttons;
			const FLinearColor Accent = P.Get(EMixtormatColorRole::Accent);
			Button.Base = P.Get(EMixtormatColorRole::Ground);
			Button.Height = Theme.Button.Height;
			Button.HairlineWidth = Theme.Button.HairlineWidth;

			auto BodyRamp = [&Accent, &Theme](const float Top, const float Bottom, FMixtormatResolvedRamp& Out)
			{
				FMixtormatFalloff Falloff;
				Falloff.Start = Top;
				Falloff.End = Bottom;
				Falloff.Power = 1.0f;
				Falloff.Samples = 2;
				BuildFalloffRamp(Falloff, EMixtormatAxis::Vertical, Accent, Theme.Button.GradientSaturation, Out);
			};

			BodyRamp(Theme.Button.RestTop, Theme.Button.RestBottom, Button.Body);
			BodyRamp(Theme.Button.HoverTop, Theme.Button.HoverBottom, Button.BodyHover);
			BodyRamp(Theme.Button.SelectedTop, Theme.Button.SelectedBottom, Button.BodySelected);

			FMixtormatColorRef HairlineRef = MakeColorRef(EMixtormatColorRole::Accent);
			HairlineRef.Saturation = Theme.Button.HairlineSaturation;
			HairlineRef.Opacity = Theme.Button.HairlineOpacity;
			Button.Hairline = ResolveColor(P, HairlineRef);
			HairlineRef.Opacity = Theme.Button.HairlineHoverOpacity;
			Button.HairlineHover = ResolveColor(P, HairlineRef);
			HairlineRef.Opacity = Theme.Button.HairlineSelectedOpacity;
			Button.HairlineSelected = ResolveColor(P, HairlineRef);

			FMixtormatColorRef SeparatorRef = MakeColorRef(EMixtormatColorRole::Text);
			SeparatorRef.Opacity = Theme.Button.SeparatorOpacity;
			Button.Separator = ResolveColor(P, SeparatorRef);
			Button.SeparatorWidth = Theme.Button.SeparatorWidth;
			Button.SeparatorHeight = Theme.Button.SeparatorHeight;
		}

		// ---- Menu ----------------------------------------------------------------------
		{
			FMixtormatResolvedMenuStyle& Menu = OutStyle.Menus;
			Menu.Ground = P.Get(EMixtormatColorRole::MenuGround);

			// The lip is the only tinted part of the ground, and it is the accent rather than a
			// second grey -- a menu edge that catches the same light the controls do.
			FMixtormatColorRef LipRef = MakeColorRef(EMixtormatColorRole::Accent);
			LipRef.Opacity = Theme.Menu.LipTintOpacity;
			Menu.Lip = ResolveColor(P, LipRef);

			FMixtormatColorRef BorderRef = MakeColorRef(EMixtormatColorRole::Hairline);
			BorderRef.Opacity = Theme.Menu.BorderOpacity;
			Menu.Border = ResolveColor(P, BorderRef);

			Menu.ItemHover = P.Get(EMixtormatColorRole::Panel);
			Menu.ItemChecked = P.Get(EMixtormatColorRole::Accent);

			FMixtormatColorRef DisabledRef = MakeColorRef(EMixtormatColorRole::Text);
			DisabledRef.Opacity = Theme.Menu.ItemDisabledOpacity;
			Menu.ItemDisabled = ResolveColor(P, DisabledRef);

			Menu.Width = Theme.Menu.Width;
			Menu.ItemHeight = Theme.Menu.ItemHeight;
			Menu.CornerRadius = Theme.Menu.CornerRadius;
		}

		// ---- Gallery -------------------------------------------------------------------
		{
			FMixtormatResolvedGalleryStyle& Gallery = OutStyle.Gallery;
			Gallery.Base = P.Get(EMixtormatColorRole::ThumbnailGround);

			FMixtormatColorRef BorderRef = MakeColorRef(EMixtormatColorRole::Hairline);
			BorderRef.Opacity = Theme.Gallery.BorderOpacity;
			Gallery.Border = ResolveColor(P, BorderRef);

			FMixtormatColorRef LiftRef = MakeColorRef(EMixtormatColorRole::Accent);
			LiftRef.Opacity = Theme.Gallery.HoverLiftOpacity;
			Gallery.HoverLift = ResolveColor(P, LiftRef);

			FMixtormatColorRef SelectedRef = MakeColorRef(EMixtormatColorRole::Accent);
			SelectedRef.Opacity = Theme.Gallery.SelectedEdgeOpacity;
			Gallery.SelectedEdge = ResolveColor(P, SelectedRef);

			Gallery.CaptionGround = P.Get(EMixtormatColorRole::Shade);
			Gallery.BorderWidth = Theme.Gallery.BorderWidth;
			Gallery.HoverLiftOpacity = Theme.Gallery.HoverLiftOpacity;
			Gallery.TileSize = Theme.Gallery.TileSize;
			Gallery.CornerRadius = Theme.Gallery.CornerRadius;
		}

		// ---- Preview -------------------------------------------------------------------
		{
			FMixtormatResolvedPreviewStyle& Preview = OutStyle.Preview;
			FMixtormatColorRef PlateRef = MakeColorRef(EMixtormatColorRole::OverlayGround);
			Preview.OverlayPlate = ResolveColor(P, PlateRef);
			Preview.OverlayPlateOpacity = Theme.Preview.OverlayPlateOpacity;
			Preview.HoverAccent = Theme.Preview.OverlayHoverAccent;
			Preview.PressAccent = Theme.Preview.OverlayPressAccent;
			Preview.ToolbarIconSize = Theme.PreviewLayout.IconSize;
		}

		// ---- Shell ---------------------------------------------------------------------
		{
			FMixtormatResolvedShellStyle& Shell = OutStyle.Shell;
			Shell.Ground = P.Get(EMixtormatColorRole::Shell);

			FMixtormatColorRef SepRef = MakeColorRef(EMixtormatColorRole::Hairline);
			SepRef.Opacity = Theme.Shell.SplitterOpacity;
			Shell.Separator = ResolveColor(P, SepRef);

			Shell.SeparatorWidth = Theme.Shell.SplitterVisualWidth;
			Shell.SeparatorHitWidth = Theme.Shell.SplitterHitWidth;
			Shell.SeparatorOpacity = Theme.Shell.SplitterOpacity;
			Shell.SeparatorHoverOpacity = Theme.Shell.SplitterHoverOpacity;
		}

		// ---- Icons / Typography / Geometry ----------------------------------------------
		// Copied, not derived: these are already in their final form, and giving a metric or a
		// text spec a second representation is just a place for the two to disagree.
		OutStyle.Icons = Theme.Icons;
		OutStyle.Typography.Family = Theme.Typography.Family;
		for (int32 Index = 0; Index < FMixtormatResolvedTypography::RoleCount; ++Index)
		{
			OutStyle.Typography.Roles[Index] = Theme.Typography.Roles[Index];
		}

		OutStyle.ControlLayout = Theme.ControlLayout;
		OutStyle.FoldoutLayout = Theme.FoldoutLayout;
		OutStyle.CardLayout = Theme.CardLayout;
		OutStyle.LayerLayout = Theme.LayerLayout;
		OutStyle.MenuLayout = Theme.MenuLayout;
		OutStyle.PreviewLayout = Theme.PreviewLayout;
		OutStyle.GalleryLayout = Theme.GalleryLayout;
		OutStyle.ShellLayout = Theme.Shell;
	}
}