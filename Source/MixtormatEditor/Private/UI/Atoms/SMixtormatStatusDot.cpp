#include "UI/Atoms/SMixtormatStatusDot.h"

#include "Style/MixtormatStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

void SMixtormatStatusDot::Construct(const FArguments& InArgs)
{
	bFilled = InArgs._bFilled;
	if (InArgs._ToolTip.IsSet())
	{
		SetToolTipText(InArgs._ToolTip);
	}

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(InArgs._Size)
		.HeightOverride(InArgs._Size)
		[
			SNew(SImage).Image(this, &SMixtormatStatusDot::GetBrush)
		]
	];
}

const FSlateBrush* SMixtormatStatusDot::GetBrush() const
{
	return FMixtormatStyle::Get().GetBrush(bFilled.Get(false)
		? TEXT("Mixtormat.StatusDot.Filled")
		: TEXT("Mixtormat.StatusDot.Hollow"));
}
