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
		Rust = 2,
	Wood = 3
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

UENUM(BlueprintType)
enum class EMixtormatWoodMode : uint8
{
	FullWood = 0 UMETA(DisplayName = "Full Wood (Substrate + Finish)"),
	FinishOnly = 1 UMETA(DisplayName = "Finish Pass Only")
};

UENUM(BlueprintType)
enum class EMixtormatWoodSpecies : uint8
{
	Oak = 0 UMETA(DisplayName = "Oak (Ring-Porous)"),
	Maple = 1 UMETA(DisplayName = "Maple (Diffuse-Porous)"),
	Pine = 2 UMETA(DisplayName = "Pine (Conifer / Resin Canals)")
};

UENUM(BlueprintType)
enum class EMixtormatWoodCut : uint8
{
	PlainSawn = 0 UMETA(DisplayName = "Plain-Sawn (Tangential)"),
	QuarterSawn = 1 UMETA(DisplayName = "Quarter-Sawn (Radial)"),
	RiftSawn = 2 UMETA(DisplayName = "Rift-Sawn (Oblique)"),
	EndGrain = 3 UMETA(DisplayName = "End-Grain (Cross)")
};

UENUM(BlueprintType)
enum class EMixtormatWoodFigure : uint8
{
	None = 0 UMETA(DisplayName = "None (Straight)"),
	Curly = 1 UMETA(DisplayName = "Curly / Fiddleback"),
	Quilted = 2 UMETA(DisplayName = "Quilted (Blistered)"),
	Birdseye = 3 UMETA(DisplayName = "Birdseye (Conical Dimple)"),
	Interlocked = 4 UMETA(DisplayName = "Interlocked (Ribbon)")
};

UENUM(BlueprintType)
enum class EMixtormatWoodPrep : uint8
{
	Planed = 0 UMETA(DisplayName = "Planed"),
	Sanded = 1 UMETA(DisplayName = "Sanded"),
	RawSawn = 2 UMETA(DisplayName = "Raw Sawn (Kerf)"),
	WireBrushed = 3 UMETA(DisplayName = "Wire-Brushed")
};

UENUM(BlueprintType)
enum class EMixtormatWoodFinish : uint8
{
	Bare = 0 UMETA(DisplayName = "None / Bare"),
	WipedOil = 1 UMETA(DisplayName = "Wiped Oil"),
	DissolvedDye = 2 UMETA(DisplayName = "Dissolved Dye"),
	PigmentStain = 3 UMETA(DisplayName = "Pigment Stain"),
	PoreFiller = 4 UMETA(DisplayName = "Pore Filler"),
	FilmCoating = 5 UMETA(DisplayName = "Film Coating"),
	Wax = 6 UMETA(DisplayName = "Wax")
};

// Coherent 3D log anatomy (rings, vessels/tracheids/canals, rays) sliced by cut plane,
// plus orthogonal figure deformation, surface preparation/aging, and stackable finish/coating.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatWood
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Full Wood generates 3D wood anatomy and optional finish; Finish Pass Only applies repeatable dye, stain, filler, film, or wax over the incoming wood surface."))
	EMixtormatWoodMode Mode = EMixtormatWoodMode::FullWood;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Master contribution after scoped masks. Zero is exact identity."))
	float Amount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "-4", UIMax = "4", Delta = "0.01", ToolTip = "Pure multiplier on signed wood and coating relief."))
	float HeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "9999", Delta = "1"))
	int32 Seed = 42;

	// --- Species & 3D Anatomy ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Species anatomical family: Oak (ring-porous earlywood vessels, dendritic latewood flame tracts, multiseriate + uniseriate rays), Maple (diffuse-porous uniform vessels, crisp latewood line, fine rays), Pine (non-porous conifer tracheids with normal axial/radial resin canals)."))
	EMixtormatWoodSpecies Species = EMixtormatWoodSpecies::Oak;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "1", UIMax = "32", Delta = "0.1", ToolTip = "Annual growth ring frequency in 3D log radial space."))
	float RingScale = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0.05", UIMax = "0.95", Delta = "0.01", ToolTip = "Fraction of each annual growth ring occupied by dense summer/latewood."))
	float LatewoodRatio = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Sharpness of the earlywood-to-latewood transition within each annual ring, separate from the sharp outer annual ring boundary."))
	float LatewoodSharpness = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Organic 3D radial and axial growth-ring undulation."))
	float RingWobble = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "0.15", Delta = "0.001", ToolTip = "Relative depth of open vessel pores (Oak/Maple) or resin canals and tracheid grooves (Pine), independent of pore density."))
	float PoreDepth = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Abundance of anatomical vessel pores (or axial/radial resin canals in Pine). Does not rescale surviving pore depth."))
	float PoreDensity = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Prominence of 3D radial medullary rays (multiseriate silver fleck + uniseriate rays in Oak; fine rays in Maple/Pine). Appearance changes automatically with Cut."))
	float RayAmount = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0.25", UIMax = "4", Delta = "0.02", ToolTip = "Relative 3D spacing and ribbon height of medullary rays."))
	float RayScale = 1.0f;

	// --- 3D Cut Orientation ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Sawing plane through the 3D log cylinder: Plain-Sawn (tangential cathedral arches and tall ray spindles), Quarter-Sawn (radial parallel ribbons and wide ray fleck), Rift-Sawn (45-degree oblique), or End-Grain (concentric cross-section)."))
	EMixtormatWoodCut Cut = EMixtormatWoodCut::PlainSawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0.05", UIMax = "2", Delta = "0.01", ToolTip = "Normalized distance of the board plane from the log pith axis. Smaller values tighten cathedral arches and increase ring curvature."))
	float PithOffset = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "-45", UIMax = "45", Delta = "0.5", ToolTip = "Longitudinal cant angle of the board cut relative to the log axis (degrees). Controls cathedral arch frequency on plain-sawn boards and grain runout."))
	float CutTilt = 8.0f;

	// --- Figure (Separate from Species) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "3D fiber undulation variant (independent of species): Curly/Fiddleback axial wave, Quilted biaxial blister, Birdseye conical radial dimple, or Interlocked alternating spiral ribbon."))
	EMixtormatWoodFigure Figure = EMixtormatWoodFigure::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Amplitude of the 3D figure deformation and subsurface fiber inclination."))
	float FigureAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0.25", UIMax = "8", Delta = "0.05", ToolTip = "Spatial frequency of figure waves, blisters, or birdseye dimples."))
	float FigureScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fiber-inclination modulation of the wood color ramp. Static chatoyance approximation, not view-dependent fiber scattering."))
	float Chatoyance = 0.45f;

	// --- Preparation & Aging ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Mechanical surface preparation before finishing: Planed, Sanded, Raw Sawn kerf marks, or Wire-Brushed earlywood erosion."))
	EMixtormatWoodPrep Prep = EMixtormatWoodPrep::Planed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Intensity of tool marks, sanding scratches, saw kerf chatter, or wire-brushed earlywood excavation."))
	float PrepIntensity = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "UV lignin photodegradation (silver-gray bleaching), differential earlywood recession, and fine axial drying checks."))
	float Weathering = 0.0f;

	// --- Bare Wood Material ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Baseline bare-wood roughness before finish sheen is applied."))
	float Roughness = 0.58f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood")
	FMixtormatColorRamp ColorRamp;

	// --- Finish & Coating ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Finish process: Bare, Wiped Oil, Dissolved Dye, Pigment Stain (pools in pores/depressions), Pore Filler, Film Coating, or Wax."))
	EMixtormatWoodFinish Finish = EMixtormatWoodFinish::WipedOil;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Finish stage strength. Zero leaves the wood or incoming surface untouched."))
	float FinishAmount = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Refractive wet-out darkening and figure/chatoyance amplification from oil, dye, or film penetration. Independent of added colorant tint."))
	float WetOut = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Added dye, pigment stain, or pore-filler colorant strength. Dye penetrates fibers; Pigment Stain and Pore Filler concentrate in open pores and depressions."))
	float TintAmount = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Colorant tint for Dissolved Dye, Pigment Stain, Pore Filler, or toned Film Coating."))
	FLinearColor FinishTint = FLinearColor(0.18f, 0.09f, 0.04f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "0.15", Delta = "0.001", ToolTip = "Relative coating thickness budget, independent of input height min/max (not millimeters). Used by Wiped Oil, Film Coating and Wax; dye/stain do not build a film. Pore Filler uses depression capacity."))
	float FilmBuild = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Bias deposition toward local depressions. Pore Filler fills a fraction of available capacity; film deposition stays within Film Build. Local approximation, not a volume-conserving leveling solve."))
	float DepressionFill = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Target finish sheen (0 = dead-flat matte, 1 = high-gloss polished film), independent of film build and wax amount."))
	float Sheen = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Coating-top application micro-relief (brush marks or spray orange peel) scaled by remaining coating thickness."))
	float ApplicationTexture = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "360", Delta = "1", ToolTip = "Brush or wiping direction in degrees for coating application marks."))
	float ApplicationAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "High-point coating abrasion inferred from local relief, not object edges. Removes deposited material; full wear restores the incoming substrate. Absorbed oil/dye treatment remains."))
	float Wear = 0.0f;

	// --- Channels ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood")
	bool bWriteHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood")
	bool bWriteColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood")
	bool bWriteRoughness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wood", meta = (ToolTip = "Full Wood writes dielectric optics (metallic zero, F0 0.04). Finish-only blends coating optics over incoming material. Sheen controls roughness, not F0."))
	bool bWriteMaterial = true;

	FMixtormatWood()
	{
		ColorRamp.Stops = {
			{0.0f, FLinearColor(0.16f, 0.095f, 0.052f, 1.0f)},
			{0.38f, FLinearColor(0.36f, 0.22f, 0.12f, 1.0f)},
			{0.72f, FLinearColor(0.54f, 0.37f, 0.21f, 1.0f)},
			{1.0f, FLinearColor(0.68f, 0.50f, 0.31f, 1.0f)}};
	}
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0.01", UIMax = "8", Delta = "0.01", ToolTip = "Relative cluster size; smaller values produce finer corrosion. Slider range is not a binding limit. The magnitude sets periodic spacing; zero reaches the unresolved-detail limit. Not centimeters."))
	float FeatureSize = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "-1", UIMax = "1", Delta = "0.01", ToolTip = "Positive values favor geometric depressions; negative values favor ridges. Not alloy, coating damage, humidity or world gravity."))
	float SurfaceBias = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rust", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Continuous multiscale variation in cluster shape, spacing, local orientation, deposit and pigment. Zero removes added domain distortion and pigment contrast; no illumination is baked into color."))
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
		ColorRamp.Stops = {
			{0.0f, FLinearColor(.042f, .016f, .009f, 1.0f)},
			{0.32f, FLinearColor(.14f, .038f, .014f, 1.0f)},
			{0.68f, FLinearColor(.34f, .095f, .022f, 1.0f)},
			{1.0f, FLinearColor(.58f, .22f, .042f, 1.0f)}};
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surfacing")
	FMixtormatWood Wood;
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

struct MIXTORMATRUNTIME_API FMixtormatWoodPreset
{
	FText Name;
	FText Description;
	FMixtormatWood Settings;
};

namespace MixtormatSurfacing
{
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatRustPreset> GetRustPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatWoodPreset> GetWoodPresets();
	// Shared by dispatch/remap eligibility and channel application; dye/stain add no relief.
	MIXTORMATRUNTIME_API bool WoodWritesHeight(const FMixtormatWood& Wood);
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSkinPreset> GetSkinPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatMossPreset> GetMossPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(EMixtormatSurfacingType Type);
	MIXTORMATRUNTIME_API bool PublishesField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
	MIXTORMATRUNTIME_API bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
}
