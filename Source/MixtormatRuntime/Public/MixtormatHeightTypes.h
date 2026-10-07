// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatHeightTypes.generated.h"

// How a layer's height combines with the height already standing below it, weighted by the
// layer's coverage so every op fades in the same way. Replace is the old OVER; Max with Softness
// is the old BLEND smooth merge. Height Blend is the one op that decides coverage itself: the
// incoming surface runs over the one below where it is higher, gated by the mask.
//
// Serialised by value and mirrored by the MIXTORMAT_HEIGHT_OP_* defines in MixtormatHeightOps.ush.
UENUM(BlueprintType)
enum class EMixtormatHeightOp : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	// Signed about 0.5, the flat midpoint: below + (layer - 0.5).
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	// 0.5 is neutral: below * (layer * 2).
	Multiply UMETA(DisplayName = "Multiply"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	Difference UMETA(DisplayName = "Difference"),
	HeightBlend UMETA(DisplayName = "Height Blend")
};

// The one height-combine block, used by a layer against the stack below it and by a Generator
// module against the layer's running height. Same fields, same meaning, one shader function.
//
// Op, Softness and Amount are common to every op. The rest only matter at Op = Height Blend:
//
//     a = base + BaseBias;  b = blend + BlendBias;  m = saturate(mask * Strength)
//     t = smoothstep(Threshold - EdgeSoftness, Threshold + EdgeSoftness, m + b - a)
//     height = lerp(a, b, t)
//
// where t is also the coverage every channel uses. A layer's Strength is driven through its own
// HeightBlendAmount (a Driver binds a flat property of the layer), so only a module reads Strength.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatHeightBlend
{
	GENERATED_BODY()

	FMixtormatHeightBlend() = default;
	explicit FMixtormatHeightBlend(const EMixtormatHeightOp InOp)
		: Op(InOp)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	EMixtormatHeightOp Op = EMixtormatHeightOp::Add;

	// Width of the rounded join for Min and Max, in height units. 0 is a hard min or max.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (UIMin = "0.0", UIMax = "0.5"))
	float Softness = 0.0f;

	// How much of the op's height reaches the result.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Amount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "0.0", UIMax = "4.0"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Threshold = 0.5f;

	// Half-width of the smoothstep that turns the height contest into coverage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (DisplayName = "Edge Softness", UIMin = "0.0"))
	float EdgeSoftness = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BaseBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blend", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BlendBias = 0.0f;
};
