// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatToggle.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "UI/Primitives/SMixtormatWellBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatToggle::Construct(const FArguments& InArgs)
{
	IsChecked = InArgs._IsChecked;

	// An SCheckBox still does the work -- hit testing, keyboard, the toggled callback, the
	// accessible role -- with its own painting stripped out. Rebuilding that on SCompoundWidget
	// would mean reimplementing focus and input handling to get a rectangle drawn differently.
	ChildSlot
	[
		SNew(SCheckBox)
		.Style(&FMixtormatStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.Toggle")))
		.ToolTipText(InArgs._ToolTip)
		.IsChecked(InArgs._IsChecked)
		.OnCheckStateChanged(InArgs._OnCheckStateChanged)
		[
			// The well, built by the shared painter so the toggle, the slider trough and the
			// dropdown chip are literally the same object rather than three that drifted apart.
			//
			// The disabled shade rides on top as its own pass. The design gives the toggle its own
			// recess (0.3 -> 0.12) rather than dimming the whole row, because a parent opacity would
			// fade the well, the fill and the label together and break the relationship between
			// them that makes a disabled control read as disabled rather than as faded.
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ToggleSize)
			.HeightOverride(MixtormatTokens::ToggleSize)
			[
				SNew(SMixtormatWellBox)
				.IsHovered(this, &SMixtormatToggle::IsHovered)
				.IsEnabled(this, &SMixtormatToggle::IsEnabled)
				.bDisabledShade(true)
				.DisabledShadeTop(MixtormatTokens::ToggleDisabledShadeTop)
				.DisabledShadeBottom(MixtormatTokens::ToggleDisabledShadeBottom)
				[
					SNew(SMixtormatGradientBox)
					// The fill is the accent at authored opacity, sampled through the same falloff
					// and saturation the slider uses -- one vocabulary for both.
					.StartColor(this, &SMixtormatToggle::GetFillTop)
					.EndColor(this, &SMixtormatToggle::GetFillBottom)
					.Orientation(Orient_Vertical)
					.CornerRadius(0.0f)
					.Padding(FMargin(MixtormatTokens::ToggleFillInset))
					[
						SNew(SBox)
						// Sized explicitly: a gradient box has no intrinsic size of its own, so an
						// unsized one here would collapse to nothing and the toggle would never
						// appear to fill.
						.WidthOverride(MixtormatTokens::ToggleFillSize)
						.HeightOverride(MixtormatTokens::ToggleFillSize)
					]
				]
			]
		]
	];
}

FLinearColor SMixtormatToggle::GetFillTop() const
{
	// The mixed (Undetermined) state keeps the resting fill. That is a real visual for "this row
	// disagrees with itself", so it is preserved rather than folded into the on state.
	const ECheckBoxState State = IsChecked.Get(ECheckBoxState::Unchecked);
	if (State == ECheckBoxState::Undetermined)
	{
		return MixtormatPalette::FillBodyTop();
	}
	if (State == ECheckBoxState::Checked)
	{
		return IsHovered() ? MixtormatPalette::FillBodyHoverTop() : MixtormatPalette::FillBodyTop();
	}
	// Unchecked: no fill at all, rather than a fill at zero alpha. Same result, one less element.
	return FLinearColor::Transparent;
}

FLinearColor SMixtormatToggle::GetFillBottom() const
{
	const ECheckBoxState State = IsChecked.Get(ECheckBoxState::Unchecked);
	if (State == ECheckBoxState::Undetermined)
	{
		return MixtormatPalette::FillBodyBottom();
	}
	if (State == ECheckBoxState::Checked)
	{
		return IsHovered() ? MixtormatPalette::FillBodyHoverBottom() : MixtormatPalette::FillBodyBottom();
	}
	return FLinearColor::Transparent;
}
