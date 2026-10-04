// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FMixtormatOnSegmentChosen, int32);

// A row of exclusive choices, drawn as the words themselves.
//
// Built for the layer's COMBINE / OVERRIDE / COAT / DETAIL normal choice.
// Parameterised on cells and an active index rather than on any particular enum, so it also serves
// mask blend modes and anything else discrete.
//
// No borders: the strip is one well, divided by a hairline between cells only, never around them.
class SMixtormatSegmentedControl final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSegmentedControl)
		: _ActiveIndex(0)
	{}
		SLATE_ARGUMENT(TArray<FText>, Options)
		SLATE_ARGUMENT(TArray<FText>, ToolTips)
		SLATE_ATTRIBUTE(int32, ActiveIndex)
		SLATE_EVENT(FMixtormatOnSegmentChosen, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<int32> ActiveIndex;
};
