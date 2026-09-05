#include "UI/Atoms/SMixtormatToggle.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
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
			// The well. Same gradient and radius as a chip or a slider trough, because it is the
			// same thing: a recess in the panel that a value sits in.
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.Orientation(Orient_Vertical)
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(FMargin(MixtormatTokens::ToggleFillInset))
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::ToggleSize)
				.HeightOverride(MixtormatTokens::ToggleSize)
				[
					// Sized explicitly: a gradient box has no intrinsic size of its own, so an
					// unsized one here would collapse to nothing and the toggle would never
					// appear to fill.
					SNew(SBox)
					.WidthOverride(MixtormatTokens::ToggleFillSize)
					.HeightOverride(MixtormatTokens::ToggleFillSize)
					.Visibility(this, &SMixtormatToggle::GetFillVisibility)
					[
						SNew(SMixtormatGradientBox)
						.StartColor(this, &SMixtormatToggle::GetFillTop)
						.EndColor(this, &SMixtormatToggle::GetFillBottom)
						.Orientation(Orient_Vertical)
						.CornerRadius(MixtormatTokens::CornerRadiusInner)
					]
				]
			]
		]
	];
}

bool SMixtormatToggle::IsFilled() const
{
	return IsChecked.Get(ECheckBoxState::Unchecked) != ECheckBoxState::Unchecked;
}

EVisibility SMixtormatToggle::GetFillVisibility() const
{
	// Hit testing belongs to the checkbox around it; the fill is decoration and must not eat the
	// click that lands on it.
	return IsFilled() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
}

FLinearColor SMixtormatToggle::GetFillTop() const
{
	if (IsChecked.Get(ECheckBoxState::Unchecked) == ECheckBoxState::Undetermined)
	{
		return MixtormatPalette::FillTop();
	}
	return IsHovered() ? MixtormatPalette::FillTopHover() : MixtormatPalette::FillTop();
}

FLinearColor SMixtormatToggle::GetFillBottom() const
{
	if (IsChecked.Get(ECheckBoxState::Unchecked) == ECheckBoxState::Undetermined)
	{
		return MixtormatPalette::FillBottom();
	}
	return IsHovered() ? MixtormatPalette::FillBottomHover() : MixtormatPalette::FillBottom();
}
