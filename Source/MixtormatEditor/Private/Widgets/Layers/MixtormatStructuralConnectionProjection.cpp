// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/Layers/MixtormatStructuralConnectionProjection.h"

// The canonical placement predicate lives in MixtormatLayerChildren.cpp. Its current
// declaration header imports SMixtormat; keep this display-only model independent of it.
namespace MixtormatLayersPrivate
{
	bool CanKeepScopedPlacement(const FMixtormatLayerChild& Owner, const FMixtormatLayerChild& Child);
}

#define LOCTEXT_NAMESPACE "MixtormatStructuralConnectionProjection"

namespace
{
	using FIdentityIndex = TMap<FGuid, int32>;

	FIdentityIndex IndexChildren(const TArray<FMixtormatLayerChild>& Children)
	{
		FIdentityIndex Result;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FGuid& Id = Children[Index].ChildId;
			if (!Id.IsValid()) { continue; }
			if (int32* Existing = Result.Find(Id)) { *Existing = INDEX_NONE; }
			else { Result.Add(Id, Index); }
		}
		return Result;
	}

	int32 FindUnique(const FIdentityIndex& Identities, const FGuid& Id)
	{
		const int32* Index = Id.IsValid() ? Identities.Find(Id) : nullptr;
		return Index ? *Index : INDEX_NONE;
	}

	bool IsModule(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::HeightPush
			|| Child.Type == EMixtormatLayerChildType::StructuralWarp;
	}

	// Binding resolution uses first-match lookups. Never use an ambiguous instance chain
	// to justify display ownership, even if that lookup happened to produce a target.
	bool HasUniqueInstanceMapping(const TArray<FMixtormatLayer>& Effective,
		const TArray<FMixtormatLayerGroup>& Groups, const FMixtormatChildAddress& Address,
		const FMixtormatLayerChild& Child)
	{
		const FMixtormatLayerChild* Current = &Child;
		TArray<FMixtormatChildAddress> Visited;
		Visited.Add(Address);
		while (Current->IsInstance())
		{
			// ResolveChildInstances calls FindChild with the explicit owner. An invalid
			// SourceLayerId is unresolved, not shorthand for the current container.
			if (!Current->SourceLayerId.IsValid()) { return false; }
			FMixtormatChildAddress SourceAddress;
			SourceAddress.OwnerId = Current->SourceLayerId;
			SourceAddress.ChildId = Current->SourceChildId;
			const TArray<FMixtormatLayerChild>* SourceChildren = nullptr;
			int32 OwnerCount = 0;
			for (const FMixtormatLayer& Layer : Effective)
			{
				if (Layer.LayerId != Current->SourceLayerId) { continue; }
				SourceChildren = &Layer.Children;
				++OwnerCount;
			}
			for (const FMixtormatLayerGroup& Group : Groups)
			{
				if (Group.GroupId != Current->SourceLayerId) { continue; }
				SourceChildren = &Group.Children;
				SourceAddress.OwnerType = EMixtormatChildOwnerType::Group;
				++OwnerCount;
			}
			if (OwnerCount != 1 || !SourceChildren) { return false; }
			const int32 SourceIndex = FindUnique(IndexChildren(*SourceChildren), Current->SourceChildId);
			if (SourceIndex == INDEX_NONE || Visited.Contains(SourceAddress)) { return false; }
			Visited.Add(SourceAddress);
			Current = &(*SourceChildren)[SourceIndex];
		}
		return true;
	}
}

TArray<FMixtormatProjectedChildRow> MixtormatStructuralConnections::BuildChildProjection(
	const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups,
	const int32 LayerIndex)
{
	TArray<FMixtormatProjectedChildRow> Rows;
	if (!Layers.IsValidIndex(LayerIndex)) { return Rows; }
	const FMixtormatLayer& Layer = Layers[LayerIndex];
	const TArray<FMixtormatLayerChild>& Children = Layer.Children;
	const int32 Count = Children.Num();
	const FIdentityIndex Identities = IndexChildren(Children);

	int32 OwnerCount = 0;
	for (const FMixtormatLayer& Candidate : Layers)
	{
		if (Candidate.LayerId == Layer.LayerId) { ++OwnerCount; }
	}
	for (const FMixtormatLayerGroup& Group : Groups)
	{
		if (Group.GroupId == Layer.LayerId) { ++OwnerCount; }
	}
	const bool bUniqueOwner = Layer.LayerId.IsValid() && OwnerCount == 1;
	bool bSafeGroup = true;
	if (Layer.GroupId.IsValid())
	{
		int32 GroupCount = 0;
		for (const FMixtormatLayerGroup& Group : Groups)
		{
			if (Group.GroupId != Layer.GroupId) { continue; }
			++GroupCount;
			const FIdentityIndex GroupIdentities = IndexChildren(Group.Children);
			for (int32 Index = 0; Index < Group.Children.Num(); ++Index)
			{
				if (FindUnique(GroupIdentities, Group.Children[Index].ChildId) != Index) { bSafeGroup = false; }
			}
		}
		for (const FMixtormatLayer& Candidate : Layers)
		{
			if (Candidate.LayerId == Layer.GroupId) { bSafeGroup = false; }
		}
		bSafeGroup = bSafeGroup && GroupCount == 1;
	}

	TArray<FMixtormatProjectedChildRow> Authored;
	Authored.SetNum(Count);
	TArray<int32> Parents;
	Parents.Init(INDEX_NONE, Count);
	TArray<TArray<int32>> Ancestors;
	Ancestors.SetNum(Count);
	TArray<bool> SafeScopes;
	SafeScopes.Init(true, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FMixtormatProjectedChildRow& Row = Authored[Index];
		Row.Address.OwnerId = Layer.LayerId;
		Row.Address.ChildId = Children[Index].ChildId;
		Row.AuthoredChildIndex = Index;
		SafeScopes[Index] = FindUnique(Identities, Children[Index].ChildId) == Index;
		int32 Current = Index;
		// Unique parent lookup plus a Count-sized visited bound also handles forward/cyclic data.
		while (Children[Current].ScopeOwnerChildId.IsValid())
		{
			const int32 Parent = FindUnique(Identities, Children[Current].ScopeOwnerChildId);
			if (Parent == INDEX_NONE || Parent == Index || Ancestors[Index].Contains(Parent)
				|| Ancestors[Index].Num() >= Count)
			{
				SafeScopes[Index] = false;
				break;
			}
			if (Current == Index) { Parents[Index] = Parent; }
			Ancestors[Index].Add(Parent);
			if (Parent >= Current
				|| !MixtormatLayersPrivate::CanKeepScopedPlacement(Children[Parent], Children[Current]))
			{
				SafeScopes[Index] = false;
			}
			Current = Parent;
		}
		Row.AuthoredScopeDepth = Ancestors[Index].Num();
		// Broken identity/scope data is still represented, never silently sanitized.
		if (!bUniqueOwner || FindUnique(Identities, Children[Index].ChildId) != Index)
		{
			Row.PresentationReason = IssueText(EIssue::DuplicateIdentity);
		}
		else if (!SafeScopes[Index])
		{
			Row.PresentationReason = LOCTEXT("UnsafeScope", "Authored scope is missing, ambiguous, cyclic, incompatible or out of order");
		}
	}
	// Keep unique real parents on incomplete chains, but never emit a cyclic visual graph.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		int32 Current = Parents[Index];
		for (int32 Step = 0; Current != INDEX_NONE && Step < Count; ++Step)
		{
			if (Current == Index) { Parents[Index] = INDEX_NONE; break; }
			Current = Parents[Current];
		}
	}

	for (int32 Root = 0; Root < Count; ++Root)
	{
		int32 End = Root + 1;
		while (End < Count && Ancestors[End].Contains(Root)) { ++End; }
		bool bSafe = SafeScopes[Root];
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!Ancestors[Index].Contains(Root)) { continue; }
			if (Index <= Root || Index >= End || !SafeScopes[Index]) { bSafe = false; }
		}
		if (bSafe) { Authored[Root].AuthoredSubtreeEnd = End; }
	}

	// A contiguous outer block is not safe when one of its inner scopes is split.
	// Reverse authored order visits valid descendants before their owners.
	for (int32 Root = Count - 1; Root >= 0; --Root)
	{
		const int32 End = Authored[Root].AuthoredSubtreeEnd;
		if (End == INDEX_NONE) { continue; }
		for (int32 Index = Root + 1; Index < End; ++Index)
		{
			if (Authored[Index].AuthoredSubtreeEnd == INDEX_NONE)
			{
				Authored[Root].AuthoredSubtreeEnd = INDEX_NONE;
				break;
			}
		}
	}

	TArray<TArray<int32>> Incoming;
	Incoming.SetNum(Count);
	TArray<int32> BlockModule;
	BlockModule.Init(INDEX_NONE, Count);
	TArray<int32> Targets;
	Targets.Init(INDEX_NONE, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (!IsModule(Children[Index])) { continue; }
		FMixtormatProjectedChildRow& Row = Authored[Index];
		Row.Kind = EMixtormatProjectedChildKind::AuthoredRepair;
		const FMixtormatStructuralConnectionContext Context(Layers, Groups, Row.Address);
		Row.Status = Context.Evaluate();
		const FMixtormatLayer* Resolved = Context.GetResolvedDestination();
		const FIdentityIndex ResolvedIds = Resolved ? IndexChildren(Resolved->Children) : FIdentityIndex{};
		const int32 ResolvedStart = FindUnique(ResolvedIds, Row.Address.ChildId);
		const FMixtormatLayerChild* Module = Resolved && ResolvedStart != INDEX_NONE
			? &Resolved->Children[ResolvedStart] : nullptr;
		if (Module && IsModule(*Module))
		{
			const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
			Row.ResolvedSource = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
			Row.ResolvedTargetId = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
		}
		if (!Row.PresentationReason.IsEmpty()) { continue; }
		if (!bSafeGroup)
		{
			Row.PresentationReason = LOCTEXT("GroupMapping", "Shared group provenance is missing or ambiguous");
			continue;
		}
		if (Children[Index].ScopeOwnerChildId.IsValid())
		{
			Row.PresentationReason = IssueText(EIssue::ScopedModule);
			continue;
		}
		const int32 End = Row.AuthoredSubtreeEnd;
		if (End == INDEX_NONE)
		{
			Row.PresentationReason = LOCTEXT("SplitSubtree", "Authored operation subtree is not safely contiguous");
			continue;
		}
		if (!Module || Module->Type != Children[Index].Type
			|| !Context.Effective.IsValidIndex(Context.LayerIndex)
			|| Context.Effective[Context.LayerIndex].LayerId != Layer.LayerId)
		{
			Row.PresentationReason = LOCTEXT("PayloadMapping", "Resolved operation cannot be mapped uniquely to its authored placement");
			continue;
		}
		const TArray<FMixtormatLayerChild>& EffectiveChildren = Context.Effective[Context.LayerIndex].Children;
		const FIdentityIndex EffectiveIds = IndexChildren(EffectiveChildren);
		const int32 EffectiveStart = FindUnique(EffectiveIds, Row.Address.ChildId);
		bool bSafeMapping = EffectiveStart != INDEX_NONE && EffectiveStart == Context.ChildIndex;
		for (int32 ChildIndex = Index; ChildIndex < End; ++ChildIndex)
		{
			const FMixtormatLayerChild& Child = Children[ChildIndex];
			const int32 EffectiveIndex = FindUnique(EffectiveIds, Child.ChildId);
			const int32 ResolvedIndex = FindUnique(ResolvedIds, Child.ChildId);
			// Each local GUID must occupy the same relative slot in both mapped blocks.
			// A shared prefix/tail may shift the blocks, but may not split or interleave them.
			if (!bSafeMapping || EffectiveIndex == INDEX_NONE || ResolvedIndex == INDEX_NONE
				|| EffectiveIndex != EffectiveStart + (ChildIndex - Index)
				|| ResolvedIndex != ResolvedStart + (ChildIndex - Index)
				|| EffectiveChildren[EffectiveIndex].ScopeOwnerChildId != Child.ScopeOwnerChildId
				|| Resolved->Children[ResolvedIndex].ScopeOwnerChildId != Child.ScopeOwnerChildId
				|| !HasUniqueInstanceMapping(Context.Effective, Groups, Authored[ChildIndex].Address,
					EffectiveChildren[EffectiveIndex]))
			{
				bSafeMapping = false;
				break;
			}
		}
		if (!bSafeMapping)
		{
			Row.PresentationReason = LOCTEXT("BlockMapping", "Operation subtree has ambiguous effective, group or instance mapping");
			continue;
		}
		const int32 Target = FindUnique(Identities, Row.ResolvedTargetId);
		if (Target == INDEX_NONE)
		{
			const EIssue Issue = !Row.ResolvedTargetId.IsValid() ? EIssue::Unset
				: Identities.Contains(Row.ResolvedTargetId) ? EIssue::DuplicateIdentity : EIssue::MissingChild;
			Row.PresentationReason = !Identities.Contains(Row.ResolvedTargetId)
							&& EffectiveIds.Contains(Row.ResolvedTargetId)
				? LOCTEXT("SharedTarget", "Effective target has no unique local authored placement; shared children remain in group UI")
				: IssueText(Issue);
			continue;
		}
		const int32 EffectiveTarget = FindUnique(EffectiveIds, Row.ResolvedTargetId);
		const int32 ResolvedTarget = FindUnique(ResolvedIds, Row.ResolvedTargetId);
		if (EffectiveTarget == INDEX_NONE || ResolvedTarget == INDEX_NONE
			|| EffectiveChildren[EffectiveTarget].ScopeOwnerChildId != Children[Target].ScopeOwnerChildId
			|| Resolved->Children[ResolvedTarget].ScopeOwnerChildId != Children[Target].ScopeOwnerChildId)
		{
			Row.PresentationReason = LOCTEXT("TargetMapping", "Resolved target is not a unique local authored child");
			continue;
		}
		if (Children[Target].Type != EMixtormatLayerChildType::Generator)
		{
			Row.PresentationReason = IssueText(EIssue::WrongTargetKind);
			continue;
		}
		if (Children[Target].ScopeOwnerChildId.IsValid())
		{
			Row.PresentationReason = IssueText(EIssue::ScopedTarget);
			continue;
		}
		if (!SafeScopes[Target] || Authored[Target].AuthoredSubtreeEnd == INDEX_NONE)
		{
			Row.PresentationReason = LOCTEXT("TargetScope", "Target authored subtree is not safely contiguous");
			continue;
		}
		// Placement is independent of canonical source/kind/order/enabled eligibility.
		Row.Kind = EMixtormatProjectedChildKind::IncomingConnection;
		Targets[Index] = Target;
		Incoming[Target].Add(Index);
		for (int32 ChildIndex = Index; ChildIndex < End; ++ChildIndex) { BlockModule[ChildIndex] = Index; }
	}

	TArray<int32> DisplayIndices;
	DisplayIndices.Init(INDEX_NONE, Count);
	Rows.Reserve(Count);
	const auto Emit = [&](const int32 Index)
	{
		DisplayIndices[Index] = Rows.Num();
		FMixtormatProjectedChildRow Row = Authored[Index];
		const int32 ModuleIndex = BlockModule[Index];
		if (ModuleIndex != INDEX_NONE)
		{
			Row.bInIncomingBlock = true;
			Row.ModuleAuthoredIndex = ModuleIndex;
			Row.ScopeDepthWithinIncoming = Row.AuthoredScopeDepth;
		}
		Rows.Add(MoveTemp(Row));
	};
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (BlockModule[Index] != INDEX_NONE) { continue; }
		Emit(Index);
		// Buckets were filled in authored execution order; no map iteration orders rows.
		for (const int32 ModuleIndex : Incoming[Index])
		{
			for (int32 ChildIndex = ModuleIndex; ChildIndex < Authored[ModuleIndex].AuthoredSubtreeEnd; ++ChildIndex)
			{
				Emit(ChildIndex);
			}
		}
	}
	for (FMixtormatProjectedChildRow& Row : Rows)
	{
		const int32 Index = Row.AuthoredChildIndex;
		const int32 Parent = Targets[Index] != INDEX_NONE ? Targets[Index] : Parents[Index];
		Row.VisualParentRowIndex = Parent != INDEX_NONE ? DisplayIndices[Parent] : INDEX_NONE;
	}
	return Rows;
}

#undef LOCTEXT_NAMESPACE
