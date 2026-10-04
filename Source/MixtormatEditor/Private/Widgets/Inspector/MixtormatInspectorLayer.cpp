// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "UI/Rows/SMixtormatRow.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

extern const EMixtormatUVRotation GMixtormatUVRotations[4] = { EMixtormatUVRotation::None, EMixtormatUVRotation::Quarter, EMixtormatUVRotation::Half, EMixtormatUVRotation::ThreeQuarter };

TSharedRef<SWidget> SMixtormat::BuildBaseColorBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatColorBlendMode Mode : MixtormatUI::ColorBlendModes())
	{
		Menu.Item(
			MixtormatUI::ColorBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
				{
					WorkingLayers[SelectedLayerIndex].BaseColorBlendMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				return WorkingLayers.IsValidIndex(SelectedLayerIndex)
					&& WorkingLayers[SelectedLayerIndex].BaseColorBlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerRotationMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatUVRotation Rotation : GMixtormatUVRotations)
	{
		Menu.Item(
			MixtormatUI::UVRotationText(Rotation),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Rotation]()
			{
				if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
				{
					WorkingLayers[SelectedLayerIndex].Rotation = Rotation;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Rotation]()
			{
				return WorkingLayers.IsValidIndex(SelectedLayerIndex)
					&& WorkingLayers[SelectedLayerIndex].Rotation == Rotation;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceAdjustmentCards()
{
	const auto Layer = [this]()
	{
		return TFunction<FMixtormatLayer*()>([this]() -> FMixtormatLayer*
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
		});
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// Labels lose the prefix their card now carries: "Roughness Bias" inside a card headed
	// ROUGHNESS said roughness twice, and spent on the label the width the value needed.
	TSharedRef<SVerticalBox> Transform = AddCard(Panel, LOCTEXT("CardTransform", "Transform"));
	AddSliderRow(Transform, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("TilingLabel", "Tiling"), Layer(), &FMixtormatLayer::Tiling, 1.0, 8.0, 2.0, 0.1));
	// UV placement on top of Tiling. Integer scale and no rotation, both because the compositor
	// wraps every source read in a frac() and anything else seams. Offset and flip are safe.
	AddSliderRow(Transform, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("UVScaleXLabel", "Scale X"), Layer(), &FMixtormatLayer::UVScaleX, 1.0, 16.0, 1,
			LOCTEXT("UVScaleHint", "Per-axis multiplier on Tiling. Integer only: a fractional scale lands mid-cell at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("UVScaleYLabel", "Scale Y"), Layer(), &FMixtormatLayer::UVScaleY, 1.0, 16.0, 1,
			LOCTEXT("UVScaleHint", "Per-axis multiplier on Tiling. Integer only: a fractional scale lands mid-cell at the UV wrap and seams."))));
	AddSliderRow(Transform, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UVOffsetXLabel", "Offset X"), Layer(), &FMixtormatLayer::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("UVOffsetHint", "Shifts where the source is read from. Safe at any value: translating a periodic function leaves it periodic.")),
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UVOffsetYLabel", "Offset Y"), Layer(), &FMixtormatLayer::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("UVOffsetHint", "Shifts where the source is read from. Safe at any value: translating a periodic function leaves it periodic."))));
	// Rotate and the two flips share one row. They are the same decision -- which way round the
	// source is read -- and as three rows they left most of the width empty while pushing the
	// card taller than the values above it. The chip takes the compact width so the flips fit
	// beside it rather than under it.
	AddSliderRow(Transform, MixtormatRow::MakeDropdown(
		LOCTEXT("UVRotationLabel", "Rotate"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SMixtormatChip)
			.MinWidth(MixtormatTokens::RowFieldMinWidthCompact)
			.Text_Lambda([this]()
			{
				return WorkingLayers.IsValidIndex(SelectedLayerIndex)
					? MixtormatUI::UVRotationText(WorkingLayers[SelectedLayerIndex].Rotation)
					: FText::GetEmpty();
			})
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &SMixtormat::BuildLayerRotationMenu))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::RowLabelGap, 0.0f, 0.0f, 0.0f)
		[
			MakeMemberToggle<FMixtormatLayer>(
				LOCTEXT("UVFlipULabel", "Flip U"), Layer(), &FMixtormatLayer::bFlipU)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::RowLabelGap, 0.0f, 0.0f, 0.0f)
		[
			MakeMemberToggle<FMixtormatLayer>(
				LOCTEXT("UVFlipVLabel", "Flip V"), Layer(), &FMixtormatLayer::bFlipV)
		],
		LOCTEXT("UVRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams; 90 degree steps are permutations of the unit square, so they stay tileable.")));

	TSharedRef<SVerticalBox> Roughness = AddCard(Panel, LOCTEXT("CardRoughness", "Roughness"));
	AddSliderRow(Roughness, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessLabel", "Bias"), Layer(), &FMixtormatLayer::RoughnessBias, 0.0, 1.0, 0.5, 0.01));
	AddSliderRow(Roughness, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessContrastLabel", "Contrast"), Layer(), &FMixtormatLayer::RoughnessContrast, -1.0, 2.0, 0.0, 0.01));
	AddSliderRow(Roughness, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessOffsetLabel", "Offset"), Layer(), &FMixtormatLayer::RoughnessOffset, -0.5, 0.5, 0.0, 0.01));

	TSharedRef<SVerticalBox> AuthoredNormal = AddCard(
		Panel, LOCTEXT("CardAuthoredMaterialNormal", "Authored Material Normal"));
	AddSliderRow(AuthoredNormal, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("AuthoredMaterialNormalStrengthLabel", "Strength"), Layer(), &FMixtormatLayer::NormalIntensity, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("AuthoredMaterialNormalStrengthHint", "Strength of this layer's authored material normal map. 1 preserves the source, 0 flattens it, and above 1 strengthens its slope. Does not control relief normals generated from the final height.")));
	TSharedRef<SVerticalBox> Relief = AddCard(Panel, LOCTEXT("CardRelief", "Relief"));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightBoostLabel", "Height Booster"), Layer(), &FMixtormatLayer::HeightBoost, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("HeightBoostHint", "Gain on this layer's height. 1 is untouched; 0 flattens it; values above 1 deepen the relief and, with it, the normals derived from that height. An imported normal map is left at its authored strength, so a surface whose height and normal describe the same relief is not deepened twice. Applied before displacement, height blending, and derived normals. Not Height Influence, which controls how much of this layer reaches the composite.")));

	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightLevelOffsetLabel", "Height Offset"), Layer(), &FMixtormatLayer::HeightLevelOffset, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("HeightLevelOffsetHint", "Adds to only this layer's boosted height before compositing. Positive values raise it; negative values sink it. Displacement, height blending, and derived normals all use the shifted result.")));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightSmoothLabel", "Height Smooth"), Layer(), &FMixtormatLayer::HeightSmooth, 0.0, 8.0, 0.0, 0.05,
		LOCTEXT("HeightSmoothHint", "Softens this layer's own height before anything reads it -- displacement, the height blend and the derived normals all see the smoothed result. Only this layer: it is applied at the source, before the composite merges anything, which is what the Layer Blur effect cannot do. Measured in output texels, so tiling does not scale it, and both axes together.")));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightShapeLabel", "Height Shape"), Layer(), &FMixtormatLayer::HeightShape, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("HeightShapeHint", "Redistributes this layer's height between its own ends instead of moving or scaling it. Positive bulges the form, raising the midtones toward the peaks; negative pinches it, sinking them toward the pits. Both extremes stay put either way, so the relief changes shape rather than depth -- Height Booster is the one that changes depth. Applied before Booster and Offset.")));

	AddGeneratedFeatureCards(Panel);
	return Panel;
}

void SMixtormat::AddGeneratedFeatureCards(const TSharedRef<SVerticalBox>& Panel)
{
	const auto Layer = [this]()
	{
		return TFunction<FMixtormatLayer*()>([this]() -> FMixtormatLayer*
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
		});
	};

	TSharedRef<SVerticalBox> Masks = AddCard(
		Panel,
		LOCTEXT("CardFeaturedMasks", "Featured Masks"),
		nullptr,
		SNew(SMixtormatLayerIcon)
			.bOn_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::GeneratedFeature; })
			.bActive_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::GeneratedFeature; })
			.ToolTipText(LOCTEXT("PreviewGeneratedFeature", "Preview the cavity-to-convex feature mask in unlit dark red and cyan"))
			.OnClicked_Lambda([this]() { ToggleFeaturePreview(EMixtormatDebugPreviewMode::GeneratedFeature); }));

	AddSliderRow(Masks, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("FeatureInfluenceLabel", "Normal Influence"), Layer(), &FMixtormatLayer::FeatureInfluence, 0.0, 1.0, 0.0, 0.01));
	// Paired with its own invert, the way the Height and AO influences below are.
	AddSliderRow(Masks, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("FeatureBiasLabel", "Cavity to Convex"), Layer(), &FMixtormatLayer::FeatureBias, 0.0, 1.0, 0.0, 0.01),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertGeneratedFeatureLabel", "Invert"), Layer(), &FMixtormatLayer::bInvertFeature,
			LOCTEXT("InvertGeneratedFeatureHint", "Apply one-minus to the selected cavity-to-convex feature mask"))));

	// Four short labels that are read against each other, so two across.
	AddSliderRow(Masks, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("CurvatureRadiusLabel", "Radius"), Layer(), &FMixtormatLayer::CurvatureRadius, 1.0, 32.0, 2),
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("CurvatureSmoothingLabel", "Smoothing"), Layer(), &FMixtormatLayer::CurvatureSmoothing, 1.0, 4.0, 2)));
	AddSliderRow(Masks, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("CurvatureStrengthLabel", "Strength"), Layer(), &FMixtormatLayer::CurvatureStrength, 0.0, 8.0, 1.0, 0.05),
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("CurvaturePowerLabel", "Power"), Layer(), &FMixtormatLayer::CurvaturePower, 0.001, 8.0, 1.0, 0.05)));

	// Each influence pairs with its own invert, which is what paired rows exist for.
	AddSliderRow(Masks, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UnderlyingHeightInfluenceLabel", "Height"), Layer(),
			&FMixtormatLayer::HeightFeatureInfluence, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UnderlyingHeightInfluenceHint", "Mask this layer using the accumulated height underneath it")),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertUnderlyingHeightLabel", "Invert"), Layer(),
			&FMixtormatLayer::bInvertHeightFeature,
			LOCTEXT("InvertUnderlyingHeightHint", "Favor lower underlying height instead of higher height"))));
	AddSliderRow(Masks, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UnderlyingAOInfluenceLabel", "AO"), Layer(),
			&FMixtormatLayer::AOFeatureInfluence, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UnderlyingAOInfluenceHint", "Mask this layer using the accumulated AO underneath it")),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertUnderlyingAOLabel", "Invert"), Layer(),
			&FMixtormatLayer::bInvertAOFeature,
			LOCTEXT("InvertUnderlyingAOHint", "Favor occluded areas instead of exposed areas"))));
}

TSharedRef<SWidget> SMixtormat::BuildChannelInfluenceControls()
{
	// Layer-level rows resolve the selected layer instead of an effect; everything else is the
	// same generic binding the peel, erosion and mask panels use.
	const auto Layer = [this]() -> FMixtormatLayer*
	{
		return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("BaseColorInfluenceLabel", "Base Color"), Layer, &FMixtormatLayer::BaseColorInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessInfluenceLabel", "Roughness"), Layer, &FMixtormatLayer::RoughnessInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("AOInfluenceLabel", "Ambient Occlusion"), Layer, &FMixtormatLayer::AOInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("MetallicInfluenceLabel", "Metallic"), Layer, &FMixtormatLayer::MetallicInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("F0InfluenceLabel", "IOR / F0"), Layer, &FMixtormatLayer::F0Influence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("LayerNormalInfluenceLabel", "Normal"), Layer, &FMixtormatLayer::NormalInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("LayerHeightInfluenceLabel", "Height"), Layer, &FMixtormatLayer::HeightInfluence, 0.0, 1.0, 1.0, 0.01));
	AddSliderRow(Panel, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			MakeMemberSlider<FMixtormatLayer>(
				LOCTEXT("FuzzInfluenceLabel", "Fuzz"), Layer, &FMixtormatLayer::FuzzInfluence, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("FuzzInfluenceHint", "Fuzz amount. The strongest enabled positive influence supplies the fuzz color; the topmost layer wins ties. Roughness stays on the master material."))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("FuzzColorHint", "Fuzz Color (DA_FuzzColor). No positive fuzz influence leaves the master's color unchanged."))
			.OnClicked_Lambda([this]()
			{
				if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
				{
					return FReply::Handled();
				}
				const FGuid LayerId = WorkingLayers[SelectedLayerIndex].LayerId;
				const FLinearColor Original = WorkingLayers[SelectedLayerIndex].FuzzColor;
				const TWeakPtr<SMixtormat> WeakThis = SharedThis(this);
				const auto ApplyColor = [WeakThis, LayerId](FLinearColor Color)
				{
					if (const TSharedPtr<SMixtormat> Widget = WeakThis.Pin())
					{
						for (FMixtormatLayer& Target : Widget->WorkingLayers)
						{
							if (Target.LayerId == LayerId)
							{
								Color.A = 1.0f;
								Target.FuzzColor = Color;
								Widget->RefreshLayeredPreview();
								break;
							}
						}
					}
				};
				LastHistoryRecordTime = 0.0;
				FColorPickerArgs Args;
				Args.bUseAlpha = false;
				Args.bOnlyRefreshOnMouseUp = false;
				Args.InitialColor = Original;
				Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(ApplyColor);
				Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
					[ApplyColor, Original](const FLinearColor) { ApplyColor(Original); });
				OpenColorPicker(Args);
				return FReply::Handled();
			})
			[
				SNew(SColorBlock)
				.Size(FVector2D(24.0f, 16.0f))
				.Color_Lambda([Layer]()
				{
					const FMixtormatLayer* Selected = Layer();
					return Selected ? Selected->FuzzColor : FLinearColor::White;
				})
			]
		]);

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ChannelInfluenceHeading", "CHANNEL INFLUENCE"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildColorAdjustmentCard()
{
	const auto Layer = [this]() -> FMixtormatLayer*
	{
		return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	// Hue Shift is signed, so it fills from the centre and reads as untouched at zero.
	//
	// The row is normalised -1..1 while FMixtormatLayer::HueShift is in degrees, which is what the
	// composite pass divides by 360. Hence the scale: a full deflection is half the hue circle in
	// either direction, so every hue is reachable and the two ends meet. Without it the slider ran
	// from -1 to 1 *degree* and the control did nothing visible.
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HueShiftLabel", "Hue Shift"), Layer, &FMixtormatLayer::HueShift, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("HueShiftHint", "Rotates the layer's colour around the hue circle, leaving saturation and value alone. Full deflection either way is a half turn, so the two ends meet on the same hue."),
		MixtormatHue::DegreesPerUnit));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("SaturationLabel", "Saturation"), Layer, &FMixtormatLayer::Saturation, 0.0, 2.0, 1.0, 0.01),
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("ValueLabel", "Value"), Layer, &FMixtormatLayer::Value, 0.0, 2.0, 1.0, 0.01)));

	// No visibility lambda of its own any more: the Composition group it now sits inside
	// already collapses when there is no selected layer.
	return SNew(SMixtormatInspectorCard)
		.Title(LOCTEXT("CardColor", "Color"))
		[
			Panel
		];
}

#undef LOCTEXT_NAMESPACE
