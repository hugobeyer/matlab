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
#include "Engine/TextureCube.h"
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
	constexpr float InspectorWidth = 292.0f;
	constexpr float TopBarHeight = 32.0f;
	constexpr float StatusBarHeight = 18.0f;
	constexpr float MaterialTileSize = 90.0f;
	constexpr float MaterialThumbnailSize = 64.0f;
	constexpr float MaskTileSize = 62.0f;

	inline FAssetThumbnailConfig CleanThumbnailConfig()
	{
		FAssetThumbnailConfig Config;
		Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
		Config.bAllowAssetSpecificThumbnailOverlay = false;
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
			Brush = MakeShared<FSlateVectorImageBrush>(IconPath, FVector2D(16.0f, 16.0f));
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
					.ColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f))
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

class SMixtormatInspectorGroup final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatInspectorGroup)
		: _InitiallyExpanded(false)
	{}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(bool, InitiallyExpanded)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, HeaderAction)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		bExpanded = InArgs._InitiallyExpanded;
		TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				// Fully transparent in every state: the highlight belongs to the header bar
				// behind it, so hovering anywhere on the bar reads as one surface rather than
				// lighting a button-shaped patch inside it.
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
				.ContentPadding(FMargin(0.0f))
				.OnClicked(this, &SMixtormatInspectorGroup::ToggleExpanded)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(10.0f)
						.HeightOverride(10.0f)
						[
							SNew(SImage)
							.Image_Lambda([this]()
							{
								return FAppStyle::GetBrush(bExpanded
									? TEXT("Icons.ChevronDown")
									: TEXT("Icons.ChevronRight"));
							})
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Title)
						.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.SectionHeader")))
					]
				]
			];
		if (InArgs._HeaderAction.IsValid())
		{
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 2.0f, 0.0f)
			[
				InArgs._HeaderAction.ToSharedRef()
			];
		}

		ChildSlot
		.Padding(FMargin(0.0f))
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.InspectorGroup")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					// Fixed-height bar, rounded on its top corners only so it meets the body
					// square. Padding matches the inspector gutter, so the title lines up with
					// the row labels underneath it.
					SNew(SBox)
					.HeightOverride(MixtormatTokens::GroupHeaderHeight)
					[
						SNew(SBorder)
						.Padding(FMargin(MixtormatTokens::PanelGutter, 0.0f))
						.VAlign(VAlign_Center)
						.BorderImage_Lambda([this]()
						{
							return FMixtormatStyle::Get().GetBrush(IsHovered()
								? TEXT("Mixtormat.InspectorGroupHeaderHovered")
								: TEXT("Mixtormat.InspectorGroupHeader"));
						})
						[Header]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
					.Padding(FMargin(
						MixtormatTokens::PanelGutter,
						6.0f,
						MixtormatTokens::PanelGutter,
						MixtormatTokens::PanelGutter))
					[InArgs._Content.Widget]
				]
			]
		];
	}

private:
	FReply ToggleExpanded()
	{
		bExpanded = !bExpanded;
		return FReply::Handled();
	}

	bool bExpanded = false;
};


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

class FMixtormatSurfaceDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatSurfaceDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath SurfacePath;

	static TSharedRef<FMixtormatSurfaceDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InSurfacePath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatSurfaceDragDropOp> Operation =
			MakeShared<FMixtormatSurfaceDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->SurfacePath = InSurfacePath;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("DropSurfaceLayer", "Add {0} to Layers"),
			InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f))
			.Size(FVector2D(40.0f, 40.0f));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(
				ThumbnailAsset,
				40,
				40,
				ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}

		Operation->DecoratorWidget =
			SNew(SBorder)
			.RenderOpacity(0.94f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(5.0f)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[SNew(SBox).WidthOverride(42.0f).HeightOverride(42.0f)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(InDisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("CreateLayerGhost", "Create material layer")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
					]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceSelected,
	FText,
	FSoftObjectPath);

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
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		DisplayName = InArgs._DisplayName;
		SurfacePath = InArgs._SurfacePath;
		ThumbnailAsset = InArgs._ThumbnailAsset;
		ThumbnailPool = InArgs._ThumbnailPool;
		OnSelected = InArgs._OnSelected;
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
};

#if 0 // Superseded by the ordered drag/drop targets below.
class SMixtormatLayerDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMixtormatLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMixtormatMaskDropped, OnMaskDropped)
		SLATE_EVENT(FOnMixtormatSurfaceDropped, OnSurfaceDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetLayerIndex = InArgs._TargetLayerIndex;
		OnLayerDropped = InArgs._OnLayerDropped;
		OnMaskDropped = InArgs._OnMaskDropped;
		OnSurfaceDropped = InArgs._OnSurfaceDropped;
		bMaskDragOver = false;
		bSurfaceDragOver = false;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bMaskDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FAppStyle::GetBrush(TEXT("FocusRectangle")))
				.BorderBackgroundColor(FSlateColor(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentBlue"))))
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bSurfaceDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FAppStyle::GetBrush(TEXT("FocusRectangle")))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.55f, 1.0f, 0.9f))
			]
		];
	}

	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		if (TargetLayerIndex > 0)
		{
			if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseMaskLayer", "Release to append this mask"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
			else if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
			{
				bSurfaceDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseSurfaceLayer", "Release to add this material layer"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		bMaskDragOver = false;
		bSurfaceDragOver = false;
		if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		else if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		if (DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMixtormatMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> SurfaceOperation = DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			bSurfaceDragOver = false;
			SurfaceOperation->ResetToDefaultToolTip();
			return OnSurfaceDropped.IsBound()
				? OnSurfaceDropped.Execute(SurfaceOperation->DisplayName, SurfaceOperation->SurfacePath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		if (!Operation.IsValid()
			|| Operation->SourceLayerIndex <= 0
			|| TargetLayerIndex <= 0
			|| !OnLayerDropped.IsBound())
		{
			return FReply::Unhandled();
		}
		Operation->ResetToDefaultToolTip();
		return OnLayerDropped.Execute(Operation->SourceLayerIndex, TargetLayerIndex);
	}

private:
	FOnMixtormatLayerDropped OnLayerDropped;
	FOnMixtormatMaskDropped OnMaskDropped;
	FOnMixtormatSurfaceDropped OnSurfaceDropped;
	bool bMaskDragOver = false;
	bool bSurfaceDragOver = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceDropped,
	FText,
	FSoftObjectPath);

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatLayerDropped,
	int32,
	int32);

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatMaskDropped,
	int32,
	FSoftObjectPath);
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMixtormatLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMixtormatMaskDropped, OnMaskDropped)
		SLATE_EVENT(FOnMixtormatSurfaceDropped, OnSurfaceDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetLayerIndex = InArgs._TargetLayerIndex;
		OnLayerDropped = InArgs._OnLayerDropped;
		OnMaskDropped = InArgs._OnMaskDropped;
		OnSurfaceDropped = InArgs._OnSurfaceDropped;
		bMaskDragOver = false;
		bSurfaceDragOver = false;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bMaskDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FAppStyle::GetBrush(TEXT("FocusRectangle")))
				.BorderBackgroundColor(FSlateColor(FAppStyle::Get().GetSlateColor(TEXT("Colors.AccentBlue"))))
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bSurfaceDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FAppStyle::GetBrush(TEXT("FocusRectangle")))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.55f, 1.0f, 0.9f))
			]
		];
	}

	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		if (TargetLayerIndex > 0)
		{
			if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseMaskLayer", "Release to append this mask"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
			else if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
			{
				bSurfaceDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseSurfaceLayer", "Release to add this material layer"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		bMaskDragOver = false;
		bSurfaceDragOver = false;
		if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		else if (const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		if (DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMixtormatMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		if (const TSharedPtr<FMixtormatSurfaceDragDropOp> SurfaceOperation = DragDropEvent.GetOperationAs<FMixtormatSurfaceDragDropOp>())
		{
			bSurfaceDragOver = false;
			SurfaceOperation->ResetToDefaultToolTip();
			return OnSurfaceDropped.IsBound()
				? OnSurfaceDropped.Execute(SurfaceOperation->DisplayName, SurfaceOperation->SurfacePath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		if (!Operation.IsValid()
			|| Operation->SourceLayerIndex <= 0
			|| TargetLayerIndex <= 0
			|| !OnLayerDropped.IsBound())
		{
			return FReply::Unhandled();
		}
		Operation->ResetToDefaultToolTip();
		return OnLayerDropped.Execute(Operation->SourceLayerIndex, TargetLayerIndex);
	}

private:
	FOnMixtormatLayerDropped OnLayerDropped;
	FOnMixtormatMaskDropped OnMaskDropped;
	FOnMixtormatSurfaceDropped OnSurfaceDropped;
	bool bMaskDragOver = false;
	bool bSurfaceDragOver = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatSurfaceDropped,
	FText,
	FSoftObjectPath);

#endif

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatSurfaceDropped, FText, FSoftObjectPath);

class FMixtormatMaskDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatMaskDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath MaskPath;

	static TSharedRef<FMixtormatMaskDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InMaskPath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatMaskDragDropOp> Operation = MakeShared<FMixtormatMaskDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->MaskPath = InMaskPath;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("AddMaskDrag", "Add {0} to a layer"), InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(FLinearColor(0.08f, 0.08f, 0.08f))
			.Size(FVector2D(40.0f, 40.0f));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(ThumbnailAsset, 40, 40, ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(0.94f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(4.0f)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(34.0f).HeightOverride(34.0f)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(InDisplayName)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

class SMixtormatHierarchyConnector final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatHierarchyConnector) {}
		SLATE_ARGUMENT(bool, IsLast)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		bIsLast = InArgs._IsLast;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(14.0f, 34.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		const bool bParentEnabled) const override
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const FLinearColor Color = FSlateColor::UseSubduedForeground().GetColor(InWidgetStyle);
		TArray<FVector2f> Rail;
		Rail.Add(FVector2f(4.0f, 0.0f));
		Rail.Add(FVector2f(4.0f, bIsLast ? Size.Y * 0.5f : Size.Y));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Rail,
			ESlateDrawEffect::None,
			Color,
			true,
			1.0f);
		TArray<FVector2f> Branch;
		Branch.Add(FVector2f(4.0f, Size.Y * 0.5f));
		Branch.Add(FVector2f(Size.X, Size.Y * 0.5f));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			Branch,
			ESlateDrawEffect::None,
			Color,
			true,
			1.0f);
		return LayerId;
	}

private:
	bool bIsLast = false;
};

class FMixtormatChildDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatChildDragDropOp, FDecoratedDragDropOp)

	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;

	static TSharedRef<FMixtormatChildDragDropOp> New(
		const int32 InLayerIndex,
		const int32 InChildIndex,
		const FText& Name)
	{
		TSharedRef<FMixtormatChildDragDropOp> Operation = MakeShared<FMixtormatChildDragDropOp>();
		Operation->LayerIndex = InLayerIndex;
		Operation->ChildIndex = InChildIndex;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("ReorderChildDrag", "Move {0}"), Name);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(0.92f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(7.0f, 4.0f))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f)[SNew(STextBlock).Text(Name)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatMaskSelected, int32, FSoftObjectPath);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatChildSelected, int32, int32);
DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnMixtormatChildReordered, int32, int32, int32);

class SMixtormatChildStackItem final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatChildStackItem) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(int32, ChildIndex)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_EVENT(FOnMixtormatChildSelected, OnSelected)
		SLATE_EVENT(FOnMixtormatChildReordered, OnChildReordered)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		LayerIndex = InArgs._LayerIndex;
		ChildIndex = InArgs._ChildIndex;
		DisplayName = InArgs._DisplayName;
		OnSelected = InArgs._OnSelected;
		OnChildReordered = InArgs._OnChildReordered;
		ChildSlot
		[
			SAssignNew(MenuAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuRight)
			.OnGetMenuContent(InArgs._OnGetMenuContent)
			[InArgs._Content.Widget]
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() == EKeys::RightMouseButton)
		{
			MenuAnchor->SetIsOpen(true);
			return FReply::Handled();
		}
		return Event.GetEffectingButton() == EKeys::LeftMouseButton
			? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton)
			: FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return Event.GetEffectingButton() == EKeys::LeftMouseButton && OnSelected.IsBound()
			? OnSelected.Execute(LayerIndex, ChildIndex)
			: FReply::Unhandled();
	}

	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return FReply::Handled().BeginDragDrop(
			FMixtormatChildDragDropOp::New(LayerIndex, ChildIndex, DisplayName));
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatChildDragDropOp> Operation = Event.GetOperationAs<FMixtormatChildDragDropOp>();
		return Operation.IsValid() && Operation->LayerIndex == LayerIndex && Operation->ChildIndex != ChildIndex
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatChildDragDropOp> Operation = Event.GetOperationAs<FMixtormatChildDragDropOp>();
		return Operation.IsValid() && Operation->LayerIndex == LayerIndex && OnChildReordered.IsBound()
			? OnChildReordered.Execute(LayerIndex, Operation->ChildIndex, ChildIndex)
			: FReply::Unhandled();
	}

private:
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	FText DisplayName;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	FOnMixtormatChildSelected OnSelected;
	FOnMixtormatChildReordered OnChildReordered;
};

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

class FMixtormatLayerDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatLayerDragDropOp, FDecoratedDragDropOp)

	int32 SourceLayerIndex = INDEX_NONE;

	static TSharedRef<FMixtormatLayerDragDropOp> New(
		const int32 InSourceLayerIndex,
		const FText& DisplayName)
	{
		TSharedRef<FMixtormatLayerDragDropOp> Operation =
			MakeShared<FMixtormatLayerDragDropOp>();
		Operation->SourceLayerIndex = InSourceLayerIndex;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("MoveLayerDrag", "Move {0}"),
			DisplayName);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(0.92f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 5.0f))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Grip")))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(DisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

class SMixtormatLayerDragHandle final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerDragHandle) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(FText, DisplayName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		LayerIndex = InArgs._LayerIndex;
		DisplayName = InArgs._DisplayName;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
			? FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton)
			: FReply::Unhandled();
	}

	virtual FReply OnDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		return LayerIndex > 0
			? FReply::Handled().BeginDragDrop(
				FMixtormatLayerDragDropOp::New(LayerIndex, DisplayName))
			: FReply::Unhandled();
	}

private:
	int32 LayerIndex = INDEX_NONE;
	FText DisplayName;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatLayerDropped,
	int32,
	int32);
DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMixtormatMaskDropped,
	int32,
	FSoftObjectPath);
class SMixtormatLayerRowDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerRowDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMixtormatLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMixtormatMaskDropped, OnMaskDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		TargetLayerIndex = InArgs._TargetLayerIndex;
		OnLayerDropped = InArgs._OnLayerDropped;
		OnMaskDropped = InArgs._OnMaskDropped;
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility_Lambda([this]() { return bMaskDragOver ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.CompactRowValidDrop")))
			]
		];
	}

	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		if (TargetLayerIndex > 0)
		{
			if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(
					LOCTEXT("ReleaseMaskLayer", "Release to append this mask"),
					FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Add")));
			}
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		bMaskDragOver = false;
		if (const TSharedPtr<FMixtormatMaskDragDropOp> Operation = Event.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMixtormatMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMixtormatMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMixtormatLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMixtormatLayerDragDropOp>();
		if (!Operation.IsValid()
			|| Operation->SourceLayerIndex <= 0
			|| TargetLayerIndex <= 0
			|| !OnLayerDropped.IsBound())
		{
			return FReply::Unhandled();
		}
		return OnLayerDropped.Execute(Operation->SourceLayerIndex, TargetLayerIndex);
	}

private:
	int32 TargetLayerIndex = INDEX_NONE;
	FOnMixtormatLayerDropped OnLayerDropped;
	FOnMixtormatMaskDropped OnMaskDropped;
	bool bMaskDragOver = false;
};

class SMixtormatLayerDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatLayerDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnMixtormatSurfaceDropped, OnSurfaceDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnSurfaceDropped = InArgs._OnSurfaceDropped;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		return Event.GetOperationAs<FMixtormatSurfaceDragDropOp>().IsValid()
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMixtormatSurfaceDragDropOp> Operation = Event.GetOperationAs<FMixtormatSurfaceDragDropOp>();
		return Operation.IsValid() && OnSurfaceDropped.IsBound()
			? OnSurfaceDropped.Execute(Operation->DisplayName, Operation->SurfacePath)
			: FReply::Unhandled();
	}

private:
	FOnMixtormatSurfaceDropped OnSurfaceDropped;
};

#undef LOCTEXT_NAMESPACE
