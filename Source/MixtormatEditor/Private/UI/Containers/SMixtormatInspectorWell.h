#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

// The shell every inspector group stacks inside.
//
// Groups used to float on the window colour with no container of their own, which is why the
// column had no edge and each group read as a separate card. Here they stack flush -- no gutter,
// no rounding on the internal joins -- so the column reads as sheets layered on one another, and
// each header's additive hairline becomes the seam.
class SMixtormatInspectorWell final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatInspectorWell) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Groups are appended rather than declared inline: a panel builds its groups conditionally,
	// and a declarative list cannot express "only when a peel is selected".
	void AddGroup(const TSharedRef<SWidget>& Group);
	void ClearGroups();

private:
	TSharedPtr<SVerticalBox> Stack;
};
