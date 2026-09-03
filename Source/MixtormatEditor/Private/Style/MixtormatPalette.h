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
	inline FLinearColor Panel()        { return Hex(0x212121); }
	// Header lip: translucent blue at the top, landing on Panel by the header's base.
	inline FLinearColor HeaderTint()   { return Hex(0x0080FF, 0.14f); }
	inline FLinearColor HeaderHover()  { return Hex(0x0080FF, 0.26f); }
	// Additive hairline along a header's top edge -- light catching an edge, not a drawn line.
	inline FLinearColor Hairline()     { return Hex(0x6FA8DC, 0.25f); }
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
	// A segment marks a discrete choice rather than a magnitude, so it stays brighter than a fill.
	inline FLinearColor SegmentTop()   { return Hex(0x1E61A8); }
	inline FLinearColor SegmentBottom(){ return Hex(0x143F6E); }

	// Horizontal multiply pass over a fill. Black at alpha a leaves src * (1 - a).
	inline FLinearColor MultiplyStart(){ return Hex(0x000000, 0.35f); }
	inline FLinearColor MultiplyEnd()  { return Hex(0x000000, 0.0f); }

	// ---- Marks ------------------------------------------------------------------------------
	inline FLinearColor Accent()       { return Hex(0x0070E0); }
	inline FLinearColor AccentBright() { return Hex(0x0E86FF); }
	inline FLinearColor Modified()     { return Hex(0x90540F); }
	inline FLinearColor Destructive()  { return Hex(0xC46A6A); }
	inline FLinearColor Tick()         { return Hex(0x4A4A4A); }
	inline FLinearColor SegmentSeam()  { return Hex(0xFFFFFF, 0.08f); }

	// ---- Type -------------------------------------------------------------------------------
	inline FLinearColor RowText()      { return Hex(0xC0C0C0); }
	inline FLinearColor HeaderText()   { return Hex(0xA8A8A8); }
	inline FLinearColor CaptionText()  { return Hex(0x6E6E6E); }
	inline FLinearColor BadgeText()    { return Hex(0xFFFFFF, 0.49f); }
	inline FLinearColor BadgeSurface() { return Hex(0x161616); }
	inline FLinearColor DisabledText() { return Hex(0xFFFFFF, 0.20f); }
	inline FLinearColor ShortcutText() { return Hex(0xFFFFFF, 0.24f); }

	// ---- Layer stack ------------------------------------------------------------------------
	inline FLinearColor LayerName()    { return Hex(0xA2A2A2); }
	inline FLinearColor LayerSource()  { return Hex(0xA8A8A8, 0.50f); }
	inline FLinearColor LayerEdge()    { return Hex(0x0C5195); }
	inline FLinearColor LayerOpenTop() { return Hex(0x0070E0, 0.13f); }
	inline FLinearColor LayerOpenEnd() { return Hex(0x1D2C3A, 0.0f); }
	inline FLinearColor LayerHiddenTop()   { return Hex(0x191919); }
	inline FLinearColor LayerHiddenEnd()   { return Hex(0x000000); }
}
