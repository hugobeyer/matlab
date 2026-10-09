// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatOutputReference.h"
#include "Widgets/MixtormatChildAddress.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SScrollBox;
class SVerticalBox;

// Presentation-only snapshot. Ordering keys are never used to resolve an activation.
struct FMixtormatStructuralSourcePickerEntry
{
	FMixtormatOutputReference Source;
	FMixtormatChildAddress SourceAddress;
	FText Label;
	FText LayerLabel;
	FText ToolTip;
	FString SearchText;
	int32 LayerIndex = INDEX_NONE;
	bool bAvailable = false;
};

DECLARE_DELEGATE_OneParam(FOnMixtormatStructuralSourcePicked, const FMixtormatOutputReference&);

class SMixtormatStructuralSourcePicker final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatStructuralSourcePicker) : _bPreviewCurrent(true) {}
		SLATE_ARGUMENT(FText, Caption)
		SLATE_ARGUMENT(TArray<FMixtormatStructuralSourcePickerEntry>, Entries)
		SLATE_EVENT(FOnMixtormatStructuralSourcePicked, OnSourcePicked)
		SLATE_ARGUMENT(TSharedPtr<FMixtormatStructuralEndpointPreview>, EndpointPreview)
		SLATE_EVENT(FSimpleDelegate, OnPreviewChanged)
		SLATE_ATTRIBUTE(bool, bPreviewCurrent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMixtormatStructuralSourcePicker() override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
	void RebuildList();
	void Activate(const FMixtormatOutputReference& Source);
	void Navigate(int32 Direction);
	void RefreshEndpointPreview();
	void ReleaseEndpointPreview();
	TSharedRef<SWidget> MakeCaption(const TAttribute<FText>& Text) const;

	TArray<FMixtormatStructuralSourcePickerEntry> Entries;
	TArray<int32> EligibleEntries;
	TArray<float> EligibleOffsets;
	TSharedPtr<SEditableTextBox> Search;
	TSharedPtr<SScrollBox> Scroll;
	TSharedPtr<SVerticalBox> Rows;
	FOnMixtormatStructuralSourcePicked OnSourcePicked;
	TSharedPtr<FMixtormatStructuralEndpointPreview> EndpointPreview;
	FSimpleDelegate OnPreviewChanged;
	TAttribute<bool> bPreviewCurrent;
	FMixtormatChildAddress HoveredSource;
	FString Filter;
	int32 ActiveEntry = INDEX_NONE;
};
