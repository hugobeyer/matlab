// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatLayerIcon::Construct(const FArguments& InArgs)
{
	bVisibility = InArgs._bVisibility;
	bOn = InArgs._bOn;
	bActive = InArgs._bActive;
	Icon = InArgs._Icon;
	OnClicked = InArgs._OnClicked;
	OnClickedWithModifiers = InArgs._OnClickedWithModifiers;
	const Mixtormat::FMixtormatIconStyle& IconStyle = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(bVisibility ? Mixtormat::EMixtormatIconRole::LayerEye : Mixtormat::EMixtormatIconRole::LayerDisclosure)];
	Filled = FSlateRoundedBoxBrush(FLinearColor::White, IconStyle.MarkRadius);
	Hollow = FSlateRoundedBoxBrush(FLinearColor::Transparent, IconStyle.MarkRadius,
		FLinearColor::White, IconStyle.MarkOutlineWidth);
	const float TargetSize = IconStyle.HitSize > 0.0f ? IconStyle.HitSize
		: IconStyle.ButtonSize > 0.0f ? IconStyle.ButtonSize : IconStyle.GlyphSize;
	ChildSlot[SNew(SBox).WidthOverride(TargetSize).HeightOverride(TargetSize)];
}

int32 SMixtormatLayerIcon::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	const bool On = bOn.Get(true);
	const bool Enabled = IsEnabled() && bParentEnabled;
	const Mixtormat::FMixtormatIconStyle& IconStyle = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(bVisibility ? Mixtormat::EMixtormatIconRole::LayerEye : Mixtormat::EMixtormatIconRole::LayerDisclosure)];
	const FVector2f Size(IconStyle.GlyphSize, IconStyle.GlyphSize);
	const FVector2f Offset = (FVector2f(Geometry.GetLocalSize()) - Size) * 0.5f;
	const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	FLinearColor Color = Palette.Get((bActive.Get(false) || IsHovered()) && Enabled
		? Mixtormat::EMixtormatColorRole::Accent : Mixtormat::EMixtormatColorRole::Text);
	Color.A = !Enabled
		? IconStyle.DisabledOpacity
		: (IsHovered() || bActive.Get(false)) ? IconStyle.HoverOpacity
		: IconStyle.RestOpacity;
	FSlateDrawElement::MakeBox(Elements, LayerId, Geometry.ToPaintGeometry(Size,
		FSlateLayoutTransform(Offset)), bVisibility ? (On ? &Filled : &Hollow) : Icon.Get(),
		ESlateDrawEffect::None, Color * WidgetStyle.GetColorAndOpacityTint());
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId + 1,
		WidgetStyle, bParentEnabled);
}

FCursorReply SMixtormatLayerIcon::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

FReply SMixtormatLayerIcon::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (!IsEnabled() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled();
}

FReply SMixtormatLayerIcon::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	return OnMouseButtonDown(Geometry, Event);
}

FReply SMixtormatLayerIcon::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	if (Geometry.IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		if (OnClickedWithModifiers.IsBound())
		{
			OnClickedWithModifiers.Execute(Event);
		}
		else
		{
			OnClicked.ExecuteIfBound();
		}
	}
	return FReply::Handled();
}
