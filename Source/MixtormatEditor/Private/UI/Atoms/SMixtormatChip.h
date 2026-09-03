#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// A value that opens a menu: current text, optional leading thumbnail, disclosure arrow.
//
// One widget for blend-mode enums and asset pickers alike -- the thumbnail is the only thing that
// tells them apart, and it is what lets a row confirm which mask is bound without opening the
// picker. Sits in the same well gradient as a value row so the two read as the same family.
class SMixtormatChip final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatChip)
		: _MinWidth(120.0f)
	{}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ARGUMENT(float, MinWidth)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_NAMED_SLOT(FArguments, LeadingContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
