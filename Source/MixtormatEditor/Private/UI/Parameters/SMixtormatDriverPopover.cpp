// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/SMixtormatDriverPopover.h"

#include "Style/MixtormatDesignTokens.h"

#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Parameters/MixtormatParameterPresentation.h"

void SMixtormatDriverPopover::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SMixtormatMenuPanel)
		.MinWidth(InArgs._Width)
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().MenuLayout.PanelPadding))
		[
			MixtormatParameterPresentation::MakeScrollableContent(
				SNew(SMixtormatInspectorCard)
				.CompactLayout(true)
				.Title(InArgs._Title)
				[
					InArgs._Content.Widget
				], InArgs._Width)
		]
	];
}
