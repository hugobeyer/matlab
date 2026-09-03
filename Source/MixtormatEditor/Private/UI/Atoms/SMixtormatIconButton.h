#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;

// A glyph you can click, with no plate behind it in any state.
//
// The reset control in a group header, the overflow dots on a layer row, the eye. State reads
// through the glyph's own colour: a filled background on an 11px icon reads as a button and
// competes with the title beside it.
class SMixtormatIconButton final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatIconButton)
		: _Icon(nullptr)
		, _Size(11.0f)
		, _bActive(false)
	{}
		SLATE_ARGUMENT(const FSlateBrush*, Icon)
		SLATE_ARGUMENT(float, Size)
		// Active swaps the glyph to the accent, for a toggle that lives as an icon.
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	FSlateColor GetGlyphColor() const;

	TAttribute<bool> bActive;
	FSimpleDelegate OnClicked;
	bool bPressed = false;
};
