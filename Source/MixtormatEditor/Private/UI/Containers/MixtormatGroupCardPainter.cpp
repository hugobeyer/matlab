// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/MixtormatGroupCardPainter.h"

#include "Layout/Clipping.h"
#include "Math/NumericLimits.h"
#include "Style/MixtormatCompositing.h"
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
			&& BodySaturation == Other.BodySaturation && Ground == Other.Ground;
	}

	void FSurfacePainter::Rebuild(const FSurface& Surface)
	{
		Positions.Reset();
		Colors.Reset();
		Indices.Reset();

		const float Width = Surface.Size.X;
		const float Height = Surface.Size.Y;
		FLinearColor Ground = Surface.Ground;
		// These CSS backgrounds are opaque RGB, even when a palette override has an alpha.
		Ground.A = 1.0f;
		const float HeaderTop = FMath::Clamp(Surface.HeaderTop, 0.0f, Height);
		const float HeaderEnd = FMath::Clamp(HeaderTop + Surface.HeaderHeight, HeaderTop, Height);
		const float BodyTop = FMath::Clamp(Surface.BodyTop, HeaderEnd, Height);
		const float BodyEnd = FMath::Clamp(BodyTop + Surface.BodyHeight, BodyTop, Height);
		const float Reach = FMath::Max(Surface.Reach, 0.0f);
		// falloff.js uses the authored minimum height for the curve domain, not the physical
		// height enlarged by actions/padding. The actual arranged header still defines its seam.
		const float NominalHeight = FMath::Max(Surface.AuthoredHeaderHeight, 1.0f);
		const float Seam = NominalHeight / (NominalHeight + Reach);
		const auto OpacityAt = [&](const float T)
		{
			return MixtormatGradient::FalloffValue(T, Surface.HeaderOpacity, Surface.BodyOpacity, Surface.Power);
		};

		TArray<FOpacityStop, TInlineAllocator<16>> HeaderStops;
		for (int32 Index = 0; Index < CurveSamples; ++Index)
		{
			const float T = static_cast<float>(Index) / (CurveSamples - 1);
			HeaderStops.Add({ FMath::Lerp(HeaderTop, HeaderEnd, T), OpacityAt(T * Seam) });
		}
		TArray<FOpacityStop, TInlineAllocator<16>> BodyStops;
		if (Reach > 0.0f)
		{
			// min(t * reach, t * 50%) caps each tail independently of the curve domain.
			const float TailHeight = FMath::Min(Reach, (BodyEnd - BodyTop) * 0.5f);
			for (int32 Index = 0; Index < CurveSamples; ++Index)
			{
				const float T = static_cast<float>(Index) / (CurveSamples - 1);
				BodyStops.Add({ BodyTop + T * TailHeight, OpacityAt(Seam + T * (1.0f - Seam)) });
			}
			for (int32 Index = CurveSamples - 1; Index >= 0; --Index)
			{
				const float T = static_cast<float>(Index) / (CurveSamples - 1);
				const float Y = BodyEnd - T * TailHeight;
				if (Y > BodyStops[BodyStops.Num() - 1].Y)
				{
					BodyStops.Add({ Y, OpacityAt(Seam + T * (1.0f - Seam)) });
				}
			}
		}
		else
		{
			// Zero reach is a flat body, NOT a zero-length mirrored tail/hard stop.
			BodyStops.Add({ BodyTop, Surface.BodyOpacity });
			BodyStops.Add({ BodyEnd, Surface.BodyOpacity });
		}

		const auto AddBand = [&](const float Top, const float Bottom,
			const TArrayView<const FOpacityStop> Stops, const float Saturation)
		{
			if (Bottom <= Top)
			{
				return;
			}
			check(Stops.Num() == 0 || Stops.Num() >= 2);
			// Vertical vertex colours interpolate linearly between the six authored samples.
			// Two vertices per stop suffice; plain ground margins need only one quad.
			const int32 RowCount = Stops.Num() > 0 ? Stops.Num() : 2;
			const int32 FirstVertex = Positions.Num();
			// All five bands total at most 48 vertices, independent of the card's dimensions.
			check(static_cast<uint64>(FirstVertex + RowCount * 2 - 1)
				<= static_cast<uint64>(TNumericLimits<SlateIndex>::Max()));
			const FLinearColor Source = MixtormatCompositing::Saturate(Ground, FMath::Max(Saturation, 0.0f));
			for (int32 Row = 0; Row < RowCount; ++Row)
			{
				const float Y = Stops.Num() > 0 ? Stops[Row].Y : (Row == 0 ? Top : Bottom);
				FLinearColor Color = Ground;
				if (Stops.Num() > 0)
				{
					FLinearColor Lift = Source;
					Lift.A = FMath::Clamp(Stops[Row].Opacity, 0.0f, 1.0f);
					Color = MixtormatCompositing::Additive(Ground, Lift);
				}
				Positions.Add(FVector2f(0.0f, Y));
				Positions.Add(FVector2f(Width, Y));
				Colors.Add(Color);
				Colors.Add(Color);
			}
			for (int32 Row = 0; Row < RowCount - 1; ++Row)
			{
				const int32 A = FirstVertex + Row * 2;
				const int32 B = A + 1;
				const int32 C = A + 2;
				const int32 D = A + 3;
				Indices.Add(static_cast<SlateIndex>(A));
				Indices.Add(static_cast<SlateIndex>(B));
				Indices.Add(static_cast<SlateIndex>(C));
				Indices.Add(static_cast<SlateIndex>(B));
				Indices.Add(static_cast<SlateIndex>(D));
				Indices.Add(static_cast<SlateIndex>(C));
			}
		};

		AddBand(0.0f, HeaderTop, {}, 1.0f);
		AddBand(HeaderTop, HeaderEnd, MakeArrayView(HeaderStops), Surface.HeaderSaturation);
		AddBand(HeaderEnd, BodyTop, {}, 1.0f);
		AddBand(BodyTop, BodyEnd, MakeArrayView(BodyStops), Surface.BodySaturation);
		AddBand(BodyEnd, Height, {}, 1.0f);
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
