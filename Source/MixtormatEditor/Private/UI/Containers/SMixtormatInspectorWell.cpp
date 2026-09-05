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
		.Padding(FMargin(0.0f, MixtormatTokens::InspectorTopMargin, 0.0f, 0.0f))
		[
			SAssignNew(Stack, SVerticalBox)
		]
	];
}

void SMixtormatInspectorWell::AddGroup(const TSharedRef<SWidget>& Group)
{
	if (Stack.IsValid())
	{
		// Groups used to stack flush, which is why the column read as one sheet. They are rounded
		// blocks now, and a rounded corner needs something to be rounded against: this gap is
		// where the darker surround shows through.
		Stack->AddSlot()
			.AutoHeight()
			.Padding(
				MixtormatTokens::GroupOuterGap,
				0.0f,
				MixtormatTokens::GroupOuterGap,
				MixtormatTokens::GroupOuterGap)
			[Group];
	}
}

void SMixtormatInspectorWell::ClearGroups()
{
	if (Stack.IsValid())
	{
		Stack->ClearChildren();
	}
}
