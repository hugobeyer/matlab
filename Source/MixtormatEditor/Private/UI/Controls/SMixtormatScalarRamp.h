// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatScalarRamp.h"
#include "Style/MixtormatDesignTokens.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnMixtormatScalarRampChanged, const FMixtormatScalarRamp&);

class SMixtormatScalarRamp final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatScalarRamp)
		: _Height(MixtormatTokens::ScalarRampHeight)
		, _CanonicalXMin(0.0f)
		, _CanonicalXMax(1.0f)
		, _CanonicalYMin(0.0f)
		, _CanonicalYMax(1.0f)
		, _SoftYMin(-1.5f)
		, _SoftYMax(1.5f)
		, _ExtendedYMin(-3.0f)
		, _ExtendedYMax(3.0f)
	{}
		SLATE_ATTRIBUTE(FMixtormatScalarRamp, Ramp)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, CanonicalXMin)
		SLATE_ARGUMENT(float, CanonicalXMax)
		SLATE_ARGUMENT(float, CanonicalYMin)
		SLATE_ARGUMENT(float, CanonicalYMax)
		SLATE_ARGUMENT(float, SoftYMin)
		SLATE_ARGUMENT(float, SoftYMax)
		SLATE_ARGUMENT(float, ExtendedYMin)
		SLATE_ARGUMENT(float, ExtendedYMax)
		SLATE_EVENT(FOnMixtormatScalarRampChanged, OnChanged)
		SLATE_EVENT(FSimpleDelegate, OnBeginInteractiveEdit)
		SLATE_EVENT(FSimpleDelegate, OnEndInteractiveEdit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	enum class EEscapeArm : uint8 { None, Canonical, Hard };
	enum class EDragStage : uint8 { Locked, Canonical, Hard };
	FVector2f ToGraph(const FGeometry&, const FVector2D&) const;
	FVector2f ToScreen(const FVector2f&, const FVector2D&) const;
	int32 HitPoint(const FVector2f&, const FVector2D&) const;
	void FrameCurve();
	void NotifyEdit(bool bInteractive);
	void StartPointDrag(const FGeometry&, const FPointerEvent&, int32 PointIndex, bool bNewPoint);
	void FinishDrag();
	void SelectInterpolation(EMixtormatScalarRampInterpolation);
	void ResetCurve();

	FMixtormatScalarRamp Ramp;
	FOnMixtormatScalarRampChanged OnChanged;
	FSimpleDelegate OnBeginInteractiveEdit;
	FSimpleDelegate OnEndInteractiveEdit;
	float Height = MixtormatTokens::ScalarRampHeight;
	float CanonicalXMin = 0.0f;
	float CanonicalXMax = 1.0f;
	float CanonicalYMin = 0.0f;
	float CanonicalYMax = 1.0f;
	float SoftYMin = -1.5f;
	float SoftYMax = 1.5f;
	float ExtendedYMin = -3.0f;
	float ExtendedYMax = 3.0f;
	float ViewYMin = 0.0f;
	float ViewYMax = 1.0f;
	bool bAutoZoom = true;
	bool bDraggingPoint = false;
	bool bDraggingFrame = false;
	bool bMoved = false;
	bool bLockX = false;
	bool bLockY = false;
	int32 DragPoint = INDEX_NONE;
	int32 HoverPoint = INDEX_NONE;
	float DragStartX = 0.0f;
	float DragStartY = 0.0f;
	float DragViewYMin = 0.0f;
	float DragViewYMax = 1.0f;
	float DragStartScreenY = 0.0f;
	float LastScreenX = 0.0f;
	float LastScreenY = 0.0f;
	EDragStage DragStage = EDragStage::Locked;
	TArray<EEscapeArm> EscapeArms;
};
