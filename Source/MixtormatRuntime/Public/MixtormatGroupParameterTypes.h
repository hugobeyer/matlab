// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "MixtormatParameterTypes.h"
#include "MixtormatGroupParameterTypes.generated.h"

// Independent group-owned authoring, never an alias of a member's stored base value.
// Targets are leaf parameter addresses, not controls: fanout cannot form a control cycle.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGroupControl
{
	GENERATED_BODY()
	UPROPERTY() FGuid ControlId = FGuid::NewGuid();
	UPROPERTY(EditAnywhere, Category = "Group Control") FText Label;
	UPROPERTY(EditAnywhere, Category = "Group Control") bool bEnabled = true;
	UPROPERTY() FMixtormatParameterValue Value;
	UPROPERTY() FMixtormatParameterValue DefaultValue;
	// Authored scrub presentation, initialized from canonical metadata of the exposed parameter.
	UPROPERTY() float UiMin = 0.0f;
	UPROPERTY() float UiMax = 1.0f;
	UPROPERTY() float Snap = 0.0f;
	// Color ramps have a consumer-defined X domain; scalar ramps store it in the value itself.
	UPROPERTY() float ColorDomainMin = 0.0f;
	UPROPERTY() float ColorDomainMax = 1.0f;
	UPROPERTY() TArray<FMixtormatParameterAddress> Targets;
};
