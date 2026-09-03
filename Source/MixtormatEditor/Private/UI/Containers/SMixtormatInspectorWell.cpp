#include "UI/Containers/SMixtormatInspectorWell.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatInspectorWell::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.InspectorWell")))
		.Padding(FMargin(0.0f))
		[
			SAssignNew(Stack, SVerticalBox)
		]
	];
}

void SMixtormatInspectorWell::AddGroup(const TSharedRef<SWidget>& Group)
{
	if (Stack.IsValid())
	{
		// No padding between groups: flush stacking is the whole point of the well.
		Stack->AddSlot().AutoHeight()[Group];
	}
}

void SMixtormatInspectorWell::ClearGroups()
{
	if (Stack.IsValid())
	{
		Stack->ClearChildren();
	}
}
