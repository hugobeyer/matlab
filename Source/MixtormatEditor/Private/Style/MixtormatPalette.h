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
	// Every colour below is sampled from the approved graphite reference. Widgets use semantic
	// roles only, so the visual system can be retuned here without hunting through Slate code.
	inline FLinearColor Window()       { return Hex(0x0F0F0F); }
	inline FLinearColor TopBar()       { return Hex(0x171819); }
	inline FLinearColor Shell()        { return Hex(0x111213); }
	inline FLinearColor Panel()        { return Hex(0x191B1D); }
	inline FLinearColor PanelBottom()  { return Hex(0x141617); }
	inline FLinearColor RaisedPanel()  { return Hex(0x202224); }
	inline FLinearColor RaisedPanelHover() { return Hex(0x26292B); }
	inline FLinearColor Viewport()     { return Hex(0x161719); }
	inline FLinearColor ThumbnailBackground() { return Hex(0x101112); }
	inline FLinearColor Inset()        { return Hex(0x101112); }
	inline FLinearColor Border()       { return Hex(0x292C2E); }
	inline FLinearColor BorderStrong() { return Hex(0x383C3E); }
	inline FLinearColor Shadow()       { return Hex(0x000000, 0.52f); }
	inline FLinearColor HeaderTint()   { return Hex(0x25282B, 0.72f); }
	inline FLinearColor HeaderHover()  { return Hex(0x2E3236, 0.82f); }
	inline FLinearColor HeaderTintFade() { return Hex(0x25282B); }
	// A hovered header's top edge. The accent at the tint's own weight, so hover reads as the
	// same lip catching light rather than as a differently-coloured bar.
	inline FLinearColor HeaderTintHoverAccent() { return Hex(0x35525E, 0.85f); }
	// The hairline over that edge, lit. Bright enough to read as a glow against the accent
	// beneath it, which the flat divider grey does not.
	inline FLinearColor HairlineGlow() { return Hex(0x7FC4DB, 0.85f); }
	inline FLinearColor Hairline()     { return Hex(0x6F7D82, 0.16f); }
	inline FLinearColor Divider()      { return Hex(0x242729); }
	inline FLinearColor FocusFill()    { return Hex(0x4D8FA8, 0.10f); }
	inline FLinearColor SelectionFill(){ return Hex(0x4D8FA8, 0.16f); }

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
	inline FLinearColor GroupSurround() { return Hex(0x0C0D0E); }

	// ---- Wells ------------------------------------------------------------------------------
	inline FLinearColor WellTop()      { return Hex(0x070808); }
	inline FLinearColor WellBottom()   { return Hex(0x0C0E0F); }
	inline FLinearColor WellTopHover() { return Hex(0x0A0B0C); }
	inline FLinearColor WellBottomHover() { return Hex(0x121416); }
	inline FLinearColor WellOutline()  { return Hex(0x242729); }
	inline FLinearColor WellOutlineHover() { return Hex(0x383D41); }
	inline FLinearColor WellEntry()    { return Hex(0x070808); }

	// ---- Active -----------------------------------------------------------------------------
	inline FLinearColor FillTop()      { return Hex(0x303438); }
	inline FLinearColor FillBottom()   { return Hex(0x24282B); }
	inline FLinearColor FillTopHover() { return Hex(0x383D41); }
	inline FLinearColor FillBottomHover() { return Hex(0x2A2F32); }
	inline FLinearColor FillTopActive() { return Hex(0x41484D); }
	inline FLinearColor FillBottomActive() { return Hex(0x30363A); }
	inline FLinearColor FillDisabled() { return Hex(0x24282B, 0.45f); }
	inline FLinearColor SegmentTop()   { return Hex(0x33383C); }
	inline FLinearColor SegmentBottom(){ return Hex(0x25292C); }

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
	inline FLinearColor MenuTint()     { return Hex(0x4D8FA8, 0.10f); }
	inline FLinearColor MenuGroundTop(){ return Hex(0x1A1C1E); }
	inline FLinearColor MenuGround()   { return Hex(0x151617); }
	// A destructive row keeps the same shape as a normal hover and only changes hue, so the
	// gesture reads the same and the consequence does not.
	inline FLinearColor DestructiveTop()   { return Hex(0x5E2A2A); }
	inline FLinearColor DestructiveBottom(){ return Hex(0x3A1C1C); }

	// ---- Marks ------------------------------------------------------------------------------
	inline FLinearColor Accent()       { return Hex(0x4D8FA8); }
	inline FLinearColor AccentBright() { return Hex(0x6CA8BF); }
	inline FLinearColor Modified()     { return Hex(0xC28A3D); }
	inline FLinearColor Destructive()  { return Hex(0xC46A6A); }
	inline FLinearColor Tick()         { return Hex(0x4A4D4F); }
	inline FLinearColor SegmentSeam()  { return Hex(0xFFFFFF, 0.08f); }

	// ---- Type -------------------------------------------------------------------------------
	inline FLinearColor RowText()      { return Hex(0xC0C0C0); }
	// A glyph on hover, brighter still than RowText -- the icon button's only other state besides
	// Accent/AccentBright, which stay reserved for a control that is actually on.
	inline FLinearColor IconHover()    { return Hex(0xE6E6E6); }
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
	inline FLinearColor TileNameStrip() { return Hex(0x040404, 0.90f); }
	inline FLinearColor TileNameText() { return Hex(0xDADADA); }
	inline FLinearColor PreviewBackground() { return Hex(0x050609); }
	inline FLinearColor PreviewFog() { return Hex(0x020203); }
	inline FLinearColor ErrorText() { return Hex(0xE63333); }
	inline FLinearColor SegmentActiveText() { return Hex(0xE8F0F8); }
	inline FLinearColor SegmentShade() { return Hex(0x000000, 0.10f); }

	// ---- Layer stack ------------------------------------------------------------------------
	inline FLinearColor LayerName()    { return Hex(0xA2A2A2); }
	inline FLinearColor LayerSource()  { return Hex(0xA8A8A8, 0.50f); }
	inline FLinearColor LayerEdge()    { return Hex(0x0C6F95); }
	inline FLinearColor LayerHiddenTop()   { return Hex(0x191B1D); }
	inline FLinearColor LayerHiddenEnd()   { return Hex(0x101112); }
}