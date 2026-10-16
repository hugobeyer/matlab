// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatBlur.generated.h"

// Owner-typed separable Gaussian. The owner determines the filtered output and scheduling.
// Both axes complete before Amount, masking or semantic output resolve is applied.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBlur
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	bool bEnabled = true;

	// Master radius in composition texels; each axis multiplies this by its scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (DisplayName = "Radius", ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0", Delta = "0.1"))
	float Radius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (DisplayName = "X Scale", ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float ScaleX = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (DisplayName = "Y Scale", ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float ScaleY = 1.0f;

	// Blend the completed filter against the original input; zero is the identity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float Amount = 1.0f;

	// Layer-only channel weights multiply Amount after the completed two-axis filter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Base Color", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BaseColorWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Normal", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float NormalWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Roughness", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RoughnessWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "AO", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float AOWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Metallic", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float MetallicWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Specular / F0", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float F0Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Channels", meta = (DisplayName = "Height", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float HeightWeight = 1.0f;

	bool HasLayerChannelWeight() const
	{
		return BaseColorWeight > 0.0f || NormalWeight > 0.0f || RoughnessWeight > 0.0f
			|| AOWeight > 0.0f || MetallicWeight > 0.0f || F0Weight > 0.0f || HeightWeight > 0.0f;
	}

	float GetRadiusX() const { return Radius * ScaleX; }
	float GetRadiusY() const { return Radius * ScaleY; }

	bool BlursAnything() const
	{
		return bEnabled && Amount > 0.0f && Radius > 0.0f && (ScaleX > 0.0f || ScaleY > 0.0f);
	}
};

// Editor slider ranges.
namespace MixtormatBlurRange
{
	constexpr double RadiusMin = 0.0;
	constexpr double RadiusMax = 32.0;
	constexpr double RadiusDefault = 4.0;
	constexpr double SnapDelta = 0.1;
	constexpr double ScaleMin = 0.0;
	constexpr double ScaleMax = 4.0;
	constexpr double ScaleDefault = 1.0;
	constexpr double ScaleSnapDelta = 0.01;
	constexpr double AmountMin = 0.0;
	constexpr double AmountMax = 1.0;
	constexpr double AmountDefault = 1.0;
	constexpr double AmountSnapDelta = 0.01;
}
