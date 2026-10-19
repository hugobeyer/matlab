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
	TSharedRef<SWidget> BuildParameterMappingDropdown(FMixtormatBakeMaterialMapping& Mapping, const FText& OutputLabel);
	TSharedRef<SWidget> BuildParameterMenu(FMixtormatBakeMaterialMapping* TargetMapping);
	void OnCustomMasterChanged(const FAssetData& AssetData);
	void RefreshDiscoveredParameters();
	FText GetParameterDisplayLabel(const FMixtormatBakeMaterialMapping& Mapping) const;
	FReply Accept();
	FReply Cancel();
	void CloseWindow();

	FMixtormatBakeSettings Settings;
	TArray<FMixtormatDiscoveredTextureParameter> DiscoveredParameters;
	TSharedPtr<SEditableTextBox> DestinationTextBox;
	FText ValidationText;
	bool bAccepted = false;
};
