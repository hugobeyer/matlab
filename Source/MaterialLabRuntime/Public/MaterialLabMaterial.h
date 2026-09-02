#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MaterialLabMaterial.generated.h"

class UMaterialInterface;
class UMaterialLabEffect;
class UMaterialLabMask;
class UMaterialLabSurface;
class UTexture2D;

UENUM(BlueprintType)
enum class EMaterialLabLayerType : uint8
{
	Material UMETA(DisplayName = "Material Layer"),
	Fill UMETA(DisplayName = "Fill Layer"),
	Effect UMETA(DisplayName = "Effect Layer")
};

UENUM(BlueprintType)
enum class EMaterialLabCompositionMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Coat UMETA(DisplayName = "Coat")
};

UENUM(BlueprintType)
enum class EMaterialLabLayerChannelMode : uint8
{
	CompleteSurface UMETA(DisplayName = "Complete Surface"),
	NormalDetail UMETA(DisplayName = "Normal Detail")
};

UENUM(BlueprintType)
enum class EMaterialLabNormalSourceType : uint8
{
	Surface UMETA(DisplayName = "Surface Normal"),
	Texture UMETA(DisplayName = "Standalone Normal")
};

UENUM(BlueprintType)
enum class EMaterialLabNormalBlendMode : uint8
{
	Combine UMETA(DisplayName = "Combine (RNM)"),
	Override UMETA(DisplayName = "Override")
};

UENUM(BlueprintType)
enum class EMaterialLabHeightSource : uint8
{
	Automatic = 0 UMETA(DisplayName = "Automatic (Legacy)"),
	RAMHAlpha = 1 UMETA(DisplayName = "RAMH Height"),
	CombinedMask = 2 UMETA(DisplayName = "Mask as Height"),
	Constant = 3 UMETA(DisplayName = "Constant Height"),
	LayerHeight = 4 UMETA(DisplayName = "Layer Height")
};

UENUM(BlueprintType)
enum class EMaterialLabMaskBlendMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Multiply UMETA(DisplayName = "Multiply"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max")
};

USTRUCT(BlueprintType)
struct MATERIALLABRUNTIME_API FMaterialLabMaskLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UMaterialLabMask> Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMaterialLabMaskBlendMode BlendMode = EMaterialLabMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "1", ClampMax = "16"))
	int32 Tiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Balance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0"))
	float Contrast = 1.0f;
};

USTRUCT(BlueprintType)
struct MATERIALLABRUNTIME_API FMaterialLabLayerEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TSoftObjectPtr<UMaterialLabEffect> Effect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Front = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.000001"))
	float Width = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MacroWarp = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MicroWarp = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MicroMorph = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float Thickness = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float Lift = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float DetailStrength = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	FLinearColor StainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float StainRoughness = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StainHeightInfluence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StainHeightWarp = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float StainHeightBias = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.01"))
	float StainHeightContrast = 1.0f;
};

UENUM(BlueprintType)
enum class EMaterialLabLayerChildType : uint8
{
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect")
};

USTRUCT(BlueprintType)
struct MATERIALLABRUNTIME_API FMaterialLabLayerChild
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child")
	EMaterialLabLayerChildType Type = EMaterialLabLayerChildType::Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMaterialLabLayerChildType::Mask"))
	FMaterialLabMaskLayer Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMaterialLabLayerChildType::Effect"))
	FMaterialLabLayerEffect Effect;
};

USTRUCT(BlueprintType)
struct MATERIALLABRUNTIME_API FMaterialLabLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMaterialLabLayerType Type = EMaterialLabLayerType::Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TSoftObjectPtr<UMaterialLabSurface> SourceSurface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMaterialLabLayerChannelMode ChannelMode = EMaterialLabLayerChannelMode::CompleteSurface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normal Detail", meta = (EditCondition = "ChannelMode == EMaterialLabLayerChannelMode::NormalDetail"))
	EMaterialLabNormalSourceType NormalSourceType = EMaterialLabNormalSourceType::Surface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normal Detail", meta = (EditCondition = "ChannelMode == EMaterialLabLayerChannelMode::NormalDetail && NormalSourceType == EMaterialLabNormalSourceType::Texture"))
	TSoftObjectPtr<UTexture2D> NormalTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMaterialLabCompositionMode CompositionMode = EMaterialLabCompositionMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideBaseColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideBaseColor"))
	FLinearColor BaseColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideRoughness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideRoughness", ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideIOR = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideIOR", ClampMin = "1.0", ClampMax = "3.0"))
	float IOR = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideMetallic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideMetallic", ClampMin = "0.0", ClampMax = "1.0"))
	float Metallic = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "1.0", ClampMax = "8.0", Delta = "1.0"))
	float Tiling = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoughnessBias = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RoughnessContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	float RoughnessOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float NormalIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float HueShift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Saturation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Value = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	EMaterialLabNormalBlendMode NormalBlendMode = EMaterialLabNormalBlendMode::Combine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending")
	bool bHeightBlendEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	EMaterialLabHeightSource HeightSource = EMaterialLabHeightSource::LayerHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Mask Strength", EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "4.0"))
	float HeightBlendAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HeightThreshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Softness", EditCondition = "bHeightBlendEnabled", ClampMin = "0.0"))
	float HeightRange = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.01"))
	float HeightContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Blend Height Bias", EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Base Height Bias", EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	bool bInvertHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float ConstantHeight = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float MaskHeightInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HeightContactAOAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0001", ClampMax = "1.0"))
	float HeightContactAOWidth = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightBorderLift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0001", ClampMax = "1.0"))
	float HeightBorderWidth = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "8.0"))
	float HeightBorderNormalStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	int32 HeightReferenceLayerIndex = INDEX_NONE;

	// Serialized only to migrate the original single-mask recipe format.
	UPROPERTY()
	TSoftObjectPtr<UMaterialLabMask> Mask;

	UPROPERTY()
	TSoftObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY()
	float MaskTiling = 1.0f;

	UPROPERTY()
	float MaskBalance = 0.5f;

	UPROPERTY()
	float MaskContrast = 1.0f;

	UPROPERTY()
	bool bInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Children", meta = (TitleProperty = "Type"))
	TArray<FMaterialLabLayerChild> Children;

	// Serialized only to migrate recipes created before ordered children.
	UPROPERTY()
	TArray<FMaterialLabMaskLayer> Masks;

	// Serialized only to migrate recipes created before ordered children.
	UPROPERTY()
	TArray<FMaterialLabLayerEffect> Effects;

	void GetEffectiveMasks(TArray<FMaterialLabMaskLayer>& OutMasks) const;
	void MigrateLegacyChildren();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeatureBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertHeightFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertAOFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "1", ClampMax = "32"))
	int32 CurvatureRadius = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0"))
	float CurvatureStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.001"))
	float CurvaturePower = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bFlipNormalY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseColorInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoughnessInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MetallicInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (DisplayName = "IOR / F0 Influence", ClampMin = "0.0", ClampMax = "1.0"))
	float F0Influence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightInfluence = 1.0f;
};

UCLASS(BlueprintType)
class MATERIALLABRUNTIME_API UMaterialLabMaterial final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UFUNCTION(BlueprintPure, Category = "Material Lab")
	bool CanAddLayer() const;

	UFUNCTION(BlueprintCallable, Category = "Material Lab")
	bool AddLayer(EMaterialLabLayerType Type);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layers", meta = (TitleProperty = "DisplayName"))
	TArray<FMaterialLabLayer> Layers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedBaseColor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedNormal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedRAM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedHeight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UMaterialInterface> BakedMaterial;
};
