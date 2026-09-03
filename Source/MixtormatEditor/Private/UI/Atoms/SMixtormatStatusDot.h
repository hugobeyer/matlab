#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// The small circle at the end of a layer child row.
//
// Hollow when the child is inactive, solid accent when it is. Deliberately not a checkbox: it
// reports state at a glance in a dense stack rather than inviting a click, and a checkbox at this
// size would read as an unticked box on every inactive row.
class SMixtormatStatusDot final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatStatusDot)
		: _Size(9.0f)
		, _bFilled(false)
	{}
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(bool, bFilled)
		SLATE_ATTRIBUTE(FText, ToolTip)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const FSlateBrush* GetBrush() const;

	TAttribute<bool> bFilled;
};
