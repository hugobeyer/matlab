// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatOutputReference.h"
#include "MixtormatSurfacingTypes.generated.h"

UENUM(BlueprintType)
enum class EMixtormatSurfacingType : uint8
{
	Skin
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
			{0.0f, FLinearColor(0.18f, 0.055f, 0.035f, 1.0f)},
			{0.5f, FLinearColor(0.52f, 0.25f, 0.15f, 1.0f)},
			{1.0f, FLinearColor(0.72f, 0.43f, 0.28f, 1.0f)}
		};
		RoughnessRamp.Points = {{0.0f, 0.55f}, {0.5f, 0.45f}, {1.0f, 0.4f}};
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "9999", Delta = "1", ToolTip = "Repeatable tissue, fold and pore variation."))
	int32 Seed = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "32", Delta = "1", ToolTip = "Periodic tissue cells across the tile."))
	int32 TissueCells = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "64", Delta = "1", ToolTip = "Periodic fold cells across the tile."))
	int32 FoldCells = 16;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "256", Delta = "1", ToolTip = "Fine wrinkle cells per detail repeat. Subpixel detail is filtered."))
	int32 DetailCells = 96;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "256", Delta = "1", ToolTip = "Periodic pore cells per detail repeat."))
	int32 PoreCells = 125;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "16", Delta = "1", ToolTip = "Integer repeats of baked microdetail. This is not independent runtime detail mapping."))
	int32 DetailTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "512", Delta = "1", ToolTip = "Total relaxation budget across coarse-to-fine grids. Coarse iterations change broad folds; fine iterations refine them. Zero retains analytic folds with no simulated correction. Not full 3D mechanics or self-contact."))
	int32 SolverIterations = 128;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "64", UIMax = "2048", Delta = "1", ToolTip = "Maximum mechanics-grid dimension, capped by output resolution. Analytic tissue, fold seeds and pores always retain output resolution. Only simulated corrections are upsampled. Higher grids cost more GPU time and memory."))
	int32 SolverResolution = 512;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Film bending resistance during height-field relaxation. Higher values resist tight bends."))
	float Bending = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Tissue foundation support during film relaxation. Higher values hold the film closer to its foundation."))
	float Support = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Contribution after child masks. Zero preserves every incoming channel exactly."))
	float Amount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "-4", UIMax = "4", Delta = "0.01", ToolTip = "Signed gain on shallow surface relief. Zero removes Skin displacement, not its material response."))
	float HeightScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fullness of the periodic tissue lobes."))
	float Fullness = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Softness of tissue and fold transitions."))
	float Softness = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Authored compression response; curvature is a geometric cue, not measured strain."))
	float Compression = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Authored stretching and fold suppression."))
	float Tension = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Signed relief strength of connected fold valleys and raised shoulders."))
	float FoldDepth = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.01", UIMax = "0.5", Delta = "0.001", ToolTip = "Fold width relative to cell spacing."))
	float FoldWidth = 0.14f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (Units = "deg", UIMin = "-180", UIMax = "180", Delta = "0.1", ToolTip = "Preferred fold direction in degrees; periodic construction preserves tile boundaries."))
	float Direction = -10.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Crossing-fold contribution."))
	float Crossing = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "How strongly tissue variation shapes folds."))
	float TissueCoupling = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "3", Delta = "0.01", ToolTip = "Total layered drift travel in noise-cell spacings, independent of integration steps. Also controls bounded fold-phase wandering."))
	float MarbleDrift = 1.15f;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.125", UIMax = "8", Delta = "0.01", ToolTip = "Drift-noise lattice frequency multiplier, rounded to integer periods for tiling. Changes warp scale independently of drift amount; higher values make smaller features."))
		float WarpScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "5", Delta = "1", ToolTip = "Number of layered domain warps shaping connected wandering fold families."))
	int32 WarpLayers = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Local variation around the preferred fold direction, preserving connected fold families."))
	float DirectionVariation = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "8", Delta = "0.01", ToolTip = "Elongation of fold families along their preferred direction. One is isotropic."))
	float Elongation = 3.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Cellular breakup mixed into the connected fold pattern. Zero retains only wandering fold families."))
	float Cellular = 0.12f;
	// Radius in destination UV space, independent of output resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.001", UIMax = "0.1", Delta = "0.001", ToolTip = "Upstream curvature analysis radius in destination UV units, not pixels."))
	float CurvatureRadius = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "Curvature-driven fold response; not a physical strain measurement."))
	float CurvatureStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.25", UIMax = "3", Delta = "0.01", ToolTip = "Pore radius multiplier, independent of pore cell density. Pixel filtering preserves pore area."))
		float PoreSize = 1.0f;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "Nonnegative pore relief depth multiplier. Values above the slider range are supported; size and density are independent."))
	float PoreDepth = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Variation between periodic pores."))
	float PoreVariation = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fine surface grain amount."))
	float Grain = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Multiscale pigment variation linked to tissue thickness and absorption. Zero removes pigment variation, not separately controlled vascular absorption."))
	float PigmentVariation = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Tissue thickness variation linking pigment and vascular absorption to the tissue field. Not geometric self-contact."))
	float ThicknessVariation = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Vascular absorption contribution linked to tissue thickness and pigmentation. Zero removes vascular absorption."))
	float VascularAmount = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Cavity occlusion contribution."))
	float CavityAO = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Additional roughness in cavities."))
	float CavityRoughness = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "1", UIMax = "2.5", Delta = "0.01", ToolTip = "Dielectric index of refraction. F0 is derived as ((IOR - 1) / (IOR + 1)) squared."))
	float IOR = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Spatial fuzz contribution; shading sheen, not geometric hair."))
	float FuzzAmount = 0.05f;
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

namespace MixtormatSurfacing
{
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSkinPreset> GetSkinPresets();
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(EMixtormatSurfacingType Type);
	MIXTORMATRUNTIME_API bool PublishesField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
	MIXTORMATRUNTIME_API bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
}
