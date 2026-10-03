// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Related inspector values on a continuous stepped sheet; popovers opt into the compact layout.
class SMixtormatInspectorCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatInspectorCard) : _CompactLayout(false) {}
		// Empty for a card that groups without naming -- a single control that needs the sheet
		// but has nothing to be called that its own row does not already say. Any HeaderAction
		// still gets its line.
		SLATE_ATTRIBUTE(FText, Title)

	SLATE_ARGUMENT(TSharedPtr<SWidget>, LeadingHeaderContent)
	SLATE_ARGUMENT(bool, CompactLayout)

		// Sits at the right end of the title line: a preview toggle, a reset. Optional.
		SLATE_ARGUMENT(TSharedPtr<SWidget>, HeaderAction)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bCompactLayout = false;
};
