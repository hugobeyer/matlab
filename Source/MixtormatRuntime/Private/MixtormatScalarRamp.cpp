// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatScalarRamp.h"

void FMixtormatScalarRamp::ResetToIdentity()
{
	Points = { {0.0f, 0.0f}, {1.0f, 1.0f} };
	Interpolation = EMixtormatScalarRampInterpolation::Linear;
}

void FMixtormatScalarRamp::Sanitize()
{
	if (Points.Num() < 2)
	{
		ResetToIdentity();
		return;
	}
	for (FMixtormatScalarRampPoint& Point : Points)
	{
		if (!FMath::IsFinite(Point.X)) { Point.X = 0.0f; }
		if (!FMath::IsFinite(Point.Y)) { Point.Y = Point.X; }
		Point.Y = FMath::Clamp(Point.Y, MinOutput, MaxOutput);
	}
	Points.Sort([](const FMixtormatScalarRampPoint& A, const FMixtormatScalarRampPoint& B)
	{
		return A.X < B.X;
	});
	if (Points.Num() > MaxPoints) { Points.SetNum(MaxPoints); }
	Points[0].X = 0.0f;
	Points.Last().X = 1.0f;
	for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
	{
		const float MinX = Points[Index - 1].X + 1.0e-4f;
		const float MaxX = 1.0f - static_cast<float>(Points.Num() - 1 - Index) * 1.0e-4f;
		Points[Index].X = FMath::Clamp(Points[Index].X, MinX, MaxX);
	}
	if (static_cast<uint8>(Interpolation) > static_cast<uint8>(EMixtormatScalarRampInterpolation::BSpline))
	{
		Interpolation = EMixtormatScalarRampInterpolation::Linear;
	}
}
