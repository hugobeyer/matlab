// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"

// The collapsed gallery drawer's restore tab: a small centred handle sitting above the status bar,
// not a full-width strip. Paints the foldout header anatomy with only its top corners rounded
// (MakeGalleryTabRecipe), so it reads as the drawer's handle pointing up. Content is the
// disclosure chevron beside the gallery icon; a left click reopens the drawer.
//
// It handles its own mouse input rather than wrapping an SButton: a button paints its own rounded
// hover plate over this surface, which would round all four corners and contradict the recipe's
// deliberately square bottom edge.
class SMixtormatGalleryTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGalleryTab) {}
		SLATE_EVENT(FSimpleDelegate, OnActivated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	FSimpleDelegate OnActivated;
	bool bHovered = false;
};
