// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatLiveTheme.h"

#include "Services/MixtormatPaths.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TMap<FName, FLinearColor>& ColorOverrides()
	{
		static TMap<FName, FLinearColor> Values;
		return Values;
	}

	bool IsValidColor(const FLinearColor& Value)
	{
		return FMath::IsFinite(Value.R) && FMath::IsFinite(Value.G)
			&& FMath::IsFinite(Value.B) && FMath::IsFinite(Value.A)
			&& Value.R >= 0.0f && Value.R <= 1.0f
			&& Value.G >= 0.0f && Value.G <= 1.0f
			&& Value.B >= 0.0f && Value.B <= 1.0f
			&& Value.A >= 0.0f && Value.A <= 1.0f;
	}
}


void FMixtormatLiveTheme::Initialize()
{
	// Capture authored defaults before any panel edit or file load.
	Numbers();
	Colors();
	MixtormatTokens::RecomputeDerived();
}

const TArray<FMixtormatThemeNumber>& FMixtormatLiveTheme::Numbers()
{
#define THEME_NUMBER_UI(Category, Name, Min, Max, Expose) \
	{TEXT(#Name), TEXT(Category), &MixtormatTokens::Name, MixtormatTokens::Name, Min, Max, Expose}
#define THEME_NUMBER(Category, Name, Min, Max) THEME_NUMBER_UI(Category, Name, Min, Max, true)
	static const TArray<FMixtormatThemeNumber> Entries = {
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampHeight, 48.0f, 240.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampCurveThickness, 0.5f, 5.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampGridThickness, 0.25f, 3.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampMajorGridThickness, 0.25f, 4.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampPointSize, 3.0f, 16.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampIconSize, 8.0f, 28.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampIconGap, 0.0f, 16.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampToolbarGap, 0.0f, 20.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampToolbarGroupGap, 0.0f, 32.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampToolbarHeight, 14.0f, 40.0f),
		THEME_NUMBER("Scalar Ramp / Curve Editor", ScalarRampViewportPadding, 0.0f, 32.0f),
		THEME_NUMBER("Rows", RowHeight, 12.0f, 48.0f),
		THEME_NUMBER("Rows", RowGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", PairedGap, 0.0f, 24.0f),
				THEME_NUMBER("Controls / Well", WellShadeTop, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellShadeBottom, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderWidth, 0.0f, 4.0f),
				THEME_NUMBER("Controls / Well", WellBorderOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderTopOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderBottomOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderHoverOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderHoverTopOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderHoverBottomOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellBorderSaturation, 0.0f, 3.0f),
				THEME_NUMBER("Controls / Well", WellHoverLiftOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Well", WellRadius, 0.0f, 12.0f),
				THEME_NUMBER("Controls / Well", ZeroTickOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyTop, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyBottom, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyHoverTop, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyHoverBottom, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyActiveTop, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillBodyActiveBottom, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillDisabledOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillShadeStart, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillShadeMid, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillShadeEnd, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillShadeMidPosition, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Fill", FillFalloffPower, 0.01f, 8.0f),
				THEME_NUMBER("Controls / Fill", FillSaturation, 0.0f, 3.0f),
				THEME_NUMBER("Controls / Fill", FillHoverSaturation, 0.0f, 3.0f),
				THEME_NUMBER("Controls / Fill", FillActiveSaturation, 0.0f, 3.0f),
				THEME_NUMBER("Controls / Fill", FillDisabledSaturation, 0.0f, 3.0f),
				THEME_NUMBER("Controls / Text", ControlLabelOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Text", ControlValueOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Text", TextDisabledOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Toggle", ToggleSize, 8.0f, 40.0f),
				THEME_NUMBER("Controls / Toggle", ToggleFillInset, 0.0f, 8.0f),
				THEME_NUMBER("Controls / Toggle", ToggleDisabledShadeTop, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Toggle", ToggleDisabledShadeBottom, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Typography", FontControlLabel, 6.0f, 24.0f),
				THEME_NUMBER("Controls / Typography", FontControlValue, 6.0f, 24.0f),
				THEME_NUMBER("Controls / Typography", ControlLabelBold, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Typography", ControlValueBold, 0.0f, 1.0f),
				THEME_NUMBER("Controls / Typography", ControlLabelLetterSpacing, 0.0f, 1000.0f),
				THEME_NUMBER("Foldouts", FoldoutHeight, 12.0f, 48.0f),
						THEME_NUMBER("Foldouts", FoldoutGutter, 0.0f, 20.0f),
						THEME_NUMBER("Foldouts", FoldoutBodyTop, 0.0f, 24.0f),
						THEME_NUMBER("Foldouts", FoldoutBodyBottom, 0.0f, 24.0f),
						THEME_NUMBER("Foldouts", FoldoutOuterTop, 0.0f, 24.0f),
						THEME_NUMBER("Foldouts", FoldoutOuterBottom, 0.0f, 24.0f),
						THEME_NUMBER("Foldouts", FoldoutHeaderPaddingTop, 0.0f, 16.0f),
						THEME_NUMBER("Foldouts", FoldoutHeaderPaddingBottom, 0.0f, 16.0f),
						THEME_NUMBER("Foldouts", FoldoutRadius, 0.0f, 12.0f),
						THEME_NUMBER("Foldouts", FoldoutIconSize, 6.0f, 32.0f),
								THEME_NUMBER("Foldouts", FoldoutIconPadding, 0.0f, 8.0f),
														THEME_NUMBER("Foldouts", FoldoutChevronInset, 0.0f, 8.0f),
								THEME_NUMBER("Foldouts", FoldoutHeaderGap, 0.0f, 24.0f),
						THEME_NUMBER("Foldouts", HeaderTintOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", HeaderHoverOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", HairlineHoverOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutFalloffPower, 0.01f, 4.0f),
						THEME_NUMBER("Foldouts", FoldoutAccentMultiplyOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutAccentHoverMultiplyOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutHairlineOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutHairlineSaturation, 0.0f, 3.0f),
						THEME_NUMBER("Foldouts", FoldoutHairlineHoverSaturation, 0.0f, 3.0f),
						THEME_NUMBER("Foldouts", FoldoutSaturation, 0.0f, 3.0f),
						THEME_NUMBER("Foldouts", FoldoutHoverSaturation, 0.0f, 3.0f),
						THEME_NUMBER("Foldouts", FontFoldoutTitle, 6.0f, 20.0f),
						THEME_NUMBER("Foldouts", FoldoutTitleBold, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutTitleTracking, 0.0f, 4.0f),
						THEME_NUMBER("Foldouts", FoldoutTitleOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Foldouts", FoldoutTitleDisabledOpacity, 0.0f, 1.0f),
						THEME_NUMBER("Sliders", ModifiedStripeOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Sliders", TickInsetY, 0.0f, 12.0f),
				THEME_NUMBER("Sliders", TickWidth, 0.0f, 4.0f),
		THEME_NUMBER("Rows", SegmentedControlGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", SliderRowGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", RowTextInset, 1.0f, 24.0f),
		THEME_NUMBER("Rows", RowLabelGap, 0.0f, 32.0f),
		THEME_NUMBER("Rows", RowFieldMinWidth, 40.0f, 400.0f),
		THEME_NUMBER("Rows", DropdownLabelRatio, 0.0f, 1.0f),
		THEME_NUMBER("Sliders", DraggerTextInset, 0.0f, 24.0f),
		THEME_NUMBER("Sliders", ModifiedStripeWidth, 0.0f, 12.0f),
		THEME_NUMBER("Sliders", DragRangeDistance, 80.0f, 1200.0f),
		THEME_NUMBER("Sliders", FineDragScale, 0.01f, 1.0f),
		THEME_NUMBER("Sliders", DragThreshold, 0.0f, 16.0f),
		THEME_NUMBER("Surfaces", CornerRadius, 0.0f, 12.0f),
		THEME_NUMBER("Surfaces", OutlineWidth, 0.0f, 4.0f),
		// No live readers remain; retain both persisted names for existing theme files.
		THEME_NUMBER_UI("Surfaces", PanelGutter, 0.0f, 32.0f, false),
		THEME_NUMBER_UI("Surfaces", GroupHeaderHeight, 16.0f, 64.0f, false),
		THEME_NUMBER("Surfaces", GroupOuterGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", GroupHeaderItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", CardPadding, 0.0f, 32.0f),
		THEME_NUMBER("Surfaces", CardGap, 0.0f, 32.0f),
		THEME_NUMBER("Surfaces", CardTitleGap, 0.0f, 24.0f),
		THEME_NUMBER("Group Cards", GroupCardRadius, 0.0f, 12.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderMarginTop, 0.0f, 24.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderMarginBottom, 0.0f, 24.0f),
		THEME_NUMBER("Group Cards", GroupCardFalloffPower, 0.01f, 4.0f),
		THEME_NUMBER("Group Cards", GroupCardGradientReach, 0.0f, 128.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderSaturation, 0.0f, 4.0f),
		THEME_NUMBER("Group Cards", GroupCardBodySaturation, 0.0f, 4.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Group Cards", GroupCardBodyOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Group Cards", GroupCardHorizontalPadding, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderPaddingLeft, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderPaddingTop, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderPaddingRight, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardHeaderPaddingBottom, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardOuterMarginLeft, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardOuterMarginTop, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardOuterMarginRight, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardOuterMarginBottom, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardContentPaddingTop, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardContentPaddingBottom, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", FontGroupCardTitle, 6.0f, 20.0f),
		THEME_NUMBER("Group Cards", GroupCardTitleBold, 0.0f, 1.0f),
		THEME_NUMBER("Group Cards", GroupCardTitleLetterSpacing, 0.0f, 1000.0f),
		THEME_NUMBER("Group Cards", GroupCardTitleHeight, 16.0f, 64.0f),
		THEME_NUMBER("Group Cards", GroupCardTitleDropDepth, 0.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardTitleWidthRatio, 0.5f, 0.5f),
		THEME_NUMBER("Group Cards", GroupCardLeadingIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardLeadingGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", HeaderContentGap, 0.0f, 24.0f),
		THEME_NUMBER("Buttons", ButtonHeight, 16.0f, 48.0f),
		THEME_NUMBER("Buttons", ButtonPaddingCompact, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingPrimary, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingTab, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", TabWidth, 48.0f, 200.0f),
		THEME_NUMBER("Buttons", TabHeight, 12.0f, 64.0f),
		THEME_NUMBER("Buttons", SegmentHeight, 12.0f, 48.0f),
		THEME_NUMBER("Buttons", ToolbarIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Buttons", ToolbarButtonMargin, 0.0f, 24.0f),
		THEME_NUMBER("Buttons", ToolbarLabelPadding, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayInset, 0.0f, 48.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayClusterInset, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayButtonGap, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", OverlayHoverAccent, 0.0f, 1.0f),
		THEME_NUMBER("Preview overlays", OverlayPressAccent, 0.0f, 1.0f),
		THEME_NUMBER("Preview overlays", OverlayIconRestOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Inspector", InspectorTopMargin, 0.0f, 48.0f),
		THEME_NUMBER("Inspector", GroupHeaderAlign, 0.0f, 2.0f),
		THEME_NUMBER("Inspector", SubgroupHeaderAlign, 0.0f, 2.0f),
		THEME_NUMBER("Inspector", InspectorHairlineThickness, 0.0f, 4.0f),
		THEME_NUMBER("Inspector", InspectorHairlineInset, 0.0f, 64.0f),
		THEME_NUMBER("Inspector", InspectorHairlineUnderHeader, 0.0f, 1.0f),

		THEME_NUMBER("Inspector", InspectorMaskGalleryMaxHeight, 120.0f, 1000.0f),
		THEME_NUMBER("Inspector", InspectorFeatureButtonGap, 0.0f, 24.0f),
		THEME_NUMBER("Inspector", InspectorColorSwatchWidth, 32.0f, 240.0f),
		THEME_NUMBER("Inspector", InspectorColorSwatchHeight, 8.0f, 48.0f),
		THEME_NUMBER("Shell", PanelPadding, 0.0f, 32.0f),
		THEME_NUMBER("Shell", LayerStackWidth, 160.0f, 640.0f),
		THEME_NUMBER("Shell", InspectorWidth, 200.0f, 720.0f),
		THEME_NUMBER("Shell", TopBarHeight, 24.0f, 64.0f),
		THEME_NUMBER("Shell", StatusBarHeight, 14.0f, 48.0f),
		THEME_NUMBER("Shell", SplitterHandleSize, 0.0f, 12.0f),
		THEME_NUMBER("Shell", SplitterHitSize, 2.0f, 24.0f),
		THEME_NUMBER("Shell", BottomLibraryCollapseButtonWidth, 72.0f, 240.0f),
		THEME_NUMBER("Shell", BottomLibraryCollapseButtonHeight, 18.0f, 40.0f),
		THEME_NUMBER("Layers", IconButtonHitSlop, 0.0f, 24.0f),
		THEME_NUMBER("Layers", IconButtonSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", GroupRowCrossStrength, 0.0f, 1.0f),
		THEME_NUMBER("Layers", GroupAccentStrength, 0.0f, 1.0f),
		THEME_NUMBER("Layers", GroupAccentSelectedStrength, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", LayerHoverSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", LayerSelectedSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", LayerGroupSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", ChildSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", ChildHoverSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", ChildSelectedSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", ChildLeftOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", ChildRightOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", ChildHoverLeftOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", ChildHoverRightOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", ChildSelectedLeftOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", ChildSelectedRightOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerActiveHairlineWidth, 0.0f, 4.0f),
		THEME_NUMBER("Layers", LayerActiveHairlineOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerActiveGlowOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerActiveGlowReach, 0.0f, 128.0f),
		THEME_NUMBER("Layers", LayerActiveGlowSaturation, 0.0f, 3.0f),
		THEME_NUMBER("Layers", LayerIconSize, 6.0f, 32.0f),
		THEME_NUMBER("Layers", LayerIconOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerVisibilitySize, 0.0f, 32.0f),
		THEME_NUMBER("Layers", LayerVisibilityRadius, 0.0f, 16.0f),
		THEME_NUMBER("Layers", IconOffOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Foldouts", FoldoutIconOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Foldouts", FoldoutIconHoverOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", LayerGroupTitleSize, 6.0f, 24.0f),
		THEME_NUMBER("Layers", LayerGroupTitleWeight, 400.0f, 700.0f),
		THEME_NUMBER("Layers", LayerHierarchyLineWidth, 0.0f, 4.0f),
		THEME_NUMBER("Layers", LayerRowHeight, 20.0f, 64.0f),
		THEME_NUMBER("Layers", LayerChildRowHeight, 16.0f, 48.0f),
		THEME_NUMBER("Layers", LayerGroupRowHeight, 16.0f, 48.0f),
		THEME_NUMBER("Layers", LayerThumbnailSize, 12.0f, 48.0f),
		THEME_NUMBER("Layers", LayerChildIndent, 0.0f, 80.0f),
		THEME_NUMBER("Layers", LayerScopeIndent, 0.0f, 48.0f),
		THEME_NUMBER("Layers", LayerChildIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", LayerEyeSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", ChevronSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", LayerConnectorOpacity, 0.0f, 1.0f),
		THEME_NUMBER("Layers", BadgeCornerRadius, 0.0f, 8.0f),
		THEME_NUMBER("Layers", BadgeWidth, 16.0f, 160.0f),
		THEME_NUMBER("Layers", LayerItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Layers", LayerRowInsetLeading, 0.0f, 32.0f),
		THEME_NUMBER("Layers", LayerRowInsetTrailing, 0.0f, 32.0f),
		THEME_NUMBER("Layers", LayerRowGap, 0.0f, 24.0f),
		THEME_NUMBER("Layers", LayerStackHeaderHeight, 14.0f, 32.0f),
		THEME_NUMBER("Layers", MaskBarTileSize, 32.0f, 160.0f),
		THEME_NUMBER("Menus and dialogs", MenuWidth, 140.0f, 400.0f),
		THEME_NUMBER("Galleries", MaskPickerWidth, 200.0f, 1200.0f),
		THEME_NUMBER("Galleries", MaskPickerMaxHeight, 200.0f, 1200.0f),
		THEME_NUMBER("Galleries", MaskGalleryTileGap, 0.0f, 24.0f),
		THEME_NUMBER("Menus and dialogs", MenuPanelPadding, 0.0f, 24.0f),
		THEME_NUMBER("Menus and dialogs", MenuItemHeight, 12.0f, 48.0f),
		THEME_NUMBER("Menus and dialogs", MenuLipHeight, 8.0f, 64.0f),
		THEME_NUMBER("Menus and dialogs", MenuIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Menus and dialogs", DialogPadding, 0.0f, 48.0f),
		THEME_NUMBER("Menus and dialogs", DialogButtonGap, 0.0f, 32.0f),
		THEME_NUMBER("Menus and dialogs", DialogActionsTopMargin, 0.0f, 48.0f),
		THEME_NUMBER("Typography", FontBody, 8.0f, 24.0f),
		THEME_NUMBER("Typography", FontCaption, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontCardTitle, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontTile, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontGroupHeader, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontLayerSource, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontBadge, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontDialogLabel, 6.0f, 24.0f),
		THEME_NUMBER("Typography", FontMaskBarHeading, 6.0f, 24.0f),
		THEME_NUMBER("Typography", FontDragGhostLabel, 6.0f, 24.0f),
		THEME_NUMBER("Typography", FontSliderLabel, 6.0f, 24.0f),
		THEME_NUMBER("Typography", CardTitleBold, 0.0f, 1.0f),
		THEME_NUMBER("Typography", GroupHeaderBold, 0.0f, 1.0f)
	};
#undef THEME_NUMBER
#undef THEME_NUMBER_UI
	return Entries;
}

const TArray<FMixtormatThemeColor>& FMixtormatLiveTheme::Colors()
{
#define THEME_COLOR(Category, Name) {TEXT(#Name), MixtormatPalette::Name(), TEXT(Category), true}
	static const TArray<FMixtormatThemeColor> Entries = {
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampBackground),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampOutsideRangeBackground),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampGrid),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampMajorGrid),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampCurve),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampFill),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampPoint),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampPointHover),
		THEME_COLOR("Scalar Ramp / Curve Editor", ScalarRampPointSelected),
		THEME_COLOR("Shell", Window), THEME_COLOR("Shell", TopBar), THEME_COLOR("Shell", Shell),
		THEME_COLOR("Base Palette", Ground),
		THEME_COLOR("Surfaces", Panel), THEME_COLOR("Surfaces", RaisedPanel),
		THEME_COLOR("Surfaces", RaisedPanelHover), THEME_COLOR("Foldouts", GroupSurround),
		THEME_COLOR("Group Cards", GroupCardBackground), THEME_COLOR("Foldouts", HeaderTint),
		THEME_COLOR("Foldouts", Hairline), THEME_COLOR("Foldouts", HairlineHover),
		THEME_COLOR("Foldouts", HeaderText),
		THEME_COLOR("Rows", RowText), THEME_COLOR("Typography", CaptionText),
		THEME_COLOR("Surfaces", CardTitleText), THEME_COLOR("Group Cards", GroupCardTitleText),
		THEME_COLOR("Layers", LayerName), THEME_COLOR("Layers", LayerSource),
		THEME_COLOR("Layers", LayerEdge), THEME_COLOR("Base Palette", Accent),
		THEME_COLOR("Base Palette", AccentBright), THEME_COLOR("Base Palette", SelectionFill),
		THEME_COLOR("Base Palette", FocusFill), THEME_COLOR("Surfaces", Border),
		THEME_COLOR("Surfaces", BorderStrong), THEME_COLOR("Controls / Well", WellTop),
		THEME_COLOR("Controls / Well", WellBottom), THEME_COLOR("Controls / Fill", FillTop),
		THEME_COLOR("Controls / Fill", FillBottom), THEME_COLOR("Controls / Fill", FillTopHover),
		THEME_COLOR("Controls / Fill", FillBottomHover), THEME_COLOR("Sliders", Modified),
		THEME_COLOR("Preview overlays", PreviewDot),
		THEME_COLOR("Menus and dialogs", MenuGround), THEME_COLOR("Menus and dialogs", MenuTint),
		THEME_COLOR("Layers", BadgeTop), THEME_COLOR("Layers", BadgeBottom),
		THEME_COLOR("Layers", BadgeHairline), THEME_COLOR("Inspector", InspectorHairline),
		THEME_COLOR("Preview overlays", OverlayButtonPlate),
		THEME_COLOR("Controls / Well", WellTopHover), THEME_COLOR("Controls / Well", WellBottomHover),
		THEME_COLOR("Controls / Well", WellOutline), THEME_COLOR("Controls / Well", WellOutlineHover),
		THEME_COLOR("Controls / Well", WellEntry),
		THEME_COLOR("Buttons", IconRest), THEME_COLOR("Buttons", IconHover),
		THEME_COLOR("Layers", GroupRowCross)
	};
#undef THEME_COLOR
	return Entries;
}

const TArray<FString>& FMixtormatLiveTheme::Categories()
{
	static const TArray<FString> Entries = []()
	{
		TArray<FString> Result;
		for (const FMixtormatThemeNumber& Entry : Numbers())
		{
			if (Entry.bExposeInUI)
			{
				Result.AddUnique(Entry.Category);
			}
		}
		for (const FMixtormatThemeColor& Entry : Colors())
		{
			if (Entry.bExposeInUI)
			{
				Result.AddUnique(Entry.Category);
			}
		}
		Result.Sort();
		return Result;
	}();
	return Entries;
}

FLinearColor FMixtormatLiveTheme::ResolveColor(const FName Name, const FLinearColor& Default)
{
	const FLinearColor* Override = ColorOverrides().Find(Name);
	return Override ? *Override : Default;
}

bool FMixtormatLiveTheme::SetNumber(const FName Name, const float Value)
{
	const FMixtormatThemeNumber* Entry = Numbers().FindByPredicate(
		[Name](const FMixtormatThemeNumber& Item) { return Item.Name == Name; });
	if (!Entry || !FMath::IsFinite(Value) || Value < Entry->Minimum || Value > Entry->Maximum)
	{
		return false;
	}
	*Entry->Value = Value;
	MixtormatTokens::RecomputeDerived();
	return true;
}

bool FMixtormatLiveTheme::SetColor(const FName Name, const FLinearColor& Value)
{
	if (!IsValidColor(Value) || !Colors().ContainsByPredicate(
		[Name](const FMixtormatThemeColor& Item) { return Item.Name == Name; }))
	{
		return false;
	}
	ColorOverrides().Add(Name, Value);
	return true;
}

void FMixtormatLiveTheme::Reset()
{
	for (const FMixtormatThemeNumber& Entry : Numbers())
	{
		*Entry.Value = Entry.Default;
	}
	ColorOverrides().Reset();
	MixtormatTokens::RecomputeDerived();
}

FString FMixtormatLiveTheme::Serialize()
{
	Initialize();
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Numeric = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Palette = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	for (const FMixtormatThemeNumber& Entry : Numbers())
	{
		Numeric->SetNumberField(Entry.Name.ToString(), *Entry.Value);
	}
	for (const FMixtormatThemeColor& Entry : Colors())
	{
		const FLinearColor Value = ResolveColor(Entry.Name, Entry.Default);
		Palette->SetArrayField(Entry.Name.ToString(), {
			MakeShared<FJsonValueNumber>(Value.R), MakeShared<FJsonValueNumber>(Value.G),
			MakeShared<FJsonValueNumber>(Value.B), MakeShared<FJsonValueNumber>(Value.A)});
	}
	Root->SetObjectField(TEXT("numbers"), Numeric);
	Root->SetObjectField(TEXT("colors"), Palette);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

bool FMixtormatLiveTheme::Deserialize(const FString& Text, FString& Error)
{
	Initialize();
	Error.Reset();
	TSharedPtr<FJsonObject> Root;
	double Version = 0;
	const TSharedPtr<FJsonObject>* Numeric = nullptr;
	const TSharedPtr<FJsonObject>* Palette = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		|| !Root.IsValid() || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1
		|| !Root->TryGetObjectField(TEXT("numbers"), Numeric)
		|| !Root->TryGetObjectField(TEXT("colors"), Palette))
	{
		Error = TEXT("Invalid theme: expected version 1 with numbers and colors objects.");
		return false;
	}

	TMap<FName, float> PendingNumbers;
	TMap<FName, FLinearColor> PendingColors;
	for (const auto& Pair : (*Numeric)->Values)
	{
		const FName Name(*Pair.Key);
		const FMixtormatThemeNumber* Entry = Numbers().FindByPredicate(
			[Name](const FMixtormatThemeNumber& Item) { return Item.Name == Name; });
		double Value = 0;
		if (!Entry || !Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value)
			|| Value < Entry->Minimum || Value > Entry->Maximum)
		{
			Error = FString::Printf(TEXT("Unknown or out-of-range numeric token: %s"), *Pair.Key);
			return false;
		}
		PendingNumbers.Add(Name, static_cast<float>(Value));
	}
	for (const auto& Pair : (*Palette)->Values)
	{
		const FName Name(*Pair.Key);
		const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
		if (!Colors().ContainsByPredicate([Name](const FMixtormatThemeColor& Item) { return Item.Name == Name; })
			|| !Pair.Value->TryGetArray(Channels) || Channels->Num() != 4)
		{
			Error = FString::Printf(TEXT("Unknown color or invalid RGBA array: %s"), *Pair.Key);
			return false;
		}
		float Values[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			double Value = 0;
			if (!(*Channels)[Index]->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value < 0 || Value > 1)
			{
				Error = FString::Printf(TEXT("Color channels must be finite linear values from 0 to 1: %s"), *Pair.Key);
				return false;
			}
			Values[Index] = static_cast<float>(Value);
		}
		PendingColors.Add(Name, FLinearColor(Values[0], Values[1], Values[2], Values[3]));
	}

	// A valid file is a complete override layer: omitted entries take their authored defaults.
	Reset();
	for (const auto& Pair : PendingNumbers)
	{
		SetNumber(Pair.Key, Pair.Value);
	}
	ColorOverrides() = MoveTemp(PendingColors);
	return true;
}

FString FMixtormatLiveTheme::SavePath()
{
	return FMixtormatPaths::LiveThemePath();
}

bool FMixtormatLiveTheme::Save(FString& Error)
{
	Error.Reset();
	const FString Path = SavePath();
	const FString TemporaryPath = Path + TEXT(".tmp");
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
		|| !FFileHelper::SaveStringToFile(Serialize(), *TemporaryPath)
		|| !IFileManager::Get().Move(*Path, *TemporaryPath, true, true))
	{
		Error = FString::Printf(
			TEXT("Could not save the theme. Check the project's Saved/%s folder permissions."),
			*FMixtormatPaths::ProductName().ToString());
		return false;
	}
	return true;
}

bool FMixtormatLiveTheme::Load(FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		Error = FString::Printf(
			TEXT("Could not read Saved/%s/LiveTheme.json. Save a theme first."),
			*FMixtormatPaths::ProductName().ToString());
		return false;
	}
	return Deserialize(Text, Error);
}
