// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Shared internals for the SMixtormat implementation files.
//
// SMixtormat.cpp had grown to ~8700 lines holding one class's 143 methods plus nine
// helper widgets. The methods now live in SMixtormat_<Area>.cpp files -- all still
// members of the same class, so the split needs no change to SMixtormat.h.
//
// Helper widgets live in their own files under Dialogs/ and Gallery/. Consumers include
// them directly where practical; the remaining gallery includes are retained temporarily
// for the layer implementation while its clipboard/group work is in progress.

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
#include "UI/Layers/SMixtormatLayerContainer.h"
#include "UI/Layers/SMixtormatLayerGroupRow.h"
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

#include "Widgets/Gallery/SMixtormatGalleryScrollBox.h"
#include "Widgets/Gallery/SMixtormatMaskCard.h"
#include "Widgets/Gallery/SMixtormatTextureTile.h"

// MixtormatUI's helpers use LOCTEXT, so the namespace has to be live while they are
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

	// Reorders the stack by an arbitrary permutation and carries the height references with it.
	//
	// NewOrder[n] is the index the layer now at position n used to occupy. Grouping a scattered
	// selection is the reason this exists: gathering layers into a contiguous block is not a
	// sequence of single-layer moves, and running RemapHeightReferencesAfterMove once per member
	// would remap against indices that the previous member's move already invalidated.
	//
	// Returns how many references were dropped. A reference can only point at a layer below its
	// own -- that is what ValidateHeightReferences enforces -- so gathering layers past each other
	// can turn a reference forward, and a forward reference has no meaning to preserve. The count
	// is for telling the user, not for deciding whether the move was legal.
	inline int32 ReorderLayersByPermutation(
		TArray<FMixtormatLayer>& Layers,
		const TArray<int32>& NewOrder)
	{
		check(NewOrder.Num() == Layers.Num());

		TArray<int32> OldToNew;
		OldToNew.Init(INDEX_NONE, Layers.Num());
		for (int32 NewIndex = 0; NewIndex < NewOrder.Num(); ++NewIndex)
		{
			OldToNew[NewOrder[NewIndex]] = NewIndex;
		}

		// Read every reference before any of them move, or a rewritten one gets rewritten again.
		TArray<int32> RemappedReferences;
		RemappedReferences.Reserve(Layers.Num());
		for (const FMixtormatLayer& Layer : Layers)
		{
			RemappedReferences.Add(
				Layers.IsValidIndex(Layer.HeightReferenceLayerIndex)
					? OldToNew[Layer.HeightReferenceLayerIndex]
					: INDEX_NONE);
		}

		TArray<FMixtormatLayer> Reordered;
		Reordered.Reserve(Layers.Num());
		for (int32 NewIndex = 0; NewIndex < NewOrder.Num(); ++NewIndex)
		{
			const int32 OldIndex = NewOrder[NewIndex];
			Reordered.Add(Layers[OldIndex]);
			Reordered.Last().HeightReferenceLayerIndex = RemappedReferences[OldIndex];
		}
		Layers = MoveTemp(Reordered);

		int32 DroppedCount = 0;
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			int32& ReferenceIndex = Layers[LayerIndex].HeightReferenceLayerIndex;
			if (ReferenceIndex != INDEX_NONE && ReferenceIndex >= LayerIndex)
			{
				++DroppedCount;
			}
		}
		ValidateHeightReferences(Layers);
		return DroppedCount;
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

	inline FText MaskSourceText(const EMixtormatMaskSource Source)
	{
		switch (Source)
		{
		case EMixtormatMaskSource::LayerValues: return LOCTEXT("MaskSourceLayerValues", "Layer Values");
		default: return LOCTEXT("MaskSourceTexture", "Texture");
		}
	}

	inline FText LayerValueChannelText(const EMixtormatLayerValueChannel Channel)
	{
		switch (Channel)
		{
		case EMixtormatLayerValueChannel::Red: return LOCTEXT("LayerValueRed", "Red");
		case EMixtormatLayerValueChannel::Green: return LOCTEXT("LayerValueGreen", "Green");
		case EMixtormatLayerValueChannel::Blue: return LOCTEXT("LayerValueBlue", "Blue");
		case EMixtormatLayerValueChannel::Roughness: return LOCTEXT("LayerValueRoughness", "Roughness");
		default: return LOCTEXT("LayerValueLuminance", "Luminance");
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
		case EMixtormatPatternMode::FracturePlates: return LOCTEXT("PatternModeFracturePlates", "Fracture Plates");
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


	inline FText LayerBlurScopeText(const EMixtormatLayerBlurScope Scope)
	{
		return Scope == EMixtormatLayerBlurScope::Layer
			? LOCTEXT("LayerBlurScopeLayer", "Layer Coverage")
			: LOCTEXT("LayerBlurScopeComposite", "Whole Composite");
	}

	inline FText CurvatureSourceText(const EMixtormatCurvatureSource Source)
	{
		return Source == EMixtormatCurvatureSource::Height
			? LOCTEXT("CurvSourceHeight", "Surface Height")
			: LOCTEXT("CurvSourceMask", "Mask Itself");
	}

	inline FText CurvatureModeText(const EMixtormatCurvatureMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatCurvatureMode::Gaussian: return LOCTEXT("CurvModeGaussian", "Gaussian");
		case EMixtormatCurvatureMode::MaxPrincipal: return LOCTEXT("CurvModeMax", "Max Principal");
		case EMixtormatCurvatureMode::MinPrincipal: return LOCTEXT("CurvModeMin", "Min Principal");
		case EMixtormatCurvatureMode::AngleDeficit: return LOCTEXT("CurvModeDeficit", "Angle Deficit");
		case EMixtormatCurvatureMode::AngleDeficitConvex: return LOCTEXT("CurvModeDeficitConvex", "Deficit Convex");
		case EMixtormatCurvatureMode::AngleDeficitConcave: return LOCTEXT("CurvModeDeficitConcave", "Deficit Concave");
		default: return LOCTEXT("CurvModeMean", "Mean");
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

#undef LOCTEXT_NAMESPACE
