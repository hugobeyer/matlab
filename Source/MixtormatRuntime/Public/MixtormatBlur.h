// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.h"
#include "MixtormatBlur.generated.h"

UENUM(BlueprintType)
enum class EMixtormatBlurMode : uint8
{
	Gaussian,
	Directional,
	Smear
};

UENUM(BlueprintType)
enum class EMixtormatBlurDirectionSource : uint8
{
	Angle,
	Flow
};

UENUM(BlueprintType)
enum class EMixtormatBlurTarget : uint8
{
	Height,
	Flow,
	HeightAndFlow UMETA(DisplayName = "Height and Flow")
};

// The owner determines the filtered output and scheduling.
// Complete filtering before applying Amount, masking or semantic output resolve.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBlur
{
	GENERATED_BODY()

	FMixtormatBlur()
	{
		FlowSource.Kind = EMixtormatPublishedFieldKind::Flow;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	EMixtormatBlurMode Mode = EMixtormatBlurMode::Gaussian;

	// Generator/Behavior output selection, independent of the Flow guidance socket.
	// Other owners retain their own output contract and ignore this setting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	EMixtormatBlurTarget Target = EMixtormatBlurTarget::Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Direction")
	EMixtormatBlurDirectionSource DirectionSource = EMixtormatBlurDirectionSource::Angle;

	// Composition texels. Directional uses a nonnegative extent; Smear preserves the sign.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Direction", meta = (UIMin = "-32.0", UIMax = "32.0", Delta = "0.1"))
	float Length = 4.0f;

	// Degrees in texture space. Also supplies the base direction for Flow steering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Direction", meta = (UIMin = "-180.0", UIMax = "180.0", Delta = "1.0"))
	float Angle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Direction", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float FlowInfluence = 1.0f;

	// An enabled but unset reference is unavailable, never an implicit source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur|Direction")
	FMixtormatOutputReference FlowSource;

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

	// Layer-only channel weights multiply Amount after the completed filter.
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
		if (!bEnabled || !(Amount > 0.0f)) { return false; }
		if (Mode == EMixtormatBlurMode::Gaussian)
		{
			return Radius > 0.0f && (ScaleX > 0.0f || ScaleY > 0.0f);
		}
		if (Mode != EMixtormatBlurMode::Directional && Mode != EMixtormatBlurMode::Smear) { return false; }
		if (!FMath::IsFinite(Length) || Length == 0.0f) { return false; }
		return DirectionSource == EMixtormatBlurDirectionSource::Angle
			|| (DirectionSource == EMixtormatBlurDirectionSource::Flow
				&& FlowSource.bEnabled && FlowSource.HasSource()
				&& FlowSource.Kind == EMixtormatPublishedFieldKind::Flow);
	}
};

// Editor slider ranges.
namespace MixtormatBlurRange
{
	constexpr double RadiusMin = 0.0;
	constexpr double RadiusMax = 32.0;
	constexpr double RadiusDefault = 4.0;
	constexpr double SnapDelta = 0.1;
	constexpr double LengthMin = -32.0;
	constexpr double LengthMax = 32.0;
	constexpr double LengthDefault = 4.0;
	constexpr double AngleMin = -180.0;
	constexpr double AngleMax = 180.0;
	constexpr double AngleDefault = 0.0;
	constexpr double AngleSnapDelta = 1.0;
	constexpr double FlowInfluenceMin = 0.0;
	constexpr double FlowInfluenceMax = 1.0;
	constexpr double FlowInfluenceDefault = 1.0;
	constexpr double FlowInfluenceSnapDelta = 0.01;
	constexpr double ScaleMin = 0.0;
	constexpr double ScaleMax = 4.0;
	constexpr double ScaleDefault = 1.0;
	constexpr double ScaleSnapDelta = 0.01;
	constexpr double AmountMin = 0.0;
	constexpr double AmountMax = 1.0;
	constexpr double AmountDefault = 1.0;
	constexpr double AmountSnapDelta = 0.01;
}
