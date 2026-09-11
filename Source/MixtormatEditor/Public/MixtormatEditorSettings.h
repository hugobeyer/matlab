#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MixtormatEditorSettings.generated.h"

// Editor-preference colors for Mixtormat debug visualization. Read via
// GetDefault<UMixtormatEditorSettings>(); nothing in the compositor or bake path consumes these
// yet -- this only makes the values persistent and editable.
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
};
