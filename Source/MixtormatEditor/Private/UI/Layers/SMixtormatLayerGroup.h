#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

// A layer and everything inside it.
//
// The accent edge down the left is what encloses a layer's masks and effects -- without it an
// expanded layer's children read as siblings of the next layer rather than as its contents.
class SMixtormatLayerGroup final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerGroup)
		: _bExpanded(false)
	{}
		SLATE_ATTRIBUTE(bool, bExpanded)
		SLATE_NAMED_SLOT(FArguments, Header)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void AddChild(const TSharedRef<SWidget>& Child);

private:
	TSharedPtr<SVerticalBox> Children;
};
