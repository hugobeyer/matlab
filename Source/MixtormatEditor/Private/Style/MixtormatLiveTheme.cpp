// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatLiveTheme.h"

#include "Services/MixtormatPaths.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatGroupButtonTokens.h"
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
#define THEME_NUMBER_UI(Category, Name, Min, Max, Expose) THEME_NUMBER_S_UI(Category, Name, Min, Max, 1.0f, 1, Expose)
#define THEME_NUMBER_S_UI(Category, Name, Min, Max, Step, Precision, Expose) \
	{TEXT(#Name), TEXT(Category), &MixtormatTokens::Name, MixtormatTokens::Name, Min, Max, Step, Precision, Expose}
#define THEME_NUMBER(Category, Name, Min, Max) THEME_NUMBER_UI(Category, Name, Min, Max, true)
	// Sub-pixel and sub-unit tokens carry their own step: a 0..1 opacity nudged by 1.0 can only ever
	// land on its endpoints, and a hairline width nudged by 1.0 can only be 0 or 1.
#define THEME_NUMBER_S(Category, Name, Min, Max, Step, Precision) \
	THEME_NUMBER_S_UI(Category, Name, Min, Max, Step, Precision, true)
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
				THEME_NUMBER_S("Controls / Well", WellShadeTop, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellShadeBottom, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderWidth, 0.0f, 4.0f, 0.25f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderTopOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderBottomOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderHoverOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderHoverTopOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderHoverBottomOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellBorderSaturation, 0.0f, 3.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Well", WellHoverLiftOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Well", WellRadius, 0.0f, 12.0f, 0.5f, 2),
				THEME_NUMBER_S("Controls / Well", ZeroTickOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyTop, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyBottom, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyHoverTop, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyHoverBottom, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyActiveTop, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillBodyActiveBottom, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillDisabledOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillShadeStart, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillShadeMid, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillShadeEnd, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillShadeMidPosition, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Fill", FillFalloffPower, 0.01f, 8.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Fill", FillSaturation, 0.0f, 3.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Fill", FillHoverSaturation, 0.0f, 3.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Fill", FillActiveSaturation, 0.0f, 3.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Fill", FillDisabledSaturation, 0.0f, 3.0f, 0.05f, 2),
				THEME_NUMBER_S("Controls / Text", ControlLabelOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Text", ControlValueOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Text", TextDisabledOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER("Controls / Toggle", ToggleSize, 8.0f, 40.0f),
				THEME_NUMBER_S("Controls / Toggle", ToggleFillInset, 0.0f, 8.0f, 0.5f, 1),
				THEME_NUMBER_S("Controls / Toggle", ToggleDisabledShadeTop, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Controls / Toggle", ToggleDisabledShadeBottom, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER("Controls / Typography", FontControlLabel, 6.0f, 24.0f),
				THEME_NUMBER("Controls / Typography", FontControlValue, 6.0f, 24.0f),
				THEME_NUMBER_S("Controls / Typography", ControlLabelLetterSpacing, 0.0f, 1000.0f, 1.0f, 0),
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
														// Retained for saved LiveTheme compatibility; no production reader remains.
																												THEME_NUMBER_UI("Foldouts", FoldoutChevronInset, 0.0f, 8.0f, false),
								THEME_NUMBER("Foldouts", FoldoutHeaderGap, 0.0f, 24.0f),
						THEME_NUMBER_S("Foldouts", HeaderTintOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", HeaderHoverOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", HairlineHoverOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutFalloffPower, 0.01f, 4.0f, 0.05f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutAccentMultiplyOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutAccentHoverMultiplyOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutHairlineOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutHairlineSaturation, 0.0f, 3.0f, 0.05f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutHairlineHoverSaturation, 0.0f, 3.0f, 0.05f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutSaturation, 0.0f, 3.0f, 0.05f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutHoverSaturation, 0.0f, 3.0f, 0.05f, 2),
						THEME_NUMBER("Foldouts", FontFoldoutTitle, 6.0f, 20.0f),
						THEME_NUMBER_S("Foldouts", FoldoutTitleTracking, 0.0f, 4.0f, 0.1f, 1),
						THEME_NUMBER_S("Foldouts", FoldoutTitleOpacity, 0.0f, 1.0f, 0.01f, 2),
						THEME_NUMBER_S("Foldouts", FoldoutTitleDisabledOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Sliders", ModifiedStripeOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Sliders", TickInsetY, 0.0f, 12.0f, 0.5f, 1),
				THEME_NUMBER_S("Sliders", TickWidth, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER("Rows", SegmentedControlGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", SliderRowGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", RowTextInset, 1.0f, 24.0f),
		THEME_NUMBER("Rows", RowLabelGap, 0.0f, 32.0f),
		THEME_NUMBER("Rows", RowFieldMinWidth, 40.0f, 400.0f),
		THEME_NUMBER_S("Rows", DropdownLabelRatio, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER("Sliders", DraggerTextInset, 0.0f, 24.0f),
		THEME_NUMBER_S("Sliders", ModifiedStripeWidth, 0.0f, 12.0f, 0.5f, 1),
		// No production reader; kept deserializable for existing theme files.
				THEME_NUMBER_UI("Sliders", DragRangeDistance, 80.0f, 1200.0f, false),
		THEME_NUMBER("Sliders", FineDragScale, 0.01f, 1.0f),
		THEME_NUMBER("Sliders", DragThreshold, 0.0f, 16.0f),
		THEME_NUMBER_S("Surfaces", CornerRadius, 0.0f, 12.0f, 0.5f, 1),
		THEME_NUMBER_S("Surfaces", OutlineWidth, 0.0f, 4.0f, 0.25f, 2),
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
		THEME_NUMBER_S("Group Cards", GroupCardFalloffPower, 0.01f, 4.0f, 0.05f, 2),
		THEME_NUMBER_S("Group Cards", GroupCardGradientReach, 0.0f, 128.0f, 1.0f, 1),
		THEME_NUMBER_S("Group Cards", GroupCardHeaderSaturation, 0.0f, 4.0f, 0.05f, 2),
		THEME_NUMBER_S("Group Cards", GroupCardBodySaturation, 0.0f, 4.0f, 0.05f, 2),
		THEME_NUMBER_S("Group Cards", GroupCardHeaderOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Group Cards", GroupCardBodyOpacity, 0.0f, 1.0f, 0.01f, 2),
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
		THEME_NUMBER_S("Group Cards", GroupCardTitleLetterSpacing, 0.0f, 1000.0f, 1.0f, 0),
		THEME_NUMBER("Group Cards", GroupCardTitleHeight, 16.0f, 64.0f),
		// Inspector cards dropped the title drop/ratio; both stay deserializable only.
		THEME_NUMBER_UI("Group Cards", GroupCardTitleDropDepth, 0.0f, 32.0f, false),
		THEME_NUMBER_UI("Group Cards", GroupCardTitleWidthRatio, 0.5f, 0.5f, false),
		THEME_NUMBER("Group Cards", GroupCardLeadingIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Group Cards", GroupCardLeadingGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", HeaderContentGap, 0.0f, 24.0f),
		THEME_NUMBER_S("Buttons", GroupButtonGradientTop, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonGradientBottom, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHoverGradientTop, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHoverGradientBottom, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonSelectedGradientTop, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonSelectedGradientBottom, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonGradientSaturation, 0.0f, 4.0f, 0.05f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHairlineOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHoverHairlineOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonSelectedHairlineOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHairlineWidth, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonHairlineSaturation, 0.0f, 4.0f, 0.05f, 2),
		THEME_NUMBER("Buttons", GroupButtonFontSize, 6.0f, 20.0f),
		THEME_NUMBER_S("Buttons", GroupButtonFontWeight, 100.0f, 900.0f, 100.0f, 0),
		THEME_NUMBER_S("Buttons", GroupButtonTracking, 0.0f, 4.0f, 0.1f, 1),
		THEME_NUMBER("Buttons", GroupButtonHeight, 12.0f, 40.0f),
		THEME_NUMBER_S("Buttons", GroupButtonTextOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Buttons", GroupButtonSeparatorWidth, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER("Buttons", GroupButtonSeparatorHeight, 12.0f, 32.0f),
		THEME_NUMBER_S("Buttons", GroupButtonSeparatorOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER("Buttons", ButtonHeight, 16.0f, 48.0f),
		THEME_NUMBER("Buttons", ButtonPaddingCompact, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingPrimary, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingTab, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", TabWidth, 48.0f, 200.0f),
		THEME_NUMBER("Buttons", TabHeight, 12.0f, 64.0f),
		THEME_NUMBER("Buttons", SegmentHeight, 12.0f, 48.0f),
		THEME_NUMBER("Buttons", ToolbarIconSize, 8.0f, 32.0f),
				THEME_NUMBER("Buttons", PanelToolbarIconSize, 8.0f, 32.0f),
				THEME_NUMBER("Buttons", TopBarIconOpacity, 0.0f, 1.0f),
				THEME_NUMBER("Buttons", GroupButtonPaddingHorizontal, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ToolbarButtonMargin, 0.0f, 24.0f),
		THEME_NUMBER("Buttons", ToolbarLabelPadding, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayInset, 0.0f, 48.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayClusterInset, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayButtonGap, 0.0f, 24.0f),
		THEME_NUMBER_S("Preview overlays", OverlayHoverAccent, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Preview overlays", OverlayPressAccent, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Preview overlays", OverlayIconRestOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER("Inspector", InspectorTopMargin, 0.0f, 48.0f),
		THEME_NUMBER("Inspector", GroupHeaderAlign, 0.0f, 2.0f),
		THEME_NUMBER("Inspector", SubgroupHeaderAlign, 0.0f, 2.0f),
		THEME_NUMBER_S("Inspector", InspectorHairlineThickness, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER("Inspector", InspectorHairlineInset, 0.0f, 64.0f),
		// Kept registered (not deleted) so a theme saved before it became a boolean still
		// validates; hidden because the panel now edits it as a boolean instead.
		THEME_NUMBER_UI("Inspector", InspectorHairlineUnderHeader, 0.0f, 1.0f, false),

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
		THEME_NUMBER_S("Layers", GroupRowCrossStrength, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", GroupAccentStrength, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", GroupAccentSelectedStrength, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", LayerSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", LayerHoverSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", LayerSelectedSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", LayerGroupSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", ChildSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", ChildHoverSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", ChildSelectedSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER_S("Layers", ChildLeftOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", ChildRightOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", ChildHoverLeftOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", ChildHoverRightOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", ChildSelectedLeftOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", ChildSelectedRightOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", LayerActiveHairlineWidth, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER_S("Layers", LayerActiveHairlineOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", LayerActiveGlowOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER("Layers", LayerActiveGlowReach, 0.0f, 128.0f),
		THEME_NUMBER_S("Layers", LayerActiveGlowSaturation, 0.0f, 3.0f, 0.05f, 2),
		THEME_NUMBER("Layers", LayerIconSize, 6.0f, 32.0f),
		THEME_NUMBER_S("Layers", LayerIconOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", LayerVisibilitySize, 0.0f, 32.0f, 0.5f, 1),
		THEME_NUMBER_S("Layers", LayerVisibilityRadius, 0.0f, 16.0f, 0.5f, 1),
		THEME_NUMBER_S("Layers", IconOffOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Foldouts", FoldoutIconOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Foldouts", FoldoutIconHoverOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER("Layers", LayerGroupTitleSize, 6.0f, 24.0f),
		THEME_NUMBER("Layers", LayerGroupTitleWeight, 400.0f, 700.0f),
		THEME_NUMBER_S("Layers", LayerHierarchyLineWidth, 0.0f, 4.0f, 0.25f, 2),
		THEME_NUMBER("Layers", LayerRowHeight, 20.0f, 64.0f),
		THEME_NUMBER("Layers", LayerChildRowHeight, 16.0f, 48.0f),
		THEME_NUMBER("Layers", LayerGroupRowHeight, 16.0f, 48.0f),
		THEME_NUMBER("Layers", LayerThumbnailSize, 12.0f, 48.0f),
		THEME_NUMBER("Layers", LayerChildIndent, 0.0f, 80.0f),
		THEME_NUMBER("Layers", LayerScopeIndent, 0.0f, 48.0f),
		THEME_NUMBER("Layers", LayerChildIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", LayerEyeSize, 8.0f, 32.0f),
		THEME_NUMBER("Layers", ChevronSize, 8.0f, 32.0f),
		THEME_NUMBER_S("Layers", LayerConnectorOpacity, 0.0f, 1.0f, 0.01f, 2),
		THEME_NUMBER_S("Layers", BadgeCornerRadius, 0.0f, 8.0f, 0.5f, 1),
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
				THEME_NUMBER_S("Menus and dialogs", MenuIconOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER("Menus and dialogs", HelpMaxWidth, 120.0f, 640.0f),
				THEME_NUMBER_S("Menus and dialogs", HelpPadding, 0.0f, 32.0f, 0.5f, 1),
				THEME_NUMBER_S("Menus and dialogs", HelpBodyOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER_S("Menus and dialogs", HelpDelay, 0.0f, 2.0f, 0.05f, 2),
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
				THEME_NUMBER("Typography", FontMenuShortcut, 6.0f, 24.0f),
				THEME_NUMBER_S("Typography", ShortcutTextOpacity, 0.0f, 1.0f, 0.01f, 2),
				THEME_NUMBER("Typography", FontDragGhostLabel, 6.0f, 24.0f),
				THEME_NUMBER("Typography", FontSliderLabel, 6.0f, 24.0f)
			};
			#undef THEME_NUMBER
			#undef THEME_NUMBER_S
			#undef THEME_NUMBER_UI
			#undef THEME_NUMBER_S_UI
			return Entries;
		}

		const TArray<FMixtormatThemeBool>& FMixtormatLiveTheme::Booleans()
		{
		#define THEME_BOOL(Category, Name) \
			{TEXT(#Name), TEXT(Category), &MixtormatTokens::Name, MixtormatTokens::Name, true}
			static const TArray<FMixtormatThemeBool> Entries = {
				THEME_BOOL("Typography", ControlLabelBold),
				THEME_BOOL("Typography", ControlValueBold),
				THEME_BOOL("Typography", CardTitleBold),
				THEME_BOOL("Typography", GroupHeaderBold),
				THEME_BOOL("Typography", FoldoutTitleBold),
				THEME_BOOL("Group Cards", GroupCardTitleBold),
			};
		#undef THEME_BOOL
			return Entries;
		}

		const TArray<FMixtormatThemeChoice>& FMixtormatLiveTheme::Choices()
		{
		#define THEME_CHOICE(Category, Name, Default, ...) \
			{TEXT(#Name), TEXT(Category), &MixtormatTokens::Name, \
				TArray<FString>{ __VA_ARGS__ }, Default, true}
		#define THEME_BLEND_CHOICE(Category, Name, Default) \
			THEME_CHOICE(Category, Name, Default, TEXT("Normal"), TEXT("Additive / Plus Lighter"), \
				TEXT("Multiply"), TEXT("Soft Light"))
			// Blend indices match MixtormatCompositing::EMixtormatBlendMode, and each default is the value
			// tokens.css authors for that surface -- not a house style.
			static const TArray<FMixtormatThemeChoice> Entries = {
				THEME_BLEND_CHOICE("Surfaces", SurfaceBlendMode, 1),
				THEME_BLEND_CHOICE("Controls / Well", WellBlendMode, 2),
				THEME_BLEND_CHOICE("Foldouts", FoldoutBlendMode, 0),
				THEME_BLEND_CHOICE("Foldouts", FoldoutAccentBlendMode, 3),
				THEME_BLEND_CHOICE("Group Cards", CardBlendMode, 1),
				THEME_BLEND_CHOICE("Buttons", GroupButtonBlendMode, 0),
				THEME_BLEND_CHOICE("Layers", LayerBlendMode, 0),
				THEME_BLEND_CHOICE("Layers", LayerGroupBlendMode, 3),
				THEME_CHOICE("Typography", FontFamily, 0, TEXT("Inter"), TEXT("Roboto")),
				THEME_CHOICE("Controls / Text", DraggerLabelCase, 0, TEXT("None"), TEXT("Uppercase")),
			};
		#undef THEME_BLEND_CHOICE
		#undef THEME_CHOICE
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
		for (const FMixtormatThemeBool& Entry : Booleans())
		{
			if (Entry.bExposeInUI)
			{
				Result.AddUnique(Entry.Category);
			}
		}
		for (const FMixtormatThemeChoice& Entry : Choices())
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

bool FMixtormatLiveTheme::SetBool(const FName Name, const bool Value)
{
	const FMixtormatThemeBool* Entry = Booleans().FindByPredicate(
		[Name](const FMixtormatThemeBool& Item) { return Item.Name == Name; });
	if (!Entry)
	{
		return false;
	}
	*Entry->Value = Value;
	MixtormatTokens::RecomputeDerived();
	return true;
}

bool FMixtormatLiveTheme::SetChoice(const FName Name, const int32 Value)
{
	const FMixtormatThemeChoice* Entry = Choices().FindByPredicate(
		[Name](const FMixtormatThemeChoice& Item) { return Item.Name == Name; });
	if (!Entry || !Entry->Options.IsValidIndex(Value))
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
	for (const FMixtormatThemeBool& Entry : Booleans())
	{
		*Entry.Value = Entry.Default;
	}
	for (const FMixtormatThemeChoice& Entry : Choices())
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
	const TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> ChoicesObject = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	for (const FMixtormatThemeNumber& Entry : Numbers())
	{
		Numeric->SetNumberField(Entry.Name.ToString(), *Entry.Value);
	}
	// Booleans and choices are new object sections rather than more numeric fields: a weight switch
	// stored as 1.0 is indistinguishable from a real quantity, which is what made this worth fixing.
	// Version stays 1 because both sections are optional on read -- a document without them is still
	// a complete, valid version 1 theme.
	for (const FMixtormatThemeBool& Entry : Booleans())
	{
		Flags->SetBoolField(Entry.Name.ToString(), *Entry.Value);
	}
	for (const FMixtormatThemeChoice& Entry : Choices())
	{
		const int32 Index = Entry.Options.IsValidIndex(*Entry.Value) ? *Entry.Value : Entry.Default;
		ChoicesObject->SetStringField(Entry.Name.ToString(), Entry.Options[Index]);
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
	Root->SetObjectField(TEXT("booleans"), Flags);
	Root->SetObjectField(TEXT("choices"), ChoicesObject);
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
	const TSharedPtr<FJsonObject>* Flags = nullptr;
	const TSharedPtr<FJsonObject>* ChoicesObject = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		|| !Root.IsValid() || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1
		|| !Root->TryGetObjectField(TEXT("numbers"), Numeric)
		|| !Root->TryGetObjectField(TEXT("colors"), Palette))
	{
		Error = TEXT("Invalid theme: expected version 1 with numbers and colors objects.");
		return false;
	}
	// "booleans" and "choices" are optional. A theme saved before either existed is still a complete
	// document: the omitted sections simply leave every entry on its authored default, which is what
	// Reset below does before the pending values are applied. This is why the version is not bumped.
	Root->TryGetObjectField(TEXT("booleans"), Flags);
	Root->TryGetObjectField(TEXT("choices"), ChoicesObject);

	TMap<FName, float> PendingNumbers;
	TMap<FName, bool> PendingBools;
	TMap<FName, int32> PendingChoices;
	TMap<FName, FLinearColor> PendingColors;
	for (const auto& Pair : (*Numeric)->Values)
	{
		const FName Name(*Pair.Key);
		const FMixtormatThemeNumber* Entry = Numbers().FindByPredicate(
			[Name](const FMixtormatThemeNumber& Item) { return Item.Name == Name; });
		double Value = 0;
		if (!Entry)
		{
			// Compatibility path: these tokens used to be numeric weight switches, so a theme saved
			// before they became booleans carries them here as 0.0/1.0. Anything other than a clean
			// 0 or 1 is a real mistake rather than a legacy boolean, and is still rejected.
			const FMixtormatThemeBool* Flag = Booleans().FindByPredicate(
				[Name](const FMixtormatThemeBool& Item) { return Item.Name == Name; });
			if (Flag && Pair.Value->TryGetNumber(Value)
				&& (FMath::IsNearlyEqual(Value, 0.0) || FMath::IsNearlyEqual(Value, 1.0)))
			{
				PendingBools.Add(Name, FMath::IsNearlyEqual(Value, 1.0));
				continue;
			}
			Error = FString::Printf(TEXT("Unknown or out-of-range numeric token: %s"), *Pair.Key);
			return false;
		}
		if (!Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value)
			|| Value < Entry->Minimum || Value > Entry->Maximum)
		{
			Error = FString::Printf(TEXT("Unknown or out-of-range numeric token: %s"), *Pair.Key);
			return false;
		}
		PendingNumbers.Add(Name, static_cast<float>(Value));
	}
	if (Flags)
	{
		for (const auto& Pair : (*Flags)->Values)
		{
			const FName Name(*Pair.Key);
			bool Value = false;
			if (!Booleans().ContainsByPredicate(
					[Name](const FMixtormatThemeBool& Item) { return Item.Name == Name; })
				|| !Pair.Value->TryGetBool(Value))
			{
				Error = FString::Printf(TEXT("Unknown boolean token: %s"), *Pair.Key);
				return false;
			}
			PendingBools.Add(Name, Value);
		}
	}
	if (ChoicesObject)
	{
		for (const auto& Pair : (*ChoicesObject)->Values)
		{
			const FName Name(*Pair.Key);
			const FMixtormatThemeChoice* Entry = Choices().FindByPredicate(
				[Name](const FMixtormatThemeChoice& Item) { return Item.Name == Name; });
			FString Value;
			const int32 Index = Entry ? Entry->FindOption(Value) : INDEX_NONE;
			if (!Entry || !Pair.Value->TryGetString(Value) || Index == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("Unknown choice token or invalid value: %s"), *Pair.Key);
				return false;
			}
			PendingChoices.Add(Name, Index);
		}
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
	for (const auto& Pair : PendingBools)
	{
		SetBool(Pair.Key, Pair.Value);
	}
	for (const auto& Pair : PendingChoices)
	{
		SetChoice(Pair.Key, Pair.Value);
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
