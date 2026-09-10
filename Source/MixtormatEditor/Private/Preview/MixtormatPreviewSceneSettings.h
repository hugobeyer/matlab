#pragma once

#include "CoreMinimal.h"

struct FEngineShowFlags;
struct FPreviewSceneProfile;

enum class EMixtormatPreviewQuality : uint8;
enum class EMixtormatStudioLighting : uint8;

struct FMixtormatStudioLightSettings
{
	float LightBrightness = 2.0f;
	float SkyBrightness = 0.45f;
	float LightSourceAngle = 10.0f;
	FRotator LightRotation = FRotator(-35.0f, -45.0f, 0.0f);
};

namespace MixtormatPreviewCamera
{
	constexpr float FovDefault = 40.0f;
	constexpr float FovMinimum = 20.0f;
	constexpr float FovMaximum = 90.0f;

	constexpr float DistanceDefault = 225.0f;
	constexpr float DistanceMinimum = 75.0f;
	constexpr float DistanceMaximum = 400.0f;

	constexpr float YawDefault = 195.0f;
	constexpr float PitchDefault = -8.0f;
	constexpr float FocusMargin = 1.15f;
}

namespace MixtormatPreviewSceneSettings
{
	constexpr int32 SurfaceThumbnailResolution = 128;
	constexpr int32 MaskThumbnailResolution = 256;

	void ConfigureLookdevProfile(FPreviewSceneProfile& Profile);
	FString GetStudioEnvironmentObjectPath(EMixtormatStudioLighting LightingPreset);
	FMixtormatStudioLightSettings GetStudioLighting(EMixtormatStudioLighting LightingPreset);
	void ConfigureQuality(FEngineShowFlags& ShowFlags, EMixtormatPreviewQuality Quality);
	float CalculateFocusDistance(
		float BoundsRadius,
		float HorizontalFovDegrees,
		const FVector2D& ViewportSize,
		float Margin = MixtormatPreviewCamera::FocusMargin);
}
