// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatGroupButtonTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatTheme.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

namespace MixtormatGroupButton
{
	enum class EState : uint8 { Rest, Hover, Active, Selected, Disabled, DisabledSelected };

	EState ResolveState(bool bEnabled, bool bHovered, bool bPressed, bool bSelected);
	FLinearColor TextColor(EState State);
	// The six widget states collapse onto the recipe's three; Disabled and DisabledSelected are the
		// same numbers at a lower opacity rather than separate surfaces.
		Mixtormat::EMixtormatButtonState ToButtonState(EState State);
	// Resource-free adapters: the container owns all plate/hairline/separator paint.
	// Copy the existing style to preserve sounds and interaction semantics.
	FButtonStyle MakeButtonStyle(const FButtonStyle& Existing);
	FCheckBoxStyle MakeCheckBoxStyle(const FCheckBoxStyle& Existing);
}

// Paint only. No focus, input, timers or extra layout slots. The caller supplies the
// actual backdrop for explicit additive compositing (Ground by default).
class SMixtormatGroupButtonSurface final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatGroupButtonSurface)
		: _Hovered(false), _Pressed(false), _Selected(false), _ShowSeparator(false)
		, _Padding(0.0f)
	{}
		SLATE_ATTRIBUTE(bool, Hovered)
		SLATE_ATTRIBUTE(bool, Pressed)
		SLATE_ATTRIBUTE(bool, Selected)
		SLATE_ATTRIBUTE(FLinearColor, Ground)
		SLATE_ARGUMENT(bool, ShowSeparator)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TAttribute<bool> Hovered, Pressed, Selected;

	// Retained so existing callers keep compiling. The recipe names the Ground role itself, so the
	// surface no longer needs a backdrop handed to it -- which is why nothing reads this any more.
	TAttribute<FLinearColor> Ground;
	bool bShowSeparator = false;
};
