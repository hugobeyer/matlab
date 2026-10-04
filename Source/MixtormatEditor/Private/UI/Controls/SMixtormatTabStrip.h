// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A row of page tabs, with the selected one underlined and the rule carrying on past the last tab
// to close the panel below it.
//
// Written because the left panel had two tabs hand-assembled from a box, a border, a checkbox, a
// label and an underline apiece -- ninety lines to say "LAYERS or LIBRARY", duplicated once per
// tab, with the selected-state brush swap spelled out four separate times. Adding a third tab
// meant copying it again, and the copies had already drifted in their padding.
//
// Not SMixtormatSegmentedControl, though it takes the same arguments on purpose. That control is
// one well divided by hairlines, for a choice that lives inside a panel -- COMBINE / OVERRIDE / COAT.
// A tab is the edge of the panel it opens: it needs the underline that joins the selected tab to
// the surface below and separates the rest from it, which is the whole affordance and the one
// thing a segmented control deliberately does not draw. Same delegate type, since the question
// asked -- "which of these is active" -- really is the same.
class SMixtormatTabStrip final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatTabStrip)
		: _UseGroupButtonVisuals(true)
		, _StretchTabs(false)
		, _ActiveIndex(0)
	{}
		SLATE_ARGUMENT(bool, UseGroupButtonVisuals)
		// Shell tabs share the available column width; compact callers keep their fixed width.
		SLATE_ARGUMENT(bool, StretchTabs)
		SLATE_ARGUMENT(TArray<FText>, Options)
		SLATE_ARGUMENT(TArray<FText>, ToolTips)
		SLATE_ATTRIBUTE(int32, ActiveIndex)
		SLATE_EVENT(FMixtormatOnSegmentChosen, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<int32> ActiveIndex;
	// Checkbox children hold this address; never give them a temporary/local adapter.
	FCheckBoxStyle GroupButtonCheckBoxStyle;
};
