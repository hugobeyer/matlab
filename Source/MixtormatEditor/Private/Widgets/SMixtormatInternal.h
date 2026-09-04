#pragma once

// Shared internals for the SMixtormat implementation files.
//
// SMixtormat.cpp had grown to ~8700 lines holding one class's 143 methods plus nine
// helper widgets. The methods now live in SMixtormat_<Area>.cpp files -- all still
// members of the same class, so the split needs no change to SMixtormat.h -- and the
// helper widgets and layout constants they share live here.

#include "Widgets/SMixtormat.h"
#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Compositing/MixtormatBakeService.h"
#include "ContentBrowserModule.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Editor.h"
#include "Components/MeshComponent.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "InputCoreTypes.h"
#include "Engine/Texture2D.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialEditingLibrary.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Services/MixtormatRegistry.h"
#include "Services/MixtormatSurfaceImporter.h"
#include "Style/MixtormatStyle.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Atoms/SMixtormatStatusDot.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UI/Controls/SMixtormatSlider.h"
#include "UI/Controls/SMixtormatTile.h"
#include "UI/Layers/MixtormatLayerBadges.h"
#include "UI/Layers/SMixtormatLayerChildRow.h"
#include "UI/Layers/SMixtormatLayerGroup.h"
#include "UI/Layers/SMixtormatLayerRow.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Containers/SMixtormatInspectorGroup.h"
#include "UI/DragDrop/MixtormatDragDropOps.h"
#include "UI/DragDrop/SMixtormatDropTargets.h"
#include "UI/Containers/SMixtormatInspectorWell.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "UObject/Package.h"

// The helper widgets below use LOCTEXT, so the namespace has to be live while they are
// declared. Each implementation file defines its own after including this.
#define LOCTEXT_NAMESPACE "SMixtormat"

namespace MixtormatUI
{
	constexpr float PanelPadding = 4.0f;
	constexpr float SplitterHandleSize = 1.0f;
	constexpr float SplitterHitSize = 5.0f;
	constexpr float LayerStackWidth = 240.0f;
	constexpr float InspectorWidth = 300.0f;
	constexpr float TopBarHeight = 32.0f;
	constexpr float StatusBarHeight = 18.0f;
	constexpr float MaskTileSize = 62.0f;

	inline FAssetThumbnailConfig CleanThumbnailConfig()
	{
		FAssetThumbnailConfig Config;
		Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
		Config.AllowAssetSpecificThumbnailOverlay = false;
		return Config;
	}

	inline const TCHAR* PackedMapLabel(const UMixtormatSurface& Surface)
	{
		switch (Surface.BlendHeightProvenance)
		{
		case EMixtormatBlendHeightProvenance::DerivedFromNormal:
			return TEXT("RAMH Derived");
		case EMixtormatBlendHeightProvenance::AuthoredRAMH:
			return TEXT("RAMH Authored");
		default:
			return Surface.bHasBlendHeight ? TEXT("RAMH Authored") : TEXT("RAM");
		}
	}

	inline FText HeightBlendSourceText(const UMixtormatSurface* Surface)
	{
		if (!Surface || !Surface->bHasBlendHeight)
		{
			return LOCTEXT(
				"HeightUsingConstantFallback",
				"Base · Previous Composite   Blend · Scalar Layer Height");
		}
		return Surface->BlendHeightProvenance
			== EMixtormatBlendHeightProvenance::DerivedFromNormal
			? LOCTEXT(
				"HeightUsingDerivedNormal",
				"Base · Previous Composite   Blend · Height derived from Normal")
			: LOCTEXT(
				"HeightUsingRAMH",
				"Base · Previous Composite   Blend · Authored RAMH alpha");
	}

	inline void ValidateHeightReferences(TArray<FMixtormatLayer>& Layers)
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			int32& ReferenceIndex = Layers[LayerIndex].HeightReferenceLayerIndex;
			if (ReferenceIndex < 0 || ReferenceIndex >= LayerIndex)
			{
				ReferenceIndex = INDEX_NONE;
			}
		}
	}

	inline void RemapHeightReferencesAfterInsert(TArray<FMixtormatLayer>& Layers, const int32 InsertIndex)
	{
		for (FMixtormatLayer& Layer : Layers)
		{
			if (Layer.HeightReferenceLayerIndex >= InsertIndex)
			{
				++Layer.HeightReferenceLayerIndex;
			}
		}
		ValidateHeightReferences(Layers);
	}

	inline void RemapHeightReferencesAfterDelete(TArray<FMixtormatLayer>& Layers, const int32 DeletedIndex)
	{
		for (FMixtormatLayer& Layer : Layers)
		{
			if (Layer.HeightReferenceLayerIndex == DeletedIndex)
			{
				Layer.HeightReferenceLayerIndex = INDEX_NONE;
			}
			else if (Layer.HeightReferenceLayerIndex > DeletedIndex)
			{
				--Layer.HeightReferenceLayerIndex;
			}
		}
		ValidateHeightReferences(Layers);
	}

	inline void RemapHeightReferencesAfterMove(
		TArray<FMixtormatLayer>& Layers,
		const int32 SourceIndex,
		const int32 TargetIndex)
	{
		for (FMixtormatLayer& Layer : Layers)
		{
			int32& ReferenceIndex = Layer.HeightReferenceLayerIndex;
			if (ReferenceIndex == SourceIndex)
			{
				ReferenceIndex = TargetIndex;
			}
			else if (SourceIndex < TargetIndex
				&& ReferenceIndex > SourceIndex
				&& ReferenceIndex <= TargetIndex)
			{
				--ReferenceIndex;
			}
			else if (TargetIndex < SourceIndex
				&& ReferenceIndex >= TargetIndex
				&& ReferenceIndex < SourceIndex)
			{
				++ReferenceIndex;
			}
		}
		ValidateHeightReferences(Layers);
	}

	inline const FSlateBrush* LucideIcon(const FName IconName)
	{
		static TMap<FName, TSharedPtr<FSlateVectorImageBrush>> Brushes;
		TSharedPtr<FSlateVectorImageBrush>& Brush = Brushes.FindOrAdd(IconName);
		if (!Brush.IsValid())
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MaterialLab"));
			const FString IconPath = FPaths::Combine(
				Plugin.IsValid() ? Plugin->GetBaseDir() : FString(),
				TEXT("Resources/Icons"),
				IconName.ToString() + TEXT(".svg"));
			Brush = MakeShared<FSlateVectorImageBrush>(
				IconPath,
				FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
		}
		return Brush.Get();
	}

	inline FText MaskBlendModeText(const EMixtormatMaskBlendMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatMaskBlendMode::Add: return LOCTEXT("MaskModeAdd", "Add");
		case EMixtormatMaskBlendMode::Subtract: return LOCTEXT("MaskModeSubtract", "Subtract");
		case EMixtormatMaskBlendMode::Multiply: return LOCTEXT("MaskModeMultiply", "Multiply");
		case EMixtormatMaskBlendMode::Min: return LOCTEXT("MaskModeMin", "Min");
		case EMixtormatMaskBlendMode::Max: return LOCTEXT("MaskModeMax", "Max");
		case EMixtormatMaskBlendMode::AddSub: return LOCTEXT("MaskModeAddSub", "Add/Sub");
		case EMixtormatMaskBlendMode::Overlay: return LOCTEXT("MaskModeOverlay", "Overlay");
		default: return LOCTEXT("MaskModeReplace", "Replace");
		}
	}

	inline FText GradeTonemapText(const EMixtormatGradeTonemap Mode)
	{
		switch (Mode)
		{
		case EMixtormatGradeTonemap::Reinhard: return LOCTEXT("GradeTmReinhard", "Reinhard");
		case EMixtormatGradeTonemap::ACES: return LOCTEXT("GradeTmACES", "ACES");
		case EMixtormatGradeTonemap::Filmic: return LOCTEXT("GradeTmFilmic", "Filmic");
		default: return LOCTEXT("GradeTmNone", "None");
		}
	}

	inline FText ErosionDirectionModeText(const EMixtormatErosionDirectionMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatErosionDirectionMode::Lerp: return LOCTEXT("EroDirLerp", "Lerp");
		case EMixtormatErosionDirectionMode::Flow: return LOCTEXT("EroDirFlow", "Flow");
		default: return LOCTEXT("EroDirWeight", "Weight");
		}
	}

	inline FText ErosionCurvatureModeText(const EMixtormatErosionCurvatureMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatErosionCurvatureMode::Valley: return LOCTEXT("EroCurvValley", "Valley");
		case EMixtormatErosionCurvatureMode::Ridge: return LOCTEXT("EroCurvRidge", "Ridge");
		default: return LOCTEXT("EroCurvMean", "Mean");
		}
	}
}

class SMixtormatBakeSettingsDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBakeSettingsDialog) {}
		SLATE_ARGUMENT(FMixtormatBakeSettings, InitialSettings)
		SLATE_ARGUMENT(int32, Resolution)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Settings = InArgs._InitialSettings;
		Resolution = InArgs._Resolution;
		ChildSlot
		[
			SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeDestinationLabel", "Destination Folder"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SAssignNew(DestinationTextBox, SEditableTextBox)
						.Text(FText::FromString(Settings.DestinationPath))
						.OnTextChanged_Lambda([this](const FText& Text)
						{
							Settings.DestinationPath = Text.ToString();
							ValidationText = FText::GetEmpty();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SComboButton)
						.OnGetMenuContent(this, &SMixtormatBakeSettingsDialog::BuildPathPicker)
						.ButtonContent()
						[
							SNew(STextBlock).Text(LOCTEXT("BrowseBakeDestination", "Browse..."))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeBaseNameLabel", "Output Base Name"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 10.0f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(Settings.BaseName))
					.OnTextChanged_Lambda([this](const FText& Text)
					{
						Settings.BaseName = Text.ToString();
						ValidationText = FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::Format(
							LOCTEXT("BakeSharedResolution", "Resolution: {0}K ({1} × {1}) · shared preview/bake"),
							FText::AsNumber(Resolution / 1024),
							FText::AsNumber(Resolution));
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeOutputPreviewLabel", "Generated Asset Names"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Join(
							FMixtormatBakeService::GetOutputAssetNames(Settings),
							TEXT("\n")));
					})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return ValidationText; })
					.ColorAndOpacity(MixtormatPalette::ErrorText())
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("CancelBakeSettings", "Cancel"))
						.OnClicked(this, &SMixtormatBakeSettingsDialog::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("AcceptBakeSettings", "Bake"))
						.OnClicked(this, &SMixtormatBakeSettingsDialog::Accept)
					]
				]
			]
		];
	}

	bool WasAccepted() const { return bAccepted; }
	const FMixtormatBakeSettings& GetSettings() const { return Settings; }

private:
	TSharedRef<SWidget> BuildPathPicker()
	{
		FPathPickerConfig Config;
		Config.DefaultPath = Settings.DestinationPath;
		Config.OnPathSelected = FOnPathSelected::CreateSP(
			this,
			&SMixtormatBakeSettingsDialog::SelectPath);
		FContentBrowserModule& ContentBrowserModule =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		return SNew(SBox)
			.WidthOverride(360.0f)
			.HeightOverride(420.0f)
			[ContentBrowserModule.Get().CreatePathPicker(Config)];
	}

	void SelectPath(const FString& Path)
	{
		Settings.DestinationPath = Path;
		DestinationTextBox->SetText(FText::FromString(Path));
		ValidationText = FText::GetEmpty();
		FSlateApplication::Get().DismissAllMenus();
	}

	FReply Accept()
	{
		Settings.DestinationPath.TrimStartAndEndInline();
		Settings.BaseName.TrimStartAndEndInline();
		while (Settings.DestinationPath.RemoveFromEnd(TEXT("/"))) {}
		FText Error;
		if (!FMixtormatBakeService::ValidateSettings(Settings, Error))
		{
			ValidationText = Error;
			return FReply::Handled();
		}
		bAccepted = true;
		CloseWindow();
		return FReply::Handled();
	}

	FReply Cancel()
	{
		CloseWindow();
		return FReply::Handled();
	}

	void CloseWindow()
	{
		if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
		{
			Window->RequestDestroyWindow();
		}
	}

	FMixtormatBakeSettings Settings;
	TSharedPtr<SEditableTextBox> DestinationTextBox;
	FText ValidationText;
	int32 Resolution = 2048;
	bool bAccepted = false;
};

enum class EMixtormatActionDialogResult : uint8
{
	Cancel,
	Confirm,
	Alternate
};

class SMixtormatActionDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatActionDialog) {}
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(FText, ConfirmLabel)
		SLATE_ARGUMENT(FText, CancelLabel)
		SLATE_ARGUMENT(FText, AlternateLabel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(InArgs._Message)
						.AutoWrapText(true)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(InArgs._CancelLabel)
						.OnClicked(this, &SMixtormatActionDialog::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Visibility(InArgs._AlternateLabel.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.Text(InArgs._AlternateLabel)
						.OnClicked(this, &SMixtormatActionDialog::Alternate)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(InArgs._ConfirmLabel)
						.OnClicked(this, &SMixtormatActionDialog::Confirm)
					]
				]
			]
		];
	}

	bool WasConfirmed() const { return Result == EMixtormatActionDialogResult::Confirm; }
	EMixtormatActionDialogResult GetResult() const { return Result; }

private:
	FReply Confirm()
	{
		Result = EMixtormatActionDialogResult::Confirm;
		CloseWindow();
		return FReply::Handled();
	}

	FReply Alternate()
	{
		Result = EMixtormatActionDialogResult::Alternate;
		CloseWindow();
		return FReply::Handled();
	}

	FReply Cancel()
	{
		CloseWindow();
		return FReply::Handled();
	}

	void CloseWindow()
	{
		if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
		{
			Window->RequestDestroyWindow();
		}
	}

	EMixtormatActionDialogResult Result = EMixtormatActionDialogResult::Cancel;
};

inline bool ShowMixtormatActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& CancelLabel)
{
	TSharedPtr<SMixtormatActionDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(560.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMixtormatActionDialog)
			.Message(Message)
			.ConfirmLabel(ConfirmLabel)
			.CancelLabel(CancelLabel)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->WasConfirmed();
}

inline EMixtormatActionDialogResult ShowMixtormatThreeActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& AlternateLabel,
	const FText& CancelLabel)
{
	TSharedPtr<SMixtormatActionDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(560.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMixtormatActionDialog)
			.Message(Message)
			.ConfirmLabel(ConfirmLabel)
			.AlternateLabel(AlternateLabel)
			.CancelLabel(CancelLabel)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->GetResult();
}

enum class EMixtormatBakeResultAction : uint8
{
	Close,
	Reveal,
	Open,
	Apply,
	Rebake
};

class SMixtormatBakeResultDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBakeResultDialog) {}
		SLATE_ARGUMENT(FText, Message)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(InArgs._Message)
						.AutoWrapText(true)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeActionButton(LOCTEXT("CloseBakeResult", "Close"), EMixtormatBakeResultAction::Close)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("RebakeBakeResult", "Re-bake"), EMixtormatBakeResultAction::Rebake)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("ApplyBakeResult", "Apply to Selected Actors"), EMixtormatBakeResultAction::Apply)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("OpenBakeResult", "Open Material Instance"), EMixtormatBakeResultAction::Open)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("RevealBakeResult", "Reveal Outputs"), EMixtormatBakeResultAction::Reveal)
					]
				]
			]
		];
	}

	EMixtormatBakeResultAction GetAction() const { return Action; }

private:
	TSharedRef<SWidget> MakeActionButton(
		const FText& Label,
		const EMixtormatBakeResultAction InAction)
	{
		return SNew(SButton)
			.Text(Label)
			.OnClicked_Lambda([this, InAction]()
			{
				Action = InAction;
				CloseWindow();
				return FReply::Handled();
			});
	}

	void CloseWindow()
	{
		if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
		{
			Window->RequestDestroyWindow();
		}
	}

	EMixtormatBakeResultAction Action = EMixtormatBakeResultAction::Close;
};

inline EMixtormatBakeResultAction ShowMixtormatBakeResultDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Message)
{
	TSharedPtr<SMixtormatBakeResultDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("BakeResultTitle", "Bake Complete"))
		.ClientSize(FVector2D(760.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMixtormatBakeResultDialog)
			.Message(Message)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->GetAction();
}

class SMixtormatTextureTile final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatTextureTile) {}
		SLATE_ARGUMENT(UObject*, Texture)
		SLATE_ARGUMENT(FVector2D, ImageSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Brush.SetResourceObject(InArgs._Texture);
		Brush.SetImageSize(InArgs._ImageSize);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ChildSlot[SNew(SImage).Image(&Brush)];
	}

private:
	FSlateBrush Brush;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceSelected,
	FText,
	FSoftObjectPath);
DECLARE_DELEGATE_OneParam(FOnMixtormatSurfaceGalleryZoom, int32);

class SMixtormatSurfaceCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSurfaceCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_NAMED_SLOT(FArguments, HoverContent)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, SurfacePath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMixtormatSurfaceSelected, OnSelected)
		SLATE_EVENT(FOnMixtormatSurfaceGalleryZoom, OnGalleryZoom)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		DisplayName = InArgs._DisplayName;
		SurfacePath = InArgs._SurfacePath;
		ThumbnailAsset = InArgs._ThumbnailAsset;
		ThumbnailPool = InArgs._ThumbnailPool;
		OnSelected = InArgs._OnSelected;
		OnGalleryZoom = InArgs._OnGalleryZoom;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				InArgs._Content.Widget
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.Visibility_Lambda([this]()
				{
					return IsHovered()
						? EVisibility::HitTestInvisible
						: EVisibility::Collapsed;
				})
				[
					InArgs._HoverContent.Widget
				]
			]
		];
	}

	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			if (OnSelected.IsBound())
			{
				OnSelected.Execute(DisplayName, SurfacePath);
			}
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}
		return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const int32 Direction = FMath::Sign(MouseEvent.GetWheelDelta());
		if (Direction != 0 && OnGalleryZoom.IsBound())
		{
			OnGalleryZoom.Execute(Direction);
			return FReply::Handled();
		}
		return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
	}

	virtual FReply OnDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled().BeginDragDrop(
			FMixtormatSurfaceDragDropOp::New(
				DisplayName,
				SurfacePath,
				ThumbnailAsset,
				ThumbnailPool));
	}

private:
	FText DisplayName;
	FSoftObjectPath SurfacePath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMixtormatSurfaceSelected OnSelected;
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
};

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatMaskSelected, int32, FSoftObjectPath);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatChildSelected, int32, int32);

class SMixtormatMaskCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatMaskCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, MaskPath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMixtormatMaskSelected, OnSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		LayerIndex = InArgs._LayerIndex;
		DisplayName = InArgs._DisplayName;
		MaskPath = InArgs._MaskPath;
		ThumbnailAsset = InArgs._ThumbnailAsset;
		ThumbnailPool = InArgs._ThumbnailPool;
		OnSelected = InArgs._OnSelected;
		ChildSlot
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return SCompoundWidget::OnPreviewMouseButtonDown(Geometry, Event);
		}
		if (OnSelected.IsBound())
		{
			OnSelected.Execute(LayerIndex, MaskPath);
		}
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return FReply::Handled().BeginDragDrop(
			FMixtormatMaskDragDropOp::New(DisplayName, MaskPath, ThumbnailAsset, ThumbnailPool));
	}

private:
	int32 LayerIndex = INDEX_NONE;
	FText DisplayName;
	FSoftObjectPath MaskPath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMixtormatMaskSelected OnSelected;
};

#undef LOCTEXT_NAMESPACE
