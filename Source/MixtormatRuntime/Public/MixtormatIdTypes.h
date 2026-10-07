// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaskTypes.h"
#include "MixtormatOutputReference.h"
#include "MixtormatIdTypes.generated.h"

class UMixtormatSurface;

// Cluster IDs. Segments the surface into regions that follow its own structure and emits an
// integer ID per pixel -- a region label, not coverage.
//
// A Filter rather than a Mask because of what the output *is*. A mask child emits 0..1 coverage
// and composes through a BlendMode and a Weight, and every one of those operations is defined on
// coverage. Ask what Max of two ID maps means, or lerp(idA, idB, 0.5), and the answer is a third
// integer that labels nothing. So there is no BlendMode, no Weight and no shaping chain here:
// they would all be operations the data cannot support. FMixtormatColorIdMask is the other half
// of this -- selecting *from* an ID map is coverage and is correctly a mask.
//
// A Filter rather than an Effect because of where it runs. Effects run after the layer
// composites, which is why craquelure relief had to defer itself. This needs the surface maps as
// input and its output has to exist before the mask chain and the colour stage can read it, so it
// runs ahead of both.
//
// Regions come out of the image: there is no cell size, no cluster count and no compactness,
// because the sizes are whatever the height and roughness say they are. Two scales is a second
// instance at a wider Threshold, not a second algorithm.
// What a cluster filter segments.
//
// Two genuinely different questions, and which one is right depends on what the regions are for.
// Layer Surface finds the structure in this layer's own scan -- the bricks in the brick texture --
// regardless of what is under it. Composite Below finds the structure in the surface actually
// accumulated beneath this layer, so the regions follow what is visible there, including whatever
// every layer under this one contributed.
//
// It is also the noise control. A raw scan can be busy enough that the segmentation shatters into
// far more regions than the eye reads as pieces; the composited surface below has usually been
// through blending, height and grading and comes out calmer. Neither is the right answer in
// general, which is why this is a switch and not a default.
UENUM(BlueprintType)
enum class EMixtormatClusterSource : uint8
{
	LayerSurface UMETA(DisplayName = "Layer Surface"),
	CompositeBelow UMETA(DisplayName = "Composite Below")
};

// Scalar guides for bounded Surface IDs. Values are also the shader's feature indices.
UENUM(BlueprintType)
enum class EMixtormatSurfaceIdFeature : uint8
{
	Height = 0 UMETA(DisplayName = "Height"),
	Curvature = 1 UMETA(DisplayName = "Curvature / Shape"),
	Convex = 2 UMETA(DisplayName = "Convex Curvature"),
	Concave = 3 UMETA(DisplayName = "Concave Curvature"),
	NormalFlatness = 4 UMETA(DisplayName = "Normal Flatness"),
	Luminance = 5 UMETA(DisplayName = "Color / Luminance"),
	Roughness = 6 UMETA(DisplayName = "Roughness"),
	AO = 7 UMETA(DisplayName = "Ambient Occlusion"),
	Metallic = 8 UMETA(DisplayName = "Metallic")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatClusterFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs")
	bool bEnabled = true;

	// On the bottom layer there is nothing composited below, so Composite Below has only the flat
	// substrate to look at and would return a single region covering everything. It falls back to
	// the layer's own surface there rather than silently producing nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs")
	EMixtormatClusterSource Source = EMixtormatClusterSource::LayerSurface;

	// False on existing assets: keep the serialized Cluster algorithm unchanged.
	// New Surface IDs use the same producer slot and downstream ID consumers.
	UPROPERTY()
	bool bSurfaceIds = false;

	// Each connected island gets its own ID. Off: every area in the same band shares one ID,
	// even where the areas do not touch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs")
	bool bSplitIslands = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs")
	EMixtormatSurfaceIdFeature PrimaryFeature = EMixtormatSurfaceIdFeature::Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs")
	EMixtormatSurfaceIdFeature SecondaryFeature = EMixtormatSurfaceIdFeature::Roughness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FeatureMix = 0.0f;

	// Stencil radius, not a dense 64x64 convolution. Used by curvature and normal flatness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs", meta = (UIMin = "1", UIMax = "64"))
	int32 FormScale = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs", meta = (UIMin = "0", UIMax = "2"))
	int32 GuideBlur = 1;

	// Occupied bands are compacted to consecutive IDs. Disconnected equal bands share an ID.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs", meta = (UIMin = "2", UIMax = "256"))
	int32 MaxIds = 64;

	// Grayscale closing of the continuous guide, applied only at quantized ID edges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface IDs", meta = (UIMin = "0", UIMax = "2"))
	int32 EdgeClose = 1;

	bool CanSampleSurface(const UMixtormatSurface* Surface) const;

	// Band width, and so region granularity: the scale control, and the only one there is.
	//
	// Roughness is quantised into bands of this width and two neighbours join only if they land in
	// the same band. Read against a 0..1 signal after the height and roughness have both been
	// renormalised, which is what makes one number mean the same thing on every scan instead of
	// drifting with whatever range that particular texture's height happened to occupy. Scaled by
	// 0.25 inside the kernel, so the dial spans roughly four bands to hundreds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Threshold = 0.33f;

	// Where the band boundaries fall. Same granularity, different partition -- a reseed that moves
	// every boundary without changing the character, which is the control Threshold cannot be.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Offset = 0.0f;

	// How strongly a step in height blocks a merge that roughness would otherwise allow. The
	// criterion is two-channel: same roughness band *and* |dHeight| * this <= Threshold, so a
	// region has to be uniform in roughness and not step in height. At 0 it is roughness bands
	// alone, which is the right answer on a surface whose height carries no region structure.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (UIMin = "0.0", UIMax = "4.0"))
	float HeightInfluence = 1.0f;
};

// Per-region HSV variation. Takes the layer's albedo and a cluster filter's ID map, and rewrites
// the colour so every region is a slightly different one.
//
// A Filter for the same reason the cluster is: the output is albedo, not coverage. It has no
// BlendMode and no Weight because there is no mask chain to join -- it replaces what the layer's
// colour is, the way HueShift and Saturation on the layer already do, rather than deciding where
// the layer lands. It is applied at the composite's own albedo sample, immediately before the
// layer's global HueShift/Saturation/Value: per-region jitter first, then the whole-layer grade
// on top, which is the order that reads as "one material, varied" rather than two gradings
// fighting.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it passes the
// albedo through untouched.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatHsvIdFilter
{
	GENERATED_BODY()

	// Same cap and same reason as FMixtormatColorIdMask::MaxColors: a fixed constant array in the
	// shader and a fixed set of rows in the inspector, both of which have to agree with this.
	static constexpr int32 MaxPaletteColors = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs")
	bool bEnabled = true;

	// The palette regions draw from, sampled as a ramp: entries are evenly spaced stops and a
	// region lands anywhere between two of them.
	//
	// This is the half that matters, and it is what pure HSV jitter cannot do. Jitter can only
	// wander from wherever the texture already sits; a palette lets the variation be *aimed* --
	// brick reds through ochres, or the green-to-grey of weathered copper -- and then jittered
	// around that. Empty means no tint, and the jitter rows below still apply.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs")
	TArray<FLinearColor> Palette;

	// How far a region tints toward the colour it sampled, as a random amount in this range.
	// A range rather than one number so most regions can sit near the texture's own colour with
	// a few pulled much further toward the palette.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float RampMixMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float RampMixMax = 0.15f;

	// -- Jitter ---------------------------------------------------------------------------------
	// Min/max pairs rather than a single +/- amount, so variation can be *biased*: hue 0 to 0.1
	// shifts only warm. A symmetric amount cannot express that.
	//
	// Hue is added because it is circular; saturation and value are multiplied because they are
	// magnitudes. Keep all of it small -- a couple of percent of hue is already clearly visible on
	// a flat surface, and the target is "same kiln, different firing", not a rainbow.

	// -1..1 maps to +/-180 degrees, the same convention as FMixtormatLayer::HueShift.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float HueMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float HueMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "0.0", UIMax = "4.0"))
	float SaturationMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "0.0", UIMax = "4.0"))
	float SaturationMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "0.0", UIMax = "4.0"))
	float ValueMin = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (UIMin = "0.0", UIMax = "4.0"))
	float ValueMax = 1.0f;

	// Reshuffles which region gets which colour without changing the ranges. Five decorrelated
	// randoms come off one hash of the ID and this seed -- palette position, tint amount, hue,
	// saturation and value -- so moving it moves all five together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (UIMin = "0"))
	int32 Seed = 1;
};

UENUM(BlueprintType)
enum class EMixtormatIdCombineMode : uint8
{
	// A pair of neighbouring regions draws once between them and joins if it falls under Amount.
	// Every boundary in the map is a candidate, so the result reads as a coarser version of the
	// same pattern -- the shapes stay in family, there are simply fewer and bigger ones.
	Merge UMETA(DisplayName = "Merge"),
	// Whole regions are drawn instead, and a chosen one dissolves into whatever it borders. The
	// survivors keep their original outline exactly, so this reads as pieces being taken out of
	// the map rather than the whole map being coarsened.
	Subtract UMETA(DisplayName = "Subtract")
};

// Fewer, larger regions, from the ID map above this node.
//
// Sits between an ID creator and whatever consumes it -- a cluster filter or a pattern upstream,
// an HSV tint, random values, a ramp or a peel downstream -- and rewrites the map in place. It is
// the only ID node that both reads and writes one, which is what makes it stackable: several in a
// row keep coarsening, and everything downstream still just sees an ID map.
//
// Merges neighbours, never arbitrary pairs. Merging by hash bucket alone would give two regions
// on opposite sides of the surface the same number without either becoming larger; joining
// neighbours grows genuinely bigger contiguous cells, which is what an unsubdivide has to do if
// the result is going to drive colour variation or relief.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCombineIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs")
	bool bEnabled = true;

	// Chance that any one candidate takes. 0 passes the map through untouched, 1 collapses every
	// region that touches another into a single one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Amount = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (UIMin = "0"))
	int32 Seed = 0;

	// How many rounds of merging run, which is the unsubdivide depth.
	//
	// Each round draws with its own salt and tests the regions the previous round produced, so
	// raising it keeps coarsening instead of re-deciding the same pairs. Two rounds at a low
	// Amount is a different picture from one round at a high one: the first grows clusters of
	// clusters, the second grows one big one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (UIMin = "1", UIMax = "8"))
	int32 Passes = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs")
	EMixtormatIdCombineMode Mode = EMixtormatIdCombineMode::Merge;
};

UENUM(BlueprintType)
enum class EMixtormatIdGroupMode : uint8
{
	Difference = 0 UMETA(DisplayName = "Difference"),
	MaxId = 1 UMETA(DisplayName = "Max ID"),
	Pair = 2 UMETA(DisplayName = "Pair"),
	MinId = 3 UMETA(DisplayName = "Min ID")
};

// Ordered live inputs are ordinary RegionIds OutputReference child rows whose
// ScopeOwnerChildId names this group's child. Fold them in authored row order.
// Legacy nested Region-ID producers remain supported; no input payload is duplicated here.
// Difference preserves equal IDs and hashes unequal overlaps; Max/Min select valid IDs.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatIdGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ID Group")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ID Group")
	EMixtormatIdGroupMode Mode = EMixtormatIdGroupMode::Difference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ID Group", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "16"))
	int32 BoundaryWidth = 1;
};

// Serialized by value. Append new modes; never reorder or insert between existing entries.
UENUM(BlueprintType)
enum class EMixtormatPatternMode : uint8
{
	Grid = 0 UMETA(DisplayName = "Grid"),
	RunningBond = 1 UMETA(DisplayName = "Running Bond"),
	Herringbone = 2 UMETA(DisplayName = "Herringbone"),
	Basketweave = 3 UMETA(DisplayName = "Basketweave"),
	Hex = 4 UMETA(DisplayName = "Hex"),
	OctagonSquare = 5 UMETA(DisplayName = "Octagon + Square"),
	Flagstone = 6 UMETA(DisplayName = "Flagstone"),
	Voronoi = 7 UMETA(DisplayName = "Voronoi"),
	Hopscotch = 8 UMETA(DisplayName = "Hopscotch"),
	FrenchAshlar = 9 UMETA(DisplayName = "French / Modular Ashlar"),
	FracturePlates = 10 UMETA(DisplayName = "Fracture Plates")
};

UENUM(BlueprintType)
enum class EMixtormatGridMode : uint8
{
	Straight = 0 UMETA(DisplayName = "Straight"),
	Staggered = 1 UMETA(DisplayName = "Staggered"),
	Diamond = 2 UMETA(DisplayName = "Diamond / 45 Degree")
};

// Procedural region producer. A periodic rectangular lattice and the jittered Voronoi end of the
// same solver publish the same sparse pixel-index IDs as Cluster IDs, so HSV/Random/Ramp From IDs
// consume either producer without a special path.
//
// Rows/Columns/RowOffset/Jitter are the pattern vocabulary. The relief and UV blocks are optional
// intrinsic consumers of the same regions: zero relief leaves height/normal/RAM untouched, and UV
// variation off leaves the layer's source mapping untouched.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatPatternFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	EMixtormatPatternMode PatternMode = EMixtormatPatternMode::Grid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	EMixtormatGridMode GridMode = EMixtormatGridMode::Straight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "1", UIMax = "256"))
	int32 Rows = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "1", UIMax = "256"))
	int32 Columns = 8;

	// Fraction of one cell shifted per row. The shader quantises this just enough for the final row
	// to meet the first row periodically; 0.5 is running bond when the row count closes on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "1.0"))
	float RowOffset = 0.0f;

	// 0 keeps feature points at cell centres; 1 gives the full periodic Voronoi solve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Jitter = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	bool bSwapAxes = false;

	// Rounds the cell corners by blending the two nearest walls instead of taking a hard minimum,
	// so a chamfer run off the edge distance fillets into the corner instead of creasing. In cell
	// fractions, so it means the same thing on a large lattice and a small one. 0 is the true
	// Voronoi corner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Rounding = 0.0f;

	// Whether Edge Width and the chamfer are measured as a fraction of the cell rather than in
	// output pixels. Relative frames every cell the same way whatever its size or aspect;
	// absolute keeps an even visual width across cells of different sizes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges")
	bool bRelativeEdgeWidth = false;

	// Region-less grout. Consumers already treat MIXTORMAT_INVALID_REGION as pass-through/no mask.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "64.0"))
	float GapPixels = 1.0f;

	// Where the grout sits relative to the cells. Negative sinks it into a trench, positive stands
	// it proud as a raised mortar line. Inert at Gap 0, since there is then nothing outside the
	// IDs to move.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float GapHeight = -1.0f;

	// Varies the grout once per piece rather than once per surface, for every Pattern Mode.
	//
	// Cut per piece, not per wall: the wall stays exactly where the topology put it and each side
	// of it pulls back by its own draw, so the visible grout between two pieces is the sum of two
	// independent amounts. A per-wall gap would need a neighbour id, which only the cellular modes
	// can produce; this asks nothing of the mode beyond the edge distance every one of them
	// already publishes. Symmetric about Gap, the same convention as the bevel's Variation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "1.0"))
	float GapRandom = 0.0f;

	// Spends that same grout unevenly around the piece instead of ringing it: the piece sits off
	// centre in its own socket, hard against one wall with the space behind it. Bounded by the
	// piece's own half-gap, so a piece can never cross into its neighbour -- which is also why
	// both of these are inert at Gap 0, where there is no space to sit off centre in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "0.0", UIMax = "1.0"))
	float GapSlide = 0.0f;

	// Per-region source UV variation. Pattern is the first producer that knows an analytic centre,
	// so this stays Pattern-only until Cluster IDs exposes equivalent region bounds/centres.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bUVVariation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bOrthogonalUV = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (UIMin = "-360.0", UIMax = "360.0"))
	float UVRotationMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (UIMin = "-360.0", UIMax = "360.0"))
	float UVRotationMax = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (UIMin = "0.05", UIMax = "8.0"))
	float UVScaleMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (UIMin = "0.05", UIMax = "8.0"))
	float UVScaleMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (UIMin = "0.0", UIMax = "1.0"))
	float UVOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bRandomFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bRandomFlipV = false;

	// Per-cell elevation, not per-cell tilt: every cell is a flat face sitting somewhere off the
	// base. Tilting a region is Ramp From IDs' job, and that filter composites over this one --
	// chamfer and settle here, slope there.
	//
	// Height is the elevation every cell gets; Height Random is how far below it a cell may be
	// drawn, as a multiplier on Height. One-sided on purpose: a cell that went below the base
	// would feather back up at its own wall and read as a recessed panel in a raised frame.
	// At Height Random 0 every cell sits at full Height; at 1 they spread down to the base.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightRandom = 0.5f;

	// The chamfer's cross-section, from the grout line up to the flat of the cell. -1 is a cove
	// that hugs the grout then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose
	// that lifts away and rounds over onto the face. Both ends of the curve stay pinned, so this
	// changes the chamfer's shape and never its width or height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Profile = 0.25f;

	// Offsets the roundness per cell rather than scaling it, so one cell's bullnose can be its
	// neighbour's cove -- which scaling a signed control could never produce.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ProfileRandom = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "0.5"))
	float Feather = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FeatherRandom = 0.5f;

	// What the feather does on the way up, rather than how wide it is.
	//
	// The run-out is a straight line, and a straight line is the one shape a normal map cannot
	// show -- a normal reads a change in slope, and a constant ramp has none, so the band lights
	// as a single flat facet. Gain bends it: the slope leaving the wall goes from 1 to 1 + Gain,
	// and past 1 the curve arcs above the face and leaves a raised lip just inside the edge. Both
	// ends stay pinned, so the grout wall and the flat face never move.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (UIMin = "0.0", UIMax = "4.0"))
	float FeatherGain = 0.0f;

	// Edge relief and shading share one edge-distance field. Width is in output pixels, so the
	// bevel remains visually even when Rows and Columns make non-square cells.
	//
	// Signed, and it lifts the cell *face*: positive stands the cell proud of the grout with the
	// chamfer ramping down to it, negative sinks the face below the grout instead. The gap itself
	// is untouched either way -- that is Gap Height's job, so the two compose rather than fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BevelHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "0.25", UIMax = "64.0"))
	float BevelWidthPixels = 4.0f;

	// The same width for Relative mode, as a fraction of the way from the cell wall to its
	// deepest interior point. Separate from the pixel value because the two need different
	// ranges to be draggable at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BevelWidthCells = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BevelVariation = 0.0f;

	// Slides the chamfer band across the grout line, in output pixels. Negative puts it out in
	// the gap, positive pulls it onto the cell face, zero starts it exactly at the gap wall.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "-32.0", UIMax = "32.0"))
	float BevelInsetPixels = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "0.0", UIMax = "1.0"))
	float EdgeRoughness = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "0.0", UIMax = "1.0"))
	float EdgeRoughnessAmount = 0.5f;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs", meta = (UIMin = "0"))
	int32 Seed = 1;

	// ---------------------------------------------------------------------------------------
	// Fracture Plates. Read only when PatternMode is FracturePlates; every other mode ignores
	// them, so these are inert on an existing asset.
	//
	// The mode's primary lattice is the shared Columns/Rows pair above, its seed placement is the
	// shared Jitter, and its shuffle is the shared Seed -- it is a Pattern topology like every
	// other one, not a second pattern system bolted alongside.
	// ---------------------------------------------------------------------------------------

	// Spread of the additive power weights that decide how much territory a plate wins from its
	// neighbours. This is where "some very large regions, some small" comes from: at 0 every plate
	// is the same importance and the result is pavement, and raising it grows a few plates at the
	// expense of the rest. Additive rather than multiplicative on purpose -- an additive weight
	// moves a boundary while leaving it straight, a multiplicative one bows it into an arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FractureSizeVariation = 0.3f;

	// Probability that a primary plate fractures internally at all. A plate that does not stays
	// whole and publishes one ID, so this is the control for how much of the surface reads as
	// large unbroken pieces against locally shattered ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FractureSecondaryAmount = 0.5f;

	// How many pieces a fracturing plate breaks into, drawn per plate. Both ends are structural
	// indices rather than artistic amounts, so they are range-clamped: below 2 is not a fracture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "2", UIMax = "8"))
	int32 FractureSecondaryMin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "2", UIMax = "8"))
	int32 FractureSecondaryMax = 3;

	// How far the secondary sites sit from their parent's, in cell fractions. Small values put
	// the split near the middle of the plate; large ones push the pieces toward its walls.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.05", UIMax = "0.75"))
	float FractureSecondaryRadius = 0.34f;

	// Breaks up the even ring the secondary sites are laid on, in angle and in radius, so a split
	// plate does not come out as a regular pie.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FractureSecondaryJitter = 0.55f;

	// How far, in output pixels, the fracture field displaces a plate wall from the straight line
	// the power diagram would give it. The displacement field is piecewise planar, so the wall
	// stays a chain of straight runs meeting at angles rather than becoming a curve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "32.0"))
	float FractureEdgeIrregularity = 8.0f;

	// The run length of those straight segments, in output pixels. Absolute: Columns and Rows do
	// not stretch it, so changing the plate count leaves the crack character alone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "8.0", UIMax = "512.0"))
	float FractureEdgeScale = 96.0f;

	// A second, shorter octave of the same field: the small branching kinks that sit on the long
	// primary fracture runs. Also absolute, in output pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "16.0"))
	float FractureEdgeDetail = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "4.0", UIMax = "128.0"))
	float FractureEdgeDetailScale = 24.0f;
};

// Per-region tilt from a gradient. Gives every region its own local frame, runs a linear ramp
// across it at a random angle, and uses that to lift one side of the region and sink the other in
// the composited height and normal.
//
// This is what stops tiles, cobbles and shards lying in the same plane. Overlaying a noise varies
// pixels; this varies pieces, so the surface reads as individually settled fragments rather than
// one flat sheet with a texture on it.
//
// A Filter by category and by menu, but it owns a pass that runs *after* the layer composites --
// the same shape as craquelure, which builds its network before the composite and defers its
// relief until the height it is carving actually exists. Deliberately not an Effect: effects here
// are asset-backed, picked from a registry, and there is no asset a per-region tilt points at.
//
// The local frame is recovered from the ID itself: an ID is the linear index of its region's root
// pixel, so the root's position comes for free and every bounding box is measured relative to it.
// That is why nothing compacts these IDs to a dense range -- doing so would destroy the one thing
// this node needs from them.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it does
// nothing.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRampIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs")
	bool bEnabled = true;

	// -- Relief ---------------------------------------------------------------------------------
	// The composited ramp height is authoritative for its derived normal.

	// How strongly each region's ramp meets the surface. A blend weight, not a tilt amount: at 0
	// the surface is untouched under every blend mode, not only the additive ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief", meta = (UIMin = "0.0", UIMax = "0.5"))
	float HeightAmount = 0.05f;

	// Per-region jitter on that strength. Base plus jitter rather than range times amount, unlike
	// Pattern IDs' Height pair: a uniform ramp strength is meaningful on its own -- every region
	// tilts equally, each in its own direction -- so 0 here leaves every region at full strength
	// rather than switching the node off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float IntensityRandom = 0.0f;


	// How the ramp meets the height under it. AddSub is the centred case this node used to
	// hard-code, and is the default so existing materials keep their look.
	//
	// The normal is derived from the blended height rather than blended separately, so it always
	// describes the surface that was actually written -- under Min and Multiply a separately
	// blended normal would not.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::AddSub;

	// -- Gradient -------------------------------------------------------------------------------

	// Whether each region's gradient runs in its own random direction. Off puts every gradient on
	// the same axis, which reads as a comb rather than as settling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient")
	bool bRotateRandom = true;

	// Quantises each region's angle, so a lattice reads as deliberately laid rather than
	// scattered. Applied to the true screen-space angle, which is the only place a degree step
	// means what it says.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient")
	bool bAngleStepping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient", meta = (UIMin = "1.0", UIMax = "90.0"))
	float AngleStepDegrees = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs", meta = (UIMin = "0"))
	int32 Seed = 1;
};

// Per-region source UV placement, from whatever Region IDs sit above it.
//
// The UV half of what Pattern IDs used to own, lifted out so it reads any producer: Pattern IDs,
// Cluster IDs, Combine IDs, or anything else that publishes a sparse Region ID map. Pattern's own
// UV block stays where it is for materials authored against it -- see FMixtormatPatternFilter --
// and this node shadows it when both are present in one layer.
//
// The region frame is derived, not inherited. Every producer's ID is the linear index of its
// region's root pixel, so the root's position comes for free; the bounding box measured relative
// to it across the torus gives a centre for any region at all, without asking the producer for an
// analytic one. That frame is axis-aligned by construction: this node does not claim to know
// which way an arbitrary blob "points", and Orthogonal below is a snap on the *random* rotation
// rather than a recovered intrinsic basis.
//
// Writes nothing but the source mapping. Region IDs, height, normal, AO and the mask chain are
// all left exactly as they were.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatUvIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs")
	bool bEnabled = true;

	// Snaps the drawn rotation to quarter turns. On a lattice that reads as tiles laid square and
	// turned, rather than scattered; off gives the full continuous draw between the two limits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform")
	bool bOrthogonal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "-360.0", UIMax = "360.0"))
	float RotationMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "-360.0", UIMax = "360.0"))
	float RotationMax = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "0.05", UIMax = "8.0"))
	float ScaleMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "0.05", UIMax = "8.0"))
	float ScaleMax = 1.0f;

	// How far a region's source read may wander from its own centre, per axis, as a fraction of
	// the source tile. Per-axis where Pattern's single Offset was not: the two axes of a plank or
	// a brick rarely want the same slip, and one scalar could only ever move both together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "0.0", UIMax = "1.0"))
	float OffsetU = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform", meta = (UIMin = "0.0", UIMax = "1.0"))
	float OffsetV = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform")
	bool bRandomFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs|Transform")
	bool bRandomFlipV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV From IDs", meta = (UIMin = "0"))
	int32 Seed = 1;
};

// An independent Region-ID consumer. An unassigned source reads the nearest preceding valid
// Region IDs; an assigned source is a typed reference, never a scalar mask or an ID Group setting.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBoundaryIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs")
	FMixtormatOutputReference RegionIdsSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0", Delta = "0.01"))
	float WidthPixels = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float Softness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0", Delta = "0.01"))
	float GapWidthPixels = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GapSoftness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (UIMin = "-64.0", UIMax = "64.0", Delta = "0.01"))
	float GapBiasPixels = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs", meta = (ClampMin = "0.25", UIMin = "0.25", UIMax = "256.0", Delta = "0.01"))
	float DistanceRangePixels = 64.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary From IDs")
	bool bInvertDistance = false;
};

// Per-region elevation, chamfer and edge shading, from whatever Region IDs sit above it.
//
// The relief half of what Pattern IDs used to own. Pattern published an analytic edge-distance
// field alongside its IDs and the relief pass read that; an arbitrary producer publishes no such
// thing, so this node derives the equivalent field from the ID map itself -- a jump-flooded
// distance to the nearest pixel carrying a different (or invalid) ID. Downstream of that the
// arithmetic is the same pass Pattern relief has always run through, so a Pattern source
// reproduces the Pattern look and a Cluster or Combine source gets the same treatment honestly
// rather than by approximation.
//
// Gap *width* is topology and stays on the producer. Gap Height is appearance and lives here:
// the two compose instead of fighting over the same pixels.
//
// Writes height, the normal derived from it through the shared height-to-normal route, and the
// roughness/AO the edge shading has always provided. Region IDs and UVs are left untouched.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatReliefIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs")
	bool bEnabled = true;

	// -- Height ---------------------------------------------------------------------------------

	// The elevation every region gets, and how far below it a region may be drawn as a multiplier
	// on that. One-sided on purpose, exactly as Pattern's pair is: a region that went below the
	// base would feather back up at its own wall and read as a recessed panel in a raised frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Height", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Height", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightRandom = 0.5f;

	// -- Profile --------------------------------------------------------------------------------

	// The chamfer's cross-section, from the boundary up to the flat of the region. -1 is a cove,
	// 0 a straight flat chamfer, +1 a bullnose. Both ends stay pinned, so this changes the
	// chamfer's shape and never its width or height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Profile", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Profile = 0.25f;

	// Offsets the roundness per region rather than scaling it, so one region's bullnose can be
	// its neighbour's cove -- which scaling a signed control could never produce.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Profile", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ProfileRandom = 0.5f;

	// Eases each region's elevation out at its own boundary, so neighbours at different heights
	// meet through a ramp rather than a one-texel cliff. In region fractions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Profile", meta = (UIMin = "0.0", UIMax = "0.5"))
	float Feather = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Profile", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FeatherRandom = 0.5f;

	// What the feather does on the way up, rather than how wide it is. The run-out is a straight
	// line and a straight line is the one shape a normal map cannot show; Gain bends it, and past
	// 1 leaves a raised lip just inside the edge. Both ends stay pinned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Profile", meta = (UIMin = "0.0", UIMax = "4.0"))
	float FeatherGain = 0.0f;

	// -- Bevel ----------------------------------------------------------------------------------

	// Signed, and it lifts the region *face*: positive stands the region proud of the boundary
	// with the chamfer ramping down to it, negative sinks the face instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BevelHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "0.25", UIMax = "64.0"))
	float BevelWidthPixels = 4.0f;

	// The same width for Relative mode, as a fraction of the way from the region boundary to its
	// deepest interior point. Separate from the pixel value because the two need different ranges
	// to be draggable at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BevelWidthCells = 0.25f;

	// Whether the chamfer width and the feather are measured as a fraction of the region rather
	// than in output pixels. Relative frames every region the same way whatever its size;
	// absolute keeps an even visual width across regions of different sizes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel")
	bool bRelativeWidth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BevelVariation = 0.0f;

	// Slides the chamfer band across the region boundary, in output pixels. Negative puts it
	// outside, positive pulls it onto the face, zero starts it exactly at the boundary.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "-32.0", UIMax = "32.0"))
	float BevelInsetPixels = 0.0f;

	// Where a region-less band -- Pattern's grout, or any pixel the producer marked invalid --
	// sits relative to the regions. Negative sinks it into a trench, positive stands it proud.
	// Inert when every pixel belongs to a region, since there is then nothing outside the IDs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Bevel", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float GapHeight = 0.0f;

	// -- Edge -----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Edge", meta = (UIMin = "0.0", UIMax = "1.0"))
	float EdgeRoughness = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs|Edge", meta = (UIMin = "0.0", UIMax = "1.0"))
	float EdgeRoughnessAmount = 0.5f;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relief From IDs", meta = (UIMin = "0"))
	int32 Seed = 1;
};
