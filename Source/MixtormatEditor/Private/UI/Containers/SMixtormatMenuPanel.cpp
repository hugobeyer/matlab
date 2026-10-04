// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatMenuPanel.h"

#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatPalette.h"
#include "UI/Primitives/MixtormatGradientPainter.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatMenuPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(InArgs._MinWidth)
		.Padding(InArgs._Padding)
		[
			InArgs._Content.Widget
		]
	];
}

int32 SMixtormatMenuPanel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());

	// Both the ground ramp and additive tint finish at the fixed lip height. Short popups
	// show only the corresponding part of that ramp, rather than compressing it to fit.
	const float LipProgress = MixtormatTokens::MenuLipHeight > UE_SMALL_NUMBER
		? FMath::Min(Size.Y / MixtormatTokens::MenuLipHeight, 1.0f)
		: 1.0f;
	const float LipStop = Size.Y > UE_SMALL_NUMBER
		? FMath::Min(MixtormatTokens::MenuLipHeight / Size.Y, 1.0f)
		: 1.0f;
	const FLinearColor Top = MixtormatCompositing::Additive(
		MixtormatPalette::MenuGroundTop(), MixtormatPalette::MenuTint());
	const FLinearColor Bottom = MixtormatPalette::MenuGround();
	const FLinearColor LipEnd = MixtormatGradient::LerpSRGB(Top, Bottom, LipProgress);
	const MixtormatGradient::FStop Ground[] = {
		{ 0.0f, Top },
		{ LipStop, LipEnd },
		{ 1.0f, LipEnd },
	};
	MixtormatGradient::Paint(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Size,
		Orient_Vertical,
		Ground,
		FVector4f(MixtormatTokens::WellRadius));

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}
