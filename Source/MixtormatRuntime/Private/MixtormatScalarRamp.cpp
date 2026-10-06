// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatScalarRamp.h"

void FMixtormatScalarRamp::ResetToIdentity()
{
	Points = { {DomainMin, DomainMin}, {DomainMax, DomainMax} };
	Interpolation = EMixtormatScalarRampInterpolation::Linear;
}

void FMixtormatScalarRamp::Sanitize()
{
	if (!FMath::IsFinite(DomainMin)) { DomainMin = 0.0f; }
	if (!FMath::IsFinite(DomainMax)) { DomainMax = 1.0f; }
	if (DomainMax <= DomainMin) { DomainMax = DomainMin + 1.0f; }
	if (Points.Num() < 2)
	{
		ResetToIdentity();
		return;
	}
	for (FMixtormatScalarRampPoint& Point : Points)
	{
		if (!FMath::IsFinite(Point.X)) { Point.X = DomainMin; }
		if (!FMath::IsFinite(Point.Y)) { Point.Y = Point.X; }
		Point.Y = FMath::Clamp(Point.Y, MinOutput, MaxOutput);
	}
	Points.Sort([](const FMixtormatScalarRampPoint& A, const FMixtormatScalarRampPoint& B)
	{
		return A.X < B.X;
	});
	if (Points.Num() > MaxPoints) { Points.SetNum(MaxPoints); }
	Points[0].X = DomainMin;
	Points.Last().X = DomainMax;
	for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
	{
		const float MinX = Points[Index - 1].X + 1.0e-4f;
		const float MaxX = DomainMax - static_cast<float>(Points.Num() - 1 - Index) * 1.0e-4f;
		Points[Index].X = FMath::Clamp(Points[Index].X, MinX, MaxX);
	}
	if (static_cast<uint8>(Interpolation) > static_cast<uint8>(EMixtormatScalarRampInterpolation::BSpline))
	{
		Interpolation = EMixtormatScalarRampInterpolation::Linear;
	}
}
