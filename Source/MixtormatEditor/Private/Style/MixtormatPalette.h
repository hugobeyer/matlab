// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatLiveTheme.h"

// The design palette, as sRGB hex, exactly as the component exploration specified it.
//
// MixtormatStyle.cpp registers brushes; this is the source those brushes and any hand-painted
// widget read from. Colours live here rather than in the style set because gradients are painted,
// not brushed -- a widget drawing a gradient needs the colour, not a brush.
//
// Two rules the values encode, both learned the hard way:
//
//   Wells are always darker than the surface they sit in. A trough that matched its panel made
//   the whole control invisible.
//
//   Tints are translucent, not opaque. HeaderTint takes its weight from the container behind it,
//   so it stays correct if that container's shade changes; an opaque navy had to be re-picked.
namespace MixtormatPalette
{
	// Defined in the Marks section below. The value fill is authored as the accent at a given
	// opacity, so the fill helpers need Accent() before that section is reached -- and the fill is
	// a Marks concept anyway, only living here because it sits with the other active-state colours.
	inline FLinearColor Accent();

	inline FLinearColor Hex(const uint32 RGB, const float Alpha = 1.0f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>((RGB >> 16) & 0xFF),
			static_cast<uint8>((RGB >> 8) & 0xFF),
			static_cast<uint8>(RGB & 0xFF)));
		Color.A = Alpha;
		return Color;
	}

	// ---- Surfaces ---------------------------------------------------------------------------
	// Every colour below is sampled from the approved graphite reference. Widgets use semantic
	// roles only, so the visual system can be retuned here without hunting through Slate code.
	inline FLinearColor Window()       { return FMixtormatLiveTheme::ResolveColor(TEXT("Window"), Hex(0x0F0F0F)); }
	inline FLinearColor TopBar()       { return FMixtormatLiveTheme::ResolveColor(TEXT("TopBar"), Hex(0x171819)); }
	inline FLinearColor Shell()        { return FMixtormatLiveTheme::ResolveColor(TEXT("Shell"), Hex(0x111213)); }
	// The base everything else sits on: 14 15 16, the prototype's --ground-rgb.
	//
	// Deliberately its own role and not an alias for Shell (17 18 19) or Panel (25 27 29). The
	// prototype paints its cards, foldout bodies and panel columns off ground rather than off a
	// panel, and a card body at 9% over ground is a different value than the same 9% over a panel.
	// Aliasing would make that difference invisible in the token and permanent in the result.
	//
	// Nothing reads this yet -- Phase 2 introduces the source; the surfaces adopt it as they are
	// restyled, so a change here is a change to what "the ground" means rather than a repaint.
	inline FLinearColor Ground()       { return FMixtormatLiveTheme::ResolveColor(TEXT("Ground"), Hex(0x0E0F10)); }
	inline FLinearColor Panel()        { return FMixtormatLiveTheme::ResolveColor(TEXT("Panel"), Hex(0x191B1D)); }
	inline FLinearColor PanelBottom()  { return Hex(0x141617); }
	inline FLinearColor RaisedPanel()  { return FMixtormatLiveTheme::ResolveColor(TEXT("RaisedPanel"), Hex(0x202224)); }
	inline FLinearColor GroupCardBackground()
		{
			FLinearColor Color = FMixtormatLiveTheme::ResolveColor(TEXT("GroupCardBackground"), RaisedPanel());
			// Add the background itself at five percent opacity; preserve its hue and alpha.
			Color.R = FMath::Clamp(Color.R + Color.R * 0.05f, 0.0f, 1.0f);
			Color.G = FMath::Clamp(Color.G + Color.G * 0.05f, 0.0f, 1.0f);
			Color.B = FMath::Clamp(Color.B + Color.B * 0.05f, 0.0f, 1.0f);
			return Color;
		}
	inline FLinearColor RaisedPanelHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("RaisedPanelHover"), Hex(0x26292B)); }
	inline FLinearColor Viewport()     { return Hex(0x161719); }
	inline FLinearColor ThumbnailBackground() { return Hex(0x101112); }
	inline FLinearColor Inset()        { return Hex(0x101112); }
	inline FLinearColor Border()       { return FMixtormatLiveTheme::ResolveColor(TEXT("Border"), Hex(0x292C2E)); }
	inline FLinearColor BorderStrong() { return FMixtormatLiveTheme::ResolveColor(TEXT("BorderStrong"), Hex(0x383C3E)); }
	inline FLinearColor Shadow()       { return Hex(0x000000, 0.52f); }
	inline FLinearColor HeaderTint()   { return FMixtormatLiveTheme::ResolveColor(TEXT("HeaderTint"), Hex(0x25282B, 0.72f)); }
	inline FLinearColor HeaderHover()  { return Hex(0x2E3236, 0.82f); }
	inline FLinearColor HeaderTintFade() { return Hex(0x25282B); }
	// A hovered header's top edge. The accent at the tint's own weight, so hover reads as the
	// same lip catching light rather than as a differently-coloured bar.
	inline FLinearColor HeaderTintHoverAccent() { return Hex(0x35525E, 0.85f); }
	// The hairline as its own role. FoldoutHairline/FoldoutHairlineHover are the foldout's readings
	// of it -- the registered keys are "Hairline" and "HairlineHover", so a live edit to either
	// moves every surface that draws that line, which is what a shared role means.
	inline FLinearColor Hairline()     { return FMixtormatLiveTheme::ResolveColor(TEXT("Hairline"), Hex(0x6F7D82, 0.16f)); }
	inline FLinearColor HairlineHover(){ return FMixtormatLiveTheme::ResolveColor(TEXT("HairlineHover"), Hex(0x7FC4DB, 0.85f)); }
	// The hairline over a lit edge. Kept distinct from HairlineHover because the two are authored
	// at different weights for different edges and other surfaces still read this one.
	inline FLinearColor HairlineGlow() { return Hex(0x7FC4DB, 0.85f); }
	inline FLinearColor Divider()      { return Hex(0x242729); }
	// The foldout header's additive lift, and the ground it lifts. The prototype composites the
		// lift additively over Ground and saturates the *result*, so both the opacity and the
		// saturation are properties of this paint layer rather than of a stored colour.
		inline FLinearColor FoldoutLift()       { return FMixtormatLiveTheme::ResolveColor(TEXT("HeaderTint"), Hex(0x252828, MixtormatTokens::HeaderTintOpacity)); }
		inline FLinearColor FoldoutLiftHover()  { return Hex(0x35525E, MixtormatTokens::HeaderHoverOpacity); }
		// The accent cross pass. Fades to zero contribution at the body seam so it hands the header to
		// the body without leaving a mark of its own there.
		inline FLinearColor FoldoutAccent()         { return Accent(); }
		inline FLinearColor FoldoutAccentHover()    { return Accent(); }
		// The hairline: its own colour at its own opacity, saturated at paint time.
		inline FLinearColor FoldoutHairline()       { return FMixtormatLiveTheme::ResolveColor(TEXT("Hairline"), Hex(0x6F7D82, MixtormatTokens::FoldoutHairlineOpacity)); }
		inline FLinearColor FoldoutHairlineHover()  { return FMixtormatLiveTheme::ResolveColor(TEXT("HairlineHover"), Hex(0x7FC4DB, MixtormatTokens::HairlineHoverOpacity)); }

		inline FLinearColor FocusFill()    { return FMixtormatLiveTheme::ResolveColor(TEXT("FocusFill"), Hex(0x4D8FA8, 0.10f)); }
	inline FLinearColor SelectionFill(){ return FMixtormatLiveTheme::ResolveColor(TEXT("SelectionFill"), Hex(0x4D8FA8, 0.16f)); }

	// ---- Viewport overlay -------------------------------------------------------------------
	// The plate behind a floating cluster of viewport controls. The well shades, but translucent:
	// these sit on top of the thing being judged rather than in a panel, so an opaque plate takes
	// a bite out of the render. Enough weight to keep the controls legible against a bright HDRI,
	// not enough to read as a second window.
	inline float OverlayPlateOpacity() { return 0.62f; }
	inline FLinearColor OverlayPlateTop()    { return Hex(0x070808, OverlayPlateOpacity()); }
	inline FLinearColor OverlayPlateBottom() { return Hex(0x0C0E0F, OverlayPlateOpacity()); }

	// The ground a group sits on inside the well. A step darker than Shell so a group reads as a
	// raised block with a margin around it rather than as a sheet flush with its container.
	inline FLinearColor GroupSurround() { return FMixtormatLiveTheme::ResolveColor(TEXT("GroupSurround"), Hex(0x0C0D0E)); }

	// ---- Wells ------------------------------------------------------------------------------
	// The well is authored as ground plus a black multiply shade, not as two hand-picked greys. That
	// matters: the same shade values produce the recess at every size, and moving the ground moves
	// the recess with it, which a fixed pair of colours cannot do.
	//
	// WellTop/WellBottom keep their names and their persisted LiveTheme keys. Their values are now
	// ground under the authored shade -- computed here rather than hard-coded, so the tokens below
	// own the appearance. WellOutline is the border at rest and WellOutlineHover on hover; each
	// carries its own overall opacity, and the top/bottom endpoint falloff is applied at paint time
	// by the callers that can express it.
	inline FLinearColor WellTop()      { return FMixtormatLiveTheme::ResolveColor(TEXT("WellTop"), Hex(0x0A0B0C)); }
	inline FLinearColor WellBottom()   { return FMixtormatLiveTheme::ResolveColor(TEXT("WellBottom"), Hex(0x121315)); }
	inline FLinearColor WellTopHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("WellTopHover"), Hex(0x0E0F11)); }
	inline FLinearColor WellBottomHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("WellBottomHover"), Hex(0x17191C)); }
	// The outline at rest: the authored border colour at its own overall opacity, saturated. The
	// endpoints (0.33 top, 0.11 bottom) are *not* applied here -- a single flat colour cannot carry
	// a vertical falloff, so callers scale these per edge as they paint.
	inline FLinearColor WellOutline()
	{
		// FLinearColor has no two-argument constructor, so the alpha is assigned rather than passed.
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(0x24, 0x27, 0x29));
		Color.A = MixtormatTokens::WellBorderOpacity;
		return FMixtormatLiveTheme::ResolveColor(TEXT("WellOutline"),
			MixtormatCompositing::Saturate(Color, MixtormatTokens::WellBorderSaturation));
	}
	inline FLinearColor WellOutlineHover()
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(0x38, 0x3D, 0x41));
		Color.A = MixtormatTokens::WellBorderHoverOpacity;
		return FMixtormatLiveTheme::ResolveColor(TEXT("WellOutlineHover"),
			MixtormatCompositing::Saturate(Color, MixtormatTokens::WellBorderSaturation));
	}
	// The well's recess: ground under a black multiply ramp. Returned as a pair rather than a
	// gradient so the caller decides the axis and can drive it off tokens.
	inline FLinearColor WellShade()      { return Hex(0x000000, MixtormatTokens::WellShadeTop); }
	inline FLinearColor WellShadeEnd()   { return Hex(0x000000, MixtormatTokens::WellShadeBottom); }
	inline FLinearColor WellEntry()    { return FMixtormatLiveTheme::ResolveColor(TEXT("WellEntry"), Hex(0x0A0B0C)); }

	// The centre mark on a signed range, drawn in ground over the fill so it reads as a gap rather
	// than as a lighter line.
	inline FLinearColor ZeroTick()
	{
		return FLinearColor(Ground().R, Ground().G, Ground().B, MixtormatTokens::ZeroTickOpacity);
	}

	// ---- Active -----------------------------------------------------------------------------
	// The value fill: the accent at authored opacity, so rest, hover, active and disabled are one
	// colour at four strengths. The flat greys this replaced could not express that relationship --
	// hover had to be re-picked by hand and drifted from the accent every time it was retuned.
	//
	// Saturation is applied here to the returned paint layer, never to Accent() itself: the same
	// accent appears at 0.7 as a fill and unsaturated as a tab, a border and a menu row.
	inline FLinearColor Fill(const float Alpha, const float Saturation)
	{
		FLinearColor Color = Accent();
		Color.A = Alpha;
		return MixtormatCompositing::Saturate(Color, Saturation);
	}
	// The body ramp endpoints, one call per state so a caller reads them as authored values rather
	// than assembling a gradient out of four literals.
	inline FLinearColor FillBodyTop()           { return Fill(MixtormatTokens::FillBodyTop, MixtormatTokens::FillSaturation); }
	inline FLinearColor FillBodyBottom()        { return Fill(MixtormatTokens::FillBodyBottom, MixtormatTokens::FillSaturation); }
	inline FLinearColor FillBodyHoverTop()      { return Fill(MixtormatTokens::FillBodyHoverTop, MixtormatTokens::FillHoverSaturation); }
	inline FLinearColor FillBodyHoverBottom()   { return Fill(MixtormatTokens::FillBodyHoverBottom, MixtormatTokens::FillHoverSaturation); }
	inline FLinearColor FillBodyActiveTop()     { return Fill(MixtormatTokens::FillBodyActiveTop, MixtormatTokens::FillActiveSaturation); }
	inline FLinearColor FillBodyActiveBottom()  { return Fill(MixtormatTokens::FillBodyActiveBottom, MixtormatTokens::FillActiveSaturation); }
	inline FLinearColor FillDisabled()          { return Fill(MixtormatTokens::FillDisabledOpacity, MixtormatTokens::FillDisabledSaturation); }

	inline FLinearColor FillTop()      { return FMixtormatLiveTheme::ResolveColor(TEXT("FillTop"), FillBodyTop()); }
	inline FLinearColor FillBottom()   { return FMixtormatLiveTheme::ResolveColor(TEXT("FillBottom"), FillBodyBottom()); }
	inline FLinearColor FillTopHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("FillTopHover"), FillBodyHoverTop()); }
	inline FLinearColor FillBottomHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("FillBottomHover"), FillBodyHoverBottom()); }
	inline FLinearColor FillTopActive() { return FillBodyActiveTop(); }
	inline FLinearColor FillBottomActive() { return FillBodyActiveBottom(); }
	inline FLinearColor SegmentTop()   { return Hex(0x33383C); }
	inline FLinearColor SegmentBottom(){ return Hex(0x25292C); }

	// Horizontal multiply pass over a fill. Black at alpha a leaves src * (1 - a).
	//
	// Three stops, not two: the design runs 35% to 10% by 62% and only then to nothing, so the
	// shade drops away fast and then holds flat across the rest of the bar. Interpolated straight
	// between the ends it becomes an even ramp, and the fill reads as eased rather than lit from
	// one edge.
	inline FLinearColor MultiplyStart(){ return Hex(0x000000, MixtormatTokens::FillShadeStart); }
	inline FLinearColor MultiplyMid()  { return Hex(0x000000, MixtormatTokens::FillShadeMid); }
	inline FLinearColor MultiplyEnd()  { return Hex(0x000000, MixtormatTokens::FillShadeEnd); }

	// ---- Menus ------------------------------------------------------------------------------
	// The popover ground: tinted at the top lip, settling to flat by the first item's base.
	inline FLinearColor MenuTint()     { return FMixtormatLiveTheme::ResolveColor(TEXT("MenuTint"), Hex(0x4D8FA8, 0.10f)); }
	inline FLinearColor MenuGroundTop(){ return Hex(0x1A1C1E); }
	inline FLinearColor MenuGround()   { return FMixtormatLiveTheme::ResolveColor(TEXT("MenuGround"), Hex(0x151617)); }
	// A destructive row keeps the same shape as a normal hover and only changes hue, so the
	// gesture reads the same and the consequence does not.
	inline FLinearColor DestructiveTop()   { return Hex(0x5E2A2A); }
	inline FLinearColor DestructiveBottom(){ return Hex(0x3A1C1C); }

	// ---- Marks ------------------------------------------------------------------------------
	inline FLinearColor Accent()       { return FMixtormatLiveTheme::ResolveColor(TEXT("Accent"), Hex(0x4D8FA8)); }
	inline FLinearColor AccentBright() { return FMixtormatLiveTheme::ResolveColor(TEXT("AccentBright"), Hex(0x6CA8BF)); }
	inline FLinearColor Modified()     { return FMixtormatLiveTheme::ResolveColor(TEXT("Modified"), Hex(0xC28A3D)); }
	// A row's dot while the viewport shows a preview taken from it. Orange, distinct from Modified's
	// amber so a reference row being previewed still reads as both.
	inline FLinearColor PreviewDot()   { return FMixtormatLiveTheme::ResolveColor(TEXT("PreviewDot"), Hex(0xFF7A1A)); }
	inline FLinearColor Destructive()  { return Hex(0xC46A6A); }
	inline FLinearColor Tick()
		{
			return ZeroTick();
		}
	inline FLinearColor SegmentSeam()  { return Hex(0xFFFFFF, 0.08f); }

	// ---- Type -------------------------------------------------------------------------------
	inline FLinearColor RowText()      { return FMixtormatLiveTheme::ResolveColor(TEXT("RowText"), Hex(0xC0C0C0)); }
	// An icon button's resting glyph. White, not RowText: an icon carries no letterforms to read,
	// so the contrast that makes body text comfortable makes a 14px glyph look switched off. This
	// is the eye, the chevron and every toolbar button -- they all resolve here.
	// A hair under white rather than at it, so hover still has somewhere brighter to go. The eye
	// reads both as white; the difference only does work on the transition.
	inline FLinearColor IconRest()     { return FMixtormatLiveTheme::ResolveColor(TEXT("IconRest"), Hex(0xF2F2F2)); }
	// A glyph on hover, brighter still than IconRest -- the icon button's only other state besides
	// Accent/AccentBright, which stay reserved for a control that is actually on.
	inline FLinearColor IconHover()    { return FMixtormatLiveTheme::ResolveColor(TEXT("IconHover"), Hex(0xFFFFFF)); }
	inline FLinearColor HeaderText()   { return FMixtormatLiveTheme::ResolveColor(TEXT("HeaderText"), Hex(0xA8A8A8)); }
	inline FLinearColor CaptionText()  { return FMixtormatLiveTheme::ResolveColor(TEXT("CaptionText"), Hex(0x6E6E6E)); }
	// Between CaptionText and HeaderText on purpose: a card title outranks the captions inside
	// the card and sits under the foldout header that contains it.
	inline FLinearColor CardTitleText(){ return FMixtormatLiveTheme::ResolveColor(TEXT("CardTitleText"), Hex(0x8C8C8C)); }
	inline FLinearColor GroupCardTitleText() { return FMixtormatLiveTheme::ResolveColor(TEXT("GroupCardTitleText"), Hex(0x8C8C8C)); }
	inline FLinearColor BadgeText()    { return Hex(0xFFFFFF, 0.6f); }
	inline FLinearColor BadgeSurface() { return Hex(0x0d0d0d); }
	// Plate behind each viewport rail button; hover/press add Accent to it.
	inline FLinearColor OverlayButtonPlate() { return FMixtormatLiveTheme::ResolveColor(TEXT("OverlayButtonPlate"), Hex(0x151618, 0.85f)); }
	inline FLinearColor InspectorHairline() { return FMixtormatLiveTheme::ResolveColor(TEXT("InspectorHairline"), Hex(0xFFFFFF, 0.08f)); }
	// The badge well: lighter at the top, a hairline lip, lifts on hover when it opens a menu.
	inline FLinearColor BadgeTop()      { return FMixtormatLiveTheme::ResolveColor(TEXT("BadgeTop"), Hex(0x1A1B1D)); }
	inline FLinearColor BadgeBottom()   { return FMixtormatLiveTheme::ResolveColor(TEXT("BadgeBottom"), Hex(0x0B0C0D)); }
	inline FLinearColor BadgeTopHover() { return Hex(0x24262A); }
	inline FLinearColor BadgeBottomHover() { return Hex(0x121315); }
	inline FLinearColor BadgeHairline() { return FMixtormatLiveTheme::ResolveColor(TEXT("BadgeHairline"), Hex(0xFFFFFF, 0.14f)); }
	inline FLinearColor DisabledText() { return Hex(0xFFFFFF, 0.20f); }
	// CSS text-rgb at 40%: a shortcut is a quiet right-hand annotation, not a dimmed label.
		inline FLinearColor ShortcutText() { return RowText().CopyWithNewOpacity(MixtormatTokens::ShortcutTextOpacity); }

	// The brand mark sunk into an empty viewport. Black rather than a grey, so it darkens whatever
	// it sits on instead of fighting it -- the viewport's background is not ours to know.
	inline FLinearColor Watermark()    { return Hex(0x000000, 0.28f); }

	// Stands in for a thumbnail that has not resolved -- a drag can start before the asset loads,
	// and an empty swatch reads as "nothing here" rather than as a missing picture.
	inline FLinearColor ThumbnailPlaceholder() { return Hex(0x141414); }
	inline FLinearColor TileNameStrip() { return Hex(0x040404, 0.90f); }
	inline FLinearColor TileNameText() { return Hex(0xDADADA); }
	// Black, and deliberately. The studio floor fades to an unlit black slab -- no emissive --
	// so the background has to be the one colour that slab can reach exactly. The old 0x050609
	// was already indistinguishable from black anyway: 0.0015 linear, which the tonemapper's
	// toe and the -0.5 exposure bias crush to zero before it ever reaches the screen.
	inline FLinearColor PreviewBackground() { return Hex(0x000000); }
	inline FLinearColor ErrorText() { return Hex(0xE63333); }
	inline FLinearColor SegmentActiveText() { return Hex(0xE8F0F8); }
	inline FLinearColor SegmentShade() { return Hex(0x000000, MixtormatTokens::SegmentShadeAlpha); }

	// ---- Layer stack ------------------------------------------------------------------------
	inline FLinearColor LayerName()    { return FMixtormatLiveTheme::ResolveColor(TEXT("LayerName"), Hex(0xA2A2A2)); }
	inline FLinearColor LayerSource()  { return FMixtormatLiveTheme::ResolveColor(TEXT("LayerSource"), Hex(0xA8A8A8, 0.50f)); }
	inline FLinearColor LayerEdge()    { return FMixtormatLiveTheme::ResolveColor(TEXT("LayerEdge"), Hex(0x0C6F95)); }
	// Layer rows are quieter than generic active controls, while retaining a clear vertical lift.
	inline FLinearColor LayerHoverTop()      { return Hex(0x2D3134); }
	inline FLinearColor LayerHoverBottom()   { return Hex(0x222629); }
	inline FLinearColor LayerSelectedTop()   { return Hex(0x383E42); }
	// The left-edge lift on a group header's cross-axis pass. A group is the only row painted on
	// two axes, and this is the one that says so: layers ramp top-down, child rows ramp left-right,
	// and a group does both. Mid-grey rather than black, because it lifts the left edge instead of
	// darkening it -- the opposite direction to a child row, so the two never read as the same
	// surface at different sizes.
	inline FLinearColor GroupRowCross() { return FMixtormatLiveTheme::ResolveColor(TEXT("GroupRowCross"), Hex(0x33383C)); }

	// The colours a group can be tagged with. Eight, spread around the wheel and matched in
	// saturation, so two groups picked at random stay tellable apart -- the whole point of the
	// feature. They are tinted into a dark row, so mid-bright is right: a near-black swatch would
	// vanish and a near-white one would flatten the gradient.
	inline TArray<FLinearColor> GroupAccents()
	{
		return {
			Hex(0xE05252), Hex(0xE08A42), Hex(0xE0C24A), Hex(0x6FBF5A),
			Hex(0x4FB0B5), Hex(0x5A8FD6), Hex(0x9B72D0), Hex(0xD066A5)};
	}
	inline FLinearColor LayerSelectedBottom(){ return Hex(0x2A2F32); }
	// Children run horizontally and stay one value step below their owning layer.
	inline FLinearColor LayerChildHoverLeft()    { return Hex(0x17191B, 0.72f); }
	inline FLinearColor LayerChildHoverRight()   { return Hex(0x25292C, 0.82f); }
	inline FLinearColor LayerChildSelectedLeft() { return Hex(0x1D2022, 0.90f); }
	inline FLinearColor LayerChildSelectedRight(){ return Hex(0x303539, 0.94f); }
	inline FLinearColor LayerHiddenTop()   { return Hex(0x191B1D); }
	inline FLinearColor LayerHiddenEnd()   { return Hex(0x101112); }

	// Scalar-ramp editor colors are semantic roles so the live style panel can tune them.
	inline FLinearColor ScalarRampBackground() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampBackground"), Hex(0x090A0B)); }
	inline FLinearColor ScalarRampOutsideRangeBackground() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampOutsideRangeBackground"), Hex(0x050606)); }
	inline FLinearColor ScalarRampGrid() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampGrid"), Hex(0xFFFFFF, 0.07f)); }
	inline FLinearColor ScalarRampMajorGrid() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampMajorGrid"), Hex(0xFFFFFF, 0.14f)); }
	inline FLinearColor ScalarRampCurve() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampCurve"), Hex(0x7FC4DB)); }
	inline FLinearColor ScalarRampFill() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampFill"), Hex(0x4D8FA8, 0.18f)); }
	inline FLinearColor ScalarRampPoint() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampPoint"), Hex(0xD7E5EA)); }
	inline FLinearColor ScalarRampPointHover() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampPointHover"), Hex(0xFFFFFF)); }
	inline FLinearColor ScalarRampPointSelected() { return FMixtormatLiveTheme::ResolveColor(TEXT("ScalarRampPointSelected"), Hex(0x6CA8BF)); }
}