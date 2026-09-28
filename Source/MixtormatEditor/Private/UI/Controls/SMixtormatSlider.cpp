// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatSlider.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/Controls/MixtormatEntryCommit.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Primitives/MixtormatGradientPainter.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SEditableText.h"

#define LOCTEXT_NAMESPACE "Mixtormat"


void SMixtormatSlider::Construct(const FArguments& InArgs)
{
	LabelAttribute = InArgs._Label;
	ValueAttribute = InArgs._Value;
	MinValueAttribute = InArgs._MinValue;
	MaxValueAttribute = InArgs._MaxValue;
	DefaultValueAttribute = InArgs._DefaultValue;
	DeltaAttribute = InArgs._Delta;
	Precision = InArgs._Precision;
	bInteger = InArgs._bInteger;
	ExpandableAttribute = InArgs._ExpandableRange;
	HardMinAttribute = InArgs._HardMinValue;
	HardMaxAttribute = InArgs._HardMaxValue;
	OnValueChanged = InArgs._OnValueChanged;
	OnReset = InArgs._OnReset;
	OnBeginDrag = InArgs._OnBeginDrag;
	OnEndDrag = InArgs._OnEndDrag;
	EntrySession = MakeShared<FMixtormatEntryCommit>();

	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}
	else
	{
		SetToolTipText(LOCTEXT(
			"SliderHint",
			"Drag to adjust · click to type · Shift fine · Ctrl snap · MMB or hover + Backspace to reset"));
	}

	const FEditableTextBoxStyle& EntryStyle =
		FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.ValueSlider.Entry"));

	ChildSlot
	.Padding(FMargin(MixtormatTokens::RowTextInset, 0.0f))
	.VAlign(VAlign_Center)
	[
		SAssignNew(EntryWidget, SEditableText)
		.Font(EntryStyle.TextStyle.Font)
		.ColorAndOpacity(EntryStyle.ForegroundColor)
		.SelectAllTextWhenFocused(true)
		.ClearKeyboardFocusOnCommit(true)
		.RevertTextOnEscape(true)
		.Visibility(EVisibility::Collapsed)
		.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& KeyEvent)
		{
			return EntrySession->HandleKeyDown(Geometry, KeyEvent);
		})
		.OnTextCommitted(this, &SMixtormatSlider::HandleTextCommitted)
	];
}

double SMixtormatSlider::GetValue() const
{
	return ValueAttribute.Get(0.0);
}

FString SMixtormatSlider::FormatValue(const double Value) const
{
	if (bInteger)
	{
		return FString::Printf(TEXT("%d"), FMath::RoundToInt(Value));
	}
	return FString::Printf(TEXT("%.*f"), FMath::Clamp(Precision, 0, 6), Value);
}

void SMixtormatSlider::CommitValue(double Value, const bool bClampToRange)
{
	// Dragging stays inside the range; a typed value passes through untouched, so the
	// inspector constrains the scrub visually without constraining what reaches the shader.
	if (bClampToRange)
	{
		Value = FMath::Clamp(Value, MinValueAttribute.Get(0.0), MaxValueAttribute.Get(1.0));
	}
	// The back-end clamp holds for typed values too; the visual range does not.
	Value = FMath::Clamp(Value, HardMinAttribute.Get(-UE_BIG_NUMBER), HardMaxAttribute.Get(UE_BIG_NUMBER));
	if (bInteger)
	{
		Value = FMath::RoundToDouble(Value);
	}
	OnValueChanged.ExecuteIfBound(Value);
}

void SMixtormatSlider::ResetToDefault()
{
	// A reset returns to the authored range, not the grown one.
	ExpandedMin.Reset();
	ExpandedMax.Reset();
	OnReset.ExecuteIfBound();
}

void SMixtormatSlider::BeginTextEntry()
{
	if (bEditing || !EntryWidget.IsValid())
	{
		return;
	}
	bEditing = true;
	EntryWidget->SetVisibility(EVisibility::Visible);
	EntryWidget->SetText(FText::FromString(FormatValue(GetValue())));
	FSlateApplication::Get().SetKeyboardFocus(EntryWidget, EFocusCause::SetDirectly);
	// Weak: a commit can rebuild the panel and destroy this slider before the session returns.
	const TWeakPtr<SMixtormatSlider> WeakSelf = StaticCastSharedRef<SMixtormatSlider>(AsShared());
	EntrySession->Begin(EntryWidget.ToSharedRef(), [WeakSelf]()
	{
		if (const TSharedPtr<SMixtormatSlider> Self = WeakSelf.Pin())
		{
			Self->EndTextEntry();
		}
	});
}

void SMixtormatSlider::EndTextEntry()
{
	if (!bEditing)
	{
		return;
	}
	bEditing = false;
	if (EntryWidget.IsValid())
	{
		EntryWidget->SetVisibility(EVisibility::Collapsed);
	}
}

void SMixtormatSlider::HandleTextCommitted(const FText& Text, const ETextCommit::Type CommitType)
{
	// Every commit accepts -- Enter, Tab, focus moved, focus cleared by leaving the window or
	// clicking elsewhere -- unless Escape or a right click asked to cancel first.
	const bool bCancelled = EntrySession->Finish();
	if (!bEditing)
	{
		return;
	}
	EndTextEntry();
	if (bCancelled)
	{
		return;
	}
	double Parsed = 0.0;
	if (LexTryParseString(Parsed, *Text.ToString()))
	{
		CommitValue(Parsed, false);
	}
}

FVector2D SMixtormatSlider::ComputeDesiredSize(float) const
{
	return FVector2D(MixtormatTokens::RowFieldMinWidth, MixtormatTokens::RowHeight);
}

FCursorReply SMixtormatSlider::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return bEditing
		? FCursorReply::Cursor(EMouseCursor::TextEditBeam)
		: FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
}

FReply SMixtormatSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		ResetToDefault();
		return FReply::Handled();
	}
	if (bEditing || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bDragging = true;
	bMovedPastThreshold = false;
	DragStartValue = GetValue();
	DragValue = DragStartValue;
	bPushedLow = false;
	bPushedHigh = false;
	DragTravel = 0.0f;
	const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
	DragStartScreen = FIntPoint(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));

	// Capturing is what makes the owner's interactive-edit check see the scrub, so the
	// preview drops to its drag resolution and undo history is deferred. High-precision
	// movement hides the cursor and reports raw deltas, so a scrub is never stopped by the
	// screen edge.
	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.UseHighPrecisionMouseMovement(SharedThis(this));
}

FReply SMixtormatSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	// Raw screen pixels to Slate units, so DPI scale does not change the rate.
	const float Scale = FMath::Max(MyGeometry.Scale, UE_KINDA_SMALL_NUMBER);
	const float Delta = static_cast<float>(MouseEvent.GetCursorDelta().X) / Scale;
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);
	if (!bMovedPastThreshold)
	{
		DragTravel += Delta;
		if (FMath::Abs(DragTravel) < MixtormatTokens::DragThreshold)
		{
			return FReply::Handled();
		}
		bMovedPastThreshold = true;
		OnBeginDrag.ExecuteIfBound();
		// The threshold travel counts, so the value does not lag the hand by four pixels.
		return ApplyDrag(DragTravel, Width, MouseEvent);
	}
	return ApplyDrag(Delta, Width, MouseEvent);
}

void SMixtormatSlider::GetDisplayRange(double& OutMin, double& OutMax) const
{
	OutMin = MinValueAttribute.Get(0.0);
	OutMax = MaxValueAttribute.Get(1.0);
	if (!ExpandableAttribute.Get(false))
	{
		return;
	}
	const double Value = GetValue();
	// Back inside the authored range (a reset, a typed value, an undo): show the authored range
	// again. Only a drag in progress keeps a grown range while the value sits inside it.
	if (!bDragging && Value >= OutMin && Value <= OutMax)
	{
		return;
	}
	if (ExpandedMin.IsSet())
	{
		OutMin = FMath::Min(OutMin, ExpandedMin.GetValue());
	}
	if (ExpandedMax.IsSet())
	{
		OutMax = FMath::Max(OutMax, ExpandedMax.GetValue());
	}
	OutMin = FMath::Min(OutMin, Value);
	OutMax = FMath::Max(OutMax, Value);
}

FReply SMixtormatSlider::ApplyDrag(const float Delta, const float Width, const FPointerEvent& MouseEvent)
{
	double MinValue = 0.0;
	double MaxValue = 1.0;
	GetDisplayRange(MinValue, MaxValue);
	const double Range = FMath::Max(MaxValue - MinValue, 0.0);
	// Rate is fixed per unit of travel and read per event, so Shift changes speed from here
	// on instead of rescaling everything dragged so far.
	// A row at least DragRangeDistance wide tracks the cursor one to one, so the fill stays
	// under the hand; a narrower one (a paired half row) spreads the range over the minimum
	// distance instead of scrubbing faster than a full row.
	const double Rate = Range / FMath::Max(Width, MixtormatTokens::DragRangeDistance)
		* (MouseEvent.IsShiftDown() ? MixtormatTokens::FineDragScale : 1.0);
	const double Proposed = DragValue + static_cast<double>(Delta) * Rate;

	// The drag never leaves the range it started with. Pushing an end that still has room is
	// remembered; the range grows when the button is released (FinishDragRange).
	const double HardMin = HardMinAttribute.Get(-UE_BIG_NUMBER);
	const double HardMax = HardMaxAttribute.Get(UE_BIG_NUMBER);
	const double Low = FMath::Max(FMath::Min(MinValue, DragStartValue), HardMin);
	const double High = FMath::Min(FMath::Max(MaxValue, DragStartValue), HardMax);
	if (ExpandableAttribute.Get(false) && Range > 0.0)
	{
		bPushedHigh = Proposed > High && High < HardMax;
		bPushedLow = Proposed < Low && Low > HardMin;
	}
	DragValue = FMath::Clamp(Proposed, Low, High);

	double NewValue = DragValue;
	const double Snap = DeltaAttribute.Get(0.0);
	if (MouseEvent.IsControlDown() && Snap > 0.0)
	{
		NewValue = FMath::RoundToDouble(NewValue / Snap) * Snap;
	}
	if (bInteger)
	{
		NewValue = FMath::RoundToDouble(NewValue);
	}
	OnValueChanged.ExecuteIfBound(FMath::Clamp(NewValue, Low, High));
	return FReply::Handled();
}

void SMixtormatSlider::FinishDragRange()
{
	if (!ExpandableAttribute.Get(false))
	{
		bPushedLow = bPushedHigh = false;
		return;
	}
	double MinValue = 0.0;
	double MaxValue = 1.0;
	GetDisplayRange(MinValue, MaxValue);
	const double Span = FMath::Max(MaxValue - MinValue, 0.0);
	if (bPushedHigh)
	{
		ExpandedMax = FMath::Min(HardMaxAttribute.Get(UE_BIG_NUMBER), MaxValue + Span);
	}
	if (bPushedLow)
	{
		ExpandedMin = FMath::Max(HardMinAttribute.Get(-UE_BIG_NUMBER), MinValue - Span);
	}
	const double Value = GetValue();
	if (!bPushedLow && !bPushedHigh
		&& Value > MinValueAttribute.Get(0.0) && Value < MaxValueAttribute.Get(1.0))
	{
		ExpandedMin.Reset();
		ExpandedMax.Reset();
	}
	bPushedLow = bPushedHigh = false;
}

void SMixtormatSlider::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Alt-tab or a modal mid-scrub: close the drag so the owner restores preview quality and
	// commits undo, instead of waiting for a button-up that will never arrive.
	if (bDragging && bMovedPastThreshold)
	{
		FinishDragRange();
		OnEndDrag.ExecuteIfBound();
	}
	bDragging = false;
	bMovedPastThreshold = false;
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FReply SMixtormatSlider::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return FReply::Unhandled();
	}

	bDragging = false;
	if (bMovedPastThreshold)
	{
		FinishDragRange();
		OnEndDrag.ExecuteIfBound();
	}
	else
	{
		// A press that never moved is a request to type, not a zero-length scrub.
		BeginTextEntry();
	}
	bMovedPastThreshold = false;
	// The cursor was hidden in place; show it again where the press began.
	return FReply::Handled().ReleaseMouseCapture().SetMousePos(DragStartScreen);
}

int32 SMixtormatSlider::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const bool bHighlight = IsHovered() || bDragging;
	const FVector2D Size = AllottedGeometry.GetLocalSize();

	const TCHAR* BackgroundKey =
		!bEnabled ? TEXT("Mixtormat.ValueSlider.BackgroundDisabled")
		: bEditing ? TEXT("Mixtormat.ValueSlider.BackgroundEntry")
		: bMovedPastThreshold ? TEXT("Mixtormat.ValueSlider.BackgroundActive")
		: bHighlight ? TEXT("Mixtormat.ValueSlider.BackgroundHovered")
		: TEXT("Mixtormat.ValueSlider.Background");

	// MakeBox forwards InTint verbatim -- it does NOT multiply by the brush's own tint, and InTint
	// defaults to white. Every painted element here has to pass the brush tint explicitly or it
	// renders white whatever colour the style registered. SBorder and SImage do this for you,
	// which is why only the hand-painted widget was affected.
	const FSlateBrush* BackgroundBrush = Style.GetBrush(BackgroundKey);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		BackgroundBrush,
		ESlateDrawEffect::None,
		BackgroundBrush->GetTint(InWidgetStyle));

	// The same well ramp the dropdown chips paint (darker at the top), inset by the outline so
	// the brush's border stays visible. Typing and disabled keep their flat brushes.
	if (bEnabled && !bEditing)
	{
		const bool bLifted = bHighlight || bMovedPastThreshold;
		const MixtormatGradient::FStop Well[] = {
			{ 0.0f, bLifted ? MixtormatPalette::WellTopHover() : MixtormatPalette::WellTop() },
			{ 1.0f, bLifted ? MixtormatPalette::WellBottomHover() : MixtormatPalette::WellBottom() },
		};
		const float Inset = MixtormatTokens::OutlineWidth;
		const FVector2f WellSize(
			FMath::Max(static_cast<float>(Size.X) - Inset * 2.0f, 0.0f),
			FMath::Max(static_cast<float>(Size.Y) - Inset * 2.0f, 0.0f));
		MixtormatGradient::Paint(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(WellSize, FSlateLayoutTransform(FVector2f(Inset, Inset))),
			WellSize, Orient_Vertical, Well,
			FVector4f(FMath::Max(MixtormatTokens::CornerRadius - Inset, 0.0f)));
	}

	if (bEditing)
	{
		// The entry field replaces the whole bar while typing; painting the fill and the value
		// text underneath it would show two numbers at once.
		return SCompoundWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
	}

	const double Value = GetValue();
	double MinValue = 0.0;
	double MaxValue = 1.0;
	GetDisplayRange(MinValue, MaxValue);
	const double Range = MaxValue - MinValue;
	const bool bValidRange = Range > UE_DOUBLE_SMALL_NUMBER;

	// Where the fill starts. On a range that spans zero it starts at zero and grows either way,
	// so an untouched signed control reads as empty instead of half-set -- which is what made a
	// column of zeroed growth weights look like a column of deliberate settings.
	const bool bBidirectional = bValidRange && MinValue < 0.0 && MaxValue > 0.0;
	const float OriginFraction = bValidRange
		? static_cast<float>(FMath::Clamp((bBidirectional ? 0.0 : MinValue) - MinValue, 0.0, Range) / Range)
		: 0.0f;
	const float ValueFraction = bValidRange
		? static_cast<float>(FMath::Clamp((Value - MinValue) / Range, 0.0, 1.0))
		: 0.0f;

	const float FillLeft = FMath::Min(OriginFraction, ValueFraction) * Size.X;
	const float FillRight = FMath::Max(OriginFraction, ValueFraction) * Size.X;
	if (FillRight - FillLeft > MixtormatTokens::MinPaintedFill)
	{
		// Two stacked gradients, as the design specifies the fill: a vertical ramp for the body,
		// and a horizontal black shade over it that falls away fast and then holds. Painted rather
		// than brushed because Slate has neither a gradient brush nor a multiply blend -- and a
		// flat brush here is what made the bar read as a solid block with an eased edge.
		const bool bScrubbing = bMovedPastThreshold;
		const FLinearColor BodyTop =
			!bEnabled ? MixtormatPalette::FillDisabled()
			: bScrubbing ? MixtormatPalette::FillTopActive()
			: bHighlight ? MixtormatPalette::FillTopHover()
			: MixtormatPalette::FillTop();
		const FLinearColor BodyBottom =
			!bEnabled ? MixtormatPalette::FillDisabled()
			: bScrubbing ? MixtormatPalette::FillBottomActive()
			: bHighlight ? MixtormatPalette::FillBottomHover()
			: MixtormatPalette::FillBottom();

		const FVector2f FillSize(FillRight - FillLeft, static_cast<float>(Size.Y));
		// Drawn at the fill's own size rather than full-size behind a clip. The clipped version
		// collapsed to a couple of pixels at the bottom of the row -- correct width, no height --
		// and this is the same explicitly-sized geometry the tick and the stripe below already
		// use, which does render.
		const FPaintGeometry FillGeometry = AllottedGeometry.ToPaintGeometry(
			FillSize, FSlateLayoutTransform(FVector2f(FillLeft, 0.0f)));

		const MixtormatGradient::FStop Body[] = {
			{ 0.0f, BodyTop },
			{ 1.0f, BodyBottom },
		};
		MixtormatGradient::Paint(
			OutDrawElements, LayerId + 1, FillGeometry, FillSize,
			Orient_Vertical, Body, FVector4f(MixtormatTokens::CornerRadius));

		const MixtormatGradient::FStop Shade[] = {
			{ 0.0f, MixtormatPalette::MultiplyStart() },
			{ MixtormatTokens::MultiplyMidPosition, MixtormatPalette::MultiplyMid() },
			{ 1.0f, MixtormatPalette::MultiplyEnd() },
		};
		MixtormatGradient::Paint(
			OutDrawElements, LayerId + 2, FillGeometry, FillSize,
			Orient_Horizontal, Shade, FVector4f(MixtormatTokens::CornerRadius));
	}

	// Centre tick, so zero is still locatable when the fill is empty.
	if (bBidirectional)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(MixtormatTokens::TickWidth, static_cast<float>(Size.Y) - MixtormatTokens::TickInsetY * 2.0f),
				FSlateLayoutTransform(FVector2f(OriginFraction * static_cast<float>(Size.X), MixtormatTokens::TickInsetY))),
			Style.GetBrush(TEXT("Mixtormat.ValueSlider.Tick")),
			ESlateDrawEffect::None,
			Style.GetBrush(TEXT("Mixtormat.ValueSlider.Tick"))->GetTint(InWidgetStyle));
	}

	// Leading stripe when the value differs from its default. Survives at this row height where a
	// dot or an italic label would not, and does not compete with the blue fill.
	const bool bModified = bInteger
		? FMath::RoundToInt(Value) != FMath::RoundToInt(DefaultValueAttribute.Get(0.0))
		: !FMath::IsNearlyEqual(Value, DefaultValueAttribute.Get(0.0), 1.0e-6);
	if (bModified && bEnabled)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(MixtormatTokens::ModifiedStripeWidth, static_cast<float>(Size.Y)),
				FSlateLayoutTransform(FVector2f::ZeroVector)),
			Style.GetBrush(TEXT("Mixtormat.ValueSlider.Modified")),
			ESlateDrawEffect::None,
			Style.GetBrush(TEXT("Mixtormat.ValueSlider.Modified"))->GetTint(InWidgetStyle));
	}

	const FTextBlockStyle& LabelStyle = Style.GetWidgetStyle<FTextBlockStyle>(
		bEnabled ? TEXT("Mixtormat.ValueSlider.Label") : TEXT("Mixtormat.ValueSlider.LabelDisabled"));
	const FTextBlockStyle& ValueStyle =
		Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.ValueSlider.Value"));

	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FString ValueText = FormatValue(Value);
	const float TextHeight = FontMeasure->Measure(TEXT("0"), LabelStyle.Font).Y;
	const float TextY = (static_cast<float>(Size.Y) - TextHeight) * 0.5f;
	const float ValueWidth = FontMeasure->Measure(ValueText, ValueStyle.Font).X;
	const float LabelX = MixtormatTokens::RowTextInset
		+ (bModified && bEnabled ? MixtormatTokens::ModifiedLabelInset : 0.0f);

	// A long label is cut where the value begins rather than overrunning it. Slate's ellipsis
	// policy belongs to STextBlock and is not available to a painted string, so the clip is the
	// equivalent -- and at this row height a hard cut reads better than an ellipsis anyway.
	const float LabelRoom = static_cast<float>(Size.X) - ValueWidth - MixtormatTokens::RowTextInset * 2.0f - LabelX;
	if (LabelRoom > 1.0f)
	{
		OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.MakeChild(
			FVector2f(LabelX + LabelRoom, static_cast<float>(Size.Y)),
			FSlateLayoutTransform())));

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)),
				FSlateLayoutTransform(FVector2f(LabelX, TextY))),
			LabelAttribute.Get(FText::GetEmpty()),
			LabelStyle.Font,
			ESlateDrawEffect::None,
			LabelStyle.ColorAndOpacity.GetSpecifiedColor());

		OutDrawElements.PopClip();
	}

	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 4,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)),
			FSlateLayoutTransform(
				FVector2f(static_cast<float>(Size.X) - ValueWidth - MixtormatTokens::RowTextInset, TextY))),
		FText::FromString(ValueText),
		ValueStyle.Font,
		ESlateDrawEffect::None,
		(bEnabled ? ValueStyle : LabelStyle).ColorAndOpacity.GetSpecifiedColor());

	return LayerId + 5;
}

#undef LOCTEXT_NAMESPACE
