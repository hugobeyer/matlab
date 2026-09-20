// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatIconButton.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatIconButton::Construct(const FArguments& InArgs)
{
	bActive = InArgs._bActive;
	OnClicked = InArgs._OnClicked;
	OnClickedWithModifiers = InArgs._OnClickedWithModifiers;
	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}

	// Two boxes: the outer one is the click target and the inner one is the glyph. Hit testing
	// follows geometry, so growing the outer box alone makes the button easier to hit without
	// making the icon bigger or the row louder.
	const float GlyphSize = InArgs._Size;
	const float TargetSize = GlyphSize + MixtormatTokens::IconButtonHitSlop;
	ChildSlot
	[
		SNew(SBox)
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
	];
}

FSlateColor SMixtormatIconButton::GetGlyphColor() const
{
	// No plate, so every state has to live in the glyph itself.
	if (bActive.Get(false))
	{
		return IsHovered() ? MixtormatPalette::AccentBright() : MixtormatPalette::Accent();
	}
	return IsHovered() ? MixtormatPalette::IconHover() : MixtormatPalette::IconRest();
}

FCursorReply SMixtormatIconButton::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

FReply SMixtormatIconButton::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
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
