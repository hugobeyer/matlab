// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatSlider.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/Controls/MixtormatEntryCommit.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "UI/Primitives/MixtormatWell.h"
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
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	ControlLabelTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	DisabledControlLabelTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	ControlValueTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlValue),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}
	else
	{
		SetToolTipText(LOCTEXT(
			"SliderHint",
			"Drag to adjust · click to type · Shift fine · Ctrl+Shift finer · Ctrl snap · MMB or hover + Backspace to reset"));
	}


	ChildSlot
	.Padding(TAttribute<FMargin>::CreateLambda([]() { return FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset, 0.0f); }))
	.VAlign(VAlign_Center)
	[
		SAssignNew(EntryWidget, SEditableText)
		.Font(ControlValueTextStyle.Font)
		.ColorAndOpacity(ControlValueTextStyle.ColorAndOpacity)
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
	const Mixtormat::FMixtormatControlMetrics& Layout = FMixtormatThemeStore::GetResolved().ControlLayout;
		return FVector2D(Layout.RowFieldMinWidth, Layout.RowHeight);
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
	LastDragScreenX = static_cast<float>(MouseEvent.GetScreenSpacePosition().X);

	// Capturing is what makes the owner's interactive-edit check see the scrub, so undo history
	// is deferred and previews are rate-limited. The cursor stays the real, visible one: it is
	// read as a position rather than as raw deltas, so a stalled frame loses nothing, and the
	// screen edge is handled by wrapping it (WrapCursorAtScreenEdge).
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMixtormatSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	// Cursor travel since the last event, screen pixels to Slate units so DPI scale does not
	// change the rate. Taken from positions rather than the event's own delta so a wrap of the
	// cursor (below) can be excluded from it.
	const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
	const float Scale = FMath::Max(MyGeometry.Scale, UE_KINDA_SMALL_NUMBER);
	const float Delta = (static_cast<float>(Screen.X) - LastDragScreenX) / Scale;
	LastDragScreenX = static_cast<float>(Screen.X);
	const float Width = static_cast<float>(MyGeometry.GetLocalSize().X);
	if (!bMovedPastThreshold)
	{
		DragTravel += Delta;
		if (FMath::Abs(DragTravel) < MixtormatTokens::DragThreshold)
		{
			return WrapCursorAtScreenEdge(Screen);
		}
		bMovedPastThreshold = true;
		OnBeginDrag.ExecuteIfBound();
		// The threshold travel counts, so the value does not lag the hand by four pixels.
		ApplyDrag(DragTravel, Width, MouseEvent);
		return WrapCursorAtScreenEdge(Screen);
	}
	ApplyDrag(Delta, Width, MouseEvent);
	return WrapCursorAtScreenEdge(Screen);
}

FReply SMixtormatSlider::WrapCursorAtScreenEdge(const FVector2D& Screen)
{
	// Blender-style continuous grab: at the edge of the monitor's work area the cursor jumps to
	// the opposite edge. The landing point becomes the new reference, so the jump itself adds
	// no travel and the scrub carries on where it was.
	const FSlateRect WorkArea = FSlateApplication::Get().GetWorkArea(
		FSlateRect::FromPointAndExtent(FVector2f(Screen), FVector2f(1.0f, 1.0f)));
	const float Left = WorkArea.Left + MixtormatTokens::DragWrapMargin;
	const float Right = WorkArea.Right - 1.0f - MixtormatTokens::DragWrapMargin;
	float LandingX = 0.0f;
	if (Screen.X <= Left)
	{
		// Lands inside the wrap margin of the far edge, or the next move would wrap straight back.
		LandingX = Right - MixtormatTokens::DragWrapMargin;
	}
	else if (Screen.X >= Right)
	{
		LandingX = Left + MixtormatTokens::DragWrapMargin;
	}
	else
	{
		return FReply::Handled();
	}
	LastDragScreenX = LandingX;
	return FReply::Handled().SetMousePos(FIntPoint(FMath::RoundToInt(LandingX), FMath::RoundToInt(Screen.Y)));
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

void SMixtormatSlider::ApplyDrag(const float Delta, const float Width, const FPointerEvent& MouseEvent)
{
	double MinValue = 0.0;
	double MaxValue = 1.0;
	GetDisplayRange(MinValue, MaxValue);
	const double Range = FMath::Max(MaxValue - MinValue, 0.0);
	// Rate is fixed per unit of travel and read per event, so a modifier changes speed from here
	// on instead of rescaling everything dragged so far.
	// The range spans the row's own width, whatever it is, so the fill stays under the cursor on
	// a full row and on a paired half row alike. Shift slows the scrub; Ctrl+Shift slows it
	// further, and Ctrl on its own is left free for snapping.
	const double Tier = !MouseEvent.IsShiftDown() ? 1.0
		: MouseEvent.IsControlDown() ? MixtormatTokens::FinestDragScale
		: MixtormatTokens::FineDragScale;
	const double Rate = Range / FMath::Max(Width, UE_KINDA_SMALL_NUMBER) * Tier;
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
	if (MouseEvent.IsControlDown() && !MouseEvent.IsShiftDown() && Snap > 0.0)
	{
		NewValue = FMath::RoundToDouble(NewValue / Snap) * Snap;
	}
	if (bInteger)
	{
		NewValue = FMath::RoundToDouble(NewValue);
	}
	OnValueChanged.ExecuteIfBound(FMath::Clamp(NewValue, Low, High));
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
	return FReply::Handled().ReleaseMouseCapture();
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
	const FVector2f LocalSize(static_cast<float>(Size.X), static_cast<float>(Size.Y));

	// Every visual state below is resolved to numbers and colours here, once, rather than by
	// choosing between named brushes.
	//
	// That is a structural change, not a cosmetic one. Brush *names* cannot be interpolated: a
	// later state animation needs a rest value and a state value it can lerp between, and four
	// opaque brushes give it nothing to lerp. Resolving state to alpha/saturation/offset keeps both
	// endpoints available for that pass without touching this function again.
	//
	// It also removes the entry and disabled brushes entirely. Those were flat, which is why a
	// typing field and a disabled trough looked like different objects from a live one rather than
	// as the same control in a different state.
	const bool bHovered = IsHovered();
	const bool bScrubbing = bMovedPastThreshold;

	// The fill's authored state, resolved from the widget's own flags. The recipe carries the
	// numbers; this only decides which set.
	const Mixtormat::EMixtormatFillState FillState =
		!bEnabled ? Mixtormat::EMixtormatFillState::Disabled
		: bScrubbing ? Mixtormat::EMixtormatFillState::Active
		: bHighlight ? Mixtormat::EMixtormatFillState::Hover
		: Mixtormat::EMixtormatFillState::Rest;

	// The well. Ground, recess and border are the shared painter's job now, so the trough, the
	// chip and the toggle cannot drift apart again.
	MixtormatWell::FParams WellParams;
	WellParams.bHovered = bHighlight;
	WellParams.bFlat = bEditing;
	MixtormatWell::PaintBackground(
		OutDrawElements, LayerId, AllottedGeometry, LocalSize, WellParams);

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
		// Two stacked passes, as the design specifies the fill: a vertical ramp for the body added
		// over the accent, and a horizontal black shade over that with a movable midpoint.
		//
		// Drawn at the fill's own size rather than full-size behind a clip. The clipped version
		// collapsed to a couple of pixels at the bottom of the row -- correct width, no height --
		// and this is the same explicitly-sized geometry the tick and the stripe below already use,
		// which does render.
		const FVector2f FillSize(FillRight - FillLeft, LocalSize.Y);
		const FPaintGeometry FillGeometry = AllottedGeometry.ToPaintGeometry(
			FillSize, FSlateLayoutTransform(FVector2f(FillLeft, 0.0f)));

		const Mixtormat::FMixtormatSurfaceRecipe FillRecipe =
			Mixtormat::MakeFillRecipe(FMixtormatThemeStore::GetTheme(), FillState);

		Mixtormat::FMixtormatSurfaceSamples FillSamples;
		Mixtormat::CompositeSurface(
			FillRecipe,
			FMixtormatThemeStore::GetResolved().Palette,
			Mixtormat::FMixtormatStateModifier(),
			FillSamples);

		Mixtormat::FMixtormatSurfacePainter::PaintBody(
			OutDrawElements, LayerId + 1, FillGeometry, FillRecipe, FillSamples);
	}

	// The well's border goes on last, over the fill, so the rim stays continuous across it.
	MixtormatWell::PaintBorder(
		OutDrawElements, LayerId + 3, AllottedGeometry, LocalSize, WellParams);

	// Centre tick, so zero is still locatable when the fill is empty. Drawn in ground rather than
	// in a grey, so it reads as a gap in the fill instead of as a hairline laid over it.
	if (bBidirectional)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(MixtormatTokens::TickWidth, LocalSize.Y - MixtormatTokens::TickInsetY * 2.0f),
				FSlateLayoutTransform(FVector2f(OriginFraction * LocalSize.X, MixtormatTokens::TickInsetY))),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline));
	}

	// Leading stripe when the value differs from its default. Survives at this row height where a
	// dot or an italic label would not, and does not compete with the blue fill.
	//
	// Width and intensity are separate authored values. The comparison against DefaultValueAttribute
	// is untouched -- only the paint changed.
	const bool bModified = bInteger
		? FMath::RoundToInt(Value) != FMath::RoundToInt(DefaultValueAttribute.Get(0.0))
		: !FMath::IsNearlyEqual(Value, DefaultValueAttribute.Get(0.0), 1.0e-6);
	if (bModified && bEnabled)
	{
		FLinearColor Marker = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Modified);
		Marker.A *= MixtormatTokens::ModifiedStripeOpacity;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(MixtormatTokens::ModifiedStripeWidth, LocalSize.Y),
				FSlateLayoutTransform(FVector2f::ZeroVector)),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			Marker);
	}

	const FTextBlockStyle& LabelStyle = bEnabled ? ControlLabelTextStyle : DisabledControlLabelTextStyle;
	const FTextBlockStyle& ValueStyle = ControlValueTextStyle;

	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FString ValueText = FormatValue(Value);
	const float TextHeight = FontMeasure->Measure(TEXT("0"), LabelStyle.Font).Y;
	const float TextY = (static_cast<float>(Size.Y) - TextHeight) * 0.5f;
	const float ValueWidth = FontMeasure->Measure(ValueText, ValueStyle.Font).X;
	const float LabelX = FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset
		+ (bModified && bEnabled ? MixtormatTokens::ModifiedLabelInset : 0.0f);

	// A long label is cut where the value begins rather than overrunning it. Slate's ellipsis
	// policy belongs to STextBlock and is not available to a painted string, so the clip is the
	// equivalent -- and at this row height a hard cut reads better than an ellipsis anyway.
	const float LabelRoom = static_cast<float>(Size.X) - ValueWidth - FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset * 2.0f - LabelX;
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
				FVector2f(static_cast<float>(Size.X) - ValueWidth - FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset, TextY))),
		FText::FromString(ValueText),
		ValueStyle.Font,
		ESlateDrawEffect::None,
		(bEnabled ? ValueStyle : LabelStyle).ColorAndOpacity.GetSpecifiedColor());

	return LayerId + 5;
}

#undef LOCTEXT_NAMESPACE
