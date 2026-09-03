#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// The fixed-width mark that says how a row composites.
//
// A layer's BLEND / OVER / COAT / DETAIL, a mask's blend mode abbreviated, an effect's type. It is
// deliberately a fixed width rather than hugging its text: the badges then form a scannable column
// down the right edge of the stack, and the word can change without the column moving.
//
// One widget for the layer stack, the child rows and the group headers, so the three can never
// drift apart.
class SMixtormatBadge final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBadge) {}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, ToolTip)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
