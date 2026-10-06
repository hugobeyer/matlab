// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatScalarRamp.h"
#include "UI/Controls/SMixtormatRampEditor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

DECLARE_DELEGATE_OneParam(FOnMixtormatScalarRampChanged, const FMixtormatScalarRamp&);

// The scalar half of the shared ramp editor: the same points, domain and interaction as the colour
// ramp, with a float payload and a Y axis the curve can be dragged and zoomed along.
class SMixtormatScalarRamp final : public SMixtormatRampEditorBase
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

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;
	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent&) override;

protected:
	// ---- SMixtormatRampEditorBase -------------------------------------------------------------
	virtual int32 GetPointCount() const override { return Ramp.Points.Num(); }
	virtual int32 GetMaxPoints() const override { return FMixtormatScalarRamp::MaxPoints; }
	virtual float GetPointX(int32 Index) const override { return Ramp.Points[Index].X; }
	virtual void SetPointX(int32 Index, float X) override { Ramp.Points[Index].X = X; }
	virtual int32 InsertPointAt(float X, float GraphY) override;
	virtual void RemovePoint(int32 Index) override;
	virtual void ResetPoints() override;
	virtual void FinishPointDrag() override;
	virtual void BeginPointDrag(int32 Index, bool bCreated, const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void ApplyPointDrag(int32 Index, float GraphX, float GraphY, const FVector2f& ScreenPos) override;
	virtual void OnRampEdited(bool bInteractive) override;
	virtual void PaintRampContent(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size) const override;
	virtual void PaintPointMarker(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size, int32 Index, bool bActive, bool bHover) const override;
	virtual int32 GetInterpolationCount() const override { return 4; }
	virtual int32 GetInterpolation() const override { return static_cast<int32>(Ramp.Interpolation); }
	virtual void SetInterpolation(int32 Index) override;
	virtual FText GetInterpolationLabel(int32 Index) const override;
	virtual const FSlateBrush* GetInterpolationIcon(int32 Index) const override;
	virtual float GetViewYMin() const override { return ViewYMin; }
	virtual float GetViewYMax() const override { return ViewYMax; }
	virtual float GetInputViewYMin() const override { return bDragging ? DragViewYMin : ViewYMin; }
	virtual float GetInputViewYMax() const override { return bDragging ? DragViewYMax : ViewYMax; }
	virtual void FrameView() override { bAutoZoom = true; FrameCurve(); }

private:
	enum class EEscapeArm : uint8 { None, Canonical, Hard };
	enum class EDragStage : uint8 { Locked, Canonical, Hard };

	void FrameCurve();
	void FinishDrag();

	FMixtormatScalarRamp Ramp;
	FOnMixtormatScalarRampChanged OnChanged;
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
	bool bDraggingFrame = false;
	bool bMoved = false;
	bool bLockX = false;
	bool bLockY = false;
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
