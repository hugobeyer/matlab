// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SMixtormatPopupAnchor;

// Wraps an existing inspector control with the tiny procedural-state affordance used by
// references and drivers. The existing slider/toggle remains untouched; this only adds the state
// slot and owns the two popovers (RMB actions and the left-click Driver editor).
class SMixtormatParameterControl final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatParameterControl)
		: _bHasTarget(true)
		, _bReferenced(false)
		, _bDriven(false)
		, _bBroken(false)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		// Whether this row currently addresses a parameter at all. False before anything is
		// selected: the state slot collapses and the press falls through, so the group around it
		// answers the right-click the way it did before this wrapper existed.
		SLATE_ATTRIBUTE(bool, bHasTarget)
		SLATE_ATTRIBUTE(bool, bReferenced)
		SLATE_ATTRIBUTE(bool, bDriven)
		SLATE_ATTRIBUTE(bool, bBroken)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
		SLATE_EVENT(FOnGetContent, OnGetDriverContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	// Broken outranks referenced outranks driven. One enum so the brush and the tooltip cannot
	// disagree -- they were two independent if-chains, which is how a red dot ends up explaining
	// itself as a driver.
	enum class EState : uint8 { None, Broken, Referenced, Driven };

	EState GetState() const;
	FName GetStateBrushName() const;
	EVisibility GetStateVisibility() const;
	FText GetStateToolTip() const;
	void OpenDriverPopover();

	TAttribute<bool> bHasTarget;
	TAttribute<bool> bReferenced;
	TAttribute<bool> bDriven;
	TAttribute<bool> bBroken;
	TSharedPtr<SMixtormatPopupAnchor> ContextAnchor;
	TSharedPtr<SMixtormatPopupAnchor> DriverAnchor;
};
