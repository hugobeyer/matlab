// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerConnector.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"

FVector2D SMixtormatLayerConnector::ComputeDesiredSize(float) const
{
	return FVector2D(MixtormatTokens::LayerChildIconSize, MixtormatTokens::LayerChildRowHeight);
}

int32 SMixtormatLayerConnector::OnPaint(const FPaintArgs&, const FGeometry& Geometry,
	const FSlateRect&, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& Style, bool) const
{
	const FVector2f Size(Geometry.GetLocalSize());
	const float Weight = MixtormatTokens::LayerHierarchyLineWidth;
	const float Reach = MixtormatTokens::LayerChildIndent * 0.5f;
	const float X = FMath::Max(0.0f, Size.X - Reach);
	const float Mid = Size.Y * 0.5f;
	const float Gap = MixtormatTokens::LayerRowGap;
	const FLinearColor Color = MixtormatPalette::RowText().CopyWithNewOpacity(
		MixtormatTokens::LayerConnectorOpacity) * Style.GetColorAndOpacityTint();
	const auto Stroke = [&](const FVector2f& Start, const FVector2f& End)
	{
		TArray<FVector2D> Points;
		Points.Add(FVector2D(Start));
		Points.Add(FVector2D(End));
		FSlateDrawElement::MakeLines(Elements, LayerId, Geometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, Color, false, Weight);
	};
	Stroke(FVector2f(X + Weight * 0.5f, -Gap), FVector2f(X + Weight * 0.5f, bLast ? Mid : Size.Y + Gap));
	Stroke(FVector2f(X, Mid + Weight * 0.5f), FVector2f(Size.X - MixtormatTokens::LayerItemGap, Mid + Weight * 0.5f));
	return LayerId;
}
