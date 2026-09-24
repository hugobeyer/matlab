// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Atoms/SMixtormatToggle.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatTile.h"
#include "Widgets/Input/SSpinBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UnrealClient.h"
#include "Widgets/Images/SImage.h"

// The inspector column: every per-selection parameter panel.

#define LOCTEXT_NAMESPACE "SMixtormat"

// The Exact ID picker's click surface: the Region IDs preview drawn at a fixed square size, with
// the whole image as one target. Local position over local size is the UV, which is the entire
// reason the picker is a flat image rather than a click in the 3D viewport -- recovering a UV
// from a mesh hit needs a project-wide setting this plugin cannot guarantee, and fails silently
// where it is off.
class SMixtormatRegionIdPickSurface final : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnPicked, FVector2D);

	SLATE_BEGIN_ARGS(SMixtormatRegionIdPickSurface) {}
		SLATE_ARGUMENT(TSharedPtr<FSlateBrush>, Brush)
		SLATE_EVENT(FOnPicked, OnPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnPicked = InArgs._OnPicked;
		ChildSlot
		[
			SNew(SImage).Image(InArgs._Brush.IsValid() ? InArgs._Brush.Get() : nullptr)
		];
	}

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0)
		{
			return FReply::Unhandled();
		}
		OnPicked.ExecuteIfBound(FVector2D(
			FMath::Clamp(Local.X / Size.X, 0.0, 1.0),
			FMath::Clamp(Local.Y / Size.Y, 0.0, 1.0)));
		return FReply::Handled();
	}

private:
	FOnPicked OnPicked;
};

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
	AddPeelSlider(Panel, LOCTEXT("PPeelAO", "Contact AO"), &FMixtormatLayerEffect::PeelAOStrength, 0.0, 1.0, 0.8, 0.01);
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

// Ten sliders, and that is the whole effect.
//
// Runoff has as many derived constants behind it as Stain has exposed controls -- strata count,
// spacing, length step, opacity falloff, warp contrast, lip width, three noise parameters and the
// breakup blend. None of them are here. The test for whether a number belongs on this panel is
// whether an artist looking at a dirty wall would have an opinion about it, and nobody has an
// opinion about lacunarity.
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



// The mask blur's opposite number, so the controls deliberately read the same: two radii, and a
// weight. Scope is the one thing it has that the mask blur cannot, because a mask has no notion
// of what is underneath it.
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
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupAO", "AO"), &FMixtormatLayerEffect::BreakupAOAmount,
			0.0, 1.0, 0.35, 0.01,
			LOCTEXT("BreakupAOHint", "Local contact occlusion from cavities, SDF seams and piece-ID boundaries.")),
		Slider(LOCTEXT("BreakupAORadius", "AO Radius"), &FMixtormatLayerEffect::BreakupAORadius,
			1.0, 32.0, 8.0, 0.25)));
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

// Exact ID first, Color Range second -- the order the node is explained in, not the order the
// enum happens to be numbered. Color Range is still value 0 and still the default, so nothing
// saved before the mode existed changes behaviour.
TSharedRef<SWidget> SMixtormat::BuildColorIdModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatColorIdMode Modes[] = {
		EMixtormatColorIdMode::ExactId,
		EMixtormatColorIdMode::ColorRange,
	};
	for (const EMixtormatColorIdMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::ColorIdModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->Mode = Mode;
					RefreshLayeredPreview();
					// The row's own kind text does not move, but the inspector swaps a whole
					// section either way, and the preview has to be asked for again.
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
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

bool SMixtormat::CanPickRegionId() const
{
	// The pick buffer is written only while a Region IDs preview is being composited, and cleared
	// to the sentinel otherwise, so offering the picker at any other time would hand the artist a
	// black square and a value of -1. The message the popover shows instead names the eye to turn
	// on, which is the actual next step.
	return DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput
		&& ChildPreviewTarget.Kind == EMixtormatPreviewOutputKind::RegionIds
		&& ChildPreviewTarget.IsValid()
		&& !PreviewViewports.IsEmpty()
		&& PreviewViewports[0].IsValid();
}

bool SMixtormat::ReadRegionIdAtUV(const FVector2D UV, int32& OutRegionId) const
{
	if (PreviewViewports.IsEmpty() || !PreviewViewports[0].IsValid())
	{
		return false;
	}
	UTextureRenderTarget2D* Pick = PreviewViewports[0]->GetRegionIdPick();
	FTextureRenderTargetResource* Resource =
		Pick ? Pick->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource)
	{
		return false;
	}

	const int32 X = FMath::Clamp(
		FMath::FloorToInt(UV.X * static_cast<double>(Pick->SizeX)), 0, Pick->SizeX - 1);
	const int32 Y = FMath::Clamp(
		FMath::FloorToInt(UV.Y * static_cast<double>(Pick->SizeY)), 0, Pick->SizeY - 1);

	// One texel, read as FLinearColor: the target is PF_R32_FLOAT, so the red channel comes back
	// as the exact float the pick pass wrote. ReadPixels would quantise it to 8 bits, which would
	// turn every id into the same handful of values.
	TArray<FLinearColor> Pixels;
	if (!Resource->ReadLinearColorPixels(
		Pixels, FReadSurfaceDataFlags(), FIntRect(X, Y, X + 1, Y + 1))
		|| Pixels.IsEmpty())
	{
		return false;
	}

	const float Raw = Pixels[0].R;
	if (Raw < 0.0f)
	{
		// The no-region sentinel. Grout, or a pixel outside every region -- there is no id to
		// take, so the field keeps whatever it had.
		return false;
	}
	OutRegionId = FMath::RoundToInt(Raw);
	return true;
}

void SMixtormat::PickRegionIdAtUV(const FVector2D UV)
{
	int32 PickedId = 0;
	if (!ReadRegionIdAtUV(UV, PickedId))
	{
		return;
	}
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		C->ExactRegionId = PickedId;
		RefreshLayeredPreview();
	}
}

TSharedRef<SWidget> SMixtormat::BuildRegionIdPickerPopup()
{
	if (!CanPickRegionId())
	{
		return SNew(SBox)
			.Padding(MixtormatTokens::TileGap)
			.WidthOverride(MixtormatTokens::MaskPickerWidth * 0.5f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("RegionIdPickerUnavailable",
					"Turn on the Region IDs preview on the Pattern IDs, Cluster IDs or Combine IDs "
					"node above this mask -- the eye in its inspector header -- then open this "
					"again. The picker reads the ID map that preview is built from."))
			];
	}

	// The composite's own debug target: exactly the pixels the viewport is showing, so the region
	// clicked here is the region seen there. The brush is held on the panel rather than rebuilt
	// per open, because a brush pointing at a render target has to outlive the widget drawing it.
	if (!RegionIdPreviewBrush.IsValid())
	{
		RegionIdPreviewBrush = MakeShared<FSlateBrush>();
	}
	UTextureRenderTarget2D* DebugTarget = PreviewViewports[0]->GetCompositedDebug();
	RegionIdPreviewBrush->SetResourceObject(DebugTarget);
	RegionIdPreviewBrush->ImageSize = FVector2D(
		MixtormatTokens::MaskPickerWidth, MixtormatTokens::MaskPickerWidth);

	const float ViewSize = MixtormatTokens::MaskPickerWidth;
	return SNew(SBox)
		.Padding(MixtormatTokens::TileGap)
		.WidthOverride(ViewSize)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("RegionIdPickerHint",
					"Click a region to take its ID. Anywhere with no region -- grout, or a gap -- "
					"leaves the current ID alone, and so does closing this without clicking."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(ViewSize)
				.HeightOverride(ViewSize)
				[
					// The whole image is one click target. Local position over local size is the
					// UV directly -- the debug target is the composition, square and unrotated
					// relative to itself, and the pick buffer went through the same quarter turn
					// the debug view did, so the two are always in the same frame.
					SNew(SMixtormatRegionIdPickSurface)
					.Brush(RegionIdPreviewBrush)
					.OnPicked_Lambda([this](const FVector2D UV)
					{
						PickRegionIdAtUV(UV);
						FSlateApplication::Get().DismissAllMenus();
					})
				]
			]
		];
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
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
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

	// SELECTION first, because it decides what the rest of this panel is even about.
	//
	// Exact ID compares the integer Region ID published by the nearest ID node above this mask --
	// Pattern IDs, Cluster IDs or Combine IDs -- and is the mode to reach for when the regions
	// were generated in this stack. Color Range samples an authored ID map and accepts whatever
	// lands within Threshold of a chosen colour, which is the mode for a map that arrived with the
	// mesh. The two share nothing but the blend and shaping tail, so each hides the other's rows
	// rather than leaving half the panel inert.
	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("IdGrpSelection", "Selection")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("IdSelectionMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::ColorIdModeText(C->Mode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdModeMenu)),
		LOCTEXT("IdSelectionModeHint",
			"Exact ID selects one discrete Region ID from the ID node above this mask, as an "
			"integer comparison -- so the selection is unaffected by whatever colour the Region "
			"IDs preview happens to paint that region, and unaffected by re-seeding the preview. "
			"Color Range compares the sampled colour of an authored ID map against a chosen "
			"colour, within Threshold and feathered by Width.")));

	// Exact ID only. A numeric entry rather than a slider: Region IDs are pixel indices, so the
	// useful range runs to the square of the composition resolution and no slider can address it
	// meaningfully. Turn on the Region IDs preview eye on the node above to see which regions
	// exist while you set this.
	AddSliderRow(Panel, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Mode == EMixtormatColorIdMode::ExactId
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::Make(
				LOCTEXT("IdExactRegion", "Region ID"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, MixtormatTokens::TileGap, 0.0f)
				[
					// The eyedropper. Opens the live Region IDs preview and takes the integer id
					// of whatever is clicked -- never a colour, and never a guess at which id a
					// colour came from.
					SNew(SMixtormatChip)
					.Text(LOCTEXT("IdExactPick", "Pick"))
					.ToolTip(LOCTEXT("IdExactPickHint",
						"Click a region in the Region IDs preview to take its ID. Needs that "
						"preview turned on, from the eye in the inspector header of the ID node "
						"above this mask. Closing the popover without clicking changes nothing."))
					.OnGetMenuContent(
						FOnGetContent::CreateSP(this, &SMixtormat::BuildRegionIdPickerPopup))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::RowFieldMinWidth)
				[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MinSliderValue(0)
				.MaxSliderValue(4096)
				.Delta(1)
				.Value_Lambda([this]()
				{
					const FMixtormatColorIdMask* C = GetSelectedColorId();
					return C ? C->ExactRegionId : 0;
				})
				.OnValueChanged_Lambda([this](const int32 NewValue)
				{
					if (FMixtormatColorIdMask* C = GetSelectedColorId())
					{
						C->ExactRegionId = FMath::Max(NewValue, 0);
						RefreshLayeredPreview();
					}
				})
				]
				],
				LOCTEXT("IdExactRegionHint",
					"The Region ID to select. Compared as an integer against the map published by "
					"the nearest ID node above -- never reconstructed from a preview colour, so it "
					"survives a change of seed or of preview palette. A mask in this mode with no "
					"ID node above it is skipped rather than blended, the same as Random From IDs."))
		]);

	// Everything from here to the end of Placement belongs to Color Range. An Exact ID mask has
	// no map to pick, no colours to list and nothing to place: it reads the Region IDs at the
	// composition's own resolution, where a UV transform would interpolate labels.
	const TSharedRef<SVerticalBox> Range = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Mode == EMixtormatColorIdMode::ExactId
				? EVisibility::Collapsed
				: EVisibility::Visible;
		});

	// The map. Any Texture2D rather than the library gallery the other mask slots offer: an ID
	// map arrives with the mesh from whatever built it, and it is not a Mixtormat asset and never
	// will be.
	AddSliderRow(Range, MixtormatRow::MakeCaption(LOCTEXT("IdGrpSource", "ID Map")));
	Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
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
	AddSliderRow(Range, MixtormatRow::MakeCaption(LOCTEXT("IdGrpColors", "Selected IDs")));

	// Every row that could exist is laid out once and shows itself when the selection reaches it.
	// The panel is built at construction, long before anything is selected, so a loop over the
	// current entries would bake in whatever the count happened to be then -- which is zero.
	for (int32 ColorIndex = 0; ColorIndex < FMixtormatColorIdMask::MaxColors; ++ColorIndex)
	{
		{
			Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
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

	Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
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

	AddSliderRow(Range, MixtormatRow::MakePair(
		// Threshold and Width, not Tolerance and Softness. The fields keep their serialised names
		// -- nothing saved moves -- but the labels say what they do: one sets where acceptance
		// cuts off, the other how wide the transition around it is.
		Slider(LOCTEXT("IdThreshold", "Threshold"), &FMixtormatColorIdMask::Tolerance, 0.0, 1.0, 0.10, 0.001,
			LOCTEXT("IdThresholdHint", "How far from a selected colour still counts, as a distance in RGB. The diagonal of the colour cube is about 1.73, so this is small by nature: the default admits the wobble a compressed map leaves across a flat region without reaching a neighbouring ID. Raise it until the part fills in; if it starts claiming its neighbours, the map wants a cleaner import rather than a wider tolerance.")),
		Slider(LOCTEXT("IdWidth", "Width"), &FMixtormatColorIdMask::Softness, 0.0, 0.5, 0.02, 0.001,
			LOCTEXT("IdWidthHint", "Width of the transition either side of Threshold. The map is point sampled -- the average of two IDs is a third colour that names neither -- so the selection edge is a hard texel boundary, and this is what feathers it."))));

	AddSliderRow(Range, MixtormatRow::MakeCaption(LOCTEXT("IdGrpPlacement", "Placement")));
	AddSliderRow(Range, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingX", "Tiling X"), Id, &FMixtormatColorIdMask::TilingX, 1.0, 16.0, 1,
			LOCTEXT("IdTilingXHint", "Integer only. A fractional scale lands mid-texel at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingY", "Tiling Y"), Id, &FMixtormatColorIdMask::TilingY, 1.0, 16.0, 1,
			LOCTEXT("IdTilingYHint", "Integer only, for the same reason as Tiling X."))));
	AddSliderRow(Range, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdOffsetU", "Offset U"), &FMixtormatColorIdMask::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetUHint", "Moves where the map is read from, in UV. Unrelated to Offset under Blend, which lifts the mask value instead.")),
		Slider(LOCTEXT("IdOffsetV", "Offset V"), &FMixtormatColorIdMask::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetVHint", "Moves where the map is read from, in UV."))));
	AddSliderRow(Range, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipU", "Flip U"), Id, &FMixtormatColorIdMask::bFlipU,
			LOCTEXT("IdFlipUHint", "Mirrors the map horizontally before it is tiled.")),
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipV", "Flip V"), Id, &FMixtormatColorIdMask::bFlipV,
			LOCTEXT("IdFlipVHint", "Mirrors the map vertically before it is tiled. The usual fix when a map was authored under the other texture-coordinate convention."))));
	AddSliderRow(Range, MixtormatRow::Make(
		LOCTEXT("IdRotation", "Rotation"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::UVRotationText(C->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdRotationMenu)),
		LOCTEXT("IdRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams.")));

	AddSliderRow(Panel, Range);

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
	AddSliderRow(Panel, Slider(LOCTEXT("IdWeight", "Weight"), &FMixtormatColorIdMask::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("IdWeightHint", "How far the blend is taken. 0 is the off switch for this node, and it costs nothing -- the pass is skipped rather than run to reproduce its input.")));
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatColorIdMask* ColorId = GetSelectedColorId();
		return ColorId ? &ColorId->Shaping : nullptr;
	});

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
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
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
		EMixtormatPatternMode::FrenchAshlar,
		EMixtormatPatternMode::FracturePlates
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

	// Amounts are neutral at zero, not at the legacy struct defaults (which apply relief
	// and shading). Keep authored, currently dormant modifiers accessible as well.
	const auto HasLegacyTreatment = [Pattern]()
	{
		const FMixtormatPatternFilter* P = Pattern();
		if (!P)
		{
			return false;
		}
		const FMixtormatPatternFilter Defaults;
		return P->bUVVariation
			|| P->bOrthogonalUV != Defaults.bOrthogonalUV
			|| P->UVRotationMin != Defaults.UVRotationMin
			|| P->UVRotationMax != Defaults.UVRotationMax
			|| P->UVScaleMin != Defaults.UVScaleMin
			|| P->UVScaleMax != Defaults.UVScaleMax
			|| P->UVOffset != Defaults.UVOffset
			|| P->bRandomFlipU != Defaults.bRandomFlipU
			|| P->bRandomFlipV != Defaults.bRandomFlipV
			|| P->HeightAmount != 0.0f
			|| P->GapHeight != 0.0f
			|| P->BevelHeight != 0.0f
			|| P->EdgeRoughnessAmount != 0.0f
			|| P->AOAmount != 0.0f
			|| P->HeightRandom != Defaults.HeightRandom
			|| P->Profile != Defaults.Profile
			|| P->ProfileRandom != Defaults.ProfileRandom
			|| P->Feather != Defaults.Feather
			|| P->FeatherRandom != Defaults.FeatherRandom
			|| P->FeatherGain != Defaults.FeatherGain
			|| P->bRelativeEdgeWidth != Defaults.bRelativeEdgeWidth
			|| P->BevelWidthPixels != Defaults.BevelWidthPixels
			|| P->BevelWidthCells != Defaults.BevelWidthCells
			|| P->BevelVariation != Defaults.BevelVariation
			|| P->BevelInsetPixels != Defaults.BevelInsetPixels
			|| P->EdgeRoughness != Defaults.EdgeRoughness
			|| P->AOSpread != Defaults.AOSpread;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> LegacyTreatment = SNew(SVerticalBox);

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
		LOCTEXT("PatternModeHint", "Selects the procedural topology used to publish Pattern regions. Fracture Plates generates hierarchical irregular fracture plates for cracked plaster, concrete, asphalt, stone, and similar broken surfaces.")));
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
				|| Pattern->PatternMode == EMixtormatPatternMode::Voronoi
				|| Pattern->PatternMode == EMixtormatPatternMode::FracturePlates);
		})
		[
			Slider(LOCTEXT("PatternJitter", "Jitter"), &FMixtormatPatternFilter::Jitter, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("PatternJitterHint", "Varies Running Bond bricks, or the feature points used by Flagstone, Voronoi and Fracture Plates."))
		]));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternSwapAxes", "Swap Axes"), &FMixtormatPatternFilter::bSwapAxes,
			LOCTEXT("PatternSwapAxesHint", "Swaps the lattice axes without changing the ID contract; useful for bars and directional patterns.")),
		Slider(LOCTEXT("PatternGap", "Gap"), &FMixtormatPatternFilter::GapPixels, 0.0, 64.0, 0.0, 0.25,
			LOCTEXT("PatternGapHint", "Region-less grout width in output pixels. Gap pixels emit the invalid-region sentinel, so HSV/Random/Ramp From IDs pass through there."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternRounding", "Rounding"), &FMixtormatPatternFilter::Rounding, 0.0, 1.0, 0.0, 0.005,
			LOCTEXT("PatternRoundingHint", "Rounds the cell corners by blending the two nearest walls instead of taking a hard minimum, so a chamfer fillets into the corner rather than creasing. In cell fractions. 0 is the true Voronoi corner.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternGapRandom", "Gap Random"), &FMixtormatPatternFilter::GapRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternGapRandomHint", "Varies the grout once per piece, for every Pattern Mode. The wall never moves -- each side of it pulls back by its own draw, so the gap between two pieces is the sum of two independent amounts and no two boundaries come out the same width. Symmetric about Gap. A piece is never cut back so far that it disappears, however small it is. Needs a Gap above 0.")),
		Slider(LOCTEXT("PatternGapSlide", "Piece Slide"), &FMixtormatPatternFilter::GapSlide, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternGapSlideHint", "Spends that same grout unevenly around the piece instead of ringing it, so pieces sit off centre in their own sockets. Bounded by each piece's half-gap, so a piece slides only into the room it already has and can never cross into its neighbour. Needs a Gap above 0."))));

	// Fracture Plates' own block, gated the same way Grid Mode is: the mode owns these controls,
	// so they are only in the panel when it is selected. Cells X/Y, Jitter and Seed are the shared
	// lattice rows above and stay there -- Fracture Plates is a Pattern topology like the others,
	// not a second pattern system with its own copy of the lattice.
	const TSharedRef<SVerticalBox> FractureRows = SNew(SVerticalBox);
	AddSliderRow(FractureRows,
		MixtormatRow::MakeCaption(LOCTEXT("PatternGrpFracture", "Fracture Plates")));
	AddSliderRow(FractureRows, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureSizeVariation", "Size Variation"),
			&FMixtormatPatternFilter::FractureSizeVariation, 0.0, 1.0, 0.3, 0.01,
			LOCTEXT("PatternFractureSizeVariationHint", "Spread of the additive power weights that decide how much territory a plate wins from its neighbours. At 0 every plate is the same importance and the result is even pavement; raising it grows a few plates at the expense of the rest, which is where the mix of very large and small pieces comes from. Additive rather than multiplicative, so a weight moves a boundary while leaving it straight.")),
		Slider(LOCTEXT("PatternFractureSecondaryAmount", "Secondary Amount"),
			&FMixtormatPatternFilter::FractureSecondaryAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PatternFractureSecondaryAmountHint", "Probability that a primary plate fractures internally at all. A plate that does not stays whole and publishes one ID, so this is the control for how much of the surface reads as large unbroken pieces against locally shattered ones."))));
	AddSliderRow(FractureRows, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternFractureSecondaryMin", "Secondary Min"), Pattern,
			&FMixtormatPatternFilter::FractureSecondaryMin, 2.0, 8.0, 2,
			LOCTEXT("PatternFractureSecondaryMinHint", "Fewest pieces a fracturing plate breaks into. Below 2 is not a fracture, so this end is bounded.")),
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternFractureSecondaryMax", "Secondary Max"), Pattern,
			&FMixtormatPatternFilter::FractureSecondaryMax, 2.0, 8.0, 3,
			LOCTEXT("PatternFractureSecondaryMaxHint", "Most pieces a fracturing plate breaks into. Raised below Secondary Min, it follows it."))));
	AddSliderRow(FractureRows, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureSecondaryRadius", "Secondary Radius"),
			&FMixtormatPatternFilter::FractureSecondaryRadius, 0.05, 0.75, 0.34, 0.005,
			LOCTEXT("PatternFractureSecondaryRadiusHint", "How far the secondary sites sit from their parent's, in cell fractions. Small values put the split near the middle of the plate; large ones push the pieces out toward its walls. The split is always clipped to its parent, whatever this is set to.")),
		Slider(LOCTEXT("PatternFractureSecondaryJitter", "Secondary Jitter"),
			&FMixtormatPatternFilter::FractureSecondaryJitter, 0.0, 1.0, 0.55, 0.01,
			LOCTEXT("PatternFractureSecondaryJitterHint", "Breaks up the even ring the secondary sites are laid on, in angle and in radius, so a split plate does not come out as a regular pie."))));
	AddSliderRow(FractureRows, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureEdgeIrregularity", "Edge Irregularity"),
			&FMixtormatPatternFilter::FractureEdgeIrregularity, 0.0, 32.0, 8.0, 0.1,
			LOCTEXT("PatternFractureEdgeIrregularityHint", "How far, in output pixels, the fracture field displaces a plate wall from the straight line the power diagram would give it. The field is piecewise planar, so the wall stays a chain of straight runs meeting at angles rather than becoming a curve.")),
		Slider(LOCTEXT("PatternFractureEdgeScale", "Edge Scale"),
			&FMixtormatPatternFilter::FractureEdgeScale, 8.0, 512.0, 96.0, 1.0,
			LOCTEXT("PatternFractureEdgeScaleHint", "The run length of those straight segments, in output pixels. Absolute: Rows and Columns do not stretch it, so changing the plate count leaves the crack character alone."))));
	AddSliderRow(FractureRows, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureEdgeDetail", "Edge Detail"),
			&FMixtormatPatternFilter::FractureEdgeDetail, 0.0, 16.0, 2.5, 0.05,
			LOCTEXT("PatternFractureEdgeDetailHint", "A second, shorter octave of the same field: the small branching kinks that sit on the long primary fracture runs.")),
		Slider(LOCTEXT("PatternFractureEdgeDetailScale", "Edge Detail Scale"),
			&FMixtormatPatternFilter::FractureEdgeDetailScale, 4.0, 128.0, 24.0, 0.5,
			LOCTEXT("PatternFractureEdgeDetailScaleHint", "Run length of the detail octave, in output pixels. Also absolute."))));
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatPatternFilter* SelectedPattern = GetSelectedPatternId();
			return SelectedPattern
				&& SelectedPattern->PatternMode == EMixtormatPatternMode::FracturePlates
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			FractureRows
		]);

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatPatternFilter>(
		LOCTEXT("PatternSeed", "Seed"), Pattern, &FMixtormatPatternFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("PatternSeedHint", "Reshuffles feature jitter and every per-region UV, height and bevel draw while preserving the lattice.")));

	const FText LegacyHint = LOCTEXT("PatternLegacyTreatmentHint", "Legacy Pattern UV/relief settings are preserved for compatibility. New setups should use UV From IDs and Relief From IDs.");
	AddSliderRow(LegacyTreatment,
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(LegacyHint));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpUV", "UV Variation")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternUVEnable", "Enable"), &FMixtormatPatternFilter::bUVVariation,
			LOCTEXT("PatternUVEnableHint", "Transforms the layer source independently around each pattern region centre.")),
		Toggle(LOCTEXT("PatternUVOrthogonal", "90° Only"), &FMixtormatPatternFilter::bOrthogonalUV,
			LOCTEXT("PatternUVOrthogonalHint", "Snaps random region rotation to 90-degree steps, preserving the source tile's periodic orientation."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVRotMin", "Rot Min"), &FMixtormatPatternFilter::UVRotationMin, -360.0, 360.0, 0.0, 1.0,
			LOCTEXT("PatternUVRotMinHint", "Low end of the per-region source rotation range in degrees.")),
		Slider(LOCTEXT("PatternUVRotMax", "Rot Max"), &FMixtormatPatternFilter::UVRotationMax, -360.0, 360.0, 360.0, 1.0,
			LOCTEXT("PatternUVRotMaxHint", "High end of the per-region source rotation range in degrees."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVScaleMin", "Scale Min"), &FMixtormatPatternFilter::UVScaleMin, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMinHint", "Low end of the per-region source scale multiplier.")),
		Slider(LOCTEXT("PatternUVScaleMax", "Scale Max"), &FMixtormatPatternFilter::UVScaleMax, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMaxHint", "High end of the per-region source scale multiplier."))));
	AddSliderRow(LegacyTreatment, Slider(
		LOCTEXT("PatternUVOffset", "Offset"), &FMixtormatPatternFilter::UVOffset, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("PatternUVOffsetHint", "Maximum random source translation per region, as a fraction of one source repeat.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternFlipU", "Flip U"), &FMixtormatPatternFilter::bRandomFlipU,
			LOCTEXT("PatternFlipUHint", "Randomly mirrors the source across U per region.")),
		Toggle(LOCTEXT("PatternFlipV", "Flip V"), &FMixtormatPatternFilter::bRandomFlipV,
			LOCTEXT("PatternFlipVHint", "Randomly mirrors the source across V per region."))));

	AddSliderRow(LegacyTreatment, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpRelief", "Relief")));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternGapHeight", "Gap Height"), &FMixtormatPatternFilter::GapHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternGapHeightHint", "Where the grout sits relative to the cells. Negative sinks it into a trench, positive stands it proud as a raised mortar line. Needs a Gap above 0 -- without one every pixel belongs to a cell and there is nothing outside the IDs to move.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternHeight", "Height"), &FMixtormatPatternFilter::HeightAmount, 0.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternHeightHint", "How far each cell stands off the base. The face stays flat -- for a slope across each cell, stack Ramp From IDs over this.")),
		Slider(LOCTEXT("PatternHeightRandom", "Height Random"), &FMixtormatPatternFilter::HeightRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("PatternHeightRandomHint", "How far below Height a cell may be drawn, as a multiplier. At 0 every cell sits at full Height; at 1 they spread the whole way down to the base. Never negative -- a cell below the base would feather back up at its wall and read as a recessed panel in a raised frame."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternProfile", "Profile"), &FMixtormatPatternFilter::Profile, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileHint", "The chamfer's cross-section, from the grout line up to the flat of the cell. -1 is a cove that hugs the grout then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose that lifts away and rounds over. Never changes the chamfer's width or height.")),
		Slider(LOCTEXT("PatternProfileRandom", "Profile Random"), &FMixtormatPatternFilter::ProfileRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileRandomHint", "Offsets the roundness per cell, so one cell's bullnose can be its neighbour's cove."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFeather", "Feather"), &FMixtormatPatternFilter::Feather, 0.0, 0.5, 0.15, 0.005,
			LOCTEXT("PatternFeatherHint", "Eases each cell's height out at its boundary so neighbouring pieces meet through a ramp rather than a one-texel cliff.")),
		Slider(LOCTEXT("PatternFeatherRandom", "Feather Random"), &FMixtormatPatternFilter::FeatherRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternFeatherRandomHint", "Varies the feather width once per cell, so the run-out is not identical on every piece."))));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternFeatherGain", "Feather Gain"), &FMixtormatPatternFilter::FeatherGain, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("PatternFeatherGainHint", "What the feather does on the way up, rather than how wide it is. The run-out is a straight line, and a straight line is the one shape a normal map cannot show -- a normal reads a change in slope, and a constant ramp has none, so the band lights as a single flat facet however much height it moves. Gain bends the curve: the slope leaving the wall goes from 1 to 1 + Gain, and past 1 it arcs above the face and leaves a raised lip just inside the edge. Both ends stay pinned, so the grout wall and the flat face never move.")));


	AddSliderRow(LegacyTreatment, MixtormatRow::MakeCaption(LOCTEXT("PatternGrpEdges", "Edges")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelHeight", "Height"), &FMixtormatPatternFilter::BevelHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternBevelHeightHint", "Stands each cell proud of the grout, with the chamfer ramping down to it. Negative sinks the cell face below the grout instead. The gap itself is untouched either way -- that is Gap Height.")),
		Slider(LOCTEXT("PatternBevelWidth", "Width"), &FMixtormatPatternFilter::BevelWidthPixels, 0.25, 64.0, 4.0, 0.25,
			LOCTEXT("PatternBevelWidthHint", "Chamfer width, in output pixels or as a fraction of the cell depending on Relative Width below."))));
	AddSliderRow(LegacyTreatment, Toggle(
		LOCTEXT("PatternRelativeEdge", "Relative Width"), &FMixtormatPatternFilter::bRelativeEdgeWidth,
		LOCTEXT("PatternRelativeEdgeHint", "Measures the chamfer as a fraction of the cell instead of in output pixels: 0 at the wall, 1 at the point furthest inside. Frames every cell the same way whatever its size or aspect, and is normalised against how far jitter pushes the deepest interior point. Off keeps an even visual width across cells of different sizes. Width comes from Width (Cells) when on and Width when off.")));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternBevelWidthCells", "Width (Cells)"), &FMixtormatPatternFilter::BevelWidthCells, 0.0, 1.0, 0.25, 0.005,
			LOCTEXT("PatternBevelWidthCellsHint", "The chamfer width used in Relative mode, as a fraction of the way from the cell wall to its deepest interior point. 1 runs the chamfer all the way in, leaving no flat face.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelVariation", "Variation"), &FMixtormatPatternFilter::BevelVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternBevelVariationHint", "Varies bevel width once per region.")),
		Slider(LOCTEXT("PatternBevelInset", "Inset"), &FMixtormatPatternFilter::BevelInsetPixels, -32.0, 32.0, 0.0, 0.25,
			LOCTEXT("PatternBevelInsetHint", "Slides the chamfer across the grout line in output pixels. Negative puts it out in the gap, positive pulls it onto the cell face."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternEdgeRoughness", "Roughness"), &FMixtormatPatternFilter::EdgeRoughness, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("PatternEdgeRoughnessHint", "Roughness value approached at region edges. Applied after the layer composite, so it intentionally bypasses the layer Roughness Influence control.")),
		Slider(LOCTEXT("PatternEdgeRoughnessAmount", "Amount"), &FMixtormatPatternFilter::EdgeRoughnessAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternEdgeRoughnessAmountHint", "Strength of edge roughness. 0 leaves the packed roughness channel unchanged."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternAOAmount", "AO"), &FMixtormatPatternFilter::AOAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternAOAmountHint", "Darkens packed AO at region creases. Applied after the layer composite, so it intentionally bypasses the layer AO Influence control.")),
		Slider(LOCTEXT("PatternAOSpread", "Spread"), &FMixtormatPatternFilter::AOSpread, 1.0, 8.0, 2.0, 0.05,
			LOCTEXT("PatternAOSpreadHint", "How much farther the edge AO reaches relative to the bevel width."))));

	Panel->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, 0.0f)
	[
		SNew(SBox)
		.Visibility_Lambda([HasLegacyTreatment]()
		{
			return HasLegacyTreatment() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("PatternLegacyTreatmentHeading", "LEGACY TREATMENT"))
			.InitiallyExpanded(false)
			.ToolTipText(LegacyHint)
			[
				LegacyTreatment
			]
		]
	];

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
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::PatternId))
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

TSharedRef<SWidget> SMixtormat::BuildUvIdControls()
{
	const auto Uv = [this]() { return GetSelectedUvId(); };

	const auto Slider = [this, Uv](
		const FText& Label,
		float FMixtormatUvIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatUvIdFilter>(Label, Uv, Member, Min, Max, Default, Snap, Hint);
	};

	const auto Checkbox = [this](
		const FText& Label,
		bool FMixtormatUvIdFilter::* Member,
		const FText& Hint)
	{
		return MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Member]()
				{
					const FMixtormatUvIdFilter* U = GetSelectedUvId();
					return U && (U->*Member) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Member](const ECheckBoxState State)
				{
					if (FMixtormatUvIdFilter* U = GetSelectedUvId())
					{
						U->*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("UvIdGrpTransform", "Transform")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdRotationMin", "Rotation Min"), &FMixtormatUvIdFilter::RotationMin, -360.0, 360.0, 0.0, 1.0,
			LOCTEXT("UvIdRotationMinHint", "The low end of each region's rotation draw, in degrees. Equal limits turn every region by the same fixed amount.")),
		Slider(LOCTEXT("UvIdRotationMax", "Rotation Max"), &FMixtormatUvIdFilter::RotationMax, -360.0, 360.0, 360.0, 1.0,
			LOCTEXT("UvIdRotationMaxHint", "The high end of that draw. The angle is taken about the region's own centre, which is measured from the ID map rather than supplied by the producer -- so this works after Pattern IDs, Cluster IDs or Combine IDs alike."))));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdOrthogonal", "Orthogonal"),
		&FMixtormatUvIdFilter::bOrthogonal,
		LOCTEXT("UvIdOrthogonalHint", "Snaps the drawn rotation to quarter turns. A quarter turn is a permutation of the unit square, so it never disturbs the source tiling; an arbitrary angle can. This is a snap on the random draw, not a recovered intrinsic axis -- an arbitrary region has no direction to recover.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdScaleMin", "Scale Min"), &FMixtormatUvIdFilter::ScaleMin, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("UvIdScaleMinHint", "The low end of each region's zoom on the source. Below 1 magnifies the texture inside the region; above 1 fits more of it in.")),
		Slider(LOCTEXT("UvIdScaleMax", "Scale Max"), &FMixtormatUvIdFilter::ScaleMax, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("UvIdScaleMaxHint", "The high end of that draw. Equal limits give every region the same zoom."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdOffsetU", "Offset X"), &FMixtormatUvIdFilter::OffsetU, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UvIdOffsetUHint", "How far a region's source read may slip along U from its own centre, as a fraction of the source tile. This is what stops neighbouring regions showing the same patch of texture.")),
		Slider(LOCTEXT("UvIdOffsetV", "Offset Y"), &FMixtormatUvIdFilter::OffsetV, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UvIdOffsetVHint", "The same along V. Per-axis, unlike Pattern IDs' single Offset: a plank wants slip along its length and none across it."))));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdFlipU", "Random Flip U"),
		&FMixtormatUvIdFilter::bRandomFlipU,
		LOCTEXT("UvIdFlipUHint", "Mirrors roughly half the regions across U, drawn per region. A mirror maps the unit square onto itself exactly, so it never seams.")));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdFlipV", "Random Flip V"),
		&FMixtormatUvIdFilter::bRandomFlipV,
		LOCTEXT("UvIdFlipVHint", "The same across V.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("UvIdGrpRandom", "Random")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatUvIdFilter>(
		LOCTEXT("UvIdSeed", "Seed"), Uv, &FMixtormatUvIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("UvIdSeedHint", "Reshuffles which region gets which rotation, scale, offset and flip without changing any of the ranges. Independent of the producer's seed, so reseeding here does not re-generate the regions.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedUvId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("UvIdHeading", "UV FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatUvIdFilter* Selected = GetSelectedUvId();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatUvIdFilter* Selected = GetSelectedUvId())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("UvIdEnabledHint", "Enable this UV filter")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildReliefIdControls()
{
	const auto Relief = [this]() { return GetSelectedReliefId(); };

	const auto Slider = [this, Relief](
		const FText& Label,
		float FMixtormatReliefIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatReliefIdFilter>(Label, Relief, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpHeight", "Height")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdHeight", "Amount"), &FMixtormatReliefIdFilter::HeightAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdHeightHint", "The elevation every region gets above the surface under it. Each region is a flat face at its own height -- tilting one is Ramp From IDs, which composites over this.")),
		Slider(LOCTEXT("ReliefIdHeightRandom", "Variation"), &FMixtormatReliefIdFilter::HeightRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdHeightRandomHint", "How far below Amount a region may be drawn, as a multiplier on it. One-sided: at 0 every region sits at full Amount, at 1 they spread down to the base. A region that went below the base would feather back up at its own boundary and read as a recessed panel in a raised frame."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpProfile", "Profile")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdProfile", "Profile"), &FMixtormatReliefIdFilter::Profile, -1.0, 1.0, 0.25, 0.01,
			LOCTEXT("ReliefIdProfileHint", "The chamfer's cross-section. -1 is a cove that hugs the boundary then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose that rounds over onto it. Both ends stay pinned, so this changes the shape and never the width or height.")),
		Slider(LOCTEXT("ReliefIdProfileRandom", "Variation"), &FMixtormatReliefIdFilter::ProfileRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdProfileRandomHint", "Offsets the profile per region rather than scaling it, so one region's bullnose can be its neighbour's cove."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdFeather", "Feather"), &FMixtormatReliefIdFilter::Feather, 0.0, 0.5, 0.1, 0.005,
			LOCTEXT("ReliefIdFeatherHint", "Eases each region's elevation out at its own boundary, so neighbours at different heights meet through a ramp rather than a one-texel cliff. In region fractions, measured against the region's own reach, so it means the same on a large region and a small one.")),
		Slider(LOCTEXT("ReliefIdFeatherRandom", "Variation"), &FMixtormatReliefIdFilter::FeatherRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdFeatherRandomHint", "Scales the run-out per region. One-sided: the draw only narrows the feather from the authored value, never widens it past what was asked for."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdFeatherGain", "Feather Gain"), &FMixtormatReliefIdFilter::FeatherGain, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("ReliefIdFeatherGainHint", "What the run-out does on the way up, rather than how wide it is. A straight ramp has no change in slope and lights as one flat facet; Gain bends it, and past 1 leaves a raised lip just inside the edge -- the rolled-over rim of a settled tile.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpBevel", "Bevel")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelHeight", "Height"), &FMixtormatReliefIdFilter::BevelHeight, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdBevelHeightHint", "Signed, and it lifts the region face: positive stands the region proud with the chamfer ramping down to the boundary, negative sinks the face instead. The region-less band is untouched either way -- that is Gap Height below.")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("ReliefIdRelativeWidth", "Relative Width"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatReliefIdFilter* R = GetSelectedReliefId();
				return R && R->bRelativeWidth ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatReliefIdFilter* R = GetSelectedReliefId())
				{
					R->bRelativeWidth = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("ReliefIdRelativeWidthHint", "Measures the chamfer as a fraction of the way from a region's boundary to its deepest interior point, rather than in output pixels. Relative frames every region the same way whatever its size; absolute keeps an even visual width across regions of different sizes.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelWidthPixels", "Width"), &FMixtormatReliefIdFilter::BevelWidthPixels, 0.25, 64.0, 4.0, 0.25,
			LOCTEXT("ReliefIdBevelWidthPixelsHint", "The chamfer's width in output pixels. Used when Relative Width is off.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelWidthCells", "Width (Relative)"), &FMixtormatReliefIdFilter::BevelWidthCells, 0.0, 1.0, 0.25, 0.01,
			LOCTEXT("ReliefIdBevelWidthCellsHint", "The same width as a region fraction. A separate control because a pixel width and a fraction need different ranges to be draggable at all.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdBevelVariation", "Variation"), &FMixtormatReliefIdFilter::BevelVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdBevelVariationHint", "Narrows the chamfer per region. One-sided, like Height: the draw never widens one past the authored value.")),
		Slider(LOCTEXT("ReliefIdBevelInset", "Inset"), &FMixtormatReliefIdFilter::BevelInsetPixels, -32.0, 32.0, 0.0, 0.25,
			LOCTEXT("ReliefIdBevelInsetHint", "Slides the chamfer band across the boundary, in output pixels. Negative walks it outside the region, positive pulls it onto the face, zero starts it exactly at the boundary."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdGapHeight", "Gap Height"), &FMixtormatReliefIdFilter::GapHeight, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdGapHeightHint", "Where a region-less band sits relative to the regions -- Pattern IDs' grout, or any pixel the producer marked invalid. Negative sinks it into a trench, positive stands it proud as a raised mortar line. Gap width is topology and stays on the producer; only its height lives here, so the two compose instead of fighting.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpEdge", "Edge")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdEdgeRoughness", "Roughness"), &FMixtormatReliefIdFilter::EdgeRoughness, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("ReliefIdEdgeRoughnessHint", "The roughness written along region boundaries -- the scuffed, unpolished band a worn edge has.")),
		Slider(LOCTEXT("ReliefIdEdgeRoughnessAmount", "Amount"), &FMixtormatReliefIdFilter::EdgeRoughnessAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdEdgeRoughnessAmountHint", "How strongly that band replaces the roughness already there. At 0 the edge shading pass is skipped entirely."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpAO", "AO")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdAO", "Amount"), &FMixtormatReliefIdFilter::AOAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdAOHint", "Contact occlusion along region boundaries. Multiplies the existing AO rather than replacing it.")),
		Slider(LOCTEXT("ReliefIdAOSpread", "Spread"), &FMixtormatReliefIdFilter::AOSpread, 1.0, 8.0, 1.0, 0.1,
			LOCTEXT("ReliefIdAOSpreadHint", "How far that occlusion reaches in from the boundary, as a multiple of the chamfer width."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("ReliefIdGrpRandom", "Random")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatReliefIdFilter>(
		LOCTEXT("ReliefIdSeed", "Seed"), Relief, &FMixtormatReliefIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("ReliefIdSeedHint", "Reshuffles which region gets which height, chamfer width and profile without changing any of the ranges. Independent of the producer's seed, so reseeding here does not re-generate the regions.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedReliefId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ReliefIdHeading", "RELIEF FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatReliefIdFilter* Selected = GetSelectedReliefId();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatReliefIdFilter* Selected = GetSelectedReliefId())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("ReliefIdEnabledHint", "Enable this relief filter")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildIdGroupFeatureMenu()
{
	MixtormatMenu::FBuilder Menu;
	const auto Entry = [this, &Menu](const EMixtormatIdGroupMode Mode, const FText Label)
	{
		Menu.Item(Label, nullptr, FSimpleDelegate::CreateLambda([this, Mode]()
		{
			if (FMixtormatIdGroup* Group = GetSelectedIdGroup())
			{
				Group->Mode = Mode;
				RefreshLayeredPreview();
				RebuildLayerList();
			}
		}))
		.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
		{
			const FMixtormatIdGroup* Group = GetSelectedIdGroup();
			return Group && Group->Mode == Mode;
		}));
	};

	Entry(EMixtormatIdGroupMode::Difference, LOCTEXT("IdGroupModeDifference", "Difference"));
	Entry(EMixtormatIdGroupMode::MaxId, LOCTEXT("IdGroupModeMaxId", "Max ID"));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildIdGroupControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("IdGroupModeLabel", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatIdGroup* Group = GetSelectedIdGroup();
				if (!Group)
				{
					return FText::GetEmpty();
				}
				return Group->Mode == EMixtormatIdGroupMode::MaxId
					? LOCTEXT("IdGroupModeMaxId", "Max ID")
					: LOCTEXT("IdGroupModeDifference", "Difference");
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildIdGroupFeatureMenu)),
		LOCTEXT("IdGroupModeHint",
			"Difference creates a new ID where both children overlap with different IDs. "
			"Max ID keeps the larger valid ID at each pixel.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedIdGroup() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("IdGroupHeading", "ID GROUP"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::IdGroup))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatIdGroup* Selected = GetSelectedIdGroup();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatIdGroup* Selected = GetSelectedIdGroup())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("IdGroupEnabledHint", "Enable this ID group"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildCombineIdModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatIdCombineMode> Modes[] = {
		{ LOCTEXT("CombineModeMerge", "Merge"), EMixtormatIdCombineMode::Merge },
		{ LOCTEXT("CombineModeSubtract", "Subtract"), EMixtormatIdCombineMode::Subtract },
	};
	for (const TPair<FText, EMixtormatIdCombineMode>& Entry : Modes)
	{
		Menu.Item(
			Entry.Key,
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode = Entry.Value]()
			{
				if (FMixtormatCombineIdFilter* C = GetSelectedCombineId())
				{
					C->Mode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode = Entry.Value]()
			{
				const FMixtormatCombineIdFilter* C = GetSelectedCombineId();
				return C && C->Mode == Mode;
			}));
	}
	return Menu.Build();
}

// The same two choices, bound to a named row rather than to the selection. The row menu is
// built on right-click for whatever row was clicked, which is not necessarily the selected one.
TSharedRef<SWidget> SMixtormat::BuildCombineIdModeMenuFor(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatIdCombineMode> Modes[] = {
		{ LOCTEXT("CombineModeMerge", "Merge"), EMixtormatIdCombineMode::Merge },
		{ LOCTEXT("CombineModeSubtract", "Subtract"), EMixtormatIdCombineMode::Subtract },
	};
	for (const TPair<FText, EMixtormatIdCombineMode>& Entry : Modes)
	{
		Menu.Item(
			Entry.Key,
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Mode = Entry.Value]()
			{
				if (FMixtormatLayerChild* Child = ResolveChild(LayerIndex, ChildIndex))
				{
					if (Child->Type == EMixtormatLayerChildType::CombineId)
					{
						Child->CombineId.Mode = Mode;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Mode = Entry.Value]()
			{
				const FMixtormatLayerChild* Child = ResolveChild(LayerIndex, ChildIndex);
				return Child
					&& Child->Type == EMixtormatLayerChildType::CombineId
					&& Child->CombineId.Mode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildCombineIdControls()
{
	const auto Combine = [this]() { return GetSelectedCombineId(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("CombineModeLabel", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatCombineIdFilter* C = GetSelectedCombineId();
				if (!C)
				{
					return FText::GetEmpty();
				}
				return C->Mode == EMixtormatIdCombineMode::Subtract
					? LOCTEXT("CombineModeSubtract", "Subtract")
					: LOCTEXT("CombineModeMerge", "Merge");
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildCombineIdModeMenu)),
		LOCTEXT("CombineModeHint", "Merge draws once per neighbouring pair and joins them, so the whole map comes out coarser with its shapes still in family. Subtract draws whole regions and dissolves the chosen ones into whatever they border, so the survivors keep their exact outline and the map reads as pieces removed from it.")));

	AddSliderRow(Panel, MakeMemberSlider<FMixtormatCombineIdFilter>(
		LOCTEXT("CombineAmount", "Amount"), Combine, &FMixtormatCombineIdFilter::Amount, 0.0, 1.0, 0.35, 0.01,
		LOCTEXT("CombineAmountHint", "Chance that any one candidate takes. 0 passes the ID map through untouched; 1 collapses every region that touches another into a single one.")));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatCombineIdFilter>(
		LOCTEXT("CombinePasses", "Passes"), Combine, &FMixtormatCombineIdFilter::Passes, 1.0, 8.0, 1,
		LOCTEXT("CombinePassesHint", "How many rounds of merging run -- the unsubdivide depth. Each round draws with its own salt and tests the regions the previous round produced, so raising it keeps coarsening rather than re-deciding the same pairs. Two rounds at a low Amount grows clusters of clusters; one round at a high Amount grows one big cluster.")));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatCombineIdFilter>(
		LOCTEXT("CombineSeed", "Seed"), Combine, &FMixtormatCombineIdFilter::Seed, 0.0, 64.0, 0,
		LOCTEXT("CombineSeedHint", "Reshuffles which regions join without changing how many do. Independent of the seed on whatever produced the IDs, so reseeding here does not re-segment.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCombineId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CombineIdHeading", "COMBINE IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::CombineId))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatCombineIdFilter* Selected = GetSelectedCombineId();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatCombineIdFilter* Selected = GetSelectedCombineId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("CombineEnabledHint", "Enable this ID combiner"))
				])
			[
				Panel
			]
		];
}

// Strata Carver, the first GENERATORS panel.
//
// Deliberately smaller than the Houdini prototype. The prototype exposes every term of the
// solver because it was being designed; an artist using it is choosing a rock, not tuning a
// distance metric. Eleven rows carry the whole look -- how deep, how big, how banded, where --
// and the twenty solver terms sit behind ADVANCED where they can be reached and are not in the
// way. Scale is the clearest case: it is the artist's word for the Worley cell count, and the
// gather derives the cells from it rather than asking anyone to think in lattices.
TSharedRef<SWidget> SMixtormat::BuildStrataCarverControls()
{
	const auto Carver = [this]() { return GetSelectedStrataCarver(); };

	const auto Slider = [this, Carver](
		const FText& Label,
		float FMixtormatStrataCarver::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatStrataCarver>(Label, Carver, Member, Min, Max, Default, Snap, Hint);
	};

	const auto SliderInt = [this, Carver](
		const FText& Label,
		int32 FMixtormatStrataCarver::* Member,
		const double Min,
		const double Max,
		const int32 Default,
		const FText& Hint)
	{
		return MakeMemberSliderInt<FMixtormatStrataCarver>(Label, Carver, Member, Min, Max, Default, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, SliderInt(
		LOCTEXT("StrataSeed", "Seed"), &FMixtormatStrataCarver::Seed, 0.0, 64.0, 3,
		LOCTEXT("StrataSeedHint", "Reshuffles the whole carve: where it starts, which Worley family decides that, and every per-region draw. Operation Seed under ADVANCED is the cheaper dial -- it re-solves the same seed field instead of laying down a new one.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataDepth", "Depth"), &FMixtormatStrataCarver::Depth, 0.0, 1.0, 0.05, 0.001,
		LOCTEXT("StrataDepthHint", "How far the carve cuts, in the layer's own height units -- the same units the source map is in, not a fraction of its range. The incoming height is never normalised, so one value gives the same absolute groove on a flat layer, a wood plank and a cliff. 0 skips the entire solve rather than running it to produce nothing.")));

	AddSliderRow(Panel, SliderInt(
		LOCTEXT("StrataIterations", "Iterations"), &FMixtormatStrataCarver::Iterations, 1.0, 64.0, 64,
		LOCTEXT("StrataIterationsHint", "How long the solve runs. The jump schedule halves its stride from Jump Start down to one texel and then restarts, so extra iterations buy depth of recursion rather than reach -- the wide passes carry a front across the tile, the narrow ones are where the strata push accumulates and the bands form. One iteration is a single wide jump and reads as blobs; the picture has stopped changing by 64.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataSeedThreshold", "Seed Threshold"), &FMixtormatStrataCarver::SeedThreshold, 0.0, 1.0, 0.25, 0.01,
		LOCTEXT("StrataSeedThresholdHint", "Where the seed field is cut into carved and not carved. Low floods the surface from everywhere at once; high leaves a few isolated origins with long runs between them, which is the setting that reads as weathering rather than as texture.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		SliderInt(LOCTEXT("StrataScale", "Scale"), &FMixtormatStrataCarver::Scale, 1.0, 64.0, 3,
			LOCTEXT("StrataScaleHint", "Feature size, as cells across the tile. This is the Worley cell count wearing a name an artist can act on: 3 is three broad formations, 20 is gravel.")),
		SliderInt(LOCTEXT("StrataSeedDetail", "Seed Detail"), &FMixtormatStrataCarver::SeedDetail, 1.0, 8.0, 3,
			LOCTEXT("StrataSeedDetailHint", "Octaves in the internal seed. Each doubles the cell count and halves its weight, so the first octave keeps the shape and the rest only roughen its edges. High values turn the seed into dirt rather than into strata."))));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StrataGrpBands", "Strata")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataFrequency", "Frequency"), &FMixtormatStrataCarver::StrataFrequency, 0.0, 64.0, 4.0, 0.5,
		LOCTEXT("StrataFrequencyHint", "How many bedding planes cross the tile. Rounded to a whole number of bands before use -- a fractional count leaves a partial band at the wrap and the seam shows as a sheared stripe. Orientation comes from the layer's own UV rotation rather than from a second control here.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataAmount", "Amount"), &FMixtormatStrataCarver::StrataAmount, 0.0, 16.0, 3.0, 0.05,
		LOCTEXT("StrataAmountHint", "How hard the bands bite. It is a propagation cost, not a texture: crossing a bedding plane is expensive and running along one is not, so raising it makes the carve follow the layering instead of spreading evenly. At 0 the solve is an ordinary distance field.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataWarp", "Warp"), &FMixtormatStrataCarver::StrataWarp, 0.0, 4.0, 0.54, 0.01,
		LOCTEXT("StrataWarpHint", "How far the bands wander off straight. The warp is a periodic curl field, so bending them cannot break the tile however far this is pushed.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataPush", "Push"), &FMixtormatStrataCarver::PushAmount, 0.0, 8.0, 0.5, 0.01,
		LOCTEXT("StrataPushHint", "The recursion. A front that has been running along a bedding plane builds credit and gets cheaper, so it keeps running along that plane -- which is what turns a distance field into layered rock. Push Decay under ADVANCED sets how fast that credit dies behind it.")));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("StrataGrpInfluence", "Influence")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataMaskInfluence", "Mask Influence"), &FMixtormatStrataCarver::MaskInfluence, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("StrataMaskInfluenceHint", "How much a mask scoped under this generator controls it. 0 ignores the mask entirely; 1 lets it decide where carving starts, how cheaply it spreads and how deep it cuts. It is not a final multiply -- a mask applied only at the end gives a hard cutout with full-strength carving inside it, whereas steering the seed and the cost as well is what makes the weathering fade at its own edge. Add the mask by right-clicking this row.")));

	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataIdInfluence", "ID Influence"), &FMixtormatStrataCarver::IDInfluence, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("StrataIdInfluenceHint", "How much Region IDs above this generator vary it -- propagation cost, push, strata phase and depth, one draw each. Every pixel of a region gets the same numbers, so this is per-brick or per-plate variation and never noise. At 0 the ID map has exactly no effect: the shader branches past it rather than blending against it.")));

	// ADVANCED. Collapsed, because none of it is reached while choosing a rock, and every one of
	// these has a default that was arrived at rather than guessed.
	TSharedRef<SVerticalBox> Advanced = SNew(SVerticalBox);

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataStepScale", "Step Scale"), &FMixtormatStrataCarver::StepScale, 0.001, 4.0, 0.3, 0.005,
			LOCTEXT("StrataStepScaleHint", "Distance added per propagation step before cost. The solver's speed dial: lower spreads further in the same iteration count and softens the field, higher keeps the carve local.")),
		SliderInt(LOCTEXT("StrataJumpStart", "Jump Start"), &FMixtormatStrataCarver::JumpStart, 1.0, 256.0, 24,
			LOCTEXT("StrataJumpStartHint", "The widest stride the schedule starts from, in solve texels. It sets how far a front reaches in one pass, not how many passes run -- the schedule halves from here to one and restarts, so this also sets how often the cycle comes back round."))));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataMaxValue", "Max Value"), &FMixtormatStrataCarver::MaxValue, 1.0, 1024.0, 256.0, 1.0,
			LOCTEXT("StrataMaxValueHint", "The unreachable distance. Anything still holding it when the solve ends never had a front arrive and reads as uncarved, and it is also what the raw distance is divided by once, at the very end, to become a 0..1 carve.")),
		Slider(LOCTEXT("StrataWorleyJitter", "Worley Jitter"), &FMixtormatStrataCarver::WorleyJitter, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("StrataWorleyJitterHint", "Feature-point jitter in the seed lattice. 0 puts the points on the grid and the formations come out regular; 1 is full Voronoi."))));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataBandFrequency", "Band Frequency"), &FMixtormatStrataCarver::BandFrequency, 0.0, 16.0, 1.0, 0.05,
			LOCTEXT("StrataBandFrequencyHint", "Rings inside each seed cell, for the banded Worley family. Distinct from Strata Frequency: those are bedding planes across the whole tile, these are one nodule's own growth layers.")),
		Slider(LOCTEXT("StrataCost", "Cost"), &FMixtormatStrataCarver::CostAmount, 0.0, 32.0, 5.0, 0.1,
			LOCTEXT("StrataCostHint", "How hard the seed field resists propagation. High makes fronts hug the cheap channels and the carve comes out as veins; low lets it flood and the carve comes out as patches."))));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataPushDecay", "Push Decay"), &FMixtormatStrataCarver::PushDecay, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("StrataPushDecayHint", "How fast accumulated push dies behind the front. At 0 one push would be carried across the whole tile and every band would run to the edge.")),
		SliderInt(LOCTEXT("StrataOperationSeed", "Operation Seed"), &FMixtormatStrataCarver::OperationSeed, 0.0, 64.0, 6,
			LOCTEXT("StrataOperationSeedHint", "Reshuffles which Worley family and which combining operation each iteration picks, leaving the seed field alone. The cheap dial: it re-solves the same origins into a different rock instead of moving them."))));

	AddSliderRow(Advanced, MixtormatRow::MakeCaption(LOCTEXT("StrataGrpRemap", "Output")));
	AddSliderRow(Advanced, MixtormatRow::MakeCaption(LOCTEXT("StrataGrpRemapNote", "Applied after the solve, never during it -- scrubbing these reshapes the finished field instead of re-running it.")));

	AddSliderRow(Advanced, Slider(
		LOCTEXT("StrataBias", "Bias"), &FMixtormatStrataCarver::Bias, 0.001, 1.0, 0.68, 0.01,
		LOCTEXT("StrataBiasHint", "A gamma-style pull about the midpoint. 0.5 is the identity, below deepens the carve toward its origins, above spreads it out toward the edges of its reach.")));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataRemapInMin", "In Min"), &FMixtormatStrataCarver::RemapInMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("StrataRemapInMinHint", "Low end of the input window. Raising it discards the shallowest carve and leaves only the deepest runs.")),
		Slider(LOCTEXT("StrataRemapInMax", "In Max"), &FMixtormatStrataCarver::RemapInMax, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("StrataRemapInMaxHint", "High end of the input window. Lowering it flattens the deepest carve into one level."))));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataRemapOutMin", "Out Min"), &FMixtormatStrataCarver::RemapOutMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("StrataRemapOutMinHint", "What the window's low end becomes. Above 0 carves everywhere, including where no front ever arrived.")),
		Slider(LOCTEXT("StrataRemapOutMax", "Out Max"), &FMixtormatStrataCarver::RemapOutMax, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("StrataRemapOutMaxHint", "What the window's high end becomes. Below 1 scales the whole carve without touching Depth, which is the one to reach for when the shape is right and only the strength is not."))));

	AddSliderRow(Advanced, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataClampMin", "Clamp Min"), &FMixtormatStrataCarver::ClampMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("StrataClampMinHint", "Floor on the finished carve, after the remap.")),
		Slider(LOCTEXT("StrataClampMax", "Clamp Max"), &FMixtormatStrataCarver::ClampMax, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("StrataClampMaxHint", "Ceiling on the finished carve, after the remap."))));

	Panel->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, 0.0f)
	[
		SNew(SMixtormatInspectorGroup)
		.Title(LOCTEXT("StrataAdvancedHeading", "ADVANCED"))
		.InitiallyExpanded(false)
		[
			Advanced
		]
	];

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedStrataCarver() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("StrataCarverHeading", "STRATA CARVER"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
						return Child && Child->Type == EMixtormatLayerChildType::Generator
							&& Child->Generator.bEnabled
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						// The wrapper's flag, not the payload's. One switch turns off the node
						// whatever generator it is carrying, which is the same thing the bypass
						// preview and the gather branch both test.
						if (FMixtormatGenerator* Generator = GetSelectedGenerator())
						{
							Generator->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("StrataEnabledHint", "Enable this generator")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildFractureControls()
{
	const auto Fracture = [this]() { return GetSelectedFracture(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MakeMemberEnum<FMixtormatFracture>(
		LOCTEXT("FractureSource", "Source"), Fracture,
		&FMixtormatFracture::FractureSource,
		LOCTEXT("FractureSourceHint", "Generated creates pieces. Region IDs reshapes Pattern/Cluster/Combine footprints. Combined subdivides those pieces.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatFracture>(
			LOCTEXT("FractureScale", "Scale"), Fracture,
			&FMixtormatFracture::FractureScale, 2.0, 32.0, 7.0, 1.0,
			LOCTEXT("FractureScaleHint", "Broad generated fracture regions across one repeat.")),
		MakeMemberSliderInt<FMixtormatFracture>(
			LOCTEXT("FractureSeed", "Seed"), Fracture,
			&FMixtormatFracture::FractureSeed, 0.0, 9999.0, 11,
			LOCTEXT("FractureSeedHint", "Changes piece layout, directional breaks and face variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatFracture>(
			LOCTEXT("FractureAmount", "Amount"), Fracture,
			&FMixtormatFracture::FractureAmount, 0.0, 1.0, 0.62, 0.01,
			LOCTEXT("FractureAmountHint", "Blends the boundary deformation and face shaping. Zero preserves the input height.")),
		MakeMemberSlider<FMixtormatFracture>(
			LOCTEXT("FractureWidth", "Width"), Fracture,
			&FMixtormatFracture::FractureWidth, 0.0, 1.0, 0.28, 0.01,
			LOCTEXT("FractureWidthHint", "Width of the fractured shoulder; Variation breaks up its contour and slope width."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatFracture>(
			LOCTEXT("FractureDepth", "Depth"), Fracture,
			&FMixtormatFracture::FractureDepth, 0.0, 0.5, 0.08, 0.001,
			LOCTEXT("FractureDepthHint", "Depth of fracture faces measured from the piece shoulder, in layer-height units.")),
		MakeMemberSlider<FMixtormatFracture>(
			LOCTEXT("FractureProfile", "Slope Profile"), Fracture,
			&FMixtormatFracture::FractureProfile, 0.25, 4.0, 1.0, 0.01,
			LOCTEXT("FractureProfileHint", "Shapes three planar slope sections. One is linear; lower or higher changes the slope breaks."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatFracture>(
		LOCTEXT("FractureChamfer", "Chamfer"), Fracture,
		&FMixtormatFracture::FractureChamfer, 0.0, 3.0, 0.25, 0.01,
		LOCTEXT("FractureChamferHint", "SDF intersection chamfer in normalized field units. Zero uses a hard intersection. Independent of Slope Profile; no re-normalization.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatFracture>(
		LOCTEXT("FractureVariation", "Variation"), Fracture,
		&FMixtormatFracture::FractureVariation, 0.0, 1.0, 0.38, 0.01,
		LOCTEXT("FractureVariationHint", "Directional zigzags, signed boundary offsets, shoulder width and broken slope variation; no micro noise.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedFracture() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("FractureHeading", "FRACTURE"))
			.InitiallyExpanded(true)
			.HeaderAction(MixtormatRow::MakeCheckbox(
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
				LOCTEXT("FractureEnabledHint", "Enable this fracture module")))
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

	// Always shown rather than behind an output mode. Relief height also drives its normal.
	TSharedRef<SVerticalBox> ReliefGroup = SNew(SVerticalBox);
	AddSliderRow(ReliefGroup, MixtormatRow::MakeCaption(LOCTEXT("CraqGrpRelief", "Relief")));
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

	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatGeneratedMask* Generated = GetSelectedGeneratedMask();
		return Generated ? &Generated->Shaping : nullptr;
	});
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
				Panel
			]
		];
}

// Two radii and nothing else. A direction enum would have been one control instead of two, but
// it could not be driven per axis and could not express an unequal blur -- and the shader already
// runs a dispatch per axis, so the second number is free.
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

// Source and Mode lead, because they decide what every number under them means: the same Range
// that keeps cavities under Mean keeps saddles under Gaussian.
TSharedRef<SWidget> SMixtormat::BuildMaskCurvatureControls()
{
	using namespace MixtormatMaskCurvatureRange;
	const auto Curve = [this]() { return GetSelectedLayerCurvature(); };

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::Make(
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
			})),
		LOCTEXT("CurvatureSourceHint", "Which field is measured. Surface Height is what this layer is being laid onto, so the mask follows shape already in the surface. Mask Itself reads the mask at this point in the chain -- after a Blur, if one precedes this, which is usually what gives a painted edge enough shape to measure at all.")));

	AddSliderRow(Panel, MixtormatRow::Make(
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
			})),
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
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontBody))
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
			MixtormatRow::Make(
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
					})),
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
			MixtormatRow::Make(
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
						this, &SMixtormat::BuildMaskLayerValueChannelMenu)),
				LOCTEXT("SelectedMaskLayerValueChannelHint",
					"Which scalar to take. Luminance is Rec. 709 over the layer's linear albedo "
					"and is what reads as brightness; the single channels are the raw albedo "
					"components. Roughness is the layer's resolved roughness, after its bias, "
					"contrast and offset.\n\n"
					"Inversion, Balance, Contrast and Offset under Shaping do the levelling -- "
					"there is no second set of range controls here."))
		]);

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
	//
	// Texture masks only. A Layer Values mask reads the layer it is on at the composition's own
	// resolution -- there is no map to tile, offset, flip or turn -- so the whole block is
	// collapsed rather than shown inert. This is the other half of removing the Source dropdown:
	// the inspector is now specific to what the mask actually reads.
	const TSharedRef<SVerticalBox> Placement = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatMaskLayer* M = GetSelectedLayerMask();
			return M && M->UsesLayerValues()
				? EVisibility::Collapsed
				: EVisibility::Visible;
		});
	AddSliderRow(Placement, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpPlacement", "Placement")));
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
	AddSliderRow(Placement, MixtormatRow::Make(
		LOCTEXT("MaskRotationLabel", "Rotate"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatMaskLayer* M = GetSelectedLayerMask();
				return M ? MixtormatUI::UVRotationText(M->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskRotationMenu)),
		LOCTEXT("MaskRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams; 90 degree steps are permutations of the unit square, so they stay tileable. Applied before the tiling, so the mask turns and the lattice repeats the turned result.")));

	AddSliderRow(Panel, Placement);

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
		LOCTEXT("RoughnessContrastLabel", "Contrast"), Layer(), &FMixtormatLayer::RoughnessContrast, -1.0, 2.0, 0.0, 0.01));
	AddSliderRow(Roughness, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("RoughnessOffsetLabel", "Offset"), Layer(), &FMixtormatLayer::RoughnessOffset, -0.5, 0.5, 0.0, 0.01));

	// Relief owns the layer's depth controls and, next to them, the authored normal map's own
	// strength. The two sit together because the question "why is my normal map flat" is answered
	// by one of them and not the other: Height Booster shapes the height and the normals
	// reconstructed from it, Normal Strength shapes the imported map, and neither reaches the
	// other's territory.
	TSharedRef<SVerticalBox> Relief = AddCard(Panel, LOCTEXT("CardRelief", "Relief"));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("HeightBoostLabel", "Height Booster"), Layer(), &FMixtormatLayer::HeightBoost, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("HeightBoostHint", "Gain on this layer's height. 1 is untouched; 0 flattens it; values above 1 deepen the relief and, with it, the normals derived from that height. An imported normal map is left at its authored strength, so a surface whose height and normal describe the same relief is not deepened twice. Applied before displacement, height blending, and derived normals. Not Height Influence, which controls how much of this layer reaches the composite.")));
	AddSliderRow(Relief, MakeMemberSlider<FMixtormatLayer>(
		LOCTEXT("NormalStrengthLabel", "Normal Strength"), Layer(), &FMixtormatLayer::NormalIntensity, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("NormalStrengthHint", "Strength of this layer's imported normal map, as a slope gain. 1 is the map exactly as authored, 0 flattens it, and above 1 steepens its tilt. The only control that can strengthen an authored normal -- Normal Influence runs 0..1 and can only fade one out. Not Height Booster, which deepens this layer's height and the normals reconstructed from that height; a surface whose height and normal describe the same relief would otherwise carry it twice.")));
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

// One card, one title. Feature, Curvature and Surface Mask are all the same question -- what
// generated signal masks this layer -- and as three sheets they read as three unrelated runs
// with the widest gap in the panel between values that are set together. Captions between the
// runs only rebuilt that split a line at a time, so the card title carries the subject alone.
//
// No group header of its own. This is one run of values inside Surface Adjustments, and a second
// header bar over it only repeated the one already above -- with a chevron that hid controls the
// panel exists to offer. The eye that previews the feature mask sits at the end of the title
// line, where a card's actions go.
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
		MakeFeaturePreviewButton(
			EMixtormatDebugPreviewMode::GeneratedFeature,
			LOCTEXT("PreviewGeneratedFeature", "Preview the cavity-to-convex feature mask in unlit dark red and cyan")));

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
			+ SVerticalBox::Slot().AutoHeight()
			[
				NumericRow(LOCTEXT("HeightMaskStrength", "Blend Strength"), &FMixtormatLayer::HeightBlendAmount, 0.0f, 4.0f, 0.01f, 1.0f)
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
							|| GetSelectedLayerBlur()
							|| GetSelectedLayerCurvature()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
							// Both halves of the Pattern split. Missing from these lists, a new
							// node shows the layer's own sections instead of its own -- which is
							// exactly how Combine IDs' bug read.
							|| GetSelectedUvId()
							|| GetSelectedReliefId()
							|| GetSelectedIdGroup()
							|| GetSelectedCombineId()
							// The category, not the kind. A generator whose panel is not yet
							// written still has to claim the inspector, or it would show the
							// layer's own sections instead and read as a broken selection.
							|| HasSelectedGenerator()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					+ SScrollBox::Slot()[BuildProceduralPeelControls()]
					+ SScrollBox::Slot()[BuildStainControls()]
					+ SScrollBox::Slot()[BuildRunoffControls()]
					+ SScrollBox::Slot()[BuildErosionControls()]
					+ SScrollBox::Slot()[BuildGradeControls()]
					+ SScrollBox::Slot()[BuildFlowWarpControls()]
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
					+ SScrollBox::Slot()[BuildIdGroupControls()]
					+ SScrollBox::Slot()[BuildCombineIdControls()]
					+ SScrollBox::Slot()[BuildStrataCarverControls()]
					+ SScrollBox::Slot()[BuildFractureControls()]
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
							|| GetSelectedLayerBlur()
							|| GetSelectedLayerCurvature()
							|| GetSelectedCraquelure()
							|| GetSelectedColorId()
							|| GetSelectedFilter()
							|| GetSelectedPatternId()
							|| GetSelectedHsvFilter()
							|| GetSelectedRandomId()
							|| GetSelectedRampId()
							// Both halves of the Pattern split. Missing from these lists, a new
							// node shows the layer's own sections instead of its own -- which is
							// exactly how Combine IDs' bug read.
							|| GetSelectedUvId()
							|| GetSelectedReliefId()
							|| GetSelectedIdGroup()
							|| GetSelectedCombineId()
							// The category, not the kind. A generator whose panel is not yet
							// written still has to claim the inspector, or it would show the
							// layer's own sections instead and read as a broken selection.
							|| HasSelectedGenerator()
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
								.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SegmentedControlGap)
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
											LOCTEXT("BaseColorBlendLabel", "Blend"),
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
	AddSliderRow(TargetPanel, MixtormatRow::MakeCaption(LOCTEXT("MaskGrpShape", "Shaping")));
	AddSliderRow(TargetPanel, MakeMemberToggle<FMixtormatMaskShaping>(
		LOCTEXT("MaskInvertLabel", "Invert"), Resolve, &FMixtormatMaskShaping::bInvert));
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
}

#undef LOCTEXT_NAMESPACE
