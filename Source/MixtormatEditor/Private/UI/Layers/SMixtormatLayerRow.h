#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;

// One layer in the stack.
//
// Three text fields, answering three different questions, which is what lets a stack of layers all
// named "Untitled" still be readable:
//
//   Name     what the user called it
//   Source   what it is made of      -- "Mat - Rust Orange", "FILL"
//   Badge    how it composites       -- BLEND / OVER / COAT / DETAIL
//
// The badge is the only derived field: it is not typed, it is read from the layer's composition
// mode, and it is fixed-width so the badges form a scannable column down the right edge.
class SMixtormatLayerRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerRow)
		: _bEnabled(true)
		, _bExpanded(false)
		, _bSelected(false)
		, _Thumbnail(nullptr)
	{}
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(FText, Source)
		SLATE_ATTRIBUTE(FText, Badge)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ATTRIBUTE(bool, bExpanded)
		SLATE_ATTRIBUTE(bool, bSelected)
		SLATE_ATTRIBUTE(const FSlateBrush*, Thumbnail)
		SLATE_EVENT(FSimpleDelegate, OnToggleEnabled)
		SLATE_EVENT(FSimpleDelegate, OnToggleExpanded)
		SLATE_EVENT(FSimpleDelegate, OnSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FLinearColor GetBackgroundStart() const;
	FLinearColor GetBackgroundEnd() const;
	FSlateColor GetNameColor() const;

	TAttribute<bool> bLayerEnabled;
	TAttribute<bool> bExpanded;
	TAttribute<bool> bSelected;
	FSimpleDelegate OnToggleExpanded;
	FSimpleDelegate OnSelected;
};
