// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerSurface.h"

#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

void SMixtormatLayerSurface::Construct(const FArguments& InArgs)
{
	Kind = InArgs._Kind;
	StartColor = InArgs._StartColor;
	EndColor = InArgs._EndColor;
	CrossColor = InArgs._CrossColor;
	bSelected = InArgs._bSelected;
	bHovered = InArgs._bHovered;
	ChildSlot[InArgs._Content.Widget];
}

int32 SMixtormatLayerSurface::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	using namespace MixtormatTokens;
	using namespace MixtormatCompositing;
	const FVector2f Size(Geometry.GetLocalSize());
	const bool Selected = bSelected.Get(false);
	const bool Hovered = bHovered.Get(false);
	const bool Child = Kind == EKind::Child;
	const bool Group = Kind == EKind::Group;
	const float Saturation = Child
		? (Selected ? ChildSelectedSaturation : Hovered ? ChildHoverSaturation : ChildSaturation)
		: (Selected ? LayerSelectedSaturation : Hovered ? LayerHoverSaturation
			: Group ? LayerGroupSaturation : LayerSaturation);
	const FLinearColor Start = StartColor.Get();
	const FLinearColor End = EndColor.Get();
	const FLinearColor Cross = CrossColor.Get();
	const FLinearColor Ground = MixtormatPalette::Ground();
	const FLinearColor Tint = WidgetStyle.GetColorAndOpacityTint();
	const float Reach = FMath::Min(Size.Y, FMath::Max(0.0f, LayerActiveGlowReach));
	const FLinearColor Glow = Saturate(MixtormatPalette::Accent().CopyWithNewOpacity(
		LayerActiveGlowOpacity), LayerActiveGlowSaturation);

	const auto ColorAt = [&](const float X, const float Y, const bool Hairline)
	{
		FLinearColor Source = MixtormatGradient::LerpSRGB(Start, End, Child ? X : Y / Size.Y);
		if (Group)
		{
			// The horizontal cross is normal-composited inside the group paint layer;
			// that entire layer is soft-light composited over the row's ground.
			const float Coverage = Cross.A * (1.0f - X);
			Source = MixtormatGradient::LerpSRGB(Source, Cross.CopyWithNewOpacity(Source.A), Coverage);
		}
		Source = Saturate(Source, Saturation);
		FLinearColor Result = Group ? SoftLight(Ground, Source)
			: FMath::Lerp(Ground, Source.CopyWithNewOpacity(1.0f), Source.A);
		if (Selected && Reach > 0.0f)
		{
			Result = Additive(Result, Glow, FMath::Max(0.0f, 1.0f - Y / Reach));
		}
		if (Hairline)
		{
			if (Selected)
			{
				Result = Additive(Result, Saturate(MixtormatPalette::Accent().CopyWithNewOpacity(
					LayerActiveHairlineOpacity), LayerActiveGlowSaturation));
			}
			else
			{
				const FLinearColor Line = MixtormatPalette::Hairline().CopyWithNewOpacity(FoldoutHairlineOpacity);
				Result = FMath::Lerp(Result, Line.CopyWithNewOpacity(1.0f), Line.A);
			}
		}
		return Result * Tint;
	};

	if (Size.X > 0.0f && Size.Y > 0.0f)
	{
		if (Group || Child)
		{
			// Two-axis compositing needs a backdrop sample, not a translucent accent wash.
			// One logical-pixel band keeps the cross and glow independent without textures.
			const int32 Bands = FMath::Max(1, FMath::CeilToInt(Size.Y));
			for (int32 Band = 0; Band < Bands; ++Band)
			{
				const float Top = Size.Y * Band / Bands;
				const float Bottom = Size.Y * (Band + 1) / Bands;
				MixtormatGradient::FStop Stops[13];
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(Stops); ++Index)
				{
					const float X = static_cast<float>(Index) / (UE_ARRAY_COUNT(Stops) - 1);
					Stops[Index] = { X, ColorAt(X, (Top + Bottom) * 0.5f, false) };
				}
				const FVector2f BandSize(Size.X, Bottom - Top);
				MixtormatGradient::Paint(Elements, LayerId, Geometry.ToPaintGeometry(BandSize,
					FSlateLayoutTransform(FVector2f(0.0f, Top))), BandSize, Orient_Horizontal,
					MakeArrayView(Stops), FVector4f(0.0f));
			}
		}
		else
		{
			TArray<MixtormatGradient::FStop, TInlineAllocator<16>> Stops;
			// Include the reach endpoint explicitly: the glow must stop, not stretch to the bottom.
			for (int32 Index = 0; Index <= 12; ++Index)
			{
				const float Y = (Selected && Reach > 0.0f ? Reach : Size.Y) * Index / 12.0f;
				Stops.Add({ Y / Size.Y, ColorAt(0.0f, Y, false) });
			}
			if (Selected && Reach > 0.0f && Reach < Size.Y)
			{
				Stops.Add({ 1.0f, ColorAt(0.0f, Size.Y, false) });
			}
			MixtormatGradient::Paint(Elements, LayerId, Geometry.ToPaintGeometry(), Size,
				Orient_Vertical, MakeArrayView(Stops), FVector4f(0.0f));
		}
		if (Selected || !Child)
		{
			const float Thickness = FMath::Clamp(Selected ? LayerActiveHairlineWidth : HairlineThickness, 0.0f, Size.Y);
			MixtormatGradient::FStop LineStops[13];
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(LineStops); ++Index)
			{
				const float X = static_cast<float>(Index) / (UE_ARRAY_COUNT(LineStops) - 1);
				LineStops[Index] = { X, ColorAt(X, 0.0f, true) };
			}
			const FVector2f LineSize(Size.X, Thickness);
			MixtormatGradient::Paint(Elements, LayerId + 1, Geometry.ToPaintGeometry(LineSize,
				FSlateLayoutTransform()), LineSize, Orient_Horizontal, MakeArrayView(LineStops), FVector4f(0.0f));
		}
	}
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId + 2,
		WidgetStyle, bParentEnabled);
}
