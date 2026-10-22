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

// Curvature-driven surface response, not a tissue mechanics simulation.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatSkin
{
	GENERATED_BODY()

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Contribution after child masks. Zero preserves every incoming channel exactly."))
	float Amount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "-4", UIMax = "4", Delta = "0.01", ToolTip = "Signed relief gain. Zero removes Skin displacement, not its material response."))
	float HeightScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fullness of the periodic tissue lobes."))
	float Fullness = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Softness of tissue and fold transitions."))
	float Softness = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Authored compression response; curvature is a geometric cue, not measured strain."))
	float Compression = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Authored stretching and fold suppression."))
	float Tension = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "2", Delta = "0.01", ToolTip = "Depth of the primary fold relief."))
	float FoldDepth = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.01", UIMax = "0.5", Delta = "0.001", ToolTip = "Fold width relative to cell spacing."))
	float FoldWidth = 0.14f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (Units = "deg", UIMin = "-180", UIMax = "180", Delta = "0.1", ToolTip = "Preferred fold direction in degrees; periodic construction preserves tile boundaries."))
	float Direction = -10.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Crossing-fold contribution."))
	float Crossing = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "How strongly tissue variation shapes folds."))
	float TissueCoupling = 0.9f;
	// Radius in destination UV space, independent of output resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0.001", UIMax = "0.1", Delta = "0.001", ToolTip = "Upstream curvature analysis radius in destination UV units, not pixels."))
	float CurvatureRadius = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "4", Delta = "0.01", ToolTip = "Curvature-driven fold response; not a physical strain measurement."))
	float CurvatureStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Pore relief depth."))
	float PoreDepth = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Variation between periodic pores."))
	float PoreVariation = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Fine surface grain amount."))
	float Grain = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", Delta = "0.01", ToolTip = "Pigment variation along the linear albedo ramp."))
	float PigmentVariation = 0.12f;
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

namespace MixtormatSurfacing
{
	MIXTORMATRUNTIME_API TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(EMixtormatSurfacingType Type);
	MIXTORMATRUNTIME_API bool PublishesField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
	MIXTORMATRUNTIME_API bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		EMixtormatPublishedFieldKind Kind, FName OutputName);
}
