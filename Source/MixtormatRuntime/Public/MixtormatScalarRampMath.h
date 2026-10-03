// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatScalarRamp.h"
#include "Containers/StaticArray.h"

namespace MixtormatScalarRampMath
{
	struct FBounds
	{
		float MinY = 0.0f;
		float MaxY = 1.0f;
	};

	struct FGpuPayload
	{
		uint32 PointCount = 0;
		uint32 Interpolation = 0;
		TStaticArray<FVector4f, FMixtormatScalarRamp::MaxPoints> Points;
	};

	MIXTORMATRUNTIME_API float Evaluate(const FMixtormatScalarRamp& Ramp, float X);
	MIXTORMATRUNTIME_API float EvaluateTangent(const FMixtormatScalarRamp& Ramp, float X);
	MIXTORMATRUNTIME_API FBounds ComputeBounds(const FMixtormatScalarRamp& Ramp, int32 Samples = 256);
	MIXTORMATRUNTIME_API void SamplePolyline(
		const FMixtormatScalarRamp& Ramp, int32 Samples, TArray<FVector2f>& OutPoints);
	MIXTORMATRUNTIME_API float ResistedTravel(float Travel, float Range, bool bStrongBoundary = false);
	MIXTORMATRUNTIME_API FGpuPayload PrepareGpuPayload(const FMixtormatScalarRamp& Ramp);
}
