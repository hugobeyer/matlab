// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerHierarchy.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"

void SMixtormatLayerHierarchy::Construct(const FArguments& InArgs)
{
	Hierarchy = InArgs._Hierarchy;
	StrokePoints.SetNumUninitialized(2);
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot[InArgs._Content.Widget];
}

int32 SMixtormatLayerHierarchy::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 ChildLayer = SCompoundWidget::OnPaint(Args, Geometry, CullingRect,
		Elements, LayerId, Style, bParentEnabled);
	const FVector2f Size(Geometry.GetLocalSize());
	const Mixtormat::FMixtormatResolvedLayerStyle& LayerStyle = FMixtormatThemeStore::GetResolved().Layers;
	const float Weight = FMath::Max(0.0f, LayerStyle.HierarchyWidth);
	if (Weight <= 0.0f || Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return ChildLayer;
	}
	const Mixtormat::FMixtormatLayerMetrics& Layout = FMixtormatThemeStore::GetResolved().LayerLayout;
	const Mixtormat::FMixtormatHierarchyTheme& HierarchyStyle = FMixtormatThemeStore::GetResolved().LayerHierarchy;
	const float Gap = Layout.Gap;
	const float HalfIndent = HierarchyStyle.Indent * 0.5f;
	const float Mid = FMath::Min(Hierarchy.RowHeight, Size.Y) * 0.5f;
	const FLinearColor Color = LayerStyle.HierarchyRail * Style.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const auto Stroke = [&](float X1, float Y1, float X2, float Y2)
	{
		const bool bVertical = FMath::IsNearlyEqual(X1, X2);
		const FVector2f Offset(
			FMath::Min(X1, X2) - (bVertical ? Weight * 0.5f : 0.0f),
			FMath::Min(Y1, Y2) - (bVertical ? 0.0f : Weight * 0.5f));
		const FVector2f Extent(bVertical ? Weight : FMath::Abs(X2 - X1),
			bVertical ? FMath::Abs(Y2 - Y1) : Weight);
		if (Extent.X <= 0.0f || Extent.Y <= 0.0f) { return; }
		FSlateDrawElement::MakeBox(Elements, ChildLayer + 1,
			Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Offset)),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), Effect, Color);
	};
	if (Hierarchy.Indent > 0.0f)
	{
		const float X = Hierarchy.BranchInset + Hierarchy.Indent - HalfIndent + Weight * 0.5f;
		Stroke(X, -Gap, X, Hierarchy.bLast ? Mid : Size.Y + Gap);
		Stroke(X, Mid + Weight * 0.5f,
			X + HierarchyStyle.ChildArmLength, Mid + Weight * 0.5f);
		LayerId += 1;
	}
	if (Hierarchy.bHasChildren)
		{
			// Start below the parent's glyph; meet the first scoped child's stem across the row gap.
			const float X = Hierarchy.BranchInset + Hierarchy.Indent + HalfIndent + Weight * 0.5f;
			const Mixtormat::FMixtormatIconStyle& ChildIcon = FMixtormatThemeStore::GetResolved().Icons.Roles[
				static_cast<uint8>(Mixtormat::EMixtormatIconRole::LayerDisclosure)];
			const float Start = Mid + ChildIcon.GlyphSize * 0.5f + HierarchyStyle.ParentJoinOffset;
		const float End = Geometry.GetLocalSize().Y - 1.0f;
			Stroke(X, FMath::Min(Start, Size.Y), X, Size.Y + Gap);
		}
		for (const float Indent : Hierarchy.AncestorIndents)
	{
		const float X = Hierarchy.BranchInset + Indent - HalfIndent + Weight * 0.5f;
		Stroke(X, -Gap, X, Size.Y + Gap);
	}
	return ChildLayer + 1;
}
