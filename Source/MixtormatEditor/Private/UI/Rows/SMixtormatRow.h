#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SWidget.h"
#include "Styling/SlateTypes.h"

// The inspector's row vocabulary.
//
// Every control in a panel is one of a handful of shapes, all the same height and all sharing one
// label column, so a panel of mixed controls reads as a single grid rather than as a pile of
// separately-authored widgets. SMixtormatSlider is the value row; this is everything else --
// toggles, pickers, swatches, buttons -- plus the furniture that groups them.
//
// The point is compositional: a panel calls these instead of assembling an SHorizontalBox with a
// label and a trailing control each time, which is what let row heights and label positions drift
// apart across panels in the first place.
namespace MixtormatRow
{
	// A label on the left and one trailing control on the right, at the shared row height. The
	// label ellipsizes rather than pushing the control off the row.
	TSharedRef<SWidget> Make(
		const FText& Label,
		const TSharedRef<SWidget>& TrailingContent,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	// A label that sits beside its control rather than across the row from it. The label column
	// exists so a run of values scans down one edge; a lone toggle has no value to line up with,
	// and stranding its label at the far left leaves the row reading as two unrelated halves.
	TSharedRef<SWidget> MakeTrailing(
		const FText& Label,
		const TSharedRef<SWidget>& TrailingContent,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	// Two rows side by side. For values whose labels are one short word -- at the inspector's
	// width each half is about 139px, so anything longer will clip.
	TSharedRef<SWidget> MakePair(
		const TSharedRef<SWidget>& Left,
		const TSharedRef<SWidget>& Right);

	// Names a run of rows. Costs more height than a hairline, so it is for groupings the row
	// labels do not already imply.
	TSharedRef<SWidget> MakeCaption(const FText& Caption);

	// Separates two runs without naming them. The cheap option.
	TSharedRef<SWidget> MakeHairline();

	// The inspector's boolean, sized for a row's trailing slot. See SMixtormatToggle.
	TSharedRef<SWidget> MakeCheckbox(
		const TAttribute<ECheckBoxState>& IsChecked,
		const FOnCheckStateChanged& OnStateChanged,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	// A combo chip: current value, optional leading thumbnail, disclosure arrow. Serves blend
	// modes, enums and asset pickers alike -- the thumbnail is what tells an asset chip apart,
	// and it is enough to confirm what is bound without opening the picker.
	TSharedRef<SWidget> MakeChip(
		const TAttribute<FText>& Text,
		const FOnGetContent& OnGetMenuContent,
		const TSharedPtr<SWidget>& LeadingContent = nullptr,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());
}
