// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatChildScope.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Rows/SMixtormatRow.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

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
	// A discrete choice, so it is a segmented strip rather than a dropdown: the whole set reads at
	// once and the current fold is visible without opening anything. A kind that cannot fold keeps
	// the strip but refuses the change, which is the same answer the runtime gives.
	AddSliderRow(TargetPanel,
		SNew(SBox).Visibility_Lambda([bVisible]() { return bVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			MixtormatRow::MakeTrailing(
				LOCTEXT("BehaviorFieldBlend", "Combine"),
				SNew(SMixtormatSegmentedControl)
				.Options(TArray<FText>{
					LOCTEXT("BehaviorFieldBlendDefault", "Default"),
					LOCTEXT("BehaviorFieldBlendAdd", "Add"),
					LOCTEXT("BehaviorFieldBlendMultiply", "Multiply"),
					LOCTEXT("BehaviorFieldBlendMin", "Min"),
					LOCTEXT("BehaviorFieldBlendMax", "Max")})
				.ToolTips(TArray<FText>{
					LOCTEXT("BehaviorFieldBlendDefaultHint", "The operation's own default fold. Push stays additive."),
					LOCTEXT("BehaviorFieldBlendAddHint", "Adds this field to the current native height."),
					LOCTEXT("BehaviorFieldBlendMultiplyHint", "Multiplies the current native height by this field."),
					LOCTEXT("BehaviorFieldBlendMinHint", "Keeps the lower of the two."),
					LOCTEXT("BehaviorFieldBlendMaxHint", "Keeps the higher of the two.")})
				.ActiveIndex_Lambda([ResolveField, Kind]() -> int32
				{
					const FMixtormatBehaviorFieldInput* Field = ResolveField();
					// Show the effective combine rather than a stored value the runtime would
					// refuse: a blend on a kind that cannot fold reads as the operation default.
					if (!Field || !FMixtormatBehaviorFieldInput::KindSupportsBlend(Kind()))
					{
						return 0;
					}
					switch (Field->Blend)
					{
					case EMixtormatBehaviorFieldBlend::Add: return 1;
					case EMixtormatBehaviorFieldBlend::Multiply: return 2;
					case EMixtormatBehaviorFieldBlend::Min: return 3;
					case EMixtormatBehaviorFieldBlend::Max: return 4;
					default: return 0;
					}
				})
				.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, ResolveField, Kind](const int32 Index)
				{
					FMixtormatBehaviorFieldInput* Field = ResolveField();
					if (!Field) { return; }
					// The others are Push-only: a coordinate field and a distance field have no
					// base to fold into, so the strip refuses rather than storing a dead value.
					if (Index != 0 && !FMixtormatBehaviorFieldInput::KindSupportsBlend(Kind())) { return; }
					const EMixtormatBehaviorFieldBlend Blend =
						Index == 1 ? EMixtormatBehaviorFieldBlend::Add :
						Index == 2 ? EMixtormatBehaviorFieldBlend::Multiply :
						Index == 3 ? EMixtormatBehaviorFieldBlend::Min :
						Index == 4 ? EMixtormatBehaviorFieldBlend::Max :
						EMixtormatBehaviorFieldBlend::Operation;
					if (Field->Blend == Blend) { return; }
					Field->Blend = Blend;
					RefreshLayeredPreview();
					RebuildLayerList();
				}))
			)
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
					&& (MixtormatOutputReferences::ResolveGeneratorInputSource(
						WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE
						|| MixtormatOutputReferences::ResolveSource(
							WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE);
				return bAvailable ? FText::FromName(Ref.OutputName)
					: LOCTEXT("BehaviorPushMissingSource", "Source unavailable");
			}
			default: return LOCTEXT("BehaviorPushUnset", "Choose source");
			}
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorPushSourceMenu)),
		LOCTEXT("BehaviorPushHeightHint", "Explicit signed height input; no automatic SDF conversion.")));
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
				.Title(LOCTEXT("BehaviorPushTitle", "PUSH")).PresetAction(MakeModulePresetButton())
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

TSharedRef<SWidget> SMixtormat::BuildBehaviorCarveControls()
{
	const auto Sculpt = [this]() { return GetSelectedBehaviorCarve(); };
	const auto Flow = [Sculpt]() -> FMixtormatBehaviorFlowSettings*
	{
		FMixtormatBehavior* B = Sculpt();
		return B ? &B->Flow : nullptr;
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Operation = AddCard(Panel, LOCTEXT("BehaviorSculptOperationCard", "SCULPT"));
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorSculptStrength", "Strength"), Sculpt, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01));
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorSculptDepth", "Depth"), Flow, &FMixtormatBehaviorFlowSettings::Depth,
		-2.0, 2.0, 1.0, 0.01,
		LOCTEXT("BehaviorSculptDepthHint", "Positive cuts Height; negative raises Height.")));
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorSculptWidth", "Width"), Flow, &FMixtormatBehaviorFlowSettings::Width,
		0.0, 0.25, 0.01, 0.001));
	TSharedRef<SVerticalBox> Trace = AddCard(Panel, LOCTEXT("BehaviorSculptTraceCard", "TRACE"));
	AddSliderRow(Trace, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorSculptTraceLength", "Trace Length"), Flow,
		&FMixtormatBehaviorFlowSettings::TraceLength, 0.0, 1.0, 0.1, 0.001));
	AddSliderRow(Trace, MakeMemberSliderInt<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorSculptTraceSteps", "Steps"), Flow,
		&FMixtormatBehaviorFlowSettings::TraceSteps, 1, 256, 16));
	AddSliderRow(Trace, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorSculptFalloff", "Falloff"), Flow,
		&FMixtormatBehaviorFlowSettings::Falloff, 0.1, 8.0, 1.0, 0.01));
	return SNew(SVerticalBox)
		.Visibility_Lambda([Sculpt]() { return Sculpt() ? EVisibility::Visible : EVisibility::Collapsed; })
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
				.Title(LOCTEXT("BehaviorSculptHeading", "SCULPT")).PresetAction(MakeModulePresetButton())
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Sculpt]()
					{
						const FMixtormatBehavior* B = Sculpt();
						return B && B->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Sculpt](ECheckBoxState State)
					{
						if (FMixtormatBehavior* B = Sculpt())
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
		const auto Flow = [Warp]() -> FMixtormatBehaviorFlowSettings*
		{
			FMixtormatBehavior* Selected = Warp();
			return Selected ? &Selected->Flow : nullptr;
		};
		TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
		TSharedRef<SVerticalBox> Operation = AddCard(Panel, LOCTEXT("BehaviorWarpOperationCard", "OPERATION"));
		TSharedRef<SVerticalBox> Standard = SNew(SVerticalBox);
		Panel->AddSlot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([Warp]()
			{
				const FMixtormatBehavior* B = Warp();
				return B && B->Flow.ModeMix < 1.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatInspectorCard).Title(LOCTEXT("BehaviorWarpStandardCard", "STANDARD WARP"))[Standard]
			]
		];
		TSharedRef<SVerticalBox> TracedWarpPanel = SNew(SVerticalBox);
		Panel->AddSlot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([Warp]()
			{
				const FMixtormatBehavior* B = Warp();
				return B && B->Stage == EMixtormatBehaviorStage::PostGeneration
					&& B->Flow.ModeMix > 0.0f ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatInspectorCard).Title(LOCTEXT("BehaviorWarpTracedCard", "TRACED FLOW"))[TracedWarpPanel]
			]
		];

		AddSliderRow(Operation, SNew(SBox)
			.Visibility_Lambda([Warp]()
			{
				const FMixtormatBehavior* B = Warp();
				return B && B->Stage == EMixtormatBehaviorStage::PostGeneration
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
					LOCTEXT("BehaviorWarpModeMix", "Mode Mix"), Flow,
					&FMixtormatBehaviorFlowSettings::ModeMix, 0.0, 1.0, 0.0, 0.01,
					LOCTEXT("BehaviorWarpModeMixHint", "0 = Standard Warp, 1 = Traced Flow. Before Generation uses Standard coordinates."))
			]);

		AddSliderRow(Standard,
			SNew(SBox)
			.Visibility_Lambda([Warp]()
			{
				const FMixtormatBehavior* B = Warp();
				return B && B->Flow.ModeMix < 1.0f
					&& B->Direction.Origin != EMixtormatBehaviorFieldOrigin::None
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MixtormatRow::MakeDropdown(
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
						const bool bAvailable = OwnerIndex != INDEX_NONE
							&& MixtormatOutputReferences::ResolveGeneratorInputSource(
								WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
						if (!bAvailable)
						{
							return FText::Format(LOCTEXT("BehaviorWarpUnavailableSource", "Unavailable / {0}"),
								FText::FromName(Ref.OutputName));
						}
						return FText::Format(LOCTEXT("BehaviorWarpSourceChip", "{0} / {1}"),
							LOCTEXT("BehaviorWarpLayerChip", "Layer"),
							FText::FromName(Ref.OutputName));
					}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorWarpSourceMenu)),
					LOCTEXT("BehaviorWarpDirectionHint", "Completed Flow or lifted UV Map from an earlier source."))
			]);
	AddSliderRow(Standard, SNew(SBox)
		.Visibility_Lambda([Warp]()
		{
			const FMixtormatBehavior* B = Warp();
			return B && B->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeTrailing(
				LOCTEXT("BehaviorWarpStageLabel", "Sampling Stage"),
				SNew(SMixtormatSegmentedControl)
				.Options(TArray<FText>{
					LOCTEXT("BehaviorWarpPreStage", "Before"),
					LOCTEXT("BehaviorWarpPostStage", "After")})
				.ToolTips(TArray<FText>{
					LOCTEXT("BehaviorWarpPreStageHint", "Changes generator sampling coordinates before generation."),
					LOCTEXT("BehaviorWarpPostStageHint", "Remaps completed generator outputs after generation.")})
				.ActiveIndex_Lambda([Warp]() -> int32
				{
					const FMixtormatBehavior* B = Warp();
					return B && B->Stage == EMixtormatBehaviorStage::PreGeneration ? 0 : 1;
				})
				.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, Warp](const int32 Index)
				{
					FMixtormatBehavior* B = Warp();
					if (!B) { return; }
					const EMixtormatBehaviorStage Stage = Index == 0
						? EMixtormatBehaviorStage::PreGeneration : EMixtormatBehaviorStage::PostGeneration;
					if (B->Stage == Stage) { return; }
					// Before needs a published direction source; the strip refuses the change
					// rather than storing a stage the gather would reject.
					if (Stage == EMixtormatBehaviorStage::PreGeneration
						&& B->Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput) { return; }
					B->Stage = Stage;
					RefreshLayeredPreview();
					RebuildLayerList();
				}))
			)]);
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorWarpStrength", "Strength"), Warp, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorWarpStrengthHint", "Signed strength of the displacement. Zero is neutral; negative reverses displacement.")));

	AddSliderRow(Standard, SNew(SBox).Visibility_Lambda([Warp]()
	{
		const FMixtormatBehavior* B = Warp();
		return B && B->Flow.ModeMix < 1.0f
			&& B->Direction.Origin == EMixtormatBehaviorFieldOrigin::None
			? EVisibility::Visible : EVisibility::Collapsed;
	})[
		MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
				LOCTEXT("BehaviorWarpAutoTraceLength", "Trace Length"),
				Flow, &FMixtormatBehaviorFlowSettings::StandardTraceLength, 0.0, 1.0, 0.1, 0.001),
			MakeMemberSliderInt<FMixtormatBehaviorFlowSettings>(
				LOCTEXT("BehaviorWarpAutoTraceSteps", "Steps"),
				Flow, &FMixtormatBehaviorFlowSettings::StandardTraceSteps, 1, 64, 16))]);
	AddBehaviorFieldCompositionRows(Standard,
		[Warp]() -> FMixtormatBehaviorFieldInput* { return Warp() ? &Warp()->Direction : nullptr; }, false);
	AddSliderRow(Standard,
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
	Standard->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility_Lambda([Warp, Reference]()
		{
			const FMixtormatBehavior* Selected = Warp();
			const FMixtormatOutputReference* Ref = Reference();
			return Selected && Selected->Flow.ModeMix < 1.0f
				&& Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				&& Ref && Ref->Kind == EMixtormatPublishedFieldKind::Flow
				? EVisibility::Visible : EVisibility::Collapsed;
		})[FlowPanel]
	];
	AddSliderRow(TracedWarpPanel, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorTracedWarpTraceLength", "Trace Length"), Flow, &FMixtormatBehaviorFlowSettings::TraceLength,
		0, 1, 0.1, 0.001));
	AddSliderRow(TracedWarpPanel, MakeMemberSliderInt<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorTracedWarpSteps", "Trace Steps"), Flow, &FMixtormatBehaviorFlowSettings::TraceSteps,
		1, 64, 16));
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
				.Title(LOCTEXT("BehaviorWarpHeading", "WARP")).PresetAction(MakeModulePresetButton())
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
		const auto Flow = [Deform]() -> FMixtormatBehaviorFlowSettings*
		{
			FMixtormatBehavior* Selected = Deform();
			return Selected ? &Selected->Flow : nullptr;
		};
		TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
		TSharedRef<SVerticalBox> Operation = AddCard(Panel, LOCTEXT("BehaviorDeformOperationCard", "OPERATION"));
		TSharedRef<SVerticalBox> Standard = SNew(SVerticalBox);
		Panel->AddSlot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([Deform]()
			{
				const FMixtormatBehavior* B = Deform();
				return B && !B->Flow.bUseTracedFlow ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatInspectorCard).Title(LOCTEXT("BehaviorDeformStandardCard", "STANDARD DEFORM"))[Standard]
			]
		];
		TSharedRef<SVerticalBox> TracedDeformPanel = SNew(SVerticalBox);
		Panel->AddSlot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([Deform]()
			{
				const FMixtormatBehavior* B = Deform();
				return B && B->Flow.bUseTracedFlow ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatInspectorCard).Title(LOCTEXT("BehaviorDeformTracedCard", "TRACED DEFORM"))[TracedDeformPanel]
			]
		];
		AddSliderRow(Operation, MixtormatRow::MakeTrailing(
			LOCTEXT("BehaviorDeformModeLabel", "Deform Mode"),
			SNew(SMixtormatSegmentedControl)
			.Options(TArray<FText>{LOCTEXT("BehaviorDeformModeLabelStd", "Standard"), LOCTEXT("BehaviorDeformModeLabelTrace", "Traced")})
			.ActiveIndex_Lambda([Deform]() { const auto* B = Deform(); return B && B->Flow.bUseTracedFlow ? 1 : 0; })
			.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, Deform](int32 Index)
			{
				if (auto* B = Deform())
				{
					B->Flow.bUseTracedFlow = Index == 1;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))));
		AddSliderRow(Standard,
			SNew(SBox)
			.Visibility_Lambda([Deform]()
			{
				const FMixtormatBehavior* B = Deform();
				return B && !B->Flow.bUseTracedFlow
					&& B->Direction.Origin != EMixtormatBehaviorFieldOrigin::None
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MixtormatRow::MakeDropdown(
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
						const bool bAvailable = OwnerIndex != INDEX_NONE
							&& MixtormatOutputReferences::ResolveGeneratorInputSource(
								WorkingLayers, LayerIndex, OwnerIndex, Ref) != INDEX_NONE;
						if (!bAvailable)
						{
							return FText::Format(LOCTEXT("BehaviorDeformUnavailableSource", "Unavailable / {0}"),
								FText::FromName(Ref.OutputName));
						}
						return FText::Format(LOCTEXT("BehaviorDeformSourceChip", "{0} / {1}"),
							LOCTEXT("BehaviorDeformLayerChip", "Layer"),
							FText::FromName(Ref.OutputName));
					}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBehaviorDeformSourceMenu)),
					LOCTEXT("BehaviorDeformDirectionHint", "Completed Flow or lifted UV Map from an earlier source. Deform resamples native height only, leaving IDs and coverage fixed."))
			]);
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorDeformStrength", "Strength"), Deform, &FMixtormatBehavior::Strength,
		-4.0, 4.0, 1.0, 0.01,
		LOCTEXT("BehaviorDeformStrengthHint", "Signed strength of the displacement. Zero is neutral; negative reverses displacement.")));
	AddSliderRow(Standard, SNew(SBox).Visibility_Lambda([Deform]()
	{
		const FMixtormatBehavior* B = Deform();
		return B && !B->Flow.bUseTracedFlow
			&& B->Direction.Origin == EMixtormatBehaviorFieldOrigin::None
			? EVisibility::Visible : EVisibility::Collapsed;
	})[
		MixtormatRow::MakePair(
			MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
				LOCTEXT("BehaviorDeformAutoTraceLength", "Trace Length"),
				Flow, &FMixtormatBehaviorFlowSettings::StandardTraceLength, 0.0, 1.0, 0.1, 0.001),
			MakeMemberSliderInt<FMixtormatBehaviorFlowSettings>(
				LOCTEXT("BehaviorDeformAutoTraceSteps", "Steps"),
				Flow, &FMixtormatBehaviorFlowSettings::StandardTraceSteps, 1, 64, 16))]);
	AddBehaviorFieldCompositionRows(Standard,
		[Deform]() -> FMixtormatBehaviorFieldInput* { return Deform() ? &Deform()->Direction : nullptr; }, false);
	AddSliderRow(Standard,
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
	Standard->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility_Lambda([Deform, Reference]()
		{
			const FMixtormatBehavior* Selected = Deform();
			const FMixtormatOutputReference* Ref = Reference();
			return Selected && !Selected->Flow.bUseTracedFlow
				&& Selected->Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput
				&& Ref && Ref->Kind == EMixtormatPublishedFieldKind::Flow
				? EVisibility::Visible : EVisibility::Collapsed;
		})[FlowPanel]
	];
	AddSliderRow(TracedDeformPanel, MakeMemberSlider<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorDeformOwnTraceLength", "Trace Length"), Flow,
		&FMixtormatBehaviorFlowSettings::TraceLength, 0.0, 1.0, 0.1, 0.001));
	AddSliderRow(TracedDeformPanel, MakeMemberSliderInt<FMixtormatBehaviorFlowSettings>(
		LOCTEXT("BehaviorDeformTraceSteps", "Steps"), Flow,
		&FMixtormatBehaviorFlowSettings::TraceSteps, 1, 64, 16));
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
				.Title(LOCTEXT("BehaviorDeformHeading", "DEFORM")).PresetAction(MakeModulePresetButton())
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


TSharedRef<SWidget> SMixtormat::BuildBehaviorFeatherControls()
{
	const auto Get = [this]() -> FMixtormatBehavior*
	{
		FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
		return Child && Child->Type == EMixtormatLayerChildType::Behavior
			&& Child->Behavior.Type == EMixtormatBehaviorType::Feather ? &Child->Behavior : nullptr;
	};
	const auto Settings = [Get]() -> FMixtormatBehaviorFeatherSettings*
	{
		FMixtormatBehavior* B = Get();
		return B ? &B->Feather : nullptr;
	};
	const auto IsFlowOwner = [this]()
	{
		const FMixtormatChildAddress Address = GetSelectedChildAddress();
		const FMixtormatLayerChild* Child = ResolveChildAt(Address);
		if (!Child || !Child->ScopeOwnerChildId.IsValid()) { return false; }
		const FMixtormatLayer* Layer = WorkingLayers.FindByPredicate(
			[&Address](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Address.OwnerId; });
		if (!Layer) { return false; }
		const FMixtormatLayerChild* Owner = Layer->Children.FindByPredicate(
			[Child](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.ChildId == Child->ScopeOwnerChildId;
			});
		return Owner && Owner->Type == EMixtormatLayerChildType::Generator
			&& Owner->Generator.Type == EMixtormatGeneratorType::Noise
			&& Owner->Generator.Noise.NoisePreset == EMixtormatNoisePreset::Flow;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Operation = AddCard(Panel, LOCTEXT("BehaviorFeatherOperationCard", "OPERATION"));

	// Mode is preserved solely for legacy Flow Feather; Height Feather is always Eikonal.
	AddSliderRow(Operation, SNew(SBox)
		.Visibility_Lambda([IsFlowOwner]()
		{
			return IsFlowOwner() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeTrailing(LOCTEXT("BehaviorFeatherLabel", "Mode"),
				SNew(SMixtormatSegmentedControl)
				.Options(TArray<FText>{LOCTEXT("BehaviorFeatherOption0", "Blur"), LOCTEXT("BehaviorFeatherOption1", "Eikonal")})
				.ActiveIndex_Lambda([Settings]() { const auto* F = Settings(); return F && F->Mode == EMixtormatBehaviorFeatherMode::Blur ? 0 : F && F->Mode == EMixtormatBehaviorFeatherMode::Eikonal ? 1 : 0; })
				.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, Settings](int32 Index)
				{
					if (auto* F = Settings())
					{
						if (Index == 0) { F->Mode = EMixtormatBehaviorFeatherMode::Blur; }
						if (Index == 1) { F->Mode = EMixtormatBehaviorFeatherMode::Eikonal; }
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				})))
		]);

	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehavior>(
		LOCTEXT("BehaviorFeatherStrength", "Strength"), Get,
		&FMixtormatBehavior::Strength, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("BehaviorFeatherStrengthHelp", "Feather amount. Zero preserves the native generator output and skips GPU work.")));
	AddSliderRow(Operation, MakeMemberSlider<FMixtormatBehaviorFeatherSettings>(
		LOCTEXT("BehaviorFeatherRadius", "Width (px)"), Settings,
		&FMixtormatBehaviorFeatherSettings::Radius, 0.0, 32.0, 3.0, 0.1,
		LOCTEXT("BehaviorFeatherRadiusHelp", "Eikonal transition width in output texels.")));

	TSharedRef<SVerticalBox> Eikonal = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility_Lambda([Settings, IsFlowOwner]()
		{
			if (!IsFlowOwner()) { return EVisibility::Visible; }
			const FMixtormatBehaviorFeatherSettings* F = Settings();
			return F && F->Mode == EMixtormatBehaviorFeatherMode::Eikonal
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorCard)
				.Title(LOCTEXT("BehaviorFeatherEikonalCard", "EIKONAL"))
			[
				Eikonal
			]
		]
	];
	AddSliderRow(Eikonal, MakeMemberSliderInt<FMixtormatBehaviorFeatherSettings>(
		LOCTEXT("BehaviorFeatherIterations", "Iterations"), Settings,
		&FMixtormatBehaviorFeatherSettings::Iterations, 1, 64, 32,
		LOCTEXT("BehaviorFeatherIterationsHelp", "Refinement budget for Eikonal feathering. Full-tile slope sweeps establish the base before diagonal refinements.")));

	AddSliderRow(Eikonal, SNew(SBox)
		.Visibility_Lambda([IsFlowOwner]()
		{
			return IsFlowOwner() ? EVisibility::Collapsed : EVisibility::Visible;
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MixtormatRow::MakeCaption(LOCTEXT("BehaviorFeatherSideLabel", "FEATHER SIDE"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.SegmentedControlGap)
			[
				SNew(SMixtormatSegmentedControl)
				.Options(TArray<FText>{
					LOCTEXT("BehaviorFeatherSideOption0", "Both"),
					LOCTEXT("BehaviorFeatherSideOption1", "Inside"),
					LOCTEXT("BehaviorFeatherSideOption2", "Outside")})
				.ToolTips(TArray<FText>{
					LOCTEXT("BehaviorFeatherSideOption0Hint", "Feather both sides of the boundary."),
					LOCTEXT("BehaviorFeatherSideOption1Hint", "Feather only the inside of the boundary."),
					LOCTEXT("BehaviorFeatherSideOption2Hint", "Feather only the outside of the boundary.")})
				.ActiveIndex_Lambda([Settings]()
				{
					const auto* F = Settings();
					return F && F->Side == EMixtormatBehaviorFeatherSide::Both ? 0 :
						F && F->Side == EMixtormatBehaviorFeatherSide::Inside ? 1 :
						F && F->Side == EMixtormatBehaviorFeatherSide::Outside ? 2 : 0;
				})
				.OnChosen(FMixtormatOnSegmentChosen::CreateLambda([this, Settings](int32 Index)
				{
					if (auto* F = Settings())
					{
						if (Index == 0) { F->Side = EMixtormatBehaviorFeatherSide::Both; }
						if (Index == 1) { F->Side = EMixtormatBehaviorFeatherSide::Inside; }
						if (Index == 2) { F->Side = EMixtormatBehaviorFeatherSide::Outside; }
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}))
			]
		]);
	AddSliderRow(Eikonal, MakeMemberSlider<FMixtormatBehaviorFeatherSettings>(
		LOCTEXT("BehaviorFeatherBias", "Direction Bias"), Settings,
		&FMixtormatBehaviorFeatherSettings::DirectionBias, -1.0, 1.0, 0.0, 0.01,
		LOCTEXT("BehaviorFeatherBiasHelp", "Bias Eikonal propagation along the horizontal generator axis.")));
	return SNew(SVerticalBox)
		.Visibility_Lambda([Get]() { return Get() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).IsEnabled_Lambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child && !Child->IsInstance();
			})
			[
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("BehaviorFeatherTitle", "FEATHER")).PresetAction(MakeModulePresetButton())
				.InitiallyExpanded(true)
				.HeaderAction(MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([Get]()
					{
						const FMixtormatBehavior* B = Get();
						return B && B->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}), FOnCheckStateChanged::CreateLambda([this, Get](ECheckBoxState State)
					{
						if (FMixtormatBehavior* B = Get())
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

#undef LOCTEXT_NAMESPACE
