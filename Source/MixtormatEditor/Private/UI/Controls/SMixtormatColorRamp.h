// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "UI/Controls/SMixtormatRampEditor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

DECLARE_DELEGATE_OneParam(FOnMixtormatColorRampChanged, const FMixtormatColorRamp&);

// The colour half of the shared ramp editor: the same points, domain and interaction as the scalar
// ramp, with an RGB payload and a gradient bar instead of a curve.
class SMixtormatColorRamp final : public SMixtormatRampEditorBase
{
public:
	SLATE_BEGIN_ARGS(SMixtormatColorRamp)
		: _Height(MixtormatTokens::ColorRampHeight)
		, _DomainMin(-1.0f)
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

	// Built-in presets, exposed so an owner (the inspector group header) can offer them as a
	// dropdown. Custom saved presets slot in here later by extending the index space.
	static int32 GetPresetCount();
	static FText GetPresetName(int32 PresetIndex);
	void ApplyPreset(int32 PresetIndex);

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;

protected:
	// ---- SMixtormatRampEditorBase -------------------------------------------------------------
	virtual int32 GetPointCount() const override { return Ramp.Stops.Num(); }
	virtual int32 GetMaxPoints() const override { return FMixtormatColorRamp::MaxStops; }
	virtual float GetPointX(int32 Index) const override { return Ramp.Stops[Index].X; }
	virtual void SetPointX(int32 Index, float X) override { Ramp.Stops[Index].X = X; }
	virtual int32 InsertPointAt(float X, float GraphY) override;
	virtual void RemovePoint(int32 Index) override;
	virtual void ResetPoints() override;
	virtual void ApplyPointDrag(int32 Index, float GraphX, float GraphY, const FVector2f& ScreenPos) override;
	virtual void SwapPointState(int32 IndexA, int32 IndexB) override;
	virtual void NotifyPointRemoved(int32 RemovedIndex) override;
	virtual void NotifyPointInserted(int32 InsertedIndex) override;
	virtual bool IsEndpointLocked(int32 Index) const override;
	virtual void OnRampEdited(bool bInteractive) override;
	virtual void PaintRampContent(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size) const override;
	virtual void PaintPointMarker(FSlateWindowElementList& Elements, int32 Layer,
		const FGeometry& Geometry, const FVector2D& Size, int32 Index, bool bActive, bool bHover) const override;
	virtual int32 GetInterpolationCount() const override { return 3; }
	virtual int32 GetInterpolation() const override { return static_cast<int32>(Ramp.Interpolation); }
	virtual void SetInterpolation(int32 Index) override;
	virtual FText GetInterpolationLabel(int32 Index) const override;
	virtual const FSlateBrush* GetInterpolationIcon(int32 Index) const override;
	virtual FVector2f GetMarkerScreenPosition(const FVector2D& Size, int32 Index) const override;

	// Screen Y of the colour-stop handle under the gradient bar (shared by paint and hit-test).
	float GetHandleScreenY(const FVector2D& Size) const;

private:
	void OpenStopPicker(int32 StopIndex);
	void SetSelectedStopColor(FLinearColor Color);
	void SetSelectedStopX(float X, bool bInteractive = false);
	FReply OnSwatchClicked();
	TSharedRef<SWidget> BuildSelectedStopRow();
	void RebuildChrome();

	TAttribute<FMixtormatColorRamp> RampAttribute;
	FMixtormatColorRamp Ramp;
	FOnMixtormatColorRampChanged OnChanged;
	TSharedPtr<SVerticalBox> ChromeBox;
	bool bPositionDragging = false;
};
