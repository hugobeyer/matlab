// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatMaskShaping.h"
#include "MixtormatGeneratorTypes.h"
#include "MixtormatMaskTypes.generated.h"

class UMixtormatMask;
class UTexture2D;

UENUM(BlueprintType)
enum class EMixtormatMaskBlendMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Multiply UMETA(DisplayName = "Multiply"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	AddSub UMETA(DisplayName = "Add/Sub"),
	Overlay UMETA(DisplayName = "Overlay"),
	// Appended, never reordered: these are serialised by value, so inserting above would
	// silently re-map every mask layer already authored against the old ordering.
	Difference UMETA(DisplayName = "Difference"),
	Exclusion UMETA(DisplayName = "Exclusion")
};

// Where a Mask child reads its scalar from.
//
// Texture is the authored map or the published output a mask has always read. Layer Values reads
// the layer the mask is on instead: its own resolved albedo and roughness, so a mask can be the
// shape of the material rather than a painted map. Everything downstream is unchanged -- the
// placement, the shaping, the blur and curvature children, the blend into the chain -- because
// this names the source and nothing else.
UENUM(BlueprintType)
enum class EMixtormatMaskSource : uint8
{
	Texture = 0 UMETA(DisplayName = "Texture"),
	LayerValues = 1 UMETA(DisplayName = "Layer Values"),
		// Appended: the same noise field producer, consumed as coverage rather than height.
		Noise = 2 UMETA(DisplayName = "Noise")
};

// Which scalar comes out of the layer's resolved values.
//
// The numbering is the shader's: MixtormatMask.usf switches on this directly, so these are not
// free to reorder. Luminance is Rec. 709 over linear RGB, which is what the compositor holds.
UENUM(BlueprintType)
enum class EMixtormatLayerValueChannel : uint8
{
	Luminance = 0 UMETA(DisplayName = "Luminance"),
	Red = 1 UMETA(DisplayName = "Red"),
	Green = 2 UMETA(DisplayName = "Green"),
	Blue = 3 UMETA(DisplayName = "Blue"),
	// No Alpha: the compositor's albedo carries none -- every layer writes 1 there -- so the
	// fourth channel of the resolved values is the layer's roughness instead, which is a signal
	// an artist actually has.
	Roughness = 4 UMETA(DisplayName = "Roughness")
};

// How a Color ID Mask decides which pixels it selects.
//
// Color Range is what the node has always done: sample an authored ID map and accept whatever
// lands within Threshold of a chosen colour, feathered by Width. Exact ID skips colour entirely
// and compares the integer Region ID produced by the nearest ID node above it -- Pattern IDs,
// Cluster IDs or Combine IDs -- so the selection is stable regardless of what colour the preview
// happens to paint that region.
//
// ColorRange is 0 so every mask authored before this enum existed loads with the behaviour it
// already had. Appended only; never reorder.
UENUM(BlueprintType)
enum class EMixtormatColorIdMode : uint8
{
	ColorRange = 0 UMETA(DisplayName = "Color Range"),
	ExactId = 1 UMETA(DisplayName = "Exact ID")
};

// Quarter turns only. The compositor wraps every source read in a frac(), so a transform has
// to map the unit square onto itself or it seams at the repeat; an arbitrary angle drags the
// corners of the tile outside the domain. Same reason per-axis scale is an integer.
UENUM(BlueprintType)
enum class EMixtormatUVRotation : uint8
{
	None UMETA(DisplayName = "0"),
	Quarter UMETA(DisplayName = "90"),
	Half UMETA(DisplayName = "180"),
	ThreeQuarter UMETA(DisplayName = "270")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UMixtormatMask> Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UTexture2D> MaskTexture;

	// Optional live scalar output published by another child. This is intentionally separate
	// from FMixtormatLayerChild::SourceLayerId/SourceChildId: those make the whole child an
	// instance and require source/destination types to match. A published mask remains an
	// ordinary Mask child, so its Blend Mode, Weight, Shaping and UV controls stay local.
	UPROPERTY()
	FGuid PublishedSourceLayerId;

	UPROPERTY()
	FGuid PublishedSourceChildId;

	UPROPERTY()
	FName PublishedSourceOutput;

	// Appended shelf discriminator. Layer is the legacy serialized default; a shelf
	// mask names the entry's SourceId without aliasing any material LayerId.
	UPROPERTY()
	EMixtormatOutputReferenceOwnerKind PublishedSourceOwnerKind = EMixtormatOutputReferenceOwnerKind::Layer;

	UPROPERTY()
	FGuid PublishedSourceShelfId;

	bool HasPublishedSource() const
	{
		const FGuid& OwnerId = PublishedSourceOwnerKind == EMixtormatOutputReferenceOwnerKind::Shelf
			? PublishedSourceShelfId : PublishedSourceLayerId;
		return OwnerId.IsValid() && !PublishedSourceOutput.IsNone();
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	FMixtormatMaskShaping Shaping;

	// Source placement. Integer per axis, because a fractional scale lands mid-cell at the UV
	// wrap and seams.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (UIMin = "1"))
	int32 TilingX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (UIMin = "1"))
	int32 TilingY = 1;

	// In UV, and unrelated to Offset below, which lifts the mask value rather than moving
	// where it is read from. Safe at any value: translating a periodic function leaves it
	// periodic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	float UVOffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bFlipV = false;

	// Applied before the tiling, so the mask turns and the lattice replicates the turned
	// result. Rotating afterwards would turn each cell instead of the pattern.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatUVRotation Rotation = EMixtormatUVRotation::None;

	// Appended, and defaulted to the behaviour every saved mask already has: Texture reads the
	// authored map exactly as before, so a material written before Layer Values existed loads
	// unchanged and needs no upgrade path.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatMaskSource Source = EMixtormatMaskSource::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (EditCondition = "Source == EMixtormatMaskSource::LayerValues"))
	EMixtormatLayerValueChannel LayerValueChannel = EMixtormatLayerValueChannel::Luminance;

	// Local field settings; raw Value is mapped to coverage before ordinary mask shaping.
	// Height output settings are retained in the shared payload but do not affect this source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask|Noise", meta = (EditCondition = "Source == EMixtormatMaskSource::Noise"))
	FMixtormatNoise Noise;

	bool UsesNoise() const
	{
		return Source == EMixtormatMaskSource::Noise && !HasPublishedSource();
	}

	bool UsesLayerValues() const
	{
		// A published source wins: it is an explicit wiring to another child's output, and the
		// mask has no business second-guessing it.
		return Source == EMixtormatMaskSource::LayerValues && !HasPublishedSource();
	}
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratedMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask")
	bool bEnabled = true;

	// Signal weights. All default to zero so a new node is neutral until authored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float CurvatureWeight = 0.0f;

	// 0 = cavity, 1 = convex.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "0.0", UIMax = "1.0"))
	float CurvatureBias = 0.0f;

	// Raw curvature is a normal difference over 2 x radius, so it is small. Strength is gain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "0.0"))
	float CurvatureStrength = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "0.001"))
	float CurvaturePower = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float DirectionWeight = 0.0f;

	// Tangent-space direction in degrees. 90 is +Y.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "0.0", UIMax = "360.0"))
	float DirectionAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "0.001", UIMax = "64.0"))
	float DirectionBroadness = 1.0f;

	// Positive weight uses inverted AO, concentrating in occluded areas.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float AOWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float HeightWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float HeightBias = 0.0f;

	// Drainage and crest lines from an erosion filter on a layer below. Unlike the other
	// signals this is not derived here: erosion already computes where its passes agree on a
	// crest, and this is the first thing to consume that output. Zero everywhere when nothing
	// below erodes, so a weight on it is inert rather than wrong.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (UIMin = "-8.0", UIMax = "8.0"))
	float RidgeWeight = 0.0f;

	// Shaping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping")
	bool bNormalizeWeights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "1", UIMax = "32"))
	int32 Broadness = 2;

	// Averages curvature over this many widening rings. 1 is a single kernel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "1", UIMax = "4"))
	int32 Smoothing = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "0.001", UIMax = "0.999"))
	float Bias = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "0.0", UIMax = "4.0"))
	float WarpAmount = 0.0f;

	// Flow source: 0 samples the accumulated normal slope, 1 the accumulated height
	// gradient. Both point downhill, so intermediate values blend two flow fields.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "0.0", UIMax = "1.0"))
	float WarpSource = 0.0f;

	// Gradient sample radius in pixels. Larger values follow broader slopes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (UIMin = "1", UIMax = "16"))
	int32 WarpRadius = 1;

	// Accumulator controls, matching FMixtormatMaskLayer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (UIMin = "0.0", UIMax = "4.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping")
	FMixtormatMaskShaping Shaping;

	bool HasAnySignal() const
	{
		return !FMath::IsNearlyZero(CurvatureWeight)
			|| !FMath::IsNearlyZero(DirectionWeight)
			|| !FMath::IsNearlyZero(AOWeight)
			|| !FMath::IsNearlyZero(HeightWeight)
			|| !FMath::IsNearlyZero(RidgeWeight);
	}
};

// Two constructions, not two presets for one.
//
// Lattice measures distance to a Voronoi cell wall. Its junctions are three-way at roughly 120
// degrees, because that is what a bisector diagram is, and at Jitter 0 the cells are a regular
// grid -- which is grout: brick, tile, plank.
//
// Propagated grows cracks outward from nuclei and stops them where they meet, which puts the
// junctions at right angles. A crack reaching an older crack stops there, because the older one
// has already released the stress driving it. That T-junction is what a drying film actually
// does and what a bisector diagram cannot produce.
UENUM(BlueprintType)
enum class EMixtormatCraquelureMode : uint8
{
	Lattice UMETA(DisplayName = "Lattice"),
	Propagated UMETA(DisplayName = "Propagated")
};

// Craquelure. A generated crack network used as a mask or carved into height and normals.
//
// Its own node rather than a signal on the generated mask. Everything that node produces is
// derived from the surface beneath it and it early-returns when there is none; craquelure is
// generated rather than derived and means something on the bottom layer, so living there forced
// that early return to be picked apart into a per-signal guard. It is a mask rather than a
// filter so it can drive anything downstream -- chipping placement, a stain, a peel -- rather
// than only cutting the surface itself.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCraquelure
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	EMixtormatCraquelureMode Mode = EMixtormatCraquelureMode::Propagated;

	// Cells across one UV repeat, and the only scale control there is.
	//
	// Each mode used to have its own: Lattice called it Period and counted Voronoi cells,
	// Propagated called it SeedCells and counted nucleation sites. They answer the same question --
	// how big is a piece of the broken surface -- so they are one number now, read by whichever
	// mode is running. Any integer tiles, because both lattices wrap on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "1", UIMax = "128"))
	int32 Scale = 80;

	// 0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives organic
	// crazing. Also one control from two -- Lattice jittered its cell points and Propagated its
	// nuclei, and hiding the lattice is the same intent either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Jitter = 1.0f;

	// In cell units, so it means the same thing at any Scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Width = 0.05f;

	// Thins individual cracks, so the network reads as breaks that opened at different times
	// rather than as a uniform lattice. Keyed on the whole crack, not on either side of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Variation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "0"))
	int32 Seed = 1;

	// -- Warp ---------------------------------------------------------------------------------
	// Bends the finished network rather than steering how it grows, so it costs a resolve pass and
	// rebuilds nothing. Periodic curl noise: divergence-free, and it wraps on its own period, so
	// the result still tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Warp = 0.25f;

	// Size of the warp's swirls, in repeats across the UV. Its seed comes off Seed, so reseeding
	// the network reseeds the warp with it rather than leaving a second seed to remember.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (UIMin = "1", UIMax = "32"))
	int32 WarpScale = 32;

	// -- Growth (Propagated mode) ---------------------------------------------------------------
	// Ignored in Lattice mode, which needs none of it: a Voronoi diagram has no growth to steer.

	// How far a crack can reach, in pixels at a 1024 reference, scaled by the render resolution so
	// a preview and an export grow the same network rather than the same pixel count. The dispatch
	// count scales with it, so this is the control that costs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "1", UIMax = "1024"))
	int32 Iterations = 32;

	// Fraction of cells that get a nucleus at all: how many separate cracks there are, as opposed
	// to how far each one runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Density = 0.5f;

	// Size of the stress, toughness and flow fields as a multiple of Scale, rather than as a
	// second cell count.
	//
	// It used to be NoiseCells, an absolute number that fought Scale: moving either changed the
	// character, because what decides it is the field's size *relative* to the pieces. Under 1 the
	// fields steer whole regions; over 1 they roughen individual cracks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.1", UIMax = "8.0"))
	float Detail = 4.0f;

	// How far the stress and toughness fields depart from uniform. At 0 the network is steered
	// only by flow and roughness, which reads as combed rather than as fractured.
	//
	// One dial where there were two. Stress driving cracking on and toughness holding it back are
	// two ends of one balance, they read from independent noise either way, and a uniform stress
	// field over a varying toughness one is not a thing anyone reached for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FieldContrast = 0.2f;

	// How strongly those fields win against a tip's own heading. Was StressGain and ToughnessCost:
	// two weights on opposite signs of the same comparison.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "4.0"))
	float FractureBias = 0.5f;

	// How much a crack is a line rather than a blob. Was Persistence, weighting a tip's preference
	// to carry straight on, and TurnResponse, setting how fast its stored heading caught up -- how
	// hard it resists turning and what happens when it does. They only ever moved together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Straightness = 1.0f;

	// Coherent wander: how much the curl-flow field bends a running crack. At 1 cracks follow the
	// field and come out combed.
	//
	// Deliberately not merged with Roughness. Flow is directional and continuous, Roughness is
	// per-step noise; combed and ragged are different looks and one dial cannot do both.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Flow = 0.25f;

	// Incoherent wander: random jitter in the choice of next step. Roughens a crack's edge without
	// giving it anywhere to go.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (UIMin = "0.0", UIMax = "4.0"))
	float Roughness = 0.1f;

	// -- Advanced -------------------------------------------------------------------------------
	// Growth-kernel internals, right for almost everything. Here because between them they decide
	// whether the network terminates like a drying film or keeps running.

	// Score a step has to beat before a tip advances at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (UIMin = "0.0", UIMax = "4.0"))
	float GrowthThreshold = 0.0f;

	// How many existing cracks a tip may touch before it stops. This is what makes the network meet
	// at right angles like a drying film instead of at the 120 degrees a bisector diagram gives,
	// and it is the whole reason the propagated mode exists.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (UIMin = "1", UIMax = "8"))
	int32 CollisionLimit = 4;

	// -- Relief -----------------------------------------------------------------------------------
	// The generated groove is authoritative for both height and its derived normal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ReliefDepth = 0.25f;

	// The groove's mouth, and the curve of its wall between a straight V and a rounded U. Not
	// merged: a wide V and a narrow U are both things you would ask for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ReliefWidth = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.05", UIMax = "8.0"))
	float ReliefProfile = 2.0f;

	// Per-crack variation on the relief, keyed off the same lineage id the crack network already
	// carries -- stable per crack rather than noisy per pixel, and applied after the network
	// exists so it never touches which cracks grow or where. Each is its own multiplier on top
	// of Height/Groove/Profile above and is neutral at 0, so an authored relief looks the same
	// until one of these is raised.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ReliefGrooveVariation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ReliefProfileVariation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ReliefWidthVariation = 0.0f;

	// -- Output -----------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	FMixtormatMaskShaping Shaping;

	FMixtormatCraquelure()
	{
		Shaping.bInvert = true;
	}
};

// Colour ID mask. Selects the parts of an ID map that carry one of a set of chosen colours.
//
// This is the one placement control that comes from outside the tool. A painted mask, a generated
// one and a curvature mask all describe where something is in the abstract; an ID map describes
// where something is by name, because the person who built the mesh already decided which
// polygons were the handle and which were the panel. Being able to say "the bolts" and have it
// mean the bolts is a different kind of control from anything else in the stack.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatColorIdMask
{
	GENERATED_BODY()

	// The cap on the selection, and the one place it is stated. The shader holds the colours in a
	// fixed constant array and the inspector lays out a fixed set of rows, so both have to agree
	// with this or one of them silently ignores what the other allows.
	static constexpr int32 MaxColors = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bEnabled = true;

	// Which of the two selections this node runs. Appended and defaulted to Color Range, which is
	// what every mask authored before the mode existed was doing, so nothing needs migrating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	EMixtormatColorIdMode Mode = EMixtormatColorIdMode::ColorRange;

	// Exact ID only: the Region ID to select, compared as an integer against the map published by
	// the nearest ID node above this one. Deliberately not derived from a colour -- a region's
	// preview colour is a hash of its ID chosen for legibility, and reading it back would make
	// the selection depend on the display.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "0", EditCondition = "Mode == EMixtormatColorIdMode::ExactId"))
	// int32, not uint32: a Region ID is a pixel index into the composition, so even a 4096 square
	// tops out around 16.7 million, and every editor control and parameter binding in the plugin
	// speaks int32.
	int32 ExactRegionId = 0;

	// Import this with sRGB off and no compression. Both settings move the colours the map
	// stores, and a selection is a comparison against a colour the artist chose in a picker: DXT
	// invents intermediate values along every id boundary and shifts flat regions by more than a
	// tight tolerance allows, and an sRGB decode moves every value at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	TSoftObjectPtr<UTexture2D> IdTexture;

	// The colours to select, unioned. Several per node because selecting a set is the common
	// case, and one node per colour would be a stack whose blend modes all have to agree just to
	// express an OR.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	TArray<FLinearColor> Colors;

	// How far from a chosen colour still counts, as a distance in RGB. The diagonal of the cube
	// is about 1.73, so this is small by nature: the default admits the compression wobble in a
	// flat region without reaching a neighbouring id.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "0.0", UIMax = "1.732"))
	float Tolerance = 0.10f;

	// Width of the transition either side of Tolerance. The map is point sampled, so the
	// selection edge is a hard pixel boundary; this is what feathers it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "0.0", UIMax = "0.5"))
	float Softness = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.0f;

	// Source placement, matching the painted mask. Integer tiling per axis, because a fractional
	// scale lands mid-texel at the UV wrap and seams.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "1"))
	int32 TilingX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (UIMin = "1"))
	int32 TilingY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	float UVOffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bFlipV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	EMixtormatUVRotation Rotation = EMixtormatUVRotation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID|Shaping")
	FMixtormatMaskShaping Shaping;
};

// Random value per region. Takes a cluster filter's ID map and emits 0..1: every pixel in a
// region gets the same value, different regions get different values.
//
// This one genuinely *is* coverage, which is why it is a mask and not a filter. It blends through
// a BlendMode and a Weight like any other mask child and carries the shared shaping block, and
// every existing consumer of a mask works with it unchanged -- stain only the recessed regions,
// wear only the proud ones, chipping only on the large fragments. Region-aware placement for
// effects that already exist, with no new work on their side.
//
// The precedent is FMixtormatColorIdMask, which is also a mask that consumes an ID map. Selecting
// from an ID map is coverage; producing one is not.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it contributes
// nothing.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRandomIdMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	bool bEnabled = true;

	// The range a region's value is drawn from, before shaping. Min above Max is not an error --
	// it inverts the draw, which is the same picture as bInvert and costs nothing to allow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MaxValue = 1.0f;

	// No zeroinvalid dial, deliberately. The prototype needs one because Houdini hands it an
	// unbound id layer as -1; here the union-find assigns every pixel a root, so there is no
	// pixel outside every region and the control would be a dial that never fires. The shaders
	// still carry the sentinel check, so the day something can produce a region-less pixel --
	// a minimum region size, say -- the handling is already in place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (UIMin = "0"))
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Weight = 1.0f;

	// The shared block, embedded rather than redeclared. Contrast above 1 about the 0.5 midpoint
	// is what the prototype's ramp mode was for: it pulls most regions toward the mean and leaves
	// a few outliers, which is the distribution that reads as subtle instead of as noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	FMixtormatMaskShaping Shaping;
};
