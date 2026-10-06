// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"

// The inspector column: every per-selection parameter panel.

#define LOCTEXT_NAMESPACE "SMixtormat"









// Ten sliders, and that is the whole effect.
//
// Runoff has as many derived constants behind it as Stain has exposed controls -- strata count,
// spacing, length step, opacity falloff, warp contrast, lip width, three noise parameters and the
// breakup blend. None of them are here. The test for whether a number belongs on this panel is
// whether an artist looking at a dirty wall would have an opinion about it, and nobody has an
// opinion about lacunarity.







// The mask blur's opposite number, so the controls deliberately read the same: two radii, and a
// weight. Scope is the one thing it has that the mask blur cannot, because a mask has no notion
// of what is underneath it.





















// Exact ID first, Color Range second -- the order the node is explained in, not the order the
// enum happens to be numbered. Color Range is still value 0 and still the default, so nothing
// saved before the mode existed changes behaviour.






















// The palette rows, and the picker plumbing behind them. Resolved by index rather than through
// GetSelectedHsvFilter for the same reason the colour ID picker is: the window is modeless, so
// the selection can move while it is open and committing to whatever happens to be selected
// then would edit a different node from the one the user opened.






































// The same two choices, bound to a named row rather than to the selection. The row menu is
// built on right-click for whatever row was clicked, which is not necessarily the selected one.




// The BLEND row every layer and every module shares: Op and Softness, then Amount. One block with
// one meaning, whatever it is attached to -- a layer's against the stack below, a module's against
// the layer's running height. Resolve names the FMixtormatHeightBlend it edits.


// The module's blend into its Generator layer's running height. First in every module panel,
// because it is the one question every module answers however its own controls read. At Height
// Blend the module's own Strength, Threshold, Softness and biases open beneath the shared row.


// Strata Carver, the first GENERATORS panel.
//
// Rows run from what the beds are (size, direction, thickness, height), through their profile
// (verticality, ramp shape), to what bends and roughens them, then the laminae.



































// Two radii and nothing else. A direction enum would have been one control instead of two, but
// it could not be driven per axis and could not express an unequal blur -- and the shader already
// runs a dispatch per axis, so the second number is free.




// Source and Mode lead, because they decide what every number under them means: the same Range
// that keeps cavities under Mean keeps saddles under Gaussian.








// One card, one title. Feature, Curvature and Surface Mask are all the same question -- what
// generated signal masks this layer -- and as three sheets they read as three unrelated runs
// with the widest gap in the panel between values that are set together. Captions between the
// runs only rebuilt that split a line at a time, so the card title carries the subject alone.
//
// No group header of its own. This is one run of values inside Surface Adjustments, and a second
// header bar over it only repeated the one already above -- with a chevron that hid controls the
// panel exists to offer. The feature preview eye leads the card title;
// other actions remain at the trailing edge.










TSharedRef<SWidget> SMixtormat::BuildInspectorPanel()
{
	// The layer rows below all bind through this one resolver, the same way the peel, erosion,
	// mask and generated-mask panels bind through theirs.
	const auto LayerForRows = [this]()
	{
		return TFunction<FMixtormatLayer*()>([this]() -> FMixtormatLayer*
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
		});
	};
	const auto GeneratorBlend = [this]() -> FMixtormatHeightBlend*
	{
		return WorkingLayers.IsValidIndex(SelectedLayerIndex)
			? &WorkingLayers[SelectedLayerIndex].HeightBlend : nullptr;
	};
	const TSharedRef<SVerticalBox> GeneratorComposition = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> GeneratorHeight = AddCard(
		GeneratorComposition, LOCTEXT("CardHeightBlend", "Height Blend"));
	AddSliderRow(GeneratorHeight, MakeMemberEnum<FMixtormatHeightBlend, EMixtormatHeightOp>(
		LOCTEXT("GeneratorLayerHeightOperation", "Height Operation"), GeneratorBlend,
		&FMixtormatHeightBlend::Op,
		LOCTEXT("GeneratorLayerHeightOperationHint", "How the generated height stack combines with the layer below."),
		FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
	AddSliderRow(GeneratorHeight,
		SNew(SBox).Visibility_Lambda([GeneratorBlend]()
		{
			const FMixtormatHeightBlend* Blend = GeneratorBlend();
			return Blend && (Blend->Op == EMixtormatHeightOp::Min || Blend->Op == EMixtormatHeightOp::Max)
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MakeMemberSlider<FMixtormatHeightBlend>(LOCTEXT("GeneratorLayerSoftness", "Softness"),
				GeneratorBlend, &FMixtormatHeightBlend::Softness, 0.0, 0.5, 0.1, 0.001)
		]);
	AddSliderRow(GeneratorHeight, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("GeneratorLayerAmount", "Amount"), GeneratorBlend,
		&FMixtormatHeightBlend::Amount, 0.0, 1.0, 1.0, 0.01));
	const TSharedRef<SVerticalBox> GeneratorBlending = AddCard(
			GeneratorComposition, LOCTEXT("CardBlendingOpacity", "Blending / Opacity"));
		AddSliderRow(GeneratorBlending, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("GeneratorLayerOpacity", "Opacity"), LayerForRows(),
		&FMixtormatLayer::Opacity, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("GeneratorLayerOpacityHint", "Weights this layer's height coverage, including the Height Blend contest.")));
	const TSharedRef<SVerticalBox> GeneratorHeightBlend = SNew(SVerticalBox)
		.Visibility_Lambda([GeneratorBlend]()
		{
			const FMixtormatHeightBlend* Blend = GeneratorBlend();
			return Blend && Blend->Op == EMixtormatHeightOp::HeightBlend
				? EVisibility::Visible : EVisibility::Collapsed;
		});
	// Layer composition currently reads HeightBlendAmount for strength, unlike module composition.
	AddSliderRow(GeneratorHeightBlend, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("GeneratorLayerBlendStrength", "Strength"), LayerForRows(),
		&FMixtormatLayer::HeightBlendAmount, 0.0, 4.0, 1.0, 0.01));
	AddSliderRow(GeneratorHeightBlend, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("GeneratorLayerThreshold", "Threshold"), GeneratorBlend,
		&FMixtormatHeightBlend::Threshold, 0.0, 1.0, 0.5, 0.01));
	AddSliderRow(GeneratorHeightBlend, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("GeneratorLayerEdgeSoftness", "Edge Softness"), GeneratorBlend,
		&FMixtormatHeightBlend::EdgeSoftness, 0.0, 1.0, 0.1, 0.005));
	AddSliderRow(GeneratorHeightBlend, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("GeneratorLayerBaseBias", "Base Bias"), GeneratorBlend,
		&FMixtormatHeightBlend::BaseBias, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(GeneratorHeightBlend, MakeMemberSlider<FMixtormatHeightBlend>(
		LOCTEXT("GeneratorLayerBlendBias", "Blend Bias"), GeneratorBlend,
		&FMixtormatHeightBlend::BlendBias, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(GeneratorHeight, GeneratorHeightBlend);
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const TSharedRef<SVerticalBox> BlendPanel = SNew(SVerticalBox);
	AddHeightBlendRows(
		BlendPanel,
		[this]() -> FMixtormatHeightBlend*
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex)
				? &WorkingLayers[SelectedLayerIndex].HeightBlend
				: nullptr;
		},
		LOCTEXT("HeightOpHint", "How this layer's height combines with the height below it, weighted by its coverage. Replace cross-fades (the old OVER); Max with Softness merges (the old BLEND, the default). Add and Subtract are signed about 0.5. Min, Max and Difference behave like Replace on bare ground. Height Blend lets the layer run over the stack where its mask is strong and its height is higher: its settings open in the card below."),
		LOCTEXT("HeightAmountHint", "How much of this op's height reaches the stack."));
	return SNew(SBox)
		.WidthOverride(MixtormatTokens::InspectorWidth)
		[
			SNew(SBorder)
			.Padding(FMargin(0.0f))
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
			[
				SNew(SVerticalBox)
				// The selection header (thumbnail, name, source, badge) takes the inspector's top
				// margin; it sits above the well, so the well's own padding never reached it.
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, MixtormatTokens::InspectorTopMargin, 2.0f, 3.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						// Thumbnail, name, source, badge -- the same four fields in the same order as
						// the row in the stack that selected it, so moving from one to the other
						// re-reads nothing.
						SNew(SBox)
						.HeightOverride(FMixtormatThemeStore::GetResolved().LayerLayout.RowHeight)
						.Padding(FMargin(
							MixtormatTokens::LayerRowInsetLeading,
							0.0f,
							MixtormatTokens::LayerRowInsetTrailing,
							0.0f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SAssignNew(SelectedThumbnailBox, SBox)
								.WidthOverride(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize)
								.HeightOverride(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerNameInset, 0.0f, 0.0f, 0.0f)
							[
								SAssignNew(SelectedSurfaceText, STextBlock)
								.Text(LOCTEXT("NoSelectedSurface", "No layer selected"))
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							.Padding(FMixtormatThemeStore::GetResolved().LayerLayout.ItemGap, 0.0f, FMixtormatThemeStore::GetResolved().LayerLayout.ItemGap, 0.0f)
							[
								SAssignNew(SelectedIdentityText, STextBlock)
								.Text(LOCTEXT("NoIdentity", "—"))
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SMixtormatBadge)
								.Text_Lambda([this]() { return GetSelectedBadgeText(); })
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildInstanceBanner()
				]
				// Nothing selected: document-wide settings. Empty for now.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return bHasWorkingMaterial && !HasAnySelection() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.GroupOuterGap, 0.0f))
					[
						SNew(SMixtormatInspectorGroup)
						.Title(LOCTEXT("GlobalHeading", "GLOBAL"))
						.InitiallyExpanded(true)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GlobalEmpty", "No global settings yet."))
							.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(0.5f)))
						]
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					.ScrollBarStyle(&Style.GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
					.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
					// Readable, not editable. The rows keep their values and their layout; only
					// the writing is taken away, which is what an instance means.
					.IsEnabled_Lambda([this]() { return !IsSelectedChildInstance(); })
					.Visibility_Lambda([this]()
					{
						return GetSelectedLayerEffect()
							|| GetSelectedGeneratedMask()
							|| GetSelectedLayerMask()
							|| GetSelectedLayerBlur()
							|| GetSelectedLayerCurvature()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
							// Both halves of the Pattern split claim their own inspector sections.
							|| GetSelectedUvId()
							|| GetSelectedReliefId()
							|| GetSelectedBoundaryId()
							|| GetSelectedIdGroup()
							// The category, not the kind. A generator whose panel is not yet
							// written still has to claim the inspector, or it would show the
							// layer's own sections instead and read as a broken selection.
							|| HasSelectedGenerator()
							|| HasSelectedOutputReference()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SScrollBox::Slot()[BuildProceduralPeelControls()]
					+ SScrollBox::Slot()[BuildStainControls()]
					+ SScrollBox::Slot()[BuildRunoffControls()]
					+ SScrollBox::Slot()[BuildErosionControls()]
					+ SScrollBox::Slot()[BuildGradeControls()]
					+ SScrollBox::Slot()[BuildFlowWarpControls()]
										+ SScrollBox::Slot()[BuildGeneratorFlowControls(EMixtormatEffectType::ShapeDeform)]
										+ SScrollBox::Slot()[BuildGeneratorFlowControls(EMixtormatEffectType::GeneratorFlow)]
										+ SScrollBox::Slot()[BuildGeneratorFlowControls(EMixtormatEffectType::FlowCarve)]
					+ SScrollBox::Slot()[BuildLayerBlurControls()]
					+ SScrollBox::Slot()[BuildBreakupControls()]
					+ SScrollBox::Slot()[BuildWornEdgesControls()]
					+ SScrollBox::Slot()[BuildGeneratedMaskControls()]
					+ SScrollBox::Slot()[BuildLayerMaskControls()]
					+ SScrollBox::Slot()[BuildMaskBlurControls()]
					+ SScrollBox::Slot()[BuildMaskCurvatureControls()]
					+ SScrollBox::Slot()[BuildCraquelureControls()]
					+ SScrollBox::Slot()[BuildColorIdControls()]
					+ SScrollBox::Slot()[BuildFilterControls()]
					+ SScrollBox::Slot()[BuildPatternIdControls()]
					+ SScrollBox::Slot()[BuildHsvFilterControls()]
					+ SScrollBox::Slot()[BuildRandomIdControls()]
					+ SScrollBox::Slot()[BuildRampIdControls()]
					+ SScrollBox::Slot()[BuildUvIdControls()]
					+ SScrollBox::Slot()[BuildReliefIdControls()]
					+ SScrollBox::Slot()[BuildBoundaryIdControls()]
					+ SScrollBox::Slot()[BuildIdGroupControls()]
					+ SScrollBox::Slot()[BuildOutputReferenceControls()]

					+ SScrollBox::Slot()[BuildStrataCarverControls()]
					+ SScrollBox::Slot()[BuildCracksControls()]
					+ SScrollBox::Slot()[BuildRockFormationControls()]
					+ SScrollBox::Slot()[BuildPebblesControls()]
					+ SScrollBox::Slot()[BuildCliffStrataControls()]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					// The scroll box has to sit outside the group, not inside it. An expandable
					// area sizes its content to whatever that content asks for, so a scroll box
					// within one is handed unbounded height and never scrolls -- the layer
					// inspector simply ran off the bottom of the panel.
					SNew(SScrollBox)
					.ScrollBarStyle(&Style.GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
					.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
					// The exact inverse of the child inspector above it: a selected child owns
					// the panel on its own, and the layer's own sections come back when nothing
					// is selected. Both lists have to name every child type or a new one shows
					// its controls *and* the layer's underneath -- which is what a missing entry
					// looks like, since the default here is Visible.
					.Visibility_Lambda([this]()
					{
						return GetSelectedLayerEffect()
							|| GetSelectedGeneratedMask()
							|| GetSelectedLayerMask()
							|| GetSelectedLayerBlur()
							|| GetSelectedLayerCurvature()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
							// Both halves of the Pattern split claim their own inspector sections.
							|| GetSelectedUvId()
							|| GetSelectedReliefId()
							|| GetSelectedBoundaryId()
							|| GetSelectedIdGroup()
							// The category, not the kind. A generator whose panel is not yet
							// written still has to claim the inspector, or it would show the
							// layer's own sections instead and read as a broken selection.
							|| HasSelectedGenerator()
							|| HasSelectedOutputReference()
							? EVisibility::Collapsed : EVisibility::Visible;
					})

					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SMixtormatInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return bHasSelectedLayer && WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type == EMixtormatLayerType::Generator
									? EVisibility::Visible : EVisibility::Collapsed;
							})
							.Title(LOCTEXT("GeneratorLayerHeading", "GENERATOR LAYER"))
							.InitiallyExpanded(true)
							[
								GeneratorComposition
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
						// No wrapping LAYER group. Selecting a layer shows its sections --
						// Channel Influence, Composition, Surface Adjustments, Colour, Height
						// Mask Blending -- as siblings in the column. A group whose only job was
						// to hold other groups added a header, an indent and a second thing to
						// expand before anything could be edited.
						SNew(SVerticalBox)
						.Visibility_Lambda([this]()
						{
							return bHasSelectedLayer && WorkingLayers.IsValidIndex(SelectedLayerIndex)
															&& WorkingLayers[SelectedLayerIndex].Type != EMixtormatLayerType::Generator
															? EVisibility::Visible : EVisibility::Collapsed;
						})
						+ SVerticalBox::Slot().AutoHeight()[BuildStrataCarverControls()]
						+ SVerticalBox::Slot().AutoHeight()[BuildCracksControls()]
						+ SVerticalBox::Slot().AutoHeight()[BuildRockFormationControls()]
						+ SVerticalBox::Slot().AutoHeight()[BuildPebblesControls()]
						+ SVerticalBox::Slot().AutoHeight()[BuildCliffStrataControls()]

						// No "Normal Detail Only" checkbox: DETAIL is one of the four cells in
						// COMPOSITION, which writes the same ChannelMode. Two controls for one field
						// meant the segment could say BLEND while the box said the layer was detail.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
						[
							SNew(SBox)
							.Visibility_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMixtormatLayerChannelMode::NormalDetail ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MixtormatRow::MakeDropdown(
									LOCTEXT("NormalSourceLabel", "Normal Source"),
									SNew(SMixtormatChip).MinWidth(0.0f)
							.Text_Lambda([this]() { if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)) return LOCTEXT("NormalSource", "Choose Normal Source..."); const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex]; return Layer.NormalSourceType == EMixtormatNormalSourceType::Texture ? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName()) : LOCTEXT("SurfaceNormal", "Surface Normal"); })
							.OnGetMenuContent_Lambda([this]() { return BuildNormalSourceMenu(SelectedLayerIndex); }))
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SMixtormatInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type == EMixtormatLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("FillPropertiesHeading", "FILL PROPERTIES"))
							.InitiallyExpanded(true)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[SNew(STextBlock).Text(LOCTEXT("FillBaseColor", "Base Color"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
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
										.Size(FVector2D(MixtormatTokens::InspectorColorSwatchWidth, MixtormatTokens::InspectorColorSwatchHeight))
									]
								]
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillRoughness", "Roughness"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Roughness, 0.0, 1.0, 0.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillIOR", "IOR"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::IOR, 1.0, 3.0, 1.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillMetallic", "Metallic"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Metallic, 0.0, 1.0, 0.0, 0.01)
							]
							// A fill has no surface, so its height is this one value. It is the
							// fill's own material property, not a height-mask setting.
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								SNew(SBox)
								.Visibility_Lambda([this]()
								{
									return WorkingLayers.IsValidIndex(SelectedLayerIndex)
										&& WorkingLayers[SelectedLayerIndex].Type == EMixtormatLayerType::Fill
										? EVisibility::Visible : EVisibility::Collapsed;
								})
								[
									MakeMemberSlider<FMixtormatLayer>(
										LOCTEXT("FillHeight", "Height"), LayerForRows(),
										&FMixtormatLayer::ConstantHeight, 0.0, 1.0, 0.5, 0.01)
								]
							]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildChannelInfluenceControls()
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SMixtormatInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("CompositionLabel", "COMPOSITION"))
							.InitiallyExpanded(true)
							[
								SNew(SVerticalBox)

								// What this layer's height does to the stack below. First, because it is
								// the composition question every layer answers; at Height Blend the card
								// below opens to decide where the layer runs over what is under it.
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SMixtormatInspectorCard)
									.Title(LOCTEXT("CardHeightBlend", "Height Blend"))
									[BlendPanel]
								]

								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FMixtormatThemeStore::GetResolved().CardLayout.Gap, 0.0f, 0.0f)
																[
																	SNew(SMixtormatInspectorCard)
																	.Title(LOCTEXT("CardBlendingOpacity", "Blending / Opacity"))
																	[
																		SNew(SVerticalBox)
																// One control, not three fields. BLEND / OVER / COAT / DETAIL are the
								// only combinations of ChannelMode, CompositionMode and NormalBlendMode
								// that mean anything, and they are the same four words the layer's
								// badge prints -- so the stack and the inspector teach one vocabulary.
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.SegmentedControlGap)
								[
									SNew(SMixtormatSegmentedControl)
									.Options(MixtormatLayerBadges::CompositionOptions())
									.ToolTips(MixtormatLayerBadges::CompositionToolTips())
									.ActiveIndex_Lambda([this]()
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex)
											? static_cast<int32>(MixtormatLayerBadges::CompositionOf(
												WorkingLayers[SelectedLayerIndex]))
											: 0;
									})
									.OnChosen_Lambda([this](const int32 Index)
									{
										if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
										{
											return;
										}
										MixtormatLayerBadges::ApplyComposition(
											WorkingLayers[SelectedLayerIndex],
											static_cast<MixtormatLayerBadges::EComposition>(Index));
										RefreshLayeredPreview();
										RebuildLayerList();
									})
								]
								// The mode and how far it is taken, on one row. Base colour only:
								// a mode that suits colour rarely suits roughness, and roughness
								// already has Bias, Contrast and Offset of its own.
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
								[
									MixtormatRow::MakePair(
										MixtormatRow::MakeDropdown(
											LOCTEXT("BaseColorBlendLabel", "Blend"),
											MixtormatRow::MakeChip(
												TAttribute<FText>::CreateLambda([this]()
												{
													return WorkingLayers.IsValidIndex(SelectedLayerIndex)
														? MixtormatUI::ColorBlendModeText(
															WorkingLayers[SelectedLayerIndex].BaseColorBlendMode)
														: FText::GetEmpty();
												}),
												FOnGetContent::CreateSP(this, &SMixtormat::BuildBaseColorBlendModeMenu),
																								nullptr, TAttribute<FText>(), 0.0f),
											LOCTEXT("BaseColorBlendHint", "How this layer's base colour combines with what is composited below it. Normal replaces, which is what every layer did before this control existed. Affects base colour only.")),
										MakeMemberSlider<FMixtormatLayer>(
											LOCTEXT("BaseColorBlendAmountLabel", "Amount"),
											[this]() -> FMixtormatLayer*
											{
												return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
											},
											&FMixtormatLayer::BaseColorBlendAmount, 0.0, 1.0, 1.0, 0.01,
											LOCTEXT("BaseColorBlendAmountHint", "How much of the blend mode happens -- it lerps between this layer's plain colour and the blended result. Not a fourth opacity: Opacity, the mask chain and Base Color Influence all decide coverage, this decides mode strength. At Normal there is nothing to fade and it does nothing.")))
								]

														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("OpacityLabel", "Opacity"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Opacity, 0.0, 1.0, 1.0, 0.01)
							]
									]
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, FMixtormatThemeStore::GetResolved().CardLayout.Gap, 0.0f, 0.0f)
								[
									BuildColorAdjustmentCard()
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SMixtormatInspectorGroup)
							.Visibility_Lambda([this]()
							{
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type != EMixtormatLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("SurfaceAdjustmentsHeading", "SURFACE ADJUSTMENTS"))
							.InitiallyExpanded(true)
							[
								BuildSurfaceAdjustmentCards()
						]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildHeightBlendControls()
						]
					]
					]
				]
			]
		];
}



#undef LOCTEXT_NAMESPACE
