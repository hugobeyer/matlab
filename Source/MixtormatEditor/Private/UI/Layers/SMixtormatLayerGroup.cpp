#include "UI/Layers/SMixtormatLayerGroup.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatLayerGroup::Construct(const FArguments& InArgs)
{
	const TAttribute<bool> bExpanded = InArgs._bExpanded;

	ChildSlot
	[
		SNew(SHorizontalBox)

		// The enclosing edge. One pixel, accent, full height of the group -- it is the only thing
		// that says these children belong to this layer.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::LayerEdgeWidth)
			.Visibility_Lambda([bExpanded]()
			{
				return bExpanded.Get(false) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.LayerEdge")))
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[InArgs._Header.Widget]
			+ SVerticalBox::Slot()
			.AutoHeight()
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

void SMixtormatLayerGroup::AddChild(const TSharedRef<SWidget>& Child)
{
	if (Children.IsValid())
	{
		// One pixel between children, matching the stack's own rhythm.
		Children->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::LayerRowGap, 0.0f, 0.0f)[Child];
	}
}
