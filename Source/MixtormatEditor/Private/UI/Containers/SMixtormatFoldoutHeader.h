// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"

// The foldout header's paint stack, as its own hand-painted widget.
//
// The prototype builds a foldout header from four layers that each composite differently against
// the same ground (components.css:163-178):
//
//   ground        the surface colour
//   lift          an additive tint ramp, reaching zero contribution at the body seam
//   accent cross  a saturated accent over the lift, fading to zero at the same seam
//   hairline      a one-pixel line at the top edge, saturated independently
//
// Slate has one blend mode -- normal -- so the two compositing layers are resolved to final
// colours here and painted flat, while the two alpha ramps are painted as gradients. That is the
// same trick `MixtormatPalette::GroupCardBackground` already uses for the card's additive lift.
//
// This is a widget rather than a style entry because none of it is expressible as a brush: two of
// the four layers are gradients whose *opacity* follows a falloff curve, and the hairline has to be
// able to sit above the others without being their border.
//
// A compound widget, not a leaf: the header's own content (chevron, title, actions) is its child,
// and a leaf widget has no slot to put it in.
class SMixtormatFoldoutHeader final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatFoldoutHeader) {}
		SLATE_ATTRIBUTE(bool, IsHovered)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_DEFAULT_SLOT(FArguments, Content)
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

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TAttribute<bool> Hovered;
	TAttribute<bool> bEnabled;
};