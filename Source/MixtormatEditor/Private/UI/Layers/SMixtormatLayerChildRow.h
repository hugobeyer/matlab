#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;

// A mask, effect or generated mask inside a layer.
//
// Shorter than a layer row and indented under it, with the same three fields -- name, kind, badge
// -- and a status dot instead of an eye. Selected children carry the tint from their right edge
// rather than their top, so a selected child never looks like a small layer.
class SMixtormatLayerChildRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerChildRow)
		: _Icon(nullptr)
		, _bActive(true)
		, _bSelected(false)
	{}
		SLATE_ARGUMENT(const FSlateBrush*, Icon)
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(FText, Kind)
		SLATE_ATTRIBUTE(FText, Badge)
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FLinearColor GetTintStart() const;
	FLinearColor GetTintEnd() const;

	TAttribute<bool> bSelected;
	FSimpleDelegate OnSelected;
};
