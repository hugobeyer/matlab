// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerContainer.h"

#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatLayerContainer::Construct(const FArguments& InArgs)
{
	const TAttribute<bool> bExpanded = InArgs._bExpanded;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.InsetPanel")))
		[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[InArgs._Header.Widget]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Children, SVerticalBox)
					.Visibility_Lambda([bExpanded]()
					{
						return bExpanded.Get(false) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
		]
	];
}

void SMixtormatLayerContainer::AddChild(const TSharedRef<SWidget>& Child)
{
	if (Children.IsValid())
	{
		Children->AddSlot().AutoHeight().Padding(0.0f, FMixtormatThemeStore::GetResolved().LayerLayout.Gap, 0.0f, 0.0f)[Child];
	}
}
