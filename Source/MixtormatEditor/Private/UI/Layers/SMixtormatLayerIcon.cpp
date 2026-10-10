// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
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
		static_cast<uint8>(bVisibility ? Mixtormat::EMixtormatIconRole::LayerVisToggle : Mixtormat::EMixtormatIconRole::LayerDisclosure)];
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
		static_cast<uint8>(bVisibility ? Mixtormat::EMixtormatIconRole::LayerVisToggle : Mixtormat::EMixtormatIconRole::LayerDisclosure)];
	const float GlyphSize = MaxSize > 0.0f ? FMath::Min(IconStyle.GlyphSize, MaxSize) : IconStyle.GlyphSize;
	const FVector2f Size(GlyphSize, GlyphSize);
	const FVector2f Offset = (FVector2f(Geometry.GetLocalSize()) - Size) * 0.5f;
	const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;

	// State colour and coverage, then the state's blend against the row body the mark sits on.
	// Normal skips the composite, so the default path is the plain tint it always was.
	const bool bHot = IsHovered() || bActive.Get(false);
	FLinearColor Source = bVisibility && !On
		? Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)
		: Palette.Get(bHot && Enabled
			? Mixtormat::EMixtormatColorRole::Accent : Mixtormat::EMixtormatColorRole::Text);
	MixtormatCompositing::EMixtormatBlendMode Blend = MixtormatCompositing::EMixtormatBlendMode::Normal;
	if (!Enabled)
	{
		// A disabled mark recesses: black at full coverage, and the role's Disabled blend decides
		// how it darkens the row -- Soft Light by default, which deepens rather than replacing.
		Source = FLinearColor::Black;
		Source.A = 1.0f;
		Blend = IconStyle.DisabledBlend;
	}
	else
	{
		Source.A = bHot ? IconStyle.HoverOpacity : IconStyle.RestOpacity;
		Blend = bHot ? IconStyle.HoverBlend : IconStyle.RestBlend;
	}
	if (Blend != MixtormatCompositing::EMixtormatBlendMode::Normal)
	{
		// Slate cannot multiply the image against the already-painted row at draw time.
		// Resolve the blend RGB using the row's representative colour, then let the
		// squircle brush carry its original coverage. Compositing at partial alpha
		// and forcing A=1 painted an opaque, backdrop-coloured glyph instead.
		const float Coverage = FMath::Clamp(Source.A, 0.0f, 1.0f);
		const Mixtormat::FMixtormatResolvedLayerStyle& LayerStyle = FMixtormatThemeStore::GetResolved().Layers;
		const Mixtormat::FMixtormatResolvedRamp& RowRamp = bHot ? LayerStyle.RowHover : LayerStyle.Row;
		const FLinearColor RowMid = RowRamp.Colors.IsEmpty()
			? LayerStyle.Base : RowRamp.Colors[RowRamp.Colors.Num() / 2];
		FLinearColor FullStrengthSource = Source;
		FullStrengthSource.A = 1.0f;
		Source = MixtormatCompositing::ApplyBlend(Blend,
			MixtormatCompositing::Normal(LayerStyle.Base, RowMid), FullStrengthSource);
		Source.A = Coverage;
	}

	const FSlateBrush* Brush = bVisibility ? MixtormatIcons::Squircle() : Icon.Get();

	if (Brush)
	{
		FSlateDrawElement::MakeBox(
			Elements,
			LayerId,
			Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)),
			Brush,
			ESlateDrawEffect::None,
			Source * WidgetStyle.GetColorAndOpacityTint());
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
