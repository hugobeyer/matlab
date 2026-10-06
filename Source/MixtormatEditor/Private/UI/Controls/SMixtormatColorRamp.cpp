// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatColorRamp.h"

#include "Editor.h"
#include "MixtormatColorRampMath.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Colors/SColorPicker.h"

namespace MixtormatColorRampPrivate
{
	bool Equals(const FMixtormatColorRamp& A, const FMixtormatColorRamp& B)
	{
		if (A.Interpolation != B.Interpolation || A.Stops.Num() != B.Stops.Num()) { return false; }
		for (int32 Index = 0; Index < A.Stops.Num(); ++Index)
		{
			const FMixtormatColorRampStop& AS = A.Stops[Index];
			const FMixtormatColorRampStop& BS = B.Stops[Index];
			if (AS.X != BS.X || AS.Color.R != BS.Color.R || AS.Color.G != BS.Color.G
				|| AS.Color.B != BS.Color.B || AS.Color.A != BS.Color.A)
			{
				return false;
			}
		}
		return true;
	}

	FLinearColor Opaque(const FLinearColor& Color)
	{
		return FLinearColor(Color.R, Color.G, Color.B, 1.0f);
	}
}

void SMixtormatColorRamp::Construct(const FArguments& Args)
{
	RampAttribute = Args._Ramp;
	Ramp = RampAttribute.Get(FMixtormatColorRamp());
	Ramp.Sanitize();
	Height = Args._Height;
	DomainMin = Args._DomainMin;
	DomainMax = Args._DomainMax;
	OnChanged = Args._OnChanged;
	OnBeginInteractiveEdit = Args._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = Args._OnEndInteractiveEdit;
	BuildLayout();
}

void SMixtormatColorRamp::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDragging || !RampAttribute.IsBound()) { return; }

	FMixtormatColorRamp AuthoredRamp = RampAttribute.Get(Ramp);
	AuthoredRamp.Sanitize();
	if (MixtormatColorRampPrivate::Equals(Ramp, AuthoredRamp)) { return; }

	Ramp = MoveTemp(AuthoredRamp);
	if (!Ramp.Stops.IsValidIndex(SelectedPoint)) { SelectedPoint = INDEX_NONE; }
	HoverPoint = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SMixtormatColorRamp::InsertPointAt(const float X, const float GraphY)
{
	int32 Insert = 0;
	while (Insert < Ramp.Stops.Num() && Ramp.Stops[Insert].X < X) { ++Insert; }
	Ramp.Stops.Insert(FMixtormatColorRampStop{X, MixtormatColorRampMath::Evaluate(Ramp, X)}, Insert);
	return Insert;
}

void SMixtormatColorRamp::RemovePoint(const int32 Index)
{
	Ramp.Stops.RemoveAt(Index);
}

void SMixtormatColorRamp::ResetPoints()
{
	Ramp.Stops = {
		FMixtormatColorRampStop{DomainMin, FLinearColor::Black},
		FMixtormatColorRampStop{DomainMax, FLinearColor::White}
	};
	Ramp.Interpolation = EMixtormatColorRampInterpolation::Linear;
}

void SMixtormatColorRamp::ApplyPointDrag(const int32 Index, const float GraphX, const float GraphY,
	const FVector2f& ScreenPos)
{
	// X is no longer clamped against neighbours here: the shared editor swaps adjacent stops
	// when the dragged stop crosses one, so the array stays sorted and the dragged payload keeps
	// its identity. Endpoints are still locked to the domain.
	Ramp.Stops[Index].X = GraphX;
}

void SMixtormatColorRamp::SwapPointState(const int32 IndexA, const int32 IndexB)
{
	if (Ramp.Stops.IsValidIndex(IndexA) && Ramp.Stops.IsValidIndex(IndexB))
	{
		Swap(Ramp.Stops[IndexA].Color, Ramp.Stops[IndexB].Color);
	}
}

void SMixtormatColorRamp::NotifyPointRemoved(const int32 RemovedIndex)
{
	// Keep the selection on a sensible neighbour: the one that took the removed stop's place.
	if (SelectedPoint == RemovedIndex)
	{
		SelectedPoint = FMath::Min(RemovedIndex, GetPointCount() - 1);
	}
	else if (SelectedPoint > RemovedIndex)
	{
		--SelectedPoint;
	}
}

void SMixtormatColorRamp::NotifyPointInserted(const int32 InsertedIndex)
{
	// Inserting a stop selects the new stop so the user can immediately recolour it.
	SelectedPoint = InsertedIndex;
}

bool SMixtormatColorRamp::IsEndpointLocked(const int32 Index) const
{
	// Color ramp endpoints stay locked to the domain.
	return Index == 0 || Index == Ramp.Stops.Num() - 1;
}

void SMixtormatColorRamp::OnRampEdited(const bool bInteractive)
{
	OnChanged.ExecuteIfBound(Ramp);
}

void SMixtormatColorRamp::SetInterpolation(const int32 Index)
{
	Ramp.Interpolation = static_cast<EMixtormatColorRampInterpolation>(Index);
}

FText SMixtormatColorRamp::GetInterpolationLabel(const int32 Index) const
{
	switch (Index)
	{
	case 0: return FText::FromString(TEXT("Constant"));
	case 1: return FText::FromString(TEXT("Linear"));
	default: return FText::FromString(TEXT("Smooth"));
	}
}

const FSlateBrush* SMixtormatColorRamp::GetInterpolationIcon(const int32 Index) const
{
	switch (Index)
	{
	case 0: return MixtormatIcons::ScalarRampConstant();
	case 1: return MixtormatIcons::ScalarRampLinear();
	default: return MixtormatIcons::ScalarRampSpline();
	}
}

void SMixtormatColorRamp::PaintRampContent(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size) const
{
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float Y0 = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y) - Pad;
	const float BarHeight = FMath::Max((Y1 - Y0) - MixtormatTokens::ScalarRampPointSize, 4.0f);

	// The gradient bar, sampled across the domain. One box per sample, so the bar reads the same
	// interpolation the GPU evaluates.
	constexpr int32 Samples = 96;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float T0 = static_cast<float>(Index) / static_cast<float>(Samples);
		const float T1 = static_cast<float>(Index + 1) / static_cast<float>(Samples);
		const float XA = XToScreen(Size, DomainMin + (DomainMax - DomainMin) * T0);
		const float XB = XToScreen(Size, DomainMin + (DomainMax - DomainMin) * T1);
		const float Value = DomainMin + (DomainMax - DomainMin) * (T0 + T1) * 0.5f;
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(FMath::Max(XB - XA, 1.0f), BarHeight), FSlateLayoutTransform(FVector2f(XA, Y0))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			MixtormatColorRampPrivate::Opaque(MixtormatColorRampMath::Evaluate(Ramp, Value)));
	}
}

void SMixtormatColorRamp::PaintPointMarker(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size, const int32 Index,
	const bool bActive, const bool bHover) const
{
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float Y0 = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y) - Pad;
	const float BarHeight = FMath::Max((Y1 - Y0) - MixtormatTokens::ScalarRampPointSize, 4.0f);
	const float SX = XToScreen(Size, Ramp.Stops[Index].X);
	const float R = MixtormatTokens::ScalarRampPointSize * 0.5f;
	const float HandleY = Y0 + BarHeight + R;
	const bool bSelected = Index == SelectedPoint;
	// Selected stop gets a clear accent outline/ring so it stays visible while not being dragged.
	if (bSelected)
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(R * 2.0f + 6.0f, R * 2.0f + 6.0f),
			FSlateLayoutTransform(FVector2f(SX - R - 3.0f, HandleY - R - 3.0f))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			Pal.Get(Mixtormat::EMixtormatColorRole::Accent));
	}
	const FLinearColor Border = bActive ? Pal.Get(Mixtormat::EMixtormatColorRole::Accent)
		: bHover ? Pal.Get(Mixtormat::EMixtormatColorRole::Text)
		: Pal.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(
		FVector2f(R * 2.0f + 2.0f, R * 2.0f + 2.0f),
		FSlateLayoutTransform(FVector2f(SX - R - 1.0f, HandleY - R - 1.0f))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Border);
	FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(
		FVector2f(R * 2.0f, R * 2.0f), FSlateLayoutTransform(FVector2f(SX - R, HandleY - R))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
		MixtormatColorRampPrivate::Opaque(Ramp.Stops[Index].Color));
}

void SMixtormatColorRamp::OpenStopPicker(const int32 StopIndex)
{
	if (!Ramp.Stops.IsValidIndex(StopIndex)) { return; }
	const FLinearColor Original = Ramp.Stops[StopIndex].Color;
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.ParentWidget = SharedThis(this);
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

FReply SMixtormatColorRamp::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Ctrl-click a stop to recolour it; everything else is the shared ramp interaction.
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Event.IsControlDown())
	{
		const int32 Hit = HitPoint(Geometry.GetLocalSize(),
			Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		if (Hit != INDEX_NONE)
		{
			OpenStopPicker(Hit);
			return FReply::Handled();
		}
	}
	return SMixtormatRampEditorBase::OnMouseButtonDown(Geometry, Event);
}
