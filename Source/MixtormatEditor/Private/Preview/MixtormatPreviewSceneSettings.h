// Copyright 2026 Hugo Beyer. All Rights Reserved.

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

	// The floor is scaled to this many mesh radii, and the floor material fades itself out in its
	// own object space -- so the fade lands at the same place relative to the object whatever the
	// object's size. That replaces the height fog, which reached a fixed world distance and so
	// had to be re-derived from the camera every time anything moved, and which tinted the
	// traced reflections on the way past.
	constexpr float FloorRadiusInMeshRadii = 12.0f;

	// Where the floor starts fading and where it has fully reached the background, in the same
	// units. Both stay well inside FloorRadiusInMeshRadii so the fade finishes before the mesh
	// runs out -- a fade still in progress at the mesh edge is a visible straight line.
	constexpr float FloorFadeInInMeshRadii = 3.0f;
	constexpr float FloorFadeOutInMeshRadii = 8.0f;

	// The studio presets author SkyBrightness for a captured HDRI. A specified cubemap loses a
	// little energy through the SH diffuse convolution, so every scene lighting itself from one
	// scales by this on the way in. It lives here because both the live viewport and the
	// thumbnail renderer need it and a second copy is how they drifted apart.
	constexpr float CubemapReflectionBoost = 1.35f;

	// Brightness of the specified-cubemap reflection capture both scenes place around the
	// subject. It is a strong number because the capture is the only specular source that is
	// always there: Lumen and SSR trace the rendered scene, and where a ray escapes into the
	// hidden backdrop they find nothing. The capture reads the studio HDRI asset directly, so
	// the sphere keeps its reflections no matter what the tracing does or does not find.
	constexpr float ReflectionCaptureBrightness = 10.0f;

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
