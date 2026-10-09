// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// The UI STYLE eye's flash: a hit-test-invisible overlay above the workspace that outlines the
// located widget's exact bounds -- a translucent orange fill under a 1px border -- pulsing twice
// before it expires. Lives at the shell root so dock-tab clipping cannot cut it off.
class SMixtormatLocatorOutline final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLocatorOutline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime,
		float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	// Kept from Construct: FindWidgetWindow needs a shared ref, which a const OnPaint cannot
	// produce for itself.
	TWeakPtr<SWidget> Self;
};
