// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/SMixtormatDriverPopover.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"

void SMixtormatDriverPopover::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SMixtormatMenuPanel)
		.MinWidth(InArgs._Width)
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().MenuLayout.PanelPadding))
		[
			SNew(SBox)
			.WidthOverride(InArgs._Width)
			.MaxDesiredHeight(MixtormatTokens::ParameterPopoverMaxHeight)
			[
				SNew(SScrollBox)
				.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
				.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
				+ SScrollBox::Slot()
				[
					SNew(SMixtormatInspectorCard)
					.CompactLayout(true)
					.Title(InArgs._Title)
					[
						InArgs._Content.Widget
					]
				]
			]
		]
	];
}
