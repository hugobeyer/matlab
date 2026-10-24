// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatChildScope.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SMixtormat"


void SMixtormat::AddNativeFlowControls(const TSharedRef<SVerticalBox>& Cards)
{
	const auto Settings = [this]() -> FMixtormatNativeFlowSettings*
	{
		FMixtormatGenerator* G = GetSelectedGenerator();
		return G && (G->Type == EMixtormatGeneratorType::Cracks
			|| G->Type == EMixtormatGeneratorType::RockFormation
			|| G->Type == EMixtormatGeneratorType::Pebbles
			|| G->Type == EMixtormatGeneratorType::RootNetwork
			|| G->Type == EMixtormatGeneratorType::Scales
			|| G->Type == EMixtormatGeneratorType::CliffStrata)
			? &G->NativeFlow : nullptr;
	};
	const FMixtormatNativeFlowSettings Defaults;
	const TSharedRef<SVerticalBox> Card = AddCard(Cards, LOCTEXT("NativeFlowCard", "NATIVE FLOW"));
	AddSliderRow(Card, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatNativeFlowSettings>(
			LOCTEXT("NativeFlowAmount", "Amount"), Settings, &FMixtormatNativeFlowSettings::Amount,
			0.0, 2.0, Defaults.Amount, 0.01, LOCTEXT("NativeFlowAmountHint", "Native vector magnitude; zero disables its influence without affecting height or IDs.")),
		MakeMemberSlider<FMixtormatNativeFlowSettings>(
			LOCTEXT("NativeFlowAngle", "Tilt"), Settings, &FMixtormatNativeFlowSettings::Angle,
			-180.0, 180.0, Defaults.Angle, 0.5, LOCTEXT("NativeFlowAngleHint", "Rotate the final directional field in degrees."))));
	const FMixtormatGenerator* Selected = GetSelectedGenerator();
	if (Selected && !MixtormatGeneratorHasFlowBoundary(Selected->Type))
	{
		AddSliderRow(Card, MakeMemberSlider<FMixtormatNativeFlowSettings>(
			LOCTEXT("NativeFlowSurface", "Surface"), Settings, &FMixtormatNativeFlowSettings::BoundaryInfluence,
			0.0, 1.0, Defaults.BoundaryInfluence, 0.01, LOCTEXT("NativeFlowSurfaceHint", "Height-gradient contribution on generators without signed boundaries.")));
	}
	else
	{
		AddSliderRow(Card, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNativeFlowSettings>(
				LOCTEXT("NativeFlowSlope", "Slope"), Settings, &FMixtormatNativeFlowSettings::SlopeInfluence,
				0.0, 1.0, Defaults.SlopeInfluence, 0.01, LOCTEXT("NativeFlowSlopeHint", "Blend native signed-height downhill direction into the boundary flow.")),
			MakeMemberSlider<FMixtormatNativeFlowSettings>(
				LOCTEXT("NativeFlowBoundary", "Boundary"), Settings, &FMixtormatNativeFlowSettings::BoundaryInfluence,
				0.0, 1.0, Defaults.BoundaryInfluence, 0.01, LOCTEXT("NativeFlowBoundaryHint", "Weight the generator's signed-boundary direction; invalid boundary stencils remain inert."))));
	}
	AddSliderRow(Card, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatNativeFlowSettings>(
			LOCTEXT("NativeFlowGravity", "Gravity"), Settings, &FMixtormatNativeFlowSettings::GravityInfluence,
			0.0, 1.0, Defaults.GravityInfluence, 0.01, LOCTEXT("NativeFlowGravityHint", "Mix an independent directional bias into the geometry-derived flow.")),
		MakeMemberSlider<FMixtormatNativeFlowSettings>(
			LOCTEXT("NativeFlowGravityAngle", "Gravity Angle"), Settings, &FMixtormatNativeFlowSettings::GravityAngle,
			-180.0, 180.0, Defaults.GravityAngle, 0.5, LOCTEXT("NativeFlowGravityAngleHint", "Direction of the gravity bias, in degrees."))));
}

TSharedRef<SWidget> SMixtormat::BuildCracksControls()
{
	const auto Crack = [this]() { return GetSelectedCracks(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("CrackOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("CrackNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Crack]()
				{
					const FMixtormatCracks* G = Crack();
					return G && G->bCrackNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Crack](const ECheckBoxState State)
				{
					if (FMixtormatCracks* G = Crack())
					{
						G->bCrackNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("CrackNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5.")));
		AddSliderRow(Output, MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackHeightScale", "Scale"), Crack, &FMixtormatCracks::CrackHeightScale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("CrackHeightScaleHint", "Scales the signed generator height after normalization.")));
	}

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCracks>(
			LOCTEXT("CrackCells", "Cells"), Crack, &FMixtormatCracks::CrackCells, 1.0, 32.0, 7,
			LOCTEXT("CrackCellsHint", "Pieces per row; the tile is Cells x Cells. Every length and depth below is relative to one cell.")),
		MakeMemberSliderInt<FMixtormatCracks>(
			LOCTEXT("CrackSeed", "Seed"), Crack, &FMixtormatCracks::CrackSeed, 0.0, 9999.0, 1,
			LOCTEXT("CrackSeedHint", "Changes the network, its roughness and every piece."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackJitter", "Jitter"), Crack, &FMixtormatCracks::CrackJitter, 0.0, 1.0, 0.85, 0.01,
			LOCTEXT("CrackJitterHint", "How irregular the pieces are. 0 is a square grid.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidth", "Width"), Crack, &FMixtormatCracks::CrackWidth, 0.0, 0.5, 0.1, 0.005,
			LOCTEXT("CrackWidthHint", "Crack width, in cell widths."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatCracks>(
		LOCTEXT("CrackDepth", "Depth"), Crack, &FMixtormatCracks::CrackDepth, 0.0, 2.0, 0.415, 0.01,
		LOCTEXT("CrackDepthHint", "Groove depth at the base width, relative to the cell. Wider cracks cut deeper.")));

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("CrackGrpShape", "Shape"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackRough", "Rough"), Crack, &FMixtormatCracks::CrackRough, 0.0, 1.0, 0.361, 0.01,
			LOCTEXT("CrackRoughHint", "How angular the crack lines are. Scale does not change it.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackScale", "Scale"), Crack, &FMixtormatCracks::CrackScale, 0.5, 16.0, 7.8, 0.05,
			LOCTEXT("CrackScaleHint", "Bends per cell width along a crack."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackDetail", "Detail"), Crack, &FMixtormatCracks::CrackDetail, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("CrackDetailHint", "How much finer texture rides on the bends.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackFeather", "Feather"), Crack, &FMixtormatCracks::CrackFeather, 0.0, 1.0, 0.176, 0.01,
			LOCTEXT("CrackFeatherHint", "Small gives fault-like kinks, large gives soft bends."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpWidth", "Width Variation"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackLineVariation", "Per Crack"), Crack, &FMixtormatCracks::CrackLineVariation, 0.0, 1.0, 0.541, 0.01,
			LOCTEXT("CrackLineVariationHint", "Every crack its own width, from hairline to wide.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackRegionVariation", "Regional"), Crack, &FMixtormatCracks::CrackRegionVariation, 0.0, 1.0, 0.615, 0.01,
			LOCTEXT("CrackRegionVariationHint", "Whole regions wider or thinner."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidthVariation", "Along"), Crack, &FMixtormatCracks::CrackWidthVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("CrackWidthVariationHint", "Fine wobble along each crack.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidthScale", "Along Scale"), Crack, &FMixtormatCracks::CrackWidthScale, 0.5, 32.0, 6.44, 0.1,
			LOCTEXT("CrackWidthScaleHint", "Wobbles per cell width along a crack."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpRim", "Rims and Gaps"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChip", "Chips"), Crack, &FMixtormatCracks::CrackChip, 0.0, 1.0, 0.3, 0.01,
			LOCTEXT("CrackChipHint", "Share of the rim carrying chips. Each side of a crack chips on its own.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChipSize", "Chip Size"), Crack, &FMixtormatCracks::CrackChipSize, 0.0, 0.5, 0.12, 0.005,
			LOCTEXT("CrackChipSizeHint", "Chip size, in cell widths."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackGap", "Flat Floors"), Crack, &FMixtormatCracks::CrackGap, 0.0, 1.0, 0.15, 0.01,
			LOCTEXT("CrackGapHint", "Chance of a flat floor in the scalar crack height profile; does not create transparency.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackGapWidth", "Floor Width"), Crack, &FMixtormatCracks::CrackGapWidth, 0.0, 4.0, 1.5, 0.05,
			LOCTEXT("CrackGapWidthHint", "Flat floor width relative to the crack width, within the same height profile."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpPieces", "Pieces"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackSlip", "Slip"), Crack, &FMixtormatCracks::CrackSlip, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("CrackSlipHint", "Every piece rises or sinks by its own amount, independently of crack width.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackTilt", "Tilt"), Crack, &FMixtormatCracks::CrackTilt, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("CrackTiltHint", "Every piece tips its own random way."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpChamfer", "Chamfer"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChamferAmount", "Amount"), Crack, &FMixtormatCracks::CrackChamferAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CrackChamferAmountHint", "Shapes the negative groove wall profile without cutting the piece base or changing the flat floor.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChamferEdge", "Edge Reach"), Crack, &FMixtormatCracks::CrackChamferEdge, 0.0, 0.5, 0.12, 0.001,
			LOCTEXT("CrackChamferEdgeHint", "Wall-profile transition distance in cell widths; independent of crack width, with no built-in propagation noise."))));

	AddNativeFlowControls(Cards);

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCracks() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CracksHeading", "CRACKS")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::Cracks;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGenerator* Generator = GetSelectedGenerator())
							{
								Generator->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("CracksEnabledHint", "Enable these cracks"))
				])
			[
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildFinalSettingsControls()
{
	const auto Final = [this]() -> FMixtormatFinalSettings*
	{
		return bHasWorkingMaterial ? &WorkingFinalSettings : nullptr;
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatFinalSettings>(
			LOCTEXT("FinalAOAmount", "AO"), Final, &FMixtormatFinalSettings::HeightAOAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("FinalAOAmountHint", "Ambient occlusion from the final height, applied once after every layer. Layers and effects never write AO.")),
		MakeMemberSlider<FMixtormatFinalSettings>(
			LOCTEXT("FinalAORadius", "AO Radius"), Final, &FMixtormatFinalSettings::HeightAORadius, 1.0, 64.0, 8.0, 0.5,
			LOCTEXT("FinalAORadiusHint", "How far the occlusion reaches, in pixels at 1024. Scales with resolution."))));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatFinalSettings>(
		LOCTEXT("FinalAutoRemapHeight", "Auto Remap Height"), Final, &FMixtormatFinalSettings::bAutoRemapHeight,
		LOCTEXT("FinalAutoRemapHeightHint", "Remap the composite below the first enabled height-writing Skin/Moss layer to 0-1 before surfacing. Skin/Moss and later layers keep their relief scale, so the final height may exceed 0-1. With no height-writing surfacing, remap the finished stack.")));
	const Mixtormat::FMixtormatPreviewMetrics& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
	return SNew(SMixtormatMenuPanel)
		.MinWidth(Layout.FinalPopupWidth)
		.Padding(FMargin(Layout.OverlayClusterInset))
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
			[
				Panel
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildBooleanStackControls()
{
	const auto B = [this]() { return GetSelectedBooleanStack(); };
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	// Operation and edge style are also on the row badge. A badge is a good place to read a value
	// and a poor place to discover one, so the same two choices are segmented here as well.
	TSharedRef<SVerticalBox> Operation = AddCard(Cards, LOCTEXT("BooleanStackOperation", "OPERATION"));
	AddSliderRow(Operation, MixtormatRow::MakeTrailing(
		LOCTEXT("BooleanStackOpLabel", "Boolean"),
		SNew(SMixtormatSegmentedControl)
		.Options(TArray<FText>{
			LOCTEXT("BooleanStackOpUnion", "Union"),
			LOCTEXT("BooleanStackOpSubtract", "Subtract"),
			LOCTEXT("BooleanStackOpIntersect", "Intersect")})
		.ToolTips(TArray<FText>{
			LOCTEXT("BooleanStackOpUnionHint", "Keeps the higher of the two signed surfaces."),
			LOCTEXT("BooleanStackOpSubtractHint", "Removes the source's positive relief from the target."),
			LOCTEXT("BooleanStackOpIntersectHint", "Keeps the lower of the two signed surfaces.")})
		.ActiveIndex_Lambda([B]() -> int32
		{
			const FMixtormatBooleanStack* Selected = B();
			if (!Selected) { return 0; }
			switch (Selected->Operation)
			{
			case EMixtormatBooleanFieldOperation::Subtract: return 1;
			case EMixtormatBooleanFieldOperation::Intersect: return 2;
			default: return 0;
			}
		})
		.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, B](const int32 Index)
		{
			FMixtormatBooleanStack* Selected = B();
			if (!Selected) { return; }
			const EMixtormatBooleanFieldOperation Op =
				Index == 1 ? EMixtormatBooleanFieldOperation::Subtract :
				Index == 2 ? EMixtormatBooleanFieldOperation::Intersect :
				EMixtormatBooleanFieldOperation::Union;
			if (Selected->Operation == Op) { return; }
			Selected->Operation = Op;
			LastHistoryRecordTime = 0.0;
			RefreshLayeredPreview();
			RebuildLayerList();
		}))));
	AddSliderRow(Operation, MixtormatRow::MakeTrailing(
		LOCTEXT("BooleanStackEdgeLabel", "Edge"),
		SNew(SMixtormatSegmentedControl)
		.Options(TArray<FText>{
			LOCTEXT("BooleanStackEdgeHard", "Hard"),
			LOCTEXT("BooleanStackEdgeRound", "Round"),
			LOCTEXT("BooleanStackEdgeBevel", "Bevel")})
		.ToolTips(TArray<FText>{
			LOCTEXT("BooleanStackEdgeHardHint", "Sharp seam. Join Width does not apply."),
			LOCTEXT("BooleanStackEdgeRoundHint", "Rounded join of the given width."),
			LOCTEXT("BooleanStackEdgeBevelHint", "Straight-slope join of the given width.")})
		.ActiveIndex_Lambda([B]() -> int32
		{
			const FMixtormatBooleanStack* Selected = B();
			if (!Selected) { return 0; }
			switch (Selected->EdgeStyle)
			{
			case EMixtormatBooleanEdgeStyle::Round: return 1;
			case EMixtormatBooleanEdgeStyle::Bevel: return 2;
			default: return 0;
			}
		})
		.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, B](const int32 Index)
		{
			FMixtormatBooleanStack* Selected = B();
			if (!Selected) { return; }
			const EMixtormatBooleanEdgeStyle Style =
				Index == 1 ? EMixtormatBooleanEdgeStyle::Round :
				Index == 2 ? EMixtormatBooleanEdgeStyle::Bevel :
				EMixtormatBooleanEdgeStyle::Hard;
			if (Selected->EdgeStyle == Style) { return; }
			Selected->EdgeStyle = Style;
			LastHistoryRecordTime = 0.0;
			RefreshLayeredPreview();
			RebuildLayerList();
		}))));

	TSharedRef<SVerticalBox> Target = AddCard(Cards, LOCTEXT("BooleanStackTarget", "TARGET"));
	AddSliderRow(Target, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackTargetX", "Offset X"), B, &FMixtormatBooleanStack::TargetOffsetX,
			-2.0, 2.0, 0.0, 0.005,
			LOCTEXT("BooleanStackTargetXHint", "Periodic target offset in tile UV X.")),
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackTargetY", "Offset Y"), B, &FMixtormatBooleanStack::TargetOffsetY,
			-2.0, 2.0, 0.0, 0.005,
			LOCTEXT("BooleanStackTargetYHint", "Periodic target offset in tile UV Y."))));
	AddSliderRow(Target, MakeMemberSlider<FMixtormatBooleanStack>(
		LOCTEXT("BooleanStackTargetHeight", "Height Offset"), B, &FMixtormatBooleanStack::TargetHeightOffset,
		-2.0, 2.0, 0.0, 0.005,
		LOCTEXT("BooleanStackTargetHeightHint", "Move target in signed height units.")));

	TSharedRef<SVerticalBox> Source = AddCard(Cards, LOCTEXT("BooleanStackSource", "SOURCE"));
	AddSliderRow(Source, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackSourceX", "Offset X"), B, &FMixtormatBooleanStack::SourceOffsetX,
			-2.0, 2.0, 0.0, 0.005,
			LOCTEXT("BooleanStackSourceXHint", "Periodic source offset in tile UV X.")),
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackSourceY", "Offset Y"), B, &FMixtormatBooleanStack::SourceOffsetY,
			-2.0, 2.0, 0.0, 0.005,
			LOCTEXT("BooleanStackSourceYHint", "Periodic source offset in tile UV Y."))));
	AddSliderRow(Source, MakeMemberSlider<FMixtormatBooleanStack>(
		LOCTEXT("BooleanStackSourceHeight", "Height Offset"), B, &FMixtormatBooleanStack::SourceHeightOffset,
		-2.0, 2.0, 0.0, 0.005,
		LOCTEXT("BooleanStackSourceHeightHint", "Move source in signed height units after scale.")));

	TSharedRef<SVerticalBox> Shaping = AddCard(Cards, LOCTEXT("BooleanStackShaping", "SHAPING"));
	AddSliderRow(Shaping, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackScale", "Source Scale"), B, &FMixtormatBooleanStack::SourceScale,
			-4.0, 4.0, 1.0, 0.01,
			LOCTEXT("BooleanStackScaleHint", "Signed source relief gain; negative values flip relief.")),
		MakeMemberSlider<FMixtormatBooleanStack>(
			LOCTEXT("BooleanStackAmount", "Amount"), B, &FMixtormatBooleanStack::Amount,
			0.0, 1.0, 1.0, 0.01,
			LOCTEXT("BooleanStackAmountHint", "Blend the combined field against the target."))));
	AddSliderRow(Shaping, SNew(SBox)
		.Visibility_Lambda([B]()
		{
			const FMixtormatBooleanStack* Selected = B();
			return Selected && Selected->EdgeStyle != EMixtormatBooleanEdgeStyle::Hard
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MakeMemberSlider<FMixtormatBooleanStack>(
				LOCTEXT("BooleanStackWidth", "Join Width"), B, &FMixtormatBooleanStack::JoinWidth,
				0.0, 1.0, 0.1, 0.005,
				LOCTEXT("BooleanStackWidthHint", "Round or bevel width in signed height units."))
		]);
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedBooleanStack() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("BooleanStackHeading", "BOOLEAN"))
			.PresetAction(MakeModulePresetButton())
			[Cards]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBooleanOpsControls()
{
	const auto B = [this]() { return GetSelectedBooleanOps(); };
    TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
    TSharedRef<SVerticalBox> ScatterCards = SNew(SVerticalBox);
    Cards->AddSlot().AutoHeight()
    [
        ScatterCards
    ];
	{
		TSharedRef<SVerticalBox> Scatter = AddCard(ScatterCards, LOCTEXT("BooleanScatter", "SCATTER CARVE"));
		AddSliderRow(Scatter, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatBooleanOps>(
				LOCTEXT("BooleanSeed", "Seed"), B, &FMixtormatBooleanOps::Seed, 0, 9999, 1,
				LOCTEXT("BooleanSeedHint", "Repeatable cutter distribution.")),
			MakeMemberSliderInt<FMixtormatBooleanOps>(
				LOCTEXT("BooleanCount", "Count"), B, &FMixtormatBooleanOps::Count, 0, 256, 16,
				LOCTEXT("BooleanCountHint", "Independent cutters; large counts are GPU-expensive."))));
		AddSliderRow(Scatter, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanRadius", "Radius"), B, &FMixtormatBooleanOps::Radius, 0.0, 0.5, 0.06, 0.001,
				LOCTEXT("BooleanRadiusHint", "Cutter radius in tile UV units.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanRadiusVariation", "Radius Var"), B, &FMixtormatBooleanOps::RadiusVariation, 0.0, 1.0, 0.5, 0.01,
				LOCTEXT("BooleanRadiusVariationHint", "Per-cutter radius reduction."))));
	}
	{
		TSharedRef<SVerticalBox> Shape = AddCard(ScatterCards, LOCTEXT("BooleanShape", "GEOMETRY"));
		AddSliderRow(Shape, MakeMemberEnum<FMixtormatBooleanOps>(
			LOCTEXT("BooleanOperation", "Boolean"), B, &FMixtormatBooleanOps::Operation,
			LOCTEXT("BooleanOperationHint", "Subtract cuts the existing solid; Union adds the cutter.")));
		AddSliderRow(Shape, MakeMemberEnum<FMixtormatBooleanOps>(
			LOCTEXT("BooleanShapeType", "Shape"), B, &FMixtormatBooleanOps::Shape,
			LOCTEXT("BooleanShapeTypeHint", "Ellipsoid or a convex polygon prism.")));
		AddSliderRow(Shape, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatBooleanOps>(
				LOCTEXT("BooleanSides", "Sides"), B, &FMixtormatBooleanOps::Sides, 3, 32, 7,
				LOCTEXT("BooleanSidesHint", "Polygon side count.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanIrregularity", "Irregularity"), B, &FMixtormatBooleanOps::Irregularity, 0.0, 1.0, 0.25, 0.01,
				LOCTEXT("BooleanIrregularityHint", "Randomized polygon support planes."))));
		AddSliderRow(Shape, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanRotation", "Rotation"), B, &FMixtormatBooleanOps::Rotation, -180.0, 180.0, 0.0, 1.0,
				LOCTEXT("BooleanRotationHint", "Angle in degrees.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanRotationVar", "Rotation Var"), B, &FMixtormatBooleanOps::RotationVariation, 0.0, 180.0, 180.0, 1.0,
				LOCTEXT("BooleanRotationVarHint", "Symmetric random rotation in degrees."))));
	}
	{
		TSharedRef<SVerticalBox> Height = AddCard(ScatterCards, LOCTEXT("BooleanHeight", "DEPTH"));
		AddSliderRow(Height, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanCenterDepth", "Center Depth"), B, &FMixtormatBooleanOps::CenterDepth, -1.0, 1.0, 0.02, 0.005,
				LOCTEXT("BooleanCenterDepthHint", "Positive embeds the cutters; negative forces union, as in the prototype.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanDepthVar", "Depth Var"), B, &FMixtormatBooleanOps::DepthVariation, 0.0, 0.5, 0.08, 0.005,
				LOCTEXT("BooleanDepthVarHint", "Seed-centered depth variation."))));
		AddSliderRow(Height, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanRefHeight", "Reference Height"), B, &FMixtormatBooleanOps::ReferenceHeight, -1.0, 1.0, 0.0, 0.005,
				LOCTEXT("BooleanRefHeightHint", "Offset from the existing surface at each seed.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanDepthScale", "Depth Scale"), B, &FMixtormatBooleanOps::DepthScale, 0.0, 8.0, 1.0, 0.01,
				LOCTEXT("BooleanDepthScaleHint", "Vertical cutter radius relative to its UV radius."))));
	}
	{
		TSharedRef<SVerticalBox> Chamfer = AddCard(ScatterCards, LOCTEXT("BooleanChamfer", "CHAMFER"));
		AddSliderRow(Chamfer, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanChamferFraction", "Width"), B, &FMixtormatBooleanOps::ChamferFraction, 0.0, 1.0, 0.2, 0.005,
				LOCTEXT("BooleanChamferFractionHint", "Chamfer reach relative to each cutter radius.")),
			MakeMemberSlider<FMixtormatBooleanOps>(
				LOCTEXT("BooleanChamferVar", "Variation"), B, &FMixtormatBooleanOps::ChamferVariation, 0.0, 1.0, 0.5, 0.01,
				LOCTEXT("BooleanChamferVarHint", "Per-cutter chamfer width variation."))));
		AddSliderRow(Chamfer, MakeMemberSlider<FMixtormatBooleanOps>(
			LOCTEXT("BooleanHeightToUV", "Height / UV"), B, &FMixtormatBooleanOps::HeightToUV, 0.01, 4.0, 1.0, 0.01,
			LOCTEXT("BooleanHeightToUVHint", "Raw signed height units converted to tile UV distance for the chamfer solve.")));
	}
	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedBooleanOps() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("BooleanOpsHeading", "SCATTER CARVE")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
					FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::BooleanOps;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State)
						{
							if (FMixtormatGenerator* Generator = GetSelectedGenerator())
							{
								Generator->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("BooleanEnabledHint", "Enable Boolean Ops"))
				])
			[
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildPebblesControls()
{
	const auto Pebble = [this]() { return GetSelectedPebbles(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("PebbleOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("PebbleNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Pebble]()
				{
					const FMixtormatPebbles* G = Pebble();
					return G && G->bPebbleNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Pebble](const ECheckBoxState State)
				{
					if (FMixtormatPebbles* G = Pebble())
					{
						G->bPebbleNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("PebbleNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5.")));
		AddSliderRow(Output, MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleHeightScale", "Scale"), Pebble, &FMixtormatPebbles::PebbleHeightScale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("PebbleHeightScaleHint", "Scales the signed generator height after normalization.")));
	}

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleCells", "Cells"), Pebble, &FMixtormatPebbles::PebbleCells, 1.0, 32.0, 4,
			LOCTEXT("PebbleCellsHint", "Stones per row; the tile is Cells x Cells.")),
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleSeed", "Seed"), Pebble, &FMixtormatPebbles::PebbleSeed, 0.0, 9999.0, 1,
			LOCTEXT("PebbleSeedHint", "Changes placement, shape and height of every stone."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleDensity", "Density"), Pebble, &FMixtormatPebbles::PebbleDensity, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("PebbleDensityHint", "Chance a cell has a stone.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleJitter", "Jitter"), Pebble, &FMixtormatPebbles::PebbleJitter, 0.0, 1.0, 0.7, 0.01,
			LOCTEXT("PebbleJitterHint", "Position randomness inside the cell."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleScale", "Scale"), Pebble, &FMixtormatPebbles::PebbleScale, 0.1, 2.0, 1.1, 0.01,
			LOCTEXT("PebbleScaleHint", "Stone size; 1 fills a cell.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleScaleVariation", "Scale Var"), Pebble, &FMixtormatPebbles::PebbleScaleVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PebbleScaleVariationHint", "Random size spread per stone."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleRotation", "Rotation"), Pebble, &FMixtormatPebbles::PebbleRotation, 0.0, 180.0, 180.0, 1.0,
		LOCTEXT("PebbleRotationHint", "Maximum random rotation, degrees.")));

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("PebbleGrpShape", "Shape"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatPebbles>(
		LOCTEXT("PebbleDirection", "Cut Direction"), Pebble, &FMixtormatPebbles::PebbleDirection,
		LOCTEXT("PebbleDirectionHint", "How the cut planes are oriented: evenly spread, random, opposed pairs, golden angle, or biased to the axes.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleCuts", "Cuts"), Pebble, &FMixtormatPebbles::PebbleCuts, 3.0, 24.0, 10,
			LOCTEXT("PebbleCutsHint", "Planes per stone. More reads rounder.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleIrregularity", "Irregularity"), Pebble, &FMixtormatPebbles::PebbleIrregularity, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PebbleIrregularityHint", "Jitters radius, chamfer, cut angle and cut count."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleChamfer", "Chamfer"), Pebble, &FMixtormatPebbles::PebbleChamfer, 0.0, 0.2, 0.02, 0.001,
		LOCTEXT("PebbleChamferHint", "Edge chamfer between cuts.")));

	Panel = AddCard(Cards, LOCTEXT("PebbleGrpHeight", "Height"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleSteepness", "Steepness"), Pebble, &FMixtormatPebbles::PebbleSteepness, 0.0, 16.0, 5.6, 0.05,
			LOCTEXT("PebbleSteepnessHint", "Facet rise per unit inward.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleSteepnessVariation", "Steep Var"), Pebble, &FMixtormatPebbles::PebbleSteepnessVariation, 0.0, 4.0, 1.7, 0.01,
			LOCTEXT("PebbleSteepnessVariationHint", "Log2 spread of steepness per facet."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleBiasVariation", "Facet Offset"), Pebble, &FMixtormatPebbles::PebbleBiasVariation, 0.0, 0.5, 0.08, 0.005,
			LOCTEXT("PebbleBiasVariationHint", "Random height offset per facet.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleHeightGain", "Height"), Pebble, &FMixtormatPebbles::PebbleHeightGain, 0.0, 2.0, 1.0, 0.01,
			LOCTEXT("PebbleHeightGainHint", "Overall stone height."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleHeightVariation", "Height Var"), Pebble, &FMixtormatPebbles::PebbleHeightVariation, 0.0, 1.0, 0.3, 0.01,
		LOCTEXT("PebbleHeightVariationHint", "Random height drop per stone.")));
	AddSliderRow(Panel, MixtormatRow::MakeTrailing(
		LOCTEXT("PebbleFacetIds", "Facet IDs"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatPebbles* P = GetSelectedPebbles();
				return P && P->bPebbleFacetIds ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatPebbles* P = GetSelectedPebbles())
				{
					P->bPebbleFacetIds = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("PebbleFacetIdsHint", "One region ID per facet instead of per stone.")));

	AddNativeFlowControls(Cards);

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedPebbles() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("PebblesHeading", "PEBBLES")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::Pebbles;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGenerator* Generator = GetSelectedGenerator())
							{
								Generator->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("PebblesEnabledHint", "Enable these pebbles"))
				])
			[
				Cards
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildRootNetworkControls()
{
	const auto Root = [this]() { return GetSelectedRootNetwork(); };
	const FMixtormatRootNetwork Defaults;
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);

	// Output Card
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Cards, LOCTEXT("RootOutputCard", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("RootNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Root]()
				{
					const FMixtormatRootNetwork* R = Root();
					return R && R->bRootNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Root](const ECheckBoxState State)
				{
					if (FMixtormatRootNetwork* R = Root())
					{
						R->bRootNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("RootNormalizeHeightHint", "Preserves zero height while scaling peak to unit bounds.")));
		AddSliderRow(Output, MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootHeightScale", "Height Scale"), Root, &FMixtormatRootNetwork::RootHeightScale,
				-4.0, 4.0, Defaults.RootHeightScale, 0.01,
				LOCTEXT("RootHeightScaleHint", "Height multiplier applied downstream.")));
	}

	// Growth Card
	{
		const TSharedRef<SVerticalBox> Growth = AddCard(Cards, LOCTEXT("RootGrowthCard", "GROWTH"));
		AddSliderRow(Growth, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatRootNetwork>(
				LOCTEXT("RootSeed", "Seed"), Root, &FMixtormatRootNetwork::RootSeed,
				0, 9999, Defaults.RootSeed,
				LOCTEXT("RootSeedHint", "Deterministic random seed.")),
			MakeMemberSliderInt<FMixtormatRootNetwork>(
				LOCTEXT("RootTrunks", "Trunk Roots"), Root, &FMixtormatRootNetwork::RootTrunks,
				1, 128, Defaults.RootTrunks,
				LOCTEXT("RootTrunksHint", "Number of starting root trunks."))
		));
		AddSliderRow(Growth, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatRootNetwork>(
				LOCTEXT("RootAttractors", "Attractors"), Root, &FMixtormatRootNetwork::RootAttractors,
				32, 2048, Defaults.RootAttractors,
				LOCTEXT("RootAttractorsHint", "Attraction points guiding root network expansion.")),
			MakeMemberSliderInt<FMixtormatRootNetwork>(
				LOCTEXT("RootIterations", "Iterations"), Root, &FMixtormatRootNetwork::RootIterations,
				1, 256, Defaults.RootIterations,
				LOCTEXT("RootIterationsHint", "Number of colonisation growth iterations."))
		));
		AddSliderRow(Growth, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootStepLength", "Step Length"), Root, &FMixtormatRootNetwork::RootStepLength,
				0.005, 0.25, Defaults.RootStepLength, 0.002,
				LOCTEXT("RootStepLengthHint", "Distance grown per node segment.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootInfluenceRadius", "Influence Radius"), Root, &FMixtormatRootNetwork::RootInfluenceRadius,
				0.01, 0.50, Defaults.RootInfluenceRadius, 0.005,
				LOCTEXT("RootInfluenceRadiusHint", "Search radius around nodes for attractors."))
		));
		AddSliderRow(Growth, MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootKillDistance", "Kill Distance"), Root, &FMixtormatRootNetwork::RootKillDistance,
				0.002, 0.25, Defaults.RootKillDistance, 0.002,
				LOCTEXT("RootKillDistanceHint", "Distance at which an attractor is consumed.")));
	}

	// Branching Card
	{
		const TSharedRef<SVerticalBox> Branching = AddCard(Cards, LOCTEXT("RootBranchingCard", "BRANCHING"));
		AddSliderRow(Branching, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootBranchSpread", "Branch Spread"), Root, &FMixtormatRootNetwork::RootBranchSpread,
				0.0, 1.0, Defaults.RootBranchSpread, 0.01,
				LOCTEXT("RootBranchSpreadHint", "Deflection angle for lateral branches.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootDirectionInertia", "Direction Inertia"), Root, &FMixtormatRootNetwork::RootDirectionInertia,
				0.0, 1.0, Defaults.RootDirectionInertia, 0.01,
				LOCTEXT("RootDirectionInertiaHint", "Inertial persistence along previous segment direction."))
		));
		AddSliderRow(Branching, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootCurlStrength", "Curl Strength"), Root, &FMixtormatRootNetwork::RootCurlStrength,
				0.0, 2.0, Defaults.RootCurlStrength, 0.01,
				LOCTEXT("RootCurlStrengthHint", "Periodic curl noise meandering perturbation.")),
			MakeMemberSliderInt<FMixtormatRootNetwork>(
				LOCTEXT("RootCurlScale", "Curl Frequency"), Root, &FMixtormatRootNetwork::RootCurlScale,
				1, 32, Defaults.RootCurlScale,
				LOCTEXT("RootCurlScaleHint", "Lattice frequency of curl perturbation."))
		));
			AddSliderRow(Branching, MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootKink", "Kink & Zigzag"), Root, &FMixtormatRootNetwork::RootKink,
				0.0, 1.0, Defaults.RootKink, 0.01,
				LOCTEXT("RootKinkHint", "Tortuous wandering and sudden sharp obstacle deflection angle.")));
	}

	// Steering & Tropism Card
	{
		const TSharedRef<SVerticalBox> Steering = AddCard(Cards, LOCTEXT("RootSteeringCard", "STEERING & TROPISM"));
		AddSliderRow(Steering, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootAzimuthAngle", "Azimuth Angle"), Root, &FMixtormatRootNetwork::RootAzimuthAngle,
				0.0, 360.0, Defaults.RootAzimuthAngle, 1.0,
				LOCTEXT("RootAzimuthAngleHint", "Compass angle of prevailing growth direction.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootAzimuthStrength", "Azimuth Force"), Root, &FMixtormatRootNetwork::RootAzimuthStrength,
				0.0, 2.0, Defaults.RootAzimuthStrength, 0.01,
				LOCTEXT("RootAzimuthStrengthHint", "Strength of directional bias steering growth."))
		));
		AddSliderRow(Steering, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootSwirlStrength", "Swirl Force"), Root, &FMixtormatRootNetwork::RootSwirlStrength,
				-2.0, 2.0, Defaults.RootSwirlStrength, 0.01,
				LOCTEXT("RootSwirlStrengthHint", "Rotational vortex steering curling roots around center.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootAxisRatio", "Axis Preference (X/Y)"), Root, &FMixtormatRootNetwork::RootAxisRatio,
				0.2, 5.0, Defaults.RootAxisRatio, 0.05,
				LOCTEXT("RootAxisRatioHint", "Anisotropic ratio: >1 favors horizontal spread, <1 favors vertical spread."))
		));
	}

	// Profile & Sweep Card
	{
		const TSharedRef<SVerticalBox> Profile = AddCard(Cards, LOCTEXT("RootProfileCard", "PROFILE & SWEEP"));
		AddSliderRow(Profile, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootBaseRadius", "Base Radius"), Root, &FMixtormatRootNetwork::RootBaseRadius,
				0.002, 0.15, Defaults.RootBaseRadius, 0.001,
				LOCTEXT("RootBaseRadiusHint", "Maximum trunk radius in UV space.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootTipTaper", "Tip Taper"), Root, &FMixtormatRootNetwork::RootTipTaper,
				0.0, 1.0, Defaults.RootTipTaper, 0.01,
				LOCTEXT("RootTipTaperHint", "Taper ratio from trunk base to root tips."))
		));
		AddSliderRow(Profile, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootProfileFlatness", "Flatness"), Root, &FMixtormatRootNetwork::RootProfileFlatness,
				0.0, 1.0, Defaults.RootProfileFlatness, 0.01,
				LOCTEXT("RootProfileFlatnessHint", "Flattening of the root dome profile.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootProfileBias", "Pinch / Broaden"), Root, &FMixtormatRootNetwork::RootProfileBias,
				-1.0, 1.0, Defaults.RootProfileBias, 0.01,
				LOCTEXT("RootProfileBiasHint", "Negative pinches into a crested ridge; positive broadens into steep shoulders."))
		));
		AddSliderRow(Profile, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootJunctionSmooth", "Junction Smooth"), Root, &FMixtormatRootNetwork::RootJunctionSmooth,
				0.0, 1.0, Defaults.RootJunctionSmooth, 0.01,
				LOCTEXT("RootJunctionSmoothHint", "Smooth polynomial union blend at branch junctions.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootWeaveAmount", "Weave Modulation"), Root, &FMixtormatRootNetwork::RootWeaveAmount,
				0.0, 1.0, Defaults.RootWeaveAmount, 0.01,
				LOCTEXT("RootWeaveAmountHint", "Height over-under sine wave modulation along branches."))
		));
	}

	// Radius Taper Ramp Card
	{
		TArray<TPair<FText, TFunction<void()>>> RadiusPresets;
		RadiusPresets.Add({LOCTEXT("RadiusLinear", "Linear Taper"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->RadiusCurve.Points = { FMixtormatScalarRampPoint(0.0f, 1.0f), FMixtormatScalarRampPoint(1.0f, 0.1f) };
				R->RadiusCurve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
				R->bUseRadiusCurve = true;
				RefreshLayeredPreview();
			}
		}});
		RadiusPresets.Add({LOCTEXT("RadiusFlared", "Flared Buttress"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->RadiusCurve.Points = { FMixtormatScalarRampPoint(0.0f, 1.8f), FMixtormatScalarRampPoint(0.15f, 1.0f), FMixtormatScalarRampPoint(1.0f, 0.2f) };
				R->RadiusCurve.Interpolation = EMixtormatScalarRampInterpolation::Spline;
				R->bUseRadiusCurve = true;
				RefreshLayeredPreview();
			}
		}});
		RadiusPresets.Add({LOCTEXT("RadiusBulbous", "Bulbous / Knots"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->RadiusCurve.Points = {
					FMixtormatScalarRampPoint(0.0f, 1.0f),
					FMixtormatScalarRampPoint(0.25f, 0.65f),
					FMixtormatScalarRampPoint(0.50f, 1.35f),
					FMixtormatScalarRampPoint(0.75f, 0.60f),
					FMixtormatScalarRampPoint(1.0f, 0.25f)
				};
				R->RadiusCurve.Interpolation = EMixtormatScalarRampInterpolation::Spline;
				R->bUseRadiusCurve = true;
				RefreshLayeredPreview();
			}
		}});
		RadiusPresets.Add({LOCTEXT("RadiusPegs", "Blunt Aerial Pegs"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->RadiusCurve.Points = { FMixtormatScalarRampPoint(0.0f, 1.0f), FMixtormatScalarRampPoint(0.85f, 0.95f), FMixtormatScalarRampPoint(1.0f, 0.8f) };
				R->RadiusCurve.Interpolation = EMixtormatScalarRampInterpolation::Spline;
				R->bUseRadiusCurve = true;
				RefreshLayeredPreview();
			}
		}});

		const auto MakePresetsMenu = [](const FText& Heading, const TArray<TPair<FText, TFunction<void()>>>& Items) -> TSharedRef<SWidget>
		{
			return SNew(SComboButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
				.HasDownArrow(false)
				.ContentPadding(FMargin(0.0f))
				.OnGetMenuContent_Lambda([Heading, Items]() -> TSharedRef<SWidget>
				{
					MixtormatMenu::FBuilder Menu;
					Menu.Caption(Heading);
					for (const auto& Item : Items)
					{
						Menu.Item(Item.Key, nullptr, FSimpleDelegate::CreateLambda(Item.Value));
					}
					return Menu.Build();
				})
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("PresetsDropdownLabel", "Presets"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						[
							SNew(SImage)
							.Image(MixtormatIcons::ChevronDown())
							.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
						]
					]
				];
		};

		const TSharedRef<SWidget> RadiusHeader = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakePresetsMenu(LOCTEXT("RadiusPresetsHeading", "RADIUS PRESETS"), RadiusPresets)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Root]()
					{
						const FMixtormatRootNetwork* R = Root();
						return R && R->bUseRadiusCurve ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Root](ECheckBoxState S)
					{
						if (FMixtormatRootNetwork* R = Root())
						{
							R->bUseRadiusCurve = S == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					}),
					LOCTEXT("RootRadiusCurveEnabledHint", "Enable Radius Curve"))
			];

		const TSharedRef<SVerticalBox> RadiusCard = AddCard(Cards, LOCTEXT("RootRadiusCard", "RADIUS TAPER RAMP"), RadiusHeader);
		const TSharedRef<SWidget> RadiusRampWidget = SNew(SMixtormatScalarRamp)
			.IsEnabled_Lambda([this, Root]() { return Root() && !GroupControlValueFor(Root(), FMixtormatRootNetwork::StaticStruct(), TEXT("RadiusCurve")); })
			.Ramp_Lambda([this, Root]()
			{
				const FMixtormatRootNetwork* R = Root();
				if (const auto* Value = GroupControlValueFor(R, FMixtormatRootNetwork::StaticStruct(), TEXT("RadiusCurve"))) { return Value->ScalarRampValue; }
								return R ? R->RadiusCurve : FMixtormatScalarRamp();
			})
			.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
			.CanonicalXMin(0.0f).CanonicalXMax(1.0f)
			.CanonicalYMin(0.0f).CanonicalYMax(2.0f)
			.SoftYMin(-0.5f).SoftYMax(2.5f)
			.ExtendedYMin(-1.0f).ExtendedYMax(3.0f)
			.OnChanged_Lambda([this, Root](const FMixtormatScalarRamp& NewRamp)
			{
				if (FMixtormatRootNetwork* R = Root())
				{
					if (GroupControlValueFor(R, FMixtormatRootNetwork::StaticStruct(), TEXT("RadiusCurve"))) { return; }
					R->RadiusCurve = NewRamp;
					RefreshLayeredPreview();
				}
			})
			.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
			.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });

		AddSliderRow(RadiusCard, WrapParameterControl(RadiusRampWidget,
					MakeAddressResolver<FMixtormatRootNetwork>(Root, &FMixtormatRootNetwork::RadiusCurve)));
	}

	// Soil Burial Ramp Card
	{
		TArray<TPair<FText, TFunction<void()>>> BurialPresets;
		BurialPresets.Add({LOCTEXT("BurialDefault", "Submerged Tips (Default)"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->BurialCurve.Points = { FMixtormatScalarRampPoint(0.0f, 0.0f), FMixtormatScalarRampPoint(1.0f, 1.0f) };
				R->BurialCurve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
				R->bUseBurialCurve = true;
				RefreshLayeredPreview();
			}
		}});
		BurialPresets.Add({LOCTEXT("BurialArching", "Surface Arching"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->BurialCurve.Points = { FMixtormatScalarRampPoint(0.0f, 0.8f), FMixtormatScalarRampPoint(0.5f, 0.0f), FMixtormatScalarRampPoint(1.0f, 0.9f) };
				R->BurialCurve.Interpolation = EMixtormatScalarRampInterpolation::Spline;
				R->bUseBurialCurve = true;
				RefreshLayeredPreview();
			}
		}});
		BurialPresets.Add({LOCTEXT("BurialDeep", "Deep Trench"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->BurialCurve.Points = { FMixtormatScalarRampPoint(0.0f, 0.2f), FMixtormatScalarRampPoint(1.0f, 1.8f) };
				R->BurialCurve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
				R->bUseBurialCurve = true;
				RefreshLayeredPreview();
			}
		}});
		BurialPresets.Add({LOCTEXT("BurialUniform", "Uniform Sink"), [this, Root]()
		{
			if (FMixtormatRootNetwork* R = Root())
			{
				R->BurialCurve.Points = { FMixtormatScalarRampPoint(0.0f, 1.0f), FMixtormatScalarRampPoint(1.0f, 1.0f) };
				R->BurialCurve.Interpolation = EMixtormatScalarRampInterpolation::Linear;
				R->bUseBurialCurve = true;
				RefreshLayeredPreview();
			}
		}});

		const auto MakePresetsMenu = [](const FText& Heading, const TArray<TPair<FText, TFunction<void()>>>& Items) -> TSharedRef<SWidget>
		{
			return SNew(SComboButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
				.HasDownArrow(false)
				.ContentPadding(FMargin(0.0f))
				.OnGetMenuContent_Lambda([Heading, Items]() -> TSharedRef<SWidget>
				{
					MixtormatMenu::FBuilder Menu;
					Menu.Caption(Heading);
					for (const auto& Item : Items)
					{
						Menu.Item(Item.Key, nullptr, FSimpleDelegate::CreateLambda(Item.Value));
					}
					return Menu.Build();
				})
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("PresetsDropdownLabel", "Presets"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						[
							SNew(SImage)
							.Image(MixtormatIcons::ChevronDown())
							.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
						]
					]
				];
		};

		const TSharedRef<SWidget> BurialHeader = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakePresetsMenu(LOCTEXT("BurialPresetsHeading", "BURIAL PRESETS"), BurialPresets)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Root]()
					{
						const FMixtormatRootNetwork* R = Root();
						return R && R->bUseBurialCurve ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Root](ECheckBoxState S)
					{
						if (FMixtormatRootNetwork* R = Root())
						{
							R->bUseBurialCurve = S == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					}),
					LOCTEXT("RootBurialCurveEnabledHint", "Enable Soil Burial Curve"))
			];

		const TSharedRef<SVerticalBox> BurialCard = AddCard(Cards, LOCTEXT("RootBurialCard", "SOIL BURIAL RAMP"), BurialHeader);
		AddSliderRow(BurialCard, MakeMemberSlider<FMixtormatRootNetwork>(
			LOCTEXT("RootBurial", "Base Burial Depth"), Root, &FMixtormatRootNetwork::RootBurial,
			0.0, 1.0, Defaults.RootBurial, 0.01,
			LOCTEXT("RootBurialHint", "Sinks thin roots beneath the soil plane while thick roots break through.")));

		const TSharedRef<SWidget> BurialRampWidget = SNew(SMixtormatScalarRamp)
			.IsEnabled_Lambda([this, Root]() { return Root() && !GroupControlValueFor(Root(), FMixtormatRootNetwork::StaticStruct(), TEXT("BurialCurve")); })
			.Ramp_Lambda([this, Root]()
			{
				const FMixtormatRootNetwork* R = Root();
				if (const auto* Value = GroupControlValueFor(R, FMixtormatRootNetwork::StaticStruct(), TEXT("BurialCurve"))) { return Value->ScalarRampValue; }
								return R ? R->BurialCurve : FMixtormatScalarRamp();
			})
			.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
			.CanonicalXMin(0.0f).CanonicalXMax(1.0f)
			.CanonicalYMin(0.0f).CanonicalYMax(2.0f)
			.SoftYMin(-0.5f).SoftYMax(2.5f)
			.ExtendedYMin(-1.0f).ExtendedYMax(3.0f)
			.OnChanged_Lambda([this, Root](const FMixtormatScalarRamp& NewRamp)
			{
				if (FMixtormatRootNetwork* R = Root())
				{
					if (GroupControlValueFor(R, FMixtormatRootNetwork::StaticStruct(), TEXT("BurialCurve"))) { return; }
					R->BurialCurve = NewRamp;
					RefreshLayeredPreview();
				}
			})
			.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
			.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });

		AddSliderRow(BurialCard, WrapParameterControl(BurialRampWidget,
					MakeAddressResolver<FMixtormatRootNetwork>(Root, &FMixtormatRootNetwork::BurialCurve)));
	}

	// Root UV & Texturing Card
	{
		const TSharedRef<SVerticalBox> UVCard = AddCard(Cards, LOCTEXT("RootUVCard", "ROOT UV & TEXTURING"));
		AddSliderRow(UVCard, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootUVScaleU", "UV Scale U"), Root, &FMixtormatRootNetwork::RootUVScaleU,
				0.1, 32.0, Defaults.RootUVScaleU, 0.1,
				LOCTEXT("RootUVScaleUHint", "Circumferential cross-section cylinder arc tiling multiplier.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootUVScaleV", "UV Scale V"), Root, &FMixtormatRootNetwork::RootUVScaleV,
				0.1, 32.0, Defaults.RootUVScaleV, 0.1,
				LOCTEXT("RootUVScaleVHint", "Longitudinal arc length tiling multiplier along root stems."))
		));
		AddSliderRow(UVCard, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootUVOffsetU", "UV Offset U"), Root, &FMixtormatRootNetwork::RootUVOffsetU,
				-1.0, 1.0, Defaults.RootUVOffsetU, 0.01,
				LOCTEXT("RootUVOffsetUHint", "Circumferential rotational offset around the root cylinder.")),
			MakeMemberSlider<FMixtormatRootNetwork>(
				LOCTEXT("RootUVOffsetV", "UV Offset V"), Root, &FMixtormatRootNetwork::RootUVOffsetV,
				-1.0, 1.0, Defaults.RootUVOffsetV, 0.01,
				LOCTEXT("RootUVOffsetVHint", "Longitudinal translation offset along root length."))
		));
	}

	AddNativeFlowControls(Cards);

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRootNetwork() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RootNetworkHeading", "ROOT NETWORK")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::RootNetwork;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* G = GetSelectedGenerator();
							return G && G->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGenerator* G = GetSelectedGenerator())
							{
								G->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
							}
						}),
						LOCTEXT("RootNetworkEnabledHint", "Enable Root Network"))
				])
			[
				Cards
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildScalesControls()
{
	const auto Scale = [this]() { return GetSelectedScales(); };
	const FMixtormatScales Defaults;
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Panel = AddCard(Cards, LOCTEXT("ScalesOutput", "OUTPUT"));
	const auto Slider = [this, Scale, Defaults](const FText& Label, float FMixtormatScales::* Member,
		double Min, double Max, const FText& Hint = FText::GetEmpty())
	{
		return MakeMemberSlider<FMixtormatScales>(Label, Scale, Member, Min, Max, Defaults.*Member, .01, Hint);
	};
	const auto Pair = [this, &Panel](TSharedRef<SWidget> A, TSharedRef<SWidget> B)
	{
		AddSliderRow(Panel, MixtormatRow::MakePair(A, B));
	};
	Pair(Slider(LOCTEXT("ScalesHeightScale", "Height Scale"), &FMixtormatScales::HeightScale, -4, 4),
		MixtormatRow::MakeTrailing(LOCTEXT("ScalesNormalize", "Normalize"), MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([Scale]() { const auto* S = Scale(); return S && S->bNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
			FOnCheckStateChanged::CreateLambda([this, Scale](ECheckBoxState State) { if (auto* S = Scale()) { S->bNormalizeHeight = State == ECheckBoxState::Checked; RefreshLayeredPreview(); } })),
			LOCTEXT("ScalesNormalizeHint", "Zero-preserving normalization of the visible scale height.")));
	Panel = AddCard(Cards, LOCTEXT("ScalesLayout", "LAYOUT"));
	Pair(MakeMemberSliderInt<FMixtormatScales>(LOCTEXT("ScalesCountX", "Count X"), Scale, &FMixtormatScales::CountX, 1, 64, Defaults.CountX),
		MakeMemberSliderInt<FMixtormatScales>(LOCTEXT("ScalesCountY", "Count Y"), Scale, &FMixtormatScales::CountY, 1, 64, Defaults.CountY));
	Pair(Slider(LOCTEXT("ScalesStagger", "Row Stagger"), &FMixtormatScales::Stagger, 0, 1),
		Slider(LOCTEXT("ScalesDirection", "Direction"), &FMixtormatScales::Direction, -180, 180,
			LOCTEXT("ScalesDirectionHint", "Rotate plates around their tucked roots, in degrees. The periodic row lattice stays fixed.")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatScales>(LOCTEXT("ScalesSeed", "Seed"), Scale, &FMixtormatScales::Seed, 0, 99999, Defaults.Seed));
	Panel = AddCard(Cards, LOCTEXT("ScalesShape", "SHAPE"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatScales>(LOCTEXT("ScalesShapeMode", "Shape"), Scale, &FMixtormatScales::Shape));
	Pair(Slider(LOCTEXT("ScalesWidth", "Width"), &FMixtormatScales::Width, .25, 2),
		Slider(LOCTEXT("ScalesLength", "Length"), &FMixtormatScales::Length, .25, 2));
	Pair(Slider(LOCTEXT("ScalesRoundness", "Tip Roundness"), &FMixtormatScales::TipRoundness, 0, 1),
		Slider(LOCTEXT("ScalesTaper", "Taper"), &FMixtormatScales::Taper, 0, .75));
	Panel = AddCard(Cards, LOCTEXT("ScalesOverlap", "OVERLAP"));
	AddSliderRow(Panel, Slider(LOCTEXT("ScalesOverlapAmount", "Overlap"), &FMixtormatScales::Overlap, 0, 1,
		LOCTEXT("ScalesOverlapHint", "Extends plates from their fixed roots. Type outside the slider range to create gaps or larger overlaps; large footprints cost more.")));
	Pair(Slider(LOCTEXT("ScalesTipLift", "Tip Lift"), &FMixtormatScales::TipLift, 0, 1),
		Slider(LOCTEXT("ScalesRootTuck", "Root Tuck"), &FMixtormatScales::RootTuck, 0, 1));
	Panel = AddCard(Cards, LOCTEXT("ScalesProfile", "PROFILE"));
	Pair(Slider(LOCTEXT("ScalesThickness", "Thickness"), &FMixtormatScales::Thickness, 0, 1),
		Slider(LOCTEXT("ScalesDome", "Dome"), &FMixtormatScales::Dome, 0, 1));
	Pair(Slider(LOCTEXT("ScalesRidge", "Center Ridge"), &FMixtormatScales::Ridge, 0, 1),
		Slider(LOCTEXT("ScalesBevel", "Edge Bevel"), &FMixtormatScales::Bevel, 0, .25));
	Panel = AddCard(Cards, LOCTEXT("ScalesVariation", "VARIATION"));
	Pair(Slider(LOCTEXT("ScalesSizeRandom", "Size"), &FMixtormatScales::SizeRandom, 0, .25),
		Slider(LOCTEXT("ScalesRotationRandom", "Rotation"), &FMixtormatScales::RotationRandom, 0, 15,
			LOCTEXT("ScalesRotationHint", "Random rotation in degrees. Typed values may exceed the safe drag range; crossings use the highest surface.")));
	Pair(Slider(LOCTEXT("ScalesHeightRandom", "Height"), &FMixtormatScales::HeightRandom, 0, .25),
		Slider(LOCTEXT("ScalesRowJitter", "Row Jitter"), &FMixtormatScales::RowJitter, 0, .25));
	Panel = AddCard(Cards, LOCTEXT("ScalesSurface", "SURFACE"));
	Pair(Slider(LOCTEXT("ScalesGrooveAmount", "Grooves"), &FMixtormatScales::GrooveAmount, 0, .1),
		Slider(LOCTEXT("ScalesGrooveCount", "Groove Count"), &FMixtormatScales::GrooveCount, 1, 32));
	AddSliderRow(Panel, Slider(LOCTEXT("ScalesGrooveDirection", "Groove Direction"), &FMixtormatScales::GrooveDirection, -180, 180));
	AddNativeFlowControls(Cards);
	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedScales() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("ScalesHeading", "SCALES")).PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0)[
				MakeChildOutputPreviewButton([]() { FMixtormatLayerChild Probe; Probe.Type = EMixtormatLayerChildType::Generator;
					Probe.Generator.Type = EMixtormatGeneratorType::Scales; return GetChildPreviewOutputSet(Probe); }())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]() { const auto* G = GetSelectedGenerator(); return G && G->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { if (auto* G = GetSelectedGenerator()) { G->bEnabled = State == ECheckBoxState::Checked; RefreshLayeredPreview(); RebuildLayerList(); } }))])
		[Cards]];
}

TSharedRef<SWidget> SMixtormat::BuildCliffStrataControls()
{
	const auto C=[this](){return GetSelectedCliffStrata();};
	TSharedRef<SVerticalBox> Panel=SNew(SVerticalBox);const TSharedRef<SVerticalBox> Cards=Panel;
	auto Pair=[&](TSharedRef<SWidget>A,TSharedRef<SWidget>B){AddSliderRow(Panel,MixtormatRow::MakePair(A,B));};
	Panel=AddCard(Cards,LOCTEXT("CliffOutput","OUTPUT"));
	Pair(
		MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightScale","Scale"),C,&FMixtormatCliffStrata::CliffHeightScale,-4,4,1,.01,LOCTEXT("CliffHeightScaleHint","Scales the signed generator height after normalization.")),
		MixtormatRow::MakeTrailing(
			LOCTEXT("CliffNormalizeHeight","Normalize"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([C](){const FMixtormatCliffStrata* G=C();return G&&G->bCliffNormalizeHeight?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}),
				FOnCheckStateChanged::CreateLambda([this,C](ECheckBoxState S){if(FMixtormatCliffStrata* G=C()){G->bCliffNormalizeHeight=S==ECheckBoxState::Checked;RefreshLayeredPreview();}})),
			LOCTEXT("CliffNormalizeHeightHint","Zero-preserving max-absolute normalization to -0.5..0.5.")));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDepthMin","Depth Min"),C,&FMixtormatCliffStrata::DepthMin,-4,4,-1,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDepthMax","Depth Max"),C,&FMixtormatCliffStrata::DepthMax,-4,4,1,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffLayout","LAYOUT"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffCountX","Count X"),C,&FMixtormatCliffStrata::CountX,1,64,8,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffCountY","Count Y"),C,&FMixtormatCliffStrata::CountY,1,64,7,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDensity","Density"),C,&FMixtormatCliffStrata::Density,0,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffJitter","Jitter"),C,&FMixtormatCliffStrata::Jitter,0,1,.1,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffSizeMin","Size Min"),C,&FMixtormatCliffStrata::SizeMin,.02,3,1,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffSizeMax","Size Max"),C,&FMixtormatCliffStrata::SizeMax,.02,3,1.35,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffAspect","Aspect"),C,&FMixtormatCliffStrata::SizeAspect,.02,3,.95,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFlowVariation","Flow Var"),C,&FMixtormatCliffStrata::FlowVariation,0,3.14159,.6,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffFormation","FORMATION"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightMin","Height Min"),C,&FMixtormatCliffStrata::HeightMin,-1,1,.025,.005,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightMax","Height Max"),C,&FMixtormatCliffStrata::HeightMax,-1,1,.1,.005,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffRotation","Rotation"),C,&FMixtormatCliffStrata::Rotation,-1,1,.35,.01,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSteps","Steps"),C,&FMixtormatCliffStrata::Steps,0,32,0,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffLeanX","Lean X"),C,&FMixtormatCliffStrata::LeanX,-2,2,.2,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffLeanY","Lean Y"),C,&FMixtormatCliffStrata::LeanY,-2,2,0,.01,FText::GetEmpty()));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffFormationCells","Cells"),C,&FMixtormatCliffStrata::FormationCells,1,32,5,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFormationAmount","Amount"),C,&FMixtormatCliffStrata::FormationAmount,0,1,1,.01,FText::GetEmpty()));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSides","Sides"),C,&FMixtormatCliffStrata::Sides,3,12,4,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSeed","Seed"),C,&FMixtormatCliffStrata::Seed,0,99999,1234,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffQuarters","QUARTERS"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterY","Y Count"),C,&FMixtormatCliffStrata::QuarterYCount,1,32,8,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterFill","Fill"),C,&FMixtormatCliffStrata::QuarterFill,0,1,.5,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterSize","Size"),C,&FMixtormatCliffStrata::QuarterSize,.01,3,.9,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterHeight","Height"),C,&FMixtormatCliffStrata::QuarterHeight,0,6,2.75,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQJX","Jitter X"),C,&FMixtormatCliffStrata::QuarterJitterX,0,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQJY","Jitter Y"),C,&FMixtormatCliffStrata::QuarterJitterY,0,1,.5,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffProjection","PROJECTION"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYaw","Yaw"),C,&FMixtormatCliffStrata::CameraYaw,-1,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffPitch","Pitch"),C,&FMixtormatCliffStrata::CameraPitch,-1,1,.25,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffViewScale","View Scale"),C,&FMixtormatCliffStrata::ViewScale,1.0f,8,2.50f,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffPattern","PATTERN"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffVoroCells","Voronoi Cells"),C,&FMixtormatCliffStrata::VoronoiCells,1,64,13,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFlowVoro","Flow Voronoi"),C,&FMixtormatCliffStrata::FlowVoronoi,-2,2,0,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffShape","SHAPE"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffUnitDist","Unit Distance"),C,&FMixtormatCliffStrata::UnitDistance,.001,2,.3,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffUnitId","ID Variation"),C,&FMixtormatCliffStrata::UnitDistanceIdLerp,0,1,.5,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCarveDepth","Carve Depth"),C,&FMixtormatCliffStrata::CarveDepth,-2,2,.375,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCarveVoro","Carve Voronoi"),C,&FMixtormatCliffStrata::CarveVoronoi,-2,2,.1,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYBias","Y Bias"),C,&FMixtormatCliffStrata::YBias,-4,4,.3,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYBiasVoro","Y Bias Voronoi"),C,&FMixtormatCliffStrata::YBiasVoronoi,-4,4,.9,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffNegYTaper","Negative Y Taper"),C,&FMixtormatCliffStrata::NegativeYUnitDistanceTaper,-4,4,1,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffChamfer","CHAMFER"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatCliffStrata>(
		LOCTEXT("CliffChamferMode", "Mode"), C, &FMixtormatCliffStrata::ChamferMode,
		LOCTEXT("CliffChamferModeHint", "Height Map chamfers the carved height. Actual Box bevels the projected blocks, including their silhouettes; it adds ray intersection work.")));
	const auto IsBoxChamfer = [C]()
	{
		const FMixtormatCliffStrata* G = C();
		return G && G->ChamferMode == EMixtormatCliffChamferMode::ActualBox;
	};
	AddSliderRow(Panel, SNew(SBox).IsEnabled_Lambda(IsBoxChamfer)[
		MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffBoxChamferWidth", "Box Width"),
			C, &FMixtormatCliffStrata::BoxChamferWidth, 0, 1, FMixtormatCliffStrata().BoxChamferWidth, .01,
			LOCTEXT("CliffBoxChamferWidthHint", "Bevel width relative to the block's smallest half-dimension. Zero keeps sharp boxes. Does not use the height-map Amount or Voronoi controls."))]);
	AddSliderRow(Panel, SNew(SBox).IsEnabled_Lambda([IsBoxChamfer]() { return !IsBoxChamfer(); })[
		MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferWidth","Width"),C,&FMixtormatCliffStrata::ChamferWidth,0,2,.5,.01,FText::GetEmpty()),
			MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferIntensity","Amount"),C,&FMixtormatCliffStrata::ChamferIntensity,-4,4,1,.01,FText::GetEmpty()))]);
	AddSliderRow(Panel, SNew(SBox).IsEnabled_Lambda([IsBoxChamfer]() { return !IsBoxChamfer(); })[
		MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferVoro","Voronoi"),C,&FMixtormatCliffStrata::ChamferVoronoi,-2,2,.05,.01,FText::GetEmpty())]);
	Panel=AddCard(Cards,LOCTEXT("CliffCavity","CAVITY"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityAmount","Amount"),C,&FMixtormatCliffStrata::CavityIntensity,-4,4,.25,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffBlockCavityWidth","Block"),C,&FMixtormatCliffStrata::BlockCavityWidth,.001,1,.02,.005,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffRowCavityWidth","Row"),C,&FMixtormatCliffStrata::RowCavityWidth,.001,1,.25,.005,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityThreshold","Threshold"),C,&FMixtormatCliffStrata::CavityVoronoiThreshold,0,1,.5,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityGain","Gain"),C,&FMixtormatCliffStrata::CavityVoronoiMaskGain,.001,1,.125,.005,FText::GetEmpty()));
	AddNativeFlowControls(Cards);

	return SNew(SBox).Visibility_Lambda([this](){return GetSelectedCliffStrata()?EVisibility::Visible:EVisibility::Collapsed;})[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("CliffStrataHeading","CLIFF STRATA")).PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().Padding(0,0,FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap,0)[MakeChildOutputPreviewButton([](){FMixtormatLayerChild Probe;Probe.Type=EMixtormatLayerChildType::Generator;Probe.Generator.Type=EMixtormatGeneratorType::CliffStrata;return GetChildPreviewOutputSet(Probe);}())]
			+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(TAttribute<ECheckBoxState>::CreateLambda([this](){const FMixtormatGenerator* G=GetSelectedGenerator();return G&&G->bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}),FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S){if(FMixtormatGenerator* G=GetSelectedGenerator()){G->bEnabled=S==ECheckBoxState::Checked;RefreshLayeredPreview();RebuildLayerList();}}))])
		[Cards]];
}








TSharedRef<SWidget> SMixtormat::BuildHeightBlendSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	if (!GetSelectedHeightBlend()) { return Menu.Build(); }
	Menu.Item(LOCTEXT("HeightBlendSourceAuto", "Auto (Previous)"), nullptr,
		FSimpleDelegate::CreateLambda([this]()
		{
			if (FMixtormatGeneratorHeightBlend* Blend = GetSelectedHeightBlend())
			{
				Blend->SourceLayerId.Invalidate();
				Blend->SourceChildId.Invalidate();
				RefreshLayeredPreview();
			}
		}));
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FGuid LayerId = WorkingLayers[SelectedLayerIndex].LayerId;
		for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Generator && Child.Type != EMixtormatLayerChildType::Boolean) { continue; }
			const FGuid ChildId = Child.ChildId;
			Menu.Item(GetLayerChildName(Child), MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, LayerId, ChildId]()
				{
					if (FMixtormatGeneratorHeightBlend* Blend = GetSelectedHeightBlend())
					{
						Blend->SourceLayerId = LayerId;
						Blend->SourceChildId = ChildId;
						RefreshLayeredPreview();
					}
				}));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildHeightBlendModuleControls()
{
    const auto Blend = [this]() { return GetSelectedHeightBlend(); };
    const auto OpIs = [Blend](const EMixtormatGeneratorHeightOp Op)
    {
        const FMixtormatGeneratorHeightBlend* B = Blend();
        return B && B->Op == Op;
    };

    TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

    AddSliderRow(Panel, MixtormatRow::MakePair(
        MakeMemberEnum<FMixtormatGeneratorHeightBlend, EMixtormatGeneratorHeightOp>(
            LOCTEXT("HeightBlendOp", "Operation"), Blend, &FMixtormatGeneratorHeightBlend::Op,
            LOCTEXT("HeightBlendOpHint", "How the running generator height combines with the referenced module."),
            FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })),
        MixtormatRow::MakeTrailing(
            LOCTEXT("HeightBlendSource", "Source"),
            MixtormatRow::MakeChip(
                TAttribute<FText>::CreateLambda([this]()
                {
                    const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
                    if (!B || !B->SourceChildId.IsValid())
                    {
                        return LOCTEXT("HeightBlendSourceAutoLabel", "Auto (Previous)");
                    }
                    if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
                    {
                        for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
                        {
                            if (Child.ChildId == B->SourceChildId)
                            {
                                return GetLayerChildName(Child);
                            }
                        }
                    }
                    return LOCTEXT("HeightBlendSourceMissing", "Missing");
                }),
                FOnGetContent::CreateSP(this, &SMixtormat::BuildHeightBlendSourceMenu)),
            LOCTEXT("HeightBlendSourceHint", "Another module in this layer whose signed height is the second operand."))));

    AddSliderRow(Panel, MixtormatRow::MakePair(
        MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
            LOCTEXT("HeightBlendAmount", "Amount"), Blend,
            &FMixtormatGeneratorHeightBlend::Amount, 0.0, 1.0, 1.0, 0.01,
            LOCTEXT("HeightBlendAmountHint", "How much of the combined result replaces the running height.")),
        SNew(SWidgetSwitcher)
        .WidgetIndex_Lambda([OpIs]() -> int32
        {
            if (OpIs(EMixtormatGeneratorHeightOp::Multiply)) { return 0; }
            if (OpIs(EMixtormatGeneratorHeightOp::Min)
                || OpIs(EMixtormatGeneratorHeightOp::Max)
                || OpIs(EMixtormatGeneratorHeightOp::HeightBlend)) { return 1; }
            return 2;
        })
        + SWidgetSwitcher::Slot()
        [
            MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                LOCTEXT("HeightBlendScale", "Scale"), Blend,
                &FMixtormatGeneratorHeightBlend::Scale, -4.0, 4.0, 1.0, 0.01,
                LOCTEXT("HeightBlendScaleHint", "Multiply/Scale factor. With no source the module is neutral."))
        ]
        + SWidgetSwitcher::Slot()
        [
            MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                LOCTEXT("HeightBlendSoftness", "Softness"), Blend,
                &FMixtormatGeneratorHeightBlend::Softness, 0.0, 1.0, 0.0, 0.001,
                LOCTEXT("HeightBlendSoftnessHint", "Rounded join for Min, Max and Height Blend, in height units. 0 is a hard join."))
        ]
        + SWidgetSwitcher::Slot()
        [
            SNullWidget::NullWidget
        ]));

    AddSliderRow(Panel,
        SNew(SBox)
        .Visibility_Lambda([OpIs]()
        {
            return OpIs(EMixtormatGeneratorHeightOp::HeightBlend)
                ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MixtormatRow::MakePair(
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendThreshold", "Threshold"), Blend,
                    &FMixtormatGeneratorHeightBlend::Threshold, -1.0, 1.0, 0.0, 0.01),
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendEdgeSoftness", "Edge Softness"), Blend,
                    &FMixtormatGeneratorHeightBlend::EdgeSoftness, 0.0, 1.0, 0.1, 0.005))
        ]);
    AddSliderRow(Panel,
        SNew(SBox)
        .Visibility_Lambda([OpIs]()
        {
            return OpIs(EMixtormatGeneratorHeightOp::HeightBlend)
                ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MixtormatRow::MakePair(
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendBaseBias", "Base Bias"), Blend,
                    &FMixtormatGeneratorHeightBlend::BaseBias, -1.0, 1.0, 0.0, 0.01),
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendBlendBias", "Blend Bias"), Blend,
                    &FMixtormatGeneratorHeightBlend::BlendBias, -1.0, 1.0, 0.0, 0.01))
        ]);

    return SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return GetSelectedHeightBlend() ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SMixtormatInspectorGroup)
            .Title(LOCTEXT("HeightBlendHeading", "HEIGHT BLEND")).PresetAction(MakeModulePresetButton())
            .InitiallyExpanded(true)
            .HeaderAction(
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    MixtormatRow::MakeCheckbox(
                        TAttribute<ECheckBoxState>::CreateLambda([this]()
                        {
                            const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
                            return B && B->bEnabled
                                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        }),
                        FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State)
                        {
                            if (FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend())
                            {
                                B->bEnabled = State == ECheckBoxState::Checked;
                                RefreshLayeredPreview();
                                RebuildLayerList();
                            }
                        }))
                ])
            [
                Panel
            ]
        ];
}

TSharedRef<SWidget> SMixtormat::BuildHeightCurveControls()
{
	const auto Curve = [this]() { return GetSelectedHeightCurve(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MakeMemberToggle<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapNormalize", "Normalize Input"), Curve, &FMixtormatGeneratorHeightCurve::bNormalizeInput,
		LOCTEXT("HeightRemapNormalizeHint", "Measure the running height and divide by its largest absolute value before input-range remapping. Zero and signs are preserved; an all-zero field stays zero.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
			LOCTEXT("HeightRemapInputMin", "Input Min"), Curve, &FMixtormatGeneratorHeightCurve::InputMin, -2.0, 0.0, -1.0, 0.01,
			LOCTEXT("HeightRemapInputMinHint", "Negative endpoint mapped to -1 before the curve. Zero maps negative input to the curve center without dividing by zero.")),
		MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
			LOCTEXT("HeightRemapInputMax", "Input Max"), Curve, &FMixtormatGeneratorHeightCurve::InputMax, 0.0, 2.0, 1.0, 0.01,
			LOCTEXT("HeightRemapInputMaxHint", "Positive endpoint mapped to 1 before the curve. Zero maps positive input to the curve center. The pivot remains zero."))));
	const TSharedRef<SWidget> CurveEditor = SNew(SMixtormatScalarRamp)
		.IsEnabled_Lambda([this, Curve]() { return Curve() && !GroupControlValueFor(Curve(), FMixtormatGeneratorHeightCurve::StaticStruct(), TEXT("Curve")); })
		.Ramp(TAttribute<FMixtormatScalarRamp>::CreateLambda([this, Curve]()
		{
			const FMixtormatGeneratorHeightCurve* C = Curve();
			if (const auto* Value = GroupControlValueFor(C, FMixtormatGeneratorHeightCurve::StaticStruct(), TEXT("Curve"))) { return Value->ScalarRampValue; }
						return C ? C->Curve : FMixtormatScalarRamp();
		}))
		.CanonicalXMin(-1.0f).CanonicalXMax(1.0f)
		.bInverted_Lambda([Curve]() { const FMixtormatGeneratorHeightCurve* C = Curve(); return C && C->bInvert; })
		.OnInvertChanged_Lambda([this, Curve](ECheckBoxState State)
		{
			if (FMixtormatGeneratorHeightCurve* C = Curve())
			{
				C->bInvert = State == ECheckBoxState::Checked;
				RefreshLayeredPreview();
			}
		})
		.CanonicalYMin(-1.0f).CanonicalYMax(1.0f)
		.SoftYMin(-1.5f).SoftYMax(1.5f)
		.ExtendedYMin(-3.0f).ExtendedYMax(3.0f)
		.OnChanged(FOnMixtormatScalarRampChanged::CreateLambda([this, Curve](const FMixtormatScalarRamp& NewRamp)
		{
			if (FMixtormatGeneratorHeightCurve* C = Curve())
			{
				if (GroupControlValueFor(C, FMixtormatGeneratorHeightCurve::StaticStruct(), TEXT("Curve"))) { return; }
				C->Curve = NewRamp;
				RefreshLayeredPreview();
			}
		}));
	AddSliderRow(Panel, WrapParameterControl(CurveEditor,
		MakeAddressResolver<FMixtormatGeneratorHeightCurve>(Curve, &FMixtormatGeneratorHeightCurve::Curve)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
			LOCTEXT("HeightRemapOffset", "Offset"), Curve, &FMixtormatGeneratorHeightCurve::Offset, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("HeightRemapOffsetHint", "Signed offset applied after the curve and before inversion.")),
		MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
			LOCTEXT("HeightRemapAmount", "Amount"), Curve, &FMixtormatGeneratorHeightCurve::Amount, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("HeightRemapAmountHint", "Blend the remapped result with the untouched running height. Zero bypasses the remap, including normalization."))));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightCurve() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("HeightRemapHeading", "HEIGHT REMAP")).PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatGeneratorHeightCurve* C = GetSelectedHeightCurve();
					return C && C->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S)
				{
					if (FMixtormatGeneratorHeightCurve* C = GetSelectedHeightCurve())
					{
						C->bEnabled = S == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))])
		[Panel]];
}

TSharedRef<SWidget> SMixtormat::BuildColorRampSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	if (!GetSelectedHeightColorRamp()) { return Menu.Build(); }
	Menu.Item(LOCTEXT("ColorRampSourceModuleNonePick", "None"), nullptr,
		FSimpleDelegate::CreateLambda([this]()
		{
			if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
			{
				R->SourceChildId.Invalidate();
				RefreshLayeredPreview();
			}
		}));
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		// Only earlier modules: this sublayer runs in chain order, so a later module has not
		// produced a height yet and referencing it would silently read the running chain. The
		// order filter applies only when the selection actually resolves to this ramp in this
		// array; otherwise list everything, the same as Height Blend's source menu.
		const TArray<FMixtormatLayerChild>& Children = WorkingLayers[SelectedLayerIndex].Children;
		const int32 SelfIndex = GetSelectedChildIndex();
		const bool bFilterByOrder = Children.IsValidIndex(SelfIndex)
			&& Children[SelfIndex].Type == EMixtormatLayerChildType::HeightColorRamp;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (bFilterByOrder && Index >= SelfIndex) { break; }
			const FMixtormatLayerChild& Child = Children[Index];
			const bool bIsGeneratorOrBoolean = Child.Type == EMixtormatLayerChildType::Generator
				|| Child.Type == EMixtormatLayerChildType::Boolean;
			const bool bIsFlowBehavior = Child.Type == EMixtormatLayerChildType::Behavior
				&& (Child.Behavior.Type == EMixtormatBehaviorType::FlowField
					|| Child.Behavior.Type == EMixtormatBehaviorType::Feather);
			if (!bIsGeneratorOrBoolean && !bIsFlowBehavior) { continue; }
			const FGuid ChildId = Child.ChildId;
			Menu.Item(GetLayerChildName(Child), MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, ChildId]()
				{
					if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
					{
						R->SourceChildId = ChildId;
						if (R->Source != EMixtormatColorRampSource::ModuleRef
							&& R->Source != EMixtormatColorRampSource::FlowMagnitude
							&& R->Source != EMixtormatColorRampSource::FlowAngle
							&& R->Source != EMixtormatColorRampSource::NoiseValue
							&& R->Source != EMixtormatColorRampSource::ModuleCoverage
							&& R->Source != EMixtormatColorRampSource::ModuleEdgeDistance
							&& R->Source != EMixtormatColorRampSource::RootArcLength
							&& R->Source != EMixtormatColorRampSource::RootAcross
							&& R->Source != EMixtormatColorRampSource::RegionRandom)
						{
							R->Source = EMixtormatColorRampSource::ModuleRef;
						}
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMossControls()
{
	const auto Moss = [this]() { return GetSelectedMoss(); };
	const FMixtormatMoss Defaults;
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);

	const auto Slider = [this, Moss, Defaults](const FText& Label, float FMixtormatMoss::* Member,
		double Min, double Max, double Delta, const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatMoss>(Label, Moss, Member, Min, Max, Defaults.*Member, Delta, Hint);
	};

	// Output & Simulation
	TSharedRef<SVerticalBox> Panel = AddCard(Cards, LOCTEXT("MossOutput", "OUTPUT"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossAmount", "Amount"), &FMixtormatMoss::Amount, 0, 2, .01,
			LOCTEXT("MossAmountHint", "Overall surfacing contribution after scoped masks.")),
		Slider(LOCTEXT("MossHeightScale", "Height Scale"), &FMixtormatMoss::HeightScale, -4, 4, .01,
			LOCTEXT("MossHeightScaleHint", "Scales the signed colony relief after optional normalization. Negative values invert the relief."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberEnum<FMixtormatMoss>(
			LOCTEXT("MossPreview", "Preview"), Moss, &FMixtormatMoss::Preview,
			LOCTEXT("MossPreviewHint", "Moss shows normal relief; diagnostic modes route grayscale to Height only. Named outputs remain normal.")),
		MixtormatRow::MakeTrailing(LOCTEXT("MossNormalize", "Normalize"), MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([Moss]() { const auto* M = Moss(); return M && M->bNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
			FOnCheckStateChanged::CreateLambda([this, Moss](ECheckBoxState State) { if (auto* M = Moss()) { M->bNormalizeHeight = State == ECheckBoxState::Checked; RefreshLayeredPreview(); } })),
			LOCTEXT("MossNormalizeHint", "Zero-preserving normalization of moss height before Height Scale."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatMoss>(LOCTEXT("MossWriteHeight", "Write Height"), Moss, &FMixtormatMoss::bWriteHeight,
			LOCTEXT("MossWriteHeightHint", "Write cushion and shoot relief into layer height.")),
		MakeMemberToggle<FMixtormatMoss>(LOCTEXT("MossWriteColor", "Write Color"), Moss, &FMixtormatMoss::bWriteColor,
			LOCTEXT("MossWriteColorHint", "Write colony, shoot, and living-tip pigment to albedo."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatMoss>(LOCTEXT("MossWriteRoughness", "Write Roughness"), Moss, &FMixtormatMoss::bWriteRoughness,
			LOCTEXT("MossWriteRoughnessHint", "Write dry/wet moss roughness.")),
		MakeMemberToggle<FMixtormatMoss>(LOCTEXT("MossWriteNormal", "Write Normal"), Moss, &FMixtormatMoss::bWriteNormal,
			LOCTEXT("MossWriteNormalHint", "Write 3D cushion dome and leaf blade detail normals."))));

	// Seeding & Growth
	Panel = AddCard(Cards, LOCTEXT("MossGrowth", "SEEDING & GROWTH"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatMoss>(
		LOCTEXT("MossSeedMode", "Seeding Mode"), Moss, &FMixtormatMoss::SeedMode,
		LOCTEXT("MossSeedModeHint", "Habitat & Colonies combines crevice rooting with scattered patches; Crevices & Shelter roots strictly in mortar joints, cracks, and ledges; Colony Scatter distributes patches across terrain.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossCoverage", "Coverage"), &FMixtormatMoss::Coverage, 0, 1, .01,
			LOCTEXT("MossCoverageHint", "Overall moss abundance and colonization threshold.")),
		Slider(LOCTEXT("MossSplatter", "Spore Splatter"), &FMixtormatMoss::Splatter, 0, 1, .01,
			LOCTEXT("MossSplatterHint", "Stochastic pioneer spore splatter, micro-tuft islands, and dithered crustose edge breakup around colonies."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMoss>(
			LOCTEXT("MossColonyDensity", "Colony Density"), Moss, &FMixtormatMoss::ColonyDensity, 1, 32, Defaults.ColonyDensity,
			LOCTEXT("MossColonyDensityHint", "Periodic parent-colony cells across the tile.")),
		MakeMemberSliderInt<FMixtormatMoss>(
			LOCTEXT("MossSeed", "Seed"), Moss, &FMixtormatMoss::Seed, 0, 99999, Defaults.Seed)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossSpread", "Growth Spread"), &FMixtormatMoss::GrowthSpread, 0, 1, .01,
			LOCTEXT("MossSpreadHint", "Habitat-guided expansion speed across favorable crevices and ledges.")),
		Slider(LOCTEXT("MossCompetition", "Competition"), &FMixtormatMoss::Competition, 0, 1, .01,
			LOCTEXT("MossCompetitionHint", "Contact inhibition between neighboring colonies: creates distinct inter-cushion clefts when high, or seamless merging mats when low."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMoss>(
			LOCTEXT("MossGrowthIterations", "Growth Steps"), Moss, &FMixtormatMoss::GrowthIterations, 0, 2048, Defaults.GrowthIterations,
			LOCTEXT("MossGrowthIterationsHint", "Colony-front reaction-diffusion iterations.")),
		MakeMemberSliderInt<FMixtormatMoss>(
			LOCTEXT("MossSimulationResolution", "Sim Resolution"), Moss, &FMixtormatMoss::SimulationResolution, 64, 2048, Defaults.SimulationResolution,
			LOCTEXT("MossSimulationResolutionHint", "Simulation grid resolution; micro-shoots and leaves always evaluate at full output resolution."))));

	// Habitat & Gravity Slope
	Panel = AddCard(Cards, LOCTEXT("MossHabitat", "HABITAT & GRAVITY"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossCavityAttraction", "Crevice Attraction"), &FMixtormatMoss::CavityAttraction, 0, 1, .01,
			LOCTEXT("MossCavityAttractionHint", "Attraction to mortar joints, rock cracks, bark furrows, and concave pockets.")),
		Slider(LOCTEXT("MossOcclusion", "Shelter AO"), &FMixtormatMoss::OcclusionInfluence, 0, 1, .01,
			LOCTEXT("MossOcclusionHint", "Smooth multiscale ambient shelter preference over exposed ridges."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossAnglePref", "Gravity Shelf"), &FMixtormatMoss::SurfaceAnglePreference, 0, 1, .01,
			LOCTEXT("MossAnglePrefHint", "Directional gravitational slope bias: upward-facing shelves and ledges along the Slope Angle axis catch water and spores while underside overhangs shed them.")),
		Slider(LOCTEXT("MossSlopeAngle", "Slope Angle"), &FMixtormatMoss::SlopeAngle, -180, 180, 1,
			LOCTEXT("MossSlopeAngleHint", "Downhill gravitational runoff axis in degrees (-90 = top-to-bottom). Upward shelves opposing this direction catch moss."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossMoisture", "Moisture"), &FMixtormatMoss::Moisture, 0, 1, .01,
			LOCTEXT("MossMoistureHint", "Environmental moisture availability.")),
		Slider(LOCTEXT("MossExposure", "Peak Tolerance"), &FMixtormatMoss::ExposureTolerance, 0, 1, .01,
			LOCTEXT("MossExposureHint", "Tolerance to exposed convex ridges and high peaks."))));

	// Cushion & Shoot Structure
	Panel = AddCard(Cards, LOCTEXT("MossStructure", "CUSHIONS & SHOOTS"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatMoss>(
		LOCTEXT("MossGrowthForm", "Growth Habit"), Moss, &FMixtormatMoss::GrowthForm,
		LOCTEXT("MossGrowthFormHint", "Compact Cushion (acrocarpous rosettes), Swept Tuft, Creeping Mat (pleurocarpous sheets), or Feather Weft.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossCushionHeight", "Cushion Height"), &FMixtormatMoss::CushionHeight, 0, 1, .005,
			LOCTEXT("MossCushionHeightHint", "Cushion bulk thickness (0 = flat crustose/mat film hugging the substrate, higher = plump hummock pillows).")),
		Slider(LOCTEXT("MossMatureHeight", "Thickness Variation"), &FMixtormatMoss::MatureHeight, 0, 1, .01,
			LOCTEXT("MossMatureHeightHint", "Multi-scale thickness variation from thin pioneer edges and low mats to tall billowing hummock crowns."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossCushionSize", "Cushion Span"), &FMixtormatMoss::CushionSize, 0.005, 0.15, .001,
			LOCTEXT("MossCushionSizeHint", "Cushion dome rounding diameter from colony boundaries into the mature interior.")),
		Slider(LOCTEXT("MossClumpDensity", "Sub-Pillow Cells"), &FMixtormatMoss::ClumpDensity, 0, 256, 1,
			LOCTEXT("MossClumpDensityHint", "Sub-cushion hummock division density across the tile. Zero disables sub-clump division."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossSoftness", "Crown Softness"), &FMixtormatMoss::Softness, 0, 1, .01,
			LOCTEXT("MossSoftnessHint", "Low values create crisp divided rock cushions; high values create soft merged forest pillows.")),
		Slider(LOCTEXT("MossPlumpness", "Turgor Plumpness"), &FMixtormatMoss::Plumpness, 0, 1, .01,
			LOCTEXT("MossPlumpnessHint", "Swelling of hydrated cushions and leaf rosettes."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossLeafScale", "Leaf Scale"), &FMixtormatMoss::LeafScale, .1, 8, .05,
			LOCTEXT("MossLeafScaleHint", "Relative tuft and leaf size, independent of colony spacing.")),
		Slider(LOCTEXT("MossShootLength", "Shoot Relief"), &FMixtormatMoss::ShootLength, 0, 1, .01,
			LOCTEXT("MossShootLengthHint", "3D micro-shoot relief and leaf blade prominence."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossShootDensity", "Shoot Density"), &FMixtormatMoss::ShootDensity, 0, 1, .01,
			LOCTEXT("MossShootDensityHint", "Density of micro leaf tufts aligned by cushion slope and growth flow.")),
		Slider(LOCTEXT("MossFiberThickness", "Blade Width"), &FMixtormatMoss::FiberThickness, 0, 1, .01,
			LOCTEXT("MossFiberThicknessHint", "Width of tapered leaf blades relative to tuft spacing."))));

	// Color & Surface Optics
	Panel = AddCard(Cards, LOCTEXT("MossAppearance", "COLOR & SURFACE"));
	const TSharedRef<SWidget> ColorRampEditor = SNew(SMixtormatColorRamp)
		.IsEnabled_Lambda([this, Moss]() { return Moss() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Moss(), FMixtormatMoss::StaticStruct(), TEXT("ColorRamp")); })
		.Ramp_Lambda([this, Moss, Defaults]()
		{
			if (const auto* Value = GroupControlValueFor(Moss(), FMixtormatMoss::StaticStruct(), TEXT("ColorRamp")))
			{ return Value->ColorRampValue; }
			return Moss() ? Moss()->ColorRamp : Defaults.ColorRamp;
		})
		.DomainMin(0.0f).DomainMax(1.0f)
		.OnChanged_Lambda([this, Moss](const FMixtormatColorRamp& Ramp)
		{
			if (auto* M = Moss(); M && !IsSelectedChildInstance()
				&& !GroupControlValueFor(M, FMixtormatMoss::StaticStruct(), TEXT("ColorRamp")))
			{
				M->ColorRamp = Ramp;
				RefreshLayeredPreview();
			}
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	AddSliderRow(Panel, WrapParameterControl(ColorRampEditor,
		MakeAddressResolver<FMixtormatMoss>(Moss, &FMixtormatMoss::ColorRamp)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossColonyVariation", "Colony Variation"), &FMixtormatMoss::ColonyVariation, 0, 1, .01,
			LOCTEXT("MossColonyVariationHint", "Multi-scale patch and per-colony hue, warmth, and tone variation.")),
		Slider(LOCTEXT("MossLeafVariation", "Shoot Variation"), &FMixtormatMoss::LeafVariation, 0, 1, .01,
			LOCTEXT("MossLeafVariationHint", "Per-tuft and visible-leaf pigment variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossTipColor", "Living Tips"), &FMixtormatMoss::TipColor, 0, 1, .01,
			LOCTEXT("MossTipColorHint", "Shifts raised cushion crowns and living leaf tips toward the bright top of the color ramp.")),
		Slider(LOCTEXT("MossDamage", "Dry / Senescent"), &FMixtormatMoss::Damage, 0, 1, .01,
			LOCTEXT("MossDamageHint", "Bleached golden-brown senescent shoots and collapsed dry tufts."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossHydration", "Hydration"), &FMixtormatMoss::Hydration, 0, 1, .01,
			LOCTEXT("MossHydrationHint", "Reversible leaf opening and curl (low = crisp curled dry blades, high = open turgid rosettes).")),
		Slider(LOCTEXT("MossNormalStrength", "Normal Strength"), &FMixtormatMoss::NormalStrength, 0, 3, .01,
			LOCTEXT("MossNormalStrengthHint", "3D leaf blade, tuft rosette, and cushion dome normal intensity."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossSurfaceWetness", "Surface Wetness"), &FMixtormatMoss::SurfaceWetness, 0, 1, .01,
			LOCTEXT("MossSurfaceWetnessHint", "Wet sheen interpolating roughness from Dry Roughness toward Wet Roughness.")),
		Slider(LOCTEXT("MossWetDarkening", "Wet Darkening"), &FMixtormatMoss::WetDarkening, 0, 1, .01,
			LOCTEXT("MossWetDarkeningHint", "Natural pigment darkening in wet and recessed cushion zones."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MossDryRoughness", "Dry Roughness"), &FMixtormatMoss::DryRoughness, 0, 1, .01,
			LOCTEXT("MossDryRoughnessHint", "Base matte roughness of dry moss shoots.")),
		Slider(LOCTEXT("MossWetRoughness", "Wet Roughness"), &FMixtormatMoss::WetRoughness, 0, 1, .01,
			LOCTEXT("MossWetRoughnessHint", "Specular roughness at full Surface Wetness."))));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedMoss() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup)
		.Title(LOCTEXT("MossHeading", "SURFACING · MOSS")).PresetAction(MakeModulePresetButton())
		.InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0)[
				MakeChildOutputPreviewButton([]() { FMixtormatLayerChild Probe; Probe.Type = EMixtormatLayerChildType::Surfacing;
					Probe.Surfacing.Type = EMixtormatSurfacingType::Moss; return GetChildPreviewOutputSet(Probe); }())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]() { const auto* C = ResolveChildAt(GetSelectedChildAddress()); return C && C->Surfacing.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { if (auto* C = ResolveChildAt(GetSelectedChildAddress())) { C->Surfacing.bEnabled = State == ECheckBoxState::Checked; RefreshLayeredPreview(); RebuildLayerList(); } }))])
		[Cards]];
}

TSharedRef<SWidget> SMixtormat::BuildMyceliumControls()
{
	const auto Mycelium = [this]() { return GetSelectedMycelium(); };
	const FMixtormatMycelium Defaults;
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);

	// Output
	TSharedRef<SVerticalBox> Panel = AddCard(Cards, LOCTEXT("MyceliumOutput", "OUTPUT"));
	const auto Slider = [this, Mycelium, Defaults](const FText& Label, float FMixtormatMycelium::* Member,
		double Min, double Max, double Delta, const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatMycelium>(Label, Mycelium, Member, Min, Max, Defaults.*Member, Delta, Hint);
	};
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumHeightScale", "Height Scale"), &FMixtormatMycelium::HeightScale, -4, 4, .01,
			LOCTEXT("MyceliumHeightScaleHint", "Scales the signed hyphal network relief after optional normalization. Negative values invert the relief.")),
		MixtormatRow::MakeTrailing(LOCTEXT("MyceliumNormalize", "Normalize"), MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([Mycelium]() { const auto* M = Mycelium(); return M && M->bNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
			FOnCheckStateChanged::CreateLambda([this, Mycelium](ECheckBoxState State) { if (auto* M = Mycelium()) { M->bNormalizeHeight = State == ECheckBoxState::Checked; RefreshLayeredPreview(); } })),
			LOCTEXT("MyceliumNormalizeHint", "Zero-preserving normalization of mycelium height before Height Scale."))));

	// Growth & Colonies
	Panel = AddCard(Cards, LOCTEXT("MyceliumColonies", "COLONIES & GROWTH"));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatMycelium>(
		LOCTEXT("MyceliumColonyCount", "Colony Count"), Mycelium, &FMixtormatMycelium::ColonyCount, 1, 16, Defaults.ColonyCount,
		LOCTEXT("MyceliumColonyCountHint", "Number of competing colony inoculation origins across the substrate.")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatMycelium>(
		LOCTEXT("MyceliumSeed", "Seed"), Mycelium, &FMixtormatMycelium::Seed, 0, 99999, Defaults.Seed));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumGrowthExtent", "Growth Extent"), &FMixtormatMycelium::GrowthExtent, 0, 1, .01,
			LOCTEXT("MyceliumGrowthExtentHint", "Hyphal network exploration radius from colony centers.")),
		Slider(LOCTEXT("MyceliumCoverage", "Coverage"), &FMixtormatMycelium::Coverage, 0, 1, .01,
			LOCTEXT("MyceliumCoverageHint", "Overall hyphal network density and coverage abundance."))));

	// Hyphal Hierarchy
	Panel = AddCard(Cards, LOCTEXT("MyceliumHierarchy", "HYPHAL HIERARCHY"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumNutrient", "Nutrient Attraction"), &FMixtormatMycelium::NutrientAttraction, 0, 1, .01,
			LOCTEXT("MyceliumNutrientHint", "Attraction to substrate crevices and porous high-nutrient paths.")),
		Slider(LOCTEXT("MyceliumBranching", "Branch Frequency"), &FMixtormatMycelium::BranchingFrequency, 0, 1, .01,
			LOCTEXT("MyceliumBranchingHint", "Frequency of lateral branch bifurcations."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumPersistence", "Persistence"), &FMixtormatMycelium::DirectionalPersistence, 0, 1, .01,
			LOCTEXT("MyceliumPersistenceHint", "Directional momentum and persistence against wandering.")),
		Slider(LOCTEXT("MyceliumConnectivity", "Anastomosis"), &FMixtormatMycelium::NetworkConnectivity, 0, 1, .01,
			LOCTEXT("MyceliumConnectivityHint", "Reconnection of hyphal tips into closed loops and interconnected networks."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumSecondary", "Secondary Branches"), &FMixtormatMycelium::SecondaryBranchDensity, 0, 1, .01,
			LOCTEXT("MyceliumSecondaryHint", "Density of secondary branch generations exploring lateral space.")),
		Slider(LOCTEXT("MyceliumMicro", "Microfilaments"), &FMixtormatMycelium::MicrofilamentDensity, 0, 1, .01,
			LOCTEXT("MyceliumMicroHint", "Density of delicate wispy terminal hyphae filling local space."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("MyceliumFilamentWidth", "Filament Width"), &FMixtormatMycelium::FilamentWidth, 0.003, 0.05, .001,
			LOCTEXT("MyceliumFilamentWidthHint", "Base thickness of primary hyphal strands.")),
		Slider(LOCTEXT("MyceliumTaper", "Taper"), &FMixtormatMycelium::Taper, 0, 1, .01,
			LOCTEXT("MyceliumTaperHint", "Rate of thinning towards branch tips."))));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedMycelium() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup)
		.Title(LOCTEXT("MyceliumHeading", "MYCELIUM")).PresetAction(MakeModulePresetButton())
		.InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0)[
				MakeChildOutputPreviewButton([]() { FMixtormatLayerChild Probe; Probe.Type = EMixtormatLayerChildType::Generator;
					Probe.Generator.Type = EMixtormatGeneratorType::Mycelium; return GetChildPreviewOutputSet(Probe); }())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]() { const auto* G = GetSelectedGenerator(); return G && G->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { if (auto* G = GetSelectedGenerator()) { G->bEnabled = State == ECheckBoxState::Checked; RefreshLayeredPreview(); RebuildLayerList(); } }))])
		[Cards]];
}

TSharedRef<SWidget> SMixtormat::BuildHeightColorRampControls()
{
	const auto Ramp = [this]() { return GetSelectedHeightColorRamp(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	TSharedPtr<SMixtormatColorRamp> RampWidget;
	const TSharedRef<SVerticalBox> RampCard = AddCard(Panel, LOCTEXT("ColorRampCardRamp", "RAMP"));
	const TSharedRef<SWidget> RampEditor = SAssignNew(RampWidget, SMixtormatColorRamp)
		.IsEnabled_Lambda([this, Ramp]() { return Ramp() && !GroupControlValueFor(Ramp(), FMixtormatGeneratorHeightColorRamp::StaticStruct(), TEXT("Ramp")); })
		.Ramp(TAttribute<FMixtormatColorRamp>::CreateLambda([this, Ramp]()
		{
			const FMixtormatGeneratorHeightColorRamp* R = Ramp();
			if (R)
			{
				if (const auto* Value = GroupControlValueFor(R, FMixtormatGeneratorHeightColorRamp::StaticStruct(), TEXT("Ramp")))
				{ auto Effective = *R; Effective.Ramp = Value->ColorRampValue; return Effective.GetUnitRamp(); }
			}
			return R ? R->GetUnitRamp() : FMixtormatColorRamp();
		}))
		.DomainMin(0.0f).DomainMax(1.0f)
		.OnChanged(FOnMixtormatColorRampChanged::CreateLambda([this, Ramp](const FMixtormatColorRamp& NewRamp)
		{
			if (FMixtormatGeneratorHeightColorRamp* R = Ramp())
			{
				if (GroupControlValueFor(R, FMixtormatGeneratorHeightColorRamp::StaticStruct(), TEXT("Ramp"))) { return; }
				R->SetUnitRamp(NewRamp);
				RefreshLayeredPreview();
			}
		}));
	AddSliderRow(RampCard, WrapParameterControl(RampEditor,
		MakeAddressResolver<FMixtormatGeneratorHeightColorRamp>(Ramp, &FMixtormatGeneratorHeightColorRamp::Ramp)));
	AddSliderRow(RampCard, MixtormatRow::MakePair(
		MakeMemberEnum<FMixtormatGeneratorHeightColorRamp, EMixtormatColorBlendMode>(
			LOCTEXT("ColorRampBlendMode", "Blend Mode"), Ramp, &FMixtormatGeneratorHeightColorRamp::BlendMode,
			LOCTEXT("ColorRampBlendModeHint", "How this ramp's color blends with the previous ramp or the layer's base/fill color."),
			FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); })),
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampWeight", "Weight"), Ramp, &FMixtormatGeneratorHeightColorRamp::Weight, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("ColorRampWeightHint", "How strongly this ramp colors the layer's Base Color. Scoped masks under this ramp also gate its coverage."))));

	// Where the ramp's scalar comes from. Source is always selectable even when scoped under a
	// parent module, and Module chip is shown whenever Source reads a specific module.
	const auto RampUsesModuleTarget = [Ramp]()
	{
		const FMixtormatGeneratorHeightColorRamp* R = Ramp();
		return R && (R->Source == EMixtormatColorRampSource::ModuleRef
			|| R->Source == EMixtormatColorRampSource::FlowMagnitude
			|| R->Source == EMixtormatColorRampSource::FlowAngle
			|| R->Source == EMixtormatColorRampSource::NoiseValue
			|| R->Source == EMixtormatColorRampSource::ModuleCoverage
			|| R->Source == EMixtormatColorRampSource::ModuleEdgeDistance
			|| R->Source == EMixtormatColorRampSource::RootArcLength
			|| R->Source == EMixtormatColorRampSource::RootAcross
			|| R->Source == EMixtormatColorRampSource::RegionRandom);
	};
	const TSharedRef<SVerticalBox> InputCard = AddCard(Panel, LOCTEXT("ColorRampCardInput", "INPUT"));
	AddSliderRow(InputCard,
		MakeMemberEnum<FMixtormatGeneratorHeightColorRamp, EMixtormatColorRampSource>(
			LOCTEXT("ColorRampSource", "Source"), Ramp, &FMixtormatGeneratorHeightColorRamp::Source,
			LOCTEXT("ColorRampSourceHint", "Which scalar feeds the ramp: running height, an earlier module's height, layer input height, composite below, flow magnitude/angle, or noise value."),
			FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
	AddSliderRow(InputCard, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampNormalize", "Normalize"), Ramp, &FMixtormatGeneratorHeightColorRamp::bNormalizeInput,
			LOCTEXT("ColorRampNormalizeHint", "Stretch measured source height across 0..1 before Scale and Offset. Constant height maps to 0. Off maps signed height -1..1 to 0..1 without measuring its range.")),
		MakeMemberToggle<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampMirrorRepeat", "Mirror Repeat"), Ramp, &FMixtormatGeneratorHeightColorRamp::bMirrorRepeat,
			LOCTEXT("ColorRampMirrorRepeatHint", "Reflect extended lookup coordinates at 0 and 1. Off holds the end colors. Scale 2 traverses the ramp forward then backward; negative offsets also repeat."))));
	AddSliderRow(InputCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampInputScale", "Scale"), Ramp, &FMixtormatGeneratorHeightColorRamp::InputScale, -8.0, 8.0, 1.0, 0.01),
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampInputOffset", "Offset"), Ramp, &FMixtormatGeneratorHeightColorRamp::InputOffset, -1.0, 1.0, 0.0, 0.01)));
	AddSliderRow(InputCard,
		SNew(SBox)
		.Visibility_Lambda([RampUsesModuleTarget]()
		{
			return RampUsesModuleTarget()
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeTrailing(
				LOCTEXT("ColorRampSourceModule", "Module"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp();
						if (!R)
						{
							return LOCTEXT("ColorRampSourceModuleNone", "None");
						}
						FGuid TargetId = R->SourceChildId;
						if (!TargetId.IsValid())
						{
							if (const FMixtormatLayerChild* Selected = ResolveChildAt(GetSelectedChildAddress()))
							{
								TargetId = Selected->ScopeOwnerChildId;
							}
						}
						if (!TargetId.IsValid())
						{
							return LOCTEXT("ColorRampSourceModuleAuto", "Auto");
						}
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
							{
								if (Child.ChildId == TargetId)
								{
									return GetLayerChildName(Child);
								}
							}
						}
						return LOCTEXT("ColorRampSourceModuleMissing", "Missing");
					}),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildColorRampSourceMenu)),
				LOCTEXT("ColorRampSourceModuleHint", "The earlier module whose scalar field this ramp maps. When None/Auto, uses the parent module if scoped or the nearest compatible module above."))
		]);

	const TSharedRef<SVerticalBox> ModCard = AddCard(Panel, LOCTEXT("ColorRampCardModulation", "MODULATION"));
	AddSliderRow(ModCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampFlowModulation", "Flow Shift"), Ramp, &FMixtormatGeneratorHeightColorRamp::FlowModulation, -2.0, 2.0, 0.0, 0.01,
			LOCTEXT("ColorRampFlowModulationHint", "Scale the Flow-driven lookup offset. Negative values flip the influence; zero disables it. Signed Flow lets opposite directions shift the lookup in opposite ways. Does not change color blend Weight.")),
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampFlowModulationAngle", "Flow Angle"), Ramp, &FMixtormatGeneratorHeightColorRamp::FlowModulationAngle, 0.0, 360.0, 90.0, 1.0,
			LOCTEXT("ColorRampFlowModulationAngleHint", "Direction in degrees for Flow modulation. With Signed Flow, aligned Flow gives a positive offset, opposite Flow a negative offset, and perpendicular Flow no offset, before Flow Shift is applied."))));
	AddSliderRow(ModCard, MakeMemberToggle<FMixtormatGeneratorHeightColorRamp>(
		LOCTEXT("ColorRampSignedFlow", "Signed Flow"), Ramp, &FMixtormatGeneratorHeightColorRamp::bSignedFlowModulation,
		LOCTEXT("ColorRampSignedFlowHint", "Off preserves one-sided magnitude and directional bias. On uses a signed projection along Flow Angle, so opposite Flow directions shift the lookup both ways. Flow Shift scales either mode.")));
	AddSliderRow(ModCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampUVModulation", "UV Shift"), Ramp, &FMixtormatGeneratorHeightColorRamp::UVModulation, -2.0, 2.0, 0.0, 0.01,
			LOCTEXT("ColorRampUVModulationHint", "Modulate the ramp lookup coordinate using the module's UV coordinates.")),
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampUVModulationAngle", "UV Angle"), Ramp, &FMixtormatGeneratorHeightColorRamp::UVModulationAngle, 0.0, 360.0, 45.0, 1.0,
			LOCTEXT("ColorRampUVModulationAngleHint", "Angle in degrees of the UV gradient axis used for coordinate modulation."))));
	AddSliderRow(ModCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampIdVariation", "ID Variation"), Ramp, &FMixtormatGeneratorHeightColorRamp::IdVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("ColorRampIdVariationHint", "Per-Region ID random offset applied to the ramp lookup coordinate.")),
		MakeMemberSliderInt<FMixtormatGeneratorHeightColorRamp>(
			LOCTEXT("ColorRampIdSeed", "ID Seed"), Ramp, &FMixtormatGeneratorHeightColorRamp::IdVariationSeed, 0.0, 100.0, 1,
			LOCTEXT("ColorRampIdSeedHint", "Seed for per-region ID random variation."))));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightColorRamp() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("ColorRampHeading", "COLOR RAMP")).PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
				FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakeChildOutputPreviewButton(
					GetPreviewOutputSetForChildType(EMixtormatLayerChildType::HeightColorRamp))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f,
				FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				// Presets as a compact header dropdown instead of a chip strip under the ramp.
				SNew(SComboButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
				.HasDownArrow(false)
				.ContentPadding(FMargin(0.0f))
				.IsEnabled_Lambda([this]() { const auto* R = GetSelectedHeightColorRamp(); return R && !GroupControlValueFor(R, FMixtormatGeneratorHeightColorRamp::StaticStruct(), TEXT("Ramp")); })
								.OnGetMenuContent_Lambda([RampWidget]() -> TSharedRef<SWidget>
				{
					MixtormatMenu::FBuilder Menu;
					Menu.Caption(LOCTEXT("ColorRampPresetsHeading", "PRESETS"));
					for (int32 Index = 0; Index < SMixtormatColorRamp::GetPresetCount(); ++Index)
					{
						const int32 PresetIndex = Index;
						Menu.Item(SMixtormatColorRamp::GetPresetName(PresetIndex), nullptr,
							FSimpleDelegate::CreateLambda([RampWidget, PresetIndex]()
							{
								if (RampWidget.IsValid()) { RampWidget->ApplyPreset(PresetIndex); }
							}));
					}
					return Menu.Build();
				})
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("ColorRampPresets", "Ramp Presets"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						[
							SNew(SImage)
							.Image(MixtormatIcons::ChevronDown())
							.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp();
					return R && R->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S)
				{
					if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
					{
						R->bEnabled = S == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))])
		[Panel]];
}

TSharedRef<SWidget> SMixtormat::BuildRockFormationControls()
{
	const auto Rock = [this]() { return GetSelectedRockFormation(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("RockOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockHeightScale", "Scale"), Rock, &FMixtormatRockFormation::RockHeightScale, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("RockHeightScaleHint", "Scales the signed generator height after normalization.")),
			MixtormatRow::MakeTrailing(
				LOCTEXT("RockNormalizeHeight", "Normalize"),
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Rock]()
					{
						const FMixtormatRockFormation* G = Rock();
						return G && G->bRockNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Rock](const ECheckBoxState State)
					{
						if (FMixtormatRockFormation* G = Rock())
						{
							G->bRockNormalizeHeight = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					})),
				LOCTEXT("RockNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5."))));
		AddSliderRow(Output, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockDepthMin", "Depth Min"), Rock, &FMixtormatRockFormation::RockDepthMin, -4.0, 4.0, -1.0, 0.01,
				LOCTEXT("RockDepthMinHint", "Minimum depth value after remapping (default -1).")),
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockDepthMax", "Depth Max"), Rock, &FMixtormatRockFormation::RockDepthMax, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("RockDepthMaxHint", "Maximum depth value after remapping (default 1)."))));
	}

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("RockGrpShape", "SHAPE"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStyle", "Style"), Rock, &FMixtormatRockFormation::RockStyle, 0.0, 1.0, 0.05, 0.01,
			LOCTEXT("RockStyleHint", "0..1 blends preset positions 1..2.5, from layered through boulder toward rubble.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockCells", "Cells"), Rock, &FMixtormatRockFormation::RockCells, 1.0, 32.0, 4,
			LOCTEXT("RockCellsHint", "Cells across one repeat of the tile."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockRows", "Rows"), Rock, &FMixtormatRockFormation::RockRows, 1.0, 32.0, 12,
			LOCTEXT("RockRowsHint", "Number of rock rows from top to bottom of the tile.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockSeed", "Seed"), Rock, &FMixtormatRockFormation::RockSeed, 0.0, 9999.0, 1,
			LOCTEXT("RockSeedHint", "Changes cell layout, fractures, lean, jag and chips."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFracture", "Fracture"), Rock, &FMixtormatRockFormation::RockFracture, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockFractureHint", "Scales how many times each cell is split into chunks.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFractureHeightBias", "Fracture Height Bias"), Rock, &FMixtormatRockFormation::RockFractureHeightBias, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockFractureHeightBiasHint", "Randomly raises or lowers individual fractured pieces."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSizeRandom", "Size Random"), Rock, &FMixtormatRockFormation::RockSizeRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSizeRandomHint", "Mixes big and small cells while preserving the seamless partition.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretch", "Stretch"), Rock, &FMixtormatRockFormation::RockStretch, -1.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockStretchHint", "0 is neutral; +1 doubles cells along Stretch Angle, -1 across it."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretchAngle", "Stretch Angle"), Rock, &FMixtormatRockFormation::RockStretchAngle, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockStretchAngleHint", "Direction cells elongate in. 0..1 is half a turn.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretchRandom", "Stretch Random"), Rock, &FMixtormatRockFormation::RockStretchRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockStretchRandomHint", "Narrows each chunk along a random axis, inside its own space."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockHeightClusters", "Height Clusters"), Rock, &FMixtormatRockFormation::RockHeightClusters, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockHeightClustersHint", "Scales clustered height variation between chunks and rows.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSkew", "Skew"), Rock, &FMixtormatRockFormation::RockSkew, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockSkewHint", "Offsets rock rows diagonally in cell widths while preserving tile wrapping."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpTilt", "TILT"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltAngle", "Angle"), Rock, &FMixtormatRockFormation::RockTiltAngle, -1.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockTiltAngleHint", "Shared lean toward Direction. +/-1 is +/- one eighth of a turn; walls and chamfers follow.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltDirection", "Direction"), Rock, &FMixtormatRockFormation::RockTiltDirection, 0.0, 1.0, 0.75, 0.01,
			LOCTEXT("RockTiltDirectionHint", "Which way rocks lean. 0..1 is a full turn."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltRandom", "Random"), Rock, &FMixtormatRockFormation::RockTiltRandom, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockTiltRandomHint", "Random lean per rock; 1 is one eighth of a turn. Pieces share their rock's lean with a small deviation.")),
		SNullWidget::NullWidget));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSpin", "Spin"), Rock, &FMixtormatRockFormation::RockSpin, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSpinHint", "Rotates each chunk about the vertical axis. +/-1 is +/- half a turn; chunks scale uniformly to fit, without clipping corners.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSpinRandom", "Spin Random"), Rock, &FMixtormatRockFormation::RockSpinRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSpinRandomHint", "Random spin per chunk. 1 adds up to half a turn in either direction."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpEdges", "EDGES"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockGap", "Gap"), Rock, &FMixtormatRockFormation::RockGap, -1.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockGapHint", "Positive shrinks rock regions; zero preserves their size; negative expands them. Shapes height only, never layer transparency.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamfer", "Chamfer"), Rock, &FMixtormatRockFormation::RockChamfer, -1.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockChamferHint", "Share of each edge's room to the chunk centre, scaled by 0.75. Positive cuts the bevel in; zero is no chamfer; negative raises a lip. Seams take 0.6 of the outline chamfer."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamferRandom", "Chamfer Random"), Rock, &FMixtormatRockFormation::RockChamferRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockChamferRandomHint", "Varies chamfer width between chunk edges.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamferJag", "Chamfer Jag"), Rock, &FMixtormatRockFormation::RockChamferJag, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockChamferJagHint", "Varies bevel width along each rim with its own jag pattern. Seams take 1.5 times the variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockEdgeJag", "Edge Jag"), Rock, &FMixtormatRockFormation::RockEdgeJag, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockEdgeJagHint", "Zigzag strength relative to each chunk. Seams use half the strength at twice the frequency.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockJagScale", "Jag Scale"), Rock, &FMixtormatRockFormation::RockJagScale, 1.0, 16.0, 4.0, 0.01,
			LOCTEXT("RockJagScaleHint", "Jag frequency relative to each chunk's size, not the tile or pixels."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockJagDetail", "Jag Detail"), Rock, &FMixtormatRockFormation::RockJagDetail, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockJagDetailHint", "Strength of the finer zigzag octaves.")),
		SNullWidget::NullWidget));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockRimChips", "Rim Chips"), Rock, &FMixtormatRockFormation::RockRimChips, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockRimChipsHint", "Angular bites along the rims, widening the chamfer around each chip.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockRimChipSize", "Rim Chip Size"), Rock, &FMixtormatRockFormation::RockRimChipSize, 0.0, 1.0, 0.075, 0.001,
			LOCTEXT("RockRimChipSizeHint", "Chip size relative to each chunk."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpFacets", "FACETS"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetChips", "Chips"), Rock, &FMixtormatRockFormation::RockFacetChips, -1.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockFacetChipsHint", "Depth of faceted cuts into each chunk, eased so planes never pass its centre. Positive cuts in; zero is flat; negative raises the facets out.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockFacetIterations", "Iterations"), Rock, &FMixtormatRockFormation::RockFacetIterations, 0.0, 8.0, 3,
			LOCTEXT("RockFacetIterationsHint", "Facet rounds, adding 3, 5, 7 and then more planes. 0 disables facet cuts."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetFalloff", "Falloff"), Rock, &FMixtormatRockFormation::RockFacetFalloff, 0.0, 4.0, 4.0, 0.01,
			LOCTEXT("RockFacetFalloffHint", "Scales each successive facet round. Values above 1 grow later rounds.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetRandom", "Random"), Rock, &FMixtormatRockFormation::RockFacetRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockFacetRandomHint", "Varies round count, depth and falloff per rock. Pieces take a small share of their own variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetAlign", "Align"), Rock, &FMixtormatRockFormation::RockFacetAlign, -1.0, 1.0, 0.75, 0.01,
			LOCTEXT("RockFacetAlignHint", "Positive swings facet planes toward the lean's low side; negative toward its high side.")),
		SNullWidget::NullWidget));

	AddNativeFlowControls(Cards);

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRockFormation() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RockFormationHeading", "ROCK FORMATION")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						// The generator kind decides the outputs, so the probe names it.
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::RockFormation;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGenerator* Generator = GetSelectedGenerator())
							{
								Generator->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("RockFormationEnabledHint", "Enable this rock formation"))
				])
			[
				Cards
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildNoiseControls()
{
	const auto Noise = [this]() { return GetSelectedNoise(); };
	TArray<EMixtormatNoiseType> NoiseTypes;
	const UEnum* NoiseEnum = StaticEnum<EMixtormatNoiseType>();
	for (int32 Index = 0; Index < NoiseEnum->NumEnums(); ++Index)
	{
		const int64 Value = NoiseEnum->GetValueByIndex(Index);
		if (Value != INDEX_NONE && !NoiseEnum->HasMetaData(TEXT("Hidden"), Index))
		{
			NoiseTypes.Add(static_cast<EMixtormatNoiseType>(Value));
		}
	}
	const auto NoiseWidgetIndex = [Noise, NoiseTypes]() -> int32
	{
		const FMixtormatNoise* N = Noise();
		if (!N) { return INDEX_NONE; }
		const int32 TypeIndex = NoiseTypes.IndexOfByKey(N->NoiseType);
		if (TypeIndex == INDEX_NONE) { return INDEX_NONE; }
		// Distinct primary previews: Noise Value, Flow preset direction, or written Flow.
		const int32 OutputMode = N->bNoiseWriteFlow ? 2
			: N->NoisePreset == EMixtormatNoisePreset::Flow ? 1 : 0;
		return TypeIndex * 3 + OutputMode;
	};
	TSharedRef<SWidgetSwitcher> HeaderPreview = SNew(SWidgetSwitcher).WidgetIndex_Lambda(NoiseWidgetIndex);
	// Noise keeps its single header preview control like the other generators.
	// Published fields and Copy Output remain available in the child context menu;
	// don't duplicate their per-output preview/copy rows inside the Inspector.
	for (const EMixtormatNoiseType NoiseType : NoiseTypes)
	{
		for (int32 OutputMode = 0; OutputMode < 3; ++OutputMode)
		{
			FMixtormatLayerChild Probe;
			Probe.Type = EMixtormatLayerChildType::Generator;
			Probe.Generator.Type = EMixtormatGeneratorType::Noise;
			Probe.Generator.Noise.NoiseType = NoiseType;
			Probe.Generator.Noise.NoisePreset = OutputMode == 1
				? EMixtormatNoisePreset::Flow : EMixtormatNoisePreset::Noise;
			Probe.Generator.Noise.bNoiseWriteFlow = OutputMode == 2;
			HeaderPreview->AddSlot()[MakeChildOutputPreviewButton(GetChildPreviewOutputSet(Probe))];
		}
	}
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Cards, LOCTEXT("NoiseOutput", "OUTPUT"));
		// Independent output gates (P0 contract section 2). Both presets show the same controls;
		// the toggles change what is written, not the node's identity.
		AddSliderRow(Output, MixtormatRow::MakePair(
			MixtormatRow::MakeTrailing(LOCTEXT("NoiseWriteHeight", "Height"),
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Noise]()
					{
						const FMixtormatNoise* N = Noise();
						return N && N->bNoiseWriteHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Noise](const ECheckBoxState State)
					{
						if (FMixtormatNoise* N = Noise())
						{
							N->bNoiseWriteHeight = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					}),
					LOCTEXT("NoiseWriteHeightHint", "Contribute signed height to the generator layer. Off leaves the scalar field available for gradients and Flow."))),
			MixtormatRow::MakeTrailing(LOCTEXT("NoiseWriteFlow", "Flow"),
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Noise]()
					{
						const FMixtormatNoise* N = Noise();
						return N && N->bNoiseWriteFlow ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Noise](const ECheckBoxState State)
					{
						if (FMixtormatNoise* N = Noise())
						{
							N->bNoiseWriteFlow = State == ECheckBoxState::Checked;
							UpdateAutomaticFlowPreview();
							RefreshLayeredPreview();
						}
					}),
					LOCTEXT("NoiseWriteFlowHint", "Generate and publish the weighted MODE Flow. Off keeps the Gradient and FlowDirection outputs.")))));
		const auto bHeightOutput = [Noise]()
		{
			const FMixtormatNoise* N = Noise();
			return N && N->bNoiseWriteHeight;
		};
		AddSliderRow(Output, SNew(SBox).Visibility_Lambda([bHeightOutput]()
			{ return bHeightOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseHeightScale", "Scale"), Noise, &FMixtormatNoise::NoiseHeightScale, -4.0, 4.0, 1.0, 0.01,
					LOCTEXT("NoiseHeightScaleHint", "Scales the signed generator height after centring.")),
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseHeightBias", "Bias"), Noise, &FMixtormatNoise::NoiseHeightBias, -1.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseHeightBiasHint", "Offsets the centred height, before Scale.")))]);
		AddSliderRow(Output, SNew(SBox).Visibility_Lambda([bHeightOutput]()
			{ return bHeightOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakeTrailing(
				LOCTEXT("NoiseNormalizeHeight", "Normalize"),
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([Noise]()
						{
							const FMixtormatNoise* N = Noise();
							return N && N->bNoiseNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this, Noise](const ECheckBoxState State)
						{
							if (FMixtormatNoise* N = Noise())
							{
								N->bNoiseNormalizeHeight = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
							}
						}),
						LOCTEXT("NoiseNormalizeHeightHint", "Centre height onto -1..1 so Height Blend compares fairly. Off uses the raw field.")))]);
	}
	Cards->AddSlot().AutoHeight()[BuildNoisePatternPlacementControls(Noise)];
	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedNoise() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(GetSelectedNoise() && GetSelectedNoise()->NoisePreset == EMixtormatNoisePreset::Flow
				? LOCTEXT("FlowHeading", "FLOW") : LOCTEXT("NoiseHeading", "NOISE")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
					FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					HeaderPreview
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGenerator* Generator = GetSelectedGenerator())
							{
								Generator->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("NoiseEnabledHint", "Enable this noise module"))
				])
			[
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildNoisePatternPlacementControls(TFunction<FMixtormatNoise*()> Noise, bool bMaskEmbedded)
{
	const auto IsDirectional = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::Bars
			|| N->NoiseType == EMixtormatNoiseType::Phasor);
	};
	const auto IsPhasor = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && N->NoiseType == EMixtormatNoiseType::Phasor;
	};
	const auto IsWorley = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::WorleyF1
			|| N->NoiseType == EMixtormatNoiseType::WorleyF2
			|| N->NoiseType == EMixtormatNoiseType::WorleyF1MinusF2);
	};
	const auto LayerMixAddress = MakeAddressResolver(Noise, &FMixtormatNoise::NoiseLayerMix);
	const auto IsMultiOctave = [this, Noise, LayerMixAddress]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::FBM
			|| N->NoiseType == EMixtormatNoiseType::Ridged
			|| N->NoiseType == EMixtormatNoiseType::Billow
			|| GetEffectiveFloatParameter(LayerMixAddress(), N->NoiseLayerMix) > 0.0f);
	};
	const auto HasNativeOctaves = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::FBM
			|| N->NoiseType == EMixtormatNoiseType::Ridged
			|| N->NoiseType == EMixtormatNoiseType::Billow);
	};
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Pattern = AddCard(Cards, LOCTEXT("NoisePattern", "PATTERN"));
		AddSliderRow(Pattern, MakeMemberEnum<FMixtormatNoise, EMixtormatNoiseType>(
			LOCTEXT("NoiseType", "Type"), Noise, &FMixtormatNoise::NoiseType,
			LOCTEXT("NoiseTypeHint", "Noise family. Bars and Phasor use Direction; Worley produces cell IDs."),
			FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
		AddSliderRow(Pattern, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatNoise>(
				LOCTEXT("NoiseSeed", "Seed"), Noise, &FMixtormatNoise::NoiseSeed, 0.0, 9999.0, 1),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseScale", "Scale"), Noise, &FMixtormatNoise::NoiseScale, 1.0, 64.0, 8.0, 0.1,
				LOCTEXT("NoiseScaleHint", "Overall lattice density. Non-uniform Size X/Y changes element aspect without breaking tiling."))));
		AddSliderRow(Pattern, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseElementSizeX", "Size X"), Noise, &FMixtormatNoise::NoiseElementSizeX,
				0.125, 4.0, 1.0, 0.025,
				LOCTEXT("NoiseElementSizeXHint", "Element width relative to Scale. Larger gives wider cells; the periodic X count is rounded to an integer.")),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseElementSizeY", "Size Y"), Noise, &FMixtormatNoise::NoiseElementSizeY,
				0.125, 4.0, 1.0, 0.025,
				LOCTEXT("NoiseElementSizeYHint", "Element height relative to Scale. Larger gives taller cells; the Y period wraps independently."))));
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorFrequency", "Frequency"),
					Noise, &FMixtormatNoise::NoisePhasorFrequency, 0.0, 12.0, 2.0, 0.05),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorAnisotropy", "Anisotropy"),
					Noise, &FMixtormatNoise::NoisePhasorAnisotropy, 0.0, 8.0, 0.0, 0.05))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorPhase", "Phase Variation"),
					Noise, &FMixtormatNoise::NoisePhasorPhaseVariation, 0.0, 1.0, 0.5, 0.01),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorOrientation", "Orientation Variation"),
					Noise, &FMixtormatNoise::NoisePhasorOrientationVariation, 0.0, 3.14, 0.35, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MakeMemberSliderInt<FMixtormatNoise>(LOCTEXT("NoisePhasorComponents", "Components"),
				Noise, &FMixtormatNoise::NoisePhasorComponents, 1.0, 4.0, 2,
				LOCTEXT("NoisePhasorComponentsHint", "Overlapping seeded impulse layers."))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorScale", "Phasor Scale"),
					Noise, &FMixtormatNoise::NoisePhasorScale, 0.0, 4.0, 1.0, 0.01),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorBias", "Bias"),
					Noise, &FMixtormatNoise::NoisePhasorBias, -1.0, 1.0, 0.0, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsWorley]()
			{ return IsWorley() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberEnum<FMixtormatNoise, EMixtormatNoiseWorleyMetric>(
					LOCTEXT("NoiseWorleyMetric", "Metric"), Noise, &FMixtormatNoise::NoiseWorleyMetric),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseWorleyJitter", "Jitter"),
					Noise, &FMixtormatNoise::NoiseWorleyJitter, 0.0, 1.0, 1.0, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsWorley]()
			{ return IsWorley() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseWorleyCellDepth", "Cell Depth"),
				Noise, &FMixtormatNoise::NoiseWorleyCellDepth, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("NoiseWorleyCellDepthHint", "Modulates cell distance without moving Voronoi feature points or IDs."))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([HasNativeOctaves]()
			{ return HasNativeOctaves() ? EVisibility::Collapsed : EVisibility::Visible; })[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseLayerMix", "Layer Mix"),
				Noise, &FMixtormatNoise::NoiseLayerMix, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("NoiseLayerMixHint", "Mix additional octave layers into this noise. Zero preserves its original one-octave field."))]);
		AddSliderRow(Pattern,
			SNew(SBox)
			.Visibility_Lambda([IsMultiOctave]() { return IsMultiOctave() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MixtormatRow::MakePair(
					MakeMemberSliderInt<FMixtormatNoise>(
						LOCTEXT("NoiseDetail", "Detail"), Noise, &FMixtormatNoise::NoiseDetail, 1.0, 8.0, 4,
						LOCTEXT("NoiseDetailHint", "Octave count for FBM/Ridged/Billow and enabled layered noise families.")),
					MakeMemberSlider<FMixtormatNoise>(
						LOCTEXT("NoiseRoughness", "Roughness"), Noise, &FMixtormatNoise::NoiseRoughness, 0.0, 1.0, 0.5, 0.01))
			]);
		AddSliderRow(Pattern,
			SNew(SBox)
			.Visibility_Lambda([IsMultiOctave]() { return IsMultiOctave() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseLacunarity", "Lacunarity"), Noise, &FMixtormatNoise::NoiseLacunarity, 1.0, 4.0, 2.0, 0.01,
					LOCTEXT("NoiseLacunarityHint", "Frequency step between octaves."))
			]);
		AddSliderRow(Pattern,
			SNew(SBox)
			.Visibility_Lambda([IsDirectional]() { return IsDirectional() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseDirection", "Direction"), Noise, &FMixtormatNoise::NoiseDirection, 0.0, 360.0, 0.0, 1.0,
					LOCTEXT("NoiseDirectionHint", "Stripe or phasor orientation, snapped to a tileable direction."))
			]);
	}
	{
		const TSharedRef<SVerticalBox> Distort = AddCard(Cards, LOCTEXT("NoiseDistortion", "DISTORTION"));
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionStrength", "Strength"),
				Noise, &FMixtormatNoise::NoiseDistortionStrength, 0.0, 2.0, 0.0, 0.01,
				LOCTEXT("NoiseDistortionStrengthHint", "Zero bypasses smooth/curl distortion; Jaggedness is independent.")),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionFrequency", "Frequency"),
				Noise, &FMixtormatNoise::NoiseDistortionFrequency, 1.0, 64.0, 4.0, 1.0)));
		AddSliderRow(Distort, MakeMemberSlider<FMixtormatNoise>(
			LOCTEXT("NoiseDistortionJaggedness", "Jaggedness"), Noise,
			&FMixtormatNoise::NoiseDistortionJaggedness, 0.0, 2.0, 0.0, 0.01,
			LOCTEXT("NoiseDistortionJaggednessHint", "Angular crease warp, blended with the smooth distortion.")));
		// Crease shaping only matters once a jagged warp is actually authored, so the rows are
		// collapsed at zero rather than left greyed out.
		AddSliderRow(Distort, SNew(SBox).Visibility_Lambda([Noise]()
			{
				const FMixtormatNoise* N = Noise();
				return N && N->NoiseDistortionJaggedness > 0.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseJaggedSharpness", "Jagged Sharpness"), Noise,
					&FMixtormatNoise::NoiseJaggedSharpness, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseJaggedSharpnessHint", "Breaks per cell edge. Higher packs tighter; amplitude follows.")),
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseJaggedDetail", "Jagged Variation"), Noise,
					&FMixtormatNoise::NoiseJaggedDetail, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseJaggedDetailHint", "Fracture scales stacked. 0 is a single scale.")))]);
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatNoise>(LOCTEXT("NoiseDistortionOctaves", "Octaves"),
				Noise, &FMixtormatNoise::NoiseDistortionOctaves, 1.0, 8.0, 2),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionRoughness", "Roughness"),
				Noise, &FMixtormatNoise::NoiseDistortionRoughness, 0.0, 1.0, 0.5, 0.01)));
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionLacunarity", "Lacunarity"),
					Noise, &FMixtormatNoise::NoiseDistortionLacunarity, 1.0, 4.0, 2.0, 0.05),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionCurlMix", "Curl Mix"),
					Noise, &FMixtormatNoise::NoiseDistortionCurlMix, 0.0, 1.0, 1.0, 0.01,
					LOCTEXT("NoiseDistortionCurlMixHint", "0 = directional, 1 = curl-driven domain distortion."))));
		AddSliderRow(Distort, SNew(SBox).Visibility_Lambda([Noise]()
			{
				const FMixtormatNoise* N = Noise();
				return N && N->NoiseDistortionCurlMix < 1.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionDirection", "Direction"),
				Noise, &FMixtormatNoise::NoiseDistortionDirection, 0.0, 360.0, 0.0, 1.0)]);
	}

	if (!bMaskEmbedded)
	{
	{
		// MODE uses the node's own Height, plus the current ordered surface Height
		// for Slope. Scoped Flow contributions accumulate automatically through Add/Mix.
		const auto bFlowOutput = [Noise]()
		{
			const FMixtormatNoise* N = Noise();
			return N && N->bNoiseWriteFlow;
		};
		const auto AddFlowOnlyCard = [Cards, bFlowOutput](const FText& Title)
		{
			TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			Cards->AddSlot().AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([bFlowOutput]()
				{
					return bFlowOutput() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SMixtormatInspectorCard)
					.Title(Title)
					[
						Rows
					]
				]
			];
			return Rows;
		};
		const TSharedRef<SVerticalBox> Mode = AddFlowOnlyCard(LOCTEXT("NoiseMode", "MODE"));
		AddSliderRow(Mode, SNew(SBox).Visibility_Lambda([bFlowOutput]()
			{ return bFlowOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeHeight", "Height"),
					Noise, &FMixtormatNoise::NoiseDirectionHeightWeight, -4.0, 4.0, 1.0, 0.01,
					LOCTEXT("NoiseModeHeightHint", "Downhill vectors from this node's own noise height.")),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeCurl", "Curl"),
					Noise, &FMixtormatNoise::NoiseDirectionCurlWeight, -4.0, 4.0, 0.0, 0.01,
					LOCTEXT("NoiseModeCurlHint", "Rotational variation from the shared periodic curl.")))]);
		AddSliderRow(Mode, SNew(SBox).Visibility_Lambda([bFlowOutput]()
			{ return bFlowOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeConstant", "Force"),
					Noise, &FMixtormatNoise::NoiseDirectionConstantWeight, -4.0, 4.0, 0.0, 0.01,
					LOCTEXT("NoiseModeConstantHint", "Signed Force driven by Angle and Gravity.")),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeAngle", "Angle"),
					Noise, &FMixtormatNoise::NoiseDirectionAngle, 0.0, 360.0, 0.0, 1.0,
					LOCTEXT("NoiseModeAngleHint", "Force orientation in degrees.")))]);
		AddSliderRow(Mode, SNew(SBox).Visibility_Lambda([bFlowOutput]()
			{ return bFlowOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeSlope", "Slope"),
					Noise, &FMixtormatNoise::NoiseDirectionSlopeWeight, -4.0, 4.0, 0.0, 0.01,
					LOCTEXT("NoiseModeSlopeHint", "Downhill direction of the preceding working surface height.")),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseModeStrength", "Strength"),
					Noise, &FMixtormatNoise::NoiseDirectionStrength, -4.0, 4.0, 1.0, 0.01,
					LOCTEXT("NoiseModeStrengthHint", "Final scalar on the combined generated Flow.")))]);
		AddSliderRow(Mode, MakeMemberSlider<FMixtormatNoise>(
			LOCTEXT("NoiseModeGravity", "Gravity / Surface Follow"), Noise,
			&FMixtormatNoise::NoiseDirectionGravity, 0.0, 2.0, 0.0, 0.01));
		const TSharedRef<SVerticalBox> Composition = AddFlowOnlyCard(LOCTEXT("NoiseFlowComposition", "COMPOSITION"));
		AddSliderRow(Composition, SNew(SBox).Visibility_Lambda([bFlowOutput]()
			{ return bFlowOutput() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseFlowAdd", "Add"),
					Noise, &FMixtormatNoise::NoiseFlowAdd, -4.0, 4.0, 1.0, 0.01,
					LOCTEXT("NoiseFlowAddHint", "Add this generated Flow to the preceding working Flow.")),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseFlowMix", "Mix"),
					Noise, &FMixtormatNoise::NoiseFlowMix, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseFlowMixHint", "Replace or interpolate the accumulated Flow inside this node's mask.")))]);
	}
	{
		const TSharedRef<SVerticalBox> Place = AddCard(Cards, LOCTEXT("NoisePlacement", "PLACEMENT"));
		AddSliderRow(Place, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseOffsetX", "Offset X"), Noise, &FMixtormatNoise::NoiseOffsetX, -1.0, 1.0, 0.0, 0.01),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseOffsetY", "Offset Y"), Noise, &FMixtormatNoise::NoiseOffsetY, -1.0, 1.0, 0.0, 0.01)));
	}

	}
	return Cards;
}

#undef LOCTEXT_NAMESPACE
