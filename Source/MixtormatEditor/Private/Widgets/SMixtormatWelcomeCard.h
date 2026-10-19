// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;

// Dismissible first-run welcome card providing workspace overview and quick guide.
class SMixtormatWelcomeCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatWelcomeCard) {}
		SLATE_EVENT(FSimpleDelegate, OnDismiss)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildOverviewView();
	TSharedRef<SWidget> BuildGuideView();
	TSharedRef<SWidget> BuildFeatureRow(const FText& Title, const FText& Description);
	TSharedRef<SWidget> BuildGuideContent(int32 TopicIndex);

	void SwitchToGuide();
	void SwitchToOverview();
	void HandleDismiss();

	FSimpleDelegate OnDismiss;
	TSharedPtr<SWidgetSwitcher> ViewSwitcher;
	TSharedPtr<SWidgetSwitcher> GuideTopicSwitcher;
	int32 ActiveTopicIndex = 0;
};
