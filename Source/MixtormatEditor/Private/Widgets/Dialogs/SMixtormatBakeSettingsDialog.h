// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Compositing/MixtormatBakeService.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

class SMixtormatBakeSettingsDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBakeSettingsDialog) {}
		SLATE_ARGUMENT(FMixtormatBakeSettings, InitialSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool WasAccepted() const { return bAccepted; }
	const FMixtormatBakeSettings& GetSettings() const { return Settings; }

private:
	TSharedRef<SWidget> BuildPathPicker();
	void SelectPath(const FString& Path);
	FReply Accept();
	FReply Cancel();
	void CloseWindow();

	FMixtormatBakeSettings Settings;
	TSharedPtr<SEditableTextBox> DestinationTextBox;
	FText ValidationText;
	bool bAccepted = false;
};
