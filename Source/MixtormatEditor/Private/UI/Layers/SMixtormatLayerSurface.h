// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Style/MixtormatRecipes.h"

// Paint only: content retains its layout, input routing and drop-target geometry.
class SMixtormatLayerSurface final : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMixtormatLayerSurface)
		: _Kind(Mixtormat::EMixtormatLayerKind::Layer), _GroupTint(FLinearColor::Transparent)
		, _bVisible(true), _bReference(false), _bInstanceSource(false)
		, _bSelected(false), _bHovered(false)
	{}
		SLATE_ARGUMENT(Mixtormat::EMixtormatLayerKind, Kind)
		SLATE_ATTRIBUTE(FLinearColor, GroupTint)
		SLATE_ATTRIBUTE(bool, bVisible)
		SLATE_ATTRIBUTE(bool, bReference)
		SLATE_ATTRIBUTE(bool, bInstanceSource)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_ATTRIBUTE(bool, bHovered)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override;

private:
	Mixtormat::EMixtormatLayerKind Kind;
	TAttribute<FLinearColor> GroupTint;
	TAttribute<bool> bSelected, bHovered, bVisible, bReference, bInstanceSource;
};
