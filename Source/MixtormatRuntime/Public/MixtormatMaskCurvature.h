// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaskCurvature.generated.h"

// Which field the curvature is measured on.
UENUM(BlueprintType)
enum class EMixtormatCurvatureSource : uint8
{
	// The composited height under this layer -- the same texture the generated masks read. Keeps
	// a mask to the shape of what it is being laid onto: the rim of a rivet already in the
	// surface, the inside of a panel gap.
	Height = 0 UMETA(DisplayName = "Surface Height"),
	// The mask itself, as it stands at this point in the chain: placed, shaped, and blurred if a
	// Blur precedes this. Keeps a mask to the shape of its own silhouette -- its corners, the
	// pinches in it, the edges of the blobs it is made of. Pair it with a Blur, which is what
	// turns a hard painted edge into something that has curvature to measure at all.
	Mask = 1 UMETA(DisplayName = "Mask Itself")
};

// Which invariant of the shape operator to keep by.
UENUM(BlueprintType)
enum class EMixtormatCurvatureMode : uint8
{
	// K. Positive wherever the surface bends the same way along both axes -- a dome or a pit --
	// and negative on a saddle. Zero on anything developable, so a cylinder and a flat plane read
	// alike however sharply the cylinder curves. The one that finds corners rather than edges.
	Gaussian = 0 UMETA(DisplayName = "Gaussian"),
	// H, the average of the two principal curvatures. What the older normal-map curvature
	// approximates. Non-zero on a cylinder, so this is the one that finds edges and creases.
	Mean = 1 UMETA(DisplayName = "Mean"),
	// The larger principal curvature. Reads the sharpest convex bend at each point whatever
	// direction it runs in, so it finds ridges and outer edges without averaging them against the
	// flat axis the way Mean does.
	MaxPrincipal = 2 UMETA(DisplayName = "Max Principal"),
	// The smaller one: the sharpest concave bend. Valleys, grooves, and the inside of a fillet.
	MinPrincipal = 3 UMETA(DisplayName = "Min Principal")
};

// Narrows a mask to where a field bends a particular way.
//
// Scoped beneath the mask it filters, like Blur, and for the same reason: a node can be driven,
// published as a source, and instanced, where a field on the mask could be none of those.
//
// It only ever keeps. The result multiplies into what the mask already covers, so this cannot
// introduce coverage the mask did not have -- which is what makes it composable with everything
// above it in the chain rather than something that has to run last.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskCurvature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature")
	EMixtormatCurvatureSource Source = EMixtormatCurvatureSource::Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature")
	EMixtormatCurvatureMode Mode = EMixtormatCurvatureMode::Mean;

	// Tap spacing in texels: the width of the neighbourhood the curvature is measured over. Small
	// finds the shape of small things, large finds the shape of what those things sit on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature", meta = (ClampMin = "1", ClampMax = "32"))
	int32 Kernel = 2;

	// Height amplitude. The field is 0..1 with no statement of what that is worth against a
	// texel, and curvature is not scale invariant -- both second derivatives scale with this --
	// so it is the control that decides whether a shape registers as a gentle swell or a cliff.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float Scale = 8.0f;

	// The window of signed curvature that becomes coverage. Curvature is unbounded and its useful
	// band moves with Scale and Kernel, so it is stated rather than assumed. Low above High
	// inverts the ramp, which is the whole difference between keeping cavities and keeping edges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature", meta = (ClampMin = "-64.0", ClampMax = "64.0"))
	float RangeLow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature", meta = (ClampMin = "-64.0", ClampMax = "64.0"))
	float RangeHigh = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature")
	bool bInvert = false;

	// How much of the narrowing to apply. Zero is the identity, which is what lets the node be
	// dialled back or driven to nothing without being removed from the chain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curvature", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	bool KeepsAnything() const
	{
		return bEnabled && Weight > 0.0f;
	}
};

// Editor slider ranges, deliberately narrower than the clamps: a clamp is what an asset may hold,
// a range is what a drag has to be able to resolve.
namespace MixtormatMaskCurvatureRange
{
	constexpr double KernelMin = 1.0;
	constexpr double KernelMax = 16.0;
	constexpr int32 KernelDefault = 2;

	constexpr double ScaleMin = 0.0;
	constexpr double ScaleMax = 32.0;
	constexpr double ScaleDefault = 8.0;

	constexpr double RangeMin = -8.0;
	constexpr double RangeMax = 8.0;
	constexpr double RangeLowDefault = 0.0;
	constexpr double RangeHighDefault = 1.0;

	constexpr double WeightMin = 0.0;
	constexpr double WeightMax = 1.0;
	constexpr double WeightDefault = 1.0;

	constexpr double SnapDelta = 0.01;
}
