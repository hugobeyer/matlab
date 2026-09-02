#include "Widgets/SMaterialLab.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Compositing/MaterialLabBakeService.h"
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
#include "MaterialLabEffect.h"
#include "MaterialLabMask.h"
#include "MaterialLabSurface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Services/MaterialLabRegistry.h"
#include "Services/MaterialLabSurfaceImporter.h"
#include "Style/MaterialLabStyle.h"
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

#define LOCTEXT_NAMESPACE "SMaterialLab"

namespace MaterialLabUI
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

	FAssetThumbnailConfig CleanThumbnailConfig()
	{
		FAssetThumbnailConfig Config;
		Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
		Config.bAllowAssetSpecificThumbnailOverlay = false;
		return Config;
	}

	const TCHAR* PackedMapLabel(const UMaterialLabSurface& Surface)
	{
		switch (Surface.BlendHeightProvenance)
		{
		case EMaterialLabBlendHeightProvenance::DerivedFromNormal:
			return TEXT("RAMH Derived");
		case EMaterialLabBlendHeightProvenance::AuthoredRAMH:
			return TEXT("RAMH Authored");
		default:
			return Surface.bHasBlendHeight ? TEXT("RAMH Authored") : TEXT("RAM");
		}
	}

	FText HeightBlendSourceText(const UMaterialLabSurface* Surface)
	{
		if (!Surface || !Surface->bHasBlendHeight)
		{
			return LOCTEXT(
				"HeightUsingConstantFallback",
				"Base · Previous Composite   Blend · Scalar Layer Height");
		}
		return Surface->BlendHeightProvenance
			== EMaterialLabBlendHeightProvenance::DerivedFromNormal
			? LOCTEXT(
				"HeightUsingDerivedNormal",
				"Base · Previous Composite   Blend · Height derived from Normal")
			: LOCTEXT(
				"HeightUsingRAMH",
				"Base · Previous Composite   Blend · Authored RAMH alpha");
	}

	void ValidateHeightReferences(TArray<FMaterialLabLayer>& Layers)
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

	void RemapHeightReferencesAfterInsert(TArray<FMaterialLabLayer>& Layers, const int32 InsertIndex)
	{
		for (FMaterialLabLayer& Layer : Layers)
		{
			if (Layer.HeightReferenceLayerIndex >= InsertIndex)
			{
				++Layer.HeightReferenceLayerIndex;
			}
		}
		ValidateHeightReferences(Layers);
	}

	void RemapHeightReferencesAfterDelete(TArray<FMaterialLabLayer>& Layers, const int32 DeletedIndex)
	{
		for (FMaterialLabLayer& Layer : Layers)
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

	void RemapHeightReferencesAfterMove(
		TArray<FMaterialLabLayer>& Layers,
		const int32 SourceIndex,
		const int32 TargetIndex)
	{
		for (FMaterialLabLayer& Layer : Layers)
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

	const FSlateBrush* LucideIcon(const FName IconName)
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

	FText MaskBlendModeText(const EMaterialLabMaskBlendMode Mode)
	{
		switch (Mode)
		{
		case EMaterialLabMaskBlendMode::Add: return LOCTEXT("MaskModeAdd", "Add");
		case EMaterialLabMaskBlendMode::Subtract: return LOCTEXT("MaskModeSubtract", "Subtract");
		case EMaterialLabMaskBlendMode::Multiply: return LOCTEXT("MaskModeMultiply", "Multiply");
		case EMaterialLabMaskBlendMode::Min: return LOCTEXT("MaskModeMin", "Min");
		case EMaterialLabMaskBlendMode::Max: return LOCTEXT("MaskModeMax", "Max");
		default: return LOCTEXT("MaskModeReplace", "Replace");
		}
	}
}

class SMaterialLabBakeSettingsDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabBakeSettingsDialog) {}
		SLATE_ARGUMENT(FMaterialLabBakeSettings, InitialSettings)
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
						.OnGetMenuContent(this, &SMaterialLabBakeSettingsDialog::BuildPathPicker)
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
							FMaterialLabBakeService::GetOutputAssetNames(Settings),
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
						.OnClicked(this, &SMaterialLabBakeSettingsDialog::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("AcceptBakeSettings", "Bake"))
						.OnClicked(this, &SMaterialLabBakeSettingsDialog::Accept)
					]
				]
			]
		];
	}

	bool WasAccepted() const { return bAccepted; }
	const FMaterialLabBakeSettings& GetSettings() const { return Settings; }

private:
	TSharedRef<SWidget> BuildPathPicker()
	{
		FPathPickerConfig Config;
		Config.DefaultPath = Settings.DestinationPath;
		Config.OnPathSelected = FOnPathSelected::CreateSP(
			this,
			&SMaterialLabBakeSettingsDialog::SelectPath);
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
		if (!FMaterialLabBakeService::ValidateSettings(Settings, Error))
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

	FMaterialLabBakeSettings Settings;
	TSharedPtr<SEditableTextBox> DestinationTextBox;
	FText ValidationText;
	int32 Resolution = 2048;
	bool bAccepted = false;
};

enum class EMaterialLabActionDialogResult : uint8
{
	Cancel,
	Confirm,
	Alternate
};

class SMaterialLabActionDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabActionDialog) {}
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
						.OnClicked(this, &SMaterialLabActionDialog::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Visibility(InArgs._AlternateLabel.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.Text(InArgs._AlternateLabel)
						.OnClicked(this, &SMaterialLabActionDialog::Alternate)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(InArgs._ConfirmLabel)
						.OnClicked(this, &SMaterialLabActionDialog::Confirm)
					]
				]
			]
		];
	}

	bool WasConfirmed() const { return Result == EMaterialLabActionDialogResult::Confirm; }
	EMaterialLabActionDialogResult GetResult() const { return Result; }

private:
	FReply Confirm()
	{
		Result = EMaterialLabActionDialogResult::Confirm;
		CloseWindow();
		return FReply::Handled();
	}

	FReply Alternate()
	{
		Result = EMaterialLabActionDialogResult::Alternate;
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

	EMaterialLabActionDialogResult Result = EMaterialLabActionDialogResult::Cancel;
};

bool ShowMaterialLabActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& CancelLabel)
{
	TSharedPtr<SMaterialLabActionDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(560.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMaterialLabActionDialog)
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

EMaterialLabActionDialogResult ShowMaterialLabThreeActionDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Title,
	const FText& Message,
	const FText& ConfirmLabel,
	const FText& AlternateLabel,
	const FText& CancelLabel)
{
	TSharedPtr<SMaterialLabActionDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(560.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMaterialLabActionDialog)
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

enum class EMaterialLabBakeResultAction : uint8
{
	Close,
	Reveal,
	Open,
	Apply,
	Rebake
};

class SMaterialLabBakeResultDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabBakeResultDialog) {}
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
						MakeActionButton(LOCTEXT("CloseBakeResult", "Close"), EMaterialLabBakeResultAction::Close)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("RebakeBakeResult", "Re-bake"), EMaterialLabBakeResultAction::Rebake)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("ApplyBakeResult", "Apply to Selected Actors"), EMaterialLabBakeResultAction::Apply)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("OpenBakeResult", "Open Material Instance"), EMaterialLabBakeResultAction::Open)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("RevealBakeResult", "Reveal Outputs"), EMaterialLabBakeResultAction::Reveal)
					]
				]
			]
		];
	}

	EMaterialLabBakeResultAction GetAction() const { return Action; }

private:
	TSharedRef<SWidget> MakeActionButton(
		const FText& Label,
		const EMaterialLabBakeResultAction InAction)
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

	EMaterialLabBakeResultAction Action = EMaterialLabBakeResultAction::Close;
};

EMaterialLabBakeResultAction ShowMaterialLabBakeResultDialog(
	const TSharedRef<SWidget>& Owner,
	const FText& Message)
{
	TSharedPtr<SMaterialLabBakeResultDialog> Dialog;
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("BakeResultTitle", "Bake Complete"))
		.ClientSize(FVector2D(760.0f, 320.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Dialog, SMaterialLabBakeResultDialog)
			.Message(Message)
		];
	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(Owner),
		false);
	return Dialog->GetAction();
}

class SMaterialLabInspectorGroup final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabInspectorGroup)
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
				SNew(SButton)
				.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.InspectorHeaderButton")))
				.ContentPadding(FMargin(2.0f, 1.0f))
				.OnClicked(this, &SMaterialLabInspectorGroup::ToggleExpanded)
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
						.TextStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("MaterialLab.SectionHeader")))
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
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.InspectorGroup")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder)
					.Padding(FMargin(3.0f, 2.0f))
					.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.InspectorGroupHeader")))
					[Header]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
					.Padding(FMargin(6.0f, 5.0f, 6.0f, 6.0f))
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

class SMaterialLabNumericResetBox final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabNumericResetBox) {}
		SLATE_EVENT(FSimpleDelegate, OnReset)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ResetDelegate = InArgs._OnReset;
		SetToolTipText(LOCTEXT("NumericResetHint", "Drag to adjust · MMB or hover + Backspace to reset"));
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			ResetDelegate.ExecuteIfBound();
			return FReply::Handled();
		}
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

private:
	FSimpleDelegate ResetDelegate;
};

class SMaterialLabTextureTile final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabTextureTile) {}
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

class FMaterialLabSurfaceDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMaterialLabSurfaceDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath SurfacePath;

	static TSharedRef<FMaterialLabSurfaceDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InSurfacePath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMaterialLabSurfaceDragDropOp> Operation =
			MakeShared<FMaterialLabSurfaceDragDropOp>();
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
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(5.0f)
				.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.DragGhostAccent")))
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
	FOnMaterialLabSurfaceSelected,
	FText,
	FSoftObjectPath);

class SMaterialLabSurfaceCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabSurfaceCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_NAMED_SLOT(FArguments, HoverContent)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, SurfacePath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMaterialLabSurfaceSelected, OnSelected)
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
			FMaterialLabSurfaceDragDropOp::New(
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
	FOnMaterialLabSurfaceSelected OnSelected;
};

#if 0 // Superseded by the ordered drag/drop targets below.
class SMaterialLabLayerDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabLayerDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMaterialLabLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMaterialLabMaskDropped, OnMaskDropped)
		SLATE_EVENT(FOnMaterialLabSurfaceDropped, OnSurfaceDropped)
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
			if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseMaskLayer", "Release to append this mask"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
			else if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> Operation = Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
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
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		else if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> Operation = Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		if (DragDropEvent.GetOperationAs<FMaterialLabSurfaceDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> SurfaceOperation = DragDropEvent.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
		{
			bSurfaceDragOver = false;
			SurfaceOperation->ResetToDefaultToolTip();
			return OnSurfaceDropped.IsBound()
				? OnSurfaceDropped.Execute(SurfaceOperation->DisplayName, SurfaceOperation->SurfacePath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
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
	FOnMaterialLabLayerDropped OnLayerDropped;
	FOnMaterialLabMaskDropped OnMaskDropped;
	FOnMaterialLabSurfaceDropped OnSurfaceDropped;
	bool bMaskDragOver = false;
	bool bSurfaceDragOver = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabSurfaceDropped,
	FText,
	FSoftObjectPath);

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabLayerDropped,
	int32,
	int32);

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabMaskDropped,
	int32,
	FSoftObjectPath);
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMaterialLabLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMaterialLabMaskDropped, OnMaskDropped)
		SLATE_EVENT(FOnMaterialLabSurfaceDropped, OnSurfaceDropped)
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
			if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(LOCTEXT("ReleaseMaskLayer", "Release to append this mask"), FAppStyle::GetBrush(TEXT("Icons.Plus")));
			}
			else if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> Operation = Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
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
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
		else if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> Operation = Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		if (DragDropEvent.GetOperationAs<FMaterialLabSurfaceDragDropOp>().IsValid())
		{
			return FReply::Handled();
		}
		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		if (const TSharedPtr<FMaterialLabSurfaceDragDropOp> SurfaceOperation = DragDropEvent.GetOperationAs<FMaterialLabSurfaceDragDropOp>())
		{
			bSurfaceDragOver = false;
			SurfaceOperation->ResetToDefaultToolTip();
			return OnSurfaceDropped.IsBound()
				? OnSurfaceDropped.Execute(SurfaceOperation->DisplayName, SurfaceOperation->SurfacePath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
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
	FOnMaterialLabLayerDropped OnLayerDropped;
	FOnMaterialLabMaskDropped OnMaskDropped;
	FOnMaterialLabSurfaceDropped OnSurfaceDropped;
	bool bMaskDragOver = false;
	bool bSurfaceDragOver = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabSurfaceDropped,
	FText,
	FSoftObjectPath);

#endif

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMaterialLabSurfaceDropped, FText, FSoftObjectPath);

class FMaterialLabMaskDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMaterialLabMaskDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath MaskPath;

	static TSharedRef<FMaterialLabMaskDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InMaskPath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMaterialLabMaskDragDropOp> Operation = MakeShared<FMaterialLabMaskDragDropOp>();
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
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(4.0f)
				.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.DragGhost")))
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

class SMaterialLabHierarchyConnector final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabHierarchyConnector) {}
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

class FMaterialLabChildDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMaterialLabChildDragDropOp, FDecoratedDragDropOp)

	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;

	static TSharedRef<FMaterialLabChildDragDropOp> New(
		const int32 InLayerIndex,
		const int32 InChildIndex,
		const FText& Name)
	{
		TSharedRef<FMaterialLabChildDragDropOp> Operation = MakeShared<FMaterialLabChildDragDropOp>();
		Operation->LayerIndex = InLayerIndex;
		Operation->ChildIndex = InChildIndex;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("ReorderChildDrag", "Move {0}"), Name);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(0.92f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(7.0f, 4.0f))
				.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Grip")))]
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

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMaterialLabMaskSelected, int32, FSoftObjectPath);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMaterialLabChildSelected, int32, int32);
DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnMaterialLabChildReordered, int32, int32, int32);

class SMaterialLabChildStackItem final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabChildStackItem) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(int32, ChildIndex)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_EVENT(FOnMaterialLabChildSelected, OnSelected)
		SLATE_EVENT(FOnMaterialLabChildReordered, OnChildReordered)
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
			FMaterialLabChildDragDropOp::New(LayerIndex, ChildIndex, DisplayName));
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMaterialLabChildDragDropOp> Operation = Event.GetOperationAs<FMaterialLabChildDragDropOp>();
		return Operation.IsValid() && Operation->LayerIndex == LayerIndex && Operation->ChildIndex != ChildIndex
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMaterialLabChildDragDropOp> Operation = Event.GetOperationAs<FMaterialLabChildDragDropOp>();
		return Operation.IsValid() && Operation->LayerIndex == LayerIndex && OnChildReordered.IsBound()
			? OnChildReordered.Execute(LayerIndex, Operation->ChildIndex, ChildIndex)
			: FReply::Unhandled();
	}

private:
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	FText DisplayName;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	FOnMaterialLabChildSelected OnSelected;
	FOnMaterialLabChildReordered OnChildReordered;
};

class SMaterialLabMaskCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabMaskCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, LayerIndex)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, MaskPath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMaterialLabMaskSelected, OnSelected)
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
			FMaterialLabMaskDragDropOp::New(DisplayName, MaskPath, ThumbnailAsset, ThumbnailPool));
	}

private:
	int32 LayerIndex = INDEX_NONE;
	FText DisplayName;
	FSoftObjectPath MaskPath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMaterialLabMaskSelected OnSelected;
};

class FMaterialLabLayerDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMaterialLabLayerDragDropOp, FDecoratedDragDropOp)

	int32 SourceLayerIndex = INDEX_NONE;

	static TSharedRef<FMaterialLabLayerDragDropOp> New(
		const int32 InSourceLayerIndex,
		const FText& DisplayName)
	{
		TSharedRef<FMaterialLabLayerDragDropOp> Operation =
			MakeShared<FMaterialLabLayerDragDropOp>();
		Operation->SourceLayerIndex = InSourceLayerIndex;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("MoveLayerDrag", "Move {0}"),
			DisplayName);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(0.92f)
			.Padding(FMargin(3.0f, 3.0f, 6.0f, 7.0f))
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 5.0f))
				.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Grip")))]
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

class SMaterialLabLayerDragHandle final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabLayerDragHandle) {}
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
				FMaterialLabLayerDragDropOp::New(LayerIndex, DisplayName))
			: FReply::Unhandled();
	}

private:
	int32 LayerIndex = INDEX_NONE;
	FText DisplayName;
};

DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabLayerDropped,
	int32,
	int32);
DECLARE_DELEGATE_RetVal_TwoParams(
	FReply,
	FOnMaterialLabMaskDropped,
	int32,
	FSoftObjectPath);
class SMaterialLabLayerRowDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabLayerRowDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(int32, TargetLayerIndex)
		SLATE_EVENT(FOnMaterialLabLayerDropped, OnLayerDropped)
		SLATE_EVENT(FOnMaterialLabMaskDropped, OnMaskDropped)
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
				.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.CompactRowValidDrop")))
			]
		];
	}

	virtual void OnDragEnter(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		if (TargetLayerIndex > 0)
		{
			if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
			{
				bMaskDragOver = true;
				Operation->SetToolTip(
					LOCTEXT("ReleaseMaskLayer", "Release to append this mask"),
					FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Add")));
			}
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& Event) override
	{
		bMaskDragOver = false;
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> Operation = Event.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			Operation->ResetToDefaultToolTip();
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>().IsValid())
		{
			return TargetLayerIndex > 0 ? FReply::Handled() : FReply::Unhandled();
		}
		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
		return Operation.IsValid() && Operation->SourceLayerIndex > 0 && TargetLayerIndex > 0
			&& Operation->SourceLayerIndex != TargetLayerIndex ? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (const TSharedPtr<FMaterialLabMaskDragDropOp> MaskOperation = DragDropEvent.GetOperationAs<FMaterialLabMaskDragDropOp>())
		{
			bMaskDragOver = false;
			MaskOperation->ResetToDefaultToolTip();
			return TargetLayerIndex > 0 && OnMaskDropped.IsBound()
				? OnMaskDropped.Execute(TargetLayerIndex, MaskOperation->MaskPath)
				: FReply::Unhandled();
		}

		const TSharedPtr<FMaterialLabLayerDragDropOp> Operation = DragDropEvent.GetOperationAs<FMaterialLabLayerDragDropOp>();
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
	FOnMaterialLabLayerDropped OnLayerDropped;
	FOnMaterialLabMaskDropped OnMaskDropped;
	bool bMaskDragOver = false;
};

class SMaterialLabLayerDropTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLabLayerDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnMaterialLabSurfaceDropped, OnSurfaceDropped)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnSurfaceDropped = InArgs._OnSurfaceDropped;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		return Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>().IsValid()
			? FReply::Handled() : FReply::Unhandled();
	}

	virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
	{
		const TSharedPtr<FMaterialLabSurfaceDragDropOp> Operation = Event.GetOperationAs<FMaterialLabSurfaceDragDropOp>();
		return Operation.IsValid() && OnSurfaceDropped.IsBound()
			? OnSurfaceDropped.Execute(Operation->DisplayName, Operation->SurfacePath)
			: FReply::Unhandled();
	}

private:
	FOnMaterialLabSurfaceDropped OnSurfaceDropped;
};

void SMaterialLab::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

	if (FMaterialLabRegistry::GetSurfaces().IsEmpty()
		|| FMaterialLabRegistry::GetMasks().IsEmpty())
	{
		FMaterialLabSurfaceImporter::ImportDefaultLibrary();
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Window")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[BuildTopBar()]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(MainSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()[BuildAuthoringPage()]
				+ SWidgetSwitcher::Slot()
				[
					BuildWorkspacePage(
						LOCTEXT("MixerHeading", "Material Mixer"),
						LOCTEXT("MixerDescription", "Legacy mixer scaffold. Ordered material layers are the primary workflow."))
				]
				+ SWidgetSwitcher::Slot()[BuildPresetsPage()]
			]
			+ SVerticalBox::Slot().AutoHeight()[BuildStatusBar()]
		]
	];

	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildLayerList();
	RebuildMaskList();
	ResetEditHistory(true);
}

bool SMaterialLab::AreLayerStacksEqual(
	const TArray<FMaterialLabLayer>& A,
	const TArray<FMaterialLabLayer>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}

	const UScriptStruct* LayerStruct = FMaterialLabLayer::StaticStruct();
	for (int32 LayerIndex = 0; LayerIndex < A.Num(); ++LayerIndex)
	{
		if (!LayerStruct->CompareScriptStruct(&A[LayerIndex], &B[LayerIndex], 0))
		{
			return false;
		}
	}
	return true;
}

bool SMaterialLab::HaveSameLayerStructure(
	const TArray<FMaterialLabLayer>& A,
	const TArray<FMaterialLabLayer>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 LayerIndex = 0; LayerIndex < A.Num(); ++LayerIndex)
	{
		if (A[LayerIndex].Children.Num() != B[LayerIndex].Children.Num())
		{
			return false;
		}
		for (int32 ChildIndex = 0; ChildIndex < A[LayerIndex].Children.Num(); ++ChildIndex)
		{
			if (A[LayerIndex].Children[ChildIndex].Type
				!= B[LayerIndex].Children[ChildIndex].Type)
			{
				return false;
			}
		}
	}
	return true;
}

void SMaterialLab::ResetEditHistory(const bool bCurrentStateIsSaved)
{
	UndoHistory.Reset();
	RedoHistory.Reset();
	CurrentHistoryState.Layers = WorkingLayers;
	bHistoryInitialized = true;
	bApplyingHistory = false;
	LastHistoryRecordTime = 0.0;
	if (bCurrentStateIsSaved)
	{
		SavedLayers = WorkingLayers;
	}
}

void SMaterialLab::RecordEditHistory()
{
	if (bApplyingHistory)
	{
		return;
	}
	if (!bHistoryInitialized)
	{
		ResetEditHistory(false);
		return;
	}
	if (AreLayerStacksEqual(CurrentHistoryState.Layers, WorkingLayers))
	{
		return;
	}

	constexpr double InteractiveEditWindowSeconds = 0.3;
	constexpr int32 MaximumHistoryStates = 100;
	const double Now = FPlatformTime::Seconds();
	const bool bCoalesceInteractiveEdit = !UndoHistory.IsEmpty()
		&& Now - LastHistoryRecordTime <= InteractiveEditWindowSeconds
		&& HaveSameLayerStructure(CurrentHistoryState.Layers, WorkingLayers);
	if (!bCoalesceInteractiveEdit)
	{
		UndoHistory.Add(CurrentHistoryState);
		if (UndoHistory.Num() > MaximumHistoryStates)
		{
			UndoHistory.RemoveAt(0, UndoHistory.Num() - MaximumHistoryStates);
		}
	}

	CurrentHistoryState.Layers = WorkingLayers;
	RedoHistory.Reset();
	LastHistoryRecordTime = Now;
}

bool SMaterialLab::IsCurrentStateSaved() const
{
	return WorkingMaterialAsset.IsValid()
		&& AreLayerStacksEqual(WorkingLayers, SavedLayers);
}

void SMaterialLab::ApplyEditHistoryState(const FEditHistoryState& State)
{
	bApplyingHistory = true;
	WorkingLayers = State.Layers;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	CurrentHistoryState = State;
	LastHistoryRecordTime = 0.0;

	SelectedLayerIndex = WorkingLayers.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(SelectedLayerIndex, 0, WorkingLayers.Num() - 1);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].bEnabled;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");

	SyncSelectedLayerControls();
	RefreshLayeredPreview(false);
	RebuildLayerList();
	RebuildMaskList();
	bApplyingHistory = false;
}

void SMaterialLab::SynchronizeHistoryAfterCancelledEdit()
{
	CurrentHistoryState.Layers = WorkingLayers;
	if (!UndoHistory.IsEmpty()
		&& AreLayerStacksEqual(UndoHistory.Last().Layers, WorkingLayers))
	{
		UndoHistory.Pop();
	}
	RedoHistory.Reset();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
}

FReply SMaterialLab::UndoMaterialEdit()
{
	if (UndoHistory.IsEmpty())
	{
		return FReply::Handled();
	}
	RedoHistory.Add(CurrentHistoryState);
	const FEditHistoryState State = UndoHistory.Pop();
	ApplyEditHistoryState(State);
	return FReply::Handled();
}

FReply SMaterialLab::RedoMaterialEdit()
{
	if (RedoHistory.IsEmpty())
	{
		return FReply::Handled();
	}
	UndoHistory.Add(CurrentHistoryState);
	const FEditHistoryState State = RedoHistory.Pop();
	ApplyEditHistoryState(State);
	return FReply::Handled();
}

TSharedRef<SWidget> SMaterialLab::MakeResettableNumeric(
	const TSharedRef<SWidget>& NumericWidget,
	const FSimpleDelegate& ResetDelegate)
{
	const FSimpleDelegate BoundedReset = FSimpleDelegate::CreateLambda([this, ResetDelegate]()
	{
		LastHistoryRecordTime = 0.0;
		ResetDelegate.ExecuteIfBound();
	});
	TSharedRef<SMaterialLabNumericResetBox> ResetBox =
		SNew(SMaterialLabNumericResetBox)
		.OnReset(BoundedReset)
		[
			NumericWidget
		];
	FNumericResetBinding& Binding = NumericResetBindings.AddDefaulted_GetRef();
	Binding.Widget = ResetBox;
	Binding.Reset = BoundedReset;
	return ResetBox;
}

bool SMaterialLab::ResetHoveredNumericControl()
{
	for (int32 Index = NumericResetBindings.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<SWidget> Widget = NumericResetBindings[Index].Widget.Pin();
		if (!Widget.IsValid())
		{
			NumericResetBindings.RemoveAtSwap(Index);
			continue;
		}
		if (Widget->IsHovered())
		{
			NumericResetBindings[Index].Reset.ExecuteIfBound();
			return true;
		}
	}
	return false;
}

FReply SMaterialLab::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const bool bModifierDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::BackSpace && ResetHoveredNumericControl())
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SMaterialLab::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	(void)MyGeometry;
	const bool bModifierDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::BackSpace && ResetHoveredNumericControl())
	{
		return FReply::Handled();
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Z)
	{
		return InKeyEvent.IsShiftDown() ? RedoMaterialEdit() : UndoMaterialEdit();
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Y)
	{
		return RedoMaterialEdit();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMaterialLab::ShowPage(const int32 PageIndex)
{
	if (MainSwitcher.IsValid())
	{
		MainSwitcher->SetActiveWidgetIndex(PageIndex);
	}
	return FReply::Handled();
}

FReply SMaterialLab::ShowLeftPage(const int32 PageIndex)
{
	LeftTabIndex = PageIndex;
	if (LeftSwitcher.IsValid())
	{
		LeftSwitcher->SetActiveWidgetIndex(PageIndex);
	}
	return FReply::Handled();
}

FReply SMaterialLab::ImportSurfaces()
{
	const FMaterialLabImportResult Result = FMaterialLabSurfaceImporter::ImportFromDialog();
	if (!Result.bCancelled)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Result.ToMessage());
		RebuildCategoryList();
		RebuildSurfaceList();
	}
	return FReply::Handled();
}

FReply SMaterialLab::ReimportShippedLibrary()
{
	const FMaterialLabImportResult Result = FMaterialLabSurfaceImporter::ReimportShippedLibrary();
	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildMaskList();
	WorkingStatusText = Result.Errors.IsEmpty()
		? FString::Printf(
			TEXT("Reimported shipped library (%d surface(s), %d mask(s), %d effect(s))"),
			Result.ImportedSurfaceCount,
			Result.ImportedMaskCount,
			Result.ImportedEffectCount)
		: TEXT("Reimport reported issues");
	FMessageDialog::Open(EAppMsgType::Ok, Result.ToMessage());
	return FReply::Handled();
}

FReply SMaterialLab::RefreshSurfaceList()
{
	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::SelectSurface(FText DisplayName, FSoftObjectPath AssetPath)
{
	SelectedSurfacePath = AssetPath;
	SelectedLibrarySurfaceName = DisplayName;
	if (bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	bHasSelectedLayer = false;
	SelectedLayerIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	WorkingLayers.Reset();
	WorkingMaterialName = TEXT("No material");
	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(LOCTEXT("NoSelectedLayer", "No layer selected"));
	}
	if (WorkingBaseLayerText.IsValid())
	{
		WorkingBaseLayerText->SetText(DisplayName);
	}

	const UMaterialLabSurface* Surface = Cast<UMaterialLabSurface>(AssetPath.TryLoad());
	if (Surface)
	{
		SelectedPreviewMaterial.Reset(Cast<UMaterialInstanceConstant>(Surface->PreviewMaterial.Get()));
		bHasWorkingMaterial = false;
		if (SelectedPreviewMaterial.IsValid())
		{
			CurrentTiling = FMath::Max(1.0f, FMath::RoundToFloat(
				UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
					SelectedPreviewMaterial.Get(), TEXT("ML_Tiling"))));
			CurrentRoughnessBias = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("ML_RoughnessBias"));
			CurrentRoughnessContrast = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("ML_RoughnessContrast"));
			CurrentRoughnessOffset = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("ML_RoughnessOffset"));
			CurrentNormalIntensity = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("ML_NormalIntensity"));
		}
		if (SelectedIdentityText.IsValid())
		{
			SelectedIdentityText->SetText(FText::FromName(Surface->Family));
		}
		if (SelectedMapsText.IsValid())
		{
			SelectedMapsText->SetText(FText::FromString(FString::Printf(
				TEXT("BC %s  N %s  %s %s"),
				Surface->BaseColor ? TEXT("✓") : TEXT("—"),
				Surface->Normal ? TEXT("✓") : TEXT("—"),
				MaterialLabUI::PackedMapLabel(*Surface),
				Surface->RoughnessAOMetallic ? TEXT("✓") : TEXT("—"))));
		}

		if (bPreviewDisplacementEnabled)
		{
			PreviewSelectedSurfaceWithDisplacement();
		}
		else
		{
			for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
			{
				if (Viewport.IsValid())
				{
					Viewport->SetPreviewMaterial(Surface->PreviewMaterial);
				}
			}
		}
	}

	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::HandleSurfaceDropped(FText DisplayName, FSoftObjectPath AssetPath)
{
	SelectSurface(DisplayName, AssetPath);
	return bHasWorkingMaterial
		? AddWorkingLayer(EMaterialLabLayerType::Material)
		: StartNewMaterial();
}

FReply SMaterialLab::SetPreviewMesh(const EMaterialLabPreviewMesh MeshType)
{
	PreviewMesh = MeshType;
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewMesh(MeshType);
		}
	}
	return FReply::Handled();
}

FReply SMaterialLab::SetPreviewQuality(const EMaterialLabPreviewQuality Quality)
{
	PreviewQuality = Quality;
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewQuality(Quality);
		}
	}
	return FReply::Handled();
}

void SMaterialLab::SetPreviewFov(const float FovDegrees)
{
	PreviewFov = FMath::Clamp(FovDegrees, 20.0f, 90.0f);
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetCameraFov(PreviewFov);
		}
	}
}

void SMaterialLab::SetPreviewDisplacementEnabled(const bool bEnabled)
{
	bPreviewDisplacementEnabled = bEnabled;
	if (bPreviewDisplacementEnabled && !bHasWorkingMaterial)
	{
		PreviewSelectedSurfaceWithDisplacement();
	}
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewDisplacementEnabled(bPreviewDisplacementEnabled);
		}
	}
}

void SMaterialLab::SetPreviewDisplacementAmount(const float Amount)
{
	PreviewDisplacementAmount = FMath::Clamp(Amount, 0.0f, 4.0f);
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewDisplacementAmount(PreviewDisplacementAmount);
		}
	}
}

void SMaterialLab::PreviewSelectedSurfaceWithDisplacement()
{
	if (bHasWorkingMaterial || SelectedSurfacePath.IsNull())
	{
		return;
	}

	FMaterialLabLayer PreviewLayer;
	PreviewLayer.DisplayName = SelectedLibrarySurfaceName;
	PreviewLayer.Type = EMaterialLabLayerType::Material;
	PreviewLayer.SourceSurface = TSoftObjectPtr<UMaterialLabSurface>(SelectedSurfacePath);
	PreviewLayer.Tiling = CurrentTiling;
	PreviewLayer.RoughnessBias = CurrentRoughnessBias;
	PreviewLayer.RoughnessContrast = CurrentRoughnessContrast;
	PreviewLayer.RoughnessOffset = CurrentRoughnessOffset;
	PreviewLayer.NormalIntensity = CurrentNormalIntensity;

	TArray<FMaterialLabLayer> PreviewLayers;
	PreviewLayers.Add(MoveTemp(PreviewLayer));
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewLayers(PreviewLayers, CompositionResolution);
		}
	}
}

FReply SMaterialLab::SetStudioLighting(const EMaterialLabStudioLighting LightingPreset)
{
	StudioLighting = LightingPreset;
	SelectedHdriPath.Reset();
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetStudioLighting(LightingPreset);
		}
	}
	return FReply::Handled();
}

FReply SMaterialLab::SetHdriLighting(const FSoftObjectPath HdriPath)
{
	UTextureCube* Cubemap = Cast<UTextureCube>(HdriPath.TryLoad());
	if (!Cubemap)
	{
		return FReply::Handled();
	}

	SelectedHdriPath = HdriPath;
	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetHdriLighting(Cubemap);
		}
	}
	return FReply::Handled();
}

FReply SMaterialLab::StartNewMaterial()
{
	if (!SelectedPreviewMaterial.IsValid())
	{
		return FReply::Handled();
	}

	bHasWorkingMaterial = true;
	WorkingMaterialAsset.Reset();
	WorkingMaterialName = TEXT("Untitled MatLab Material");
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	WorkingLayers.Reset();

	FMaterialLabLayer& BaseLayer = WorkingLayers.AddDefaulted_GetRef();
	BaseLayer.DisplayName = SelectedLibrarySurfaceName;
	BaseLayer.Type = EMaterialLabLayerType::Material;
	BaseLayer.SourceSurface = TSoftObjectPtr<UMaterialLabSurface>(SelectedSurfacePath);
	BaseLayer.Tiling = CurrentTiling;
	BaseLayer.RoughnessBias = CurrentRoughnessBias;
	BaseLayer.RoughnessContrast = CurrentRoughnessContrast;
	BaseLayer.RoughnessOffset = CurrentRoughnessOffset;
	BaseLayer.NormalIntensity = CurrentNormalIntensity;

	SelectedLayerIndex = 0;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(SelectedLibrarySurfaceName);
	}
	SavedLayers.Reset();
	ResetEditHistory(false);
	bIsWorkingMaterialDirty = true;
	RefreshLayeredPreview(false);
	WorkingStatusText = TEXT("New material · unsaved");
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::NewWorkingMaterial()
{
	if (!ConfirmDiscardUnsavedChanges())
	{
		return FReply::Handled();
	}

	bHasWorkingMaterial = false;
	bHasSelectedLayer = false;
	bIsWorkingMaterialDirty = false;
	SelectedLayerIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	WorkingLayers.Reset();
	SavedLayers.Reset();
	WorkingMaterialAsset.Reset();
	ResetEditHistory(true);
	WorkingMaterialName = TEXT("No material");
	WorkingStatusText = TEXT("Select a library material, then drag it into Layers");
	if (bPreviewDisplacementEnabled)
	{
		PreviewSelectedSurfaceWithDisplacement();
	}
	else if (SelectedPreviewMaterial.IsValid())
	{
		for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
		{
			if (Viewport.IsValid())
			{
				Viewport->SetPreviewMaterial(SelectedPreviewMaterial.Get());
			}
		}
	}
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::OpenWorkingMaterial()
{
	if (!ConfirmDiscardUnsavedChanges())
	{
		return FReply::Handled();
	}

	FOpenAssetDialogConfig DialogConfig;
	DialogConfig.DialogTitleOverride = LOCTEXT("OpenMaterialLabMaterial", "Open Material Lab Material");
	DialogConfig.DefaultPath = TEXT("/Game");
	DialogConfig.AssetClassNames.Add(UMaterialLabMaterial::StaticClass()->GetClassPathName());
	DialogConfig.bAllowMultipleSelection = false;

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const TArray<FAssetData> Assets =
		ContentBrowserModule.Get().CreateModalOpenAssetDialog(DialogConfig);
	if (Assets.IsEmpty())
	{
		return FReply::Handled();
	}

	UMaterialLabMaterial* MaterialAsset = Cast<UMaterialLabMaterial>(Assets[0].GetAsset());
	if (!MaterialAsset || MaterialAsset->Layers.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("InvalidMaterialLabMaterial", "The selected recipe has no base layer."));
		return FReply::Handled();
	}

	WorkingMaterialAsset.Reset(MaterialAsset);
	WorkingLayers = MaterialAsset->Layers;
	WorkingLayers[0].bEnabled = true;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	WorkingMaterialName = MaterialAsset->DisplayName.IsEmpty()
		? MaterialAsset->GetName()
		: MaterialAsset->DisplayName.ToString();
	bHasWorkingMaterial = true;
	SelectedLayerIndex = 0;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers[0].bEnabled;

	if (const UMaterialLabSurface* BaseSurface = WorkingLayers[0].SourceSurface.LoadSynchronous())
	{
		SelectedSurfacePath = WorkingLayers[0].SourceSurface.ToSoftObjectPath();
		SelectedLibrarySurfaceName = WorkingLayers[0].DisplayName;
		SelectedPreviewMaterial.Reset(Cast<UMaterialInstanceConstant>(BaseSurface->PreviewMaterial.Get()));
	}

	SyncSelectedLayerControls();
	ResetEditHistory(true);
	RefreshLayeredPreview(false);
	bIsWorkingMaterialDirty = false;
	WorkingStatusText = FString::Printf(TEXT("Opened %s"), *WorkingMaterialName);
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::SaveWorkingMaterial()
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}
	if (!WorkingMaterialAsset.IsValid())
	{
		return SaveWorkingMaterialAs();
	}

	UMaterialLabMaterial* MaterialAsset = WorkingMaterialAsset.Get();
	MaterialAsset->Modify();
	MaterialAsset->DisplayName = FText::FromString(WorkingMaterialName);
	MaterialAsset->Layers = WorkingLayers;
	MaterialAsset->MarkPackageDirty();
	bool bSaved = false;
	if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
	{
		bSaved = AssetSubsystem->SaveLoadedAsset(MaterialAsset, false);
	}
	bIsWorkingMaterialDirty = !bSaved;
	if (bSaved)
	{
		SavedLayers = WorkingLayers;
		CurrentHistoryState.Layers = WorkingLayers;
	}
	WorkingStatusText = bSaved
		? FString::Printf(TEXT("Saved %s"), *WorkingMaterialName)
		: TEXT("Save failed");
	return FReply::Handled();
}

FReply SMaterialLab::SaveWorkingMaterialAs()
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	FSaveAssetDialogConfig DialogConfig;
	DialogConfig.DialogTitleOverride = LOCTEXT("SaveMaterialLabMaterialAs", "Save Material Lab Material As");
	DialogConfig.DefaultPath = TEXT("/Game/MaterialLab/Materials");
	DialogConfig.DefaultAssetName = TEXT("MLM_Untitled");
	DialogConfig.AssetClassNames.Add(UMaterialLabMaterial::StaticClass()->GetClassPathName());
	DialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	const FString ObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(DialogConfig);
	if (ObjectPath.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
	UPackage* Package = CreatePackage(*PackageName);
	UMaterialLabMaterial* MaterialAsset = Package
		? NewObject<UMaterialLabMaterial>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional)
		: nullptr;
	if (!MaterialAsset)
	{
		WorkingStatusText = FString::Printf(TEXT("Save failed · %s"), *ObjectPath);
		return FReply::Handled();
	}
	FAssetRegistryModule::AssetCreated(MaterialAsset);

	WorkingMaterialAsset.Reset(MaterialAsset);
	WorkingMaterialName = MaterialAsset->GetName();
	MaterialAsset->Modify();
	MaterialAsset->DisplayName = FText::FromString(WorkingMaterialName);
	MaterialAsset->Layers = WorkingLayers;
	MaterialAsset->MarkPackageDirty();
	bool bSaved = false;
	if (UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>())
	{
		bSaved = AssetSubsystem->SaveLoadedAsset(MaterialAsset, false);
	}
	bIsWorkingMaterialDirty = !bSaved;
	if (bSaved)
	{
		SavedLayers = WorkingLayers;
		CurrentHistoryState.Layers = WorkingLayers;
	}
	WorkingStatusText = bSaved
		? FString::Printf(TEXT("Saved %s"), *WorkingMaterialName)
		: TEXT("Save failed");
	return FReply::Handled();
}

FReply SMaterialLab::SetCompositionResolution(const int32 Resolution)
{
	if (Resolution == 1024 || Resolution == 2048 || Resolution == 4096)
	{
		CompositionResolution = Resolution;
		WorkingStatusText = FString::Printf(
			TEXT("Preview and bake resolution: %d × %d"),
			CompositionResolution,
			CompositionResolution);
		RefreshLayeredPreview(false);
	}
	FSlateApplication::Get().DismissAllMenus();
	return FReply::Handled();
}

FReply SMaterialLab::BakeWorkingMaterial()
{
	if (!WorkingMaterialAsset.IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("SaveBeforeBake", "Save the Material Lab recipe before baking."));
		return FReply::Handled();
	}

	const FSoftObjectPath RecipePath(WorkingMaterialAsset.Get());
	if (BakeSettingsRecipePath != RecipePath)
	{
		BakeSettingsRecipePath = RecipePath;
		BakeOutputBaseName = WorkingMaterialAsset->GetName();
		BakeDestinationPath = FString::Printf(
			TEXT("%s/Baked/%s"),
			*FPackageName::GetLongPackagePath(WorkingMaterialAsset->GetOutermost()->GetName()),
			*WorkingMaterialAsset->GetName());
	}

	TSharedPtr<SMaterialLabBakeSettingsDialog> SettingsDialog;
	const TSharedRef<SWindow> SettingsWindow = SNew(SWindow)
		.Title(LOCTEXT("BakeSettingsTitle", "Bake Material"))
		.ClientSize(FVector2D(480.0f, 410.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(SettingsDialog, SMaterialLabBakeSettingsDialog)
			.InitialSettings(FMaterialLabBakeSettings{BakeDestinationPath, BakeOutputBaseName})
			.Resolution(CompositionResolution)
		];
	FSlateApplication::Get().AddModalWindow(
		SettingsWindow,
		FSlateApplication::Get().FindWidgetWindow(AsShared()),
		false);
	if (!SettingsDialog->WasAccepted())
	{
		WorkingStatusText = TEXT("Bake canceled");
		return FReply::Handled();
	}

	const FMaterialLabBakeSettings Settings = SettingsDialog->GetSettings();
	BakeDestinationPath = Settings.DestinationPath;
	BakeOutputBaseName = Settings.BaseName;
	return ExecuteBake(Settings, true);
}

FReply SMaterialLab::ExecuteBake(
	const FMaterialLabBakeSettings& Settings,
	const bool bConfirmExistingOutputs)
{
	if (!WorkingMaterialAsset.IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("MissingRebakeRecipe", "Re-bake stopped because the recipe is no longer available."));
		return FReply::Handled();
	}

	const TArray<FString> ExistingOutputs =
		FMaterialLabBakeService::FindExistingOutputObjectPaths(Settings);
	if (bConfirmExistingOutputs && !ExistingOutputs.IsEmpty())
	{
		const FText ConflictMessage = FText::Format(
			LOCTEXT(
				"BakeOutputConflict",
				"These outputs already exist:\n\n{0}\n\nUpdate them in place, or cancel without changes."),
			FText::FromString(FString::Join(ExistingOutputs, TEXT("\n"))));
		if (!ShowMaterialLabActionDialog(
			AsShared(),
			LOCTEXT("BakeConflictTitle", "Existing Bake Outputs"),
			ConflictMessage,
			LOCTEXT("UpdateBakeOutputs", "Update Existing"),
			LOCTEXT("CancelBakeConflict", "Cancel")))
		{
			WorkingStatusText = TEXT("Bake canceled · existing outputs unchanged");
			return FReply::Handled();
		}
	}

	SaveWorkingMaterial();
	if (bIsWorkingMaterialDirty || PreviewViewports.IsEmpty() || !PreviewViewports[0].IsValid())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("BakeSaveFailed", "Bake stopped because the recipe could not be saved."));
		return FReply::Handled();
	}

	FMaterialLabBakeResult Result;
	{
		TGuardValue<bool> BakingGuard(bIsBaking, true);
		FScopedSlowTask SlowTask(5.0f, LOCTEXT("BakeProgressTitle", "Baking Material Lab outputs..."));
		SlowTask.MakeDialog(false);
		WorkingStatusText = TEXT("Bake · Compose");
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("BakeStageCompose", "Compose"));
		if (!PreviewViewports[0]->ComposeLayersAtResolution(WorkingLayers, CompositionResolution))
		{
			RefreshLayeredPreview(false);
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("BakeComposeFailed", "Compose failed. Check the current recipe and compositor output."));
			WorkingStatusText = TEXT("Bake failed · Compose");
			return FReply::Handled();
		}

		UTextureRenderTarget2D* BaseColor = PreviewViewports[0]->GetCompositedBaseColor();
		UTextureRenderTarget2D* Normal = PreviewViewports[0]->GetCompositedNormal();
		UTextureRenderTarget2D* RAM = PreviewViewports[0]->GetCompositedRAM();
		UTextureRenderTarget2D* Height = PreviewViewports[0]->GetCompositedHeight();
		if (!BaseColor || !Normal || !RAM || !Height)
		{
			RefreshLayeredPreview(false);
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("MissingBakeOutputs", "Readback cannot start because the GPU compositor has no valid outputs."));
			WorkingStatusText = TEXT("Bake failed · Readback");
			return FReply::Handled();
		}

		const auto ReportProgress = [this, &SlowTask](const EMaterialLabBakeStage Stage)
		{
			FText StageText;
			switch (Stage)
			{
			case EMaterialLabBakeStage::Readback:
				StageText = LOCTEXT("BakeStageReadback", "Readback");
				break;
			case EMaterialLabBakeStage::CreateTextures:
				StageText = LOCTEXT("BakeStageCreateTextures", "Create Textures");
				break;
			case EMaterialLabBakeStage::CreateMaterial:
				StageText = LOCTEXT("BakeStageCreateMaterial", "Create Material");
				break;
			default:
				StageText = LOCTEXT("BakeStageSave", "Save");
				break;
			}
			WorkingStatusText = FString::Printf(TEXT("Bake · %s"), *StageText.ToString());
			SlowTask.EnterProgressFrame(1.0f, StageText);
		};
		Result = FMaterialLabBakeService::Bake(
			*WorkingMaterialAsset.Get(),
			*BaseColor,
			*Normal,
			*RAM,
			*Height,
			Settings,
			ReportProgress);
	}

	RefreshLayeredPreview(false);
	if (!Result.Succeeded())
	{
		FString ErrorText = TEXT("Bake failed:\n\n");
		for (const FText& Error : Result.Errors)
		{
			if (!ErrorText.EndsWith(TEXT("\n\n")))
			{
				ErrorText += TEXT("\n");
			}
			ErrorText += FString::Printf(TEXT("• %s"), *Error.ToString());
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorText));
		WorkingStatusText = TEXT("Bake failed · review reported asset paths");
		return FReply::Handled();
	}

	const TArray<FString> OutputPaths = FMaterialLabBakeService::GetOutputObjectPaths(Settings);
	const FText SuccessMessage = FText::Format(
		LOCTEXT(
			"BakeSucceeded",
			"Bake completed successfully:\n\n{0}"),
		FText::FromString(FString::Join(OutputPaths, TEXT("\n"))));
	WorkingStatusText = FString::Printf(
		TEXT("Baked %s · 5 outputs saved"),
		*Settings.BaseName);

	switch (ShowMaterialLabBakeResultDialog(AsShared(), SuccessMessage))
	{
	case EMaterialLabBakeResultAction::Reveal:
	{
		TArray<FAssetData> Assets;
		Assets.Reserve(5);
		Assets.Add(FAssetData(Result.BaseColor));
		Assets.Add(FAssetData(Result.Normal));
		Assets.Add(FAssetData(Result.RAM));
		Assets.Add(FAssetData(Result.Height));
		Assets.Add(FAssetData(Result.Material));
		FContentBrowserModule& ContentBrowserModule =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		WorkingStatusText += TEXT(" · revealed in Content Browser");
		break;
	}
	case EMaterialLabBakeResultAction::Open:
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor
			? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
			: nullptr;
		if (!AssetEditorSubsystem || !AssetEditorSubsystem->OpenEditorForAsset(Result.Material))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("OpenBakedMaterialFailed", "Could not open {0}."),
					FText::FromString(Result.Material->GetPathName())));
			WorkingStatusText += TEXT(" · material instance could not be opened");
		}
		else
		{
			WorkingStatusText += TEXT(" · material instance opened");
		}
		break;
	}
	case EMaterialLabBakeResultAction::Apply:
		ApplyBakedMaterialToSelectedActors(*Result.Material);
		break;
	case EMaterialLabBakeResultAction::Rebake:
		return ExecuteBake(Settings, false);
	default:
		break;
	}
	return FReply::Handled();
}

void SMaterialLab::ApplyBakedMaterialToSelectedActors(UMaterialInterface& Material)
{
	struct FMaterialAssignment
	{
		AActor* Actor = nullptr;
		UMeshComponent* Component = nullptr;
		int32 SlotIndex = INDEX_NONE;
	};

	USelection* SelectedActors = GEditor ? GEditor->GetSelectedActors() : nullptr;
	if (!SelectedActors)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("ApplyBakeNoSelection", "Select one or more actors with material slots first."));
		return;
	}

	TArray<FMaterialAssignment> Assignments;
	TSet<AActor*> TargetActors;
	int32 ReplacementCount = 0;
	int32 AlreadyAssignedCount = 0;
	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = Cast<AActor>(*SelectionIt);
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* Component : MeshComponents)
		{
			if (!IsValid(Component))
			{
				continue;
			}
			for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
			{
				UMaterialInterface* ExistingMaterial = Component->GetMaterial(SlotIndex);
				if (ExistingMaterial == &Material)
				{
					++AlreadyAssignedCount;
					continue;
				}
				ReplacementCount += ExistingMaterial ? 1 : 0;
				Assignments.Add({Actor, Component, SlotIndex});
				TargetActors.Add(Actor);
			}
		}
	}

	if (Assignments.IsEmpty())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			AlreadyAssignedCount > 0
				? LOCTEXT("ApplyBakeAlreadyAssigned", "The baked material is already assigned to the selected actors.")
				: LOCTEXT("ApplyBakeNoSlots", "The selected actors have no compatible material slots."));
		return;
	}

	if (ReplacementCount > 0)
	{
		const FText Confirmation = FText::Format(
			LOCTEXT(
				"ConfirmReplaceSelectedMaterials",
				"Applying {0} will replace {1} existing material assignment(s) across {2} selected actor(s).\n\nCancel preserves every current assignment."),
			FText::FromString(Material.GetPathName()),
			FText::AsNumber(ReplacementCount),
			FText::AsNumber(TargetActors.Num()));
		if (!ShowMaterialLabActionDialog(
			AsShared(),
			LOCTEXT("ReplaceSelectedMaterialsTitle", "Replace Existing Materials?"),
			Confirmation,
			LOCTEXT("ReplaceAndApplyBakedMaterial", "Replace and Apply"),
			LOCTEXT("CancelApplyBakedMaterial", "Cancel")))
		{
			WorkingStatusText = TEXT("Apply canceled · actor materials unchanged");
			return;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyBakedMaterialTransaction", "Apply Baked Material"));
	TSet<UMeshComponent*> ModifiedComponents;
	for (const FMaterialAssignment& Assignment : Assignments)
	{
		if (!ModifiedComponents.Contains(Assignment.Component))
		{
			Assignment.Component->Modify();
			ModifiedComponents.Add(Assignment.Component);
		}
		Assignment.Actor->Modify();
		Assignment.Component->SetMaterial(Assignment.SlotIndex, &Material);
	}
	for (UMeshComponent* Component : ModifiedComponents)
	{
		Component->PostEditChange();
	}
	for (AActor* Actor : TargetActors)
	{
		Actor->PostEditChange();
		Actor->MarkPackageDirty();
	}

	WorkingStatusText = FString::Printf(
		TEXT("Applied %s · %d slot(s) on %d actor(s) · Undo available"),
		*Material.GetName(),
		Assignments.Num(),
		TargetActors.Num());
}

FReply SMaterialLab::AddWorkingLayer(const EMaterialLabLayerType LayerType)
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}
	if (LayerType != EMaterialLabLayerType::Fill && SelectedSurfacePath.IsNull())
	{
		WorkingStatusText = TEXT("Select a library surface first");
		return FReply::Handled();
	}

	FMaterialLabLayer& Layer = WorkingLayers.AddDefaulted_GetRef();
	Layer.Type = LayerType;
	const int32 LayerNumber = WorkingLayers.Num();

	switch (LayerType)
	{
	case EMaterialLabLayerType::Material:
		Layer.DisplayName = SelectedLibrarySurfaceName.IsEmpty()
			? FText::Format(LOCTEXT("MaterialLayerNumber", "Material Layer {0}"), FText::AsNumber(LayerNumber))
			: SelectedLibrarySurfaceName;
		Layer.SourceSurface = TSoftObjectPtr<UMaterialLabSurface>(SelectedSurfacePath);
		break;
	case EMaterialLabLayerType::Fill:
		Layer.DisplayName = FText::Format(LOCTEXT("FillLayerNumber", "Fill Layer {0}"), FText::AsNumber(LayerNumber));
		Layer.bOverrideBaseColor = true;
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		break;
	case EMaterialLabLayerType::Effect:
		Layer.DisplayName = FText::Format(LOCTEXT("EffectLayerNumber", "Effect Layer {0}"), FText::AsNumber(LayerNumber));
		Layer.SourceSurface = TSoftObjectPtr<UMaterialLabSurface>(SelectedSurfacePath);
		break;
	}

	SelectedLayerIndex = WorkingLayers.Num() - 1;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::DuplicateSelectedLayer()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FReply::Handled();
	}

	SoloLayerIndex = INDEX_NONE;
	FMaterialLabLayer Copy = WorkingLayers[SelectedLayerIndex];
	Copy.DisplayName = FText::Format(
		LOCTEXT("CopiedLayerName", "{0} Copy"),
		Copy.DisplayName);
	WorkingLayers.Insert(Copy, SelectedLayerIndex + 1);
	MaterialLabUI::RemapHeightReferencesAfterInsert(WorkingLayers, SelectedLayerIndex + 1);
	++SelectedLayerIndex;
	bHasSelectedLayer = Copy.bEnabled;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::DeleteSelectedLayer()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex) || SelectedLayerIndex == 0)
	{
		return FReply::Handled();
	}

	SoloLayerIndex = INDEX_NONE;
	const int32 DeletedLayerIndex = SelectedLayerIndex;
	WorkingLayers.RemoveAt(DeletedLayerIndex);
	MaterialLabUI::RemapHeightReferencesAfterDelete(WorkingLayers, DeletedLayerIndex);
	SelectedLayerIndex = FMath::Clamp(DeletedLayerIndex - 1, 0, WorkingLayers.Num() - 1);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].bEnabled;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::MoveSelectedLayer(const int32 Direction)
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex) || SelectedLayerIndex == 0)
	{
		return FReply::Handled();
	}

	const int32 TargetIndex = FMath::Clamp(
		SelectedLayerIndex + Direction,
		1,
		WorkingLayers.Num() - 1);
	if (TargetIndex != SelectedLayerIndex)
	{
		SoloLayerIndex = INDEX_NONE;
		const int32 SourceIndex = SelectedLayerIndex;
		WorkingLayers.Swap(SourceIndex, TargetIndex);
		MaterialLabUI::RemapHeightReferencesAfterMove(WorkingLayers, SourceIndex, TargetIndex);
		SelectedLayerIndex = TargetIndex;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMaterialLab::HandleLayerDropped(
	const int32 SourceLayerIndex,
	const int32 TargetLayerIndex)
{
	if (!WorkingLayers.IsValidIndex(SourceLayerIndex)
		|| !WorkingLayers.IsValidIndex(TargetLayerIndex)
		|| SourceLayerIndex <= 0
		|| TargetLayerIndex <= 0
		|| SourceLayerIndex == TargetLayerIndex)
	{
		return FReply::Unhandled();
	}

	SoloLayerIndex = INDEX_NONE;
	FMaterialLabLayer MovedLayer = MoveTemp(WorkingLayers[SourceLayerIndex]);
	WorkingLayers.RemoveAt(SourceLayerIndex);
	WorkingLayers.Insert(MoveTemp(MovedLayer), TargetLayerIndex);
	MaterialLabUI::RemapHeightReferencesAfterMove(WorkingLayers, SourceLayerIndex, TargetLayerIndex);
	SelectedLayerIndex = TargetLayerIndex;
	bHasSelectedLayer = WorkingLayers[SelectedLayerIndex].bEnabled;
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMaterialLab::SelectWorkingLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers[LayerIndex].bEnabled;
	SyncSelectedLayerControls();
	RebuildMaskList();
	if (bWasBypassingChild)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FReply SMaterialLab::SelectWorkingChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	const bool bEffect = WorkingLayers[LayerIndex].Children[ChildIndex].Type
		== EMaterialLabLayerChildType::Effect;
	SelectedEffectIndex = bEffect ? ChildIndex : INDEX_NONE;
	SelectedMaskIndex = bEffect ? INDEX_NONE : ChildIndex;
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	RebuildLayerList();
	if (bWasBypassingChild)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FMaterialLabLayerEffect* SMaterialLab::GetSelectedLayerEffect()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedEffectIndex))
	{
		return nullptr;
	}
	FMaterialLabLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedEffectIndex];
	return Child.Type == EMaterialLabLayerChildType::Effect ? &Child.Effect : nullptr;
}

const FMaterialLabLayerEffect* SMaterialLab::GetSelectedLayerEffect() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedEffectIndex))
	{
		return nullptr;
	}
	const FMaterialLabLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedEffectIndex];
	return Child.Type == EMaterialLabLayerChildType::Effect ? &Child.Effect : nullptr;
}

FMaterialLabMaskLayer* SMaterialLab::GetSelectedLayerMask()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	FMaterialLabLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMaterialLabLayerChildType::Mask ? &Child.Mask : nullptr;
}

const FMaterialLabMaskLayer* SMaterialLab::GetSelectedLayerMask() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)
		|| !WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMaterialLabLayerChild& Child = WorkingLayers[SelectedLayerIndex].Children[SelectedMaskIndex];
	return Child.Type == EMaterialLabLayerChildType::Mask ? &Child.Mask : nullptr;
}

int32 SMaterialLab::GetSelectedChildIndex() const
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return INDEX_NONE;
	}

	const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
	if (Layer.Children.IsValidIndex(SelectedEffectIndex)
		&& Layer.Children[SelectedEffectIndex].Type == EMaterialLabLayerChildType::Effect)
	{
		return SelectedEffectIndex;
	}
	if (Layer.Children.IsValidIndex(SelectedMaskIndex)
		&& Layer.Children[SelectedMaskIndex].Type == EMaterialLabLayerChildType::Mask)
	{
		return SelectedMaskIndex;
	}
	return INDEX_NONE;
}

void SMaterialLab::SetWorkingLayerEnabled(const ECheckBoxState CheckState, const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
	{
		return;
	}

	WorkingLayers[LayerIndex].bEnabled = CheckState == ECheckBoxState::Checked;
	if (SelectedLayerIndex == LayerIndex)
	{
		bHasSelectedLayer = WorkingLayers[LayerIndex].bEnabled;
	}
	RefreshLayeredPreview();
}

void SMaterialLab::SyncSelectedLayerControls()
{
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		bHasSelectedLayer = false;
		return;
	}

	const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
	CurrentTiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
	CurrentRoughnessBias = Layer.RoughnessBias;
	CurrentRoughnessContrast = Layer.RoughnessContrast;
	CurrentRoughnessOffset = Layer.RoughnessOffset;
	CurrentNormalIntensity = Layer.NormalIntensity;

	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(Layer.DisplayName);
	}
	if (SelectedIdentityText.IsValid())
	{
		const FText LayerType = Layer.Type == EMaterialLabLayerType::Fill
			? LOCTEXT("FillLayerIdentity", "Fill")
			: Layer.Type == EMaterialLabLayerType::Effect
				? LOCTEXT("EffectLayerIdentity", "Effect")
				: LOCTEXT("MaterialLayerIdentity", "Material");
		SelectedIdentityText->SetText(LayerType);
	}
	if (SelectedMapsText.IsValid())
	{
		if (Layer.Type == EMaterialLabLayerType::Fill)
		{
			SelectedMapsText->SetText(LOCTEXT("FillLayerMaps", "Generated BC · RAM"));
		}
		else if (const UMaterialLabSurface* Surface = Layer.SourceSurface.LoadSynchronous())
		{
			SelectedMapsText->SetText(FText::FromString(FString::Printf(
				TEXT("BC %s  N %s  %s %s"),
				Surface->BaseColor ? TEXT("✓") : TEXT("—"),
				Surface->Normal ? TEXT("✓") : TEXT("—"),
				MaterialLabUI::PackedMapLabel(*Surface),
				Surface->RoughnessAOMetallic ? TEXT("✓") : TEXT("—"))));
		}
	}

	if (GetSelectedLayerEffect())
	{
		if (SelectedSurfaceText.IsValid()) SelectedSurfaceText->SetText(LOCTEXT("SelectedPeelingEffect", "Peeling"));
		if (SelectedIdentityText.IsValid()) SelectedIdentityText->SetText(LOCTEXT("SelectedPeelingIdentity", "Effect"));
		if (SelectedMapsText.IsValid()) SelectedMapsText->SetText(LOCTEXT("SelectedPeelingMaps", "PDM · MSK · H · SDF"));
	}
}

FReply SMaterialLab::AssignMaskToLayer(const int32 LayerIndex, const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
	{
		return FReply::Handled();
	}

	FMaterialLabLayer& Layer = WorkingLayers[LayerIndex];
	UObject* MaskObject = MaskPath.TryLoad();
	FMaterialLabMaskLayer NewMask;
	if (const UMaterialLabMask* Mask = Cast<UMaterialLabMask>(MaskObject))
	{
		NewMask.Mask = TSoftObjectPtr<UMaterialLabMask>(MaskPath);
		NewMask.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
		NewMask.Tiling = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
		NewMask.Balance = Mask->DefaultBalance;
		NewMask.Contrast = Mask->DefaultContrast;
		NewMask.bInvert = Mask->bDefaultInvert;
	}
	else if (Cast<UTexture2D>(MaskObject))
	{
		NewMask.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
	}
	else
	{
		return FReply::Handled();
	}

	const bool bHasMask = Layer.Children.ContainsByPredicate([](const FMaterialLabLayerChild& Child)
	{
		return Child.Type == EMaterialLabLayerChildType::Mask;
	});
	NewMask.BlendMode = bHasMask
		? EMaterialLabMaskBlendMode::Multiply
		: EMaterialLabMaskBlendMode::Replace;
	FMaterialLabLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMaterialLabLayerChildType::Mask;
	Child.Mask = MoveTemp(NewMask);
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = Layer.Children.Num() - 1;
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMaterialLab::ReplaceMaskInLayer(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const FSoftObjectPath MaskPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMaterialLabLayerChildType::Mask)
	{
		return FReply::Handled();
	}

	UObject* MaskObject = MaskPath.TryLoad();
	FMaterialLabMaskLayer Replacement = WorkingLayers[LayerIndex].Children[ChildIndex].Mask;
	Replacement.Mask.Reset();
	Replacement.MaskTexture.Reset();
	if (const UMaterialLabMask* Mask = Cast<UMaterialLabMask>(MaskObject))
	{
		Replacement.Mask = TSoftObjectPtr<UMaterialLabMask>(MaskPath);
		Replacement.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
		Replacement.Tiling = FMath::Clamp(FMath::RoundToInt(Mask->DefaultTiling), 1, 16);
		Replacement.Balance = Mask->DefaultBalance;
		Replacement.Contrast = Mask->DefaultContrast;
		Replacement.bInvert = Mask->bDefaultInvert;
	}
	else if (Cast<UTexture2D>(MaskObject))
	{
		Replacement.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
	}
	else
	{
		return FReply::Handled();
	}

	WorkingLayers[LayerIndex].Children[ChildIndex].Mask = MoveTemp(Replacement);
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMaterialLab::ClearLayerMask(const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0)
	{
		FMaterialLabLayer& Layer = WorkingLayers[LayerIndex];
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedEffectIndex != INDEX_NONE)
			{
				int32 RemovedBeforeSelection = 0;
				for (int32 ChildIndex = 0;
					ChildIndex < FMath::Min(SelectedEffectIndex, Layer.Children.Num());
					++ChildIndex)
				{
					RemovedBeforeSelection += Layer.Children[ChildIndex].Type
						== EMaterialLabLayerChildType::Mask ? 1 : 0;
				}
				SelectedEffectIndex -= RemovedBeforeSelection;
			}
			SelectedMaskIndex = INDEX_NONE;
		}
		Layer.Children.RemoveAll([](const FMaterialLabLayerChild& Child)
		{
			return Child.Type == EMaterialLabLayerChildType::Mask;
		});
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMaterialLab::RemoveMaskFromLayer(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMaterialLabLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedMaskIndex == ChildIndex)
			{
				SelectedMaskIndex = INDEX_NONE;
			}
			else if (SelectedMaskIndex > ChildIndex)
			{
				--SelectedMaskIndex;
			}
			if (SelectedEffectIndex > ChildIndex)
			{
				--SelectedEffectIndex;
			}
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMaterialLab::ReorderLayerChild(
	const int32 LayerIndex,
	const int32 SourceChildIndex,
	const int32 TargetChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(SourceChildIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(TargetChildIndex)
		|| SourceChildIndex == TargetChildIndex)
	{
		return FReply::Unhandled();
	}

	FMaterialLabLayerChild MovedChild = MoveTemp(WorkingLayers[LayerIndex].Children[SourceChildIndex]);
	WorkingLayers[LayerIndex].Children.RemoveAt(SourceChildIndex);
	WorkingLayers[LayerIndex].Children.Insert(MoveTemp(MovedChild), TargetChildIndex);
	if (SelectedLayerIndex == LayerIndex)
	{
		auto UpdateSelectedIndex = [SourceChildIndex, TargetChildIndex](int32& SelectedIndex)
		{
			if (SelectedIndex == SourceChildIndex)
			{
				SelectedIndex = TargetChildIndex;
			}
			else if (SourceChildIndex < TargetChildIndex
				&& SelectedIndex > SourceChildIndex && SelectedIndex <= TargetChildIndex)
			{
				--SelectedIndex;
			}
			else if (SourceChildIndex > TargetChildIndex
				&& SelectedIndex >= TargetChildIndex && SelectedIndex < SourceChildIndex)
			{
				++SelectedIndex;
			}
		};
		UpdateSelectedIndex(SelectedEffectIndex);
		UpdateSelectedIndex(SelectedMaskIndex);
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMaterialLab::ToggleLayerExpanded(const int32 LayerIndex)
{
	if (ExpandedLayerIndices.Contains(LayerIndex))
	{
		ExpandedLayerIndices.Remove(LayerIndex);
	}
	else
	{
		ExpandedLayerIndices.Add(LayerIndex);
	}
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMaterialLab::SetLayerNormalDetail(const int32 LayerIndex, const bool bNormalDetail)
{
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0)
	{
		WorkingLayers[LayerIndex].ChannelMode = bNormalDetail
			? EMaterialLabLayerChannelMode::NormalDetail
			: EMaterialLabLayerChannelMode::CompleteSurface;
		RefreshLayeredPreview();
		RebuildLayerList();
		SyncSelectedLayerControls();
	}
	return FReply::Handled();
}

FReply SMaterialLab::AssignNormalTexture(const int32 LayerIndex, const FSoftObjectPath NormalPath)
{
	if (WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0 && Cast<UTexture2D>(NormalPath.TryLoad()))
	{
		FMaterialLabLayer& Layer = WorkingLayers[LayerIndex];
		Layer.ChannelMode = EMaterialLabLayerChannelMode::NormalDetail;
		Layer.NormalSourceType = EMaterialLabNormalSourceType::Texture;
		Layer.NormalTexture = TSoftObjectPtr<UTexture2D>(NormalPath);
		RefreshLayeredPreview();
		RebuildLayerList();
		SyncSelectedLayerControls();
	}
	return FReply::Handled();
}

FReply SMaterialLab::AddEffectToLayer(const int32 LayerIndex, const FSoftObjectPath EffectPath)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || LayerIndex == 0)
	{
		return FReply::Handled();
	}

	FMaterialLabLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.Type != EMaterialLabLayerType::Material && Layer.Type != EMaterialLabLayerType::Fill)
	{
		return FReply::Handled();
	}

	const UMaterialLabEffect* Effect = Cast<UMaterialLabEffect>(EffectPath.TryLoad());
	if (!Effect)
	{
		return FReply::Handled();
	}

	FMaterialLabLayerChild& Child = Layer.Children.AddDefaulted_GetRef();
	Child.Type = EMaterialLabLayerChildType::Effect;
	FMaterialLabLayerEffect& LayerEffect = Child.Effect;
	LayerEffect.Effect = TSoftObjectPtr<UMaterialLabEffect>(EffectPath);
	if (Effect->EffectType == EMaterialLabEffectType::Stain)
	{
		LayerEffect.StainColor = Effect->DefaultStainColor;
		LayerEffect.StainRoughness = Effect->DefaultStainRoughness;
		LayerEffect.StainHeightInfluence = Effect->DefaultStainHeightInfluence;
		LayerEffect.StainHeightWarp = Effect->DefaultStainHeightWarp;
		LayerEffect.StainHeightBias = Effect->DefaultStainHeightBias;
		LayerEffect.StainHeightContrast = Effect->DefaultStainHeightContrast;
	}
	else
	{
		LayerEffect.Front = Effect->DefaultFront;
		LayerEffect.Width = Effect->DefaultWidth;
		LayerEffect.MacroWarp = Effect->DefaultMacroWarp;
		LayerEffect.MicroWarp = Effect->DefaultMicroWarp;
		LayerEffect.MicroMorph = Effect->DefaultMicroMorph;
		LayerEffect.Thickness = Effect->DefaultThickness;
		LayerEffect.Lift = Effect->DefaultLift;
		LayerEffect.DetailStrength = Effect->DefaultDetailStrength;
	}
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = Layer.Children.Num() - 1;
	SelectedMaskIndex = INDEX_NONE;
	ExpandedLayerIndices.Add(LayerIndex);
	SyncSelectedLayerControls();
	RefreshLayeredPreview();
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMaterialLab::ToggleLayerEffect(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMaterialLabLayerChildType::Effect)
	{
		FMaterialLabLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
		Effect.bEnabled = !Effect.bEnabled;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

FReply SMaterialLab::RemoveLayerEffect(const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMaterialLabLayerChildType::Effect)
	{
		WorkingLayers[LayerIndex].Children.RemoveAt(ChildIndex);
		if (SelectedLayerIndex == LayerIndex)
		{
			if (SelectedEffectIndex == ChildIndex)
			{
				SelectedEffectIndex = INDEX_NONE;
			}
			else if (SelectedEffectIndex > ChildIndex)
			{
				--SelectedEffectIndex;
			}
			if (SelectedMaskIndex > ChildIndex)
			{
				--SelectedMaskIndex;
			}
			SyncSelectedLayerControls();
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	}
	return FReply::Handled();
}

void SMaterialLab::SetMaskEnabled(const ECheckBoxState CheckState, const int32 LayerIndex, const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMaterialLabLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children[ChildIndex].Mask.bEnabled = CheckState == ECheckBoxState::Checked;
		RefreshLayeredPreview();
	}
}

void SMaterialLab::SetMaskBlendMode(
	const int32 LayerIndex,
	const int32 ChildIndex,
	const EMaterialLabMaskBlendMode BlendMode)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& WorkingLayers[LayerIndex].Children[ChildIndex].Type == EMaterialLabLayerChildType::Mask)
	{
		WorkingLayers[LayerIndex].Children[ChildIndex].Mask.BlendMode = BlendMode;
		RefreshLayeredPreview();
		RebuildLayerList();
	}
}

FReply SMaterialLab::OpenFillColorPicker(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| WorkingLayers[LayerIndex].Type != EMaterialLabLayerType::Fill)
	{
		return FReply::Handled();
	}

	LastHistoryRecordTime = 0.0;
	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.InitialColor = WorkingLayers[LayerIndex].BaseColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this,
		&SMaterialLab::SetFillBaseColor,
		LayerIndex);
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(
		this,
		&SMaterialLab::RestoreFillBaseColor,
		LayerIndex);
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

void SMaterialLab::SetFillBaseColor(FLinearColor NewColor, const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		NewColor.A = WorkingLayers[LayerIndex].BaseColor.A;
		WorkingLayers[LayerIndex].BaseColor = NewColor;
		RefreshLayeredPreview();
	}
}

void SMaterialLab::RestoreFillBaseColor(FLinearColor OriginalColor, const int32 LayerIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		OriginalColor.A = WorkingLayers[LayerIndex].BaseColor.A;
		WorkingLayers[LayerIndex].BaseColor = OriginalColor;
		SynchronizeHistoryAfterCancelledEdit();
		RefreshLayeredPreview(false);
	}
}

FReply SMaterialLab::OpenStainColorPicker(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMaterialLabLayerChildType::Effect)
	{
		return FReply::Handled();
	}

	FMaterialLabLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
	LastHistoryRecordTime = 0.0;
	FColorPickerArgs PickerArgs;
	PickerArgs.bUseAlpha = false;
	PickerArgs.bOnlyRefreshOnMouseUp = false;
	PickerArgs.InitialColor = Effect.StainColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this,
		&SMaterialLab::SetStainColor,
		LayerIndex,
		ChildIndex);
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(
		this,
		&SMaterialLab::RestoreStainColor,
		LayerIndex,
		ChildIndex);
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

void SMaterialLab::SetStainColor(
	FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		FMaterialLabLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
		NewColor.A = Effect.StainColor.A;
		Effect.StainColor = NewColor;
		RefreshLayeredPreview();
	}
}

void SMaterialLab::RestoreStainColor(
	FLinearColor OriginalColor,
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		FMaterialLabLayerEffect& Effect = WorkingLayers[LayerIndex].Children[ChildIndex].Effect;
		OriginalColor.A = Effect.StainColor.A;
		Effect.StainColor = OriginalColor;
		SynchronizeHistoryAfterCancelledEdit();
		RefreshLayeredPreview(false);
	}
}

FReply SMaterialLab::HandleLayerMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& PointerEvent,
	const int32 LayerIndex)
{
	SelectWorkingLayer(LayerIndex);
	if (PointerEvent.GetEffectingButton() == EKeys::RightMouseButton
		&& LayerIndex > 0
		&& WorkingLayers.IsValidIndex(LayerIndex)
		&& LayerContextAnchors.IsValidIndex(LayerIndex)
		&& LayerContextAnchors[LayerIndex].IsValid())
	{
		LayerContextAnchors[LayerIndex]->SetIsOpen(true);
	}
	return FReply::Handled();
}

bool SMaterialLab::CanCloseTab()
{
	return ConfirmDiscardUnsavedChanges();
}

bool SMaterialLab::ConfirmDiscardUnsavedChanges()
{
	if (!bIsWorkingMaterialDirty)
	{
		return true;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(
		EAppMsgType::YesNoCancel,
		LOCTEXT(
			"SaveUnsavedMaterialChanges",
			"Save changes to the current Material Lab recipe?\n\nYes: Save\nNo: Discard\nCancel: Keep editing"));
	if (Choice == EAppReturnType::Yes)
	{
		SaveWorkingMaterial();
		return !bIsWorkingMaterialDirty;
	}
	return Choice == EAppReturnType::No;
}

void SMaterialLab::RefreshLayeredPreview(const bool bMarkDirty)
{
	if (!bHasWorkingMaterial)
	{
		return;
	}

	if (bMarkDirty)
	{
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	}
	if (bPreviewRefreshPending)
	{
		return;
	}

	bPreviewRefreshPending = true;
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SMaterialLab::FlushPendingPreviewRefresh));
}

EActiveTimerReturnType SMaterialLab::FlushPendingPreviewRefresh(
	const double CurrentTime,
	const float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	bPreviewRefreshPending = false;
	if (!bHasWorkingMaterial)
	{
		return EActiveTimerReturnType::Stop;
	}

	TArray<FMaterialLabLayer> PreviewOverrideLayers;
	const TArray<FMaterialLabLayer>* PreviewLayers = &WorkingLayers;
	const int32 BypassedChildIndex = GetSelectedChildIndex();
	if (bBypassSelectedChild
		&& WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(BypassedChildIndex))
	{
		PreviewOverrideLayers = WorkingLayers;
		FMaterialLabLayerChild& Child = PreviewOverrideLayers[SelectedLayerIndex].Children[BypassedChildIndex];
		if (Child.Type == EMaterialLabLayerChildType::Effect)
		{
			Child.Effect.bEnabled = false;
		}
		else
		{
			Child.Mask.bEnabled = false;
		}
		PreviewLayers = &PreviewOverrideLayers;
	}
	if (WorkingLayers.IsValidIndex(SoloLayerIndex))
	{
		FMaterialLabLayer SoloLayer = (*PreviewLayers)[SoloLayerIndex];
		PreviewOverrideLayers.Reset();
		PreviewOverrideLayers.Add(MoveTemp(SoloLayer));
		PreviewOverrideLayers[0].bEnabled = true;
		PreviewOverrideLayers[0].HeightReferenceLayerIndex = INDEX_NONE;
		PreviewLayers = &PreviewOverrideLayers;
	}
	else if (bShowCompositionBefore && !WorkingLayers.IsEmpty())
	{
		FMaterialLabLayer BaseLayer = (*PreviewLayers)[0];
		PreviewOverrideLayers.Reset();
		PreviewOverrideLayers.Add(MoveTemp(BaseLayer));
		PreviewOverrideLayers[0].bEnabled = true;
		PreviewOverrideLayers[0].HeightReferenceLayerIndex = INDEX_NONE;
		PreviewLayers = &PreviewOverrideLayers;
	}

	for (const TSharedPtr<SMaterialLabPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewLayers(*PreviewLayers, CompositionResolution);
		}
	}
	return EActiveTimerReturnType::Stop;
}

void SMaterialLab::PreviewSurfaceScalarParameter(const FName ParameterName, const float Value)
{
	(void)ParameterName;
	(void)Value;
	RefreshLayeredPreview();
}

FReply SMaterialLab::SetCategoryFilter(const FName Family)
{
	CategoryFilter = Family;
	RebuildCategoryList();
	RebuildSurfaceList();
	return FReply::Handled();
}

void SMaterialLab::HandleSearchChanged(const FText& SearchTextValue)
{
	SearchText = SearchTextValue.ToString();
	RebuildSurfaceList();
}

void SMaterialLab::RebuildCategoryList()
{
	if (!CategoryListBox.IsValid())
	{
		return;
	}

	TArray<FName> Families;
	for (const FMaterialLabSurfaceEntry& Surface : FMaterialLabRegistry::GetSurfaces())
	{
		Families.AddUnique(Surface.Family);
	}
	Families.Remove(NAME_None);
	Families.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	if (!CategoryFilter.IsNone() && !Families.Contains(CategoryFilter))
	{
		CategoryFilter = NAME_None;
	}

	CategoryListBox->ClearChildren();
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	const auto AddCategory = [this, &Style](const FName Family, const FText& Label)
	{
		CategoryListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, CategoryListBox->GetChildren()->Num() > 0 ? 4.0f : 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(
				CategoryFilter == Family ? TEXT("MaterialLab.TabButtonActive") : TEXT("MaterialLab.TabButton")))
			.Text(Label)
			.OnClicked_Lambda([this, Family]() { return SetCategoryFilter(Family); })
		];
	};

	AddCategory(NAME_None, LOCTEXT("AllCategory", "All Materials"));
	for (const FName Family : Families)
	{
		AddCategory(Family, FText::FromName(Family));
	}
}

void SMaterialLab::RebuildSurfaceList()
{
	if (!SurfaceListBox.IsValid())
	{
		return;
	}

	SurfaceListBox->ClearChildren();
	SurfaceThumbnails.Reset();
	const TArray<FMaterialLabSurfaceEntry> Surfaces = FMaterialLabRegistry::GetSurfaces();
	int32 VisibleSurfaceIndex = 0;
	for (const FMaterialLabSurfaceEntry& Surface : Surfaces)
	{
		if (!CategoryFilter.IsNone() && Surface.Family != CategoryFilter)
		{
			continue;
		}
		if (!SearchText.IsEmpty() && !Surface.DisplayName.ToString().Contains(SearchText))
		{
			continue;
		}

		SurfaceListBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			BuildSurfaceCard(Surface.DisplayName, Surface.AssetPath, Surface.ThumbnailAsset)
		];
		++VisibleSurfaceIndex;
	}

	if (VisibleSurfaceIndex == 0)
	{
		SurfaceListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("EmptyRegistry", "Starter library is empty. Export the metal maps to:\n{0}"),
				FText::FromString(FMaterialLabSurfaceImporter::GetDefaultSourceDirectory())))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

void SMaterialLab::RebuildLayerList()
{
	if (!LayerListBox.IsValid())
	{
		return;
	}

	LayerListBox->ClearChildren();
	LayerContextAnchors.Reset();
	LayerThumbnails.Reset();
	for (int32 LayerIndex = 0; LayerIndex < WorkingLayers.Num(); ++LayerIndex)
	{
		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			BuildLayerRow(LayerIndex)
		];
	}
}

void SMaterialLab::RebuildMaskList()
{
	if (!MaskListBox.IsValid())
	{
		return;
	}

	MaskListBox->ClearChildren();
	MaskThumbnails.Reset();

	const TArray<FMaterialLabMaskEntry> Masks = FMaterialLabRegistry::GetMasks();
	for (int32 MaskIndex = 0; MaskIndex < Masks.Num(); ++MaskIndex)
	{
		const FMaterialLabMaskEntry& Mask = Masks[MaskIndex];
		MaskListBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			BuildMaskCard(SelectedLayerIndex, Mask.DisplayName, Mask.AssetPath, Mask.ThumbnailAsset, true)
		];
	}

	if (Masks.IsEmpty())
	{
		MaskListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EmptyMaskRegistry", "No mask assets in /MaterialLab/Masks"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

TSharedRef<SWidget> SMaterialLab::BuildCompositionResolutionMenu()
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const int32 Resolutions[] = {1024, 2048, 4096};
	for (const int32 Resolution : Resolutions)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(FText::Format(
				LOCTEXT("CompositionResolutionOption", "{0}K ({1} × {1})"),
				FText::AsNumber(Resolution / 1024),
				FText::AsNumber(Resolution)))
			.OnClicked(this, &SMaterialLab::SetCompositionResolution, Resolution)
		];
	}
	return SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
		[Menu];
}

TSharedRef<SWidget> SMaterialLab::BuildTopBar()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBox)
		.HeightOverride(MaterialLabUI::TopBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(8.0f, 3.0f))
			.BorderImage(Style.GetBrush(TEXT("MaterialLab.TopBar")))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "MATERIAL LAB"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.OnClicked_Lambda([this]() { return ShowPage(0); })
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(
								bIsWorkingMaterialDirty
									? WorkingMaterialName + TEXT("  •")
									: WorkingMaterialName);
						})
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.IsEnabled_Lambda([this]() { return !UndoHistory.IsEmpty(); })
					.Text(LOCTEXT("UndoMaterialEdit", "Undo"))
					.ToolTipText(LOCTEXT("UndoMaterialEditHint", "Undo the last Material Lab recipe edit (Ctrl+Z)."))
					.OnClicked(this, &SMaterialLab::UndoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.IsEnabled_Lambda([this]() { return !RedoHistory.IsEmpty(); })
					.Text(LOCTEXT("RedoMaterialEdit", "Redo"))
					.ToolTipText(LOCTEXT("RedoMaterialEditHint", "Redo the last Material Lab recipe edit (Ctrl+Y or Ctrl+Shift+Z)."))
					.OnClicked(this, &SMaterialLab::RedoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.PrimaryButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMaterialLab::SaveWorkingMaterial)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Save")))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "Save Material"))]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.OnClicked(this, &SMaterialLab::SaveWorkingMaterialAs)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.SaveAs")))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("SaveAsTop", "Save As..."))]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
					.ToolTipText(LOCTEXT("CompositionResolutionHint", "Choose the shared preview and bake texture resolution."))
					.OnGetMenuContent(this, &SMaterialLab::BuildCompositionResolutionMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::Format(
								LOCTEXT("CurrentCompositionResolution", "{0}K"),
								FText::AsNumber(CompositionResolution / 1024));
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.IsEnabled_Lambda([this]() { return WorkingMaterialAsset.IsValid() && bHasWorkingMaterial; })
					.Text(LOCTEXT("BakeMaterialTop", "Bake"))
					.ToolTipText(LOCTEXT("BakeMaterialHint", "Bake the current GPU-composited BC, Normal, and RAM outputs."))
					.OnClicked(this, &SMaterialLab::BakeWorkingMaterial)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.HasDownArrow(false)
					.ButtonContent()[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Overflow")))]
					.OnGetMenuContent(this, &SMaterialLab::BuildWorkflowMenu)
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildAuthoringPage()
{
	return SNew(SBorder)
		.Padding(0.0f)
		.IsEnabled_Lambda([this]() { return !bIsBaking; })
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Window")))
		[
			SNew(SSplitter)
			.PhysicalSplitterHandleSize(MaterialLabUI::SplitterHandleSize)
			.HitDetectionSplitterHandleSize(MaterialLabUI::SplitterHitSize)
			+ SSplitter::Slot().Value(0.19f)
			[
				BuildLeftPanel()
			]
			+ SSplitter::Slot().Value(0.60f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)
				.PhysicalSplitterHandleSize(MaterialLabUI::SplitterHandleSize)
				.HitDetectionSplitterHandleSize(MaterialLabUI::SplitterHitSize)
				+ SSplitter::Slot().Value(0.64f)[BuildPreviewPanel()]
				+ SSplitter::Slot().Value(0.36f)[BuildBottomLibrary()]
			]
			+ SSplitter::Slot().Value(0.21f)
			[
				BuildInspectorPanel()
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildLeftPanel()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(Style.GetBrush(TEXT("MaterialLab.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.0f, 4.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(88.0f)
					[
						SNew(SBorder)
						.Padding(0.0f)
						.BorderImage_Lambda([this]() { return FMaterialLabStyle::Get().GetBrush(LeftTabIndex == 0 ? TEXT("MaterialLab.InsetPanel") : TEXT("MaterialLab.SectionBar")); })
						[
					SNew(SCheckBox)
					.Style(&Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("MaterialLab.TabToggle")))
					.IsChecked_Lambda([this]() { return LeftTabIndex == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ShowLeftPage(0); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("LayersLeftTab", "LAYERS")).Justification(ETextJustify::Center)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[SNew(SBox).HeightOverride(2.0f)[SNew(SBorder).BorderImage_Lambda([this]() { return FMaterialLabStyle::Get().GetBrush(LeftTabIndex == 0 ? TEXT("MaterialLab.TabUnderlineSelected") : TEXT("MaterialLab.TabUnderline")); })]]
					]
				]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(88.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					.BorderImage_Lambda([this]() { return FMaterialLabStyle::Get().GetBrush(LeftTabIndex == 1 ? TEXT("MaterialLab.InsetPanel") : TEXT("MaterialLab.SectionBar")); })
					[
					SNew(SCheckBox)
					.Style(&Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("MaterialLab.TabToggle")))
					.IsChecked_Lambda([this]() { return LeftTabIndex == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ShowLeftPage(1); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("LibraryLeftTab", "LIBRARY")).Justification(ETextJustify::Center)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[SNew(SBox).HeightOverride(2.0f)[SNew(SBorder).BorderImage_Lambda([this]() { return FMaterialLabStyle::Get().GetBrush(LeftTabIndex == 1 ? TEXT("MaterialLab.TabUnderlineSelected") : TEXT("MaterialLab.TabUnderline")); })]]
					]
				]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HeightOverride(1.0f)
				[SNew(SBorder).BorderImage(Style.GetBrush(TEXT("MaterialLab.TabUnderline")))]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SAssignNew(LeftSwitcher, SWidgetSwitcher)
				.WidgetIndex(0)
				+ SWidgetSwitcher::Slot()[BuildLayerStackPanel()]
				+ SWidgetSwitcher::Slot()[BuildLibraryPage()]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildBottomLibrary()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBorder)
		.Padding(3.0f)
		.BorderImage(Style.GetBrush(TEXT("MaterialLab.Panel")))
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(MaterialLabUI::SplitterHandleSize)
			.HitDetectionSplitterHandleSize(MaterialLabUI::SplitterHitSize)
			+ SSplitter::Slot().Value(0.72f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f)
				[SNew(STextBlock).Text(LOCTEXT("MaterialsColumn", "MATERIALS")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(150.0f)[BuildLibraryPage()]]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f)[BuildSurfaceList()]
				]
			]
			+ SSplitter::Slot().Value(0.28f)
			[
				BuildMaskBar()
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildStatusBar()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBox)
		.HeightOverride(MaterialLabUI::StatusBarHeight)
		[
			SNew(SBorder)
			.Padding(FMargin(6.0f, 2.0f))
			.BorderImage(Style.GetBrush(TEXT("MaterialLab.TopBar")))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(WorkingStatusText); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("MaterialLab.MutedText")))]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("RealtimeStatus", "●  Real-time Preview     Quality: High     Shader Model: SM6")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("MaterialLab.MutedText")))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("LayerStatus", "Layers {0}"), FText::AsNumber(WorkingLayers.Num())); }).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("MaterialLab.MutedText")))]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildWorkflowMenu()
{
	return SNew(SBorder)
		.Padding(6.0f)
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
				.Text(LOCTEXT("NewMaterialMenu", "New Material"))
				.OnClicked(this, &SMaterialLab::NewWorkingMaterial)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
				.Text(LOCTEXT("OpenMaterialMenu", "Open Material..."))
				.OnClicked(this, &SMaterialLab::OpenWorkingMaterial)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("AuthoringMenu", "Material Authoring"), 0)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("MixerMenu", "Mixer (Legacy)"), 1)]
			+ SVerticalBox::Slot().AutoHeight()[BuildNavButton(LOCTEXT("PresetsMenu", "Presets"), 2)]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildNavButton(const FText& Label, const int32 PageIndex)
{
	return SNew(SButton)
		.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
		.OnClicked_Lambda([this, PageIndex]() { return ShowPage(PageIndex); })
		[SNew(STextBlock).Text(Label)];
}

TSharedRef<SWidget> SMaterialLab::BuildLibraryPage()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBorder)
		.Padding(MaterialLabUI::PanelPadding)
		.BorderImage(Style.GetBrush(TEXT("MaterialLab.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search materials..."))
				.OnTextChanged(this, &SMaterialLab::HandleSearchChanged)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(CategoryListBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)[SNew(SSpacer)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.ContentPadding(5.0f)
					.ToolTipText(LOCTEXT("ChooseTextureFolderHint", "Choose Texture Folder..."))
					.OnClicked(this, &SMaterialLab::ImportSurfaces)
					[
						SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Folder")))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
					.ContentPadding(5.0f)
					.ToolTipText(LOCTEXT(
						"ReimportShippedHint",
						"Reimport Shipped Library from Plugins/MaterialLab/Content/Textures."))
					.OnClicked(this, &SMaterialLab::ReimportShippedLibrary)
					[
						SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Refresh")))
					]
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildSurfaceList()
{
	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
		[
			SAssignNew(SurfaceListBox, SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(2.0f, 2.0f))
		];
}

TSharedRef<SWidget> SMaterialLab::BuildLayerStackPanel()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SMaterialLabLayerDropTarget)
		.OnSurfaceDropped(this, &SMaterialLab::HandleSurfaceDropped)
		[
			SNew(SBox)
			.WidthOverride(MaterialLabUI::LayerStackWidth)
			[
				SNew(SBorder)
			.Padding(MaterialLabUI::PanelPadding)
			.BorderImage(Style.GetBrush(TEXT("MaterialLab.Panel")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Collapsed : EVisibility::Visible; })
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.PrimaryButton")))
						.Text(LOCTEXT("CreateWorkingMaterial", "Create Material"))
						.IsEnabled_Lambda([this]() { return SelectedPreviewMaterial.IsValid(); })
						.ToolTipText(LOCTEXT("CreateWorkingMaterialHint", "Select a saved library surface first, then create a nondestructive layered recipe."))
						.OnClicked(this, &SMaterialLab::StartNewMaterial)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
						.Text(LOCTEXT("OpenWorkingMaterialFromLayers", "Open Saved Recipe..."))
						.OnClicked(this, &SMaterialLab::OpenWorkingMaterial)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([]()
						{
							return FMaterialLabRegistry::GetSurfaces().IsEmpty()
								? LOCTEXT("NoSavedSurfaces", "No saved Material Lab surfaces were found. Import a complete texture set or open an existing recipe.")
								: LOCTEXT("SelectSurfaceToBegin", "Select or drag a library surface to begin.");
						})
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SComboButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.PrimaryButton")))
						.ButtonContent()
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
													[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Add")))]
													+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
													[SNew(STextBlock).Text(LOCTEXT("AddLayer", "Add Layer"))]
												]
						.OnGetMenuContent(this, &SMaterialLab::BuildAddLayerMenu)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
						.ToolTipText(LOCTEXT("MoveLayerUpHint", "Move layer up"))
						.IsEnabled_Lambda([this]() { return SelectedLayerIndex > 1; })
						.OnClicked_Lambda([this]() { return MoveSelectedLayer(-1); })
						[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.ArrowUp")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
						.ToolTipText(LOCTEXT("MoveLayerDownHint", "Move layer down"))
						.IsEnabled_Lambda([this]()
						{
							return SelectedLayerIndex > 0
								&& SelectedLayerIndex < WorkingLayers.Num() - 1;
						})
						.OnClicked_Lambda([this]() { return MoveSelectedLayer(1); })
						[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.ArrowDown")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
						.ToolTipText(LOCTEXT("DuplicateLayerHint", "Duplicate selected layer"))
						.IsEnabled_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex); })
						.OnClicked(this, &SMaterialLab::DuplicateSelectedLayer)
						[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Duplicate")))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.TopButton")))
						.ToolTipText(LOCTEXT("DeleteLayerHint", "Delete selected layer"))
						.IsEnabled_Lambda([this]() { return SelectedLayerIndex > 0; })
						.OnClicked(this, &SMaterialLab::DeleteSelectedLayer)
						[SNew(SImage).Image(Style.GetBrush(TEXT("MaterialLab.Icon.Trash")))]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()[SAssignNew(LayerListBox, SVerticalBox)]
				]
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildLayerRow(const int32 LayerIndex)
{
	const FMaterialLabLayer& Layer = WorkingLayers[LayerIndex];
	const FText TypeLabel = LayerIndex == 0
		? LOCTEXT("BaseLockedLabel", "BASE · LOCKED")
		: Layer.ChannelMode == EMaterialLabLayerChannelMode::NormalDetail
			? LOCTEXT("NormalDetailLayerLabel", "NORMAL DETAIL")
			: Layer.Type == EMaterialLabLayerType::Fill
				? LOCTEXT("FillLayerLabel", "FILL")
				: Layer.Type == EMaterialLabLayerType::Effect
					? LOCTEXT("EffectLayerLabel", "EFFECT")
					: LOCTEXT("MaterialLayerLabel", "MATERIAL");
	int32 MaskCount = 0;
	int32 EffectCount = 0;
	for (const FMaterialLabLayerChild& Child : Layer.Children)
	{
		MaskCount += Child.Type == EMaterialLabLayerChildType::Mask ? 1 : 0;
		EffectCount += Child.Type == EMaterialLabLayerChildType::Effect ? 1 : 0;
	}
	const FText LayerSummary = Layer.Children.IsEmpty()
		? TypeLabel
		: FText::Format(
			LOCTEXT("LayerChildSummary", "{0} · {1} effect(s) · {2} mask(s)"),
			TypeLabel,
			FText::AsNumber(EffectCount),
			FText::AsNumber(MaskCount));

	TSharedRef<SWidget> LayerThumbnail = SNew(SColorBlock)
		.Color_Lambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex)
				&& WorkingLayers[LayerIndex].Type == EMaterialLabLayerType::Fill
				? WorkingLayers[LayerIndex].BaseColor
				: FLinearColor(0.08f, 0.08f, 0.08f);
		})
		.Size(FVector2D(32.0f, 32.0f));
	if (Layer.Type != EMaterialLabLayerType::Fill)
	{
		if (Layer.ChannelMode == EMaterialLabLayerChannelMode::NormalDetail
			&& Layer.NormalSourceType == EMaterialLabNormalSourceType::Texture
			&& !Layer.NormalTexture.IsNull())
		{
			if (UTexture2D* Texture = Layer.NormalTexture.LoadSynchronous())
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(Texture), 32, 32, ThumbnailPool);
				LayerThumbnails.Add(Thumbnail);
				LayerThumbnail = Thumbnail->MakeThumbnailWidget();
			}
		}
		else if (const UMaterialLabSurface* Surface = Layer.SourceSurface.LoadSynchronous())
		{
			if (Surface->PreviewMaterial)
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
					FAssetData(Surface->PreviewMaterial.Get()), 32, 32, ThumbnailPool);
				LayerThumbnails.Add(Thumbnail);
				LayerThumbnail = Thumbnail->MakeThumbnailWidget();
			}
		}
	}


	TSharedPtr<SMenuAnchor> ContextAnchor;
	TSharedRef<SMenuAnchor> Row = SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.OnGetMenuContent(this, &SMaterialLab::BuildLayerContextMenu, LayerIndex)
		[
			SNew(SBorder)
			.Padding(4.0f)
			.BorderImage_Lambda([this, LayerIndex]()
			{
				return SelectedLayerIndex == LayerIndex
					? FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.LayerCardSelected"))
					: FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.LayerCard"));
			})
			.OnMouseButtonDown(this, &SMaterialLab::HandleLayerMouseButtonDown, LayerIndex)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(12.0f)
					.Visibility(LayerIndex > 0 ? EVisibility::Visible : EVisibility::Hidden)
					[
						SNew(SMaterialLabLayerDragHandle)
						.LayerIndex(LayerIndex)
						.DisplayName(Layer.DisplayName)
						[
							SNew(SImage)
							.Image(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Grip")))
							.ToolTipText(LOCTEXT("LayerDragHandleHint", "Drag to reorder"))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleButtonCheckbox")))
					.ToolTipText(LOCTEXT("SoloLayerHint", "Preview only this layer"))
					.IsChecked_Lambda([this, LayerIndex]()
					{
						return SoloLayerIndex == LayerIndex
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, LayerIndex](const ECheckBoxState State)
					{
						SoloLayerIndex = State == ECheckBoxState::Checked ? LayerIndex : INDEX_NONE;
						if (SoloLayerIndex != INDEX_NONE)
						{
							bShowCompositionBefore = false;
						}
						RefreshLayeredPreview(false);
						RebuildLayerList();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SoloLayerButton", "S"))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsEnabled(LayerIndex > 0)
					.IsChecked_Lambda([this, LayerIndex]()
					{
						return WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].bEnabled
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(this, &SMaterialLab::SetWorkingLayerEnabled, LayerIndex)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(32.0f).HeightOverride(32.0f)[LayerThumbnail]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(5.0f, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Layer.DisplayName)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LayerSummary)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.DragHandle")))
					.ContentPadding(FMargin(2.0f, 0.0f))
					.ToolTipText(LOCTEXT("ToggleLayerDetails", "Show layer details"))
					.OnClicked(this, &SMaterialLab::ToggleLayerExpanded, LayerIndex)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(ExpandedLayerIndices.Contains(LayerIndex)
							? TEXT("Icons.ChevronDown")
							: TEXT("Icons.ChevronRight")))
					]
				]
			]
		];

	LayerContextAnchors.SetNum(FMath::Max(LayerContextAnchors.Num(), LayerIndex + 1));
	LayerContextAnchors[LayerIndex] = ContextAnchor;

	TSharedRef<SVerticalBox> RowContent = SNew(SVerticalBox);
	RowContent->AddSlot().AutoHeight()[Row];
	if (ExpandedLayerIndices.Contains(LayerIndex))
	{

		if (Layer.ChannelMode == EMaterialLabLayerChannelMode::NormalDetail)
		{
			const FText SourceText = Layer.NormalSourceType == EMaterialLabNormalSourceType::Texture
				? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName())
				: FText::Format(LOCTEXT("SurfaceNormalSource", "Surface · {0}"), Layer.DisplayName);
			RowContent->AddSlot().AutoHeight().Padding(20.0f, 2.0f, 4.0f, 2.0f)
			[SNew(STextBlock).Text(SourceText).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		}

		for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
		{
			const FMaterialLabLayerChild& Child = Layer.Children[ChildIndex];
			const bool bEffect = Child.Type == EMaterialLabLayerChildType::Effect;
			FText ChildName;
			FText ChildSummary;
			TSharedRef<SWidget> ChildIcon = SNew(SImage).Image(MaterialLabUI::LucideIcon(TEXT("nodes")));
			if (bEffect)
			{
				const UMaterialLabEffect* EffectAsset = Child.Effect.Effect.LoadSynchronous();
				if (EffectAsset && EffectAsset->EffectType == EMaterialLabEffectType::Peeling)
				{
					ChildName = LOCTEXT("PeelingEffectName", "Peeling");
				}
				else if (EffectAsset && EffectAsset->EffectType == EMaterialLabEffectType::Stain)
				{
					ChildName = LOCTEXT("StainEffectName", "Stain");
				}
				else
				{
					ChildName = FText::FromString(Child.Effect.Effect.ToSoftObjectPath().GetAssetName());
				}
				ChildSummary = FText::FromString(FString::Printf(TEXT("Effect · %.2f"), Child.Effect.Strength));
			}
			else
			{
				const FMaterialLabMaskLayer& MaskLayer = Child.Mask;
				const FSoftObjectPath MaskPath = !MaskLayer.Mask.IsNull()
					? MaskLayer.Mask.ToSoftObjectPath() : MaskLayer.MaskTexture.ToSoftObjectPath();
				ChildName = FText::FromString(MaskPath.GetAssetName());
				ChildSummary = FText::Format(
					LOCTEXT("MaskChildSummary", "{0} · {1}"),
					MaterialLabUI::MaskBlendModeText(MaskLayer.BlendMode),
					FText::FromString(FString::Printf(TEXT("%.2f"), MaskLayer.Weight)));
				if (UObject* MaskObject = MaskPath.TryLoad())
				{
					UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
					if (const UMaterialLabMask* MaskAsset = Cast<UMaterialLabMask>(MaskObject))
					{
						Texture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
					}
					if (Texture)
					{
						TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
							FAssetData(Texture), 24, 24, ThumbnailPool);
						LayerThumbnails.Add(Thumbnail);
						ChildIcon = Thumbnail->MakeThumbnailWidget();
					}
				}
			}

			RowContent->AddSlot().AutoHeight().Padding(8.0f, 1.0f, 4.0f, 1.0f)
			[
				SNew(SMaterialLabChildStackItem)
				.LayerIndex(LayerIndex)
				.ChildIndex(ChildIndex)
				.DisplayName(ChildName)
				.OnGetMenuContent_Lambda([this, LayerIndex, ChildIndex, bEffect]()
				{
					return bEffect
						? BuildEffectContextMenu(LayerIndex, ChildIndex)
						: BuildMaskContextMenu(LayerIndex, ChildIndex);
				})
				.OnSelected(this, &SMaterialLab::SelectWorkingChild)
				.OnChildReordered(this, &SMaterialLab::ReorderLayerChild)
				[
					SNew(SBox)
					.HeightOverride(34.0f)
					[
						SNew(SBorder)
						.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
						.BorderImage_Lambda([this, LayerIndex, ChildIndex, bEffect]()
						{
							const bool bSelected = SelectedLayerIndex == LayerIndex
								&& (bEffect ? SelectedEffectIndex : SelectedMaskIndex) == ChildIndex;
							return bSelected
								? FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.LayerCardSelected"))
								: FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.LayerCard"));
						})
						.ToolTipText(LOCTEXT("ChildRowHint", "LMB: select · drag: reorder · overflow: actions"))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
							[
								SNew(SMaterialLabHierarchyConnector)
								.IsLast(ChildIndex == Layer.Children.Num() - 1
									&& (LayerIndex == 0
										|| (Layer.Type != EMaterialLabLayerType::Material
											&& Layer.Type != EMaterialLabLayerType::Fill)))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Grip")))]
							+ SHorizontalBox::Slot().AutoWidth().Padding(2.0f, 0.0f).VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([this, LayerIndex, ChildIndex]()
								{
									if (!WorkingLayers.IsValidIndex(LayerIndex)
										|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
									{
										return ECheckBoxState::Unchecked;
									}
									const FMaterialLabLayerChild& Current = WorkingLayers[LayerIndex].Children[ChildIndex];
									const bool bEnabled = Current.Type == EMaterialLabLayerChildType::Effect
										? Current.Effect.bEnabled : Current.Mask.bEnabled;
									return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this, LayerIndex, ChildIndex, bEffect](const ECheckBoxState State)
								{
									if (bEffect)
									{
										ToggleLayerEffect(LayerIndex, ChildIndex);
									}
									else
									{
										SetMaskEnabled(State, LayerIndex, ChildIndex);
									}
								})
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)[ChildIcon]]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f).VAlign(VAlign_Center)
							[SNew(STextBlock).Text(ChildName).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
							+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(ChildSummary)
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SComboButton)
								.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
								.ContentPadding(2.0f)
								.HasDownArrow(false)
								.OnGetMenuContent_Lambda([this, LayerIndex, ChildIndex, bEffect]()
								{
									return bEffect
										? BuildEffectContextMenu(LayerIndex, ChildIndex)
										: BuildMaskContextMenu(LayerIndex, ChildIndex);
								})
								.ButtonContent()[SNew(SImage).Image(MaterialLabUI::LucideIcon(TEXT("ellipsis")))]
							]
						]
					]
				]
			];
		}
		if (LayerIndex > 0
			&& (Layer.Type == EMaterialLabLayerType::Material || Layer.Type == EMaterialLabLayerType::Fill))
		{
			RowContent->AddSlot().AutoHeight().Padding(8.0f, 1.0f, 4.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
				[SNew(SMaterialLabHierarchyConnector).IsLast(true)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
					.ContentPadding(FMargin(7.0f, 2.0f))
					.HasDownArrow(false)
					.OnGetMenuContent(this, &SMaterialLab::BuildAddChildMenu, LayerIndex)
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[SNew(SImage).Image(MaterialLabUI::LucideIcon(TEXT("plus")))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("AddChild", "Add Child"))]
					]
				]
			];
		}
	}

	return SNew(SMaterialLabLayerRowDropTarget)
		.TargetLayerIndex(LayerIndex)
		.OnLayerDropped(this, &SMaterialLab::HandleLayerDropped)
		.OnMaskDropped(this, &SMaterialLab::AssignMaskToLayer)
		[RowContent];
}

TSharedRef<SWidget> SMaterialLab::BuildLayerContextMenu(const int32 LayerIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection(TEXT("MaterialLabLayerActions"), LOCTEXT("LayerActionsSection", "Layer"));
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.ContainsByPredicate([](const FMaterialLabLayerChild& Child)
		{
			return Child.Type == EMaterialLabLayerChildType::Mask;
		}))
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ClearLayerMasksContext", "Remove All Masks"),
			LOCTEXT("ClearLayerMasksContextHint", "Remove every mask child from this layer."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { ClearLayerMask(LayerIndex); })));
	}
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateLayerContext", "Duplicate Layer"),
		LOCTEXT("DuplicateLayerContextHint", "Duplicate this layer and its child effects and masks."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { DuplicateSelectedLayer(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteLayerContext", "Delete Layer"),
		LOCTEXT("DeleteLayerContextHint", "Delete this layer."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { DeleteSelectedLayer(); })));
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMaterialLab::BuildAddChildMenu(const int32 LayerIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddMaskChild", "Mask"),
		LOCTEXT("AddMaskChildHint", "Append a reusable mask child."),
		FNewMenuDelegate::CreateLambda([this, LayerIndex](FMenuBuilder& MaskMenu)
		{
			const TArray<FMaterialLabMaskEntry> Masks = FMaterialLabRegistry::GetMasks();
			for (const FMaterialLabMaskEntry& Entry : Masks)
			{
				MaskMenu.AddMenuEntry(
					Entry.DisplayName,
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, MaskPath = Entry.AssetPath]()
					{
						AssignMaskToLayer(LayerIndex, MaskPath);
					})));
			}
			if (Masks.IsEmpty())
			{
				MaskMenu.AddMenuEntry(
					LOCTEXT("MasksUnavailable", "No masks available"),
					LOCTEXT("MasksUnavailableHint", "Import or reimport reusable masks first."),
					FSlateIcon(),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
			}
		}),
		false,
		FSlateIcon());
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		LOCTEXT("AddEffectChildHint", "Append an ordered effect child."),
		FNewMenuDelegate::CreateLambda([this, LayerIndex](FMenuBuilder& EffectMenu)
		{
			const TArray<FMaterialLabEffectEntry> Effects = FMaterialLabRegistry::GetEffects();
			for (const FMaterialLabEffectEntry& Entry : Effects)
			{
				EffectMenu.AddMenuEntry(
					Entry.DisplayName,
					FText::FromName(Entry.Category),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, EffectPath = Entry.AssetPath]()
					{
						AddEffectToLayer(LayerIndex, EffectPath);
					})));
			}
			if (Effects.IsEmpty())
			{
				EffectMenu.AddMenuEntry(
					LOCTEXT("EffectsUnavailable", "No effects available"),
					LOCTEXT("EffectsUnavailableHint", "Reimport the shipped library to create effect assets."),
					FSlateIcon(),
					FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
			}
		}),
		false,
		FSlateIcon());
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMaterialLab::BuildEffectContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RemoveEffectChild", "Remove Effect"),
		LOCTEXT("RemoveEffectChildHint", "Remove this effect child."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveLayerEffect(LayerIndex, ChildIndex);
		})));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMaterialLab::BuildAddLayerMenu()
{
	return SNew(SBorder)
		.Padding(6.0f)
		.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.Text(FText::Format(
					LOCTEXT("AddSelectedMaterialLayer", "Material Layer · {0}"),
					SelectedLibrarySurfaceName.IsEmpty()
						? LOCTEXT("NoSelectedLibraryMaterial", "Select from Library")
						: SelectedLibrarySurfaceName))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddMaterialLayerHint", "Inherit the currently selected immutable library material."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMaterialLabLayerType::Material); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddFillLayer", "Fill Layer"))
				.ToolTipText(LOCTEXT("AddFillLayerHint", "Create a constant Base Color, Roughness, IOR, and Metallic surface."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMaterialLabLayerType::Fill); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddEffectLayer", "Effect Layer"))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddEffectLayerHint", "Add the selected library surface as a reusable replacement or coating effect."))
				.OnClicked_Lambda([this]() { return AddWorkingLayer(EMaterialLabLayerType::Effect); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddNormalDetailLayer", "Normal Detail Layer"))
				.IsEnabled(!SelectedSurfacePath.IsNull())
				.ToolTipText(LOCTEXT("AddNormalDetailLayerHint", "Use only the selected surface normal and preserve BC/RAM."))
				.OnClicked_Lambda([this]()
				{
					AddWorkingLayer(EMaterialLabLayerType::Effect);
					return SetLayerNormalDetail(SelectedLayerIndex, true);
				})
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildMaskBar()
{
	return SNew(SBorder)
		.Padding(FMargin(8.0f, 7.0f))
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.InsetPanel")))
		.Visibility_Lambda([this]()
		{
			return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MaskBarHeading", "MASKS · DRAG ONTO A LAYER"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(MaskListBox, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(3.0f, 3.0f))
				]
			]
		];
}


TSharedRef<SWidget> SMaterialLab::BuildMaskBlendModeMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const EMaterialLabMaskBlendMode Modes[] = {
		EMaterialLabMaskBlendMode::Replace,
		EMaterialLabMaskBlendMode::Add,
		EMaterialLabMaskBlendMode::Subtract,
		EMaterialLabMaskBlendMode::Multiply,
		EMaterialLabMaskBlendMode::Min,
		EMaterialLabMaskBlendMode::Max};
	for (const EMaterialLabMaskBlendMode Mode : Modes)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(MaterialLabUI::MaskBlendModeText(Mode))
			.OnClicked_Lambda([this, LayerIndex, MaskIndex, Mode]()
			{
				SetMaskBlendMode(LayerIndex, MaskIndex, Mode);
				return FReply::Handled();
			})
		];
	}
	return SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[Menu];
}

TSharedRef<SWidget> SMaterialLab::BuildMaskContextMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	return SNew(SBox)
		.WidthOverride(340.0f)
		.MaxDesiredHeight(460.0f)
		[
			SNew(SBorder)
			.Padding(6.0f)
			.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 4.0f)
				[SNew(STextBlock).Text(LOCTEXT("ReplaceMaskHeading", "REPLACE MASK")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
				+ SVerticalBox::Slot().FillHeight(1.0f)[BuildMaskReplacementGallery(LayerIndex, MaskIndex)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
					.Text(LOCTEXT("DeleteMaskContext", "Remove This Mask"))
					.OnClicked(this, &SMaterialLab::RemoveMaskFromLayer, LayerIndex, MaskIndex)
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildMaskReplacementGallery(const int32 LayerIndex, const int32 MaskIndex)
{
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(3.0f, 3.0f));
	for (const FMaterialLabMaskEntry& Mask : FMaterialLabRegistry::GetMasks())
	{
		TSharedRef<SWidget> ThumbnailWidget = SNew(SBorder)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ThumbnailBackground")));
		if (Mask.ThumbnailAsset.IsValid())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				Mask.ThumbnailAsset,
				50,
				50,
				ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			ThumbnailWidget = Thumbnail->MakeThumbnailWidget(MaterialLabUI::CleanThumbnailConfig());
		}

		Grid->AddSlot()
		[
			SNew(SBox)
			.WidthOverride(MaterialLabUI::MaskTileSize)
			.HeightOverride(MaterialLabUI::MaskTileSize)
			[
				SNew(SButton)
				.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.ThumbnailCard")))
				.ContentPadding(3.0f)
				.ToolTipText(Mask.DisplayName)
				.OnClicked_Lambda([this, LayerIndex, MaskIndex, Path = Mask.AssetPath]()
				{
					return ReplaceMaskInLayer(LayerIndex, MaskIndex, Path);
				})
				[SNew(SBox).WidthOverride(50.0f).HeightOverride(50.0f)[ThumbnailWidget]]
			]
		];
	}
	return SNew(SScrollBox) + SScrollBox::Slot()[Grid];
}

TSharedRef<SWidget> SMaterialLab::BuildNormalSourceMenu(const int32 LayerIndex)
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	if (WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].SourceSurface.IsNull())
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.Text(LOCTEXT("UseSurfaceNormal", "Use Surface Normal"))
			.OnClicked_Lambda([this, LayerIndex]()
			{
				if (WorkingLayers.IsValidIndex(LayerIndex))
				{
					WorkingLayers[LayerIndex].NormalSourceType = EMaterialLabNormalSourceType::Surface;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
				return FReply::Handled();
			})
		];
	}
	for (const FMaterialLabNormalEntry& Normal : FMaterialLabRegistry::GetNormals())
	{
		Menu->AddSlot().AutoHeight()
		[SNew(SButton).Text(Normal.DisplayName).OnClicked_Lambda([this, LayerIndex, Path = Normal.AssetPath]() { return AssignNormalTexture(LayerIndex, Path); })];
	}
	return SNew(SBox).WidthOverride(240.0f).MaxDesiredHeight(420.0f)
		[SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[SNew(SScrollBox) + SScrollBox::Slot()[Menu]]];
}

TSharedRef<SWidget> SMaterialLab::BuildMaskCard(
	const int32 LayerIndex,
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset,
	const bool bCompact)
{
	const float ThumbnailSize = bCompact ? 56.0f : 42.0f;
	TSharedRef<SWidget> ThumbnailWidget = SNew(SBorder)
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ThumbnailBackground")));
	if (ThumbnailAsset.IsValid())
	{
		UObject* ThumbnailObject = ThumbnailAsset.GetAsset();
		UTexture2D* ThumbnailTexture = Cast<UTexture2D>(ThumbnailObject);
		if (const UMaterialLabMask* MaskAsset = Cast<UMaterialLabMask>(ThumbnailObject))
		{
			ThumbnailTexture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
		}
		if (ThumbnailTexture)
		{
			ThumbnailWidget = SNew(SMaterialLabTextureTile)
				.Texture(ThumbnailTexture)
				.ImageSize(FVector2D(ThumbnailSize, ThumbnailSize));
		}
	}

	if (bCompact)
	{
		return SNew(SBox)
			.WidthOverride(MaterialLabUI::MaskTileSize)
			.HeightOverride(MaterialLabUI::MaskTileSize)
			[
				SNew(SMaterialLabMaskCard)
				.LayerIndex(LayerIndex)
				.DisplayName(Name)
				.MaskPath(AssetPath)
				.ThumbnailAsset(ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.OnSelected(this, &SMaterialLab::AssignMaskToLayer)
				[
					SNew(SButton)
					.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.ThumbnailCard")))
					.ContentPadding(3.0f)
					.ToolTipText(Name)
					.IsEnabled_Lambda([this, LayerIndex]()
					{
						return WorkingLayers.IsValidIndex(LayerIndex) && LayerIndex > 0 && WorkingLayers[LayerIndex].bEnabled;
					})
					[SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]]
				]
			];
	}

	return SNew(SButton)
		.ContentPadding(5.0f)
		.OnClicked_Lambda([this, LayerIndex, AssetPath]() { return AssignMaskToLayer(LayerIndex, AssetPath); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Name)
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildSurfaceCard(
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset)
{
	TSharedRef<SWidget> ThumbnailWidget = SNew(SBorder)
		.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ThumbnailBackground")));
	if (ThumbnailAsset.IsValid())
	{
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
			ThumbnailAsset,
			MaterialLabUI::MaterialThumbnailSize,
			MaterialLabUI::MaterialThumbnailSize,
			ThumbnailPool);
		SurfaceThumbnails.Add(Thumbnail);
		ThumbnailWidget = Thumbnail->MakeThumbnailWidget(MaterialLabUI::CleanThumbnailConfig());
	}

	return SNew(SMaterialLabSurfaceCard)
		.DisplayName(Name)
		.SurfacePath(AssetPath)
		.ThumbnailAsset(ThumbnailAsset)
		.ThumbnailPool(ThumbnailPool)
		.OnSelected(this, &SMaterialLab::SelectSurface)
		.HoverContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Bottom)
			[
				SNew(SBorder)
				.Padding(FMargin(3.0f, 2.0f))
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.015f, 0.015f, 0.015f, 0.9f))
				[
					SNew(STextBlock)
					.Text(Name)
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]
		]
		[
			SNew(SBox)
			.WidthOverride(MaterialLabUI::MaterialTileSize)
			.HeightOverride(MaterialLabUI::MaterialTileSize)
			[
				SNew(SButton)
				.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.ThumbnailCard")))
				.ContentPadding(3.0f)
				.ToolTipText(LOCTEXT("DragMaterialToLayers", "Drag to Layers"))
				.OnClicked_Lambda([this, Name, AssetPath]() { return SelectSurface(Name, AssetPath); })
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.WidthOverride(MaterialLabUI::MaterialThumbnailSize)
					.HeightOverride(MaterialLabUI::MaterialThumbnailSize)
					[
						ThumbnailWidget
					]
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildPreviewPanel()
{
	TSharedPtr<SMaterialLabPreviewViewport> PreviewViewport;
	TSharedRef<SHorizontalBox> MeshControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> LightingControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> QualityControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> ComparisonControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> FovControls = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewFovLabel", "FOV"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(160.0f)
			[
				SNew(SSlider)
				.Style(&FMaterialLabStyle::Get().GetWidgetStyle<FSliderStyle>(TEXT("MaterialLab.ScrubSlider")))
				.Value_Lambda([this]() { return (PreviewFov - 20.0f) / 70.0f; })
				.OnValueChanged_Lambda([this](const float Value) { SetPreviewFov(20.0f + Value * 70.0f); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 3.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SNumericEntryBox<float>)
			.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
			.AllowSpin(true)
			.MinValue(20.0f)
			.MaxValue(90.0f)
			.MinSliderValue(20.0f)
			.MaxSliderValue(90.0f)
			.Delta(0.5f)
			.MinDesiredValueWidth(48.0f)
			.Value_Lambda([this]() -> TOptional<float> { return PreviewFov; })
			.OnValueChanged_Lambda([this](const float Value) { SetPreviewFov(Value); })
		];
	const FCheckBoxStyle* OverlayToggle = &FMaterialLabStyle::Get().GetWidgetStyle<FCheckBoxStyle>(
		TEXT("MaterialLab.ViewportOverlayToggle"));
	const auto AddComparisonButton = [this, &ComparisonControls, OverlayToggle](
		const bool bBefore,
		const FText& Label,
		const FText& ToolTip)
	{
		ComparisonControls->AddSlot().AutoWidth()
		[
			SNew(SCheckBox)
			.Style(OverlayToggle)
			.ToolTipText(ToolTip)
			.IsEnabled_Lambda([this]() { return bHasWorkingMaterial && !WorkingLayers.IsEmpty(); })
			.IsChecked_Lambda([this, bBefore]()
			{
				return SoloLayerIndex == INDEX_NONE && bShowCompositionBefore == bBefore
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, bBefore](const ECheckBoxState State)
			{
				if (State != ECheckBoxState::Checked)
				{
					return;
				}
				bShowCompositionBefore = bBefore;
				if (SoloLayerIndex != INDEX_NONE)
				{
					SoloLayerIndex = INDEX_NONE;
					RebuildLayerList();
				}
				RefreshLayeredPreview(false);
			})
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
			]
		];
	};
	AddComparisonButton(
		true,
		LOCTEXT("PreviewCompositionBefore", "Before"),
		LOCTEXT("PreviewCompositionBeforeHint", "Preview the base layer before added layers are composed"));
	AddComparisonButton(
		false,
		LOCTEXT("PreviewCompositionAfter", "After"),
		LOCTEXT("PreviewCompositionAfterHint", "Preview the complete layer stack"));
	ComparisonControls->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SCheckBox)
		.Style(OverlayToggle)
		.ToolTipText(LOCTEXT(
			"PreviewBypassSelectedChildHint",
			"Temporarily disable the selected Mask or Effect in the preview only"))
		.IsEnabled_Lambda([this]() { return GetSelectedChildIndex() != INDEX_NONE; })
		.IsChecked_Lambda([this]()
		{
			return bBypassSelectedChild && GetSelectedChildIndex() != INDEX_NONE
				? ECheckBoxState::Checked
				: ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
		{
			bBypassSelectedChild = State == ECheckBoxState::Checked
				&& GetSelectedChildIndex() != INDEX_NONE;
			RefreshLayeredPreview(false);
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewBypassSelectedChild", "Bypass Child"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
	];
	ComparisonControls->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SCheckBox)
		.Style(OverlayToggle)
		.ToolTipText(LOCTEXT(
			"PreviewDisplacementHint",
			"Preview the composited Height through the protected master's authored ML_UseHeight displacement path"))
		.IsEnabled_Lambda([this]()
		{
			return bHasWorkingMaterial
				? !WorkingLayers.IsEmpty()
				: SelectedPreviewMaterial.IsValid();
		})
		.IsChecked_Lambda([this]()
		{
			return bPreviewDisplacementEnabled
				? ECheckBoxState::Checked
				: ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
		{
			SetPreviewDisplacementEnabled(State == ECheckBoxState::Checked);
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewDisplacement", "Displacement"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
	];
	ComparisonControls->AddSlot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]()
		{
			return bPreviewDisplacementEnabled
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewDisplacementAmountLabel", "Amount"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(100.0f)
			[
				SNew(SSlider)
				.Style(&FMaterialLabStyle::Get().GetWidgetStyle<FSliderStyle>(TEXT("MaterialLab.ScrubSlider")))
				.ToolTipText(LOCTEXT("PreviewDisplacementAmountHint", "Scale the centered composited Height used by the authored displacement path"))
				.Value_Lambda([this]() { return PreviewDisplacementAmount / 4.0f; })
				.OnValueChanged_Lambda([this](const float Value)
				{
					SetPreviewDisplacementAmount(Value * 4.0f);
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SNumericEntryBox<float>)
			.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
			.AllowSpin(true)
			.MinValue(0.0f)
			.MaxValue(4.0f)
			.MinSliderValue(0.0f)
			.MaxSliderValue(4.0f)
			.Delta(0.05f)
			.MinDesiredValueWidth(42.0f)
			.Value_Lambda([this]() -> TOptional<float> { return PreviewDisplacementAmount; })
			.OnValueChanged_Lambda([this](const float Value)
			{
				SetPreviewDisplacementAmount(Value);
			})
		]
	];

	const auto AddMeshButton = [this, &MeshControls, OverlayToggle](
		const EMaterialLabPreviewMesh MeshType,
		const FText& ToolTip,
		const FName IconName)
	{
		MeshControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, MeshType]()
				{
					return PreviewMesh == MeshType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, MeshType](ECheckBoxState) { SetPreviewMesh(MeshType); })
				[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddMeshButton(EMaterialLabPreviewMesh::Sphere, LOCTEXT("SpherePreview", "Sphere"), TEXT("MaterialLab.Icon.Sphere"));
	AddMeshButton(EMaterialLabPreviewMesh::Plane, LOCTEXT("PlanePreview", "Plane"), TEXT("MaterialLab.Icon.Plane"));
	AddMeshButton(EMaterialLabPreviewMesh::Cube, LOCTEXT("CubePreview", "Cube"), TEXT("MaterialLab.Icon.Cube"));

	const auto AddQualityButton = [this, &QualityControls, OverlayToggle](
		const EMaterialLabPreviewQuality Quality,
		const FText& ToolTip,
		const FName IconName)
	{
		QualityControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, Quality]()
				{
					return PreviewQuality == Quality ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Quality](ECheckBoxState) { SetPreviewQuality(Quality); })
				[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddQualityButton(
		EMaterialLabPreviewQuality::Low,
		LOCTEXT("LowPreviewQuality", "Low · Direct light only · No AO, SSR, or Lumen"),
		TEXT("MaterialLab.Icon.QualityLow"));
	AddQualityButton(
		EMaterialLabPreviewQuality::Medium,
		LOCTEXT("MediumPreviewQuality", "Medium · Stable shadows, AO, and SSR · No Lumen"),
		TEXT("MaterialLab.Icon.QualityMedium"));
	AddQualityButton(
		EMaterialLabPreviewQuality::High,
		LOCTEXT("HighPreviewQuality", "High · Lumen · Uses project ray tracing when supported"),
		TEXT("MaterialLab.Icon.QualityHigh"));

	const auto AddPresetButton = [this, &LightingControls, OverlayToggle](
		const EMaterialLabStudioLighting Preset,
		const FText& ToolTip,
		const FName IconName)
	{
		LightingControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, Preset]()
				{
					return SelectedHdriPath.IsNull() && StudioLighting == Preset
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Preset](ECheckBoxState) { SetStudioLighting(Preset); })
				[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddPresetButton(EMaterialLabStudioLighting::Neutral, LOCTEXT("NeutralStudioButton", "Neutral studio"), TEXT("MaterialLab.Icon.LightNeutral"));
	AddPresetButton(EMaterialLabStudioLighting::Soft, LOCTEXT("SoftStudioButton", "Soft studio"), TEXT("MaterialLab.Icon.LightSoft"));
	AddPresetButton(EMaterialLabStudioLighting::Dramatic, LOCTEXT("DramaticStudioButton", "Dramatic studio"), TEXT("MaterialLab.Icon.LightDramatic"));
	AddPresetButton(EMaterialLabStudioLighting::Rim, LOCTEXT("RimStudioButton", "Rim lighting"), TEXT("MaterialLab.Icon.LightRim"));

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter HdriFilter;
	HdriFilter.ClassPaths.Add(UTextureCube::StaticClass()->GetClassPathName());
	HdriFilter.PackagePaths.Add(FName(TEXT("/MaterialLab/Lighting")));
	HdriFilter.bRecursiveClasses = true;
	HdriFilter.bRecursivePaths = true;
	TArray<FAssetData> HdriAssets;
	AssetRegistryModule.Get().GetAssets(HdriFilter, HdriAssets);
	HdriAssets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.ToString() < B.AssetName.ToString();
	});

	for (const FAssetData& HdriAsset : HdriAssets)
	{
		const FSoftObjectPath HdriPath = HdriAsset.GetSoftObjectPath();
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(HdriAsset, 20, 20, ThumbnailPool);
		HdriThumbnails.Add(Thumbnail);
		LightingControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(FText::FromName(HdriAsset.AssetName))
				.IsChecked_Lambda([this, HdriPath]()
				{
					return SelectedHdriPath == HdriPath ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, HdriPath](ECheckBoxState) { SetHdriLighting(HdriPath); })
				[
					SNew(SBox).WidthOverride(20.0f).HeightOverride(20.0f)
					[Thumbnail->MakeThumbnailWidget()]
				]
			]
		];
	}

	TSharedRef<SWidget> PreviewPanel = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(PreviewViewport, SMaterialLabPreviewViewport)
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ViewportOverlayGroup")))
			[QualityControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ViewportOverlayGroup")))
			[FovControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ViewportOverlayGroup")))
			[MeshControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ViewportOverlayGroup")))
			[ComparisonControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.ViewportOverlayGroup")))
			[LightingControls]
		];

	PreviewViewports.Add(PreviewViewport);
	PreviewViewport->SetPreviewQuality(PreviewQuality);
	PreviewViewport->SetCameraFov(PreviewFov);
	PreviewViewport->SetStudioLighting(StudioLighting);
	return PreviewPanel;
}

TSharedRef<SWidget> SMaterialLab::BuildStudioLightingMenu()
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const TPair<EMaterialLabStudioLighting, FText> Presets[] = {
		{EMaterialLabStudioLighting::Neutral, LOCTEXT("NeutralStudioMenu", "Neutral Studio")},
		{EMaterialLabStudioLighting::Soft, LOCTEXT("SoftStudioMenu", "Soft Studio")},
		{EMaterialLabStudioLighting::Dramatic, LOCTEXT("DramaticStudioMenu", "Dramatic")},
		{EMaterialLabStudioLighting::Rim, LOCTEXT("RimStudioMenu", "Rim Light")}};
	for (const TPair<EMaterialLabStudioLighting, FText>& Preset : Presets)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
			.Text(Preset.Value)
			.OnClicked_Lambda([this, Lighting = Preset.Key]() { return SetStudioLighting(Lighting); })
		];
	}
	return SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[Menu];
}

TSharedRef<SWidget> SMaterialLab::BuildLayerMaskControls()
{
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) && SelectedLayerIndex > 0
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SMaterialLabInspectorGroup)
			.Title(LOCTEXT("LayerMaskHeading", "MASK BLENDING"))
			.InitiallyExpanded(false)
			.HeaderAction(
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("SelectedMaskEnabled", "Enable selected mask"))
				.IsEnabled_Lambda([this]() { return GetSelectedLayerMask() != nullptr; })
				.IsChecked_Lambda([this]() { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask && Mask->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->bEnabled = State == ECheckBoxState::Checked; RefreshLayeredPreview(); RebuildLayerList(); } }))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 2.0f, 2.0f, 5.0f)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text(LOCTEXT("SelectMaskForInspector", "Select a mask child in the layer stack to edit its settings."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask();
					if (!Mask) return LOCTEXT("NoSelectedMask", "No mask selected");
					const FSoftObjectPath Path = !Mask->Mask.IsNull() ? Mask->Mask.ToSoftObjectPath() : Mask->MaskTexture.ToSoftObjectPath();
					return FText::FromString(Path.GetAssetName());
				})
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("SelectedMaskBlendMode", "Blend Mode"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SComboButton)
					.ButtonContent()[SNew(STextBlock).Text_Lambda([this]() { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask ? MaterialLabUI::MaskBlendModeText(Mask->BlendMode) : FText::GetEmpty(); })]
					.OnGetMenuContent_Lambda([this]() { return BuildMaskBlendModeMenu(SelectedLayerIndex, SelectedMaskIndex); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("SelectedMaskWeight", "Weight"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeResettableNumeric(
						SNew(SNumericEntryBox<float>)
						.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
						.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
						.Value_Lambda([this]() -> TOptional<float> { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask ? Mask->Weight : 1.0f; })
						.OnValueChanged_Lambda([this](const float Value) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->Weight = Value; RefreshLayeredPreview(); } }),
						FSimpleDelegate::CreateLambda([this]() { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); Mask && !FMath::IsNearlyEqual(Mask->Weight, 1.0f)) { Mask->Weight = 1.0f; RefreshLayeredPreview(); } }))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("MaskTilingLabel", "Mask Tiling"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeResettableNumeric(
						SNew(SNumericEntryBox<int32>)
						.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
						.AllowSpin(true).MinValue(1).MaxValue(16).MinSliderValue(1).MaxSliderValue(16).Delta(1).MinDesiredValueWidth(96.0f)
						.Value_Lambda([this]() -> TOptional<int32> { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask ? Mask->Tiling : 1; })
						.OnValueChanged_Lambda([this](const int32 Value) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->Tiling = Value; RefreshLayeredPreview(); } }),
						FSimpleDelegate::CreateLambda([this]() { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); Mask && Mask->Tiling != 1) { Mask->Tiling = 1; RefreshLayeredPreview(); } }))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("MaskBalanceLabel", "Mask Balance"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeResettableNumeric(
						SNew(SNumericEntryBox<float>)
						.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
						.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
						.Value_Lambda([this]() -> TOptional<float> { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask ? Mask->Balance : 0.5f; })
						.OnValueChanged_Lambda([this](const float Value) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->Balance = Value; RefreshLayeredPreview(); } }),
						FSimpleDelegate::CreateLambda([this]() { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); Mask && !FMath::IsNearlyEqual(Mask->Balance, 0.5f)) { Mask->Balance = 0.5f; RefreshLayeredPreview(); } }))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("MaskContrastLabel", "Mask Contrast"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeResettableNumeric(
						SNew(SNumericEntryBox<float>)
						.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
						.AllowSpin(true).MinValue(0.0f).MaxValue(4.0f).MinSliderValue(0.0f).MaxSliderValue(4.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
						.Value_Lambda([this]() -> TOptional<float> { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask ? Mask->Contrast : 1.0f; })
						.OnValueChanged_Lambda([this](const float Value) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->Contrast = Value; RefreshLayeredPreview(); } }),
						FSimpleDelegate::CreateLambda([this]() { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); Mask && !FMath::IsNearlyEqual(Mask->Contrast, 1.0f)) { Mask->Contrast = 1.0f; RefreshLayeredPreview(); } }))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("MaskInvertLabel", "Invert Mask"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { const FMaterialLabMaskLayer* Mask = GetSelectedLayerMask(); return Mask && Mask->bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { if (FMaterialLabMaskLayer* Mask = GetSelectedLayerMask()) { Mask->bInvert = State == ECheckBoxState::Checked; RefreshLayeredPreview(); } })
				]
			]

		]
	]
];
}

TSharedRef<SWidget> SMaterialLab::BuildSurfaceMaskInfluenceControls()
{
	const auto InfluenceRow = [this](
		const FText& Label,
		const FText& ToolTip,
		float FMaterialLabLayer::* Member) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).ToolTipText(ToolTip)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeResettableNumeric(
					SNew(SNumericEntryBox<float>)
					.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
					.AllowSpin(true)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.Delta(0.01f)
					.MinDesiredValueWidth(96.0f)
					.Value_Lambda([this, Member]() -> TOptional<float>
					{
						return WorkingLayers.IsValidIndex(SelectedLayerIndex)
							? WorkingLayers[SelectedLayerIndex].*Member
							: 0.0f;
					})
					.OnValueChanged_Lambda([this, Member](const float Value)
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							WorkingLayers[SelectedLayerIndex].*Member = Value;
							RefreshLayeredPreview();
						}
					}),
					FSimpleDelegate::CreateLambda([this, Member]()
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex)
							&& !FMath::IsNearlyZero(WorkingLayers[SelectedLayerIndex].*Member))
						{
							WorkingLayers[SelectedLayerIndex].*Member = 0.0f;
							RefreshLayeredPreview();
						}
					}))
			];
	};
	const auto InvertRow = [this](
		const FText& Label,
		const FText& ToolTip,
		bool FMaterialLabLayer::* Member) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).ToolTipText(ToolTip)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, Member]()
				{
					return WorkingLayers.IsValidIndex(SelectedLayerIndex)
						&& WorkingLayers[SelectedLayerIndex].*Member
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Member](const ECheckBoxState State)
				{
					if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
					{
						WorkingLayers[SelectedLayerIndex].*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})
			];
	};

	return SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) && SelectedLayerIndex > 0
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 3.0f)
		[
			InfluenceRow(
				LOCTEXT("UnderlyingHeightInfluenceLabel", "Height Influence"),
				LOCTEXT("UnderlyingHeightInfluenceHint", "Mask this layer using the accumulated height underneath it"),
				&FMaterialLabLayer::HeightFeatureInfluence)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			InvertRow(
				LOCTEXT("InvertUnderlyingHeightLabel", "Invert Height"),
				LOCTEXT("InvertUnderlyingHeightHint", "Favor lower underlying height instead of higher height"),
				&FMaterialLabLayer::bInvertHeightFeature)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			InfluenceRow(
				LOCTEXT("UnderlyingAOInfluenceLabel", "AO Influence"),
				LOCTEXT("UnderlyingAOInfluenceHint", "Mask this layer using the accumulated AO underneath it"),
				&FMaterialLabLayer::AOFeatureInfluence)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			InvertRow(
				LOCTEXT("InvertUnderlyingAOLabel", "Invert AO"),
				LOCTEXT("InvertUnderlyingAOHint", "Favor occluded areas instead of exposed areas"),
				&FMaterialLabLayer::bInvertAOFeature)
		];
}

TSharedRef<SWidget> SMaterialLab::BuildChannelInfluenceControls()
{
	const auto NumericRow = [this](
		const FText& Label,
		float FMaterialLabLayer::* Member) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeResettableNumeric(
					SNew(SNumericEntryBox<float>)
					.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
					.AllowSpin(true)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.Delta(0.01f)
					.MinDesiredValueWidth(96.0f)
					.Value_Lambda([this, Member]() -> TOptional<float>
					{
						return WorkingLayers.IsValidIndex(SelectedLayerIndex)
							? WorkingLayers[SelectedLayerIndex].*Member
							: 1.0f;
					})
					.OnValueChanged_Lambda([this, Member](const float Value)
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							WorkingLayers[SelectedLayerIndex].*Member = Value;
							RefreshLayeredPreview();
						}
					}),
					FSimpleDelegate::CreateLambda([this, Member]()
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex)
							&& !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].*Member, 1.0f))
						{
							WorkingLayers[SelectedLayerIndex].*Member = 1.0f;
							RefreshLayeredPreview();
						}
					}))
			];
	};

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
			{
				return EVisibility::Collapsed;
			}
			const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
			return Layer.Type != EMaterialLabLayerType::Effect
				&& Layer.ChannelMode == EMaterialLabLayerChannelMode::CompleteSurface
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SNew(SMaterialLabInspectorGroup)
			.Title(LOCTEXT("ChannelInfluenceHeading", "CHANNEL INFLUENCE"))
			.InitiallyExpanded(false)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("BaseColorInfluenceLabel", "Base Color"), &FMaterialLabLayer::BaseColorInfluence)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("RoughnessInfluenceLabel", "Roughness"), &FMaterialLabLayer::RoughnessInfluence)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("AOInfluenceLabel", "Ambient Occlusion"), &FMaterialLabLayer::AOInfluence)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("MetallicInfluenceLabel", "Metallic"), &FMaterialLabLayer::MetallicInfluence)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("F0InfluenceLabel", "IOR / F0"), &FMaterialLabLayer::F0Influence)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[NumericRow(LOCTEXT("LayerNormalInfluenceLabel", "Normal"), &FMaterialLabLayer::NormalInfluence)]
				+ SVerticalBox::Slot().AutoHeight()
				[NumericRow(LOCTEXT("LayerHeightInfluenceLabel", "Height"), &FMaterialLabLayer::HeightInfluence)]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildColorAdjustmentControls()
{
	const auto NumericRow = [this](
		const FText& Label,
		float FMaterialLabLayer::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeResettableNumeric(
					SNew(SNumericEntryBox<float>)
					.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
					.AllowSpin(true)
					.MinValue(MinValue)
					.MaxValue(MaxValue)
					.MinSliderValue(MinValue)
					.MaxSliderValue(MaxValue)
					.Delta(Delta)
					.MinDesiredValueWidth(96.0f)
					.Value_Lambda([this, Member]() -> TOptional<float>
					{
						return WorkingLayers.IsValidIndex(SelectedLayerIndex)
							? WorkingLayers[SelectedLayerIndex].*Member
							: 0.0f;
					})
					.OnValueChanged_Lambda([this, Member](const float NewValue)
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							WorkingLayers[SelectedLayerIndex].*Member = NewValue;
							RefreshLayeredPreview();
						}
					}),
					FSimpleDelegate::CreateLambda([this, Member, DefaultValue]()
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex)
							&& !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].*Member, DefaultValue))
						{
							WorkingLayers[SelectedLayerIndex].*Member = DefaultValue;
							RefreshLayeredPreview();
						}
					}))
			];
	};

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
			{
				return EVisibility::Collapsed;
			}
			const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
			return Layer.Type != EMaterialLabLayerType::Effect
				&& Layer.ChannelMode == EMaterialLabLayerChannelMode::CompleteSurface
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SNew(SMaterialLabInspectorGroup)
			.Title(LOCTEXT("ColorAdjustmentsHeading", "COLOR ADJUSTMENTS"))
			.InitiallyExpanded(false)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("HueShiftLabel", "Hue Shift"), &FMaterialLabLayer::HueShift, -180.0f, 180.0f, 0.1f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("SaturationLabel", "Saturation"), &FMaterialLabLayer::Saturation, 0.0f, 2.0f, 0.01f, 1.0f)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					NumericRow(LOCTEXT("ValueLabel", "Value"), &FMaterialLabLayer::Value, 0.0f, 2.0f, 0.01f, 1.0f)
				]
			]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildHeightBlendControls()
{
	const auto NumericRow = [this](
		const FText& Label,
		float FMaterialLabLayer::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeResettableNumeric(
					SNew(SNumericEntryBox<float>)
					.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
					.AllowSpin(true)
					.MinValue(MinValue)
					.MaxValue(MaxValue)
					.MinSliderValue(MinValue)
					.MaxSliderValue(MaxValue)
					.Delta(Delta)
					.MinDesiredValueWidth(96.0f)
					.Value_Lambda([this, Member]() -> TOptional<float>
					{
						return WorkingLayers.IsValidIndex(SelectedLayerIndex)
							? WorkingLayers[SelectedLayerIndex].*Member
							: 0.0f;
					})
					.OnValueChanged_Lambda([this, Member](const float Value)
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							WorkingLayers[SelectedLayerIndex].*Member = Value;
							RefreshLayeredPreview();
						}
					}),
					FSimpleDelegate::CreateLambda([this, Member, DefaultValue]()
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex)
							&& !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].*Member, DefaultValue))
						{
							WorkingLayers[SelectedLayerIndex].*Member = DefaultValue;
							RefreshLayeredPreview();
						}
					}))
			];
	};

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
			{
				return EVisibility::Collapsed;
			}
			const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
			return Layer.Type != EMaterialLabLayerType::Effect
				&& Layer.ChannelMode == EMaterialLabLayerChannelMode::CompleteSurface
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
		[
			SNew(SMaterialLabInspectorGroup)
			.Title(LOCTEXT("HeightMaskBlendingHeading", "HEIGHT MASK BLENDING"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("EnableHeightBlend", "Enable Height Blend"))
				.IsChecked_Lambda([this]()
				{
					return WorkingLayers.IsValidIndex(SelectedLayerIndex)
						&& WorkingLayers[SelectedLayerIndex].bHeightBlendEnabled
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
				{
					if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
					{
						WorkingLayers[SelectedLayerIndex].bHeightBlendEnabled = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				}))
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]()
				{
					return WorkingLayers.IsValidIndex(SelectedLayerIndex)
						&& WorkingLayers[SelectedLayerIndex].bHeightBlendEnabled
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
					{
						return FText::GetEmpty();
					}
					const UMaterialLabSurface* Surface =
						WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
					return MaterialLabUI::HeightBlendSourceText(Surface);
				})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SComboButton)
				.Visibility(EVisibility::Collapsed)
				.ToolTipText(LOCTEXT("HeightSourceTooltip", "Compatibility-only height source selector"))
				.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
				{
					TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
					const auto AddOption = [this, &Menu](
						const EMaterialLabHeightSource Source,
						const FText& Label,
						const FText& ToolTip)
					{
						Menu->AddSlot().AutoHeight()
						[
							SNew(SButton)
							.Text(Label)
							.ToolTipText(ToolTip)
							.OnClicked_Lambda([this, Source]()
							{
								if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
								{
									WorkingLayers[SelectedLayerIndex].HeightSource = Source;
									RefreshLayeredPreview();
								}
								FSlateApplication::Get().DismissAllMenus();
								return FReply::Handled();
							})
						];
					};
					AddOption(
						EMaterialLabHeightSource::LayerHeight,
						LOCTEXT("LayerHeightSourceOption", "Layer Height (Recommended)"),
						LOCTEXT("LayerHeightSourceHint", "Use RAMH alpha when available; otherwise use Constant Height"));
					AddOption(
						EMaterialLabHeightSource::RAMHAlpha,
						LOCTEXT("RAMHHeightSourceOption", "RAMH Height"),
						LOCTEXT("RAMHHeightSourceHint", "Use authored RAMH alpha; falls back to Constant Height when unavailable"));
					AddOption(
						EMaterialLabHeightSource::Constant,
						LOCTEXT("ConstantHeightSourceOption", "Constant Height"),
						LOCTEXT("ConstantHeightSourceHint", "Use one uniform height value for this layer"));
					AddOption(
						EMaterialLabHeightSource::CombinedMask,
						LOCTEXT("MaskHeightSourceOption", "Mask as Height (Optional)"),
						LOCTEXT("MaskHeightSourceHint", "Explicitly use the ordered combined mask as height"));
					AddOption(
						EMaterialLabHeightSource::Automatic,
						LOCTEXT("LegacyAutomaticHeightSourceOption", "Automatic (Legacy)"),
						LOCTEXT("LegacyAutomaticHeightSourceHint", "Compatibility mode: RAMH, then mask, then Constant Height"));
					return SNew(SBox).WidthOverride(260.0f)[Menu];
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							return LOCTEXT("InvalidLayerHeightSource", "Current Height · Layer Height");
						}
						switch (WorkingLayers[SelectedLayerIndex].HeightSource)
						{
						case EMaterialLabHeightSource::RAMHAlpha: return LOCTEXT("RAMHHeightSource", "Current Height · RAMH");
						case EMaterialLabHeightSource::CombinedMask: return LOCTEXT("MaskHeightSource", "Current Height · Mask");
						case EMaterialLabHeightSource::Constant: return LOCTEXT("ConstantHeightSource", "Current Height · Constant");
						case EMaterialLabHeightSource::Automatic: return LOCTEXT("AutomaticHeightSource", "Current Height · Automatic (Legacy)");
						case EMaterialLabHeightSource::LayerHeight:
						default: return LOCTEXT("LayerHeightSource", "Current Height · Layer Height");
						}
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SComboButton)
				.Visibility(EVisibility::Collapsed)
				.ToolTipText(LOCTEXT("HeightReferenceTooltip", "Compatibility-only height reference selector"))
				.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
				{
					TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
					Menu->AddSlot().AutoHeight()
					[
						SNew(SButton)
						.Text(LOCTEXT("PreviousCompositeHeightReference", "Previous Composite"))
						.OnClicked_Lambda([this]()
						{
							if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
							{
								WorkingLayers[SelectedLayerIndex].HeightReferenceLayerIndex = INDEX_NONE;
								RefreshLayeredPreview();
							}
							FSlateApplication::Get().DismissAllMenus();
							return FReply::Handled();
						})
					];
					for (int32 LayerIndex = 0; LayerIndex < SelectedLayerIndex; ++LayerIndex)
					{
						const FText LayerName = WorkingLayers[LayerIndex].DisplayName.IsEmpty()
							? FText::Format(LOCTEXT("HeightReferenceLayerFallback", "Layer {0}"), FText::AsNumber(LayerIndex + 1))
							: WorkingLayers[LayerIndex].DisplayName;
						Menu->AddSlot().AutoHeight()
						[
							SNew(SButton)
							.Text(LayerName)
							.OnClicked_Lambda([this, LayerIndex]()
							{
								if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && LayerIndex < SelectedLayerIndex)
								{
									WorkingLayers[SelectedLayerIndex].HeightReferenceLayerIndex = LayerIndex;
									RefreshLayeredPreview();
								}
								FSlateApplication::Get().DismissAllMenus();
								return FReply::Handled();
							})
						];
					}
					return SNew(SBox).WidthOverride(220.0f)[Menu];
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							return LOCTEXT("InvalidHeightReference", "Compare Against · Previous Composite");
						}
						const int32 ReferenceIndex = WorkingLayers[SelectedLayerIndex].HeightReferenceLayerIndex;
						if (!WorkingLayers.IsValidIndex(ReferenceIndex) || ReferenceIndex >= SelectedLayerIndex)
						{
							return LOCTEXT("DefaultHeightReference", "Compare Against · Previous Composite");
						}
						return FText::Format(
							LOCTEXT("SelectedHeightReference", "Compare Against · {0}"),
							WorkingLayers[ReferenceIndex].DisplayName);
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				NumericRow(LOCTEXT("HeightMaskStrength", "Mask Strength"), &FMaterialLabLayer::HeightBlendAmount, 0.0f, 4.0f, 0.01f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("HeightBlendThreshold", "Threshold"), &FMaterialLabLayer::HeightThreshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("HeightSoftness", "Softness"), &FMaterialLabLayer::HeightRange, 0.0f, 1.0f, 0.005f, 0.1f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("BaseHeightBias", "Base Height Bias"), &FMaterialLabLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					NumericRow(LOCTEXT("BlendHeightBias", "Blend Height Bias"), &FMaterialLabLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					NumericRow(LOCTEXT("HeightThreshold", "Legacy Height Threshold"), &FMaterialLabLayer::HeightThreshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility(EVisibility::Collapsed)
				[NumericRow(LOCTEXT("HeightRange", "Blend Softness"), &FMaterialLabLayer::HeightRange, 0.0001f, 1.0f, 0.005f, 0.1f)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContrast", "Legacy Height Contrast"), &FMaterialLabLayer::HeightContrast, 0.01f, 8.0f, 0.05f, 1.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightOffset", "Legacy Height Offset"), &FMaterialLabLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBias", "Legacy Comparison Bias"), &FMaterialLabLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]()
				{
					if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
					{
						return EVisibility::Collapsed;
					}
					const UMaterialLabSurface* Surface =
						WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
					return Surface && Surface->bHasBlendHeight
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					NumericRow(LOCTEXT("ConstantHeight", "Layer Height (No RAMH)"), &FMaterialLabLayer::ConstantHeight, 0.0f, 1.0f, 0.01f, 0.5f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					NumericRow(LOCTEXT("MaskHeightInfluence", "Mask Modulation (Compatibility)"), &FMaterialLabLayer::MaskHeightInfluence, 0.0f, 1.0f, 0.01f, 0.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)
			[
				SNew(SMaterialLabInspectorGroup)
				.Title(LOCTEXT("HeightContactBorders", "CONTACT BORDERS"))
				.InitiallyExpanded(true)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[SNew(STextBlock).Text(LOCTEXT("HeightContactAOGroup", "CONTACT AO")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContactAOAmount", "Amount"), &FMaterialLabLayer::HeightContactAOAmount, 0.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContactAOWidth", "Width"), &FMaterialLabLayer::HeightContactAOWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)
					[SNew(STextBlock).Text(LOCTEXT("HeightBorderNormalGroup", "BORDER NORMAL")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightBorderLift", "Lift"), &FMaterialLabLayer::HeightBorderLift, -1.0f, 1.0f, 0.005f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightBorderWidth", "Width"), &FMaterialLabLayer::HeightBorderWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBorderNormalStrength", "Intensity"), &FMaterialLabLayer::HeightBorderNormalStrength, 0.0f, 8.0f, 0.001f, 1.0f)]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					return SelectedLayerIndex > 0
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.ToolTipText(LOCTEXT(
					"InvertBaseHeightHint",
					"Invert the accumulated height from layers below before comparing it with this layer."))
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("InvertBaseHeight", "Invert Base Height"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return WorkingLayers.IsValidIndex(SelectedLayerIndex)
							&& WorkingLayers[SelectedLayerIndex].bInvertHeight
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
					{
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							WorkingLayers[SelectedLayerIndex].bInvertHeight = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					})
				]
			]
		]
	];
}

TSharedRef<SWidget> SMaterialLab::BuildEffectInspectorControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerEffect() ? EVisibility::Visible : EVisibility::Collapsed;
		});


	const auto AddFloatControl = [this](
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		float FMaterialLabLayerEffect::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue)
	{
		TargetPanel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(Label)]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeResettableNumeric(
					SNew(SNumericEntryBox<float>)
					.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
					.AllowSpin(true)
					.MinValue(MinValue)
					.MaxValue(MaxValue)
					.MinSliderValue(MinValue)
					.MaxSliderValue(MaxValue)
					.Delta(Delta)
					.MinDesiredValueWidth(96.0f)
					.Value_Lambda([this, Member]() -> TOptional<float>
					{
						const FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
						return Effect ? Effect->*Member : 0.0f;
					})
					.OnValueChanged_Lambda([this, Member](const float Value)
					{
						if (FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect())
						{
							Effect->*Member = Value;
							RefreshLayeredPreview();
						}
					}),
					FSimpleDelegate::CreateLambda([this, Member, DefaultValue]()
					{
						FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
						if (!Effect)
						{
							return;
						}
						float ResetValue = DefaultValue;
						if (const UMaterialLabEffect* EffectAsset = Effect->Effect.LoadSynchronous())
						{
							if (Member == &FMaterialLabLayerEffect::Front) ResetValue = EffectAsset->DefaultFront;
							else if (Member == &FMaterialLabLayerEffect::Width) ResetValue = EffectAsset->DefaultWidth;
							else if (Member == &FMaterialLabLayerEffect::MacroWarp) ResetValue = EffectAsset->DefaultMacroWarp;
							else if (Member == &FMaterialLabLayerEffect::MicroWarp) ResetValue = EffectAsset->DefaultMicroWarp;
							else if (Member == &FMaterialLabLayerEffect::MicroMorph) ResetValue = EffectAsset->DefaultMicroMorph;
							else if (Member == &FMaterialLabLayerEffect::Thickness) ResetValue = EffectAsset->DefaultThickness;
							else if (Member == &FMaterialLabLayerEffect::Lift) ResetValue = EffectAsset->DefaultLift;
							else if (Member == &FMaterialLabLayerEffect::DetailStrength) ResetValue = EffectAsset->DefaultDetailStrength;
							else if (Member == &FMaterialLabLayerEffect::StainRoughness) ResetValue = EffectAsset->DefaultStainRoughness;
							else if (Member == &FMaterialLabLayerEffect::StainHeightInfluence) ResetValue = EffectAsset->DefaultStainHeightInfluence;
							else if (Member == &FMaterialLabLayerEffect::StainHeightWarp) ResetValue = EffectAsset->DefaultStainHeightWarp;
							else if (Member == &FMaterialLabLayerEffect::StainHeightBias) ResetValue = EffectAsset->DefaultStainHeightBias;
							else if (Member == &FMaterialLabLayerEffect::StainHeightContrast) ResetValue = EffectAsset->DefaultStainHeightContrast;
						}
						if (!FMath::IsNearlyEqual(Effect->*Member, ResetValue))
						{
							Effect->*Member = ResetValue;
							RefreshLayeredPreview();
						}
					}))
			]
		];
	};

	AddFloatControl(Panel, LOCTEXT("PeelingIntensity", "Intensity / Strength"), &FMaterialLabLayerEffect::Strength, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(Panel, LOCTEXT("PeelingBias", "Bias / Front"), &FMaterialLabLayerEffect::Front, 0.0f, 1.0f, 0.005f, 0.08f);
	AddFloatControl(Panel, LOCTEXT("PeelingWidth", "Transition Width"), &FMaterialLabLayerEffect::Width, 0.000001f, 0.25f, 0.001f, 0.015f);
	AddFloatControl(Panel, LOCTEXT("PeelingMacroWarp", "Macro Warp"), &FMaterialLabLayerEffect::MacroWarp, -1.0f, 1.0f, 0.005f, 0.01f);
	AddFloatControl(Panel, LOCTEXT("PeelingMicroWarp", "Micro Warp"), &FMaterialLabLayerEffect::MicroWarp, -1.0f, 1.0f, 0.001f, 0.003f);
	AddFloatControl(Panel, LOCTEXT("PeelingMicroMorph", "Micro Morph"), &FMaterialLabLayerEffect::MicroMorph, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(Panel, LOCTEXT("PeelingThickness", "Thickness"), &FMaterialLabLayerEffect::Thickness, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingLift", "Lift"), &FMaterialLabLayerEffect::Lift, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingDetailStrength", "Detail Strength"), &FMaterialLabLayerEffect::DetailStrength, 0.0f, 1.0f, 0.005f, 0.02f);

	TSharedRef<SVerticalBox> StainPanel = SNew(SVerticalBox);
	StainPanel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(LOCTEXT("StainColorLabel", "Stain Color"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("OpenStainColorPicker", "Open the stain color picker"))
			.OnClicked_Lambda([this]()
			{
				return OpenStainColorPicker(SelectedLayerIndex, SelectedEffectIndex);
			})
			[
				SNew(SColorBlock)
				.Color_Lambda([this]()
				{
					const FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
					return Effect ? Effect->StainColor : FLinearColor::White;
				})
				.Size(FVector2D(76.0f, 16.0f))
			]
		]
	];
	AddFloatControl(StainPanel, LOCTEXT("StainIntensity", "Intensity / Strength"), &FMaterialLabLayerEffect::Strength, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainRoughness", "Roughness Influence"), &FMaterialLabLayerEffect::StainRoughness, -1.0f, 1.0f, 0.01f, 0.2f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightInfluence", "Height Influence"), &FMaterialLabLayerEffect::StainHeightInfluence, 0.0f, 1.0f, 0.01f, 0.5f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightWarp", "Height Warp"), &FMaterialLabLayerEffect::StainHeightWarp, 0.0f, 1.0f, 0.01f, 0.35f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightBias", "Valley / Ridge Bias"), &FMaterialLabLayerEffect::StainHeightBias, -1.0f, 1.0f, 0.01f, -1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightContrast", "Height Contrast"), &FMaterialLabLayerEffect::StainHeightContrast, 0.01f, 8.0f, 0.05f, 1.0f);

	const auto MakeEnabledToggle = [this](const FText& ToolTip)
	{
		return SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleSwitch")))
			.ToolTipText(ToolTip)
			.IsChecked_Lambda([this]()
			{
				const FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
				return Effect && Effect->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
			{
				if (FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect())
				{
					Effect->bEnabled = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			});
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMaterialLabInspectorGroup)
			.Visibility_Lambda([this]()
			{
				const FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
				const UMaterialLabEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
				return Asset && Asset->EffectType == EMaterialLabEffectType::Peeling
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Title(LOCTEXT("PeelingSettingsHeading", "PEELING SETTINGS"))
			.InitiallyExpanded(false)
			.HeaderAction(MakeEnabledToggle(LOCTEXT("PeelingEnabled", "Enable Peeling")))
			[Panel]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMaterialLabInspectorGroup)
			.Visibility_Lambda([this]()
			{
				const FMaterialLabLayerEffect* Effect = GetSelectedLayerEffect();
				const UMaterialLabEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
				return Asset && Asset->EffectType == EMaterialLabEffectType::Stain
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Title(LOCTEXT("StainSettingsHeading", "STAIN SETTINGS"))
			.InitiallyExpanded(true)
			.HeaderAction(MakeEnabledToggle(LOCTEXT("StainEnabled", "Enable Stain")))
			[StainPanel]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildInspectorPanel()
{
	const ISlateStyle& Style = FMaterialLabStyle::Get();
	return SNew(SBox)
		.WidthOverride(MaterialLabUI::InspectorWidth)
		[
			SNew(SBorder)
			.Padding(MaterialLabUI::PanelPadding)
			.BorderImage(Style.GetBrush(TEXT("MaterialLab.Panel")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 3.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SAssignNew(SelectedSurfaceText, STextBlock)
							.Text(LOCTEXT("NoSelectedSurface", "No layer selected"))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SAssignNew(SelectedIdentityText, STextBlock)
							.Text(LOCTEXT("NoIdentity", "—"))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
					[
						SAssignNew(SelectedMapsText, STextBlock)
						.Text(LOCTEXT("NoMaps", "BC —  N —  RAM —"))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					.Visibility_Lambda([this]() { return GetSelectedLayerEffect() ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SScrollBox::Slot()[BuildEffectInspectorControls()]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SMaterialLabInspectorGroup)
					.Visibility_Lambda([this]() { return GetSelectedLayerEffect() ? EVisibility::Collapsed : EVisibility::Visible; })
					.Title(LOCTEXT("LayerInspectorHeading", "LAYER"))
					.InitiallyExpanded(false)
					[
						SNew(SScrollBox)
						.Visibility_Lambda([this]() { return GetSelectedLayerEffect() ? EVisibility::Collapsed : EVisibility::Visible; })
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([this]()
						{
							return bHasSelectedLayer ? EVisibility::Visible : EVisibility::Collapsed;
						})
						+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 3.0f)
						[
							SNew(SCheckBox)
							.Visibility_Lambda([this]()
							{
								return SelectedLayerIndex > 0
									&& WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type != EMaterialLabLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.ToolTipText(LOCTEXT("NormalOnlyModeHint", "Compose only this layer's normal and preserve Base Color and RAM below."))
							.IsChecked_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMaterialLabLayerChannelMode::NormalDetail ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { SetLayerNormalDetail(SelectedLayerIndex, State == ECheckBoxState::Checked); })
							[
								SNew(STextBlock).Text(LOCTEXT("NormalOnlyMode", "Normal Detail Only"))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(SComboButton)
							.Visibility_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMaterialLabLayerChannelMode::NormalDetail ? EVisibility::Visible : EVisibility::Collapsed; })
							.ButtonContent()[SNew(STextBlock).Text_Lambda([this]() { if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)) return LOCTEXT("NormalSource", "Choose Normal Source..."); const FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex]; return Layer.NormalSourceType == EMaterialLabNormalSourceType::Texture ? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName()) : LOCTEXT("SurfaceNormal", "Surface Normal"); })]
							.OnGetMenuContent_Lambda([this]() { return BuildNormalSourceMenu(SelectedLayerIndex); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SMaterialLabInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type == EMaterialLabLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("FillPropertiesHeading", "FILL PROPERTIES"))
							.InitiallyExpanded(false)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[SNew(STextBlock).Text(LOCTEXT("FillBaseColor", "Base Color"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("MaterialLab.CompactRowButton")))
									.ContentPadding(2.0f)
									.ToolTipText(LOCTEXT("OpenFillColorPicker", "Open the color picker"))
									.OnClicked_Lambda([this]() { return OpenFillColorPicker(SelectedLayerIndex); })
									[
										SNew(SColorBlock)
										.Color_Lambda([this]()
										{
											return WorkingLayers.IsValidIndex(SelectedLayerIndex)
												? WorkingLayers[SelectedLayerIndex].BaseColor
												: FLinearColor::White;
										})
										.Size(FVector2D(108.0f, 18.0f))
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FillRoughness", "Roughness"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float>
										{
											return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].Roughness : 0.5f;
										})
										.OnValueChanged_Lambda([this](const float Value)
										{
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].Roughness = Value;
											RefreshLayeredPreview();
										}),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].Roughness, 0.5f)) { WorkingLayers[SelectedLayerIndex].Roughness = 0.5f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FillIOR", "IOR"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(1.0f).MaxValue(3.0f).MinSliderValue(1.0f).MaxSliderValue(3.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float>
										{
											return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].IOR : 1.5f;
										})
										.OnValueChanged_Lambda([this](const float Value)
										{
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].IOR = Value;
											RefreshLayeredPreview();
										}),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].IOR, 1.5f)) { WorkingLayers[SelectedLayerIndex].IOR = 1.5f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FillMetallic", "Metallic"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float>
										{
											return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].Metallic : 0.0f;
										})
										.OnValueChanged_Lambda([this](const float Value)
										{
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].Metallic = Value;
											RefreshLayeredPreview();
										}),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyZero(WorkingLayers[SelectedLayerIndex].Metallic)) { WorkingLayers[SelectedLayerIndex].Metallic = 0.0f; RefreshLayeredPreview(); } }))
								]
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildChannelInfluenceControls()
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SMaterialLabInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return SelectedLayerIndex > 0
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("CompositionLabel", "COMPOSITION"))
							.InitiallyExpanded(false)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("CompositionModeLabel", "Layer Composition"))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 8.0f)
							[
								SNew(SButton)
								.Text_Lambda([this]()
								{
									return WorkingLayers.IsValidIndex(SelectedLayerIndex)
										&& WorkingLayers[SelectedLayerIndex].CompositionMode == EMaterialLabCompositionMode::Coat
										? LOCTEXT("CoatComposition", "Coat · Vertical Layer")
										: LOCTEXT("ReplaceComposition", "Replace · Horizontal Blend");
								})
								.OnClicked_Lambda([this]()
								{
									if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
									{
										FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
										Layer.CompositionMode = Layer.CompositionMode == EMaterialLabCompositionMode::Replace
											? EMaterialLabCompositionMode::Coat
											: EMaterialLabCompositionMode::Replace;
										RefreshLayeredPreview();
									}
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NormalBlendModeLabel", "Normal Composition"))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 8.0f)
							[
								SNew(SButton)
								.Text_Lambda([this]()
								{
									return WorkingLayers.IsValidIndex(SelectedLayerIndex)
										&& WorkingLayers[SelectedLayerIndex].NormalBlendMode == EMaterialLabNormalBlendMode::Override
										? LOCTEXT("OverrideNormalComposition", "Override · Masked")
										: LOCTEXT("CombineNormalComposition", "Combine · RNM · Masked");
								})
								.ToolTipText(LOCTEXT("NormalBlendModeHint", "Combine uses RNM with the final layer mask. Override replaces toward the layer normal using that same mask."))
								.OnClicked_Lambda([this]()
								{
									if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
									{
										FMaterialLabLayer& Layer = WorkingLayers[SelectedLayerIndex];
										Layer.NormalBlendMode = Layer.NormalBlendMode == EMaterialLabNormalBlendMode::Combine
											? EMaterialLabNormalBlendMode::Override
											: EMaterialLabNormalBlendMode::Combine;
										RefreshLayeredPreview();
									}
									return FReply::Handled();
								})
							]
							+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("OpacityLabel", "Opacity"))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 8.0f)
							[
								MakeResettableNumeric(
									SNew(SNumericEntryBox<float>)
									.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
									.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).MinDesiredValueWidth(96.0f)
									.Value_Lambda([this]() -> TOptional<float>
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex)
											? WorkingLayers[SelectedLayerIndex].Opacity
											: 1.0f;
									})
									.OnValueChanged_Lambda([this](const float Value)
									{
										if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
										{
											WorkingLayers[SelectedLayerIndex].Opacity = Value;
											RefreshLayeredPreview();
										}
									}),
									FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].Opacity, 1.0f)) { WorkingLayers[SelectedLayerIndex].Opacity = 1.0f; RefreshLayeredPreview(); } }))
								]

							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SMaterialLabInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type != EMaterialLabLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("SurfaceAdjustmentsHeading", "SURFACE ADJUSTMENTS"))
							.InitiallyExpanded(false)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("TilingLabel", "Tiling"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<int32>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(1).MaxValue(8).MinSliderValue(1).MaxSliderValue(8).Delta(1).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<int32> { return FMath::Max(1, FMath::RoundToInt(CurrentTiling)); })
										.OnValueChanged_Lambda([this](const int32 Value)
										{
											CurrentTiling = static_cast<float>(Value);
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].Tiling = CurrentTiling;
											PreviewSurfaceScalarParameter(TEXT("ML_Tiling"), CurrentTiling);
										}),
										FSimpleDelegate::CreateLambda([this]() { if (!FMath::IsNearlyEqual(CurrentTiling, 2.0f)) { CurrentTiling = 2.0f; if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].Tiling = 2.0f; PreviewSurfaceScalarParameter(TEXT("ML_Tiling"), 2.0f); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("RoughnessLabel", "Roughness Bias"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return CurrentRoughnessBias; })
										.OnValueChanged_Lambda([this](const float Value)
										{
											CurrentRoughnessBias = Value;
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessBias = Value;
											PreviewSurfaceScalarParameter(TEXT("ML_RoughnessBias"), Value);
										}),
										FSimpleDelegate::CreateLambda([this]() { if (!FMath::IsNearlyEqual(CurrentRoughnessBias, 0.5f)) { CurrentRoughnessBias = 0.5f; if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessBias = 0.5f; PreviewSurfaceScalarParameter(TEXT("ML_RoughnessBias"), 0.5f); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("RoughnessContrastLabel", "Roughness Contrast"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(2.0f).MinSliderValue(0.0f).MaxSliderValue(2.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return CurrentRoughnessContrast; })
										.OnValueChanged_Lambda([this](const float Value)
										{
											CurrentRoughnessContrast = Value;
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessContrast = Value;
											PreviewSurfaceScalarParameter(TEXT("ML_RoughnessContrast"), Value);
										}),
										FSimpleDelegate::CreateLambda([this]() { if (!FMath::IsNearlyEqual(CurrentRoughnessContrast, 1.0f)) { CurrentRoughnessContrast = 1.0f; if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessContrast = 1.0f; PreviewSurfaceScalarParameter(TEXT("ML_RoughnessContrast"), 1.0f); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("RoughnessOffsetLabel", "Roughness Offset"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(-0.5f).MaxValue(0.5f).MinSliderValue(-0.5f).MaxSliderValue(0.5f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return CurrentRoughnessOffset; })
										.OnValueChanged_Lambda([this](const float Value)
										{
											CurrentRoughnessOffset = Value;
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessOffset = Value;
											PreviewSurfaceScalarParameter(TEXT("ML_RoughnessOffset"), Value);
										}),
										FSimpleDelegate::CreateLambda([this]() { if (!FMath::IsNearlyZero(CurrentRoughnessOffset)) { CurrentRoughnessOffset = 0.0f; if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].RoughnessOffset = 0.0f; PreviewSurfaceScalarParameter(TEXT("ML_RoughnessOffset"), 0.0f); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("NormalLabel", "Normal Intensity"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(2.0f).MinSliderValue(0.0f).MaxSliderValue(2.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return CurrentNormalIntensity; })
										.OnValueChanged_Lambda([this](const float Value)
										{
											CurrentNormalIntensity = Value;
											if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].NormalIntensity = Value;
											PreviewSurfaceScalarParameter(TEXT("ML_NormalIntensity"), Value);
										}),
										FSimpleDelegate::CreateLambda([this]() { if (!FMath::IsNearlyEqual(CurrentNormalIntensity, 1.0f)) { CurrentNormalIntensity = 1.0f; if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) WorkingLayers[SelectedLayerIndex].NormalIntensity = 1.0f; PreviewSurfaceScalarParameter(TEXT("ML_NormalIntensity"), 1.0f); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
							[
								SNew(SMaterialLabInspectorGroup)
								.Title(LOCTEXT("GeneratedFeaturesHeading", "NORMAL / HEIGHT / AO INFLUENCE"))
								.InitiallyExpanded(false)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
									[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FeatureInfluenceLabel", "Normal Influence"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].FeatureInfluence : 0.0f; })
										.OnValueChanged_Lambda([this](const float Value) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].FeatureInfluence = Value; RefreshLayeredPreview(); } }),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyZero(WorkingLayers[SelectedLayerIndex].FeatureInfluence)) { WorkingLayers[SelectedLayerIndex].FeatureInfluence = 0.0f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FeatureBiasLabel", "Cavity ↔ Convex"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(1.0f).MinSliderValue(0.0f).MaxSliderValue(1.0f).Delta(0.01f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].FeatureBias : 0.0f; })
										.OnValueChanged_Lambda([this](const float Value) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].FeatureBias = Value; RefreshLayeredPreview(); } }),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyZero(WorkingLayers[SelectedLayerIndex].FeatureBias)) { WorkingLayers[SelectedLayerIndex].FeatureBias = 0.0f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("CurvatureRadiusLabel", "Radius"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<int32>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(1).MaxValue(32).MinSliderValue(1).MaxSliderValue(32).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<int32> { return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].CurvatureRadius : 2; })
										.OnValueChanged_Lambda([this](const int32 Value) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].CurvatureRadius = Value; RefreshLayeredPreview(); } }),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].CurvatureRadius != 2) { WorkingLayers[SelectedLayerIndex].CurvatureRadius = 2; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("CurvatureStrengthLabel", "Strength"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.0f).MaxValue(8.0f).MinSliderValue(0.0f).MaxSliderValue(8.0f).Delta(0.05f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].CurvatureStrength : 1.0f; })
										.OnValueChanged_Lambda([this](const float Value) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].CurvatureStrength = Value; RefreshLayeredPreview(); } }),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].CurvatureStrength, 1.0f)) { WorkingLayers[SelectedLayerIndex].CurvatureStrength = 1.0f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("CurvaturePowerLabel", "Power"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									MakeResettableNumeric(
										SNew(SNumericEntryBox<float>)
										.SpinBoxStyle(&FMaterialLabStyle::Get().GetWidgetStyle<FSpinBoxStyle>(TEXT("MaterialLab.ScrubControl")))
										.AllowSpin(true).MinValue(0.001f).MaxValue(8.0f).MinSliderValue(0.001f).MaxSliderValue(8.0f).Delta(0.05f).MinDesiredValueWidth(96.0f)
										.Value_Lambda([this]() -> TOptional<float> { return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? WorkingLayers[SelectedLayerIndex].CurvaturePower : 1.0f; })
										.OnValueChanged_Lambda([this](const float Value) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].CurvaturePower = Value; RefreshLayeredPreview(); } }),
										FSimpleDelegate::CreateLambda([this]() { if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !FMath::IsNearlyEqual(WorkingLayers[SelectedLayerIndex].CurvaturePower, 1.0f)) { WorkingLayers[SelectedLayerIndex].CurvaturePower = 1.0f; RefreshLayeredPreview(); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("FlipNormalYLabel", "Flip Normal Y"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SCheckBox)
									.IsChecked_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].bFlipNormalY ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
									.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { if (WorkingLayers.IsValidIndex(SelectedLayerIndex)) { WorkingLayers[SelectedLayerIndex].bFlipNormalY = State == ECheckBoxState::Checked; RefreshLayeredPreview(); } })
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								BuildSurfaceMaskInfluenceControls()
							]
								]
							]
						]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildColorAdjustmentControls()
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildHeightBlendControls()
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							BuildLayerMaskControls()
						]
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> SMaterialLab::BuildWorkspacePage(const FText& Heading, const FText& Description)
{
	return SNew(SBorder)
		.Padding(MaterialLabUI::PanelPadding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Heading).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 14.0f)[SNew(STextBlock).Text(Description).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("WetnessLayer", "Wetness · 100%"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)[SNew(SButton).Text(LOCTEXT("RustLayer", "Rust · 72%"))]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("BaseLayer", "Base Steel · 100%"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f)
						[
							SNew(SButton)
							.IsEnabled(false)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[SNew(SImage).Image(FMaterialLabStyle::Get().GetBrush(TEXT("MaterialLab.Icon.Add")))]
								+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f).VAlign(VAlign_Center)
								[SNew(STextBlock).Text(LOCTEXT("AddLayer", "Add Layer"))]
							]
						]
		];
}

TSharedRef<SWidget> SMaterialLab::BuildPresetsPage()
{
	return SNew(SBorder)
		.Padding(24.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("PresetsHeading", "LOOK PRESETS")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(STextBlock).Text(LOCTEXT("PresetsDescription", "Saved Material Lab looks will appear here for editor and runtime use.")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f)[SNew(SButton).Text(LOCTEXT("CreatePreset", "Create Preset from Current Look")).IsEnabled(false)]
		];
}

#undef LOCTEXT_NAMESPACE
