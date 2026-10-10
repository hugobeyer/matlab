// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A vertical strip of page icons: the left column's navigation.
//
// The horizontal tab strip it replaces took a full row of the column's height and had to travel
// with the panel; a rail costs a narrow column and stays put while the layer stack floats. Icons
// rather than labels because a rail has no room for words -- the tooltip carries the name.
class SMixtormatIconRail final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatIconRail)
		: _ActiveIndex(0)
	{}
		SLATE_ARGUMENT(TArray<const FSlateBrush*>, Options)
		SLATE_ARGUMENT(TArray<FText>, Labels)
		SLATE_ARGUMENT(TArray<FText>, ToolTips)
		SLATE_ATTRIBUTE(int32, ActiveIndex)
		SLATE_EVENT(FMixtormatOnSegmentChosen, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& WidgetStyle,
		bool bParentEnabled) const override;

private:
	TAttribute<int32> ActiveIndex;
};
