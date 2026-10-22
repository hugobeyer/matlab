// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatChildScope.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatTile.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

extern const EMixtormatUVRotation GMixtormatUVRotations[4];

namespace
{
	const EMixtormatMaskSource GMixtormatMaskSources[] = {
		EMixtormatMaskSource::Texture,
		EMixtormatMaskSource::LayerValues,
		EMixtormatMaskSource::Noise
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
		EMixtormatMaskBlendMode::Overlay,
		EMixtormatMaskBlendMode::Difference,
		EMixtormatMaskBlendMode::Exclusion,
		EMixtormatMaskBlendMode::Divide
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
			FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileGap,
			FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileGap));

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
			.ThumbnailResolution(FMath::RoundToInt(FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileMaximum))
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
		.Padding(0.0f, FMixtormatThemeStore::GetResolved().GalleryLayout.TileGap, 0.0f, 0.0f)
		[
			Footer.ToSharedRef()
		];
	}

	return SNew(SBox)
		.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.MaskPickerWidth)
		.Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.TileGap)
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
	return BuildRegionFilterPanel(true);
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
		// Feature labels such as "Normal Flatness" need a full-width chip; pairing two
		// dropdowns clips their value text even at the normal Inspector width.
		AddSliderRow(Panel,
			FeatureRow(LOCTEXT("SurfaceIdsPrimary", "Primary"), &FMixtormatClusterFilter::PrimaryFeature));
		AddSliderRow(Panel,
			FeatureRow(LOCTEXT("SurfaceIdsSecondary", "Secondary"), &FMixtormatClusterFilter::SecondaryFeature));
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
			.Title(bSurfaceIds ? LOCTEXT("SurfaceIdsHeading", "SURFACE IDS") : LOCTEXT("ClusterFilterHeading", "CLUSTER IDS")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
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

	Panel = AddCard(Cards, LOCTEXT("CraqGrpBlend", "BLENDING"));
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

	AddMaskOutputRangeRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatCraquelure* C = GetSelectedCraquelure();
		return C ? &C->Shaping : nullptr;
	});

	// Reuse the shared levels, curve and offset controls on all mask producers.
	AddMaskShapingRows(Cards, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatCraquelure* C = GetSelectedCraquelure();
		return C ? &C->Shaping : nullptr;
	});

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCraquelure() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CraquelureHeading", "CRAQUELURE")).PresetAction(MakeModulePresetButton())
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
	return BuildMaskSourceMenuFor(GetSelectedChildAddress());
}

TSharedRef<SWidget> SMixtormat::BuildMaskSourceMenuFor(const FMixtormatChildAddress Destination)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerChild* Child = ResolveChildAt(Destination);
	const bool bEditable = Child && !Child->IsInstance();
	for (const EMixtormatMaskSource Source : GMixtormatMaskSources)
	{
		Menu.Item(
			MixtormatUI::MaskSourceText(Source),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Destination, Source]()
			{
				SelectMaskSource(Destination, Source);
			}))
			.Enabled(bEditable)
			.Checked(TAttribute<bool>::CreateLambda([this, Destination, Source]()
			{
				const FMixtormatLayerChild* Target = ResolveChildAt(Destination);
				return Target && !Target->Mask.HasPublishedSource() && Target->Mask.Source == Source;
			}));
	}
	FMixtormatLayerChild CopiedMask;
	const bool bCopiedMask = ResolveGatingMaskPayload(CopiedMask);
	const FMixtormatChildAddress CopiedSource{EMixtormatChildOwnerType::Layer,
		CopiedMask.Mask.PublishedSourceLayerId, CopiedMask.Mask.PublishedSourceChildId};
	FMixtormatChildAddress SourceAddress = CopiedSource;
	if (!ResolveChildAt(SourceAddress)) { SourceAddress.OwnerType = EMixtormatChildOwnerType::Group; }
	FText PasteReason;
	const bool bCanPasteValue = bCopiedMask && CopiedMask.Mask.PublishedSourceOutput == TEXT("Value")
		&& CanSelectMaskNoiseValue(Destination, SourceAddress, PasteReason);
	Menu.Item(LOCTEXT("PasteCopiedNoiseValue", "Paste Copied Noise Value"), nullptr,
		FSimpleDelegate::CreateLambda([this, Destination, SourceAddress]()
		{ SelectMaskNoiseValue(Destination, SourceAddress); })).Enabled(bCanPasteValue);
	Menu.SubMenu(LOCTEXT("MaskNoiseValueFrom", "Noise Value from…"), nullptr,
		FOnGetContent::CreateLambda([this, Destination]() { return BuildMaskNoiseValueMenu(Destination); }))
		.Enabled(bEditable);
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
	const TFunction<FMixtormatGeneratedMask*()> Gen = [this]() { return GetSelectedGeneratedMask(); };
	const TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);

	const auto IsPositive = [this, Gen](float FMixtormatGeneratedMask::* Member)
	{
		const auto Address = MakeAddressResolver(Gen, Member);
		return TAttribute<bool>::CreateLambda([this, Gen, Member, Address]()
		{
			const FMixtormatGeneratedMask* Selected = Gen();
			return Selected && GetEffectiveFloatParameter(Address(), Selected->*Member) > 0.0f;
		});
	};
	const auto IsAnyPositive = [this, Gen](float FMixtormatGeneratedMask::* MemberA, float FMixtormatGeneratedMask::* MemberB)
	{
		const auto AddressA = MakeAddressResolver(Gen, MemberA);
		const auto AddressB = MakeAddressResolver(Gen, MemberB);
		return TAttribute<bool>::CreateLambda([this, Gen, MemberA, MemberB, AddressA, AddressB]()
		{
			const FMixtormatGeneratedMask* Selected = Gen();
			return Selected && (GetEffectiveFloatParameter(AddressA(), Selected->*MemberA) > 0.0f
				|| GetEffectiveFloatParameter(AddressB(), Selected->*MemberB) > 0.0f);
		});
	};
	const TAttribute<bool> CurvatureActive = IsAnyPositive(&FMixtormatGeneratedMask::CavityWeight, &FMixtormatGeneratedMask::ConvexWeight);
	const TAttribute<bool> AOActive = IsPositive(&FMixtormatGeneratedMask::AOWeight);
	const TAttribute<bool> SlopeActive = IsPositive(&FMixtormatGeneratedMask::SlopeWeight);
	const TAttribute<bool> DirectionActive = IsPositive(&FMixtormatGeneratedMask::DirectionWeight);
	const TAttribute<bool> HeightActive = IsPositive(&FMixtormatGeneratedMask::HeightWeight);

	// Card 1: SURFACE
	const TSharedRef<SVerticalBox> SurfaceCard = AddCard(Cards, LOCTEXT("GenSurfaceHeading", "SURFACE"));
	AddSliderRow(SurfaceCard, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenLayerMix", "Layer Mix"), Gen, &FMixtormatGeneratedMask::LayerMix, 0.0, 1.0, 0.5, 0.01,
		LOCTEXT("GenLayerMixHint", "0 reads the composite below, 1 reads this layer's own surface and generators, 0.5 blends both. Automatically adapts on Layer 0.")));

	// Card 2: CURVATURE
	const TSharedRef<SVerticalBox> CurvatureCard = AddCard(Cards, LOCTEXT("GenCurvatureHeading", "CURVATURE"));
	AddSliderRow(CurvatureCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenCavityWeight", "Cavity / Crevice"), Gen, &FMixtormatGeneratedMask::CavityWeight, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("GenCavityWeightHint", "Concavity (crevices, valleys, inner seams).")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenConvexWeight", "Convex / Edges"), Gen, &FMixtormatGeneratedMask::ConvexWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenConvexWeightHint", "Convexity (peaks, ridges, sharp outer edges)."))));

	const TSharedRef<SWidget> CurvatureCurveEditor =
		SNew(SMixtormatScalarRamp)
		.Ramp_Lambda([Gen]()
		{
			const FMixtormatGeneratedMask* M = Gen();
			return M ? M->CurvatureCurve : FMixtormatScalarRamp();
		})
		.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
		.CanonicalYMin(0.0f)
		.CanonicalYMax(1.0f)
		.SoftYMin(-1.5f)
		.SoftYMax(1.5f)
		.ExtendedYMin(-3.0f)
		.ExtendedYMax(3.0f)
		.OnChanged_Lambda([this, Gen](const FMixtormatScalarRamp& Ramp)
		{
			if (FMixtormatGeneratedMask* M = Gen())
			{
				M->CurvatureCurve = Ramp;
				RefreshLayeredPreview();
			}
		});

	const TSharedRef<SVerticalBox> CurvatureRows = SNew(SVerticalBox);
	AddSliderRow(CurvatureRows, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvatureRadius", "Radius"), Gen, &FMixtormatGeneratedMask::CurvatureRadius, 1.0, 32.0, 2,
			LOCTEXT("GenCurvatureRadiusHint", "Curvature sample radius in texels. Multi-scale rings sample r, 2r, 4r around this base radius.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvatureStrength", "Gain"), Gen, &FMixtormatGeneratedMask::CurvatureStrength, 0.0, 16.0, 4.0, 0.05,
			LOCTEXT("GenCurvatureStrengthHint", "Gain applied to combined height-Laplacian and normal-Sobel curvature."))));
	AddSliderRow(CurvatureRows, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvatureSmoothing", "Rings"), Gen, &FMixtormatGeneratedMask::CurvatureSmoothing, 1.0, 4.0, 2,
			LOCTEXT("GenCurvatureSmoothingHint", "Widening Sobel / Laplacian rings to suppress high-frequency noise and capture broader form.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenCurvatureMacroMix", "Macro Mix"), Gen, &FMixtormatGeneratedMask::CurvatureMacroMix, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("GenCurvatureMacroMixHint", "Fine-to-broad multi-scale balance (0 favors tight micro-edges, 1 favors broad macro-forms)."))));
	AddSliderRow(CurvatureRows, SNew(STextBlock)
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.Text(LOCTEXT("GenCurvatureCurveLabel", "Response Curve")));
	AddSliderRow(CurvatureRows, CurvatureCurveEditor);

	CurvatureCard->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([CurvatureActive]() { return CurvatureActive.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[ CurvatureRows ]
	];

	// Card 3: OCCLUSION (AO)
	const TSharedRef<SVerticalBox> AOCard = AddCard(Cards, LOCTEXT("GenAOHeading", "OCCLUSION (AO)"));
	AddSliderRow(AOCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenAOWeight", "Occlusion"), Gen, &FMixtormatGeneratedMask::AOWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenAOHint", "Horizon + surface ambient occlusion weight.")),
		MakeMemberToggle<FMixtormatGeneratedMask>(
			LOCTEXT("GenAOInvert", "Invert (Exposed)"), Gen, &FMixtormatGeneratedMask::bAOInvert,
			LOCTEXT("GenAOInvertHint", "Invert occlusion to select exposed peaks instead of sheltered crevices."))));

	const TSharedRef<SVerticalBox> AORows = SNew(SVerticalBox);
	AddSliderRow(AORows, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenAORadius", "Radius"), Gen, &FMixtormatGeneratedMask::AORadius, 1.0, 64.0, 12.0, 0.5,
			LOCTEXT("GenAORadiusHint", "Horizon search radius in texels for height-based crevice occlusion.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenAOPower", "Contrast"), Gen, &FMixtormatGeneratedMask::AOPower, 0.1, 4.0, 1.5, 0.05,
			LOCTEXT("GenAOPowerHint", "Occlusion falloff contrast power."))));
	AOCard->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([AOActive]() { return AOActive.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[ AORows ]
	];

	// Card 4: SLOPE & DIRECTION
	const TSharedRef<SVerticalBox> SlopeDirCard = AddCard(Cards, LOCTEXT("GenSlopeDirHeading", "SLOPE & DIRECTION"));
	AddSliderRow(SlopeDirCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenSlopeWeight", "Slope"), Gen, &FMixtormatGeneratedMask::SlopeWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenSlopeHint", "Slope angle selection weight (0 = flat up-facing, 90 = vertical cliff).")),
		MakeMemberToggle<FMixtormatGeneratedMask>(
			LOCTEXT("GenSlopeInvert", "Invert"), Gen, &FMixtormatGeneratedMask::bSlopeInvert,
			LOCTEXT("GenSlopeInvertHint", "Invert slope selection."))));

	const TSharedRef<SVerticalBox> SlopeRows = SNew(SVerticalBox);
	AddSliderRow(SlopeRows, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenSlopeMin", "Min Angle"), Gen, &FMixtormatGeneratedMask::SlopeMin, 0.0, 90.0, 25.0, 0.5,
			LOCTEXT("GenSlopeMinHint", "Minimum slope angle in degrees.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenSlopeMax", "Max Angle"), Gen, &FMixtormatGeneratedMask::SlopeMax, 0.0, 90.0, 90.0, 0.5,
			LOCTEXT("GenSlopeMaxHint", "Maximum slope angle in degrees."))));
	AddSliderRow(SlopeRows, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenSlopeSoftness", "Softness"), Gen, &FMixtormatGeneratedMask::SlopeSoftness, 0.01, 1.0, 0.2, 0.01,
		LOCTEXT("GenSlopeSoftnessHint", "Transition softness at slope angle thresholds.")));
	SlopeDirCard->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([SlopeActive]() { return SlopeActive.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[ SlopeRows ]
	];

	AddSliderRow(SlopeDirCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionWeight", "Direction"), Gen, &FMixtormatGeneratedMask::DirectionWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenDirectionHint", "Directional facing mask weight.")),
		MakeMemberToggle<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionInvert", "Invert"), Gen, &FMixtormatGeneratedMask::bDirectionInvert,
			LOCTEXT("GenDirectionInvertHint", "Invert directional facing selection."))));

	const TSharedRef<SVerticalBox> DirectionRows = SNew(SVerticalBox);
	AddSliderRow(DirectionRows, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionAngle", "Azimuth"), Gen, &FMixtormatGeneratedMask::DirectionAngle, 0.0, 360.0, 90.0, 1.0,
			LOCTEXT("GenDirectionAngleHint", "Tangent-space azimuth in degrees. 90 is +Y.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenDirectionElevation", "Elevation"), Gen, &FMixtormatGeneratedMask::DirectionElevation, 0.0, 90.0, 35.0, 1.0,
			LOCTEXT("GenDirectionElevationHint", "0 faces sideways, 90 faces straight up."))));
	AddSliderRow(DirectionRows, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenDirectionBroadness", "Focus"), Gen, &FMixtormatGeneratedMask::DirectionBroadness, 0.05, 8.0, 1.5, 0.05,
		LOCTEXT("GenDirectionBroadnessHint", "Directional falloff focus sharpness.")));
	SlopeDirCard->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([DirectionActive]() { return DirectionActive.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[ DirectionRows ]
	];

	// Card 5: HEIGHT & DRAINAGE
	const TSharedRef<SVerticalBox> HeightCard = AddCard(Cards, LOCTEXT("GenHeightHeading", "HEIGHT & DRAINAGE"));
	AddSliderRow(HeightCard, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenHeightWeight", "Height"), Gen, &FMixtormatGeneratedMask::HeightWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenHeightHint", "Normalized height selection weight.")),
		MakeMemberSlider<FMixtormatGeneratedMask>(
			LOCTEXT("GenRidgeWeight", "Erosion Ridge"), Gen, &FMixtormatGeneratedMask::RidgeWeight, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("GenRidgeWeightHint", "Crest/drainage output from erosion below."))));

	const TSharedRef<SWidget> HeightCurveEditor =
		SNew(SMixtormatScalarRamp)
		.Ramp_Lambda([Gen]()
		{
			const FMixtormatGeneratedMask* M = Gen();
			return M ? M->HeightCurve : FMixtormatScalarRamp();
		})
		.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
		.CanonicalYMin(0.0f)
		.CanonicalYMax(1.0f)
		.SoftYMin(-1.5f)
		.SoftYMax(1.5f)
		.ExtendedYMin(-3.0f)
		.ExtendedYMax(3.0f)
		.OnChanged_Lambda([this, Gen](const FMixtormatScalarRamp& Ramp)
		{
			if (FMixtormatGeneratedMask* M = Gen())
			{
				M->HeightCurve = Ramp;
				RefreshLayeredPreview();
			}
		});

	const TSharedRef<SVerticalBox> HeightRows = SNew(SVerticalBox);
	AddSliderRow(HeightRows, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenHeightBias", "Height Offset"), Gen, &FMixtormatGeneratedMask::HeightBias, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("GenHeightBiasHint", "Offsets normalized height before curve evaluation.")));
	AddSliderRow(HeightRows, SNew(STextBlock)
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.Text(LOCTEXT("GenHeightCurveLabel", "Height Profile")));
	AddSliderRow(HeightRows, HeightCurveEditor);

	HeightCard->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([HeightActive]() { return HeightActive.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[ HeightRows ]
	];

	// Card 6: SHAPING
	AddMaskShapingRows(Cards, [Gen]() -> FMixtormatMaskShaping*
	{
		FMixtormatGeneratedMask* Generated = Gen();
		return Generated ? &Generated->Shaping : nullptr;
	});

	// Card 7: BLENDING
	const TSharedRef<SVerticalBox> BlendingCard = AddCard(Cards, LOCTEXT("GenBlendingHeading", "BLENDING"));
	AddSliderRow(BlendingCard, MixtormatRow::MakeDropdown(
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
	AddSliderRow(BlendingCard, MakeMemberSlider<FMixtormatGeneratedMask>(
		LOCTEXT("GenWeight", "Weight"), Gen, &FMixtormatGeneratedMask::Weight, 0.0, 1.0, 1.0, 0.01));
	AddMaskOutputRangeRows(BlendingCard, [Gen]() -> FMixtormatMaskShaping*
	{
		FMixtormatGeneratedMask* Generated = Gen();
		return Generated ? &Generated->Shaping : nullptr;
	});

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedGeneratedMask() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("GeneratedMaskHeading", "GENERATED MASK")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
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
			.Title(LOCTEXT("MaskCurvatureHeading", "CURVATURE")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}

bool SMixtormat::CanSelectBlurFlowSource(const FMixtormatChildAddress& Destination,
	const FMixtormatOutputReference& Reference) const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Destination);
	if (!Child || Child->Type != EMixtormatLayerChildType::Blur) { return false; }
	TArray<FMixtormatLayer> EffectiveLayers;
	MixtormatLayerGroups::BuildEffectiveLayers(WorkingLayers, WorkingLayerGroups, EffectiveLayers);
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	for (FMixtormatLayer& Layer : EffectiveLayers)
	{
		MixtormatParameterBinding::ResolveChildInstances(Scope, Layer);
	}
	bool bFound = false;
	for (int32 LayerIndex = 0; LayerIndex < EffectiveLayers.Num(); ++LayerIndex)
	{
		const FMixtormatLayer& Layer = EffectiveLayers[LayerIndex];
		const bool bGroup = Destination.OwnerType == EMixtormatChildOwnerType::Group;
		if (bGroup ? Layer.GroupId != Destination.OwnerId : Layer.LayerId != Destination.OwnerId) { continue; }
		const FGuid ChildId = bGroup
			? MixtormatLayerGroups::MakeEffectiveChildId(Destination.OwnerId, Destination.ChildId, Layer.LayerId)
			: Destination.ChildId;
		const int32 ChildIndex = MixtormatChildScope::FindIndexById(Layer.Children, ChildId);
		if (MixtormatChildScope::ResolveBlurFlowSource(EffectiveLayers, LayerIndex, ChildIndex, Reference) == INDEX_NONE)
		{
			return false;
		}
		bFound = true;
	}
	return bFound;
}

TSharedRef<SWidget> SMixtormat::BuildBlurFlowSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->Type != EMixtormatLayerChildType::Blur || Selected->IsInstance()) { return Menu.Build(); }
	const auto Assign = [this, Address](const FMixtormatOutputReference& Reference)
	{
		FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || Child->Type != EMixtormatLayerChildType::Blur || Child->IsInstance()) { return; }
		if (Reference.HasSource() && !CanSelectBlurFlowSource(Address, Reference)) { return; }
		Child->Blur.FlowSource = Reference;
		RefreshLayeredPreview();
		RebuildLayerList();
	};
	if (Selected->Blur.FlowSource.HasSource())
	{
		Menu.Item(LOCTEXT("BlurClearFlowSource", "Clear source"), nullptr,
			FSimpleDelegate::CreateLambda([Assign]() { Assign(FMixtormatBlur{}.FlowSource); }));
		Menu.Separator();
	}
	bool bHasCompatibleSource = false;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	for (const FMixtormatLayer& AuthoredLayer : WorkingLayers)
	{
		FMixtormatLayer SourceLayer = AuthoredLayer;
		MixtormatParameterBinding::ResolveChildInstances(Scope, SourceLayer);
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			for (const FMixtormatPublishedOutputDesc& Output : GetChildCapabilities(Producer).Outputs)
			{
				if (!Output.bCopyableAsField || Output.FieldKind != EMixtormatPublishedFieldKind::Flow) { continue; }
				FMixtormatOutputReference Reference;
				Reference.SourceLayerId = SourceLayer.LayerId;
				Reference.SourceChildId = Producer.ChildId;
				Reference.OutputName = Output.Name;
				Reference.Kind = EMixtormatPublishedFieldKind::Flow;
				if (!CanSelectBlurFlowSource(Address, Reference)) { continue; }
				bHasCompatibleSource = true;
				Menu.Item(FText::Format(LOCTEXT("BlurFlowSourceEntry", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label), MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([Assign, Reference]() { Assign(Reference); }));
			}
		}
	}
	if (!bHasCompatibleSource)
	{
		Menu.Caption(LOCTEXT("BlurNoCompatibleFlow", "No completed Flow source is available"));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildBlurControls()
{
	using namespace MixtormatBlurRange;
	const auto Blur = [this]() { return GetSelectedLayerBlur(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerBlur() ? EVisibility::Visible : EVisibility::Collapsed;
		});

	const auto EditableBlur = [this]()
	{
		const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
		return Child && Child->Type == EMixtormatLayerChildType::Blur && !Child->IsInstance();
	};
	TSharedRef<SVerticalBox> Settings = AddCard(Panel, LOCTEXT("BlurSettingsCard", "FILTER"));
	AddSliderRow(Settings, SNew(SBox)
		.IsEnabled_Lambda(EditableBlur)
		[
			SNew(SMixtormatSegmentedControl)
			.Options(TArray<FText>{LOCTEXT("BlurModeGaussian", "Gaussian"),
				LOCTEXT("BlurModeDirectional", "Directional"), LOCTEXT("BlurModeSmear", "Smear")})
			.ToolTips(TArray<FText>{
				LOCTEXT("BlurModeGaussianHint", "Gaussian uses Radius and X/Y Scale."),
				LOCTEXT("BlurModeDirectionalHint", "Averages samples on both sides of the chosen path."),
				LOCTEXT("BlurModeSmearHint", "Trails samples on one side; negative Length reverses direction.")})
			.ActiveIndex_Lambda([Blur]() -> int32
			{
				const FMixtormatBlur* B = Blur();
				return B ? static_cast<int32>(B->Mode) : INDEX_NONE;
			})
			.OnChosen_Lambda([this, Blur, EditableBlur](int32 Index)
			{
				if (!EditableBlur() || Index < 0 || Index > 2) { return; }
				if (FMixtormatBlur* B = Blur())
				{
					B->Mode = static_cast<EMixtormatBlurMode>(Index);
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			})
		]);
	TSharedRef<SVerticalBox> TargetControls = SNew(SVerticalBox);
	TargetControls->AddSlot().AutoHeight()
		[
			MixtormatRow::MakeCaption(LOCTEXT("BlurTargetCaption", "TARGET"))
		];
	TargetControls->AddSlot().AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.SegmentedControlGap)
		[
			SNew(SMixtormatSegmentedControl)
			.Options(TArray<FText>{LOCTEXT("BlurTargetHeight", "Height"),
				LOCTEXT("BlurTargetFlow", "Flow"), LOCTEXT("BlurTargetBoth", "Height + Flow")})
			.ToolTips(TArray<FText>{
				LOCTEXT("BlurTargetHeightHint", "Filter the generator's signed Height."),
				LOCTEXT("BlurTargetFlowHint", "Filter the owner's valid Flow output."),
				LOCTEXT("BlurTargetBothHint", "Filter both Height and Flow without changing their ownership.")})
			.ActiveIndex_Lambda([Blur]() -> int32
			{
				const FMixtormatBlur* B = Blur();
				return B ? static_cast<int32>(B->Target) : INDEX_NONE;
			})
			.OnChosen_Lambda([this, Blur, EditableBlur](int32 Index)
			{
				if (!EditableBlur() || Index < 0 || Index > 2) { return; }
				if (FMixtormatBlur* B = Blur())
				{
					B->Target = static_cast<EMixtormatBlurTarget>(Index);
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			})
		];
	AddSliderRow(Settings, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
			if (!Children) { return EVisibility::Collapsed; }
			const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(*Children, ResolveChildIndexAt(Address));
			return Children->IsValidIndex(OwnerIndex)
				&& ((*Children)[OwnerIndex].Type == EMixtormatLayerChildType::Generator
					|| (*Children)[OwnerIndex].Type == EMixtormatLayerChildType::Behavior)
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda(EditableBlur)
		[
			TargetControls
		]);
	TSharedRef<SVerticalBox> Gaussian = SNew(SVerticalBox)
		.Visibility_Lambda([Blur]()
		{
			const FMixtormatBlur* B = Blur();
			return B && B->Mode == EMixtormatBlurMode::Gaussian ? EVisibility::Visible : EVisibility::Collapsed;
		});
	Panel->AddSlot().AutoHeight()[Gaussian];
	TSharedRef<SVerticalBox> Footprint = AddCard(Gaussian, LOCTEXT("BlurFootprintCard", "FOOTPRINT"));
	AddSliderRow(Footprint, MakeMemberSlider<FMixtormatBlur>(
		LOCTEXT("BlurRadiusLabel", "Radius"), Blur, &FMixtormatBlur::Radius,
		RadiusMin, RadiusMax, RadiusDefault, SnapDelta,
		LOCTEXT("BlurRadiusHint", "Master Gaussian radius in composition texels. Affects both axes, multiplied independently by X Scale and Y Scale. Zero leaves the input unchanged.")));
	AddSliderRow(Footprint, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatBlur>(
			LOCTEXT("BlurScaleXLabel", "X Scale"), Blur, &FMixtormatBlur::ScaleX,
			ScaleMin, ScaleMax, ScaleDefault, ScaleSnapDelta,
			LOCTEXT("BlurScaleXHint", "Horizontal radius multiplier. One uses the master Radius; zero disables horizontal filtering. No automatic X/Y link is needed.")),
		MakeMemberSlider<FMixtormatBlur>(
			LOCTEXT("BlurScaleYLabel", "Y Scale"), Blur, &FMixtormatBlur::ScaleY,
			ScaleMin, ScaleMax, ScaleDefault, ScaleSnapDelta,
			LOCTEXT("BlurScaleYHint", "Vertical radius multiplier. One uses the master Radius; zero disables vertical filtering. Each scale remains independently bindable."))));

	TSharedRef<SVerticalBox> Directional = SNew(SVerticalBox)
		.Visibility_Lambda([Blur]()
		{
			const FMixtormatBlur* B = Blur();
			return B && B->Mode != EMixtormatBlurMode::Gaussian ? EVisibility::Visible : EVisibility::Collapsed;
		});
	Panel->AddSlot().AutoHeight()[Directional];
	TSharedRef<SVerticalBox> Direction = AddCard(Directional, LOCTEXT("BlurDirectionCard", "DIRECTION"));
	AddSliderRow(Direction, SNew(SBox)
		.IsEnabled_Lambda(EditableBlur)
		[
			SNew(SMixtormatSegmentedControl)
			.Options(TArray<FText>{LOCTEXT("BlurDirectionAngle", "Angle"),
				LOCTEXT("BlurDirectionFlow", "Flow")})
			.ToolTips(TArray<FText>{
				LOCTEXT("BlurDirectionAngleHint", "Follow Angle; no Flow connection required."),
				LOCTEXT("BlurDirectionFlowHint", "Blend Angle with a connected, completed Flow source. Unavailable Flow leaves the filter unchanged.")})
			.ActiveIndex_Lambda([Blur]() -> int32
			{
				const FMixtormatBlur* B = Blur();
				return B ? static_cast<int32>(B->DirectionSource) : INDEX_NONE;
			})
			.OnChosen_Lambda([this, Blur, EditableBlur](int32 Index)
			{
				if (!EditableBlur() || Index < 0 || Index > 1) { return; }
				if (FMixtormatBlur* B = Blur())
				{
					B->DirectionSource = static_cast<EMixtormatBlurDirectionSource>(Index);
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			})
		]);
	AddSliderRow(Direction, SNew(SBox)
		.Visibility_Lambda([Blur]()
		{
			const FMixtormatBlur* B = Blur();
			return B && B->Mode == EMixtormatBlurMode::Smear
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda(EditableBlur)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MixtormatRow::MakeCaption(LOCTEXT("BlurSmearCombineCaption", "TRAIL COMBINE"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.SegmentedControlGap)
			[
				SNew(SMixtormatSegmentedControl)
				.Options(TArray<FText>{LOCTEXT("BlurSmearAverage", "Average"),
					LOCTEXT("BlurSmearMin", "Min"), LOCTEXT("BlurSmearMax", "Max")})
				.ToolTips(TArray<FText>{
					LOCTEXT("BlurSmearAverageHint", "Average valid samples along the one-sided trail."),
					LOCTEXT("BlurSmearMinHint", "Use the minimum of sampled trail values; erodes scalar height and coverage."),
					LOCTEXT("BlurSmearMaxHint", "Use the maximum of sampled trail values; dilates scalar height and coverage.")})
				.ActiveIndex_Lambda([Blur]() -> int32
				{
					const FMixtormatBlur* B = Blur();
					return B ? static_cast<int32>(B->SmearCombine) : INDEX_NONE;
				})
				.OnChosen_Lambda([this, Blur, EditableBlur](int32 Index)
				{
					if (!EditableBlur() || Index < 0 || Index > 2) { return; }
					if (FMixtormatBlur* B = Blur())
					{
						B->SmearCombine = static_cast<EMixtormatSmearCombine>(Index);
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				})
			]
		]);
	AddSliderRow(Direction, MakeMemberSlider<FMixtormatBlur>(
		LOCTEXT("BlurLengthLabel", "Length"), Blur, &FMixtormatBlur::Length,
		LengthMin, LengthMax, LengthDefault, SnapDelta,
		LOCTEXT("BlurLengthHint", "Path extent in composition texels. Directional uses a nonnegative extent on both sides. Smear preserves the sign: positive trails opposite travel; negative reverses it. Zero leaves the input unchanged.")));
	AddSliderRow(Direction, MakeMemberSlider<FMixtormatBlur>(
		LOCTEXT("BlurAngleLabel", "Angle"), Blur, &FMixtormatBlur::Angle,
		AngleMin, AngleMax, AngleDefault, AngleSnapDelta,
		LOCTEXT("BlurAngleHint", "Texture-space direction in degrees; also the base direction blended with normalized Flow. Flow magnitude does not change Length.")));
	TSharedRef<SVerticalBox> Flow = SNew(SVerticalBox)
		.Visibility_Lambda([Blur]()
		{
			const FMixtormatBlur* B = Blur();
			return B && B->DirectionSource == EMixtormatBlurDirectionSource::Flow
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda(EditableBlur);
	Direction->AddSlot().AutoHeight()[Flow];
	AddSliderRow(Flow, MixtormatRow::MakeDropdown(
		LOCTEXT("BlurFlowSourceLabel", "Flow Source"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this, Blur]()
		{
			const FMixtormatBlur* B = Blur();
			if (!B || !B->FlowSource.HasSource()) { return LOCTEXT("BlurFlowUnset", "Choose Flow source"); }
			const FMixtormatOutputReference& Ref = B->FlowSource;
			if (!CanSelectBlurFlowSource(GetSelectedChildAddress(), Ref))
			{
				return FText::Format(LOCTEXT("BlurFlowUnavailable", "Unavailable / {0}"), FText::FromName(Ref.OutputName));
			}
			const FMixtormatLayerChild* Producer = MixtormatParameterBinding::FindChild(
				FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, Ref.SourceLayerId, Ref.SourceChildId);
			return FText::Format(LOCTEXT("BlurFlowSelected", "{0} / {1}"),
				Producer ? GetLayerChildName(*Producer) : FText::GetEmpty(), FText::FromName(Ref.OutputName));
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBlurFlowSourceMenu)),
		LOCTEXT("BlurFlowSourceHint", "Choose a typed Flow output from a completed earlier producer scope. Disabled, forward, self/owner and incomplete sources are unavailable. Group filters require availability in every member.")));
	AddSliderRow(Flow, MakeMemberSlider<FMixtormatBlur>(
		LOCTEXT("BlurFlowInfluenceLabel", "Flow Influence"), Blur, &FMixtormatBlur::FlowInfluence,
		FlowInfluenceMin, FlowInfluenceMax, FlowInfluenceDefault, FlowInfluenceSnapDelta,
		LOCTEXT("BlurFlowInfluenceHint", "Direction steering only: zero follows Angle, one follows normalized Flow. A cancelled direction has no displacement. Amount and layer channel weights control the final mix, not steering.")));

	TSharedRef<SVerticalBox> AmountPanel = AddCard(Panel, LOCTEXT("BlurAmountCard", "AMOUNT"));
	AddSliderRow(AmountPanel, MakeMemberSlider<FMixtormatBlur>(
		LOCTEXT("BlurAmountLabel", "Amount"), Blur, &FMixtormatBlur::Amount,
		AmountMin, AmountMax, AmountDefault, AmountSnapDelta,
		LOCTEXT("BlurAmountHint", "How much of the completed filter replaces the original input. Zero keeps the input unchanged; one uses the fully filtered result.")));

	TSharedRef<SVerticalBox> ChannelsPanel = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const FMixtormatLayerChild* Child = ResolveChildAt(Address);
			return Address.OwnerType == EMixtormatChildOwnerType::Layer
				&& Child && Child->Type == EMixtormatLayerChildType::Blur
				&& !Child->ScopeOwnerChildId.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
		});
	Panel->AddSlot().AutoHeight()[ChannelsPanel];
	TSharedRef<SVerticalBox> Channels = AddCard(ChannelsPanel, LOCTEXT("BlurChannelsCard", "CHANNELS"));
	const auto ChannelSlider = [this, Blur](const FText& Label, float FMixtormatBlur::* Member)
	{
		return MakeMemberSlider<FMixtormatBlur>(Label, Blur, Member,
			AmountMin, AmountMax, AmountDefault, AmountSnapDelta,
			LOCTEXT("BlurChannelWeightHint", "Blends this channel toward the completed filter result, multiplied by Amount. Zero keeps the original channel; one applies the full Amount."));
	};
	AddSliderRow(Channels, MixtormatRow::MakePair(
		ChannelSlider(LOCTEXT("BlurBaseColorLabel", "Base Color"), &FMixtormatBlur::BaseColorWeight),
		ChannelSlider(LOCTEXT("BlurNormalLabel", "Normal"), &FMixtormatBlur::NormalWeight)));
	AddSliderRow(Channels, MixtormatRow::MakePair(
		ChannelSlider(LOCTEXT("BlurRoughnessLabel", "Roughness"), &FMixtormatBlur::RoughnessWeight),
		ChannelSlider(LOCTEXT("BlurMetallicLabel", "Metallic"), &FMixtormatBlur::MetallicWeight)));
	AddSliderRow(Channels, MixtormatRow::MakePair(
		ChannelSlider(LOCTEXT("BlurAOChannelLabel", "AO"), &FMixtormatBlur::AOWeight),
		ChannelSlider(LOCTEXT("BlurF0Label", "Specular / F0"), &FMixtormatBlur::F0Weight)));
	AddSliderRow(Channels, ChannelSlider(
		LOCTEXT("BlurHeightLabel", "Height"), &FMixtormatBlur::HeightWeight));

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedLayerBlur() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("MaskBlurHeading", "BLUR")).PresetAction(MakeModulePresetButton())
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

	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return GetSelectedLayerMask() ? EVisibility::Visible : EVisibility::Collapsed; });
	TSharedRef<SVerticalBox> Panel = AddCard(Cards, LOCTEXT("MaskSourceCard", "SOURCE"));

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
			return M && !M->UsesLayerValues() && !M->UsesNoise() && !M->HasPublishedSource()
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

	// A published/gating mask reads an existing child output. Switching to Texture/Noise/Values
	// discards that reference, with no way to restore the original output from these buttons.
	// Show the resolved connection instead; only standalone masks select their inline source.
	AddSliderRow(Panel, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && M->HasPublishedSource() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MixtormatRow::Make(LOCTEXT("PublishedGateSource", "Gate Source"),
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
				.Text_Lambda([this]() { return GetMaskSourceLabel(GetSelectedChildAddress()); }))
		]);

	// Exclusive source kinds use the same segmented group as the layer composition mode.
	// These are for standalone masks only; published connections are displayed above.
	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f,
		FMixtormatThemeStore::GetResolved().ControlLayout.SegmentedControlGap)
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && !M->HasPublishedSource() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
			return Child && !Child->IsInstance();
		})
		[
			SNew(SMixtormatSegmentedControl)
			.Options(TArray<FText>{LOCTEXT("MaskSourceTextureChoice", "Texture"),
				LOCTEXT("MaskSourceNoiseChoice", "Noise"),
				LOCTEXT("MaskSourceValuesChoice", "Layer Values")})
			.ToolTips(TArray<FText>{
				LOCTEXT("MaskSourceTextureHint", "Sample an assigned mask texture."),
				LOCTEXT("MaskSourceNoiseHint", "Produce inline tileable Noise coverage."),
				LOCTEXT("MaskSourceValuesHint", "Read the selected material layer channel.")})
			.ActiveIndex_Lambda([this]() -> int32
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				if (!M || M->HasPublishedSource()) { return INDEX_NONE; }
				switch (M->Source)
				{
				case EMixtormatMaskSource::Texture: return 0;
				case EMixtormatMaskSource::Noise: return 1;
				case EMixtormatMaskSource::LayerValues: return 2;
				default: return INDEX_NONE;
				}
			})
			.OnChosen_Lambda([this](const int32 Index)
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				if (!M || Index < 0 || Index > 2) { return; }
				const EMixtormatMaskSource Kind = Index == 0
					? EMixtormatMaskSource::Texture
					: Index == 1 ? EMixtormatMaskSource::Noise : EMixtormatMaskSource::LayerValues;
				SelectMaskSource(GetSelectedChildAddress(), Kind);
			})
		]
	];


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
					"Input Levels, Curve, Offset and Invert under Shaping control the final mask."))
		]);


	// Keep all placement transforms below source selection, blending and shaping.
	AddSliderRow(Cards, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && M->UsesNoise() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
			return Child && !Child->IsInstance();
		})
		[
			BuildNoisePatternPlacementControls([this]() -> FMixtormatNoise*
			{
				FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M && M->UsesNoise() ? &M->Noise : nullptr;
			}, true)
		]);

	// Source placement. Every transform here maps the unit square onto itself, which is the
	// constraint: the read is wrapped in a frac(), so anything else seams at the repeat.
	//
	// Texture masks only. A Layer Values mask reads the layer it is on at the composition's own
	// resolution -- there is no map to tile, offset, flip or turn -- so the whole block is
	// collapsed rather than shown inert. Published and inline-Noise coverage are still sampled
	// maps, so their placement stays meaningful. This is the other half of removing the Source
	// dropdown: the inspector is now specific to what the mask actually reads.
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


	AddMaskShapingRows(Cards, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatMaskLayer* M = GetSelectedLayerMask();
		return M ? &M->Shaping : nullptr;
	});

	AddSliderRow(Cards, PlacementCard);

	const TSharedRef<SVerticalBox> Blend = AddCard(Cards, LOCTEXT("MaskBlendCard", "BLENDING"));
	AddSliderRow(Blend, MixtormatRow::MakeDropdown(
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

	AddSliderRow(Blend, MakeMemberSlider<FMixtormatMaskLayer>(
		LOCTEXT("SelectedMaskWeight", "Weight"), Mask, &FMixtormatMaskLayer::Weight, 0.0, 1.0, 1.0, 0.01));

	AddMaskOutputRangeRows(Blend, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatMaskLayer* Selected = GetSelectedLayerMask();
		return Selected ? &Selected->Shaping : nullptr;
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
			.Title(LOCTEXT("LayerMaskHeading", "MASK")).PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
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
	// Construct the ramp separately; nested Slate slots inside AddSliderRow can
	// obscure the actual type error at the widget argument.
	const TSharedRef<SWidget> CurveEditor =
			SNew(SMixtormatScalarRamp)
		.Ramp_Lambda([Resolve]()
		{
			const FMixtormatMaskShaping* Shaping = Resolve();
			return Shaping ? Shaping->CurveBias : FMixtormatScalarRamp();
		})
		.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
		.bInverted_Lambda([Resolve]() { const FMixtormatMaskShaping* S = Resolve(); return S && S->bInvert; })
		.OnInvertChanged_Lambda([this, Resolve](ECheckBoxState State)
		{
			if (FMixtormatMaskShaping* S = Resolve())
			{
				S->bInvert = State == ECheckBoxState::Checked;
				RefreshLayeredPreview();
			}
		})
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
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	const TSharedRef<SVerticalBox> CurveRows = SNew(SVerticalBox);
	CurveRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGapTight)
	[
		SNew(STextBlock)
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.Text(LOCTEXT("MaskCurveBiasLabel", "Curve"))
	];
	CurveRows->AddSlot().AutoHeight()[CurveEditor];
	AddSliderRow(TargetPanel, CurveRows);
	AddSliderRow(TargetPanel, MakeMemberSlider<FMixtormatMaskShaping>(
		LOCTEXT("MaskOffsetLabel", "Offset"), Resolve, &FMixtormatMaskShaping::Offset,
		-1.0, 1.0, 0.0, 0.01,
		LOCTEXT("MaskOffsetHint", "Offsets the curved mask before clamping to 0..1.")));
}

TSharedRef<SWidget> SMixtormat::BuildMaskOutputRangeMenu(
	TFunction<FMixtormatMaskShaping*()> Resolve)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskOutputRange Ranges[] = {
		EMixtormatMaskOutputRange::Clamp,
		EMixtormatMaskOutputRange::Unclamped,
		EMixtormatMaskOutputRange::Mirror
	};
	for (const EMixtormatMaskOutputRange Range : Ranges)
	{
		Menu.Item(
			MixtormatUI::MaskOutputRangeText(Range),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Resolve, Range]()
			{
				if (FMixtormatMaskShaping* S = Resolve())
				{
					S->OutputRange = Range;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([Resolve, Range]()
			{
				const FMixtormatMaskShaping* S = Resolve();
				return S && S->OutputRange == Range;
			}));
	}
	return Menu.Build();
}

void SMixtormat::AddMaskOutputRangeRows(
	const TSharedRef<SVerticalBox>& BlendingCard,
	TFunction<FMixtormatMaskShaping*()> Resolve)
{
	AddSliderRow(BlendingCard, MixtormatRow::MakeDropdown(
		LOCTEXT("MaskOutputRangeLabel", "Output Range"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([Resolve]()
			{
				const FMixtormatMaskShaping* S = Resolve();
				return S ? MixtormatUI::MaskOutputRangeText(S->OutputRange) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskOutputRangeMenu, Resolve),
			nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("MaskOutputRangeHint", "Controls how blended scalar values are output. Clamp clips to 0..1, Unclamped preserves negative and HDR values, Mirror reflects values into 0..1.")));

	AddSliderRow(BlendingCard, SNew(SBox)
		.Visibility_Lambda([Resolve]()
		{
			const FMixtormatMaskShaping* S = Resolve();
			return (S && S->OutputRange == EMixtormatMaskOutputRange::Mirror)
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MakeMemberSlider<FMixtormatMaskShaping>(
				LOCTEXT("MaskMirrorScaleLabel", "Mirror Scale"), Resolve,
				&FMixtormatMaskShaping::MirrorScale, 0.1, 10.0, 1.0, 0.05,
				LOCTEXT("MaskMirrorScaleHint", "Frequency of scalar reflection across the range, folding overflow values back into 0..1."))
		]);
}

#undef LOCTEXT_NAMESPACE
