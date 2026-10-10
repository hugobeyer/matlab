// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatHeightTypes.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatColorRamp.h"
#include "MixtormatOutputReference.h"
#include "MixtormatGeneratorTypes.generated.h"

// Which generator an FMixtormatGenerator carries.
//
// Serialised by value on the child. Append only -- a new generator takes the next number and
// brings its own payload struct; nothing existing moves.
UENUM(BlueprintType)
enum class EMixtormatGeneratorType : uint8
{
	StrataCarver UMETA(DisplayName = "Strata Carver"),
	// Slot 1 was Fracture. Cracks replaces it in place; Config/DefaultMixtormat.ini carries the
	// name redirect, so a saved Fracture child loads as Cracks at its defaults.
	Cracks UMETA(DisplayName = "Cracks"),
	// Appended: serialized by value.
	RockFormation UMETA(DisplayName = "Rock Formation"),
	Pebbles UMETA(DisplayName = "Pebbles"),
	CliffStrata UMETA(DisplayName = "Cliff Strata"),
	// Appended: a plain field producer. It publishes Value / Gradient / IDs and decides nothing
	// about what they mean -- height, roughness, mask and the rest are downstream decisions.
	Noise UMETA(DisplayName = "Noise")
};

// How a pebble's cut planes are oriented.
// Generators that can own Shape Deform / Generator Flow / Flow Carve / Gravity Flow.
// Noise and Cliff Strata support Height steering; boundary controls require a signed boundary field.
// One list for runtime gather, GPU passes and editor placement.
inline bool MixtormatCanOwnGeneratorFlow(const EMixtormatGeneratorType Type)
{
	return Type == EMixtormatGeneratorType::StrataCarver
		|| Type == EMixtormatGeneratorType::RockFormation
		|| Type == EMixtormatGeneratorType::Pebbles
		|| Type == EMixtormatGeneratorType::Cracks
		|| Type == EMixtormatGeneratorType::CliffStrata
		|| Type == EMixtormatGeneratorType::Noise;
}

inline bool MixtormatGeneratorHasFlowBoundary(const EMixtormatGeneratorType Type)
{
	return MixtormatCanOwnGeneratorFlow(Type)
		&& Type != EMixtormatGeneratorType::Noise
		&& Type != EMixtormatGeneratorType::CliffStrata;
}

UENUM(BlueprintType)
enum class EMixtormatPebbleDirection : uint8
{
	Stratified UMETA(DisplayName = "Stratified"),
	Random UMETA(DisplayName = "Random"),
	OpposedPairs UMETA(DisplayName = "Opposed Pairs"),
	Golden UMETA(DisplayName = "Golden Angle"),
	Axis UMETA(DisplayName = "Axis Biased")
};

// Strata Carver: coherent folded interfaces, hard shelves, recessed soft beds and joint-cut
// slabs. Interface offsets are bounded so beds cannot cross. One field pass publishes signed
// relief, bed IDs, position inside each bed and stable per-bed random.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatStrataCarver
{
	GENERATED_BODY()

	// Generator height contract: zero is neutral. Shared normalization preserves zero;
	// Height Scale is applied afterwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Output")
	bool bStrataNormalizeHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float StrataHeightScale = 1.0f;

	// Draws interfaces, bed hardness, slab joints, cross-bedding and the shared fold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0"))
	int32 Seed = 3;

	// How far the strata relief reaches, in the same units as the layer's height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Depth = 0.25f;

	// Beds across the tile along the bedding's own direction. Rounded to a whole number so the
	// column closes on the tile, and it is also how many distinct beds are drawn before they
	// repeat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "1.0", UIMax = "64.0"))
	float StrataFrequency = 6.0f;

	// Direction of the bedding, in degrees. Snaps to the nearest tileable lattice angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "360.0"))
	float StrataRotation = 0.0f;

	// Varies interface spacing and lateral thickness. The shader saturates to keep beds ordered.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ThicknessVariation = 0.5f;

	// Per-bed shelf elevation variation, independent of the hard/soft material contrast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightVariation = 0.5f;

	// Sharpens and narrows the shoulders between a shelf and its shared bed seams.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (DisplayName = "Edge Sharpness", UIMin = "0.0", UIMax = "1.0"))
	float Verticality = 0.7f;

	// Serialized data only. The ramp algorithm has been replaced, not retained as another mode.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Strata now uses geological shelves; use LedgeWidth."))
	float RampShape = 0.0f;

	// Wider ledges leave more of each bed as a planar shelf. Hardness also affects shoulder width.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float LedgeWidth = 0.65f;

	// 0 gives all beds equal hardness; 1 gives the full per-bed hard/soft contrast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float HardnessContrast = 0.75f;

	// Recesses soft beds and shared seams without changing their IDs or spacing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float SoftRecession = 0.65f;

	// Shared fold amplitude in tile widths, and its integer frequency along the bedding.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "0.25"))
	float Bend = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "1", UIMax = "8"))
	int32 BendScale = 2;

	// Joint-cut depth, rim chipping and planar slab displacement. Zero leaves continuous shelves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (DisplayName = "Slab Breakup", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float Breakup = 0.35f;

	// Slabs per primitive strike repeat. Integer counts keep the joint network tileable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "1", UIMax = "16"))
	int32 JointScale = 4;

	// Joint width as a fraction of nominal slab spacing. Zero closes cuts but retains slab relief.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "0.25", Delta = "0.005"))
	float JointWidth = 0.035f;

	// How many beds the upstream composite height shifts the bedding by; zero ignores it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "16.0"))
	float HeightFollow = 0.0f;

	// Fine laminae, stronger in soft beds and filtered by pixel footprint; cross-bedding tilts them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Lamination = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "3.0"))
	float CrossBedding = 1.0f;

	// 0 ignores any mask scoped under this generator entirely. 1 lets it scale where the strata
	// act.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MaskInfluence = 1.0f;

	// How much Region IDs above this generator vary the relief per region. Exactly zero at 0 --
	// the shader branches rather than multiplying by zero, so a stack with no ID producer and a
	// stack with one at influence 0 are bit-identical.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (UIMin = "0.0", UIMax = "1.0"))
	float IDInfluence = 0.0f;
};

UENUM(BlueprintType)
enum class EMixtormatRockHeightMode : uint8
{
	Raw = 0 UMETA(DisplayName = "Raw"),
	Analytic = 1 UMETA(DisplayName = "Analytic"),
	Measured = 2 UMETA(DisplayName = "Measured")
};

// Rock Formation: tileable BSP-fractured, tilted slabs with jagged rims, faceted tops,
// chamfers and walls. Height mode and scale consume the cached field without reshaping it.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRockFormation
{
	GENERATED_BODY()

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation|Output")
	bool bRockNormalizeHeight = true;

// 0..1 spans preset positions 1..2.5: layered through boulder toward rubble.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockStyle = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "1", UIMax = "32"))
	int32 RockCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "1", UIMax = "32"))
	int32 RockRows = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation")
	int32 RockSeed = 1;

	// Scales the preset's BSP split count per cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockFracture = 1.0f;

	// Share of each edge's room to the chunk centre; seams take a fixed smaller share.
	// Signed: positive cuts the bevel in (current), zero is no chamfer, negative raises a lip.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockChamfer = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockFractureHeightBias = 0.0f;

	// Domain offset before height shaping: positive shrinks chunks, negative expands them.
	// Zero preserves packing; 1 is twice the natural gap, with a positive-only one-pixel floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockGap = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockChamferRandom = 1.0f;

	// Normalised spin: +/-1 is +/-180 degrees, fitted back into the leaf rather than clipped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockSpin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockSpinRandom = 0.0f;

	// +/-1 is +/-45 degrees; pieces share their rock's lean with a small random deviation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockTiltAngle = 0.1f;

	// 0..1 is a full turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockTiltDirection = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockTiltRandom = 0.2f;

	// Per-cell weights shift the borders while preserving the seamless partition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockSizeRandom = 0.0f;

	// 0 is neutral; +/-1 doubles cells along/across Stretch Angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockStretch = 0.5f;

	// 0..1 is half a turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockStretchAngle = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockStretchRandom = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockHeightClusters = 0.5f;

	// Diagonal row offset, in cell widths.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockSkew = 0.5f;

	// Jag sizes follow each chunk; seams use fixed strength and frequency ratios.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockEdgeJag = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (ClampMin = "1.0", UIMin = "1.0", Delta = "0.01"))
	float RockJagScale = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockJagDetail = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockChamferJag = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockRimChips = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float RockRimChipSize = 0.075f;

	// Depth of faceted cuts into each chunk. Signed: positive cuts in (current), zero is flat,
	// negative raises the facets out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockFacetChips = 0.5f;

	// Each round adds two planes; falloff above 1 grows later rounds instead of shrinking them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0"))
	int32 RockFacetIterations = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float RockFacetFalloff = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RockFacetRandom = 1.0f;

	// +1 cuts toward the lean's low side, -1 toward its high side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float RockFacetAlign = 0.75f;

	// Deprecated: the shared signed normalization supersedes the per-generator height mode. Kept
	// only so existing assets load; it is never read.
	UPROPERTY()
	EMixtormatRockHeightMode RockHeightMode = EMixtormatRockHeightMode::Analytic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float RockHeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float RockDepthMin = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rock Formation|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float RockDepthMax = 1.0f;
};

// Pebbles: faceted, chamfered stones scattered on a tileable jittered grid. Each stone has its
// own seed, size, rotation and height; overlaps keep the highest. The field depends on these
// settings only (not Amount), so it is cached.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatPebbles
{
	GENERATED_BODY()

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Output")
	bool bPebbleNormalizeHeight = true;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles")
	int32 PebbleSeed = 1;

	// Stones per row; the tile is Cells x Cells.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "1", UIMax = "32"))
	int32 PebbleCells = 4;

	// Chance a cell has a stone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float PebbleDensity = 1.0f;

	// Position randomness inside the cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float PebbleJitter = 0.7f;

	// Stone size; 1 fills a cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "0.1", UIMax = "2.0", Delta = "0.01"))
	float PebbleScale = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float PebbleScaleVariation = 0.5f;

	// Maximum random rotation, degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles", meta = (UIMin = "0.0", UIMax = "180.0", Delta = "1.0"))
	float PebbleRotation = 180.0f;

	// Planes per stone; more reads rounder.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Shape", meta = (UIMin = "3", UIMax = "24"))
	int32 PebbleCuts = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Shape")
	EMixtormatPebbleDirection PebbleDirection = EMixtormatPebbleDirection::Stratified;

	// Master jitter: radius, chamfer, cut angle and cut count.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Shape", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float PebbleIrregularity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Shape", meta = (UIMin = "0.0", UIMax = "0.2", Delta = "0.001"))
	float PebbleChamfer = 0.02f;

	// Facet rise per unit inward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Height", meta = (UIMin = "0.0", UIMax = "16.0", Delta = "0.05"))
	float PebbleSteepness = 5.6f;

	// Log2 spread of steepness per facet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Height", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float PebbleSteepnessVariation = 1.7f;

	// Random facet offset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Height", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.005"))
	float PebbleBiasVariation = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Height", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float PebbleHeightGain = 1.0f;

	// Random height drop per stone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Height", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float PebbleHeightVariation = 0.3f;

	// Region IDs per facet (stone * 64 + facet) instead of per stone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles")
	bool bPebbleFacetIds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pebbles|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float PebbleHeightScale = 1.0f;
};


// Cliff Strata: projected 3D prism blocks shaped by the paired CliffStrata prototype.
// Raw projected depth, block/row identity and projected flow are internal; the final generator
// publishes block Region IDs and scalar masks only. No signed BoundaryField is claimed here.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCliffStrata
{
	GENERATED_BODY()

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Output")
	bool bCliffNormalizeHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float CliffHeightScale = 1.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	int32 CountX = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	int32 CountY = 7;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float Density = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float SizeMin = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float SizeMax = 1.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float SizeAspect = 0.95f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float Jitter = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Layout")
	float FlowVariation = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float HeightMin = 0.025f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float HeightMax = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	int32 Steps = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float Rotation = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float LeanX = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float LeanY = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	int32 FormationCells = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float FormationAmount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	bool bQuarterCopies = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	int32 QuarterYCount = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float QuarterFill = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float QuarterSize = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float QuarterHeight = 2.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float QuarterJitterX = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	float QuarterJitterY = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	int32 Sides = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Formation")
	bool bShapeRandom = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Projection")
	float CameraYaw = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Projection")
	float CameraPitch = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Projection")
	float ViewScale = 2.50f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Projection")
	float DepthMin = -1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Projection")
	float DepthMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float UnitDistance = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float UnitDistanceIdLerp = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float CarveDepth = 0.375f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float CarveVoronoi = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float YBias = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float YBiasVoronoi = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	bool bYBiasVoronoiInvert = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	float NegativeYUnitDistanceTaper = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Shape")
	bool bReverse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Pattern")
	int32 Seed = 1234;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Pattern")
	int32 VoronoiCells = 13;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Pattern")
	float FlowVoronoi = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float ChamferWidth = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float ChamferIntensity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float ChamferVoronoi = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float BlockCavityWidth = 0.02f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float RowCavityWidth = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float CavityIntensity = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float CavityVoronoiThreshold = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff Strata|Edges")
	float CavityVoronoiMaskGain = 0.125f;
};

// Cracks: a tileable network of straight cracks (the borders of a jittered cell lattice), made
// rough by a per-crack zigzag and a feathered push near the cracks, with widths that vary along
// each crack, per crack and by region, chipped rims, random opened gaps, and pieces that rise,
// sink and tip. Chamfer shapes only the negative groove delta using neutral-speed arrival.
//
// Every length is in cell widths and every depth scales with the cell, so the look holds at any
// resolution and any cell count. The field is a signed height change (Add is the natural blend),
// and it depends on these settings only, so it is cached; chamfer shapes its delta afterwards.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCracks
{
	GENERATED_BODY()

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Output")
	bool bCrackNormalizeHeight = true;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks")
	int32 CrackSeed = 1;

	// Cells across the tile, per axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks", meta = (UIMin = "1", UIMax = "32"))
	int32 CrackCells = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackJitter = 0.85f;

	// Scalar groove width, in cell widths; does not move piece borders or control visibility.
	// Zero disables the groove, rim chips and groove chamfer; slip and tilt remain unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.005"))
	float CrackWidth = 0.1f;

	// Groove depth at the base width, relative to the cell. A crack that swells wider cuts deeper.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float CrackDepth = 0.415f;

	// How angular the crack lines are. A steepness, not a size: Scale never changes it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Shape", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackRough = 0.361f;

	// Bends per cell width along a crack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Shape", meta = (UIMin = "0.5", UIMax = "16.0", Delta = "0.05"))
	float CrackScale = 7.8f;

	// How much finer texture rides on the bends.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Shape", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackDetail = 1.0f;

	// Blend between the push noise's cells: small = fault-like kinks, large = soft bends.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Shape", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackFeather = 0.176f;

	// Fine width wobble along each crack: 0 none, 1 from nothing to double.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Width", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackWidthVariation = 0.5f;

	// Fine width wobbles per cell width along a crack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Width", meta = (UIMin = "0.5", UIMax = "32.0", Delta = "0.1"))
	float CrackWidthScale = 6.44f;

	// Every crack its own width, from hairline to wide.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Width", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackLineVariation = 0.541f;

	// Whole regions wider or thinner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Width", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackRegionVariation = 0.615f;

	// Share of rim slots that carry a chip; each side of a crack chips on its own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Rim", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackChip = 0.3f;

	// Chip size, in cell widths.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Rim", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.005"))
	float CrackChipSize = 0.12f;

	// Chance that a crack has opened into a flat-floored gap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Rim", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackGap = 0.15f;

	// The scalar groove's flat floor width, relative to the crack's; not a visibility gap.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Rim", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.05"))
	float CrackGapWidth = 1.5f;

	// Every piece rises or sinks by its own random amount, relative to the cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Pieces", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackSlip = 0.1f;

	// Every piece tips its own random way, as a slope.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Pieces", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackTilt = 0.1f;

	// Strength of wall-profile shaping on the non-positive crack delta; never cuts the piece base.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Chamfer", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float CrackChamferAmount = 0.0f;

	// Wall-profile transition distance in cell widths, independent of crack Width.
	// Does not expand the groove domain or introduce propagation noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Chamfer", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.001"))
	float CrackChamferEdge = 0.12f;

	// Multiplies the crack field. The generator height is signed about zero, so Add carves the
	// cracks into the height below and Replace gives flat ground with the cracks cut in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cracks|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float CrackHeightScale = 1.0f;
};

// How a Generator-layer Height Blend module combines the running signed height with another
// module's signed height. This is the one place specialised generator height combination lives;
// the generators themselves are plain signed field producers.
UENUM(BlueprintType)
enum class EMixtormatGeneratorHeightOp : uint8
{
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	Difference UMETA(DisplayName = "Difference"),
	// Scales the running height by Scale; the referenced module is not read.
	Multiply UMETA(DisplayName = "Multiply / Scale"),
	HeightBlend UMETA(DisplayName = "Height Blend")
};

// A Generator-layer sublayer that combines the running signed height with another module's signed
// height. Ordered in the layer's child chain like any other module.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratorHeightBlend
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend")
	EMixtormatGeneratorHeightOp Op = EMixtormatGeneratorHeightOp::Add;

	// Another module in this Generator layer whose signed height is the second operand. Invalid is a
	// neutral operand per operation (0 for Add/Subtract, 1 for Multiply) or a pass-through for the
	// operations that have no neutral (Min, Max, Difference, Height Blend).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend")
	FGuid SourceLayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend")
	FGuid SourceChildId;

	// How much of the combined result replaces the running height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Amount = 1.0f;

	// Multiply/Scale factor. Read only when a source is set; with no source the multiplicative
	// operand is neutral (1), so an unconnected Multiply changes nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float Scale = 1.0f;

	// Width of the rounded join for Min, Max and Height Blend, in height units. 0 is a hard join.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Softness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Threshold = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (DisplayName = "Edge Softness", UIMin = "0.0", UIMax = "1.0"))
	float EdgeSoftness = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BaseBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BlendBias = 0.0f;
};

// Ordered before its target generator. This shifts the target's bedding coordinate, not its
// finished relief or the layer's running height. Source uses the existing typed output address.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratorHeightPush
{
	GENERATED_BODY()

	FMixtormatGeneratorHeightPush()
	{
		Source.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
		Source.OutputName = FName(TEXT("Height"));
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Push")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Push")
	FMixtormatOutputReference Source;

	// A later Strata generator in this layer. Other target semantics are not enabled yet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Push")
	FGuid TargetChildId;

	// Bedding-coordinate shift per signed source-height unit; zero is exactly neutral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Push", meta = (UIMin = "-16.0", UIMax = "16.0", Delta = "0.01"))
	float Amount = 1.0f;
};

// Ordered structural pullback before an explicit target; never resamples finished geology.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratorStructuralWarp
{
	GENERATED_BODY()

	FMixtormatGeneratorStructuralWarp()
	{
		Source.Kind = EMixtormatPublishedFieldKind::Flow;
		Source.OutputName = FName(TEXT("FlowDirection"));
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structural Warp")
	bool bEnabled = true;

	// Completed Flow or identity-winding lifted UVMap. Vector2 is not a coordinate contract.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structural Warp")
	FMixtormatOutputReference Source;

	// Any later, enabled, unscoped generator in this layer may be targeted.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structural Warp")
	FGuid TargetChildId;
};

// A Generator-layer sublayer that remaps the running signed height through the shared scalar ramp.
// The ramp is authored in -1..1 with zero at the centre; the signed field is never converted to
// 0..1 first.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratorHeightCurve
{
	GENERATED_BODY()

	FMixtormatGeneratorHeightCurve()
	{
		Curve.DomainMin = -1.0f;
		Curve.DomainMax = 1.0f;
		Curve.Points = {
			FMixtormatScalarRampPoint{-1.0f, -1.0f},
			FMixtormatScalarRampPoint{0.0f, 0.0f},
			FMixtormatScalarRampPoint{1.0f, 1.0f}
		};
		Curve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	bool bEnabled = true;

	// Optional zero-preserving max-absolute normalization to -1..1 before the signed input range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	bool bNormalizeInput = false;

	// Signed input range. The pivot is zero: positive values divide by InputMax, negative by
	// abs(InputMin). Default -1..1 matches the canonical signed domain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	float InputMin = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	float InputMax = 1.0f;

	// Sign-preserving power transform: sign(x) * pow(abs(x), exponent). Identity at 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	float Balance = 1.0f;

	// Multiplicative contrast pivoted around zero. Identity at 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	float Contrast = 1.0f;

	// Signed addition. Identity at 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	float Offset = 0.0f;

	// True means -x, not 1-x. Identity when false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap")
	FMixtormatScalarRamp Curve;

	// How much of the remapped result replaces the running height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Remap", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Amount = 1.0f;
};

// Which scalar height a Color Ramp module reads. Serialized by value: append, never reorder.
UENUM(BlueprintType)
enum class EMixtormatColorRampSource : uint8
{
	// The running generator height: the chain, exactly as the module behaved before this existed.
	GeneratorRunning UMETA(DisplayName = "Generator Height"),
	// An earlier module in this layer, by child id. A missing or later reference reads the
	// running height rather than failing.
	ModuleRef UMETA(DisplayName = "Module"),
	// The layer's own input height, resolved before any generator module runs.
	LayerHeight UMETA(DisplayName = "Layer Height"),
	// The accumulated height composited below this layer. Empty on the bottom layer.
	CompositeBelow UMETA(DisplayName = "Composite Below")
};

// A Generator-layer sublayer that maps the running signed height through a reusable colour ramp
// and publishes the result as a colour field for later albedo/material use.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratorHeightColorRamp
{
	GENERATED_BODY()

	FMixtormatGeneratorHeightColorRamp()
	{
		Ramp.Stops = {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)},
			FMixtormatColorRampStop{0.0f, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)}
		};
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	bool bEnabled = true;

	// Where the ramp's scalar comes from. The default preserves the original contract: the
	// running generator height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	EMixtormatColorRampSource Source = EMixtormatColorRampSource::GeneratorRunning;

	// The module referenced by ModuleRef. Only an earlier module can have produced a height by
	// the time this sublayer runs, so a later or missing id reads the running height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	FGuid SourceChildId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Ramp")
	FMixtormatColorRamp Ramp;

	// Deprecated serialized-only field. The live published output is always the canonical "Color";
	// this is retained so older assets keep their saved value, but it is never read for identity
	// and is deliberately not exposed as an editable inspector field.
	UPROPERTY()
	FName OutputName = FName(TEXT("Color"));
};

// Which algorithm the Noise module evaluates.
//
// Serialised by value. Append only. The families split by their natural output contract:
// the lattice families (Gradient, Value, FBM), Bars and Phasor are zero-centred and signed; Ridged,
// Billow and the Worley distances are 0..1 magnitudes. The module's height output is the signed
// remap of whichever contract the family defines -- see FMixtormatNoise.
UENUM(BlueprintType)
enum class EMixtormatNoiseType : uint8
{
	Gradient UMETA(DisplayName = "Gradient"),
	Value UMETA(DisplayName = "Value"),
	FBM UMETA(DisplayName = "FBM"),
	Ridged UMETA(DisplayName = "Ridged"),
	Billow UMETA(DisplayName = "Billow"),
	WorleyF1 UMETA(DisplayName = "Worley F1"),
	WorleyF2 UMETA(DisplayName = "Worley F2"),
	WorleyF1MinusF2 UMETA(DisplayName = "Worley F1-F2"),
	Bars UMETA(DisplayName = "Bars / Stripes"),
	Phasor UMETA(DisplayName = "Phasor")
};

// New Noise V2 selector; the property did not exist in legacy assets.
UENUM(BlueprintType)
enum class EMixtormatNoiseWorleyMetric : uint8
{
	Euclidean UMETA(DisplayName = "Euclidean"),
	Manhattan UMETA(DisplayName = "Manhattan"),
	Chebyshev UMETA(DisplayName = "Chebyshev")
};

// Noise: a tileable, seeded, resolution-independent scalar field producer.
//
// It publishes what the algorithm genuinely produces -- a Value, a Gradient with directional
// meaning, and for the Worley family the stable cell IDs -- and decides nothing about what any
// of it means. Height, roughness, mask, erosion and colour are downstream decisions; the
// Generator layer's Height Blend / Height Curve sublayers and the published-field consumers
// own them.
//
// The module's height contribution to its Generator layer is the family's value remapped to the
// shared signed contract (zero-centred, so zero is the neutral generator height):
//
//   Gradient / Value / FBM / Bars / Phasor   the signed value (zero-centred)
//   Ridged / Billow                 Value * 2 - 1   (crests and bumps read up)
//   Worley F1 / F2 / F1-F2          1 - Value * 2   (feature points and walls read up)
//
// Every family is periodic by construction -- the lattice is wrapped to an integer period
// before it is hashed -- so the field tiles exactly at any Scale, and Offset translates it
// without breaking the wrap. Direction is used by Bars and Phasor, where
// it snaps to the nearest angle that tiles, exactly like Strata Carver's bedding.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatNoise
{
	GENERATED_BODY()

	// The algorithm. Decides the outputs and the value contract, nothing else.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Pattern")
	EMixtormatNoiseType NoiseType = EMixtormatNoiseType::Gradient;

	// Shifts every per-cell draw, and slides the Bars phase.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Pattern", meta = (UIMin = "0", UIMax = "9999"))
	int32 NoiseSeed = 1;

	// Lattice cells across the tile. The base period of every family; floored at 1, because a
	// lattice that does not close on the tile cannot be wrapped.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Pattern", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	float NoiseScale = 8.0f;

	// Octaves for FBM / Ridged / Billow. Each runs on its own integer period, so the stack
	// tiles whatever the per-octave periods turn out to be.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Detail", meta = (UIMin = "1", UIMax = "8", ClampMin = "1", ClampMax = "8"))
	int32 NoiseDetail = 4;

	// Persistence for FBM / Ridged / Billow: how much of each finer octave survives.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Detail", meta = (UIMin = "0", UIMax = "1", Delta = "0.01"))
	float NoiseRoughness = 0.5f;

	// Frequency multiplier between octaves for FBM / Ridged / Billow. Rounded per octave to the
	// integer period the lattice needs, so a continuous control never breaks tileability.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Detail", meta = (UIMin = "1", UIMax = "4", Delta = "0.05"))
	float NoiseLacunarity = 2.0f;

	// Domain offset in tile widths. Translating a periodic field leaves it periodic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Placement", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float NoiseOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Placement", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float NoiseOffsetY = 0.0f;

	// Bars / Phasor: the direction the waves advance across, in degrees. 0 is horizontal stripes;
	// the angle snaps to the nearest one that tiles, like Strata Carver's bedding direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Placement", meta = (UIMin = "0", UIMax = "360", Delta = "1"))
	float NoiseDirection = 0.0f;

	// Phasor only. Existing Bars keeps its original cosine/seed appearance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Phasor", meta = (UIMin = "0.0", UIMax = "12.0", Delta = "0.05"))
	float NoisePhasorFrequency = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Phasor", meta = (UIMin = "0.0", UIMax = "8.0", Delta = "0.05"))
	float NoisePhasorAnisotropy = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Phasor", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float NoisePhasorPhaseVariation = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Phasor", meta = (UIMin = "0.0", UIMax = "3.14", Delta = "0.01"))
	float NoisePhasorOrientationVariation = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Phasor", meta = (UIMin = "1", UIMax = "4", ClampMin = "1", ClampMax = "4"))
	int32 NoisePhasorComponents = 2;

	// Worley only. Euclidean + jitter 1 is the original unchanged cellular field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Worley")
	EMixtormatNoiseWorleyMetric NoiseWorleyMetric = EMixtormatNoiseWorleyMetric::Euclidean;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Worley", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float NoiseWorleyJitter = 1.0f;

	// Optional domain distortion shared by scalar families. Zero strength is an exact bypass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float NoiseDistortionStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "1.0", UIMax = "64.0", Delta = "1.0"))
	float NoiseDistortionFrequency = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "1", UIMax = "8", ClampMin = "1", ClampMax = "8"))
	int32 NoiseDistortionOctaves = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float NoiseDistortionRoughness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "1.0", UIMax = "4.0", Delta = "0.05"))
	float NoiseDistortionLacunarity = 2.0f;

	// 0 = directional FBM displacement, 1 = divergence-free curl displacement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float NoiseDistortionCurlMix = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Distortion", meta = (UIMin = "0.0", UIMax = "360.0", Delta = "1.0"))
	float NoiseDistortionDirection = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Output")
	bool bNoiseNormalizeHeight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise|Output", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float NoiseHeightScale = 1.0f;
};


// One GENERATORS child, whatever kind it is.
//
// The wrapper exists so the category is one thing everywhere -- one child type, one owner type,
// one badge, one dispatch, one gather branch, one inspector slot -- and the kind is a field
// inside it rather than a second discriminator bolted onto EMixtormatLayerChildType. Adding a
// generator is then: a value on EMixtormatGeneratorType, a payload struct beside StrataCarver,
// a case in AddGeneratorLayerPasses, and an inspector panel. Nothing that already exists changes.
//
// This is the opposite of the trade EMixtormatLayerChildType makes for filters, and deliberately
// so. Filters are discriminated at the top level because every dispatch in the plugin already
// switches on Type and a nested branch would be invisible to the compiler. Generators run in one
// place -- a single call before the composite -- so there is exactly one switch to keep honest,
// and the flat alternative would add a child-type value per generator forever.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGenerator
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator")
	EMixtormatGeneratorType Type = EMixtormatGeneratorType::StrataCarver;

	// Explicit inputs belong to this generator, not the layer-wide source-sampling placement.
	// Height is the source module's completed signed output (after its tools and Height Scale).
	// Consumers are implemented separately; disabled defaults preserve existing assets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator|Inputs")
	FMixtormatOutputReference HeightSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator|Inputs")
	FMixtormatOutputReference WarpSource;

	FMixtormatGenerator()
	{
		HeightSource.bEnabled = false;
		HeightSource.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
		HeightSource.OutputName = FName(TEXT("Height"));
		WarpSource.bEnabled = false;
		WarpSource.Kind = EMixtormatPublishedFieldKind::Flow;
		WarpSource.OutputName = FName(TEXT("FlowDirection"));
	}

	// Deprecated: per-generator blend is gone. Generator modules are plain signed field producers
	// and combination lives in the Generator-layer Height Blend sublayer. Kept only so existing
	// assets load; it is never read.
	UPROPERTY()
	FMixtormatHeightBlend HeightBlend = FMixtormatHeightBlend(EMixtormatHeightOp::Add);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::StrataCarver"))
	FMixtormatStrataCarver StrataCarver;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::Cracks"))
	FMixtormatCracks Cracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::RockFormation"))
	FMixtormatRockFormation RockFormation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::Pebbles"))
	FMixtormatPebbles Pebbles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::CliffStrata"))
	FMixtormatCliffStrata CliffStrata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::Noise"))
	FMixtormatNoise Noise;
};
