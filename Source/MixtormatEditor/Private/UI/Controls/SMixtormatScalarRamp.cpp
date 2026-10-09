// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatScalarRamp.h"

#include "Framework/Application/SlateApplication.h"
#include "MixtormatScalarRampMath.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"
#include "UI/Atoms/MixtormatIcons.h"

namespace MixtormatScalarRampPrivate
{
	bool Equals(const FMixtormatScalarRamp& A, const FMixtormatScalarRamp& B)
	{
		if (A.Interpolation != B.Interpolation || A.Points.Num() != B.Points.Num()) { return false; }
		for (int32 Index = 0; Index < A.Points.Num(); ++Index)
		{
			if (A.Points[Index].X != B.Points[Index].X || A.Points[Index].Y != B.Points[Index].Y)
			{
				return false;
			}
		}
		return true;
	}
}

void SMixtormatScalarRamp::Construct(const FArguments& Args)
{
	RampAttribute = Args._Ramp;
	Ramp = RampAttribute.Get(FMixtormatScalarRamp());
	Ramp.Sanitize();
	Height = Args._Height;
	CanonicalXMin = Args._CanonicalXMin;
	CanonicalXMax = Args._CanonicalXMax;
	CanonicalYMin = Args._CanonicalYMin;
	CanonicalYMax = Args._CanonicalYMax;
	SoftYMin = Args._SoftYMin;
	SoftYMax = Args._SoftYMax;
	ExtendedYMin = Args._ExtendedYMin;
	ExtendedYMax = Args._ExtendedYMax;
	DomainMin = CanonicalXMin;
	DomainMax = CanonicalXMax;
	OnChanged = Args._OnChanged;
	OnBeginInteractiveEdit = Args._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = Args._OnEndInteractiveEdit;
	EscapeArms.Init(EEscapeArm::None, Ramp.Points.Num());
	FrameCurve();
	BuildLayout();
}

void SMixtormatScalarRamp::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDragging || bDraggingFrame || !RampAttribute.IsBound()) { return; }

	FMixtormatScalarRamp AuthoredRamp = RampAttribute.Get(Ramp);
	AuthoredRamp.Sanitize();
	if (MixtormatScalarRampPrivate::Equals(Ramp, AuthoredRamp)) { return; }

	Ramp = MoveTemp(AuthoredRamp);
	EscapeArms.Init(EEscapeArm::None, Ramp.Points.Num());
	if (!Ramp.Points.IsValidIndex(SelectedPoint)) { SelectedPoint = INDEX_NONE; }
	HoverPoint = INDEX_NONE;
	bAutoZoom = true;
	FrameCurve();
}

int32 SMixtormatScalarRamp::InsertPointAt(const float X, const float GraphY)
{
	int32 Insert = 1;
	while (Insert < Ramp.Points.Num() - 1 && Ramp.Points[Insert].X < X) { ++Insert; }
	const float ClampedX = FMath::Clamp(X, Ramp.Points[Insert - 1].X + 0.001f, Ramp.Points[Insert].X - 0.001f);
	Ramp.Points.Insert({ClampedX, FMath::Clamp(GraphY, CanonicalYMin, CanonicalYMax)}, Insert);
	EscapeArms.Insert(EEscapeArm::None, Insert);
	return Insert;
}

void SMixtormatScalarRamp::RemovePoint(const int32 Index)
{
	Ramp.Points.RemoveAt(Index);
	EscapeArms.RemoveAt(Index);
}

void SMixtormatScalarRamp::ResetPoints()
{
	Ramp.ResetToIdentity();
	EscapeArms.Init(EEscapeArm::None, Ramp.Points.Num());
}

void SMixtormatScalarRamp::FinishPointDrag()
{
	if (EscapeArms.IsValidIndex(DragPoint))
	{
		const float Y = Ramp.Points[DragPoint].Y;
		const bool bArmStillAtBoundary = EscapeArms[DragPoint] == EEscapeArm::Canonical
			? FMath::IsNearlyEqual(Y, CanonicalYMin) || FMath::IsNearlyEqual(Y, CanonicalYMax)
			: EscapeArms[DragPoint] == EEscapeArm::Hard
				? (FMath::IsNearlyEqual(Y, SoftYMin) || FMath::IsNearlyEqual(Y, SoftYMax)) : true;
		if (!bArmStillAtBoundary) { EscapeArms[DragPoint] = EEscapeArm::None; }
	}
}

void SMixtormatScalarRamp::SwapPointState(const int32 IndexA, const int32 IndexB)
{
	// Full (X,Y) payload must travel with the drag identity. Shared SwapPoints already swaps X;
	// Y has to move here or the neighbour's height stays under the cursor after a cross.
	if (Ramp.Points.IsValidIndex(IndexA) && Ramp.Points.IsValidIndex(IndexB))
	{
		Swap(Ramp.Points[IndexA].Y, Ramp.Points[IndexB].Y);
	}
	// EscapeArms must move with its scalar ramp point; otherwise the arm would stay indexed to
	// the previous array position and silently apply to the wrong point after a crossing swap.
	if (EscapeArms.IsValidIndex(IndexA) && EscapeArms.IsValidIndex(IndexB))
	{
		Swap(EscapeArms[IndexA], EscapeArms[IndexB]);
	}
}

FVector2f SMixtormatScalarRamp::GetMarkerScreenPosition(const FVector2D& Size, const int32 Index) const
{
	if (!Ramp.Points.IsValidIndex(Index))
	{
		return FVector2f::ZeroVector;
	}
	return GraphToScreen(Size, Ramp.Points[Index].X, Ramp.Points[Index].Y);
}

void SMixtormatScalarRamp::NotifyPointRemoved(const int32 RemovedIndex)
{
	// Keep the selection on a sensible neighbour: the one that took the removed point's place.
	if (SelectedPoint == RemovedIndex)
	{
		SelectedPoint = FMath::Min(RemovedIndex, GetPointCount() - 1);
	}
	else if (SelectedPoint > RemovedIndex)
	{
		--SelectedPoint;
	}
}

void SMixtormatScalarRamp::NotifyPointInserted(const int32 InsertedIndex)
{
	// Inserting a point selects the new point so the user can immediately edit it.
	SelectedPoint = InsertedIndex;
}

bool SMixtormatScalarRamp::IsEndpointLocked(const int32 Index) const
{
	// Scalar signed remap endpoints stay locked to the domain unless explicitly redesigned.
	return Index == 0 || Index == Ramp.Points.Num() - 1;
}

void SMixtormatScalarRamp::BeginPointDrag(const int32 Index, const bool bCreated,
	const FGeometry& Geometry, const FPointerEvent& Event)
{
	DragStartX = Ramp.Points[Index].X;
	DragStartY = Ramp.Points[Index].Y;
	DragViewYMin = ViewYMin;
	DragViewYMax = ViewYMax;
	const FVector2f Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	DragStartScreenY = Local.Y;
	LastScreenX = Local.X;
	LastScreenY = Local.Y;
	bLockX = Event.IsControlDown();
	bLockY = Event.IsShiftDown();
	EEscapeArm Arm = EscapeArms.IsValidIndex(Index) ? EscapeArms[Index] : EEscapeArm::None;
	const bool bAtCanonicalBoundary = FMath::IsNearlyEqual(DragStartY, CanonicalYMin) || FMath::IsNearlyEqual(DragStartY, CanonicalYMax);
	const bool bAtSoftBoundary = FMath::IsNearlyEqual(DragStartY, SoftYMin) || FMath::IsNearlyEqual(DragStartY, SoftYMax);
	if ((Arm == EEscapeArm::Canonical && !bAtCanonicalBoundary) || (Arm == EEscapeArm::Hard && !bAtSoftBoundary)) { Arm = EEscapeArm::None; }
	if (EscapeArms.IsValidIndex(Index) && !bLockY) { EscapeArms[Index] = EEscapeArm::None; }
	DragStage = bLockY ? EDragStage::Locked
		: Arm == EEscapeArm::Canonical ? EDragStage::Canonical
		: Arm == EEscapeArm::Hard ? EDragStage::Hard
		: ((DragStartY < CanonicalYMin || DragStartY > CanonicalYMax) && DragStartY > SoftYMin && DragStartY < SoftYMax
			? EDragStage::Canonical : EDragStage::Locked);
}

void SMixtormatScalarRamp::ApplyPointDrag(const int32 Index, const float GraphX, const float GraphY,
	const FVector2f& ScreenPos)
{
	// X is no longer clamped against neighbours here: the shared editor swaps adjacent points
	// when the dragged point crosses one, so the array stays sorted and the dragged payload
	// keeps its identity. Endpoints are still locked to the domain.
	if (!bLockX && Index > 0 && Index < Ramp.Points.Num() - 1)
	{
		Ramp.Points[Index].X = GraphX;
	}
	const float Travel = FMath::Abs(ScreenPos.Y - DragStartScreenY);
	if (!bLockY)
	{
		float Y = FMath::Clamp(GraphY, CanonicalYMin, CanonicalYMax);
		if (DragStage == EDragStage::Canonical)
		{
			const float Sign = FMath::IsNearlyEqual(DragStartY, CanonicalYMin) ? -1.0f
				: FMath::IsNearlyEqual(DragStartY, CanonicalYMax) ? 1.0f : (DragStartY > 0.0f ? 1.0f : -1.0f);
			const float Limit = Sign > 0.0f ? SoftYMax : SoftYMin;
			const bool bOutward = Sign > 0.0f ? GraphY > DragStartY : GraphY < DragStartY;
			if (bOutward)
			{
				const float Distance = FMath::Abs(Limit - DragStartY);
				const float V = MixtormatScalarRampMath::ResistedTravel(Travel, Distance);
				Y = DragStartY + Sign * V;
				if (V >= Distance) { Y = Limit; EscapeArms[Index] = EEscapeArm::Hard; }
			}
			else { Y = FMath::Clamp(GraphY, SoftYMin, SoftYMax); }
		}
		else if (DragStage == EDragStage::Hard)
		{
			const float Sign = DragStartY >= 0.0f ? 1.0f : -1.0f;
			const float Limit = Sign > 0.0f ? ExtendedYMax : ExtendedYMin;
			const bool bOutward = Sign > 0.0f ? GraphY > DragStartY : GraphY < DragStartY;
			if (bOutward)
			{
				const float Distance = FMath::Abs(Limit - DragStartY);
				const float V = MixtormatScalarRampMath::ResistedTravel(Travel, Distance, true);
				Y = DragStartY + Sign * V;
			}
			else { Y = FMath::Clamp(GraphY, SoftYMin, SoftYMax); }
		}
		else
		{
			Y = FMath::Clamp(GraphY, CanonicalYMin, CanonicalYMax);
			if ((GraphY <= CanonicalYMin && (DragStartY > CanonicalYMin || GraphY < CanonicalYMin))
				|| (GraphY >= CanonicalYMax && (DragStartY < CanonicalYMax || GraphY > CanonicalYMax)))
			{
				EscapeArms[Index] = EEscapeArm::Canonical;
			}
		}
		Ramp.Points[Index].Y = Y;
	}
	bMoved = true;
}

void SMixtormatScalarRamp::OnRampEdited(const bool bInteractive)
{
	if (bAutoZoom) { FrameCurve(); }
	OnChanged.ExecuteIfBound(Ramp);
}

void SMixtormatScalarRamp::SetInterpolation(const int32 Index)
{
	Ramp.Interpolation = static_cast<EMixtormatScalarRampInterpolation>(Index);
}

FText SMixtormatScalarRamp::GetInterpolationLabel(const int32 Index) const
{
	switch (Index)
	{
	case 0: return FText::FromString(TEXT("Constant"));
	case 1: return FText::FromString(TEXT("Linear"));
	case 2: return FText::FromString(TEXT("Spline"));
	default: return FText::FromString(TEXT("B-Spline"));
	}
}

const FSlateBrush* SMixtormatScalarRamp::GetInterpolationIcon(const int32 Index) const
{
	switch (Index)
	{
	case 0: return MixtormatIcons::ScalarRampConstant();
	case 1: return MixtormatIcons::ScalarRampLinear();
	case 2: return MixtormatIcons::ScalarRampSpline();
	default: return MixtormatIcons::ScalarRampBSpline();
	}
}

void SMixtormatScalarRamp::PaintRampContent(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size) const
{
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;
	TArray<FVector2f> Curve;
	MixtormatScalarRampMath::SamplePolyline(Ramp, 96, Curve);
	FLinearColor Fill = Pal.Get(Mixtormat::EMixtormatColorRole::Accent);
	Fill.A *= 0.18f;
	const float Baseline = YToScreen(Size, 0.0f);
	for (int32 Index = 0; Index < Curve.Num() - 1; ++Index)
	{
		const FVector2f A = GraphToScreen(Size, Curve[Index].X, Curve[Index].Y);
		const FVector2f B = GraphToScreen(Size, Curve[Index + 1].X, Curve[Index + 1].Y);
		const float Top = FMath::Min(FMath::Min(A.Y, B.Y), Baseline);
		const float Bottom = FMath::Max(FMath::Max(A.Y, B.Y), Baseline);
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(FMath::Max(B.X - A.X, 1.0f), Bottom - Top), FSlateLayoutTransform(FVector2f(A.X, Top))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Fill);
	}
	for (int32 Index = 0; Index < Curve.Num() - 1; ++Index)
	{
		const TArray<FVector2f> Segment = {
			GraphToScreen(Size, Curve[Index].X, Curve[Index].Y),
			GraphToScreen(Size, Curve[Index + 1].X, Curve[Index + 1].Y) };
		FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Segment,
			ESlateDrawEffect::None, Pal.Get(Mixtormat::EMixtormatColorRole::Accent), true,
			FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampCurveThickness);
	}
}

void SMixtormatScalarRamp::PaintPointMarker(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size, const int32 Index,
	const bool bActive, const bool bHover) const
{
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;
	const FVector2f P = GraphToScreen(Size, Ramp.Points[Index].X, Ramp.Points[Index].Y);
	const float R = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampPointSize * 0.5f;
	const bool bSelected = Index == SelectedPoint;
	// Selected point gets a clear accent outline/ring so it stays visible while not being dragged.
	if (bSelected)
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(R * 2.0f + 4.0f, R * 2.0f + 4.0f), FSlateLayoutTransform(P - FVector2f(R + 2.0f, R + 2.0f))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			Pal.Get(Mixtormat::EMixtormatColorRole::Accent));
	}
	const FLinearColor C = bActive ? Pal.Get(Mixtormat::EMixtormatColorRole::Accent)
		: bHover ? Pal.Get(Mixtormat::EMixtormatColorRole::Text)
		: Pal.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(
		FVector2f(R * 2.0f, R * 2.0f), FSlateLayoutTransform(P - FVector2f(R, R))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
}

void SMixtormatScalarRamp::FrameCurve()
{
	const auto Bounds = MixtormatScalarRampMath::ComputeBounds(Ramp);
	if (Bounds.MinY >= CanonicalYMin && Bounds.MaxY <= CanonicalYMax)
	{
		ViewYMin = CanonicalYMin;
		ViewYMax = CanonicalYMax;
		Invalidate(EInvalidateWidgetReason::Paint);
		return;
	}
	const float Span = FMath::Max(Bounds.MaxY - Bounds.MinY, 0.1f);
	const float Padding = Span * FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding / FMath::Max(Height, 1.0f);
	ViewYMin = FMath::Max(ExtendedYMin, Bounds.MinY - Padding);
	ViewYMax = FMath::Min(ExtendedYMax, Bounds.MaxY + Padding);
	if (ViewYMax - ViewYMin < 0.1f)
	{
		ViewYMin = FMath::Max(ExtendedYMin, Bounds.MinY - 0.05f);
		ViewYMax = FMath::Min(ExtendedYMax, Bounds.MaxY + 0.05f);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SMixtormatScalarRamp::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		const FVector2D Size = Geometry.GetLocalSize();
		const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		const FVector2f Pos(Local);
		const int32 Hit = HitPoint(Size, Local);
		if (Hit != INDEX_NONE)
		{
			OnBeginInteractiveEdit.ExecuteIfBound();
			if (Hit == 0) { Ramp.Points[Hit].Y = CanonicalYMin; }
			else if (Hit == Ramp.Points.Num() - 1) { Ramp.Points[Hit].Y = CanonicalYMax; }
			else
			{
				const auto& A = Ramp.Points[Hit - 1];
				const auto& B = Ramp.Points[Hit + 1];
				Ramp.Points[Hit].Y = FMath::Lerp(A.Y, B.Y, (Ramp.Points[Hit].X - A.X) / (B.X - A.X));
			}
			EscapeArms[Hit] = EEscapeArm::None;
			FrameCurve();
			NotifyEdit(true);
			OnEndInteractiveEdit.ExecuteIfBound();
			return FReply::Handled();
		}
		TArray<FVector2f> Curve;
		MixtormatScalarRampMath::SamplePolyline(Ramp, 128, Curve);
		float Best = 9.0f;
		float InsertX = 0.0f;
		for (int32 Index = 0; Index < Curve.Num() - 1; ++Index)
		{
			const FVector2f A = GraphToScreen(Size, Curve[Index].X, Curve[Index].Y);
			const FVector2f B = GraphToScreen(Size, Curve[Index + 1].X, Curve[Index + 1].Y);
			const FVector2f D = B - A;
			const float T = FMath::Clamp(FVector2f::DotProduct(Pos - A, D) / FMath::Max(D.SizeSquared(), 1.0f), 0.0f, 1.0f);
			const float Dist = FVector2f::Distance(Pos, A + D * T);
			if (Dist < Best) { Best = Dist; InsertX = FMath::Lerp(Curve[Index].X, Curve[Index + 1].X, T); }
		}
		if (Best < 8.0f && Ramp.Points.Num() < FMixtormatScalarRamp::MaxPoints)
		{
			int32 Index = 0;
			while (Index < Ramp.Points.Num() && Ramp.Points[Index].X < InsertX) { ++Index; }
			Ramp.Points.Insert({InsertX, FMath::Clamp(MixtormatScalarRampMath::Evaluate(Ramp, InsertX), CanonicalYMin, CanonicalYMax)}, Index);
			EscapeArms.Insert(EEscapeArm::None, Index);
			OnBeginInteractiveEdit.ExecuteIfBound();
			NotifyEdit(true);
			OnEndInteractiveEdit.ExecuteIfBound();
			return FReply::Handled();
		}
		bDraggingFrame = true;
		bMoved = false;
		DragStartY = ViewYMin;
		DragStartX = ViewYMax;
		LastScreenY = Pos.Y;
		DragStartScreenY = Pos.Y;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return SMixtormatRampEditorBase::OnMouseButtonDown(Geometry, Event);
}

FReply SMixtormatScalarRamp::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (HasMouseCapture() && bDraggingFrame)
	{
		const FVector2f Pos(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		const float Dy = Pos.Y - LastScreenY;
		LastScreenY = Pos.Y;
		bMoved |= FMath::Abs(Pos.Y - DragStartScreenY) > 2.0f;
		bAutoZoom = false;
		const auto Bounds = MixtormatScalarRampMath::ComputeBounds(Ramp);
		const float Extent = Bounds.MaxY - Bounds.MinY;
		const float Factor = FMath::Exp(Dy * 0.006f);
		const float MinSpan = FMath::Max(Extent, 0.15f);
		const float Span = FMath::Clamp(FMath::Max(MinSpan, (ViewYMax - ViewYMin) * Factor),
			0.15f, ExtendedYMax - ExtendedYMin);
		const float CurveCenter = (Bounds.MinY + Bounds.MaxY) * 0.5f;
		ViewYMin = FMath::Max(ExtendedYMin, CurveCenter - Span * 0.5f);
		ViewYMax = FMath::Min(ExtendedYMax, CurveCenter + Span * 0.5f);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	return SMixtormatRampEditorBase::OnMouseMove(Geometry, Event);
}

FReply SMixtormatScalarRamp::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDraggingFrame
		&& (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::MiddleMouseButton))
	{
		bDraggingFrame = false;
		if (!bMoved) { bAutoZoom = true; FrameCurve(); }
		return FReply::Handled().ReleaseMouseCapture();
	}
	return SMixtormatRampEditorBase::OnMouseButtonUp(Geometry, Event);
}
