#include "UI/Primitives/MixtormatGradientPainter.h"

#include "Rendering/DrawElements.h"
#include "Style/MixtormatDesignTokens.h"

namespace MixtormatGradient
{
	namespace
	{
		EOrientation SlateAxis(const EOrientation CssOrientation)
		{
			return CssOrientation == Orient_Vertical ? Orient_Horizontal : Orient_Vertical;
		}

		FVector2f AxisPoint(const bool bVertical, const FVector2f& Size, const float T)
		{
			return bVertical ? FVector2f(0.0f, Size.Y * T) : FVector2f(Size.X * T, 0.0f);
		}
	}

	FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& B, const float T)
	{
		// sRGB *and* premultiplied, because that is what a CSS gradient does. Interpolating the
		// colour straight and the alpha separately is the obvious implementation and it is wrong
		// the moment one stop is translucent: a 14% blue running to opaque #212121 keeps almost
		// full-strength blue through the middle instead of dying in the first few pixels, so the
		// header's lip became a blue wash down the whole bar.
		//
		// Weighting each colour by its own alpha before the mix is what makes a nearly-transparent
		// stop contribute nearly nothing.
		const FColor SrgbA = A.ToFColor(true);
		const FColor SrgbB = B.ToFColor(true);
		const float Alpha = FMath::Lerp(A.A, B.A, T);

		const auto Mix = [&](const uint8 X, const uint8 Y)
		{
			const float Premultiplied = FMath::Lerp(
				static_cast<float>(X) * A.A,
				static_cast<float>(Y) * B.A,
				T);
			// Back out of premultiplied space. A fully transparent result has no colour to carry.
			const float Straight = Alpha > UE_SMALL_NUMBER ? Premultiplied / Alpha : 0.0f;
			return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Straight), 0, 255));
		};

		FLinearColor Result = FLinearColor::FromSRGBColor(FColor(
			Mix(SrgbA.R, SrgbB.R), Mix(SrgbA.G, SrgbB.G), Mix(SrgbA.B, SrgbB.B)));
		Result.A = Alpha;
		return Result;
	}

	void Paint(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2f& Size,
		const EOrientation CssOrientation,
		const TArrayView<const FStop> Stops,
		const FVector4f& CornerRadii)
	{
		if (Stops.Num() < 2)
		{
			return;
		}

		const bool bVertical = CssOrientation == Orient_Vertical;
		const int32 Samples = MixtormatTokens::GradientSamplesPerSpan;

		TArray<FSlateGradientStop> Sampled;
		Sampled.Reserve((Stops.Num() - 1) * Samples + 1);
		for (int32 SpanIndex = 0; SpanIndex < Stops.Num() - 1; ++SpanIndex)
		{
			const FStop& From = Stops[SpanIndex];
			const FStop& To = Stops[SpanIndex + 1];
			// The first sample of every span after the first repeats the previous span's last, so
			// it is skipped: a duplicated stop is a zero-length span the batcher has to sort.
			for (int32 Index = SpanIndex == 0 ? 0 : 1; Index <= Samples; ++Index)
			{
				const float Local = static_cast<float>(Index) / static_cast<float>(Samples);
				Sampled.Emplace(
					AxisPoint(bVertical, Size, FMath::Lerp(From.Position, To.Position, Local)),
					LerpSRGB(From.Color, To.Color, Local));
			}
		}

		FSlateDrawElement::MakeGradient(
			OutDrawElements,
			LayerId,
			Geometry,
			Sampled,
			SlateAxis(CssOrientation),
			ESlateDrawEffect::None,
			CornerRadii);
	}
}
