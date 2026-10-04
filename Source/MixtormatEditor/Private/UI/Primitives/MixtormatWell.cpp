// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Primitives/MixtormatWell.h"

#include "Rendering/DrawElements.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Styling/AppStyle.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

namespace MixtormatWell
{
	namespace
	{
		// The border's colour at one edge: the authored border colour, scaled by the overall weight
		// and then by this edge's own endpoint intensity.
		//
		// The two multiplications are the whole point. The prototype masks a solid border with a
		// vertical gradient, so the effective alpha at each edge is overall * endpoint -- and a flat
		// multiplier, which is what the palette used to do, threw the endpoint away entirely.
		FLinearColor BorderAt(const FLinearColor& Base, const float EndpointOpacity)
		{
			FLinearColor Color = Base;
			Color.A *= EndpointOpacity;
			return Color;
		}
	}

	void PaintBackground(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params)
	{
		if (Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		// On hover the prototype adds a translucent ground over itself, which brightens the whole
		// recess rather than changing the ramp's shape. Reproduced by lifting the ground under the
		// same two shade endpoints, so the ratio between them survives.
		const FLinearColor Surface = Params.bHovered
			? MixtormatCompositing::Additive(
				MixtormatPalette::Ground(), MixtormatPalette::Ground(),
				MixtormatTokens::WellHoverLiftOpacity)
			: MixtormatPalette::Ground();

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			Surface);

		if (Params.bFlat)
		{
			return;
		}

		// The recess. Black at alpha over the ground leaves ground * (1 - a), which is the multiply
		// the prototype writes as `mix-blend-mode: multiply` -- so this is the same second-axis pass
		// SMixtormatGradientBox already performs, not a second implementation of it.
		const MixtormatGradient::FStop Shade[] = {
			{ 0.0f, MixtormatPalette::WellShade() },
			{ 1.0f, MixtormatPalette::WellShadeEnd() },
		};
		MixtormatGradient::Paint(
			OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Size, Orient_Vertical, Shade,
			FVector4f(MixtormatTokens::WellRadius));
	}

	void PaintBorder(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params)
	{
		if (Params.bFlat || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return;
		}

		const FLinearColor Base = Params.bHovered
			? MixtormatPalette::WellOutlineHover()
			: MixtormatPalette::WellOutline();
		const float TopOpacity = Params.bHovered
			? MixtormatTokens::WellBorderHoverTopOpacity
			: MixtormatTokens::WellBorderTopOpacity;
		const float BottomOpacity = Params.bHovered
			? MixtormatTokens::WellBorderHoverBottomOpacity
			: MixtormatTokens::WellBorderBottomOpacity;

		const float Width = FMath::Min(
			FMath::Max(MixtormatTokens::WellBorderWidth, 0.0f),
			FMath::Min(Size.X, Size.Y) * 0.5f);
		if (Width <= 0.0f)
		{
			return;
		}

		// Two bars rather than a rounded-box brush. A brush could carry a radius, but it cannot
		// carry two different alphas on two edges, which is the property that matters here; at the
		// authored square radius the bars are exact, and at a non-zero radius the ground beneath
		// keeps the rounding while the straight bars sit on top of it.
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(
				FVector2f(Size.X, Width), FSlateLayoutTransform(FVector2f::ZeroVector)),
			White, ESlateDrawEffect::None, BorderAt(Base, TopOpacity));
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(
				FVector2f(Size.X, Width),
				FSlateLayoutTransform(FVector2f(0.0f, Size.Y - Width))),
			White, ESlateDrawEffect::None, BorderAt(Base, BottomOpacity));
	}
}