// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Rendering/DrawElements.h"
#include "Style/MixtormatThemeStore.h"
#include "MixtormatCompositing.h"

class SMixtormatTextFieldGradient final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatTextFieldGradient) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		ChildSlot [ Args._Content.Widget ];
	}

	int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
		const int32 LayerId, const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const override
	{
		const Mixtormat::FMixtormatTextFieldTheme& Theme = FMixtormatThemeStore::GetTheme().TextField;
		FLinearColor TopShade = Theme.Shade;
		TopShade.A = FMath::Clamp(Theme.ShadeOpacity, 0.0f, 1.0f);
		FLinearColor BottomShade = Theme.Shade;
		BottomShade.A = FMath::Clamp(Theme.ShadeBottomOpacity, 0.0f, 1.0f);
		const FLinearColor Top = MixtormatCompositing::ApplyBlend(Theme.ShadeBlend, Theme.Surface, TopShade);
		const FLinearColor Bottom = MixtormatCompositing::ApplyBlend(Theme.ShadeBlend, Theme.Surface, BottomShade);
		TArray<FSlateGradientStop> Stops;
		Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, Top));
		Stops.Add(FSlateGradientStop(FVector2D(0.0f, Geometry.GetLocalSize().Y), Bottom));
		FSlateDrawElement::MakeGradient(Elements, LayerId, Geometry.ToPaintGeometry(),
			Stops, Orient_Vertical, ESlateDrawEffect::None);
		return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
			LayerId + 1, WidgetStyle, bParentEnabled);
	}
};
