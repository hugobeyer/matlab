// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// Paint only: content retains its layout, input routing and drop-target geometry.
class SMixtormatLayerSurface final : public SCompoundWidget
{
public:
	enum class EKind : uint8 { Layer, Group, Child };
	SLATE_BEGIN_ARGS(SMixtormatLayerSurface)
		: _Kind(EKind::Layer), _StartColor(FLinearColor::Transparent)
		, _EndColor(FLinearColor::Transparent), _CrossColor(FLinearColor::Transparent)
		, _bSelected(false), _bHovered(false)
	{}
		SLATE_ARGUMENT(EKind, Kind)
		SLATE_ATTRIBUTE(FLinearColor, StartColor)
		SLATE_ATTRIBUTE(FLinearColor, EndColor)
		SLATE_ATTRIBUTE(FLinearColor, CrossColor)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_ATTRIBUTE(bool, bHovered)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override;

private:
	EKind Kind;
	TAttribute<FLinearColor> StartColor, EndColor, CrossColor;
	TAttribute<bool> bSelected, bHovered;
};
