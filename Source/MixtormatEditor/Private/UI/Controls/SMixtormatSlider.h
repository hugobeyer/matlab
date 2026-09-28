// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableText;
class FMixtormatEntryCommit;

DECLARE_DELEGATE_OneParam(FMixtormatOnSliderValueChanged, double);

// Soft-range policy handed through the row helpers: whether dragging past an end may grow
// the range, and the legal bounds that growth stops at.
struct FMixtormatSliderRangeOptions
{
	TAttribute<bool> bExpandable = false;
	TAttribute<double> HardMin = -UE_BIG_NUMBER;
	TAttribute<double> HardMax = UE_BIG_NUMBER;
};

// One-row value control in the Blender idiom: a single bar carrying the label on the left,
// the value on the right, and a fill showing where the value sits in its range. Drag to
// scrub, click without dragging to type, middle-click to reset.
//
// It replaces a label plus SNumericEntryBox plus reset wrapper, which is what made the
// inspector rows uneven -- every row now has identical geometry regardless of what it edits.
//
// Values are carried as double with an integer flag rather than templated, because the
// inspector mixes float and int32 rows in the same columns and templating them would double
// the helper surface for no gain.
class SMixtormatSlider final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSlider)
		: _Label()
		, _Value(0.0)
		, _MinValue(0.0)
		, _MaxValue(1.0)
		, _DefaultValue(0.0)
		, _Delta(0.0)
		, _Precision(3)
		, _bInteger(false)
		, _ExpandableRange(false)
		, _HardMinValue(-UE_BIG_NUMBER)
		, _HardMaxValue(UE_BIG_NUMBER)
	{}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(double, Value)
		// Drag range. Typed entry deliberately is not clamped to it: the inspector constrains
		// the scrub visually while a typed value reaches the shader intact, which is the rule
		// the erosion work established. Attributes rather than arguments so a migrated row can
		// resolve its range from the parameter UI metadata -- including a developer session
		// override -- on every paint instead of freezing it at construct time, when nothing is
		// selected yet.
		SLATE_ATTRIBUTE(double, MinValue)
		SLATE_ATTRIBUTE(double, MaxValue)
		// Attribute like the range: the modified-value stripe and the reset comparison must
		// resolve the SAME default the row's reset delegate uses, not a stale literal.
		SLATE_ATTRIBUTE(double, DefaultValue)
		// Snap step, used when Ctrl is held. 0 disables snapping. Attribute, same reason.
		SLATE_ATTRIBUTE(double, Delta)
		SLATE_ARGUMENT(int32, Precision)
		SLATE_ARGUMENT(bool, bInteger)
		// Soft range. Min/Max are the initial visual range. A drag stops at the current end;
		// pushing against it and releasing grows the range by its own span (up to the hard
		// bounds below), so the next drag has room. With no room left it just stays clamped.
		// Attribute: a parameter row learns its policy only once something is selected.
		SLATE_ATTRIBUTE(bool, ExpandableRange)
		// Back-end clamp: drags, typed values and range growth all stop here. Unbounded by
		// default. Editor-side only; never reaches a shader.
		SLATE_ATTRIBUTE(double, HardMinValue)
		SLATE_ATTRIBUTE(double, HardMaxValue)
		SLATE_ATTRIBUTE(FText, ToolTip)
		SLATE_EVENT(FMixtormatOnSliderValueChanged, OnValueChanged)
		SLATE_EVENT(FSimpleDelegate, OnReset)
		// Fired around a scrub so the owner can drop preview quality and defer undo history
		// for its duration.
		SLATE_EVENT(FSimpleDelegate, OnBeginDrag)
		SLATE_EVENT(FSimpleDelegate, OnEndDrag)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Also invoked by the owner's hover + Backspace handler, so the binding registry keeps
	// working for converted rows.
	void ResetToDefault();


	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	double GetValue() const;
	FString FormatValue(double Value) const;
	void CommitValue(double Value, bool bClampToRange);
	FReply ApplyDrag(float Delta, float Width, const FPointerEvent& MouseEvent);
	// The range the bar draws and scrubs over: the soft range, widened by any expansion and,
	// for an expandable slider, by a value typed outside it.
	void GetDisplayRange(double& OutMin, double& OutMax) const;
	void BeginTextEntry();
	void EndTextEntry();
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitType);


	TAttribute<FText> LabelAttribute;
	TAttribute<double> ValueAttribute;
	TAttribute<double> MinValueAttribute;
	TAttribute<double> MaxValueAttribute;
	TAttribute<double> DefaultValueAttribute;
	TAttribute<double> DeltaAttribute;
	int32 Precision = 3;
	bool bInteger = false;
	TAttribute<bool> ExpandableAttribute;
	TAttribute<double> HardMinAttribute;
	TAttribute<double> HardMaxAttribute;
	// Grown ends of the soft range. Kept after the drag so the bar stays scaled while the
	// value lives out there; dropped when a drag starts back inside the soft range.
	TOptional<double> ExpandedMin;
	TOptional<double> ExpandedMax;
	// Set while a drag is held against an end that still has room; applied on release.
	bool bPushedLow = false;
	bool bPushedHigh = false;
	// Grows the pushed ends on release, or drops an old expansion once the value is back
	// strictly inside the soft range.
	void FinishDragRange();

	FMixtormatOnSliderValueChanged OnValueChanged;
	FSimpleDelegate OnReset;
	FSimpleDelegate OnBeginDrag;
	FSimpleDelegate OnEndDrag;

	TSharedPtr<SEditableText> EntryWidget;
	bool bEditing = false;
	// Shared typed-entry rules (Enter/Tab/focus loss/left click accept; Escape/right click cancel).
	TSharedPtr<FMixtormatEntryCommit> EntrySession;
	bool bDragging = false;
	bool bMovedPastThreshold = false;
	double DragStartValue = 0.0;
	// Unrounded, range-clamped scrub value. Accumulated per move so Shift can change the rate
	// mid-drag without jumping, and clamped as it goes so reversing at a limit responds at once.
	double DragValue = 0.0;
	// Total travel before the threshold, in Slate units.
	float DragTravel = 0.0f;
	// Where the hidden cursor is put back on release.
	FIntPoint DragStartScreen = FIntPoint::ZeroValue;
};
