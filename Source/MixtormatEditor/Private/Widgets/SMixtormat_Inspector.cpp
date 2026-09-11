#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Atoms/SMixtormatToggle.h"
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

	FText FlowWarpBlendModeText(const EMixtormatFlowWarpBlendMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatFlowWarpBlendMode::MinHeight:
			return LOCTEXT("FlowWarpBlendMin", "Min Height");
		case EMixtormatFlowWarpBlendMode::MaxHeight:
			return LOCTEXT("FlowWarpBlendMax", "Max Height");
		default:
			return LOCTEXT("FlowWarpBlendReplace", "Replace");
		}
	}
}

TSharedRef<SWidget> SMixtormat::BuildProceduralPeelControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpSource", "Source")));

	// The peel's own seed mask. Kept as a bespoke row because it picks an asset, not a value.
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
	[
		SNew(SBox).HeightOverride(MixtormatTokens::RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("PPeelMaskSlot", "Peel Mask"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMixtormatChip)
				.ToolTip(LOCTEXT("PPeelMaskSlotHint", "The mask that seeds this peel. Independent of the layer's mask children; unset falls back to the accumulated child mask."))
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
				.OnGetMenuContent_Lambda([this]()
				{
					// A grid, not a list. Masks are images, and picking "Grunge_Fine" over
					// "Grunge_Coarse" by filename meant assigning one, looking at the viewport
					// and coming back. The popover is wider than the 300px inspector on purpose:
					// a menu is its own window and is not clipped by the panel that opened it.
					TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
						.UseAllottedSize(true)
						.InnerSlotPadding(FVector2D(
							MixtormatTokens::MaskGalleryTileGap,
							MixtormatTokens::MaskGalleryTileGap));

					for (const FMixtormatMaskEntry& Entry : FMixtormatRegistry::GetMasks())
					{
						const FSoftObjectPath Path = Entry.AssetPath;
						Grid->AddSlot()
						[
							SNew(SMixtormatTile)
							.TileSize_Lambda([this]() { return MaskGalleryTileSize; })
							.DisplayName(Entry.DisplayName)
							.ThumbnailAsset(Entry.ThumbnailAsset)
							.ThumbnailPool(ThumbnailPool)
							.ThumbnailResolution(FMath::RoundToInt(MixtormatTokens::MaskGalleryTileMaximum))
							.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
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
							.MaxHeight(MixtormatTokens::InspectorMaskGalleryMaxHeight)
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
	// 139px. Anything longer would clip, which is why Adhesion's weights below are not paired.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelMaskTiling", "Tiling"), &FMixtormatLayerEffect::PeelMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("PPeelMaskInv", "Invert"),
			[this]() { return GetSelectedProceduralPeel(); },
			&FMixtormatLayerEffect::bPeelMaskInvert)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpAdhesion", "Adhesion")));
	AddPeelSlider(Panel, LOCTEXT("PPeelMaskW", "Mask Gain"), &FMixtormatLayerEffect::PeelSeedMaskWeight, 0.0, 4.0, 0.0, 0.01,
		LOCTEXT("PPeelMaskWHint", "Scales the mask before the threshold. At 0 nothing crosses it and there is no peel at all, whichever mask is chosen."));
	AddPeelSlider(Panel, LOCTEXT("PPeelAdhesion", "Adhesion"), &FMixtormatLayerEffect::PeelSeedThreshold, 0.0, 1.0, 0.62, 0.01,
		LOCTEXT("PPeelAdhesionHint", "How easily the peel nucleates. Lower values make peeling start more readily; higher values require stronger surface features."));

	AddPeelSlider(Panel, LOCTEXT("PPeelCurvW", "Convexity"), &FMixtormatLayerEffect::PeelSeedCurvatureWeight, -2.0, 2.0, 0.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelCurvBias", "Convex Bias"), &FMixtormatLayerEffect::PeelSeedCurvatureBias, 0.0, 1.0, 1.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelAOW", "Occlusion"), &FMixtormatLayerEffect::PeelSeedAOWeight, -2.0, 2.0, 0.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelHeightW", "Height"), &FMixtormatLayerEffect::PeelSeedHeightWeight, -2.0, 2.0, 0.0, 0.01);

	AddSliderRow(Panel, MixtormatRow::MakeHairline());
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatLayerEffect>(
		LOCTEXT("PPeelNormalize", "Normalize Weights"),
		[this]() { return GetSelectedProceduralPeel(); },
		&FMixtormatLayerEffect::bPeelNormalizeSeedWeights));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpPropagation", "Propagation")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelCurvRadius", "Radius"), &FMixtormatLayerEffect::PeelCurvatureRadius, 1.0, 64.0, 2),
		MakePeelSlider(LOCTEXT("PPeelPropagation", "Propagation"), &FMixtormatLayerEffect::PeelGrowthStrength, 0.05, 32.0, 1.0, 0.05)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpShape", "Peel Shape")));
	AddPeelSlider(Panel, LOCTEXT("PPeelLiftVar", "Lift Variation"), &FMixtormatLayerEffect::PeelLiftVariation, 0.0, 1.0, 0.6, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelSizeVar", "Size Variation"), &FMixtormatLayerEffect::PeelSizeVariation, 0.0, 1.0, 0.5, 0.01,
		LOCTEXT("PPeelSizeVarHint", "Per-cell speed factor. Set this to 0 as well as the adhesion weights to check that the field dilates uniformly."));
	AddPeelSliderInt(Panel, LOCTEXT("PPeelDamageScale", "Damage Scale"), &FMixtormatLayerEffect::PeelClusterPeriod, 1.0, 128.0, 4,
		LOCTEXT("PPeelDamageScaleHint", "Cell count for per-flake variation. One random value per cell, so adjacent flakes differ in size and lift."));
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

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpRelief", "Relief")));
	AddPeelSlider(Panel, LOCTEXT("PPeelSharp", "Edge Sharpness"), &FMixtormatLayerEffect::PeelEdgeSharpness, 0.0, 4.0, 1.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelAO", "Contact AO"), &FMixtormatLayerEffect::PeelAOStrength, 0.0, 1.0, 0.8, 0.01);
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

TSharedRef<SWidget> SMixtormat::BuildStainModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatStainMode Modes[] = {
		EMixtormatStainMode::Wet,
		EMixtormatStainMode::Deposit
	};
	for (const EMixtormatStainMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::StainModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatLayerEffect* E = GetSelectedStain())
				{
					E->StainMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatLayerEffect* E = GetSelectedStain();
				return E && E->StainMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildStainControls()
{
	const auto Stain = [this]() { return GetSelectedStain(); };
	const auto Slider = [this, Stain](
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(
			Label, Stain, Member, Min, Max, Default, Snap, Hint);
	};

	const auto MakeMaskRow = [this](
		const FText& Label,
		const FText& Fallback,
		const FText& Hint,
		TSoftObjectPtr<UMixtormatMask> FMixtormatLayerEffect::* MaskMember,
		TSoftObjectPtr<UTexture2D> FMixtormatLayerEffect::* TextureMember)
	{
		return SNew(SBox)
			.HeightOverride(MixtormatTokens::RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatChip)
					.ToolTip(Hint)
					.Text_Lambda([this, MaskMember, TextureMember, Fallback]()
					{
						const FMixtormatLayerEffect* E = GetSelectedStain();
						if (!E)
						{
							return Fallback;
						}
						if (!(E->*MaskMember).IsNull())
						{
							return FText::FromString((E->*MaskMember).ToSoftObjectPath().GetAssetName());
						}
						if (!(E->*TextureMember).IsNull())
						{
							return FText::FromString((E->*TextureMember).ToSoftObjectPath().GetAssetName());
						}
						return Fallback;
					})
					.OnGetMenuContent_Lambda([this, MaskMember, TextureMember, Fallback]()
					{
						return SNew(SBox)
							.WidthOverride(MixtormatTokens::MaskPickerWidth)
							.Padding(MixtormatTokens::TileGap)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								.MaxHeight(MixtormatTokens::MaskPickerMaxHeight)
								[
									SNew(SScrollBox) + SScrollBox::Slot()
									[
										BuildMaskGallery([this, MaskMember, TextureMember](const FSoftObjectPath& Path)
										{
											FMixtormatLayerEffect* E = GetSelectedStain();
											if (!E)
											{
												return;
											}
											UObject* MaskObject = Path.TryLoad();
											if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
											{
												E->*MaskMember = TSoftObjectPtr<UMixtormatMask>(Path);
												E->*TextureMember = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
											}
											else if (Cast<UTexture2D>(MaskObject))
											{
												(E->*MaskMember).Reset();
												E->*TextureMember = TSoftObjectPtr<UTexture2D>(Path);
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
									.Text(Fallback)
									.OnClicked_Lambda([this, MaskMember, TextureMember]()
									{
										if (FMixtormatLayerEffect* E = GetSelectedStain())
										{
											(E->*MaskMember).Reset();
											(E->*TextureMember).Reset();
											RefreshLayeredPreview();
										}
										return FReply::Handled();
									})
								]
							];
					})
				]
			];
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("StainMode", "Output Mask"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* E = GetSelectedStain();
				return E ? MixtormatUI::StainModeText(E->StainMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildStainModeMenu)),
		LOCTEXT("StainModeHint", "Wet outputs absorbed liquid. Deposit outputs dried dirt and mineral residue. Both come from the same transport solve.")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainStrength", "Amount"), &FMixtormatLayerEffect::Strength, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("StainStrengthHint", "Final blend of the selected wet or deposit mask. At 0 the transport solve is skipped.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpSource", "Source")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
	[
		MakeMaskRow(
			LOCTEXT("StainSourceMask", "Liquid Mask"),
			LOCTEXT("StainSourceFallback", "Child / Auto"),
			LOCTEXT("StainSourceMaskHint", "Where liquid enters the solve. Unset uses preceding child masks; with none, concavity and convexity generate the source."),
			&FMixtormatLayerEffect::StainSourceMask,
			&FMixtormatLayerEffect::StainSourceMaskTexture)
	];
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainSourceTiling", "Tiling"), Stain,
			&FMixtormatLayerEffect::StainSourceMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("StainSourceInvert", "Invert"), Stain,
			&FMixtormatLayerEffect::bStainSourceMaskInvert)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainSourceAmount", "Liquid Amount"), &FMixtormatLayerEffect::StainSourceAmount,
		0.0, 1.0, 0.12, 0.005,
		LOCTEXT("StainSourceAmountHint", "Liquid injected from the mask and from enabled curvature sources each iteration.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpDirt", "Dirt / Minerals")));
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
	[
		MakeMaskRow(
			LOCTEXT("StainDirtMask", "Dirt Mask"),
			LOCTEXT("StainDirtFallback", "Use Source Mask"),
			LOCTEXT("StainDirtMaskHint", "Material dissolved and redeposited by the liquid. Unset reuses the liquid source."),
			&FMixtormatLayerEffect::StainDirtMask,
			&FMixtormatLayerEffect::StainDirtMaskTexture)
	];
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainDirtTiling", "Tiling"), Stain,
			&FMixtormatLayerEffect::StainDirtMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("StainDirtInvert", "Invert"), Stain,
			&FMixtormatLayerEffect::bStainDirtMaskInvert)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainDirtAmount", "Dirt Amount"), &FMixtormatLayerEffect::StainDirtAmount,
		0.0, 1.0, 0.35, 0.01,
		LOCTEXT("StainDirtAmountHint", "How much soluble material is available to become a dry deposit.")));

	// Where liquid comes from when nothing authored says. Four weights over the surface
	// accumulated below the layer, so a stain can be driven entirely by geometry -- the Liquid
	// Mask above is an option, not a requirement.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpSurface", "Auto Source")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StainConcavity", "Concavity"), &FMixtormatLayerEffect::StainConcavityWeight, 0.0, 2.0, 0.35, 0.01,
			LOCTEXT("StainConcavityHint", "Adds source in cavities, using the compositor's existing curvature analysis.")),
		Slider(LOCTEXT("StainConvexity", "Convexity"), &FMixtormatLayerEffect::StainConvexityWeight, 0.0, 2.0, 0.15, 0.01,
			LOCTEXT("StainConvexityHint", "Adds source on exposed convex detail, where runoff commonly begins."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StainOcclusion", "Occlusion"), &FMixtormatLayerEffect::StainOcclusionWeight, -2.0, 2.0, 0.0, 0.01,
			LOCTEXT("StainOcclusionHint", "Adds source in occluded areas, read from the accumulated AO beneath the layer. Unlike Concavity this includes contact AO between layers, so it sees shelter the normal alone cannot show.")),
		Slider(LOCTEXT("StainSlope", "Slope"), &FMixtormatLayerEffect::StainSlopeWeight, -2.0, 2.0, 0.0, 0.01,
			LOCTEXT("StainSlopeHint", "Adds source on faces tilted into the flow, which catch liquid, and removes it from faces tilted away, which shed it. A different question from Surface Follow: this is where liquid lands, not where it goes."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StainHeightWeight", "Height"), &FMixtormatLayerEffect::StainHeightWeight, -2.0, 2.0, 0.0, 0.01,
			LOCTEXT("StainHeightWeightHint", "Signed. Positive sources runoff from high ground; negative pools liquid in the low.")),
		Slider(LOCTEXT("StainHeightBias", "Height Bias"), &FMixtormatLayerEffect::StainSourceHeightBias, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("StainHeightBiasHint", "Shifts the height the weight measures against, so the split between high and low lands where the surface actually sits."))));

	// One control where there were two. Roughness and Porosity read the same channel, so on any
	// dielectric they were the same number -- and Porosity duplicated Absorption, which already
	// scales how much the material takes up.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpMaterial", "Material Response")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainSurfaceResponse", "Surface Response"), &FMixtormatLayerEffect::StainSurfaceResponse, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("StainSurfaceResponseHint", "How much of the solve comes from the material beneath the layer. Its roughness drives drag, wandering and lateral spread; its roughness against one-minus-metallic drives absorption, so a rough dielectric drinks and polished or metallic surfaces do not. 1 uses the surface directly, 0 solves against a neutral middle. Absorption is the separate control for how much this particular material takes up.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpTransport", "Transport")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StainGravity", "Gravity"), &FMixtormatLayerEffect::StainGravity, -2.0, 2.0, 1.0, 0.01,
			LOCTEXT("StainGravityHint", "Signed V-axis gravity. Negate it when the material's UVs run the other way.")),
		Slider(LOCTEXT("StainSurfaceFollow", "Surface Follow"), &FMixtormatLayerEffect::StainSurfaceFollow, 0.0, 2.0, 1.0, 0.01,
			LOCTEXT("StainSurfaceFollowHint", "Blends height and normal downhill directions into gravity."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StainSpread", "Spread"), &FMixtormatLayerEffect::StainSpread, 0.0, 1.0, 0.12, 0.01,
			LOCTEXT("StainSpreadHint", "Lateral pressure and rough-surface dispersion.")),
		Slider(LOCTEXT("StainAbsorption", "Absorption"), &FMixtormatLayerEffect::StainAbsorption, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("StainAbsorptionHint", "Rate liquid enters porous material and becomes the wet mask."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainAccumulation", "Accumulation"), &FMixtormatLayerEffect::StainAccumulation,
		0.0, 1.0, 0.5, 0.01,
		LOCTEXT("StainAccumulationHint", "Controls how much liquid remains and pools locally while flow transports the rest.")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StainDrying", "Drying"), &FMixtormatLayerEffect::StainDrying,
		0.0, 1.0, 0.20, 0.01,
		LOCTEXT("StainDryingHint", "Evaporation and residue deposition rate. More drying shifts coverage from wet to deposit.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StainGrpQuality", "Quality")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainIterations", "Iterations"), Stain,
			&FMixtormatLayerEffect::StainIterations, 4.0, 64.0, 20,
			LOCTEXT("StainIterationsHint", "Transport steps, solved at composition resolution. Each step advects two texels, so a run reaches the same distance in half the steps it used to need.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("StainSeed", "Seed"), Stain,
			&FMixtormatLayerEffect::StainSeed, 1.0, 999.0, 1)));

	// No Shade group. Stain resolves a layer mask and shades nothing: the layer it masks supplies
	// colour, roughness, normal and height, which is what a layer is for. The colour multiplier
	// and the roughness target were both this filter arguing with the stack over channels the
	// stack had already resolved.
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedStain() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("StainHeading", "STAIN TRANSPORT"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MakeFeaturePreviewButton(
					EMixtormatDebugPreviewMode::Stain,
					LOCTEXT("PreviewStain", "Preview the resolved stain coverage in unlit dark red and cyan")))
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

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("GradeGrpLevels", "Levels")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeInputMin", "Input Min"), Grade, &FMixtormatLayerEffect::GradeInputMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GradeInputMinHint", "Remapped first, ahead of Brightness/Contrast: the value here maps to Output Min. 0 with Input Max 1 is the identity.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeInputMax", "Input Max"), Grade, &FMixtormatLayerEffect::GradeInputMax, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("GradeInputMaxHint", "The value that maps to Output Max. 1 with Input Min 0 is the identity."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeOutputMin", "Output Min"), Grade, &FMixtormatLayerEffect::GradeOutputMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GradeOutputMinHint", "What Input Min maps to. 0 is the identity.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeOutputMax", "Output Max"), Grade, &FMixtormatLayerEffect::GradeOutputMax, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("GradeOutputMaxHint", "What Input Max maps to. 1 is the identity."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeBiasR", "Bias R"), Grade, &FMixtormatLayerEffect::GradeBiasR, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("GradeBiasRHint", "Added after the levels remap and the linear stage, ahead of the tonemap. 0 is the identity.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("GradeBiasG", "Bias G"), Grade, &FMixtormatLayerEffect::GradeBiasG, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("GradeBiasGHint", "Added after the levels remap and the linear stage, ahead of the tonemap. 0 is the identity."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("GradeBiasB", "Bias B"), Grade, &FMixtormatLayerEffect::GradeBiasB, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("GradeBiasBHint", "Added after the levels remap and the linear stage, ahead of the tonemap. 0 is the identity.")));

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

TSharedRef<SWidget> SMixtormat::BuildFlowWarpBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatFlowWarpBlendMode Modes[] = {
		EMixtormatFlowWarpBlendMode::Replace,
		EMixtormatFlowWarpBlendMode::MinHeight,
		EMixtormatFlowWarpBlendMode::MaxHeight
	};
	for (const EMixtormatFlowWarpBlendMode Mode : Modes)
	{
		Menu.Item(
			FlowWarpBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatLayerEffect* Flow = GetSelectedFlowWarp())
				{
					Flow->FlowWarpBlendMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatLayerEffect* Flow = GetSelectedFlowWarp();
				return Flow && Flow->FlowWarpBlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildFlowWarpControls()
{
	const auto Flow = [this]() { return GetSelectedFlowWarp(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpAmount", "Amount"), Flow,
			&FMixtormatLayerEffect::FlowWarpAmount, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("FlowWarpAmountHint", "Signed displacement. Zero is an exact pass-through; negative values reverse the flow.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpWeight", "Weight"), Flow,
			&FMixtormatLayerEffect::FlowWarpWeight, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("FlowWarpWeightHint", "Blends the coherent warped surface back over the original."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpScale", "Scale"), Flow,
			&FMixtormatLayerEffect::FlowWarpScale, 1.0, 128.0, 8,
			LOCTEXT("FlowWarpScaleHint", "Tileable curl cells across one UV repeat.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpSeed", "Seed"), Flow,
			&FMixtormatLayerEffect::FlowWarpSeed, 0.0, 1024.0, 1,
			LOCTEXT("FlowWarpSeedHint", "Chooses another deterministic curl field."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayerEffect>(
		LOCTEXT("FlowWarpDirection", "Direction"), Flow,
		&FMixtormatLayerEffect::FlowWarpDirection, -180.0, 180.0, 0.0, 1.0,
		LOCTEXT("FlowWarpDirectionHint", "Rotates the curl without rotating its tileable lattice.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("FlowWarpSlopeGroup", "Slope Guidance")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpMaskSlope", "Mask Slope"), Flow,
			&FMixtormatLayerEffect::FlowWarpMaskSlopeInfluence, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("FlowWarpMaskSlopeHint", "Steers downhill along the scoped or accumulated mask.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpHeightSlope", "Height Slope"), Flow,
			&FMixtormatLayerEffect::FlowWarpHeightSlopeInfluence, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("FlowWarpHeightSlopeHint", "Steers downhill along the current composited height."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpKernelX", "Kernel X"), Flow,
			&FMixtormatLayerEffect::FlowWarpDerivativeKernelX, 1.0, 64.0, 2.0, 1.0,
			LOCTEXT("FlowWarpKernelXHint", "Horizontal derivative radius in pixels. Larger values smooth finer slope detail.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("FlowWarpKernelY", "Kernel Y"), Flow,
			&FMixtormatLayerEffect::FlowWarpDerivativeKernelY, 1.0, 64.0, 2.0, 1.0,
			LOCTEXT("FlowWarpKernelYHint", "Vertical derivative radius in pixels. Larger values smooth finer slope detail."))));

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("FlowWarpBlend", "Blend"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatLayerEffect* Selected = GetSelectedFlowWarp();
				return Selected
					? FlowWarpBlendModeText(Selected->FlowWarpBlendMode)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildFlowWarpBlendModeMenu)),
		LOCTEXT("FlowWarpBlendHint", "Replace uses the warped sample. Min or Max Height chooses between original and warped height, then keeps every material channel from that same sample.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedFlowWarp() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("FlowWarpHeading", "FLOW WARP"))
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

	// Chipping contributes no base colour. Its resolved mask only weights surface channels.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ChipGrpOutput", "Output")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("ChipRoughAmount", "Roughness"), &FMixtormatLayerEffect::ChipRoughnessAmount,
		-1.0, 1.0, 0.0, 0.01,
		LOCTEXT("ChipRoughAmountHint", "Signed, mask-weighted offset on composited roughness; positive moves toward rough.")));

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


TSharedRef<SWidget> SMixtormat::BuildWornEdgesControls()
{
	const auto Wear = [this]() { return GetSelectedWornEdges(); };
	const auto Slider = [this, Wear](
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint = FText::GetEmpty())
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(Label, Wear, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("WearGrpShape", "Shape")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("WearRadius", "Radius"), Wear, &FMixtormatLayerEffect::EdgeWearRadius, 1.0, 64.0, 24,
			LOCTEXT("WearRadiusHint", "Maximum directional search reach in output pixels.")),
		Slider(LOCTEXT("WearSlope", "Slope"), &FMixtormatLayerEffect::EdgeWearSlope, 0.0, 4.0, 0.35, 0.01,
			LOCTEXT("WearSlopeHint", "Allowed rise from a lower neighbour before it can pull the current height down."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("WearStrength", "Strength"), &FMixtormatLayerEffect::EdgeWearStrength, 0.0, 1.0, 0.75, 0.01,
			LOCTEXT("WearStrengthHint", "Blend toward the directional MIN target. Zero is an exact pass-through and skips the effect.")),
		Slider(LOCTEXT("WearFeather", "Feather"), &FMixtormatLayerEffect::EdgeWearFeather, 0.0, 8.0, 1.0, 0.01,
			LOCTEXT("WearFeatherHint", "Softens the erosion threshold; it does not blur the finished height."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("WearGrpDirection", "Direction")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("WearDirections", "Directions"), Wear, &FMixtormatLayerEffect::EdgeWearDirections, 8.0, 32.0, 16,
			LOCTEXT("WearDirectionsHint", "Exact uniformly-spaced ray count. Higher values reduce angular stepping and cost proportionally more.")),
		Slider(LOCTEXT("WearAngularAA", "Angular AA"), &FMixtormatLayerEffect::EdgeWearAngularAA, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("WearAngularAAHint", "Blends the strongest directional minimum toward the average of the four strongest minima."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("WearGravity", "Gravity"), &FMixtormatLayerEffect::EdgeWearGravity, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("WearGravityHint", "Biases allowed slope by alignment with Gravity Angle. Zero is isotropic.")),
		Slider(LOCTEXT("WearGravityAngle", "Angle"), &FMixtormatLayerEffect::EdgeWearGravityAngle, -360.0, 360.0, 90.0, 1.0,
			LOCTEXT("WearGravityAngleHint", "Gravity direction in tangent-space degrees."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("WearGrpVariation", "Variation")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatLayerEffect>(
		LOCTEXT("WearSeed", "Seed"), Wear, &FMixtormatLayerEffect::EdgeWearSeed, 0.0, 1024.0, 1,
		LOCTEXT("WearSeedHint", "Reseeds the periodic resistance fields and independent per-region draws.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("WearMacroScale", "Macro Scale"), Wear, &FMixtormatLayerEffect::EdgeWearMacroScale, 1.0, 64.0, 12),
		Slider(LOCTEXT("WearMacroAmount", "Amount"), &FMixtormatLayerEffect::EdgeWearMacroAmount, 0.0, 2.0, 0.75, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("WearCellScale", "Cell Scale"), Wear, &FMixtormatLayerEffect::EdgeWearCellScale, 1.0, 64.0, 8),
		Slider(LOCTEXT("WearCellAmount", "Amount"), &FMixtormatLayerEffect::EdgeWearCellAmount, 0.0, 2.0, 1.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("WearRidgeScale", "Ridge Scale"), Wear, &FMixtormatLayerEffect::EdgeWearRidgeScale, 1.0, 64.0, 8),
		Slider(LOCTEXT("WearRidgeAmount", "Amount"), &FMixtormatLayerEffect::EdgeWearRidgeAmount, 0.0, 2.0, 1.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("WearMicroScale", "Micro Scale"), Wear, &FMixtormatLayerEffect::EdgeWearMicroScale, 1.0, 128.0, 40),
		Slider(LOCTEXT("WearMicroAmount", "Amount"), &FMixtormatLayerEffect::EdgeWearMicroAmount, 0.0, 2.0, 0.5, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("WearWarpScale", "Warp Scale"), Wear, &FMixtormatLayerEffect::EdgeWearWarpScale, 1.0, 64.0, 32),
		Slider(LOCTEXT("WearWarpAmount", "Amount"), &FMixtormatLayerEffect::EdgeWearWarpAmount, 0.0, 2.0, 0.25, 0.01)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("WearNoiseContrast", "Noise Contrast"), &FMixtormatLayerEffect::EdgeWearNoiseContrast, 0.05, 8.0, 0.5, 0.01,
		LOCTEXT("WearNoiseContrastHint", "Signed shaping of the combined resistance field.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("WearGrpId", "Per ID")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("WearIdVariation", "Variation"), &FMixtormatLayerEffect::EdgeWearIdVariation, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("WearIdVariationHint", "Master amount for deterministic variation from the nearest Region ID.")),
		Slider(LOCTEXT("WearIdRadius", "Radius"), &FMixtormatLayerEffect::EdgeWearIdRadius, 0.0, 4.0, 0.5, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("WearIdSlope", "Slope"), &FMixtormatLayerEffect::EdgeWearIdSlope, 0.0, 4.0, 0.3, 0.01),
		Slider(LOCTEXT("WearIdStrength", "Strength"), &FMixtormatLayerEffect::EdgeWearIdStrength, 0.0, 4.0, 0.25, 0.01)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("WearIdNoise", "Noise"), &FMixtormatLayerEffect::EdgeWearIdNoise, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("WearIdNoiseHint", "Varies the relative Macro/Cell/Ridge/Micro family weights independently per Region ID.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("WearGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(
			LOCTEXT("WearRoughnessWeight", "Roughness Weight"),
			&FMixtormatLayerEffect::EdgeWearRoughnessWeight,
			0.0, 1.0, 0.0, 0.01,
			LOCTEXT("WearRoughnessWeightHint", "Scales the roughness change through generated EdgeWearMask. Zero leaves roughness unchanged.")),
		Slider(
			LOCTEXT("WearRoughnessOffset", "Roughness Offset"),
			&FMixtormatLayerEffect::EdgeWearRoughnessOffset,
			-1.0, 1.0, 0.0, 0.01,
			LOCTEXT("WearRoughnessOffsetHint", "Signed roughness change on worn coverage. Positive roughens; negative smooths."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedWornEdges() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("WornEdgesHeading", "WORN EDGES"))
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

TSharedRef<SWidget> SMixtormat::BuildRampIdBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	// The set Ramp From IDs offers, which is deliberately not the whole enum: Replace would
	// discard the height under the region rather than meeting it, which is never what a ramp is
	// for.
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::Overlay,
		EMixtormatMaskBlendMode::Difference,
		EMixtormatMaskBlendMode::Exclusion
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->BlendMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->BlendMode == Mode;
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
			.HeaderAction(
				MakeFeaturePreviewButton(
					EMixtormatDebugPreviewMode::LayerMask,
					LOCTEXT("PreviewColorId", "Preview this selection in unlit dark red and cyan")))
			[
				Panel
			]
		];
}

// The palette rows, and the picker plumbing behind them. Resolved by index rather than through
// GetSelectedHsvFilter for the same reason the colour ID picker is: the window is modeless, so
// the selection can move while it is open and committing to whatever happens to be selected
// then would edit a different node from the one the user opened.
void SMixtormat::SetHsvPaletteColor(
	const FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex,
	const int32 ColorIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
	if (Child.Type != EMixtormatLayerChildType::HsvFilter
		|| !Child.HsvFilter.Palette.IsValidIndex(ColorIndex))
	{
		return;
	}
	Child.HsvFilter.Palette[ColorIndex] = NewColor;
	RefreshLayeredPreview();
}

FReply SMixtormat::OpenHsvPalettePicker(const int32 ColorIndex)
{
	const FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter();
	if (!Selected || !Selected->Palette.IsValidIndex(ColorIndex))
	{
		return FReply::Handled();
	}

	const int32 LayerIndex = SelectedLayerIndex;
	const int32 ChildIndex = SelectedMaskIndex;
	const FLinearColor OriginalColor = Selected->Palette[ColorIndex];

	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = OriginalColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex](const FLinearColor NewColor)
		{
			SetHsvPaletteColor(NewColor, LayerIndex, ChildIndex, ColorIndex);
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex, OriginalColor](const FLinearColor)
		{
			SetHsvPaletteColor(OriginalColor, LayerIndex, ChildIndex, ColorIndex);
		});
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

FReply SMixtormat::AddHsvPaletteEntry()
{
	if (FMixtormatHsvIdFilter* Hsv = GetSelectedHsvFilter())
	{
		if (Hsv->Palette.Num() < FMixtormatHsvIdFilter::MaxPaletteColors)
		{
			// Seeded from the last entry rather than from black, so adding a stop extends a ramp
			// the artist is already building instead of dropping a hole in the middle of it.
			//
			// Copied to a local first, and it has to be: Add can reallocate, and TArray asserts
			// outright on being handed a reference into the container it is growing.
			const FLinearColor Seeded =
				Hsv->Palette.IsEmpty() ? FLinearColor::White : Hsv->Palette.Last();
			Hsv->Palette.Add(Seeded);
			RefreshLayeredPreview();
			RebuildLayerList();
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveHsvPaletteEntry(const int32 ColorIndex)
{
	if (FMixtormatHsvIdFilter* Hsv = GetSelectedHsvFilter())
	{
		if (Hsv->Palette.IsValidIndex(ColorIndex))
		{
			Hsv->Palette.RemoveAt(ColorIndex);
			RefreshLayeredPreview();
			RebuildLayerList();
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildHsvFilterControls()
{
	const auto Hsv = [this]() { return GetSelectedHsvFilter(); };

	const auto Slider = [this, Hsv](
		const FText& Label,
		float FMixtormatHsvIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatHsvIdFilter>(Label, Hsv, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// The palette, and it is the half that matters. Jitter alone can only wander from wherever
	// the texture already sits; a palette lets the variation be aimed at colours that were
	// chosen. Same row shape as the colour ID selection, for the same reason: a colour is picked
	// by looking at it.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("HsvGrpPalette", "Palette")));

	// Every row that could exist is laid out once and shows itself when the data reaches it. The
	// panel is built at construction, long before anything is selected, so a loop over the
	// current entries would bake in whatever the count happened to be then -- which is zero.
	for (int32 ColorIndex = 0; ColorIndex < FMixtormatHsvIdFilter::MaxPaletteColors; ++ColorIndex)
	{
		Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this, ColorIndex]()
			{
				const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
				return H && H->Palette.IsValidIndex(ColorIndex)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.ToolTipText(LOCTEXT("HsvSwatchHint", "A stop on the palette regions draw from. Stops are evenly spaced and a region can land anywhere between two of them."))
				.OnClicked_Lambda([this, ColorIndex]() { return OpenHsvPalettePicker(ColorIndex); })
				[
					SNew(SColorBlock)
					.Color_Lambda([this, ColorIndex]()
					{
						const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
						return H && H->Palette.IsValidIndex(ColorIndex)
							? H->Palette[ColorIndex]
							: FLinearColor::Black;
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(MixtormatTokens::TileGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.Text(LOCTEXT("HsvRemoveColor", "Remove"))
				.OnClicked_Lambda([this, ColorIndex]() { return RemoveHsvPaletteEntry(ColorIndex); })
			]
		];
	}

	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.Text(LOCTEXT("HsvAddColor", "Add Colour"))
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
			return H && H->Palette.Num() < FMixtormatHsvIdFilter::MaxPaletteColors;
		})
		.OnClicked_Lambda([this]() { return AddHsvPaletteEntry(); })
	];

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvMixMin", "Tint Min"), &FMixtormatHsvIdFilter::RampMixMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("HsvMixMinHint", "How far a region tints toward the colour it sampled, at the low end. A range rather than one number so most regions can sit near the texture's own colour with a few pulled much further.")),
		Slider(LOCTEXT("HsvMixMax", "Tint Max"), &FMixtormatHsvIdFilter::RampMixMax, 0.0, 1.0, 0.15, 0.01,
			LOCTEXT("HsvMixMaxHint", "The high end of the same range. At 1 a region takes the palette colour outright and loses the texture's own; the useful territory is well below that."))));

	// Min/max pairs rather than a +/- amount, so variation can be biased: hue 0 to 0.1 shifts
	// only warm, which a symmetric amount cannot express.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("HsvGrpJitter", "Jitter")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvHueMin", "Hue Min"), &FMixtormatHsvIdFilter::HueMin, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("HsvHueMinHint", "Added, because hue is circular. -1 to 1 spans a full turn either way, the same convention as the layer's own Hue Shift -- so a couple of hundredths is already clearly visible on a flat surface. Both ends at 0 switches hue jitter off.")),
		Slider(LOCTEXT("HsvHueMax", "Hue Max"), &FMixtormatHsvIdFilter::HueMax, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("HsvHueMaxHint", "The other end of the hue range. Set both positive to shift only warm, both negative to shift only cool."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvSatMin", "Sat Min"), &FMixtormatHsvIdFilter::SaturationMin, 0.0, 2.0, 0.9, 0.01,
			LOCTEXT("HsvSatMinHint", "A multiplier around 1, because saturation is a magnitude rather than a position on a circle. 1 on both ends leaves saturation alone.")),
		Slider(LOCTEXT("HsvSatMax", "Sat Max"), &FMixtormatHsvIdFilter::SaturationMax, 0.0, 2.0, 1.1, 0.01,
			LOCTEXT("HsvSatMaxHint", "The high end of the saturation multiplier."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvValMin", "Value Min"), &FMixtormatHsvIdFilter::ValueMin, 0.0, 2.0, 0.9, 0.01,
			LOCTEXT("HsvValMinHint", "A multiplier around 1, like saturation. This is the one that reads as regions being fired differently, and the one to reach for first.")),
		Slider(LOCTEXT("HsvValMax", "Value Max"), &FMixtormatHsvIdFilter::ValueMax, 0.0, 2.0, 1.1, 0.01,
			LOCTEXT("HsvValMaxHint", "The high end of the value multiplier."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatHsvIdFilter>(
		LOCTEXT("HsvSeed", "Seed"), Hsv, &FMixtormatHsvIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("HsvSeedHint", "Reshuffles which region gets which colour without changing any of the ranges. All five draws -- palette position, tint amount, hue, saturation, value -- come off one hash of this and the region ID, so they move together.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedHsvFilter() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("HsvFilterHeading", "HSV FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("HsvEnabledHint", "Enable this HSV filter")))
			[
				Panel
			]
		];
}

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

TSharedRef<SWidget> SMixtormat::BuildPatternModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatPatternMode Modes[] = {
		EMixtormatPatternMode::Grid,
		EMixtormatPatternMode::RunningBond,
		EMixtormatPatternMode::Herringbone,
		EMixtormatPatternMode::Basketweave,
		EMixtormatPatternMode::Hex,
		EMixtormatPatternMode::OctagonSquare,
		EMixtormatPatternMode::Flagstone,
		EMixtormatPatternMode::Voronoi,
		EMixtormatPatternMode::Hopscotch,
		EMixtormatPatternMode::FrenchAshlar
	};
	for (const EMixtormatPatternMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::PatternModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatPatternFilter* Pattern = GetSelectedPatternId())
				{
					Pattern->PatternMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern && Pattern->PatternMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGridModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatGridMode Modes[] = {
		EMixtormatGridMode::Straight,
		EMixtormatGridMode::Staggered,
		EMixtormatGridMode::Diamond
	};
	for (const EMixtormatGridMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::GridModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatPatternFilter* Pattern = GetSelectedPatternId())
				{
					Pattern->GridMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern && Pattern->GridMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildPatternIdControls()
{
	const auto Pattern = [this]() { return GetSelectedPatternId(); };
	const auto Slider = [this, Pattern](
		const FText& Label,
		float FMixtormatPatternFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatPatternFilter>(
			Label, Pattern, Member, Min, Max, Default, Snap, Hint);
	};
	const auto Toggle = [this](
		const FText& Label,
		bool FMixtormatPatternFilter::* Member,
		const FText& Hint) -> TSharedRef<SWidget>
	{
		return MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Member]()
				{
					const FMixtormatPatternFilter* P = GetSelectedPatternId();
					return P && P->*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Member](const ECheckBoxState State)
				{
					if (FMixtormatPatternFilter* P = GetSelectedPatternId())
					{
						P->*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpLattice", "Lattice")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("PatternMode", "Pattern Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern
					? MixtormatUI::PatternModeText(Pattern->PatternMode)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildPatternModeMenu)),
		LOCTEXT("PatternModeHint", "Selects the procedural topology used to publish Pattern regions.")));
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && Pattern->PatternMode == EMixtormatPatternMode::Grid
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::Make(
				LOCTEXT("PatternGridMode", "Grid Mode"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
						return Pattern
							? MixtormatUI::GridModeText(Pattern->GridMode)
							: FText::GetEmpty();
					}),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildGridModeMenu)),
				LOCTEXT("PatternGridModeHint", "Selects the straight, staggered, or diamond Grid topology."))
		]);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && Pattern->PatternMode != EMixtormatPatternMode::Hex;
		})
		[
			MakeMemberSliderInt<FMixtormatPatternFilter>(
				LOCTEXT("PatternRows", "Rows"), Pattern, &FMixtormatPatternFilter::Rows, 1.0, 256.0, 8,
				LOCTEXT("PatternRowsHint", "Rows across one UV repeat. Hex derives this from Columns and output aspect."))
		],
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternColumns", "Columns"), Pattern, &FMixtormatPatternFilter::Columns, 1.0, 256.0, 8,
			LOCTEXT("PatternColumnsHint", "Columns across one UV repeat. Set Columns to 1 for stripe-like regions."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && (Pattern->PatternMode == EMixtormatPatternMode::RunningBond
				|| (Pattern->PatternMode == EMixtormatPatternMode::Grid
					&& Pattern->GridMode == EMixtormatGridMode::Staggered));
		})
		[
			Slider(LOCTEXT("PatternRowOffset", "Row Offset"), &FMixtormatPatternFilter::RowOffset, 0.0, 1.0, 0.0, 0.005,
				LOCTEXT("PatternRowOffsetHint", "Alternating-row shift in cell units. Used by Staggered Grid and Running Bond."))
		],
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && (Pattern->PatternMode == EMixtormatPatternMode::RunningBond
				|| Pattern->PatternMode == EMixtormatPatternMode::Flagstone
				|| Pattern->PatternMode == EMixtormatPatternMode::Voronoi);
		})
		[
			Slider(LOCTEXT("PatternJitter", "Jitter"), &FMixtormatPatternFilter::Jitter, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("PatternJitterHint", "Varies Running Bond bricks or the feature points used by Flagstone and Voronoi."))
		]));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternSwapAxes", "Swap Axes"), &FMixtormatPatternFilter::bSwapAxes,
			LOCTEXT("PatternSwapAxesHint", "Swaps the lattice axes without changing the ID contract; useful for bars and directional patterns.")),
		Slider(LOCTEXT("PatternGap", "Gap"), &FMixtormatPatternFilter::GapPixels, 0.0, 64.0, 0.0, 0.25,
			LOCTEXT("PatternGapHint", "Region-less grout width in output pixels. Gap pixels emit the invalid-region sentinel, so HSV/Random/Ramp From IDs pass through there."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternRounding", "Rounding"), &FMixtormatPatternFilter::Rounding, 0.0, 1.0, 0.0, 0.005,
			LOCTEXT("PatternRoundingHint", "Rounds the cell corners by blending the two nearest walls instead of taking a hard minimum, so a chamfer fillets into the corner rather than creasing. In cell fractions. 0 is the true Voronoi corner.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternGapHeight", "Gap Height"), &FMixtormatPatternFilter::GapHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternGapHeightHint", "Where the grout sits relative to the cells. Negative sinks it into a trench, positive stands it proud as a raised mortar line. Needs a Gap above 0 -- without one every pixel belongs to a cell and there is nothing outside the IDs to move.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpUV", "UV Variation")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternUVEnable", "Enable"), &FMixtormatPatternFilter::bUVVariation,
			LOCTEXT("PatternUVEnableHint", "Transforms the layer source independently around each pattern region centre.")),
		Toggle(LOCTEXT("PatternUVOrthogonal", "90° Only"), &FMixtormatPatternFilter::bOrthogonalUV,
			LOCTEXT("PatternUVOrthogonalHint", "Snaps random region rotation to 90-degree steps, preserving the source tile's periodic orientation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVRotMin", "Rot Min"), &FMixtormatPatternFilter::UVRotationMin, -360.0, 360.0, 0.0, 1.0,
			LOCTEXT("PatternUVRotMinHint", "Low end of the per-region source rotation range in degrees.")),
		Slider(LOCTEXT("PatternUVRotMax", "Rot Max"), &FMixtormatPatternFilter::UVRotationMax, -360.0, 360.0, 360.0, 1.0,
			LOCTEXT("PatternUVRotMaxHint", "High end of the per-region source rotation range in degrees."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVScaleMin", "Scale Min"), &FMixtormatPatternFilter::UVScaleMin, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMinHint", "Low end of the per-region source scale multiplier.")),
		Slider(LOCTEXT("PatternUVScaleMax", "Scale Max"), &FMixtormatPatternFilter::UVScaleMax, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMaxHint", "High end of the per-region source scale multiplier."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("PatternUVOffset", "Offset"), &FMixtormatPatternFilter::UVOffset, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("PatternUVOffsetHint", "Maximum random source translation per region, as a fraction of one source repeat.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternFlipU", "Flip U"), &FMixtormatPatternFilter::bRandomFlipU,
			LOCTEXT("PatternFlipUHint", "Randomly mirrors the source across U per region.")),
		Toggle(LOCTEXT("PatternFlipV", "Flip V"), &FMixtormatPatternFilter::bRandomFlipV,
			LOCTEXT("PatternFlipVHint", "Randomly mirrors the source across V per region."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpRelief", "Relief")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternHeight", "Height"), &FMixtormatPatternFilter::HeightAmount, 0.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternHeightHint", "How far each cell stands off the base. The face stays flat -- for a slope across each cell, stack Ramp From IDs over this.")),
		Slider(LOCTEXT("PatternHeightRandom", "Height Random"), &FMixtormatPatternFilter::HeightRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("PatternHeightRandomHint", "How far below Height a cell may be drawn, as a multiplier. At 0 every cell sits at full Height; at 1 they spread the whole way down to the base. Never negative -- a cell below the base would feather back up at its wall and read as a recessed panel in a raised frame."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternProfile", "Profile"), &FMixtormatPatternFilter::Profile, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileHint", "The chamfer's cross-section, from the grout line up to the flat of the cell. -1 is a cove that hugs the grout then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose that lifts away and rounds over. Never changes the chamfer's width or height.")),
		Slider(LOCTEXT("PatternProfileRandom", "Profile Random"), &FMixtormatPatternFilter::ProfileRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileRandomHint", "Offsets the roundness per cell, so one cell's bullnose can be its neighbour's cove."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFeather", "Feather"), &FMixtormatPatternFilter::Feather, 0.0, 0.5, 0.15, 0.005,
			LOCTEXT("PatternFeatherHint", "Eases each cell's height out at its boundary so neighbouring pieces meet through a ramp rather than a one-texel cliff.")),
		Slider(LOCTEXT("PatternFeatherRandom", "Feather Random"), &FMixtormatPatternFilter::FeatherRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternFeatherRandomHint", "Varies the feather width once per cell, so the run-out is not identical on every piece."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternNormal", "Normal"), &FMixtormatPatternFilter::NormalStrength, 0.0, 32.0, 8.0, 0.05,
			LOCTEXT("PatternNormalHint", "Normal strength derived from the same elevation and bevel height field.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpEdges", "Edges")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelHeight", "Height"), &FMixtormatPatternFilter::BevelHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternBevelHeightHint", "Stands each cell proud of the grout, with the chamfer ramping down to it. Negative sinks the cell face below the grout instead. The gap itself is untouched either way -- that is Gap Height.")),
		Slider(LOCTEXT("PatternBevelWidth", "Width"), &FMixtormatPatternFilter::BevelWidthPixels, 0.25, 64.0, 4.0, 0.25,
			LOCTEXT("PatternBevelWidthHint", "Chamfer width, in output pixels or as a fraction of the cell depending on Relative Width below."))));
	AddSliderRow(Panel, Toggle(
		LOCTEXT("PatternRelativeEdge", "Relative Width"), &FMixtormatPatternFilter::bRelativeEdgeWidth,
		LOCTEXT("PatternRelativeEdgeHint", "Measures the chamfer as a fraction of the cell instead of in output pixels: 0 at the wall, 1 at the point furthest inside. Frames every cell the same way whatever its size or aspect, and is normalised against how far jitter pushes the deepest interior point. Off keeps an even visual width across cells of different sizes. Width comes from Width (Cells) when on and Width when off.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternBevelWidthCells", "Width (Cells)"), &FMixtormatPatternFilter::BevelWidthCells, 0.0, 1.0, 0.25, 0.005,
			LOCTEXT("PatternBevelWidthCellsHint", "The chamfer width used in Relative mode, as a fraction of the way from the cell wall to its deepest interior point. 1 runs the chamfer all the way in, leaving no flat face.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelVariation", "Variation"), &FMixtormatPatternFilter::BevelVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternBevelVariationHint", "Varies bevel width once per region.")),
		Slider(LOCTEXT("PatternBevelInset", "Inset"), &FMixtormatPatternFilter::BevelInsetPixels, -32.0, 32.0, 0.0, 0.25,
			LOCTEXT("PatternBevelInsetHint", "Slides the chamfer across the grout line in output pixels. Negative puts it out in the gap, positive pulls it onto the cell face."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternEdgeRoughness", "Roughness"), &FMixtormatPatternFilter::EdgeRoughness, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("PatternEdgeRoughnessHint", "Roughness value approached at region edges. Applied after the layer composite, so it intentionally bypasses the layer Roughness Influence control.")),
		Slider(LOCTEXT("PatternEdgeRoughnessAmount", "Amount"), &FMixtormatPatternFilter::EdgeRoughnessAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternEdgeRoughnessAmountHint", "Strength of edge roughness. 0 leaves the packed roughness channel unchanged."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternAOAmount", "AO"), &FMixtormatPatternFilter::AOAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternAOAmountHint", "Darkens packed AO at region creases. Applied after the layer composite, so it intentionally bypasses the layer AO Influence control.")),
		Slider(LOCTEXT("PatternAOSpread", "Spread"), &FMixtormatPatternFilter::AOSpread, 1.0, 8.0, 2.0, 0.05,
			LOCTEXT("PatternAOSpreadHint", "How much farther the edge AO reaches relative to the bevel width."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatPatternFilter>(
		LOCTEXT("PatternSeed", "Seed"), Pattern, &FMixtormatPatternFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("PatternSeedHint", "Reshuffles feature jitter and every per-region UV, height and bevel draw while preserving the lattice.")));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedPatternId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("PatternIdHeading", "PATTERN IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				.Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::ClusterIds,
						LOCTEXT("PreviewPatternIds", "Preview Pattern IDs as a hashed colour per region. Gap pixels show the invalid-region colour."))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatPatternFilter* Selected = GetSelectedPatternId();
							return Selected && Selected->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatPatternFilter* Selected = GetSelectedPatternId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("PatternEnabledHint", "Enable this Pattern ID producer"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildRampIdControls()
{
	const auto Ramp = [this]() { return GetSelectedRampId(); };

	const auto Slider = [this, Ramp](
		const FText& Label,
		float FMixtormatRampIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatRampIdFilter>(Label, Ramp, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// Relief first, because it is the whole point of the node. The gradient controls below shape
	// what the ramp does; these decide whether it does anything at all.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RampGrpRelief", "Relief")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RampIntensity", "Intensity"), &FMixtormatRampIdFilter::HeightAmount, 0.0, 0.5, 0.05, 0.001,
			LOCTEXT("RampIntensityHint", "How strongly each region's ramp meets the surface. A blend weight, so 0 leaves the surface untouched under every mode and skips the pass entirely.")),
		Slider(LOCTEXT("RampIntensityRandom", "Random Intensity"), &FMixtormatRampIdFilter::IntensityRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RampIntensityRandomHint", "Per-region jitter on that strength. 0 leaves every region at full Intensity -- unlike Pattern IDs' Height pair, a uniform ramp strength is meaningful on its own, since every region still tilts in its own direction."))));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RampBlendMode", "Blend"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R ? MixtormatUI::MaskBlendModeText(R->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildRampIdBlendModeMenu)),
		LOCTEXT("RampBlendModeHint", "How the ramp meets the height under it. Add/Sub is the centred case -- a region rises on one side exactly as much as it falls on the other -- and Min carves, Multiply darkens. The normal is derived from the blended height rather than blended separately, so it always describes the surface actually written.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("RampNormal", "Normal Intensity"), &FMixtormatRampIdFilter::NormalStrength, 0.0, 32.0, 8.0, 0.05,
			LOCTEXT("RampNormalHint", "Gain on the normal derived from the slope. Independent of Intensity, so a region can catch light as though tipped without displacing as far -- but it is scaled by Intensity too, since a region that is not tipped has no slope to light.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("RampAO", "AO"), &FMixtormatRampIdFilter::AOAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RampAOHint", "Contact and cavity occlusion derived from the height change made by the ramp. Multiplies the existing AO rather than replacing it.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RampGrpGradient", "Gradient")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RampRotate", "Random Rotation"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->bRotateRandom ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->bRotateRandom = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("RampRotateHint", "Gives every region's gradient its own direction. The ramp fits its region's bounding box exactly at any angle, so turning this on never clips or flattens it. Off puts every gradient on the same axis, which reads as a comb over the whole surface rather than as pieces that settled independently.")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RampAngleStep", "Angle Stepping"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->bAngleStepping ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->bAngleStepping = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("RampAngleStepHint", "Snaps every region's angle to a multiple of the step below, so a lattice reads as deliberately laid rather than scattered.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("RampAngleStepDegrees", "Step"), &FMixtormatRampIdFilter::AngleStepDegrees, 1.0, 90.0, 5.0, 0.5,
			LOCTEXT("RampAngleStepDegreesHint", "The snap interval in degrees. Applied to the true screen-space angle, so on a long brick 45 degrees is 45 degrees on screen and the steps stay visually even.")));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatRampIdFilter>(
		LOCTEXT("RampSeed", "Seed"), Ramp, &FMixtormatRampIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("RampSeedHint", "Reshuffles which region gets which angle and strength without changing any of the ranges. Independent of the cluster filter's controls, so reseeding here does not re-segment.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRampId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RampIdHeading", "RAMP FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatRampIdFilter* Selected = GetSelectedRampId();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatRampIdFilter* Selected = GetSelectedRampId())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("RampEnabledHint", "Enable this ramp filter")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildRandomIdBlendModeMenu()
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
				if (FMixtormatRandomIdMask* R = GetSelectedRandomId())
				{
					R->BlendMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatRandomIdMask* R = GetSelectedRandomId();
				return R && R->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildRandomIdControls()
{
	const auto Random = [this]() { return GetSelectedRandomId(); };

	const auto Slider = [this, Random](
		const FText& Label,
		float FMixtormatRandomIdMask::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatRandomIdMask>(Label, Random, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RandomMin", "Min"), &FMixtormatRandomIdMask::MinValue, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RandomMinHint", "The low end of the range a region's value is drawn from. Narrowing the range is how the variation is kept subtle without touching anything downstream.")),
		Slider(LOCTEXT("RandomMax", "Max"), &FMixtormatRandomIdMask::MaxValue, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RandomMaxHint", "The high end. Setting Min above Max runs the range backwards, which is the same picture as Invert."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatRandomIdMask>(
		LOCTEXT("RandomSeed", "Seed"), Random, &FMixtormatRandomIdMask::Seed, 0.0, 64.0, 1,
		LOCTEXT("RandomSeedHint", "Reshuffles which region gets which value without changing the range. Independent of the cluster filter's own controls, so reseeding here does not re-segment -- it is the cheap dial.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RandomGrpBlend", "Blend")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RandomBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatRandomIdMask* R = GetSelectedRandomId();
				return R ? MixtormatUI::MaskBlendModeText(R->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildRandomIdBlendModeMenu)),
		LOCTEXT("RandomBlendModeHint", "How the per-region value combines with the mask accumulated above it in this layer. Multiply against a painted mask is the common one: vary only where you already wanted the layer.")));

	AddSliderRow(Panel, Slider(LOCTEXT("RandomWeight", "Weight"), &FMixtormatRandomIdMask::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("RandomWeightHint", "How far the blend is taken. 0 is the off switch for this node, and it costs nothing -- the pass is skipped rather than run to reproduce its input.")));

	// The shared shaping block, and it is doing real work here rather than being boilerplate.
	// Contrast above 1 about the 0.5 midpoint pulls most regions toward the mean and leaves a
	// few outliers, which is what the prototype's ramp distribution mode was for.
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatRandomIdMask* R = GetSelectedRandomId();
		return R ? &R->Shaping : nullptr;
	});

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRandomId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RandomIdHeading", "RANDOM FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::LayerMask,
						LOCTEXT("PreviewRandomId", "Preview this mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatRandomIdMask* Selected = GetSelectedRandomId();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatRandomIdMask* Selected = GetSelectedRandomId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("RandomEnabledHint", "Enable this mask"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildClusterSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatClusterSource Sources[] = {
		EMixtormatClusterSource::LayerSurface,
		EMixtormatClusterSource::CompositeBelow
	};
	for (const EMixtormatClusterSource Source : Sources)
	{
		Menu.Item(
			Source == EMixtormatClusterSource::CompositeBelow
				? LOCTEXT("ClusterSourceComposite", "Composite Below")
				: LOCTEXT("ClusterSourceLayer", "Layer Surface"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Source]()
			{
				if (FMixtormatClusterFilter* C = GetSelectedFilter())
				{
					C->Source = Source;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Source]()
			{
				const FMixtormatClusterFilter* C = GetSelectedFilter();
				return C && C->Source == Source;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildFilterControls()
{
	const auto Filter = [this]() { return GetSelectedFilter(); };

	// No blend mode, no weight and no shaping block. Those are the mask vocabulary, and this node
	// emits integer region labels rather than coverage -- there is nothing meaningful to contrast
	// or invert about a label. The eye on the header is the only consumer that exists so far.
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("ClusterSource", "Source"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatClusterFilter* C = GetSelectedFilter();
				if (!C) { return FText::GetEmpty(); }
				return C->Source == EMixtormatClusterSource::CompositeBelow
					? LOCTEXT("ClusterSourceCompositeChip", "Composite Below")
					: LOCTEXT("ClusterSourceLayerChip", "Layer Surface");
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildClusterSourceMenu)),
		LOCTEXT("ClusterSourceHint", "What gets segmented. Layer Surface reads this layer's own packed map -- the bricks in the brick texture -- through its UV transform. Composite Below reads the surface already accumulated underneath, at full resolution and untiled, so the regions follow what is actually visible there. Composite Below is usually the calmer of the two: a raw scan can be busy enough to shatter into far more regions than the eye reads as pieces, while the surface below has been through blending and grading already. On the bottom layer there is nothing below, so it falls back to the layer's own map.")));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatClusterFilter>(
		LOCTEXT("ClusterThreshold", "Threshold"), Filter, &FMixtormatClusterFilter::Threshold,
		0.0, 1.0, 0.33, 0.005,
		LOCTEXT("ClusterThresholdHint", "Band width, and so how big a region is. Roughness is quantised into bands of this width and neighbours join only inside one. Read after height and roughness are both renormalised to 0-1, which is what makes one value mean the same thing on every scan instead of drifting with that texture's own range.")));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatClusterFilter>(
		LOCTEXT("ClusterOffset", "Offset"), Filter, &FMixtormatClusterFilter::Offset,
		-1.0, 1.0, 0.0, 0.005,
		LOCTEXT("ClusterOffsetHint", "Where the band boundaries fall. Same granularity, different partition -- a reseed that moves every boundary without changing the character of the result.")));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatClusterFilter>(
		LOCTEXT("ClusterHeightInfluence", "Height Influence"), Filter, &FMixtormatClusterFilter::HeightInfluence,
		0.0, 8.0, 1.0, 0.01,
		LOCTEXT("ClusterHeightInfluenceHint", "How strongly a step in height blocks a merge roughness would otherwise allow. The criterion is two-channel: same roughness band and no height step. At 0 it falls back to roughness bands alone.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedFilter() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ClusterFilterHeading", "CLUSTER IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::ClusterIds,
						LOCTEXT("PreviewClusterIds", "Preview the region ID map as a hashed colour per region. Debug only -- the IDs themselves are sparse integers and nothing consumes them yet."))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatClusterFilter* Selected = GetSelectedFilter();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatClusterFilter* Selected = GetSelectedFilter())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("ClusterEnabledHint", "Enable this cluster filter"))
				])
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
	// Only the growth block is mode-gated now. Scale, Jitter, Width and Variation used to have a
	// lattice-only twin each; they are shared, so there is nothing left to hide from lattice mode.
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

	// Scale and Jitter are shared. Each mode used to declare its own -- Cells/Jitter for the
	// lattice, Seed Cells/Seed Jitter for the growth -- which read as four decisions and behaved as
	// two, and switching mode moved the result because the pairs held different numbers.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqScale", "Scale"), Craq, &FMixtormatCraquelure::Scale, 1.0, 128.0, 8,
			LOCTEXT("CraqScaleHint", "Cells across one UV repeat: how big a piece of the broken surface is. Lattice reads it as the Voronoi cell count, Propagated as the lattice its nuclei are placed on. Any integer tiles, because both wrap on it.")),
		Slider(LOCTEXT("CraqJitter", "Jitter"), &FMixtormatCraquelure::Jitter, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("CraqJitterHint", "0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives organic crazing. One control spans tile seams to cracked paint."))));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWidth", "Width"), &FMixtormatCraquelure::Width, 0.002, 0.5, 0.04, 0.001,
			LOCTEXT("CraqWidthHint", "In cell units, so it means the same thing at any Scale. Built on distance to the crack rather than on a difference of feature points, which is what holds the width even instead of going heavy in large cells and hairline in small ones.")),
		Slider(LOCTEXT("CraqVariation", "Variation"), &FMixtormatCraquelure::Variation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqVariationHint", "Thins individual cracks, so the network reads as breaks that opened at different times rather than a uniform lattice. Keyed on the whole crack, so it varies as one thing instead of splitting down its centre."))));

	TSharedRef<SVerticalBox> GrowGroup = SNew(SVerticalBox);
	GrowGroup->SetVisibility(PropagatedOnly);

	AddSliderRow(GrowGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpGrowth", "Growth")));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqDensity", "Density"), &FMixtormatCraquelure::Density, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("CraqDensityHint", "Fraction of cells that actually get a nucleus: how many separate cracks there are, as opposed to how far each one runs.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqIterations", "Reach"), Craq, &FMixtormatCraquelure::Iterations, 1.0, 1024.0, 48,
			LOCTEXT("CraqIterationsHint", "How far a crack can travel, in pixels at a 1024 reference and scaled by the render resolution so a preview and an export grow the same network. One dispatch per step and by some way the most expensive node here, so this is the control that costs."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqDetail", "Detail"), &FMixtormatCraquelure::Detail, 0.1, 8.0, 0.65, 0.01,
			LOCTEXT("CraqDetailHint", "Size of the stress, toughness and flow fields as a multiple of Scale. Under 1 they steer whole regions; over 1 they roughen individual cracks. A multiple rather than a cell count of its own, so moving Scale keeps the character instead of fighting it.")),
		Slider(LOCTEXT("CraqFieldContrast", "Contrast"), &FMixtormatCraquelure::FieldContrast, 0.0, 1.0, 0.4, 0.01,
			LOCTEXT("CraqFieldContrastHint", "How far the stress and toughness fields depart from uniform. At 0 the network is steered only by Flow and Roughness, which reads as combed rather than fractured. One dial for both: they are the two ends of one balance."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqStraightness", "Straightness"), &FMixtormatCraquelure::Straightness, 0.0, 1.0, 0.35, 0.01,
			LOCTEXT("CraqStraightnessHint", "How much a crack is a line rather than a blob. Drives both halves of holding a heading -- how much alignment counts when a step is scored, and how fast the stored direction follows the step taken -- which run opposite ways and were easy to set against each other as two controls.")),
		Slider(LOCTEXT("CraqFractureBias", "Fracture Bias"), &FMixtormatCraquelure::FractureBias, 0.0, 8.0, 0.85, 0.01,
			LOCTEXT("CraqFractureBiasHint", "How strongly the stress and toughness fields win against a tip's own heading. Raise it and cracks detour a long way around hard material; drop it and they drive straight through."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqFlow", "Flow"), &FMixtormatCraquelure::Flow, 0.0, 1.0, 0.18, 0.01,
			LOCTEXT("CraqFlowHint", "Coherent wander: how strongly the curl-flow field bends a running crack. At 1 cracks follow the field and come out combed.")),
		Slider(LOCTEXT("CraqRoughness", "Roughness"), &FMixtormatCraquelure::Roughness, 0.0, 8.0, 0.32, 0.01,
			LOCTEXT("CraqRoughnessHint", "Incoherent wander: per-step randomness in the scoring. Low gives clean arcs, high gives a brittle, ragged line. Kept apart from Flow because directional and random wander are different looks."))));

	AddSliderRow(GrowGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpAdvanced", "Advanced")));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqThreshold", "Threshold"), &FMixtormatCraquelure::GrowthThreshold, 0.0, 3.0, 0.55, 0.01,
			LOCTEXT("CraqThresholdHint", "The score a step has to beat to happen at all. Raising it starves growth, which is what decides how much of the surface ends up cracked.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqCollision", "Collision"), Craq, &FMixtormatCraquelure::CollisionLimit, 1.0, 8.0, 2,
			LOCTEXT("CraqCollisionHint", "Cracked neighbours a pixel may already have and still be grown into. At 2 a crack reaching an older one stops, because the older crack already released the stress driving it -- that is the right-angle junction of a drying film. Raise it and cracks cross, which reads as scratches rather than fracture."))));

	Panel->AddSlot().AutoHeight()[GrowGroup];

	// Always shown, rather than behind an output mode. A crack normally wants to mask, cut and
	// catch light at the same time, and the three weights say how much of each -- Height and
	// Normal here, Weight in Blend below. Each is its own off switch at zero, so nothing has to
	// be chosen between.
	TSharedRef<SVerticalBox> ReliefGroup = SNew(SVerticalBox);
	AddSliderRow(ReliefGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpRelief", "Relief")));
	AddSliderRow(ReliefGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqReliefDepth", "Height"), &FMixtormatCraquelure::ReliefDepth, 0.0, 0.5, 0.04, 0.001,
			LOCTEXT("CraqReliefDepthHint", "How deep the crack cuts into the composited height. The groove is a cone on the distance to the crack -- the eikonal solution, so its wall has one constant slope -- subtracted under a minimum, so this can only lower the height. 0 skips the pass.")),
		Slider(LOCTEXT("CraqReliefNormal", "Normal"), &FMixtormatCraquelure::ReliefNormalStrength, 0.0, 32.0, 8.0, 0.05,
			LOCTEXT("CraqReliefNormalHint", "Gain on the normal derived from the groove wall. Independent of Height, so a crack can catch light without displacing, or displace without being relit."))));
	AddSliderRow(ReliefGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqReliefWidth", "Groove"), &FMixtormatCraquelure::ReliefWidth, 0.002, 1.0, 0.08, 0.001,
			LOCTEXT("CraqReliefWidthHint", "Half-width of the groove, in cell units like Width. Separate from Width because they are different things: Width is the hairline the mask draws, this is the mouth of the dish around it, and a fine dark crack usually sits in a much wider depression.")),
		Slider(LOCTEXT("CraqReliefProfile", "Profile"), &FMixtormatCraquelure::ReliefProfile, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("CraqReliefProfileHint", "Shape of the groove wall. 1 is the straight cone the distance field gives directly, the constant-slope fracture case; below 1 flares it to a dish, above draws it into a narrow V with a broad shoulder."))));
	AddSliderRow(ReliefGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqReliefGrooveVar", "Groove Var"), &FMixtormatCraquelure::ReliefGrooveVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqReliefGrooveVarHint", "Per-crack variation in groove depth, keyed on the same lineage id the network already carries -- stable per crack rather than noisy per pixel. 0 leaves every groove the same depth.")),
		Slider(LOCTEXT("CraqReliefProfileVar", "Profile Var"), &FMixtormatCraquelure::ReliefProfileVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqReliefProfileVarHint", "Per-crack variation in wall shape, so some grooves read as a sharper V and others as a broader dish. 0 leaves Profile constant across the network."))));
	AddSliderRow(ReliefGroup, Slider(
		LOCTEXT("CraqReliefWidthVar", "Width Var"), &FMixtormatCraquelure::ReliefWidthVariation, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("CraqReliefWidthVarHint", "Per-crack variation in the groove's mouth, so some dishes sit wider than others without moving the crack itself. 0 leaves Groove constant across the network.")));
	Panel->AddSlot().AutoHeight()[ReliefGroup];

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpWarp", "Warp")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWarp", "Amount"), &FMixtormatCraquelure::Warp, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqWarpHint", "Bends the finished network. Applied where the crack field is read rather than to how it grows, so it costs one pass and rebuilds nothing -- dragging this is a cache hit, unlike every control above it. Periodic curl noise, which is divergence-free and wraps on its own period, so the result still tiles.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqWarpScale", "Scale"), Craq, &FMixtormatCraquelure::WarpScale, 1.0, 32.0, 4,
			LOCTEXT("CraqWarpScaleHint", "Size of the swirls doing the bending. Below the crack Scale it bends whole regions; above it, it roughens individual cracks."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatCraquelure>(
		LOCTEXT("CraqSeed", "Seed"), Craq, &FMixtormatCraquelure::Seed, 0.0, 64.0, 1,
		LOCTEXT("CraqSeedHint", "Reshuffles the crack network without changing its scale or density. The warp is seeded off this too, so reseeding moves both rather than leaving a second seed to remember.")));

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

	AddSliderRow(Panel, Slider(LOCTEXT("CraqWeight", "Weight"), &FMixtormatCraquelure::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("CraqWeightHint", "How far the blend is taken. 0 is the off switch for this node.")));

	// The shared shaping block, so Invert / Balance / Contrast / Offset behave here exactly as they
	// do on a texture mask instead of being a fourth hand-written copy with its own ranges.
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatCraquelure* C = GetSelectedCraquelure();
		return C ? &C->Shaping : nullptr;
	});

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCraquelure() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CraquelureHeading", "CRAQUELURE"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MakeFeaturePreviewButton(
					EMixtormatDebugPreviewMode::LayerMask,
					LOCTEXT("PreviewCraquelure", "Preview this crack network in unlit dark red and cyan")))
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


	// Erosion contributes no base colour. Its resolved mask only weights surface channels.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroRoughAmount", "Roughness"), &FMixtormatLayerEffect::ErosionRoughnessAmount, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("EroRoughAmountHint", "Signed, mask-weighted offset on composited roughness; positive moves toward rough.")),
		MakeErosionSlider(LOCTEXT("EroCarveDepth", "Full At Depth"), &FMixtormatLayerEffect::ErosionCarveDepth, 0.001, 1.0, 0.05, 0.001,
			LOCTEXT("EroCarveDepthHint", "Carve depth that reaches the full roughness weight."))));
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
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
	// Shared by layer-scoped and feature-scoped mask rows: one generic binding per row,
	// captions for grouping, and pairs where both labels are one short word.
	const auto Mask = [this]() { return GetSelectedLayerMask(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Visible : EVisibility::Collapsed; });

	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
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

	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatMaskLayer* M = GetSelectedLayerMask();
		return M ? &M->Shaping : nullptr;
	});

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex)
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("LayerMaskHeading", "MASK BLENDING"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
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
	AddSliderRow(Transform, MixtormatRow::Make(
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
		LOCTEXT("RoughnessContrastLabel", "Contrast"), Layer(), &FMixtormatLayer::RoughnessContrast, 0.0, 2.0, 1.0, 0.01));
	AddSliderRow(Roughness, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessOffsetLabel", "Offset"), Layer(), &FMixtormatLayer::RoughnessOffset, -0.5, 0.5, 0.0, 0.01));

	// Relief, not Normal: the card holds both halves of how strongly this layer's own detail
	// reads -- how hard the light follows it and how deep it actually is -- and those are always
	// reached for together. Same word craquelure and the ramp filter use for the same pair.
	TSharedRef<SVerticalBox> Relief = AddCard(Panel, LOCTEXT("CardRelief", "Relief"));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("NormalLabel", "Normal Intensity"), Layer(), &FMixtormatLayer::NormalIntensity, 0.0, 2.0, 1.0, 0.01,
		LOCTEXT("NormalIntensityHint", "Gain on this layer's normal map. Independent of Height Booster, so the surface can catch light as though deeper without actually displacing further.")));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightBoostLabel", "Height Booster"), Layer(), &FMixtormatLayer::HeightBoost, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("HeightBoostHint", "Gain on this layer's height, signed about the flat midpoint: peaks rise and pits sink by the same factor, so the surface exaggerates without floating. 1 is untouched, 0 is flat. Applied before anything reads the height, so displacement, the height blend and the derived normals all agree. Not Height Influence, which is coverage -- how much of this layer's height reaches the composite rather than how deep it is.")));

	AddGeneratedFeatureCards(Panel);
	return Panel;
}

// No group header of its own. These are three runs of values inside Surface Adjustments, and a
// second header bar over them only repeated the one already above -- with a chevron that hid
// controls the panel exists to offer. The eye that previewed the feature mask moves to the card
// it belongs to, at the end of that card's title line.
void SMixtormat::AddGeneratedFeatureCards(const TSharedRef<SVerticalBox>& Panel)
{
	const auto Layer = [this]()
	{
		return TFunction<FMixtormatLayer*()>([this]() -> FMixtormatLayer*
		{
			return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
		});
	};

	TSharedRef<SVerticalBox> Feature = AddCard(
		Panel,
		LOCTEXT("CardFeature", "Feature"),
		MakeFeaturePreviewButton(
			EMixtormatDebugPreviewMode::GeneratedFeature,
			LOCTEXT("PreviewGeneratedFeature", "Preview the cavity-to-convex feature mask in unlit dark red and cyan")));
	AddSliderRow(Feature, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("FeatureInfluenceLabel", "Normal Influence"), Layer(), &FMixtormatLayer::FeatureInfluence, 0.0, 1.0, 0.0, 0.01));
	AddSliderRow(Feature, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("FeatureBiasLabel", "Cavity to Convex"), Layer(), &FMixtormatLayer::FeatureBias, 0.0, 1.0, 0.0, 0.01));
	AddSliderRow(Feature, MakeMemberToggle<FMixtormatLayer>(
		LOCTEXT("InvertGeneratedFeatureLabel", "Invert"), Layer(), &FMixtormatLayer::bInvertFeature,
		LOCTEXT("InvertGeneratedFeatureHint", "Apply one-minus to the selected cavity-to-convex feature mask")));

	// Four short labels that are read against each other, so two across.
	TSharedRef<SVerticalBox> Curvature = AddCard(Panel, LOCTEXT("CardCurvature", "Curvature"));
	AddSliderRow(Curvature, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("CurvatureRadiusLabel", "Radius"), Layer(), &FMixtormatLayer::CurvatureRadius, 1.0, 32.0, 2),
		MakeMemberSliderInt<FMixtormatLayer>(
			LOCTEXT("CurvatureSmoothingLabel", "Smoothing"), Layer(), &FMixtormatLayer::CurvatureSmoothing, 1.0, 4.0, 2)));
	AddSliderRow(Curvature, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("CurvatureStrengthLabel", "Strength"), Layer(), &FMixtormatLayer::CurvatureStrength, 0.0, 8.0, 1.0, 0.05),
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("CurvaturePowerLabel", "Power"), Layer(), &FMixtormatLayer::CurvaturePower, 0.001, 8.0, 1.0, 0.05)));

	TSharedRef<SVerticalBox> SurfaceMask = AddCard(Panel, LOCTEXT("CardSurfaceMask", "Surface Mask"));
	// Each influence pairs with its own invert, which is what paired rows exist for.
	AddSliderRow(SurfaceMask, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayer>(
			LOCTEXT("UnderlyingHeightInfluenceLabel", "Height"), Layer(),
			&FMixtormatLayer::HeightFeatureInfluence, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UnderlyingHeightInfluenceHint", "Mask this layer using the accumulated height underneath it")),
		MakeMemberToggle<FMixtormatLayer>(
			LOCTEXT("InvertUnderlyingHeightLabel", "Invert"), Layer(),
			&FMixtormatLayer::bInvertHeightFeature,
			LOCTEXT("InvertUnderlyingHeightHint", "Favor lower underlying height instead of higher height"))));
	AddSliderRow(SurfaceMask, MixtormatRow::MakePair(
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
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("FuzzInfluenceLabel", "Fuzz"), Layer, &FMixtormatLayer::FuzzInfluence, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("FuzzInfluenceHint", "How much this layer pushes the substrate's Fuzz Slab amount. 0 leaves the master material's own fuzz alone; the fuzz shading itself stays on DA_FuzzRoughness and DA_FuzzColor there.")));

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
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::HeightBlend,
						LOCTEXT("PreviewHeightBlendMask", "Preview the computed height blend mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SMixtormatToggle)
					.ToolTip(LOCTEXT("EnableHeightBlend", "Enable Height Blend"))
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SMixtormatChip)
				.Visibility(EVisibility::Collapsed)
				.ToolTip(LOCTEXT("HeightSourceTooltip", "Compatibility-only height source selector"))
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
					return SNew(SBox).WidthOverride(MixtormatTokens::OptionMenuWidth)[Menu];
				})
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SMixtormatChip)
				.Visibility(EVisibility::Collapsed)
				.ToolTip(LOCTEXT("HeightReferenceTooltip", "Compatibility-only height reference selector"))
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
					return SNew(SBox).WidthOverride(MixtormatTokens::OptionMenuWidth)[Menu];
				})
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

			+ SVerticalBox::Slot().AutoHeight()
			[
				NumericRow(LOCTEXT("HeightMaskStrength", "Mask Strength"), &FMixtormatLayer::HeightBlendAmount, 0.0f, 4.0f, 0.01f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					NumericRow(LOCTEXT("HeightBlendThreshold", "Threshold"), &FMixtormatLayer::HeightThreshold, 0.0f, 1.0f, 0.01f, 0.5f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					NumericRow(LOCTEXT("HeightSoftness", "Softness"), &FMixtormatLayer::HeightRange, 0.0f, 1.0f, 0.005f, 0.1f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					NumericRow(LOCTEXT("BaseHeightBias", "Base Height Bias"), &FMixtormatLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
				[
					NumericRow(LOCTEXT("BlendHeightBias", "Blend Height Bias"), &FMixtormatLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightContrast", "Legacy Height Contrast"), &FMixtormatLayer::HeightContrast, 0.01f, 8.0f, 0.05f, 1.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightOffset", "Legacy Height Offset"), &FMixtormatLayer::HeightOffset, -1.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBias", "Legacy Comparison Bias"), &FMixtormatLayer::HeightBias, -1.0f, 1.0f, 0.01f, 0.0f)]
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SBox)
				.Visibility(EVisibility::Collapsed)
				[
					NumericRow(LOCTEXT("MaskHeightInfluence", "Mask Modulation (Compatibility)"), &FMixtormatLayer::MaskHeightInfluence, 0.0f, 1.0f, 0.01f, 0.0f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, MixtormatTokens::SliderRowGap)
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("HeightContactBorders", "CONTACT BORDERS"))
				.InitiallyExpanded(true)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("HeightContactAOGroup", "CONTACT AO")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontCaption))]
						+ SHorizontalBox::Slot().AutoWidth()
						[MakeFeaturePreviewButton(EMixtormatDebugPreviewMode::ContactAO, LOCTEXT("PreviewContactAO", "Preview Contact AO coverage in unlit dark red and cyan"))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightContactAOAmount", "Amount"), &FMixtormatLayer::HeightContactAOAmount, 0.0f, 1.0f, 0.01f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightContactAOWidth", "Width"), &FMixtormatLayer::HeightContactAOWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, MixtormatTokens::SliderRowGap)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(LOCTEXT("HeightBorderNormalGroup", "BORDER NORMAL")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontCaption))]
						+ SHorizontalBox::Slot().AutoWidth()
						[MakeFeaturePreviewButton(EMixtormatDebugPreviewMode::BorderNormal, LOCTEXT("PreviewBorderNormal", "Preview Border Normal coverage in unlit dark red and cyan"))]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightBorderLift", "Lift"), &FMixtormatLayer::HeightBorderLift, -1.0f, 1.0f, 0.005f, 0.0f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightBorderWidth", "Width"), &FMixtormatLayer::HeightBorderWidth, 0.0001f, 1.0f, 0.005f, 0.05f)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
					[NumericRow(LOCTEXT("HeightBorderNormalStrength", "Intensity"), &FMixtormatLayer::HeightBorderNormalStrength, 0.0f, 8.0f, 0.001f, 1.0f)]

					// Shared by both effects above, because both are built from the same field and
					// both stipple for the same reason: the height underneath carries detail at
					// every scale, and the field is differentiated at one texel and scaled by
					// OutputSize. Width cannot fix that -- Width is a softness in the height
					// domain, so it widens the band without changing what the band is made of.
					//
					// This blurs the height the field is derived from, with the same separable
					// Gaussian the mask smoothing uses. Only these two effects read the blurred
					// copy, and both fields are flat outside the band, so the smoothing is
					// confined to the contact width by construction rather than by a mask.
					+ SVerticalBox::Slot().AutoHeight()
					[NumericRow(LOCTEXT("HeightBorderSmoothing", "Smoothing"), &FMixtormatLayer::HeightBorderSmoothing, 1.0f, 32.0f, 0.25f, 1.0f)]
				]
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
	AddFloatControl(Panel, LOCTEXT("PeelingCurlLength", "Curl Length"), &FMixtormatLayerEffect::MicroMorph, 0.0f, 1.0f, 0.01f, 1.0f);
	AddFloatControl(Panel, LOCTEXT("PeelingThickness", "Thickness"), &FMixtormatLayerEffect::Thickness, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingLift", "Lift"), &FMixtormatLayerEffect::Lift, 0.0f, 1.0f, 0.005f, 0.04f);
	AddFloatControl(Panel, LOCTEXT("PeelingDetailStrength", "Detail Strength"), &FMixtormatLayerEffect::DetailStrength, 0.0f, 1.0f, 0.005f, 0.02f);


	const auto MakeEnabledToggle = [this](const FText& ToolTip)
	{
		// Was FAppStyle's ToggleSwitch -- an engine control in the middle of a Mixtormat panel.
		return SNew(SMixtormatToggle)
			.ToolTip(ToolTip)
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
			.InitiallyExpanded(true)
			.HeaderAction(MakeEnabledToggle(LOCTEXT("PeelingEnabled", "Enable Peeling")))
			[Panel]
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
		.WidthOverride(MixtormatTokens::InspectorWidth)
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
				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildInstanceBanner()
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					// Readable, not editable. The rows keep their values and their layout; only
					// the writing is taken away, which is what an instance means.
					.IsEnabled_Lambda([this]() { return !IsSelectedChildInstance(); })
					.Visibility_Lambda([this]()
					{
						return GetSelectedLayerEffect()
							|| GetSelectedGeneratedMask()
							|| GetSelectedLayerMask()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SScrollBox::Slot()[BuildEffectInspectorControls()]
					+ SScrollBox::Slot()[BuildProceduralPeelControls()]
					+ SScrollBox::Slot()[BuildStainControls()]
					+ SScrollBox::Slot()[BuildErosionControls()]
					+ SScrollBox::Slot()[BuildGradeControls()]
					+ SScrollBox::Slot()[BuildFlowWarpControls()]
					+ SScrollBox::Slot()[BuildChippingControls()]
					+ SScrollBox::Slot()[BuildWornEdgesControls()]
					+ SScrollBox::Slot()[BuildGeneratedMaskControls()]
					+ SScrollBox::Slot()[BuildLayerMaskControls()]
					+ SScrollBox::Slot()[BuildCraquelureControls()]
					+ SScrollBox::Slot()[BuildColorIdControls()]
					+ SScrollBox::Slot()[BuildFilterControls()]
					+ SScrollBox::Slot()[BuildPatternIdControls()]
					+ SScrollBox::Slot()[BuildHsvFilterControls()]
					+ SScrollBox::Slot()[BuildRandomIdControls()]
					+ SScrollBox::Slot()[BuildRampIdControls()]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					// The scroll box has to sit outside the group, not inside it. An expandable
					// area sizes its content to whatever that content asks for, so a scroll box
					// within one is handed unbounded height and never scrolls -- the layer
					// inspector simply ran off the bottom of the panel.
					SNew(SScrollBox)
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
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
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
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
						[
							SNew(SMixtormatChip)
							.Visibility_Lambda([this]() { return WorkingLayers.IsValidIndex(SelectedLayerIndex) && WorkingLayers[SelectedLayerIndex].ChannelMode == EMixtormatLayerChannelMode::NormalDetail ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this]() { if (!WorkingLayers.IsValidIndex(SelectedLayerIndex)) return LOCTEXT("NormalSource", "Choose Normal Source..."); const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex]; return Layer.NormalSourceType == EMixtormatNormalSourceType::Texture ? FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName()) : LOCTEXT("SurfaceNormal", "Surface Normal"); })
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
							.InitiallyExpanded(true)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
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
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillRoughness", "Roughness"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Roughness, 0.0, 1.0, 0.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("FillIOR", "IOR"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::IOR, 1.0, 3.0, 1.5, 0.01)
							]
														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
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
								return WorkingLayers.IsValidIndex(SelectedLayerIndex)
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
								// The mode and how far it is taken, on one row. Base colour only:
								// a mode that suits colour rarely suits roughness, and roughness
								// already has Bias, Contrast and Offset of its own.
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
								[
									MixtormatRow::MakePair(
										MixtormatRow::Make(
											LOCTEXT("BaseColorBlendLabel", "Color Blend"),
											MixtormatRow::MakeChip(
												TAttribute<FText>::CreateLambda([this]()
												{
													return WorkingLayers.IsValidIndex(SelectedLayerIndex)
														? MixtormatUI::ColorBlendModeText(
															WorkingLayers[SelectedLayerIndex].BaseColorBlendMode)
														: FText::GetEmpty();
												}),
												FOnGetContent::CreateSP(this, &SMixtormat::BuildBaseColorBlendModeMenu)),
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

														+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
							[
								MakeMemberSlider<FMixtormatLayer>(
									LOCTEXT("OpacityLabel", "Opacity"),
									[this]() -> FMixtormatLayer*
									{
										return WorkingLayers.IsValidIndex(SelectedLayerIndex) ? &WorkingLayers[SelectedLayerIndex] : nullptr;
									},
									&FMixtormatLayer::Opacity, 0.0, 1.0, 1.0, 0.01)
							]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, MixtormatTokens::CardGap, 0.0f, 0.0f)
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
		];
}

void SMixtormat::AddMaskShapingRows(
	const TSharedRef<SVerticalBox>& TargetPanel,
	TFunction<FMixtormatMaskShaping*()> Resolve)
{
	using namespace MixtormatMaskShapingRange;

	AddSliderRow(TargetPanel, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpShape", "Shaping")));
	AddSliderRow(TargetPanel, MakeMemberToggle<FMixtormatMaskShaping>(
		LOCTEXT("MaskInvertLabel", "Invert"), Resolve, &FMixtormatMaskShaping::bInvert));
	AddSliderRow(TargetPanel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskBalanceLabel", "Balance"), Resolve, &FMixtormatMaskShaping::Balance,
			BalanceMin, BalanceMax, BalanceDefault, SnapDelta,
			LOCTEXT("MaskBalanceHint", "Thins the mask toward black above the middle and thickens it toward white below, as a power curve -- so it erodes what is there rather than fading it out. The ends are aggressive but never flatten the mask.")),
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskContrastLabel", "Contrast"), Resolve, &FMixtormatMaskShaping::Contrast,
			ContrastMin, ContrastMax, ContrastDefault, SnapDelta,
			LOCTEXT("MaskContrastHint", "Hardens the transition about the mask's midpoint. Masks are stored raw rather than sRGB, so that midpoint really is the 50% grey you painted."))));
	AddSliderRow(TargetPanel, MakeMemberSlider<FMixtormatMaskShaping>(
		LOCTEXT("MaskOffsetLabel", "Offset"), Resolve, &FMixtormatMaskShaping::Offset,
		OffsetMin, OffsetMax, OffsetDefault, SnapDelta,
		LOCTEXT("MaskOffsetHint", "Lifts the whole mask after contrast. Plus one reaches full white and minus one full black from any input, whatever the contrast is set to.")));
}

#undef LOCTEXT_NAMESPACE
