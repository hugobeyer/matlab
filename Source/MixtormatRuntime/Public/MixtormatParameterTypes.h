// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatParameterTypes.generated.h"

UENUM(BlueprintType)
enum class EMixtormatParameterOwnerType : uint8
{
	Layer UMETA(DisplayName = "Layer"),
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect"),
	Generated UMETA(DisplayName = "Generated Mask"),
	Craquelure UMETA(DisplayName = "Craquelure"),
	ColorId UMETA(DisplayName = "Color ID"),
	ClusterId UMETA(DisplayName = "Cluster IDs"),
	HsvId UMETA(DisplayName = "HSV From IDs"),
	RandomId UMETA(DisplayName = "Random From IDs"),
	PatternId UMETA(DisplayName = "Pattern IDs"),
	RampId UMETA(DisplayName = "Ramp From IDs"),
	MaskShaping UMETA(DisplayName = "Mask Shaping"),
	Blur UMETA(DisplayName = "Blur"),
	Curvature UMETA(DisplayName = "Curvature"),
	// Appended, like everything above it. The owner names the *category*, not the generator
	// kind: the parameter view resolves to whichever payload FMixtormatGenerator::Type selects,
	// so a second generator gets its parameters bound without a second owner value.
	Generator UMETA(DisplayName = "Generator"),
	// Appended for the two nodes that took Pattern's UV and relief responsibilities. Their
	// controls are ordinary bindable scalars, so they need an owner of their own for a reference
	// or a driver to address; Pattern's own legacy fields keep PatternId.
	UvId UMETA(DisplayName = "UV From IDs"),
	ReliefId UMETA(DisplayName = "Relief From IDs"),
	// Appended with its child type. Combine IDs arrived after this enum's last pass and its rows
	// fell through to Layer, which stored their bindings on the layer and resolved to nothing.
	CombineId UMETA(DisplayName = "Combine IDs"),
	IdGroup UMETA(DisplayName = "ID Group"),
	BoundaryId UMETA(DisplayName = "Boundary From IDs"),
	// Appended with the Generator-layer sublayers, so their rows can be bound and driven.
	HeightBlend UMETA(DisplayName = "Height Blend"),
	HeightCurve UMETA(DisplayName = "Height Remap"),
	HeightColorRamp UMETA(DisplayName = "Color Ramp"),
	// Deprecated. HeightPush (23), StructuralWarp (24) and StructuralWarpFlow (26) are dead
	// parameter owners: their authoring path and payloads are gone, and their values are held
	// back so the enum's remaining indices keep matching what saved bindings stored. Nothing
	// resolves to them any more, so a binding naming one simply stops driving anything.
	MaskNoise = 25 UMETA(DisplayName = "Mask Noise"),
	// The one structural parameter owner. Flow lives inside the Behavior's Direction socket,
	// so one owner covers the payload and one covers the typed reference inside it.
	Behavior = 27 UMETA(DisplayName = "Behavior"),
	BehaviorFlow = 28 UMETA(DisplayName = "Behavior Flow")
};

UENUM(BlueprintType)
enum class EMixtormatParameterValueType : uint8
{
	Float UMETA(DisplayName = "Float"),
	Int UMETA(DisplayName = "Integer"),
	Bool UMETA(DisplayName = "Boolean"),
	Enum UMETA(DisplayName = "Enum"),
	// Appended: an address whose property maps to no value type (structs, arrays, strings).
	// Bindings serialize this enum by value, so appending keeps existing assets intact. An
	// Invalid address matches no property: every typed read/write refuses it instead of
	// silently reinterpreting the value as a float (plan D6).
	Invalid UMETA(DisplayName = "Invalid")
};

UENUM(BlueprintType)
enum class EMixtormatDriverCombineMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Multiply UMETA(DisplayName = "Multiply"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	Lerp UMETA(DisplayName = "Lerp")
};

UENUM(BlueprintType)
enum class EMixtormatDriverSourceKind : uint8
{
	None UMETA(DisplayName = "None"),
	CombinedMask UMETA(DisplayName = "Layer Mask"),
	ChildMask UMETA(DisplayName = "Child Mask"),
	RegionIds UMETA(DisplayName = "Region IDs")
};

UENUM(BlueprintType)
enum class EMixtormatIdDriverMapping : uint8
{
	RandomPerId UMETA(DisplayName = "Random Per ID"),
	SpecificId UMETA(DisplayName = "Specific ID"),
	IdRange UMETA(DisplayName = "ID Range")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterAddress
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid LayerId;

	UPROPERTY()
	FGuid ChildId;

	UPROPERTY()
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Layer;

	UPROPERTY()
	FName Parameter;

	UPROPERTY()
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	// Used for enum compatibility. Empty for scalar/bool types.
	UPROPERTY()
	FName TypeName;

	bool IsValid() const
	{
		return LayerId.IsValid() && !Parameter.IsNone();
	}
};

// What a reference does when the destination is edited.
//
// Follow is one-way: the destination shows the source and an edit to the destination is an edit
// to the destination, which is what breaks the reference. Link makes the pair one value with two
// places to reach it -- an edit at either end lands on the authoritative source, so neither side
// is the copy. References only; a GPU Driver modulates a value it does not own and has nothing to
// write back to.
UENUM(BlueprintType)
enum class EMixtormatReferenceMode : uint8
{
	Follow UMETA(DisplayName = "Follow Source"),
	Link   UMETA(DisplayName = "Link Values")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	FMixtormatParameterAddress Source;

	// Follow by default, so everything already serialized keeps the one-way behaviour it was
	// authored with.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	EMixtormatReferenceMode Mode = EMixtormatReferenceMode::Follow;
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterDriver
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FGuid SourceLayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FGuid SourceChildId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatDriverSourceKind SourceKind = EMixtormatDriverSourceKind::None;

	// Optional published output name. This keeps the serialized model extensible without making
	// every future mask/effect output another enum value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FName SourceOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatDriverCombineMode Combine = EMixtormatDriverCombineMode::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float Amount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float InputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float InputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float OutputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float OutputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatIdDriverMapping IdMapping = EMixtormatIdDriverMapping::RandomPerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 SpecificId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 IdRangeMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 IdRangeMax = 255;

	// The range one region's random draw lands in, before the Driver chain's own remap. Separate
	// from OutputMin/OutputMax on purpose: this shapes the signal a region *produces*, the remap
	// shapes what the chain does with any signal, and collapsing them would make the Output rows
	// mean two different things depending on the source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float IdRandomMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float IdRandomMax = 1.0f;
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FName DestinationParameter;

	UPROPERTY()
	EMixtormatParameterOwnerType DestinationOwner = EMixtormatParameterOwnerType::Layer;

	UPROPERTY()
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	UPROPERTY()
	FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FMixtormatParameterReference Reference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FMixtormatParameterDriver Driver;
};
