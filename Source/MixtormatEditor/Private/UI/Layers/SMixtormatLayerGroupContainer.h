// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/Containers/MixtormatGroupCardPainter.h"

class SBox;
class SVerticalBox;

// One rounded sheet around the existing header target and member targets.
class SMixtormatLayerGroupContainer final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerGroupContainer) {}
		SLATE_NAMED_SLOT(FArguments, Header)
		SLATE_NAMED_SLOT(FArguments, Body)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
		FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;

private:
	TSharedPtr<SVerticalBox> Stack;
	TSharedPtr<SBox> HeaderBox;
	TSharedPtr<SBox> BodyBox;
	mutable MixtormatGroupCard::FSurfacePainter SurfacePainter;
};
