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

	// Shared with the live preview resolution (SMixtormat::CompositionResolution), same as the
	// toolbar's own resolution picker.
	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Resolution"))
	EMixtormatBakeResolution DefaultBakeResolution = EMixtormatBakeResolution::Res2048;

	// Infrastructure only: nothing in FMixtormatBakeService currently reads this. Stored so the
	// setting exists ahead of that work, not to imply AA is already applied to bake output.
	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default AA Samples", ClampMin = "1", UIMin = "1"))
	int32 DefaultBakeAASamples = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Bake", meta = (DisplayName = "Default Output Path"))
	FString DefaultBakeOutputPath = TEXT("/Game/Mixtormat/Baked");
};
