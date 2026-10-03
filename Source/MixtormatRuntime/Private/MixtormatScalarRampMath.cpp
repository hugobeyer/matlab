// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatScalarRampMath.h"

namespace
{
	struct FCubicData
	{
		TArray<float> SecondDerivatives;
	};

	FCubicData MakeNaturalSpline(const FMixtormatScalarRamp& Ramp)
	{
		const int32 Count = Ramp.Points.Num();
		FCubicData Data;
		Data.SecondDerivatives.Init(0.0f, Count);
		if (Count < 3) { return Data; }
		TArray<float> Lower, Diagonal, Upper, RHS;
		const int32 Unknowns = Count - 2;
		Lower.Init(0.0f, Unknowns);
		Diagonal.Init(0.0f, Unknowns);
		Upper.Init(0.0f, Unknowns);
		RHS.Init(0.0f, Unknowns);
		for (int32 Row = 0; Row < Unknowns; ++Row)
		{
			const int32 I = Row + 1;
			const float H0 = FMath::Max(Ramp.Points[I].X - Ramp.Points[I - 1].X, 1.0e-6f);
			const float H1 = FMath::Max(Ramp.Points[I + 1].X - Ramp.Points[I].X, 1.0e-6f);
			Lower[Row] = H0;
			Diagonal[Row] = 2.0f * (H0 + H1);
			Upper[Row] = H1;
			RHS[Row] = 6.0f * ((Ramp.Points[I + 1].Y - Ramp.Points[I].Y) / H1
				- (Ramp.Points[I].Y - Ramp.Points[I - 1].Y) / H0);
		}
		for (int32 Row = 1; Row < Unknowns; ++Row)
		{
			const float Factor = Lower[Row] / Diagonal[Row - 1];
			Diagonal[Row] -= Factor * Upper[Row - 1];
			RHS[Row] -= Factor * RHS[Row - 1];
		}
		for (int32 Row = Unknowns - 1; Row >= 0; --Row)
		{
			const float Next = Row + 1 < Unknowns ? Upper[Row] * Data.SecondDerivatives[Row + 2] : 0.0f;
			Data.SecondDerivatives[Row + 1] = (RHS[Row] - Next) / Diagonal[Row];
		}
		return Data;
	}

	int32 SegmentAt(const FMixtormatScalarRamp& Ramp, const float X)
	{
		for (int32 Index = 0; Index < Ramp.Points.Num() - 1; ++Index)
		{
			if (X <= Ramp.Points[Index + 1].X) { return Index; }
		}
		return Ramp.Points.Num() - 2;
	}
}

float MixtormatScalarRampMath::Evaluate(const FMixtormatScalarRamp& Ramp, const float InputX)
{
	if (Ramp.Interpolation == EMixtormatScalarRampInterpolation::Linear && Ramp.Points.Num() == 2
		&& FMath::IsNearlyEqual(Ramp.Points[0].X, 0.0f) && FMath::IsNearlyEqual(Ramp.Points[0].Y, 0.0f)
		&& FMath::IsNearlyEqual(Ramp.Points[1].X, 1.0f) && FMath::IsNearlyEqual(Ramp.Points[1].Y, 1.0f))
	{
		return InputX;
	}
	if (Ramp.Points.Num() < 2) { return FMath::Clamp(InputX, 0.0f, 1.0f); }
	const float X = FMath::Clamp(InputX, 0.0f, 1.0f);
	const int32 I = SegmentAt(Ramp, X);
	const FMixtormatScalarRampPoint& A = Ramp.Points[I];
	const FMixtormatScalarRampPoint& B = Ramp.Points[I + 1];
	const float H = FMath::Max(B.X - A.X, 1.0e-6f);
	const float T = FMath::Clamp((X - A.X) / H, 0.0f, 1.0f);
	switch (Ramp.Interpolation)
	{
	case EMixtormatScalarRampInterpolation::Constant:
		return X >= 1.0f ? B.Y : A.Y;
	case EMixtormatScalarRampInterpolation::Spline:
	{
		const auto Secant = [&Ramp](const int32 S)
		{
			const auto& P = Ramp.Points[S]; const auto& Q = Ramp.Points[S + 1];
			return (Q.Y - P.Y) / FMath::Max(Q.X - P.X, 1.0e-6f);
		};
		const float D = Secant(I);
		auto Tangent = [&Ramp, &Secant](const int32 K)
		{
			if (K == 0) { return Secant(0); }
			if (K == Ramp.Points.Num() - 1) { return Secant(K - 1); }
			const float L = Secant(K - 1), R = Secant(K);
			if (L * R <= 0.0f) { return 0.0f; }
			const float HL = Ramp.Points[K].X - Ramp.Points[K - 1].X;
			const float HR = Ramp.Points[K + 1].X - Ramp.Points[K].X;
			const float W1 = 2.0f * HR + HL;
			const float W2 = HR + 2.0f * HL;
			return (W1 + W2) / (W1 / L + W2 / R);
		};
		const float M0 = Tangent(I), M1 = Tangent(I + 1);
		(void)D;
		const float T2 = T * T, T3 = T2 * T;
		return (2*T3 - 3*T2 + 1)*A.Y + (T3 - 2*T2 + T)*H*M0
			+ (-2*T3 + 3*T2)*B.Y + (T3 - T2)*H*M1;
	}
	case EMixtormatScalarRampInterpolation::BSpline:
	{
		// Natural cubic interpolation: solve knot second derivatives, then evaluate the same
		// cubic segment in the CPU reference and the GPU implementation.
		const FCubicData Data = MakeNaturalSpline(Ramp);
		const float AWeight = 1.0f - T, BWeight = T;
		return FMath::Clamp(AWeight*A.Y + BWeight*B.Y + ((AWeight*AWeight*AWeight-AWeight)*Data.SecondDerivatives[I]
			+ (BWeight*BWeight*BWeight-BWeight)*Data.SecondDerivatives[I + 1]) * H * H / 6.0f,
			FMixtormatScalarRamp::MinOutput, FMixtormatScalarRamp::MaxOutput);
	}
	case EMixtormatScalarRampInterpolation::Linear:
	default:
		return FMath::Lerp(A.Y, B.Y, T);
	}
}

float MixtormatScalarRampMath::EvaluateTangent(const FMixtormatScalarRamp& Ramp, const float X)
{
	constexpr float Epsilon = 1.0e-4f;
	const float A = FMath::Max(0.0f, X - Epsilon), B = FMath::Min(1.0f, X + Epsilon);
	return B > A ? (Evaluate(Ramp, B) - Evaluate(Ramp, A)) / (B - A) : 0.0f;
}

void MixtormatScalarRampMath::SamplePolyline(const FMixtormatScalarRamp& Ramp, int32 Samples, TArray<FVector2f>& OutPoints)
{
	OutPoints.Reset(); Samples = FMath::Max(Samples, 2); OutPoints.Reserve(Samples + Ramp.Points.Num() * 2);
	if (Ramp.Interpolation == EMixtormatScalarRampInterpolation::Constant && Ramp.Points.Num() >= 2)
	{
		for (int32 I = 0; I < Samples; ++I)
		{
			const float X = static_cast<float>(I) / static_cast<float>(Samples - 1);
			OutPoints.Emplace(X, Evaluate(Ramp, X));
		}
		for (int32 I = 1; I < Ramp.Points.Num() - 1; ++I)
		{
			const float X = Ramp.Points[I].X;
			const float Epsilon = FMath::Min(1.0e-5f, FMath::Min(X, 1.0f - X) * 0.25f);
			OutPoints.Emplace(X - Epsilon, Ramp.Points[I - 1].Y);
			OutPoints.Emplace(X + Epsilon, Ramp.Points[I].Y);
		}
		OutPoints.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X; });
		return;
	}
	for (int32 I = 0; I < Samples; ++I)
	{
		const float X = static_cast<float>(I) / static_cast<float>(Samples - 1);
		OutPoints.Emplace(X, Evaluate(Ramp, X));
	}
}

MixtormatScalarRampMath::FBounds MixtormatScalarRampMath::ComputeBounds(const FMixtormatScalarRamp& Ramp, int32 Samples)
{
	TArray<FVector2f> Curve; SamplePolyline(Ramp, Samples, Curve);
	FBounds Bounds; Bounds.MinY = Bounds.MaxY = Curve.IsEmpty() ? 0.0f : Curve[0].Y;
	for (const FVector2f& Point : Curve) { Bounds.MinY = FMath::Min(Bounds.MinY, Point.Y); Bounds.MaxY = FMath::Max(Bounds.MaxY, Point.Y); }
	for (const FMixtormatScalarRampPoint& Point : Ramp.Points)
	{
		Bounds.MinY = FMath::Min(Bounds.MinY, Point.Y);
		Bounds.MaxY = FMath::Max(Bounds.MaxY, Point.Y);
	}
	return Bounds;
}

float MixtormatScalarRampMath::ResistedTravel(const float Travel, const float Range, const bool bStrongBoundary)
{
	constexpr float FullTravelPixels = 240.0f;
	const float Resistance = bStrongBoundary ? 0.06f : 0.025f;
	const float T = FMath::Max(Travel, 0.0f);
	const float Denominator = FMath::Loge(1.0f + Resistance * FullTravelPixels);
	return Range * FMath::Clamp(FMath::Loge(1.0f + Resistance * T) / Denominator, 0.0f, 1.0f);
}

MixtormatScalarRampMath::FGpuPayload MixtormatScalarRampMath::PrepareGpuPayload(const FMixtormatScalarRamp& Source)
{
	FMixtormatScalarRamp Ramp = Source;
	Ramp.Sanitize();
	FGpuPayload Payload;
	Payload.PointCount = FMath::Min(Ramp.Points.Num(), FMixtormatScalarRamp::MaxPoints);
	Payload.Interpolation = static_cast<uint32>(Ramp.Interpolation);
	const FCubicData Natural = MakeNaturalSpline(Ramp);
	TArray<float> Tangents; Tangents.Init(0.0f, Ramp.Points.Num());
	for (int32 I = 0; I < Ramp.Points.Num(); ++I)
	{
		if (I == 0)
		{
			Tangents[I] = (Ramp.Points[1].Y - Ramp.Points[0].Y)
				/ FMath::Max(Ramp.Points[1].X - Ramp.Points[0].X, 1.0e-6f);
		}
		else if (I == Ramp.Points.Num() - 1)
		{
			Tangents[I] = (Ramp.Points[I].Y - Ramp.Points[I - 1].Y)
				/ FMath::Max(Ramp.Points[I].X - Ramp.Points[I - 1].X, 1.0e-6f);
		}
		else
		{
			const float L = (Ramp.Points[I].Y - Ramp.Points[I - 1].Y)
				/ FMath::Max(Ramp.Points[I].X - Ramp.Points[I - 1].X, 1.0e-6f);
			const float R = (Ramp.Points[I + 1].Y - Ramp.Points[I].Y)
				/ FMath::Max(Ramp.Points[I + 1].X - Ramp.Points[I].X, 1.0e-6f);
			if (L * R > 0.0f)
			{
				const float HL = Ramp.Points[I].X - Ramp.Points[I - 1].X;
				const float HR = Ramp.Points[I + 1].X - Ramp.Points[I].X;
				const float W1 = 2.0f * HR + HL;
				const float W2 = HR + 2.0f * HL;
				Tangents[I] = (W1 + W2) / (W1 / L + W2 / R);
			}
		}
	}
	for (int32 I = 0; I < FMixtormatScalarRamp::MaxPoints; ++I)
	{
		const int32 SourceIndex = FMath::Min(I, Ramp.Points.Num() - 1);
		const FMixtormatScalarRampPoint& P = Ramp.Points[SourceIndex];
		Payload.Points[I] = FVector4f(P.X, P.Y, Tangents[SourceIndex], Natural.SecondDerivatives[SourceIndex]);
	}
	return Payload;
}
