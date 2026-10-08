// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatTypography.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	using EIssue = MixtormatOutputReferences::EStructuralLinkIssue;
	using ERole = EMixtormatStructuralConnectionRole;

	bool IsStructuralModule(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::HeightPush
			|| Child.Type == EMixtormatLayerChildType::StructuralWarp;
	}

	FText ConnectionIssueText(const EIssue Issue)
	{
		switch (Issue)
		{
		case EIssue::None: return FText::GetEmpty();
		case EIssue::Unset: return LOCTEXT("StructuralUnset", "No connection selected");
		case EIssue::MissingLayer: return LOCTEXT("StructuralMissingLayer", "Referenced layer no longer exists");
		case EIssue::MissingChild: return LOCTEXT("StructuralMissingChild", "Referenced child no longer exists");
		case EIssue::DuplicateIdentity: return LOCTEXT("StructuralDuplicate", "Duplicate identity is ambiguous");
		case EIssue::DisabledLayer: return LOCTEXT("StructuralDisabledLayer", "Layer is disabled");
		case EIssue::DisabledSource: return LOCTEXT("StructuralDisabledSource", "Source or its owner is disabled");
		case EIssue::DisabledTarget: return LOCTEXT("StructuralDisabledTarget", "Target is disabled");
		case EIssue::DisabledReference: return LOCTEXT("StructuralDisabledReference", "Source reference is disabled");
		case EIssue::WrongOwnerLayer: return LOCTEXT("StructuralWrongLayer", "Requires a Generator layer");
		case EIssue::WrongModuleType: return LOCTEXT("StructuralWrongModule", "Requires Height Push or Structural Warp");
		case EIssue::ScopedModule: return LOCTEXT("StructuralScopedModule", "Module must be an unscoped sibling");
		case EIssue::WrongSourceKind: return LOCTEXT("StructuralSourceKind", "Requires signed Height for Push or typed Flow / UV Map for Warp");
		case EIssue::WrongSourceScope: return LOCTEXT("StructuralSourceScope", "Requires an unscoped Height generator or generator-owned Flow / UV Map effect");
		case EIssue::IncompleteSourceScope: return LOCTEXT("StructuralIncompleteScope", "Move the module after the complete source scope");
		case EIssue::InvalidSourceScope: return LOCTEXT("StructuralInvalidScope", "Source scope or generator owner is invalid");
		case EIssue::ForwardSource: return LOCTEXT("StructuralForwardSource", "Source must finish before the module");
		case EIssue::ForwardTarget: return LOCTEXT("StructuralForwardTarget", "Target must be after the module");
		case EIssue::WrongTargetKind: return LOCTEXT("StructuralTargetKind", "Push requires Strata; Warp requires a generator");
		case EIssue::ScopedTarget: return LOCTEXT("StructuralScopedTarget", "Target must be unscoped");
		case EIssue::UnavailableEffectAsset: return LOCTEXT("StructuralMissingEffect", "Source effect asset is unavailable");
		}
		return LOCTEXT("StructuralUnsupported", "Unsupported structural connection");
	}

	FText ConnectionMenuReason(const EIssue Issue)
	{
		switch (Issue)
		{
		case EIssue::None: return FText::GetEmpty();
		case EIssue::Unset: return LOCTEXT("StructuralMenuUnset", "Not connected");
		case EIssue::MissingLayer: return LOCTEXT("StructuralMenuMissingLayer", "Missing layer");
		case EIssue::MissingChild: return LOCTEXT("StructuralMenuMissingChild", "Missing child");
		case EIssue::DuplicateIdentity: return LOCTEXT("StructuralMenuDuplicate", "Ambiguous ID");
		case EIssue::DisabledLayer: return LOCTEXT("StructuralMenuDisabledLayer", "Layer disabled");
		case EIssue::DisabledSource: return LOCTEXT("StructuralMenuDisabledSource", "Source disabled");
		case EIssue::DisabledTarget: return LOCTEXT("StructuralMenuDisabledTarget", "Target disabled");
		case EIssue::DisabledReference: return LOCTEXT("StructuralMenuDisabledReference", "Link disabled");
		case EIssue::WrongOwnerLayer: return LOCTEXT("StructuralMenuWrongLayer", "Generator layer only");
		case EIssue::WrongModuleType: return LOCTEXT("StructuralMenuWrongModule", "Wrong module");
		case EIssue::ScopedModule: return LOCTEXT("StructuralMenuScopedModule", "Module scoped");
		case EIssue::WrongSourceKind: return LOCTEXT("StructuralMenuSourceKind", "Wrong output");
		case EIssue::WrongSourceScope: return LOCTEXT("StructuralMenuSourceScope", "Wrong source scope");
		case EIssue::IncompleteSourceScope: return LOCTEXT("StructuralMenuIncompleteScope", "Scope unfinished");
		case EIssue::InvalidSourceScope: return LOCTEXT("StructuralMenuInvalidScope", "Invalid scope");
		case EIssue::ForwardSource: return LOCTEXT("StructuralMenuForwardSource", "After module");
		case EIssue::ForwardTarget: return LOCTEXT("StructuralMenuForwardTarget", "Before module");
		case EIssue::WrongTargetKind: return LOCTEXT("StructuralMenuTargetKind", "Wrong target type");
		case EIssue::ScopedTarget: return LOCTEXT("StructuralMenuScopedTarget", "Target scoped");
		case EIssue::UnavailableEffectAsset: return LOCTEXT("StructuralMenuMissingEffect", "Missing effect");
		}
		return LOCTEXT("StructuralMenuUnsupported", "Unsupported");
	}

	// Prepare one raw effective projection per menu. Only the destination copy receives
	// bindings/instances, matching Gather's distinct source/Warp-target and Push-target views.
	struct FConnectionProjection
	{
		TArray<FMixtormatLayer> Effective;
		const TArray<FMixtormatLayerGroup>& Groups;
		int32 LayerIndex = INDEX_NONE;
		int32 ChildIndex = INDEX_NONE;

		FConnectionProjection(const TArray<FMixtormatLayer>& Layers,
			const TArray<FMixtormatLayerGroup>& InGroups, const FMixtormatChildAddress& Address)
			: Groups(InGroups)
		{
			MixtormatLayerGroups::BuildEffectiveLayers(Layers, Groups, Effective);
			if (Address.OwnerType != EMixtormatChildOwnerType::Layer || !Address.IsValid()) { return; }
			LayerIndex = Effective.IndexOfByPredicate([&](const FMixtormatLayer& Layer)
				{ return Layer.LayerId == Address.OwnerId; });
			if (!Effective.IsValidIndex(LayerIndex)) { return; }
			ChildIndex = Effective[LayerIndex].Children.IndexOfByPredicate([&](const FMixtormatLayerChild& Child)
				{ return Child.ChildId == Address.ChildId; });
		}

		MixtormatOutputReferences::FStructuralLinkStatus Evaluate(
			const FMixtormatOutputReference* Source = nullptr, const FGuid* Target = nullptr) const
		{
			if (!Effective.IsValidIndex(LayerIndex))
			{
				MixtormatOutputReferences::FStructuralLinkStatus Status;
				Status.ModuleIssue = EIssue::MissingLayer;
				return Status;
			}
			FMixtormatLayer Resolved = Effective[LayerIndex];
			if (Resolved.Children.IsValidIndex(ChildIndex))
			{
				FMixtormatLayerChild& Module = Resolved.Children[ChildIndex];
				if (Module.Type == EMixtormatLayerChildType::HeightPush)
				{
					if (Source) { Module.HeightPush.Source = *Source; }
					if (Target) { Module.HeightPush.TargetChildId = *Target; }
				}
				else if (Module.Type == EMixtormatLayerChildType::StructuralWarp)
				{
					if (Source) { Module.StructuralWarp.Source = *Source; }
					if (Target) { Module.StructuralWarp.TargetChildId = *Target; }
				}
			}
			MixtormatParameterBinding::ApplyDirectReferences(FMixtormatBindingScope{Effective, Groups}, Resolved);
			return MixtormatOutputReferences::EvaluateStructuralLinkForGather(Effective, LayerIndex, ChildIndex, Resolved);
		}
	};

	FText ConnectionIssueCode(const EIssue Issue)
	{
		switch (Issue)
		{
		case EIssue::ForwardSource:
		case EIssue::ForwardTarget: return LOCTEXT("StructuralOrderCode", "ORDER");
		case EIssue::WrongSourceKind:
		case EIssue::WrongTargetKind: return LOCTEXT("StructuralTypeCode", "TYPE");
		case EIssue::WrongSourceScope:
		case EIssue::IncompleteSourceScope:
		case EIssue::InvalidSourceScope:
		case EIssue::ScopedModule:
		case EIssue::ScopedTarget: return LOCTEXT("StructuralScopeCode", "SCOPE");
		case EIssue::DisabledLayer:
		case EIssue::DisabledReference:
		case EIssue::DisabledSource:
		case EIssue::DisabledTarget: return LOCTEXT("StructuralOffCode", "OFF");
		case EIssue::None:
		case EIssue::Unset: return FText::GetEmpty();
		default: return LOCTEXT("StructuralBrokenCode", "BROKEN");
		}
	}

	EIssue ConnectionIssue(const MixtormatOutputReferences::FStructuralLinkStatus& Status, const ERole Role)
	{
		// Disabled modules/layers can still be configured. Producer inactivity is an edge issue.
		if (Status.ModuleIssue != EIssue::None && Status.ModuleIssue != EIssue::DisabledLayer)
		{
			return Status.ModuleIssue;
		}
		return Role == ERole::Target ? Status.Target.Issue : Status.Source.Issue;
	}
}

FText SMixtormat::GetStructuralChildLabel(const FMixtormatLayer& Layer, const int32 ChildIndex) const
{
	if (!Layer.Children.IsValidIndex(ChildIndex)) { return LOCTEXT("StructuralUnavailable", "Unavailable"); }
	const FText Name = GetLayerChildName(Layer.Children[ChildIndex]);
	int32 Matches = 0;
	int32 Ordinal = 0;
	for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
	{
		if (!GetLayerChildName(Layer.Children[Index]).EqualTo(Name)) { continue; }
		++Matches;
		if (Index <= ChildIndex) { ++Ordinal; }
	}
	return Matches > 1 ? FText::Format(LOCTEXT("StructuralChildOrdinal", "{0} {1}"), Name, FText::AsNumber(Ordinal)) : Name;
}

FText SMixtormat::GetStructuralConnectionLabel(const FMixtormatChildAddress Address, const ERole Role) const
{
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!Module || !IsStructuralModule(*Module)) { return FText::GetEmpty(); }
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference& Source = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	const FGuid Target = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	if (Role == ERole::Target ? !Target.IsValid() : !Source.SourceLayerId.IsValid() || !Source.SourceChildId.IsValid())
	{
		return LOCTEXT("StructuralConnectionNone", "None");
	}
	const FConnectionProjection Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto Status = Projection.Evaluate();
	const auto& Edge = Role == ERole::Target ? Status.Target : Status.Source;
	if (!Projection.Effective.IsValidIndex(Edge.LayerIndex)
		|| !Projection.Effective[Edge.LayerIndex].Children.IsValidIndex(Edge.ChildIndex))
	{
		const EIssue Issue = ConnectionIssue(Status, Role);
		return FText::Format(LOCTEXT("StructuralConnectionReason", "{0} — {1}"),
			LOCTEXT("StructuralUnavailable", "Unavailable"), ConnectionIssueText(Issue));
	}
	const FMixtormatLayer& Layer = Projection.Effective[Edge.LayerIndex];
	FText Label = GetStructuralChildLabel(Layer, Edge.ChildIndex);
	if (Role == ERole::Source)
	{
		if (Layer.LayerId != Address.OwnerId)
		{
			Label = FText::Format(LOCTEXT("StructuralOriginLabel", "{0} / {1}"), Layer.DisplayName, Label);
		}
		const FText Output = Source.Kind == EMixtormatPublishedFieldKind::ScalarSigned && Source.OutputName == TEXT("Height")
			? LOCTEXT("StructuralHeightOutput", "Height")
			: StaticEnum<EMixtormatPublishedFieldKind>()->GetDisplayNameTextByValue(static_cast<int64>(Source.Kind));
		Label = FText::Format(LOCTEXT("StructuralOutputLabel", "{0} · {1}"), Label, Output);
	}
	return Edge.Issue == EIssue::None ? Label : FText::Format(
		LOCTEXT("StructuralConnectionReason", "{0} · {1} — {2}"),
		ConnectionIssueCode(Edge.Issue), Label, ConnectionIssueText(Edge.Issue));
}

TSharedRef<SWidget> SMixtormat::BuildStructuralConnectionMenu(const FMixtormatChildAddress Address, const ERole Role)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (Address.OwnerType != EMixtormatChildOwnerType::Layer || !Module || !IsStructuralModule(*Module)) { return Menu.Build(); }
	const FConnectionProjection Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto CurrentStatus = Projection.Evaluate();
	const bool bEditable = !Module->IsInstance() && !Module->ScopeOwnerChildId.IsValid()
		&& (CurrentStatus.ModuleIssue == EIssue::None || CurrentStatus.ModuleIssue == EIssue::DisabledLayer);
	Menu.Item(LOCTEXT("StructuralConnectionNone", "None"), nullptr,
		FSimpleDelegate::CreateLambda([this, Address, Role]() { SetStructuralConnection(Address, Role); })).Enabled(bEditable);
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference CurrentSource = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	struct FEntry
	{
		FText Label;
		FText LayerLabel;
		FGuid LayerId;
		FMixtormatOutputReference Source;
		FGuid Target;
		EIssue Issue = EIssue::None;
	};
	TArray<FEntry> Entries;
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Role == ERole::Target && Layer.LayerId != Address.OwnerId) { continue; }
		for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& Candidate = Layer.Children[Index];
			const FText Name = GetStructuralChildLabel(Layer, Index);
			const auto AddEntry = [&](const FMixtormatOutputReference& Source, const FGuid Target, const FText& Label)
			{
				FEntry Entry;
				Entry.Source = Source;
				Entry.Target = Target;
				Entry.Label = Label;
				Entry.LayerLabel = Layer.DisplayName;
				Entry.LayerId = Layer.LayerId;
				Entry.Issue = ConnectionIssue(Projection.Evaluate(Role == ERole::Source ? &Source : nullptr,
					Role == ERole::Target ? &Target : nullptr), Role);
				Entries.Add(MoveTemp(Entry));
			};
			if (Role == ERole::Target)
			{
				if (Candidate.Type == EMixtormatLayerChildType::Generator) { AddEntry(CurrentSource, Candidate.ChildId, Name); }
				continue;
			}
			FMixtormatOutputReference Source = CurrentSource;
			Source.bEnabled = true;
			Source.SourceLayerId = Layer.LayerId;
			Source.SourceChildId = Candidate.ChildId;
			if (bPush)
			{
				if (Candidate.Type != EMixtormatLayerChildType::Generator) { continue; }
				Source.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
				Source.OutputName = TEXT("Height");
				AddEntry(Source, FGuid(), Name);
				continue;
			}
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Candidate);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || (Output.FieldKind != EMixtormatPublishedFieldKind::Flow
					&& Output.FieldKind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
				Source.Kind = Output.FieldKind;
				Source.OutputName = Output.Name;
				const FText OutputLabel = Output.FieldKind == EMixtormatPublishedFieldKind::Flow
					? LOCTEXT("StructuralMenuFlow", "Flow") : LOCTEXT("StructuralMenuUV", "UV");
				AddEntry(Source, FGuid(), FText::Format(LOCTEXT("StructuralMenuOutput", "{0} · {1}"), Name, OutputLabel));
			}
		}
	}
	// Keep eligible choices first; layer captions avoid repeating long origin paths on every row.
	for (const bool bAvailable : {true, false})
	{
		FGuid LastLayerId;
		bool bStarted = false;
		for (const FEntry& Entry : Entries)
		{
			if ((Entry.Issue == EIssue::None) != bAvailable) { continue; }
			if (!bStarted && !bAvailable)
			{
				Menu.Separator();
				Menu.Caption(LOCTEXT("StructuralMenuUnavailable", "Unavailable"));
			}
			bStarted = true;
			if (Role == ERole::Source && Entry.LayerId != LastLayerId)
			{
				Menu.Caption(Entry.LayerId == Address.OwnerId
					? LOCTEXT("StructuralMenuThisLayer", "This layer") : Entry.LayerLabel);
				LastLayerId = Entry.LayerId;
			}
			const FText Label = bAvailable ? Entry.Label : FText::Format(
				LOCTEXT("StructuralMenuShortReason", "{0} · {1}"), Entry.Label, ConnectionMenuReason(Entry.Issue));
			Menu.Item(Label, MixtormatIcons::Generator(), FSimpleDelegate::CreateLambda([this, Address, Role, Entry]()
			{
				SetStructuralConnection(Address, Role, Role == ERole::Source ? &Entry.Source : nullptr,
					Role == ERole::Target ? &Entry.Target : nullptr);
			})).Enabled(bEditable && bAvailable);
		}
	}
	return Menu.Build();
}

EStructuralLinkHighlightRole SMixtormat::GetStructuralHighlightRole(const FMixtormatChildAddress Address) const
{
	const FMixtormatChildAddress SelectedAddress = GetSelectedChildAddress();
	const FMixtormatLayerChild* Module = ResolveChildAt(SelectedAddress);
	if (!Module || !IsStructuralModule(*Module)) { return EStructuralLinkHighlightRole::None; }
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference& Source = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	const FGuid TargetId = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	const bool bSource = Source.SourceLayerId.IsValid() && Source.SourceChildId.IsValid()
		&& Address.OwnerId == Source.SourceLayerId && Address.ChildId == Source.SourceChildId;
	const bool bTarget = TargetId.IsValid() && Address.OwnerType == SelectedAddress.OwnerType
		&& Address.OwnerId == SelectedAddress.OwnerId && Address.ChildId == TargetId;
	if (bSource && bTarget) { return EStructuralLinkHighlightRole::Both; }
	if (bSource) { return EStructuralLinkHighlightRole::Source; }
	return bTarget ? EStructuralLinkHighlightRole::Target : EStructuralLinkHighlightRole::None;
}

bool SMixtormat::IsSelectedStructuralSourceLayer(const FGuid LayerId, const FGuid GroupId) const
{
	const FMixtormatLayerChild* Module = ResolveChildAt(GetSelectedChildAddress());
	if (!Module || !IsStructuralModule(*Module)) { return false; }
	const FMixtormatOutputReference& Source = Module->Type == EMixtormatLayerChildType::HeightPush
		? Module->HeightPush.Source : Module->StructuralWarp.Source;
	FMixtormatChildAddress SourceAddress;
	SourceAddress.OwnerId = Source.SourceLayerId;
	SourceAddress.ChildId = Source.SourceChildId;
	SourceAddress.OwnerType = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Source.SourceLayerId)
		? EMixtormatChildOwnerType::Group : EMixtormatChildOwnerType::Layer;
	return SourceAddress.IsValid() && ResolveChildAt(SourceAddress)
		&& (Source.SourceLayerId == LayerId || (GroupId.IsValid() && Source.SourceLayerId == GroupId));
}

FText SMixtormat::GetStructuralIncomingCountLabel(const int32 LayerIndex, const int32 ChildIndex) const
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Generator)
	{
		return FText::GetEmpty();
	}
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (const TArray<FText>* Cached = StructuralIncomingCountLabels.Find(Layer.LayerId))
	{
		return Cached->IsValidIndex(ChildIndex) ? (*Cached)[ChildIndex] : FText::GetEmpty();
	}
	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(WorkingLayers, WorkingLayerGroups, Effective);
	FMixtormatLayer Resolved = Effective[LayerIndex];
	MixtormatParameterBinding::ApplyDirectReferences(FMixtormatBindingScope{Effective, WorkingLayerGroups}, Resolved);
	TArray<int32> PushCounts;
	TArray<int32> WarpCounts;
	PushCounts.Init(0, Layer.Children.Num());
	WarpCounts.Init(0, Layer.Children.Num());
	for (int32 Index = 0; Index < Resolved.Children.Num(); ++Index)
	{
		const FMixtormatLayerChild& Module = Resolved.Children[Index];
		if (!IsStructuralModule(Module)) { continue; }
		const auto Status = MixtormatOutputReferences::EvaluateStructuralLinkForGather(Effective, LayerIndex, Index, Resolved);
		const int32 TargetIndex = Status.Target.ChildIndex;
		if (!Status.bCanExecuteStructurally || !Layer.Children.IsValidIndex(TargetIndex)
			|| Layer.Children[TargetIndex].Type != EMixtormatLayerChildType::Generator) { continue; }
		const FGuid TargetId = Module.Type == EMixtormatLayerChildType::HeightPush
			? Module.HeightPush.TargetChildId : Module.StructuralWarp.TargetChildId;
		if (TargetId != Layer.Children[TargetIndex].ChildId) { continue; }
		if (Module.Type == EMixtormatLayerChildType::HeightPush) { ++PushCounts[TargetIndex]; }
		else { ++WarpCounts[TargetIndex]; }
	}
	TArray<FText> Labels;
	Labels.SetNum(Layer.Children.Num());
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		const int32 PushCount = PushCounts[Index];
		const int32 WarpCount = WarpCounts[Index];
		if (PushCount == 0 && WarpCount == 0) { continue; }
		if (PushCount == 0) { Labels[Index] = FText::Format(LOCTEXT("StructuralWarpCount", "{0} WARP"), FText::AsNumber(WarpCount)); }
		else if (WarpCount == 0) { Labels[Index] = FText::Format(LOCTEXT("StructuralPushCount", "{0} PUSH"), FText::AsNumber(PushCount)); }
		else
		{
			Labels[Index] = FText::Format(LOCTEXT("StructuralBothCount", "{0} PUSH · {1} WARP"),
				FText::AsNumber(PushCount), FText::AsNumber(WarpCount));
		}
	}
	const FText Label = Labels[ChildIndex];
	StructuralIncomingCountLabels.Add(Layer.LayerId, MoveTemp(Labels));
	return Label;
}

TSharedRef<SWidget> SMixtormat::BuildStructuralLinkChips(const FMixtormatChildAddress Address)
{
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!Module || (Module->Type != EMixtormatLayerChildType::HeightPush
		&& Module->Type != EMixtormatLayerChildType::StructuralWarp))
	{
		return SNullWidget::NullWidget;
	}
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle ArrowStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerSource),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const auto MakeConnectionChip = [this, Address](const ERole Role)
	{
		// The list is rebuilt on authored edits, so resolve once here rather than copying the
		// effective projection and validating/loading a source asset on every Slate attribute tick.
		const FText Label = GetStructuralConnectionLabel(Address, Role);
		return SNew(SBox)
			.MinDesiredWidth(MixtormatTokens::StructuralLinkChipMinWidth)
			.MaxDesiredWidth(MixtormatTokens::StructuralLinkChipMaxWidth)
			[
				SNew(SMixtormatBadge)
				.bAutoWidth(true)
				.Text(Label)
				.ToolTip(Label)
				.OnGetMenuContent_Lambda([this, Address, Role]()
				{
					return BuildStructuralConnectionMenu(Address, Role);
				})
			];
	};
	const ERole SourceRole = ERole::Source;
	const ERole TargetRole = ERole::Target;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeConnectionChip(SourceRole)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(MixtormatTokens::StructuralLinkArrowGap, 0.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(ArrowStyle.Font)
				.ColorAndOpacity(ArrowStyle.ColorAndOpacity)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeConnectionChip(TargetRole)
		];
}

FReply SMixtormat::SetStructuralConnection(const FMixtormatChildAddress Address, const ERole Role,
	const FMixtormatOutputReference* Source, const FGuid* TargetId)
{
	FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!bHasWorkingMaterial || Address.OwnerType != EMixtormatChildOwnerType::Layer
		|| !Module || !IsStructuralModule(*Module) || Module->IsInstance() || Module->ScopeOwnerChildId.IsValid())
	{
		return FReply::Unhandled();
	}
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	FMixtormatOutputReference& CurrentSource = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	FGuid& CurrentTarget = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	FMixtormatOutputReference ProposedSource = CurrentSource;
	const FGuid ProposedTarget = TargetId ? *TargetId : FGuid();
	if (Role == ERole::Source)
	{
		if (Source)
		{
			// A connection changes its typed address, not the module's authored trace settings.
			ProposedSource.bEnabled = true;
			ProposedSource.SourceLayerId = Source->SourceLayerId;
			ProposedSource.SourceChildId = Source->SourceChildId;
			ProposedSource.Kind = Source->Kind;
			ProposedSource.OutputName = Source->OutputName;
		}
		else
		{
			ProposedSource.SourceLayerId.Invalidate();
			ProposedSource.SourceChildId.Invalidate();
		}
	}
	const FConnectionProjection Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto Status = Projection.Evaluate(Role == ERole::Source ? &ProposedSource : nullptr,
		Role == ERole::Target ? &ProposedTarget : nullptr);
	const EIssue Issue = ConnectionIssue(Status, Role);
	const bool bDisconnect = Role == ERole::Source ? Source == nullptr : TargetId == nullptr;
	if ((Issue != EIssue::None && !(bDisconnect && Issue == EIssue::Unset))
		|| (Status.ModuleIssue != EIssue::None && Status.ModuleIssue != EIssue::DisabledLayer))
	{
		WorkingStatusText = ConnectionIssueText(Issue).ToString();
		return FReply::Handled();
	}
	const bool bUnchanged = Role == ERole::Target ? CurrentTarget == ProposedTarget
		: CurrentSource.SourceLayerId == ProposedSource.SourceLayerId && CurrentSource.SourceChildId == ProposedSource.SourceChildId
			&& CurrentSource.Kind == ProposedSource.Kind && CurrentSource.OutputName == ProposedSource.OutputName
			&& CurrentSource.bEnabled == ProposedSource.bEnabled;
	if (bUnchanged) { return FReply::Handled(); }
	if (Role == ERole::Target) { CurrentTarget = ProposedTarget; }
	else { CurrentSource = ProposedSource; }
	// Synchronize inherited payloads before capturing the single committed history state.
	RefreshLayeredPreview(false);
	// Discrete connection edits must not coalesce with the preceding or following slider edit.
	LastHistoryRecordTime = 0.0;
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
	SyncSelectedLayerControls();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
