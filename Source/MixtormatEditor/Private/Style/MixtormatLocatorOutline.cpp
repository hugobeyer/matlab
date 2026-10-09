// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatLocatorOutline.h"

#include "Style/MixtormatStyleLocator.h"
#include "Style/MixtormatThemeStore.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

void SMixtormatLocatorOutline::Construct(const FArguments&)
{
	ChildSlot[SNullWidget::NullWidget];
	SetVisibility(EVisibility::HitTestInvisible);
	Self = SharedThis(this);
}

void SMixtormatLocatorOutline::Tick(const FGeometry&, double, float)
{
	// The locator owns the pulse timing and expires itself; this only keeps the paint live.
	if (Mixtormat::FMixtormatStyleLocator::Tick())
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 SMixtormatLocatorOutline::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	FSlateRect TargetRect;
	const TSharedPtr<SWindow> MyWindow = Self.IsValid()
		? FSlateApplication::Get().FindWidgetWindow(Self.Pin().ToSharedRef()) : nullptr;
	if (!Mixtormat::FMixtormatStyleLocator::GetTargetRect(TargetRect)
		|| !MyWindow.IsValid()
		|| Mixtormat::FMixtormatStyleLocator::GetTargetWindow().Pin() != MyWindow)
	{
		return Layer;
	}

	const float Pulse = Mixtormat::FMixtormatStyleLocator::GetPulseAlpha();
	const FVector2f TargetSize(static_cast<float>(TargetRect.Right - TargetRect.Left),
		static_cast<float>(TargetRect.Bottom - TargetRect.Top));
	if (Pulse <= 0.0f || TargetSize.X <= 1.0f || TargetSize.Y <= 1.0f)
	{
		return Layer;
	}

	const FVector2f LocalTopLeft = FVector2f(AllottedGeometry.AbsoluteToLocal(
		FVector2D(TargetRect.Left, TargetRect.Top)));
	const FLinearColor Orange = FMixtormatThemeStore::GetResolved().Palette.Get(
		Mixtormat::EMixtormatColorRole::Modified);

	// A whisper of fill so the region reads without hiding what is under it, and a crisp
	// half-opacity border that carries the exact bounds.
	FLinearColor Fill = Orange;
	Fill.A = 0.12f * Pulse;
	FSlateDrawElement::MakeBox(OutDrawElements, Layer,
		AllottedGeometry.ToPaintGeometry(TargetSize, FSlateLayoutTransform(LocalTopLeft)),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Fill);

	FLinearColor Border = Orange;
	Border.A = 0.5f * Pulse;
	TArray<FVector2f> Outline;
	Outline.Add(FVector2f::ZeroVector);
	Outline.Add(FVector2f(TargetSize.X, 0.0f));
	Outline.Add(TargetSize);
	Outline.Add(FVector2f(0.0f, TargetSize.Y));
	Outline.Add(FVector2f::ZeroVector);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1,
		AllottedGeometry.ToPaintGeometry(TargetSize, FSlateLayoutTransform(LocalTopLeft)),
		Outline, ESlateDrawEffect::None, Border, false, 1.0f);
	return Layer + 2;
}
