#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// One run of related values, titled, on its own sheet inside a group body.
//
// The level below the group. A group answers "which part of the layer is this" -- surface
// adjustments, colour -- and a card answers "which run of values is this": Transform, Roughness,
// Normal. Those runs were previously separated by a caption row, which named a run but did not
// contain it, so a long group read as one column of sliders with words dropped into it at
// intervals and nothing saying where a run ended.
//
// The card is what carries the contrast now. The group body is the same colour as the column it
// sits in, so a card is the only lit surface in the panel: ground, sheets on the ground, and the
// wells cut into the sheets.
//
// Anatomy is a title line above the sheet, not inside it. Inside, the title sat on the same lit
// surface as the values and read as another row of the card -- a heading has to be outside the
// thing it heads to look like a label for it. It also means a card carrying an action, like a
// preview eye, has somewhere to put it: the far end of that same line, above the sheet.
class SMixtormatInspectorCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatInspectorCard) {}
		// Empty for a card that groups without naming -- a single control that needs the sheet
		// but has nothing to be called that its own row does not already say. Any HeaderAction
		// still gets its line.
		SLATE_ARGUMENT(FText, Title)

		// Sits at the right end of the title line: a preview toggle, a reset. Optional.
		SLATE_ARGUMENT(TSharedPtr<SWidget>, HeaderAction)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
