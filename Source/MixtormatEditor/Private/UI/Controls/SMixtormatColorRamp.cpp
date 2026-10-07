// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatColorRamp.h"

#include "Editor.h"
#include "MixtormatColorRampMath.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/CoreStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatColorRampPrivate
{
	bool Equals(const FMixtormatColorRamp& A, const FMixtormatColorRamp& B)
	{
		if (A.Interpolation != B.Interpolation || A.Stops.Num() != B.Stops.Num()) { return false; }
		for (int32 Index = 0; Index < A.Stops.Num(); ++Index)
		{
			const FMixtormatColorRampStop& AS = A.Stops[Index];
			const FMixtormatColorRampStop& BS = B.Stops[Index];
			if (AS.X != BS.X || AS.Color.R != BS.Color.R || AS.Color.G != BS.Color.G
				|| AS.Color.B != BS.Color.B || AS.Color.A != BS.Color.A)
			{
				return false;
			}
		}
		return true;
	}

	FLinearColor Opaque(const FLinearColor& Color)
	{
		return FLinearColor(Color.R, Color.G, Color.B, 1.0f);
	}
}

void SMixtormatColorRamp::Construct(const FArguments& Args)
{
	RampAttribute = Args._Ramp;
	Ramp = RampAttribute.Get(FMixtormatColorRamp());
	Ramp.Sanitize();
	Height = Args._Height;
	DomainMin = Args._DomainMin;
	DomainMax = Args._DomainMax;
	OnChanged = Args._OnChanged;
	OnBeginInteractiveEdit = Args._OnBeginInteractiveEdit;
	OnEndInteractiveEdit = Args._OnEndInteractiveEdit;
	BuildLayout();
	// Chrome under the shared toolbar/graph: the selected stop's swatch and position. Presets
	// live in the owning inspector group's header as a dropdown.
	ChromeBox = SNew(SVerticalBox);
	RebuildChrome();
	const TSharedRef<SWidget> Existing = ChildSlot.GetWidget();
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Existing]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ChromeBox.ToSharedRef()]
	];
	if (SelectedPoint == INDEX_NONE && Ramp.Stops.Num() > 0) { SelectedPoint = 0; }
}

FVector2D SMixtormatColorRamp::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2D Base = SMixtormatRampEditorBase::ComputeDesiredSize(LayoutScaleMultiplier);
	// The stop row (~22px) stacked under the shared graph.
	return FVector2D(Base.X, Base.Y + 24.0f);
}

void SMixtormatColorRamp::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDragging || !RampAttribute.IsBound()) { return; }

	FMixtormatColorRamp AuthoredRamp = RampAttribute.Get(Ramp);
	AuthoredRamp.Sanitize();
	if (MixtormatColorRampPrivate::Equals(Ramp, AuthoredRamp)) { return; }

	Ramp = MoveTemp(AuthoredRamp);
	if (!Ramp.Stops.IsValidIndex(SelectedPoint)) { SelectedPoint = INDEX_NONE; }
	HoverPoint = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SMixtormatColorRamp::InsertPointAt(const float X, const float GraphY)
{
	int32 Insert = 0;
	while (Insert < Ramp.Stops.Num() && Ramp.Stops[Insert].X < X) { ++Insert; }
	Ramp.Stops.Insert(FMixtormatColorRampStop{X, MixtormatColorRampMath::Evaluate(Ramp, X)}, Insert);
	return Insert;
}

void SMixtormatColorRamp::RemovePoint(const int32 Index)
{
	Ramp.Stops.RemoveAt(Index);
}

void SMixtormatColorRamp::ResetPoints()
{
	Ramp.Stops = {
		FMixtormatColorRampStop{DomainMin, FLinearColor::Black},
		FMixtormatColorRampStop{DomainMax, FLinearColor::White}
	};
	Ramp.Interpolation = EMixtormatColorRampInterpolation::Linear;
}

void SMixtormatColorRamp::ApplyPointDrag(const int32 Index, const float GraphX, const float GraphY,
	const FVector2f& ScreenPos)
{
	// X is no longer clamped against neighbours here: the shared editor swaps adjacent stops
	// when the dragged stop crosses one, so the array stays sorted and the dragged payload keeps
	// its identity. Endpoints are still locked to the domain.
	Ramp.Stops[Index].X = GraphX;
}

void SMixtormatColorRamp::SwapPointState(const int32 IndexA, const int32 IndexB)
{
	if (Ramp.Stops.IsValidIndex(IndexA) && Ramp.Stops.IsValidIndex(IndexB))
	{
		Swap(Ramp.Stops[IndexA].Color, Ramp.Stops[IndexB].Color);
	}
}

void SMixtormatColorRamp::NotifyPointRemoved(const int32 RemovedIndex)
{
	// Keep the selection on a sensible neighbour: the one that took the removed stop's place.
	if (SelectedPoint == RemovedIndex)
	{
		SelectedPoint = FMath::Min(RemovedIndex, GetPointCount() - 1);
	}
	else if (SelectedPoint > RemovedIndex)
	{
		--SelectedPoint;
	}
	RebuildChrome();
}

void SMixtormatColorRamp::NotifyPointInserted(const int32 InsertedIndex)
{
	// Inserting a stop selects the new stop so the user can immediately recolour it.
	SelectedPoint = InsertedIndex;
	RebuildChrome();
}

bool SMixtormatColorRamp::IsEndpointLocked(const int32 Index) const
{
	// Color ramp endpoints stay locked to the domain.
	return Index == 0 || Index == Ramp.Stops.Num() - 1;
}

void SMixtormatColorRamp::OnRampEdited(const bool bInteractive)
{
	OnChanged.ExecuteIfBound(Ramp);
	if (!bInteractive) { RebuildChrome(); }
}

void SMixtormatColorRamp::SetInterpolation(const int32 Index)
{
	Ramp.Interpolation = static_cast<EMixtormatColorRampInterpolation>(Index);
}

FText SMixtormatColorRamp::GetInterpolationLabel(const int32 Index) const
{
	switch (Index)
	{
	case 0: return FText::FromString(TEXT("Constant"));
	case 1: return FText::FromString(TEXT("Linear"));
	default: return FText::FromString(TEXT("Smooth"));
	}
}

const FSlateBrush* SMixtormatColorRamp::GetInterpolationIcon(const int32 Index) const
{
	switch (Index)
	{
	case 0: return MixtormatIcons::ScalarRampConstant();
	case 1: return MixtormatIcons::ScalarRampLinear();
	default: return MixtormatIcons::ScalarRampSpline();
	}
}

void SMixtormatColorRamp::PaintRampContent(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float Y0 = Rect.Y0;
	const float Y1 = Rect.Y1;
	const float BarHeight = FMath::Max((Y1 - Y0) - MixtormatTokens::ScalarRampPointSize, 4.0f);

	// The gradient bar, sampled across the domain. Sample at the pixel rate the bar is drawn at
	// and evaluate each sample at its left edge, so adjacent boxes meet at the same colour and
	// the bar reads continuous at any inspector width instead of banding at a fixed count.
	const int32 Samples = FMath::Clamp(FMath::CeilToInt(Rect.X1 - Rect.X0), 64, 512);
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float T0 = static_cast<float>(Index) / static_cast<float>(Samples);
		const float T1 = static_cast<float>(Index + 1) / static_cast<float>(Samples);
		const float XA = XToScreen(Size, DomainMin + (DomainMax - DomainMin) * T0);
		const float XB = XToScreen(Size, DomainMin + (DomainMax - DomainMin) * T1);
		const float Value = DomainMin + (DomainMax - DomainMin) * T0;
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(FMath::Max(XB - XA, 1.0f), BarHeight), FSlateLayoutTransform(FVector2f(XA, Y0))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			MixtormatColorRampPrivate::Opaque(MixtormatColorRampMath::Evaluate(Ramp, Value)));
	}
}

void SMixtormatColorRamp::PaintPointMarker(FSlateWindowElementList& Elements, const int32 Layer,
	const FGeometry& Geometry, const FVector2D& Size, const int32 Index,
	const bool bActive, const bool bHover) const
{
	const Mixtormat::FMixtormatResolvedPalette& Pal = FMixtormatThemeStore::GetResolved().Palette;
	const FVector2f Marker = GetMarkerScreenPosition(Size, Index);
	const float SX = Marker.X;
	const float HandleY = Marker.Y;
	const float R = MixtormatTokens::ScalarRampPointSize * 0.5f;
	const bool bSelected = Index == SelectedPoint;
	// Selected stop gets a clear accent outline/ring so it stays visible while not being dragged.
	if (bSelected)
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(
			FVector2f(R * 2.0f + 6.0f, R * 2.0f + 6.0f),
			FSlateLayoutTransform(FVector2f(SX - R - 3.0f, HandleY - R - 3.0f))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
			Pal.Get(Mixtormat::EMixtormatColorRole::Accent));
	}
	const FLinearColor Border = bActive ? Pal.Get(Mixtormat::EMixtormatColorRole::Accent)
		: bHover ? Pal.Get(Mixtormat::EMixtormatColorRole::Text)
		: Pal.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(
		FVector2f(R * 2.0f + 2.0f, R * 2.0f + 2.0f),
		FSlateLayoutTransform(FVector2f(SX - R - 1.0f, HandleY - R - 1.0f))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Border);
	FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(
		FVector2f(R * 2.0f, R * 2.0f), FSlateLayoutTransform(FVector2f(SX - R, HandleY - R))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None,
		MixtormatColorRampPrivate::Opaque(Ramp.Stops[Index].Color));
}


float SMixtormatColorRamp::GetHandleScreenY(const FVector2D& Size) const
{
	const FGraphRect Rect = GetGraphRect(Size);
	const float BarHeight = FMath::Max((Rect.Y1 - Rect.Y0) - MixtormatTokens::ScalarRampPointSize, 4.0f);
	const float R = MixtormatTokens::ScalarRampPointSize * 0.5f;
	return Rect.Y0 + BarHeight + R;
}

FVector2f SMixtormatColorRamp::GetMarkerScreenPosition(const FVector2D& Size, const int32 Index) const
{
	if (!Ramp.Stops.IsValidIndex(Index))
	{
		return FVector2f::ZeroVector;
	}
	return FVector2f(XToScreen(Size, Ramp.Stops[Index].X), GetHandleScreenY(Size));
}

namespace MixtormatColorRampPresets
{
	struct FPreset
	{
		const TCHAR* Name = nullptr;
		TArray<FMixtormatColorRampStop> Stops;
	};

	static FPreset Make(const TCHAR* Name, const TArray<FMixtormatColorRampStop>& Stops)
	{
		FPreset P;
		P.Name = Name;
		P.Stops = Stops;
		return P;
	}

	// Compact built-in material-oriented gradients over the signed height domain [-1, 1].
	static TArray<FPreset> All()
	{
		TArray<FPreset> Out;
		Out.Add(Make(TEXT("Stone"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.12f, 0.12f, 0.13f)},
			FMixtormatColorRampStop{-0.25f, FLinearColor(0.28f, 0.27f, 0.26f)},
			FMixtormatColorRampStop{0.35f, FLinearColor(0.48f, 0.46f, 0.43f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.72f, 0.70f, 0.66f)},
		}));
		Out.Add(Make(TEXT("Clay"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.22f, 0.12f, 0.08f)},
			FMixtormatColorRampStop{-0.2f, FLinearColor(0.42f, 0.24f, 0.16f)},
			FMixtormatColorRampStop{0.4f, FLinearColor(0.62f, 0.38f, 0.26f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.78f, 0.58f, 0.42f)},
		}));
		Out.Add(Make(TEXT("Oxide"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.08f, 0.05f, 0.04f)},
			FMixtormatColorRampStop{-0.15f, FLinearColor(0.35f, 0.12f, 0.06f)},
			FMixtormatColorRampStop{0.3f, FLinearColor(0.65f, 0.22f, 0.08f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.85f, 0.45f, 0.18f)},
		}));
		Out.Add(Make(TEXT("Moss"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.06f, 0.08f, 0.04f)},
			FMixtormatColorRampStop{-0.2f, FLinearColor(0.12f, 0.22f, 0.08f)},
			FMixtormatColorRampStop{0.35f, FLinearColor(0.28f, 0.42f, 0.16f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.48f, 0.58f, 0.28f)},
		}));
		Out.Add(Make(TEXT("Mineral"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.10f, 0.12f, 0.18f)},
			FMixtormatColorRampStop{-0.3f, FLinearColor(0.18f, 0.28f, 0.42f)},
			FMixtormatColorRampStop{0.2f, FLinearColor(0.35f, 0.55f, 0.62f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.75f, 0.82f, 0.88f)},
		}));
		Out.Add(Make(TEXT("Sand"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.28f, 0.22f, 0.14f)},
			FMixtormatColorRampStop{-0.15f, FLinearColor(0.55f, 0.45f, 0.28f)},
			FMixtormatColorRampStop{0.4f, FLinearColor(0.78f, 0.68f, 0.45f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.92f, 0.86f, 0.68f)},
		}));
		Out.Add(Make(TEXT("Split Surface"), {
			FMixtormatColorRampStop{-1.0f, FLinearColor(0.08f, 0.08f, 0.09f)},
			FMixtormatColorRampStop{-0.05f, FLinearColor(0.22f, 0.20f, 0.18f)},
			FMixtormatColorRampStop{0.05f, FLinearColor(0.55f, 0.52f, 0.48f)},
			FMixtormatColorRampStop{1.0f, FLinearColor(0.88f, 0.86f, 0.82f)},
		}));
		return Out;
	}
}

void SMixtormatColorRamp::ApplyPreset(const int32 PresetIndex)
{
	const TArray<MixtormatColorRampPresets::FPreset> Presets = MixtormatColorRampPresets::All();
	if (!Presets.IsValidIndex(PresetIndex)) { return; }
	OnBeginInteractiveEdit.ExecuteIfBound();
	Ramp.Stops = Presets[PresetIndex].Stops;
	Ramp.Interpolation = EMixtormatColorRampInterpolation::Linear;
	Ramp.Sanitize();
	SelectedPoint = Ramp.Stops.Num() > 0 ? 0 : INDEX_NONE;
	NotifyEdit(true);
	OnEndInteractiveEdit.ExecuteIfBound();
	RebuildChrome();
}

void SMixtormatColorRamp::SetSelectedStopColor(const FLinearColor Color)
{
	if (!Ramp.Stops.IsValidIndex(SelectedPoint)) { return; }
	Ramp.Stops[SelectedPoint].Color = Color;
	NotifyEdit(false);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SMixtormatColorRamp::SetSelectedStopX(const float X)
{
	if (!Ramp.Stops.IsValidIndex(SelectedPoint)) { return; }
	// Endpoints stay locked to the domain; interior stops may move and cross via drag.
	if (IsEndpointLocked(SelectedPoint)) { return; }
	Ramp.Stops[SelectedPoint].X = FMath::Clamp(X, DomainMin, DomainMax);
	// Keep array sorted by swapping through neighbours if needed.
	while (SelectedPoint > 0 && Ramp.Stops[SelectedPoint].X < Ramp.Stops[SelectedPoint - 1].X)
	{
		Swap(Ramp.Stops[SelectedPoint], Ramp.Stops[SelectedPoint - 1]);
		--SelectedPoint;
	}
	while (SelectedPoint + 1 < Ramp.Stops.Num()
		&& Ramp.Stops[SelectedPoint].X > Ramp.Stops[SelectedPoint + 1].X)
	{
		Swap(Ramp.Stops[SelectedPoint], Ramp.Stops[SelectedPoint + 1]);
		++SelectedPoint;
	}
	NotifyEdit(false);
}

FReply SMixtormatColorRamp::OnSwatchClicked()
{
	if (Ramp.Stops.IsValidIndex(SelectedPoint))
	{
		OpenStopPicker(SelectedPoint);
	}
	return FReply::Handled();
}

int32 SMixtormatColorRamp::GetPresetCount()
{
	return MixtormatColorRampPresets::All().Num();
}

FText SMixtormatColorRamp::GetPresetName(const int32 PresetIndex)
{
	const TArray<MixtormatColorRampPresets::FPreset> Presets = MixtormatColorRampPresets::All();
	return Presets.IsValidIndex(PresetIndex)
		? FText::FromString(Presets[PresetIndex].Name) : FText::GetEmpty();
}

TSharedRef<SWidget> SMixtormatColorRamp::BuildSelectedStopRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Stop")))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(22.0f).HeightOverride(16.0f)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ContentPadding(0.0f)
				.IsEnabled_Lambda([this]() { return Ramp.Stops.IsValidIndex(SelectedPoint); })
				.OnClicked(this, &SMixtormatColorRamp::OnSwatchClicked)
				.ToolTipText(FText::FromString(TEXT("Edit stop colour")))
				[
					SNew(SColorBlock)
					.Color_Lambda([this]()
					{
						return Ramp.Stops.IsValidIndex(SelectedPoint)
							? Ramp.Stops[SelectedPoint].Color : FLinearColor::Black;
					})
					.ShowBackgroundForAlpha(false)
					.Size(FVector2D(22.0, 16.0))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Pos")))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SSpinBox<float>)
			.MinValue(DomainMin).MaxValue(DomainMax)
			.MinSliderValue(DomainMin).MaxSliderValue(DomainMax)
			.Delta(0.01f)
			.IsEnabled_Lambda([this]()
			{
				return Ramp.Stops.IsValidIndex(SelectedPoint) && !IsEndpointLocked(SelectedPoint);
			})
			.Value_Lambda([this]()
			{
				return Ramp.Stops.IsValidIndex(SelectedPoint) ? Ramp.Stops[SelectedPoint].X : 0.0f;
			})
			.OnValueChanged_Lambda([this](const float V) { SetSelectedStopX(V); })
			.OnValueCommitted_Lambda([this](const float V, ETextCommit::Type)
			{
				SetSelectedStopX(V);
			})
		];
}

void SMixtormatColorRamp::RebuildChrome()
{
	if (!ChromeBox.IsValid()) { return; }
	ChromeBox->ClearChildren();
	ChromeBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[BuildSelectedStopRow()];
}

void SMixtormatColorRamp::OpenStopPicker(const int32 StopIndex)
{
	if (!Ramp.Stops.IsValidIndex(StopIndex)) { return; }
	const FLinearColor Original = Ramp.Stops[StopIndex].Color;
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = Original;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, StopIndex](const FLinearColor NewColor)
		{
			if (Ramp.Stops.IsValidIndex(StopIndex))
			{
				Ramp.Stops[StopIndex].Color = NewColor;
				NotifyEdit(false);
			}
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, StopIndex, Original](const FLinearColor)
		{
			if (Ramp.Stops.IsValidIndex(StopIndex))
			{
				Ramp.Stops[StopIndex].Color = Original;
				NotifyEdit(false);
			}
		});
	OpenColorPicker(PickerArgs);
}

FReply SMixtormatColorRamp::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Ctrl-click a stop to recolour it; everything else is the shared ramp interaction.
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Event.IsControlDown())
	{
		const int32 Hit = HitPoint(Geometry.GetLocalSize(),
			Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		if (Hit != INDEX_NONE)
		{
			OpenStopPicker(Hit);
			return FReply::Handled();
		}
	}
	return SMixtormatRampEditorBase::OnMouseButtonDown(Geometry, Event);
}
