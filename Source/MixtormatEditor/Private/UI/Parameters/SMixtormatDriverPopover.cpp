// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/SMixtormatDriverPopover.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatDriverPopover::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SMixtormatMenuPanel)
		.MinWidth(MixtormatTokens::DriverPopoverWidth)
		.Padding(FMargin(MixtormatTokens::MenuPanelPadding))
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::DriverPopoverWidth)
			[
				SNew(SMixtormatInspectorCard)
				.Title(InArgs._Title)
				[
					InArgs._Content.Widget
				]
			]
		]
	];
}
