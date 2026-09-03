#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableText;

DECLARE_DELEGATE_OneParam(FMixtormatOnSliderValueChanged, double);

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
	{}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ATTRIBUTE(double, Value)
		// Drag range. Typed entry deliberately is not clamped to it: the inspector constrains
		// the scrub visually while a typed value reaches the shader intact, which is the rule
		// the erosion work established.
		SLATE_ARGUMENT(double, MinValue)
		SLATE_ARGUMENT(double, MaxValue)
		SLATE_ARGUMENT(double, DefaultValue)
		// Snap step, used when Ctrl is held. 0 disables snapping.
		SLATE_ARGUMENT(double, Delta)
		SLATE_ARGUMENT(int32, Precision)
		SLATE_ARGUMENT(bool, bInteger)
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
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	double GetValue() const;
	FString FormatValue(double Value) const;
	void CommitValue(double Value, bool bClampToRange);
	void BeginTextEntry();
	void EndTextEntry();
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitType);

	FText Label;
	TAttribute<double> ValueAttribute;
	double MinValue = 0.0;
	double MaxValue = 1.0;
	double DefaultValue = 0.0;
	double Delta = 0.0;
	int32 Precision = 3;
	bool bInteger = false;

	FMixtormatOnSliderValueChanged OnValueChanged;
	FSimpleDelegate OnReset;
	FSimpleDelegate OnBeginDrag;
	FSimpleDelegate OnEndDrag;

	TSharedPtr<SEditableText> EntryWidget;
	bool bEditing = false;
	bool bDragging = false;
	bool bMovedPastThreshold = false;
	double DragStartValue = 0.0;
	float DragStartX = 0.0f;
};
