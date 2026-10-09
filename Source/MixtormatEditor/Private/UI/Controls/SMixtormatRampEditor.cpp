// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatRampEditor.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatRampEditorBase::Construct(const FArguments& InArgs)
{
	Height = InArgs._Height;
	DomainMin = InArgs._DomainMin;
	DomainMax = InArgs._DomainMax;
	OnBeginInteractiveEdit = InArgs._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = InArgs._OnEndInteractiveEdit;
	BuildLayout();
}

void SMixtormatRampEditorBase::BuildLayout()
{
	const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);
	const auto AddButton = [&Toolbar](const FSlateBrush* Brush, const FText& Tip,
		const FSimpleDelegate& Click, const TAttribute<bool>& Active, const float LeftGap)
	{
		Toolbar->AddSlot().AutoWidth().Padding(LeftGap, 0.0f)
		[
			SNew(SMixtormatIconButton).Icon(Brush)
			.Size(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize)
			.ToolTip(Tip).bActive(Active).OnClicked(Click)
		];
	};
	for (int32 Index = 0; Index < GetInterpolationCount(); ++Index)
	{
		AddButton(GetInterpolationIcon(Index), GetInterpolationLabel(Index),
			FSimpleDelegate::CreateLambda([this, Index]() { SelectInterpolation(Index); }),
			TAttribute<bool>::CreateLambda([this, Index]() { return GetInterpolation() == Index; }),
			Index == 0 ? 0.0f : FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconGap);
	}
	AddButton(MixtormatIcons::ScalarRampFrame(), FText::FromString(TEXT("Auto Zoom")),
		FSimpleDelegate::CreateLambda([this]() { FrameView(); }),
		TAttribute<bool>::CreateLambda([this]() { return true; }),
		MixtormatTokens::ScalarRampToolbarGroupGap);
	AddButton(MixtormatIcons::ScalarRampReset(), FText::FromString(TEXT("Reset Curve")),
		FSimpleDelegate::CreateLambda([this]() { ResetRamp(); }), false,
		FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconGap);

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

SMixtormatRampEditorBase::FGraphRect SMixtormatRampEditorBase::GetGraphRect(const FVector2D& Size) const
{
	const float Pad = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding;
	const float Toolbar = FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap;
	FGraphRect Rect;
	Rect.X0 = Pad;
	Rect.X1 = static_cast<float>(Size.X) - Pad;
	Rect.Y0 = Toolbar + Pad;
	// Prefer the authored graph height so chrome stacked under the ramp (preset strip, stop
	// row) cannot stretch the paint/hit band into those widgets.
	const float PreferredY1 = Toolbar + Pad + Height + Pad;
	const float SizeY1 = static_cast<float>(Size.Y) - Pad;
	Rect.Y1 = FMath::Min(PreferredY1, SizeY1);
	if (Rect.Y1 <= Rect.Y0 + 1.0f) { Rect.Y1 = SizeY1; }
	return Rect;
}

float SMixtormatRampEditorBase::XToScreen(const FVector2D& Size, const float X) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	return Rect.X0 + (X - DomainMin) / FMath::Max(DomainMax - DomainMin, 1.0e-4f) * (Rect.X1 - Rect.X0);
}

float SMixtormatRampEditorBase::ScreenToX(const FVector2D& Size, const float ScreenX) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float T = FMath::Clamp((ScreenX - Rect.X0) / FMath::Max(Rect.X1 - Rect.X0, 1.0f), 0.0f, 1.0f);
	return DomainMin + T * (DomainMax - DomainMin);
}

float SMixtormatRampEditorBase::YToScreen(const FVector2D& Size, const float Y) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float ViewMin = GetViewYMin(), ViewMax = GetViewYMax();
	return Rect.Y0 + (ViewMax - Y) / FMath::Max(ViewMax - ViewMin, 1.0e-4f) * (Rect.Y1 - Rect.Y0);
}

FVector2f SMixtormatRampEditorBase::GraphToScreen(const FVector2D& Size, const float X, const float Y) const
{
	return FVector2f(XToScreen(Size, X), YToScreen(Size, Y));
}

FVector2f SMixtormatRampEditorBase::ScreenToGraph(const FVector2D& Size, const FVector2D& Position) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float ViewMin = GetInputViewYMin(), ViewMax = GetInputViewYMax();
	const float Y = ViewMax - (static_cast<float>(Position.Y) - Rect.Y0)
		/ FMath::Max(Rect.Y1 - Rect.Y0, 1.0f) * (ViewMax - ViewMin);
	return FVector2f(ScreenToX(Size, static_cast<float>(Position.X)), Y);
}

int32 SMixtormatRampEditorBase::HitPoint(const FVector2D& Size, const FVector2D& Position) const
{
	// 2D hit testing against each marker's actual screen position (curve point or colour handle).
	const float Radius = MixtormatTokens::ScalarRampPointSize * 1.2f;
	int32 Best = INDEX_NONE;
	float BestDistSq = Radius * Radius;
	for (int32 Index = 0; Index < GetPointCount(); ++Index)
	{
		const FVector2f Screen = GetMarkerScreenPosition(Size, Index);
		const float DX = static_cast<float>(Position.X) - Screen.X;
		const float DY = static_cast<float>(Position.Y) - Screen.Y;
		const float DistSq = DX * DX + DY * DY;
		if (DistSq <= BestDistSq)
		{
			// Selected point wins on a tie so the user can re-grab the same stop without ambiguity.
			const bool bTie = FMath::IsNearlyEqual(DistSq, BestDistSq);
			if (bTie && Index == SelectedPoint) { Best = Index; BestDistSq = DistSq; }
			else if (!bTie) { Best = Index; BestDistSq = DistSq; }
		}
	}
	return Best;
}

void SMixtormatRampEditorBase::SwapPoints(const int32 IndexA, const int32 IndexB)
{
	if (IndexA == IndexB || IndexA < 0 || IndexB < 0
		|| IndexA >= GetPointCount() || IndexB >= GetPointCount()) { return; }
	// Swap the X payload.
	const float AX = GetPointX(IndexA);
	const float BX = GetPointX(IndexB);
	SetPointX(IndexA, BX);
	SetPointX(IndexB, AX);
	// Swap any parallel per-point state (e.g. EscapeArms on the scalar ramp).
	SwapPointState(IndexA, IndexB);
	// Update every index-based selection so the dragged payload stays selected across the swap.
	auto Follow = [IndexA, IndexB](int32& Index)
	{
		if (Index == IndexA) { Index = IndexB; }
		else if (Index == IndexB) { Index = IndexA; }
	};
	Follow(DragPoint);
	Follow(HoverPoint);
	Follow(SelectedPoint);
}

FVector2D SMixtormatRampEditorBase::ComputeDesiredSize(float) const
{
	return FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.RowFieldMinWidth * 2.0f,
		Height + FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampViewportPadding * 2.0f
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarHeight
		+ FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampToolbarGap);
}

void SMixtormatRampEditorBase::PaintGrid(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float X0 = Rect.X0, X1 = Rect.X1, Y0 = Rect.Y0, Y1 = Rect.Y1;
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;
	const FVector2f GraphSize(X1 - X0, Y1 - Y0);
	FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(GraphSize,
		FSlateLayoutTransform(FVector2f(X0, Y0))), FCoreStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None, Pal.Get(Mixtormat::EMixtormatColorRole::Ground));

	const float ViewMin = GetViewYMin(), ViewMax = GetViewYMax();
	if (ViewMin < 0.0f)
	{
		const float ZeroY = YToScreen(Size, 0.0f);
		const float Top = ViewMax <= 0.0f ? Y0 : FMath::Clamp(ZeroY, Y0, Y1);
		FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(
			FVector2f(GraphSize.X, Y1 - Top), FSlateLayoutTransform(FVector2f(X0, Top))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			Pal.Get(Mixtormat::EMixtormatColorRole::Shade));
	}
	if (ViewMax > 1.0f)
	{
		const float OneY = YToScreen(Size, 1.0f);
		const float Bottom = FMath::Min(Y1, OneY);
		FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(
			FVector2f(GraphSize.X, Bottom - Y0), FSlateLayoutTransform(FVector2f(X0, Y0))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			Pal.Get(Mixtormat::EMixtormatColorRole::Shade));
	}

	const FLinearColor Grid = Pal.Get(Mixtormat::EMixtormatColorRole::Hairline);
	FLinearColor Major = Grid;
	Major.A = FMath::Min(Major.A * 2.0f, 1.0f);
	for (int32 Index = 0; Index <= 4; ++Index)
	{
		const float X = X0 + GraphSize.X * static_cast<float>(Index) / 4.0f;
		const TArray<FVector2f> Line = { FVector2f(X, Y0), FVector2f(X, Y1) };
		FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Line,
			ESlateDrawEffect::None, Index == 0 || Index == 4 ? Major : Grid, false,
			Index == 0 || Index == 4 ? MixtormatTokens::ScalarRampMajorGridThickness
				: MixtormatTokens::ScalarRampGridThickness);
	}
	// Zero is the neutral line for a signed domain; emphasise it when the domain straddles zero.
	if (DomainMin < 0.0f && DomainMax > 0.0f)
	{
		const float ZeroX = XToScreen(Size, 0.0f);
		const TArray<FVector2f> Line = { FVector2f(ZeroX, Y0), FVector2f(ZeroX, Y1) };
		FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Line,
			ESlateDrawEffect::None, Major, false, MixtormatTokens::ScalarRampMajorGridThickness);
	}
	for (int32 Index = 0; Index <= 4; ++Index)
	{
		const float V = ViewMin + (ViewMax - ViewMin) * static_cast<float>(Index) / 4.0f;
		const float Y = YToScreen(Size, V);
		const bool bBoundary = Index == 0 || Index == 4;
		const TArray<FVector2f> Line = { FVector2f(X0, Y), FVector2f(X1, Y) };
		FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Line,
			ESlateDrawEffect::None, bBoundary ? Major : Grid, false,
			bBoundary ? MixtormatTokens::ScalarRampMajorGridThickness
				: MixtormatTokens::ScalarRampGridThickness);
	}
}

int32 SMixtormatRampEditorBase::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& Cull, FSlateWindowElementList& Elements, const int32 Layer,
	const FWidgetStyle& Style, const bool Enabled) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	PaintGrid(Elements, Layer, Geometry, Size);
	PaintRampContent(Elements, Layer + 3, Geometry, Size);
	for (int32 Index = 0; Index < GetPointCount(); ++Index)
	{
		PaintPointMarker(Elements, Layer + 5, Geometry, Size, Index,
			Index == DragPoint, Index == HoverPoint);
	}
	return SCompoundWidget::OnPaint(Args, Geometry, Cull, Elements, Layer + 6, Style, Enabled);
}

void SMixtormatRampEditorBase::NotifyEdit(bool bInteractive)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	OnRampEdited(bInteractive);
	if (!bInteractive)
	{
		OnBeginInteractiveEdit.ExecuteIfBound();
		OnEndInteractiveEdit.ExecuteIfBound();
	}
}

void SMixtormatRampEditorBase::SelectInterpolation(const int32 Index)
{
	if (GetInterpolation() == Index) { return; }
	OnBeginInteractiveEdit.ExecuteIfBound();
	SetInterpolation(Index);
	NotifyEdit(true);
	OnEndInteractiveEdit.ExecuteIfBound();
}

void SMixtormatRampEditorBase::ResetRamp()
{
	OnBeginInteractiveEdit.ExecuteIfBound();
	ResetPoints();
	FrameView();
	NotifyEdit(true);
	OnEndInteractiveEdit.ExecuteIfBound();
}

FReply SMixtormatRampEditorBase::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());

	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const int32 Hit = HitPoint(Size, Local);
		if (Hit > 0 && Hit < GetPointCount() - 1)
		{
			OnBeginInteractiveEdit.ExecuteIfBound();
			RemovePoint(Hit);
			NotifyPointRemoved(Hit);
			NotifyEdit(true);
			OnEndInteractiveEdit.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }

	int32 Hit = HitPoint(Size, Local);
	bool bCreated = false;
	if (Hit == INDEX_NONE && GetPointCount() < GetMaxPoints())
	{
		const FVector2f Graph = ScreenToGraph(Size, Local);
		OnBeginInteractiveEdit.ExecuteIfBound();
		Hit = InsertPointAt(Graph.X, Graph.Y);
		bCreated = true;
		NotifyPointInserted(Hit);
	}
	if (Hit == INDEX_NONE) { return FReply::Handled(); }
	DragPoint = Hit;
	SelectedPoint = Hit;
	bDragging = true;
	BeginPointDrag(Hit, bCreated, Geometry, Event);
	if (!bCreated) { OnBeginInteractiveEdit.ExecuteIfBound(); }
	else { NotifyEdit(true); }
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMixtormatRampEditorBase::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	if (!HasMouseCapture())
	{
		const int32 NewHover = HitPoint(Size, Local);
		if (NewHover != HoverPoint) { HoverPoint = NewHover; Invalidate(EInvalidateWidgetReason::Paint); }
		return FReply::Unhandled();
	}
	if (!bDragging || DragPoint < 0 || DragPoint >= GetPointCount()) { return FReply::Unhandled(); }
	const FVector2f Graph = ScreenToGraph(Size, Local);
	ApplyPointDrag(DragPoint, Graph.X, Graph.Y, FVector2f(Local));
	// Allow horizontal point crossing: when the dragged point passes a neighbour, swap the two
	// adjacent points and update every index-based state so the dragged payload stays selected.
	if (AllowsPointCrossing())
	{
		const float DraggedX = GetPointX(DragPoint);
		const int32 Count = GetPointCount();
		// Swap with the left neighbour when the dragged point has moved past it.
		if (DragPoint > 0 && !IsEndpointLocked(DragPoint - 1) && DraggedX < GetPointX(DragPoint - 1))
		{
			SwapPoints(DragPoint - 1, DragPoint);
		}
		// Swap with the right neighbour when the dragged point has moved past it.
		else if (DragPoint + 1 < Count && !IsEndpointLocked(DragPoint + 1) && DraggedX > GetPointX(DragPoint + 1))
		{
			SwapPoints(DragPoint, DragPoint + 1);
		}
	}
	NotifyEdit(true);
	return FReply::Handled();
}

FReply SMixtormatRampEditorBase::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	if (bDragging)
	{
		FinishPointDrag();
		bDragging = false;
		DragPoint = INDEX_NONE;
		OnEndInteractiveEdit.ExecuteIfBound();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SMixtormatRampEditorBase::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	if (bDragging)
	{
		FinishPointDrag();
		bDragging = false;
		DragPoint = INDEX_NONE;
		OnEndInteractiveEdit.ExecuteIfBound();
	}
	SCompoundWidget::OnMouseCaptureLost(Event);
}

FReply SMixtormatRampEditorBase::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::F && !Event.IsRepeat()) { FrameView(); return FReply::Handled(); }
	return FReply::Unhandled();
}
