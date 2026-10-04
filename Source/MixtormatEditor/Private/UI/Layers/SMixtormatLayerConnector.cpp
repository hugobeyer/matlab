// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerConnector.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"

FVector2D SMixtormatLayerConnector::ComputeDesiredSize(float) const
{
	const Mixtormat::FMixtormatLayerMetrics& Layout = FMixtormatThemeStore::GetResolved().LayerLayout;
		return FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.LayerChildIconSize, Layout.ChildRowHeight);
}

int32 SMixtormatLayerConnector::OnPaint(const FPaintArgs&, const FGeometry& Geometry,
	const FSlateRect&, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2f Size(Geometry.GetLocalSize());
	const Mixtormat::FMixtormatResolvedLayerStyle& LayerStyle = FMixtormatThemeStore::GetResolved().Layers;
	const float Weight = FMath::Max(0.0f, LayerStyle.HierarchyWidth);
		if (Weight <= 0.0f || Size.X <= 0.0f || Size.Y <= 0.0f)
		{
			return LayerId;
		}
		const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
			? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const Mixtormat::FMixtormatLayerMetrics& Layout = FMixtormatThemeStore::GetResolved().LayerLayout;
	const float Reach = Layout.ChildIndent * 0.5f;
	const float X = FMath::Max(0.0f, Size.X - Reach);
	const float Mid = Size.Y * 0.5f;
	const float Gap = Layout.Gap;
	const FLinearColor Color = LayerStyle.HierarchyRail * Style.GetColorAndOpacityTint();
	const auto Stroke = [&](const FVector2f& Start, const FVector2f& End)
	{
		const bool bVertical = FMath::IsNearlyEqual(Start.X, End.X);
		const FVector2f Offset(
			FMath::Min(Start.X, End.X) - (bVertical ? Weight * 0.5f : 0.0f),
			FMath::Min(Start.Y, End.Y) - (bVertical ? 0.0f : Weight * 0.5f));
		const FVector2f Extent(bVertical ? Weight : FMath::Abs(End.X - Start.X),
			bVertical ? FMath::Abs(End.Y - Start.Y) : Weight);
		if (Extent.X <= 0.0f || Extent.Y <= 0.0f) { return; }
		FSlateDrawElement::MakeBox(Elements, LayerId,
			Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Offset)),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), Effect, Color);
	};
	Stroke(FVector2f(X + Weight * 0.5f, -Gap), FVector2f(X + Weight * 0.5f, bLast ? Mid : Size.Y + Gap));
	Stroke(FVector2f(X, Mid + Weight * 0.5f), FVector2f(Size.X - Layout.ItemGap, Mid + Weight * 0.5f));
	return LayerId;
}
