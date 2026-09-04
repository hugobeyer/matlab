#pragma once

#include "CoreMinimal.h"

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
	// The container well every inspector group stacks inside.
	inline FLinearColor Shell()        { return Hex(0x161616); }
	// A group body.
	inline FLinearColor Panel()        { return Hex(0x333333); }
	// Where a panel-coloured surface lands when it shades downward -- a layer row, a group header.
	// The body below stays flat Panel, so the darkening is what separates the two without a border.
	inline FLinearColor PanelBottom()  { return Hex(0x1A1A1A); }
	// Header lip: translucent blue at the top, landing on Panel by the header's base.
	//
	// This is the tool's one "this is the thing you are working on" surface. An inspector group
	// header and a selected layer row are the same statement, so they are the same tint rather
	// than two blues that have to be kept in step by hand.
	inline FLinearColor HeaderTint()   { return Hex(0x004080, 0.01f); }
	inline FLinearColor HeaderHover()  { return Hex(0x0080FF, 0.16f); }
	// The same hue at zero alpha, for a tint that fades out rather than landing on a known
	// surface -- a child row runs over whatever its layer group is sitting on.
	inline FLinearColor HeaderTintFade() { return Hex(0x212121, 1.0f); }
	// Additive hairline along a header's top edge -- light catching an edge, not a drawn line.
	inline FLinearColor Hairline()     { return Hex(0x6FA8DC, 0.33f); }
	inline FLinearColor Divider()      { return Hex(0x2A2A2A); }

	// ---- Wells ------------------------------------------------------------------------------
	inline FLinearColor WellTop()      { return Hex(0x161616); }
	inline FLinearColor WellBottom()   { return Hex(0x1B1B1B); }
	inline FLinearColor WellTopHover() { return Hex(0x1B1B1B); }
	inline FLinearColor WellBottomHover() { return Hex(0x202020); }

	// ---- Active -----------------------------------------------------------------------------
	// Held between the accent and flat steel: saturated enough to read as blue, not so much that a
	// column of filled rows pulls the eye off the values printed on them.
	inline FLinearColor FillTop()      { return Hex(0x224970); }
	inline FLinearColor FillBottom()   { return Hex(0x18334E); }
	inline FLinearColor FillTopHover() { return Hex(0x285586); }
	inline FLinearColor FillBottomHover() { return Hex(0x1D3F60); }
	// While the value is actually being scrubbed.
	inline FLinearColor FillTopActive() { return Hex(0x2A5C93); }
	inline FLinearColor FillBottomActive() { return Hex(0x1F456B); }
	// A disabled fill keeps its hue and loses its weight, rather than going grey: grey would read
	// as a different control, and the value it is showing is still true.
	inline FLinearColor FillDisabled() { return Hex(0x18334E, 0.45f); }
	// A segment marks a discrete choice rather than a magnitude, so it stays brighter than a fill.
	inline FLinearColor SegmentTop()   { return Hex(0x1E61A8); }
	inline FLinearColor SegmentBottom(){ return Hex(0x143F6E); }

	// Horizontal multiply pass over a fill. Black at alpha a leaves src * (1 - a).
	//
	// Three stops, not two: the design runs 35% to 10% by 62% and only then to nothing, so the
	// shade drops away fast and then holds flat across the rest of the bar. Interpolated straight
	// between the ends it becomes an even ramp, and the fill reads as eased rather than lit from
	// one edge.
	inline FLinearColor MultiplyStart(){ return Hex(0x000000, 0.35f); }
	inline FLinearColor MultiplyMid()  { return Hex(0x000000, 0.10f); }
	inline FLinearColor MultiplyEnd()  { return Hex(0x000000, 0.0f); }

	// ---- Menus ------------------------------------------------------------------------------
	// The popover ground: tinted at the top lip, settling to flat by the first item's base.
	inline FLinearColor MenuTint()     { return Hex(0x0080FF, 0.10f); }
	inline FLinearColor MenuGroundTop(){ return Hex(0x1A1A1A); }
	inline FLinearColor MenuGround()   { return Hex(0x191919); }
	// A destructive row keeps the same shape as a normal hover and only changes hue, so the
	// gesture reads the same and the consequence does not.
	inline FLinearColor DestructiveTop()   { return Hex(0x5E2A2A); }
	inline FLinearColor DestructiveBottom(){ return Hex(0x3A1C1C); }

	// ---- Marks ------------------------------------------------------------------------------
	inline FLinearColor Accent()       { return Hex(0x0070E0); }
	inline FLinearColor AccentBright() { return Hex(0x0E86FF); }
	inline FLinearColor Modified()     { return Hex(0x90540F); }
	inline FLinearColor Destructive()  { return Hex(0xC46A6A); }
	inline FLinearColor Tick()         { return Hex(0x4A4A4A); }
	inline FLinearColor SegmentSeam()  { return Hex(0xFFFFFF, 0.08f); }

	// ---- Type -------------------------------------------------------------------------------
	inline FLinearColor RowText()      { return Hex(0xC0C0C0); }
	// A glyph on hover, brighter still than RowText -- the icon button's only other state besides
	// Accent/AccentBright, which stay reserved for a control that is actually on.
	inline FLinearColor IconHover()    { return Hex(0xf2f2f2); }
	inline FLinearColor HeaderText()   { return Hex(0xA8A8A8); }
	inline FLinearColor CaptionText()  { return Hex(0x6E6E6E); }
	inline FLinearColor BadgeText()    { return Hex(0xFFFFFF, 0.6f); }
	inline FLinearColor BadgeSurface() { return Hex(0x0d0d0d); }
	inline FLinearColor DisabledText() { return Hex(0xFFFFFF, 0.20f); }
	inline FLinearColor ShortcutText() { return Hex(0xFFFFFF, 0.24f); }

	// The brand mark sunk into an empty viewport. Black rather than a grey, so it darkens whatever
	// it sits on instead of fighting it -- the viewport's background is not ours to know.
	inline FLinearColor Watermark()    { return Hex(0x000000, 0.28f); }

	// Stands in for a thumbnail that has not resolved -- a drag can start before the asset loads,
	// and an empty swatch reads as "nothing here" rather than as a missing picture.
	inline FLinearColor ThumbnailPlaceholder() { return Hex(0x141414); }

	// ---- Layer stack ------------------------------------------------------------------------
	inline FLinearColor LayerName()    { return Hex(0xA2A2A2); }
	inline FLinearColor LayerSource()  { return Hex(0xA8A8A8, 0.50f); }
	inline FLinearColor LayerEdge()    { return Hex(0x0C5195); }
	inline FLinearColor LayerHiddenTop()   { return Hex(0x191919); }
	inline FLinearColor LayerHiddenEnd()   { return Hex(0x000000); }
}
