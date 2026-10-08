// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

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
		LOCTEXT("StructuralConnectionReason", "{0} — {1}"), Label, ConnectionIssueText(Edge.Issue));
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
			const FText Name = FText::Format(LOCTEXT("StructuralMenuOrigin", "{0} / {1}"),
				Layer.DisplayName, GetStructuralChildLabel(Layer, Index));
			const auto AddEntry = [&](const FMixtormatOutputReference& Source, const FGuid Target, const FText& Label)
			{
				FEntry Entry;
				Entry.Source = Source;
				Entry.Target = Target;
				Entry.Label = Label;
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
				AddEntry(Source, FGuid(), FText::Format(LOCTEXT("StructuralMenuHeight", "{0} · Height"), Name));
				continue;
			}
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Candidate);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || (Output.FieldKind != EMixtormatPublishedFieldKind::Flow
					&& Output.FieldKind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
				Source.Kind = Output.FieldKind;
				Source.OutputName = Output.Name;
				AddEntry(Source, FGuid(), FText::Format(LOCTEXT("StructuralMenuOutput", "{0} · {1}"), Name, Output.Label));
			}
		}
	}
	// Preserve authored order within each partition; unavailable choices explain their reason.
	for (const bool bAvailable : {true, false})
	{
		for (const FEntry& Entry : Entries)
		{
			if ((Entry.Issue == EIssue::None) != bAvailable) { continue; }
			const FText Label = bAvailable ? Entry.Label : FText::Format(
				LOCTEXT("StructuralConnectionReason", "{0} — {1}"), Entry.Label, ConnectionIssueText(Entry.Issue));
			Menu.Item(Label, MixtormatIcons::Generator(), FSimpleDelegate::CreateLambda([this, Address, Role, Entry]()
			{
				SetStructuralConnection(Address, Role, Role == ERole::Source ? &Entry.Source : nullptr,
					Role == ERole::Target ? &Entry.Target : nullptr);
			})).Enabled(bEditable && bAvailable);
		}
	}
	return Menu.Build();
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
