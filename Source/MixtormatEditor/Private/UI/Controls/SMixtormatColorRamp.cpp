// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatColorRamp.h"

#include "MixtormatColorRampMath.h"
#include "Editor.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

void SMixtormatColorRamp::Construct(const FArguments& Args)
{
	Ramp = Args._Ramp.Get(FMixtormatColorRamp());
	Ramp.Sanitize();
	Height = Args._Height;
	DomainMin = Args._DomainMin;
	DomainMax = Args._DomainMax;
	OnChanged = Args._OnChanged;
	OnBeginInteractiveEdit = Args._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = Args._OnEndInteractiveEdit;

	const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);
	const auto AddButton = [&Toolbar](const FText& Label, const FSimpleDelegate& Click)
	{
		Toolbar->AddSlot().AutoWidth().Padding(0.0f, 0.0f,
			FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconGap, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(2.0f)
			.OnClicked_Lambda([Click]() { Click.ExecuteIfBound(); return FReply::Handled(); })
			[SNew(STextBlock).Text(Label)]
		];
	};
	AddButton(FText::FromString(TEXT("Interpolation")), FSimpleDelegate::CreateSP(this, &SMixtormatColorRamp::CycleInterpolation));
	AddButton(FText::FromString(TEXT("Reset")), FSimpleDelegate::CreateSP(this, &SMixtormatColorRamp::ResetRamp));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f,
			FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap)
		[
			SNew(SBox).HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight)[Toolbar]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(Height + FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding * 2.0f)
		]
	];
}

float SMixtormatColorRamp::XToScreen(const FVector2D& Size, const float X) const
{
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X) - Pad;
	return X0 + (X - DomainMin) / FMath::Max(DomainMax - DomainMin, 1.0e-4f) * (X1 - X0);
}

float SMixtormatColorRamp::ScreenToX(const FVector2D& Size, const float ScreenX) const
{
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X) - Pad;
	const float T = FMath::Clamp((ScreenX - X0) / FMath::Max(X1 - X0, 1.0f), 0.0f, 1.0f);
	return DomainMin + T * (DomainMax - DomainMin);
}

int32 SMixtormatColorRamp::HitStop(const FVector2D& Size, const FVector2D& Position) const
{
	const float Radius = MixtormatTokens::ScalarRampPointSize * 1.3f;
	for (int32 Index = Ramp.Stops.Num() - 1; Index >= 0; --Index)
	{
		const float SX = XToScreen(Size, Ramp.Stops[Index].X);
		if (FMath::Abs(static_cast<float>(Position.X) - SX) <= Radius) { return Index; }
	}
	return INDEX_NONE;
}

FVector2D SMixtormatColorRamp::ComputeDesiredSize(float) const
{
	return FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.RowFieldMinWidth * 2.0f,
		Height + FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding * 2.0f
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap);
}

int32 SMixtormatColorRamp::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X) - Pad;
	const float Y0 = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y) - Pad;
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;

	// Gradient bar, sampled across the domain.
	constexpr int32 Samples = 96;
	const float BarHeight = FMath::Max((Y1 - Y0) - MixtormatTokens::ScalarRampPointSize, 4.0f);
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float T0 = static_cast<float>(Index) / static_cast<float>(Samples);
		const float T1 = static_cast<float>(Index + 1) / static_cast<float>(Samples);
		const float XA = X0 + (X1 - X0) * T0;
		const float XB = X0 + (X1 - X0) * T1;
		const float Value = DomainMin + (DomainMax - DomainMin) * (T0 + T1) * 0.5f;
		const FLinearColor Color = MixtormatColorRampMath::Evaluate(Ramp, Value);
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(FMath::Max(XB - XA, 1.0f), BarHeight), FSlateLayoutTransform(FVector2f(XA, Y0))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
	}

	// Zero line for a signed domain.
	if (DomainMin < 0.0f && DomainMax > 0.0f)
	{
		const float ZeroX = XToScreen(Size, 0.0f);
		const TArray<FVector2f> Line = { FVector2f(ZeroX, Y0), FVector2f(ZeroX, Y0 + BarHeight) };
		FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Line,
			ESlateDrawEffect::None, Pal.Get(Mixtormat::EMixtormatColorRole::TextMuted), false,
			MixtormatTokens::ScalarRampGridThickness);
	}

	// Stop handles.
	const float HandleY = Y0 + BarHeight + MixtormatTokens::ScalarRampPointSize * 0.5f;
	for (int32 Index = 0; Index < Ramp.Stops.Num(); ++Index)
	{
		const float SX = XToScreen(Size, Ramp.Stops[Index].X);
		const float R = MixtormatTokens::ScalarRampPointSize * 0.5f;
		const FLinearColor Border = Index == DragStop ? Pal.Get(Mixtormat::EMixtormatColorRole::Accent)
			: Index == HoverStop ? Pal.Get(Mixtormat::EMixtormatColorRole::Text)
			: Pal.Get(Mixtormat::EMixtormatColorRole::TextMuted);
		FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(
			FVector2f(R * 2.0f, R * 2.0f), FSlateLayoutTransform(FVector2f(SX - R, HandleY - R))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Ramp.Stops[Index].Color);
		FSlateDrawElement::MakeBox(Elements, Layer + 3, Geometry.ToPaintGeometry(
			FVector2f(R * 2.0f + 2.0f, R * 2.0f + 2.0f), FSlateLayoutTransform(FVector2f(SX - R - 1.0f, HandleY - R - 1.0f))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Border);
	}
	return SCompoundWidget::OnPaint(Args, Geometry, Cull, Elements, Layer + 4, Style, Enabled);
}

void SMixtormatColorRamp::NotifyEdit(bool bInteractive)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	OnChanged.ExecuteIfBound(Ramp);
	if (!bInteractive) { OnBeginInteractiveEdit.ExecuteIfBound(); OnEndInteractiveEdit.ExecuteIfBound(); }
}

void SMixtormatColorRamp::OpenStopPicker(const int32 StopIndex)
{
	if (!Ramp.Stops.IsValidIndex(StopIndex)) { return; }
	const FLinearColor Original = Ramp.Stops[StopIndex].Color;
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.InitialColor = Original;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, StopIndex](const FLinearColor NewColor)
		{
			if (Ramp.Stops.IsValidIndex(StopIndex))
			{
				Ramp.Stops[StopIndex].Color = NewColor;
				NotifyEdit(false);
			}
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, StopIndex, Original](const FLinearColor)
		{
			if (Ramp.Stops.IsValidIndex(StopIndex))
			{
				Ramp.Stops[StopIndex].Color = Original;
				NotifyEdit(false);
			}
		});
	OpenColorPicker(PickerArgs);
}

void SMixtormatColorRamp::CycleInterpolation()
{
	OnBeginInteractiveEdit.ExecuteIfBound();
	const uint8 Next = (static_cast<uint8>(Ramp.Interpolation) + 1u)
		% (static_cast<uint8>(EMixtormatColorRampInterpolation::Smooth) + 1u);
	Ramp.Interpolation = static_cast<EMixtormatColorRampInterpolation>(Next);
	NotifyEdit(true);
	OnEndInteractiveEdit.ExecuteIfBound();
}

void SMixtormatColorRamp::ResetRamp()
{
	OnBeginInteractiveEdit.ExecuteIfBound();
	Ramp.Stops = {
		FMixtormatColorRampStop{DomainMin, FLinearColor::Black},
		FMixtormatColorRampStop{DomainMax, FLinearColor::White}
	};
	Ramp.Interpolation = EMixtormatColorRampInterpolation::Linear;
	NotifyEdit(true);
	OnEndInteractiveEdit.ExecuteIfBound();
}

FReply SMixtormatColorRamp::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const int32 Hit = HitStop(Size, Local);

	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (Hit != INDEX_NONE && Ramp.Stops.Num() > 2)
		{
			OnBeginInteractiveEdit.ExecuteIfBound();
			Ramp.Stops.RemoveAt(Hit);
			NotifyEdit(true);
			OnEndInteractiveEdit.ExecuteIfBound();
		}
		return FReply::Handled();
	}

	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (Event.IsControlDown() && Hit != INDEX_NONE)
		{
			OpenStopPicker(Hit);
			return FReply::Handled();
		}
		if (Hit == INDEX_NONE)
		{
			if (Ramp.Stops.Num() >= FMixtormatColorRamp::MaxStops) { return FReply::Handled(); }
			const float X = ScreenToX(Size, static_cast<float>(Local.X));
			OnBeginInteractiveEdit.ExecuteIfBound();
			int32 InsertAt = Ramp.Stops.Num();
			for (int32 Index = 0; Index < Ramp.Stops.Num(); ++Index)
			{
				if (Ramp.Stops[Index].X > X) { InsertAt = Index; break; }
			}
			Ramp.Stops.Insert(FMixtormatColorRampStop{X, MixtormatColorRampMath::Evaluate(Ramp, X)}, InsertAt);
			DragStop = InsertAt;
			bDragging = true;
			bMoved = true;
			NotifyEdit(true);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		DragStop = Hit;
		bDragging = true;
		bMoved = false;
		OnBeginInteractiveEdit.ExecuteIfBound();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMixtormatColorRamp::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	if (!HasMouseCapture())
	{
		const int32 NewHover = HitStop(Size, Local);
		if (NewHover != HoverStop) { HoverStop = NewHover; Invalidate(EInvalidateWidgetReason::Paint); }
		return FReply::Unhandled();
	}
	if (bDragging && Ramp.Stops.IsValidIndex(DragStop))
	{
		// Clamp between the neighbouring stops so the order never changes mid-drag.
		const float MinX = DragStop > 0 ? Ramp.Stops[DragStop - 1].X + 1.0e-4f : DomainMin;
		const float MaxX = DragStop + 1 < Ramp.Stops.Num()
			? Ramp.Stops[DragStop + 1].X - 1.0e-4f : DomainMax;
		Ramp.Stops[DragStop].X = FMath::Clamp(ScreenToX(Size, static_cast<float>(Local.X)), MinX, MaxX);
		bMoved = true;
		NotifyEdit(true);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMixtormatColorRamp::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	if (bDragging)
	{
		bDragging = false;
		DragStop = INDEX_NONE;
		OnEndInteractiveEdit.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SMixtormatColorRamp::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if (bDragging)
	{
		bDragging = false;
		DragStop = INDEX_NONE;
		OnEndInteractiveEdit.ExecuteIfBound();
	}
	SCompoundWidget::OnMouseCaptureLost(Event);
}
