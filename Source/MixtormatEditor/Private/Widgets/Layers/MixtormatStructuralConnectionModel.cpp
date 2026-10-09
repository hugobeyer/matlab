// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Layers/MixtormatStructuralConnectionModel.h"
#include "MixtormatParameterBinding.h"
#include "Widgets/MixtormatChildCapabilities.h"

// Preserve the existing canonical text's localization identity.
#define LOCTEXT_NAMESPACE "SMixtormat"

FText MixtormatStructuralConnections::IssueText(const EIssue Issue)
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

#undef LOCTEXT_NAMESPACE

FMixtormatStructuralConnectionContext::FMixtormatStructuralConnectionContext(
	const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& InGroups,
	const FMixtormatChildAddress& Address)
	: Groups(InGroups)
{
	using EIssue = MixtormatStructuralConnections::EIssue;
	MixtormatLayerGroups::BuildEffectiveLayers(Layers, Groups, Effective);
	if (Address.OwnerType != EMixtormatChildOwnerType::Layer || !Address.IsValid()) { return; }
	for (int32 Index = 0; Index < Effective.Num(); ++Index)
	{
		if (Effective[Index].LayerId != Address.OwnerId) { continue; }
		if (LayerIndex != INDEX_NONE)
		{
			LayerIndex = INDEX_NONE;
			AddressIssue = EIssue::DuplicateIdentity;
			return;
		}
		LayerIndex = Index;
	}
	if (!Effective.IsValidIndex(LayerIndex)) { return; }
	AddressIssue = EIssue::MissingChild;
	for (int32 Index = 0; Index < Effective[LayerIndex].Children.Num(); ++Index)
	{
		if (Effective[LayerIndex].Children[Index].ChildId != Address.ChildId) { continue; }
		if (ChildIndex != INDEX_NONE)
		{
			ChildIndex = INDEX_NONE;
			AddressIssue = EIssue::DuplicateIdentity;
			return;
		}
		ChildIndex = Index;
	}
	if (ChildIndex != INDEX_NONE)
	{
		AddressIssue = EIssue::None;
		ResolvedDestination = Effective[LayerIndex];
		MixtormatParameterBinding::ApplyDirectReferences(
			FMixtormatBindingScope{Effective, Groups}, ResolvedDestination);
	}
}

const FMixtormatLayer* FMixtormatStructuralConnectionContext::GetResolvedDestination() const
{
	return AddressIssue == MixtormatStructuralConnections::EIssue::None ? &ResolvedDestination : nullptr;
}

MixtormatOutputReferences::FStructuralLinkStatus FMixtormatStructuralConnectionContext::Evaluate(
	const FMixtormatOutputReference* Source, const FGuid* Target) const
{
	if (AddressIssue != MixtormatStructuralConnections::EIssue::None)
	{
		MixtormatOutputReferences::FStructuralLinkStatus Status;
		Status.ModuleIssue = AddressIssue;
		return Status;
	}
	if (!Source && !Target)
	{
		return MixtormatOutputReferences::EvaluateStructuralLinkForGather(
			Effective, LayerIndex, ChildIndex, ResolvedDestination);
	}
	// Only the destination copy receives bindings/instances. Sources and Warp targets
	// retain the raw effective view, while Push targets use the resolved destination.
	FMixtormatLayer Resolved = Effective[LayerIndex];
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
	MixtormatParameterBinding::ApplyDirectReferences(FMixtormatBindingScope{Effective, Groups}, Resolved);
	return MixtormatOutputReferences::EvaluateStructuralLinkForGather(Effective, LayerIndex, ChildIndex, Resolved);
}

TArray<FMixtormatStructuralSourceCandidate> FMixtormatStructuralConnectionContext::CollectSources(
	const TArray<FMixtormatLayer>& AuthoredLayers, const FMixtormatOutputReference& CurrentSource) const
{
	using EIssue = MixtormatStructuralConnections::EIssue;
	TArray<FMixtormatStructuralSourceCandidate> Candidates;
	if (AddressIssue != EIssue::None) { return Candidates; }
	const EMixtormatLayerChildType ModuleType = Effective[LayerIndex].Children[ChildIndex].Type;
	const bool bPush = ModuleType == EMixtormatLayerChildType::HeightPush;
	if (!bPush && ModuleType != EMixtormatLayerChildType::StructuralWarp) { return Candidates; }
	for (int32 AuthoredLayerIndex = 0; AuthoredLayerIndex < AuthoredLayers.Num(); ++AuthoredLayerIndex)
	{
		const FMixtormatLayer& Layer = AuthoredLayers[AuthoredLayerIndex];
		for (int32 AuthoredChildIndex = 0; AuthoredChildIndex < Layer.Children.Num(); ++AuthoredChildIndex)
		{
			const FMixtormatLayerChild& Child = Layer.Children[AuthoredChildIndex];
			FMixtormatOutputReference Source = CurrentSource;
			Source.bEnabled = true;
			Source.SourceLayerId = Layer.LayerId;
			Source.SourceChildId = Child.ChildId;
			const auto AddCandidate = [&]()
			{
				FMixtormatStructuralSourceCandidate Candidate;
				Candidate.SourceAddress.OwnerType = EMixtormatChildOwnerType::Layer;
				Candidate.SourceAddress.OwnerId = Layer.LayerId;
				Candidate.SourceAddress.ChildId = Child.ChildId;
				Candidate.Source = Source;
				Candidate.LayerIndex = AuthoredLayerIndex;
				Candidate.ChildIndex = AuthoredChildIndex;
				Candidate.Status = Evaluate(&Source);
				// Disabled destinations remain configurable; producer inactivity is an edge issue.
				Candidate.Issue = Candidate.Status.ModuleIssue != EIssue::None
					&& Candidate.Status.ModuleIssue != EIssue::DisabledLayer
					? Candidate.Status.ModuleIssue : Candidate.Status.Source.Issue;
				Candidates.Add(MoveTemp(Candidate));
			};
			if (bPush)
			{
				if (Child.Type != EMixtormatLayerChildType::Generator) { continue; }
				Source.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
				Source.OutputName = TEXT("Height");
				AddCandidate();
				continue;
			}
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Child);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || (Output.FieldKind != EMixtormatPublishedFieldKind::Flow
					&& Output.FieldKind != EMixtormatPublishedFieldKind::UVMap)) { continue; }
				Source.Kind = Output.FieldKind;
				Source.OutputName = Output.Name;
				AddCandidate();
			}
		}
	}
	return Candidates;
}
