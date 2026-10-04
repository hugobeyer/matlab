// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "UI/Atoms/SMixtormatIconButton.h"
#include "Brushes/SlateRoundedBoxBrush.h"

// Layer-only glyph paint; the legacy icon button's hit box and click contract are retained.
class SMixtormatLayerIcon final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerIcon)
		: _Size(0.0f), _bVisibility(false), _bOn(true), _bActive(false)
	{}
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(bool, bVisibility)
		SLATE_ATTRIBUTE(bool, bOn)
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_ATTRIBUTE(const FSlateBrush*, Icon)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FOnMixtormatIconClicked, OnClickedWithModifiers)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override;
	virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override;

private:
	bool bVisibility = false;
	bool bPressed = false;
	TAttribute<bool> bOn, bActive;
	TAttribute<const FSlateBrush*> Icon;
	FSimpleDelegate OnClicked;
	FOnMixtormatIconClicked OnClickedWithModifiers;
	FSlateRoundedBoxBrush Filled{FLinearColor::White, 0.0f};
	FSlateRoundedBoxBrush Hollow{FLinearColor::Transparent, 0.0f, FLinearColor::White, 1.0f};
};
