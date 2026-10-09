// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatLayerIcon::Construct(const FArguments& InArgs)
{
	bVisibility = InArgs._bVisibility;
	MaxSize = InArgs._MaxSize;
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
	const float BoundedTargetSize = MaxSize > 0.0f ? FMath::Min(TargetSize, MaxSize) : TargetSize;
	ChildSlot[SNew(SBox).WidthOverride(BoundedTargetSize).HeightOverride(BoundedTargetSize)];
}

int32 SMixtormatLayerIcon::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	const bool On = bOn.Get(true);
	const bool Enabled = IsEnabled() && bParentEnabled;
	const Mixtormat::FMixtormatIconStyle& IconStyle = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(bVisibility ? Mixtormat::EMixtormatIconRole::LayerEye : Mixtormat::EMixtormatIconRole::LayerDisclosure)];
	const float GlyphSize = MaxSize > 0.0f ? FMath::Min(IconStyle.GlyphSize, MaxSize) : IconStyle.GlyphSize;
	const FVector2f Size(GlyphSize, GlyphSize);
	const FVector2f Offset = (FVector2f(Geometry.GetLocalSize()) - Size) * 0.5f;
	const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	// A disabled EYE recesses like SMixtormatWellBox's disabled shade: pure black at full
	// strength -- the same recess the wells sink with -- not a faded mark and not the Shade
	// role, which reads gray over the row. The disclosure glyph keeps its ordinary fade.
	const bool bRecessed = !Enabled && bVisibility;
	FLinearColor Color = bVisibility && !On
		? Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)
		: Palette.Get((bActive.Get(false) || IsHovered())
			? Mixtormat::EMixtormatColorRole::Accent : Mixtormat::EMixtormatColorRole::Text);
	if (bRecessed)
	{
		Color = FLinearColor::Black;
		Color.A = 1.0f;
	}
	else
	{
		Color.A = !Enabled
			? IconStyle.DisabledOpacity
			: (IsHovered() || bActive.Get(false)) ? IconStyle.HoverOpacity
			: IconStyle.RestOpacity;
	}
	// A visibility square owns its brush; every other use borrows the caller's, and an unbound
	// Icon attribute resolves to null. MakeBox dereferences the brush, so a non-visibility caller
	// that omits .Icon(...) would fault here rather than draw nothing.
	const FSlateBrush* Brush = bVisibility
		? (On ? &Filled : &Hollow)
		: Icon.Get();

	if (Brush)
	{
		FSlateDrawElement::MakeBox(
			Elements,
			LayerId,
			Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)),
			Brush,
			ESlateDrawEffect::None,
			Color * WidgetStyle.GetColorAndOpacityTint());
	}

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
