// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "UI/Rows/SMixtormatRow.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildHeightBlendControls()
{
	// Every row in this panel goes through here, so the panel converts by changing one body.
	const auto NumericRow = [this](
		const FText& Label,
		float FMixtormatLayer::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue) -> TSharedRef<SWidget>
	{
		return MakeMemberSlider<FMixtormatLayer>(
			Label,
			[this]() -> FMixtormatLayer*
			{
				return WorkingLayers.IsValidIndex(SelectedLayerIndex)
					? &WorkingLayers[SelectedLayerIndex]
					: nullptr;
			},
			Member,
			MinValue,
			MaxValue,
			DefaultValue,
			Delta);
	};

	// The shared Height Blend block (Threshold, Edge Softness, the two biases) lives on the layer's
	// FMixtormatHeightBlend, the same struct a module edits.
	const auto BlendRow = [this](
		const FText& Label,
		float FMixtormatHeightBlend::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue) -> TSharedRef<SWidget>
	{
		return MakeMemberSlider<FMixtormatHeightBlend>(
			Label,
			[this]() -> FMixtormatHeightBlend*
			{
				return WorkingLayers.IsValidIndex(SelectedLayerIndex)
					? &WorkingLayers[SelectedLayerIndex].HeightBlend
					: nullptr;
			},
			Member,
			MinValue,
			MaxValue,
			DefaultValue,
			Delta);
	};

	const TSharedRef<SVerticalBox> ContactCards = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> ContactBorders = AddCard(
		ContactCards, LOCTEXT("HeightContactBorders", "CONTACT BORDERS"));
	const TSharedRef<SVerticalBox> ContactAO = AddCard(
		ContactBorders, LOCTEXT("HeightContactAOGroup", "CONTACT AO"), nullptr,
		SNew(SMixtormatLayerIcon)
			.bOn_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::ContactAO; })
			.bActive_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::ContactAO; })
			.ToolTipText(LOCTEXT("PreviewContactAO", "Preview Contact AO coverage in unlit dark red and cyan"))
			.OnClicked_Lambda([this]() { ToggleFeaturePreview(EMixtormatDebugPreviewMode::ContactAO); }));
	AddSliderRow(ContactAO, NumericRow(LOCTEXT("HeightContactAOAmount", "Amount"), &FMixtormatLayer::HeightContactAOAmount, 0.0f, 1.0f, 0.01f, 0.0f));
	AddSliderRow(ContactAO, NumericRow(LOCTEXT("HeightContactAOWidth", "Width"), &FMixtormatLayer::HeightContactAOWidth, 0.0001f, 1.0f, 0.005f, 0.05f));
	const TSharedRef<SVerticalBox> BorderNormal = AddCard(
		ContactBorders, LOCTEXT("HeightBorderNormalGroup", "BORDER NORMAL"), nullptr,
		SNew(SMixtormatLayerIcon)
			.bOn_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::BorderNormal; })
			.bActive_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::BorderNormal; })
			.ToolTipText(LOCTEXT("PreviewBorderNormal", "Preview Border Normal coverage in unlit dark red and cyan"))
			.OnClicked_Lambda([this]() { ToggleFeaturePreview(EMixtormatDebugPreviewMode::BorderNormal); }));
	AddSliderRow(BorderNormal, NumericRow(LOCTEXT("HeightBorderLift", "Lift"), &FMixtormatLayer::HeightBorderLift, -1.0f, 1.0f, 0.005f, 0.0f));
	AddSliderRow(BorderNormal, NumericRow(LOCTEXT("HeightBorderWidth", "Width"), &FMixtormatLayer::HeightBorderWidth, 0.0001f, 1.0f, 0.005f, 0.05f));

	// Both contact effects differentiate the same height field. Smooth that shared field
	// before either reads it; width changes the band, not its underlying detail.
	AddSliderRow(ContactBorders, NumericRow(LOCTEXT("HeightBorderSmoothing", "Smoothing"), &FMixtormatLayer::HeightBorderSmoothing, 1.0f, 32.0f, 0.25f, 1.0f));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
			{
				return EVisibility::Collapsed;
			}
			const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
			// The settings open only at Op = Height Blend; the BLEND row in COMPOSITION chooses it.
			return Layer.ChannelMode == EMixtormatLayerChannelMode::CompleteSurface
				&& Layer.HeightBlend.Op == EMixtormatHeightOp::HeightBlend
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})

		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("HeightMaskBlendingHeading", "HEIGHT BLEND"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::HeightBlend,
						LOCTEXT("PreviewHeightBlendMask", "Preview the computed height blend mask in unlit dark red and cyan"))
				])
			[
				SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				NumericRow(LOCTEXT("HeightMaskStrength", "Blend Strength"), &FMixtormatLayer::HeightBlendAmount, 0.0f, 4.0f, 0.01f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					BlendRow(LOCTEXT("HeightBlendThreshold", "Threshold"), &FMixtormatHeightBlend::Threshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					BlendRow(LOCTEXT("HeightSoftness", "Edge Softness"), &FMixtormatHeightBlend::EdgeSoftness, 0.0f, 1.0f, 0.005f, 0.1f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					BlendRow(LOCTEXT("BaseHeightBias", "Base Bias"), &FMixtormatHeightBlend::BaseBias, -1.0f, 1.0f, 0.01f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					BlendRow(LOCTEXT("BlendHeightBias", "Blend Bias"), &FMixtormatHeightBlend::BlendBias, -1.0f, 1.0f, 0.01f, 0.0f)
				]

				// Rounds the height field itself, which is why it sits here rather than under
				// Contact Borders: Contact AO and Border Normal are both consumers of that field,
				// and this shapes it before either reads it.
				//
				// Radius is the control that matters. A placement mask is a step, so a layer's
				// height drops from full to nothing across one texel and the layer reads as a
				// decal laid on the surface. Blurring the mask and taking the height from the
				// blurred copy makes that a ramp, and once the radius is wide enough for a shape's
				// two blurred edges to overlap, its interior domes -- a fillet rather than a
				// softened edge. At 0 the two blur passes are skipped entirely.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					NumericRow(LOCTEXT("HeightSmoothRadius", "Smooth Radius"), &FMixtormatLayer::HeightSmoothRadius, 0.0f, 32.0f, 0.5f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					NumericRow(LOCTEXT("HeightSmoothAmount", "Smooth Amount"), &FMixtormatLayer::HeightSmoothAmount, 0.0f, 1.0f, 0.01f, 1.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SBox)
				.Visibility_Lambda([this]()
				{
					if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
					{
						return EVisibility::Collapsed;
					}
					// A fill shows its height in FILL PROPERTIES instead.
					if (WorkingLayers[SelectedLayerIndex].Type == EMixtormatLayerType::Fill)
					{
						return EVisibility::Collapsed;
					}
					const UMixtormatSurface* Surface =
						WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
					return Surface && Surface->bHasBlendHeight
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					NumericRow(LOCTEXT("ConstantHeight", "Layer Height (No RAMH)"), &FMixtormatLayer::ConstantHeight, 0.0f, 1.0f, 0.01f, 0.5f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, MixtormatTokens::SliderRowGap)
			[
				ContactCards
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]()
				{
					return WorkingLayers.IsValidIndex(SelectedLayerIndex)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.ToolTipText(LOCTEXT(
					"InvertBaseHeightHint",
					"Invert the accumulated height from layers below before comparing it with this layer."))
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					MixtormatRow::MakeTrailing(
						LOCTEXT("InvertBaseHeight", "Invert Base Height"),
						MixtormatRow::MakeCheckbox(
							TAttribute<ECheckBoxState>::CreateLambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].bInvertHeight
									? ECheckBoxState::Checked
									: ECheckBoxState::Unchecked;
							}),
							FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
							{
								if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
								{
									WorkingLayers[SelectedLayerIndex].bInvertHeight = State == ECheckBoxState::Checked;
									RefreshLayeredPreview();
								}
							})))
				]
			]
		]
	];
}


void SMixtormat::AddHeightBlendRows(
	const TSharedRef<SVerticalBox>& Panel,
	TFunction<FMixtormatHeightBlend*()> Resolve,
	const FText& OpHint,
	const FText& AmountHint)
{
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberEnum<FMixtormatHeightBlend, EMixtormatHeightOp>(
			LOCTEXT("HeightBlendOp", "Blend"), Resolve, &FMixtormatHeightBlend::Op, OpHint,
			FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })),
		MakeMemberSlider<FMixtormatHeightBlend>(
			LOCTEXT("HeightBlendSoftness", "Softness"), Resolve, &FMixtormatHeightBlend::Softness, 0.0, 0.5, 0.1, 0.001,
			LOCTEXT("HeightBlendSoftnessHint", "Rounded join for Min and Max, in height units. 0 is a hard min or max."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("HeightBlendAmount", "Amount"), Resolve, &FMixtormatHeightBlend::Amount, 0.0, 1.0, 1.0, 0.01,
		AmountHint));
}
#undef LOCTEXT_NAMESPACE
