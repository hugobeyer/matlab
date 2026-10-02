// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildProceduralPeelControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpSeeding", "Seeding")));

	// Seed Mask, not "Peel Mask" and emphatically not "Mask". Two different masks reach a peel and
	// they do opposite things:
	//
	//   Seed Mask   -- this one. It seeds the procedural growth: it is scaled by Mask Influence,
	//                  tested against Adhesion, and wherever it crosses, peeling *starts*. It
	//                  decides where the effect originates.
	//   Scoped mask -- a Mask child dragged under the Peeling row. That one gates the finished
	//                  effect: it decides where the result is allowed to show.
	//
	// Calling both "Mask" is what made the pair unreadable. They are deliberately not merged.
	// Kept as a bespoke row because it picks an asset, not a value.
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
	[
		SNew(SBox).HeightOverride(MixtormatTokens::RowHeight)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("PPeelSeedMaskSlot", "Seed Mask"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMixtormatChip)
				.ToolTip(LOCTEXT("PPeelSeedMaskSlotHint", "Where this peel starts. Mask Influence scales it, Adhesion is the threshold it has to cross, and procedural growth spreads outward from it. A scoped Mask child independently gates only the finished result."))
				.Text_Lambda([this]()
						{
							const FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
							if (!E)
							{
								return LOCTEXT("PPeelSeedMaskNone", "None");
							}
							if (!E->PeelMask.IsNull())
							{
								return FText::FromString(E->PeelMask.ToSoftObjectPath().GetAssetName());
							}
							if (!E->PeelMaskTexture.IsNull())
							{
								return FText::FromString(E->PeelMaskTexture.ToSoftObjectPath().GetAssetName());
							}
							return LOCTEXT("PPeelSeedMaskNone", "None");
						})
				.OnGetMenuContent_Lambda([this]()
				{
					return BuildMaskAssetPicker(
						[this](const FSoftObjectPath Path)
						{
							FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
							if (!E)
							{
								return;
							}
							// The registry lists UMixtormatMask assets and plain UTexture2D side
							// by side, so the pick has to branch on the loaded class.
							UObject* MaskObject = Path.TryLoad();
							if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
							{
								E->PeelMask = TSoftObjectPtr<UMixtormatMask>(Path);
								E->PeelMaskTexture =
									TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
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
						},
						[this](const FSoftObjectPath Path)
						{
							const FMixtormatLayerEffect* E = GetSelectedProceduralPeel();
							return E
								&& (E->PeelMask.ToSoftObjectPath() == Path
									|| E->PeelMaskTexture.ToSoftObjectPath() == Path);
						},
						SNew(SButton)
						.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
						.Text(LOCTEXT("PPeelSeedMaskClear", "None"))
						.ToolTipText(LOCTEXT("PPeelSeedMaskClearHint", "Clear the Seed Mask. No external mask contributes to seeding."))
						.OnClicked_Lambda([this]()
						{
							if (FMixtormatLayerEffect* E = GetSelectedProceduralPeel())
							{
								E->PeelMask.Reset();
								E->PeelMaskTexture.Reset();
								RefreshLayeredPreview();
							}
							return FReply::Handled();
						}));
				})
			]
		]
	];

	// Paired: both labels are one short word, and at the inspector's width each half is about
	// 139px. Anything longer would clip, which is why Adhesion's weights below are not paired.
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelSeedMaskTiling", "Seed Tiling"), &FMixtormatLayerEffect::PeelMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("PPeelSeedMaskInv", "Seed Invert"),
			[this]() { return GetSelectedProceduralPeel(); },
			&FMixtormatLayerEffect::bPeelMaskInvert)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpAdhesion", "Adhesion")));
	AddPeelSlider(Panel, LOCTEXT("PPeelMaskW", "Mask Influence"), &FMixtormatLayerEffect::PeelSeedMaskWeight, 0.0, 4.0, 0.0, 0.01,
		LOCTEXT("PPeelMaskWHint", "Scales the Seed Mask before the Adhesion threshold. At 0 nothing crosses it and there is no peel at all, whichever mask is chosen. This is a seeding weight -- it has no effect on a scoped mask gating the result."));
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
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSlider(LOCTEXT("PPeelFront", "Front"), &FMixtormatLayerEffect::Front, -1.0, 1.0, 0.08, 0.005),
		MakePeelSlider(LOCTEXT("PPeelWidth", "Width"), &FMixtormatLayerEffect::Width, 0.000001, 0.25, 0.015, 0.001)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpShape", "Peel Shape")));
	AddPeelSlider(Panel, LOCTEXT("PPeelStrength", "Strength"), &FMixtormatLayerEffect::Strength, 0.0, 1.0, 1.0, 0.01);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSlider(LOCTEXT("PPeelMacroWarp", "Macro Warp"), &FMixtormatLayerEffect::MacroWarp, -1.0, 1.0, 0.01, 0.005),
		MakePeelSlider(LOCTEXT("PPeelMicroWarp", "Micro Warp"), &FMixtormatLayerEffect::MicroWarp, -1.0, 1.0, 0.003, 0.001)));
	AddPeelSlider(Panel, LOCTEXT("PPeelCurlLength", "Curl Length"), &FMixtormatLayerEffect::MicroMorph, 0.0, 1.0, 1.0, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelLiftVar", "Lift Variation"), &FMixtormatLayerEffect::PeelLiftVariation, 0.0, 1.0, 0.6, 0.01);
	AddPeelSlider(Panel, LOCTEXT("PPeelCornerLift", "Corner Lift"), &FMixtormatLayerEffect::PeelCornerLift, 0.0, 1.0, 0.6, 0.01,
		LOCTEXT("PPeelCornerLiftHint", "Extra lift on corners and tongues of the remaining sheet. Measured by the structure tensor of the peel front, so it responds to the gradient direction actually changing rather than to curvature -- a long gentle arc is not a corner."));
	AddPeelSlider(Panel, LOCTEXT("PPeelCornerRadius", "Corner Radius"), &FMixtormatLayerEffect::PeelCornerRadius, 0.05, 4.0, 1.0, 0.05,
		LOCTEXT("PPeelCornerRadiusHint", "Size of the window the corner detector looks through, as a multiple of the curl length. Wider responds to broader features and spreads the boost further back from the tip, so bigger pieces of sheet lift rather than only their sharpest points."));
	AddPeelSlider(Panel, LOCTEXT("PPeelIDInfluence", "ID Influence"), &FMixtormatLayerEffect::PeelIDInfluence, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("PPeelIDInfluenceHint", "Bias the peel toward the boundaries of the ID map above it -- a cluster filter, a pattern, random or colour IDs. A weight rather than a gate: low values make the peel prefer seams, high values confine it to them. 0 ignores the ID map entirely."));
	AddPeelSlider(Panel, LOCTEXT("PPeelSizeVar", "Size Variation"), &FMixtormatLayerEffect::PeelSizeVariation, 0.0, 1.0, 0.5, 0.01,
		LOCTEXT("PPeelSizeVarHint", "Per-cell speed factor. Set this to 0 as well as the adhesion weights to check that the field dilates uniformly."));
	AddPeelSliderInt(Panel, LOCTEXT("PPeelDamageScale", "Damage Scale"), &FMixtormatLayerEffect::PeelClusterPeriod, 1.0, 128.0, 4,
		LOCTEXT("PPeelDamageScaleHint", "Cell count for per-flake variation. One random value per cell, so adjacent flakes differ in size and lift."));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("PPeelType", "Peel Type"), [this]() { return GetSelectedProceduralPeel(); },
		&FMixtormatLayerEffect::PeelType,
		LOCTEXT("PPeelTypeHint", "Curled lifts a flap ahead of the front and folds it back behind. Flat is the chip.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("PPeelGrpRelief", "Relief")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSlider(LOCTEXT("PPeelThickness", "Thickness"), &FMixtormatLayerEffect::Thickness, 0.0, 1.0, 0.04, 0.005),
		MakePeelSlider(LOCTEXT("PPeelLift", "Lift"), &FMixtormatLayerEffect::Lift, 0.0, 1.0, 0.2, 0.01)));
	AddPeelSlider(Panel, LOCTEXT("PPeelDetailStrength", "Detail Strength"), &FMixtormatLayerEffect::DetailStrength, 0.0, 1.0, 0.02, 0.005);
	AddPeelSlider(Panel, LOCTEXT("PPeelSharp", "Edge Sharpness"), &FMixtormatLayerEffect::PeelEdgeSharpness, 0.0, 4.0, 1.0, 0.01);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakePeelSliderInt(LOCTEXT("PPeelSeed", "Seed"), &FMixtormatLayerEffect::PeelRandomSeed, 1.0, 999.0, 1),
		MakePeelSliderInt(LOCTEXT("PPeelSolveDiv", "Solve"), &FMixtormatLayerEffect::PeelSolveDivisor, 1.0, 32.0, 4,
			LOCTEXT("PPeelSolveDivHint", "Divides the resolution the front is solved at. Higher is much cheaper; 1 solves at full composition resolution. The solve never drops below 64 on a side, so past that point raising this does nothing."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedProceduralPeel() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ProcPeelHeading", "PEELING"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
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
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("StainMode", "Output Mask"), Stain, &FMixtormatLayerEffect::StainMode,
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

TSharedRef<SWidget> SMixtormat::BuildRunoffControls()
{
	const auto Runoff = [this]() { return GetSelectedRunoff(); };
	const auto Slider = [this, Runoff](
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(
			Label, Runoff, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RunoffGrpStreak", "Streak")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RunoffGravityAngle", "Gravity Angle"),
			&FMixtormatLayerEffect::RunoffGravityAngle, -180.0, 180.0, -90.0, 1.0,
			LOCTEXT("RunoffGravityAngleHint", "Which way runoff runs, in degrees. -90 is straight down the texture. Runoff is strictly one-directional and never spreads sideways off this axis, which is what separates it from the Stain transport solve.")),
		Slider(LOCTEXT("RunoffStreakRadius", "Streak Radius"),
			&FMixtormatLayerEffect::RunoffStreakRadius, 8.0, 512.0, 320.0, 1.0,
			LOCTEXT("RunoffStreakRadiusHint", "How far the strongest source reaches, in texels at 1K. Read as a fraction of the texture, so the same number is the same streak at 1K, 2K and 4K. Weaker mask values reach proportionally less, which is what makes the incoming mask a length control rather than only an opacity."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("RunoffStreakSoftness", "Streak Softness"),
		&FMixtormatLayerEffect::RunoffStreakSoftness, 0.05, 1.0, 0.46, 0.01,
		LOCTEXT("RunoffStreakSoftnessHint", "How gradually a run fades over its length. Low values keep it tight and stop it abruptly; high values let it fade out over most of its reach. It also widens the terminal lip, because a soft run deposits over a longer stretch than a sharp one.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RunoffGrpSource", "Source")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("RunoffSurfaceInfluence", "Surface Influence"),
		&FMixtormatLayerEffect::RunoffSurfaceInfluence, 0.0, 1.0, 0.95, 0.01,
		LOCTEXT("RunoffSurfaceInfluenceHint", "How much the height underneath decides where runoff starts. At 1 only cavities and the upper edges of ledges source it, which is where dirt collects. At 0 the height is ignored and the incoming mask alone is the source, for streaking from a painted mark on a flat surface.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RunoffGrpStrata", "Strata")));
	AddSliderRow(Panel, Slider(
		LOCTEXT("RunoffStrataAmount", "Strata Amount"),
		&FMixtormatLayerEffect::RunoffStrataAmount, 0.0, 1.0, 0.75, 0.01,
		LOCTEXT("RunoffStrataAmountHint", "How strongly the stacked layers read as separate deposits. Not a count: it widens their spacing, spreads their lengths and flattens their opacity falloff together, so 0 is one coherent run and 1 is a visibly layered buildup. The count itself follows from Streak Radius, since a short run has no room to show five strata.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RunoffWarpScale", "Warp Scale"),
			&FMixtormatLayerEffect::RunoffWarpScale, 1.0, 32.0, 18.0, 1.0,
			LOCTEXT("RunoffWarpScaleHint", "Feature size of the internal noise, as cells across the texture. The same field breaks up the source before the smear and warps each stratum's length, so this one control sets the grain of the whole effect.")),
		Slider(LOCTEXT("RunoffWarpAmount", "Warp Amount"),
			&FMixtormatLayerEffect::RunoffWarpAmount, 0.0, 2.0, 1.5, 0.01,
			LOCTEXT("RunoffWarpAmountHint", "How far that noise pushes each stratum's endpoint along gravity, and only along gravity. Above 1 the strata pull apart far enough to read as independent runs from the same source."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("RunoffLipStrength", "Lip Strength"),
		&FMixtormatLayerEffect::RunoffLipStrength, 0.0, 1.0, 0.55, 0.01,
		LOCTEXT("RunoffLipStrengthHint", "The narrow deposit left where a run stops, the way a drying streak leaves a tidemark. 0 ends every run on a clean fade; 1 puts a defined crust at the end of each stratum. Its width follows Streak Softness.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("RunoffGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RunoffStrength", "Strength"),
			&FMixtormatLayerEffect::RunoffStrength, 0.0, 1.0, 0.25, 0.01,
			LOCTEXT("RunoffStrengthHint", "Weight of the resolved runoff in the layer's mask chain. 0 is the identity.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("RunoffSeed", "Seed"), Runoff,
			&FMixtormatLayerEffect::RunoffSeed, 0.0, 9999.0, 1)));

	// No Shade group, for the same reason Stain has none: Runoff resolves a layer mask and shades
	// nothing. The layer it masks supplies colour, roughness, normal and height.
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedRunoff() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RunoffHeading", "RUNOFF"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MakeFeaturePreviewButton(
					EMixtormatDebugPreviewMode::Runoff,
					LOCTEXT("PreviewRunoff", "Preview the resolved runoff coverage before Strength blends it into the mask chain")))
			[
				Panel
			]
		];
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
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("GradeTonemap", "Tonemap"), [this]() { return GetSelectedGrade(); },
		&FMixtormatLayerEffect::GradeTonemap,
		LOCTEXT("GradeTonemapHint", "Applies a tonemap to the graded base color.")));
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

TSharedRef<SWidget> SMixtormat::BuildLayerBlurControls()
{
	const auto Blur = [this]() { return GetSelectedLayerBlurEffect(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("LayerBlurScopeLabel", "Scope"), Blur, &FMixtormatLayerEffect::LayerBlurScope,
		LOCTEXT("LayerBlurScopeHint", "Blurs the accumulated composite. Layer Coverage intersects layer coverage with scoped masks. Whole Composite uses scoped masks only, or affects everything when none are present.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("LayerBlurRadiusXLabel", "Radius X"), Blur, &FMixtormatLayerEffect::LayerBlurRadiusX,
			0.0, 32.0, 4.0, 0.1,
			LOCTEXT("LayerBlurRadiusXHint", "Horizontal softening in texels at the composition resolution. Zero skips the pass for that axis rather than running a one-tap identity, so X alone smears sideways and leaves verticals crisp.")),
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("LayerBlurRadiusYLabel", "Radius Y"), Blur, &FMixtormatLayerEffect::LayerBlurRadiusY,
			0.0, 32.0, 4.0, 0.1,
			LOCTEXT("LayerBlurRadiusYHint", "Vertical softening, same units. Equal to Radius X this is an ordinary Gaussian; unequal it is anisotropic."))));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(
			LOCTEXT("LayerBlurAmountLabel", "Amount"), Blur, &FMixtormatLayerEffect::LayerBlurAmount,
			0.0, 1.0, 1.0, 0.01,
			LOCTEXT("LayerBlurAmountHint", "Lerps against the unblurred surface, so zero returns exactly what it read and the passes are skipped outright.")),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("LayerBlurHeightLabel", "Blur Height"), Blur, &FMixtormatLayerEffect::bLayerBlurHeight,
			LOCTEXT("LayerBlurHeightHint", "Includes height in the blur. Off by preference when you want the surface to look softer without becoming softer: height feeds displacement, the height blend between layers, and the normals derived from it, so softening it changes what the surface is rather than only how it reads."))));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerBlurEffect() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("LayerBlurHeading", "LAYER BLUR"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
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

	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("FlowWarpBlend", "Blend"), Flow, &FMixtormatLayerEffect::FlowWarpBlendMode,
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

TSharedRef<SWidget> SMixtormat::BuildBreakupControls()
{
	const auto Breakup = [this]() { return GetSelectedBreakup(); };
	const auto Slider = [this, Breakup](
		const FText& Label, float FMixtormatLayerEffect::* Member,
		const double Min, const double Max, const double Default, const double Snap,
		const FText& Hint = FText::GetEmpty())
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(
			Label, Breakup, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpShape", "Shape")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupScale", "Scale"), Breakup,
			&FMixtormatLayerEffect::BreakupScale, 1.0, 64.0, 6),
		Slider(LOCTEXT("BreakupDensity", "Density"), &FMixtormatLayerEffect::BreakupDensity,
			0.0, 1.0, 0.72, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupSize", "Size"), &FMixtormatLayerEffect::BreakupSize,
			0.001, 1.0, 0.32, 0.005),
		Slider(LOCTEXT("BreakupStretch", "Stretch"), &FMixtormatLayerEffect::BreakupStretch,
			0.05, 4.0, 1.6, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupAngularity", "Angularity"), &FMixtormatLayerEffect::BreakupAngularity,
			0.0, 1.0, 0.72, 0.01),
		Slider(LOCTEXT("BreakupIrregularity", "Irregularity"), &FMixtormatLayerEffect::BreakupIrregularity,
			0.0, 1.0, 0.38, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupInset", "Inset"), &FMixtormatLayerEffect::BreakupInset,
			-64.0, 64.0, 0.0, 0.25,
			LOCTEXT("BreakupInsetHint", "Positive shrinks pieces and opens spacing; negative grows them before relief.")),
		Slider(LOCTEXT("BreakupDistortion", "Distortion"), &FMixtormatLayerEffect::BreakupDistortion,
			0.0, 32.0, 5.6, 0.1,
			LOCTEXT("BreakupDistortionHint", "Seamless multi-scale domain warp; no repeating sine-wave deformation."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpStructure", "Structure")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupRelief", "Relief"), &FMixtormatLayerEffect::BreakupRelief,
			-0.5, 0.5, -0.06, 0.0025,
			LOCTEXT("BreakupReliefHint", "Signed interior level: negative tears/carves; positive builds plates and rock.")),
		Slider(LOCTEXT("BreakupThicknessVariation", "Thickness Var"), &FMixtormatLayerEffect::BreakupThicknessVariation,
			0.0, 1.0, 0.30, 0.01,
			LOCTEXT("BreakupThicknessVariationHint", "Stable per-piece relief thickness variation from Breakup IDs."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupGapWidth", "Gap Width"), &FMixtormatLayerEffect::BreakupGapWidth,
			0.0, 64.0, 2.0, 0.1,
			LOCTEXT("BreakupGapWidthHint", "Opens seams at both SDF boundaries and internal piece-ID boundaries.")),
		Slider(LOCTEXT("BreakupGapDepth", "Gap Depth"), &FMixtormatLayerEffect::BreakupGapDepth,
			0.0, 0.5, 0.02, 0.001)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupFold", "Fold"), &FMixtormatLayerEffect::BreakupFold,
			0.0, 0.5, 0.025, 0.0025),
		Slider(LOCTEXT("BreakupCrease", "Crease"), &FMixtormatLayerEffect::BreakupCrease,
			0.0, 0.5, 0.018, 0.001)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupPush", "Push"), &FMixtormatLayerEffect::BreakupPush,
			-128.0, 128.0, 0.0, 0.25,
			LOCTEXT("BreakupPushHint", "Signed height advection along the SDF gradient. Now also drives structural separation.")),
		Slider(LOCTEXT("BreakupPushRelief", "Push Relief"), &FMixtormatLayerEffect::BreakupPushRelief,
			0.0, 0.5, 0.035, 0.001,
			LOCTEXT("BreakupPushReliefHint", "Adds real height separation so Push remains visible on a flat source."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpVariation", "Variation")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupDetail", "Detail"), &FMixtormatLayerEffect::BreakupDetail,
			0.0, 1.0, 0.5, 0.01),
		Slider(LOCTEXT("BreakupVariation", "Variation"), &FMixtormatLayerEffect::BreakupVariation,
			0.0, 1.0, 0.25, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupGapVariation", "Gap Variation"), &FMixtormatLayerEffect::BreakupGapVariation,
			0.0, 1.0, 0.35, 0.01),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupSeed", "Seed"), Breakup,
			&FMixtormatLayerEffect::BreakupSeed, 0.0, 9999.0, 1)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpShading", "Shading")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupNormalStrength", "Normal"), &FMixtormatLayerEffect::BreakupNormalStrength,
			0.0, 4.0, 2.0, 0.05,
			LOCTEXT("BreakupNormalStrengthHint", "Strength of the normal contribution derived from Breakup's actual height delta.")),
		Slider(LOCTEXT("BreakupNormalSharpness", "Sharpness"), &FMixtormatLayerEffect::BreakupNormalSharpness,
			0.0, 1.0, 0.75, 0.01,
			LOCTEXT("BreakupNormalSharpnessHint", "Blends broad fold normals toward a sharp one-pixel structural gradient."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("BreakupRoughness", "Roughness"), &FMixtormatLayerEffect::BreakupRoughnessAmount,
		-1.0, 1.0, 0.0, 0.01));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpAdvanced", "Advanced")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupSizeVariation", "Size Variation"), &FMixtormatLayerEffect::BreakupSizeVariation,
			0.0, 1.0, 0.3125, 0.01),
		Slider(LOCTEXT("BreakupSmoothness", "Smoothness"), &FMixtormatLayerEffect::BreakupSmoothness,
			0.0, 1.0, 0.30, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupDistortionFrequency", "Distort Scale"), Breakup,
			&FMixtormatLayerEffect::BreakupDistortionFrequency, 1.0, 16.0, 3),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("BreakupInvert", "Invert"), Breakup,
			&FMixtormatLayerEffect::bBreakupInvert)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupFoldWidth", "Fold Width"), &FMixtormatLayerEffect::BreakupFoldWidth,
			0.001, 64.0, 16.0, 0.25),
		Slider(LOCTEXT("BreakupCreaseWidth", "Crease Width"), &FMixtormatLayerEffect::BreakupCreaseWidth,
			0.001, 32.0, 1.25, 0.05)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("BreakupPushWidth", "Push Width"), &FMixtormatLayerEffect::BreakupPushWidth,
		0.001, 64.0, 24.0, 0.5));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupAmount", "Amount"), &FMixtormatLayerEffect::BreakupAmount,
			0.0, 1.0, 1.0, 0.01,
			LOCTEXT("BreakupAmountHint", "Height/shading amount. The generated IDs remain available at zero so Breakup can be used as an ID-only producer.")),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupMaskTiling", "Mask Tiling"), Breakup,
			&FMixtormatLayerEffect::BreakupMaskTiling, 1.0, 16.0, 1)));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatLayerEffect>(
		LOCTEXT("BreakupMaskInvert", "Invert Mask"), Breakup,
		&FMixtormatLayerEffect::bBreakupInvertMask));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedBreakup() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("BreakupHeading", "BREAKUP"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MakeChildOutputPreviewButton(
					GetPreviewOutputSetForEffectType(EMixtormatEffectType::Breakup)))
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
			.HeaderAction(
				MakeChildOutputPreviewButton(
					GetPreviewOutputSetForEffectType(EMixtormatEffectType::WornEdges)))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildErosionControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpFilter", "Filter")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroAmount", "Amount"), &FMixtormatLayerEffect::ErosionAmount, 0.0, 8.0, 1.5, 0.01,
			LOCTEXT("EroAmountHint", "Overall wear strength: how aggressively exposed peaks are shaved and valleys refill. 1 is clearly visible, 4+ is destructive, 8 is an extreme testing range. Zero is an exact pass-through and skips the effect.")),
		MakeErosionSlider(LOCTEXT("EroDepth", "Depth"), &FMixtormatLayerEffect::ErosionDepth, 0.0, 2.0, 1.0, 0.01,
			LOCTEXT("EroDepthHint", "How deeply the generated wear modifies the material relief, separate from how aggressively it is generated. The result remains subtractive overall."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpWear", "Wear")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSliderInt(LOCTEXT("EroRadius", "Radius"), &FMixtormatLayerEffect::ErosionRadius, 1.0, 3.0, 1,
			LOCTEXT("EroRadiusHint", "Derivative span in texels for the slope and curvature readings. 1 is the normal working value at the doubled erosion resolution; 2-3 read broader structure. Cost is fixed whatever the span.")),
		MakeErosionSliderInt(LOCTEXT("EroIterations", "Iterations"), &FMixtormatLayerEffect::ErosionIterations, 1.0, 16.0, 8,
			LOCTEXT("EroIterationsHint", "Ping-pong wear passes. Each pass analyses the previous pass's output, so wear propagates and deepens with count: 1 is a single local pass, 8 a mature result."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroGravityForce", "Gravity Force"), &FMixtormatLayerEffect::ErosionGravityForce, 0.0, 1.0, 0.6, 0.01,
			LOCTEXT("EroGravityForceHint", "Constant downhill force toward -Y, as a weight over the local slope. 0 follows terrain alone, 1 streaks straight down. Material only ever moves downhill, so flat joints and ledges survive at any value.")),
		MakeErosionSlider(LOCTEXT("EroSmoothing", "Smoothing"), &FMixtormatLayerEffect::ErosionSmoothing, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("EroSmoothingHint", "Flow momentum: how much direction memory the solver keeps between iterations, so surface noise averages out instead of steering the wear. The height itself is never blurred."))));
	AddSliderRow(Panel, MakeErosionSlider(LOCTEXT("EroSlopePower", "Slope Power"), &FMixtormatLayerEffect::ErosionSlopePower, 0.1, 1.0, 1.0, 0.01,
			LOCTEXT("EroSlopePowerHint", "Below 1 responds broadly to gentle slopes, 1 keeps the raw slope response.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroDeposit", "Deposit"), &FMixtormatLayerEffect::ErosionDeposit, 0.0, 4.0, 0.25, 0.01,
			LOCTEXT("EroDepositHint", "How much removed material refills valleys and depressions, and the strength of the downstream deposit tail left below eroded runs. Upward-facing ledges catch more with Gravity Force. Zero is pure erosion.")),
		MakeErosionSlider(LOCTEXT("EroPreserveFlats", "Preserve Flats"), &FMixtormatLayerEffect::ErosionPreserveFlats, 0.0, 0.5, 0.002, 0.001,
			LOCTEXT("EroPreserveFlatsHint", "Slope threshold below which wear is suppressed, in normalized gradient space (height change per 1/256 of the tile). Zero leaves every region eligible; higher values protect increasingly flat ones."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroVariation", "Variation"), &FMixtormatLayerEffect::ErosionVariation, 0.0, 1.0, 0.18, 0.01,
			LOCTEXT("EroVariationHint", "Seeded wear-strength variation across the tile, so the carve breaks up like material hardness patches instead of eroding uniformly. Zero is uniform.")),
		MakeErosionSliderInt(LOCTEXT("EroSeed", "Seed"), &FMixtormatLayerEffect::ErosionSeed, 0.0, 9999.0, 1,
			LOCTEXT("EroSeedHint", "Seeds the variation field. Same seed, same variation, at any resolution."))));


	// Erosion contributes no base colour. Its resolved mask only weights surface channels.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("EroGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeErosionSlider(LOCTEXT("EroRoughAmount", "Roughness"), &FMixtormatLayerEffect::ErosionRoughnessAmount, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("EroRoughAmountHint", "Signed, mask-weighted offset on composited roughness; positive moves toward rough.")),
		MakeErosionSlider(LOCTEXT("EroCarveDepth", "Full At Depth"), &FMixtormatLayerEffect::ErosionCarveDepth, 0.001, 1.0, 0.05, 0.001,
			LOCTEXT("EroCarveDepthHint", "Carve depth that reaches the full roughness weight."))));


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

#undef LOCTEXT_NAMESPACE
