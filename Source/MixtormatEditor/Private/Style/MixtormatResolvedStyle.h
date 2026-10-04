// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatTheme.h"

// Editable data is resolved once, here, and painters read only this.
//
// The reason is performance as much as clarity. A layer row is painted for every visible row on
// every frame, and resolving it from the editable theme would mean a colour lookup per row per
// frame. By the time a painter sees a ramp it is already an array of resolved colours with the
// alpha already authored along it -- no role lookup, no string, no JSON, no allocation.
//
// Nothing in this file is editable and nothing in it is a fallback. It is a projection of
// FMixtormatTheme, rebuilt wholesale on every commit.

namespace Mixtormat
{
	// The palette after resolution. Indexable by role so ResolveColor is an array read rather than
	// a string comparison -- the old ResolveColor(TEXT("...")) in the paint path is exactly what
	// this replaces.
	struct FMixtormatResolvedPalette
	{
		static constexpr int32 RoleCount = 14;

		FLinearColor Roles[RoleCount];

		FLinearColor Get(const EMixtormatColorRole Role) const
		{
			const uint8 Index = static_cast<uint8>(Role);
			return Index < RoleCount ? Roles[Index] : Roles[0];
		}

		void Set(const EMixtormatColorRole Role, const FLinearColor& Color)
		{
			const uint8 Index = static_cast<uint8>(Role);
			if (Index < RoleCount)
			{
				Roles[Index] = Color;
			}
		}
	};

	// A ramp with its colours already resolved. The alpha along the ramp carries the layer's
	// authored falloff, so a painter interpolates and draws -- it never computes an opacity.
	//
	// Fewer than two entries is a flat fill. Painters must treat that as a solid rather than
	// dividing by a zero-length span.
	struct FMixtormatResolvedRamp
	{
		EMixtormatAxis Axis = EMixtormatAxis::None;
		TArray<FLinearColor, TInlineAllocator<8>> Colors;
	};

	// The one resolver. Every colour in the UI comes from here, which is what makes "retint the
	// palette" a single edit rather than a search.
	//
	// Order matters and is fixed: resolve the role, apply the multiplier, then opacity, then
	// saturation. Saturation is applied last because the prototype specifies it as a paint-time
	// filter on the composited source, not as a property of the stored swatch.
	FLinearColor ResolveColor(const FMixtormatResolvedPalette& Palette, const FMixtormatColorRef& Ref);

	// The shared power curve. Ported once from the prototype's falloff.js and used by every
	// component that fades -- foldouts, cards, fills and glow -- so the four cannot drift apart.
	float EvaluateFalloff(const FMixtormatFalloff& Falloff, const float T);

	// Sample a falloff into a resolved ramp. `Source` supplies the colour and its alpha; the
	// curve supplies the weight at each stop. `Strength` scales the whole layer, which is how an
	// endpoint is authored separately from the layer's overall contribution.
	//
	// Samples is clamped to two: one stop is a flat fill, not a curve.
	void BuildFalloffRamp(const FMixtormatFalloff& Falloff, EMixtormatAxis Axis, const FLinearColor& Source,
		float Strength, FMixtormatResolvedRamp& OutRamp);

	// ---- Resolved component styles ----------------------------------------------------------

	struct FMixtormatResolvedWellStyle
	{
		FLinearColor Base;
		// Black multiplied down from ShadeTop at the top edge to ShadeBottom at the bottom.
		FMixtormatResolvedRamp Shade;

		// The outline is a ramp, not a colour: --well-border-top-opacity 0.33 against
		// --well-border-bottom-opacity 0.11 is a vertical fade, and collapsing it to one value
		// would have left both endpoints as controls nothing read.
		FMixtormatResolvedRamp Border;
		float BorderWidth = 1.0f;

		// Hover is a stronger border, not a second surface.
		FMixtormatResolvedRamp BorderHover;

		float Radius = 0.0f;
	};

	struct FMixtormatResolvedFillStyle
	{
		// The accent body, added over the well. One ramp per state; the painter picks.
		FMixtormatResolvedRamp Body;
		FMixtormatResolvedRamp BodyHover;
		FMixtormatResolvedRamp BodyActive;

		// A separate horizontal multiply pass with a movable midpoint, so the fill reads as eased
		// rather than lit from one edge. Painted after the body, over it.
		FMixtormatResolvedRamp Shade;
		// Where the shade's low point sits along its axis. Stored next to the ramp it positions
		// rather than inside it, because only this one gradient has a midpoint.
		float ShadeMidPosition = 0.63f;

		float FalloffPower = 1.0f;
	};

	struct FMixtormatResolvedToggleStyle
	{
		float Size = 16.0f;
		float FillInset = 3.0f;

		FMixtormatResolvedWellStyle Well;
		FMixtormatResolvedFillStyle Fill;

		// A disabled toggle does not get its own surface: the well shade deepens by these amounts.
		float DisabledShadeTop = 0.0f;
		float DisabledShadeBottom = 0.0f;
	};

	struct FMixtormatResolvedFoldoutStyle
	{
		FLinearColor Base;
		FMixtormatResolvedRamp Lift;
		FMixtormatResolvedRamp LiftHover;
		FMixtormatResolvedRamp Accent;
		FMixtormatResolvedRamp AccentHover;

		FLinearColor Hairline;
		FLinearColor HairlineHover;
		float HairlineWidth = 1.0f;
	};

	struct FMixtormatResolvedCardStyle
	{
		FLinearColor Base;
		// Header and body are one continuous ramp: the seam is simply where the header's
		// contribution has decayed to the body's, not a second surface stacked on the first.
		FMixtormatResolvedRamp Gradient;

		float HeaderHeight = 16.0f;
		float Reach = 0.0f;
		float FalloffPower = 1.0f;
		float Radius = 0.0f;
	};

	struct FMixtormatResolvedLayerStyle
	{
		FLinearColor Base;

		// Rows ramp top to bottom; child rows ramp left to right; a group row does both, the
		// second pass being the Soft Light cross.
		FMixtormatResolvedRamp Row;
		FMixtormatResolvedRamp RowHover;
		FMixtormatResolvedRamp RowSelected;

		FMixtormatResolvedRamp Child;
		FMixtormatResolvedRamp ChildHover;
		FMixtormatResolvedRamp ChildSelected;

		FLinearColor GroupCross;
		float GroupCrossStrength = 0.0f;

		// The selected row's halo, and its crisp top edge. Painted after the body and before the
		// hierarchy rails, which must sit above it.
		FLinearColor Glow;
		float GlowOpacity = 0.0f;
		float GlowReach = 0.0f;
		float GlowSaturation = 1.0f;
		FLinearColor GlowHairline;
		float GlowHairlineWidth = 0.0f;

		// The rails' colour is resolved here but painted by the hierarchy painter, never as part
		// of the row surface.
		FLinearColor HierarchyRail;
		float HierarchyWidth = 1.0f;
		float HierarchyOpacity = 0.24f;
	};

	struct FMixtormatResolvedButtonStyle
	{
		FLinearColor Base;

		FMixtormatResolvedRamp Body;
		FMixtormatResolvedRamp BodyHover;
		FMixtormatResolvedRamp BodySelected;

		// Independent of the body blend on purpose -- see FMixtormatButtonTheme.
		FLinearColor Hairline;
		FLinearColor HairlineHover;
		FLinearColor HairlineSelected;
		float HairlineWidth = 1.0f;

		FLinearColor Separator;
		float SeparatorWidth = 1.0f;
		float SeparatorHeight = 14.0f;

		float Height = 24.0f;
	};

	struct FMixtormatResolvedMenuStyle
	{
		FLinearColor Ground;
		FLinearColor Lip;
		FLinearColor Border;
		FLinearColor ItemHover;
		FLinearColor ItemChecked;
		FLinearColor ItemDisabled;

		float CornerRadius = 0.0f;
	};

	struct FMixtormatResolvedGalleryStyle
	{
		FLinearColor Base;
		FLinearColor Border;
		FLinearColor HoverLift;
		FLinearColor SelectedEdge;
		FLinearColor CaptionGround;

		float BorderWidth = 1.0f;
		float HoverLiftOpacity = 0.0f;
		float CornerRadius = 0.0f;
	};

	struct FMixtormatResolvedPreviewStyle
	{
		FLinearColor OverlayPlate;
		FLinearColor OverlayGround;
		float OverlayPlateOpacity = 0.85f;
		float HoverAccent = 0.18f;
		float PressAccent = 0.35f;
		float IconOpacity = 0.6f;
		float IconRestOpacity = 0.45f;
		float GripOpacity = 0.45f;
	};

	struct FMixtormatResolvedShellStyle
	{
		FLinearColor Ground;
		FLinearColor Separator;
		FLinearColor SeparatorHover;
		float SeparatorOpacity = 0.46f;
		float SeparatorHoverOpacity = 0.85f;
	};

	struct FMixtormatResolvedTypography
	{
		static constexpr int32 RoleCount = static_cast<int32>(EMixtormatTextRole::Count);

		// Copied, not derived: the text spec is already the painter's input, and re-deriving it
		// would be a second place to change what a role means.
		FMixtormatTextSpec Roles[RoleCount];
		EMixtormatFontFamily Family = EMixtormatFontFamily::NativeDefault;
	};

	// The complete resolved style. Rebuilt wholesale; never patched in place, so a theme edit
	// cannot leave a stale colour behind in a surface nobody re-asked about.
	struct FMixtormatResolvedStyle
	{
		FMixtormatResolvedPalette Palette;

		FMixtormatResolvedWellStyle Well;
		FMixtormatResolvedFillStyle Fill;
		FMixtormatResolvedToggleStyle Toggles;

		FMixtormatResolvedFoldoutStyle Foldouts;
		FMixtormatResolvedCardStyle Cards;
		FMixtormatResolvedLayerStyle Layers;
		FMixtormatResolvedButtonStyle Buttons;
		FMixtormatResolvedMenuStyle Menus;
		FMixtormatResolvedGalleryStyle Gallery;
		FMixtormatResolvedPreviewStyle Preview;
		FMixtormatResolvedShellStyle Shell;

		FMixtormatIconTheme Icons;
		FMixtormatResolvedTypography Typography;

		// Numeric geometry is copied straight through. A metric is already a resolved value; giving
		// it a second representation would only create a place for the two to disagree.
		FMixtormatControlMetrics ControlLayout;
		FMixtormatFoldoutMetrics FoldoutLayout;
		FMixtormatCardMetrics CardLayout;
		FMixtormatLayerMetrics LayerLayout;
		FMixtormatMenuMetrics MenuLayout;
		FMixtormatPreviewMetrics PreviewLayout;
		FMixtormatGalleryMetrics GalleryLayout;
		FMixtormatShellMetrics ShellLayout;
	};

	// Project an editable theme into resolved style. Validate first: resolution assumes legal
	// input, and a clamp that happened after resolution would leave a colour that no longer
	// matches the number the author typed.
	void ResolveTheme(const FMixtormatTheme& Theme, FMixtormatResolvedStyle& OutStyle);
}