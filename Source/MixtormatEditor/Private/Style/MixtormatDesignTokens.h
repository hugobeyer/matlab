// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Geometry and typography for the Mixtormat UI, in one place.
//
// This is half of the design system; the other half is the colours and brushes in
// MixtormatStyle.cpp, which have to resolve against FAppStyle at runtime and so cannot live in a
// plain header. Anything that is a number rather than a colour belongs here, and nothing in the
// widgets should hard-code one.
//
// The values are not arbitrary: they are the ones the component exploration was drawn at, which in
// turn were read back out of the shipped style. Changing one here changes it everywhere, which is
// the point -- before this, a row height lived in six literals across three files.
//
// The rule the widgets follow: no measurement is ever written as a literal. A value that is twice
// another is written as that token times two, so the relationship survives an edit to either. The
// only numbers left in a widget are structural rather than designed -- a zero margin, or the 1.0f
// weight of a fill slot, which is a ratio and not a length.
//
// Names say which element the value belongs to, not what size it happens to be. LayerRowInset can
// be retuned for the layer stack alone; a shared Space8 could not.
//
// Scale: component dimensions below follow the authored MixtorMat component sheets directly.
// Keep one-pixel hairlines literal; ratios such as FineDragScale and input thresholds such as
// DragThreshold are interaction values rather than layout lengths.
namespace MixtormatTokens
{
	// ---- Rows -------------------------------------------------------------------------------
	// Every inspector control is one of these tall, whatever it edits. Uniformity across row
	// types is what makes a panel of mixed controls read as a single column.
	inline float RowHeight = 18.0f;
	inline float RowGap = 3.0f;
	// Gap between the two halves of a paired row. Paired controls sit inside one row's slot, so
	// they are a control-layout value rather than a value-stack gap -- which is why this is not
	// RowGap. It used to be derived as RowGap * 2.0f, which held them 6px apart against the
	// authored 3px: two adjacent sliders read as one control until the cursor moved.
	inline float PairedGap = 3.0f;
	constexpr float RowGapTight = 2.0f;
	// A segmented control heading a run of rows takes more air under it than two rows take
	// between them: it is the mode switch everything below is read against, not another
	// value in the same column.
	inline float SegmentedControlGap = 5.0f;

	// Vertical gap between two stacked value rows. Every slider in a panel is spaced by this and
	// nothing else -- it was a literal 2 in AddSliderRow and in forty inline slot paddings, which
	// is why a panel's rows never quite lined up with each other's spacing.
	inline float SliderRowGap = 3.0f;

	// Text inset from the row's leading and trailing edges. Shared by the slider's painted text
	// and by the label of every composed row, so they line up down the column.
	inline float RowTextInset = 5.0f;

	// Gap between a row's label and the control it labels, and the width a paired value field
	// asks for before anything competes with it.
	inline float RowLabelGap = 6.0f;
	inline float RowFieldMinWidth = 120.0f;
	// For a chip that shares its row with other controls rather than owning the trailing slot.
	constexpr float RowFieldMinWidthCompact = 64.0f;
	constexpr float ColorSwatchWidth = 76.0f;
	constexpr float ColorSwatchHeight = 16.0f;
	constexpr float ColorSwatchPadding = 2.0f;

	// ---- Gradients --------------------------------------------------------------------------
	// Samples emitted per span. Slate interpolates its vertex colours in linear space, so the only
	// way to get the CSS curve is to hand it spans short enough that the difference disappears.
	// Twelve is where the banding stops being visible at these sizes; more is wasted vertices on a
	// 17px row.
	constexpr int32 GradientSamplesPerSpan = 12;
	// Where the value fill's shade stops falling and starts holding, from the design's
	// "#00000059, #0000001A 62%, #00000000".
	constexpr float MultiplyMidPosition = 0.62f;

	// ---- Surfaces ---------------------------------------------------------------------------
	inline float CornerRadius = 3.0f;
	// Inner corners -- a segment cell inside a control that is itself rounded. Half the outer
	// radius, so the two curves read as concentric rather than as two unrelated roundings.
	inline float CornerRadiusInner = 1.5f;
	inline float OutlineWidth = 1.0f;

	// Preserve each surface's authored rounding and outline weight independently.
	constexpr float PanelShadowCornerRadius = 7.0f;
	constexpr float InsetPanelCornerRadius = 4.0f;
	constexpr float DragGhostCornerRadius = 6.0f;
	constexpr float ThumbnailBackgroundCornerRadius = 2.0f;
	constexpr float InsetPanelOutlineWidth = 0.45f;
	constexpr float SectionBarOutlineWidth = 0.3f;
	constexpr float DragGhostOutlineWidth = 0.8f;
	constexpr float CompactRowOutlineWidth = 0.25f;
	constexpr float CompactRowValidDropOutlineWidth = 0.65f;
	// Thick enough to read as a deliberate mark between two rows rather than as a hairline the
	// stack already uses for its own seams.
	constexpr float DropInsertionLineThickness = 2.0f;
	// How much of a group header means "into this group" rather than either side of it. The
	// middle half, so the two edges stay easy to hit without the centre becoming a sliver.
	constexpr float GroupRowIntoZoneFraction = 0.25f;
	constexpr float CompactRowHoverOutlineWidth = 0.35f;
	constexpr float CompactRowPressedOutlineWidth = 0.5f;
	constexpr float CompactRowDisabledOutlineWidth = 0.2f;
	constexpr float PrimaryButtonOutlineWidth = 0.35f;
	constexpr float PrimaryButtonHoverOutlineWidth = 0.5f;
	constexpr float PrimaryButtonPressedOutlineWidth = 0.65f;
	constexpr float PrimaryButtonDisabledOutlineWidth = 0.25f;
	constexpr float ActiveTabOutlineWidth = 0.45f;
	constexpr float ActiveTabHoverOutlineWidth = 0.55f;
	constexpr float ActiveTabPressedOutlineWidth = 0.65f;
	constexpr float ScrubControlOutlineWidth = 0.25f;
	constexpr float ScrubControlHoverOutlineWidth = 0.35f;
	constexpr float ScrubControlActiveOutlineWidth = 0.4f;
	// Left and right inset of a group's contents from its edges. Wider than the old 4: a slider
	// runs the full width of the body, so this is the only thing between the trough and the edge
	// of the panel.
	inline float PanelGutter = 7.0f;
	inline float GroupHeaderHeight = 25.0f;
	inline float ButtonHeight = 20.0f;
	inline float ButtonPaddingCompact = 7.0f;
	inline float ButtonPaddingPrimary = 10.0f;
	inline float ButtonPaddingTab = 8.0f;
	constexpr float ButtonPressedOffset = 1.0f;
	constexpr float ThumbnailCardPadding = 2.0f;
	constexpr float CompactRowButtonPaddingHorizontal = 4.0f;
	constexpr float CompactRowButtonPaddingVertical = 1.0f;
	constexpr float ScrubControlTextInset = 3.0f;
	inline float TabHeight = 20.0f;
	inline float TabWidth = 88.0f;
	inline float TabUnderlineThickness = 1.0f;
	constexpr float TabLabelBottomInset = 0.0f;
	inline float ToolbarIconSize = 18.0f;
		// Panel toolbars (library search row, Add-layer strip, inspector toolbar) are a separate role:
		// the prototype authors --toolbar-icon-size independently of --topbar-icon-size, and the top
		// bar's 18px glyphs are far too loud inside a dense panel toolbar.
		inline float PanelToolbarIconSize = 14.0f;
		// Resting opacity for top-bar action glyphs (.top-actions .asset-icon).
		inline float TopBarIconOpacity = 0.6f;
		// A shared action's horizontal padding. components.css gives every button `padding: 0 8px`,
		// which is not the panel gutter the actions were inheriting.
		inline float GroupButtonPaddingHorizontal = 8.0f;
	inline float ViewportOverlayInset = 8.0f;
	constexpr float PreviewToolbarButtonSize = 24.0f;
	// Inner inset of a viewport overlay cluster -- the gap between its gradient plate and the
	// controls inside it.
	inline float ViewportOverlayClusterInset = 2.0f;
	// What the overlay toggle style insets its content by on every side.
	constexpr float ViewportOverlayTogglePadding = 2.0f;
	// The glyph inside a viewport overlay button, stated rather than inherited.
	//
	// The four lighting toggles have always drawn at exactly this -- the button size less the
	// toggle style's padding -- because their brushes happen to be registered at the same number.
	// The combo and plain buttons beside them inherited their own styles' padding instead and came
	// out distorted, so every glyph in that rail is now sized to this explicitly and none of them
	// depends on what its container does.
	constexpr float PreviewToolbarIconSize =
		PreviewToolbarButtonSize - ViewportOverlayTogglePadding * 2.0f;
	// Between two controls sitting side by side within one overlay cluster.
	inline float ViewportOverlayItemGap = 5.0f;
	// Between stacked buttons in an overlay rail. The buttons carry no plate of their own, so
	// without a gap the glyphs are the only thing separating one target from the next.
	inline float ViewportOverlayButtonGap = 4.0f;
	// The composition-resolution segments. Fixed rather than hugging, so the cluster's width does
	// not change when the active label goes from 1K to 4K.
	constexpr float PreviewResolutionControlWidth = 92.0f;
	// Breathing room under a group header before its first row.
	constexpr float GroupBodyTopInset = 4.0f;
	// Gap around a group inside the well, so the darker surround reads as a margin and the
	// group's rounded corners have something to be rounded against. Groups used to stack flush
	// and the column read as one undifferentiated sheet.
	inline float GroupOuterGap = 3.0f;
	// Gap between a header's chevron, its title, and the controls trailing it.
	inline float GroupHeaderItemGap = 5.0f;

	// ---- Sub-grouping -----------------------------------------------------------------------
	// A caption names a run of rows; a hairline separates two runs without naming them. The
	// caption costs more height, so it is for groupings the labels do not already imply.
	constexpr float CaptionHeightAbove = 6.0f;
	constexpr float CaptionHeightBelow = 2.0f;
	constexpr float HairlineThickness = 1.0f;
	constexpr float HairlineMargin = 2.0f;

	// ---- Slider -----------------------------------------------------------------------------
	// Leading stripe marking a value that differs from its default.
	// Width and intensity of the leading stripe marking a value that differs from its default.
		// The prototype authors these separately because the weight is a geometry decision and the
		// intensity is a paint one, and it is free to do so -- it has no persisted themes to break.
		inline float ModifiedStripeWidth = 2.0f;
		inline float ModifiedStripeOpacity = 0.8f;
	// The label shifts right by this much when the stripe is showing, so text never sits on it.
	constexpr float ModifiedLabelInset = 5.0f;
	// ---- Well --------------------------------------------------------------------------------
	// The recess every control sits in. Its three authored parts, in the order they composite:
	//
	//   1. Ground        the surface's own colour, not a panel shade. A card body and a panel column
	//                    are both painted off ground, so aliasing this to Panel would make a 9%
	//                    body mean two different things.
	//   2. Shade         a black multiply ramp, 0.64 at the top to 0.13 at the bottom. That top
	//                    value is far heavier than anything the old flat trough carried, which is
	//                    what turns the trough from "a darker rectangle" into a recess.
	//   3. Border        a 1px outline whose alpha *falls* from top to bottom, so the rim reads as
	//                    catching light along the top edge and disappearing at the foot.
	//
	// The border's two endpoints are separate tokens rather than one opacity times a flat
	// multiplier. The prototype masks the outline with a vertical gradient, and a single flat alpha
	// cannot express that -- it was the reason every well read as evenly outlined all the way round.
	inline float WellShadeTop = 0.64f;
	inline float WellShadeBottom = 0.13f;
	inline float WellBorderWidth = 1.0f;
	// The outline's overall weight, and its intensity at the top and bottom edges. The endpoints
	// are multiplied into this at paint time, so all three stay independently tunable.
	inline float WellBorderOpacity = 0.86f;
	inline float WellBorderTopOpacity = 0.33f;
	inline float WellBorderBottomOpacity = 0.11f;
	// Hover keeps the same falloff shape and lifts both ends.
	inline float WellBorderHoverOpacity = 0.47f;
	inline float WellBorderHoverTopOpacity = 0.78f;
	inline float WellBorderHoverBottomOpacity = 0.44f;
	// The border is saturated before it is composited. The prototype reaches this through a filter
	// on the border layer alone, which Slate has no equivalent for; applying it per paint layer is
	// the same operation done somewhere Slate can express it.
	inline float WellBorderSaturation = 2.0f;
	// How much the ground lifts on hover. The prototype inset-shadows a translucent ground over
	// itself, which reads as the surface brightening rather than as a highlight being drawn.
	inline float WellHoverLiftOpacity = 0.38f;
	// The prototype's wells are square. This is its own token rather than the global CornerRadius,
	// which other surfaces read and which stays at its current value.
	inline float WellRadius = 0.0f;
	// The centre mark on a range that spans zero, drawn in ground over the fill. The old value was
	// derived by multiplying the well outline down twice; it is authored here directly.
	inline float ZeroTickOpacity = 0.16f;

	// ---- Control fill ----------------------------------------------------------------------
	// The value fill is the accent added over the well, at authored opacity -- not a separate flat
	// grey family. Every number below is a percentage of the accent, which is what lets one colour
	// serve the rest, hover, active and disabled states at four different strengths.
	inline float FillBodyTop = 0.45f;
	inline float FillBodyBottom = 0.16f;
	inline float FillBodyHoverTop = 0.84f;
	inline float FillBodyHoverBottom = 0.46f;
	// Active inverts the ramp: brighter at the bottom than the top, because the fill is being
	// dragged and reads as lit from below.
	inline float FillBodyActiveTop = 0.63f;
	inline float FillBodyActiveBottom = 0.88f;
	inline float FillDisabledOpacity = 0.12f;
	// The horizontal darkening pass over the fill. Separate from the body because it is a multiply
	// against the same ground, and one background list cannot mix two blend modes.
	inline float FillShadeStart = 0.25f;
	inline float FillShadeMid = 0.0f;
	inline float FillShadeEnd = 0.02f;
	inline float FillShadeMidPosition = 0.63f;
	// Exponent on the vertical body ramp. 0.05 is a very shallow curve: the fill holds close to its
	// top value and drops late, so the bar reads as a lit surface rather than as a fade.
	inline float FillFalloffPower = 0.05f;
	// Saturation per paint layer, never applied to Accent() itself -- the accent is also used
	// unsaturated elsewhere, and the toggle fill borrows the same numbers at the same values.
	inline float FillSaturation = 0.7f;
	inline float FillHoverSaturation = 1.4f;
	inline float FillActiveSaturation = 1.0f;
	inline float FillDisabledSaturation = 0.5f;

	// ---- Control text -----------------------------------------------------------------------
	inline float ControlLabelOpacity = 0.75f;
	inline float ControlValueOpacity = 0.9f;
	inline float TextDisabledOpacity = 0.32f;

	// ---- Toggle ----------------------------------------------------------------------------
	inline float ToggleDisabledShadeTop = 0.3f;
	inline float ToggleDisabledShadeBottom = 0.12f;

	// ---- Foldout ----------------------------------------------------------------------------
		// The collapsible group header. Its geometry is authored separately from the generic group
		// header it used to borrow, because the two are not the same control: a foldout is flush with
		// the body it opens and has no surrounding margin, and its header padding adds to its height
		// rather than being eaten by it.
		inline float FoldoutHeight = 20.0f;
		inline float FoldoutGutter = 9.0f;
		inline float FoldoutBodyTop = 5.0f;
		inline float FoldoutBodyBottom = 6.0f;
		inline float FoldoutOuterTop = 1.0f;
		inline float FoldoutOuterBottom = 1.0f;
		inline float FoldoutHeaderPaddingTop = 1.0f;
		inline float FoldoutHeaderPaddingBottom = 2.0f;
		// The prototype rounds nothing but cards, so a foldout is square. Its own token rather than
		// the global CornerRadius, which several unrelated surfaces still read.
		inline float FoldoutRadius = 0.0f;

		// The header's additive lift: a tint added over the ground, reaching zero at the body seam so
		// the header dissolves into the body rather than ending at it.
		inline float HeaderTintOpacity = 0.9f;
		inline float HeaderHoverOpacity = 0.85f;
		inline float HairlineHoverOpacity = 0.85f;
		// Exponent on both the lift and the accent cross pass. Below 1 fades earlier, 1 is linear,
		// above 1 holds the top longer. The prototype authors 0.95, which is nearly linear.
		inline float FoldoutFalloffPower = 0.95f;

		// A saturated accent over the lift, fading to nothing at the same seam. This is the soft-light
		// layer in the prototype: it darkens and saturates the top edge rather than adding light to
		// it, which is why it has to be its own element rather than part of the lift.
		inline float FoldoutAccentMultiplyOpacity = 0.8f;
		inline float FoldoutAccentHoverMultiplyOpacity = 1.0f;

		// The hairline is its own one-pixel layer at the header's top edge, not the foldout's outline.
		// Its intensity and its saturation are separate: the prototype saturates the line itself,
		// which is what makes a near-neutral grey read as a lit edge.
		inline float FoldoutHairlineOpacity = 0.46f;
		inline float FoldoutHairlineSaturation = 2.0f;
		inline float FoldoutHairlineHoverSaturation = 1.4f;

		// Saturation of the header's own paint layer. Applied per layer rather than to the palette
		// role, because the tint role is also read by surfaces that are not saturated this way.
		inline float FoldoutSaturation = 0.6f;
		inline float FoldoutHoverSaturation = 1.0f;

		// The chevron is a separate role, not ChevronSize. ChevronSize is shared with the chip, the menu
		// item, the layer group row and the child-output preview, so using it here would resize five
		// callers whenever the foldout's glyph changes.
		inline float FoldoutIconSize = 10.0f;
		// Padding around that glyph inside its own hit box -- an interaction affordance, not a row gap.
		// The prototype's disclosure is a 16px box holding a 10px glyph; it is NOT a leading offset on
		// the row, which is what an earlier revision of this wrongly used it for.
		inline float FoldoutIconPadding = 3.0f;
				// Retained for saved LiveTheme compatibility; no longer used as vertical slot padding.
				inline float FoldoutChevronInset = 3.0f;
		// The summary's gap between chevron, title and trailing actions.
		inline float FoldoutHeaderGap = 7.0f;

		// ---- Control type ----------------------------------------------------------------------
	// The label and the value are two roles, not one at two weights. The label is quiet context
	// beside the number; the value keeps more of its own presence. Driving both from a single size
	// meant neither could be tuned without moving the other.
	inline float FontControlLabel = 10.0f;
	inline float FontControlValue = 10.0f;
	// Binary weight switches. These are booleans, not numbers: a weight is Regular or Bold, and
	// editing it on a 0..1 slider made a switch read as a quantity. The underlying face selection is
	// still a real Regular/Bold pair, because that is all the shipped family resolves.
	inline bool ControlLabelBold = false;
	inline bool ControlValueBold = true;
	// Authored in CSS px. Slate's LetterSpacing is in 1/1000 em, so a px value has to be converted
	// against the font size rather than copied: 0px here is 0 in both, but any future non-zero
	// value is not the same number in the two units.
	inline float ControlLabelLetterSpacing = 0.0f;

		// Foldout titles are their own type role. They were reading the compact Group Header tier,
		// which is 7px tracked caps -- a caption's role, not a section heading's. The prototype's own
		// values are a larger size, a normal weight and 1px of tracking.
		inline float FontFoldoutTitle = 9.0f;
		inline bool FoldoutTitleBold = false;
		// Authored in CSS px, like every tracking value here. Converted against FontFoldoutTitle when
		// it reaches FSlateFontInfo, because Slate's LetterSpacing is in 1/1000 em: 1px at a 9px face
		// is 111, not 1. Copying the number across would open a 1/1000 em gap and read as none at all.
		inline float FoldoutTitleTracking = 1.0f;
		inline float FoldoutTitleOpacity = 0.74f;
		inline float FoldoutTitleDisabledOpacity = 0.32f;

	// ---- Slider ----------------------------------------------------------------------------
	inline float TickInsetY = 4.0f;
	inline float TickWidth = 1.0f;
	// Narrower than this and the painted fill is a sliver rather than a bar, so it is skipped --
	// a half pixel of colour reads as a rendering artefact, not as a value near zero.
	constexpr float MinPaintedFill = 0.5f;
	// Drag-rate multipliers: Shift, and Ctrl+Shift. The base rate sweeps the range over the row's
	// own width, so the fill follows the cursor.
	inline float FineDragScale = 0.1f;
	constexpr float FinestDragScale = 0.01f;
	// Pixels from the work-area edge at which a drag wraps the cursor to the opposite edge.
	constexpr float DragWrapMargin = 2.0f;
	// Not read by the slider any more.
	inline float DragRangeDistance = 320.0f;
	// Pixels of travel before a press becomes a scrub rather than a click-to-type.
	inline float DragThreshold = 4.0f;
	// Centre tick on a range that spans zero: TickInsetY, TickWidth and ZeroTickOpacity live in
	// the Well block above, with the rest of the control's own paint values.

	// ---- Segmented control ------------------------------------------------------------------
	inline float SegmentHeight = 18.0f;
	// Hairline *between* cells -- the one border the design allows, because it divides rather
	// than encloses.
	constexpr float SegmentSeamWidth = 1.0f;
	// Multiply pass darkening the trailing edge of an active cell.
	constexpr float SegmentShadeAlpha = 0.1f;

	// ---- Icons ------------------------------------------------------------------------------
	// Sized per role, not per pixel budget: the eye is the only thing in a layer row a user aims
	// at, so it is the largest; a disclosure chevron is read, not clicked, and stays small.
	inline float IconButtonSize = 14.0f;
	// Added around an icon button's glyph, not to it: the box that takes the click grows by this
	// while the drawn icon stays IconButtonSize. It is the *total* growth, not a per-side value --
	// the box is computed once as GlyphSize + this, so a per-side 2.5px of padding is 5 here. At
	// IconButtonSize 14 that makes the target 19px against the ~24px a pointer reliably hits.
	inline float IconButtonHitSlop = 5.0f;
	inline float ChevronSize = 14.0f;
	constexpr float StatusDotSize = 8.0f;

	// The size an SVG is *registered* at, which is not the size anything displays it at -- the box
	// holding the brush scales it down. Registering small and scaling up is what makes a glyph
	// look soft, so these stay at or above the largest place each icon appears.
	constexpr float IconBrushSize = 20.0f;
	// Menu and toolbar glyphs, which sit alone rather than inside a dense row.
	constexpr float IconBrushSizeLarge = 28.0f;

	// ---- Scalar Ramp / Curve Editor -----------------------------------------------------------
	inline float ScalarRampHeight = 112.0f;
	inline float ScalarRampCurveThickness = 1.6f;
	inline float ScalarRampGridThickness = 0.7f;
	inline float ScalarRampMajorGridThickness = 1.2f;
	inline float ScalarRampPointSize = 7.0f;
	inline float ScalarRampIconSize = 13.0f;
	inline float ScalarRampIconGap = 2.0f;
	inline float ScalarRampToolbarGap = 3.0f;
	inline float ScalarRampToolbarGroupGap = 8.0f;
	inline float ScalarRampToolbarHeight = 20.0f;
	inline float ScalarRampViewportPadding = 8.0f;

	// ---- Color Ramp ---------------------------------------------------------------------------
	// The colour ramp is 1D (X + colour): the gradient bar and its handles are all it draws, so it
	// sits at roughly half the scalar ramp's height, which reserves room for the curve itself.
	inline float ColorRampHeight = 56.0f;

	// ---- Blend modes -----------------------------------------------------------------------
	// The eight `*-blend-mode` tokens the prototype authors, as indices into
	// MixtormatCompositing::EMixtormatBlendMode: 0 Normal, 1 Additive (plus-lighter), 2 Multiply,
	// 3 SoftLight. Defaults are the authored CSS values, not a house style.
	//
	// Stored as int32 because the live-theme registry points at them directly and because the paint
	// path should never compare a string; BlendModeOf is the typed read at each call site.
	inline int32 SurfaceBlendMode = 1;
	inline int32 WellBlendMode = 2;
	inline int32 FoldoutBlendMode = 0;
	inline int32 FoldoutAccentBlendMode = 3;
	inline int32 CardBlendMode = 1;
	inline int32 GroupButtonBlendMode = 0;
	inline int32 LayerBlendMode = 0;
	inline int32 LayerGroupBlendMode = 3;

	// ---- Discrete choices -----------------------------------------------------------------
	// Index into the matching Options list in the legacy registry.
	// 0 Unreal Default; display metadata only, not a backend selector.
	inline int32 FontFamily = 0;
	// 0 none, 1 uppercase (dragger-label-case in the prototype).
	inline int32 DraggerLabelCase = 0;

	// ---- Brand ------------------------------------------------------------------------------
	// The mark's own proportions, so none of these derive from anything else.
	constexpr float BrandIconWidth = 20.0f;
	constexpr float BrandIconHeight = 21.0f;
	constexpr float BrandLogoWidth = 123.0f;
	constexpr float BrandLogoHeight = 24.0f;
	constexpr float BrandWatermarkWidth = 61.0f;
	constexpr float BrandWatermarkHeight = 67.0f;

	// ---- Menus and popovers -----------------------------------------------------------------
	// A menu is its own window: it is not clipped by the panel that opened it, and it is the only
	// surface besides the drag ghost that carries a drop shadow. Dimensions follow the authored
	// context-menu component directly.
	inline float MenuWidth = 190.0f;
	inline float MenuItemHeight = 20.0f;
	constexpr float MenuItemInset = 8.0f;
	constexpr float MenuItemGap = 6.0f;
	inline float MenuPanelPadding = 3.0f;
	constexpr float MenuCaptionInsetAbove = 6.0f;
	constexpr float MenuCaptionInsetBelow = 3.0f;
	constexpr float MenuSeparatorMargin = 4.0f;
	inline float MenuIconSize = 14.0f;
		inline float MenuIconOpacity = 0.6f;
		inline float HelpMaxWidth = 310.0f;
		inline float HelpPadding = 9.0f;
			inline float HelpBodyOpacity = 0.7f;
		// Slate active-timer intervals are seconds; prototype help-delay is 350ms.
		inline float HelpDelay = 0.35f;
	constexpr float MenuCornerRadius = 3.0f;
	// Where the menu's tint has landed on its ground. The canvas puts this at a fixed 22px rather
	// than a fraction, so a tall menu and a short one have the same lip rather than the same ramp.
	inline float MenuLipHeight = 25.0f;

	// ---- Parameter references / drivers ------------------------------------------------------
	// The driver state button lives beside an existing inspector control rather than changing
	// that control's own chrome. The popover uses the same MenuPanel ground as every other popup.
	constexpr float ParameterStateGap = 4.0f;
	constexpr float ParameterStateSlotWidth = 12.0f;
	constexpr float DriverPopoverWidth = 224.0f;
	constexpr float DriverPopoverInnerGap = 4.0f;
	constexpr float DriverPopoverSectionGap = 6.0f;

	// ---- Drag ghost -------------------------------------------------------------------------
	// The card that follows the cursor during a drag. It floats over the whole editor rather than
	// sitting in a panel, so it is the one surface in the tool that carries a drop shadow -- and
	// the shadow is offset down and right, which is what reads as "lifted" rather than "outlined".
	constexpr float DragGhostOpacity = 0.93f;
	constexpr float DragGhostThumbnailSize = 56.0f;
	// Render resolution is separate from the decorator's displayed thumbnail size.
	constexpr int32 DragGhostThumbnailResolution = 40;
	constexpr float DragGhostPadding = 7.0f;
	constexpr float DragGhostTextGap = 9.0f;
	constexpr float DragGhostShadowInset = 4.0f;
	constexpr float DragGhostShadowOffsetX = 4.0f;
	constexpr float DragGhostShadowOffsetY = 5.0f;

	// ---- Badge ------------------------------------------------------------------------------
	// Fixed width, not hugging its text: the badges form a column down the right edge, and the
	// word changes without the column moving.
	inline float BadgeWidth = 44.0f;
	// Matches the badge's old flat brush radius.
	inline float BadgeCornerRadius = 1.0f;
	constexpr float BadgeHeight = 16.0f;
	// Longest word a badge is allowed to carry, and what BadgeWidth is sized for. The box does not
	// grow to fit its text -- that is the point, the marks have to form a straight column -- so a
	// longer word clips instead of widening, and the derivation tables are written against this.
	constexpr int32 BadgeMaxCharacters = 6;
	// Horizontal breathing room inside the fixed box, between the glyph and the edge it clips
	// against -- text was sitting flush on the box's own bounds.
	constexpr float BadgeTextInset = 3.0f;

	// ---- Thumbnails -------------------------------------------------------------------------
	// One tile widget serves the library, the mask replacement grid and the mask picker; only the
	// size differs. The name strip is an overlay, so it costs image rather than layout height.
	constexpr float SurfaceTileSize = 90.0f;
	constexpr float SurfaceTileSizeDense = 68.0f;
	constexpr float MaskTileSize = 96.0f;
	constexpr float MaskPickerTileSize = 76.0f;
	constexpr float MaskPickerTileSizeDense = 52.0f;

	constexpr float MaskGalleryTileMinimum = 52.0f;
	constexpr float MaskGalleryTileMaximum = 124.0f;
	constexpr float MaskGalleryTileStep = 12.0f;
	inline float MaskGalleryTileGap = 5.0f;
	constexpr float TileGap = 4.0f;
	constexpr float MaterialGalleryTileDefault = 96.0f;
	constexpr float MaterialGalleryTileMinimum = 72.0f;
	constexpr float MaterialGalleryTileMaximum = 144.0f;
	constexpr float MaterialGalleryTileStep = 12.0f;
	constexpr float MaterialGalleryTileGap = 5.0f;
	constexpr float MaterialGalleryTilePadding = 5.0f;
	constexpr float MaterialGalleryHeaderGap = 2.0f;
	constexpr float TileNameStripHeight = 12.0f;
	constexpr float TileBadgeHeight = 16.0f;
	// Two different insets: one sits on the picture, the other outside it between the image and
	// its selection outline.
	constexpr float TileTextInset = 4.0f;
	constexpr float TileImageInset = 3.0f;

	// ---- Card -------------------------------------------------------------------------------
	// A titled sub-panel inside a group body: one run of related values, on its own sheet.
	//
	// The group body is the same colour as the column it sits in now, so the card is what
	// separates one run of rows from the next -- the body is ground, the cards are the things
	// standing on it.
	//
	// Padding is deliberately smaller than PanelGutter. The body has already inset by that much,
	// and a card's own gutter stacks on top of it; matching the two would indent every slider
	// twice and leave the column looking margin-heavy at the inspector's width.
	inline float CardPadding = 4.0f;
	inline float CardGap = 10.0f;
	// Between a card's title and the sheet under it.
	inline float CardTitleGap = 3.0f;

	// Inspector-column cards use a rounded sheet with proportional title/action slots.
	// Popovers keep the compact layout and its existing CardPadding/CardTitleGap values.
	inline float GroupCardHeaderOpacity = 1.0f;
	inline float GroupCardBodyOpacity = 0.09f;
			inline float GroupCardRadius = 3.0f;
			inline float GroupCardHeaderMarginTop = 1.0f;
			inline float GroupCardHeaderMarginBottom = 0.0f;
			inline float GroupCardFalloffPower = 0.65f;
			inline float GroupCardGradientReach = 0.0f;
			inline float GroupCardHeaderSaturation = 2.0f;
			inline float GroupCardBodySaturation = 2.2f;
			inline float GroupCardTitleOpacity = 0.75f;
			inline float GroupCardTitleDisabledOpacity = 0.32f;
	inline float GroupCardHorizontalPadding = 7.0f;
	inline float GroupCardHeaderPaddingLeft = 11.0f;
	inline float GroupCardHeaderPaddingTop = 0.0f;
	inline float GroupCardHeaderPaddingRight = 8.0f;
	inline float GroupCardHeaderPaddingBottom = 0.0f;
	inline float GroupCardOuterMarginLeft = 0.0f;
	inline float GroupCardOuterMarginTop = 2.0f;
	inline float GroupCardOuterMarginRight = 0.0f;
	inline float GroupCardOuterMarginBottom = 2.0f;
	inline float GroupCardContentPaddingTop = 3.0f;
	inline float GroupCardContentPaddingBottom = 7.0f;
	inline float GroupCardTitleHeight = 16.0f;
	// Retained for theme compatibility; inspector cards no longer use a title drop.
	inline float GroupCardTitleDropDepth = 11.0f;
	inline float GroupCardTitleWidthRatio = 0.5f;
	inline float GroupCardLeadingIconSize = 12.0f;
	inline float GroupCardLeadingGap = 10.0f;
	inline float DropdownLabelRatio = 0.35f;
	// Horizontal inset of a value row's text from its edges. The prototype gives a row 8px; the old
	// 10 was half a control's width pushed in, which is why a row's label sat visibly off-centre
	// against the chip beside it.
	inline float DraggerTextInset = 8.0f;

	// The breathing room under any heading -- a card title, a group header -- and again at the
	// bottom of what it heads. Small on purpose: it is there so a run of rows is not flush
	// against the edge of the thing containing it, not to space the rows out.
	inline float HeaderContentGap = 2.0f;

	// The column's own top margin, above the first group. The panel header sits directly over
	// it and without this the first group reads as attached to that bar rather than as the
	// first thing in the column.
	inline float InspectorTopMargin = 8.0f;
	// Viewport rail buttons: accent added to the plate on hover / press, and the icon's opacity
	// at rest (full on hover).
	inline float OverlayHoverAccent = 0.18f;
	inline float OverlayPressAccent = 0.35f;
	inline float OverlayIconRestOpacity = 0.45f;
	// Header text alignment: 0 left, 1 centre, 2 right. Text only -- the foldout chevron, state,
	// action and reset keep their places. Groups are the collapsible bars; subgroups are card
	// titles and row captions inside them.
	inline float GroupHeaderAlign = 0.0f;
	inline float SubgroupHeaderAlign = 0.0f;
	// Thin separators inside the inspector, inset on both sides so they never reach the panel
	// edges. Thickness 0 turns them all off; the two switches (0/1) pick where they appear.
	inline float InspectorHairlineThickness = 1.0f;
	inline float InspectorHairlineInset = 12.0f;
	inline float InspectorHairlineUnderHeader = 0.0f;

	inline float InspectorMaskGalleryMaxHeight = 420.0f;
	inline float InspectorFeatureButtonGap = 3.0f;
	inline float InspectorColorSwatchWidth = 108.0f;
	inline float InspectorColorSwatchHeight = 18.0f;

	// ---- Shell and dialogs ------------------------------------------------------------------
	inline float PanelPadding = 7.0f;
	inline float SplitterHandleSize = 1.0f;
	inline float SplitterHitSize = 6.0f;
	inline float LayerStackWidth = 423.0f;
	inline float InspectorWidth = 520.0f;
	// The floating overlay's corner grip, and the floor a drag can shrink it to. The grip is small
	// enough to stay out of the content's way and still be a target.
	inline float InspectorOverlayGripSize = 14.0f;
	inline float InspectorOverlayMinWidth = 260.0f;
	inline float InspectorOverlayMinHeight = 160.0f;
	inline float TopBarHeight = 38.0f;
	inline float StatusBarHeight = 24.0f;
	inline float BottomLibraryCollapseButtonWidth = 120.0f;
	inline float BottomLibraryCollapseButtonHeight = 24.0f;
	// The mask gallery's starting tile size, and only that -- despite the name, nothing draws a
	// mask "bar" any more. Zoom lands on 52 + 12n (MaskGalleryTileMinimum + n * step), so a
	// default on that grid keeps every step after it on the same one.
	inline float MaskBarTileSize = 88.0f;
	// How far a group's row leans toward its accent colour. Low on purpose: the colour is there to
	// tell two groups apart at a glance, not to become the row.
	inline float GroupAccentStrength = 0.35f;
	inline float GroupAccentSelectedStrength = 0.55f;
	// Alpha of the group header's left-edge cross pass. Present whether or not the group carries
	// an accent -- it is what makes a group row read as a group.
	inline float GroupRowCrossStrength = 0.5f;
	inline float GroupAccentSwatchSize = 18.0f;
	inline float GroupAccentSwatchGap = 3.0f;
	inline float ToolbarButtonMargin = 2.0f;
	inline float ToolbarLabelPadding = 5.0f;
	constexpr float LibraryBrowseButtonGap = 4.0f;
	constexpr float PreviewComparisonToggleGap = 4.0f;
	inline float DialogPadding = 12.0f;
	inline float DialogButtonGap = 6.0f;
	inline float DialogActionsTopMargin = 10.0f;
	constexpr float ActionDialogWidth = 560.0f;
	constexpr float ActionDialogHeight = 320.0f;
	constexpr float BakeDialogBrowseButtonGap = 4.0f;
	constexpr float BakeDialogFieldTopMargin = 4.0f;
	constexpr float BakeDialogFieldBottomMargin = 10.0f;
	constexpr float BakeDialogSectionGap = 8.0f;
	constexpr float BakeDialogSettingLabelWidth = 90.0f;

	// ---- Toggle -----------------------------------------------------------------------------
	// A square well that fills rather than marking itself with a glyph. A check or a cross is a
	// second shape to read at 16px and neither survives the size; a filled box is legible as a
	// state at a glance, and it is the same well-and-fill vocabulary the sliders and chips use.
	inline float ToggleSize = 16.0f;
	// Inset of the fill from the well that holds it, on every side. The design gives the well its
	// own 1px border and the fill its own 3px inset, so the rim and the fill's margin are separate
	// decisions rather than one derived from the other.
	inline float ToggleFillInset = 3.0f;
	// Derived, not constexpr: the size and the inset are independently editable now, so the fill's
	// box cannot be a compile-time constant derived from them. RecomputeDerived keeps it in step.
	inline float ToggleFillSize = ToggleSize - ToggleFillInset * 2.0f;

	// A chip's inline thumbnail -- enough to confirm which asset is bound without opening the
	// picker, since the row already carries the name.
	constexpr float ChipThumbnailSize = 16.0f;
	constexpr float ChipHeight = 21.0f;
	constexpr float ChipGap = 5.0f;
	constexpr float ChipTextInset = 8.0f;

	// ---- Mask picker popover ----------------------------------------------------------------
	// Wider than the 300px inspector on purpose: a menu is its own window and is not clipped by
	// the panel that opened it.
	inline float MaskPickerWidth = 600.0f;
	// How tall the picker is allowed to get before it scrolls. A menu that runs past the panel it
	// opened from is worse than one that scrolls, because the entries under the cursor move when
	// the popup is repositioned to fit.
	inline float MaskPickerMaxHeight = 800.0f;
	// The plain option lists -- height source, height reference. Narrow next to the mask grid
	// because they are lines of text rather than thumbnails, and wide enough that the longest
	// option does not wrap.
	constexpr float OptionMenuWidth = 260.0f;

	constexpr int32 MaskPickerColumns = 4;
	constexpr int32 MaskPickerColumnsDense = 5;

	// ---- Layer stack ------------------------------------------------------------------------
	// The stack lives under the preview where vertical space is scarce, so these are the tightest
	// rows in the tool. Each value below is a separate decision -- a layer and its children are
	// deliberately different heights, and the indent is what carries the hierarchy now that no
	// connector rail is drawn between them.
	inline float LayerRowHeight = 26.0f;
			inline float LayerSaturation = 1.3f;
			inline float LayerHoverSaturation = 1.4f;
			inline float LayerSelectedSaturation = 2.3f;
			inline float LayerGroupSaturation = 1.0f;
			inline float ChildSaturation = 1.2f;
			inline float ChildHoverSaturation = 2.0f;
			inline float ChildSelectedSaturation = 1.0f;
			inline float ChildLeftOpacity = 0.3f;
			inline float ChildRightOpacity = 0.46f;
			inline float ChildHoverLeftOpacity = 0.72f;
			inline float ChildHoverRightOpacity = 1.0f;
			inline float ChildSelectedLeftOpacity = 0.9f;
			inline float ChildSelectedRightOpacity = 0.94f;
			inline float LayerActiveHairlineWidth = 1.0f;
			inline float LayerActiveHairlineOpacity = 0.6f;
			inline float LayerActiveGlowOpacity = 0.18f;
			inline float LayerActiveGlowReach = 32.0f;
			inline float LayerActiveGlowSaturation = 1.5f;
			inline float LayerIconSize = 14.0f;
			inline float LayerIconOpacity = 0.6f;
			inline float LayerVisibilitySize = 6.0f;
			inline float LayerVisibilityRadius = 2.0f;
			inline float IconOffOpacity = 0.42f;
			inline float FoldoutIconOpacity = 0.65f;
			inline float FoldoutIconHoverOpacity = 0.85f;
			inline float LayerGroupTitleSize = 10.0f;
			inline float LayerGroupTitleWeight = 400.0f;
			inline float LayerHierarchyLineWidth = 1.0f;
	inline float LayerChildRowHeight = 18.0f;
	// A group header is a layer row without the thumbnail, so it sits at child height rather than
	// layer height -- the stack reads as groups of layers, not as a taller kind of layer.
	inline float LayerGroupRowHeight = 18.0f;
	// The image, not a plate around it: layer thumbnails have no border, so this is the whole
	// footprint.
	inline float LayerThumbnailSize = 20.0f;
	// Children sit under the layer name; each scoped ownership level steps in again.
	inline float LayerChildIndent = 28.0f;
	inline float LayerScopeIndent = 14.0f;
	// The accent bar down the left edge of an instance's source row (and of its collapsed layer):
	// readable from across the stack where a tint alone is not.
	inline float LayerSourceBarWidth = 3.0f;
	// Leading inset is larger than trailing: the eye needs room from the panel edge, while the
	// chevron on the right is already inset by its own slot padding.
	inline float LayerRowInsetLeading = 6.0f;
	inline float LayerRowInsetTrailing = 6.0f;
	// Between every element within a row -- eye to thumbnail, name to source, badge to chevron.
	// One value, so the row reads as evenly spaced rather than as clusters.
	inline float LayerItemGap = 3.0f;
	// The name sits closer to its thumbnail than the standard gap, so the two read as one unit
	// against the source text on the far side.
	constexpr float LayerNameInset = 4.0f;
	// Between stacked rows. One pixel: enough to separate, not enough to break the column.
	inline float LayerRowGap = 2.0f;
	inline float LayerEyeSize = 15.0f;
	inline float LayerChildIconSize = 16.0f;
	// Opacity of the tree connector (tee / elbow) before a scoped child's glyph.
	inline float LayerConnectorOpacity = 0.24f;
	// The count-and-create bar above the rows: the icon buttons are what governs its natural
	// height, plus three pixels of breathing room now that it carries the label as well.
	inline float LayerStackHeaderHeight = IconButtonSize + 3.0f;
	// The accent edge enclosing an open layer's children.
	inline float LayerEdgeWidth = 1.0f;
	constexpr float DropLineThickness = 2.0f;

	// ---- Type -------------------------------------------------------------------------------
	// Type sizes follow the authored component-sheet scale so the compact rows stay visually aligned.
	inline float FontBody = 10.0f;
	// Shared by a layer's own name and by a mask/effect child's name -- the same role at both
	// levels of the stack. Same tier as body rather than a step above it: at 11px it read as too
	// large next to the 8px source and badge beside it.
	inline float FontLayerName = 10.0f;
	// A value row's label and its number. One step under body and carried in a heavier face: the
	// rows are the densest thing in the tool, and weight reads at this size where size does not.
	inline float FontSliderLabel = 9.0f;
	inline float FontCaption = 11.0f;
	// A card's own title, apart from the caption tier it used to borrow. A card title heads a
	// sheet; a caption names a run of rows inside one. They were the same style, which is why a
	// card read as another caption that happened to sit higher up.
	inline float FontCardTitle = 8.0f;
	inline float FontGroupCardTitle = 8.0f;
	inline float FontTile = 9.0f;
	// Group headers: small tracked caps. A header names a group rather than being read as content,
	// so it sits under the caption tier -- the extra header height carries it instead of the type.
	inline float FontGroupHeader = 7.0f;
	// A layer's source and its badge are both 8px and both secondary to the name, but they are
	// named apart from the group header so the stack can be retuned without touching panels.
	inline float FontLayerSource = 8.0f;
	inline float FontBadge = 8.0f;
	inline float FontDialogLabel = 9.0f;
	inline float FontMaskBarHeading = 9.0f;
		// A menu row's right-hand shortcut is authored at 9px, not at the caption's 11px.
		inline float FontMenuShortcut = 9.0f;
		inline float ShortcutTextOpacity = 0.4f;
	inline float FontDragGhostLabel = 9.0f;

	// Weight as a number, because the live theme carries numbers and colours and nothing else.
	// At or above 0.5 the face is Bold, below it Regular -- there is no half-weight in the
	// default font family, so the slider is a switch that happens to be continuous.
	inline bool CardTitleBold = false;
	inline bool GroupCardTitleBold = false;
	inline bool GroupHeaderBold = true;

	// Letter spacing is in 1/1000 em. Applied to the all-caps captions and group headers, where
	// tight caps are hard to read at this size.
	// CSS 1px at an 11px caption: 1/11*1000 = 91. This was 140, which opened the caps to 1.54px.
		constexpr int32 CaptionLetterSpacing = 91;
	// CSS 0.6px / 8px * 1000; persisted value remains in Slate's 1/1000-em units.
			inline float GroupCardTitleLetterSpacing = 75.0f;
	constexpr int32 GroupHeaderLetterSpacing = 160;
		// CSS .8px at an 11px caption: 0.8/11*1000 = 73. Shared by the menu section caption.
		constexpr int32 MenuCaptionLetterSpacing = 73;
	// The layer source is caps too, but it runs alongside a mixed-case name rather than standing
	// alone, so it is opened up less -- full caption spacing made it the loudest thing in the row.
	constexpr int32 LayerSourceLetterSpacing = 60;

	// Called after every live edit/load/reset; derived dimensions are not independent knobs.
	//
	// Only relationships that hold unconditionally live here. A value that merely happened to
	// equal another when both were authored -- TabHeight against ButtonHeight, MenuItemHeight
	// against ButtonHeight, MenuLipHeight against GroupHeaderHeight, SegmentHeight against
	// RowHeight, FontSliderLabel against FontBody -- is not a relationship, so editing one of
	// the pair silently dragged the other with it. Those five are authored independently now,
	// and the authored defaults are unchanged: the UI looks identical until one is retuned.
	inline void RecomputeDerived()
	{
		CornerRadiusInner = CornerRadius * 0.5f;
		TabUnderlineThickness = OutlineWidth;
		LayerEdgeWidth = OutlineWidth;
		FontLayerName = FontBody;
		// A genuine geometric relationship: the fill is inset from the well on every side, so it is
		// always this much smaller. Unlike the five decoupled above, neither value is meant to move
		// without the other changing the fill's size.
		ToggleFillSize = ToggleSize - ToggleFillInset * 2.0f;
	}
}
