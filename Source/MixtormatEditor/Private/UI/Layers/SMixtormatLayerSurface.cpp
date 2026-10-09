// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerSurface.h"

#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

void SMixtormatLayerSurface::Construct(const FArguments& InArgs)
{
	Kind = InArgs._Kind;
	GroupTint = InArgs._GroupTint;
	bSelected = InArgs._bSelected;
	bHovered = InArgs._bHovered;
	bVisible = InArgs._bVisible;
	bReference = InArgs._bReference;
	bInstanceSource = InArgs._bInstanceSource;
	bSuppressActiveHalo = InArgs._bSuppressActiveHalo;
	ChildSlot[InArgs._Content.Widget];
}

int32 SMixtormatLayerSurface::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	using namespace Mixtormat;
	const FVector2f Size(Geometry.GetLocalSize());
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId,
			WidgetStyle, bParentEnabled);
	}
	const FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
	const FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	FMixtormatLayerRecipeContext Context;
	Context.Kind = Kind;
	Context.bSelected = bSelected.Get(false);
	Context.bHovered = bHovered.Get(false);
	Context.bVisible = bVisible.Get(true);
	Context.bReference = bReference.Get(false);
	Context.bInstanceSource = bInstanceSource.Get(false);
	Context.GroupTint = GroupTint.Get(FLinearColor::Transparent);
	const bool Group = Kind == EMixtormatLayerKind::Group;
	const bool Child = Kind == EMixtormatLayerKind::Child;
	FMixtormatSurfaceDrawStyle DrawStyle;
	DrawStyle.Tint = WidgetStyle.GetColorAndOpacityTint();
	DrawStyle.Effects = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// Body -> group cross -> selected glow -> selected/general hairline -> foreground content.
	const FMixtormatSurfaceRecipe Body = MakeLayerBodyRecipe(Theme, Context);
	const FMixtormatSurfaceRecipe Cross = MakeLayerGroupCrossRecipe(Theme, Context);
	const float Reach = bSuppressActiveHalo.Get(false)
		? 0.0f : FMath::Min(Size.Y, FMath::Max(0.0f, Theme.Layer.ActiveGlow.Reach));
	const FMixtormatSurfaceRecipe Glow = MakeLayerGlowRecipe(Theme, Reach / Size.Y);
	FMixtormatSurfaceSamples BodySamples;
	CompositeSurface(Body, Palette, FMixtormatStateModifier(), BodySamples);
	FMixtormatSurfacePainter::PaintBody(Elements, LayerId, Geometry, Body, BodySamples, DrawStyle);

	// Geometry-only adapter: each horizontal band samples the vertical backdrop. The cross
	// and glow remain separate generic passes; all color resolution and blending stays shared.
	const auto BandGeometry = [&](float Top, float Height)
	{
		return Geometry.ToPaintGeometry(FVector2f(Size.X, Height),
			FSlateLayoutTransform(FVector2f(0.0f, Top)));
	};
	const auto BackdropAt = [&](float Y, FMixtormatSurfaceSamples& Samples)
	{
		if (Group)
		{
			CompositeOverlay(Cross, Palette, BodySamples, Y, Samples);
		}
		else
		{
			Samples = BodySamples;
		}
	};
	const auto GlowAt = [&](float Y)
	{
		FMixtormatSurfaceRecipe Band = Glow;
		for (FMixtormatPaintLayer& Layer : Band.Layers)
		{
			const float Coverage = EvaluateRamp(Layer.OpacityRamp, Y);
			Layer.OpacityRamp = MakeLinearRamp(EMixtormatAxis::Horizontal, Coverage, Coverage, 2);
		}
		return Band;
	};
	int32 PaintLayer = LayerId;
	if (Group)
	{
		++PaintLayer;
		const int32 Bands = FMath::Max(1, FMath::CeilToInt(Size.Y));
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			const float Top = Size.Y * Band / Bands;
			const float Bottom = Size.Y * (Band + 1) / Bands;
			FMixtormatSurfacePainter::PaintOverlay(Elements, PaintLayer,
				BandGeometry(Top, Bottom - Top), Cross, Palette, BodySamples,
				(Top + Bottom) * 0.5f / Size.Y, DrawStyle);
		}
	}
	if (Context.bSelected && Reach > 0.0f)
	{
		++PaintLayer;
		if (!Group && !Child)
		{
			FMixtormatSurfacePainter::PaintOverlay(Elements, PaintLayer, Geometry,
				Glow, Palette, BodySamples, 0.0f, DrawStyle);
		}
		else
		{
			const int32 Bands = FMath::Max(1, FMath::CeilToInt(Reach));
			for (int32 Band = 0; Band < Bands; ++Band)
			{
				const float Top = Reach * Band / Bands;
				const float Bottom = Reach * (Band + 1) / Bands;
				const float Y = (Top + Bottom) * 0.5f / Size.Y;
				FMixtormatSurfaceSamples Backdrop;
				BackdropAt(Y, Backdrop);
				FMixtormatSurfacePainter::PaintOverlay(Elements, PaintLayer,
					BandGeometry(Top, Bottom - Top), GlowAt(Y), Palette, Backdrop, Y, DrawStyle);
			}
		}
	}
	if (Context.bSelected || !Child)
	{
		const float Width = FMath::Clamp(Context.bSelected
			? Theme.Layer.ActiveHairlineWidth : Theme.Layer.HairlineWidth, 0.0f, Size.Y);
		if (Width > 0.0f)
		{
			FMixtormatSurfaceSamples Backdrop;
			BackdropAt(0.0f, Backdrop);
			if (Context.bSelected && Reach > 0.0f)
			{
				FMixtormatSurfaceSamples WithGlow;
				CompositeOverlay(GlowAt(0.0f), Palette, Backdrop, 0.0f, WithGlow);
				Backdrop = WithGlow;
			}
			FMixtormatSurfacePainter::PaintOverlay(Elements, ++PaintLayer,
				BandGeometry(0.0f, Width), MakeLayerHairlineRecipe(Theme, Context.bSelected),
				Palette, Backdrop, 0.0f, DrawStyle);
		}
	}
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, PaintLayer + 1,
		WidgetStyle, bParentEnabled);
}
