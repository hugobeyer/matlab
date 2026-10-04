// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatGroupButton.h"

#include "Brushes/SlateNoResource.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

namespace MixtormatGroupButton
{
	EState ResolveState(const bool bEnabled, const bool bHovered, const bool bPressed, const bool bSelected)
	{
		if (!bEnabled) { return bSelected ? EState::DisabledSelected : EState::Disabled; }
		if (bSelected) { return EState::Selected; }
		if (bPressed) { return EState::Active; }
		return bHovered ? EState::Hover : EState::Rest;
	}

	// The six widget states collapse onto the recipe's three plus a dim. The recipe has one surface
	// per appearance, not one per interaction: Disabled and DisabledSelected are the same numbers at
	// a lower opacity, which is what the old painter expressed by multiplying every layer by a Dim
	// factor, and which is now a single number on the state modifier.
	Mixtormat::EMixtormatButtonState ToButtonState(const EState State)
	{
		switch (State)
		{
		case EState::Hover: return Mixtormat::EMixtormatButtonState::Hover;
		case EState::Selected:
		case EState::Active:
		case EState::DisabledSelected: return Mixtormat::EMixtormatButtonState::Selected;
		case EState::Rest:
		case EState::Disabled:
		default: return Mixtormat::EMixtormatButtonState::Rest;
		}
	}

	FLinearColor TextColor(const EState State)
	{
		const Mixtormat::FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
		const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;

		FLinearColor Color = Mixtormat::MakeButtonTextColor(Palette, Theme, ToButtonState(State));
		if (State == EState::Disabled || State == EState::DisabledSelected)
		{
			Color.A *= Theme.ControlLayout.DisabledLabelOpacity;
		}
		return Color;
	}

	// Resource-free adapters: the container owns all plate/hairline/separator paint.
	// Copy the existing style to preserve sounds and interaction semantics.
	FButtonStyle MakeButtonStyle(const FButtonStyle& Existing)
	{
		FButtonStyle Result = Existing;
		Result.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource())
			.SetNormalForeground(TextColor(EState::Rest))
			.SetHoveredForeground(TextColor(EState::Hover))
			.SetPressedForeground(TextColor(EState::Active))
			.SetDisabledForeground(TextColor(EState::Disabled))
			.SetNormalPadding(FMargin(0.0f)).SetPressedPadding(FMargin(0.0f));
		return Result;
	}

	FCheckBoxStyle MakeCheckBoxStyle(const FCheckBoxStyle& Existing)
	{
		FCheckBoxStyle Result = Existing;
		Result.SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource())
			.SetUncheckedPressedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
			.SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
			.SetUndeterminedImage(FSlateNoResource()).SetUndeterminedHoveredImage(FSlateNoResource())
			.SetUndeterminedPressedImage(FSlateNoResource()).SetBackgroundImage(FSlateNoResource())
			.SetBackgroundHoveredImage(FSlateNoResource()).SetBackgroundPressedImage(FSlateNoResource());
		return Result;
	}
}

void SMixtormatGroupButtonSurface::Construct(const FArguments& InArgs)
{
	Hovered = InArgs._Hovered;
	Pressed = InArgs._Pressed;
	Selected = InArgs._Selected;
	Ground = InArgs._Ground;
	bShowSeparator = InArgs._ShowSeparator;

	// No gradient-stop buffer any more. The old painter kept a 13-entry TArray alive across paints
	// and rewrote it every time because it was doing the compositing itself; the recipe and painter
	// do that now, so the widget holds no paint state at all.
	ChildSlot.Padding(InArgs._Padding)[InArgs._Content.Widget];
}

int32 SMixtormatGroupButtonSurface::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	using namespace MixtormatGroupButton;

	const Mixtormat::FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
	const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;

	const EState State = ResolveState(ShouldBeEnabled(bParentEnabled), Hovered.Get(false),
		Pressed.Get(false), Selected.Get(false));

	// Disabled is not a seventh surface: it is the same plate at the authored disabled opacity,
	// which is one number on the modifier rather than a second set of gradient endpoints.
	Mixtormat::FMixtormatStateModifier Modifier;
	if (State == EState::Disabled || State == EState::DisabledSelected)
	{
		Modifier.Opacity = Theme.ControlLayout.DisabledLabelOpacity;
	}

	const Mixtormat::FMixtormatSurfaceRecipe Recipe =
		Mixtormat::MakeButtonRecipe(Theme, ToButtonState(State), bShowSeparator);

	Mixtormat::FMixtormatSurfaceSamples Samples;
	Mixtormat::CompositeSurface(Recipe, Palette, Modifier, Samples);

	// Body, then the hairline and the separator on top of it. Both edges carry their own blend,
	// independent of the body's -- which is the prototype's structure and was the whole reason this
	// surface had a hand-rolled painter.
	int32 Layer = Mixtormat::FMixtormatSurfacePainter::PaintBody(
		Elements, LayerId, Geometry, Recipe, Samples);
	Layer = Mixtormat::FMixtormatSurfacePainter::PaintBorders(
		Elements, Layer, Geometry, Recipe, Palette, WidgetStyle, Samples);

	FWidgetStyle ContentStyle = WidgetStyle;
	ContentStyle.SetForegroundColor(TextColor(State));
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, Layer + 1, ContentStyle, bParentEnabled);
}