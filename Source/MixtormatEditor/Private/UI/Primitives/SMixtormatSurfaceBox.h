// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatTheme.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Paints a surface recipe behind its content.
//
// This is the widget the declarative half of the design system is built from. SMixtormatGradientBox
// was its predecessor and took two colours and an orientation; a recipe takes a role, a blend, a
// ramp and a border, which is the difference between a control that can render Multiply and one
// that cannot.
//
// The recipe arrives as an attribute rather than as arguments, because a recipe is data that depends
// on state: a toggle rebuilds its fill when it is hovered or disabled, and rebuilding it at the call
// site would mean either rebuilding the widget tree on every state change or passing stale numbers.
// Copying the recipe per paint is cheap -- Layers and Borders carry inline allocators sized for the
// recipes the design actually authors -- and it is why this widget takes no cached brush at all.
//
// It owns no appearance. It composes what it is given and hands it to MixtormatSurfacePainter.
class SMixtormatSurfaceBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSurfaceBox)
		: _PaintBorders(true)
		, _Padding(FMargin(0.0f))
	{}
		// The surface to paint. Bound to a function when the recipe depends on widget state.
		//
		// Qualified with its namespace: this is a global-scope widget, and FMixtormatSurfaceRecipe
		// lives in namespace Mixtormat.
		SLATE_ATTRIBUTE(Mixtormat::FMixtormatSurfaceRecipe, Recipe)

		// Off for a surface whose borders belong to something else -- a fill painted inside a well,
		// where the well's rim must stay continuous across it.
		SLATE_ARGUMENT(bool, PaintBorders)

		// Inset for the content, so a fill can sit inside its well without a separate widget.
		SLATE_ARGUMENT(FMargin, Padding)

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

private:
	TAttribute<Mixtormat::FMixtormatSurfaceRecipe> Recipe;
	bool bPaintBorders = true;
};