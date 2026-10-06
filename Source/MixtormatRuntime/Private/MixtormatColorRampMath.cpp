// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatColorRampMath.h"

void FMixtormatColorRamp::Sanitize()
{
	if (Stops.Num() < 2)
	{
		Stops = { FMixtormatColorRampStop{0.0f, FLinearColor::Black},
			FMixtormatColorRampStop{1.0f, FLinearColor::White} };
	}
	for (FMixtormatColorRampStop& Stop : Stops)
	{
		if (!FMath::IsFinite(Stop.X)) { Stop.X = 0.0f; }
		Stop.Color = Stop.Color.GetClamped();
	}
	Stops.Sort([](const FMixtormatColorRampStop& A, const FMixtormatColorRampStop& B)
	{
		return A.X < B.X;
	});
	if (Stops.Num() > MaxStops) { Stops.SetNum(MaxStops); }
	if (static_cast<uint8>(Interpolation) > static_cast<uint8>(EMixtormatColorRampInterpolation::Smooth))
	{
		Interpolation = EMixtormatColorRampInterpolation::Linear;
	}
}

FLinearColor MixtormatColorRampMath::Evaluate(const FMixtormatColorRamp& Source, const float X)
{
	FMixtormatColorRamp Ramp = Source;
	Ramp.Sanitize();
	if (X <= Ramp.Stops[0].X) { return Ramp.Stops[0].Color; }
	if (X >= Ramp.Stops.Last().X) { return Ramp.Stops.Last().Color; }
	for (int32 Index = 0; Index < Ramp.Stops.Num() - 1; ++Index)
	{
		const FMixtormatColorRampStop& A = Ramp.Stops[Index];
		const FMixtormatColorRampStop& B = Ramp.Stops[Index + 1];
		if (X > B.X) { continue; }
		if (Ramp.Interpolation == EMixtormatColorRampInterpolation::Constant) { return A.Color; }
		const float H = FMath::Max(B.X - A.X, 1.0e-6f);
		float T = FMath::Clamp((X - A.X) / H, 0.0f, 1.0f);
		if (Ramp.Interpolation == EMixtormatColorRampInterpolation::Smooth)
		{
			T = T * T * (3.0f - 2.0f * T);
		}
		return FMath::Lerp(A.Color, B.Color, T);
	}
	return Ramp.Stops.Last().Color;
}

MixtormatColorRampMath::FGpuPayload MixtormatColorRampMath::PrepareGpuPayload(const FMixtormatColorRamp& Source)
{
	FMixtormatColorRamp Ramp = Source;
	Ramp.Sanitize();
	FGpuPayload Payload;
	Payload.StopCount = FMath::Min(Ramp.Stops.Num(), FMixtormatColorRamp::MaxStops);
	Payload.Interpolation = static_cast<uint32>(Ramp.Interpolation);
	for (int32 Index = 0; Index < FMixtormatColorRamp::MaxStops; ++Index)
	{
		const int32 SourceIndex = FMath::Min(Index, Ramp.Stops.Num() - 1);
		const FMixtormatColorRampStop& Stop = Ramp.Stops[SourceIndex];
		Payload.Positions[Index] = Stop.X;
		Payload.Colors[Index] = FVector4f(Stop.Color.R, Stop.Color.G, Stop.Color.B, Stop.Color.A);
	}
	return Payload;
}
