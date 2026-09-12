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

// The bake dialog's starting values. Read via GetDefault<UMixtormatEditorSettings>(); they are
// read once when the Mixtormat workspace / bake dialog is opened and never written back to, so
// editing a destination or resolution for a single bake never changes these.
UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Mixtormat"))
class MIXTORMATEDITOR_API UMixtormatEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Mixtormat"); }

	// Bake-only (SMixtormat::BakeResolution): the live preview's own resolution
	// (SMixtormat::CompositionResolution) is untouched by this setting.
	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Resolution"))
	EMixtormatBakeResolution DefaultBakeResolution = EMixtormatBakeResolution::Res2048;

	// Infrastructure only: nothing in FMixtormatBakeService or the GPU compositor implements
	// supersampling yet, so this is deliberately NOT EditAnywhere -- a user who could pick 4x here
	// would be choosing a value the bake ignores. The field stays so the plumbing through
	// FMixtormatBakeSettings and the (disabled) bake-panel control keeps compiling ahead of that
	// work; restore EditAnywhere in the same change that makes supersampling real.
	UPROPERTY(Config)
	EMixtormatBakeAASamples DefaultBakeAASamples = EMixtormatBakeAASamples::Samples1x;

	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Output Path"))
	FString DefaultBakeOutputPath = TEXT("/Game/Mixtormat/Baked");
};
