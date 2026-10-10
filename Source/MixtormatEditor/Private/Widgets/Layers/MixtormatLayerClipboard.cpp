// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatOutputReference.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace MixtormatLayersPrivate
{

	TArray<FMixtormatLayerChild> CopyChildSubtree(TArray<FMixtormatLayerChild> Copies,
		const FGuid OldOwnerId, const FGuid NewOwnerId)
	{
		TMap<FGuid, FGuid> ChildIdRemap;
		for (FMixtormatLayerChild& Copy : Copies)
		{
			const FGuid OldId = Copy.ChildId;
			Copy.SourceLayerId.Invalidate();
			Copy.SourceChildId.Invalidate();
			MixtormatParameterBinding::RegenerateChildIdentity(Copy);
			ChildIdRemap.Add(OldId, Copy.ChildId);
		}
		const auto RemapPair = [&ChildIdRemap, OldOwnerId, NewOwnerId](FGuid& OwnerId, FGuid& ChildId)
		{
			if (OwnerId == OldOwnerId)
			{
				if (const FGuid* NewId = ChildIdRemap.Find(ChildId))
				{
					OwnerId = NewOwnerId;
					ChildId = *NewId;
				}
			}
		};
		const auto RemapOutput = [&RemapPair](FMixtormatOutputReference& Reference)
		{
			// A Shelf owner is external to a layer/group subtree even when its
			// legacy unused SourceLayerId happens to match the copied owner.
			if (Reference.IsLayerSource())
			{
				RemapPair(Reference.SourceLayerId, Reference.SourceChildId);
			}
		};
		for (FMixtormatLayerChild& Copy : Copies)
		{
			if (const FGuid* Owner = ChildIdRemap.Find(Copy.ScopeOwnerChildId))
			{
				Copy.ScopeOwnerChildId = *Owner;
			}
			// Only addresses inside the copied subtree follow it; arbitrary producers stay external.
			RemapOutput(Copy.OutputReference);
			RemapOutput(Copy.Generator.HeightSource);
			RemapOutput(Copy.Generator.WarpSource);
			// Copy typed Behavior sources with the subtree rather than retaining stale child IDs.
			RemapOutput(Copy.Behavior.Direction.Published);
			RemapOutput(Copy.Behavior.Height.Published);
			RemapOutput(Copy.Behavior.Influence.Published);
			RemapOutput(Copy.BoundaryId.RegionIdsSource);
			RemapPair(Copy.Mask.PublishedSourceLayerId, Copy.Mask.PublishedSourceChildId);
			RemapOutput(Copy.HeightPush.Source);
			RemapPair(Copy.HeightBlend.SourceLayerId, Copy.HeightBlend.SourceChildId);
			if (const FGuid* Input = ChildIdRemap.Find(Copy.HeightColorRamp.SourceChildId))
			{
				Copy.HeightColorRamp.SourceChildId = *Input;
			}
			if (const FGuid* Target = ChildIdRemap.Find(Copy.HeightPush.TargetChildId))
			{
				Copy.HeightPush.TargetChildId = *Target;
			}
			RemapOutput(Copy.StructuralWarp.Source);
			if (const FGuid* Target = ChildIdRemap.Find(Copy.StructuralWarp.TargetChildId))
			{
				Copy.StructuralWarp.TargetChildId = *Target;
			}
			for (FMixtormatParameterBinding& Binding : Copy.ParameterBindings)
			{
				RemapPair(Binding.Reference.Source.LayerId, Binding.Reference.Source.ChildId);
				RemapPair(Binding.Driver.SourceLayerId, Binding.Driver.SourceChildId);
			}
		}
		return Copies;
	}
}

void SMixtormat::CopyChild(const FMixtormatChildAddress& Address, const bool bAsInstance)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!Child)
	{
		return;
	}
	FGuid PublishedOwnerId, PublishedChildId;
	if (GetPublishedOutputSource(*Child, PublishedOwnerId, PublishedChildId)
		&& !PublishedOutputPlacementsValid(FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}))
	{
		return;
	}
	FMixtormatChildClipboard Clipboard;
	Clipboard.Mode = bAsInstance ? EMixtormatChildClipboardMode::Instance : EMixtormatChildClipboardMode::Copy;
	Clipboard.Payload = *Child;
	// Copying an instance as an instance yields its source, not the instance. Pointing at the
	// instance would build a chain for the resolver to unwind with nothing gained by it.
	if (Child->IsInstance())
	{
		Clipboard.Source.OwnerId = Child->SourceLayerId;
		Clipboard.Source.ChildId = Child->SourceChildId;
		Clipboard.Source.OwnerType = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Child->SourceLayerId)
			? EMixtormatChildOwnerType::Group
			: EMixtormatChildOwnerType::Layer;
	}
	else
	{
		Clipboard.Source = Address;
	}
	ChildClipboardScopedRows.Reset();
	if (!bAsInstance && (Child->Type == EMixtormatLayerChildType::IdGroup
		|| Child->Type == EMixtormatLayerChildType::HeightPush
		|| Child->Type == EMixtormatLayerChildType::StructuralWarp
		|| Child->Type == EMixtormatLayerChildType::Behavior))
	{
		Clipboard.Source = Address;
		const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
		const int32 Root = ResolveChildIndexAt(Address);
		for (int32 Index = Root + 1; Index < FindSubtreeEnd(*Children, Root); ++Index)
		{
			ChildClipboardScopedRows.Add((*Children)[Index]);
		}
	}
	ChildClipboard = MoveTemp(Clipboard);
}

bool SMixtormat::CanCopyChildOutput(const FMixtormatChildAddress& Address, const FName OutputName) const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
	if (!Child || !Children)
	{
		return false;
	}
	const FMixtormatChildCapabilities Capabilities = GetChildCapabilities(*Child);
	return Capabilities.Outputs.ContainsByPredicate([OutputName](const FMixtormatPublishedOutputDesc& Output)
		{
			return Output.Name == OutputName && (Output.bCopyableAsMask || Output.bCopyableAsField);
		})
		&& PublishedOutputPlacementsValid(FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups})
		&& MixtormatParameterBinding::ClassifyInstancePlacement(
			FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, Address.OwnerId, Address.ChildId,
			Address.OwnerId, FindSubtreeEnd(*Children, ResolveChildIndexAt(Address)))
			== MixtormatParameterBinding::EInstancePlacement::Valid;
}

void SMixtormat::CopyChildOutput(const FMixtormatChildAddress& Address, const FName OutputName)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	if (!CanCopyChildOutput(Address, OutputName))
	{
		return;
	}
	const FMixtormatChildCapabilities Capabilities = GetChildCapabilities(*Child);
	const FMixtormatPublishedOutputDesc* Output = Capabilities.Outputs.FindByPredicate(
		[OutputName](const FMixtormatPublishedOutputDesc& Candidate)
		{
			return Candidate.Name == OutputName && (Candidate.bCopyableAsMask || Candidate.bCopyableAsField);
		});
	if (!Output)
	{
		return;
	}

	FMixtormatLayerChild PublishedChild;
	// Preserve typed payloads when an output also supports scalar-mask consumption.
	if (Output->bCopyableAsMask && !Output->bCopyableAsField)
	{
		PublishedChild.Type = EMixtormatLayerChildType::Mask;
		PublishedChild.Mask.bEnabled = true;
		PublishedChild.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
		PublishedChild.Mask.Weight = 1.0f;
		PublishedChild.Mask.PublishedSourceLayerId = Address.OwnerId;
		PublishedChild.Mask.PublishedSourceChildId = Address.ChildId;
		PublishedChild.Mask.PublishedSourceOutput = OutputName;
	}
	else
	{
		PublishedChild.Type = EMixtormatLayerChildType::OutputReference;
		PublishedChild.OutputReference.bEnabled = true;
		PublishedChild.OutputReference.SourceLayerId = Address.OwnerId;
		PublishedChild.OutputReference.SourceChildId = Address.ChildId;
		PublishedChild.OutputReference.OutputName = OutputName;
		PublishedChild.OutputReference.Kind = Output->FieldKind;
	}

	FMixtormatChildClipboard Clipboard;
	Clipboard.Mode = EMixtormatChildClipboardMode::PublishedOutput;
	Clipboard.Payload = MoveTemp(PublishedChild);
	Clipboard.Source = Address;
	Clipboard.PublishedOutput = OutputName;
	ChildClipboardScopedRows.Reset();
	ChildClipboard = MoveTemp(Clipboard);
	WorkingStatusText = FText::Format(
		LOCTEXT("ChildOutputCopied", "{0} output copied"), Output->Label).ToString();
}

bool SMixtormat::ResolveIdGroupPasteReference(FMixtormatLayerChild& Reference) const
{
	if (!ChildClipboard.IsSet()) { return false; }
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();
	if (IsRegionIdsReference(Clipboard.Payload))
	{
		return MakeRegionIdsReference(Clipboard.Payload, Clipboard.Source, Reference);
	}
	// A producer copy/instance becomes an output address here, never a copied producer payload.
	const FMixtormatLayerChild* Source = ResolveChildAt(Clipboard.Source);
	return Source && Clipboard.Mode != EMixtormatChildClipboardMode::PublishedOutput
		&& MakeRegionIdsReference(*Source, Clipboard.Source, Reference);
}

int32 SMixtormat::ResolvePasteInsertIndex(
	const FMixtormatChildAddress& Dest,
	const int32 AnchorChildIndex,
	FGuid* OutScopeOwnerChildId) const
{
	if (OutScopeOwnerChildId)
	{
		OutScopeOwnerChildId->Invalidate();
	}
	if (!ChildClipboard.IsSet())
	{
		return INDEX_NONE;
	}
	const TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	if (!DestContainer)
	{
		return INDEX_NONE;
	}
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();
	if (Clipboard.Mode == EMixtormatChildClipboardMode::Copy
		&& Clipboard.Payload.Type == EMixtormatLayerChildType::IdGroup
		&& ChildClipboardScopedRows.ContainsByPredicate([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::HeightPush
				|| Child.Type == EMixtormatLayerChildType::StructuralWarp;
		}))
	{
		// Legacy invalid subtrees remain authored, but a paste must not create new scoped modules.
		return INDEX_NONE;
	}
	if (Clipboard.Payload.Type == EMixtormatLayerChildType::HeightPush
		|| Clipboard.Payload.Type == EMixtormatLayerChildType::StructuralWarp)
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate([&](const FMixtormatLayer& Layer)
		{
			return Layer.LayerId == Dest.OwnerId;
		});
		if (Dest.OwnerType != EMixtormatChildOwnerType::Layer || !WorkingLayers.IsValidIndex(LayerIndex)
			|| WorkingLayers[LayerIndex].Type != EMixtormatLayerType::Generator) { return INDEX_NONE; }
	}
	if (DestContainer->IsValidIndex(AnchorChildIndex)
		&& (*DestContainer)[AnchorChildIndex].Type == EMixtormatLayerChildType::IdGroup)
	{
		FMixtormatLayerChild Reference;
		if (!CanAddScopedChild(*DestContainer, AnchorChildIndex)
			|| !ResolveIdGroupPasteReference(Reference)) { return INDEX_NONE; }
		Reference.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
		const int32 Insert = FindSubtreeEnd(*DestContainer, AnchorChildIndex);
		const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
		return IsPublishedSourceEnabled(Scope, Reference.OutputReference.SourceLayerId, Reference.OutputReference.SourceChildId)
			&& CanReadPublishedOutputAt(Scope, Reference, Dest.OwnerId, Insert) ? Insert : INDEX_NONE;
	}
	const auto ValidateInsert = [this, &Clipboard, DestContainer, &Dest, AnchorChildIndex, OutScopeOwnerChildId](int32 Insert, const bool bScoped) -> int32
	{
		FMixtormatLayerChild Payload = Clipboard.Payload;
		Payload.ChildId = FGuid::NewGuid();
		Payload.ScopeOwnerChildId.Invalidate();
		Payload.SourceLayerId = Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			? Clipboard.Source.OwnerId : FGuid();
		Payload.SourceChildId = Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			? Clipboard.Source.ChildId : FGuid();
		FGuid PublishedOwnerId, PublishedChildId;
		const bool bPublished = GetPublishedOutputSource(Payload, PublishedOwnerId, PublishedChildId);
		if (!bScoped && (bPublished || Payload.Type == EMixtormatLayerChildType::HeightPush
			|| Payload.Type == EMixtormatLayerChildType::StructuralWarp)
			&& DestContainer->IsValidIndex(Insert)
			&& (*DestContainer)[Insert].ScopeOwnerChildId.IsValid())
		{
			Insert = FindSiblingRoot(*DestContainer, Insert, FGuid());
			if (Insert == INDEX_NONE)
			{
				return INDEX_NONE;
			}
		}
		if (!bScoped && bPublished && PublishedChildId.IsValid() && PublishedOwnerId == Dest.OwnerId)
		{
			const int32 SourceIndex = FindChildById(*DestContainer, PublishedChildId);
			if (SourceIndex == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			// A standalone reference must not split a producer's containing subtree.
			const int32 SourceRoot = FindSiblingRoot(*DestContainer, SourceIndex, FGuid());
			if (SourceRoot == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			Insert = FMath::Max(Insert, FindSubtreeEnd(*DestContainer, SourceRoot));
		}
		if (bScoped)
		{
			Payload.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
		}
		else if (Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			&& Payload.Type != EMixtormatLayerChildType::HeightPush
			&& Payload.Type != EMixtormatLayerChildType::StructuralWarp
			&& DestContainer->IsValidIndex(AnchorChildIndex)
			&& CanAddScopedChild(*DestContainer, AnchorChildIndex)
			&& CanKeepScopedPlacement((*DestContainer)[AnchorChildIndex], Payload))
		{
			const int32 ScopedInsert = FindSubtreeEnd(*DestContainer, AnchorChildIndex);
			if (Insert <= ScopedInsert)
			{
				Insert = ScopedInsert;
				Payload.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
			}
		}
		const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
		if (Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			&& MixtormatParameterBinding::ClassifyInstancePlacement(Scope,
				Clipboard.Source.OwnerId, Clipboard.Source.ChildId, Dest.OwnerId, Insert)
				!= MixtormatParameterBinding::EInstancePlacement::Valid)
		{
			return INDEX_NONE;
		}
		if (Clipboard.Mode == EMixtormatChildClipboardMode::Copy
			&& (Clipboard.Payload.Type == EMixtormatLayerChildType::IdGroup
				|| Clipboard.Payload.Type == EMixtormatLayerChildType::HeightPush
				|| Clipboard.Payload.Type == EMixtormatLayerChildType::Behavior
				|| Clipboard.Payload.Type == EMixtormatLayerChildType::StructuralWarp))
		{
			TArray<FMixtormatLayerChild> Copies;
			Copies.Add(Clipboard.Payload);
			Copies[0].ScopeOwnerChildId = Payload.ScopeOwnerChildId;
			Copies.Append(ChildClipboardScopedRows);
			Copies = CopyChildSubtree(MoveTemp(Copies), Clipboard.Source.OwnerId, Dest.OwnerId);
			TArray<FMixtormatLayer> Layers = WorkingLayers;
			TArray<FMixtormatLayerGroup> Groups = WorkingLayerGroups;
			TArray<FMixtormatLayerChild>* Projected = nullptr;
			if (Dest.OwnerType == EMixtormatChildOwnerType::Group)
			{
				FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, Dest.OwnerId);
				Projected = Group ? &Group->Children : nullptr;
			}
			else
			{
				FMixtormatLayer* Layer = Layers.FindByPredicate(
					[&Dest](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Dest.OwnerId; });
				Projected = Layer ? &Layer->Children : nullptr;
			}
			if (!Projected) { return INDEX_NONE; }
			for (int32 Index = 0; Index < Copies.Num(); ++Index)
			{
				Projected->Insert(Copies[Index], Insert + Index);
			}
			const FMixtormatBindingScope ProjectedScope{Layers, Groups};
			for (int32 Index = 0; Index < Copies.Num(); ++Index)
			{
				if (!CanReadPublishedOutputAt(ProjectedScope, Copies[Index], Dest.OwnerId, Insert + Index))
				{
					return INDEX_NONE;
				}
			}
		}
		if (OutScopeOwnerChildId)
		{
			*OutScopeOwnerChildId = Payload.ScopeOwnerChildId;
		}
		// Plain copies of gate/field reference nodes retain their live output dependency too.
		return CanReadPublishedOutputAt(Scope, Payload, Dest.OwnerId, Insert)
			&& (!bPublished || PublishedOutputPlacementsValid(Scope)) ? Insert : INDEX_NONE;
	};


	// V2 Behaviors may only be pasted underneath a Generator module. Do not
	// allow a copied Behavior to become an invalid root row or a group child.
	if (Clipboard.Payload.Type == EMixtormatLayerChildType::Behavior)
	{
		const int32 DestLayerIndex = WorkingLayers.IndexOfByPredicate([&Dest](const FMixtormatLayer& Layer)
		{
			return Layer.LayerId == Dest.OwnerId;
		});
		if (Dest.OwnerType != EMixtormatChildOwnerType::Layer
			|| !WorkingLayers.IsValidIndex(DestLayerIndex)
			|| WorkingLayers[DestLayerIndex].Type != EMixtormatLayerType::Generator
			|| !DestContainer->IsValidIndex(AnchorChildIndex)
			|| (*DestContainer)[AnchorChildIndex].Type != EMixtormatLayerChildType::Generator
			|| !CanAddScopedChild(*DestContainer, AnchorChildIndex))
		{
			return INDEX_NONE;
		}
		return ValidateInsert(FindSubtreeEnd(*DestContainer, AnchorChildIndex), true);
	}

	if (IsGeneratorFlow(Clipboard.Payload))
	{
		if (CanAddGeneratorFlow(Dest) && !Dest.ChildId.IsValid())
		{
			return ValidateInsert(DestContainer->Num(), false);
		}
		if (!DestContainer->IsValidIndex(AnchorChildIndex)
			|| !CanOwnGeneratorFlow((*DestContainer)[AnchorChildIndex])
			|| !CanAddScopedChild(*DestContainer, AnchorChildIndex))
		{
			return INDEX_NONE;
		}
		return ValidateInsert(FindSubtreeEnd(*DestContainer, AnchorChildIndex), true);
	}

	if (Clipboard.Mode != EMixtormatChildClipboardMode::Instance)
	{
		// Ordinary copies are detached from instance identity; live output dependencies are
		// checked below. A mask filter only ever travels scoped beneath the mask it filters
		// and can never be pasted as a standalone row.
		if (IsMaskFilter(Clipboard.Payload))
		{
			return INDEX_NONE;
		}
		return ValidateInsert(DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : DestContainer->Num(), false);
	}

	// A container's top-level paste (a layer header, or a group's own top) names no row and means
	// the top; a child row means directly above that row.
	const int32 Preferred = DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : 0;

	// A source in this same container has to stay earlier than its instance, and the source never
	// moves to make that true -- so the instance moves down instead, to the first slot after it. A
	// source in a different container is already earlier by the cross-container ordering rule below
	// and keeps the asked-for row.
	int32 Insert = Preferred;
	if (Clipboard.Source.OwnerId == Dest.OwnerId)
	{
		const int32 SourceChildIndex = DestContainer->IndexOfByPredicate(
			[&Clipboard](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == Clipboard.Source.ChildId;
			});
		if (SourceChildIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		Insert = FMath::Max(Insert, SourceChildIndex + 1);
	}

	return ValidateInsert(Insert, false);
}

bool SMixtormat::CanPasteChild(const FMixtormatChildAddress& Dest, const int32 AnchorChildIndex) const
{
	return ResolvePasteInsertIndex(Dest, AnchorChildIndex) != INDEX_NONE;
}

FText SMixtormat::GetChildPasteReason(
	const FMixtormatChildAddress& Dest,
	const int32 AnchorChildIndex) const
{
	if (!ChildClipboard.IsSet())
	{
		return LOCTEXT("PasteNothingCopied", "Nothing copied. Use Copy, Copy as Instance or Copy Output on a child first.");
	}
	if (!ResolveContainer(Dest))
	{
		return FText::GetEmpty();
	}
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Dest);
	if (Clipboard.Payload.Type == EMixtormatLayerChildType::HeightPush
		|| Clipboard.Payload.Type == EMixtormatLayerChildType::StructuralWarp)
	{
		const int32 Insert = ResolvePasteInsertIndex(Dest, AnchorChildIndex);
		if (Insert == INDEX_NONE)
		{
			return LOCTEXT("StructuralPasteBlocked", "Requires an unscoped placement in a Generator layer and valid instance ordering; shared groups are unsupported.");
		}
		const FMixtormatLayer* Layer = WorkingLayers.FindByPredicate(
			[&Dest](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Dest.OwnerId; });
		if (!Layer) { return FText::GetEmpty(); }
		TArray<FMixtormatLayer> ProjectedLayers;
		ProjectedLayers.Add(*Layer);
		FMixtormatLayerChild Module = Clipboard.Payload;
		Module.ChildId = FGuid::NewGuid();
		Module.ScopeOwnerChildId.Invalidate();
		Module.SourceLayerId = Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			? Clipboard.Source.OwnerId : FGuid();
		Module.SourceChildId = Clipboard.Mode == EMixtormatChildClipboardMode::Instance
			? Clipboard.Source.ChildId : FGuid();
		ProjectedLayers[0].Children.Insert(MoveTemp(Module), Insert);
		MixtormatParameterBinding::ApplyDirectReferences(
			FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, ProjectedLayers[0]);
		if (ProjectedLayers[0].Children[Insert].Type == EMixtormatLayerChildType::StructuralWarp)
		{
			// Gather resolves Push targets on its binding-resolved copy, but Warp targets
			// still use the effective authored payloads. Preserve that existing distinction.
			FMixtormatLayerChild ResolvedModule = ProjectedLayers[0].Children[Insert];
			ProjectedLayers[0] = *Layer;
			ProjectedLayers[0].Children.Insert(MoveTemp(ResolvedModule), Insert);
		}
		// Only diagnose the saved target here, not source availability or GPU execution.
		// An empty source override avoids resolving/loading Flow assets merely for a paste hint.
		const FMixtormatOutputReference NoSource;
		using EIssue = MixtormatOutputReferences::EStructuralLinkIssue;
		const EIssue TargetIssue = MixtormatOutputReferences::EvaluateStructuralLink(
			ProjectedLayers, 0, Insert, &NoSource).Target.Issue;
		switch (TargetIssue)
		{
		case EIssue::Unset:
			return LOCTEXT("StructuralPasteTargetUnset", "Paste with no target connected. Choose an explicit later target in the Inspector; no target is chosen automatically.");
		case EIssue::MissingChild:
			return LOCTEXT("StructuralPasteTargetMissing", "Paste with an unavailable target: its saved GUID does not identify a child in this layer. The connection is retained; reconnect explicitly in the Inspector.");
		case EIssue::ForwardTarget:
			return LOCTEXT("StructuralPasteTargetOrder", "Paste with an invalid target order: the saved target must follow the module. The connection is retained; paste before that target or reconnect explicitly.");
		case EIssue::WrongTargetKind:
			return LOCTEXT("StructuralPasteTargetType", "Paste with an incompatible target: Height Push requires Strata Carver; Structural Warp requires a generator. The connection is retained; reconnect explicitly.");
		case EIssue::DisabledTarget:
			return LOCTEXT("StructuralPasteTargetDisabled", "Paste with a disabled target. The connection is retained; it cannot contribute until the target is enabled.");
		case EIssue::ScopedTarget:
			return LOCTEXT("StructuralPasteTargetScoped", "Paste with an unsupported scoped target. The connection is retained; choose a later unscoped generator.");
		case EIssue::DuplicateIdentity:
			return LOCTEXT("StructuralPasteTargetDuplicate", "Paste with an ambiguous target GUID. The connection is retained; repair duplicate identities before reconnecting.");
		case EIssue::None:
			return LOCTEXT("StructuralPasteReady", "Place a layer-local structural module with its saved target. Source eligibility is separate; no source or target is chosen automatically.");
		default:
			return LOCTEXT("StructuralPasteTargetInvalid", "Paste with an invalid retained target connection. Repair it explicitly in the Inspector; no target is chosen automatically.");
		}
	}
	if (Container->IsValidIndex(AnchorChildIndex)
		&& (*Container)[AnchorChildIndex].Type == EMixtormatLayerChildType::IdGroup)
	{
		return CanPasteChild(Dest, AnchorChildIndex)
			? LOCTEXT("PasteIntoIdGroupReady", "Add a live Region IDs source; the producer stays where it is.")
			: LOCTEXT("PasteIntoIdGroupBlocked", "Requires available Region IDs from this owner or an earlier owner, without feedback.");
	}
	if (IsGeneratorFlow(Clipboard.Payload))
	{
		return CanPasteChild(Dest, AnchorChildIndex)
			? LOCTEXT("PasteGeneratorFlowReady", "Place under this generator.")
			: LOCTEXT("PasteGeneratorFlowOwner", "Requires a Generator layer or an eligible generator child and valid instance ordering.");
	}
	FGuid PublishedOwnerId, PublishedChildId;
	if (GetPublishedOutputSource(Clipboard.Payload, PublishedOwnerId, PublishedChildId))
	{
		return CanPasteChild(Dest, AnchorChildIndex)
			? LOCTEXT("PublishedOutputPasteReady", "Place a live reference after its published source.")
			: LOCTEXT("PublishedOutputPasteBlocked", "Requires an existing earlier source and no owner feedback cycle.");
	}
	if (Clipboard.Mode != EMixtormatChildClipboardMode::Instance)
	{
		if (IsMaskFilter(Clipboard.Payload))
		{
			return LOCTEXT("PasteMaskFilterNotStandalone", "A Blur or Curvature only travels with the mask it filters.");
		}
		return LOCTEXT("PasteReady", "Place a copy of what was copied.");
	}
	const int32 Insert = ResolvePasteInsertIndex(Dest, AnchorChildIndex);
	if (Insert != INDEX_NONE)
	{
		return LOCTEXT("InstancePasteReady", "Place a live instance of the copied child.");
	}
	const TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	using EPlacement = MixtormatParameterBinding::EInstancePlacement;
	switch (MixtormatParameterBinding::ClassifyInstancePlacement(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
		Clipboard.Source.OwnerId,
		Clipboard.Source.ChildId,
		Dest.OwnerId,
		DestContainer->IsValidIndex(AnchorChildIndex) ? AnchorChildIndex : 0))
	{
	case EPlacement::SourceMissing:
		return LOCTEXT("InstanceSourceGone", "The copied child no longer exists.");
	case EPlacement::SelfReference:
		return LOCTEXT("InstanceSelf", "A child cannot be an instance of itself.");
	default:
		return LOCTEXT(
			"InstanceOrder",
			"The source composites after this position. An instance can only read a child that resolves before it.");
	}
}

bool SMixtormat::ResolveGatingMaskPayload(FMixtormatLayerChild& Payload) const
{
	if (!ChildClipboard.IsSet()) { return false; }
	const FMixtormatChildClipboard& Clipboard = ChildClipboard.GetValue();
	if (Clipboard.Mode == EMixtormatChildClipboardMode::Instance) { return false; }
	Payload = Clipboard.Payload;
	if (Payload.Type == EMixtormatLayerChildType::Mask) { return true; }
	// Only the gating gesture consumes a typed scalar as coverage. Ordinary Paste keeps
	// the original typed output reference, including Flow/UV trace and field semantics.
	if (Clipboard.Mode != EMixtormatChildClipboardMode::PublishedOutput
		|| Payload.Type != EMixtormatLayerChildType::OutputReference) { return false; }
	const FMixtormatOutputReference Reference = Payload.OutputReference;
	const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, Reference.SourceLayerId, Reference.SourceChildId);
	if (!Source) { return false; }
	const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Source);
	if (!Caps.Outputs.ContainsByPredicate([&Reference](const FMixtormatPublishedOutputDesc& Output)
		{
			return Output.Name == Reference.OutputName && Output.bCopyableAsMask
				&& Output.FieldKind == Reference.Kind;
		})) { return false; }
	Payload = FMixtormatLayerChild();
	Payload.Type = EMixtormatLayerChildType::Mask;
	Payload.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	Payload.Mask.PublishedSourceLayerId = Reference.SourceLayerId;
	Payload.Mask.PublishedSourceChildId = Reference.SourceChildId;
	Payload.Mask.PublishedSourceOutput = Reference.OutputName;
	return true;
}

bool SMixtormat::CanPasteAsGatingMask(const FMixtormatChildAddress& Address) const
{
	if (!ChildClipboard.IsSet())
	{
		return false;
	}
	FMixtormatLayerChild Payload;
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	const int32 Anchor = ResolveChildIndexAt(Address);
	if (!ResolveGatingMaskPayload(Payload)
		|| !Container || !Container->IsValidIndex(Anchor) || (*Container)[Anchor].IsInstance()
		|| !CanOwnScopedMasks((*Container)[Anchor]) || !CanAddScopedChild(*Container, Anchor))
	{
		return false;
	}
	Payload.ScopeOwnerChildId = (*Container)[Anchor].ChildId;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	return CanReadPublishedOutputAt(Scope, Payload, Address.OwnerId, FindSubtreeEnd(*Container, Anchor))
		&& PublishedOutputPlacementsValid(Scope);
}

FReply SMixtormat::PasteAsGatingMask(const FMixtormatChildAddress& Address)
{
	if (!CanPasteAsGatingMask(Address))
	{
		return FReply::Unhandled();
	}
	TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	const int32 Anchor = ResolveChildIndexAt(Address);
	// A duplicate, scoped under the row it was pasted on and placed at the end of that row's
	// subtree -- a copied output keeps reading its source, which is published before the owner runs.
	FMixtormatLayerChild Pasted;
	if (!ResolveGatingMaskPayload(Pasted)) { return FReply::Unhandled(); }
	Pasted.SourceLayerId = FGuid();
	Pasted.SourceChildId = FGuid();
	Pasted.ScopeOwnerChildId = (*Container)[Anchor].ChildId;
	const int32 Insert = FindSubtreeEnd(*Container, Anchor);
	Container->Insert(MoveTemp(Pasted), Insert);
	MixtormatParameterBinding::RegenerateChildIdentity((*Container)[Insert]);

	if (Address.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
		SetLayerExpanded(LayerIndex, true);
		SelectWorkingChild(LayerIndex, Insert);
	}
	else
	{
		CollapsedGroupIds.Remove(Address.OwnerId);
		SelectGroupChild(Address.OwnerId, Insert);
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	return FReply::Handled();
}

FReply SMixtormat::PasteChild(const FMixtormatChildAddress& Dest, const int32 AnchorChildIndex)
{
	FGuid ScopeOwnerChildId;
	const int32 Insert = ResolvePasteInsertIndex(Dest, AnchorChildIndex, &ScopeOwnerChildId);
	TArray<FMixtormatLayerChild>* DestContainer = ResolveContainer(Dest);
	if (Insert == INDEX_NONE || !DestContainer)
	{
		return FReply::Unhandled();
	}
	const FMixtormatChildClipboard Clipboard = ChildClipboard.GetValue();
	const bool bStructural = Clipboard.Payload.Type == EMixtormatLayerChildType::HeightPush
		|| Clipboard.Payload.Type == EMixtormatLayerChildType::StructuralWarp;
	const FText StructuralPasteReason = bStructural ? GetChildPasteReason(Dest, AnchorChildIndex) : FText::GetEmpty();
	int32 FinalInsert = Insert;

	const bool bIntoIdGroup = DestContainer->IsValidIndex(AnchorChildIndex)
		&& (*DestContainer)[AnchorChildIndex].Type == EMixtormatLayerChildType::IdGroup;
	if (bIntoIdGroup)
	{
		FMixtormatLayerChild Reference;
		if (!ResolveIdGroupPasteReference(Reference)) { return FReply::Unhandled(); }
		Reference.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
		DestContainer->Insert(MoveTemp(Reference), FinalInsert);
	}
	else if (Clipboard.Mode == EMixtormatChildClipboardMode::Instance)
	{
		FMixtormatLayerChild Instance = Clipboard.Payload;
		Instance.ChildId = FGuid::NewGuid();
		Instance.SourceLayerId = Clipboard.Source.OwnerId;
		Instance.SourceChildId = Clipboard.Source.ChildId;
		Instance.ScopeOwnerChildId.Invalidate();

		if (DestContainer->IsValidIndex(AnchorChildIndex)
			&& CanAddScopedChild(*DestContainer, AnchorChildIndex)
			&& CanKeepScopedPlacement((*DestContainer)[AnchorChildIndex], Instance))
		{
			const int32 ScopedInsert = FindSubtreeEnd(*DestContainer, AnchorChildIndex);
			// Same-container instances may have to remain below their source. Only attach when
			// that ordering still permits a contiguous owner subtree.
			if (FinalInsert <= ScopedInsert)
			{
				FinalInsert = ScopedInsert;
				Instance.ScopeOwnerChildId = (*DestContainer)[AnchorChildIndex].ChildId;
			}
		}
		DestContainer->Insert(MoveTemp(Instance), FinalInsert);
	}
	else
	{
		// Copies receive fresh node identity, but gate/field nodes keep their authored live
		// output reference and local shaping rather than becoming producer instances.
		FMixtormatLayerChild Pasted = Clipboard.Payload;
		Pasted.SourceLayerId = FGuid();
		Pasted.SourceChildId = FGuid();
		Pasted.ScopeOwnerChildId = ScopeOwnerChildId;
		if (Pasted.Type == EMixtormatLayerChildType::IdGroup
			|| Pasted.Type == EMixtormatLayerChildType::HeightPush
			|| Pasted.Type == EMixtormatLayerChildType::Behavior
			|| Pasted.Type == EMixtormatLayerChildType::StructuralWarp)
		{
			TArray<FMixtormatLayerChild> Copies;
			Copies.Add(MoveTemp(Pasted));
			Copies.Append(ChildClipboardScopedRows);
			Copies = CopyChildSubtree(MoveTemp(Copies), Clipboard.Source.OwnerId, Dest.OwnerId);
			for (int32 Index = 0; Index < Copies.Num(); ++Index)
			{
				DestContainer->Insert(MoveTemp(Copies[Index]), FinalInsert + Index);
			}
		}
		else
		{
			DestContainer->Insert(MoveTemp(Pasted), FinalInsert);
			MixtormatParameterBinding::RegenerateChildIdentity((*DestContainer)[FinalInsert]);
		}
	}

	if (Dest.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		const int32 DestLayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Dest](const FMixtormatLayer& Layer) { return Layer.LayerId == Dest.OwnerId; });
		SetLayerExpanded(DestLayerIndex, true);
		SelectWorkingChild(DestLayerIndex, FinalInsert);
	}
	else
	{
		CollapsedGroupIds.Remove(Dest.OwnerId);
		SelectGroupChild(Dest.OwnerId, FinalInsert);
	}
	RefreshLayeredPreview();
	RebuildLayerList();
	RebuildMaskList();
	if (bStructural)
	{
		WorkingStatusText = StructuralPasteReason.ToString();
	}
	return FReply::Handled();
}

void SMixtormat::CopyChildInstanceReference(const FMixtormatChildAddress& Address)
{
	CopyChild(Address, true);
}

#undef LOCTEXT_NAMESPACE
