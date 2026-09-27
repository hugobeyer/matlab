// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Preview/MixtormatPreviewSceneSettings.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AssetViewerSettings.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "ShowFlags.h"
#include "Materials/MaterialInterface.h"
#include "Services/MixtormatPaths.h"
#include "Widgets/SMixtormatPreviewViewport.h"

void MixtormatPreviewSceneSettings::ConfigureLookdevProfile(FPreviewSceneProfile& Profile)
{
	const FString FloorMaterialPath = FMixtormatPaths::StudioFloorMaterialObjectPath();
	Profile.EnvironmentFloorMaterialPath = FloorMaterialPath;
	Profile.EnvironmentFloorMaterial =
		TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FloorMaterialPath));
	Profile.bUseSkyLighting = true;
	Profile.bShowEnvironment = false;
	Profile.bShowFloor = true;

	Profile.bPostProcessingEnabled = true;
	Profile.bEnableToneMapping = true;
	Profile.PostProcessingSettings.bOverride_AutoExposureMinBrightness = true;
	Profile.PostProcessingSettings.AutoExposureMinBrightness = 0.0f;
	Profile.PostProcessingSettings.bOverride_AutoExposureMaxBrightness = true;
	Profile.PostProcessingSettings.AutoExposureMaxBrightness = 0.0f;
	Profile.PostProcessingSettings.bOverride_AutoExposureBias = true;
	Profile.PostProcessingSettings.AutoExposureBias = -0.5f;
	Profile.PostProcessingSettings.bOverride_BloomIntensity = true;
	Profile.PostProcessingSettings.BloomIntensity = 0.0f;
	Profile.PostProcessingSettings.bOverride_MotionBlurAmount = true;
	Profile.PostProcessingSettings.MotionBlurAmount = 0.0f;
	Profile.PostProcessingSettings.bOverride_MotionBlurMax = true;
	Profile.PostProcessingSettings.MotionBlurMax = 0.0f;
}

FString MixtormatPreviewSceneSettings::GetStudioEnvironmentObjectPath(
	const EMixtormatStudioLighting LightingPreset)
{
	const TCHAR* AssetName = TEXT("monochrome_studio_02_2k");
	switch (LightingPreset)
	{
	case EMixtormatStudioLighting::Soft:
		AssetName = TEXT("white_home_studio_2k");
		break;
	case EMixtormatStudioLighting::Dramatic:
		AssetName = TEXT("brown_photostudio_02_2k");
		break;
	case EMixtormatStudioLighting::Rim:
		AssetName = TEXT("ferndale_studio_01_2k");
		break;
	case EMixtormatStudioLighting::Workshop:
		AssetName = TEXT("empty_workshop_2k");
		break;
	case EMixtormatStudioLighting::Neutral:
	default:
		break;
	}
	return FString::Printf(TEXT("%s/%s.%s"), *FMixtormatPaths::LightingRoot(), AssetName, AssetName);
}

FMixtormatStudioLightSettings MixtormatPreviewSceneSettings::GetStudioLighting(
	const EMixtormatStudioLighting LightingPreset)
{
	FMixtormatStudioLightSettings Settings;
	switch (LightingPreset)
	{
	case EMixtormatStudioLighting::Soft:
		Settings.LightBrightness = 1.25f;
		Settings.SkyBrightness = 0.65f;
		Settings.LightSourceAngle = 16.0f;
		Settings.LightRotation = FRotator(-28.0f, 30.0f, 0.0f);
		break;
	case EMixtormatStudioLighting::Dramatic:
		Settings.LightBrightness = 3.0f;
		Settings.SkyBrightness = 0.15f;
		Settings.LightSourceAngle = 6.0f;
		Settings.LightRotation = FRotator(-48.0f, -65.0f, 0.0f);
		break;
	case EMixtormatStudioLighting::Rim:
		Settings.LightBrightness = 2.5f;
		Settings.SkyBrightness = 0.2f;
		Settings.LightSourceAngle = 8.0f;
		Settings.LightRotation = FRotator(-22.0f, 145.0f, 0.0f);
		break;
	case EMixtormatStudioLighting::Workshop:
		Settings.LightBrightness = 1.75f;
		Settings.SkyBrightness = 0.5f;
		Settings.LightSourceAngle = 12.0f;
		Settings.LightRotation = FRotator(-38.0f, -110.0f, 0.0f);
		break;
	case EMixtormatStudioLighting::Neutral:
	default:
		break;
	}
	return Settings;
}

void MixtormatPreviewSceneSettings::ConfigureQuality(
	FEngineShowFlags& ShowFlags,
	const EMixtormatPreviewQuality Quality)
{
	// Quality changes must never restore inherited editor/project motion blur.
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetDynamicShadows(true);
	// Height fog stays a screen pass over the mesh. Volumetric fog would put it in the froxel
	// volume that Lumen samples, which fogs the traced reflections themselves -- the sky is what
	// those are for. Off at every quality level, including the ones that trace.
	ShowFlags.SetVolumetricFog(false);
	const bool bLumen = Quality == EMixtormatPreviewQuality::Lumen;
	ShowFlags.SetGlobalIllumination(true);
	ShowFlags.SetSkyLighting(true);
	ShowFlags.SetReflectionEnvironment(true);
	ShowFlags.SetLumenGlobalIllumination(bLumen);
	ShowFlags.SetLumenReflections(bLumen);
	ShowFlags.SetScreenSpaceReflections(bLumen);
	// Keep the Default studio preview clean of engine AO artifacts. Lumen is still
	// available as an explicit lighting mode when broader indirect occlusion is needed.
	ShowFlags.SetAmbientOcclusion(bLumen);
	ShowFlags.SetScreenSpaceAO(bLumen);
}

float MixtormatPreviewSceneSettings::CalculateFocusDistance(
	const float BoundsRadius,
	const float HorizontalFovDegrees,
	const FVector2D& ViewportSize,
	const float Margin)
{
	const float Radius = FMath::Max(BoundsRadius, KINDA_SMALL_NUMBER);
	const float HorizontalHalfFov = FMath::DegreesToRadians(HorizontalFovDegrees) * 0.5f;
	const float AspectRatio = ViewportSize.Y > KINDA_SMALL_NUMBER
		? FMath::Max(ViewportSize.X / ViewportSize.Y, KINDA_SMALL_NUMBER)
		: 1.0f;
	const float VerticalHalfFov = FMath::Atan(FMath::Tan(HorizontalHalfFov) / AspectRatio);
	const float LimitingHalfFov = FMath::Min(HorizontalHalfFov, VerticalHalfFov);
	const float FitDistance = Radius / FMath::Max(FMath::Sin(LimitingHalfFov), KINDA_SMALL_NUMBER);
	return FitDistance * FMath::Max(Margin, KINDA_SMALL_NUMBER);
}
