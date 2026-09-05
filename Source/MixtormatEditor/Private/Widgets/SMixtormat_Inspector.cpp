#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatTile.h"

// The inspector column: every per-selection parameter panel.

#define LOCTEXT_NAMESPACE "SMixtormat"

// Quarter turns, shared shape between the mask and the layer. Rotation is offered at all only
// because 90 degree steps are permutations of the unit square: the compositor wraps every source
// read in a frac(), so an arbitrary angle would drag the tile corners outside the domain.
namespace
{
	const EMixtormatUVRotation GMixtormatUVRotations[] = {
		EMixtormatUVRotation::None,
		EMixtormatUVRotation::Quarter,
		EMixtormatUVRotation::Half,
		EMixtormatUVRotation::ThreeQuarter
	};
}

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

TSharedRef<SWidget> SMixtormat::BuildGradeTonemapMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatGradeTonemap Modes[] = {
		EMixtormatGradeTonemap::None,
		EMixtormatGradeTonemap::Reinhard,
		EMixtormatGradeTonemap::ACES,
		EMixtormatGradeTonemap::Filmic
	};
	for (const EMixtormatGradeTonemap Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::GradeTonemapText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatLayerEffect* E = GetSelectedGrade())
				{
					E->GradeTonemap = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatLayerEffect* E = GetSelectedGrade();
				return E && E->GradeTonemap == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGradeControls()
{
	const auto Grade = [this]() { return GetSelectedGrade(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("GradeAmount", "Amount"), Grade, &FMixtormatLayerEffect::GradeAmount, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("GradeAmountHint", "Blend against the ungraded color. 0 is the identity, which is the Filter contract every filter here keeps.")));

	// The order the chain runs in, which is also the order these rows are listed in.
	// Brightness and contrast are linear operations and belong above the tonemap; gamma is
	// display shaping and belongs below it.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GradeGrpLinear", "Linear")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeBrightness", "Brightness"), Grade, &FMixtormatLayerEffect::GradeBrightness, 0.0, 4.0, 1.0, 0.01,
			LOCTEXT("GradeBrightnessHint", "A gain, not an offset. Scaling linear values behaves like exposure and leaves hue alone; adding a constant washes saturation out of the darks.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeContrast", "Contrast"), Grade, &FMixtormatLayerEffect::GradeContrast, 0.0, 4.0, 1.0, 0.01,
			LOCTEXT("GradeContrastHint", "Scales the distance from the pivot. 1 is unchanged, 0 flattens everything to the pivot value."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("GradePivot", "Pivot"), Grade, &FMixtormatLayerEffect::GradeContrastPivot, 0.0, 1.0, 0.18, 0.01,
		LOCTEXT("GradePivotHint", "The value contrast pivots about. 0.18 is linear mid grey and is correct for this data; 0.5 is what display-referred habits reach for, which is why it is a control.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GradeGrpTonemap", "Tonemap")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("GradeTonemapMode", "Operator"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedGrade();
				return E ? MixtormatUI::GradeTonemapText(E->GradeTonemap) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildGradeTonemapMenu)),
		LOCTEXT("GradeTonemapHint", "Reinhard never clips but desaturates highlights and only reaches white at infinity, so bright areas go pale. ACES is contrastier with a filmic toe and is closest to what a renderer will do to this surface later. Filmic is Hable's Uncharted 2 curve.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("GradeTonemapStrength", "Strength"), Grade, &FMixtormatLayerEffect::GradeTonemapStrength, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("GradeTonemapStrengthHint", "Blend between the untonemapped and tonemapped result, so an operator can be dialled in rather than only switched on.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GradeGrpDisplay", "Display")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("GradeGamma", "Gamma"), Grade, &FMixtormatLayerEffect::GradeGamma, 0.05, 4.0, 1.0, 0.01,
		LOCTEXT("GradeGammaHint", "Applied as pow(c, 1/Gamma), so above 1 lifts the midtones. That is the convention every grading UI uses; the reciprocal is easy to get backwards.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GradeGrpMask", "Mask")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("GradeInvertMask", "Invert"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedGrade();
				return E && E->bGradeInvertMask ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatLayerEffect* E = GetSelectedGrade())
				{
					E->bGradeInvertMask = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("GradeInvertMaskHint", "The grade uses this layer's accumulated mask children, which is what makes it an adjustment layer. Invert grades everything the mask does not cover. A layer with no mask grades everywhere.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedGrade() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("GradeHeading", "GRADE"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildChippingControls()
{
	const auto Chip = [this]() { return GetSelectedChipping(); };

	const auto Slider = [this, Chip](
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(Label, Chip, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpSurface", "Surface")));

	// The smooth height selection used by the cavity-biased chip picker. Chipping needs no
	// cell lattice: raised material is wherever the current composited height clears this threshold.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipGroutLevel", "Grout Level"), &FMixtormatLayerEffect::ChipGroutLevel, 0.0, 1.0, 0.5, 0.005,
			LOCTEXT("ChipGroutLevelHint", "The height that separates raised material from recess. Chips only live above it, so this is what tells the filter where the bricks, planks or tiles are -- it reads the height you actually composited rather than a lattice of its own.")),
		Slider(LOCTEXT("ChipGroutSoft", "Softness"), &FMixtormatLayerEffect::ChipGroutSoftness, 0.001, 0.5, 0.08, 0.001,
			LOCTEXT("ChipGroutSoftHint", "Width of the transition around Grout Level. Wider softens where a chip is allowed to start and lets it fade out near a recess rather than stopping on a hard line."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpCavity", "Cavity")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipCavityInfluence", "Influence"), &FMixtormatLayerEffect::ChipCavityInfluence, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ChipCavityInfluenceHint", "Mixes local cavity into chip placement. 0 ignores cavity; 1 restricts seeds to the cavity remap.")),
		Slider(LOCTEXT("ChipCavityOffset", "Offset"), &FMixtormatLayerEffect::ChipCavityOffset, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("ChipCavityOffsetHint", "Moves the measured cavity before it is remapped."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipCavityRemapMin", "In Low"), &FMixtormatLayerEffect::ChipCavityRemapMin, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("ChipCavityRemapHint", "Cavity mapped to 0..1. Set Low above High to invert the gate.")),
		Slider(LOCTEXT("ChipCavityRemapMax", "In High"), &FMixtormatLayerEffect::ChipCavityRemapMax, -1.0, 1.0, 0.04, 0.005,
			LOCTEXT("ChipCavityRemapHint", "Cavity mapped to 0..1. Set Low above High to invert the gate."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpHeight", "Height")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipHeightInfluence", "Influence"), &FMixtormatLayerEffect::ChipHeightInfluence, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("ChipHeightInfluenceHint", "Mixes the layer's current height into seed placement. Grout Level still bounds propagation.")),
		Slider(LOCTEXT("ChipHeightScale", "Contrast"), &FMixtormatLayerEffect::ChipHeightScale, 0.1, 8.0, 1.0, 0.05,
			LOCTEXT("ChipHeightScaleHint", "Shapes the smooth height selection before it is mixed with cavity."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpMask", "Placement Mask")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
	[
		SNew(SBox).HeightOverride(18.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("ChipMaskSlot", "Mask"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("ChipMaskSlotHint", "Mask used to place chipping. Unset uses the layer's accumulated mask children."))
				.ButtonContent()
				[
					SNew(SBox).MinDesiredWidth(120.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const FMixtormatLayerEffect* C = GetSelectedChipping();
							if (!C)
							{
								return LOCTEXT("ChipMaskNone", "Child Mask");
							}
							if (!C->ChipMask.IsNull())
							{
								return FText::FromString(C->ChipMask.ToSoftObjectPath().GetAssetName());
							}
							if (!C->ChipMaskTexture.IsNull())
							{
								return FText::FromString(C->ChipMaskTexture.ToSoftObjectPath().GetAssetName());
							}
							return LOCTEXT("ChipMaskNone", "Child Mask");
						})
					]
				]
				.OnGetMenuContent_Lambda([this]()
				{
					return SNew(SBox)
						.WidthOverride(MixtormatTokens::MaskPickerWidth)
						.Padding(MixtormatTokens::TileGap)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().MaxHeight(420.0f)
							[
								SNew(SScrollBox) + SScrollBox::Slot()
								[
									BuildMaskGallery([this](const FSoftObjectPath& Path)
									{
										FMixtormatLayerEffect* C = GetSelectedChipping();
										if (!C)
										{
											return;
										}
										UObject* MaskObject = Path.TryLoad();
										if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
										{
											C->ChipMask = TSoftObjectPtr<UMixtormatMask>(Path);
											C->ChipMaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
										}
										else if (Cast<UTexture2D>(MaskObject))
										{
											C->ChipMask.Reset();
											C->ChipMaskTexture = TSoftObjectPtr<UTexture2D>(Path);
										}
										else
										{
											return;
										}
										RefreshLayeredPreview();
									})
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							.Padding(0.0f, MixtormatTokens::TileGap, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
								.Text(LOCTEXT("ChipMaskClear", "Use the layer's child mask"))
								.OnClicked_Lambda([this]()
								{
									if (FMixtormatLayerEffect* C = GetSelectedChipping())
									{
										C->ChipMask.Reset();
										C->ChipMaskTexture.Reset();
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
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("ChipMaskTiling", "Tiling"), Chip, &FMixtormatLayerEffect::ChipMaskTiling, 1.0, 16.0, 1,
			LOCTEXT("ChipMaskTilingHint", "Integer tiling for the selected placement mask.")),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("ChipInvertMask", "Invert"), Chip, &FMixtormatLayerEffect::bChipInvertMask,
			LOCTEXT("ChipInvertMaskHint", "Inverts the selected placement mask, or the layer child mask when no mask is selected."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpChips", "Chips")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipAmount", "Amount"), &FMixtormatLayerEffect::ChipAmount, 0.0, 1.0, 0.45, 0.01,
			LOCTEXT("ChipAmountHint", "How readily chips start in the smooth height selection, biased toward cavities -- density, not depth. Raising it adds chips rather than deepening existing ones. Placement is gated by this layer's mask; with no mask the height and cavity picker covers the layer. 0 leaves the height untouched and skips the passes entirely.")),
		Slider(LOCTEXT("ChipSize", "Size"), &FMixtormatLayerEffect::ChipSize, 0.0, 1.0, 0.6, 0.01,
			LOCTEXT("ChipSizeHint", "How far a chip runs before it dies: about 7 pixels at 0, 16 at the default, and past 200 at 1. The only thing that attenuates a growing chip, so it is the size control -- but Iterations is a hard cap on top of it, and at the top of this range that cap is what you will hit."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipDepth", "Depth"), &FMixtormatLayerEffect::ChipDepth, 0.0, 0.25, 0.035, 0.001,
			LOCTEXT("ChipDepthHint", "How far a fully formed chip cuts into the height. Also scales the normal, so the lighting follows the control.")),
		Slider(LOCTEXT("ChipIrregularity", "Irregularity"), &FMixtormatLayerEffect::ChipIrregularity, 0.0, 1.0, 0.6, 0.01,
			LOCTEXT("ChipIrregularityHint", "Weights a swirling noise against the straight-inward direction, and loosens the alignment test that grows a chip. 0 gives clean wedges driven straight in from the edge; 1 gives ragged wandering ones."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("ChipIterations", "Iterations"), Chip, &FMixtormatLayerEffect::ChipIterations, 1.0, 32.0, 16,
			LOCTEXT("ChipIterationsHint", "A chip advances one pixel per pass, so this is the hard limit on how far one can reach. Set it above where Size runs out or this becomes the thing deciding chip size. Scaled internally by the render resolution, so a preview and an export show the same chip size rather than the same pixel count -- which also means this is the one filter whose cost grows with output size. At 4K the scaling caps at 96 full-resolution passes.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("ChipSeed", "Seed"), Chip, &FMixtormatLayerEffect::ChipSeed, 0.0, 64.0, 1,
			LOCTEXT("ChipSeedHint", "Reshuffles where chips start and which way they wander, without changing how many there are."))));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipNormalStrength", "Normal Strength"), &FMixtormatLayerEffect::ChipNormalStrength, 0.0, 32.0, 8.0, 0.05,
			LOCTEXT("ChipNormalStrengthHint", "Gain on the normal derived from the chip mask. Same meaning and default as the erosion control, because both passes use the same Sobel normalisation.")),
		Slider(LOCTEXT("ChipMaskEdge", "Mask Edge"), &FMixtormatLayerEffect::ChipMaskEdge, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("ChipMaskEdgeHint", "Biases chips toward the edge of this layer's own mask -- where they start, how long they survive and how deep they cut. Inert on a layer whose mask is uniform, and zero by default."))));

	// Colour and roughness for the substrate a chip exposes. Same shader as the erosion shade
	// pass, but fed the chip mask directly rather than a height difference.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpShade", "Exposed")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(LOCTEXT("ChipColorLabel", "Color"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("OpenChipColorPicker", "The color a chip exposes. Blended toward rather than multiplied, so it can reach a color lighter than the surface it broke off."))
			.OnClicked_Lambda([this]()
			{
				return OpenChipColorPicker(SelectedLayerIndex, SelectedEffectIndex);
			})
			[
				SNew(SColorBlock)
				.Color_Lambda([this]()
				{
					const FMixtormatLayerEffect* E = GetSelectedChipping();
					return E ? E->ChipColor : FLinearColor::White;
				})
				.Size(FVector2D(76.0f, 16.0f))
			]
		]
	];
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ChipColorAmount", "Color"), &FMixtormatLayerEffect::ChipColorAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("ChipColorAmountHint", "How far a chip blends toward the exposed color. 0 leaves the composited base color alone and skips the pass entirely. Unlike erosion this stays correct at Depth 0, because coverage is the chip mask rather than a height difference.")),
		Slider(LOCTEXT("ChipRoughAmount", "Roughness"), &FMixtormatLayerEffect::ChipRoughnessAmount, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("ChipRoughAmountHint", "Signed offset on the composited roughness inside a chip, positive toward rough."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedChipping() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ChippingHeading", "CHIPPING"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildCraquelureBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatCraquelure* C = GetSelectedCraquelure())
				{
					C->BlendMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				return C && C->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildCraquelureModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatCraquelureMode Modes[] = {
		EMixtormatCraquelureMode::Lattice,
		EMixtormatCraquelureMode::Propagated
	};
	for (const EMixtormatCraquelureMode Mode : Modes)
	{
		const FText Label = Mode == EMixtormatCraquelureMode::Propagated
			? LOCTEXT("CraqModeMenuPropagated", "Propagated")
			: LOCTEXT("CraqModeMenuLattice", "Lattice");
		Menu.Item(
			Label,
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatCraquelure* C = GetSelectedCraquelure())
				{
					C->Mode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				return C && C->Mode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->BlendMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdRotationMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatUVRotation Rotation : GMixtormatUVRotations)
	{
		Menu.Item(
			MixtormatUI::UVRotationText(Rotation),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Rotation]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->Rotation = Rotation;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Rotation]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->Rotation == Rotation;
			}));
	}
	return Menu.Build();
}

FReply SMixtormat::AddColorIdEntry()
{
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		if (C->Colors.Num() < FMixtormatColorIdMask::MaxColors)
		{
			// A new entry is white rather than a copy of the last. Duplicating the last colour
			// would add a row that selects exactly what is already selected, which reads as the
			// button having done nothing.
			C->Colors.Add(FLinearColor::White);
			RefreshLayeredPreview();
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveColorIdEntry(const int32 ColorIndex)
{
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		if (C->Colors.IsValidIndex(ColorIndex))
		{
			C->Colors.RemoveAt(ColorIndex);
			RefreshLayeredPreview();
		}
	}
	return FReply::Handled();
}

void SMixtormat::SetColorIdColor(
	const FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex,
	const int32 ColorIndex)
{
	// Resolved by index rather than through GetSelectedColorId, because the picker is modeless:
	// the selection can move while it is open, and committing to whatever happens to be selected
	// when the user drags a swatch would edit a different node from the one they opened.
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
	if (Child.Type != EMixtormatLayerChildType::ColorId
		|| !Child.ColorId.Colors.IsValidIndex(ColorIndex))
	{
		return;
	}
	Child.ColorId.Colors[ColorIndex] = NewColor;
	RefreshLayeredPreview();
}

FReply SMixtormat::OpenColorIdPicker(const int32 ColorIndex)
{
	const FMixtormatColorIdMask* Selected = GetSelectedColorId();
	if (!Selected || !Selected->Colors.IsValidIndex(ColorIndex))
	{
		return FReply::Handled();
	}

	const int32 LayerIndex = SelectedLayerIndex;
	const int32 ChildIndex = SelectedMaskIndex;
	const FLinearColor OriginalColor = Selected->Colors[ColorIndex];

	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = OriginalColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex](const FLinearColor NewColor)
		{
			SetColorIdColor(NewColor, LayerIndex, ChildIndex, ColorIndex);
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex, OriginalColor](const FLinearColor)
		{
			SetColorIdColor(OriginalColor, LayerIndex, ChildIndex, ColorIndex);
		});
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdControls()
{
	const auto Id = [this]() { return GetSelectedColorId(); };

	const auto Slider = [this, Id](
		const FText& Label,
		float FMixtormatColorIdMask::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatColorIdMask>(Label, Id, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// The map. Any Texture2D rather than the library gallery the other mask slots offer: an ID
	// map arrives with the mesh from whatever built it, and it is not a Mixtormat asset and never
	// will be.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("IdGrpSource", "ID Map")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SObjectPropertyEntryBox)
		.AllowedClass(UTexture2D::StaticClass())
		.DisplayThumbnail(false)
		.AllowClear(true)
		.ToolTipText(LOCTEXT("IdTextureHint", "The ID map. Import it with sRGB off and compression set to an uncompressed format: both settings move the colours the map stores, and a selection is a comparison against a colour picked out of them. DXT in particular invents intermediate values along every ID boundary."))
		.ObjectPath_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C ? C->IdTexture.ToSoftObjectPath().ToString() : FString();
		})
		.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
		{
			if (FMixtormatColorIdMask* C = GetSelectedColorId())
			{
				C->IdTexture = TSoftObjectPtr<UTexture2D>(AssetData.ToSoftObjectPath());
				RefreshLayeredPreview();
			}
		})
	];

	// The selection. One row per colour: a swatch that opens a picker, and a button that drops
	// it. Rebuilt rather than bound, because the row count is the data here -- adding an ID adds
	// a widget, which no attribute can express.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("IdGrpColors", "Selected IDs")));

	// Every row that could exist is laid out once and shows itself when the selection reaches it.
	// The panel is built at construction, long before anything is selected, so a loop over the
	// current entries would bake in whatever the count happened to be then -- which is zero.
	for (int32 ColorIndex = 0; ColorIndex < FMixtormatColorIdMask::MaxColors; ++ColorIndex)
	{
		{
			Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this, ColorIndex]()
				{
					const FMixtormatColorIdMask* C = GetSelectedColorId();
					return C && C->Colors.IsValidIndex(ColorIndex)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.ToolTipText(LOCTEXT("IdSwatchHint", "The colour to select. Pick it out of the ID map with the eyedropper in the colour window."))
					.OnClicked_Lambda([this, ColorIndex]() { return OpenColorIdPicker(ColorIndex); })
					[
						SNew(SColorBlock)
						.Color_Lambda([this, ColorIndex]()
						{
							const FMixtormatColorIdMask* C = GetSelectedColorId();
							return C && C->Colors.IsValidIndex(ColorIndex)
								? C->Colors[ColorIndex]
								: FLinearColor::Black;
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(MixtormatTokens::TileGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.Text(LOCTEXT("IdRemoveColor", "Remove"))
					.OnClicked_Lambda([this, ColorIndex]() { return RemoveColorIdEntry(ColorIndex); })
				]
			];
		}
	}

	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.Text(LOCTEXT("IdAddColor", "Add ID"))
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Colors.Num() < FMixtormatColorIdMask::MaxColors;
		})
		.OnClicked_Lambda([this]() { return AddColorIdEntry(); })
	];

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdTolerance", "Tolerance"), &FMixtormatColorIdMask::Tolerance, 0.0, 1.0, 0.10, 0.001,
			LOCTEXT("IdToleranceHint", "How far from a selected colour still counts, as a distance in RGB. The diagonal of the colour cube is about 1.73, so this is small by nature: the default admits the wobble a compressed map leaves across a flat region without reaching a neighbouring ID. Raise it until the part fills in; if it starts claiming its neighbours, the map wants a cleaner import rather than a wider tolerance.")),
		Slider(LOCTEXT("IdSoftness", "Softness"), &FMixtormatColorIdMask::Softness, 0.0, 0.5, 0.02, 0.001,
			LOCTEXT("IdSoftnessHint", "Width of the transition either side of Tolerance. The map is point sampled -- the average of two IDs is a third colour that names neither -- so the selection edge is a hard texel boundary, and this is what feathers it."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("IdGrpPlacement", "Placement")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingX", "Tiling X"), Id, &FMixtormatColorIdMask::TilingX, 1.0, 16.0, 1,
			LOCTEXT("IdTilingXHint", "Integer only. A fractional scale lands mid-texel at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingY", "Tiling Y"), Id, &FMixtormatColorIdMask::TilingY, 1.0, 16.0, 1,
			LOCTEXT("IdTilingYHint", "Integer only, for the same reason as Tiling X."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdOffsetU", "Offset U"), &FMixtormatColorIdMask::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetUHint", "Moves where the map is read from, in UV. Unrelated to Offset under Blend, which lifts the mask value instead.")),
		Slider(LOCTEXT("IdOffsetV", "Offset V"), &FMixtormatColorIdMask::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetVHint", "Moves where the map is read from, in UV."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipU", "Flip U"), Id, &FMixtormatColorIdMask::bFlipU,
			LOCTEXT("IdFlipUHint", "Mirrors the map horizontally before it is tiled.")),
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipV", "Flip V"), Id, &FMixtormatColorIdMask::bFlipV,
			LOCTEXT("IdFlipVHint", "Mirrors the map vertically before it is tiled. The usual fix when a map was authored under the other texture-coordinate convention."))));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("IdRotation", "Rotation"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::UVRotationText(C->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdRotationMenu)),
		LOCTEXT("IdRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("IdGrpBlend", "Blend")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("IdBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::MaskBlendModeText(C->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdBlendModeMenu)),
		LOCTEXT("IdBlendModeHint", "How the selection combines with the mask accumulated above it in this layer. Max is what unions two ID nodes; Multiply is what intersects one with a painted mask.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdWeight", "Weight"), &FMixtormatColorIdMask::Weight, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("IdWeightHint", "How far the blend is taken. 0 is the off switch for this node, and it costs nothing -- the pass is skipped rather than run to reproduce its input.")),
		Slider(LOCTEXT("IdContrast", "Contrast"), &FMixtormatColorIdMask::Contrast, 0.0, 10.0, 1.0, 0.01,
			LOCTEXT("IdContrastHint", "Scales the selection about its midpoint before it is blended. Only does anything inside the softness band, since the rest of the mask is already flat 0 or 1."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdBalance", "Balance"), &FMixtormatColorIdMask::Balance, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("IdBalanceHint", "Pushes the softness band toward the selection or away from it without moving its midpoint.")),
		Slider(LOCTEXT("IdOffset", "Offset"), &FMixtormatColorIdMask::Offset, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("IdOffsetHint", "Lifts or lowers the whole mask after contrast. Above 0 the unselected regions stop being fully masked out."))));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("IdInvert", "Invert"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->bInvert = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("IdInvertHint", "Selects everything except the chosen IDs. Usually shorter than listing the other seven.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedColorId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ColorIdHeading", "COLOR ID"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildCraquelureControls()
{
	const auto Craq = [this]() { return GetSelectedCraquelure(); };

	const auto Slider = [this, Craq](
		const FText& Label,
		float FMixtormatCraquelure::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatCraquelure>(Label, Craq, Member, Min, Max, Default, Snap, Hint);
	};

	// Visibility for the two mode-specific groups, so the panel only ever shows the controls
	// that do anything. Both read the same selection, so a null selection collapses both.
	const auto LatticeOnly = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const FMixtormatCraquelure* C = GetSelectedCraquelure();
		return C && C->Mode == EMixtormatCraquelureMode::Lattice
			? EVisibility::Visible : EVisibility::Collapsed;
	});
	const auto PropagatedOnly = TAttribute<EVisibility>::CreateLambda([this]()
	{
		const FMixtormatCraquelure* C = GetSelectedCraquelure();
		return C && C->Mode == EMixtormatCraquelureMode::Propagated
			? EVisibility::Visible : EVisibility::Collapsed;
	});

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("CraqMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				if (!C) { return FText::GetEmpty(); }
				return C->Mode == EMixtormatCraquelureMode::Propagated
					? LOCTEXT("CraqModePropagated", "Propagated")
					: LOCTEXT("CraqModeLattice", "Lattice");
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildCraquelureModeMenu)),
		LOCTEXT("CraqModeHint", "Lattice measures distance to a Voronoi cell wall. Propagated grows cracks through stress, toughness, and curl-flow fields.")));

	TSharedRef<SVerticalBox> LatticeGroup = SNew(SVerticalBox);
	LatticeGroup->SetVisibility(LatticeOnly);

	AddSliderRow(LatticeGroup, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqPeriod", "Cells"), Craq, &FMixtormatCraquelure::Period, 1.0, 64.0, 16,
			LOCTEXT("CraqPeriodHint", "Cells across one UV repeat. Any integer tiles, because the lattice wraps on it.")),
		Slider(LOCTEXT("CraqJitter", "Jitter"), &FMixtormatCraquelure::Jitter, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("CraqJitterHint", "0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives organic crazing. This one control spans tile seams to cracked paint."))));

	Panel->AddSlot().AutoHeight()[LatticeGroup];

	TSharedRef<SVerticalBox> GrowGroup = SNew(SVerticalBox);
	GrowGroup->SetVisibility(PropagatedOnly);

	AddSliderRow(GrowGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpSeed", "Nucleation")));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqSeedCells", "Seed Cells"), Craq, &FMixtormatCraquelure::SeedCells, 1.0, 128.0, 4,
			LOCTEXT("CraqSeedCellsHint", "Nuclei are placed one per cell of this lattice. Fewer, longer cracks come from a coarse lattice; crazing from a fine one.")),
		Slider(LOCTEXT("CraqSeedChance", "Density"), &FMixtormatCraquelure::SeedChance, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("CraqSeedChanceHint", "Fraction of cells that actually get a nucleus. This is how many separate cracks there are, as opposed to how long they run."))));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqSeedJitter", "Seed Jitter"), &FMixtormatCraquelure::SeedJitter, 0.0, 1.0, 0.85, 0.01,
			LOCTEXT("CraqSeedJitterHint", "0 puts every nucleus at its cell centre, which the grown network still shows as a grid. 1 hides the seeding lattice entirely.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqIterations", "Reach"), Craq, &FMixtormatCraquelure::Iterations, 1.0, 1024.0, 48,
			LOCTEXT("CraqIterationsHint", "How far a crack can travel, in pixels at a 1024 reference and scaled by the render resolution so a preview and an export grow the same network. One dispatch per step and by some way the most expensive node here, so this is the control that costs -- at the top of the range it is around a thousand full-resolution passes."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpField", "Material")));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqNoiseCells", "Field Scale"), Craq, &FMixtormatCraquelure::NoiseCells, 1.0, 32.0, 5,
			LOCTEXT("CraqNoiseCellsHint", "Scale of the stress, toughness and direction fields. Below the seed lattice it steers whole regions; above it, it roughens individual cracks.")),
		Slider(LOCTEXT("CraqStressVar", "Stress Var"), &FMixtormatCraquelure::StressVariation, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("CraqStressVarHint", "How unevenly the driving stress is distributed. Cracks run toward high stress."))));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqToughVar", "Tough Var"), &FMixtormatCraquelure::ToughnessVariation, 0.0, 1.0, 0.45, 0.01,
			LOCTEXT("CraqToughVarHint", "How unevenly the material resists. Cracks route around tough patches, which is what stops the network looking uniform.")),
		Slider(LOCTEXT("CraqFlowStrength", "Alignment"), &FMixtormatCraquelure::FlowStrength, 0.0, 1.0, 0.18, 0.01,
			LOCTEXT("CraqFlowStrengthHint", "How strongly the direction field bends a running crack. At 1 cracks follow the field and come out combed; the default lets the field suggest without dictating."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpGrowth", "Growth")));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqPersistence", "Persistence"), &FMixtormatCraquelure::Persistence, 0.0, 1024.0, 1.65, 0.1,
			LOCTEXT("CraqPersistenceHint", "How strongly a tip keeps its heading. This is what makes a crack a line rather than a blob: drop it and the front spreads in every direction at once.")),
		Slider(LOCTEXT("CraqThreshold", "Threshold"), &FMixtormatCraquelure::GrowthThreshold, -1.0, 3.0, 0.55, 0.01,
			LOCTEXT("CraqThresholdHint", "The score a step has to beat to happen at all. Raising it starves growth, which is what decides how much of the surface ends up cracked."))));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqIrregularity", "Irregularity"), &FMixtormatCraquelure::Irregularity, 0.0, 32.0, 0.32, 0.01,
			LOCTEXT("CraqIrregularityHint", "Per-step randomness in the scoring. Low values give clean arcs, high values give a wandering, brittle line.")),
		Slider(LOCTEXT("CraqTurnResponse", "Turn"), &FMixtormatCraquelure::TurnResponse, 0.0, 1.0, 0.72, 0.01,
			LOCTEXT("CraqTurnResponseHint", "How fast a tip turns toward the step it just took. Low values curve; high values snap to the eight-way lattice and show the staircase."))));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqStressGain", "Stress Gain"), &FMixtormatCraquelure::StressGain, 0.0, 32.0, 0.75, 0.01,
			LOCTEXT("CraqStressGainHint", "How much the stress field is worth against persistence when a step is scored.")),
		Slider(LOCTEXT("CraqToughCost", "Tough Cost"), &FMixtormatCraquelure::ToughnessCost, 0.0, 4.0, 0.95, 0.01,
			LOCTEXT("CraqToughCostHint", "How much toughness counts against a step. Raise it past Stress Gain and cracks will detour a long way to avoid hard material."))));
	AddSliderRow(GrowGroup, MakeMemberSliderInt<FMixtormatCraquelure>(
		LOCTEXT("CraqCollision", "Collision Limit"), Craq, &FMixtormatCraquelure::CollisionLimit, 1.0, 8.0, 2,
		LOCTEXT("CraqCollisionHint", "Cracked neighbours a pixel may already have and still be grown into. This is the T-junction: at 2 a crack reaching an older one stops, because the older crack has already released the stress driving it. Raise it and cracks cross instead, which reads as scratches rather than fracture.")));

	Panel->AddSlot().AutoHeight()[GrowGroup];

	// Always shown, rather than behind an output mode. A crack normally wants to mask, cut and
	// catch light at the same time, and the three weights say how much of each -- Height and
	// Normal here, Weight in Blend below. Each is its own off switch at zero, so nothing has to
	// be chosen between.
	TSharedRef<SVerticalBox> ReliefGroup = SNew(SVerticalBox);
	AddSliderRow(ReliefGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpRelief", "Relief")));
	AddSliderRow(ReliefGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqReliefDepth", "Height"), &FMixtormatCraquelure::ReliefDepth, 0.0, 0.5, 0.04, 0.001,
			LOCTEXT("CraqReliefDepthHint", "How deep the crack cuts into the composited height, at the crack itself. The groove is a cone on the distance to the crack -- the solution of the eikonal equation, so its wall has one constant slope -- and it is subtracted from the surface under a minimum, so this filter can only ever lower the height. 0 leaves the height untouched and skips the pass.")),
		Slider(LOCTEXT("CraqReliefNormal", "Normal"), &FMixtormatCraquelure::ReliefNormalStrength, 0.0, 64.0, 8.0, 0.05,
			LOCTEXT("CraqReliefNormalHint", "Gain on the normal derived from the groove wall. Same meaning and normalisation as the erosion and chipping controls. Independent of Height, so a crack can catch light without displacing, or displace without being relit."))));
	AddSliderRow(ReliefGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqReliefWidth", "Groove"), &FMixtormatCraquelure::ReliefWidth, 0.002, 1.0, 0.08, 0.001,
			LOCTEXT("CraqReliefWidthHint", "Half-width of the groove, in cell units like Width. Separate from Width because they describe different things: Width is the hairline the mask draws, this is the mouth of the dish around it, and a fine dark crack usually sits in a much wider depression. Past about half a cell the dishes of neighbouring cracks overlap everywhere and the surface just sinks by a constant, so this is the one relief control that stops reading as cracks if you push it.")),
		Slider(LOCTEXT("CraqReliefProfile", "Profile"), &FMixtormatCraquelure::ReliefProfile, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("CraqReliefProfileHint", "Shape of the groove wall. 1 is the straight cone the distance field gives directly, which is the constant-slope fracture case; below 1 flares it to a dish, above draws it into a narrow V with a broad flat shoulder."))));
	Panel->AddSlot().AutoHeight()[ReliefGroup];

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWidth", "Width"), &FMixtormatCraquelure::Width, 0.002, 0.5, 0.04, 0.001,
			LOCTEXT("CraqWidthHint", "In cell units, so it means the same thing at any cell count. Built on distance to the cell boundary rather than the difference of the two nearest feature points, which is what holds the width even instead of going heavy in large cells and hairline in small ones. The floor is a hairline rather than zero: at zero the falloff collapses to a hard one-pixel line, which aliases.")),
		Slider(LOCTEXT("CraqVariation", "Variation"), &FMixtormatCraquelure::Variation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqVariationHint", "Thins individual cracks, so the network reads as breaks that opened at different times rather than a uniform lattice. Keyed on the wall between two cells rather than on either cell, so a crack varies as one thing instead of splitting down its centre."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpWarp", "Warp")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWarp", "Amount"), &FMixtormatCraquelure::Warp, 0.0, 2.0, 0.0, 0.01,
			LOCTEXT("CraqWarpHint", "Displaces the lattice so cracks wander instead of following a visibly regular network. Driven by periodic gradient noise rather than a second cellular field: a cellular displacement jumps wherever the nearest feature point changes and tears the network along every cell boundary, while gradient noise is continuous. It wraps on its own period, so the result still tiles.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqWarpPeriod", "Scale"), Craq, &FMixtormatCraquelure::WarpPeriod, 1.0, 32.0, 4,
			LOCTEXT("CraqWarpPeriodHint", "Cells in the field doing the displacing. Below the crack cell count it bends whole regions; above it, it roughens individual cracks."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqSeed", "Seed"), Craq, &FMixtormatCraquelure::Seed, 0.0, 64.0, 1,
			LOCTEXT("CraqSeedHint", "Reshuffles the crack network without changing its scale or density.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqWarpSeed", "Warp Seed"), Craq, &FMixtormatCraquelure::WarpSeed, 0.0, 64.0, 7,
			LOCTEXT("CraqWarpSeedHint", "Reshuffles the displacement independently, so a network can be re-wandered without moving its cells."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpBlend", "Blend")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("CraqBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				return C ? MixtormatUI::MaskBlendModeText(C->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildCraquelureBlendModeMenu)),
		LOCTEXT("CraqBlendModeHint", "How the crack network combines with the mask accumulated above it in this layer.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWeight", "Weight"), &FMixtormatCraquelure::Weight, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("CraqWeightHint", "How far the blend is taken. 0 is the off switch for this node.")),
		Slider(LOCTEXT("CraqContrast", "Contrast"), &FMixtormatCraquelure::Contrast, 0.0, 10.0, 1.0, 0.01,
			LOCTEXT("CraqContrastHint", "Scales the crack signal about its midpoint before it is blended."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqBalance", "Balance"), &FMixtormatCraquelure::Balance, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("CraqBalanceHint", "Pushes the network toward thinner or thicker without moving its midpoint.")),
		Slider(LOCTEXT("CraqOffset", "Offset"), &FMixtormatCraquelure::Offset, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqOffsetHint", "Lifts or lowers the whole signal after contrast."))));

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("CraqInvert", "Invert"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				return C && C->bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatCraquelure* C = GetSelectedCraquelure())
				{
					C->bInvert = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("CraqInvertHint", "Cracks are white by default. Inverted, the cells are the mask and the cracks cut it.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCraquelure() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CraquelureHeading", "CRAQUELURE"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildMaskRotationMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatUVRotation Rotation : GMixtormatUVRotations)
	{
		Menu.Item(
			MixtormatUI::UVRotationText(Rotation),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Rotation]()
			{
				if (FMixtormatMaskLayer* M = GetSelectedLayerMask())
				{
					M->Rotation = Rotation;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Rotation]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M && M->Rotation == Rotation;
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

TSharedRef<SWidget> SMixtormat::BuildErosionCurvatureModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatErosionCurvatureMode Modes[] = {
		EMixtormatErosionCurvatureMode::Mean,
		EMixtormatErosionCurvatureMode::Valley,
		EMixtormatErosionCurvatureMode::Ridge
	};
	for (const EMixtormatErosionCurvatureMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::ErosionCurvatureModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatLayerEffect* E = GetSelectedErosion())
				{
					E->ErosionCurvatureMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatLayerEffect* E = GetSelectedErosion();
				return E && E->ErosionCurvatureMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildErosionControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	const auto Ero = [this]() { return GetSelectedErosion(); };

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpFilter", "Filter")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroAmount", "Amount"), &FMixtormatLayerEffect::ErosionAmount, 0.0, 1.0, 1.0, 0.01),
		MakeErosionSlider(LOCTEXT("EroStrength", "Depth"), &FMixtormatLayerEffect::ErosionStrength, 0.0, 0.5, 0.08, 0.005,
			LOCTEXT("EroStrengthHint", "Total height removed by the largest erosion band. The result remains subtractive."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpField", "Gullies")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSliderInt(LOCTEXT("EroOctaves", "Octaves"), &FMixtormatLayerEffect::ErosionOctaves, 1.0, 12.0, 5,
			LOCTEXT("EroOctavesHint", "Stacked detail bands evaluated together. Later bands branch from the straightened slope created by earlier gullies.")),
		MakeErosionSliderInt(LOCTEXT("EroPeriod", "Period"), &FMixtormatLayerEffect::ErosionPeriod, 1.0, 128.0, 12,
			LOCTEXT("EroPeriodHint", "Cells across one UV repeat at the coarsest octave. Integral periods preserve seamless tiling."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroGain", "Gain"), &FMixtormatLayerEffect::ErosionGain, 0.0, 1.0, 0.5, 0.01),
		MakeErosionSlider(LOCTEXT("EroDetail", "Detail"), &FMixtormatLayerEffect::ErosionDetail, 0.1, 4.0, 1.5, 0.05,
			LOCTEXT("EroDetailHint", "Controls how strongly existing ridges and creases suppress finer gullies."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroGullyWeight", "Gully Weight"), &FMixtormatLayerEffect::ErosionGullyWeight, 0.0, 1.5, 0.65, 0.01),
		MakeErosionSlider(LOCTEXT("EroNormalization", "Normalization"), &FMixtormatLayerEffect::ErosionNormalization, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("EroNormalizationHint", "Restores weak blended stripes without fully normalizing cancellation points into spikes."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpSlope", "Directional Max Slope")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSliderInt(LOCTEXT("EroSlopeRadius", "Radius"), &FMixtormatLayerEffect::ErosionSlopeRadius, 1.0, 32.0, 2,
			LOCTEXT("EroSlopeRadiusHint", "Search radius for the steepest downhill direction across sixteen rays. Small values retain brick and stone edges.")),
		MakeErosionSlider(LOCTEXT("EroSlopeBlur", "Blur"), &FMixtormatLayerEffect::ErosionSlopeBlur, 0.0, 16.0, 0.0, 0.05,
			LOCTEXT("EroSlopeBlurHint", "Optional guidance blur for noisy stone. Leave at zero for masonry and sharp joints."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroAssumedSlope", "Assumed"), &FMixtormatLayerEffect::ErosionAssumedSlope, 0.0, 4.0, 0.7, 0.01),
		MakeErosionSlider(LOCTEXT("EroAssumedSlopeMix", "Assume Mix"), &FMixtormatLayerEffect::ErosionAssumedSlopeAmount, 0.0, 1.0, 1.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroSlopeOnset", "Surface Onset"), &FMixtormatLayerEffect::ErosionSlopeOnset, 0.0, 8.0, 1.0, 0.05),
		MakeErosionSlider(LOCTEXT("EroFeatureOnset", "Feature Onset"), &FMixtormatLayerEffect::ErosionFeatureOnset, 0.0, 8.0, 1.25, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpRounding", "Rounding")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroRidgeRound", "Ridges"), &FMixtormatLayerEffect::ErosionRidgeRounding, 0.0, 1.0, 0.10, 0.01),
		MakeErosionSlider(LOCTEXT("EroCreaseRound", "Creases"), &FMixtormatLayerEffect::ErosionCreaseRounding, 0.0, 1.0, 0.0, 0.01)));

	// Cavity is an offset and a window, not a bias and a contrast. Contrast around a fixed
	// centre could only widen or narrow the transition; it could not say which curvatures
	// count as a channel, which is the decision this gate is actually making.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpCavity", "Cavity")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("EroCurvatureMode", "Curvature"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedErosion();
				return E ? MixtormatUI::ErosionCurvatureModeText(E->ErosionCurvatureMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildErosionCurvatureModeMenu)),
		LOCTEXT("EroCurvatureModeHint", "Mean is the trace of the Hessian. Valley is its larger principal curvature, which reads an elongated channel at full depth where the trace halves it and a saddle cancels it. Ridge is the negated smaller one, which finds crests rather than the same signal inverted.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroCavityInfluence", "Influence"), &FMixtormatLayerEffect::ErosionCavityInfluence, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("EroCavityInfluenceHint", "How much the gate participates. 0 opens it fully and is the identity, 1 applies it as measured, and negative inverts it onto the opposite curvature. Nothing clamps it, so a typed value past 1 still extrapolates and expands the gate's contrast -- the slider just stops where the control stays predictable.")),
		MakeErosionSlider(LOCTEXT("EroCavityOffset", "Offset"), &FMixtormatLayerEffect::ErosionCavityOffset, -4.0, 4.0, 0.0, 0.01,
			LOCTEXT("EroCavityOffsetHint", "Added to the curvature before the remap, so the window moves without respecifying both ends."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroCavityRemapMin", "In Low"), &FMixtormatLayerEffect::ErosionCavityRemapMin, -4.0, 4.0, 0.0, 0.01,
			LOCTEXT("EroCavityRemapHint", "Curvature mapped to 0..1. Setting Low above High inverts the gate.")),
		MakeErosionSlider(LOCTEXT("EroCavityRemapMax", "In High"), &FMixtormatLayerEffect::ErosionCavityRemapMax, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("EroCavityRemapHint", "Curvature mapped to 0..1. Setting Low above High inverts the gate."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpHeight", "Height")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroHeightInfluence", "Influence"), &FMixtormatLayerEffect::ErosionHeightInfluence, -1.0, 1.0, 0.0, 0.05),
		MakeErosionSlider(LOCTEXT("EroHeightScale", "Peak / Valley"), &FMixtormatLayerEffect::ErosionHeightScale, 0.0, 8.0, 1.0, 0.05,
			LOCTEXT("EroHeightScaleHint", "Shapes the fade target: low material becomes a crease and high material becomes a ridge."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpMask", "Placement Mask")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
	[
		SNew(SBox).HeightOverride(18.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("EroMaskSlot", "Mask"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("EroMaskSlotHint", "Mask used to place erosion. Unset uses the layer's accumulated mask children."))
				.ButtonContent()
				[
					SNew(SBox).MinDesiredWidth(120.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const FMixtormatLayerEffect* E = GetSelectedErosion();
							if (!E)
							{
								return LOCTEXT("EroMaskNone", "Child Mask");
							}
							if (!E->ErosionMask.IsNull())
							{
								return FText::FromString(E->ErosionMask.ToSoftObjectPath().GetAssetName());
							}
							if (!E->ErosionMaskTexture.IsNull())
							{
								return FText::FromString(E->ErosionMaskTexture.ToSoftObjectPath().GetAssetName());
							}
							return LOCTEXT("EroMaskNone", "Child Mask");
						})
					]
				]
				.OnGetMenuContent_Lambda([this]()
				{
					return SNew(SBox)
						.WidthOverride(MixtormatTokens::MaskPickerWidth)
						.Padding(MixtormatTokens::TileGap)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().MaxHeight(420.0f)
							[
								SNew(SScrollBox) + SScrollBox::Slot()
								[
									BuildMaskGallery([this](const FSoftObjectPath& Path)
									{
										FMixtormatLayerEffect* E = GetSelectedErosion();
										if (!E)
										{
											return;
										}
										UObject* MaskObject = Path.TryLoad();
										if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
										{
											E->ErosionMask = TSoftObjectPtr<UMixtormatMask>(Path);
											E->ErosionMaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
										}
										else if (Cast<UTexture2D>(MaskObject))
										{
											E->ErosionMask.Reset();
											E->ErosionMaskTexture = TSoftObjectPtr<UTexture2D>(Path);
										}
										else
										{
											return;
										}
										RefreshLayeredPreview();
									})
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							.Padding(0.0f, MixtormatTokens::TileGap, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
								.Text(LOCTEXT("EroMaskClear", "Use the layer's child mask"))
								.OnClicked_Lambda([this]()
								{
									if (FMixtormatLayerEffect* E = GetSelectedErosion())
									{
										E->ErosionMask.Reset();
										E->ErosionMaskTexture.Reset();
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
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSliderInt(LOCTEXT("EroMaskTiling", "Tiling"), &FMixtormatLayerEffect::ErosionMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("EroInvertMask", "Invert"), Ero, &FMixtormatLayerEffect::bErosionInvertMask,
			LOCTEXT("EroInvertMaskHint", "Inverts the selected placement mask, or the layer child mask when no mask is selected."))));

	// What the carve exposes. Applied over the base colour and roughness the layer already
	// composited, in proportion to how deeply each pixel was cut.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpShade", "Exposed")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(LOCTEXT("EroColorLabel", "Color"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("OpenErosionColorPicker", "The color the carve exposes. Blended toward rather than multiplied, unlike a stain, so it can reach a color lighter than what it replaced."))
			.OnClicked_Lambda([this]()
			{
				return OpenErosionColorPicker(SelectedLayerIndex, SelectedEffectIndex);
			})
			[
				SNew(SColorBlock)
				.Color_Lambda([this]()
				{
					const FMixtormatLayerEffect* E = GetSelectedErosion();
					return E ? E->ErosionColor : FLinearColor::White;
				})
				.Size(FVector2D(76.0f, 16.0f))
			]
		]
	];
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroColorAmount", "Color"), &FMixtormatLayerEffect::ErosionColorAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("EroColorAmountHint", "How far the carve blends toward the exposed color. 0 leaves the composited base color alone and skips the pass entirely.")),
		MakeErosionSlider(LOCTEXT("EroRoughAmount", "Roughness"), &FMixtormatLayerEffect::ErosionRoughnessAmount, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("EroRoughAmountHint", "Signed offset on the composited roughness where the carve bit, positive toward rough."))));
	AddErosionSlider(Panel, LOCTEXT("EroCarveDepth", "Full At Depth"), &FMixtormatLayerEffect::ErosionCarveDepth, 0.001, 1.0, 0.05, 0.001,
		LOCTEXT("EroCarveDepthHint", "The carve depth that reads as fully eroded, in height units. Coverage is the carve divided by this, so it is what stops both amounts being all or nothing."));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpOutput", "Output")));
	AddErosionSlider(Panel, LOCTEXT("EroNormalStrength", "Normal Strength"), &FMixtormatLayerEffect::ErosionNormalStrength, 0.0, 32.0, 8.0, 0.05);

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
		LOCTEXT("GenRidgeWeight", "Ridge / Drainage"), Gen, &FMixtormatGeneratedMask::RidgeWeight, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("GenRidgeWeightHint", "Crest and drainage lines from an erosion filter on a layer below. Zero everywhere if nothing below erodes.")));
	AddSliderRow(Panel, MixtormatRow::MakeHairline());

	// The only generated signal here; everything else is derived from the surface below, which
	// is why this one carries its own scale and seed and works on the bottom layer.

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

	// Source placement. Every transform here maps the unit square onto itself, which is the
	// constraint: the read is wrapped in a frac(), so anything else seams at the repeat.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpPlacement", "Placement")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMaskLayer>(
			LOCTEXT("MaskTilingXLabel", "Tiling X"), Mask, &FMixtormatMaskLayer::TilingX, 1.0, 16.0, 1,
			LOCTEXT("MaskTilingHint", "Repeats across the axis. Integer only: a fractional scale lands mid-cell at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatMaskLayer>(
			LOCTEXT("MaskTilingYLabel", "Tiling Y"), Mask, &FMixtormatMaskLayer::TilingY, 1.0, 16.0, 1,
			LOCTEXT("MaskTilingHint", "Repeats across the axis. Integer only: a fractional scale lands mid-cell at the UV wrap and seams."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskUVOffsetXLabel", "Offset X"), Mask, &FMixtormatMaskLayer::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("MaskUVOffsetHint", "Shifts where the mask is read from. Safe at any value: translating a periodic function leaves it periodic. Unrelated to Offset under Shaping, which lifts the mask value instead.")),
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskUVOffsetYLabel", "Offset Y"), Mask, &FMixtormatMaskLayer::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("MaskUVOffsetHint", "Shifts where the mask is read from. Safe at any value: translating a periodic function leaves it periodic. Unrelated to Offset under Shaping, which lifts the mask value instead."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatMaskLayer>(
			LOCTEXT("MaskFlipULabel", "Flip U"), Mask, &FMixtormatMaskLayer::bFlipU),
		MakeMemberToggle<FMixtormatMaskLayer>(
			LOCTEXT("MaskFlipVLabel", "Flip V"), Mask, &FMixtormatMaskLayer::bFlipV)));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("MaskRotationLabel", "Rotate"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M ? MixtormatUI::UVRotationText(M->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskRotationMenu)),
		LOCTEXT("MaskRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams; 90 degree steps are permutations of the unit square, so they stay tileable. Applied before the tiling, so the mask turns and the lattice repeats the turned result.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpShape", "Shaping")));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatMaskLayer>(
		LOCTEXT("MaskInvertLabel", "Invert"), Mask, &FMixtormatMaskLayer::bInvert));
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
			// A Filter is procedural and has no asset-authored maps or ranges to show here.
			// Tested by class rather than by naming each one: this read !GetSelectedErosion()
			// while Grade was already a Filter, so the asset panel was showing on grades too.
			const FMixtormatLayerEffect* Selected = GetSelectedLayerEffect();
			if (!Selected)
			{
				return EVisibility::Collapsed;
			}
			const bool bProceduralFilter =
				Selected->Effect.IsNull()
				&& MixtormatEffectClassOf(Selected->ProceduralType) == EMixtormatEffectClass::Filter;
			return bProceduralFilter ? EVisibility::Collapsed : EVisibility::Visible;
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
	AddFloatControl(StainPanel, LOCTEXT("StainGravity", "Gravity"), &FMixtormatLayerEffect::StainGravity, -1.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightBias", "Valley / Ridge Bias"), &FMixtormatLayerEffect::StainHeightBias, -1.0f, 1.0f, 0.01f, -1.0f);
	AddFloatControl(StainPanel, LOCTEXT("StainHeightContrast", "Height Contrast"), &FMixtormatLayerEffect::StainHeightContrast, 0.01f, 8.0f, 0.05f, 1.0f);

	// The same orientation field the erosion filter uses, over the surface the stain runs
	// down. Height Warp traces the source uphill on a two-texel gradient, which turns
	// wherever the height has grain and frays the run; this makes it follow structure the
	// surface actually has. Amount 0 skips the field entirely, so a stain that does not ask
	// for flow costs nothing.
	AddSliderRow(StainPanel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpFlow", "Flow")));
	AddFloatControl(StainPanel, LOCTEXT("StainFlowAmount", "Flow Amount"), &FMixtormatLayerEffect::StainFlowAmount, 0.0f, 1.0f, 0.01f, 0.0f);
	AddSliderRow(StainPanel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainFlowRadius", "Radius"),
			[this]() { return GetSelectedLayerEffect(); },
			&FMixtormatLayerEffect::StainFlowRadius, 1.0, 64.0, 4,
			LOCTEXT("StainFlowRadiusHint", "Pixel radius of the gradient the orientation field is built from: the feature size the run is asked to follow.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainFlowSmooth", "Smooth"),
			[this]() { return GetSelectedLayerEffect(); },
			&FMixtormatLayerEffect::StainFlowSmoothing, 1.0, 16.0, 3,
			LOCTEXT("StainFlowSmoothHint", "How many times the orientation field is smoothed. This is what decides how far a run holds its line. 1 is the minimum: an unsmoothed field reports full coherency over what is still per-pixel noise."))));

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
						// Thumbnail, name, source, badge -- the same four fields in the same order as
						// the row in the stack that selected it, so moving from one to the other
						// re-reads nothing.
						SNew(SBox)
						.HeightOverride(MixtormatTokens::LayerRowHeight)
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
								.WidthOverride(MixtormatTokens::LayerThumbnailSize)
								.HeightOverride(MixtormatTokens::LayerThumbnailSize)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerNameInset, 0.0f, 0.0f, 0.0f)
							[
								SAssignNew(SelectedSurfaceText, STextBlock)
								.Text(LOCTEXT("NoSelectedSurface", "No layer selected"))
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
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
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					.Visibility_Lambda([this]()
					{
						return GetSelectedLayerEffect()
							|| GetSelectedGeneratedMask()
							|| GetSelectedLayerMask()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SScrollBox::Slot()[BuildEffectInspectorControls()]
					+ SScrollBox::Slot()[BuildProceduralPeelControls()]
					+ SScrollBox::Slot()[BuildErosionControls()]
					+ SScrollBox::Slot()[BuildGradeControls()]
					+ SScrollBox::Slot()[BuildChippingControls()]
					+ SScrollBox::Slot()[BuildGeneratedMaskControls()]
					+ SScrollBox::Slot()[BuildLayerMaskControls()]
					+ SScrollBox::Slot()[BuildCraquelureControls()]
					+ SScrollBox::Slot()[BuildColorIdControls()]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					// The scroll box has to sit outside the group, not inside it. An expandable
					// area sizes its content to whatever that content asks for, so a scroll box
					// within one is handed unbounded height and never scrolls -- the layer
					// inspector simply ran off the bottom of the panel.
					SNew(SScrollBox)
					.Visibility_Lambda([this]()
					{
						return GetSelectedLayerEffect()
							|| GetSelectedGeneratedMask()
							|| GetSelectedLayerMask()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							? EVisibility::Collapsed : EVisibility::Visible;
					})
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
						// No "Normal Detail Only" checkbox: DETAIL is one of the four cells in
						// COMPOSITION, which writes the same ChannelMode. Two controls for one field
						// meant the segment could say BLEND while the box said the layer was detail.
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
								// UV placement on top of Tiling. Integer scale and no rotation,
								// both because the compositor wraps every source read in a frac()
								// and anything else seams. Offset and flip are safe at any value.
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MixtormatRow::MakePair(
										MakeMemberSliderInt<FMixtormatLayer>(
											LOCTEXT("UVScaleXLabel", "Scale X"), LayerForRows(), &FMixtormatLayer::UVScaleX, 1.0, 16.0, 1,
											LOCTEXT("UVScaleHint", "Per-axis multiplier on Tiling. Integer only: a fractional scale lands mid-cell at the UV wrap and seams.")),
										MakeMemberSliderInt<FMixtormatLayer>(
											LOCTEXT("UVScaleYLabel", "Scale Y"), LayerForRows(), &FMixtormatLayer::UVScaleY, 1.0, 16.0, 1,
											LOCTEXT("UVScaleHint", "Per-axis multiplier on Tiling. Integer only: a fractional scale lands mid-cell at the UV wrap and seams.")))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MixtormatRow::MakePair(
										MakeMemberSlider<FMixtormatLayer>(
											LOCTEXT("UVOffsetXLabel", "Offset X"), LayerForRows(), &FMixtormatLayer::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
											LOCTEXT("UVOffsetHint", "Shifts where the source is read from. Safe at any value: translating a periodic function leaves it periodic.")),
										MakeMemberSlider<FMixtormatLayer>(
											LOCTEXT("UVOffsetYLabel", "Offset Y"), LayerForRows(), &FMixtormatLayer::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
											LOCTEXT("UVOffsetHint", "Shifts where the source is read from. Safe at any value: translating a periodic function leaves it periodic.")))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MixtormatRow::Make(
										LOCTEXT("UVRotationLabel", "Rotate"),
										MixtormatRow::MakeChip(
											TAttribute<FText>::CreateLambda([this]()
											{
												return WorkingLayers.IsValidIndex(SelectedLayerIndex)
													? MixtormatUI::UVRotationText(WorkingLayers[SelectedLayerIndex].Rotation)
													: FText::GetEmpty();
											}),
											FOnGetContent::CreateSP(this, &SMixtormat::BuildLayerRotationMenu)),
										LOCTEXT("UVRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams; 90 degree steps are permutations of the unit square, so they stay tileable."))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MixtormatRow::Make(
										LOCTEXT("UVFlipULabel", "Flip U"),
										MixtormatRow::MakeCheckbox(
											TAttribute<ECheckBoxState>::CreateLambda([this]()
											{
												const FMixtormatLayer* L = WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
												return L && L->bFlipU ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
											}),
											FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
											{
												if (FMixtormatLayer* L = WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr)
												{
													L->bFlipU = State == ECheckBoxState::Checked;
													RefreshLayeredPreview();
												}
											})))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
								[
									MixtormatRow::Make(
										LOCTEXT("UVFlipVLabel", "Flip V"),
										MixtormatRow::MakeCheckbox(
											TAttribute<ECheckBoxState>::CreateLambda([this]()
											{
												const FMixtormatLayer* L = WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
												return L && L->bFlipV ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
											}),
											FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
											{
												if (FMixtormatLayer* L = WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr)
												{
													L->bFlipV = State == ECheckBoxState::Checked;
													RefreshLayeredPreview();
												}
											})))
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
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
