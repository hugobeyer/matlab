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
// Scale: these sit on Unreal's own editor ladder -- StarshipCoreStyle defines Small 8pt, Normal
// 10pt, Large 14pt, and the editor draws all of it through the same Slate with no DPI wrapper.
// The first pass specified 6-7.5pt, which put every label in this tool *below* the size Unreal
// reserves for footnotes, and it read as half-size next to the Details panel. Everything sized was
// multiplied by 4/3 to land on that ladder. Deliberately not scaled: one-pixel hairlines, which
// stop being hairlines; ratios like FineDragScale; and input thresholds like DragThreshold, which
// are measured in a hand's travel rather than in the design.
namespace MixtormatTokens
{
	// ---- Rows -------------------------------------------------------------------------------
	// Every inspector control is one of these tall, whatever it edits. Uniformity across row
	// types is what makes a panel of mixed controls read as a single column.
	constexpr float RowHeight = 23.0f;
	constexpr float RowGap = 3.0f;

	// Text inset from the row's leading and trailing edges. Shared by the slider's painted text
	// and by the label of every composed row, so they line up down the column.
	constexpr float RowTextInset = 7.0f;

	// Gap between a row's label and the control it labels, and the width a paired value field
	// asks for before anything competes with it.
	constexpr float RowLabelGap = 5.0f;
	constexpr float RowFieldMinWidth = 160.0f;

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
	constexpr float CornerRadius = 3.0f;
	// Inner corners -- a segment cell inside a control that is itself rounded. Half the outer
	// radius, so the two curves read as concentric rather than as two unrelated roundings.
	constexpr float CornerRadiusInner = CornerRadius * 0.5f;
	constexpr float OutlineWidth = 1.0f;
	constexpr float PanelGutter = 12.0f;
	constexpr float GroupHeaderHeight = 29.0f;
	// Breathing room under a group header before its first row.
	constexpr float GroupBodyTopInset = 8.0f;
	// Gap between a header's chevron, its title, and the controls trailing it.
	constexpr float GroupHeaderItemGap = 7.0f;

	// ---- Sub-grouping -----------------------------------------------------------------------
	// A caption names a run of rows; a hairline separates two runs without naming them. The
	// caption costs more height, so it is for groupings the labels do not already imply.
	constexpr float CaptionHeightAbove = 8.0f;
	constexpr float CaptionHeightBelow = 3.0f;
	constexpr float HairlineThickness = 1.0f;
	constexpr float HairlineMargin = 5.0f;

	// ---- Slider -----------------------------------------------------------------------------
	// Leading stripe marking a value that differs from its default.
	constexpr float ModifiedStripeWidth = 3.0f;
	// The label shifts right by this much when the stripe is showing, so text never sits on it.
	constexpr float ModifiedLabelInset = 5.0f;
	// Pixels of travel before a press becomes a scrub rather than a click-to-type.
	constexpr float DragThreshold = 4.0f;
	// Shift-drag multiplier.
	constexpr float FineDragScale = 0.125f;
	// Centre tick on a range that spans zero.
	constexpr float TickInsetY = 4.0f;
	constexpr float TickWidth = 1.0f;
	// Narrower than this and the painted fill is a sliver rather than a bar, so it is skipped --
	// a half pixel of colour reads as a rendering artefact, not as a value near zero.
	constexpr float MinPaintedFill = 0.5f;

	// ---- Segmented control ------------------------------------------------------------------
	constexpr float SegmentHeight = 21.0f;
	// Hairline *between* cells -- the one border the design allows, because it divides rather
	// than encloses.
	constexpr float SegmentSeamWidth = 1.0f;
	// Multiply pass darkening the trailing edge of an active cell.
	constexpr float SegmentShadeAlpha = 0.28f;

	// ---- Icons ------------------------------------------------------------------------------
	// Sized per role, not per pixel budget: the eye is the only thing in a layer row a user aims
	// at, so it is the largest; a disclosure chevron is read, not clicked, and stays small.
	constexpr float IconButtonSize = 15.0f;
	constexpr float ChevronSize = 12.0f;
	constexpr float StatusDotSize = 12.0f;

	// The size an SVG is *registered* at, which is not the size anything displays it at -- the box
	// holding the brush scales it down. Registering small and scaling up is what makes a glyph
	// look soft, so these stay at or above the largest place each icon appears.
	constexpr float IconBrushSize = 24.0f;
	// Menu and toolbar glyphs, which sit alone rather than inside a dense row.
	constexpr float IconBrushSizeLarge = 32.0f;

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
	// surface besides the drag ghost that carries a drop shadow. Values are the canvas menu at the
	// system's scale -- a 20px row there is 27 here, for the same reason every other row grew.
	constexpr float MenuWidth = 253.0f;
	constexpr float MenuItemHeight = 27.0f;
	constexpr float MenuItemInset = 11.0f;
	constexpr float MenuItemGap = 9.0f;
	constexpr float MenuPanelPadding = 4.0f;
	constexpr float MenuCaptionInsetAbove = 8.0f;
	constexpr float MenuCaptionInsetBelow = 4.0f;
	constexpr float MenuSeparatorMargin = 4.0f;
	constexpr float MenuIconSize = 15.0f;
	constexpr float MenuCornerRadius = 4.0f;
	// Where the menu's tint has landed on its ground. The canvas puts this at a fixed 22px rather
	// than a fraction, so a tall menu and a short one have the same lip rather than the same ramp.
	constexpr float MenuLipHeight = GroupHeaderHeight;

	// ---- Drag ghost -------------------------------------------------------------------------
	// The card that follows the cursor during a drag. It floats over the whole editor rather than
	// sitting in a panel, so it is the one surface in the tool that carries a drop shadow -- and
	// the shadow is offset down and right, which is what reads as "lifted" rather than "outlined".
	constexpr float DragGhostOpacity = 0.93f;
	constexpr float DragGhostThumbnailSize = 56.0f;
	constexpr float DragGhostPadding = 7.0f;
	constexpr float DragGhostTextGap = 9.0f;
	constexpr float DragGhostShadowInset = 4.0f;
	constexpr float DragGhostShadowOffsetX = 4.0f;
	constexpr float DragGhostShadowOffsetY = 5.0f;

	// ---- Badge ------------------------------------------------------------------------------
	// Fixed width, not hugging its text: the badges form a column down the right edge, and the
	// word changes without the column moving.
	constexpr float BadgeWidth = 53.0f;
	constexpr float BadgeHeight = 16.0f;
	// Longest word a badge is allowed to carry, and what BadgeWidth is sized for. The box does not
	// grow to fit its text -- that is the point, the marks have to form a straight column -- so a
	// longer word clips instead of widening, and the derivation tables are written against this.
	constexpr int32 BadgeMaxCharacters = 6;

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
	constexpr float TileBadgeHeight = 16.0f;
	// Two different insets: one sits on the picture, the other outside it between the image and
	// its selection outline.
	constexpr float TileTextInset = 4.0f;
	constexpr float TileImageInset = 3.0f;

	// A chip's inline thumbnail -- enough to confirm which asset is bound without opening the
	// picker, since the row already carries the name.
	constexpr float ChipThumbnailSize = 16.0f;
	constexpr float ChipHeight = 21.0f;
	constexpr float ChipGap = 5.0f;
	constexpr float ChipTextInset = 8.0f;

	// ---- Mask picker popover ----------------------------------------------------------------
	// Wider than the 300px inspector on purpose: a menu is its own window and is not clipped by
	// the panel that opened it.
	constexpr float MaskPickerWidth = 324.0f;
	constexpr int32 MaskPickerColumns = 4;
	constexpr int32 MaskPickerColumnsDense = 5;

	// ---- Layer stack ------------------------------------------------------------------------
	// The stack lives under the preview where vertical space is scarce, so these are the tightest
	// rows in the tool. Each value below is a separate decision -- a layer and its children are
	// deliberately different heights, and the indent is what carries the hierarchy now that no
	// connector rail is drawn between them.
	constexpr float LayerRowHeight = 35.0f;
	constexpr float LayerChildRowHeight = 27.0f;
	// The image, not a plate around it: layer thumbnails have no border, so this is the whole
	// footprint.
	constexpr float LayerThumbnailSize = 24.0f;
	// Children sit under the parent's name, clear of its eye and thumbnail.
	constexpr float LayerChildIndent = 35.0f;
	// Leading inset is larger than trailing: the eye needs room from the panel edge, while the
	// chevron on the right is already inset by its own slot padding.
	constexpr float LayerRowInsetLeading = 11.0f;
	constexpr float LayerRowInsetTrailing = 7.0f;
	// Between every element within a row -- eye to thumbnail, name to source, badge to chevron.
	// One value, so the row reads as evenly spaced rather than as clusters.
	constexpr float LayerItemGap = 8.0f;
	// The name sits closer to its thumbnail than the standard gap, so the two read as one unit
	// against the source text on the far side.
	constexpr float LayerNameInset = 5.0f;
	// Between stacked rows. One pixel: enough to separate, not enough to break the column.
	constexpr float LayerRowGap = 1.0f;
	constexpr float LayerEyeSize = 16.0f;
	constexpr float LayerChildIconSize = 13.0f;
	// The accent edge enclosing an open layer's children.
	constexpr float LayerEdgeWidth = OutlineWidth;
	constexpr float DropLineThickness = 3.0f;

	// ---- Type -------------------------------------------------------------------------------
	// Slate sizes are points rendered at 96 DPI, so pixels = points * 4/3. The design specifies
	// pixels: 10px body is 7.5pt, not 8pt (which lands at 10.67px). Float, because FSlateFontInfo
	// carries a float Size and the difference is visible at this scale.
	constexpr float FontBody = 10.0f;
	// A layer's name sits one step above body: it is what the eye lands on when scanning a stack,
	// and at body size it lost to the badge column beside it. 11px.
	constexpr float FontLayerName = 11.0f;
	constexpr float FontCaption = 8.0f;
	constexpr float FontTile = 8.0f;
	// Group headers: 8px, regular weight. Bold caps at this size closed up the letterforms and
	// fought the row labels below, which are larger.
	constexpr float FontGroupHeader = 8.0f;
	// A layer's source and its badge are both 8px and both secondary to the name, but they are
	// named apart from the group header so the stack can be retuned without touching panels.
	constexpr float FontLayerSource = 8.0f;
	constexpr float FontBadge = 8.0f;

	// Letter spacing is in 1/1000 em. Applied to the all-caps captions and group headers, where
	// tight caps are hard to read at this size.
	constexpr int32 CaptionLetterSpacing = 140;
	constexpr int32 GroupHeaderLetterSpacing = 160;
	// The layer source is caps too, but it runs alongside a mixed-case name rather than standing
	// alone, so it is opened up less -- full caption spacing made it the loudest thing in the row.
	constexpr int32 LayerSourceLetterSpacing = 60;
}
