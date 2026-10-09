// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatTheme.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;

// A click that carries the modifier keys that were held. The eye on a layer row toggles the layer
// normally and solos it when ctrl or alt is down, and a plain FSimpleDelegate cannot tell the two
// apart -- the modifier state is only on the pointer event.
DECLARE_DELEGATE_OneParam(FOnMixtormatIconClicked, const FPointerEvent& /*MouseEvent*/);

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
		, _Size(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
		, _Role(Mixtormat::EMixtormatIconRole::Count)
		, _bActive(false)
	{}
		SLATE_ATTRIBUTE(const FSlateBrush*, Icon)
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(Mixtormat::EMixtormatIconRole, Role)
		// Active swaps the glyph to the accent, for a toggle that lives as an icon.
		SLATE_ATTRIBUTE(bool, bActive)
		// Draws the scalar ramp button's themed plate behind the glyph: an Accent gradient with
		// its own blend, radius and per-state shades. Off by default -- the bare glyph is the
		// shared atom's contract everywhere else.
		SLATE_ARGUMENT(bool, bPlate)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		// Bound instead of OnClicked when the handler needs the modifier keys. If both are bound
		// this one wins, so a caller never has to unbind the simple form to add modifiers.
		SLATE_EVENT(FOnMixtormatIconClicked, OnClickedWithModifiers)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	FSlateColor GetGlyphColor() const;

	TAttribute<bool> bActive;
	Mixtormat::EMixtormatIconRole Role = Mixtormat::EMixtormatIconRole::Count;
	bool bPlate = false;
	FSimpleDelegate OnClicked;
	FOnMixtormatIconClicked OnClickedWithModifiers;
	bool bPressed = false;
};
