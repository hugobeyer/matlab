// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatPopupAnchor.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"

namespace
{
	constexpr float PopupGap = 2.0f;
	constexpr float PopupBoundaryInset = 2.0f;

	FSlateRect InsetRect(const FSlateRect& Rect, const float Inset)
	{
		return FSlateRect(
			Rect.Left + Inset,
			Rect.Top + Inset,
			Rect.Right - Inset,
			Rect.Bottom - Inset);
	}
}

void SMixtormatPopupAnchor::Construct(const FArguments& InArgs)
{
	GetMenuContent = InArgs._OnGetMenuContent;

	SMenuAnchor::Construct(
		SMenuAnchor::FArguments()
		.Placement(InArgs._Placement)
		.Method(EPopupMethod::UseCurrentWindow)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SMixtormatPopupAnchor::BuildConstrainedContent))
		[
			InArgs._Content.Widget
		]);
}

void SMixtormatPopupAnchor::OpenAt(const FVector2D& ScreenPosition, const bool bFocusMenu)
{
	SummonPosition = ScreenPosition;
	bHasSummonPosition = true;
	SetIsOpen(true, bFocusMenu);
}

void SMixtormatPopupAnchor::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SMenuAnchor::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!IsOpen() || !bHasSummonPosition || !WrappedContent.IsValid())
	{
		return;
	}

	const float Scale = AllottedGeometry.GetAccumulatedLayoutTransform().GetScale();
	const FVector2D PopupSize = WrappedContent->GetDesiredSize() * Scale;
	const FSlateRect Boundary = GetPopupBoundary();

	FVector2D Position = SummonPosition + FVector2D(PopupGap, PopupGap);
	if (Position.X + PopupSize.X > Boundary.Right)
	{
		Position.X = SummonPosition.X - PopupGap - PopupSize.X;
	}
	if (Position.Y + PopupSize.Y > Boundary.Bottom)
	{
		Position.Y = SummonPosition.Y - PopupGap - PopupSize.Y;
	}

	Position.X = FMath::Clamp(Position.X, Boundary.Left, FMath::Max(Boundary.Left, Boundary.Right - PopupSize.X));
	Position.Y = FMath::Clamp(Position.Y, Boundary.Top, FMath::Max(Boundary.Top, Boundary.Bottom - PopupSize.Y));

	ScreenPopupPosition = Position;
	LocalPopupPosition = AllottedGeometry.AbsoluteToLocal(Position);
}

TSharedRef<SWidget> SMixtormatPopupAnchor::BuildConstrainedContent()
{
	const TSharedRef<SWidget> Content = GetMenuContent.IsBound()
		? GetMenuContent.Execute()
		: SNullWidget::NullWidget;

	return SNew(SBox)
		.MaxDesiredHeight(this, &SMixtormatPopupAnchor::GetMaximumPopupHeight)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Content
			]
		];
}

FSlateRect SMixtormatPopupAnchor::GetPopupBoundary() const
{
	const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());
	FSlateRect Boundary = Window.IsValid()
		? Window->GetClientRectInScreen()
		: GetCachedGeometry().GetLayoutBoundingRect();

	FWidgetPath WidgetPath;
	if (FSlateApplication::Get().GeneratePathToWidgetUnchecked(AsShared(), WidgetPath))
	{
		for (int32 WidgetIndex = 0; WidgetIndex < WidgetPath.Widgets.Num(); ++WidgetIndex)
		{
			const FArrangedWidget& ArrangedWidget = WidgetPath.Widgets[WidgetIndex];
			if (ArrangedWidget.Widget->GetType() == FName(TEXT("SDockTab")))
			{
				const FSlateRect DockRect = ArrangedWidget.Geometry.GetLayoutBoundingRect();
				Boundary = Boundary.IntersectionWith(DockRect);
			}
		}
	}

	return InsetRect(Boundary, PopupBoundaryInset);
}

FOptionalSize SMixtormatPopupAnchor::GetMaximumPopupHeight() const
{
	const FSlateRect Boundary = GetPopupBoundary();
	const float Scale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
	return FOptionalSize(FMath::Max(
		1.0f,
		(Boundary.Bottom - Boundary.Top) / FMath::Max(Scale, UE_SMALL_NUMBER)));
}
