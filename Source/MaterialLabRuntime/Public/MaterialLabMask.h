#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MaterialLabMask.generated.h"

class UTexture2D;

UCLASS(BlueprintType)
class MATERIALLABRUNTIME_API UMaterialLabMask final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mask")
	TObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UTexture2D> Thumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "1.0", ClampMax = "16.0", Delta = "1.0"))
	float DefaultTiling = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DefaultBalance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DefaultContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	bool bDefaultInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float DefaultOffset = 0.0f;
};
