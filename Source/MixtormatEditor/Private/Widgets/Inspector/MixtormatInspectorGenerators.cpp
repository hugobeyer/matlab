// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatChildScope.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildGeneratorFlowControls(const EMixtormatEffectType Type)
{
	const bool bGravity = Type == EMixtormatEffectType::GravityFlow;
	const auto Flow = [this, Type]() -> FMixtormatLayerEffect*
	{
		FMixtormatLayerEffect* Effect = GetSelectedGeneratorFlow();
		return Effect && Effect->ProceduralType == Type ? Effect : nullptr;
	};
	const auto ResolveOwner = [this]() -> const FMixtormatLayerChild*
	{
		const FMixtormatChildAddress Address = GetSelectedChildAddress();
		const FMixtormatLayerChild* Child = ResolveChildAt(Address);
		const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
		const FMixtormatLayerChild* Owner = Child && Children
			? Children->FindByPredicate([Child](const FMixtormatLayerChild& Candidate)
				{ return Candidate.ChildId == Child->ScopeOwnerChildId; }) : nullptr;
		return Owner;
	};
	const auto HasOwner = [ResolveOwner]()
	{
		const FMixtormatLayerChild* Owner = ResolveOwner();
		return Owner && Owner->Type == EMixtormatLayerChildType::Generator
			&& MixtormatCanOwnGeneratorFlow(Owner->Generator.Type);
	};
	const auto IsHeightOnlyOwner = [ResolveOwner]()
	{
		const FMixtormatLayerChild* Owner = ResolveOwner();
		return Owner && Owner->Type == EMixtormatLayerChildType::Generator
			&& !MixtormatGeneratorHasFlowBoundary(Owner->Generator.Type);
	};
	const auto SourceAddress = MakeAddressResolver<FMixtormatLayerEffect>(
		Flow, &FMixtormatLayerEffect::GeneratorFlowSource);
	const UEnum* SourceEnum = StaticEnum<EMixtormatGeneratorFlowSource>();
	const auto ActiveSource = [this, Flow, SourceAddress]() -> int64
	{
		const FMixtormatLayerEffect* Effect = Flow();
		return GetEffectiveEnumParameter(SourceAddress(), Effect ? static_cast<int64>(Effect->GeneratorFlowSource) : 0);
	};
	const auto WriteNoiseSource = [this, Flow, SourceAddress](const int64 Value)
	{
		if (Value != static_cast<int64>(EMixtormatGeneratorFlowSource::Height)) { return; }
		if (FMixtormatLayerEffect* Effect = Flow())
		{
			const FMixtormatParameterAddress Address = SourceAddress();
			if (IsParameterLocked(Address)) { return; }
			if (!TryWriteLinkedEnum(Address, Value))
			{
				Effect->GeneratorFlowSource = static_cast<EMixtormatGeneratorFlowSource>(Value);
				if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
				{
					Binding->Reference.bEnabled = false;
				}
			}
			RefreshLayeredPreview();
		}
	};
	TSharedRef<SWidget> NoiseSourceChip = MixtormatRow::MakeChip(
		TAttribute<FText>::CreateLambda([SourceEnum, ActiveSource]()
		{
			return SourceEnum->GetDisplayNameTextByValue(ActiveSource());
		}),
		FOnGetContent::CreateLambda([SourceEnum, ActiveSource, WriteNoiseSource]()
		{
			MixtormatMenu::FBuilder Menu;
			for (int32 Index = 0; Index < SourceEnum->NumEnums(); ++Index)
			{
				const int64 Value = SourceEnum->GetValueByIndex(Index);
				if (Value == INDEX_NONE || SourceEnum->HasMetaData(TEXT("Hidden"), Index)) { continue; }
				const bool bAvailable = Value == static_cast<int64>(EMixtormatGeneratorFlowSource::Height);
				const FText Label = bAvailable ? SourceEnum->GetDisplayNameTextByIndex(Index)
					: FText::Format(LOCTEXT("FlowSourceMissingBoundary", "{0} — no signed boundary field"),
						SourceEnum->GetDisplayNameTextByIndex(Index));
				Menu.Item(Label, nullptr,
					FSimpleDelegate::CreateLambda([WriteNoiseSource, Value]() { WriteNoiseSource(Value); }))
					.Checked(TAttribute<bool>::CreateLambda([ActiveSource, Value]() { return ActiveSource() == Value; }))
					.Enabled(bAvailable);
			}
			return Menu.Build();
		}), nullptr, TAttribute<FText>(), 0.0f);
	FEnumResetBinding& NoiseSourceReset = EnumResetBindings.AddDefaulted_GetRef();
	NoiseSourceReset.Widget = NoiseSourceChip;
	NoiseSourceReset.Reset = FSimpleDelegate::CreateLambda([WriteNoiseSource]()
	{
		WriteNoiseSource(static_cast<int64>(EMixtormatGeneratorFlowSource::Height));
	});
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox).IsEnabled_Lambda(HasOwner);
	AddSliderRow(Panel, SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([IsHeightOnlyOwner]() { return IsHeightOnlyOwner() ? 1 : 0; })
		+ SWidgetSwitcher::Slot()
		[
			MakeMemberEnum<FMixtormatLayerEffect>(
				bGravity ? LOCTEXT("GravityFlowSteering", "Steering") : LOCTEXT("GeneratorFlowSource", "Source"), Flow, &FMixtormatLayerEffect::GeneratorFlowSource,
				bGravity ? LOCTEXT("GravityFlowSteeringHint", "Height bends gravity downhill. Signed Distance steers around the owning generator's boundaries; it is not a scene collision solver.")
					: LOCTEXT("GeneratorFlowSourceHint", "Uses the owning generator's signed distance or height field. Noise and Cliff Strata support Height steering; neither publishes a signed boundary field."))
		]
		+ SWidgetSwitcher::Slot()
		[
			WrapParameterControl(MixtormatRow::MakeDropdown(bGravity ? LOCTEXT("GravityFlowSteering", "Steering") : LOCTEXT("GeneratorFlowSource", "Source"), NoiseSourceChip,
				LOCTEXT("NoiseGeneratorFlowSourceHint", "This generator supports Height steering. Signed Distance is unavailable because Noise and Cliff Strata do not publish signed boundary fields.")), SourceAddress)
		]);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAmount", "Amount"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowAmount, 0.0, 1.0, 1.0, 0.01),
		SNew(SBox).IsEnabled(!bGravity)
		[
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowTangent", "Normal / Tangent"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowTangent, 0.0, 1.0, 0.0, 0.01)
		]));
	if (bGravity)
	{
		AddSliderRow(Panel, MixtormatRow::MakePair(
			SNew(SBox).IsEnabled_Lambda([ActiveSource]()
				{ return ActiveSource() == static_cast<int64>(EMixtormatGeneratorFlowSource::Height); })
			[
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GravityFlowSurfaceFollow", "Surface Follow"), Flow,
					&FMixtormatLayerEffect::GravityFlowSurfaceFollow, 0.0, 2.0, 1.0, 0.01,
					LOCTEXT("GravityFlowSurfaceFollowHint", "Steers texture-space gravity downhill through this generator's height. Zero gives uniform gravity; flat areas still flow."))
			],
			SNew(SBox).IsEnabled_Lambda([ActiveSource, IsHeightOnlyOwner]()
				{ return !IsHeightOnlyOwner() && ActiveSource() == static_cast<int64>(EMixtormatGeneratorFlowSource::SignedDistance); })
			[
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GravityFlowDeflection", "Boundary Deflection"), Flow,
					&FMixtormatLayerEffect::GravityFlowDeflection, 0.0, 1.0, 1.0, 0.01,
					LOCTEXT("GravityFlowDeflectionHint", "Removes motion into the owner's signed boundary within Reach. Known interiors stay unmoved. Head-on flow can stop; this is approximate boundary steering, not fluid simulation."))
			]));
	}
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatLayerEffect>(bGravity ? LOCTEXT("GravityFlowAngle", "Gravity Angle") : LOCTEXT("GeneratorFlowAngle", "Angle"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowAngle, -180.0, 180.0, 0.0, 1.0,
			bGravity ? LOCTEXT("GravityFlowAngleHint", "Texture-space gravity: 0 degrees is -V, 90 is +U, and 180 is +V. Warping backtraces against this direction.") : FText::GetEmpty()),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowBend", "Bend"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowBend, -180.0, 180.0, 0.0, 1.0)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowRadius", "Radius (texels)"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowRadius, 1.0, 16.0, 2),
		MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowSmooth", "Smooth (texels)"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowSmooth, 0.0, 64.0, 8.0, 0.5,
			LOCTEXT("GeneratorFlowSmoothHint", "Blurs the flow direction. Removes the stepping of the raw field; collisions between opposing flows stay sharp."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox).IsEnabled_Lambda([bGravity, ActiveSource]()
			{ return !bGravity || ActiveSource() == static_cast<int64>(EMixtormatGeneratorFlowSource::SignedDistance); })
		[
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowReach", "Reach (UV)"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowReach, 0.0, 1.0, 0.1, 0.001)
		],
		SNew(SBox).IsEnabled_Lambda([bGravity, ActiveSource]()
			{ return !bGravity || ActiveSource() == static_cast<int64>(EMixtormatGeneratorFlowSource::SignedDistance); })
		[
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowFeather", "Feather"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowFeather, 0.0, 1.0, 0.5, 0.01)
		]));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox).IsEnabled(!bGravity)
		[
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAlong", "Offset Along"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowOffsetAlong, -1.0, 1.0, 0.0, 0.01)
		],
		SNew(SBox).IsEnabled(!bGravity)
		[
			MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowAcross", "Offset Across"), Flow,
				&FMixtormatLayerEffect::GeneratorFlowOffsetAcross, -1.0, 1.0, 0.0, 0.01)
		]));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowSeed", "Seed"), Flow,
			&FMixtormatLayerEffect::GeneratorFlowSeed, 0.0, 1024.0, 1),
		SNullWidget::NullWidget));

	if (Type == EMixtormatEffectType::ShapeDeform)
	{
		AddSliderRow(Panel, MixtormatRow::MakePair(
			SNew(SBox)
			.IsEnabled_Lambda([this, Flow, SourceAddress, IsHeightOnlyOwner]()
			{
				const FMixtormatLayerEffect* Effect = Flow();
				return Effect && !IsHeightOnlyOwner() && GetEffectiveEnumParameter(SourceAddress(), static_cast<int64>(Effect->GeneratorFlowSource))
					== static_cast<int64>(EMixtormatGeneratorFlowSource::SignedDistance);
			})
			[
				MakeMemberSlider<FMixtormatLayerEffect>(LOCTEXT("GeneratorFlowShapeOffset", "Shape Offset (UV)"), Flow,
					&FMixtormatLayerEffect::GeneratorFlowShapeOffset, -0.25, 0.25, 0.0, 0.001,
					LOCTEXT("GeneratorFlowShapeOffsetHint", "Signed boundary expansion or erosion. Requires Signed Distance; unavailable with Height steering, Noise or Cliff Strata."))
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
				&FMixtormatLayerEffect::GeneratorFlowSteps, 1.0, 64.0, 16,
				bGravity ? LOCTEXT("GravityFlowStepsHint", "RK2 trace steps. Near boundaries, deflected segments longer than 32 output texels stop rather than skip obstacles. Increase Steps for longer traces or higher resolutions.") : FText::GetEmpty())));
		if (Type == EMixtormatEffectType::GeneratorFlow || bGravity)
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
			? LOCTEXT("FlowCarveHeading", "FLOW CARVE") : bGravity
				? LOCTEXT("GravityFlowHeading", "GRAVITY FLOW") : LOCTEXT("GeneratorFlowHeading", "GENERATOR FLOW");
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
				// A source has no published outputs yet, so Copy is disabled; the tooltip says why
				// instead of repeating the layer-child promise the button cannot keep.
				.ToolTipText_Lambda([this, Label]()
				{
					return GetSelectedSource()
						? LOCTEXT("InspectorCopyChildOutputSourceHint",
							"Sources do not publish outputs yet, so there is nothing to copy.")
						: FText::Format(LOCTEXT("InspectorCopyChildOutputHint",
							"Copy the live {0} output reference; the producer stays in place."), Label);
				})
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
	const FMixtormatStrataCarver Defaults;

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

	{
		const TSharedRef<SVerticalBox> Bedding = AddCard(Panel, LOCTEXT("StrataBedding", "BEDDING"));
		AddSliderRow(Bedding, SliderInt(
			LOCTEXT("StrataSeed", "Seed"), &FMixtormatStrataCarver::Seed, 0.0, 9999.0, Defaults.Seed,
			LOCTEXT("StrataSeedHint", "Draws bed interfaces, hardness, folds and slab joints.")));
		AddSliderRow(Bedding, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataFrequency", "Bed Count"), &FMixtormatStrataCarver::StrataFrequency, 1.0, 64.0, Defaults.StrataFrequency, 1.0,
				LOCTEXT("StrataFrequencyHint", "Requested beds across the tile. Count and direction resolve together to a tileable lattice.")),
			Slider(LOCTEXT("StrataDepth", "Relief Depth"), &FMixtormatStrataCarver::Depth, 0.0, 1.0, Defaults.Depth, 0.001,
				LOCTEXT("StrataDepthHint", "Scales the signed geological relief before shared height normalization."))));
		AddSliderRow(Bedding, Slider(
			LOCTEXT("StrataRotation", "Direction"), &FMixtormatStrataCarver::StrataRotation, 0.0, 360.0, Defaults.StrataRotation, 0.1,
			LOCTEXT("StrataRotationHint", "Bedding orientation. Snaps to the nearest tileable lattice angle; 0 gives horizontal beds.")));
		AddSliderRow(Bedding, Slider(
			LOCTEXT("StrataThickness", "Thickness Variation"), &FMixtormatStrataCarver::ThicknessVariation, 0.0, 1.0, Defaults.ThicknessVariation, 0.01,
			LOCTEXT("StrataThicknessHint", "Varies bed thickness along the shelves. Interfaces stay ordered and never cross.")));
		AddSliderRow(Bedding, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataBend", "Fold Strength"), &FMixtormatStrataCarver::Bend, 0.0, 0.25, Defaults.Bend, 0.001,
				LOCTEXT("StrataBendHint", "Shared fold amplitude in tile widths. All beds follow the same fold.")),
			SliderInt(LOCTEXT("StrataBendScale", "Fold Scale"), &FMixtormatStrataCarver::BendScale, 1.0, 8.0, Defaults.BendScale,
				LOCTEXT("StrataBendScaleHint", "Integer fold frequency along the primitive bedding strike; also sets lateral thickness variation scale."))));
		AddSliderRow(Bedding, Slider(
			LOCTEXT("StrataHeightFollow", "Height Follow"), &FMixtormatStrataCarver::HeightFollow, 0.0, 16.0, Defaults.HeightFollow, 0.01,
			LOCTEXT("StrataHeightFollowHint", "Shifts bedding with the upstream composite height. 0 makes an independent geological field.")));
	}
	{
		const TSharedRef<SVerticalBox> Influence = AddCard(Panel, LOCTEXT("StrataInfluence", "MASK & ID INFLUENCE"));
		AddSliderRow(Influence, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataMaskInfluence", "Mask Influence"), &FMixtormatStrataCarver::MaskInfluence,
				0.0, 1.0, Defaults.MaskInfluence, 0.01,
				LOCTEXT("StrataMaskInfluenceHint", "How much the scoped mask varies the geological relief.")),
			Slider(LOCTEXT("StrataIDInfluence", "ID Influence"), &FMixtormatStrataCarver::IDInfluence,
				0.0, 1.0, Defaults.IDInfluence, 0.01,
				LOCTEXT("StrataIDInfluenceHint", "How much available Region IDs vary the relief per bed or region."))));
	}
	{
		const TSharedRef<SVerticalBox> Shelves = AddCard(Panel, LOCTEXT("StrataShelves", "SHELVES"));
		AddSliderRow(Shelves, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataLedgeWidth", "Ledge Width"), &FMixtormatStrataCarver::LedgeWidth, 0.0, 1.0, Defaults.LedgeWidth, 0.01,
				LOCTEXT("StrataLedgeWidthHint", "How much of each bed remains a broad planar shelf rather than an edge shoulder.")),
			Slider(LOCTEXT("StrataVerticality", "Edge Sharpness"), &FMixtormatStrataCarver::Verticality, 0.0, 1.0, Defaults.Verticality, 0.01,
				LOCTEXT("StrataVerticalityHint", "0 rounds shelf shoulders; 1 narrows them into sharper planar faces."))));
		AddSliderRow(Shelves, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataHardnessContrast", "Hardness Contrast"), &FMixtormatStrataCarver::HardnessContrast, 0.0, 1.0, Defaults.HardnessContrast, 0.01,
				LOCTEXT("StrataHardnessContrastHint", "Varies material hardness by bed. Hard beds stand proud with wider shelves; soft beds recede.")),
			Slider(LOCTEXT("StrataSoftRecession", "Soft Recession"), &FMixtormatStrataCarver::SoftRecession, 0.0, 1.0, Defaults.SoftRecession, 0.01,
				LOCTEXT("StrataSoftRecessionHint", "Deepens softer beds and shared seams without moving interfaces or changing bed IDs."))));
		AddSliderRow(Shelves, Slider(
			LOCTEXT("StrataHeightVariation", "Height Variation"), &FMixtormatStrataCarver::HeightVariation, 0.0, 1.0, Defaults.HeightVariation, 0.01,
			LOCTEXT("StrataHeightVariationHint", "Varies whole shelf elevations independently of hardness.")));
	}
	{
		const TSharedRef<SVerticalBox> Slabs = AddCard(Panel, LOCTEXT("StrataSlabs", "SLABS & LAMINAE"));
		AddSliderRow(Slabs, Slider(
			LOCTEXT("StrataBreakup", "Slab Breakup"), &FMixtormatStrataCarver::Breakup, 0.0, 1.0, Defaults.Breakup, 0.01,
			LOCTEXT("StrataBreakupHint", "Joint cuts, wedge-shaped rim chips and small slab offsets. 0 leaves continuous shelves.")));
		AddSliderRow(Slabs, MixtormatRow::MakePair(
			SliderInt(LOCTEXT("StrataJointScale", "Joint Count"), &FMixtormatStrataCarver::JointScale, 1.0, 16.0, Defaults.JointScale,
				LOCTEXT("StrataJointScaleHint", "Slabs per primitive strike repeat. Neighboring beds stagger their joints.")),
			Slider(LOCTEXT("StrataJointWidth", "Joint Width"), &FMixtormatStrataCarver::JointWidth, 0.0, 0.25, Defaults.JointWidth, 0.005,
				LOCTEXT("StrataJointWidthHint", "Width as a fraction of nominal slab spacing. 0 closes cuts while retaining slab relief."))));
		AddSliderRow(Slabs, MixtormatRow::MakePair(
			Slider(LOCTEXT("StrataLamination", "Lamination"), &FMixtormatStrataCarver::Lamination, 0.0, 1.0, Defaults.Lamination, 0.01,
				LOCTEXT("StrataLaminationHint", "Fine grooves inside beds, stronger in soft material. Subpixel laminae fade out.")),
			Slider(LOCTEXT("StrataCrossBedding", "Cross Bedding"), &FMixtormatStrataCarver::CrossBedding, 0.0, 3.0, Defaults.CrossBedding, 0.01,
				LOCTEXT("StrataCrossBeddingHint", "Per-bed integer tilt of the laminae along the strike; keeps the field tileable."))));
	}

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


TSharedRef<SWidget> SMixtormat::BuildBehaviorWarpSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->Type != EMixtormatLayerChildType::Behavior
		|| Selected->IsInstance()) { return Menu.Build(); }

	const int32 DestinationLayerIndex = WorkingLayers.IndexOfByPredicate(
		[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	if (!WorkingLayers.IsValidIndex(DestinationLayerIndex)) { return Menu.Build(); }
	const FMixtormatLayer& Destination = WorkingLayers[DestinationLayerIndex];
	const int32 BehaviorIndex = Destination.Children.IndexOfByPredicate(
		[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; });
	if (!Destination.Children.IsValidIndex(BehaviorIndex)) { return Menu.Build(); }
	const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(
		Destination.Children, BehaviorIndex);
	if (OwnerIndex == INDEX_NONE) { return Menu.Build(); }

	const auto Assign = [this, Address](const FMixtormatOutputReference* Source)
	{
		FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || Child->IsInstance()
			|| Child->Type != EMixtormatLayerChildType::Behavior) { return; }
		if (Source)
		{
			Child->Behavior.Direction.Published = *Source;
			Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::PublishedOutput;
		}
		else
		{
			Child->Behavior.Direction.Published = FMixtormatOutputReference{};
			Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::None;
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	};
	Menu.Item(LOCTEXT("BehaviorWarpChooseSourceLater", "Choose source later"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(nullptr); }));
	Menu.Item(LOCTEXT("BehaviorWarpOwnHeight", "Own Height Gradient"),
		MixtormatIcons::WarpStructural(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			if (FMixtormatLayerChild* Child = ResolveChildAt(Address))
			{
				if (Child->Type == EMixtormatLayerChildType::Behavior && !Child->IsInstance())
				{
					Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::OwnNativeHeight;
					Child->Behavior.Direction.Published = FMixtormatOutputReference{};
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}
		}));
	Menu.Separator();

	// All sources are explicit, typed and order-checked by the runtime resolver.
	// The generic Vector2 type is deliberately not accepted as transport.
	for (int32 SourceLayerIndex = 0;
		SourceLayerIndex <= DestinationLayerIndex; ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Producer);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField
					|| (Output.FieldKind != EMixtormatPublishedFieldKind::Flow
						&& Output.FieldKind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
				FMixtormatOutputReference Ref;
				Ref.SourceLayerId = SourceLayer.LayerId;
				Ref.SourceChildId = Producer.ChildId;
				Ref.OutputName = Output.Name;
				Ref.Kind = Output.FieldKind;
				const bool bAvailable = MixtormatOutputReferences::ResolveGeneratorInputSource(
					WorkingLayers, DestinationLayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				Menu.Item(FText::Format(LOCTEXT("BehaviorWarpSourceEntry", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label),
					MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([Assign, Ref]() { Assign(&Ref); }))
					.Enabled(bAvailable);
			}
		}
	}

	// Sources shelf producers are separately addressed and evaluated before layers.
	for (const FMixtormatSourceEntry& Shelf : WorkingSources)
	{
		if (Shelf.Child.Type != EMixtormatLayerChildType::Generator) { continue; }
		for (const EMixtormatPublishedFieldKind Kind : {
			EMixtormatPublishedFieldKind::Flow, EMixtormatPublishedFieldKind::UVMap})
		{
			FMixtormatOutputReference Ref;
			Ref.OwnerKind = EMixtormatOutputReferenceOwnerKind::Shelf;
			Ref.SourceShelfId = Shelf.SourceId;
			Ref.SourceChildId = Shelf.Child.ChildId;
			Ref.Kind = Kind;
			Ref.OutputName = Kind == EMixtormatPublishedFieldKind::Flow
				? FName(TEXT("FlowDirection")) : FName(TEXT("WarpedUV"));
			const auto Status = MixtormatOutputReferences::ClassifyShelfSourceReference(
				WorkingSources, Ref);
			if (Status.Issue != MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
			{
				continue;
			}
			Menu.Item(FText::Format(LOCTEXT("BehaviorWarpShelfSource", "Sources / {0} / {1}"),
				Shelf.DisplayName, FText::FromName(Ref.OutputName)),
				MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([Assign, Ref]() { Assign(&Ref); }));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorWarpInfluenceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->IsInstance()
		|| Selected->Type != EMixtormatLayerChildType::Behavior) { return Menu.Build(); }
	const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
		[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	if (!WorkingLayers.IsValidIndex(LayerIndex)) { return Menu.Build(); }
	const FMixtormatLayer& Dest = WorkingLayers[LayerIndex];
	const int32 ChildIndex = Dest.Children.IndexOfByPredicate(
		[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; });
	const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(Dest.Children, ChildIndex);
	if (!Dest.Children.IsValidIndex(ChildIndex) || OwnerIndex == INDEX_NONE)
	{
		return Menu.Build();
	}
	const auto Assign = [this, Address](const FMixtormatOutputReference* Reference)
	{
		if (FMixtormatLayerChild* Child = ResolveChildAt(Address))
		{
			if (Child->Type != EMixtormatLayerChildType::Behavior || Child->IsInstance()) { return; }
			Child->Behavior.Influence.Published = Reference ? *Reference : FMixtormatOutputReference{};
			Child->Behavior.Influence.Origin = Reference
				? EMixtormatBehaviorFieldOrigin::PublishedOutput : EMixtormatBehaviorFieldOrigin::None;
			RefreshLayeredPreview();
			RebuildLayerList();
		}
	};
	Menu.Item(LOCTEXT("BehaviorWarpInfluenceNone", "No additional influence"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(nullptr); }));
	Menu.Separator();

	// Scalar01 is a typed field, not a signed Noise Value automatically converted
	// into coverage. Shelf Scalar01 is not supported by the current source contract.
	for (int32 SourceLayerIndex = 0; SourceLayerIndex <= LayerIndex; ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Producer);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField
					|| Output.FieldKind != EMixtormatPublishedFieldKind::Scalar01) { continue; }
				FMixtormatOutputReference Ref;
				Ref.SourceLayerId = SourceLayer.LayerId;
				Ref.SourceChildId = Producer.ChildId;
				Ref.OutputName = Output.Name;
				Ref.Kind = Output.FieldKind;
				const bool bValid = MixtormatOutputReferences::ResolveSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				Menu.Item(FText::Format(LOCTEXT("BehaviorWarpInfluenceChoice", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label),
					MixtormatIcons::Mask(),
					FSimpleDelegate::CreateLambda([Assign, Ref]() { Assign(&Ref); }))
					.Enabled(bValid);
			}
		}
	}
	return Menu.Build();
}


TSharedRef<SWidget> SMixtormat::BuildBehaviorPushSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->IsInstance()
		|| Selected->Type != EMixtormatLayerChildType::Behavior
		|| Selected->Behavior.Type != EMixtormatBehaviorType::Push) { return Menu.Build(); }
	const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
		[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	if (!WorkingLayers.IsValidIndex(LayerIndex)) { return Menu.Build(); }
	const FMixtormatLayer& Destination = WorkingLayers[LayerIndex];
	const int32 ChildIndex = Destination.Children.IndexOfByPredicate(
		[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; });
	const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(Destination.Children, ChildIndex);
	if (OwnerIndex == INDEX_NONE) { return Menu.Build(); }
	const auto Assign = [this, Address](EMixtormatBehaviorFieldOrigin Origin, const FMixtormatOutputReference* Reference)
	{
		FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || Child->IsInstance() || Child->Type != EMixtormatLayerChildType::Behavior
			|| Child->Behavior.Type != EMixtormatBehaviorType::Push) { return; }
		Child->Behavior.Height.Origin = Origin;
		Child->Behavior.Height.Published = Reference ? *Reference : FMixtormatOutputReference{};
		RefreshLayeredPreview();
		RebuildLayerList();
	};
	Menu.Item(LOCTEXT("BehaviorPushChooseLater", "Choose source later"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(EMixtormatBehaviorFieldOrigin::None, nullptr); }));
	Menu.Item(LOCTEXT("BehaviorPushOwnNative", "Own Native Height"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([Assign]() { Assign(EMixtormatBehaviorFieldOrigin::OwnNativeHeight, nullptr); }));
	Menu.Item(LOCTEXT("BehaviorPushPrevious", "Previous Running Height"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([Assign]() { Assign(EMixtormatBehaviorFieldOrigin::PreviousRunningHeight, nullptr); }));
	Menu.Separator();
	for (int32 SourceLayerIndex = 0; SourceLayerIndex <= LayerIndex; ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Producer);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || Output.FieldKind != EMixtormatPublishedFieldKind::ScalarSigned) { continue; }
				FMixtormatOutputReference Ref;
				Ref.SourceLayerId = SourceLayer.LayerId;
				Ref.SourceChildId = Producer.ChildId;
				Ref.OutputName = Output.Name;
				Ref.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
				const bool bValid = MixtormatOutputReferences::ResolveGeneratorInputSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE
					|| MixtormatOutputReferences::ResolveSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				Menu.Item(FText::Format(LOCTEXT("BehaviorPushChoice", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label),
					MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([Assign, Ref]()
					{
						Assign(EMixtormatBehaviorFieldOrigin::PublishedOutput, &Ref);
					})).Enabled(bValid);
			}
		}
	}
	return Menu.Build();
}

// The shared composition block behind Warp, Deform, Push and Carve. Each caller
// passes the socket it owns; whether that socket may carry a reverse or a blend
// is read back from the runtime's own effective-kind rule rather than restated
// per panel, so a kind that gains or loses reverse support cannot leave one
// operation offering a control the gather would reject.
void SMixtormat::AddBehaviorFieldCompositionRows(
	const TSharedRef<SVerticalBox>& TargetPanel,
	TFunction<FMixtormatBehaviorFieldInput*()> ResolveField,
	const bool bAllowBlend)
{
	const auto Kind = [ResolveField]()
	{
		const FMixtormatBehaviorFieldInput* Field = ResolveField();
		return Field ? MixtormatBehaviorFieldEffectiveKind(*Field) : EMixtormatBehaviorFieldKind::None;
	};
	const auto Connected = [ResolveField, Kind]()
	{
		const FMixtormatBehaviorFieldInput* Field = ResolveField();
		return Field && Field->Origin != EMixtormatBehaviorFieldOrigin::None;
	};
	// TAttribute<bool> has no constructor from a functor in UE, so a raw lambda
	// compiles into a "cannot convert OtherType to bool" error. CreateLambda is
	// the idiom everywhere else in this file.
	const TAttribute<bool> bVisible = TAttribute<bool>::CreateLambda(Connected);
	const TAttribute<bool> bCanReverse = TAttribute<bool>::CreateLambda([Kind]()
	{
		return FMixtormatBehaviorFieldInput::KindSupportsReverse(Kind());
	});
	AddSliderRow(TargetPanel,
		SNew(SBox).Visibility_Lambda([bVisible]() { return bVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			MakeMemberSlider<FMixtormatBehaviorFieldInput>(
				LOCTEXT("BehaviorFieldAmplitude", "Field Amplitude"), ResolveField,
				&FMixtormatBehaviorFieldInput::Amplitude, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("BehaviorFieldAmplitudeHint", "Scales this field's own weight after the Behavior Strength, so one Behavior can weight its fields independently. Zero neutralizes this field alone."))
		]);
	// A kind with no sign has no reverse. The control stays visible and disabled
	// rather than vanishing, so the socket's full composition reads as one block.
	AddSliderRow(TargetPanel,
		SNew(SBox).Visibility_Lambda([bVisible]() { return bVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBox).IsEnabled_Lambda([bCanReverse]() { return bCanReverse.Get(); })
			[
				MixtormatRow::MakeTrailing(
					LOCTEXT("BehaviorFieldReverse", "Reverse"),
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([ResolveField]()
						{
							const FMixtormatBehaviorFieldInput* Field = ResolveField();
							return Field && Field->bReversed ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this, ResolveField](const ECheckBoxState State)
						{
							if (FMixtormatBehaviorFieldInput* Field = ResolveField())
							{
								Field->bReversed = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("BehaviorFieldReverseHint", "Flips this field's sign. On a direction field it transports the opposite way; on a Carve distance field it exchanges which side of the boundary is carved and deposited.")),
					LOCTEXT("BehaviorFieldReverseHint", "Flips this field's sign."))
			]
		]);
	if (!bAllowBlend) { return; }
	AddSliderRow(TargetPanel,
		SNew(SBox).Visibility_Lambda([bVisible]() { return bVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			MixtormatRow::MakeDropdown(
				LOCTEXT("BehaviorFieldBlend", "Combine"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([ResolveField, Kind]()
					{
						const FMixtormatBehaviorFieldInput* Field = ResolveField();
						const EMixtormatBehaviorFieldKind Effective = Kind();
						// Show the effective combine rather than a stored value the
						// runtime would refuse: a blend on a kind that cannot fold is
						// reported as the operation default it will actually run.
						if (!Field || !FMixtormatBehaviorFieldInput::KindSupportsBlend(Effective))
						{
							return LOCTEXT("BehaviorFieldBlendDefault", "Operation Default");
						}
						switch (Field->Blend)
						{
						case EMixtormatBehaviorFieldBlend::Add: return LOCTEXT("BehaviorFieldBlendAdd", "Add");
						case EMixtormatBehaviorFieldBlend::Multiply: return LOCTEXT("BehaviorFieldBlendMultiply", "Multiply");
						case EMixtormatBehaviorFieldBlend::Min: return LOCTEXT("BehaviorFieldBlendMin", "Minimum");
						case EMixtormatBehaviorFieldBlend::Max: return LOCTEXT("BehaviorFieldBlendMax", "Maximum");
						default: return LOCTEXT("BehaviorFieldBlendDefault", "Operation Default");
						}
					}),
					FOnGetContent::CreateLambda([this, ResolveField, Kind]() -> TSharedRef<SWidget>
					{
						MixtormatMenu::FBuilder Menu;
						const TAttribute<bool> bUsable = TAttribute<bool>::CreateLambda([Kind]()
						{
							return FMixtormatBehaviorFieldInput::KindSupportsBlend(Kind());
						});
						const auto Choose = [this, ResolveField](const EMixtormatBehaviorFieldBlend Blend)
						{
							if (FMixtormatBehaviorFieldInput* Field = ResolveField())
							{
								Field->Blend = Blend;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						};
						Menu.Item(LOCTEXT("BehaviorFieldBlendOperation", "Operation Default"), nullptr,
							FSimpleDelegate::CreateLambda([Choose]() { Choose(EMixtormatBehaviorFieldBlend::Operation); }));
						Menu.Item(LOCTEXT("BehaviorFieldBlendAddItem", "Add"), nullptr,
							FSimpleDelegate::CreateLambda([Choose]() { Choose(EMixtormatBehaviorFieldBlend::Add); }))
							.Enabled(bUsable);
						Menu.Item(LOCTEXT("BehaviorFieldBlendMultiplyItem", "Multiply"), nullptr,
							FSimpleDelegate::CreateLambda([Choose]() { Choose(EMixtormatBehaviorFieldBlend::Multiply); }))
							.Enabled(bUsable);
						Menu.Item(LOCTEXT("BehaviorFieldBlendMinItem", "Minimum"), nullptr,
							FSimpleDelegate::CreateLambda([Choose]() { Choose(EMixtormatBehaviorFieldBlend::Min); }))
							.Enabled(bUsable);
						Menu.Item(LOCTEXT("BehaviorFieldBlendMaxItem", "Maximum"), nullptr,
							FSimpleDelegate::CreateLambda([Choose]() { Choose(EMixtormatBehaviorFieldBlend::Max); }))
							.Enabled(bUsable);
						return Menu.Build();
					}),
					nullptr,
					LOCTEXT("BehaviorFieldBlendHint", "How this signed height field folds into the current native height. Operation Default keeps Push additive; the others are Push-only because a coordinate field and a distance field have no base to fold into.")),
				LOCTEXT("BehaviorFieldBlendHint", "Signed-scalar combine for this field."))
		]);
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorPushControls()
{
	const auto Push = [this]() { return GetSelectedBehaviorPush(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorPushHeight", "Signed Height Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this, Push]()
		{
			const FMixtormatBehavior* B = Push();
			if (!B) { return LOCTEXT("BehaviorPushUnavailable", "Unavailable"); }
			switch (B->Height.Origin)
			{
			case EMixtormatBehaviorFieldOrigin::OwnNativeHeight:
				return LOCTEXT("BehaviorPushOwnLabel", "Own Native Height");
			case EMixtormatBehaviorFieldOrigin::PreviousRunningHeight:
				return LOCTEXT("BehaviorPushPreviousLabel", "Previous Running Height");
			case EMixtormatBehaviorFieldOrigin::PublishedOutput:
			{
				const FMixtormatOutputReference& Ref = B->Height.Published;
				const FMixtormatChildAddress Address = GetSelectedChildAddress();
				const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
					[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
				const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
						[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
					: INDEX_NONE;
				const int32 OwnerIndex = ChildIndex != INDEX_NONE
					? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
						WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
				const bool bAvailable = Ref.HasSource()
					&& Ref.Kind == EMixtormatPublishedFieldKind::ScalarSigned
					&& OwnerIndex != INDEX_NONE
					&& (Ref.IsShelfSource()
						? MixtormatOutputReferences::ClassifyShelfSourceReference(WorkingSources, Ref).Issue
							== MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated
						: (MixtormatOutputReferences::ResolveGeneratorInputSource(
							WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE
							|| MixtormatOutputReferences::ResolveSource(
								WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE));
				return bAvailable ? FText::FromName(Ref.OutputName)
					: LOCTEXT("BehaviorPushMissingSource", "Source unavailable");
			}
			default: return LOCTEXT("BehaviorPushUnset", "Choose source");
			}
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorPushSourceMenu)),
		LOCTEXT("BehaviorPushHeightHint", "Explicit signed height input; no automatic SDF conversion.")));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorPushInfluence", "Influence Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([Push]()
		{
			const FMixtormatBehavior* B = Push();
			return B && B->Influence.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				? FText::FromName(B->Influence.Published.OutputName)
				: LOCTEXT("BehaviorPushNone", "None");
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpInfluenceMenu)),
		LOCTEXT("BehaviorPushInfluenceHint", "Optional Scalar01 influence, multiplied by scoped masks.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorPushStrength", "Strength"), Push, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorPushStrengthHint", "Signed native-height contribution.")));
	AddBehaviorFieldCompositionRows(Panel,
		[Push]() -> FMixtormatBehaviorFieldInput* { return Push() ? &Push()->Height : nullptr; }, true);
	return SNew(SVerticalBox)
		.Visibility_Lambda([Push]() { return Push() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child && !Child->IsInstance();
			})
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("BehaviorPushTitle", "PUSH"))
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Push]()
					{
						const FMixtormatBehavior* B = Push();
						return B && B->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Push](ECheckBoxState State)
					{
						if (FMixtormatBehavior* B = Push())
						{
							B->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					})))
				[Panel]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorCarveSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->IsInstance()
		|| Selected->Type != EMixtormatLayerChildType::Behavior
		|| Selected->Behavior.Type != EMixtormatBehaviorType::Carve) { return Menu.Build(); }
	const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
		[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	if (!WorkingLayers.IsValidIndex(LayerIndex)) { return Menu.Build(); }
	const FMixtormatLayer& Destination = WorkingLayers[LayerIndex];
	const int32 ChildIndex = Destination.Children.IndexOfByPredicate(
		[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; });
	const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(Destination.Children, ChildIndex);
	if (OwnerIndex == INDEX_NONE) { return Menu.Build(); }
	const auto Assign = [this, Address](EMixtormatBehaviorFieldOrigin Origin, const FMixtormatOutputReference* Reference)
	{
		FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || Child->IsInstance() || Child->Type != EMixtormatLayerChildType::Behavior
			|| Child->Behavior.Type != EMixtormatBehaviorType::Carve) { return; }
		Child->Behavior.Height.Origin = Origin;
		Child->Behavior.Height.Published = Reference ? *Reference : FMixtormatOutputReference{};
		RefreshLayeredPreview();
		RebuildLayerList();
	};
	Menu.Item(LOCTEXT("BehaviorCarveChooseLater", "Choose source later"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(EMixtormatBehaviorFieldOrigin::None, nullptr); }));
	Menu.Item(LOCTEXT("BehaviorCarveOwnBoundary", "Own Boundary"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([Assign]() { Assign(EMixtormatBehaviorFieldOrigin::OwnBoundary, nullptr); }))
		.Enabled(MixtormatGeneratorHasFlowBoundary(Destination.Children[OwnerIndex].Generator.Type));
	Menu.Separator();
	for (int32 SourceLayerIndex = 0; SourceLayerIndex <= LayerIndex; ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Producer);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || Output.FieldKind != EMixtormatPublishedFieldKind::SDF) { continue; }
				FMixtormatOutputReference Ref;
				Ref.SourceLayerId = SourceLayer.LayerId;
				Ref.SourceChildId = Producer.ChildId;
				Ref.OutputName = Output.Name;
				Ref.Kind = EMixtormatPublishedFieldKind::SDF;
				const bool bValid = MixtormatOutputReferences::ResolveSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				Menu.Item(FText::Format(LOCTEXT("BehaviorCarveChoice", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label),
					MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([Assign, Ref]()
					{
						Assign(EMixtormatBehaviorFieldOrigin::PublishedOutput, &Ref);
					})).Enabled(bValid);
			}
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorCarveControls()
{
	const auto Carve = [this]() { return GetSelectedBehaviorCarve(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorCarveHeight", "Signed Boundary / SDF"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this, Carve]()
		{
			const FMixtormatBehavior* B = Carve();
			if (!B) { return LOCTEXT("BehaviorCarveUnavailable", "Unavailable"); }
			switch (B->Height.Origin)
			{
			case EMixtormatBehaviorFieldOrigin::OwnBoundary:
				return LOCTEXT("BehaviorCarveOwnLabel", "Own Boundary");
			case EMixtormatBehaviorFieldOrigin::PublishedOutput:
			{
				const FMixtormatOutputReference& Ref = B->Height.Published;
				const FMixtormatChildAddress Address = GetSelectedChildAddress();
				const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
					[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
				const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
						[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
					: INDEX_NONE;
				const int32 OwnerIndex = ChildIndex != INDEX_NONE
					? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
						WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
				const bool bAvailable = Ref.HasSource()
					&& !Ref.IsShelfSource()
					&& Ref.Kind == EMixtormatPublishedFieldKind::SDF
					&& ChildIndex != INDEX_NONE
					&& OwnerIndex != INDEX_NONE
					&& MixtormatOutputReferences::ResolveSource(
						WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				return bAvailable ? FText::FromName(Ref.OutputName)
					: LOCTEXT("BehaviorCarveMissingSource", "Source unavailable");
			}
			default: return LOCTEXT("BehaviorCarveUnset", "Choose source");
			}
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorCarveSourceMenu)),
		LOCTEXT("BehaviorCarveHeightHint", "Requires a typed signed distance field or an owning generator boundary.")));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorCarveInfluence", "Influence Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([Carve]()
		{
			const FMixtormatBehavior* B = Carve();
			return B && B->Influence.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				? FText::FromName(B->Influence.Published.OutputName)
				: LOCTEXT("BehaviorCarveNone", "None");
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpInfluenceMenu)),
		LOCTEXT("BehaviorCarveInfluenceHint", "Optional Scalar01 influence, multiplied by scoped masks.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorCarveStrength", "Strength"), Carve, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorCarveStrengthHint", "Positive removes height; negative deposits.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorCarveWidth", "Boundary Width"), Carve, &FMixtormatBehavior::CarveWidth,
		0.001, 0.25, 0.02, 0.001,
		LOCTEXT("BehaviorCarveWidthHint", "Signed-distance transition width in UV units.")));
	AddBehaviorFieldCompositionRows(Panel,
		[Carve]() -> FMixtormatBehaviorFieldInput* { return Carve() ? &Carve()->Height : nullptr; }, false);
	return SNew(SVerticalBox)
		.Visibility_Lambda([Carve]() { return Carve() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child && !Child->IsInstance();
			})
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("BehaviorCarveTitle", "CARVE / DEPOSIT"))
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Carve]()
					{
						const FMixtormatBehavior* B = Carve();
						return B && B->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Carve](ECheckBoxState State)
					{
						if (FMixtormatBehavior* B = Carve())
						{
							B->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					})))
				[Panel]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorWarpControls()
{
	const auto Warp = [this]() { return GetSelectedBehaviorWarp(); };
	const auto Reference = [Warp]() -> FMixtormatOutputReference*
	{
		FMixtormatBehavior* Selected = Warp();
		return Selected ? &Selected->Direction.Published : nullptr;
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorWarpSourceLabel", "Direction Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatBehavior* Selected = GetSelectedBehaviorWarp();
			if (Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight)
			{
				return LOCTEXT("BehaviorWarpOwnHeightSelected", "Own Height Gradient");
			}
			if (!Selected || Selected->Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
				|| !Selected->Direction.Published.HasSource())
			{
				return LOCTEXT("BehaviorWarpUnset", "Choose source");
			}
			const FMixtormatOutputReference& Ref = Selected->Direction.Published;
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
				[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
			const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
				? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
					[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
				: INDEX_NONE;
			const int32 OwnerIndex = ChildIndex != INDEX_NONE
				? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
					WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
			const bool bAvailable = OwnerIndex != INDEX_NONE && (Ref.IsShelfSource()
				? MixtormatOutputReferences::ClassifyShelfSourceReference(WorkingSources, Ref).Issue
					== MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated
				: MixtormatOutputReferences::ResolveGeneratorInputSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE);
			if (!bAvailable)
			{
				return FText::Format(LOCTEXT("BehaviorWarpUnavailableSource", "Unavailable / {0}"),
					FText::FromName(Ref.OutputName));
			}
			return FText::Format(LOCTEXT("BehaviorWarpSourceChip", "{0} / {1}"),
				Ref.IsShelfSource() ? LOCTEXT("BehaviorWarpShelfChip", "Sources")
					: LOCTEXT("BehaviorWarpLayerChip", "Layer"),
				FText::FromName(Ref.OutputName));
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpSourceMenu)),
		LOCTEXT("BehaviorWarpDirectionHint", "Completed Flow or lifted UV Map from an earlier source. Choose source later leaves Warp neutral.")));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorWarpStageLabel", "Execution Stage"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([Warp]()
		{
			const FMixtormatBehavior* B = Warp();
			return B && B->Stage == EMixtormatBehaviorStage::PreGeneration
				? LOCTEXT("BehaviorWarpPreStage", "Before Generation")
				: LOCTEXT("BehaviorWarpPostStage", "After Generation");
		}), FOnGetContent::CreateLambda([this]() -> TSharedRef<SWidget>
		{
			MixtormatMenu::FBuilder Menu;
			const auto SetStage = [this](EMixtormatBehaviorStage Stage)
			{
				FMixtormatBehavior* B = GetSelectedBehaviorWarp();
				if (!B || (Stage == EMixtormatBehaviorStage::PreGeneration
					&& B->Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput)) { return; }
				B->Stage = Stage;
				RefreshLayeredPreview();
				RebuildLayerList();
			};
			Menu.Item(LOCTEXT("BehaviorWarpStagePostChoice", "After Generation"), nullptr,
				FSimpleDelegate::CreateLambda([SetStage]() { SetStage(EMixtormatBehaviorStage::PostGeneration); }));
			Menu.Item(LOCTEXT("BehaviorWarpStagePreChoice", "Before Generation"), nullptr,
				FSimpleDelegate::CreateLambda([SetStage]() { SetStage(EMixtormatBehaviorStage::PreGeneration); }))
				.Enabled(GetSelectedBehaviorWarp()
					&& GetSelectedBehaviorWarp()->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput);
			return Menu.Build();
		})),
		LOCTEXT("BehaviorWarpStageHint", "Before Generation warps native sampling and generated IDs; Own Height Gradient requires After Generation.")));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorWarpInfluenceLabel", "Influence Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatBehavior* Warp = GetSelectedBehaviorWarp();
			if (!Warp || Warp->Influence.Origin == EMixtormatBehaviorFieldOrigin::None)
			{
				return LOCTEXT("BehaviorWarpInfluenceUnset", "None");
			}
			const FMixtormatOutputReference& Ref = Warp->Influence.Published;
			if (!Ref.HasSource()) { return LOCTEXT("BehaviorWarpInfluenceMissing", "Source missing"); }
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
				[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
			const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
				? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
					[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
				: INDEX_NONE;
			const int32 OwnerIndex = ChildIndex != INDEX_NONE
				? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
					WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
			const bool bValid = Ref.Kind == EMixtormatPublishedFieldKind::Scalar01
				&& !Ref.IsShelfSource() && OwnerIndex != INDEX_NONE
				&& MixtormatOutputReferences::ResolveSource(WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
			return bValid ? FText::FromName(Ref.OutputName)
				: FText::Format(LOCTEXT("BehaviorWarpInfluenceUnavailable", "Unavailable / {0}"),
					FText::FromName(Ref.OutputName));
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpInfluenceMenu)),
		LOCTEXT("BehaviorWarpInfluenceHint", "Optional Scalar 0..1 field. Multiplies the Warp displacement and any scoped mask; missing fields disable this Warp.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorWarpStrength", "Strength"), Warp, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorWarpStrengthHint", "Signed strength of the displacement. Zero is neutral; negative reverses displacement.")));
	AddBehaviorFieldCompositionRows(Panel,
		[Warp]() -> FMixtormatBehaviorFieldInput* { return Warp() ? &Warp()->Direction : nullptr; }, false);
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([Warp]()
		{
			const FMixtormatBehavior* Selected = Warp();
			return Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MakeMemberSlider<FMixtormatBehavior>(
				LOCTEXT("BehaviorWarpGradientReach", "Gradient Reach (UV)"),
				Warp, &FMixtormatBehavior::GradientReach, 0.0, 0.25, 0.02, 0.001,
				LOCTEXT("BehaviorWarpGradientReachHint", "Maximum UV displacement from the current native-height gradient. Stable across resolutions; zero is neutral."))
		]);
	TSharedRef<SVerticalBox> FlowPanel = SNew(SVerticalBox);
	AddSliderRow(FlowPanel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatOutputReference>(LOCTEXT("BehaviorWarpFlowAmount", "Flow Amount"),
			Reference, &FMixtormatOutputReference::FlowAmount, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("BehaviorWarpFlowAmountHint", "Multiplies Warp Strength during Flow trace.")),
		MakeMemberSlider<FMixtormatOutputReference>(LOCTEXT("BehaviorWarpTraceLength", "Trace Length (UV)"),
			Reference, &FMixtormatOutputReference::FlowTraceLength, 0.0, 1.0, 0.05, 0.001,
			LOCTEXT("BehaviorWarpTraceHint", "Length of the Flow integration path."))));
	AddSliderRow(FlowPanel, MakeMemberSliderInt<FMixtormatOutputReference>(
		LOCTEXT("BehaviorWarpSteps", "Flow Steps"), Reference, &FMixtormatOutputReference::FlowSteps,
		1.0, 64.0, 16,
		LOCTEXT("BehaviorWarpStepsHint", "Integration steps; UV Maps use their coordinates directly.")));
	Panel->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility_Lambda([Warp, Reference]()
		{
			const FMixtormatBehavior* Selected = Warp();
			const FMixtormatOutputReference* Ref = Reference();
			return Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				&& Ref && Ref->Kind == EMixtormatPublishedFieldKind::Flow
				? EVisibility::Visible : EVisibility::Collapsed;
		})[FlowPanel]
	];
	return SNew(SVerticalBox)
		.Visibility_Lambda([Warp]() { return Warp() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child && !Child->IsInstance();
			})
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("BehaviorWarpHeading", "WARP"))
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Warp]()
					{
						const FMixtormatBehavior* Selected = Warp();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Warp](const ECheckBoxState State)
					{
						if (FMixtormatBehavior* Selected = Warp())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					})))
				[Panel]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorDeformSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Selected = ResolveChildAt(Address);
	if (!Selected || Selected->Type != EMixtormatLayerChildType::Behavior
		|| Selected->IsInstance()) { return Menu.Build(); }

	const int32 DestinationLayerIndex = WorkingLayers.IndexOfByPredicate(
		[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	if (!WorkingLayers.IsValidIndex(DestinationLayerIndex)) { return Menu.Build(); }
	const FMixtormatLayer& Destination = WorkingLayers[DestinationLayerIndex];
	const int32 BehaviorIndex = Destination.Children.IndexOfByPredicate(
		[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; });
	if (!Destination.Children.IsValidIndex(BehaviorIndex)) { return Menu.Build(); }
	const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(
		Destination.Children, BehaviorIndex);
	if (OwnerIndex == INDEX_NONE) { return Menu.Build(); }

	const auto Assign = [this, Address](const FMixtormatOutputReference* Source)
	{
		FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || Child->IsInstance()
			|| Child->Type != EMixtormatLayerChildType::Behavior) { return; }
		if (Source)
		{
			Child->Behavior.Direction.Published = *Source;
			Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::PublishedOutput;
		}
		else
		{
			Child->Behavior.Direction.Published = FMixtormatOutputReference{};
			Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::None;
		}
		RefreshLayeredPreview();
		RebuildLayerList();
	};
	Menu.Item(LOCTEXT("BehaviorDeformChooseSourceLater", "Choose source later"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(nullptr); }));
	Menu.Item(LOCTEXT("BehaviorDeformOwnHeight", "Own Height Gradient"),
		MixtormatIcons::WarpStructural(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			if (FMixtormatLayerChild* Child = ResolveChildAt(Address))
			{
				if (Child->Type == EMixtormatLayerChildType::Behavior && !Child->IsInstance())
				{
					Child->Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::OwnNativeHeight;
					Child->Behavior.Direction.Published = FMixtormatOutputReference{};
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}
		}));
	Menu.Separator();

	// All sources are explicit, typed and order-checked by the runtime resolver.
	// The generic Vector2 type is deliberately not accepted as transport.
	for (int32 SourceLayerIndex = 0;
		SourceLayerIndex <= DestinationLayerIndex; ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Producer : SourceLayer.Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Producer);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField
					|| (Output.FieldKind != EMixtormatPublishedFieldKind::Flow
						&& Output.FieldKind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
				FMixtormatOutputReference Ref;
				Ref.SourceLayerId = SourceLayer.LayerId;
				Ref.SourceChildId = Producer.ChildId;
				Ref.OutputName = Output.Name;
				Ref.Kind = Output.FieldKind;
				const bool bAvailable = MixtormatOutputReferences::ResolveGeneratorInputSource(
					WorkingLayers, DestinationLayerIndex, OwnerIndex, Ref) != INDEX_NONE;
				Menu.Item(FText::Format(LOCTEXT("BehaviorDeformSourceEntry", "{0} / {1} / {2}"),
					SourceLayer.DisplayName, GetLayerChildName(Producer), Output.Label),
					MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([Assign, Ref]() { Assign(&Ref); }))
					.Enabled(bAvailable);
			}
		}
	}

	// Sources shelf producers are separately addressed and evaluated before layers.
	for (const FMixtormatSourceEntry& Shelf : WorkingSources)
	{
		if (Shelf.Child.Type != EMixtormatLayerChildType::Generator) { continue; }
		for (const EMixtormatPublishedFieldKind Kind : {
			EMixtormatPublishedFieldKind::Flow, EMixtormatPublishedFieldKind::UVMap})
		{
			FMixtormatOutputReference Ref;
			Ref.OwnerKind = EMixtormatOutputReferenceOwnerKind::Shelf;
			Ref.SourceShelfId = Shelf.SourceId;
			Ref.SourceChildId = Shelf.Child.ChildId;
			Ref.Kind = Kind;
			Ref.OutputName = Kind == EMixtormatPublishedFieldKind::Flow
				? FName(TEXT("FlowDirection")) : FName(TEXT("WarpedUV"));
			const auto Status = MixtormatOutputReferences::ClassifyShelfSourceReference(
				WorkingSources, Ref);
			if (Status.Issue != MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
			{
				continue;
			}
			Menu.Item(FText::Format(LOCTEXT("BehaviorDeformShelfSource", "Sources / {0} / {1}"),
				Shelf.DisplayName, FText::FromName(Ref.OutputName)),
				MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([Assign, Ref]() { Assign(&Ref); }));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildBehaviorDeformControls()
{
	const auto Deform = [this]() { return GetSelectedBehaviorDeform(); };
	const auto Reference = [Deform]() -> FMixtormatOutputReference*
	{
		FMixtormatBehavior* Selected = Deform();
		return Selected ? &Selected->Direction.Published : nullptr;
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorDeformSourceLabel", "Direction Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatBehavior* Selected = GetSelectedBehaviorDeform();
			if (Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight)
			{
				return LOCTEXT("BehaviorDeformOwnHeightSelected", "Own Height Gradient");
			}
			if (!Selected || Selected->Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
				|| !Selected->Direction.Published.HasSource())
			{
				return LOCTEXT("BehaviorDeformUnset", "Choose source");
			}
			const FMixtormatOutputReference& Ref = Selected->Direction.Published;
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
				[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
			const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
				? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
					[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
				: INDEX_NONE;
			const int32 OwnerIndex = ChildIndex != INDEX_NONE
				? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
					WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
			const bool bAvailable = OwnerIndex != INDEX_NONE && (Ref.IsShelfSource()
				? MixtormatOutputReferences::ClassifyShelfSourceReference(WorkingSources, Ref).Issue
					== MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated
				: MixtormatOutputReferences::ResolveGeneratorInputSource(
					WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE);
			if (!bAvailable)
			{
				return FText::Format(LOCTEXT("BehaviorDeformUnavailableSource", "Unavailable / {0}"),
					FText::FromName(Ref.OutputName));
			}
			return FText::Format(LOCTEXT("BehaviorDeformSourceChip", "{0} / {1}"),
				Ref.IsShelfSource() ? LOCTEXT("BehaviorDeformShelfChip", "Sources")
					: LOCTEXT("BehaviorDeformLayerChip", "Layer"),
				FText::FromName(Ref.OutputName));
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorDeformSourceMenu)),
		LOCTEXT("BehaviorDeformDirectionHint", "Completed Flow or lifted UV Map from an earlier source. Deform resamples native height only, leaving IDs and coverage fixed.")));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("BehaviorDeformInfluenceLabel", "Influence Field"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatBehavior* Deform = GetSelectedBehaviorDeform();
			if (!Deform || Deform->Influence.Origin == EMixtormatBehaviorFieldOrigin::None)
			{
				return LOCTEXT("BehaviorDeformInfluenceUnset", "None");
			}
			const FMixtormatOutputReference& Ref = Deform->Influence.Published;
			if (!Ref.HasSource()) { return LOCTEXT("BehaviorDeformInfluenceMissing", "Source missing"); }
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
				[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
			const int32 ChildIndex = WorkingLayers.IsValidIndex(LayerIndex)
				? WorkingLayers[LayerIndex].Children.IndexOfByPredicate(
					[&Address](const FMixtormatLayerChild& Child) { return Child.ChildId == Address.ChildId; })
				: INDEX_NONE;
			const int32 OwnerIndex = ChildIndex != INDEX_NONE
				? MixtormatChildScope::ResolveBehaviorGeneratorIndex(
					WorkingLayers[LayerIndex].Children, ChildIndex) : INDEX_NONE;
			const bool bValid = Ref.Kind == EMixtormatPublishedFieldKind::Scalar01
				&& !Ref.IsShelfSource() && OwnerIndex != INDEX_NONE
				&& MixtormatOutputReferences::ResolveSource(WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
			return bValid ? FText::FromName(Ref.OutputName)
				: FText::Format(LOCTEXT("BehaviorDeformInfluenceUnavailable", "Unavailable / {0}"),
					FText::FromName(Ref.OutputName));
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpInfluenceMenu)),
		LOCTEXT("BehaviorDeformInfluenceHint", "Optional Scalar 0..1 field. Multiplies relief displacement and any scoped mask; missing fields disable Deform.")));
	AddSliderRow(Panel, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorDeformStrength", "Strength"), Deform, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorDeformStrengthHint", "Signed strength of the displacement. Zero is neutral; negative reverses displacement.")));
	AddBehaviorFieldCompositionRows(Panel,
		[Deform]() -> FMixtormatBehaviorFieldInput* { return Deform() ? &Deform()->Direction : nullptr; }, false);
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([Deform]()
		{
			const FMixtormatBehavior* Selected = Deform();
			return Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MakeMemberSlider<FMixtormatBehavior>(
				LOCTEXT("BehaviorDeformGradientReach", "Gradient Reach (UV)"),
				Deform, &FMixtormatBehavior::GradientReach, 0.0, 0.25, 0.02, 0.001,
				LOCTEXT("BehaviorDeformGradientReachHint", "Maximum UV displacement from the current native-height gradient. Stable across resolutions; zero is neutral."))
		]);
	TSharedRef<SVerticalBox> FlowPanel = SNew(SVerticalBox);
	AddSliderRow(FlowPanel, MixtormatRow::MakePair(
		MakeMemberSlider<FMixtormatOutputReference>(LOCTEXT("BehaviorDeformFlowAmount", "Flow Amount"),
			Reference, &FMixtormatOutputReference::FlowAmount, -4.0, 4.0, 1.0, 0.01,
			LOCTEXT("BehaviorDeformFlowAmountHint", "Multiplies Deform Strength during Flow trace.")),
		MakeMemberSlider<FMixtormatOutputReference>(LOCTEXT("BehaviorDeformTraceLength", "Trace Length (UV)"),
			Reference, &FMixtormatOutputReference::FlowTraceLength, 0.0, 1.0, 0.05, 0.001,
			LOCTEXT("BehaviorDeformTraceHint", "Length of the Flow integration path."))));
	AddSliderRow(FlowPanel, MakeMemberSliderInt<FMixtormatOutputReference>(
		LOCTEXT("BehaviorDeformSteps", "Flow Steps"), Reference, &FMixtormatOutputReference::FlowSteps,
		1.0, 64.0, 16,
		LOCTEXT("BehaviorDeformStepsHint", "Integration steps; UV Maps use their coordinates directly.")));
	Panel->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility_Lambda([Deform, Reference]()
		{
			const FMixtormatBehavior* Selected = Deform();
			const FMixtormatOutputReference* Ref = Reference();
			return Selected && Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				&& Ref && Ref->Kind == EMixtormatPublishedFieldKind::Flow
				? EVisibility::Visible : EVisibility::Collapsed;
		})[FlowPanel]
	];
	return SNew(SVerticalBox)
		.Visibility_Lambda([Deform]() { return Deform() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.IsEnabled_Lambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child && !Child->IsInstance();
			})
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("BehaviorDeformHeading", "WARP"))
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Deform]()
					{
						const FMixtormatBehavior* Selected = Deform();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Deform](const ECheckBoxState State)
					{
						if (FMixtormatBehavior* Selected = Deform())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					})))
				[Panel]
			]
		];
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
    const auto OpIs = [Blend](const EMixtormatGeneratorHeightOp Op)
    {
        const FMixtormatGeneratorHeightBlend* B = Blend();
        return B && B->Op == Op;
    };

    TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

    AddSliderRow(Panel, MixtormatRow::MakePair(
        MakeMemberEnum<FMixtormatGeneratorHeightBlend, EMixtormatGeneratorHeightOp>(
            LOCTEXT("HeightBlendOp", "Operation"), Blend, &FMixtormatGeneratorHeightBlend::Op,
            LOCTEXT("HeightBlendOpHint", "How the running generator height combines with the referenced module."),
            FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })),
        MixtormatRow::MakeTrailing(
            LOCTEXT("HeightBlendSource", "Source"),
            MixtormatRow::MakeChip(
                TAttribute<FText>::CreateLambda([this]()
                {
                    const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
                    if (!B || !B->SourceChildId.IsValid())
                    {
                        return LOCTEXT("HeightBlendSourceNoneLabel", "None");
                    }
                    if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
                    {
                        for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
                        {
                            if (Child.ChildId == B->SourceChildId)
                            {
                                return GetLayerChildName(Child);
                            }
                        }
                    }
                    return LOCTEXT("HeightBlendSourceMissing", "Missing");
                }),
                FOnGetContent::CreateSP(this, &SMixtormat::BuildHeightBlendSourceMenu)),
            LOCTEXT("HeightBlendSourceHint", "Another module in this layer whose signed height is the second operand."))));

    AddSliderRow(Panel, MixtormatRow::MakePair(
        MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
            LOCTEXT("HeightBlendAmount", "Amount"), Blend,
            &FMixtormatGeneratorHeightBlend::Amount, 0.0, 1.0, 1.0, 0.01,
            LOCTEXT("HeightBlendAmountHint", "How much of the combined result replaces the running height.")),
        SNew(SWidgetSwitcher)
        .WidgetIndex_Lambda([OpIs]() -> int32
        {
            if (OpIs(EMixtormatGeneratorHeightOp::Multiply)) { return 0; }
            if (OpIs(EMixtormatGeneratorHeightOp::Min)
                || OpIs(EMixtormatGeneratorHeightOp::Max)
                || OpIs(EMixtormatGeneratorHeightOp::HeightBlend)) { return 1; }
            return 2;
        })
        + SWidgetSwitcher::Slot()
        [
            MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                LOCTEXT("HeightBlendScale", "Scale"), Blend,
                &FMixtormatGeneratorHeightBlend::Scale, -4.0, 4.0, 1.0, 0.01,
                LOCTEXT("HeightBlendScaleHint", "Multiply/Scale factor. With no source the module is neutral."))
        ]
        + SWidgetSwitcher::Slot()
        [
            MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                LOCTEXT("HeightBlendSoftness", "Softness"), Blend,
                &FMixtormatGeneratorHeightBlend::Softness, 0.0, 1.0, 0.0, 0.001,
                LOCTEXT("HeightBlendSoftnessHint", "Rounded join for Min, Max and Height Blend, in height units. 0 is a hard join."))
        ]
        + SWidgetSwitcher::Slot()
        [
            SNullWidget::NullWidget
        ]));

    AddSliderRow(Panel,
        SNew(SBox)
        .Visibility_Lambda([OpIs]()
        {
            return OpIs(EMixtormatGeneratorHeightOp::HeightBlend)
                ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MixtormatRow::MakePair(
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendThreshold", "Threshold"), Blend,
                    &FMixtormatGeneratorHeightBlend::Threshold, -1.0, 1.0, 0.0, 0.01),
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendEdgeSoftness", "Edge Softness"), Blend,
                    &FMixtormatGeneratorHeightBlend::EdgeSoftness, 0.0, 1.0, 0.1, 0.005))
        ]);
    AddSliderRow(Panel,
        SNew(SBox)
        .Visibility_Lambda([OpIs]()
        {
            return OpIs(EMixtormatGeneratorHeightOp::HeightBlend)
                ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MixtormatRow::MakePair(
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendBaseBias", "Base Bias"), Blend,
                    &FMixtormatGeneratorHeightBlend::BaseBias, -1.0, 1.0, 0.0, 0.01),
                MakeMemberSlider<FMixtormatGeneratorHeightBlend>(
                    LOCTEXT("HeightBlendBlendBias", "Blend Bias"), Blend,
                    &FMixtormatGeneratorHeightBlend::BlendBias, -1.0, 1.0, 0.0, 0.01))
        ]);

    return SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return GetSelectedHeightBlend() ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SMixtormatInspectorGroup)
            .Title(LOCTEXT("HeightBlendHeading", "HEIGHT BLEND"))
            .InitiallyExpanded(true)
            .HeaderAction(
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    MixtormatRow::MakeCheckbox(
                        TAttribute<ECheckBoxState>::CreateLambda([this]()
                        {
                            const FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend();
                            return B && B->bEnabled
                                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        }),
                        FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State)
                        {
                            if (FMixtormatGeneratorHeightBlend* B = GetSelectedHeightBlend())
                            {
                                B->bEnabled = State == ECheckBoxState::Checked;
                                RefreshLayeredPreview();
                                RebuildLayerList();
                            }
                        }))
                ])
            [
                Panel
            ]
        ];
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

TSharedRef<SWidget> SMixtormat::BuildColorRampSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	if (!GetSelectedHeightColorRamp()) { return Menu.Build(); }
	Menu.Item(LOCTEXT("ColorRampSourceModuleNonePick", "None"), nullptr,
		FSimpleDelegate::CreateLambda([this]()
		{
			if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
			{
				R->SourceChildId.Invalidate();
				RefreshLayeredPreview();
			}
		}));
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		// Only earlier modules: this sublayer runs in chain order, so a later module has not
		// produced a height yet and referencing it would silently read the running chain. The
		// order filter applies only when the selection actually resolves to this ramp in this
		// array; otherwise list everything, the same as Height Blend's source menu.
		const TArray<FMixtormatLayerChild>& Children = WorkingLayers[SelectedLayerIndex].Children;
		const int32 SelfIndex = GetSelectedChildIndex();
		const bool bFilterByOrder = Children.IsValidIndex(SelfIndex)
			&& Children[SelfIndex].Type == EMixtormatLayerChildType::HeightColorRamp;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (bFilterByOrder && Index >= SelfIndex) { break; }
			const FMixtormatLayerChild& Child = Children[Index];
			if (Child.Type != EMixtormatLayerChildType::Generator) { continue; }
			const FGuid ChildId = Child.ChildId;
			Menu.Item(GetLayerChildName(Child), MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, ChildId]()
				{
					if (FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp())
					{
						R->SourceChildId = ChildId;
						R->Source = EMixtormatColorRampSource::ModuleRef;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildHeightColorRampControls()
{
	const auto Ramp = [this]() { return GetSelectedHeightColorRamp(); };
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	TSharedPtr<SMixtormatColorRamp> RampWidget;
	AddSliderRow(Panel, SAssignNew(RampWidget, SMixtormatColorRamp)
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

	// Where the ramp's scalar comes from. Generator Height keeps the original chain behaviour;
	// Module is an earlier generator's published height; Layer Height is this layer's resolved
	// input; Composite Below is the height accumulated under this layer.
	const auto RampSourceIs = [Ramp](const EMixtormatColorRampSource Source)
	{
		const FMixtormatGeneratorHeightColorRamp* R = Ramp();
		return R && R->Source == Source;
	};
	AddSliderRow(Panel, MakeMemberEnum<FMixtormatGeneratorHeightColorRamp, EMixtormatColorRampSource>(
		LOCTEXT("ColorRampSource", "Source"), Ramp, &FMixtormatGeneratorHeightColorRamp::Source,
		LOCTEXT("ColorRampSourceHint", "Which height feeds the ramp: the running generator chain, an earlier module, this layer's input, or the height composited below this layer."),
		FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([RampSourceIs]()
		{
			return RampSourceIs(EMixtormatColorRampSource::ModuleRef)
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeTrailing(
				LOCTEXT("ColorRampSourceModule", "Module"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatGeneratorHeightColorRamp* R = GetSelectedHeightColorRamp();
						if (!R || !R->SourceChildId.IsValid())
						{
							return LOCTEXT("ColorRampSourceModuleNone", "None");
						}
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							for (const FMixtormatLayerChild& Child : WorkingLayers[SelectedLayerIndex].Children)
							{
								if (Child.ChildId == R->SourceChildId)
								{
									return GetLayerChildName(Child);
								}
							}
						}
						return LOCTEXT("ColorRampSourceModuleMissing", "Missing");
					}),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildColorRampSourceMenu)),
				LOCTEXT("ColorRampSourceModuleHint", "The earlier module whose signed height this ramp maps. Picking one switches Source to Module; a missing reference reads the running height."))
		]);

	return SNew(SBox).Visibility_Lambda([this]() { return GetSelectedHeightColorRamp() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("ColorRampHeading", "COLOR RAMP")).InitiallyExpanded(true)
		.HeaderAction(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
				FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				MakeChildOutputPreviewButton(
					GetPreviewOutputSetForChildType(EMixtormatLayerChildType::HeightColorRamp))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f,
				FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
			[
				// Presets as a compact header dropdown instead of a chip strip under the ramp.
				SNew(SComboButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
				.HasDownArrow(false)
				.ContentPadding(FMargin(0.0f))
				.OnGetMenuContent_Lambda([RampWidget]() -> TSharedRef<SWidget>
				{
					MixtormatMenu::FBuilder Menu;
					Menu.Caption(LOCTEXT("ColorRampPresetsHeading", "PRESETS"));
					for (int32 Index = 0; Index < SMixtormatColorRamp::GetPresetCount(); ++Index)
					{
						const int32 PresetIndex = Index;
						Menu.Item(SMixtormatColorRamp::GetPresetName(PresetIndex), nullptr,
							FSimpleDelegate::CreateLambda([RampWidget, PresetIndex]()
							{
								if (RampWidget.IsValid()) { RampWidget->ApplyPreset(PresetIndex); }
							}));
					}
					return Menu.Build();
				})
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("ColorRampPresets", "Presets"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 2.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
						[
							SNew(SImage)
							.Image(MixtormatIcons::ChevronDown())
							.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
						]
					]
				]
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


TSharedRef<SWidget> SMixtormat::BuildNoiseControls()
{
	const auto Noise = [this]() { return GetSelectedNoise(); };
	TArray<EMixtormatNoiseType> NoiseTypes;
	const UEnum* NoiseEnum = StaticEnum<EMixtormatNoiseType>();
	for (int32 Index = 0; Index < NoiseEnum->NumEnums(); ++Index)
	{
		const int64 Value = NoiseEnum->GetValueByIndex(Index);
		if (Value != INDEX_NONE && !NoiseEnum->HasMetaData(TEXT("Hidden"), Index))
		{
			NoiseTypes.Add(static_cast<EMixtormatNoiseType>(Value));
		}
	}
	const auto NoiseWidgetIndex = [Noise, NoiseTypes]()
	{
		const FMixtormatNoise* N = Noise();
		return N ? NoiseTypes.IndexOfByKey(N->NoiseType) : INDEX_NONE;
	};
	TSharedRef<SWidgetSwitcher> Outputs = SNew(SWidgetSwitcher).WidgetIndex_Lambda(NoiseWidgetIndex);
	TSharedRef<SWidgetSwitcher> HeaderPreview = SNew(SWidgetSwitcher).WidgetIndex_Lambda(NoiseWidgetIndex);
	// The inspector persists across selections; choose outputs from the current Noise family.
	for (const EMixtormatNoiseType NoiseType : NoiseTypes)
	{
		FMixtormatLayerChild Probe;
		Probe.Type = EMixtormatLayerChildType::Generator;
		Probe.Generator.Type = EMixtormatGeneratorType::Noise;
		Probe.Generator.Noise.NoiseType = NoiseType;
		Outputs->AddSlot()[BuildChildOutputsControls(GetChildCapabilities(Probe))];
		HeaderPreview->AddSlot()[MakeChildOutputPreviewButton(GetChildPreviewOutputSet(Probe))];
	}
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Output = AddCard(Cards, LOCTEXT("NoiseOutput", "OUTPUT"));
		AddSliderRow(Output, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseHeightScale", "Scale"), Noise, &FMixtormatNoise::NoiseHeightScale, -4.0, 4.0, 1.0, 0.01,
				LOCTEXT("NoiseHeightScaleHint", "Scales the signed generator height after centring.")),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseHeightBias", "Bias"), Noise, &FMixtormatNoise::NoiseHeightBias, -1.0, 1.0, 0.0, 0.01,
				LOCTEXT("NoiseHeightBiasHint", "Offsets the centred height, before Scale."))));
		AddSliderRow(Output, MixtormatRow::MakeTrailing(
			LOCTEXT("NoiseNormalizeHeight", "Normalize"),
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Noise]()
					{
						const FMixtormatNoise* N = Noise();
						return N && N->bNoiseNormalizeHeight ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this, Noise](const ECheckBoxState State)
					{
						if (FMixtormatNoise* N = Noise())
						{
							N->bNoiseNormalizeHeight = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
						}
					}),
					LOCTEXT("NoiseNormalizeHeightHint", "Centre height onto -1..1 so Height Blend compares fairly. Off uses the raw field."))));
	}
	Cards->AddSlot().AutoHeight()[BuildNoisePatternPlacementControls(Noise)];

	Cards->AddSlot().AutoHeight()[Outputs];
	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedNoise() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("NoiseHeading", "NOISE"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f,
					FMixtormatThemeStore::GetResolved().ControlLayout.InspectorFeatureButtonGap, 0.0f)
				[
					HeaderPreview
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
						LOCTEXT("NoiseEnabledHint", "Enable this noise module"))
				])
			[
				Cards
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildNoisePatternPlacementControls(TFunction<FMixtormatNoise*()> Noise)
{
	const auto IsDirectional = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::Bars
			|| N->NoiseType == EMixtormatNoiseType::Phasor);
	};
	const auto IsPhasor = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && N->NoiseType == EMixtormatNoiseType::Phasor;
	};
	const auto IsWorley = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::WorleyF1
			|| N->NoiseType == EMixtormatNoiseType::WorleyF2
			|| N->NoiseType == EMixtormatNoiseType::WorleyF1MinusF2);
	};
	const auto LayerMixAddress = MakeAddressResolver(Noise, &FMixtormatNoise::NoiseLayerMix);
	const auto IsMultiOctave = [this, Noise, LayerMixAddress]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::FBM
			|| N->NoiseType == EMixtormatNoiseType::Ridged
			|| N->NoiseType == EMixtormatNoiseType::Billow
			|| GetEffectiveFloatParameter(LayerMixAddress(), N->NoiseLayerMix) > 0.0f);
	};
	const auto HasNativeOctaves = [Noise]()
	{
		const FMixtormatNoise* N = Noise();
		return N && (N->NoiseType == EMixtormatNoiseType::FBM
			|| N->NoiseType == EMixtormatNoiseType::Ridged
			|| N->NoiseType == EMixtormatNoiseType::Billow);
	};
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
	{
		const TSharedRef<SVerticalBox> Pattern = AddCard(Cards, LOCTEXT("NoisePattern", "PATTERN"));
		AddSliderRow(Pattern, MakeMemberEnum<FMixtormatNoise, EMixtormatNoiseType>(
			LOCTEXT("NoiseType", "Type"), Noise, &FMixtormatNoise::NoiseType,
			LOCTEXT("NoiseTypeHint", "Noise family. Bars and Phasor use Direction; Worley produces cell IDs."),
			FSimpleDelegate::CreateLambda([this]() { RefreshLayeredPreview(); RebuildLayerList(); })));
		AddSliderRow(Pattern, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatNoise>(
				LOCTEXT("NoiseSeed", "Seed"), Noise, &FMixtormatNoise::NoiseSeed, 0.0, 9999.0, 1),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseScale", "Scale"), Noise, &FMixtormatNoise::NoiseScale, 1.0, 64.0, 8.0, 0.1,
				LOCTEXT("NoiseScaleHint", "Lattice cells across the tile."))));
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorFrequency", "Frequency"),
					Noise, &FMixtormatNoise::NoisePhasorFrequency, 0.0, 12.0, 2.0, 0.05),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorAnisotropy", "Anisotropy"),
					Noise, &FMixtormatNoise::NoisePhasorAnisotropy, 0.0, 8.0, 0.0, 0.05))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorPhase", "Phase Variation"),
					Noise, &FMixtormatNoise::NoisePhasorPhaseVariation, 0.0, 1.0, 0.5, 0.01),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorOrientation", "Orientation Variation"),
					Noise, &FMixtormatNoise::NoisePhasorOrientationVariation, 0.0, 3.14, 0.35, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MakeMemberSliderInt<FMixtormatNoise>(LOCTEXT("NoisePhasorComponents", "Components"),
				Noise, &FMixtormatNoise::NoisePhasorComponents, 1.0, 4.0, 2,
				LOCTEXT("NoisePhasorComponentsHint", "Overlapping seeded impulse layers."))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsPhasor]()
			{ return IsPhasor() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorScale", "Phasor Scale"),
					Noise, &FMixtormatNoise::NoisePhasorScale, 0.0, 4.0, 1.0, 0.01),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoisePhasorBias", "Bias"),
					Noise, &FMixtormatNoise::NoisePhasorBias, -1.0, 1.0, 0.0, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsWorley]()
			{ return IsWorley() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MixtormatRow::MakePair(
				MakeMemberEnum<FMixtormatNoise, EMixtormatNoiseWorleyMetric>(
					LOCTEXT("NoiseWorleyMetric", "Metric"), Noise, &FMixtormatNoise::NoiseWorleyMetric),
				MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseWorleyJitter", "Jitter"),
					Noise, &FMixtormatNoise::NoiseWorleyJitter, 0.0, 1.0, 1.0, 0.01))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([IsWorley]()
			{ return IsWorley() ? EVisibility::Visible : EVisibility::Collapsed; })[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseWorleyCellDepth", "Cell Depth"),
				Noise, &FMixtormatNoise::NoiseWorleyCellDepth, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("NoiseWorleyCellDepthHint", "Modulates cell distance without moving Voronoi feature points or IDs."))]);
		AddSliderRow(Pattern, SNew(SBox).Visibility_Lambda([HasNativeOctaves]()
			{ return HasNativeOctaves() ? EVisibility::Collapsed : EVisibility::Visible; })[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseLayerMix", "Layer Mix"),
				Noise, &FMixtormatNoise::NoiseLayerMix, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("NoiseLayerMixHint", "Mix additional octave layers into this noise. Zero preserves its original one-octave field."))]);
		AddSliderRow(Pattern,
			SNew(SBox)
			.Visibility_Lambda([IsMultiOctave]() { return IsMultiOctave() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MixtormatRow::MakePair(
					MakeMemberSliderInt<FMixtormatNoise>(
						LOCTEXT("NoiseDetail", "Detail"), Noise, &FMixtormatNoise::NoiseDetail, 1.0, 8.0, 4,
						LOCTEXT("NoiseDetailHint", "Octave count for FBM/Ridged/Billow and enabled layered noise families.")),
					MakeMemberSlider<FMixtormatNoise>(
						LOCTEXT("NoiseRoughness", "Roughness"), Noise, &FMixtormatNoise::NoiseRoughness, 0.0, 1.0, 0.5, 0.01))
			]);
		AddSliderRow(Pattern,
			SNew(SBox)
			.Visibility_Lambda([IsMultiOctave]() { return IsMultiOctave() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseLacunarity", "Lacunarity"), Noise, &FMixtormatNoise::NoiseLacunarity, 1.0, 4.0, 2.0, 0.01,
					LOCTEXT("NoiseLacunarityHint", "Frequency step between octaves."))
			]);
	}
	{
		const TSharedRef<SVerticalBox> Place = AddCard(Cards, LOCTEXT("NoisePlacement", "PLACEMENT"));
		AddSliderRow(Place, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseOffsetX", "Offset X"), Noise, &FMixtormatNoise::NoiseOffsetX, -1.0, 1.0, 0.0, 0.01),
			MakeMemberSlider<FMixtormatNoise>(
				LOCTEXT("NoiseOffsetY", "Offset Y"), Noise, &FMixtormatNoise::NoiseOffsetY, -1.0, 1.0, 0.0, 0.01)));
		AddSliderRow(Place,
			SNew(SBox)
			.Visibility_Lambda([IsDirectional]() { return IsDirectional() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseDirection", "Direction"), Noise, &FMixtormatNoise::NoiseDirection, 0.0, 360.0, 0.0, 1.0,
					LOCTEXT("NoiseDirectionHint", "Stripe or phasor orientation, snapped to a tileable direction."))
			]);
	}

	{
		const TSharedRef<SVerticalBox> Distort = AddCard(Cards, LOCTEXT("NoiseDistortion", "DISTORTION"));
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionStrength", "Strength"),
				Noise, &FMixtormatNoise::NoiseDistortionStrength, 0.0, 2.0, 0.0, 0.01,
				LOCTEXT("NoiseDistortionStrengthHint", "Zero bypasses smooth/curl distortion; Jaggedness is independent.")),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionFrequency", "Frequency"),
				Noise, &FMixtormatNoise::NoiseDistortionFrequency, 1.0, 64.0, 4.0, 1.0)));
		AddSliderRow(Distort, MakeMemberSlider<FMixtormatNoise>(
			LOCTEXT("NoiseDistortionJaggedness", "Jaggedness"), Noise,
			&FMixtormatNoise::NoiseDistortionJaggedness, 0.0, 2.0, 0.0, 0.01,
			LOCTEXT("NoiseDistortionJaggednessHint", "Angular crease warp, blended with the smooth distortion.")));
		// Crease shaping only matters once a jagged warp is actually authored, so the rows are
		// collapsed at zero rather than left greyed out.
		AddSliderRow(Distort, SNew(SBox).Visibility_Lambda([Noise]()
			{
				const FMixtormatNoise* N = Noise();
				return N && N->NoiseDistortionJaggedness > 0.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})[
			MixtormatRow::MakePair(
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseJaggedSharpness", "Jagged Sharpness"), Noise,
					&FMixtormatNoise::NoiseJaggedSharpness, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseJaggedSharpnessHint", "Breaks per cell edge. Higher packs tighter; amplitude follows.")),
				MakeMemberSlider<FMixtormatNoise>(
					LOCTEXT("NoiseJaggedDetail", "Jagged Variation"), Noise,
					&FMixtormatNoise::NoiseJaggedDetail, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("NoiseJaggedDetailHint", "Fracture scales stacked. 0 is a single scale.")))]);
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSliderInt<FMixtormatNoise>(LOCTEXT("NoiseDistortionOctaves", "Octaves"),
				Noise, &FMixtormatNoise::NoiseDistortionOctaves, 1.0, 8.0, 2),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionRoughness", "Roughness"),
				Noise, &FMixtormatNoise::NoiseDistortionRoughness, 0.0, 1.0, 0.5, 0.01)));
		AddSliderRow(Distort, MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionLacunarity", "Lacunarity"),
					Noise, &FMixtormatNoise::NoiseDistortionLacunarity, 1.0, 4.0, 2.0, 0.05),
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionCurlMix", "Curl Mix"),
					Noise, &FMixtormatNoise::NoiseDistortionCurlMix, 0.0, 1.0, 1.0, 0.01,
					LOCTEXT("NoiseDistortionCurlMixHint", "0 = directional, 1 = curl-driven domain distortion."))));
		AddSliderRow(Distort, SNew(SBox).Visibility_Lambda([Noise]()
			{
				const FMixtormatNoise* N = Noise();
				return N && N->NoiseDistortionCurlMix < 1.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})[
			MakeMemberSlider<FMixtormatNoise>(LOCTEXT("NoiseDistortionDirection", "Direction"),
				Noise, &FMixtormatNoise::NoiseDistortionDirection, 0.0, 360.0, 0.0, 1.0)]);
	}

	return Cards;
}

#undef LOCTEXT_NAMESPACE
