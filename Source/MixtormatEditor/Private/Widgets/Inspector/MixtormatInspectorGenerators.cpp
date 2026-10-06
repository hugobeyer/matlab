// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildGeneratorFlowControls(const EMixtormatEffectType Type)
{
	const auto Flow = [this, Type]() -> FMixtormatLayerEffect*
	{
		FMixtormatLayerEffect* Effect = GetSelectedGeneratorFlow();
		return Effect && Effect->ProceduralType == Type ? Effect : nullptr;
	};
	const auto HasOwner = [this]()
	{
		const FMixtormatChildAddress Address = GetSelectedChildAddress();
		const FMixtormatLayerChild* Child = ResolveChildAt(Address);
		const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
		const FMixtormatLayerChild* Owner = Child && Children
			? Children->FindByPredicate([Child](const FMixtormatLayerChild& Candidate)
				{ return Candidate.ChildId == Child->ScopeOwnerChildId; }) : nullptr;
		return Owner && Owner->Type == EMixtormatLayerChildType::Generator
			&& MixtormatCanOwnGeneratorFlow(Owner->Generator.Type);
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox).IsEnabled_Lambda(HasOwner);
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(
		LOCTEXT("GeneratorFlowSource", "Source"), Flow, &FMixtormatLayerEffect::GeneratorFlowSource,
		LOCTEXT("GeneratorFlowSourceHint", "Uses the owning generator's signed distance or height field. Generator layers support every kind; existing generator-child scope eligibility is unchanged.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAmount", "Amount"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowAmount, 0.0, 1.0, 1.0, 0.01),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowTangent", "Normal / Tangent"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowTangent, 0.0, 1.0, 0.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAngle", "Angle"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowAngle, -180.0, 180.0, 0.0, 1.0),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowBend", "Bend"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowBend, -180.0, 180.0, 0.0, 1.0)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowRadius", "Radius (texels)"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowRadius, 1.0, 16.0, 2),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowSmooth", "Smooth (texels)"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowSmooth, 0.0, 64.0, 8.0, 0.5,
			LOCTEXT("GeneratorFlowSmoothHint", "Blurs the flow direction. Removes the stepping of the raw field; collisions between opposing flows stay sharp."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowReach", "Reach (UV)"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowReach, 0.0, 1.0, 0.1, 0.001),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowFeather", "Feather"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowFeather, 0.0, 1.0, 0.5, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAlong", "Offset Along"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowOffsetAlong, -1.0, 1.0, 0.0, 0.01),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAcross", "Offset Across"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowOffsetAcross, -1.0, 1.0, 0.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowSeed", "Seed"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowSeed, 0.0, 1024.0, 1),
		SNullWidget::NullWidget));

	if (Type == EMixtormatEffectType::ShapeDeform)
	{
		const auto SourceAddress = MakeAddressResolver<FMixtormatLayerEffect>(
			Flow, &FMixtormatLayerEffect::GeneratorFlowSource);
		AddSliderRow(Panel, MixtormatRow::MakePair(
			SNew(SBox)
			.IsEnabled_Lambda([this, Flow, SourceAddress]()
			{
				const FMixtormatLayerEffect* Effect = Flow();
				return Effect && GetEffectiveEnumParameter(SourceAddress(), static_cast<int64>(Effect->GeneratorFlowSource))
					== static_cast<int64>(EMixtormatGeneratorFlowSource::SignedDistance);
			})
			[
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowShapeOffset", "Shape Offset (UV)"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowShapeOffset, -0.25, 0.25, 0.0, 0.001,
					LOCTEXT("GeneratorFlowShapeOffsetHint", "Signed boundary expansion or erosion. Requires Signed Distance; unavailable with Height."))
			],
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowBulge", "Bulge / Pinch"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowBulge, -0.25, 0.25, 0.0, 0.001)));
	}
	else
	{
		// Both UV warping and carving trace the shared direction field.
		AddSliderRow(Panel, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowTraceLength", "Trace Length (UV)"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowTraceLength, 0.0, 1.0, 0.1, 0.001),
			MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowSteps", "Steps"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowSteps, 1.0, 64.0, 16)));
		if (Type == EMixtormatEffectType::GeneratorFlow)
		{
			// Half width like every other slider in the panel; the empty half is intentional.
			AddSliderRow(Panel, MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowWarpStrength", "Warp Strength"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowWarpStrength, -4.0, 4.0, 1.0, 0.01),
				SNullWidget::NullWidget));
		}
		else if (Type == EMixtormatEffectType::FlowCarve)
		{
			AddSliderRow(Panel, MakeMemberEnum<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowCarveMode", "Mode"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowCarveMode));
			AddSliderRow(Panel, MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowDepth", "Depth"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowDepth, 0.0, 2.0, 1.0, 0.01),
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowWidth", "Width (UV)"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowWidth, 0.0, 0.25, 0.01, 0.001)));
			AddSliderRow(Panel, MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowFalloff", "Falloff"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowFalloff, 0.1, 8.0, 1.0, 0.01),
				SNullWidget::NullWidget));
		}
	}
	const FText Title = Type == EMixtormatEffectType::ShapeDeform
		? LOCTEXT("ShapeDeformHeading", "SHAPE DEFORM")
		: Type == EMixtormatEffectType::FlowCarve
			? LOCTEXT("FlowCarveHeading", "FLOW CARVE") : LOCTEXT("GeneratorFlowHeading", "GENERATOR FLOW");
	return SNew(SBox)
		.Visibility_Lambda([Flow]() { return Flow() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(Title)
			.InitiallyExpanded(true)
			.HeaderAction(MakeChildOutputPreviewButton(GetPreviewOutputSetForEffectType(Type)))
			[Panel]
		];
}

TSharedRef<SWidget> SMixtormat::BuildChildOutputsControls(const FMixtormatChildCapabilities& Capabilities)
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	const TArray<FMixtormatPublishedOutputDesc> Copyable = GetCopyableOutputs(Capabilities);
	TSet<FName> PairedCopies;
	const auto AddOutputRow = [this, &Panel](const FText& Label,
		const FMixtormatPublishedOutputDesc* Preview, const FMixtormatPublishedOutputDesc* Copy)
	{
		FMixtormatChildPreviewOutputSet PreviewSet;
		if (Preview)
		{
			PreviewSet.Primary = FMixtormatPreviewOutputDesc{
				Preview->Name, Preview->Label, Preview->Kind, Preview->PreviewGapMaskName};
		}
		const bool bCopyable = Copy != nullptr;
		const FName CopyName = Copy ? Copy->Name : NAME_None;
		AddSliderRow(Panel, MixtormatRow::Make(Label,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakeChildOutputPreviewButton(PreviewSet)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.Text(LOCTEXT("InspectorCopyChildOutput", "Copy"))
				.ToolTipText(FText::Format(LOCTEXT("InspectorCopyChildOutputHint", "Copy the live {0} output reference; the producer stays in place."), Label))
				.IsEnabled_Lambda([this, bCopyable, CopyName]()
				{
					return bCopyable && CanCopyChildOutput(GetSelectedChildAddress(), CopyName);
				})
				.OnClicked_Lambda([this, CopyName]()
				{
					CopyChildOutput(GetSelectedChildAddress(), CopyName);
					return FReply::Handled();
				})
			]));
	};
	for (const FMixtormatPublishedOutputDesc& Preview : Capabilities.Outputs)
	{
		if (!Preview.bPreviewable) { continue; }
		const FMixtormatPublishedOutputDesc* Copy = Copyable.FindByPredicate(
			[&Preview](const FMixtormatPublishedOutputDesc& Candidate) { return Candidate.Name == Preview.Name; });
		// Typed previews can use a display address distinct from their published field address.
		if (!Copy)
		{
			Copy = Copyable.FindByPredicate([&Preview](const FMixtormatPublishedOutputDesc& Candidate)
			{
				return Candidate.bCopyableAsField && Candidate.Kind == Preview.Kind;
			});
		}
		if (Copy) { PairedCopies.Add(Copy->Name); }
		AddOutputRow(Preview.Label, &Preview, Copy);
	}
	for (const FMixtormatPublishedOutputDesc& Copy : Copyable)
	{
		if (!PairedCopies.Contains(Copy.Name)) { AddOutputRow(Copy.Label, nullptr, &Copy); }
	}
	return SNew(SMixtormatInspectorCard)
		.Title(LOCTEXT("InspectorChildOutputsHeading", "OUTPUTS"))
		[Panel];
}

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
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("StrataOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("StrataNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Carver]()
				{
					const FMixtormatStrataCarver* G = Carver();
					return G && G->bStrataNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Carver](const ECheckBoxState State)
				{
					if (FMixtormatStrataCarver* G = Carver())
					{
						G->bStrataNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("StrataNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5.")));
		AddSliderRow(Output, Slider(
			LOCTEXT("StrataOutputScale", "Scale"), &FMixtormatStrataCarver::StrataHeightScale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("StrataOutputScaleHint", "Scales the signed generator height after normalization.")));
	}

	AddSliderRow(Panel, SliderInt(
		LOCTEXT("StrataSeed", "Seed"), &FMixtormatStrataCarver::Seed, 0.0, 9999.0, 3,
		LOCTEXT("StrataSeedHint", "Draws every per-bed random and the bend.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataFrequency", "Strata Size"), &FMixtormatStrataCarver::StrataFrequency, 1.0, 64.0, 6.0, 1.0,
			LOCTEXT("StrataFrequencyHint", "Beds across the tile. Also how many distinct beds are drawn before they repeat.")),
		Slider(LOCTEXT("StrataDepth", "Depth"), &FMixtormatStrataCarver::Depth, 0.0, 1.0, 0.25, 0.001,
			LOCTEXT("StrataDepthHint", "Height relief contributed by the strata."))));
	AddSliderRow(Panel, Slider(
		LOCTEXT("StrataRotation", "Direction"), &FMixtormatStrataCarver::StrataRotation, 0.0, 360.0, 0.0, 0.1,
		LOCTEXT("StrataRotationHint", "Direction of the bedding. Snaps to the nearest angle that tiles; 180 turns the faces the other way.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataThickness", "Thickness Variation"), &FMixtormatStrataCarver::ThicknessVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("StrataThicknessHint", "0 is evenly spaced beds; 1 lets thin and thick beds sit side by side.")),
		Slider(LOCTEXT("StrataHeightVariation", "Height Variation"), &FMixtormatStrataCarver::HeightVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("StrataHeightVariationHint", "0 gives every bed the same rise from the same base; 1 varies both."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataVerticality", "Verticality"), &FMixtormatStrataCarver::Verticality, 0.0, 1.0, 0.7, 0.01,
			LOCTEXT("StrataVerticalityHint", "How steep each bed's face is. 0 is a symmetric ridge, 1 a sheer wall.")),
		Slider(LOCTEXT("StrataRampShape", "Ramp Shape"), &FMixtormatStrataCarver::RampShape, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("StrataRampShapeHint", "The dip slope's profile. -1 hollows it, 0 is straight, 1 bulges it."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataBend", "Bend"), &FMixtormatStrataCarver::Bend, 0.0, 0.25, 0.03, 0.001,
			LOCTEXT("StrataBendHint", "How far the beds bend, in tile widths. Beds keep their thickness.")),
		SliderInt(LOCTEXT("StrataBendScale", "Bend Scale"), &FMixtormatStrataCarver::BendScale, 1.0, 8.0, 2,
			LOCTEXT("StrataBendScaleHint", "How many bends cross the tile."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataBreakup", "Breakup"), &FMixtormatStrataCarver::Breakup, 0.0, 0.5, 0.1, 0.01,
			LOCTEXT("StrataBreakupHint", "How ragged each face is, in beds.")),
		Slider(LOCTEXT("StrataHeightFollow", "Height Follow"), &FMixtormatStrataCarver::HeightFollow, 0.0, 16.0, 0.0, 0.01,
			LOCTEXT("StrataHeightFollowHint", "How many beds the layer's own height shifts the bedding by. Above 0 the faces follow the contours underneath."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("StrataLamination", "Lamination"), &FMixtormatStrataCarver::Lamination, 0.0, 1.0, 0.25, 0.01,
			LOCTEXT("StrataLaminationHint", "Fine laminae inside each bed.")),
		Slider(LOCTEXT("StrataCrossBedding", "Cross Bedding"), &FMixtormatStrataCarver::CrossBedding, 0.0, 3.0, 1.0, 0.01,
			LOCTEXT("StrataCrossBeddingHint", "How far each bed tilts its laminae off the bedding plane."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedStrataCarver() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("StrataCarverHeading", "STRATA CARVER"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::StrataCarver;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatGenerator* Generator = GetSelectedGenerator();
							return Generator && Generator->bEnabled
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
						LOCTEXT("StrataEnabledHint", "Enable this generator"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildCracksControls()
{
	const auto Crack = [this]() { return GetSelectedCracks(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("CrackOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("CrackNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Crack]()
				{
					const FMixtormatCracks* G = Crack();
					return G && G->bCrackNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Crack](const ECheckBoxState State)
				{
					if (FMixtormatCracks* G = Crack())
					{
						G->bCrackNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("CrackNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5.")));
		AddSliderRow(Output, MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackHeightScale", "Scale"), Crack, &FMixtormatCracks::CrackHeightScale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("CrackHeightScaleHint", "Scales the signed generator height after normalization.")));
	}

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatCracks>(
			LOCTEXT("CrackCells", "Cells"), Crack, &FMixtormatCracks::CrackCells, 1.0, 32.0, 7,
			LOCTEXT("CrackCellsHint", "Pieces per row; the tile is Cells x Cells. Every length and depth below is relative to one cell.")),
		MakeMemberSliderInt<FMixtormatCracks>(
			LOCTEXT("CrackSeed", "Seed"), Crack, &FMixtormatCracks::CrackSeed, 0.0, 9999.0, 1,
			LOCTEXT("CrackSeedHint", "Changes the network, its roughness and every piece."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackJitter", "Jitter"), Crack, &FMixtormatCracks::CrackJitter, 0.0, 1.0, 0.85, 0.01,
			LOCTEXT("CrackJitterHint", "How irregular the pieces are. 0 is a square grid.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidth", "Width"), Crack, &FMixtormatCracks::CrackWidth, 0.0, 0.5, 0.1, 0.005,
			LOCTEXT("CrackWidthHint", "Crack width, in cell widths."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatCracks>(
		LOCTEXT("CrackDepth", "Depth"), Crack, &FMixtormatCracks::CrackDepth, 0.0, 2.0, 0.415, 0.01,
		LOCTEXT("CrackDepthHint", "Groove depth at the base width, relative to the cell. Wider cracks cut deeper.")));

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("CrackGrpShape", "Shape"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackRough", "Rough"), Crack, &FMixtormatCracks::CrackRough, 0.0, 1.0, 0.361, 0.01,
			LOCTEXT("CrackRoughHint", "How angular the crack lines are. Scale does not change it.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackScale", "Scale"), Crack, &FMixtormatCracks::CrackScale, 0.5, 16.0, 7.8, 0.05,
			LOCTEXT("CrackScaleHint", "Bends per cell width along a crack."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackDetail", "Detail"), Crack, &FMixtormatCracks::CrackDetail, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("CrackDetailHint", "How much finer texture rides on the bends.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackFeather", "Feather"), Crack, &FMixtormatCracks::CrackFeather, 0.0, 1.0, 0.176, 0.01,
			LOCTEXT("CrackFeatherHint", "Small gives fault-like kinks, large gives soft bends."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpWidth", "Width Variation"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackLineVariation", "Per Crack"), Crack, &FMixtormatCracks::CrackLineVariation, 0.0, 1.0, 0.541, 0.01,
			LOCTEXT("CrackLineVariationHint", "Every crack its own width, from hairline to wide.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackRegionVariation", "Regional"), Crack, &FMixtormatCracks::CrackRegionVariation, 0.0, 1.0, 0.615, 0.01,
			LOCTEXT("CrackRegionVariationHint", "Whole regions wider or thinner."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidthVariation", "Along"), Crack, &FMixtormatCracks::CrackWidthVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("CrackWidthVariationHint", "Fine wobble along each crack.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackWidthScale", "Along Scale"), Crack, &FMixtormatCracks::CrackWidthScale, 0.5, 32.0, 6.44, 0.1,
			LOCTEXT("CrackWidthScaleHint", "Wobbles per cell width along a crack."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpRim", "Rims and Gaps"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChip", "Chips"), Crack, &FMixtormatCracks::CrackChip, 0.0, 1.0, 0.3, 0.01,
			LOCTEXT("CrackChipHint", "Share of the rim carrying chips. Each side of a crack chips on its own.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChipSize", "Chip Size"), Crack, &FMixtormatCracks::CrackChipSize, 0.0, 0.5, 0.12, 0.005,
			LOCTEXT("CrackChipSizeHint", "Chip size, in cell widths."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackGap", "Flat Floors"), Crack, &FMixtormatCracks::CrackGap, 0.0, 1.0, 0.15, 0.01,
			LOCTEXT("CrackGapHint", "Chance of a flat floor in the scalar crack height profile; does not create transparency.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackGapWidth", "Floor Width"), Crack, &FMixtormatCracks::CrackGapWidth, 0.0, 4.0, 1.5, 0.05,
			LOCTEXT("CrackGapWidthHint", "Flat floor width relative to the crack width, within the same height profile."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpPieces", "Pieces"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackSlip", "Slip"), Crack, &FMixtormatCracks::CrackSlip, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("CrackSlipHint", "Every piece rises or sinks by its own amount, independently of crack width.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackTilt", "Tilt"), Crack, &FMixtormatCracks::CrackTilt, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("CrackTiltHint", "Every piece tips its own random way."))));

	Panel = AddCard(Cards, LOCTEXT("CrackGrpChamfer", "Chamfer"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChamferAmount", "Amount"), Crack, &FMixtormatCracks::CrackChamferAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("CrackChamferAmountHint", "Shapes the negative groove wall profile without cutting the piece base or changing the flat floor.")),
		MakeMemberSlider<FMixtormatCracks>(
			LOCTEXT("CrackChamferEdge", "Edge Reach"), Crack, &FMixtormatCracks::CrackChamferEdge, 0.0, 0.5, 0.12, 0.001,
			LOCTEXT("CrackChamferEdgeHint", "Wall-profile transition distance in cell widths; independent of crack width, with no built-in propagation noise."))));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedCracks() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("CracksHeading", "CRACKS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::Cracks;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
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
						LOCTEXT("CracksEnabledHint", "Enable these cracks"))
				])
			[
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildFinalSettingsControls()
{
	const auto Final = [this]() -> FMixtormatFinalSettings*
	{
		return bHasWorkingMaterial ? &WorkingFinalSettings : nullptr;
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatFinalSettings>(
			LOCTEXT("FinalAOAmount", "AO"), Final, &FMixtormatFinalSettings::HeightAOAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("FinalAOAmountHint", "Ambient occlusion from the final height, applied once after every layer. Layers and effects never write AO.")),
		MakeMemberSlider<FMixtormatFinalSettings>(
			LOCTEXT("FinalAORadius", "AO Radius"), Final, &FMixtormatFinalSettings::HeightAORadius, 1.0, 64.0, 8.0, 0.5,
			LOCTEXT("FinalAORadiusHint", "How far the occlusion reaches, in pixels at 1024. Scales with resolution."))));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatFinalSettings>(
		LOCTEXT("FinalAutoRemapHeight", "Auto Remap Height"), Final, &FMixtormatFinalSettings::bAutoRemapHeight,
		LOCTEXT("FinalAutoRemapHeightHint", "Remap the finished height to 0-1 from its own lowest and highest point, before AO and normals.")));
	const Mixtormat::FMixtormatPreviewMetrics& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
	return SNew(SMixtormatMenuPanel)
		.MinWidth(Layout.FinalPopupWidth)
		.Padding(FMargin(Layout.OverlayClusterInset))
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildPebblesControls()
{
	const auto Pebble = [this]() { return GetSelectedPebbles(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("PebbleOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("PebbleNormalizeHeight", "Normalize Height"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Pebble]()
				{
					const FMixtormatPebbles* G = Pebble();
					return G && G->bPebbleNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Pebble](const ECheckBoxState State)
				{
					if (FMixtormatPebbles* G = Pebble())
					{
						G->bPebbleNormalizeHeight = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			LOCTEXT("PebbleNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5.")));
		AddSliderRow(Output, MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleHeightScale", "Scale"), Pebble, &FMixtormatPebbles::PebbleHeightScale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("PebbleHeightScaleHint", "Scales the signed generator height after normalization.")));
	}

	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleCells", "Cells"), Pebble, &FMixtormatPebbles::PebbleCells, 1.0, 32.0, 4,
			LOCTEXT("PebbleCellsHint", "Stones per row; the tile is Cells x Cells.")),
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleSeed", "Seed"), Pebble, &FMixtormatPebbles::PebbleSeed, 0.0, 9999.0, 1,
			LOCTEXT("PebbleSeedHint", "Changes placement, shape and height of every stone."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleDensity", "Density"), Pebble, &FMixtormatPebbles::PebbleDensity, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("PebbleDensityHint", "Chance a cell has a stone.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleJitter", "Jitter"), Pebble, &FMixtormatPebbles::PebbleJitter, 0.0, 1.0, 0.7, 0.01,
			LOCTEXT("PebbleJitterHint", "Position randomness inside the cell."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleScale", "Scale"), Pebble, &FMixtormatPebbles::PebbleScale, 0.1, 2.0, 1.1, 0.01,
			LOCTEXT("PebbleScaleHint", "Stone size; 1 fills a cell.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleScaleVariation", "Scale Var"), Pebble, &FMixtormatPebbles::PebbleScaleVariation, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PebbleScaleVariationHint", "Random size spread per stone."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleRotation", "Rotation"), Pebble, &FMixtormatPebbles::PebbleRotation, 0.0, 180.0, 180.0, 1.0,
		LOCTEXT("PebbleRotationHint", "Maximum random rotation, degrees.")));

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("PebbleGrpShape", "Shape"));
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatPebbles>(
		LOCTEXT("PebbleDirection", "Cut Direction"), Pebble, &FMixtormatPebbles::PebbleDirection,
		LOCTEXT("PebbleDirectionHint", "How the cut planes are oriented: evenly spread, random, opposed pairs, golden angle, or biased to the axes.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPebbles>(
			LOCTEXT("PebbleCuts", "Cuts"), Pebble, &FMixtormatPebbles::PebbleCuts, 3.0, 24.0, 10,
			LOCTEXT("PebbleCutsHint", "Planes per stone. More reads rounder.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleIrregularity", "Irregularity"), Pebble, &FMixtormatPebbles::PebbleIrregularity, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PebbleIrregularityHint", "Jitters radius, chamfer, cut angle and cut count."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleChamfer", "Chamfer"), Pebble, &FMixtormatPebbles::PebbleChamfer, 0.0, 0.2, 0.02, 0.001,
		LOCTEXT("PebbleChamferHint", "Edge chamfer between cuts.")));

	Panel = AddCard(Cards, LOCTEXT("PebbleGrpHeight", "Height"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleSteepness", "Steepness"), Pebble, &FMixtormatPebbles::PebbleSteepness, 0.0, 16.0, 5.6, 0.05,
			LOCTEXT("PebbleSteepnessHint", "Facet rise per unit inward.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleSteepnessVariation", "Steep Var"), Pebble, &FMixtormatPebbles::PebbleSteepnessVariation, 0.0, 4.0, 1.7, 0.01,
			LOCTEXT("PebbleSteepnessVariationHint", "Log2 spread of steepness per facet."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleBiasVariation", "Facet Offset"), Pebble, &FMixtormatPebbles::PebbleBiasVariation, 0.0, 0.5, 0.08, 0.005,
			LOCTEXT("PebbleBiasVariationHint", "Random height offset per facet.")),
		MakeMemberSlider<FMixtormatPebbles>(
			LOCTEXT("PebbleHeightGain", "Height"), Pebble, &FMixtormatPebbles::PebbleHeightGain, 0.0, 2.0, 1.0, 0.01,
			LOCTEXT("PebbleHeightGainHint", "Overall stone height."))));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatPebbles>(
		LOCTEXT("PebbleHeightVariation", "Height Var"), Pebble, &FMixtormatPebbles::PebbleHeightVariation, 0.0, 1.0, 0.3, 0.01,
		LOCTEXT("PebbleHeightVariationHint", "Random height drop per stone.")));
	AddSliderRow(Panel, MixtormatRow::MakeTrailing(
		LOCTEXT("PebbleFacetIds", "Facet IDs"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatPebbles* P = GetSelectedPebbles();
				return P && P->bPebbleFacetIds ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatPebbles* P = GetSelectedPebbles())
				{
					P->bPebbleFacetIds = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("PebbleFacetIdsHint", "One region ID per facet instead of per stone.")));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedPebbles() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("PebblesHeading", "PEBBLES"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::Pebbles;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
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
						LOCTEXT("PebblesEnabledHint", "Enable these pebbles"))
				])
			[
				Cards
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildCliffStrataControls()
{
	const auto C=[this](){return GetSelectedCliffStrata();};
	TSharedRef<SVerticalBox> Panel=SNew(SVerticalBox);const TSharedRef<SVerticalBox> Cards=Panel;
	auto Pair=[&](TSharedRef<SWidget>A,TSharedRef<SWidget>B){AddSliderRow(Panel,MixtormatRow::MakePair(A,B));};
	Panel=AddCard(Cards,LOCTEXT("CliffOutput","OUTPUT"));
	Pair(
		MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightScale","Scale"),C,&FMixtormatCliffStrata::CliffHeightScale,-4,4,1,.01,LOCTEXT("CliffHeightScaleHint","Scales the signed generator height after normalization.")),
		MixtormatRow::MakeTrailing(
			LOCTEXT("CliffNormalizeHeight","Normalize"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([C](){const FMixtormatCliffStrata* G=C();return G&&G->bCliffNormalizeHeight?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}),
				FOnCheckStateChanged::CreateLambda([this,C](ECheckBoxState S){if(FMixtormatCliffStrata* G=C()){G->bCliffNormalizeHeight=S==ECheckBoxState::Checked;RefreshLayeredPreview();}})),
			LOCTEXT("CliffNormalizeHeightHint","Zero-preserving max-absolute normalization to -0.5..0.5.")));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDepthMin","Depth Min"),C,&FMixtormatCliffStrata::DepthMin,-4,4,-1,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDepthMax","Depth Max"),C,&FMixtormatCliffStrata::DepthMax,-4,4,1,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffLayout","LAYOUT"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffCountX","Count X"),C,&FMixtormatCliffStrata::CountX,1,64,8,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffCountY","Count Y"),C,&FMixtormatCliffStrata::CountY,1,64,7,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffDensity","Density"),C,&FMixtormatCliffStrata::Density,0,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffJitter","Jitter"),C,&FMixtormatCliffStrata::Jitter,0,1,.1,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffSizeMin","Size Min"),C,&FMixtormatCliffStrata::SizeMin,.02,3,1,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffSizeMax","Size Max"),C,&FMixtormatCliffStrata::SizeMax,.02,3,1.35,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffAspect","Aspect"),C,&FMixtormatCliffStrata::SizeAspect,.02,3,.95,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFlowVariation","Flow Var"),C,&FMixtormatCliffStrata::FlowVariation,0,3.14159,.6,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffFormation","FORMATION"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightMin","Height Min"),C,&FMixtormatCliffStrata::HeightMin,-1,1,.025,.005,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffHeightMax","Height Max"),C,&FMixtormatCliffStrata::HeightMax,-1,1,.1,.005,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffRotation","Rotation"),C,&FMixtormatCliffStrata::Rotation,-1,1,.35,.01,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSteps","Steps"),C,&FMixtormatCliffStrata::Steps,0,32,0,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffLeanX","Lean X"),C,&FMixtormatCliffStrata::LeanX,-2,2,.2,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffLeanY","Lean Y"),C,&FMixtormatCliffStrata::LeanY,-2,2,0,.01,FText::GetEmpty()));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffFormationCells","Cells"),C,&FMixtormatCliffStrata::FormationCells,1,32,5,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFormationAmount","Amount"),C,&FMixtormatCliffStrata::FormationAmount,0,1,1,.01,FText::GetEmpty()));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSides","Sides"),C,&FMixtormatCliffStrata::Sides,3,12,4,FText::GetEmpty()),MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffSeed","Seed"),C,&FMixtormatCliffStrata::Seed,0,99999,1234,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffQuarters","QUARTERS"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterY","Y Count"),C,&FMixtormatCliffStrata::QuarterYCount,1,32,8,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterFill","Fill"),C,&FMixtormatCliffStrata::QuarterFill,0,1,.5,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterSize","Size"),C,&FMixtormatCliffStrata::QuarterSize,.01,3,.9,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQuarterHeight","Height"),C,&FMixtormatCliffStrata::QuarterHeight,0,6,2.75,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQJX","Jitter X"),C,&FMixtormatCliffStrata::QuarterJitterX,0,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffQJY","Jitter Y"),C,&FMixtormatCliffStrata::QuarterJitterY,0,1,.5,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffProjection","PROJECTION"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYaw","Yaw"),C,&FMixtormatCliffStrata::CameraYaw,-1,1,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffPitch","Pitch"),C,&FMixtormatCliffStrata::CameraPitch,-1,1,.25,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffViewScale","View Scale"),C,&FMixtormatCliffStrata::ViewScale,1.0f,8,2.50f,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffPattern","PATTERN"));
	Pair(MakeMemberSliderInt<FMixtormatCliffStrata>(LOCTEXT("CliffVoroCells","Voronoi Cells"),C,&FMixtormatCliffStrata::VoronoiCells,1,64,13,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffFlowVoro","Flow Voronoi"),C,&FMixtormatCliffStrata::FlowVoronoi,-2,2,0,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffShape","SHAPE"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffUnitDist","Unit Distance"),C,&FMixtormatCliffStrata::UnitDistance,.001,2,.3,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffUnitId","ID Variation"),C,&FMixtormatCliffStrata::UnitDistanceIdLerp,0,1,.5,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCarveDepth","Carve Depth"),C,&FMixtormatCliffStrata::CarveDepth,-2,2,.375,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCarveVoro","Carve Voronoi"),C,&FMixtormatCliffStrata::CarveVoronoi,-2,2,.1,.01,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYBias","Y Bias"),C,&FMixtormatCliffStrata::YBias,-4,4,.3,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffYBiasVoro","Y Bias Voronoi"),C,&FMixtormatCliffStrata::YBiasVoronoi,-4,4,.9,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffNegYTaper","Negative Y Taper"),C,&FMixtormatCliffStrata::NegativeYUnitDistanceTaper,-4,4,1,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffChamfer","CHAMFER"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferWidth","Width"),C,&FMixtormatCliffStrata::ChamferWidth,0,2,.5,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferIntensity","Amount"),C,&FMixtormatCliffStrata::ChamferIntensity,-4,4,1,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffChamferVoro","Voronoi"),C,&FMixtormatCliffStrata::ChamferVoronoi,-2,2,.05,.01,FText::GetEmpty()));
	Panel=AddCard(Cards,LOCTEXT("CliffCavity","CAVITY"));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityAmount","Amount"),C,&FMixtormatCliffStrata::CavityIntensity,-4,4,.25,.01,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffBlockCavityWidth","Block"),C,&FMixtormatCliffStrata::BlockCavityWidth,.001,1,.02,.005,FText::GetEmpty()));
	Pair(MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffRowCavityWidth","Row"),C,&FMixtormatCliffStrata::RowCavityWidth,.001,1,.25,.005,FText::GetEmpty()),MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityThreshold","Threshold"),C,&FMixtormatCliffStrata::CavityVoronoiThreshold,0,1,.5,.01,FText::GetEmpty()));
	AddSliderRow(Panel,MakeMemberSlider<FMixtormatCliffStrata>(LOCTEXT("CliffCavityGain","Gain"),C,&FMixtormatCliffStrata::CavityVoronoiMaskGain,.001,1,.125,.005,FText::GetEmpty()));
	return SNew(SBox).Visibility_Lambda([this](){return GetSelectedCliffStrata()?EVisibility::Visible:EVisibility::Collapsed;})[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("CliffStrataHeading","CLIFF STRATA")).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().Padding(0,0,FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap,0)[MakeChildOutputPreviewButton([](){FMixtormatLayerChild Probe;Probe.Type=EMixtormatLayerChildType::Generator;Probe.Generator.Type=EMixtormatGeneratorType::CliffStrata;return GetChildPreviewOutputSet(Probe);}())]
			+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(TAttribute<ECheckBoxState>::CreateLambda([this](){const FMixtormatGenerator* G=GetSelectedGenerator();return G&&G->bEnabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}),FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S){if(FMixtormatGenerator* G=GetSelectedGenerator()){G->bEnabled=S==ECheckBoxState::Checked;RefreshLayeredPreview();RebuildLayerList();}}))])
		[Cards]];
}

TSharedRef<SWidget> SMixtormat::BuildHeightBlendSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	if (!GetSelectedHeightBlend()) { return Menu.Build(); }
	Menu.Item(LOCTEXT("HeightBlendSourceNone", "None"), nullptr,
		FSimpleDelegate::CreateLambda([this]()
		{
			if (FMixtormatGeneratorHeightBlend* Blend = GetSelectedHeightBlend())
			{
				Blend->SourceLayerId.Invalidate();
				Blend->SourceChildId.Invalidate();
				RefreshLayeredPreview();
			}
		}));
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FGuid LayerId = WorkingLayers[SelectedLayerIndex].LayerId;
		for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Generator) { continue; }
			const FGuid ChildId = Child.ChildId;
			Menu.Item(GetLayerChildName(Child), MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, LayerId, ChildId]()
				{
					if (FMixtormatGeneratorHeightBlend* Blend = GetSelectedHeightBlend())
					{
						Blend->SourceLayerId = LayerId;
						Blend->SourceChildId = ChildId;
						RefreshLayeredPreview();
					}
				}));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildHeightBlendModuleControls()
{
	const auto Blend = [this]() { return GetSelectedHeightBlend(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MakeMemberEnum<FMixtormatGeneratorHeightBlend, EMixtormatGeneratorHeightOp>(
		LOCTEXT("HeightBlendOp", "Operation"), Blend, &FMixtormatGeneratorHeightBlend::Op,
		LOCTEXT("HeightBlendOpHint", "How the running generator height combines with the referenced module."),
		FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
	AddSliderRow(Panel, MixtormatRow::MakeTrailing(
		LOCTEXT("HeightBlendSource", "Source"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
				if (!B || !B->SourceChildId.IsValid()) { return LOCTEXT("HeightBlendSourceNoneLabel", "None"); }
				if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
				{
					for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
					{
						if (Child.ChildId == B->SourceChildId) { return GetLayerChildName(Child); }
					}
				}
				return LOCTEXT("HeightBlendSourceMissing", "Missing");
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildHeightBlendSourceMenu)),
		LOCTEXT("HeightBlendSourceHint", "Another module in this layer whose signed height is the second operand.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
		LOCTEXT("HeightBlendAmount", "Amount"), Blend, &FMixtormatGeneratorHeightBlend::Amount, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("HeightBlendAmountHint", "How much of the combined result replaces the running height.")));

	// Scale belongs to Multiply/Scale alone; Softness is the rounded join shared by Min, Max and
	// Height Blend. Each row is shown only where the shader actually reads it, so the panel never
	// offers a control that does nothing for the selected operation.
	const auto OpIs = [Blend](const EMixtormatGeneratorHeightOp Op)
	{
		const FMixtormatGeneratorHeightBlend* B = Blend();
		return B && B->Op == Op;
	};
	AddSliderRow(Panel, SNew(SBox).Visibility_Lambda([OpIs]()
	{
		return OpIs(EMixtormatGeneratorHeightOp::Multiply) ? EVisibility::Visible : EVisibility::Collapsed;
	})[
		MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
			LOCTEXT("HeightBlendScale", "Scale"), Blend, &FMixtormatGeneratorHeightBlend::Scale, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("HeightBlendScaleHint", "Multiply/Scale factor. With no source the module is neutral."))]);
	AddSliderRow(Panel, SNew(SBox).Visibility_Lambda([OpIs]()
	{
		return OpIs(EMixtormatGeneratorHeightOp::Min) || OpIs(EMixtormatGeneratorHeightOp::Max)
			|| OpIs(EMixtormatGeneratorHeightOp::HeightBlend) ? EVisibility::Visible : EVisibility::Collapsed;
	})[
		MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
			LOCTEXT("HeightBlendSoftness", "Softness"), Blend, &FMixtormatGeneratorHeightBlend::Softness, 0.0, 1.0, 0.0, 0.001,
			LOCTEXT("HeightBlendSoftnessHint", "Rounded join for Min, Max and Height Blend, in height units. 0 is a hard join."))]);

	const TSharedRef<SVerticalBox> Settings = SNew(SVerticalBox)
		.Visibility_Lambda([Blend]()
		{
			const FMixtormatGeneratorHeightBlend* B = Blend();
			return B && B->Op == EMixtormatGeneratorHeightOp::HeightBlend
				? EVisibility::Visible : EVisibility::Collapsed;
		});
	AddSliderRow(Settings, MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
		LOCTEXT("HeightBlendThreshold", "Threshold"), Blend, &FMixtormatGeneratorHeightBlend::Threshold, -1.0, 1.0, 0.0, 0.01));
	AddSliderRow(Settings, MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
		LOCTEXT("HeightBlendEdgeSoftness", "Edge Softness"), Blend, &FMixtormatGeneratorHeightBlend::EdgeSoftness, 0.0, 1.0, 0.1, 0.005));
	AddSliderRow(Settings, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
			LOCTEXT("HeightBlendBaseBias", "Base Bias"), Blend, &FMixtormatGeneratorHeightBlend::BaseBias, -1.0, 1.0, 0.0, 0.01),
		MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
			LOCTEXT("HeightBlendBlendBias", "Blend Bias"), Blend, &FMixtormatGeneratorHeightBlend::BlendBias, -1.0, 1.0, 0.0, 0.01)));
	AddSliderRow(Panel, Settings);

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightBlend() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("HeightBlendHeading", "HEIGHT BLEND")).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
					return B && B->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S)
				{
					if (FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend())
					{
						B->bEnabled = S == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))])
		[Panel]];
}

TSharedRef<SWidget> SMixtormat::BuildHeightCurveControls()
{
	const auto Curve = [this]() { return GetSelectedHeightCurve(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// Optional zero-preserving max-absolute normalization to -1..1 before the signed input range.
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapNormalize", "Normalize Input"), Curve, &FMixtormatGeneratorHeightCurve::bNormalizeInput,
		LOCTEXT("HeightRemapNormalizeHint", "Zero-preserving max-absolute normalization to -1..1.")));
	// Signed input range. The pivot is zero: positive values divide by InputMax, negative by abs(InputMin).
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapInputMin", "Input Min"), Curve, &FMixtormatGeneratorHeightCurve::InputMin, -2.0, 0.0, -1.0, 0.01,
		LOCTEXT("HeightRemapInputMinHint", "Signed input range minimum. Negative values divide by abs(InputMin).")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapInputMax", "Input Max"), Curve, &FMixtormatGeneratorHeightCurve::InputMax, 0.0, 2.0, 1.0, 0.01,
		LOCTEXT("HeightRemapInputMaxHint", "Signed input range maximum. Positive values divide by InputMax.")));
	// Sign-preserving power transform: sign(x) * pow(abs(x), exponent). Identity at 1.
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapBalance", "Balance"), Curve, &FMixtormatGeneratorHeightCurve::Balance, 0.25, 4.0, 1.0, 0.01,
		LOCTEXT("HeightRemapBalanceHint", "Sign-preserving power transform. Identity at 1.")));
	// Multiplicative contrast pivoted around zero. Identity at 1.
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapContrast", "Contrast"), Curve, &FMixtormatGeneratorHeightCurve::Contrast, 0.0, 4.0, 1.0, 0.01,
		LOCTEXT("HeightRemapContrastHint", "Multiplicative contrast pivoted around zero. Identity at 1.")));
	// Signed addition. Identity at 0.
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapOffset", "Offset"), Curve, &FMixtormatGeneratorHeightCurve::Offset, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("HeightRemapOffsetHint", "Signed addition. Identity at 0.")));
	// True means -x, not 1-x. Identity when false.
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapInvert", "Invert"), Curve, &FMixtormatGeneratorHeightCurve::bInvert,
		LOCTEXT("HeightRemapInvertHint", "Negate the result (-x, not 1-x).")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatGeneratorHeightCurve>(
		LOCTEXT("HeightRemapAmount", "Amount"), Curve, &FMixtormatGeneratorHeightCurve::Amount, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("HeightRemapAmountHint", "How much of the remapped result replaces the running height.")));
	AddSliderRow(Panel, SNew(SMixtormatScalarRamp)
		.Ramp(TAttribute<FMixtormatScalarRamp>::CreateLambda([Curve]()
		{
			const FMixtormatGeneratorHeightCurve* C = Curve();
			return C ? C->Curve : FMixtormatScalarRamp();
		}))
		.CanonicalXMin(-1.0f).CanonicalXMax(1.0f)
		.CanonicalYMin(-1.0f).CanonicalYMax(1.0f)
		.SoftYMin(-1.5f).SoftYMax(1.5f)
		.ExtendedYMin(-3.0f).ExtendedYMax(3.0f)
		.OnChanged(FOnMixtormatScalarRampChanged::CreateLambda([this, Curve](const FMixtormatScalarRamp& NewRamp)
		{
			if (FMixtormatGeneratorHeightCurve* C = Curve())
			{
				C->Curve = NewRamp;
				RefreshLayeredPreview();
			}
		})));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightCurve() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("HeightRemapHeading", "HEIGHT REMAP")).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatGeneratorHeightCurve* C = GetSelectedHeightCurve();
					return C && C->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S)
				{
					if (FMixtormatGeneratorHeightCurve* C = GetSelectedHeightCurve())
					{
						C->bEnabled = S == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))])
		[Panel]];
}

TSharedRef<SWidget> SMixtormat::BuildHeightColorRampControls()
{
	const auto Ramp = [this]() { return GetSelectedHeightColorRamp(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, SNew(SMixtormatColorRamp)
		.Ramp(TAttribute<FMixtormatColorRamp>::CreateLambda([Ramp]()
		{
			const FMixtormatGeneratorHeightColorRamp* R = Ramp();
			return R ? R->Ramp : FMixtormatColorRamp();
		}))
		.DomainMin(-1.0f).DomainMax(1.0f)
		.OnChanged(FOnMixtormatColorRampChanged::CreateLambda([this, Ramp](const FMixtormatColorRamp& NewRamp)
		{
			if (FMixtormatGeneratorHeightColorRamp* R = Ramp())
			{
				R->Ramp = NewRamp;
				RefreshLayeredPreview();
			}
		})));

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightColorRamp() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("ColorRampHeading", "COLOR RAMP")).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
				FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakeChildOutputPreviewButton(
					GetPreviewOutputSetForChildType(EMixtormatLayerChildType::HeightColorRamp))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp();
					return R && R->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](ECheckBoxState S)
				{
					if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
					{
						R->bEnabled = S == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))])
		[Panel]];
}

TSharedRef<SWidget> SMixtormat::BuildRockFormationControls()
{
	const auto Rock = [this]() { return GetSelectedRockFormation(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Panel, LOCTEXT("RockOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockHeightScale", "Scale"), Rock, &FMixtormatRockFormation::RockHeightScale, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("RockHeightScaleHint", "Scales the signed generator height after normalization.")),
			MixtormatRow::MakeTrailing(
				LOCTEXT("RockNormalizeHeight", "Normalize"),
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Rock]()
					{
						const FMixtormatRockFormation* G = Rock();
						return G && G->bRockNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Rock](const ECheckBoxState State)
					{
						if (FMixtormatRockFormation* G = Rock())
						{
							G->bRockNormalizeHeight = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					})),
				LOCTEXT("RockNormalizeHeightHint", "Zero-preserving max-absolute normalization to -0.5..0.5."))));
		AddSliderRow(Output, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockDepthMin", "Depth Min"), Rock, &FMixtormatRockFormation::RockDepthMin, -4.0, 4.0, -1.0, 0.01,
				LOCTEXT("RockDepthMinHint", "Minimum depth value after remapping (default -1).")),
			MakeMemberSlider<FMixtormatRockFormation>(
				LOCTEXT("RockDepthMax", "Depth Max"), Rock, &FMixtormatRockFormation::RockDepthMax, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("RockDepthMaxHint", "Maximum depth value after remapping (default 1)."))));
	}

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("RockGrpShape", "SHAPE"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStyle", "Style"), Rock, &FMixtormatRockFormation::RockStyle, 0.0, 1.0, 0.05, 0.01,
			LOCTEXT("RockStyleHint", "0..1 blends preset positions 1..2.5, from layered through boulder toward rubble.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockCells", "Cells"), Rock, &FMixtormatRockFormation::RockCells, 1.0, 32.0, 4,
			LOCTEXT("RockCellsHint", "Cells across one repeat of the tile."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockRows", "Rows"), Rock, &FMixtormatRockFormation::RockRows, 1.0, 32.0, 12,
			LOCTEXT("RockRowsHint", "Number of rock rows from top to bottom of the tile.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockSeed", "Seed"), Rock, &FMixtormatRockFormation::RockSeed, 0.0, 9999.0, 1,
			LOCTEXT("RockSeedHint", "Changes cell layout, fractures, lean, jag and chips."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFracture", "Fracture"), Rock, &FMixtormatRockFormation::RockFracture, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockFractureHint", "Scales how many times each cell is split into chunks.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFractureHeightBias", "Fracture Height Bias"), Rock, &FMixtormatRockFormation::RockFractureHeightBias, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockFractureHeightBiasHint", "Randomly raises or lowers individual fractured pieces."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSizeRandom", "Size Random"), Rock, &FMixtormatRockFormation::RockSizeRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSizeRandomHint", "Mixes big and small cells while preserving the seamless partition.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretch", "Stretch"), Rock, &FMixtormatRockFormation::RockStretch, -1.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockStretchHint", "0 is neutral; +1 doubles cells along Stretch Angle, -1 across it."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretchAngle", "Stretch Angle"), Rock, &FMixtormatRockFormation::RockStretchAngle, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockStretchAngleHint", "Direction cells elongate in. 0..1 is half a turn.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockStretchRandom", "Stretch Random"), Rock, &FMixtormatRockFormation::RockStretchRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockStretchRandomHint", "Narrows each chunk along a random axis, inside its own space."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockHeightClusters", "Height Clusters"), Rock, &FMixtormatRockFormation::RockHeightClusters, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockHeightClustersHint", "Scales clustered height variation between chunks and rows.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSkew", "Skew"), Rock, &FMixtormatRockFormation::RockSkew, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockSkewHint", "Offsets rock rows diagonally in cell widths while preserving tile wrapping."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpTilt", "TILT"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltAngle", "Angle"), Rock, &FMixtormatRockFormation::RockTiltAngle, -1.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockTiltAngleHint", "Shared lean toward Direction. +/-1 is +/- one eighth of a turn; walls and chamfers follow.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltDirection", "Direction"), Rock, &FMixtormatRockFormation::RockTiltDirection, 0.0, 1.0, 0.75, 0.01,
			LOCTEXT("RockTiltDirectionHint", "Which way rocks lean. 0..1 is a full turn."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockTiltRandom", "Random"), Rock, &FMixtormatRockFormation::RockTiltRandom, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockTiltRandomHint", "Random lean per rock; 1 is one eighth of a turn. Pieces share their rock's lean with a small deviation.")),
		SNullWidget::NullWidget));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSpin", "Spin"), Rock, &FMixtormatRockFormation::RockSpin, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSpinHint", "Rotates each chunk about the vertical axis. +/-1 is +/- half a turn; chunks scale uniformly to fit, without clipping corners.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockSpinRandom", "Spin Random"), Rock, &FMixtormatRockFormation::RockSpinRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RockSpinRandomHint", "Random spin per chunk. 1 adds up to half a turn in either direction."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpEdges", "EDGES"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockGap", "Gap"), Rock, &FMixtormatRockFormation::RockGap, -1.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockGapHint", "Positive shrinks rock regions; zero preserves their size; negative expands them. Shapes height only, never layer transparency.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamfer", "Chamfer"), Rock, &FMixtormatRockFormation::RockChamfer, -1.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockChamferHint", "Share of each edge's room to the chunk centre, scaled by 0.75. Positive cuts the bevel in; zero is no chamfer; negative raises a lip. Seams take 0.6 of the outline chamfer."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamferRandom", "Chamfer Random"), Rock, &FMixtormatRockFormation::RockChamferRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockChamferRandomHint", "Varies chamfer width between chunk edges.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockChamferJag", "Chamfer Jag"), Rock, &FMixtormatRockFormation::RockChamferJag, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockChamferJagHint", "Varies bevel width along each rim with its own jag pattern. Seams take 1.5 times the variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockEdgeJag", "Edge Jag"), Rock, &FMixtormatRockFormation::RockEdgeJag, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockEdgeJagHint", "Zigzag strength relative to each chunk. Seams use half the strength at twice the frequency.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockJagScale", "Jag Scale"), Rock, &FMixtormatRockFormation::RockJagScale, 1.0, 16.0, 4.0, 0.01,
			LOCTEXT("RockJagScaleHint", "Jag frequency relative to each chunk's size, not the tile or pixels."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockJagDetail", "Jag Detail"), Rock, &FMixtormatRockFormation::RockJagDetail, 0.0, 1.0, 0.2, 0.01,
			LOCTEXT("RockJagDetailHint", "Strength of the finer zigzag octaves.")),
		SNullWidget::NullWidget));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockRimChips", "Rim Chips"), Rock, &FMixtormatRockFormation::RockRimChips, 0.0, 1.0, 0.1, 0.01,
			LOCTEXT("RockRimChipsHint", "Angular bites along the rims, widening the chamfer around each chip.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockRimChipSize", "Rim Chip Size"), Rock, &FMixtormatRockFormation::RockRimChipSize, 0.0, 1.0, 0.075, 0.001,
			LOCTEXT("RockRimChipSizeHint", "Chip size relative to each chunk."))));

	Panel = AddCard(Cards, LOCTEXT("RockGrpFacets", "FACETS"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetChips", "Chips"), Rock, &FMixtormatRockFormation::RockFacetChips, -1.0, 1.0, 0.5, 0.01,
			LOCTEXT("RockFacetChipsHint", "Depth of faceted cuts into each chunk, eased so planes never pass its centre. Positive cuts in; zero is flat; negative raises the facets out.")),
		MakeMemberSliderInt<FMixtormatRockFormation>(
			LOCTEXT("RockFacetIterations", "Iterations"), Rock, &FMixtormatRockFormation::RockFacetIterations, 0.0, 8.0, 3,
			LOCTEXT("RockFacetIterationsHint", "Facet rounds, adding 3, 5, 7 and then more planes. 0 disables facet cuts."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetFalloff", "Falloff"), Rock, &FMixtormatRockFormation::RockFacetFalloff, 0.0, 4.0, 4.0, 0.01,
			LOCTEXT("RockFacetFalloffHint", "Scales each successive facet round. Values above 1 grow later rounds.")),
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetRandom", "Random"), Rock, &FMixtormatRockFormation::RockFacetRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RockFacetRandomHint", "Varies round count, depth and falloff per rock. Pieces take a small share of their own variation."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatRockFormation>(
			LOCTEXT("RockFacetAlign", "Align"), Rock, &FMixtormatRockFormation::RockFacetAlign, -1.0, 1.0, 0.75, 0.01,
			LOCTEXT("RockFacetAlignHint", "Positive swings facet planes toward the lean's low side; negative toward its high side.")),
		SNullWidget::NullWidget));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRockFormation() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RockFormationHeading", "ROCK FORMATION"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton([]()
					{
						// The generator kind decides the outputs, so the probe names it.
						FMixtormatLayerChild Probe;
						Probe.Type = EMixtormatLayerChildType::Generator;
						Probe.Generator.Type = EMixtormatGeneratorType::RockFormation;
						return GetChildPreviewOutputSet(Probe);
					}())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
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
						LOCTEXT("RockFormationEnabledHint", "Enable this rock formation"))
				])
			[
				Cards
			]
		];
}

// The Noise inspector panel is an integration-pass registration, like the capability and menu
// entries: it is an SMixtormat member (BuildNoiseControls), and the declaration lives in
// SMixtormat.h beside BuildCliffStrataControls, which this file does not own. The panel itself
// follows the BuildCliffStrataControls shape -- OUTPUT card (Normalize Height, Scale), then
// Type / Seed / Scale, Detail / Roughness / Lacunarity disabled rather than hidden on the
// single-octave families, Offset X/Y, and Direction disabled everywhere but Bars.

#undef LOCTEXT_NAMESPACE
