// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MixtormatEditorSettings.generated.h"

// The bake dialog's resolution field is a fixed picker, not a free-typed int, matching
// SMixtormat::SetCompositionResolution's own allow-list (plus 512, which that toolbar menu does
// not currently offer) -- this keeps the config value always renderable and always a size the
// GPU compositor was actually sized for.
UENUM()
enum class EMixtormatBakeResolution : uint8
{
	Res512 UMETA(DisplayName = "512"),
	Res1024 UMETA(DisplayName = "1024"),
	Res2048 UMETA(DisplayName = "2048"),
	Res4096 UMETA(DisplayName = "4096"),
};

inline int32 MixtormatBakeResolutionToPixels(const EMixtormatBakeResolution Resolution)
{
	switch (Resolution)
	{
	case EMixtormatBakeResolution::Res512: return 512;
	case EMixtormatBakeResolution::Res1024: return 1024;
	case EMixtormatBakeResolution::Res4096: return 4096;
	case EMixtormatBakeResolution::Res2048:
	default: return 2048;
	}
}

// Same fixed-picker reasoning as EMixtormatBakeResolution above. AA is infrastructure only right
// now -- see DefaultBakeAASamples -- but the option set is still fixed to what the bake dialog's
// picker offers, not a free-typed int.
UENUM()
enum class EMixtormatBakeAASamples : uint8
{
	Samples1x UMETA(DisplayName = "1x"),
	Samples2x UMETA(DisplayName = "2x"),
	Samples4x UMETA(DisplayName = "4x"),
	Samples8x UMETA(DisplayName = "8x"),
};

inline int32 MixtormatBakeAASamplesToInt(const EMixtormatBakeAASamples Samples)
{
	switch (Samples)
	{
	case EMixtormatBakeAASamples::Samples2x: return 2;
	case EMixtormatBakeAASamples::Samples4x: return 4;
	case EMixtormatBakeAASamples::Samples8x: return 8;
	case EMixtormatBakeAASamples::Samples1x:
	default: return 1;
	}
}

// Editor-preference colors for Mixtormat debug visualization, plus the bake dialog's starting
// values. Read via GetDefault<UMixtormatEditorSettings>(); the bake defaults below are read once
// when the Mixtormat workspace / bake dialog is opened and never written back to, so editing a
// destination or resolution for a single bake never changes these.
UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Mixtormat"))
class MIXTORMATEDITOR_API UMixtormatEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMixtormatEditorSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Mixtormat"); }

	UPROPERTY(EditAnywhere, Config, Category = "Debug", meta = (DisplayName = "Mask Color"))
	FLinearColor MaskColor;

	UPROPERTY(EditAnywhere, Config, Category = "Debug", meta = (DisplayName = "ID Color A"))
	FLinearColor IdColorA;

	UPROPERTY(EditAnywhere, Config, Category = "Debug", meta = (DisplayName = "ID Color B"))
	FLinearColor IdColorB;

	UPROPERTY(EditAnywhere, Config, Category = "Debug", meta = (DisplayName = "Invalid / Grout Color"))
	FLinearColor InvalidGroutColor;

	// Bake-only (SMixtormat::BakeResolution): the live preview's own resolution
	// (SMixtormat::CompositionResolution) is untouched by this setting.
	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Resolution"))
	EMixtormatBakeResolution DefaultBakeResolution = EMixtormatBakeResolution::Res2048;

	// Infrastructure only: nothing in FMixtormatBakeService or the GPU compositor implements
	// supersampling yet. Stored so the setting and its bake-panel control exist ahead of that
	// work; the panel's AA control is disabled until it does something.
	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default AA Samples"))
	EMixtormatBakeAASamples DefaultBakeAASamples = EMixtormatBakeAASamples::Samples1x;

	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Output Path"))
	FString DefaultBakeOutputPath = TEXT("/Game/Mixtormat/Baked");
};
