// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/MixtormatGroupCardPainter.h"

#include "Layout/Clipping.h"
#include "Math/NumericLimits.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

namespace MixtormatGroupCard
{
	namespace
	{
		constexpr int32 CurveSamples = 6;
		// Eight segments per corner keep the tangent error below 0.06px at radius 12.
	constexpr int32 CornerSegments = 8;

		struct FOpacityStop
		{
			float Y;
			float Opacity;
		};

	}

	bool FSurface::operator==(const FSurface& Other) const
	{
		return Size == Other.Size && HeaderTop == Other.HeaderTop && HeaderHeight == Other.HeaderHeight
			&& BodyTop == Other.BodyTop && BodyHeight == Other.BodyHeight
			&& AuthoredHeaderHeight == Other.AuthoredHeaderHeight && Radius == Other.Radius
			&& Reach == Other.Reach && Power == Other.Power && HeaderOpacity == Other.HeaderOpacity
			&& BodyOpacity == Other.BodyOpacity && HeaderSaturation == Other.HeaderSaturation
			&& BodySaturation == Other.BodySaturation && Ground == Other.Ground
			&& Color == Other.Color && Blend == Other.Blend;
	}

	void FSurfacePainter::Rebuild(const FSurface& Surface)
	{
		Positions.Reset();
		Colors.Reset();
		Indices.Reset();

		const float Width = Surface.Size.X;
		const float Height = Surface.Size.Y;
		FLinearColor Ground = Surface.Ground;
		Ground.A = 1.0f;

		const float HeaderEnd = FMath::Clamp(Surface.HeaderTop + Surface.HeaderHeight, 0.0f, Height);
		const float BodyTop = FMath::Clamp(Surface.BodyTop, HeaderEnd, Height);
		const float BodyEnd = FMath::Clamp(BodyTop + Surface.BodyHeight, BodyTop, Height);
		const float Reach = FMath::Max(Surface.Reach, 0.0f);
		const float NominalHeader = FMath::Max(Surface.AuthoredHeaderHeight, 1.0f);
		const float Seam = NominalHeader / (NominalHeader + Reach);
		const auto OpacityAt = [&](const float T)
		{
			return MixtormatGradient::FalloffValue(
				T, Surface.HeaderOpacity, Surface.BodyOpacity, Surface.Power);
		};

		// A single continuous set of vertex-color stops spans the entire card.
		// In particular, no Ground band is painted between the title and the rows.
		struct FStop { float Y; float Opacity; float Saturation; };
		TArray<FStop, TInlineAllocator<32>> Stops;
		const auto AddStop = [&](float Y, const float Opacity, const float Saturation)
		{
			Y = FMath::Clamp(Y, 0.0f, Height);
			if (!Stops.IsEmpty() && Y <= Stops.Last().Y + UE_SMALL_NUMBER)
			{
				// Zero-height spans cannot form an extra hairline or a degenerate quad.
				Stops.Last().Opacity = Opacity;
				Stops.Last().Saturation = Saturation;
				return;
			}
			Stops.Add({Y, Opacity, Saturation});
		};

		AddStop(0.0f, Surface.HeaderOpacity, Surface.HeaderSaturation);
		for (int32 I = 1; I < CurveSamples; ++I)
		{
			const float T = static_cast<float>(I) / (CurveSamples - 1);
			AddStop(HeaderEnd * T, OpacityAt(T * Seam),
				FMath::Lerp(Surface.HeaderSaturation, Surface.BodySaturation, T));
		}
		// Extend the endpoint across any authored header margin with no color jump.
		AddStop(BodyTop, OpacityAt(Seam), Surface.BodySaturation);

		const float TailHeight = FMath::Min(Reach, FMath::Max(BodyEnd - BodyTop, 0.0f) * 0.5f);
		if (TailHeight > UE_SMALL_NUMBER)
		{
			for (int32 I = 1; I < CurveSamples; ++I)
			{
				const float T = static_cast<float>(I) / (CurveSamples - 1);
				AddStop(BodyTop + T * TailHeight,
					OpacityAt(Seam + T * (1.0f - Seam)), Surface.BodySaturation);
			}
			AddStop(BodyEnd - TailHeight, Surface.BodyOpacity, Surface.BodySaturation);
			for (int32 I = 1; I < CurveSamples; ++I)
			{
				const float T = static_cast<float>(I) / (CurveSamples - 1);
				AddStop(BodyEnd - TailHeight + T * TailHeight,
					OpacityAt(1.0f - T * (1.0f - Seam)), Surface.BodySaturation);
			}
		}
		else
		{
			AddStop(BodyEnd, Surface.BodyOpacity, Surface.BodySaturation);
		}
		AddStop(Height, Stops.Last().Opacity, Surface.BodySaturation);

		const int32 RowCount = Stops.Num();
		check(static_cast<uint64>(RowCount * 2 - 1)
			<= static_cast<uint64>(TNumericLimits<SlateIndex>::Max()));
		for (const FStop& Stop : Stops)
		{
			FLinearColor Source = MixtormatCompositing::Saturate(
				Surface.Color, FMath::Max(Stop.Saturation, 0.0f));
			Source.A = FMath::Clamp(Surface.Color.A * Stop.Opacity, 0.0f, 1.0f);
			const FLinearColor Result = MixtormatCompositing::ApplyBlend(
				Surface.Blend, Ground, Source);
			Positions.Add(FVector2f(0.0f, Stop.Y));
			Positions.Add(FVector2f(Width, Stop.Y));
			Colors.Add(Result);
			Colors.Add(Result);
		}
		for (int32 Row = 0; Row + 1 < RowCount; ++Row)
		{
			const int32 A = Row * 2;
			Indices.Add(static_cast<SlateIndex>(A));
			Indices.Add(static_cast<SlateIndex>(A + 1));
			Indices.Add(static_cast<SlateIndex>(A + 2));
			Indices.Add(static_cast<SlateIndex>(A + 1));
			Indices.Add(static_cast<SlateIndex>(A + 3));
			Indices.Add(static_cast<SlateIndex>(A + 2));
		}
		Vertices.SetNumUninitialized(Positions.Num());
		CachedSurface = Surface;
		bHasSurface = true;
	}

	void FSurfacePainter::Paint(FSlateWindowElementList& Elements, const int32 LayerId,
		const FGeometry& Geometry, const FSurface& Surface, const FLinearColor& Tint,
		const ESlateDrawEffect Effect)
	{
		if (Surface.Size.X <= 0.0f || Surface.Size.Y <= 0.0f)
		{
			return;
		}
		if (!bHasSurface || !(CachedSurface == Surface))
		{
			Rebuild(Surface);
		}
		const FSlateRenderTransform Transform = Geometry.GetAccumulatedRenderTransform();
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Vertices[Index] = FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,
				Positions[Index], FVector2f::ZeroVector, (Colors[Index] * Tint).ToFColor(true));
		}
		// An empty handle uses Slate's untextured vertex-colour path; no resource is owned here.
		FSlateDrawElement::MakeCustomVerts(Elements, LayerId, FSlateResourceHandle(), Vertices,
			Indices, nullptr, 0, 0, Effect);
	}

	int32 PushRoundedClip(FSlateWindowElementList& Elements, const FGeometry& Geometry, const float Radius)
	{
		const FVector2f Size(Geometry.GetLocalSize());
		Elements.PushClip(FSlateClippingZone(Geometry));
		const float R = FMath::Clamp(Radius, 0.0f, FMath::Min(Size.X, Size.Y) * 0.5f);
		if (R <= 0.0f)
		{
			return 1;
		}
		const FVector2f Center = Size * 0.5f;
		const float Extent = Size.X + Size.Y + 2.0f * R;
		int32 Count = 1;
		for (int32 Index = 1; Index < CornerSegments * 2; ++Index)
		{
			if (Index == CornerSegments)
			{
				continue; // The axis-aligned rectangle already supplies this pair of planes.
			}
			const float Angle = PI * static_cast<float>(Index) / (CornerSegments * 2);
			const FVector2f Normal(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2f Tangent(Normal.Y, -Normal.X);
			const float Support = (Center.X - R) * FMath::Abs(Normal.X)
				+ (Center.Y - R) * FMath::Abs(Normal.Y) + R;
			const FVector2f Along = Tangent * Extent;
			const FVector2f Across = Normal * Support;
			Elements.PushClip(FSlateClippingZone(
				FVector2f(Geometry.LocalToAbsolute(Center - Along - Across)),
				FVector2f(Geometry.LocalToAbsolute(Center + Along - Across)),
				FVector2f(Geometry.LocalToAbsolute(Center - Along + Across)),
				FVector2f(Geometry.LocalToAbsolute(Center + Along + Across))));
			++Count;
		}
		return Count;
	}
}
