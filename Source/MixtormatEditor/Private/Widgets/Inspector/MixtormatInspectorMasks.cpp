// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatTile.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

extern const EMixtormatUVRotation GMixtormatUVRotations[4];

namespace
{
	const EMixtormatMaskSource GMixtormatMaskSources[] = {
		EMixtormatMaskSource::Texture,
		EMixtormatMaskSource::LayerValues
	};

	const EMixtormatLayerValueChannel GMixtormatLayerValueChannels[] = {
		EMixtormatLayerValueChannel::Luminance,
		EMixtormatLayerValueChannel::Red,
		EMixtormatLayerValueChannel::Green,
		EMixtormatLayerValueChannel::Blue,
		EMixtormatLayerValueChannel::Roughness
	};


}


extern const EMixtormatUVRotation GMixtormatUVRotations[4];

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

TSharedRef<SWidget> SMixtormat::BuildMaskAssetPicker(
	TFunction<void(FSoftObjectPath)> OnPicked,
	TFunction<bool(FSoftObjectPath)> IsSelected,
	TSharedPtr<SWidget> Footer)
{
	// A grid, not a list. Masks are images, and picking "Grunge_Fine" over "Grunge_Coarse" by
	// filename meant assigning one, looking at the viewport and coming back. The popover is wider
	// than the 300px inspector on purpose: a menu is its own window and is not clipped by the
	// panel that opened it.
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
			.bSelected_Lambda([IsSelected, Path]() { return IsSelected(Path); })
			.OnActivated(FMixtormatOnTileActivated::CreateLambda([OnPicked, Path]()
			{
				OnPicked(Path);
			}))
		];
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(MixtormatTokens::InspectorMaskGalleryMaxHeight)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[Grid]
		];
	if (Footer.IsValid())
	{
		Body->AddSlot()
		.AutoHeight()
		.Padding(0.0f, MixtormatTokens::TileGap, 0.0f, 0.0f)
		[
			Footer.ToSharedRef()
		];
	}

	return SNew(SBox)
		.WidthOverride(MixtormatTokens::MaskPickerWidth)
		.Padding(MixtormatTokens::TileGap)
		[
			Body
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
	// The inspector is built once, before anything is selected, so the Surface IDs / legacy
	// Cluster split cannot be decided here. Both panels are built and each shows only while the
	// selected producer is its kind.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildRegionFilterPanel(true)]
		+ SVerticalBox::Slot().AutoHeight()[BuildRegionFilterPanel(false)];
}

TSharedRef<SWidget> SMixtormat::BuildRegionFilterPanel(const bool bSurfaceIds)
{
	const auto Filter = [this]() { return GetSelectedFilter(); };

	// Integer producers do not use mask blending or weight. Surface guides are mixed before IDs exist.
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
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
			FOnGetContent::CreateSP(this, &SMixtormat::BuildClusterSourceMenu), nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("SurfaceIdSourceHint", "Layer Surface reads this layer's maps through its UV transform. Composite Below reads the accumulated surface beneath it without retilling. On the bottom layer, the layer's own maps are used.")));

	if (bSurfaceIds)
	{
		const auto FeatureRow = [this](const FText& Label,
			EMixtormatSurfaceIdFeature FMixtormatClusterFilter::* Member)
		{
			return MixtormatRow::MakeDropdown(Label, MixtormatRow::MakeChip(
				TAttribute<FText>::CreateLambda([this, Member]()
				{
					const FMixtormatClusterFilter* C = GetSelectedFilter();
					return C ? StaticEnum<EMixtormatSurfaceIdFeature>()->GetDisplayNameTextByValue(
						static_cast<int64>(C->*Member)) : FText::GetEmpty();
				}),
				FOnGetContent::CreateLambda([this, Member]()
				{
					MixtormatMenu::FBuilder Menu;
					for (int64 Value = 0; Value <= static_cast<int64>(EMixtormatSurfaceIdFeature::Metallic); ++Value)
					{
						const auto Feature = static_cast<EMixtormatSurfaceIdFeature>(Value);
						Menu.Item(StaticEnum<EMixtormatSurfaceIdFeature>()->GetDisplayNameTextByValue(Value),
							nullptr, FSimpleDelegate::CreateLambda([this, Member, Feature]()
							{
								if (FMixtormatClusterFilter* C = GetSelectedFilter())
								{
									C->*Member = Feature;
									RefreshLayeredPreview();
								}
							}))
							.Checked(TAttribute<bool>::CreateLambda([this, Member, Feature]()
							{
								const FMixtormatClusterFilter* C = GetSelectedFilter();
								return C && C->*Member == Feature;
							}));
					}
					return Menu.Build();
				}), nullptr, TAttribute<FText>(), 0.0f),
				LOCTEXT("SurfaceIdFeatureHint", "Choose a continuous surface guide. Curvature reads height; Normal Flatness measures neighboring normal agreement, including tilted flat patches. Matching bands share an ID even across disconnected areas."));
		};
		AddSliderRow(Panel, MixtormatRow::MakePair(
			FeatureRow(LOCTEXT("SurfaceIdsPrimary", "Primary"), &FMixtormatClusterFilter::PrimaryFeature),
			FeatureRow(LOCTEXT("SurfaceIdsSecondary", "Secondary"), &FMixtormatClusterFilter::SecondaryFeature)));
		AddSliderRow(Panel, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatClusterFilter>(LOCTEXT("SurfaceIdsMix", "Mix"),
				Filter, &FMixtormatClusterFilter::FeatureMix, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("SurfaceIdsMixHint", "Mix normalized guides before quantizing: 0 is Primary, 1 is Secondary. Integer IDs are never blended.")),
			MakeMemberSliderInt<FMixtormatClusterFilter>(LOCTEXT("SurfaceIdsBudget", "Max IDs"),
				Filter, &FMixtormatClusterFilter::MaxIds, 2.0, 256.0, 64,
				LOCTEXT("SurfaceIdsBudgetHint", "Maximum occupied bands, not a target island count. Unused bands collapse to consecutive IDs with no hash collisions. Changing the guides can renumber IDs."))));
		AddSliderRow(Panel, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatClusterFilter>(LOCTEXT("SurfaceIdsScale", "Form Scale"),
				Filter, &FMixtormatClusterFilter::FormScale, 1.0, 64.0, 4,
				LOCTEXT("SurfaceIdsScaleHint", "Sampling radius in output pixels for curvature and normal flatness. A sparse stencil, not a dense convolution. Direct height, color and material channels do not use this radius.")),
			MakeMemberSliderInt<FMixtormatClusterFilter>(LOCTEXT("SurfaceIdsBlur", "Guide Blur"),
				Filter, &FMixtormatClusterFilter::GuideBlur, 0.0, 2.0, 1,
				LOCTEXT("SurfaceIdsBlurHint", "0, 1 or 2 pixel smoothing before feature extraction. Only continuous source samples are blurred, never IDs."))));
		AddSliderRow(Panel, MixtormatRow::MakeTrailing(
			LOCTEXT("SurfaceIdsSplitIslands", "Split Islands"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatClusterFilter* C = GetSelectedFilter();
					return C && C->bSplitIslands ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
				{
					if (FMixtormatClusterFilter* C = GetSelectedFilter())
					{
						C->bSplitIslands = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("SurfaceIdsSplitIslandsHint", "Each connected island gets its own ID. Off: every area in the same band shares one ID, even where the areas do not touch.")));
		AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatClusterFilter>(
			LOCTEXT("SurfaceIdsClose", "Edge Close"), Filter, &FMixtormatClusterFilter::EdgeClose,
			0.0, 2.0, 1,
			LOCTEXT("SurfaceIdsCloseHint", "Closes small dark breaks in the guide at ID edges using dilation then erosion. 0 disables closing. A final 3x3 strict-majority cleanup removes edge speckles without changing region interiors or inventing IDs.")));
	}
	else
	{
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
	}

	return SNew(SBox)
		.Visibility_Lambda([this, bSurfaceIds]()
		{
			const FMixtormatClusterFilter* Selected = GetSelectedFilter();
			return Selected && Selected->bSurfaceIds == bSurfaceIds ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(bSurfaceIds ? LOCTEXT("SurfaceIdsHeading", "SURFACE IDS") : LOCTEXT("ClusterFilterHeading", "CLUSTER IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::Filter))
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
						LOCTEXT("RegionFilterEnabledHint", "Enable this ID producer"))
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

	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
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
			FOnGetContent::CreateSP(this, &SMixtormat::BuildCraquelureModeMenu), nullptr, TAttribute<FText>(), 0.0f),
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

	const TSharedRef<SVerticalBox> GrowCards = GrowGroup;
	GrowGroup = AddCard(GrowCards, LOCTEXT("CraqGrpGrowth", "Growth"));
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

	GrowGroup = AddCard(GrowCards, LOCTEXT("CraqGrpAdvanced", "Advanced"));
	AddSliderRow(GrowGroup, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqThreshold", "Threshold"), &FMixtormatCraquelure::GrowthThreshold, 0.0, 3.0, 0.55, 0.01,
			LOCTEXT("CraqThresholdHint", "The score a step has to beat to happen at all. Raising it starves growth, which is what decides how much of the surface ends up cracked.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqCollision", "Collision"), Craq, &FMixtormatCraquelure::CollisionLimit, 1.0, 8.0, 2,
			LOCTEXT("CraqCollisionHint", "Cracked neighbours a pixel may already have and still be grown into. At 2 a crack reaching an older one stops, because the older crack already released the stress driving it -- that is the right-angle junction of a drying film. Raise it and cracks cross, which reads as scratches rather than fracture."))));

	Panel->AddSlot().AutoHeight()[GrowCards];

	// Always shown rather than behind an output mode. Relief height also drives its normal.
	const TSharedRef<SVerticalBox> ReliefGroup = AddCard(Panel, LOCTEXT("CraqGrpRelief", "Relief"));
	AddSliderRow(ReliefGroup,
		Slider(LOCTEXT("CraqReliefDepth", "Height"), &FMixtormatCraquelure::ReliefDepth, 0.0, 0.5, 0.04, 0.001,
			LOCTEXT("CraqReliefDepthHint", "How deep the crack cuts into the composited height and its derived normal. The groove is a cone on the distance to the crack -- the eikonal solution, so its wall has one constant slope -- subtracted under a minimum, so this can only lower the height. 0 skips the pass.")));
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
	const TSharedRef<SVerticalBox> Cards = Panel;
	Panel = AddCard(Cards, LOCTEXT("CraqGrpWarp", "Warp"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("CraqWarp", "Amount"), &FMixtormatCraquelure::Warp, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CraqWarpHint", "Bends the finished network. Applied where the crack field is read rather than to how it grows, so it costs one pass and rebuilds nothing -- dragging this is a cache hit, unlike every control above it. Periodic curl noise, which is divergence-free and wraps on its own period, so the result still tiles.")),
		MakeMemberSliderInt<FMixtormatCraquelure>(
			LOCTEXT("CraqWarpScale", "Scale"), Craq, &FMixtormatCraquelure::WarpScale, 1.0, 32.0, 4,
			LOCTEXT("CraqWarpScaleHint", "Size of the swirls doing the bending. Below the crack Scale it bends whole regions; above it, it roughens individual cracks."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatCraquelure>(
		LOCTEXT("CraqSeed", "Seed"), Craq, &FMixtormatCraquelure::Seed, 0.0, 64.0, 1,
		LOCTEXT("CraqSeedHint", "Reshuffles the crack network without changing its scale or density. The warp is seeded off this too, so reseeding moves both rather than leaving a second seed to remember.")));

	Panel = AddCard(Cards, LOCTEXT("CraqGrpBlend", "Blend"));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("CraqBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatCraquelure* C = GetSelectedCraquelure();
				return C ? MixtormatUI::MaskBlendModeText(C->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildCraquelureBlendModeMenu), nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("CraqBlendModeHint", "How the crack network combines with the mask accumulated above it in this layer.")));

	AddSliderRow(Panel, Slider(LOCTEXT("CraqWeight", "Weight"), &FMixtormatCraquelure::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("CraqWeightHint", "How far the blend is taken. 0 is the off switch for this node.")));

	// The shared shaping block, so Invert / Balance / Contrast / Offset behave here exactly as they
	// do on a texture mask instead of being a fourth hand-written copy with its own ranges.
	AddMaskShapingRows(Cards, [this]() -> FMixtormatMaskShaping*
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
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildMaskSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatMaskSource Source : GMixtormatMaskSources)
	{
		Menu.Item(
			MixtormatUI::MaskSourceText(Source),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Source]()
			{
				if (FMixtormatMaskLayer* M = GetSelectedLayerMask())
				{
					M->Source = Source;
					RefreshLayeredPreview();

					// The row shows the source rather than the asset name once this changes, and
					// the badge follows it, so the list has to be rebuilt and not just redrawn.
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Source]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M && M->Source == Source;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskLayerValueChannelMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatLayerValueChannel Channel : GMixtormatLayerValueChannels)
	{
		Menu.Item(
			MixtormatUI::LayerValueChannelText(Channel),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Channel]()
			{
				if (FMixtormatMaskLayer* M = GetSelectedLayerMask())
				{
					M->LayerValueChannel = Channel;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Channel]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M && M->LayerValueChannel == Channel;
			}));
	}
	return Menu.Build();
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

TSharedRef<SWidget> SMixtormat::BuildGeneratedMaskControls()
{
	// Each named section owns its rows in a shared inspector card.
	const auto Gen = [this]() { return GetSelectedGeneratedMask(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);


	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
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
			}), nullptr, TAttribute<FText>(), 0.0f)));

	const TSharedRef<SVerticalBox> Cards = Panel;
	Panel = AddCard(Cards, LOCTEXT("GenHdrSignals", "Signals"));
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

	Panel = Cards;
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatGeneratedMask* Generated = GetSelectedGeneratedMask();
		return Generated ? &Generated->Shaping : nullptr;
	});

	const TSharedRef<SVerticalBox> GeneratedRows = AddCard(
		Cards, LOCTEXT("GenHdrShaping", "Generated Shaping"));
	AddSliderRow(GeneratedRows, MakeMemberToggle<FMixtormatGeneratedMask>(
		LOCTEXT("GenNormalizeWeights", "Normalize Weights"), Gen, &FMixtormatGeneratedMask::bNormalizeWeights));
	AddSliderRow(GeneratedRows, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenBroadness", "Broadness"), Gen, &FMixtormatGeneratedMask::Broadness, 1.0, 32.0, 2),
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenSmoothing", "Smoothing"), Gen, &FMixtormatGeneratedMask::Smoothing, 1.0, 4.0, 2)));
	AddSliderRow(GeneratedRows, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenBiasCtl", "Bias"), Gen, &FMixtormatGeneratedMask::Bias, 0.001, 0.999, 0.5, 0.01));
	AddSliderRow(GeneratedRows, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenWarpAmount", "Warp"), Gen, &FMixtormatGeneratedMask::WarpAmount, 0.0, 0.05, 0.0, 0.001),
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenWarpRadius", "Radius"), Gen, &FMixtormatGeneratedMask::WarpRadius, 1.0, 16.0, 1)));
	AddSliderRow(GeneratedRows, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenWarpSource", "Warp Flow (Normal to Height)"), Gen, &FMixtormatGeneratedMask::WarpSource, 0.0, 1.0, 0.0, 0.01));

	Panel = AddCard(Cards, LOCTEXT("GenHdrBlend", "Blend"));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenGenWeight", "Weight"), Gen, &FMixtormatGeneratedMask::Weight, 0.0, 1.0, 1.0, 0.01));

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
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildCurvatureSourceMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatCurvatureSource Sources[] = {
		EMixtormatCurvatureSource::Height,
		EMixtormatCurvatureSource::Mask};
	for (const EMixtormatCurvatureSource Source : Sources)
	{
		Menu.Item(
			MixtormatUI::CurvatureSourceText(Source),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Source]()
			{
				// Guarded through ResolveChild, not WorkingLayers: the inspector passes
				// SelectedLayerIndex, which is INDEX_NONE while a group's shared child is the
				// subject, and an index guard turns that into a silent no-op.
				if (ResolveChild(LayerIndex, ChildIndex))
				{
					ResolveChild(LayerIndex, ChildIndex)->Curvature.Source = Source;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Source]()
			{
				return ResolveChild(LayerIndex, ChildIndex)
					&& ResolveChild(LayerIndex, ChildIndex)->Curvature.Source == Source;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildCurvatureModeMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatCurvatureMode Modes[] = {
		EMixtormatCurvatureMode::Gaussian,
		EMixtormatCurvatureMode::Mean,
		EMixtormatCurvatureMode::MaxPrincipal,
		EMixtormatCurvatureMode::MinPrincipal,
		EMixtormatCurvatureMode::AngleDeficit,
		EMixtormatCurvatureMode::AngleDeficitConvex,
		EMixtormatCurvatureMode::AngleDeficitConcave};
	for (const EMixtormatCurvatureMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::CurvatureModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				// Guarded through ResolveChild, not WorkingLayers: the inspector passes
				// SelectedLayerIndex, which is INDEX_NONE while a group's shared child is the
				// subject, and an index guard turns that into a silent no-op.
				if (ResolveChild(LayerIndex, ChildIndex))
				{
					ResolveChild(LayerIndex, ChildIndex)->Curvature.Mode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				return ResolveChild(LayerIndex, ChildIndex)
					&& ResolveChild(LayerIndex, ChildIndex)->Curvature.Mode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskCurvatureControls()
{
	using namespace MixtormatMaskCurvatureRange;
	const auto Curve = [this]() { return GetSelectedLayerCurvature(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("CurvatureSourceLabel", "Source"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskCurvature* Selected = GetSelectedLayerCurvature();
				return Selected
					? MixtormatUI::CurvatureSourceText(Selected->Source)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateLambda([this]()
			{
				return BuildCurvatureSourceMenu(SelectedLayerIndex, SelectedMaskIndex);
			}), nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("CurvatureSourceHint", "Which field is measured. Surface Height is what this layer is being laid onto, so the mask follows shape already in the surface. Mask Itself reads the mask at this point in the chain -- after a Blur, if one precedes this, which is usually what gives a painted edge enough shape to measure at all.")));

	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("CurvatureModeLabel", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskCurvature* Selected = GetSelectedLayerCurvature();
				return Selected
					? MixtormatUI::CurvatureModeText(Selected->Mode)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateLambda([this]()
			{
				return BuildCurvatureModeMenu(SelectedLayerIndex, SelectedMaskIndex);
			}), nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("CurvatureModeHint", "Gaussian is zero on anything that could be unrolled flat, so a cylinder reads like a plane and only corners survive it. Mean is the average bend, which is what finds edges and creases. Max and Min Principal are the sharpest convex and concave bends at each point, taken in whichever direction they actually run rather than averaged against the flat axis.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureKernelLabel", "Kernel"), Curve, &FMixtormatMaskCurvature::Kernel,
			KernelMin, KernelMax, KernelDefault,
			LOCTEXT("CurvatureKernelHint", "Tap spacing in texels: how wide a neighbourhood the curvature is measured over. Small finds the shape of small things; large finds the shape of what those things sit on.")),
		MakeMemberSlider<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureScaleLabel", "Scale"), Curve, &FMixtormatMaskCurvature::Scale,
			ScaleMin, ScaleMax, ScaleDefault, SnapDelta,
			LOCTEXT("CurvatureScaleHint", "Height amplitude. The field arrives as 0-1 with no statement of what that is worth against a texel, and curvature is not scale invariant, so this decides whether a shape registers as a gentle swell or a cliff."))));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureRangeLowLabel", "Range Low"), Curve, &FMixtormatMaskCurvature::RangeLow,
			RangeMin, RangeMax, RangeLowDefault, SnapDelta,
			LOCTEXT("CurvatureRangeHint", "The window of signed curvature that becomes coverage. Curvature is unbounded and its useful band moves with Scale and Kernel, so it is stated rather than assumed. Setting Low above High inverts the ramp, which is the whole difference between keeping cavities and keeping edges.")),
		MakeMemberSlider<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureRangeHighLabel", "Range High"), Curve, &FMixtormatMaskCurvature::RangeHigh,
			RangeMin, RangeMax, RangeHighDefault, SnapDelta,
			LOCTEXT("CurvatureRangeHint", "The window of signed curvature that becomes coverage. Curvature is unbounded and its useful band moves with Scale and Kernel, so it is stated rather than assumed. Setting Low above High inverts the ramp, which is the whole difference between keeping cavities and keeping edges."))));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureWeightLabel", "Weight"), Curve, &FMixtormatMaskCurvature::Weight,
			WeightMin, WeightMax, WeightDefault, SnapDelta,
			LOCTEXT("CurvatureWeightHint", "How much of the narrowing to apply. Zero is the identity, so the node can be dialled back, or driven to nothing, without being removed from the chain.")),
		MakeMemberToggle<FMixtormatMaskCurvature>(
			LOCTEXT("CurvatureInvertLabel", "Invert"), Curve, &FMixtormatMaskCurvature::bInvert,
			LOCTEXT("CurvatureInvertHint", "Keeps what the range rejects instead."))));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerCurvature() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("MaskCurvatureHeading", "CURVATURE"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildMaskBlurControls()
{
	using namespace MixtormatMaskBlurRange;
	const auto Blur = [this]() { return GetSelectedLayerBlur(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerBlur() ? EVisibility::Visible : EVisibility::Collapsed;
		});

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskBlur>(
			LOCTEXT("MaskBlurRadiusXLabel", "Radius X"), Blur, &FMixtormatMaskBlur::RadiusX,
			RadiusMin, RadiusMax, RadiusDefault, SnapDelta,
			LOCTEXT("MaskBlurRadiusXHint", "Horizontal softening of the mask this sits under, in texels at the composition resolution. Zero skips the pass entirely rather than running a one-tap identity, so X alone smears sideways and leaves verticals crisp.")),
		MakeMemberSlider<FMixtormatMaskBlur>(
			LOCTEXT("MaskBlurRadiusYLabel", "Radius Y"), Blur, &FMixtormatMaskBlur::RadiusY,
			RadiusMin, RadiusMax, RadiusDefault, SnapDelta,
			LOCTEXT("MaskBlurRadiusYHint", "Vertical softening, in the same units. Equal to Radius X this is an ordinary Gaussian; unequal it is anisotropic. Both at zero and the node does nothing."))));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerBlur() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("MaskBlurHeading", "BLUR"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLayerMaskControls()
{
	// Shared by layer-scoped and feature-scoped mask rows: one generic binding per row,
	// cards for grouping, and pairs where both labels are one short word.
	const auto Mask = [this]() { return GetSelectedLayerMask(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Visible : EVisibility::Collapsed; });

	const TSharedRef<SVerticalBox> Cards = Panel;
	Panel = SNew(SVerticalBox);
	Cards->AddSlot().AutoHeight()
	[
		SNew(SMixtormatInspectorCard)
		.Title_Lambda([this]()
		{
			const FMixtormatMaskLayer* Selected = GetSelectedLayerMask();
			if (!Selected)
			{
				return LOCTEXT("NoSelectedMask", "No mask selected");
			}
			if (Selected->UsesLayerValues())
			{
				// Naming the channel, because that is the whole identity of this mask -- there is
				// no asset to name and two Layer Values masks on one layer differ only by it.
				return FText::Format(
					LOCTEXT("SelectedMaskLayerValuesName", "Layer Values - {0}"),
					MixtormatUI::LayerValueChannelText(Selected->LayerValueChannel));
			}
			const FSoftObjectPath Path = !Selected->Mask.IsNull()
				? Selected->Mask.ToSoftObjectPath()
				: Selected->MaskTexture.ToSoftObjectPath();
			return FText::FromString(Path.GetAssetName());
		})
		[
			Panel
		]
	];

	// The asset, on the node itself. Before this the only way to change a texture mask's map was
	// the row's context menu, which swapped in whatever the bottom gallery happened to have
	// selected -- an action with no visible state on the thing it changed. Straight through to
	// ReplaceMaskInLayer, the same call that menu makes and the same one a gallery drag lands on,
	// so there is one mask representation and one way it is built.
	//
	// Texture masks only: a Layer Values mask has no asset, and a mask wired to another child's
	// published output has one that is not an asset at all.
	AddSliderRow(Panel, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && !M->UsesLayerValues() && !M->HasPublishedSource()
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeDropdown(
				LOCTEXT("SelectedMaskAsset", "Mask"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatMaskLayer* M = GetSelectedLayerMask();
						if (!M)
						{
							return FText::GetEmpty();
						}
						const FSoftObjectPath Path = !M->Mask.IsNull()
							? M->Mask.ToSoftObjectPath()
							: M->MaskTexture.ToSoftObjectPath();
						return Path.IsNull()
							? LOCTEXT("SelectedMaskAssetNone", "Choose Mask...")
							: FText::FromString(Path.GetAssetName());
					}),
					FOnGetContent::CreateLambda([this]()
					{
						// Captured at open time, like every other menu here: the selection is
						// what it was when the popover was asked for.
						const int32 LayerIndex = SelectedLayerIndex;
						const int32 ChildIndex = GetSelectedChildIndex();
						return BuildMaskAssetPicker(
							[this, LayerIndex, ChildIndex](const FSoftObjectPath Path)
							{
								ReplaceMaskInLayer(LayerIndex, ChildIndex, Path);
							},
							[this](const FSoftObjectPath Path)
							{
								const FMixtormatMaskLayer* M = GetSelectedLayerMask();
								return M
									&& (M->Mask.ToSoftObjectPath() == Path
										|| M->MaskTexture.ToSoftObjectPath() == Path);
							});
					}), nullptr, TAttribute<FText>(), 0.0f),
				LOCTEXT("SelectedMaskAssetHint",
					"The map this mask reads. Picking one here is the same operation as dragging "
					"a mask onto the layer from the gallery -- same child, same defaults taken "
					"from the asset -- so a mask built either way behaves identically."))
		]);

	// No Source row. What a mask reads is its identity, not a setting on it: a Texture Mask names
	// an asset and places it, a Layer Values Mask names a channel of the layer it sits on and has
	// nothing to place. Offering the two as one switchable field meant an instance could change
	// semantic kind under whoever flipped it, and it put a page of placement controls on a node
	// that ignores every one of them. Creation fixes the source -- Masks > Texture Mask, or a mask
	// dragged from the gallery; Masks > Layer Values Mask -- and the rows below follow from it.
	//
	// EMixtormatMaskSource and FMixtormatMaskLayer::Source are untouched. The two kinds still
	// share one serialised struct, so nothing saved needs migrating and no compositor branch moves.

	// Layer Values only, and collapsed on a texture mask where it would be a control over nothing.
	// UsesLayerValues(), not the raw field: an explicit published source wins over either, and a
	// mask wired to another child's output is reading neither the asset nor the layer.
	AddSliderRow(Panel, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && M->UsesLayerValues()
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeDropdown(
				LOCTEXT("SelectedMaskLayerValueChannel", "Channel"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatMaskLayer* M = GetSelectedLayerMask();
						return M
							? MixtormatUI::LayerValueChannelText(M->LayerValueChannel)
							: FText::GetEmpty();
					}),
					FOnGetContent::CreateSP(
						this, &SMixtormat::BuildMaskLayerValueChannelMenu), nullptr, TAttribute<FText>(), 0.0f),
				LOCTEXT("SelectedMaskLayerValueChannelHint",
					"Which scalar to take. Luminance is Rec. 709 over the layer's linear albedo "
					"and is what reads as brightness; the single channels are the raw albedo "
					"components. Roughness is the layer's resolved roughness, after its bias, "
					"contrast and offset.\n\n"
					"Inversion, Balance, Contrast and Offset under Shaping do the levelling -- "
					"there is no second set of range controls here."))
		]);

	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
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
			}), nullptr, TAttribute<FText>(), 0.0f)));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatMaskLayer>(
		LOCTEXT("SelectedMaskWeight", "Weight"), Mask, &FMixtormatMaskLayer::Weight, 0.0, 1.0, 1.0, 0.01));

	// Source placement. Every transform here maps the unit square onto itself, which is the
	// constraint: the read is wrapped in a frac(), so anything else seams at the repeat.
	//
	// Texture masks only. A Layer Values mask reads the layer it is on at the composition's own
	// resolution -- there is no map to tile, offset, flip or turn -- so the whole block is
	// collapsed rather than shown inert. This is the other half of removing the Source dropdown:
	// the inspector is now specific to what the mask actually reads.
	const TSharedRef<SVerticalBox> PlacementCard = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && M->UsesLayerValues()
				? EVisibility::Collapsed
				: EVisibility::Visible;
		});
	const TSharedRef<SVerticalBox> Placement = AddCard(PlacementCard, LOCTEXT("MaskGrpPlacement", "Placement"));
	AddSliderRow(Placement, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatMaskLayer>(
			LOCTEXT("MaskTilingXLabel", "Tiling X"), Mask, &FMixtormatMaskLayer::TilingX, 1.0, 16.0, 1,
			LOCTEXT("MaskTilingHint", "Repeats across the axis. Integer only: a fractional scale lands mid-cell at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatMaskLayer>(
			LOCTEXT("MaskTilingYLabel", "Tiling Y"), Mask, &FMixtormatMaskLayer::TilingY, 1.0, 16.0, 1,
			LOCTEXT("MaskTilingHint", "Repeats across the axis. Integer only: a fractional scale lands mid-cell at the UV wrap and seams."))));
	AddSliderRow(Placement, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskUVOffsetXLabel", "Offset X"), Mask, &FMixtormatMaskLayer::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("MaskUVOffsetHint", "Shifts where the mask is read from. Safe at any value: translating a periodic function leaves it periodic. Unrelated to Offset under Shaping, which lifts the mask value instead.")),
		MakeMemberSlider<FMixtormatMaskLayer>(
			LOCTEXT("MaskUVOffsetYLabel", "Offset Y"), Mask, &FMixtormatMaskLayer::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("MaskUVOffsetHint", "Shifts where the mask is read from. Safe at any value: translating a periodic function leaves it periodic. Unrelated to Offset under Shaping, which lifts the mask value instead."))));
	AddSliderRow(Placement, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatMaskLayer>(
			LOCTEXT("MaskFlipULabel", "Flip U"), Mask, &FMixtormatMaskLayer::bFlipU),
		MakeMemberToggle<FMixtormatMaskLayer>(
			LOCTEXT("MaskFlipVLabel", "Flip V"), Mask, &FMixtormatMaskLayer::bFlipV)));
	AddSliderRow(Placement, MixtormatRow::MakeDropdown(
		LOCTEXT("MaskRotationLabel", "Rotate"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M ? MixtormatUI::UVRotationText(M->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskRotationMenu), nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("MaskRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams; 90 degree steps are permutations of the unit square, so they stay tileable. Applied before the tiling, so the mask turns and the lattice repeats the turned result.")));

	AddSliderRow(Panel, PlacementCard);

	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatMaskLayer* M = GetSelectedLayerMask();
		return M ? &M->Shaping : nullptr;
	});

	return SNew(SBox)
		// Gated on the selected child actually being a Mask, not just on a layer being selected:
		// this group used to show above every child type -- Peeling, Stain, Erosion, and the rest --
		// with only a "select a mask child" placeholder standing in for controls that could never
		// apply to what was actually selected.
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerMask()
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
				+ SVerticalBox::Slot().AutoHeight()[Cards]
			]
		];
}

void SMixtormat::AddMaskShapingRows(
	const TSharedRef<SVerticalBox>& Cards,
	TFunction<FMixtormatMaskShaping*()> Resolve)
{
	const TSharedRef<SVerticalBox> TargetPanel = AddCard(Cards, LOCTEXT("MaskGrpShape", "Shaping"));
	AddSliderRow(TargetPanel, MakeMemberToggle<FMixtormatMaskShaping>(
		LOCTEXT("MaskNormalizeInputLabel", "Normalize Input"), Resolve,
		&FMixtormatMaskShaping::bNormalizeInput));
	AddSliderRow(TargetPanel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskInputMinLabel", "Input Min"), Resolve, &FMixtormatMaskShaping::InputMin,
			0.0, 1.0, 0.0, 0.01,
			LOCTEXT("MaskInputMinHint", "Black point applied after optional input normalization.")),
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskInputMaxLabel", "Input Max"), Resolve, &FMixtormatMaskShaping::InputMax,
			0.0, 1.0, 1.0, 0.01,
			LOCTEXT("MaskInputMaxHint", "White point applied after optional input normalization."))));
	AddSliderRow(TargetPanel,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGapTight)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.Text(LOCTEXT("MaskCurveBiasLabel", "Curve Bias"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMixtormatScalarRamp)
		.Ramp_Lambda([Resolve]()
		{
			const FMixtormatMaskShaping* Shaping = Resolve();
			return Shaping ? Shaping->CurveBias : FMixtormatScalarRamp();
		})
		.Height(MixtormatTokens::ScalarRampHeight)
		.CanonicalYMin(0.0f)
		.CanonicalYMax(1.0f)
		.SoftYMin(-1.5f)
		.SoftYMax(1.5f)
		.ExtendedYMin(-3.0f)
		.ExtendedYMax(3.0f)
		.OnChanged_Lambda([this, Resolve](const FMixtormatScalarRamp& Ramp)
		{
			if (FMixtormatMaskShaping* Shaping = Resolve())
			{
				Shaping->CurveBias = Ramp;
				RefreshLayeredPreview();
			}
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); })
		]);
	AddSliderRow(TargetPanel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskBalanceLabel", "Balance"), Resolve, &FMixtormatMaskShaping::Balance,
			0.0, 1.0, 0.5, 0.01,
			LOCTEXT("MaskBalanceHint", "Thins the mask toward black above the middle and thickens it toward white below, as a power curve -- so it erodes what is there rather than fading it out. The ends are aggressive but never flatten the mask.")),
		MakeMemberSlider<FMixtormatMaskShaping>(
			LOCTEXT("MaskContrastLabel", "Contrast"), Resolve, &FMixtormatMaskShaping::Contrast,
			0.0, 4.0, 1.0, 0.01,
			LOCTEXT("MaskContrastHint", "Hardens the transition about the mask's midpoint. Masks are stored raw rather than sRGB, so that midpoint really is the 50% grey you painted."))));
	AddSliderRow(TargetPanel, MakeMemberSlider<FMixtormatMaskShaping>(
		LOCTEXT("MaskOffsetLabel", "Offset"), Resolve, &FMixtormatMaskShaping::Offset,
		-1.0, 1.0, 0.0, 0.01,
		LOCTEXT("MaskOffsetHint", "Lifts the whole mask after contrast. Plus one reaches full white and minus one full black from any input, whatever the contrast is set to.")));
	AddSliderRow(TargetPanel, MakeMemberToggle<FMixtormatMaskShaping>(
		LOCTEXT("MaskInvertLabel", "Invert"), Resolve, &FMixtormatMaskShaping::bInvert));

}

#undef LOCTEXT_NAMESPACE
