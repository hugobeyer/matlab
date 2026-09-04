#include "UI/Atoms/SMixtormatIconButton.h"

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

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(InArgs._Size)
		.HeightOverride(InArgs._Size)
		[
			SNew(SImage)
			.Image(InArgs._Icon)
			.ColorAndOpacity(this, &SMixtormatIconButton::GetGlyphColor)
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
	return IsHovered() ? MixtormatPalette::IconHover() : MixtormatPalette::RowText();
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
