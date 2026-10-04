// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatThemeSchema.h"
#include "Style/MixtormatStyleLocator.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;

// Stage-9 UI STYLE editor. The widget name stays stable until Stage 10 so the window owner does not
// churn, but the old LiveTheme registry is no longer involved: every row edits FMixtormatTheme.
class SMixtormatLiveThemePanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLiveThemePanel) : _CanEdit(true) {}
		SLATE_ATTRIBUTE(bool, CanEdit)
		SLATE_EVENT(FSimpleDelegate, OnThemeChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMixtormatLiveThemePanel() override;

private:
	bool PropertyVisible(const Mixtormat::FMixtormatThemeProperty& Property) const;
	bool SectionVisible(Mixtormat::EMixtormatThemeTab Tab, const FString& Section) const;
	bool IsDefault(const Mixtormat::FMixtormatThemeProperty& Property) const;
	float NumberValue(const Mixtormat::FMixtormatThemeProperty& Property) const;

	void PreviewNumber(FName Id, float Value);
	void CommitNumber(FName Id, float Value);
	void CommitBool(FName Id, bool Value);
	void CommitChoice(FName Id, int32 Value);
	void CommitColor(FLinearColor Value, FName Id);
	void ResetProperty(FName Id);
	void CommitTheme(Mixtormat::FMixtormatTheme Theme, const FString& Message);

	FReply OpenColor(FName Id);
	FReply Save();
	FReply Load();
	FReply ResetAll();
	void SelectTab(int32 Index);
	void LocateTarget(Mixtormat::EMixtormatStyleTarget Target);
	EActiveTimerReturnType AdvanceLocateFlash(double CurrentTime, float DeltaTime);
	void UpdateStatus(const FString& Prefix);

	TSharedRef<SWidget> MakePropertyRow(const Mixtormat::FMixtormatThemeProperty& Property);

	TAttribute<bool> CanEdit;
	FSimpleDelegate OnThemeChanged;
	int32 SelectedTab = INDEX_NONE; // INDEX_NONE = All, otherwise enum index.
	FString Filter;
	FString Status;
	TMap<FName, float> PendingNumbers;
	TSharedPtr<SScrollBox> PropertyScroll;
	Mixtormat::EMixtormatStyleTarget LocatedTarget = Mixtormat::EMixtormatStyleTarget::None;
	int32 LocateFlashPhase = INDEX_NONE;
};
