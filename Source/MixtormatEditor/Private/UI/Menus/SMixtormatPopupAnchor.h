// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"

// A menu anchor that opens beside the pointer and keeps its popup inside the owning dock tab.
// Reusing the current Slate window makes that window the final hard clipping boundary.
class SMixtormatPopupAnchor final : public SMenuAnchor
{
public:
	SLATE_BEGIN_ARGS(SMixtormatPopupAnchor)
		: _Placement(MenuPlacement_BelowRightAnchor)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_ARGUMENT(EMenuPlacement, Placement)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void OpenAt(const FVector2D& ScreenPosition, bool bFocusMenu = true);

private:
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

	TSharedRef<SWidget> BuildConstrainedContent();
	FSlateRect GetPopupBoundary() const;
	FOptionalSize GetMaximumPopupHeight() const;

	FOnGetContent GetMenuContent;
	FVector2D SummonPosition = FVector2D::ZeroVector;
	bool bHasSummonPosition = false;
};
