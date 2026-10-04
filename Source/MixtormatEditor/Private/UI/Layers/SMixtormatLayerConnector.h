// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/SLeafWidget.h"

// Legacy tee/elbow choice remains caller-owned; only the baked PNG stroke is replaced.
class SMixtormatLayerConnector final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerConnector) : _bLast(false) {}
		SLATE_ARGUMENT(bool, bLast)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { bLast = InArgs._bLast; }
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
	bool bLast = false;
};
