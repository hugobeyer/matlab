#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatTile.h"

// The inspector column: every per-selection parameter panel.

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildProceduralPeelControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpSource", "Source")));

	// The peel's own seed mask. Kept as a bespoke row because it picks an asset, not a value.
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
	[
		SNew(SBox).HeightOverride(18.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("PPeelMaskSlot", "Peel Mask"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("PPeelMaskSlotHint", "The mask that seeds this peel. Independent of the layer's mask children; unset falls back to the accumulated child mask."))
				.ButtonContent()
				[
					SNew(SBox).MinDesiredWidth(120.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
							if (!E)
							{
								return LOCTEXT("PPeelMaskNone", "Child Mask");
							}
							if (!E->PeelMask.IsNull())
							{
								return FText::FromString(E->PeelMask.ToSoftObjectPath().GetAssetName());
							}
							if (!E->PeelMaskTexture.IsNull())
							{
								return FText::FromString(E->PeelMaskTexture.ToSoftObjectPath().GetAssetName());
							}
							return LOCTEXT("PPeelMaskNone", "Child Mask");
						})
					]
				]
				.OnGetMenuContent_Lambda([this]()
				{
					// A grid, not a list. Masks are images, and picking "Grunge_Fine" over
					// "Grunge_Coarse" by filename meant assigning one, looking at the viewport
					// and coming back. The popover is wider than the 300px inspector on purpose:
					// a menu is its own window and is not clipped by the panel that opened it.
					TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
						.UseAllottedSize(true)
						.InnerSlotPadding(FVector2D(MixtormatTokens::TileGap, MixtormatTokens::TileGap));

					for (const FMixtormatMaskEntry& Entry : FMixtormatRegistry::GetMasks())
					{
						const FSoftObjectPath Path = Entry.AssetPath;
						Grid->AddSlot()
						[
							SNew(SMixtormatTile)
							.TileSize(MixtormatTokens::MaskPickerTileSize)
							.DisplayName(Entry.DisplayName)
							.ThumbnailAsset(Entry.ThumbnailAsset)
							.ThumbnailPool(ThumbnailPool)
							.bSelected_Lambda([this, Path]()
							{
								const FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
								if (!E)
								{
									return false;
								}
								return E->PeelMask.ToSoftObjectPath() == Path
									|| E->PeelMaskTexture.ToSoftObjectPath() == Path;
							})
							.OnActivated(FMixtormatOnTileActivated::CreateLambda([this, Path]()
							{
								FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
								if (!E)
								{
									return;
								}

								// The registry lists UMixtormatMask assets and plain UTexture2D
								// side by side, so the pick has to branch on the loaded class --
								// assigning a texture to the UMixtormatMask slot resolves to null
								// and silently falls back to the child mask.
								UObject* MaskObject = Path.TryLoad();
								if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
								{
									E->PeelMask = TSoftObjectPtr<UMixtormatMask>(Path);
									E->PeelMaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
								}
								else if (Cast<UTexture2D>(MaskObject))
								{
									E->PeelMask.Reset();
									E->PeelMaskTexture = TSoftObjectPtr<UTexture2D>(Path);
								}
								else
								{
									return;
								}
								RefreshLayeredPreview();
							}))
						];
					}

					return SNew(SBox)
						.WidthOverride(MixtormatTokens::MaskPickerWidth)
						.Padding(MixtormatTokens::TileGap)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.MaxHeight(420.0f)
							[
								SNew(SScrollBox) + SScrollBox::Slot()[Grid]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, MixtormatTokens::TileGap, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
								.Text(LOCTEXT("PPeelMaskClear", "Use the layer's child mask"))
								.ToolTipText(LOCTEXT("PPeelMaskClearHint", "Fall back to the layer's accumulated child mask."))
								.OnClicked_Lambda([this]()
								{
									if (FMixtormatLayerEffect* E = GetSelectedProceduralPeel())
									{
										E->PeelMask.Reset();
										E->PeelMaskTexture.Reset();
										RefreshLayeredPreview();
									}
									return FReply::Handled();
								})
							]
						];
				})
			]
		]
	];

	// Paired: both labels are one short word, and at the inspector's width each half is about
	// 139px. Anything longer would clip, which is why Growth's weights below are not paired.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelMaskTiling", "Tiling"), &FMixtormatLayerEffect::PeelMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("PPeelMaskInv", "Invert"),
			[this]() { return GetSelectedProceduralPeel(); },
			&FMixtormatLayerEffect::bPeelMaskInvert)));

	AddPeelSlider(Panel, LOCTEXT("PPeelMaskW", "Mask Gain"), &FMixtormatLayerEffect::PeelSeedMaskWeight, 0.0, 4.0, 0.0, 0.01,
		LOCTEXT("PPeelMaskWHint", "Scales the mask before the threshold. At 0 nothing crosses it and there is no peel at all, whichever mask is chosen."));
	AddPeelSlider(Panel, LOCTEXT("PPeelThreshold", "Threshold"), &FMixtormatLayerEffect::PeelSeedThreshold, 0.0, 1.0, 0.62, 0.01,
		LOCTEXT("PPeelThresholdHint", "The mask value the peel contour follows. The distance field is signed about this isocontour."));

	// The labels used to carry their group as a prefix -- "Growth · Convexity" in every row --
	// which is what made the column read as repetitive. The caption says it once instead.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpGrowth", "Growth")));
	AddPeelSlider(Panel, LOCTEXT("PPeelCurvW", "Convexity"), &FMixtormatLayerEffect::PeelSeedCurvatureWeight, -2.0, 2.0, 0.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelCurvBias", "Convex Bias"), &FMixtormatLayerEffect::PeelSeedCurvatureBias, 0.0, 1.0, 1.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelAOW", "Occlusion"), &FMixtormatLayerEffect::PeelSeedAOWeight, -2.0, 2.0, 0.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelHeightW", "Height"), &FMixtormatLayerEffect::PeelSeedHeightWeight, -2.0, 2.0, 0.0, 0.01);

	AddSliderRow(Panel, MixtormatRow::MakeHairline());
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatLayerEffect>(
		LOCTEXT("PPeelNormalize", "Normalize Weights"),
		[this]() { return GetSelectedProceduralPeel(); },
		&FMixtormatLayerEffect::bPeelNormalizeSeedWeights));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelCurvRadius", "Radius"), &FMixtormatLayerEffect::PeelCurvatureRadius, 1.0, 32.0, 2),
		MakePeelSlider(LOCTEXT("PPeelGrowth", "Strength"), &FMixtormatLayerEffect::PeelGrowthStrength, 0.05, 8.0, 1.0, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpFlake", "Flake")));
	AddPeelSlider(Panel, LOCTEXT("PPeelLiftVar", "Lift Variation"), &FMixtormatLayerEffect::PeelLiftVariation, 0.0, 1.0, 0.6, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelSizeVar", "Size Variation"), &FMixtormatLayerEffect::PeelSizeVariation, 0.0, 1.0, 0.5, 0.01,
		LOCTEXT("PPeelSizeVarHint", "Per-cell speed factor. Set this to 0 as well as the growth weights to check that the field dilates uniformly."));
	AddPeelSliderInt(Panel, LOCTEXT("PPeelClusterP", "Flake Cells"), &FMixtormatLayerEffect::PeelClusterPeriod, 1.0, 512.0, 4);
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("PPeelType", "Curled"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
				return E && E->PeelType == EMixtormatPeelType::Curled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatLayerEffect* E = GetSelectedProceduralPeel())
				{
					E->PeelType = State == ECheckBoxState::Checked
						? EMixtormatPeelType::Curled
						: EMixtormatPeelType::Flat;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("PPeelTypeHint", "Checked lifts a flap ahead of the front and folds it back behind. Unchecked is the flat chip.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpSolve", "Solve")));
	AddPeelSlider(Panel, LOCTEXT("PPeelAO", "Contact AO"), &FMixtormatLayerEffect::PeelAOStrength, 0.0, 1.0, 0.8, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelSharp", "Edge Sharpness"), &FMixtormatLayerEffect::PeelEdgeSharpness, 0.0, 4.0, 1.0, 0.01);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelSeed", "Seed"), &FMixtormatLayerEffect::PeelRandomSeed, 1.0, 999.0, 1),
		MakePeelSliderInt(LOCTEXT("PPeelSolveDiv", "Solve"), &FMixtormatLayerEffect::PeelSolveDivisor, 1.0, 32.0, 4,
			LOCTEXT("PPeelSolveDivHint", "Divides the resolution the front is solved at. Higher is much cheaper; 1 solves at full composition resolution. The solve never drops below 64 on a side, so past that point raising this does nothing."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedProceduralPeel() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ProcPeelHeading", "PEEL SEEDING"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildErosionControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("EroDirMode", "Direction Mode"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedErosion();
				return E && E->ErosionDirectionMode == EMixtormatErosionDirectionMode::Lerp
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatLayerEffect* E = GetSelectedErosion())
				{
					E->ErosionDirectionMode = State == ECheckBoxState::Checked
						? EMixtormatErosionDirectionMode::Lerp
						: EMixtormatErosionDirectionMode::Weight;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("EroDirModeHint", "Checked blends directions evenly (Lerp). Unchecked adds the angle to the slope vector (Weight), so steep ground still runs downhill.")));

	AddErosionSlider(Panel, LOCTEXT("EroAmount", "Amount"), &FMixtormatLayerEffect::ErosionAmount, 0.0, 1.0, 1.0, 0.01);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpRepose", "Repose")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroRepose", "Angle"), &FMixtormatLayerEffect::ErosionRepose, 0.0, 32.0, 0.30, 0.01),
		MakeErosionSlider(LOCTEXT("EroReposeSoft", "Softness"), &FMixtormatLayerEffect::ErosionReposeSoftness, 0.0, 32.0, 0.25, 0.01)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpSlope", "Slope")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSliderInt(LOCTEXT("EroSlopeRadius", "Radius"), &FMixtormatLayerEffect::ErosionSlopeRadius, 1.0, 32.0, 2),
		MakeErosionSlider(LOCTEXT("EroSlopeBlur", "Blur"), &FMixtormatLayerEffect::ErosionSlopeBlur, 0.0, 16.0, 2.0, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpCavity", "Cavity")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroCavityBias", "Bias"), &FMixtormatLayerEffect::ErosionCavityBias, -16.0, 16.0, 0.0, 0.05),
		MakeErosionSlider(LOCTEXT("EroCavityScale", "Contrast"), &FMixtormatLayerEffect::ErosionCavityScale, 0.0, 32.0, 1.0, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpHeight", "Height")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroHeightInfluence", "Influence"), &FMixtormatLayerEffect::ErosionHeightInfluence, -16.0, 16.0, 0.0, 0.05),
		MakeErosionSlider(LOCTEXT("EroHeightScale", "Contrast"), &FMixtormatLayerEffect::ErosionHeightScale, 0.0, 32.0, 1.0, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpShape", "Shaping")));
	AddErosionSlider(Panel, LOCTEXT("EroGullyWeight", "Gully Weight"), &FMixtormatLayerEffect::ErosionGullyWeight, 0.0, 8.0, 2.0, 0.05);
	AddErosionSlider(Panel, LOCTEXT("EroNormalStrength", "Normal Strength"), &FMixtormatLayerEffect::ErosionNormalStrength, 0.0, 32.0, 8.0, 0.05);
	AddErosionSlider(Panel, LOCTEXT("EroBlendSoftness", "Blend Softness"), &FMixtormatLayerEffect::ErosionBlendSoftness, 0.0, 8.0, 0.0, 0.05);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpDir", "Direction")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroDirAngle", "Angle"), &FMixtormatLayerEffect::ErosionDirectionAngle, 0.0, 360.0, 90.0, 1.0),
		MakeErosionSlider(LOCTEXT("EroDirAmount", "Amount"), &FMixtormatLayerEffect::ErosionDirectionAmount, 0.0, 1.0, 0.0, 0.01)));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedErosion() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ErosionHeading", "EROSION"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedMaskControls()
{
	// The panel already grouped itself with SIGNALS / SHAPING / BLEND headers; those become row
	// captions so they share the inspector's rhythm instead of being bespoke text blocks.
	const auto Gen = [this]() { return GetSelectedGeneratedMask(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("GenBlendMode", "Blend Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatGeneratedMask* Selected = GetSelectedGeneratedMask();
				return Selected ? MixtormatUI::MaskBlendModeText(Selected->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateLambda([this]()
			{
				return BuildGeneratedBlendModeMenu(SelectedLayerIndex, GetSelectedChildIndex());
			}))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GenHdrSignals", "Signals")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenCurvatureWeight", "Cavity / Curvature"), Gen, &FMixtormatGeneratedMask::CurvatureWeight, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenCurvatureBias", "Cavity to Convex"), Gen, &FMixtormatGeneratedMask::CurvatureBias, 0.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvatureStrength", "Strength"), Gen, &FMixtormatGeneratedMask::CurvatureStrength, 0.0, 32.0, 4.0, 0.05),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvaturePower", "Power"), Gen, &FMixtormatGeneratedMask::CurvaturePower, 0.05, 4.0, 1.0, 0.05)));
	AddSliderRow(Panel, MixtormatRow::MakeHairline());
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenDirectionWeight", "Direction (Tangent Y)"), Gen, &FMixtormatGeneratedMask::DirectionWeight, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionAngle", "Angle"), Gen, &FMixtormatGeneratedMask::DirectionAngle, 0.0, 360.0, 90.0, 1.0),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionBroadness", "Falloff"), Gen, &FMixtormatGeneratedMask::DirectionBroadness, 0.05, 8.0, 1.0, 0.05)));
	AddSliderRow(Panel, MixtormatRow::MakeHairline());
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenAOWeight", "Inverted AO"), Gen, &FMixtormatGeneratedMask::AOWeight, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenHeightWeight", "Height"), Gen, &FMixtormatGeneratedMask::HeightWeight, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenHeightBiasCtl", "Height Bias"), Gen, &FMixtormatGeneratedMask::HeightBias, -1.0, 1.0, 0.0, 0.01));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GenHdrShaping", "Shaping")));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatGeneratedMask>(
		LOCTEXT("GenNormalizeWeights", "Normalize Weights"), Gen, &FMixtormatGeneratedMask::bNormalizeWeights));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenBroadness", "Broadness"), Gen, &FMixtormatGeneratedMask::Broadness, 1.0, 32.0, 2),
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenSmoothing", "Smoothing"), Gen, &FMixtormatGeneratedMask::Smoothing, 1.0, 4.0, 2)));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenBiasCtl", "Bias"), Gen, &FMixtormatGeneratedMask::Bias, 0.001, 0.999, 0.5, 0.01));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenWarpAmount", "Warp"), Gen, &FMixtormatGeneratedMask::WarpAmount, 0.0, 0.05, 0.0, 0.001),
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenWarpRadius", "Radius"), Gen, &FMixtormatGeneratedMask::WarpRadius, 1.0, 16.0, 1)));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenWarpSource", "Warp Flow (Normal to Height)"), Gen, &FMixtormatGeneratedMask::WarpSource, 0.0, 1.0, 0.0, 0.01));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GenHdrBlend", "Blend")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenGenWeight", "Weight"), Gen, &FMixtormatGeneratedMask::Weight, 0.0, 1.0, 1.0, 0.01),
		MakeMemberToggle<FMixtormatGeneratedMask>(
			LOCTEXT("GenGenInvert", "Invert"), Gen, &FMixtormatGeneratedMask::bInvert)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenGenBalance", "Balance"), Gen, &FMixtormatGeneratedMask::Balance, 0.0, 2.0, 0.5, 0.01),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenGenContrast", "Contrast"), Gen, &FMixtormatGeneratedMask::Contrast, 0.0, 10.0, 1.0, 0.01)));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenGenOffset", "Offset"), Gen, &FMixtormatGeneratedMask::Offset, -1.0, 1.0, 0.0, 0.01));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedGeneratedMask() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("GeneratedMaskHeading", "GENERATED MASK"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::LayerMask,
						LOCTEXT("PreviewGeneratedMask", "Preview this generated mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGeneratedMask* Selected = GetSelectedGeneratedMask();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatGeneratedMask* Selected = GetSelectedGeneratedMask())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("GeneratedEnabledHint", "Enable this generated mask"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLayerMaskControls()
{
	// Same vocabulary as the peel and erosion panels: one generic binding per row, captions for
	// the grouping, pairs where both labels are one short word.
	const auto Mask = [this]() { return GetSelectedLayerMask(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Visible : EVisibility::Collapsed; });

	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
		.Text_Lambda([this]()
		{
			const FMixtormatMaskLayer* Selected = GetSelectedLayerMask();
			if (!Selected)
			{
				return LOCTEXT("NoSelectedMask", "No mask selected");
			}
			const FSoftObjectPath Path = !Selected->Mask.IsNull()
				? Selected->Mask.ToSoftObjectPath()
				: Selected->MaskTexture.ToSoftObjectPath();
			return FText::FromString(Path.GetAssetName());
		})
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontBody))
	];

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("SelectedMaskBlendMode", "Blend Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskLayer* Selected = GetSelectedLayerMask();
				return Selected
					? MixtormatUI::MaskBlendModeText(Selected->BlendMode)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateLambda([this]()
			{
				return BuildMaskBlendModeMenu(SelectedLayerIndex, GetSelectedChildIndex());
			}))));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatMaskLayer>(
		LOCTEXT("SelectedMaskWeight", "Weight"), Mask, &FMixtormatMaskLayer::Weight, 0.0, 1.0, 1.0, 0.01));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpShape", "Shaping")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMaskLayer>(
			LOCTEXT("MaskTilingLabel", "Tiling"), Mask, &FMixtormatMaskLayer::Tiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatMaskLayer>(
			LOCTEXT("MaskInvertLabel", "Invert"), Mask, &FMixtormatMaskLayer::bInvert)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskBalanceLabel", "Balance"), Mask, &FMixtormatMaskLayer::Balance, 0.0, 2.0, 0.5, 0.01),
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskContrastLabel", "Contrast"), Mask, &FMixtormatMaskLayer::Contrast, 0.0, 10.0, 1.0, 0.01)));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatMaskLayer>(
		LOCTEXT("MaskOffsetLabel", "Offset"), Mask, &FMixtormatMaskLayer::Offset, -1.0, 1.0, 0.0, 0.01));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) && SelectedLayerIndex > 0
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("LayerMaskHeading", "MASK BLENDING"))
			.InitiallyExpanded(false)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::LayerMask,
						LOCTEXT("PreviewSelectedMask", "Preview the selected processed mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatMaskLayer* Selected = GetSelectedLayerMask();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatMaskLayer* Selected = GetSelectedLayerMask())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("SelectedMaskEnabled", "Enable selected mask"))
				])
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
				+ SVerticalBox::Slot().AutoHeight()[Panel]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceMaskInfluenceControls()
{
	const auto Layer = [this]() -> FMixtormatLayer*
	{
		return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	// Each influence pairs with its own invert, which is exactly the case paired rows exist for.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UnderlyingHeightInfluenceLabel", "Height"), Layer,
			&FMixtormatLayer::HeightFeatureInfluence, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UnderlyingHeightInfluenceHint", "Mask this layer using the accumulated height underneath it")),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertUnderlyingHeightLabel", "Invert"), Layer,
			&FMixtormatLayer::bInvertHeightFeature,
			LOCTEXT("InvertUnderlyingHeightHint", "Favor lower underlying height instead of higher height"))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UnderlyingAOInfluenceLabel", "AO"), Layer,
			&FMixtormatLayer::AOFeatureInfluence, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UnderlyingAOInfluenceHint", "Mask this layer using the accumulated AO underneath it")),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertUnderlyingAOLabel", "Invert"), Layer,
			&FMixtormatLayer::bInvertAOFeature,
			LOCTEXT("InvertUnderlyingAOHint", "Favor occluded areas instead of exposed areas"))));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) && SelectedLayerIndex > 0
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("SurfaceMaskInfluenceHeading", "SURFACE MASK INFLUENCE"))
			.InitiallyExpanded(false)
			[
				Panel
			]
		];
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

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ChannelInfluenceHeading", "CHANNEL INFLUENCE"))
			.InitiallyExpanded(false)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildColorAdjustmentControls()
{
	const auto Layer = [this]() -> FMixtormatLayer*
	{
		return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	// Hue Shift is signed, so it fills from the centre and reads as untouched at zero.
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HueShiftLabel", "Hue Shift"), Layer, &FMixtormatLayer::HueShift, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("SaturationLabel", "Saturation"), Layer, &FMixtormatLayer::Saturation, 0.0, 2.0, 1.0, 0.01),
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("ValueLabel", "Value"), Layer, &FMixtormatLayer::Value, 0.0, 2.0, 1.0, 0.01)));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ColorAdjustmentsHeading", "COLOR ADJUSTMENTS"))
			.InitiallyExpanded(false)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildHeightBlendControls()
{
	// Every row in this panel goes through here, so the panel converts by changing one body.
	// Its legacy and compatibility rows keep their own visibility rules untouched.
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

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
			{
				return EVisibility::Collapsed;
			}
			const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
			return Layer.Type != EMixtormatLayerType::Effect
				&& Layer.ChannelMode == EMixtormatLayerChannelMode::CompleteSurface
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("HeightMaskBlendingHeading", "HEIGHT MASK BLENDING"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::HeightBlend,
						LOCTEXT("PreviewHeightBlendMask", "Preview the computed height blend mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
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
					})
				])
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
					const UMixtormatSurface* Surface =
						WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
					return MixtormatUI::HeightBlendSourceText(Surface);
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
						const EMixtormatHeightSource Source,
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
						EMixtormatHeightSource::LayerHeight,
						LOCTEXT("LayerHeightSourceOption", "Layer Height (Recommended)"),
						LOCTEXT("LayerHeightSourceHint", "Use RAMH alpha when available; otherwise use Constant Height"));
					AddOption(
						EMixtormatHeightSource::RAMHAlpha,
						LOCTEXT("RAMHHeightSourceOption", "RAMH Height"),
						LOCTEXT("RAMHHeightSourceHint", "Use authored RAMH alpha; falls back to Constant Height when unavailable"));
					AddOption(
						EMixtormatHeightSource::Constant,
						LOCTEXT("ConstantHeightSourceOption", "Constant Height"),
						LOCTEXT("ConstantHeightSourceHint", "Use one uniform height value for this layer"));
					AddOption(
						EMixtormatHeightSource::CombinedMask,
						LOCTEXT("MaskHeightSourceOption", "Mask as Height (Optional)"),
						LOCTEXT("MaskHeightSourceHint", "Explicitly use the ordered combined mask as height"));
					AddOption(
						EMixtormatHeightSource::Automatic,
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
						case EMixtormatHeightSource::RAMHAlpha: return LOCTEXT("RAMHHeightSource", "Current Height · RAMH");
						case EMixtormatHeightSource::CombinedMask: return LOCTEXT("MaskHeightSource", "Current Height · Mask");
						case EMixtormatHeightSource::Constant: return LOCTEXT("ConstantHeightSource", "Current Height · Constant");
						case EMixtormatHeightSource::Automatic: return LOCTEXT("AutomaticHeightSource", "Current Height · Automatic (Legacy)");
						case EMixtormatHeightSource::LayerHeight:
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
				NumericRow(LOCTEXT("HeightMaskStrength", "Mask Strength"), &FMixtormatLayer::HeightBlendAmount, 0.0f, 4.0f, 0.01f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("HeightBlendThreshold", "Threshold"), &FMixtormatLayer::HeightThreshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("HeightSoftness", "Softness"), &FMixtormatLayer::HeightRange, 0.0f, 1.0f, 0.005f, 0.1f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					NumericRow(LOCTEXT("BaseHeightBias", "Base Height Bias"), &FMixtormatLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					NumericRow(LOCTEXT("BlendHeightBias", "Blend Height Bias"), &FMixtormatLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					NumericRow(LOCTEXT("HeightThreshold", "Legacy Height Threshold"), &FMixtormatLayer::HeightThreshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility(EVisibility::Collapsed)
				[NumericRow(LOCTEXT("HeightRange", "Blend Softness"), &FMixtormatLayer::HeightRange, 0.0001f, 1.0f, 0.005f, 0.1f)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContrast", "Legacy Height Contrast"), &FMixtormatLayer::HeightContrast, 0.01f, 8.0f, 0.05f, 1.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightOffset", "Legacy Height Offset"), &FMixtormatLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBias", "Legacy Comparison Bias"), &FMixtormatLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)]
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					NumericRow(LOCTEXT("MaskHeightInfluence", "Mask Modulation (Compatibility)"), &FMixtormatLayer::MaskHeightInfluence, 0.0f, 1.0f, 0.01f, 0.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("HeightContactBorders", "CONTACT BORDERS"))
				.InitiallyExpanded(true)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("HeightContactAOGroup", "CONTACT AO")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
						+ SHorizontalBox::Slot().AutoWidth()
						[MakeFeaturePreviewButton(EMixtormatDebugPreviewMode::ContactAO, LOCTEXT("PreviewContactAO", "Preview Contact AO coverage in unlit dark red and cyan"))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContactAOAmount", "Amount"), &FMixtormatLayer::HeightContactAOAmount, 0.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightContactAOWidth", "Width"), &FMixtormatLayer::HeightContactAOWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 3.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("HeightBorderNormalGroup", "BORDER NORMAL")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))]
						+ SHorizontalBox::Slot().AutoWidth()
						[MakeFeaturePreviewButton(EMixtormatDebugPreviewMode::BorderNormal, LOCTEXT("PreviewBorderNormal", "Preview Border Normal coverage in unlit dark red and cyan"))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightBorderLift", "Lift"), &FMixtormatLayer::HeightBorderLift, -1.0f, 1.0f, 0.005f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[NumericRow(LOCTEXT("HeightBorderWidth", "Width"), &FMixtormatLayer::HeightBorderWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBorderNormalStrength", "Intensity"), &FMixtormatLayer::HeightBorderNormalStrength, 0.0f, 8.0f, 0.001f, 1.0f)]
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

TSharedRef<SWidget> SMixtormat::BuildEffectInspectorControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
				// Erosion is procedural and has no asset-authored maps or ranges to show here.
			return GetSelectedLayerEffect() && !GetSelectedErosion()
				? EVisibility::Visible : EVisibility::Collapsed;
		});


	// Every effect row goes through the one slider, so the panel is uniform by construction:
	// same height, same label position, same value position, whatever the row edits.
	const auto AddFloatControl = [this](
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		const float MinValue,
		const float MaxValue,
		const float Delta,
		const float DefaultValue)
	{
		AddSliderRow(
			TargetPanel,
			MakeSlider(
				Label,
				TAttribute<double>::CreateLambda([this, Member]() -> double
				{
					const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
					return Effect ? static_cast<double>(Effect->*Member) : 0.0;
				}),
				MinValue,
				MaxValue,
				DefaultValue,
				Delta,
				false,
				FMixtormatOnSliderValueChanged::CreateLambda([this, Member](const double Value)
				{
					if (FMixtormatLayerEffect* Effect = GetSelectedLayerEffect())
					{
						Effect->*Member = static_cast<float>(Value);
						RefreshLayeredPreview();
					}
				}),
				FSimpleDelegate::CreateLambda([this, Member, DefaultValue]()
				{
					FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
					if (!Effect)
					{
						return;
					}

					// An asset-backed effect resets to the value the asset authored, not to
					// the hard-coded default the procedural path uses.
					float ResetValue = DefaultValue;
					if (const UMixtormatEffect* EffectAsset = Effect->Effect.LoadSynchronous())
					{
						if (Member == &FMixtormatLayerEffect::Front) ResetValue = EffectAsset->DefaultFront;
						else if (Member == &FMixtormatLayerEffect::Width) ResetValue = EffectAsset->DefaultWidth;
						else if (Member == &FMixtormatLayerEffect::MacroWarp) ResetValue = EffectAsset->DefaultMacroWarp;
						else if (Member == &FMixtormatLayerEffect::MicroWarp) ResetValue = EffectAsset->DefaultMicroWarp;
						else if (Member == &FMixtormatLayerEffect::MicroMorph) ResetValue = EffectAsset->DefaultMicroMorph;
						else if (Member == &FMixtormatLayerEffect::Thickness) ResetValue = EffectAsset->DefaultThickness;
						else if (Member == &FMixtormatLayerEffect::Lift) ResetValue = EffectAsset->DefaultLift;
						else if (Member == &FMixtormatLayerEffect::DetailStrength) ResetValue = EffectAsset->DefaultDetailStrength;
						else if (Member == &FMixtormatLayerEffect::StainRoughness) ResetValue = EffectAsset->DefaultStainRoughness;
						else if (Member == &FMixtormatLayerEffect::StainHeightInfluence) ResetValue = EffectAsset->DefaultStainHeightInfluence;
						else if (Member == &FMixtormatLayerEffect::StainHeightWarp) ResetValue = EffectAsset->DefaultStainHeightWarp;
						else if (Member == &FMixtormatLayerEffect::StainHeightBias) ResetValue = EffectAsset->DefaultStainHeightBias;
						else if (Member == &FMixtormatLayerEffect::StainHeightContrast) ResetValue = EffectAsset->DefaultStainHeightContrast;
					}
					if (!FMath::IsNearlyEqual(Effect->*Member, ResetValue))
					{
						Effect->*Member = ResetValue;
						RefreshLayeredPreview();
					}
				})));
	};

	AddFloatControl(Panel, LOCTEXT("PeelingIntensity", "Intensity / Strength"), &FMixtormatLayerEffect::Strength, 0.0f, 1.0f, 0.01f, 1.0f);
	// Front spans both signs now that the procedural field is a signed distance: negative
	// erodes inside the mask contour, positive dilates outside it.
	AddFloatControl(Panel, LOCTEXT("PeelingBias", "Bias / Front"), &FMixtormatLayerEffect::Front, -1.0f, 1.0f, 0.005f, 0.08f);
	AddFloatControl(Panel, LOCTEXT("PeelingWidth", "Transition Width"), &FMixtormatLayerEffect::Width, 0.000001f, 0.25f, 0.001f, 0.015f);
	AddFloatControl(Panel, LOCTEXT("PeelingMacroWarp", "Macro Warp"), &FMixtormatLayerEffect::MacroWarp, -1.0f, 1.0f, 0.005f, 0.01f);
	AddFloatControl(Panel, LOCTEXT("PeelingMicroWarp", "Micro Warp"), &FMixtormatLayerEffect::MicroWarp, -1.0f, 1.0f, 0.001f, 0.003f);
	AddFloatControl(Panel, LOCTEXT("PeelingMicroMorph", "Micro Morph"), &FMixtormatLayerEffect::MicroMorph, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(Panel, LOCTEXT("PeelingThickness", "Thickness"), &FMixtormatLayerEffect::Thickness, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingLift", "Lift"), &FMixtormatLayerEffect::Lift, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingDetailStrength", "Detail Strength"), &FMixtormatLayerEffect::DetailStrength, 0.0f, 1.0f, 0.005f, 0.02f);

	TSharedRef<SVerticalBox> StainPanel = SNew(SVerticalBox);
	StainPanel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(LOCTEXT("StainColorLabel", "Stain Color"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
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
					const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
					return Effect ? Effect->StainColor : FLinearColor::White;
				})
				.Size(FVector2D(76.0f, 16.0f))
			]
		]
	];
	AddFloatControl(StainPanel, LOCTEXT("StainIntensity", "Intensity / Strength"), &FMixtormatLayerEffect::Strength, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainRoughness", "Roughness Influence"), &FMixtormatLayerEffect::StainRoughness, -1.0f, 1.0f, 0.01f, 0.2f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightInfluence", "Height Influence"), &FMixtormatLayerEffect::StainHeightInfluence, 0.0f, 1.0f, 0.01f, 0.5f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightWarp", "Height Warp"), &FMixtormatLayerEffect::StainHeightWarp, 0.0f, 1.0f, 0.01f, 0.35f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightBias", "Valley / Ridge Bias"), &FMixtormatLayerEffect::StainHeightBias, -1.0f, 1.0f, 0.01f, -1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightContrast", "Height Contrast"), &FMixtormatLayerEffect::StainHeightContrast, 0.01f, 8.0f, 0.05f, 1.0f);

	const auto MakeEnabledToggle = [this](const FText& ToolTip)
	{
		return SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("ToggleSwitch")))
			.ToolTipText(ToolTip)
			.IsChecked_Lambda([this]()
			{
				const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
				return Effect && Effect->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
			{
				if (FMixtormatLayerEffect* Effect = GetSelectedLayerEffect())
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
			SNew(SMixtormatInspectorGroup)
			.Visibility_Lambda([this]()
			{
				const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
				const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
				return Asset && Asset->EffectType == EMixtormatEffectType::Peeling
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
			SNew(SMixtormatInspectorGroup)
			.Visibility_Lambda([this]()
			{
				const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
				const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
				return Asset && Asset->EffectType == EMixtormatEffectType::Stain
					? EVisibility::Visible
					: EVisibility::Collapsed;
			})
			.Title(LOCTEXT("StainSettingsHeading", "STAIN SETTINGS"))
			.InitiallyExpanded(true)
			.HeaderAction(MakeEnabledToggle(LOCTEXT("StainEnabled", "Enable Stain")))
			[StainPanel]
		];
}

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
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBox)
		.WidthOverride(MixtormatUI::InspectorWidth)
		[
			SNew(SBorder)
			.Padding(FMargin(0.0f))
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
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
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SAssignNew(SelectedIdentityText, STextBlock)
							.Text(LOCTEXT("NoIdentity", "—"))
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
						]
					]
					// The channel-availability line ("BC · N · RAMH Authored") is gone: it restated
					// what the layer's own maps already imply and cost a row of header height on
					// every selection. The widget stays declared but unparented so the several
					// call sites that push text into it keep working untouched.
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						.Visibility(EVisibility::Collapsed)
						[
							SAssignNew(SelectedMapsText, STextBlock)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildPreviewSettingsControls()
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					.Visibility_Lambda([this]() { return GetSelectedLayerEffect() ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SScrollBox::Slot()[BuildEffectInspectorControls()]
					+ SScrollBox::Slot()[BuildProceduralPeelControls()]
					+ SScrollBox::Slot()[BuildErosionControls()]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					// The scroll box has to sit outside the group, not inside it. An expandable
					// area sizes its content to whatever that content asks for, so a scroll box
					// within one is handed unbounded height and never scrolls -- the layer
					// inspector simply ran off the bottom of the panel.
					SNew(SScrollBox)
					.Visibility_Lambda([this]() { return GetSelectedLayerEffect() ? EVisibility::Collapsed : EVisibility::Visible; })
					+ SScrollBox::Slot()
					[
						// No wrapping LAYER group. Selecting a layer shows its sections --
						// Channel Influence, Composition, Surface Adjustments, Colour, Height
						// Mask Blending -- as siblings in the column. A group whose only job was
						// to hold other groups added a header, an indent and a second thing to
						// expand before anything could be edited.
						SNew(SVerticalBox)
						.Visibility_Lambda([this]()
						{
							return bHasSelectedLayer ? EVisibility::Visible : EVisibility::Collapsed;
						})
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 3.0f)
						[
							SNew(SCheckBox)
							.Visibility_Lambda([this]()
							{
								return SelectedLayerIndex > 0
									&& WorkingLayers.IsValidIndex(SelectedLayerIndex)
									&& WorkingLayers[SelectedLayerIndex].Type != EMixtormatLayerType::Fill
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.ToolTipText(LOCTEXT("NormalOnlyModeHint", "Compose only this layer's normal and preserve Base Color and RAM below."))
							.IsChecked_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMixtormatLayerChannelMode::NormalDetail ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { SetLayerNormalDetail(SelectedLayerIndex, State == ECheckBoxState::Checked); })
							[
								SNew(STextBlock).Text(LOCTEXT("NormalOnlyMode", "Normal Detail Only"))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
						[
							SNew(SComboButton)
							.Visibility_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMixtormatLayerChannelMode::NormalDetail ? EVisibility::Visible : EVisibility::Collapsed; })
							.ButtonContent()[SNew(STextBlock).Text_Lambda([this]() { if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)) return LOCTEXT("NormalSource", "Choose Normal Source..."); const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex]; return Layer.NormalSourceType == EMixtormatNormalSourceType::Texture ? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName()) : LOCTEXT("SurfaceNormal", "Surface Normal"); })]
							.OnGetMenuContent_Lambda([this]() { return BuildNormalSourceMenu(SelectedLayerIndex); })
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
										.Size(FVector2D(108.0f, 18.0f))
									]
								]
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillRoughness", "Roughness"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Roughness, 0.0, 1.0, 0.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillIOR", "IOR"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::IOR, 1.0, 3.0, 1.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillMetallic", "Metallic"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Metallic, 0.0, 1.0, 0.0, 0.01)
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
								return SelectedLayerIndex > 0
									? EVisibility::Visible
									: EVisibility::Collapsed;
							})
							.Title(LOCTEXT("CompositionLabel", "COMPOSITION"))
							.InitiallyExpanded(true)
							[
								SNew(SVerticalBox)

								// One control, not three fields. BLEND / OVER / COAT / DETAIL are the
								// only combinations of ChannelMode, CompositionMode and NormalBlendMode
								// that mean anything, and they are the same four words the layer's
								// badge prints -- so the stack and the inspector teach one vocabulary.
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
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
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
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
							.InitiallyExpanded(false)
							[
								SNew(SVerticalBox)
																+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MakeMemberSlider<FMixtormatLayer>(
										LOCTEXT("TilingLabel", "Tiling"), LayerForRows(), &FMixtormatLayer::Tiling, 1.0, 8.0, 2.0, 0.1)
								]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("RoughnessLabel", "Roughness Bias"), LayerForRows(), &FMixtormatLayer::RoughnessBias, 0.0, 1.0, 0.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("RoughnessContrastLabel", "Roughness Contrast"), LayerForRows(), &FMixtormatLayer::RoughnessContrast, 0.0, 2.0, 1.0, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("RoughnessOffsetLabel", "Roughness Offset"), LayerForRows(), &FMixtormatLayer::RoughnessOffset, -0.5, 0.5, 0.0, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("NormalLabel", "Normal Intensity"), LayerForRows(), &FMixtormatLayer::NormalIntensity, 0.0, 2.0, 1.0, 0.01)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
							[
								SNew(SMixtormatInspectorGroup)
								.Title(LOCTEXT("GeneratedFeaturesHeading", "NORMAL / HEIGHT / AO INFLUENCE"))
								.InitiallyExpanded(false)
								.HeaderAction(MakeFeaturePreviewButton(
									EMixtormatDebugPreviewMode::GeneratedFeature,
									LOCTEXT("PreviewGeneratedFeature", "Preview the cavity-to-convex feature mask in unlit dark red and cyan")))
								[
									SNew(SVerticalBox)
																		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
									[
										MakeMemberSlider<FMixtormatLayer>(
											LOCTEXT("FeatureInfluenceLabel", "Normal Influence"), LayerForRows(), &FMixtormatLayer::FeatureInfluence, 0.0, 1.0, 0.0, 0.01)
									]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FeatureBiasLabel", "Cavity to Convex"), LayerForRows(), &FMixtormatLayer::FeatureBias, 0.0, 1.0, 0.0, 0.01)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
								[SNew(STextBlock).Text(LOCTEXT("InvertGeneratedFeatureLabel", "Invert Feature"))]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SCheckBox)
									.ToolTipText(LOCTEXT("InvertGeneratedFeatureHint", "Apply one-minus to the selected cavity-to-convex feature mask"))
									.IsChecked_Lambda([this]()
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex)
											&& WorkingLayers[SelectedLayerIndex].bInvertFeature
											? ECheckBoxState::Checked
											: ECheckBoxState::Unchecked;
									})
									.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
									{
										if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
										{
											WorkingLayers[SelectedLayerIndex].bInvertFeature = State == ECheckBoxState::Checked;
											RefreshLayeredPreview();
										}
									})
								]
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSliderInt<FMixtormatLayer>(
									LOCTEXT("CurvatureRadiusLabel", "Radius"), LayerForRows(), &FMixtormatLayer::CurvatureRadius, 1.0, 32.0, 2)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSliderInt<FMixtormatLayer>(
									LOCTEXT("CurvatureSmoothingLabel", "Smoothing"), LayerForRows(), &FMixtormatLayer::CurvatureSmoothing, 1.0, 4.0, 2)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("CurvatureStrengthLabel", "Strength"), LayerForRows(), &FMixtormatLayer::CurvatureStrength, 0.0, 8.0, 1.0, 0.05)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("CurvaturePowerLabel", "Power"), LayerForRows(), &FMixtormatLayer::CurvaturePower, 0.001, 8.0, 1.0, 0.05)
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
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildGeneratedMaskControls()
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							BuildLayerMaskControls()
						]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
