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
namespace MixtormatTokens
{
	// ---- Rows -------------------------------------------------------------------------------
	// Every inspector control is one of these tall, whatever it edits. Uniformity across row
	// types is what makes a panel of mixed controls read as a single column.
	constexpr float RowHeight = 17.0f;
	constexpr float RowGap = 2.0f;

	// Text inset from the row's leading and trailing edges. Shared by the slider's painted text
	// and by the label of every composed row, so they line up down the column.
	constexpr float RowTextInset = 5.0f;

	// ---- Surfaces ---------------------------------------------------------------------------
	constexpr float CornerRadius = 2.0f;
	constexpr float OutlineWidth = 1.0f;
	constexpr float PanelGutter = 9.0f;
	constexpr float GroupHeaderHeight = 22.0f;

	// ---- Sub-grouping -----------------------------------------------------------------------
	// A caption names a run of rows; a hairline separates two runs without naming them. The
	// caption costs more height, so it is for groupings the labels do not already imply.
	constexpr float CaptionHeightAbove = 6.0f;
	constexpr float CaptionHeightBelow = 2.0f;
	constexpr float HairlineThickness = 1.0f;
	constexpr float HairlineMargin = 4.0f;

	// ---- Slider -----------------------------------------------------------------------------
	// Leading stripe marking a value that differs from its default.
	constexpr float ModifiedStripeWidth = 2.0f;
	// The label shifts right by this much when the stripe is showing, so text never sits on it.
	constexpr float ModifiedLabelInset = 4.0f;
	// Pixels of travel before a press becomes a scrub rather than a click-to-type.
	constexpr float DragThreshold = 4.0f;
	// Shift-drag multiplier.
	constexpr float FineDragScale = 0.125f;
	// Centre tick on a range that spans zero.
	constexpr float TickInsetY = 3.0f;
	constexpr float TickWidth = 1.0f;

	// ---- Segmented control ------------------------------------------------------------------
	constexpr float SegmentHeight = 16.0f;
	constexpr float StatusDotSize = 9.0f;
	constexpr float IconButtonSize = 11.0f;

	// ---- Badge ------------------------------------------------------------------------------
	// Fixed width, not hugging its text: the badges form a column down the right edge, and the
	// word changes without the column moving.
	constexpr float BadgeWidth = 32.0f;
	constexpr float BadgeHeight = 12.0f;

	// ---- Thumbnails -------------------------------------------------------------------------
	// One tile widget serves the library, the mask replacement grid and the mask picker; only the
	// size differs. The name strip is an overlay, so it costs image rather than layout height.
	constexpr float SurfaceTileSize = 90.0f;
	constexpr float SurfaceTileSizeDense = 68.0f;
	constexpr float MaskTileSize = 62.0f;
	constexpr float MaskPickerTileSize = 76.0f;
	constexpr float MaskPickerTileSizeDense = 52.0f;
	constexpr float TileGap = 4.0f;
	constexpr float TileNameStripHeight = 13.0f;
	constexpr float TileBadgeHeight = 12.0f;

	// A chip's inline thumbnail -- enough to confirm which asset is bound without opening the
	// picker, since the row already carries the name.
	constexpr float ChipThumbnailSize = 12.0f;
	constexpr float ChipHeight = 16.0f;

	// ---- Mask picker popover ----------------------------------------------------------------
	// Wider than the 300px inspector on purpose: a menu is its own window and is not clipped by
	// the panel that opened it.
	constexpr float MaskPickerWidth = 324.0f;
	constexpr int32 MaskPickerColumns = 4;
	constexpr int32 MaskPickerColumnsDense = 5;

	// ---- Layer stack ------------------------------------------------------------------------
	constexpr float LayerRowHeight = 26.0f;
	constexpr float LayerChildRowHeight = 20.0f;
	constexpr float LayerThumbnailSize = 18.0f;
	constexpr float LayerChildIndent = 26.0f;
	constexpr float DropLineThickness = 2.0f;

	// ---- Type -------------------------------------------------------------------------------
	// Slate sizes are points rendered at 96 DPI, so pixels = points * 4/3. The design specifies
	// pixels: 10px body is 7.5pt, not 8pt (which lands at 10.67px). Float, because FSlateFontInfo
	// carries a float Size and the difference is visible at this scale.
	constexpr float FontBody = 7.5f;
	constexpr float FontCaption = 6.75f;
	constexpr float FontTile = 6.75f;
	// Group headers: 8px, regular weight. Bold caps at this size closed up the letterforms and
	// fought the row labels below, which are larger.
	constexpr float FontGroupHeader = 6.0f;

	// Letter spacing is in 1/1000 em. Applied to the all-caps captions and group headers, where
	// tight caps are hard to read at this size.
	constexpr int32 CaptionLetterSpacing = 140;
	constexpr int32 GroupHeaderLetterSpacing = 160;
}
