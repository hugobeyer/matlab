// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"

// Layout/state adapter for the foldout recipe. Paint arithmetic belongs to SurfacePainter.
// The child retains the chevron, title and actions, above the Ground/lift/accent/hairline stack.
class SMixtormatFoldoutHeader final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatFoldoutHeader) {}
		SLATE_ATTRIBUTE(bool, IsHovered)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TAttribute<bool> Hovered;
	TAttribute<bool> bEnabled;
};