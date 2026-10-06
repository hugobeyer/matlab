// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "Containers/StaticArray.h"

namespace MixtormatColorRampMath
{
	struct FGpuPayload
	{
		uint32 StopCount = 0;
		uint32 Interpolation = 0;
		TStaticArray<float, FMixtormatColorRamp::MaxStops> Positions;
		TStaticArray<FVector4f, FMixtormatColorRamp::MaxStops> Colors;
	};

	MIXTORMATRUNTIME_API FLinearColor Evaluate(const FMixtormatColorRamp& Ramp, float X);
	MIXTORMATRUNTIME_API FGpuPayload PrepareGpuPayload(const FMixtormatColorRamp& Ramp);
}
