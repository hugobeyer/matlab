// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "Style/MixtormatDesignTokens.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnMixtormatColorRampChanged, const FMixtormatColorRamp&);

// A reusable colour-ramp editor: a gradient bar with draggable stops. The X domain is authored by
// the consumer, so the same widget serves a 0..1 mask ramp and a -1..1 signed height ramp.
class SMixtormatColorRamp final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatColorRamp)
		: _Height(MixtormatTokens::ScalarRampHeight)
		, _DomainMin(0.0f)
		, _DomainMax(1.0f)
	{}
		SLATE_ATTRIBUTE(FMixtormatColorRamp, Ramp)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(float, DomainMin)
		SLATE_ARGUMENT(float, DomainMax)
		SLATE_EVENT(FOnMixtormatColorRampChanged, OnChanged)
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
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	float XToScreen(const FVector2D& Size, float X) const;
	float ScreenToX(const FVector2D& Size, float ScreenX) const;
	int32 HitStop(const FVector2D& Size, const FVector2D& Position) const;
	void NotifyEdit(bool bInteractive);
	void OpenStopPicker(int32 StopIndex);
	void CycleInterpolation();
	void ResetRamp();

	FMixtormatColorRamp Ramp;
	FOnMixtormatColorRampChanged OnChanged;
	FSimpleDelegate OnBeginInteractiveEdit;
	FSimpleDelegate OnEndInteractiveEdit;
	float Height = MixtormatTokens::ScalarRampHeight;
	float DomainMin = 0.0f;
	float DomainMax = 1.0f;
	bool bDragging = false;
	bool bMoved = false;
	int32 DragStop = INDEX_NONE;
	int32 HoverStop = INDEX_NONE;
};
