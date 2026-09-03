#include "UI/Atoms/SMixtormatStatusDot.h"

#include "Style/MixtormatStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatStatusDot::Construct(const FArguments& InArgs)
{
	bFilled = InArgs._bFilled;
	OnClicked = InArgs._OnClicked;
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
			SNew(SImage).Image(this, &SMixtormatStatusDot::GetBrush)
		]
	];
}

const FSlateBrush* SMixtormatStatusDot::GetBrush() const
{
	return FMixtormatStyle::Get().GetBrush(bFilled.Get(false)
		? TEXT("Mixtormat.StatusDot.Filled")
		: TEXT("Mixtormat.StatusDot.Hollow"));
}

FCursorReply SMixtormatStatusDot::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return OnClicked.IsBound()
		? FCursorReply::Cursor(EMouseCursor::Hand)
		: FCursorReply::Unhandled();
}

FReply SMixtormatStatusDot::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	// Unhandled without a listener, so the row underneath still gets the press and can start a
	// drag from anywhere along its length -- including across the dot.
	if (!OnClicked.IsBound() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
}

FReply SMixtormatStatusDot::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		OnClicked.ExecuteIfBound();
	}
	return FReply::Handled();
}
