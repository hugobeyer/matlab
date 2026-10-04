// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatIconButton.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatIconButton::Construct(const FArguments& InArgs)
{
	bActive = InArgs._bActive;
	Role = InArgs._Role;
	OnClicked = InArgs._OnClicked;
	OnClickedWithModifiers = InArgs._OnClickedWithModifiers;
	// Two boxes: the outer one is the click target and the inner one is the glyph. Hit testing
	// follows geometry, so growing the outer box alone makes the button easier to hit without
	// making the icon bigger or the row louder.
	const bool bSemanticRole = Role != Mixtormat::EMixtormatIconRole::Count;
	const Mixtormat::FMixtormatIconStyle* Semantic = bSemanticRole
		? &FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Role)] : nullptr;
	const float GlyphSize = Semantic ? Semantic->GlyphSize : InArgs._Size;
	const float TargetSize = Semantic
		? (Semantic->HitSize > 0.0f ? Semantic->HitSize : Semantic->ButtonSize)
		: GlyphSize + MixtormatTokens::IconButtonHitSlop;
	ChildSlot
	[
		SNew(SMixtormatHelp)
		.Text(InArgs._ToolTip)
		.Enabled_Lambda([this]() { return IsEnabled(); })
		[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.WidthOverride(TargetSize)
		.HeightOverride(TargetSize)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(GlyphSize)
			.HeightOverride(GlyphSize)
			[
				SNew(SImage)
				.Image(InArgs._Icon)
				.ColorAndOpacity(this, &SMixtormatIconButton::GetGlyphColor)
			]
		]
		]
	];
}

FSlateColor SMixtormatIconButton::GetGlyphColor() const
{
	// No plate, so every state has to live in the glyph itself.
	if (Role != Mixtormat::EMixtormatIconRole::Count)
	{
		const Mixtormat::FMixtormatResolvedStyle& R = FMixtormatThemeStore::GetResolved();
		const Mixtormat::FMixtormatIconStyle& I = R.Icons.Roles[static_cast<uint8>(Role)];
		const FLinearColor C = bActive.Get(false)
			? R.Palette.Get(Mixtormat::EMixtormatColorRole::Accent)
			: R.Palette.Get(Mixtormat::EMixtormatColorRole::Text);
		return FSlateColor(C.CopyWithNewOpacity(!IsEnabled()
			? I.DisabledOpacity : IsHovered() ? I.HoverOpacity : I.RestOpacity));
	}
	if (!IsEnabled())
	{
		return MixtormatPalette::IconRest().CopyWithNewOpacity(0.25f);
	}
	if (bActive.Get(false))
	{
		return IsHovered() ? MixtormatPalette::AccentBright() : FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	}
	return IsHovered() ? MixtormatPalette::IconHover() : MixtormatPalette::IconRest();
}

FCursorReply SMixtormatIconButton::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

FReply SMixtormatIconButton::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	// Disabled: pass the press through, so a faded chevron behaves like the row behind it.
	if (!IsEnabled() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
}

// A fast second click arrives here instead of OnMouseButtonDown. Treat it as a press so the
// release fires the button again, and never let it reach the row behind (select + expand).
FReply SMixtormatIconButton::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMixtormatIconButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;

	// Only fire when the release lands on the glyph, so a press that wanders off cancels.
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		if (OnClickedWithModifiers.IsBound())
		{
			OnClickedWithModifiers.Execute(MouseEvent);
		}
		else
		{
			OnClicked.ExecuteIfBound();
		}
	}
	return FReply::Handled();
}
