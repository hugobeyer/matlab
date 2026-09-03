#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;

// A mask, effect or generated mask inside a layer.
//
// Shorter than a layer row and indented under it, with the same three fields -- name, kind, badge
// -- and a status dot instead of an eye. Selected children carry the tint from their right edge
// rather than their top, so a selected child never looks like a small layer.
//
// Like the layer row, the body of this one is the drag source and the right button is where its
// actions live. There is no grip and no overflow button: at 20px tall they would leave the name
// almost no width, and the child is reordered by dragging it rather than by aiming at a handle.
class SMixtormatLayerChildRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerChildRow)
		: _bActive(true)
		, _bSelected(false)
	{}
		// A widget rather than a brush: a mask child shows the mask's own thumbnail, which comes
		// from FAssetThumbnail and has no FSlateBrush* to pass.
		SLATE_NAMED_SLOT(FArguments, Icon)
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(FText, Kind)
		SLATE_ATTRIBUTE(FText, Badge)
		SLATE_ATTRIBUTE(bool, bActive)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		// The dot is the child's enable toggle -- it already shows the state, so it takes the
		// click too rather than adding a checkbox the row has no room for.
		SLATE_EVENT(FSimpleDelegate, OnToggleActive)
		SLATE_EVENT(FPointerEventHandler, OnDragDetected)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FLinearColor GetTintStart() const;
	FLinearColor GetTintEnd() const;

	TAttribute<bool> bSelected;
	FSimpleDelegate OnSelected;
	FPointerEventHandler OnRowDragDetected;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};
