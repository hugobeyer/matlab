// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatTheme.h"

namespace Mixtormat
{
	namespace
	{
		// tokens.css writes colours as `R G B` sRGB channels. Every default below goes through
		// here rather than through a hex literal so the provenance stays readable against the
		// prototype file, which is the visual specification.
		FLinearColor SRGB(const uint8 R, const uint8 G, const uint8 B, const float Alpha = 1.0f)
		{
			FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B));
			Color.A = Alpha;
			return Color;
		}

		FMixtormatTextSpec Text(const float Size, const EMixtormatFontWeight Weight,
			const float Opacity = 1.0f, const float TrackingPx = 0.0f)
		{
			FMixtormatTextSpec Spec;
			Spec.Size = Size;
			Spec.Weight = Weight;
			Spec.Opacity = Opacity;
			Spec.TrackingPx = TrackingPx;
			return Spec;
		}

		// An icon role where the button and hit sizes are simply the glyph plus a fixed inset.
		// Declared once so the nine roles cannot drift in *how* they pad, only in how much.
		FMixtormatIconStyle Icon(const float GlyphSize, const float RestOpacity,
			const float Padding = 2.5f, const float HitPadding = 2.5f)
		{
			FMixtormatIconStyle Style;
			Style.GlyphSize = GlyphSize;
			Style.ButtonSize = GlyphSize + Padding * 2.0f;
			Style.HitSize = GlyphSize + HitPadding * 2.0f;
			Style.RestOpacity = RestOpacity;
			Style.HoverOpacity = 1.0f;
			Style.DisabledOpacity = 0.32f;
			return Style;
		}

		FMixtormatFalloff Falloff(const float Start, const float End, const float Power, const int32 Samples = 6)
		{
			FMixtormatFalloff Result;
			Result.Start = Start;
			Result.End = End;
			Result.Power = Power;
			Result.Samples = Samples;
			return Result;
		}
	}

	FMixtormatColorRef MakeColorRef(const EMixtormatColorRole Role)
	{
		FMixtormatColorRef Ref;
		Ref.Role = Role;
		return Ref;
	}

	const TCHAR* LexToString(const EMixtormatColorRole Role)
	{
		switch (Role)
		{
		case EMixtormatColorRole::Ground: return TEXT("Ground");
		case EMixtormatColorRole::Shell: return TEXT("Shell");
		case EMixtormatColorRole::Panel: return TEXT("Panel");
		case EMixtormatColorRole::Text: return TEXT("Text");
		case EMixtormatColorRole::TextMuted: return TEXT("Text Muted");
		case EMixtormatColorRole::Accent: return TEXT("Accent");
		case EMixtormatColorRole::Modified: return TEXT("Modified");
		case EMixtormatColorRole::Warning: return TEXT("Warning");
		case EMixtormatColorRole::Error: return TEXT("Error");
		case EMixtormatColorRole::Shade: return TEXT("Shade");
		case EMixtormatColorRole::Hairline: return TEXT("Hairline");
		case EMixtormatColorRole::MenuGround: return TEXT("Menu Ground");
		case EMixtormatColorRole::ThumbnailGround: return TEXT("Thumbnail Ground");
		case EMixtormatColorRole::OverlayGround: return TEXT("Overlay Ground");
		default: return TEXT("Unknown");
		}
	}

	FMixtormatTheme MakeDefaultTheme()
	{
		FMixtormatTheme T;

		// ---- Palette -------------------------------------------------------------------
		T.Palette.Ground = SRGB(14, 15, 16);          // --ground-rgb
		T.Palette.Shell = SRGB(17, 18, 19);
		T.Palette.Panel = SRGB(25, 27, 29);          // --panel-rgb
		T.Palette.Text = SRGB(192, 195, 197);         // --text-rgb
		T.Palette.TextMuted = SRGB(192, 195, 197, 0.58f);  // --text-muted-opacity
		T.Palette.Accent = SRGB(82, 123, 137);        // --accent-rgb
		T.Palette.Modified = SRGB(230, 145, 25);      // --modified-rgb
		T.Palette.Warning = SRGB(194, 110, 100);      // --warning-rgb
		// The prototype has no --error-rgb; this carries the existing editor's error text value so
		// the role has an authored default rather than an unset black.
		T.Palette.Error = SRGB(230, 51, 51);
		T.Palette.Shade = SRGB(0, 0, 0);              // --shade-rgb
		T.Palette.Hairline = SRGB(111, 125, 130);     // --hairline-rgb
		T.Palette.MenuGround = SRGB(21, 22, 23);      // --popup-bottom-rgb
		T.Palette.ThumbnailGround = SRGB(16, 17, 18);
		T.Palette.OverlayGround = SRGB(12, 14, 15);   // --overlay-bottom-rgb

		// ---- Well ---------------------------------------------------------------------
		// --well-radius, --well-blend-mode, --well-shade-*, --well-border-*
		T.Well.Radius = 2.0f;
		T.Well.ShadeBlend = MixtormatCompositing::EMixtormatBlendMode::Multiply;
		T.Well.ShadeTop = 0.64f;
		T.Well.ShadeBottom = 0.13f;
		T.Well.BorderWidth = 1.0f;
		T.Well.BorderOpacity = 0.86f;
		T.Well.BorderTopOpacity = 0.33f;
		T.Well.BorderBottomOpacity = 0.11f;
		T.Well.BorderHoverOpacity = 0.47f;
		T.Well.BorderHoverTopOpacity = 0.78f;
		T.Well.BorderHoverBottomOpacity = 0.44f;
		T.Well.BorderSaturation = 2.0f;
		T.Well.HoverLiftOpacity = 0.38f;              // --hover-lift-opacity

		// ---- Fill ---------------------------------------------------------------------
		// --fill-body-*, --fill-shade-*, --fill-*-saturation, --fill-falloff-power
		T.Fill.BodyBlend = MixtormatCompositing::EMixtormatBlendMode::Additive;
		T.Fill.ShadeBlend = MixtormatCompositing::EMixtormatBlendMode::Multiply;
		T.Fill.Top = 0.45f;
		T.Fill.Bottom = 0.16f;
		T.Fill.HoverTop = 0.84f;
		T.Fill.HoverBottom = 0.46f;
		T.Fill.ActiveTop = 0.63f;
		T.Fill.ActiveBottom = 0.88f;
		T.Fill.Saturation = 0.7f;
		T.Fill.HoverSaturation = 1.4f;
		T.Fill.ActiveSaturation = 1.0f;
		// --fill-disabled-opacity .12, --fill-disabled-saturation .5
		T.Fill.DisabledOpacity = 0.12f;
		T.Fill.DisabledSaturation = 0.5f;
		T.Fill.ShadeStart = 0.25f;
		T.Fill.ShadeMid = 0.0f;
		T.Fill.ShadeEnd = 0.02f;
		T.Fill.ShadeMidPosition = 0.63f;
		T.Fill.FalloffPower = 0.05f;

		// ---- Toggle -------------------------------------------------------------------
		T.Toggle.Size = 16.0f;                        // --toggle-size
		T.Toggle.FillInset = 3.0f;                    // --toggle-fill-inset
		T.Toggle.DisabledShadeTop = 0.3f;
		T.Toggle.DisabledShadeBottom = 0.12f;

		T.SliderZeroTickOpacity = 0.16f;              // --zero-tick-opacity

		// ---- Control layout ------------------------------------------------------------
		T.ControlLayout.RowHeight = 18.0f;            // --row-height
		T.ControlLayout.RowGap = 3.0f;                // --row-gap
		T.ControlLayout.PairedGap = 3.0f;             // --paired-gap
		T.ControlLayout.RowTextInset = 8.0f;          // --dragger-text-inset
		T.ControlLayout.RowLabelGap = 6.0f;           // --dropdown-label-gap
		T.ControlLayout.ButtonHeight = 24.0f;         // --button-height
		T.ControlLayout.DropdownLabelRatio = 0.45f;   // --dropdown-label-ratio
				T.ControlLayout.DisabledLabelOpacity = 0.32f; // --text-disabled-opacity
		T.ControlLayout.CornerRadius = 3.0f;
		T.ControlLayout.OutlineWidth = 1.0f;
		T.ControlLayout.IconBrushSize = 20.0f;
		T.ControlLayout.IconBrushSizeLarge = 28.0f;
		T.ControlLayout.IconButtonSize = 14.0f;
		T.ControlLayout.StatusDotSize = 8.0f;
		T.ControlLayout.ToolbarLabelPadding = 5.0f;

		// ---- Foldout ------------------------------------------------------------------
		// --header-tint-rgb 37 40 43 at --header-tint-opacity .9. These two tokens are the foldout's
		// own and are the reason the lift has a local source field rather than a palette role.
		T.Foldout.LiftTint = SRGB(37, 40, 43);
		T.Foldout.LiftOpacity = 0.9f;
		// --header-hover-rgb 53 82 94 at --header-hover-opacity .85
		T.Foldout.HoverTint = SRGB(53, 82, 94);
		T.Foldout.HoverTintOpacity = 0.85f;

		T.Foldout.LiftFalloff = Falloff(1.0f, 0.0f, 0.95f);
		T.Foldout.LiftBlend = MixtormatCompositing::EMixtormatBlendMode::Normal;   // --foldout-blend-mode
		T.Foldout.AccentBlend = MixtormatCompositing::EMixtormatBlendMode::SoftLight; // --foldout-accent-blend-mode
		T.Foldout.LiftSaturation = 0.6f;             // --foldout-saturation
		T.Foldout.HoverSaturation = 1.0f;             // --foldout-hover-saturation
		T.Foldout.AccentOpacity = 0.8f;               // --foldout-accent-multiply-opacity
		T.Foldout.AccentHoverOpacity = 1.0f;          // --foldout-accent-hover-multiply-opacity
		T.Foldout.HairlineHoverTint = SRGB(127, 196, 219); // --hairline-hover-rgb
				T.Foldout.HairlineOpacity = 0.46f;            // --foldout-hairline-opacity
		T.Foldout.HairlineHoverOpacity = 0.85f;       // --hairline-hover-opacity
		T.Foldout.HairlineSaturation = 2.0f;          // --foldout-hairline-saturation
		T.Foldout.HairlineHoverSaturation = 1.4f;     // --foldout-hairline-hover-saturation

		T.FoldoutLayout.Height = 20.0f;               // --foldout-height
		T.FoldoutLayout.Gutter = 9.0f;                // --foldout-gutter
		T.FoldoutLayout.BodyTop = 5.0f;               // --foldout-body-top
		T.FoldoutLayout.BodyBottom = 6.0f;            // --foldout-body-bottom
		T.FoldoutLayout.OuterTop = 1.0f;              // --foldout-outer-top
		T.FoldoutLayout.OuterBottom = 1.0f;           // --foldout-outer-bottom
		T.FoldoutLayout.HeaderPaddingTop = 1.0f;      // --foldout-header-padding-top
		T.FoldoutLayout.HeaderPaddingBottom = 2.0f;   // --foldout-header-padding-bottom

		// ---- Card ---------------------------------------------------------------------
		T.Card.Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;  // --card-blend-mode
		T.Card.HeaderOpacity = 1.0f;                  // --card-header-opacity
		T.Card.BodyOpacity = 0.09f;                   // --card-body-opacity
		T.Card.HeaderSaturation = 2.0f;               // --card-header-saturation
		T.Card.BodySaturation = 2.2f;                 // --card-body-saturation
		T.Card.FalloffPower = 0.65f;                  // --card-falloff-power
		T.Card.Reach = 0.0f;                          // --card-gradient-reach
		T.Card.Radius = 3.0f;                         // --card-radius

		T.CardLayout.HeaderHeight = 16.0f;            // --card-header-height
		T.CardLayout.HeaderLeft = 11.0f;              // --card-header-left
		T.CardLayout.HeaderRight = 8.0f;              // --card-header-right
		T.CardLayout.HeaderMarginTop = 1.0f;          // --card-header-margin-top
		T.CardLayout.OuterTop = 2.0f;                 // --card-outer-top
		T.CardLayout.OuterBottom = 2.0f;              // --card-outer-bottom
		T.CardLayout.BodyHorizontal = 7.0f;           // --card-body-horizontal
		T.CardLayout.BodyTop = 3.0f;                  // --card-body-top
		T.CardLayout.BodyBottom = 7.0f;               // --card-body-bottom

		// ---- Layer ---------------------------------------------------------------------
		T.Layer.Radius = 2.0f;
		// --layer-*, --child-*, --layer-group-*
		T.Layer.Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		T.Layer.GroupBlend = MixtormatCompositing::EMixtormatBlendMode::SoftLight;  // --layer-group-blend-mode
		T.Layer.RestSaturation = 1.3f;
		T.Layer.HoverSaturation = 1.4f;
		T.Layer.SelectedSaturation = 2.3f;
		T.Layer.GroupSaturation = 1.0f;
		T.Layer.ChildSaturation = 1.2f;
		T.Layer.ChildHoverSaturation = 2.0f;
		T.Layer.ChildSelectedSaturation = 1.0f;
		T.Layer.RestStrength = 1.0f;
		T.Layer.HoverStrength = 1.0f;
		T.Layer.SelectedStrength = 1.0f;
		T.Layer.HairlineWidth = 1.0f;
		T.Layer.HairlineOpacity = 0.22f;
		T.Layer.RowBottom = SRGB(20, 22, 23);
		T.Layer.HoverTop = SRGB(45, 49, 52);
		T.Layer.HoverBottom = SRGB(34, 38, 41);
		T.Layer.SelectedTop = SRGB(56, 62, 66);
		T.Layer.SelectedBottom = SRGB(42, 47, 50);
		T.Layer.ChildLeft = SRGB(23, 25, 27);
		T.Layer.ChildRight = SRGB(37, 41, 44);
		T.Layer.ChildSelectedLeft = SRGB(29, 32, 34);
		T.Layer.ChildSelectedRight = SRGB(48, 53, 57);
		T.Layer.Cross = SRGB(51, 56, 60);
		T.Layer.HiddenTop = SRGB(25, 27, 29);
		T.Layer.HiddenEnd = SRGB(16, 17, 18);
		T.Layer.GroupStrength = 0.5f;                // --group-cross-opacity
		T.Layer.ChildStrength = 0.46f;               // --child-right-opacity
		T.Layer.ChildHoverStrength = 1.0f;           // --child-hover-right-opacity
		T.Layer.ChildSelectedStrength = 0.94f;       // --child-selected-right-opacity

		T.Layer.ActiveGlow.Source = MakeColorRef(EMixtormatColorRole::Accent);
		T.Layer.ActiveGlow.Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;
		T.Layer.ActiveGlow.Opacity = 0.18f;           // --layer-active-glow-opacity
		T.Layer.ActiveGlow.Saturation = 1.5f;         // --layer-active-glow-saturation
		T.Layer.ActiveGlow.Reach = 32.0f;             // --layer-active-glow-reach
		T.Layer.ActiveGlow.HairlineWidth = 1.0f;      // --layer-active-hairline-width
		T.Layer.ActiveGlow.HairlineOpacity = 0.6f;    // --layer-active-hairline-opacity

		T.Layer.ActiveHairlineWidth = 1.0f;
		T.Layer.ActiveHairlineOpacity = 0.55f;

		// --layer-hierarchy-line-*
		T.LayerHierarchy.Source = MakeColorRef(EMixtormatColorRole::Text);
		T.LayerHierarchy.Indent = 22.0f;
		T.LayerHierarchy.Width = 1.0f;
		T.LayerHierarchy.Opacity = 0.30f;
		T.LayerHierarchy.ParentJoinOffset = 0.0f;
		T.LayerHierarchy.ChildArmLength = 12.0f;

		T.LayerLayout.RowHeight = 23.0f;              // --layer-height
		T.LayerLayout.GroupRowHeight = 22.0f;         // --layer-group-height
		T.LayerLayout.ChildRowHeight = 20.0f;         // --child-height
		T.LayerLayout.Gap = 1.0f;                     // --layer-gap
		T.LayerLayout.ColumnGutter = 4.0f;
		T.LayerLayout.ThumbnailSize = 18.0f;          // --thumbnail-size
		T.LayerLayout.ChildIndent = 22.0f;            // --layer-indent

		// ---- Button --------------------------------------------------------------------
		// --group-button-*
		T.Button.Height = 24.0f;
		T.Button.HorizontalPadding = 8.0f;
		T.Button.BodyBlend = MixtormatCompositing::EMixtormatBlendMode::Normal;    // --group-button-blend-mode
		T.Button.HairlineBlend = MixtormatCompositing::EMixtormatBlendMode::Additive;
		T.Button.RestTop = 0.18f;
		T.Button.RestBottom = 0.03f;
		T.Button.HoverTop = 0.32f;
		T.Button.HoverBottom = 0.08f;
		T.Button.SelectedTop = 0.45f;
		T.Button.SelectedBottom = 0.12f;
		T.Button.GradientSaturation = 1.5f;
		T.Button.HairlineWidth = 1.0f;
		T.Button.HairlineOpacity = 0.18f;
		T.Button.HairlineHoverOpacity = 0.38f;
		T.Button.HairlineSelectedOpacity = 0.6f;
		T.Button.HairlineSaturation = 1.5f;
		T.Button.SeparatorWidth = 1.0f;
		T.Button.SeparatorHeight = 14.0f;
		T.Button.SeparatorOpacity = 0.16f;
		// --group-button-text-opacity .78
		T.Button.TextOpacity = 0.78f;

		// ---- Menu ----------------------------------------------------------------------
		// --menu-*, --popup-*
		T.Menu.LipSource = SRGB(26, 28, 30);           // --popup-top-rgb
		T.MenuLayout.Width = 190.0f;                   // --menu-width
		T.MenuLayout.RowHeight = 20.0f;                // --menu-row-height
		T.MenuLayout.ItemInset = 3.0f;                 // --menu-padding
		T.MenuLayout.LipHeight = 25.0f;                // --popup-lip-height
		T.Menu.LipTintOpacity = 0.1f;                 // --popup-tint-opacity
		T.Menu.BorderOpacity = 0.16f;                 // --popup-border-opacity
		T.Menu.ItemHoverOpacity = 1.0f;
		T.Menu.ItemCheckedOpacity = 1.0f;
		T.Menu.ItemDisabledOpacity = 0.32f;           // --text-disabled-opacity
		T.Menu.DestructiveText = SRGB(194, 110, 100);
		T.Menu.DestructiveHover = SRGB(74, 38, 38);
		T.MenuLayout.PanelPadding = 3.0f;
		T.MenuLayout.CaptionInsetAbove = 4.0f;
		T.MenuLayout.CaptionInsetBelow = 2.0f;
		T.MenuLayout.ItemGap = 5.0f;
		T.MenuLayout.SeparatorMargin = 3.0f;
		T.MenuLayout.ChevronSize = 10.0f;

		// ---- Preview / Gallery / Shell ------------------------------------------------
		T.Preview.PlateSource = SRGB(21, 22, 24);      // --overlay-plate-rgb
		T.Preview.PlateOpacity = 0.85f;               // --overlay-plate-opacity
		T.Preview.IconRestOpacity = 0.45f;            // --overlay-icon-rest-opacity
		T.Preview.HoverAccent = 0.18f;                // --overlay-hover-accent
		T.Preview.PressAccent = 0.35f;                // --overlay-press-accent
		T.PreviewLayout.OverlayInset = 8.0f;
		T.PreviewLayout.OverlayClusterInset = 2.0f;
		T.PreviewLayout.ToolbarGap = 5.0f;
		T.PreviewLayout.OverlayButtonGap = 4.0f;
		T.PreviewLayout.ComparisonToggleGap = 4.0f;
		T.PreviewLayout.ResolutionControlWidth = 92.0f;
		T.PreviewLayout.TogglePadding = 3.0f;
		T.PreviewLayout.FinalPopupWidth = 232.0f;

		T.GalleryLayout.TileSize = 80.0f;             // --gallery-tile-size
		T.GalleryLayout.TileGap = 4.0f;               // --gallery-gap

		T.Shell.TopBarHeight = 38.0f;                 // --topbar-height
		T.Shell.TopBarActionInset = 4.0f;
		T.Shell.StatusBarHeight = 22.0f;              // --status-height
		T.Shell.PanelPadding = 8.0f;                  // --panel-padding
		T.Shell.ScrollbarThickness = 5.0f;
		T.Shell.ScrollbarThumbOpacity = 0.180f;
		T.Shell.ScrollbarHoverOpacity = 0.340f;
		T.Shell.SplitterVisualWidth = 1.0f;           // --splitter-size
		T.Shell.SplitterHitWidth = 6.0f;              // --splitter-hit-size
		T.ShellTheme.SplitterHoverSource = SRGB(127, 196, 219); // --hairline-hover-rgb
		T.ShellTheme.SplitterOpacity = 0.46f;         // --foldout-hairline-opacity
		T.ShellTheme.SplitterHoverOpacity = 0.85f;    // --hairline-hover-opacity

		// ---- Icons ---------------------------------------------------------------------
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::TopBar)] = Icon(18.0f, 0.6f);           // --topbar-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PanelToolbar)] = Icon(12.0f, 0.62f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PanelToolbar)].GlyphSize = 12.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PanelToolbar)].ButtonSize = 22.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PanelToolbar)].HitSize = 24.0f;     // --toolbar-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)] = Icon(15.000f, 0.600f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].GlyphSize = 15.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].ButtonSize = 26.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].HitSize = 28.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].RestOpacity = 0.600f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].HoverOpacity = 1.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::PreviewToolbar)].DisabledOpacity = 0.320f;   // --overlay-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)] = Icon(10.0f, 0.48f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)].GlyphSize = 10.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)].ButtonSize = 18.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)].HitSize = 20.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)].MarkRadius = 2.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerEye)].MarkOutlineWidth = 1.0f;         // --layer-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerDisclosure)] = Icon(9.0f, 0.60f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerDisclosure)].GlyphSize = 9.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerDisclosure)].ButtonSize = 18.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::LayerDisclosure)].HitSize = 20.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::FoldoutDisclosure)] = Icon(9.0f, 0.60f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::FoldoutDisclosure)].GlyphSize = 9.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::FoldoutDisclosure)].ButtonSize = 18.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::FoldoutDisclosure)].HitSize = 20.0f; // --foldout-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)] = Icon(12.000f, 0.600f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].GlyphSize = 12.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].ButtonSize = 20.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].HitSize = 22.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].RestOpacity = 0.600f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].HoverOpacity = 1.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::CardLeading)].DisabledOpacity = 0.320f;     // --card-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)] = Icon(13.000f, 0.600f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].GlyphSize = 13.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].ButtonSize = 24.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].HitSize = 26.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].RestOpacity = 0.600f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].HoverOpacity = 1.000f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::Menu)].DisabledOpacity = 0.320f;             // --menu-icon-*
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::GalleryToolbar)] = Icon(11.0f, 0.62f);
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::GalleryToolbar)].GlyphSize = 11.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::GalleryToolbar)].ButtonSize = 22.0f;
		T.Icons.Roles[static_cast<uint8>(EMixtormatIconRole::GalleryToolbar)].HitSize = 24.0f;

		// ---- Typography ----------------------------------------------------------------
		// Weights below are the ones tokens.css authors, not a guess from a bold flag. Two
		// corrections fall out of auditing them:
		//
		//   ControlValue is 600 (--value-weight), so it is SemiBold. It was previously mapped onto
		//   Bold by a 500 midpoint and rendered heavier than the design.
		//
		//   TopBar is the brand mark, which components.css sets at font-weight 700, so it is Bold.
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::Body)] =
			Text(10.0f, EMixtormatFontWeight::Regular);                                              // --body-size
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::ControlLabel)] =
			Text(10.0f, EMixtormatFontWeight::Regular, 0.75f);                                        // --control-label-*
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::ControlValue)] =
			Text(10.0f, EMixtormatFontWeight::SemiBold, 0.9f);                                       // --value-weight 600
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::Caption)] =
			Text(11.0f, EMixtormatFontWeight::Regular);                                              // --caption-size
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::FoldoutTitle)] =
			Text(9.0f, EMixtormatFontWeight::Regular, 0.74f, 1.0f);                                   // --foldout-title-*
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::CardTitle)] =
			Text(8.0f, EMixtormatFontWeight::Regular, 0.75f, 0.6f);                                   // --card-title-*
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::LayerName)] =
			Text(10.0f, EMixtormatFontWeight::Regular);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::LayerSource)] =
			Text(9.0f, EMixtormatFontWeight::Regular, 0.5f);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::Menu)] =
			Text(10.0f, EMixtormatFontWeight::Regular);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::MenuShortcut)] =
			Text(10.0f, EMixtormatFontWeight::Regular, 0.58f);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::GalleryCaption)] =
			Text(9.0f, EMixtormatFontWeight::Regular);
		// components.css: `.brand { font-weight: 700 }`
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::TopBar)] =
			Text(10.0f, EMixtormatFontWeight::Bold);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::PreviewLabel)] =
			Text(10.0f, EMixtormatFontWeight::Regular);
		T.Typography.Roles[static_cast<uint8>(EMixtormatTextRole::Badge)] =
			Text(9.0f, EMixtormatFontWeight::Regular);

		return T;
	}

	void ValidateTheme(FMixtormatTheme& InOutTheme, TArray<FText>& OutIssues)
	{
		// Opacities and saturations are weights, not colours. A value outside 0..1 is a number a
		// recipe cannot use: clamp it and say so, rather than letting a ramp read backwards.
		auto Clamp01 = [&OutIssues](const TCHAR* What, float& Value)
		{
			const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
			if (!FMath::IsNearlyEqual(Clamped, Value))
			{
				OutIssues.Add(FText::Format(
					NSLOCTEXT("Mixtormat", "ThemeClamp", "{0} was {1}, clamped to {2}."),
					FText::FromString(What), FText::AsNumber(Value), FText::AsNumber(Clamped)));
				Value = Clamped;
			}
		};

		Clamp01(TEXT("Well.ShadeTop"), InOutTheme.Well.ShadeTop);
		Clamp01(TEXT("Well.ShadeBottom"), InOutTheme.Well.ShadeBottom);
		Clamp01(TEXT("Well.BorderOpacity"), InOutTheme.Well.BorderOpacity);
		Clamp01(TEXT("Well.BorderTopOpacity"), InOutTheme.Well.BorderTopOpacity);
		Clamp01(TEXT("Well.BorderBottomOpacity"), InOutTheme.Well.BorderBottomOpacity);
		Clamp01(TEXT("Well.BorderHoverOpacity"), InOutTheme.Well.BorderHoverOpacity);
		Clamp01(TEXT("Well.BorderHoverTopOpacity"), InOutTheme.Well.BorderHoverTopOpacity);
		Clamp01(TEXT("Well.BorderHoverBottomOpacity"), InOutTheme.Well.BorderHoverBottomOpacity);
		Clamp01(TEXT("Well.HoverLiftOpacity"), InOutTheme.Well.HoverLiftOpacity);

		Clamp01(TEXT("Fill.Top"), InOutTheme.Fill.Top);
		Clamp01(TEXT("Fill.Bottom"), InOutTheme.Fill.Bottom);
		Clamp01(TEXT("Fill.HoverTop"), InOutTheme.Fill.HoverTop);
		Clamp01(TEXT("Fill.HoverBottom"), InOutTheme.Fill.HoverBottom);
		Clamp01(TEXT("Fill.ActiveTop"), InOutTheme.Fill.ActiveTop);
		Clamp01(TEXT("Fill.ActiveBottom"), InOutTheme.Fill.ActiveBottom);
		Clamp01(TEXT("Fill.ShadeStart"), InOutTheme.Fill.ShadeStart);
		Clamp01(TEXT("Fill.ShadeMid"), InOutTheme.Fill.ShadeMid);
		Clamp01(TEXT("Fill.ShadeEnd"), InOutTheme.Fill.ShadeEnd);
		Clamp01(TEXT("Fill.DisabledOpacity"), InOutTheme.Fill.DisabledOpacity);

		// Saturations are multipliers and may legitimately exceed 1 -- the prototype saturates
		// the well border at 2 and card bodies at 2.2 -- so only a negative value is wrong.
		auto ClampSaturation = [&OutIssues](const TCHAR* What, float& Value)
		{
			if (Value < 0.0f)
			{
				OutIssues.Add(FText::Format(
					NSLOCTEXT("Mixtormat", "ThemeSaturation", "{0} was negative, reset to 0."),
					FText::FromString(What)));
				Value = 0.0f;
			}
		};

		ClampSaturation(TEXT("Well.BorderSaturation"), InOutTheme.Well.BorderSaturation);
		ClampSaturation(TEXT("Fill.Saturation"), InOutTheme.Fill.Saturation);
		ClampSaturation(TEXT("Fill.HoverSaturation"), InOutTheme.Fill.HoverSaturation);
		ClampSaturation(TEXT("Fill.ActiveSaturation"), InOutTheme.Fill.ActiveSaturation);
		ClampSaturation(TEXT("Foldout.LiftSaturation"), InOutTheme.Foldout.LiftSaturation);
		ClampSaturation(TEXT("Foldout.HoverSaturation"), InOutTheme.Foldout.HoverSaturation);
		ClampSaturation(TEXT("Card.HeaderSaturation"), InOutTheme.Card.HeaderSaturation);
		ClampSaturation(TEXT("Card.BodySaturation"), InOutTheme.Card.BodySaturation);
		ClampSaturation(TEXT("Layer.RestSaturation"), InOutTheme.Layer.RestSaturation);
		ClampSaturation(TEXT("Layer.HoverSaturation"), InOutTheme.Layer.HoverSaturation);
		ClampSaturation(TEXT("Layer.SelectedSaturation"), InOutTheme.Layer.SelectedSaturation);
		ClampSaturation(TEXT("Button.GradientSaturation"), InOutTheme.Button.GradientSaturation);

		Clamp01(TEXT("Toggle.DisabledShadeTop"), InOutTheme.Toggle.DisabledShadeTop);
		Clamp01(TEXT("Toggle.DisabledShadeBottom"), InOutTheme.Toggle.DisabledShadeBottom);
		Clamp01(TEXT("Foldout.LiftOpacity"), InOutTheme.Foldout.LiftOpacity);
				Clamp01(TEXT("Foldout.HoverTintOpacity"), InOutTheme.Foldout.HoverTintOpacity);
				Clamp01(TEXT("Foldout.AccentOpacity"), InOutTheme.Foldout.AccentOpacity);
		Clamp01(TEXT("Foldout.AccentHoverOpacity"), InOutTheme.Foldout.AccentHoverOpacity);
		Clamp01(TEXT("Foldout.HairlineHoverTint.A"), InOutTheme.Foldout.HairlineHoverTint.A);
				Clamp01(TEXT("Foldout.HairlineOpacity"), InOutTheme.Foldout.HairlineOpacity);
		Clamp01(TEXT("Foldout.HairlineHoverOpacity"), InOutTheme.Foldout.HairlineHoverOpacity);
		Clamp01(TEXT("Card.HeaderOpacity"), InOutTheme.Card.HeaderOpacity);
		Clamp01(TEXT("Card.BodyOpacity"), InOutTheme.Card.BodyOpacity);
		Clamp01(TEXT("Layer.ActiveGlow.Opacity"), InOutTheme.Layer.ActiveGlow.Opacity);
		Clamp01(TEXT("Layer.ActiveGlow.HairlineOpacity"), InOutTheme.Layer.ActiveGlow.HairlineOpacity);
		Clamp01(TEXT("Layer.ActiveHairlineOpacity"), InOutTheme.Layer.ActiveHairlineOpacity);
		Clamp01(TEXT("Layer.GroupStrength"), InOutTheme.Layer.GroupStrength);
				Clamp01(TEXT("Layer.RestStrength"), InOutTheme.Layer.RestStrength);
				Clamp01(TEXT("Layer.HoverStrength"), InOutTheme.Layer.HoverStrength);
				Clamp01(TEXT("Layer.SelectedStrength"), InOutTheme.Layer.SelectedStrength);
				Clamp01(TEXT("Layer.GroupTintStrength"), InOutTheme.Layer.GroupTintStrength);
				Clamp01(TEXT("Layer.GroupTintSelectedStrength"), InOutTheme.Layer.GroupTintSelectedStrength);
				Clamp01(TEXT("Layer.ReferenceHiddenTint"), InOutTheme.Layer.ReferenceHiddenTint);
				Clamp01(TEXT("Layer.ReferenceSelectedTint"), InOutTheme.Layer.ReferenceSelectedTint);
				Clamp01(TEXT("Layer.ReferenceHoverTint"), InOutTheme.Layer.ReferenceHoverTint);
				Clamp01(TEXT("Layer.ReferenceRestTint"), InOutTheme.Layer.ReferenceRestTint);
				Clamp01(TEXT("Layer.InstanceSourceLeftTint"), InOutTheme.Layer.InstanceSourceLeftTint);
				Clamp01(TEXT("Layer.InstanceSourceRightTint"), InOutTheme.Layer.InstanceSourceRightTint);
				Clamp01(TEXT("Layer.ChildLeftOpacity"), InOutTheme.Layer.ChildLeftOpacity);
				Clamp01(TEXT("Layer.ChildHoverLeftOpacity"), InOutTheme.Layer.ChildHoverLeftOpacity);
				Clamp01(TEXT("Layer.ChildSelectedLeftOpacity"), InOutTheme.Layer.ChildSelectedLeftOpacity);
				Clamp01(TEXT("Layer.HairlineOpacity"), InOutTheme.Layer.HairlineOpacity);
				ClampSaturation(TEXT("Layer.GroupSaturation"), InOutTheme.Layer.GroupSaturation);
				ClampSaturation(TEXT("Layer.ChildSaturation"), InOutTheme.Layer.ChildSaturation);
				ClampSaturation(TEXT("Layer.ChildHoverSaturation"), InOutTheme.Layer.ChildHoverSaturation);
				ClampSaturation(TEXT("Layer.ChildSelectedSaturation"), InOutTheme.Layer.ChildSelectedSaturation);
				ClampSaturation(TEXT("Layer.ActiveGlow.Saturation"), InOutTheme.Layer.ActiveGlow.Saturation);
		Clamp01(TEXT("Layer.ChildStrength"), InOutTheme.Layer.ChildStrength);
		Clamp01(TEXT("Layer.ChildHoverStrength"), InOutTheme.Layer.ChildHoverStrength);
		Clamp01(TEXT("Layer.ChildSelectedStrength"), InOutTheme.Layer.ChildSelectedStrength);
		Clamp01(TEXT("LayerHierarchy.Opacity"), InOutTheme.LayerHierarchy.Opacity);
		Clamp01(TEXT("Button.RestTop"), InOutTheme.Button.RestTop);
		Clamp01(TEXT("Button.RestBottom"), InOutTheme.Button.RestBottom);
		Clamp01(TEXT("Button.HoverTop"), InOutTheme.Button.HoverTop);
		Clamp01(TEXT("Button.HoverBottom"), InOutTheme.Button.HoverBottom);
		Clamp01(TEXT("Button.SelectedTop"), InOutTheme.Button.SelectedTop);
		Clamp01(TEXT("Button.SelectedBottom"), InOutTheme.Button.SelectedBottom);
		Clamp01(TEXT("Button.HairlineOpacity"), InOutTheme.Button.HairlineOpacity);
		Clamp01(TEXT("Button.HairlineHoverOpacity"), InOutTheme.Button.HairlineHoverOpacity);
		Clamp01(TEXT("Button.HairlineSelectedOpacity"), InOutTheme.Button.HairlineSelectedOpacity);
		Clamp01(TEXT("Button.SeparatorOpacity"), InOutTheme.Button.SeparatorOpacity);
				Clamp01(TEXT("Button.TextOpacity"), InOutTheme.Button.TextOpacity);
						Clamp01(TEXT("ControlLayout.DisabledLabelOpacity"), InOutTheme.ControlLayout.DisabledLabelOpacity);
		Clamp01(TEXT("Menu.LipTintOpacity"), InOutTheme.Menu.LipTintOpacity);
		Clamp01(TEXT("Menu.BorderOpacity"), InOutTheme.Menu.BorderOpacity);
		Clamp01(TEXT("Menu.ItemDisabledOpacity"), InOutTheme.Menu.ItemDisabledOpacity);
		Clamp01(TEXT("Menu.ItemHoverOpacity"), InOutTheme.Menu.ItemHoverOpacity);
		Clamp01(TEXT("Menu.ItemCheckedOpacity"), InOutTheme.Menu.ItemCheckedOpacity);
		Clamp01(TEXT("Gallery.BorderOpacity"), InOutTheme.Gallery.BorderOpacity);
		Clamp01(TEXT("Gallery.HoverLiftOpacity"), InOutTheme.Gallery.HoverLiftOpacity);
		Clamp01(TEXT("Gallery.SelectedEdgeOpacity"), InOutTheme.Gallery.SelectedEdgeOpacity);

		Clamp01(TEXT("Preview.PlateOpacity"), InOutTheme.Preview.PlateOpacity);
		Clamp01(TEXT("Preview.IconRestOpacity"), InOutTheme.Preview.IconRestOpacity);
		Clamp01(TEXT("Preview.HoverAccent"), InOutTheme.Preview.HoverAccent);
		Clamp01(TEXT("Preview.PressAccent"), InOutTheme.Preview.PressAccent);
		Clamp01(TEXT("Shell.SplitterOpacity"), InOutTheme.ShellTheme.SplitterOpacity);
		Clamp01(TEXT("Shell.SplitterHoverOpacity"), InOutTheme.ShellTheme.SplitterHoverOpacity);

		// A falloff with zero or negative samples describes a flat fill, not a curve. Two stops is
		// the minimum that still renders as a gradient.
		if (InOutTheme.Foldout.LiftFalloff.Samples < 2)
		{
			OutIssues.Add(FText::Format(
				NSLOCTEXT("Mixtormat", "ThemeFalloffSamples", "Foldout.LiftFalloff.Samples was {0}, raised to 2."),
				FText::AsNumber(InOutTheme.Foldout.LiftFalloff.Samples)));
			InOutTheme.Foldout.LiftFalloff.Samples = 2;
		}

		// Geometry cannot be negative; a negative width is an invisible border or an unclickable
		// hit target, and both fail silently.
		auto ClampMin = [&OutIssues](const TCHAR* What, float& Value, const float Min)
		{
			if (Value < Min)
			{
				OutIssues.Add(FText::Format(
					NSLOCTEXT("Mixtormat", "ThemeGeometry", "{0} was below {1}, raised."),
					FText::FromString(What), FText::AsNumber(Min)));
				Value = Min;
			}
		};

		ClampMin(TEXT("Gallery.BorderWidth"), InOutTheme.Gallery.BorderWidth, 0.0f);
		ClampMin(TEXT("Gallery.SelectedEdgeWidth"), InOutTheme.Gallery.SelectedEdgeWidth, 0.0f);
		ClampMin(TEXT("Gallery.CornerRadius"), InOutTheme.Gallery.CornerRadius, 0.0f);
		ClampMin(TEXT("Well.Radius"), InOutTheme.Well.Radius, 0.0f);
		ClampMin(TEXT("Well.BorderWidth"), InOutTheme.Well.BorderWidth, 0.0f);
		ClampMin(TEXT("Toggle.Size"), InOutTheme.Toggle.Size, 1.0f);
		ClampMin(TEXT("Card.Radius"), InOutTheme.Card.Radius, 0.0f);
				ClampMin(TEXT("Card.Reach"), InOutTheme.Card.Reach, 0.0f);
		ClampMin(TEXT("Button.Height"), InOutTheme.Button.Height, 1.0f);
		ClampMin(TEXT("Button.HairlineWidth"), InOutTheme.Button.HairlineWidth, 0.0f);
		ClampMin(TEXT("Layer.HairlineWidth"), InOutTheme.Layer.HairlineWidth, 0.0f);
				ClampMin(TEXT("Layer.ActiveHairlineWidth"), InOutTheme.Layer.ActiveHairlineWidth, 0.0f);
				ClampMin(TEXT("Layer.ActiveGlow.Reach"), InOutTheme.Layer.ActiveGlow.Reach, 0.0f);
				ClampMin(TEXT("LayerLayout.RowHeight"), InOutTheme.LayerLayout.RowHeight, 1.0f);
		ClampMin(TEXT("LayerLayout.GroupRowHeight"), InOutTheme.LayerLayout.GroupRowHeight, 1.0f);
		ClampMin(TEXT("LayerLayout.ChildRowHeight"), InOutTheme.LayerLayout.ChildRowHeight, 1.0f);
		ClampMin(TEXT("LayerHierarchy.Width"), InOutTheme.LayerHierarchy.Width, 0.0f);
		ClampMin(TEXT("GalleryLayout.TileSize"), InOutTheme.GalleryLayout.TileSize, 1.0f);
		ClampMin(TEXT("GalleryLayout.TileGap"), InOutTheme.GalleryLayout.TileGap, 0.0f);
		ClampMin(TEXT("GalleryLayout.TilePadding"), InOutTheme.GalleryLayout.TilePadding, 0.0f);
		ClampMin(TEXT("GalleryLayout.CaptionHeight"), InOutTheme.GalleryLayout.CaptionHeight, 0.0f);
		ClampMin(TEXT("GalleryLayout.CaptionInset"), InOutTheme.GalleryLayout.CaptionInset, 0.0f);
		ClampMin(TEXT("GalleryLayout.OverlayInset"), InOutTheme.GalleryLayout.OverlayInset, 0.0f);
		ClampMin(TEXT("GalleryLayout.HeaderGap"), InOutTheme.GalleryLayout.HeaderGap, 0.0f);
		ClampMin(TEXT("Shell.TopBarHeight"), InOutTheme.Shell.TopBarHeight, 1.0f);
		ClampMin(TEXT("Shell.StatusBarHeight"), InOutTheme.Shell.StatusBarHeight, 1.0f);
		ClampMin(TEXT("Shell.PanelPadding"), InOutTheme.Shell.PanelPadding, 0.0f);
		ClampMin(TEXT("Shell.SplitterVisualWidth"), InOutTheme.Shell.SplitterVisualWidth, 0.0f);
		ClampMin(TEXT("Shell.SplitterHitWidth"), InOutTheme.Shell.SplitterHitWidth, 0.0f);
		ClampMin(TEXT("MenuLayout.RowHeight"), InOutTheme.MenuLayout.RowHeight, 1.0f);
		ClampMin(TEXT("MenuLayout.LipHeight"), InOutTheme.MenuLayout.LipHeight, 0.0f);
		ClampMin(TEXT("MenuLayout.Width"), InOutTheme.MenuLayout.Width, 0.0f);
		ClampMin(TEXT("MenuLayout.ItemInset"), InOutTheme.MenuLayout.ItemInset, 0.0f);
		ClampMin(TEXT("MenuLayout.ItemGap"), InOutTheme.MenuLayout.ItemGap, 0.0f);
		ClampMin(TEXT("MenuLayout.PanelPadding"), InOutTheme.MenuLayout.PanelPadding, 0.0f);
		ClampMin(TEXT("MenuLayout.CaptionInsetAbove"), InOutTheme.MenuLayout.CaptionInsetAbove, 0.0f);
		ClampMin(TEXT("MenuLayout.CaptionInsetBelow"), InOutTheme.MenuLayout.CaptionInsetBelow, 0.0f);
		ClampMin(TEXT("MenuLayout.SeparatorMargin"), InOutTheme.MenuLayout.SeparatorMargin, 0.0f);
		ClampMin(TEXT("MenuLayout.ChevronSize"), InOutTheme.MenuLayout.ChevronSize, 1.0f);

		// The fill's shade midpoint is a position on its axis, so it is clamped rather than
		// rejected: an out-of-range midpoint still describes a ramp, just a shifted one.
		InOutTheme.Fill.ShadeMidPosition = FMath::Clamp(InOutTheme.Fill.ShadeMidPosition, 0.0f, 1.0f);
		InOutTheme.Fill.FalloffPower = FMath::Max(InOutTheme.Fill.FalloffPower, 0.01f);
		InOutTheme.Card.FalloffPower = FMath::Max(InOutTheme.Card.FalloffPower, 0.01f);
	}
}