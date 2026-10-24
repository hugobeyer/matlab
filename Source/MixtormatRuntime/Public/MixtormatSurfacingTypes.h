// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatGeneratorTypes.h"
#include "MixtormatOutputReference.h"
#include "MixtormatSurfacingTypes.generated.h"

UENUM(BlueprintType)
enum class EMixtormatSurfacingType : uint8
{
	Skin = 0,
	Moss = 1,
		Rust = 2
};

// Compressed height-field film relaxation on a tissue foundation.
// Connected wandering fold families have signed shoulders; this is not XPBD,
// full 3D tissue mechanics or self-contact.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatSkin
{
	GENERATED_BODY()

	static constexpr int32 MaxSolverIterations = 512;
	static constexpr int32 MinCoarseSolverResolution = 64;
	static constexpr int32 MaxCoarseSolverResolution = 256;
	static constexpr float CoarseSamplesPerFold = 8.0f;
	static constexpr int32 MinSolverResolution = 64;
	static constexpr int32 MaxSolverResolution = 2048;
	static constexpr int32 MaxWarpLayers = 8;

	FMixtormatSkin()
	{
		AlbedoRamp.Stops = {
			{0.0f, FLinearColor(0.30f, 0.15f, 0.085f, 1.0f)},
			{0.5f, FLinearColor(0.46f, 0.28f, 0.19f, 1.0f)},
			{1.0f, FLinearColor(0.62f, 0.41f, 0.29f, 1.0f)}
		};
		RoughnessRamp.Points = {{0.0f, 0.58f}, {0.5f, 0.48f}, {1.0f, 0.4f}};
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "9999", Delta = "1", ToolTip = "Repeatable tissue, fold and pore variation."))
	int32 Seed = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "64", Delta = "1", ToolTip = "Periodic tissue cells across the tile. Lower values make larger fat strokes."))
	int32 TissueCells = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "128", Delta = "1", ToolTip = "Periodic fold cells across the tile."))
	int32 FoldCells = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "512", Delta = "1", ToolTip = "Fine wrinkle cells per detail repeat. Subpixel detail is filtered."))
	int32 DetailCells = 96;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "512", Delta = "1", ToolTip = "Periodic pore cells per detail repeat."))
	int32 PoreCells = 128;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.25", UIMax = "16", Delta = "0.25", ToolTip = "Whole repeats shared by all Skin features. Relief is divided by the repeat count; Height Scale remains a separate multiplier. Fractional layer UV scales or nonperiodic upstream inputs can still cause seams."))
	float SurfaceTiling = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "32", Delta = "1", ToolTip = "Integer repeats of baked microdetail. This is not independent runtime detail mapping."))
	int32 DetailTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "512", Delta = "1", ToolTip = "Total relaxation budget across coarse-to-fine grids. Coarse iterations change broad folds; fine iterations refine them. Zero retains analytic folds with no simulated correction. Not full 3D mechanics or self-contact."))
	int32 SolverIterations = 128;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "64", UIMax = "2048", Delta = "1", ToolTip = "Maximum mechanics-grid dimension, capped by output resolution. Analytic tissue, fold seeds and pores always retain output resolution. Only simulated corrections are upsampled. Higher grids cost more GPU time and memory."))
	int32 SolverResolution = 512;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Film bending resistance during height-field relaxation. Higher values resist tight bends."))
	float Bending = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Tissue foundation support during film relaxation. Higher values hold the film closer to its foundation."))
	float Support = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Redistribute fat within its attached footprint: the upper body thins and the lower body bulges downhill. Gravity Hold resists this sag; Stretch controls elongation separately."))
	float GravityBulge = 0.18f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (Units = "deg", UIMin = "-180", UIMax = "180", Delta = "0.1", ToolTip = "Gravity direction in degrees."))
	float GravityAngle = -90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Resist gravity-driven fat sag. Higher values keep the tissue closer to its unsagged shape, even with zero Solver Iterations. Also anchors thick tissue and seeded depressions during film relaxation."))
	float GravityHold = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Contribution after child masks. Zero preserves every incoming channel exactly."))
	float Amount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "-8", UIMax = "8", Delta = "0.01", ToolTip = "Signed gain on shallow surface relief. Zero removes Skin displacement, not its material response."))
	float HeightScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Fullness of the periodic tissue lobes."))
	float Fullness = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Tissue lobe softness (radius falloff)."))
	float Softness = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "Compression response; solver strain also reacts to solved slope. Curvature is a geometric cue, not measured strain."))
	float Compression = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "Stretching and fold suppression."))
	float Tension = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "-4", UIMax = "4", Delta = "0.01", ToolTip = "Signed relief strength of connected fold valleys and raised shoulders."))
	float FoldDepth = 0.24f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.005", UIMax = "1", Delta = "0.001", ToolTip = "Fold width relative to cell spacing."))
	float FoldWidth = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (Units = "deg", UIMin = "-180", UIMax = "180", Delta = "0.1", ToolTip = "Shared direction for folds, tissue, pores and pigment. Stretch controls alignment; Direction Variation adds smooth phase-driven turns without UV warping."))
	float Direction = -10.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Crossing-fold contribution."))
	float Crossing = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "How strongly tissue and upstream substrate variation shape folds, tissue thickness and pores."))
	float TissueCoupling = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "10", Delta = "0.01", ToolTip = "Total layered drift travel in noise-cell spacings, independent of integration steps. Also controls bounded fold-phase wandering."))
	float MarbleDrift = 0.75f;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.125", UIMax = "16", Delta = "0.01", ToolTip = "Drift-noise lattice frequency multiplier, rounded to integer periods for tiling. Changes warp scale independently of drift amount; higher values make smaller features."))
		float WarpScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "5", Delta = "1", ToolTip = "Number of layered domain warps shaping connected wandering fold families."))
	int32 WarpLayers = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Smooth random turns of the shared direction field, driven by a periodic phase gradient. Works with Marble Drift at zero; Tissue Cells controls the broad variation scale."))
	float DirectionVariation = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Stretch", UIMin = "1", UIMax = "24", Delta = "0.01", ToolTip = "Shared directional elongation of folds, tissue, pores and pigment. 1 disables added stretch; larger values align features with Direction and its local variation. Gravity can also bias tissue alignment."))
	float Elongation = 1.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Cellular breakup mixed into the connected fold pattern. Zero retains only wandering fold families."))
	float Cellular = 0.22f;
	// Radius in destination UV space, independent of output resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.0005", UIMax = "0.25", Delta = "0.001", ToolTip = "Upstream curvature analysis radius in destination UV units, not pixels."))
	float CurvatureRadius = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "8", Delta = "0.01", ToolTip = "Curvature-driven fold, tissue and pore response; not a physical strain measurement."))
	float CurvatureStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.1", UIMax = "8", Delta = "0.01", ToolTip = "Pore radius multiplier, independent of pore cell density. Pixel filtering preserves pore area."))
		float PoreSize = 0.85f;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "8", Delta = "0.01", ToolTip = "Nonnegative pore relief depth multiplier. Size and density are independent."))
	float PoreDepth = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Variation between periodic pores."))
	float PoreVariation = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Fine surface grain amount."))
	float Grain = 0.22f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Strength of broad blotches and fine pigment, ramp contrast and thickness-linked absorption. Stretch follows the shared direction field. Zero uses the ramp midpoint without pigment absorption; vascular absorption stays independent."))
	float PigmentVariation = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Tissue thickness variation linking pigment and vascular absorption to the tissue field. Not geometric self-contact."))
	float ThicknessVariation = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Vascular absorption contribution linked to tissue thickness and pigmentation. Zero removes vascular absorption."))
	float VascularAmount = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.1", UIMax = "8", Delta = "0.01", ToolTip = "Vessel width scale."))
	float VascularScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Vessel density."))
	float VascularDensity = 0.6f;

	// Artist-facing appearance controls, not calibrated biological quantities.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Diffuse Redness", UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Additional broad redness, independent of pigment and branching vessels. Procedural color-only approximation; does not change mean free path."))
	float RednessAmount = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Vein Visibility", UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Visibility of a separate broad branching vein pattern. Relative color contrast against the skin, not blue paint or a spectral blood model. No raised-vessel relief or MFP change."))
	float VeinAmount = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.25", UIMax = "4", Delta = "0.01", ToolTip = "Relative vein width within its branching pattern, not millimeters. Independent of vein density and depth appearance."))
	float VeinWidth = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Vein Depth Look", UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Artistic buried-vessel appearance: deeper looks are softer and fainter. Not anatomical depth. Tune with SSS enabled to avoid excessive diffusion."))
	float VeinDepth = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fraction of seeded vein branches present. Independent of superficial vascular density and diffuse redness."))
	float VeinDensity = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Oil", UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Lower effective roughness in procedural oil patches. Preserves tissue color, relief, F0 and MFP. An artistic single-lobe approximation, not sebum mass or sweat."))
	float OilAmount = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Restrained fine-wrinkle/grain amplification and roughness variation. Does not inflate tissue, broad folds or pores, create flakes, or change MFP. Not a hydration percentage."))
	float Dryness = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Sparse attached scales with shared height, pale color and roughness coverage. Independent of dryness. Artistic coverage control, not a percentage; no detached flakes or overhangs."))
	float Flaking = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (DisplayName = "Finish Variation", UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Seeded patchiness of oil and dryness. Zero gives uniform oil/dryness; flakes remain individually placed. Uses the Skin seed and module tiling, with no external mask."))
	float FinishVariation = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Cavity occlusion contribution."))
	float CavityAO = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Additional roughness in cavities."))
	float CavityRoughness = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "2.5", Delta = "0.01", ToolTip = "Dielectric index of refraction. F0 is derived as ((IOR - 1) / (IOR + 1)) squared."))
	float IOR = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Spatial fuzz contribution; shading sheen, not geometric hair."))
	float FuzzAmount = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Roughness of the fuzz lobe."))
	float FuzzRoughness = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Subsurface scattering contribution. Mean free path remains a physical distance, not albedo."))
	float SSSAmount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (ToolTip = "Linear fuzz tint."))
	FLinearColor FuzzColor = FLinearColor(0.65f, 0.45f, 0.3f, 1.0f);
	// RGB scattering distances in centimeters; not an albedo color.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (Units = "cm", UIMin = "0", UIMax = "1", Delta = "0.001", ToolTip = "RGB mean free path in centimeters. These are scattering distances, not display colors."))
	FLinearColor MeanFreePath = FLinearColor(0.12f, 0.055f, 0.025f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FMixtormatColorRamp AlbedoRamp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FMixtormatScalarRamp RoughnessRamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteRoughness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteAO = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteF0 = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteFuzz = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bWriteSSS = true;

};

UENUM(BlueprintType)
enum class EMixtormatRustStructure : uint8
{
	Smooth = 0,
	Granular = 1,
	Layered = 2
};

// Ferrous-substrate assumption. No hidden metal, coating provenance or physical unit calibration.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRust
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Contribution after scoped masks. Zero preserves the incoming material."))
	float Amount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Clustered attack footprint. Does not renormalize surviving pit depth or deposit height."))
	float Coverage = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust")
	EMixtormatRustStructure Structure = EMixtormatRustStructure::Granular;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "0.2", Delta = "0.001", ToolTip = "Relative recession into the incoming surface. Shedding never restores consumed metal. Not millimeters."))
	float PitDepth = 0.025f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "0.2", Delta = "0.001", ToolTip = "Relative retained deposit thickness above the recessed surface, independent of pit depth. Not millimeters."))
	float Buildup = 0.04f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Clustered shedding removes deposit and visible oxide but leaves the metal-loss footprint intact."))
	float Flaking = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0.5", UIMax = "8", Delta = "0.1", ToolTip = "Relative cluster size. Larger values create fewer, broader clusters; periodic counts are rounded."))
	float FeatureSize = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "-1", UIMax = "1", Delta = "0.01", ToolTip = "Positive values favor geometric depressions; negative values favor ridges. Not alloy, coating damage, humidity or world gravity."))
	float SurfaceBias = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Correlated cluster, deposit and mineral-palette variation. No illumination is baked into pigment."))
	float Variation = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "9999", Delta = "1"))
	int32 Seed = 19;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "-4", UIMax = "4", Delta = "0.01", ToolTip = "Pure multiplier on signed buildup minus metal loss. No normalization."))
	float HeightScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01"))
	float Roughness = 0.82f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust")
	FMixtormatColorRamp ColorRamp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust")
	bool bWriteHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust")
	bool bWriteColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust")
	bool bWriteRoughness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (ToolTip = "Visible oxide becomes dielectric (metallic zero, F0 0.04, no inherited fuzz or SSS). Uncovered substrate optics are preserved."))
	bool bWriteMaterial = true;

	FMixtormatRust()
	{
		ColorRamp.Stops = {{0.0f, FLinearColor(.055f,.018f,.009f,1)},
			{0.45f, FLinearColor(.22f,.055f,.018f,1)}, {1.0f, FLinearColor(.52f,.19f,.035f,1)}};
	}
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatSurfacing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
	EMixtormatSurfacingType Type = EMixtormatSurfacingType::Skin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
	FMixtormatSkin Skin;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
	FMixtormatMoss Moss;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
		FMixtormatRust Rust;
};

// Published diagnostics are independent of channel write toggles, which govern composition.
struct MIXTORMATRUNTIME_API FMixtormatSurfacingOutput
{
	FName Name;
	FText Label;
	EMixtormatPublishedFieldKind Kind;
	// Physical diagnostics may share a display format without being material inputs.
	bool bReferenceable = true;
};

// Built-in looks contain settings only; applying them must retain destination composition gates.
struct MIXTORMATRUNTIME_API FMixtormatSkinPreset
{
	FText Name;
	FText Description;
	FMixtormatSkin Settings;
};

struct MIXTORMATRUNTIME_API FMixtormatMossPreset
{
	FText Name;
	FText Description;
	FMixtormatMoss Settings;
};

struct MIXTORMATRUNTIME_API FMixtormatRustPreset
{
	FText Name;
	FText Description;
	FMixtormatRust Settings;
};

namespace MixtormatSurfacing
{
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatRustPreset> GetRustPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSkinPreset> GetSkinPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatMossPreset> GetMossPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(EMixtormatSurfacingType Type);
	MIXTORMATRUNTIME_API bool PublishesField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
	MIXTORMATRUNTIME_API bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
}
