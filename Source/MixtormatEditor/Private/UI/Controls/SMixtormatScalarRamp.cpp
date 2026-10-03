// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatScalarRamp.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "MixtormatScalarRampMath.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatScalarRamp::Construct(const FArguments& Args)
{
	Ramp = Args._Ramp.Get(FMixtormatScalarRamp());
	Ramp.Sanitize();
	Height = Args._Height;
	CanonicalYMin = Args._CanonicalYMin;
	CanonicalYMax = Args._CanonicalYMax;
	SoftYMin = Args._SoftYMin;
	SoftYMax = Args._SoftYMax;
	ExtendedYMin = Args._ExtendedYMin;
	ExtendedYMax = Args._ExtendedYMax;
	OnChanged = Args._OnChanged;
	OnBeginInteractiveEdit = Args._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = Args._OnEndInteractiveEdit;
	EscapeArms.Init(EEscapeArm::None, Ramp.Points.Num());
	FrameCurve();

	const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);
	const auto AddButton = [this, &Toolbar](const FSlateBrush* Brush, const FText& Tip,
		const FSimpleDelegate& Click, const TAttribute<bool>& Active, const float LeftGap)
	{
		Toolbar->AddSlot().AutoWidth().Padding(LeftGap, 0.0f)
		[
			SNew(SMixtormatIconButton).Icon(Brush).Size(MixtormatTokens::ScalarRampIconSize)
			.ToolTip(Tip).bActive(Active).OnClicked(Click)
		];
	};
	AddButton(MixtormatIcons::ScalarRampConstant(), FText::FromString(TEXT("Constant")),
		FSimpleDelegate::CreateLambda([this]() { SelectInterpolation(EMixtormatScalarRampInterpolation::Constant); }),
		TAttribute<bool>::CreateLambda([this]() { return Ramp.Interpolation == EMixtormatScalarRampInterpolation::Constant; }), 0.0f);
	AddButton(MixtormatIcons::ScalarRampLinear(), FText::FromString(TEXT("Linear")),
		FSimpleDelegate::CreateLambda([this]() { SelectInterpolation(EMixtormatScalarRampInterpolation::Linear); }),
		TAttribute<bool>::CreateLambda([this]() { return Ramp.Interpolation == EMixtormatScalarRampInterpolation::Linear; }), MixtormatTokens::ScalarRampIconGap);
	AddButton(MixtormatIcons::ScalarRampSpline(), FText::FromString(TEXT("Spline")),
		FSimpleDelegate::CreateLambda([this]() { SelectInterpolation(EMixtormatScalarRampInterpolation::Spline); }),
		TAttribute<bool>::CreateLambda([this]() { return Ramp.Interpolation == EMixtormatScalarRampInterpolation::Spline; }), MixtormatTokens::ScalarRampIconGap);
	AddButton(MixtormatIcons::ScalarRampBSpline(), FText::FromString(TEXT("B-Spline")),
		FSimpleDelegate::CreateLambda([this]() { SelectInterpolation(EMixtormatScalarRampInterpolation::BSpline); }),
		TAttribute<bool>::CreateLambda([this]() { return Ramp.Interpolation == EMixtormatScalarRampInterpolation::BSpline; }), MixtormatTokens::ScalarRampIconGap);
	AddButton(MixtormatIcons::ScalarRampFrame(), FText::FromString(TEXT("Auto Zoom")),
		FSimpleDelegate::CreateLambda([this]() { bAutoZoom = true; FrameCurve(); }),
		TAttribute<bool>::CreateLambda([this]() { return bAutoZoom; }), MixtormatTokens::ScalarRampToolbarGroupGap);
	AddButton(MixtormatIcons::ScalarRampReset(), FText::FromString(TEXT("Reset Curve")),
		FSimpleDelegate::CreateLambda([this]() { ResetCurve(); }), false, MixtormatTokens::ScalarRampIconGap);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::ScalarRampToolbarGap)
		[
			SNew(SBox).HeightOverride(MixtormatTokens::ScalarRampToolbarHeight)[Toolbar]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SBox).HeightOverride(Height + MixtormatTokens::ScalarRampViewportPadding * 2.0f)]
	];
}

FVector2f SMixtormatScalarRamp::ToGraph(const FGeometry& Geometry, const FVector2D& Position) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	const float Pad = MixtormatTokens::ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X) - Pad;
	const float Y0 = MixtormatTokens::ScalarRampToolbarHeight + MixtormatTokens::ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y) - Pad;
	const float X = FMath::Clamp((static_cast<float>(Position.X) - X0) / FMath::Max(X1-X0, 1.0f), 0.0f, 1.0f);
	const float MapMin=bDraggingPoint?DragViewYMin:ViewYMin;
	const float MapMax=bDraggingPoint?DragViewYMax:ViewYMax;
	const float V = MapMax - (static_cast<float>(Position.Y) - Y0) / FMath::Max(Y1-Y0, 1.0f) * (MapMax-MapMin);
	return FVector2f(X, V);
}

FVector2f SMixtormatScalarRamp::ToScreen(const FVector2f& Point, const FVector2D& Size) const
{
	const float Pad = MixtormatTokens::ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X) - Pad;
	const float Y0 = MixtormatTokens::ScalarRampToolbarHeight + MixtormatTokens::ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y) - Pad;
	return FVector2f(X0 + Point.X * (X1-X0), Y0 + (ViewYMax-Point.Y) / FMath::Max(ViewYMax-ViewYMin, 1.0e-4f) * (Y1-Y0));
}

int32 SMixtormatScalarRamp::HitPoint(const FVector2f& Pos, const FVector2D& Size) const
{
	const float Radius = MixtormatTokens::ScalarRampPointSize * 1.3f;
	for (int32 I = Ramp.Points.Num()-1; I >= 0; --I)
	{
		const FVector2f Screen = ToScreen(FVector2f(Ramp.Points[I].X, Ramp.Points[I].Y), Size);
		if (FVector2f::Distance(Screen, Pos) <= Radius) { return I; }
	}
	return INDEX_NONE;
}

FVector2D SMixtormatScalarRamp::ComputeDesiredSize(float) const
{
	return FVector2D(MixtormatTokens::RowFieldMinWidth * 2.0f,
		Height + MixtormatTokens::ScalarRampViewportPadding * 2.0f
		+ MixtormatTokens::ScalarRampToolbarHeight + MixtormatTokens::ScalarRampToolbarGap);
}

int32 SMixtormatScalarRamp::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	const float Pad = MixtormatTokens::ScalarRampViewportPadding;
	const float X0 = Pad, X1 = static_cast<float>(Size.X)-Pad;
	const float Y0 = MixtormatTokens::ScalarRampToolbarHeight + MixtormatTokens::ScalarRampToolbarGap + Pad;
	const float Y1 = static_cast<float>(Size.Y)-Pad;
	const FLinearColor Bg = MixtormatPalette::ScalarRampBackground();
	const FVector2f GraphSize(X1-X0, Y1-Y0);
	FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(GraphSize,
		FSlateLayoutTransform(FVector2f(X0,Y0))), FCoreStyle::Get().GetBrush("WhiteBrush"),
		ESlateDrawEffect::None, Bg);
	const auto YScreen = [this,Y0,Y1](float Y) { return Y0+(ViewYMax-Y)/FMath::Max(ViewYMax-ViewYMin,1.0e-4f)*(Y1-Y0); };
	const float ZeroY=YScreen(0.0f), OneY=YScreen(1.0f);
	if (ViewYMin < 0.0f)
	{
		const float Top=ViewYMax<=0.0f?Y0:FMath::Clamp(ZeroY,Y0,Y1);
		FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2f(GraphSize.X,Y1-Top),FSlateLayoutTransform(FVector2f(X0,Top))),
			FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,MixtormatPalette::ScalarRampOutsideRangeBackground());
	}
	if (ViewYMax > 1.0f)
	{
		const float Bottom=FMath::Min(Y1,OneY);
		FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2f(GraphSize.X,Bottom-Y0),FSlateLayoutTransform(FVector2f(X0,Y0))),
			FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,MixtormatPalette::ScalarRampOutsideRangeBackground());
	}
	const FLinearColor Grid=MixtormatPalette::ScalarRampGrid(), Major=MixtormatPalette::ScalarRampMajorGrid();
	for (int32 I=0;I<=4;++I)
	{
		const float X=X0+GraphSize.X*static_cast<float>(I)/4.0f;
		const TArray<FVector2f> Line = { FVector2f(X,YScreen(1)), FVector2f(X,YScreen(0)) };
		FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Line,ESlateDrawEffect::None,
			I==0||I==4?Major:Grid,false,I==0||I==4?MixtormatTokens::ScalarRampMajorGridThickness:MixtormatTokens::ScalarRampGridThickness);
	}
	for (int32 I=0;I<=4;++I)
	{
		const float V=static_cast<float>(I)/4.0f, Y=YScreen(V);
		const bool bBoundary=I==0||I==4;
		const TArray<FVector2f> Line = { FVector2f(X0,Y), FVector2f(X1,Y) };
		FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Line,ESlateDrawEffect::None,
			bBoundary?Major:Grid,false,bBoundary?MixtormatTokens::ScalarRampMajorGridThickness:MixtormatTokens::ScalarRampGridThickness);
	}
	TArray<FVector2f> Curve; MixtormatScalarRampMath::SamplePolyline(Ramp,96,Curve);
	const FLinearColor Fill=MixtormatPalette::ScalarRampFill();
	const float Baseline=YScreen(0.0f);
	for (int32 I=0;I<Curve.Num()-1;++I)
	{
		const FVector2f A=ToScreen(Curve[I],Size),B=ToScreen(Curve[I+1],Size);
		const float Top=FMath::Min(FMath::Min(A.Y,B.Y),Baseline), Bottom=FMath::Max(FMath::Max(A.Y,B.Y),Baseline);
		FSlateDrawElement::MakeBox(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2f(FMath::Max(B.X-A.X,1.0f),Bottom-Top),FSlateLayoutTransform(FVector2f(A.X,Top))),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Fill);
	}
	for (int32 I=0;I<Curve.Num()-1;++I)
	{
		const TArray<FVector2f> Segment = { ToScreen(Curve[I],Size), ToScreen(Curve[I+1],Size) };
		FSlateDrawElement::MakeLines(Elements,Layer+4,Geometry.ToPaintGeometry(),Segment,ESlateDrawEffect::None,
			MixtormatPalette::ScalarRampCurve(),true,MixtormatTokens::ScalarRampCurveThickness);
	}
	for (int32 I=0;I<Ramp.Points.Num();++I)
	{
		const FVector2f P=ToScreen(FVector2f(Ramp.Points[I].X,Ramp.Points[I].Y),Size);
		const float R=MixtormatTokens::ScalarRampPointSize*0.5f;
		const FLinearColor C=I==DragPoint?MixtormatPalette::ScalarRampPointSelected():I==HoverPoint?MixtormatPalette::ScalarRampPointHover():MixtormatPalette::ScalarRampPoint();
		FSlateDrawElement::MakeBox(Elements,Layer+5,Geometry.ToPaintGeometry(FVector2f(R*2,R*2),FSlateLayoutTransform(P-FVector2f(R,R))),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C);
	}
	return SCompoundWidget::OnPaint(Args,Geometry,Cull,Elements,Layer+6,Style,Enabled);
}

void SMixtormatScalarRamp::FrameCurve()
{
	const auto Bounds=MixtormatScalarRampMath::ComputeBounds(Ramp);
	if (Bounds.MinY>=CanonicalYMin && Bounds.MaxY<=CanonicalYMax)
	{ ViewYMin=CanonicalYMin; ViewYMax=CanonicalYMax; Invalidate(EInvalidateWidgetReason::Paint); return; }
	const float Span=FMath::Max(Bounds.MaxY-Bounds.MinY,0.1f);
	const float Padding=Span*MixtormatTokens::ScalarRampViewportPadding/FMath::Max(Height,1.0f);
	ViewYMin=FMath::Max(ExtendedYMin,Bounds.MinY-Padding);
	ViewYMax=FMath::Min(ExtendedYMax,Bounds.MaxY+Padding);
	if (ViewYMax-ViewYMin<0.1f) { ViewYMin=FMath::Max(ExtendedYMin,Bounds.MinY-0.05f); ViewYMax=FMath::Min(ExtendedYMax,Bounds.MaxY+0.05f); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SMixtormatScalarRamp::NotifyEdit(bool bInteractive)
{
	if (bAutoZoom) { FrameCurve(); }
	Invalidate(EInvalidateWidgetReason::Paint);
	OnChanged.ExecuteIfBound(Ramp);
	if (!bInteractive) { OnBeginInteractiveEdit.ExecuteIfBound(); OnEndInteractiveEdit.ExecuteIfBound(); }
}

void SMixtormatScalarRamp::SelectInterpolation(EMixtormatScalarRampInterpolation Mode)
{
	if (Ramp.Interpolation==Mode) { return; }
	OnBeginInteractiveEdit.ExecuteIfBound(); Ramp.Interpolation=Mode; NotifyEdit(true); OnEndInteractiveEdit.ExecuteIfBound();
}

void SMixtormatScalarRamp::ResetCurve()
{
	OnBeginInteractiveEdit.ExecuteIfBound(); Ramp.ResetToIdentity(); EscapeArms.Init(EEscapeArm::None,Ramp.Points.Num()); bAutoZoom=true; FrameCurve(); NotifyEdit(true); OnEndInteractiveEdit.ExecuteIfBound();
}

void SMixtormatScalarRamp::StartPointDrag(const FGeometry& Geometry,const FPointerEvent& Event,int32 Index,bool bNew)
{
	DragPoint=Index; bDraggingPoint=true; bMoved=bNew; DragStartX=Ramp.Points[Index].X; DragStartY=Ramp.Points[Index].Y; DragViewYMin=ViewYMin; DragViewYMax=ViewYMax;
	const FVector2f Local=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()); DragStartScreenY=Local.Y; LastScreenX=Local.X; LastScreenY=Local.Y;
	bLockX=Event.IsControlDown(); bLockY=Event.IsShiftDown();
	EEscapeArm Arm=EscapeArms.IsValidIndex(Index)?EscapeArms[Index]:EEscapeArm::None;
	const bool bAtCanonicalBoundary=FMath::IsNearlyEqual(DragStartY,CanonicalYMin)||FMath::IsNearlyEqual(DragStartY,CanonicalYMax);
	const bool bAtSoftBoundary=FMath::IsNearlyEqual(DragStartY,SoftYMin)||FMath::IsNearlyEqual(DragStartY,SoftYMax);
	if ((Arm==EEscapeArm::Canonical&&!bAtCanonicalBoundary)||(Arm==EEscapeArm::Hard&&!bAtSoftBoundary)) { Arm=EEscapeArm::None; }
	if (EscapeArms.IsValidIndex(Index)&&!bLockY) { EscapeArms[Index]=EEscapeArm::None; }
	DragStage=bLockY?EDragStage::Locked:Arm==EEscapeArm::Canonical?EDragStage::Canonical:Arm==EEscapeArm::Hard?EDragStage::Hard
		: ((DragStartY<CanonicalYMin||DragStartY>CanonicalYMax)&&DragStartY>SoftYMin&&DragStartY<SoftYMax?EDragStage::Canonical:EDragStage::Locked);
	OnBeginInteractiveEdit.ExecuteIfBound();
	if (bNew) { NotifyEdit(true); }
}

FReply SMixtormatScalarRamp::OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	const FVector2f Pos(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	const FVector2D Size=Geometry.GetLocalSize(); const FVector2f Graph=ToGraph(Geometry,Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (Event.GetEffectingButton()==EKeys::RightMouseButton)
	{
		const int32 Hit=HitPoint(Pos,Size); if (Hit>0&&Hit<Ramp.Points.Num()-1)
		{
			OnBeginInteractiveEdit.ExecuteIfBound(); Ramp.Points.RemoveAt(Hit); EscapeArms.RemoveAt(Hit); NotifyEdit(true); OnEndInteractiveEdit.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	if (Event.GetEffectingButton()==EKeys::MiddleMouseButton)
	{
		const int32 Hit=HitPoint(Pos,Size);
		if (Hit!=INDEX_NONE)
		{
			OnBeginInteractiveEdit.ExecuteIfBound();
			if (Hit==0) { Ramp.Points[Hit].Y=CanonicalYMin; }
			else if (Hit==Ramp.Points.Num()-1) { Ramp.Points[Hit].Y=CanonicalYMax; }
			else { const auto& A=Ramp.Points[Hit-1]; const auto& B=Ramp.Points[Hit+1]; Ramp.Points[Hit].Y=FMath::Lerp(A.Y,B.Y,(Ramp.Points[Hit].X-A.X)/(B.X-A.X)); }
			EscapeArms[Hit]=EEscapeArm::None; FrameCurve(); NotifyEdit(true); OnEndInteractiveEdit.ExecuteIfBound(); return FReply::Handled();
		}
		TArray<FVector2f> Curve; MixtormatScalarRampMath::SamplePolyline(Ramp,128,Curve); float Best=9.0f; float InsertX=0.0f;
		for (int32 I=0;I<Curve.Num()-1;++I)
		{
			const FVector2f A=ToScreen(Curve[I],Size),B=ToScreen(Curve[I+1],Size),D=B-A;
			const float T=FMath::Clamp(FVector2f::DotProduct(Pos-A,D)/FMath::Max(D.SizeSquared(),1.0f),0.0f,1.0f);
			const float Dist=FVector2f::Distance(Pos,A+D*T); if (Dist<Best) { Best=Dist; InsertX=FMath::Lerp(Curve[I].X,Curve[I+1].X,T); }
		}
		if (Best<8.0f&&Ramp.Points.Num()<FMixtormatScalarRamp::MaxPoints)
		{
			int32 Index=0; while (Index<Ramp.Points.Num()&&Ramp.Points[Index].X<InsertX) ++Index;
			Ramp.Points.Insert({InsertX,FMath::Clamp(MixtormatScalarRampMath::Evaluate(Ramp,InsertX),CanonicalYMin,CanonicalYMax)},Index); EscapeArms.Insert(EEscapeArm::None,Index);
			OnBeginInteractiveEdit.ExecuteIfBound(); NotifyEdit(true); OnEndInteractiveEdit.ExecuteIfBound(); return FReply::Handled();
		}
		bDraggingFrame=true; bMoved=false; DragStartY=ViewYMin; DragStartX=ViewYMax; LastScreenY=Pos.Y; DragStartScreenY=Pos.Y; return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	if (Event.GetEffectingButton()!=EKeys::LeftMouseButton) { return FReply::Unhandled(); }
	int32 Hit=HitPoint(Pos,Size);
	bool bCreatedPoint=false;
	if (Hit==INDEX_NONE&&Ramp.Points.Num()<FMixtormatScalarRamp::MaxPoints)
	{
		int32 Insert=1; while (Insert<Ramp.Points.Num()-1&&Ramp.Points[Insert].X<Graph.X) ++Insert;
		const float X=FMath::Clamp(Graph.X,Ramp.Points[Insert-1].X+0.001f,Ramp.Points[Insert].X-0.001f);
		Ramp.Points.Insert({X,FMath::Clamp(Graph.Y,CanonicalYMin,CanonicalYMax)},Insert); EscapeArms.Insert(EEscapeArm::None,Insert); Hit=Insert; bCreatedPoint=true;
	}
	if (Hit==INDEX_NONE) { return FReply::Handled(); }
	StartPointDrag(Geometry,Event,Hit,bCreatedPoint); return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMixtormatScalarRamp::OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
	if (!HasMouseCapture())
	{
		const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		const int32 NewHover=HitPoint(Local,Geometry.GetLocalSize());
		if (NewHover!=HoverPoint) { HoverPoint=NewHover; Invalidate(EInvalidateWidgetReason::Paint); }
		return FReply::Unhandled();
	}
	const FVector2f Pos(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (bDraggingFrame)
	{
		const float Dy=Pos.Y-LastScreenY; LastScreenY=Pos.Y; bMoved |= FMath::Abs(Pos.Y-DragStartScreenY)>2.0f;
		bAutoZoom=false;
		const auto Bounds=MixtormatScalarRampMath::ComputeBounds(Ramp);
		const float Extent=Bounds.MaxY-Bounds.MinY;
		const float Factor=FMath::Exp(Dy*0.006f);
		const float MinSpan=FMath::Max(Extent,0.15f);
		const float Span=FMath::Clamp(FMath::Max(MinSpan,(ViewYMax-ViewYMin)*Factor),
			0.15f,ExtendedYMax-ExtendedYMin);
		const float CurveCenter=(Bounds.MinY+Bounds.MaxY)*0.5f;
		ViewYMin=FMath::Max(ExtendedYMin,CurveCenter-Span*0.5f);
		ViewYMax=FMath::Min(ExtendedYMax,CurveCenter+Span*0.5f);
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	if (!bDraggingPoint||!Ramp.Points.IsValidIndex(DragPoint)) { return FReply::Unhandled(); }
	const FVector2f Graph=ToGraph(Geometry,Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));

	if (!bLockX&&DragPoint>0&&DragPoint<Ramp.Points.Num()-1)
	{
		Ramp.Points[DragPoint].X=FMath::Clamp(Graph.X,Ramp.Points[DragPoint-1].X+0.001f,Ramp.Points[DragPoint+1].X-0.001f);
	}
	const float Travel=FMath::Abs(Pos.Y-DragStartScreenY);
	if (!bLockY)
	{
		float Y=FMath::Clamp(Graph.Y,CanonicalYMin,CanonicalYMax);
		if (DragStage==EDragStage::Canonical)
		{
			const float Sign=FMath::IsNearlyEqual(DragStartY,CanonicalYMin)?-1.0f
				:FMath::IsNearlyEqual(DragStartY,CanonicalYMax)?1.0f:(DragStartY>0.0f?1.0f:-1.0f);
			const float Limit=Sign>0.0f?SoftYMax:SoftYMin;
			const bool bOutward=Sign>0.0f?Graph.Y>DragStartY:Graph.Y<DragStartY;
			if (bOutward)
			{
				const float Distance=FMath::Abs(Limit-DragStartY);
				const float V=MixtormatScalarRampMath::ResistedTravel(Travel,Distance);
				Y=DragStartY+Sign*V;
				if (V>=Distance) { Y=Limit; EscapeArms[DragPoint]=EEscapeArm::Hard; }
			}
			else { Y=FMath::Clamp(Graph.Y,SoftYMin,SoftYMax); }
		}
		else if (DragStage==EDragStage::Hard)
		{
			const float Sign=DragStartY>=0.0f?1.0f:-1.0f;
			const float Limit=Sign>0.0f?ExtendedYMax:ExtendedYMin;
			const bool bOutward=Sign>0.0f?Graph.Y>DragStartY:Graph.Y<DragStartY;
			if (bOutward)
			{
				const float Distance=FMath::Abs(Limit-DragStartY);
				const float V=MixtormatScalarRampMath::ResistedTravel(Travel,Distance,true);
				Y=DragStartY+Sign*V;
			}
			else { Y=FMath::Clamp(Graph.Y,SoftYMin,SoftYMax); }
		}
		else
		{
			Y=FMath::Clamp(Graph.Y,CanonicalYMin,CanonicalYMax);
			if ((Graph.Y<=CanonicalYMin&&(DragStartY>CanonicalYMin||Graph.Y<CanonicalYMin))
				||(Graph.Y>=CanonicalYMax&&(DragStartY<CanonicalYMax||Graph.Y>CanonicalYMax)))
						{
							EscapeArms[DragPoint]=EEscapeArm::Canonical;
						}
		}
		Ramp.Points[DragPoint].Y=Y;
	}
	bMoved=true; NotifyEdit(true); return FReply::Handled();
}

FReply SMixtormatScalarRamp::OnMouseButtonUp(const FGeometry&,const FPointerEvent& Event)
{
	if (Event.GetEffectingButton()!=EKeys::LeftMouseButton&&Event.GetEffectingButton()!=EKeys::MiddleMouseButton) { return FReply::Unhandled(); }
	if (bDraggingPoint) { FinishDrag(); return FReply::Handled().ReleaseMouseCapture(); }
	if (bDraggingFrame) { bDraggingFrame=false; if (!bMoved) { bAutoZoom=true; FrameCurve(); } return FReply::Handled().ReleaseMouseCapture(); }
	return FReply::Unhandled();
}

void SMixtormatScalarRamp::FinishDrag()
{
	if (bDraggingPoint)
	{
		if (EscapeArms.IsValidIndex(DragPoint))
		{
			const float Y=Ramp.Points[DragPoint].Y;
			const bool bArmStillAtBoundary=EscapeArms[DragPoint]==EEscapeArm::Canonical
				?FMath::IsNearlyEqual(Y,CanonicalYMin)||FMath::IsNearlyEqual(Y,CanonicalYMax)
				:EscapeArms[DragPoint]==EEscapeArm::Hard
					?(FMath::IsNearlyEqual(Y,SoftYMin)||FMath::IsNearlyEqual(Y,SoftYMax)):true;
			if (!bArmStillAtBoundary) { EscapeArms[DragPoint]=EEscapeArm::None; }
		}
		bDraggingPoint=false; DragPoint=INDEX_NONE; OnEndInteractiveEdit.ExecuteIfBound();
	}
}

void SMixtormatScalarRamp::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	FinishDrag(); bDraggingFrame=false; SCompoundWidget::OnMouseCaptureLost(Event);
}

FReply SMixtormatScalarRamp::OnKeyDown(const FGeometry&,const FKeyEvent& Event)
{
	if (Event.GetKey()==EKeys::F&&!Event.IsRepeat()) { FrameCurve(); return FReply::Handled(); }
	return FReply::Unhandled();
}
