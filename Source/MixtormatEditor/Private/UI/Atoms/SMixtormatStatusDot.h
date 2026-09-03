#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// The small circle at the end of a layer child row.
//
// Hollow when the child is inactive, solid accent when it is. Deliberately not a checkbox: it
// reports state at a glance in a dense stack rather than inviting a click, and a checkbox at this
// size would read as an unticked box on every inactive row.
//
// It does take a click when OnClicked is bound -- a child's enable toggle has to live somewhere,
// and the dot is already the thing that shows the state. Without a handler it stays inert, so it
// keeps reading as a readout rather than a control wherever nothing listens.
class SMixtormatStatusDot final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatStatusDot)
		: _Size(MixtormatTokens::StatusDotSize)
		, _bFilled(false)
	{}
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(bool, bFilled)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	const FSlateBrush* GetBrush() const;

	TAttribute<bool> bFilled;
	FSimpleDelegate OnClicked;
	bool bPressed = false;
};
