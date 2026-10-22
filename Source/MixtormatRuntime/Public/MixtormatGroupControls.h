// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once
#include "MixtormatParameterBinding.h"

namespace MixtormatGroupControls
{
	MIXTORMATRUNTIME_API const UEnum* FindEnumType(FName TypeName);
	MIXTORMATRUNTIME_API bool SameTarget(const FMixtormatParameterAddress& A, const FMixtormatParameterAddress& B);
	MIXTORMATRUNTIME_API const FMixtormatLayerGroup* FindTargetGroup(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target);
	MIXTORMATRUNTIME_API bool ValidateTarget(const FMixtormatBindingScope& Scope, const FMixtormatLayerGroup& Group,
		const FMixtormatGroupControl& Control, const FMixtormatParameterAddress& Target, FText& Error);
	MIXTORMATRUNTIME_API bool ValidateControl(const FMixtormatGroupControl& Control, FText& Error);
	MIXTORMATRUNTIME_API bool ValidateValue(const FMixtormatParameterValue& Value, FText& Error);
	// Returns null for broken/conflicting ownership. Never retargets a missing address by name.
	MIXTORMATRUNTIME_API const FMixtormatGroupControl* FindDrivingControl(const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Target);
	MIXTORMATRUNTIME_API void Apply(const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups,
		TArray<FMixtormatLayer>& OutLayers, TArray<FMixtormatLayerGroup>& OutGroups);
}
