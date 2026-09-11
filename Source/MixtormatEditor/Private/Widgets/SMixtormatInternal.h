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
#include "Services/MixtormatPaths.h"
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
#include "MaterialEditingLibrary.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
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

	inline bool IsUserLibraryAsset(const FSoftObjectPath& AssetPath)
	{
		return AssetPath.ToString().StartsWith(
			FMixtormatPaths::ProjectLibraryRoot() + TEXT("/"),
			ESearchCase::CaseSensitive);
	}

	inline TSharedRef<SWidget> BuildLibraryOwnershipBadge(
		const FSoftObjectPath& AssetPath,
		const bool bShowBuiltIn = true)
	{
		const bool bIsUserAsset = IsUserLibraryAsset(AssetPath);
		if (!bIsUserAsset && !bShowBuiltIn)
		{
			return SNew(SBox).Visibility(EVisibility::Collapsed);
		}

		const ISlateStyle& Style = FMixtormatStyle::Get();
		return SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.ThumbnailBackground")))
			.ToolTipText(bIsUserAsset
				? LOCTEXT("UserLibraryAssetBadge", "User material")
				: LOCTEXT("BuiltInLibraryAssetBadge", "Built-in Mixtormat material"))
			[
				SNew(SBox)
				.WidthOverride(12.0f)
				.HeightOverride(12.0f)
				[
					SNew(SImage)
					.Image(Style.GetBrush(bIsUserAsset
						? TEXT("Mixtormat.Icon.Folder")
						: TEXT("Mixtormat.Brand.Icon")))
				]
			];
	}

	inline FAssetThumbnailConfig CleanThumbnailConfig()
	{
		FAssetThumbnailConfig Config;
		Config.ThumbnailLabel = EThumbnailLabel::NoLabel;
		Config.AllowAssetSpecificThumbnailOverlay = false;
		Config.ShowAssetColor = false;
		Config.ShowAssetBorder = false;
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
			const FString IconPath = FPaths::Combine(
				FMixtormatPaths::ResourcesDir(),
				TEXT("Icons"),
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
		case EMixtormatMaskBlendMode::Difference: return LOCTEXT("MaskModeDifference", "Difference");
		case EMixtormatMaskBlendMode::Exclusion: return LOCTEXT("MaskModeExclusion", "Exclusion");
		default: return LOCTEXT("MaskModeReplace", "Replace");
		}
	}

	inline FText ColorBlendModeText(const EMixtormatColorBlendMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatColorBlendMode::Add: return LOCTEXT("ColorModeAdd", "Add");
		case EMixtormatColorBlendMode::Subtract: return LOCTEXT("ColorModeSubtract", "Subtract");
		case EMixtormatColorBlendMode::Multiply: return LOCTEXT("ColorModeMultiply", "Multiply");
		case EMixtormatColorBlendMode::Divide: return LOCTEXT("ColorModeDivide", "Divide");
		case EMixtormatColorBlendMode::Screen: return LOCTEXT("ColorModeScreen", "Screen");
		case EMixtormatColorBlendMode::Overlay: return LOCTEXT("ColorModeOverlay", "Overlay");
		case EMixtormatColorBlendMode::HardLight: return LOCTEXT("ColorModeHardLight", "Hard Light");
		case EMixtormatColorBlendMode::SoftLight: return LOCTEXT("ColorModeSoftLight", "Soft Light");
		case EMixtormatColorBlendMode::ColorDodge: return LOCTEXT("ColorModeDodge", "Color Dodge");
		case EMixtormatColorBlendMode::ColorBurn: return LOCTEXT("ColorModeBurn", "Color Burn");
		case EMixtormatColorBlendMode::AddSub: return LOCTEXT("ColorModeAddSub", "Add/Sub");
		case EMixtormatColorBlendMode::Difference: return LOCTEXT("ColorModeDifference", "Difference");
		case EMixtormatColorBlendMode::Exclusion: return LOCTEXT("ColorModeExclusion", "Exclusion");
		case EMixtormatColorBlendMode::Min: return LOCTEXT("ColorModeMin", "Min");
		case EMixtormatColorBlendMode::Max: return LOCTEXT("ColorModeMax", "Max");
		case EMixtormatColorBlendMode::Hue: return LOCTEXT("ColorModeHue", "Hue");
		case EMixtormatColorBlendMode::Saturation: return LOCTEXT("ColorModeSaturation", "Saturation");
		case EMixtormatColorBlendMode::Color: return LOCTEXT("ColorModeColor", "Color");
		case EMixtormatColorBlendMode::Luminosity: return LOCTEXT("ColorModeLuminosity", "Luminosity");
		default: return LOCTEXT("ColorModeNormal", "Normal");
		}
	}

	// Every colour blend mode, in enum order, for the chip's menu.
	inline const TArray<EMixtormatColorBlendMode>& ColorBlendModes()
	{
		static const TArray<EMixtormatColorBlendMode> Modes = {
			EMixtormatColorBlendMode::Normal,
			EMixtormatColorBlendMode::Add,
			EMixtormatColorBlendMode::Subtract,
			EMixtormatColorBlendMode::Multiply,
			EMixtormatColorBlendMode::Divide,
			EMixtormatColorBlendMode::Screen,
			EMixtormatColorBlendMode::Overlay,
			EMixtormatColorBlendMode::HardLight,
			EMixtormatColorBlendMode::SoftLight,
			EMixtormatColorBlendMode::ColorDodge,
			EMixtormatColorBlendMode::ColorBurn,
			EMixtormatColorBlendMode::AddSub,
			EMixtormatColorBlendMode::Difference,
			EMixtormatColorBlendMode::Exclusion,
			EMixtormatColorBlendMode::Min,
			EMixtormatColorBlendMode::Max,
			EMixtormatColorBlendMode::Hue,
			EMixtormatColorBlendMode::Saturation,
			EMixtormatColorBlendMode::Color,
			EMixtormatColorBlendMode::Luminosity
		};
		return Modes;
	}

	inline FText PatternModeText(const EMixtormatPatternMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatPatternMode::RunningBond: return LOCTEXT("PatternModeRunningBond", "Running Bond");
		case EMixtormatPatternMode::Herringbone: return LOCTEXT("PatternModeHerringbone", "Herringbone");
		case EMixtormatPatternMode::Basketweave: return LOCTEXT("PatternModeBasketweave", "Basketweave");
		case EMixtormatPatternMode::Hex: return LOCTEXT("PatternModeHex", "Hex");
		case EMixtormatPatternMode::OctagonSquare: return LOCTEXT("PatternModeOctagonSquare", "Octagon + Square");
		case EMixtormatPatternMode::Flagstone: return LOCTEXT("PatternModeFlagstone", "Flagstone");
		case EMixtormatPatternMode::Voronoi: return LOCTEXT("PatternModeVoronoi", "Voronoi");
		case EMixtormatPatternMode::Hopscotch: return LOCTEXT("PatternModeHopscotch", "Hopscotch");
		case EMixtormatPatternMode::FrenchAshlar: return LOCTEXT("PatternModeFrenchAshlar", "French / Modular Ashlar");
		default: return LOCTEXT("PatternModeGrid", "Grid");
		}
	}

	inline FText GridModeText(const EMixtormatGridMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatGridMode::Staggered: return LOCTEXT("GridModeStaggered", "Staggered");
		case EMixtormatGridMode::Diamond: return LOCTEXT("GridModeDiamond", "Diamond / 45 Degree");
		default: return LOCTEXT("GridModeStraight", "Straight");
		}
	}

	inline FText UVRotationText(const EMixtormatUVRotation Rotation)
	{
		switch (Rotation)
		{
		case EMixtormatUVRotation::Quarter: return LOCTEXT("UVRot90", "90°");
		case EMixtormatUVRotation::Half: return LOCTEXT("UVRot180", "180°");
		case EMixtormatUVRotation::ThreeQuarter: return LOCTEXT("UVRot270", "270°");
		default: return LOCTEXT("UVRot0", "0°");
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


	inline FText ErosionCurvatureModeText(const EMixtormatErosionCurvatureMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatErosionCurvatureMode::Valley: return LOCTEXT("EroCurvValley", "Valley");
		case EMixtormatErosionCurvatureMode::Ridge: return LOCTEXT("EroCurvRidge", "Ridge");
		default: return LOCTEXT("EroCurvMean", "Mean");
		}
	}

	inline FText StainModeText(const EMixtormatStainMode Mode)
	{
		return Mode == EMixtormatStainMode::Deposit
			? LOCTEXT("StainModeDeposit", "Deposit")
			: LOCTEXT("StainModeWet", "Wet");
	}
}

namespace MixtormatBakeDialog
{
	// The fixed picker's own option set, in display order. Matches
	// EMixtormatBakeResolution / EMixtormatBakeAASamples in UMixtormatEditorSettings.
	constexpr int32 ResolutionOptions[] = {512, 1024, 2048, 4096};
	constexpr int32 AASamplesOptions[] = {1, 2, 4, 8};

	template <int32 N>
	int32 ValueToIndex(const int32 (&Options)[N], const int32 Value, const int32 DefaultIndex)
	{
		for (int32 Index = 0; Index < N; ++Index)
		{
			if (Options[Index] == Value)
			{
				return Index;
			}
		}
		return DefaultIndex;
	}
}

class SMixtormatBakeSettingsDialog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatBakeSettingsDialog) {}
		SLATE_ARGUMENT(FMixtormatBakeSettings, InitialSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Settings = InArgs._InitialSettings;
		ChildSlot
		[
			SNew(SBorder)
			.Padding(MixtormatTokens::DialogPadding)
			.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeDestinationLabel", "Destination Folder"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
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
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f, 0.0f, 0.0f)
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
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogFieldBottomMargin)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(Settings.BaseName))
					.OnTextChanged_Lambda([this](const FText& Text)
					{
						Settings.BaseName = Text.ToString();
						ValidationText = FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeSettingsSectionLabel", "Bake Settings"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f)
					[
						SNew(SBox).WidthOverride(MixtormatTokens::BakeDialogSettingLabelWidth)
						[
							SNew(STextBlock).Text(LOCTEXT("BakeResolutionLabel", "Resolution"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SMixtormatSegmentedControl)
						.Options({LOCTEXT("BakeRes512", "512"), LOCTEXT("BakeRes1024", "1024"), LOCTEXT("BakeRes2048", "2048"), LOCTEXT("BakeRes4096", "4096")})
						.ActiveIndex_Lambda([this]()
						{
							return MixtormatBakeDialog::ValueToIndex(MixtormatBakeDialog::ResolutionOptions, Settings.Resolution, 2);
						})
						.OnChosen_Lambda([this](const int32 Index)
						{
							Settings.Resolution = MixtormatBakeDialog::ResolutionOptions[Index];
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogSectionGap)
				[
					SNew(SHorizontalBox)
					.ToolTipText(LOCTEXT("BakeAASamplesDisabledHint", "Supersampling support is not available yet."))
					.IsEnabled(false)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, MixtormatTokens::BakeDialogBrowseButtonGap, 0.0f)
					[
						SNew(SBox).WidthOverride(MixtormatTokens::BakeDialogSettingLabelWidth)
						[
							SNew(STextBlock).Text(LOCTEXT("BakeAASamplesLabel", "AA Samples"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SMixtormatSegmentedControl)
						.Options({LOCTEXT("BakeAa1x", "1x"), LOCTEXT("BakeAa2x", "2x"), LOCTEXT("BakeAa4x", "4x"), LOCTEXT("BakeAa8x", "8x")})
						.ActiveIndex_Lambda([this]()
						{
							return MixtormatBakeDialog::ValueToIndex(MixtormatBakeDialog::AASamplesOptions, Settings.AASamples, 0);
						})
						.OnChosen_Lambda([this](const int32 Index)
						{
							Settings.AASamples = MixtormatBakeDialog::AASamplesOptions[Index];
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BakeOutputPreviewLabel", "Generated Asset Names"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDialogLabel))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::BakeDialogFieldTopMargin, 0.0f, MixtormatTokens::BakeDialogSectionGap)
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
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::BakeDialogSectionGap)
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
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
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
			.Padding(MixtormatTokens::DialogPadding)
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
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, MixtormatTokens::DialogActionsTopMargin, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(InArgs._CancelLabel)
						.OnClicked(this, &SMixtormatActionDialog::Cancel)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Visibility(InArgs._AlternateLabel.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.Text(InArgs._AlternateLabel)
						.OnClicked(this, &SMixtormatActionDialog::Alternate)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
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
		.ClientSize(FVector2D(MixtormatTokens::ActionDialogWidth, MixtormatTokens::ActionDialogHeight))
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
		.ClientSize(FVector2D(MixtormatTokens::ActionDialogWidth, MixtormatTokens::ActionDialogHeight))
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
			.Padding(MixtormatTokens::DialogPadding)
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
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, MixtormatTokens::DialogActionsTopMargin, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeActionButton(LOCTEXT("CloseBakeResult", "Close"), EMixtormatBakeResultAction::Close)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("RebakeBakeResult", "Re-bake"), EMixtormatBakeResultAction::Rebake)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("ApplyBakeResult", "Apply to Selected Actors"), EMixtormatBakeResultAction::Apply)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
					[
						MakeActionButton(LOCTEXT("OpenBakeResult", "Open Material Instance"), EMixtormatBakeResultAction::Open)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DialogButtonGap, 0.0f, 0.0f, 0.0f)
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
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
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
			SAssignNew(ContextAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuRight)
			.UseApplicationMenuStack(true)
			.OnGetMenuContent(InArgs._OnGetContextMenu)
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
			]
		];
	}

	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (ContextAnchor.IsValid())
			{
				ContextAnchor->SetIsOpen(true);
			}
			return FReply::Handled();
		}
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
		if (MouseEvent.IsControlDown() && Direction != 0 && OnGalleryZoom.IsBound())
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
	TSharedPtr<SMenuAnchor> ContextAnchor;
};

DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatMaskSelected, FText, FSoftObjectPath);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnMixtormatChildSelected, int32, int32);

class SMixtormatMaskCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatMaskCard) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(FText, DisplayName)
		SLATE_ARGUMENT(FSoftObjectPath, MaskPath)
		SLATE_ARGUMENT(FAssetData, ThumbnailAsset)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_EVENT(FOnMixtormatMaskSelected, OnSelected)
		SLATE_EVENT(FOnMixtormatSurfaceGalleryZoom, OnGalleryZoom)
		SLATE_EVENT(FOnGetContent, OnGetContextMenu)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		DisplayName = InArgs._DisplayName;
		MaskPath = InArgs._MaskPath;
		ThumbnailAsset = InArgs._ThumbnailAsset;
		ThumbnailPool = InArgs._ThumbnailPool;
		OnSelected = InArgs._OnSelected;
		OnGalleryZoom = InArgs._OnGalleryZoom;
		ChildSlot
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SAssignNew(ContextAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuRight)
			.UseApplicationMenuStack(true)
			.OnGetMenuContent(InArgs._OnGetContextMenu)
			[
				InArgs._Content.Widget
			]
		];
	}

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() == EKeys::RightMouseButton)
		{
			if (ContextAnchor.IsValid())
			{
				ContextAnchor->SetIsOpen(true);
			}
			return FReply::Handled();
		}
		if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return SCompoundWidget::OnPreviewMouseButtonDown(Geometry, Event);
		}
		if (OnSelected.IsBound())
		{
			OnSelected.Execute(DisplayName, MaskPath);
		}
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		const int32 Direction = FMath::Sign(Event.GetWheelDelta());
		if (Event.IsControlDown() && Direction != 0 && OnGalleryZoom.IsBound())
		{
			OnGalleryZoom.Execute(Direction);
			return FReply::Handled();
		}
		return SCompoundWidget::OnMouseWheel(Geometry, Event);
	}

	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return FReply::Handled().BeginDragDrop(
			FMixtormatMaskDragDropOp::New(DisplayName, MaskPath, ThumbnailAsset, ThumbnailPool));
	}

private:
	FText DisplayName;
	FSoftObjectPath MaskPath;
	FAssetData ThumbnailAsset;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnMixtormatMaskSelected OnSelected;
	FOnMixtormatSurfaceGalleryZoom OnGalleryZoom;
	TSharedPtr<SMenuAnchor> ContextAnchor;
};

#undef LOCTEXT_NAMESPACE
