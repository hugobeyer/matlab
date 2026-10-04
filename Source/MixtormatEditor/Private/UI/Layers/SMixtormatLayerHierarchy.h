// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Paint-only metadata copied at list rebuild; no references into the authored model.
struct FMixtormatLayerHierarchyPaint
{
	float Indent = 0.0f;
	float RowHeight = 0.0f;
	float BranchInset = 0.0f;
	bool bLast = false;
	bool bHasChildren = false;
	TArray<float> AncestorIndents;
};

// Decorates existing padding, without adding a hit target or changing child geometry.
class SMixtormatLayerHierarchy final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerHierarchy) {}
		SLATE_ARGUMENT(FMixtormatLayerHierarchyPaint, Hierarchy)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;

private:
	FMixtormatLayerHierarchyPaint Hierarchy;
	mutable TArray<FVector2D> StrokePoints;
};
