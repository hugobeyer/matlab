// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatCompositing.h"

// The editable model. Everything an author can change about the way Mixtormat looks lives in
// FMixtormatTheme and nowhere else.
//
// A surface is described by how it is constructed, not by a colour per state:
//
//     Well
//       base  = Ground
//       shade = Shade, Multiply, vertical falloff 0.64 -> 0.13
//       border= Hairline, Normal, 1px
//
// rather than by WellTop / WellBottom / WellTopHover / WellBorderHover. The second form is what
// made the old token wall unmaintainable: adding a hover state meant adding colours, and no two
// components could ever share one.
//
// This header holds *authored* data only. Painters never read it -- they read FMixtormatResolvedStyle
// (MixtormatResolvedStyle.h), which is built once per theme edit. Blend maths lives in
// MixtormatCompositing.h and is deliberately not duplicated here.

namespace Mixtormat
{
	// ---- Colour -----------------------------------------------------------------------------

	// The whole palette. Fourteen roles, and the only place a colour may be picked: anything a
	// component needs beyond these is derived from opacity, blend, saturation or falloff, which is
	// why a missing "hover" colour is a design question rather than a new swatch.
	struct FMixtormatPaletteTheme
	{
		// --ground-rgb
		FLinearColor Ground = FLinearColor::White;
		// 17 18 19 -- the shell column behind the panels. Distinct from Ground on purpose: the
		// prototype paints cards and foldout bodies off Ground, not off the panel they sit on.
		FLinearColor Shell = FLinearColor::White;
		// --panel-rgb
		FLinearColor Panel = FLinearColor::White;

		// --text-rgb
		FLinearColor Text = FLinearColor::White;
		// --text-muted-opacity applied to Text
		FLinearColor TextMuted = FLinearColor::White;

		// --accent-rgb
		FLinearColor Accent = FLinearColor::White;
		// --modified-rgb
		FLinearColor Modified = FLinearColor::White;
		// --warning-rgb
		FLinearColor Warning = FLinearColor::White;
		FLinearColor Error = FLinearColor::White;

		// --shade-rgb. Black by definition: a shade layer is darkening, so its colour is what it
		// multiplies toward, and its weight lives in the layer opacity rather than in the swatch.
		FLinearColor Shade = FLinearColor::White;
		// --hairline-rgb
		FLinearColor Hairline = FLinearColor::White;

		// --popup-bottom-rgb
		FLinearColor MenuGround = FLinearColor::White;
		FLinearColor ThumbnailGround = FLinearColor::White;
		// --overlay-bottom-rgb, painted translucent over whatever the viewport is showing
		FLinearColor OverlayGround = FLinearColor::White;
	};

	enum class EMixtormatColorRole : uint8
	{
		Ground,
		Shell,
		Panel,
		Text,
		TextMuted,
		Accent,
		Modified,
		Warning,
		Error,
		Shade,
		Hairline,
		MenuGround,
		ThumbnailGround,
		OverlayGround,
	};

	// A recipe never names a colour. It names a role and describes how to use it, so retinting the
	// palette moves every surface that referenced it without touching a single recipe.
	struct FMixtormatColorRef
	{
		EMixtormatColorRole Role = EMixtormatColorRole::Ground;

		float Opacity = 1.0f;
		// CSS `filter: saturate()`, applied to the resolved colour. A property of the paint layer,
		// not of the palette: one accent is authored at 0.7 as a fill and 1.5 on a button.
		float Saturation = 1.0f;

		// Per-channel tint applied after resolution. Lets a recipe reuse an existing role at a
		// different weight without that weight becoming a palette entry.
		FLinearColor Multiplier = FLinearColor::White;

		// An authored surface-local source; uses the same tint, opacity and saturation as a role.
		TOptional<FLinearColor> LocalColor;
	};

	// ---- Paint layers -----------------------------------------------------------------------

	enum class EMixtormatAxis : uint8
	{
		None,
		Horizontal,
		Vertical,
	};

	struct FMixtormatRampPoint
	{
		float Position = 0.0f;
		float Value = 1.0f;
	};

	// A multi-stop opacity ramp. CSS writes these as linear-gradient(); the values here are the
	// gradient's *alpha* stops, because the colour is always the layer's Source and only its weight
	// travels along the axis.
	struct FMixtormatRamp
	{
		EMixtormatAxis Axis = EMixtormatAxis::None;
		TArray<FMixtormatRampPoint, TInlineAllocator<8>> Points;
	};

	// One composited pass over what is already on screen.
	struct FMixtormatPaintLayer
	{
		FMixtormatColorRef Source;
		// Optional colour endpoint, interpolated along OpacityRamp.Axis independently of opacity.
		TOptional<FMixtormatColorRef> SourceEnd;
		MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;

		FMixtormatRamp OpacityRamp;

		// Scales the layer's contribution without changing its direction. How an endpoint of a
		// falloff is authored separately from the layer's overall strength.
		float Strength = 1.0f;

		bool bEnabled = true;
	};

	// A hairline. Used for foldout and button edges, well outlines, selected row top edges, menu
	// borders, tile selection and separators -- one model rather than a colour per surface.
	struct FMixtormatBorderLayer
	{
		FMixtormatColorRef Source;
		MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;

		// Opacity along the surface's axis, sampled at each edge's own position. Present because the
		// well authors its outline as one overall intensity times a per-edge endpoint
		// (0.33 top against 0.11 bottom): without a ramp the outline is a single flat colour, and
		// those two authored endpoints have no representation at all.
		//
		// Sampled per edge, not along an edge: a top border takes T = 0 and a bottom border T = 1,
		// so a vertical fade becomes a brighter top edge over a dimmer one. A fade *along* an edge
		// would need the edge split into segments and is not authored anywhere.
		FMixtormatRamp OpacityRamp;

		float Width = 1.0f;

		bool bTop = false;
		bool bBottom = false;
		bool bLeft = false;
		bool bRight = false;

		// How much of the edge's long axis this border spans, 1.0 being the whole edge.
		//
		// Present because a group button's separator is a centred bar rather than an edge: the
		// prototype draws it 14px tall inside a 24px button. Without this the only expressible
		// border is a full edge, and a full-height separator reads as a divider between two columns
		// instead of as a tick between two actions.
		//
		// Scales the long axis on every edge: X for a top or bottom edge, Y for a side.
		float EdgeFraction = 1.0f;
	};

	// A selected row's halo: a soft additive reach plus a crisp hairline on the top edge.
	struct FMixtormatGlowLayer
	{
		FMixtormatColorRef Source;
		MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;

		float Opacity = 0.0f;
		float Saturation = 1.0f;

		// How far past the row's bounds the halo is allowed to bleed.
		float Reach = 0.0f;

		float HairlineWidth = 0.0f;
		float HairlineOpacity = 0.0f;
	};

	// The shared power curve, ported once from the prototype's falloff.js:
	//
	//     value(t) = Start + (End - Start) * pow(t, max(Power, 0.01))
	//
	// Foldouts, cards, fills and glow all use this. There is no per-component curve formula,
	// because four near-identical curves is how they drift apart.
	struct FMixtormatFalloff
	{
		float Start = 1.0f;
		float End = 0.0f;
		float Power = 1.0f;
		// How finely the curve is described. This is not how finely Slate renders it -- the
		// gradient painter samples again between these stops.
		int32 Samples = 6;
	};

	// What the generic painter needs to draw any Mixtormat surface. Most components are this and
	// nothing else; the ones that are not (card seam, layer rails) compute geometry and still hand
	// their colour work here.
	struct FMixtormatSurfaceRecipe
	{
		FMixtormatColorRef Base;
		TArray<FMixtormatPaintLayer, TInlineAllocator<4>> Layers;
		TArray<FMixtormatBorderLayer, TInlineAllocator<2>> Borders;
		float Radius = 0.0f;
		// Set by a surface that deliberately sits ON something the painter cannot see -- the preview
		// plate over a rendered viewport -- so its authored alpha has to survive to the draw call.
		// Every other surface owns its rectangle and is composited opaque.
		bool bTranslucent = false;
	};

	// ---- State ------------------------------------------------------------------------------

	enum class EMixtormatVisualState : uint8
	{
		Rest,
		Hover,
		Pressed,
		Selected,
		Disabled,
	};

	// A state is a numeric nudge, never a second surface. Hover on a well is the same well with a
	// stronger border; duplicating the recipe per state is what produced six well colours.
	struct FMixtormatStateModifier
	{
		float Opacity = 1.0f;
		float Saturation = 1.0f;
		float Strength = 1.0f;

		TOptional<FMixtormatColorRef> SourceOverride;
		TOptional<MixtormatCompositing::EMixtormatBlendMode> BlendOverride;
	};

	// What a widget knows. It reports what the pointer is doing and stops there: deciding which
	// border opacity that implies belongs to the style system.
	struct FMixtormatVisualStateContext
	{
		bool bHovered = false;
		bool bPressed = false;
		bool bSelected = false;
		bool bEnabled = true;
	};

	// ---- Recipe families --------------------------------------------------------------------

	// Ground, plus a black Multiply shade falling top to bottom, plus a hairline outline.
	// `--well-shade-top/bottom`, `--well-border-*`.
	struct FMixtormatWellTheme
	{
		float Radius = 0.0f;

		MixtormatCompositing::EMixtormatBlendMode ShadeBlend = MixtormatCompositing::EMixtormatBlendMode::Multiply;

		float ShadeTop = 0.64f;
		float ShadeBottom = 0.13f;

		float BorderWidth = 1.0f;
		// `--well-border-opacity` scales the per-edge endpoint opacities below, rather than being
		// replaced by them.
		float BorderOpacity = 0.86f;
		float BorderTopOpacity = 0.33f;
		float BorderBottomOpacity = 0.11f;
		float BorderHoverOpacity = 0.47f;
		float BorderHoverTopOpacity = 0.78f;
		float BorderHoverBottomOpacity = 0.44f;
		float BorderSaturation = 2.0f;

		// A faint additive lift so a hovered well brightens instead of only outlining.
		float HoverLiftOpacity = 0.38f;
	};

	// The slider and progress fill: an accent body added over the well, then a horizontal black
	// Multiply shade with a movable midpoint so the fill reads as eased rather than lit from one
	// edge. `--fill-body-*`, `--fill-shade-*`.
	struct FMixtormatFillTheme
	{
		MixtormatCompositing::EMixtormatBlendMode BodyBlend = MixtormatCompositing::EMixtormatBlendMode::Additive;
		MixtormatCompositing::EMixtormatBlendMode ShadeBlend = MixtormatCompositing::EMixtormatBlendMode::Multiply;

		float Top = 0.45f;
		float Bottom = 0.16f;

		float HoverTop = 0.84f;
		float HoverBottom = 0.46f;

		float ActiveTop = 0.63f;
		float ActiveBottom = 0.88f;

		float Saturation = 0.7f;
		float HoverSaturation = 1.4f;
		float ActiveSaturation = 1.0f;

		// A disabled fill is flat at one weight rather than merely paler, so it reads as unavailable
		// instead of as a lighter value of the same thing. Applied to both ends, which is why it is
		// one number rather than a pair.
		float DisabledOpacity = 0.12f;
		float DisabledSaturation = 0.5f;

		float ShadeStart = 0.25f;
		float ShadeMid = 0.0f;
		float ShadeEnd = 0.02f;
		float ShadeMidPosition = 0.63f;

		// Exponent for both ramps: below 1 fades earlier, 1 is linear, above 1 holds the top.
		float FalloffPower = 0.05f;
	};

	// A toggle is a well, plus the fill recipe when checked. It owns no surface of its own -- which
	// is the point: the toggle is the first proof that recipes compose.
	struct FMixtormatToggleTheme
	{
		float Size = 16.0f;
		float FillInset = 3.0f;

		// How the disabled state is expressed: the well shade deepens by these amounts.
		float DisabledShadeTop = 0.3f;
		float DisabledShadeBottom = 0.12f;
	};

	// Ground, a lift that dissolves into the body at the seam, a Soft Light accent pass over the
	// same domain, then a saturated hairline. `--foldout-*`.
	struct FMixtormatFoldoutTheme
	{
		// The lift's own tint, independently authored in the prototype (`--header-tint-rgb`), and
		// used by exactly one surface.
		//
		// A local field rather than a fifteenth palette role. A role would let a well, a card or a
		// menu claim a colour that only ever means "this foldout header's lift", and the palette
		// stays small only while colours that mean one thing stay local to the one thing that
		// uses them.
		FLinearColor LiftTint;
		// `--header-tint-opacity`. Kept separate from LiftTint's own alpha because the prototype
		// carries them as two tokens, and merging them here would make one of them unauthorable.
		float LiftOpacity;

		// `--header-hover-rgb`. A different hue from the rest lift, not a brighter version of it:
		// the hovered header is the same lip catching the accent, so it lifts in the accent's hue.
		FLinearColor HoverTint;
		// `--header-hover-opacity`
		float HoverTintOpacity;

		FMixtormatFalloff LiftFalloff;

		MixtormatCompositing::EMixtormatBlendMode LiftBlend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		MixtormatCompositing::EMixtormatBlendMode AccentBlend = MixtormatCompositing::EMixtormatBlendMode::SoftLight;

		// `filter: saturate()` on the lift layer, applied per stop. Separate from the lift's
		// opacity because saturating and weighting are different operations.
		float LiftSaturation = 0.6f;
		float HoverSaturation = 1.0f;

		float AccentOpacity = 0.8f;
		float AccentHoverOpacity = 1.0f;

		// --hairline-hover-rgb; the resting line still reads the shared Hairline role.
					FLinearColor HairlineHoverTint;
					float HairlineOpacity = 0.46f;
		float HairlineHoverOpacity = 0.85f;
		float HairlineSaturation = 2.0f;
		float HairlineHoverSaturation = 1.4f;

		float ShadowOpacity = 0.055f;
		float ShadowRange = 12.0f;
		float ShadowFalloffPower = 1.8f;
	};

	// Header and body are one continuous gradient over Ground, not two stacked surfaces: the seam
	// is where the header's contribution has decayed to the body's. `--card-*`.
	struct FMixtormatCardTheme
	{
		MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Additive;

		float HeaderOpacity = 1.0f;
		float BodyOpacity = 0.09f;

		float HeaderSaturation = 2.0f;
		float BodySaturation = 2.2f;

		float FalloffPower = 0.65f;
		// How far into the body the header's contribution reaches.
		float Reach = 0.0f;

		float Radius = 3.0f;
	};

	// Layer rows. A row is Ground plus one body pass whose saturation and strength move with
	// state; group rows add a Soft Light cross pass. Nothing here is a state colour.
	struct FMixtormatLayerTheme
	{
		float Radius = 2.0f;
		MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		MixtormatCompositing::EMixtormatBlendMode GroupBlend = MixtormatCompositing::EMixtormatBlendMode::SoftLight;

		float RestSaturation = 1.3f;
		float HoverSaturation = 1.4f;
		float SelectedSaturation = 2.3f;

		float RestStrength = 1.0f;
		float HoverStrength = 1.0f;
		float SelectedStrength = 1.0f;

		FLinearColor RowBottom;
		FLinearColor HoverTop;
		FLinearColor HoverBottom;
		FLinearColor SelectedTop;
		FLinearColor SelectedBottom;
		FLinearColor ChildLeft;
		FLinearColor ChildRight;
		FLinearColor ChildSelectedLeft;
		FLinearColor ChildSelectedRight;
		FLinearColor Cross;
		FLinearColor HiddenTop;
		FLinearColor HiddenEnd;
		float GroupTintStrength = 0.35f;
		float GroupTintSelectedStrength = 0.55f;
		float ReferenceHiddenTint = 0.08f;
		float ReferenceSelectedTint = 0.38f;
		float ReferenceHoverTint = 0.30f;
		float ReferenceRestTint = 0.22f;
		float InstanceSourceLeftTint = 0.55f;
		float InstanceSourceRightTint = 0.10f;
		float HairlineWidth = 1.0f;
		float HairlineOpacity = 0.46f;

		float GroupSaturation = 1.0f;
		float GroupStrength = 0.5f;

		// Child rows ramp left to right instead of top to bottom.
		float ChildSaturation = 1.2f;
		float ChildHoverSaturation = 2.0f;
		float ChildSelectedSaturation = 1.0f;
		float ChildLeftOpacity = 0.3f;
					float ChildHoverLeftOpacity = 0.72f;
					float ChildSelectedLeftOpacity = 0.9f;
					float ChildStrength = 0.46f;
		float ChildHoverStrength = 1.0f;
		float ChildSelectedStrength = 0.94f;

		FMixtormatGlowLayer ActiveGlow;

		// The selected row's top edge. Separate from the glow because it is crisp rather than soft.
		float ActiveHairlineWidth = 1.0f;
		float ActiveHairlineOpacity = 0.6f;
	};

	// Hierarchy rails are foreground structure: painted after the row gradient, never saturated by
	// it and never multiplied through it, but still behind text and icons.
	struct FMixtormatHierarchyTheme
	{
		FMixtormatColorRef Source;

		float Indent = 28.0f;
		float Width = 1.0f;
		float Opacity = 0.24f;

		// Where a parent's stem meets its first child, and how far a child's arm runs back to it.
		float ParentJoinOffset = 0.0f;
		float ChildArmLength = 8.0f;
	};

	// One visual recipe behind tabs, segmented controls, top-bar actions and compact shared
	// actions. The body blend and the hairline blend are independent on purpose: a button whose
	// body is Normal and whose top edge is Additive is a different thing from one where both are
	// Normal, and collapsing them removes the only control the prototype gives over that.
	struct FMixtormatButtonTheme
	{
		float Height = 24.0f;
		float HorizontalPadding = 0.0f;

		MixtormatCompositing::EMixtormatBlendMode BodyBlend = MixtormatCompositing::EMixtormatBlendMode::Normal;
		MixtormatCompositing::EMixtormatBlendMode HairlineBlend = MixtormatCompositing::EMixtormatBlendMode::Additive;

		float RestTop = 0.18f;
		float RestBottom = 0.03f;

		float HoverTop = 0.32f;
		float HoverBottom = 0.08f;

		float SelectedTop = 0.45f;
		float SelectedBottom = 0.12f;

		float GradientSaturation = 1.5f;

		float HairlineWidth = 1.0f;
		float HairlineOpacity = 0.16f;
		float HairlineHoverOpacity = 0.4f;
		float HairlineSelectedOpacity = 0.6f;
		float HairlineSaturation = 1.5f;

		float SeparatorWidth = 1.0f;
		float SeparatorHeight = 14.0f;
		float SeparatorOpacity = 0.08f;

		// The label's own opacity, so the plate and its text cannot be tuned independently and
		// drift apart.
		float TextOpacity = 0.78f;
	};

	// Menu ground plus an optional top lip, a border, and per-state item rows. Destructive rows
	// keep a normal row's shape and change only hue, so the gesture reads the same as a hover.
	struct FMixtormatMenuTheme
	{
		// Component-local lip source and accent tint; never a global palette role.
		FLinearColor LipSource = FLinearColor::White;
		float LipTintOpacity = 0.1f;
		float BorderOpacity = 0.16f;
		float ItemHoverOpacity = 1.0f;
		float ItemCheckedOpacity = 1.0f;
		float ItemDisabledOpacity = 0.32f;
		FLinearColor DestructiveText = FLinearColor(0.78f, 0.28f, 0.24f, 1.0f);
		FLinearColor DestructiveHover = FLinearColor(0.45f, 0.12f, 0.12f, 1.0f);
		float CornerRadius = 0.0f;
	};

	struct FMixtormatGalleryTheme
	{
		float BorderWidth = 1.0f;
		float BorderOpacity = 0.16f;
		float HoverLiftOpacity = 0.38f;

		float SelectedEdgeWidth = 1.0f;
		float SelectedEdgeOpacity = 1.0f;

		float CornerRadius = 0.0f;
	};

	struct FMixtormatPreviewTheme
	{
		FLinearColor PlateSource = FLinearColor::White;
		float PlateOpacity = 0.85f;
		// Rail button label at rest. The glyph's own rest opacity is the PreviewToolbar icon role's,
		// which is the same authored number; it is not repeated here.
		float IconRestOpacity = 0.45f;
		float HoverAccent = 0.18f;
		float PressAccent = 0.35f;
	};

	// ---- Geometry ---------------------------------------------------------------------------
	//
	// Geometry is never colour or compositing data. These are separate structs so the authoring UI
	// can group them as "Layout" instead of burying a row height among opacities.

	struct FMixtormatControlMetrics
	{
		float RowHeight = 18.0f;
		float RowGap = 3.0f;
		float PairedGap = 3.0f;
		float RowTextInset = 8.0f;
		float RowLabelGap = 6.0f;
		float RowFieldMinWidth = 0.0f;
		float ColorSwatchWidth = 0.0f;
		float ColorSwatchHeight = 0.0f;
		float ButtonHeight = 24.0f;
		float SegmentedControlGap = 0.0f;
		float DropdownLabelRatio = 0.45f;
		float PanelGutter = 1.0f;

		// How far a disabled control's whole surface drops, plate and label together. Applied as
		// one number on the state modifier so a disabled control is the same control at lower
		// strength rather than a separately authored look.
		float DisabledLabelOpacity = 0.32f;

		// Shared geometry previously living only in MixtormatTokens / DesignTokens.
		float CornerRadius = 3.0f;
		float OutlineWidth = 1.0f;
		float IconBrushSize = 20.0f;
		float IconBrushSizeLarge = 28.0f;
		float IconButtonSize = 14.0f;
		float StatusDotSize = 8.0f;
		float ToolbarLabelPadding = 5.0f;

		// Secondary layout metrics migrated from DesignTokens (batch 5).
		float CornerRadiusInner = 1.5f;
		float DraggerTextInset = 8.0f;
		float ButtonPaddingCompact = 7.0f;
		float ButtonPaddingTab = 8.0f;
		float InspectorFeatureButtonGap = 3.0f;
		float DriverPopoverInnerGap = 4.0f;
		float DialogPadding = 12.0f;
		float DialogButtonGap = 6.0f;
		float GroupOuterGap = 3.0f;
		float LayerChildIconSize = 16.0f;
		float MaskPickerWidth = 600.0f;
		float ThumbnailCardPadding = 2.0f;
		float MaskGalleryTileGap = 5.0f;
		float MaskGalleryTileMaximum = 124.0f;
		float ScalarRampIconSize = 13.0f;
		float ScalarRampToolbarHeight = 20.0f;
		float ScalarRampToolbarGap = 3.0f;
		float ScalarRampViewportPadding = 8.0f;
		float ScalarRampIconGap = 2.0f;
		float DragGhostOpacity = 0.93f;
		float DragGhostThumbnailSize = 56.0f;
		float DragGhostPadding = 7.0f;
		float DragGhostShadowInset = 4.0f;
		float DragGhostShadowOffsetY = 2.0f;
	};

	struct FMixtormatFoldoutMetrics
	{
		float Height = 20.0f;
		float Gutter = 9.0f;
		float BodyTop = 5.0f;
		float BodyBottom = 6.0f;
		float OuterTop = 1.0f;
		float OuterBottom = 1.0f;
		float HeaderPaddingTop = 1.0f;
		float HeaderPaddingBottom = 2.0f;
		float Radius = 0.0f;
	};

	struct FMixtormatCardMetrics
	{
		float HeaderHeight = 16.0f;
		float HeaderLeft = 11.0f;
		float HeaderTop = 0.0f;
		float HeaderRight = 8.0f;
		float HeaderBottom = 0.0f;
		float HeaderMarginTop = 1.0f;
		float HeaderMarginBottom = 0.0f;

		float OuterLeft = 0.0f;
		float OuterTop = 2.0f;
		float OuterRight = 0.0f;
		float OuterBottom = 2.0f;

		float BodyHorizontal = 7.0f;
		float BodyTop = 3.0f;
		float BodyBottom = 7.0f;

		float Padding = 7.0f;
		float Gap = 0.0f;
		float TitleHeight = 0.0f;
		float HorizontalPadding = 0.0f;
		float HeaderPaddingLeft = 0.0f;
		float HeaderPaddingTop = 0.0f;
		float HeaderPaddingRight = 0.0f;
		float HeaderPaddingBottom = 0.0f;
	};

	struct FMixtormatLayerMetrics
	{
		float RowHeight = 26.0f;
		float GroupRowHeight = 18.0f;
		float ChildRowHeight = 18.0f;

		float Gap = 2.0f;
		float ColumnGutter = 7.0f;
		float PaddingX = 0.0f;

		float ThumbnailSize = 20.0f;
		float ItemGap = 0.0f;

		float ChildIndent = 28.0f;
	};

	struct FMixtormatMenuMetrics
	{
		float Width = 190.0f;
		float LipHeight = 25.0f;
		float RowHeight = 20.0f;
		float ItemInset = 3.0f;
		float ItemGap = 0.0f;
		float PanelPadding = 3.0f;
		float CaptionInsetAbove = 4.0f;
		float CaptionInsetBelow = 2.0f;
		float SeparatorMargin = 0.0f;
		float ChevronSize = 10.0f;
	};

	struct FMixtormatPreviewMetrics
	{
		// Rail buttons are sized by the PreviewToolbar icon role's ButtonSize, so there is no
		// separate rail size to author: a second copy would be a third number for one plate.
		float OverlayInset = 8.0f;
		float OverlayClusterInset = 2.0f;
		float OverlayLabelGap = 5.0f;
		float ToolbarGap = 5.0f;
		float OverlayButtonGap = 4.0f;
		float ComparisonToggleGap = 4.0f;
		float ResolutionControlWidth = 92.0f;
		float TogglePadding = 2.0f;
		float FinalPopupWidth = 232.0f;
		float LeftRailInset = 4.0f;
		float LeftRailButtonGap = 2.0f;
		float LeftOverlayGap = 8.0f;
		float LeftOverlayWidth = 320.0f;
		float LeftOverlaySurfaceOpacity = 0.82f;
		float QuickControlsCentreGap = 210.0f;
		float QuickControlsRowGap = 22.0f;
		float QuickControlsGuideAxisLength = 176.0f;
		float QuickControlsGuideAxisThickness = 1.0f;
		float QuickControlsGuideAxisOpacity = 0.24f;
		float QuickControlsGuideGlowDiameter = 112.0f;
		float QuickControlsGuideGlowOpacity = 0.019f;
	};

	struct FMixtormatGalleryMetrics
	{
		// The gallery's INITIAL tile size, read once in SMixtormat::Construct. It is a seed, not a
		// live style value: the running zoom lives on the widget and steps 72 -> 144 by 12, and
		// re-reading this every frame would silently discard the user's zoom.
		//
		// Deliberately NOT exposed in UI STYLE. Editing it live would do nothing visible, because the
		// already-running zoom would not follow it -- the worst kind of control. Expose it only if the
		// contract becomes "editing this resets the current gallery zoom", which is a behaviour change
		// and not a styling one.
		float TileSize = 80.0f;

		float TileGap = 5.0f;
		float TilePadding = 5.0f;
		float CaptionHeight = 12.0f;
		float CaptionInset = 4.0f;
		float OverlayInset = 3.0f;
		float HeaderGap = 2.0f;
		float DrawerInset = 8.0f;
		float DrawerInitialHeight = 256.0f;
		float ModeSwitchGap = 4.0f;
		float DrawerSurfaceOpacity = 0.82f;
	};

	struct FMixtormatShellTheme
	{
		FLinearColor SplitterHoverSource = FLinearColor::White;
		float SplitterOpacity = 0.46f;
		float SplitterHoverOpacity = 0.85f;

		float ColumnShadowOpacity = 0.045f;
		float ColumnShadowRange = 14.0f;
		float ColumnShadowFalloffPower = 1.7f;
	};

	struct FMixtormatShellMetrics
	{
		// Layout only. The shell's column ratios are runtime state on SMixtormat (ShellLeftFraction
		// and friends), not theme values: a theme must not be able to resize a panel the user has
		// already arranged, so there is deliberately no authored width or height seed here.
		float TopBarHeight = 38.0f;
		float TopBarActionInset = 0.0f;
		float StatusBarHeight = 24.0f;
		float PanelPadding = 7.0f;
		float ScrollbarThickness = 4.0f;
		float ScrollbarThumbOpacity = 0.22f;
		float ScrollbarHoverOpacity = 0.42f;

		// Visual treatment of a splitter, which *is* styleable even though its behaviour is not.
		float SplitterVisualWidth = 1.0f;
		float SplitterHitWidth = 6.0f;
	};

	// ---- Icons ------------------------------------------------------------------------------
	//
	// Glyph size, button size and hit size are separate because they answer different questions:
	// how big the mark is drawn, how big the plate around it is, and how much of the pointer it
	// claims. Collapsing them is why icons ended up visually mis-sized in some contexts and
	// unreachable in others.

	struct FMixtormatIconStyle
	{
		float GlyphSize = 14.0f;
		float ButtonSize = 0.0f;
		float HitSize = 0.0f;

		float RestOpacity = 0.6f;
		float HoverOpacity = 1.0f;
		float DisabledOpacity = 0.32f;
		float MarkRadius = 0.0f;
		float MarkOutlineWidth = 0.0f;
	};

	enum class EMixtormatIconRole : uint8
	{
		TopBar,
		PanelToolbar,
		PreviewToolbar,
		LayerEye,
		LayerDisclosure,
		FoldoutDisclosure,
		CardLeading,
		Menu,
		GalleryToolbar,
		Count,
	};

	struct FMixtormatIconTheme
	{
		// Indexed by EMixtormatIconRole. A fixed array rather than a map: roles are a closed set,
		// and paint paths should never pay for a hash lookup.
		FMixtormatIconStyle Roles[static_cast<uint8>(EMixtormatIconRole::Count)];
	};

	// ---- Typography -------------------------------------------------------------------------

	// One backend: Unreal's native default composite, selected only by FMixtormatTypography.
	enum class EMixtormatFontFamily : uint8
	{
		NativeDefault,
	};

	// Authored semantic weights, independent of the backend's available faces.
	// FMixtormatTypography maps SemiBold to native Medium in UE 5.8.
	// The enumerators remain dense indices, NOT the CSS weights.
	enum class EMixtormatFontWeight : uint8
	{
		Regular = 0,
		SemiBold = 1,
		Bold = 2,
	};

	// The authored CSS weight, not the native face's weight. Returns 400/600/700.
	inline int32 ToCssWeight(const EMixtormatFontWeight Weight)
	{
		switch (Weight)
		{
		case EMixtormatFontWeight::SemiBold: return 600;
		case EMixtormatFontWeight::Bold: return 700;
		case EMixtormatFontWeight::Regular:
		default: return 400;
		}
	}

	inline const TCHAR* LexToString(const EMixtormatFontWeight Weight)
	{
		switch (Weight)
		{
		case EMixtormatFontWeight::SemiBold: return TEXT("SemiBold");
		case EMixtormatFontWeight::Bold: return TEXT("Bold");
		case EMixtormatFontWeight::Regular:
		default: return TEXT("Regular");
		}
	}

	enum class EMixtormatTextRole : uint8
	{
		Body,
		ControlLabel,
		ControlValue,
		Caption,
		FoldoutTitle,
		CardTitle,
		LayerName,
		LayerSource,
		Menu,
		MenuShortcut,
		GalleryCaption,
		TopBar,
		PreviewLabel,
		Badge,
		Count,
	};

	struct FMixtormatTextSpec
	{
		float Size = 10.0f;
		EMixtormatFontWeight Weight = EMixtormatFontWeight::Regular;

		// Authored in CSS pixels, converted centrally to Slate's 1/1000 em. Copying the raw number
		// across would open a gap too small to see.
		float TrackingPx = 0.0f;
		float Opacity = 1.0f;

		bool bUppercase = false;
		bool bMonospacedNumbers = false;
	};

	struct FMixtormatTypographyTheme
	{
		// Indexed by EMixtormatTextRole, for the same reason the icon roles are.
		FMixtormatTextSpec Roles[static_cast<uint8>(EMixtormatTextRole::Count)];

		EMixtormatFontFamily Family = EMixtormatFontFamily::NativeDefault;
	};

	// ---- Theme ------------------------------------------------------------------------------

	// The complete editable theme. Fields map one-to-one onto UI STYLE tabs.
	struct FMixtormatTheme
	{
		FMixtormatPaletteTheme Palette;

		// -- Controls
		FMixtormatWellTheme Well;
		FMixtormatFillTheme Fill;
		FMixtormatToggleTheme Toggle;
		float SliderTrackHeight = 0.0f;
		float SliderHandleSize = 0.0f;
		float SliderZeroTickOpacity = 0.16f;

		FMixtormatControlMetrics ControlLayout;

		FMixtormatFoldoutTheme Foldout;
		FMixtormatFoldoutMetrics FoldoutLayout;

		FMixtormatCardTheme Card;
		FMixtormatCardMetrics CardLayout;

		FMixtormatLayerTheme Layer;
		FMixtormatHierarchyTheme LayerHierarchy;
		FMixtormatLayerMetrics LayerLayout;

		FMixtormatButtonTheme Button;

		FMixtormatMenuTheme Menu;
		FMixtormatMenuMetrics MenuLayout;

		FMixtormatPreviewTheme Preview;
		FMixtormatPreviewMetrics PreviewLayout;

		FMixtormatGalleryTheme Gallery;
		FMixtormatGalleryMetrics GalleryLayout;

		FMixtormatShellTheme ShellTheme;
		FMixtormatShellMetrics Shell;

		FMixtormatIconTheme Icons;
		FMixtormatTypographyTheme Typography;
	};

	// The authored defaults, seeded from Docs/ui-prototype/tokens.css. This is the only place a
	// default value is written; the reset control in UI STYLE applies these back.
	FMixtormatTheme MakeDefaultTheme();

	// Clamp anything out of range and report what was wrong. Called before every resolve, so a
	// hand-edited or imported theme degrades to the nearest legal value instead of producing a
	// surface nobody can reason about.
	void ValidateTheme(FMixtormatTheme& InOutTheme, TArray<FText>& OutIssues);

	// One dispatcher so no authoring surface spells a role as a string.
	FMixtormatColorRef MakeColorRef(EMixtormatColorRole Role);

	const TCHAR* LexToString(EMixtormatColorRole Role);
}