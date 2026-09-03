#include "UI/Controls/SMixtormatSlider.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SEditableText.h"

#define LOCTEXT_NAMESPACE "Mixtormat"


void SMixtormatSlider::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	ValueAttribute = InArgs._Value;
	MinValue = InArgs._MinValue;
	MaxValue = InArgs._MaxValue;
	DefaultValue = InArgs._DefaultValue;
	Delta = InArgs._Delta;
	Precision = InArgs._Precision;
	bInteger = InArgs._bInteger;
	OnValueChanged = InArgs._OnValueChanged;
	OnReset = InArgs._OnReset;
	OnBeginDrag = InArgs._OnBeginDrag;
	OnEndDrag = InArgs._OnEndDrag;

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
		Value = FMath::Clamp(Value, MinValue, MaxValue);
	}
	if (bInteger)
	{
		Value = FMath::RoundToDouble(Value);
	}
	OnValueChanged.ExecuteIfBound(Value);
}

void SMixtormatSlider::ResetToDefault()
{
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
	EndTextEntry();
	if (CommitType == ETextCommit::OnCleared)
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
	return FVector2D(120.0f, MixtormatTokens::RowHeight);
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
	DragStartX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;

	// Capturing is what makes the owner's interactive-edit check see the scrub, so the
	// preview drops to its drag resolution and undo history is deferred.
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMixtormatSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	const float PixelDelta = LocalX - DragStartX;
	if (!bMovedPastThreshold)
	{
		if (FMath::Abs(PixelDelta) < MixtormatTokens::DragThreshold)
		{
			return FReply::Handled();
		}
		bMovedPastThreshold = true;
		OnBeginDrag.ExecuteIfBound();
	}

	// Absolute rather than incremental, so a scrub that reverses direction returns to where
	// it started instead of drifting.
	const float Width = FMath::Max(MyGeometry.GetLocalSize().X, 1.0f);
	const double Range = MaxValue - MinValue;
	const double Scale = MouseEvent.IsShiftDown() ? MixtormatTokens::FineDragScale : 1.0;
	double NewValue = DragStartValue + (Range * (PixelDelta / Width)) * Scale;
	if (MouseEvent.IsControlDown() && Delta > 0.0)
	{
		NewValue = FMath::RoundToDouble(NewValue / Delta) * Delta;
	}
	CommitValue(NewValue, true);
	return FReply::Handled();
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

	if (bEditing)
	{
		// The entry field replaces the whole bar while typing; painting the fill and the value
		// text underneath it would show two numbers at once.
		return SCompoundWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
	}

	const double Value = GetValue();
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
	if (FillRight - FillLeft > 0.5f)
	{
		const TCHAR* FillKey =
			!bEnabled ? TEXT("Mixtormat.ValueSlider.FillDisabled")
			: bMovedPastThreshold ? TEXT("Mixtormat.ValueSlider.FillActive")
			: bHighlight ? TEXT("Mixtormat.ValueSlider.FillHovered")
			: TEXT("Mixtormat.ValueSlider.Fill");

		// Drawn at the fill's own size rather than full-size behind a clip. The clipped version
		// collapsed to a couple of pixels at the bottom of the row -- correct width, no height --
		// and this is the same explicitly-sized geometry the tick and the stripe below already
		// use, which does render. The right edge picks up the corner radius as a result; at a
		// 2px radius on an 18px row that is not worth another clip to avoid.
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(FillRight - FillLeft, static_cast<float>(Size.Y)),
				FSlateLayoutTransform(FVector2f(FillLeft, 0.0f))),
			Style.GetBrush(FillKey),
			ESlateDrawEffect::None,
			Style.GetBrush(FillKey)->GetTint(InWidgetStyle));
	}

	// Centre tick, so zero is still locatable when the fill is empty.
	if (bBidirectional)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 2,
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
		? FMath::RoundToInt(Value) != FMath::RoundToInt(DefaultValue)
		: !FMath::IsNearlyEqual(Value, DefaultValue, 1.0e-6);
	if (bModified && bEnabled)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 2,
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
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)),
				FSlateLayoutTransform(FVector2f(LabelX, TextY))),
			Label,
			LabelStyle.Font,
			ESlateDrawEffect::None,
			LabelStyle.ColorAndOpacity.GetSpecifiedColor());

		OutDrawElements.PopClip();
	}

	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 3,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)),
			FSlateLayoutTransform(
				FVector2f(static_cast<float>(Size.X) - ValueWidth - MixtormatTokens::RowTextInset, TextY))),
		FText::FromString(ValueText),
		ValueStyle.Font,
		ESlateDrawEffect::None,
		(bEnabled ? ValueStyle : LabelStyle).ColorAndOpacity.GetSpecifiedColor());

	return LayerId + 4;
}

#undef LOCTEXT_NAMESPACE
