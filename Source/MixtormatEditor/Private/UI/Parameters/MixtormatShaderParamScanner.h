// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatParameterDefinition.h"

struct FMixtormatShaderParamTag
{
	FString ShaderFile;
	FString UniformName;
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Effect;
	FName Parameter;
	TOptional<float> HardMin;
	TOptional<float> HardMax;
	TOptional<float> Normalize;
	bool bSaturates = false;
	bool bDivisor = false;
};

namespace MixtormatShaderParamScanner
{
	// Scans the plugin's shader sources. Tags must be adjacent to their uniform declaration.
	MIXTORMATEDITOR_API bool Scan(
		TArray<FMixtormatShaderParamTag>& OutTags,
		TArray<FString>& OutErrors);
}
