// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/SMixtormatToggle.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
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
					// The fill is the same recipe the slider paints, at the toggle's inset size --
					// one vocabulary for both. Painting a recipe rather than two colours is what
					// lets it be Additive over a Multiply recess rather than a translucent wash.
					SNew(SMixtormatSurfaceBox)
					.Recipe(this, &SMixtormatToggle::GetFillRecipe)
					// The well's rim must stay continuous across the fill, so this surface's own
					// borders are not drawn.
					.PaintBorders(false)
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

// Hover lifts the fill as well as the well behind it. Both read from the same pointer state, so
		// a toggle that brightened only its rim would look like a different control. The fill covers
		// the well, so it has to carry the well's state as well as its own.
	Mixtormat::FMixtormatSurfaceRecipe SMixtormatToggle::GetFillRecipe() const
	{
		const ECheckBoxState State = IsChecked.Get(ECheckBoxState::Unchecked);

	// Unchecked: the fill layer is switched off rather than the surface dropped, so the padding
	// and the content slot stay exactly as they are and only the paint goes away.
	if (State != ECheckBoxState::Checked && State != ECheckBoxState::Undetermined)
	{
		Mixtormat::FMixtormatSurfaceRecipe Empty;
		Empty.Base = Mixtormat::MakeColorRef(Mixtormat::EMixtormatColorRole::Ground);
		// AddDefaulted then Last, not AddDefaulted().bEnabled: AddDefaulted returns the new
				// element's index, not a reference to it.
				Empty.Layers.AddDefaulted();
				Empty.Layers.Last().bEnabled = false;
		return Empty;
	}

	const Mixtormat::EMixtormatWellState WellState =
		IsHovered() ? Mixtormat::EMixtormatWellState::Hover : Mixtormat::EMixtormatWellState::Rest;

	const Mixtormat::EMixtormatFillState FillState =
		!IsEnabled() ? Mixtormat::EMixtormatFillState::Disabled
		: IsHovered() ? Mixtormat::EMixtormatFillState::Hover
		: Mixtormat::EMixtormatFillState::Rest;

	return Mixtormat::MakeCheckedToggleRecipe(
		FMixtormatThemeStore::GetTheme(), WellState, FillState);
}
