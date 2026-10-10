// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatChildScope.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatOutputReference.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "Widgets/MixtormatChildCapabilities.h"
#include "UI/Parameters/MixtormatParameterAuthoring.h"
#include "Style/MixtormatThemeStore.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace MixtormatLayersPrivate
{

	EMixtormatEffectType EffectTypeOf(const FMixtormatLayerChild& Child)
	{
		return ResolveChildEffectType(Child);
	}

	bool IsFlowWarp(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Effect
			&& EffectTypeOf(Child) == EMixtormatEffectType::FlowWarp;
	}

	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child)
	{
		return MixtormatChildScope::CanOwnScopedMasks(Child);
	}

	bool CanOwnScopedBlurs(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask;
	}

	bool CanOwnFlowWarp(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask
			|| (Child.Type == EMixtormatLayerChildType::Effect
				&& MixtormatEffectClassOf(EffectTypeOf(Child))
					== EMixtormatEffectClass::Surface);
	}

	bool IsMaskFilter(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Blur
			|| Child.Type == EMixtormatLayerChildType::Curvature;
	}

	// The authored enable flag of any child, whatever payload carries it. One place, so a new
	// child type cannot be added to the stack and silently read or write the Mask payload's flag.
	bool IsChildEnabled(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask:            return Child.Mask.bEnabled;
		case EMixtormatLayerChildType::Effect:          return Child.Effect.bEnabled;
		case EMixtormatLayerChildType::Generated:       return Child.Generated.bEnabled;
		case EMixtormatLayerChildType::Craquelure:      return Child.Craquelure.bEnabled;
		case EMixtormatLayerChildType::ColorId:         return Child.ColorId.bEnabled;
		case EMixtormatLayerChildType::Filter:          return Child.Filter.bEnabled;
		case EMixtormatLayerChildType::HsvFilter:       return Child.HsvFilter.bEnabled;
		case EMixtormatLayerChildType::RandomId:        return Child.RandomId.bEnabled;
		case EMixtormatLayerChildType::RampId:          return Child.RampId.bEnabled;
		case EMixtormatLayerChildType::UvFromIds:       return Child.UvId.bEnabled;
		case EMixtormatLayerChildType::ReliefFromIds:   return Child.ReliefId.bEnabled;
		case EMixtormatLayerChildType::BoundaryFromIds: return Child.BoundaryId.bEnabled;
		case EMixtormatLayerChildType::PatternId:       return Child.PatternId.bEnabled;
		case EMixtormatLayerChildType::IdGroup:         return Child.IdGroup.bEnabled;
		case EMixtormatLayerChildType::OutputReference: return Child.OutputReference.bEnabled;
		case EMixtormatLayerChildType::Generator:       return Child.Generator.bEnabled;
		case EMixtormatLayerChildType::Blur:            return Child.Blur.bEnabled;
		case EMixtormatLayerChildType::Curvature:       return Child.Curvature.bEnabled;
		case EMixtormatLayerChildType::HeightBlend:     return Child.HeightBlend.bEnabled;
		case EMixtormatLayerChildType::HeightCurve:     return Child.HeightCurve.bEnabled;
		case EMixtormatLayerChildType::HeightColorRamp: return Child.HeightColorRamp.bEnabled;
		case EMixtormatLayerChildType::Behavior:        return Child.Behavior.bEnabled;
		default:
			// A new child type that carries its own enable flag must be named here rather than
			// silently reporting the Mask payload's flag.
			checkNoEntry();
			return true;
		}
	}

	void SetChildEnabled(FMixtormatLayerChild& Child, const bool bEnabled)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask:            Child.Mask.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Effect:          Child.Effect.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Generated:       Child.Generated.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Craquelure:      Child.Craquelure.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::ColorId:         Child.ColorId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Filter:          Child.Filter.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::HsvFilter:       Child.HsvFilter.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::RandomId:        Child.RandomId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::RampId:          Child.RampId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::UvFromIds:       Child.UvId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::ReliefFromIds:   Child.ReliefId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::BoundaryFromIds: Child.BoundaryId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::PatternId:       Child.PatternId.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::IdGroup:         Child.IdGroup.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::OutputReference: Child.OutputReference.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Generator:       Child.Generator.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Blur:            Child.Blur.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Curvature:       Child.Curvature.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::HeightBlend:     Child.HeightBlend.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::HeightCurve:     Child.HeightCurve.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::HeightColorRamp: Child.HeightColorRamp.bEnabled = bEnabled; break;
		case EMixtormatLayerChildType::Behavior:        Child.Behavior.bEnabled = bEnabled; break;
		default:
			// A new child type that carries its own enable flag must be named here rather than
			// silently mutating the Mask payload's flag.
			checkNoEntry();
			break;
		}
	}

	// Scoping is a property of a child array, not of what owns one. A layer's Children and a
	// group's shared Children are the same shape and obey the same rules, so these take the array
	// -- which is what lets one set of creators serve both containers. (They were duplicated per
	// container before, because the layer versions took an FMixtormatLayer a group cannot supply.)

	int32 FindChildById(const TArray<FMixtormatLayerChild>& Children, const FGuid& ChildId)
	{
		return MixtormatChildScope::FindIndexById(Children, ChildId);
	}

	int32 GetScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return 0;
		}

		int32 Depth = 0;
		int32 CurrentIndex = ChildIndex;
		FGuid OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid())
		{
			if (Visited.Contains(OwnerId))
			{
				return MaximumScopeDepth + 1;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Children, OwnerId);
			if (OwnerIndex == INDEX_NONE
				|| !CanKeepScopedPlacement(
					Children[OwnerIndex],
					Children[CurrentIndex]))
			{
				return MaximumScopeDepth + 1;
			}
			++Depth;
			CurrentIndex = OwnerIndex;
			OwnerId = Children[OwnerIndex].ScopeOwnerChildId;
		}
		return Depth;
	}

	bool IsDescendantOf(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& AncestorId)
	{
		if (!Children.IsValidIndex(ChildIndex) || !AncestorId.IsValid())
		{
			return false;
		}

		FGuid OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (OwnerId.IsValid() && !Visited.Contains(OwnerId))
		{
			if (OwnerId == AncestorId)
			{
				return true;
			}
			Visited.Add(OwnerId);
			const int32 OwnerIndex = FindChildById(Children, OwnerId);
			if (OwnerIndex == INDEX_NONE)
			{
				return false;
			}
			OwnerId = Children[OwnerIndex].ScopeOwnerChildId;
		}
		return false;
	}

	int32 FindSubtreeEnd(const TArray<FMixtormatLayerChild>& Children, const int32 RootIndex)
	{
		if (!Children.IsValidIndex(RootIndex))
		{
			return RootIndex;
		}
		const FGuid RootId = Children[RootIndex].ChildId;
		int32 End = RootIndex + 1;
		while (Children.IsValidIndex(End) && IsDescendantOf(Children, End, RootId))
		{
			++End;
		}
		return End;
	}

	// The ancestor of ChildIndex whose own owner is ParentId -- so FGuid() asks for the top-level
	// root of whatever subtree the index lands in.

	int32 FindSiblingRoot(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& ParentId)
	{
		int32 CurrentIndex = ChildIndex;
		TSet<FGuid> Visited;
		while (Children.IsValidIndex(CurrentIndex))
		{
			const FMixtormatLayerChild& Current = Children[CurrentIndex];
			if (Current.ScopeOwnerChildId == ParentId)
			{
				return CurrentIndex;
			}
			if (!Current.ScopeOwnerChildId.IsValid()
				|| Visited.Contains(Current.ScopeOwnerChildId))
			{
				return INDEX_NONE;
			}
			Visited.Add(Current.ScopeOwnerChildId);
			CurrentIndex = FindChildById(Children, Current.ScopeOwnerChildId);
		}
		return INDEX_NONE;
	}

	bool GetPublishedOutputSource(const FMixtormatLayerChild& Child, FGuid& OwnerId, FGuid& ChildId)
	{
		if (Child.Type == EMixtormatLayerChildType::OutputReference)
		{
			OwnerId = Child.OutputReference.SourceLayerId;
			ChildId = Child.OutputReference.SourceChildId;
			return true;
		}
		if (Child.Type == EMixtormatLayerChildType::BoundaryFromIds
			&& (Child.BoundaryId.RegionIdsSource.SourceLayerId.IsValid()
				|| Child.BoundaryId.RegionIdsSource.SourceChildId.IsValid()))
		{
			OwnerId = Child.BoundaryId.RegionIdsSource.SourceLayerId;
			ChildId = Child.BoundaryId.RegionIdsSource.SourceChildId;
			return true;
		}
		if (Child.Type == EMixtormatLayerChildType::Mask
			&& (Child.Mask.PublishedSourceLayerId.IsValid() || Child.Mask.PublishedSourceChildId.IsValid()))
		{
			OwnerId = Child.Mask.PublishedSourceLayerId;
			ChildId = Child.Mask.PublishedSourceChildId;
			return true;
		}
		return false;
	}

	const TArray<FMixtormatLayerChild>* FindChildrenInScope(const FMixtormatBindingScope& Scope, const FGuid OwnerId)
	{
		for (const FMixtormatLayer& Layer : Scope.GetLayers())
		{
			if (Layer.LayerId == OwnerId)
			{
				return &Layer.Children;
			}
		}
		const FMixtormatLayerGroup* Group = Scope.Groups
			? MixtormatLayerGroups::FindGroup(*Scope.Groups, OwnerId) : nullptr;
		return Group ? &Group->Children : nullptr;
	}

	bool IsRegionIdsReference(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::OutputReference
			&& Child.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds;
	}

	bool MakePublishedFieldReference(const FMixtormatLayerChild& Source,
		const FMixtormatChildAddress& Address, const EMixtormatPublishedFieldKind Kind,
		FMixtormatLayerChild& Reference)
	{
		Reference = FMixtormatLayerChild();
		Reference.Type = EMixtormatLayerChildType::OutputReference;
		// An existing reference of the same kind is chained through rather than re-resolved.
		if (Source.Type == EMixtormatLayerChildType::OutputReference
			&& Source.OutputReference.Kind == Kind)
		{
			Reference.OutputReference = Source.OutputReference;
			Reference.OutputReference.bEnabled = true;
			return Reference.OutputReference.HasSource();
		}
		const FMixtormatChildCapabilities Caps = GetChildCapabilities(Source);
		const FMixtormatPublishedOutputDesc* Output = Caps.Outputs.FindByPredicate(
			[Kind](const FMixtormatPublishedOutputDesc& Candidate)
			{
				return Candidate.bCopyableAsField && Candidate.FieldKind == Kind;
			});
		if (!Output || !Address.IsValid()) { return false; }
		Reference.OutputReference.SourceLayerId = Address.OwnerId;
		Reference.OutputReference.SourceChildId = Address.ChildId;
		Reference.OutputReference.OutputName = Output->Name;
		Reference.OutputReference.Kind = Output->FieldKind;
		return true;
	}

	bool MakeRegionIdsReference(const FMixtormatLayerChild& Source,
		const FMixtormatChildAddress& Address, FMixtormatLayerChild& Reference)
	{
		return MakePublishedFieldReference(Source, Address,
			EMixtormatPublishedFieldKind::RegionIds, Reference);
	}

	bool ValidateRegionIdsPlacement(const FMixtormatBindingScope& Scope,
		const FMixtormatLayerChild& Child, const FGuid OwnerId, const int32 InsertIndex)
	{
		TArray<FMixtormatLayer> Layers = Scope.GetLayers();
		TArray<FMixtormatLayerGroup> Groups = Scope.Groups ? *Scope.Groups : TArray<FMixtormatLayerGroup>();
		FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, OwnerId);
		FMixtormatLayer* Layer = Layers.FindByPredicate(
			[OwnerId](const FMixtormatLayer& Candidate) { return Candidate.LayerId == OwnerId; });
		TArray<FMixtormatLayerChild>* Children = Group ? &Group->Children : Layer ? &Layer->Children : nullptr;
		if (!Children) { return false; }
		FMixtormatLayerChild Proposed = Child;
		// Validate the edge even when its row is currently bypassed; paste retains authored enable state.
		Proposed.OutputReference.bEnabled = true;
		const int32 Existing = FindChildById(*Children, Child.ChildId);
		if (Existing != INDEX_NONE) { (*Children)[Existing] = Proposed; }
		else
		{
			if (InsertIndex < 0 || InsertIndex > Children->Num()) { return false; }
			Children->Insert(Proposed, InsertIndex);
		}
		TArray<FMixtormatLayer> Effective;
		MixtormatLayerGroups::BuildEffectiveLayers(Layers, Groups, Effective);
		bool bValidated = false;
		for (const FMixtormatLayer& Member : Effective)
		{
			if ((Group ? Member.GroupId != OwnerId : Member.LayerId != OwnerId) || !Member.bEnabled) { continue; }
			const FGuid EffectiveId = Group
				? MixtormatLayerGroups::MakeEffectiveChildId(OwnerId, Child.ChildId, Member.LayerId) : Child.ChildId;
			const FMixtormatLayerChild* Placement = Member.Children.FindByPredicate(
				[EffectiveId](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == EffectiveId; });
			if (!Placement || !MixtormatOutputReferences::ValidateDependency(Effective,
				Member.LayerId, EffectiveId, Placement->OutputReference)) { return false; }
			bValidated = true;
		}
		return bValidated;
	}

	bool IsPublishedSourceEnabled(const FMixtormatBindingScope& Scope, const FGuid OwnerId, const FGuid ChildId,
		TSet<FGuid>* ActiveSources)
	{
		TSet<FGuid> LocalSources;
		TSet<FGuid>& Active = ActiveSources ? *ActiveSources : LocalSources;
		if (!ChildId.IsValid() || Active.Contains(ChildId)) { return false; }
		Active.Add(ChildId);
		const TArray<FMixtormatLayerChild>* Children = FindChildrenInScope(Scope, OwnerId);
		const int32 Index = Children ? FindChildById(*Children, ChildId) : INDEX_NONE;
		if (!Children || !Children->IsValidIndex(Index)) { return false; }
		for (const FMixtormatLayer& Layer : Scope.GetLayers())
		{
			if (Layer.LayerId == OwnerId && !Layer.bEnabled) { return false; }
			if (Layer.LayerId == OwnerId && Scope.Groups)
			{
				const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(*Scope.Groups, Layer.GroupId);
				if (Group && !Group->bEnabled) { return false; }
			}
		}
		if (Scope.Groups)
		{
			const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(*Scope.Groups, OwnerId);
			if (Group && (!Group->bEnabled || !Scope.GetLayers().ContainsByPredicate(
				[OwnerId](const FMixtormatLayer& Layer) { return Layer.GroupId == OwnerId && Layer.bEnabled; })))
			{
				return false;
			}
		}
		const FMixtormatLayerChild& Child = (*Children)[Index];
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::PatternId: if (!Child.PatternId.bEnabled) { return false; } break;
		case EMixtormatLayerChildType::Filter: if (!Child.Filter.bEnabled) { return false; } break;

		case EMixtormatLayerChildType::IdGroup: if (!Child.IdGroup.bEnabled) { return false; } break;
		case EMixtormatLayerChildType::OutputReference: if (!Child.OutputReference.bEnabled) { return false; } break;
		case EMixtormatLayerChildType::Generator: if (!Child.Generator.bEnabled) { return false; } break;
		case EMixtormatLayerChildType::Effect: if (!Child.Effect.bEnabled) { return false; } break;
		default: return false;
		}
		FGuid ParentId = Child.ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (ParentId.IsValid())
		{
			const int32 ParentIndex = FindChildById(*Children, ParentId);
			if (Visited.Contains(ParentId) || !Children->IsValidIndex(ParentIndex)) { return false; }
			Visited.Add(ParentId);
			const FMixtormatLayerChild& Parent = (*Children)[ParentIndex];
			if (Parent.Type == EMixtormatLayerChildType::IdGroup && !Parent.IdGroup.bEnabled) { return false; }
			ParentId = Parent.ScopeOwnerChildId;
		}
		if (Child.IsInstance()
			&& !IsPublishedSourceEnabled(Scope, Child.SourceLayerId, Child.SourceChildId, &Active)) { return false; }
		if (Child.Type == EMixtormatLayerChildType::OutputReference
			&& (!Child.OutputReference.HasSource() || !IsPublishedSourceEnabled(Scope,
				Child.OutputReference.SourceLayerId, Child.OutputReference.SourceChildId, &Active))) { return false; }
		Active.Remove(ChildId);
		return true;
	}

	bool CanReadPublishedOutputAt(
		const FMixtormatBindingScope& Scope,
		const FMixtormatLayerChild& Child,
		const FGuid DestOwnerId,
		const int32 InsertIndex)
	{
		FGuid SourceOwnerId, SourceChildId;
		if (!GetPublishedOutputSource(Child, SourceOwnerId, SourceChildId))
		{
			return true;
		}
		if (Child.Type == EMixtormatLayerChildType::Mask
			&& Child.Mask.PublishedSourceOutput == TEXT("Value"))
		{
			const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(Scope, SourceOwnerId, SourceChildId);
			if (Source && Source->Type == EMixtormatLayerChildType::Generator
				&& Source->Generator.Type == EMixtormatGeneratorType::Noise)
			{
				// Validate the actual projected position, including per-member shared identities.
				TArray<FMixtormatLayer> Proposed = Scope.GetLayers();
				TArray<FMixtormatLayerGroup> Groups;
				if (Scope.Groups) { Groups = *Scope.Groups; }
				TArray<FMixtormatLayerChild>* Destination = nullptr;
				for (FMixtormatLayer& Layer : Proposed)
				{
					if (Layer.LayerId == DestOwnerId) { Destination = &Layer.Children; }
				}
				FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, DestOwnerId);
				if (Group) { Destination = &Group->Children; }
				if (!Destination || InsertIndex < 0 || InsertIndex > Destination->Num()) { return false; }
				FMixtormatLayerChild Candidate = Child;
				// Authoring an inactive mask still validates the connection it would consume.
				Candidate.Mask.bEnabled = true;
				if (Destination->IsValidIndex(InsertIndex)
					&& (*Destination)[InsertIndex].ChildId == Child.ChildId)
				{
					(*Destination)[InsertIndex] = Candidate;
				}
				else
				{
					Candidate.ChildId = FGuid::NewGuid();
					Destination->Insert(Candidate, InsertIndex);
				}
				TArray<FMixtormatLayer> Effective;
				MixtormatLayerGroups::BuildEffectiveLayers(Proposed, Groups, Effective);
				bool bFound = false;
				for (int32 LayerIndex = 0; LayerIndex < Effective.Num(); ++LayerIndex)
				{
					const FMixtormatLayer& Layer = Effective[LayerIndex];
					if (Group ? Layer.GroupId != DestOwnerId : Layer.LayerId != DestOwnerId) { continue; }
					const FGuid Id = Group ? MixtormatLayerGroups::MakeEffectiveChildId(
						DestOwnerId, Candidate.ChildId, Layer.LayerId) : Candidate.ChildId;
					const int32 Index = Layer.Children.IndexOfByPredicate(
						[Id](const FMixtormatLayerChild& Row) { return Row.ChildId == Id; });
					if (!Layer.Children.IsValidIndex(Index)
						|| MixtormatOutputReferences::ResolvePublishedMaskSource(
							Effective, LayerIndex, Index, Layer.Children[Index].Mask) == INDEX_NONE) { return false; }
					bFound = true;
				}
				return bFound;
			}
		}
		if (IsRegionIdsReference(Child))
		{
			const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(Scope, SourceOwnerId, SourceChildId);
			if (!Source) { return false; }
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Source);
			return Caps.Outputs.ContainsByPredicate([&Child](const FMixtormatPublishedOutputDesc& Output)
				{
					return Output.bCopyableAsField && Output.Name == Child.OutputReference.OutputName
						&& Output.FieldKind == Child.OutputReference.Kind;
				}) && ValidateRegionIdsPlacement(Scope, Child, DestOwnerId, InsertIndex);
		}
		// Flow/UV imports still run before local producers.
		if (Child.Type == EMixtormatLayerChildType::OutputReference && SourceOwnerId == DestOwnerId) { return false; }
		const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(Scope, SourceOwnerId, SourceChildId);
		if (Child.Type == EMixtormatLayerChildType::OutputReference)
		{
			if (!Source || !Child.OutputReference.HasSource()) { return false; }
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Source);
			if (!Caps.Outputs.ContainsByPredicate([&Child](const FMixtormatPublishedOutputDesc& Output)
				{
					return Output.bCopyableAsField && Output.Name == Child.OutputReference.OutputName
						&& Output.FieldKind == Child.OutputReference.Kind;
				})) { return false; }
		}
		using EPlacement = MixtormatParameterBinding::EInstancePlacement;
		if (!SourceOwnerId.IsValid() || !SourceChildId.IsValid() || !Source
			|| MixtormatParameterBinding::ClassifyInstancePlacement(
				Scope, SourceOwnerId, SourceChildId, DestOwnerId, InsertIndex) != EPlacement::Valid
			|| (Child.IsInstance() && MixtormatParameterBinding::ClassifyInstancePlacement(
				Scope, Child.SourceLayerId, Child.SourceChildId, DestOwnerId, InsertIndex) != EPlacement::Valid))
		{
			return false;
		}

		if (SourceOwnerId != DestOwnerId)
		{
			return true;
		}
		const TArray<FMixtormatLayerChild>* Children = FindChildrenInScope(Scope, DestOwnerId);
		const int32 SourceIndex = Children ? FindChildById(*Children, SourceChildId) : INDEX_NONE;
		// One legal ancestor read: an owned Behavior may use a named, copyable
		// feature from its *own* generator as a gate. The generator pass publishes that
		// pre-flow snapshot before solving the Behavior; the final/post-flow output is not
		// consulted here. No other ancestor edge is exempt from feedback rejection.
		if (Child.Type == EMixtormatLayerChildType::Mask && Children && Source
			&& Scope.GetLayers().ContainsByPredicate([DestOwnerId](const FMixtormatLayer& Layer)
				{ return Layer.LayerId == DestOwnerId && Layer.Type == EMixtormatLayerType::Generator; })
			&& Source->Type == EMixtormatLayerChildType::Generator && !Source->IsInstance()
			&& Child.Mask.HasPublishedSource()
			&& Child.Mask.PublishedSourceOutput != FName(TEXT("Value"))
			&& SourceIndex != INDEX_NONE && SourceIndex < InsertIndex)
		{
			const int32 FlowIndex = FindChildById(*Children, Child.ScopeOwnerChildId);
			if (Children->IsValidIndex(FlowIndex) && SourceIndex < FlowIndex
				&& FlowIndex < InsertIndex && (*Children)[FlowIndex].Type == EMixtormatLayerChildType::Behavior
				&& (*Children)[FlowIndex].ScopeOwnerChildId == SourceChildId)
			{
				const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Source);
				if (Caps.Outputs.ContainsByPredicate([&Child](const FMixtormatPublishedOutputDesc& Output)
					{ return Output.bCopyableAsMask && Output.Name == Child.Mask.PublishedSourceOutput; }))
				{
					return true;
				}
			}
		}
		FGuid OwnerId = Child.ScopeOwnerChildId;
		TSet<FGuid> Visited;
		while (Children && OwnerId.IsValid())
		{
			const int32 OwnerIndex = FindChildById(*Children, OwnerId);
			if (Visited.Contains(OwnerId) || !Children->IsValidIndex(OwnerIndex))
			{
				return false;
			}
			Visited.Add(OwnerId);
			// Array order alone would allow a generator's own gate, or a group's output fed
			// back into that group's inputs. Both require the owner to finish before it starts.
			if (SourceChildId == OwnerId
				|| ((*Children)[OwnerIndex].Type != EMixtormatLayerChildType::IdGroup
					&& IsDescendantOf(*Children, SourceIndex, OwnerId)))
			{
				return false;
			}
			OwnerId = (*Children)[OwnerIndex].ScopeOwnerChildId;
		}
		return true;
	}

	bool PublishedOutputPlacementsValid(const FMixtormatBindingScope& Scope)
	{
		const auto CheckChildren = [&Scope](const TArray<FMixtormatLayerChild>& Children, const FGuid OwnerId)
		{
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				if (IsRegionIdsReference(Children[Index])
					&& !IsPublishedSourceEnabled(Scope, OwnerId, Children[Index].ChildId)) { continue; }
				if (!CanReadPublishedOutputAt(Scope, Children[Index], OwnerId, Index))
				{
					return false;
				}
			}
			return true;
		};
		for (const FMixtormatLayer& Layer : Scope.GetLayers())
		{
			if (!CheckChildren(Layer.Children, Layer.LayerId))
			{
				return false;
			}
		}
		if (Scope.Groups)
		{
			for (const FMixtormatLayerGroup& Group : *Scope.Groups)
			{
				if (!CheckChildren(Group.Children, Group.GroupId))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CanAddScopedChild(const TArray<FMixtormatLayerChild>& Children, const int32 OwnerIndex)
	{
		return Children.IsValidIndex(OwnerIndex)
			&& GetScopeDepth(Children, OwnerIndex) < MaximumScopeDepth;
	}

	bool IsIdGroupChild(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::PatternId:

		case EMixtormatLayerChildType::IdGroup:
		case EMixtormatLayerChildType::ColorId:
		case EMixtormatLayerChildType::HsvFilter:
		case EMixtormatLayerChildType::RandomId:
		case EMixtormatLayerChildType::RampId:
		case EMixtormatLayerChildType::UvFromIds:
		case EMixtormatLayerChildType::ReliefFromIds:
			return true;
		case EMixtormatLayerChildType::OutputReference:
			return Child.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds;
		case EMixtormatLayerChildType::Filter:
			return Child.Filter.bSurfaceIds;
		default:
			return false;
		}
	}

	// Puts Child under the owner at OwnerChildIndex and returns where it landed, or INDEX_NONE if
	// that owner cannot take it. At the end of the owner's subtree, so the owner and everything
	// already gating through it stay one contiguous block -- which is the invariant every walk
	// here depends on.
	//
	// The type-pair rule is CanKeepScopedPlacement's, the same one a drag is validated against, so
	// what the menus offer and what a drop accepts cannot drift apart.

	int32 InsertScopedChild(
		TArray<FMixtormatLayerChild>& Children,
		const int32 OwnerChildIndex,
		FMixtormatLayerChild&& Child)
	{
		if (!Children.IsValidIndex(OwnerChildIndex)
			|| !CanKeepScopedPlacement(Children[OwnerChildIndex], Child)
			|| !CanAddScopedChild(Children, OwnerChildIndex))
		{
			return INDEX_NONE;
		}
		const int32 InsertAt = FindSubtreeEnd(Children, OwnerChildIndex);
		Child.ScopeOwnerChildId = Children[OwnerChildIndex].ChildId;
		Children.Insert(MoveTemp(Child), InsertAt);
		return InsertAt;
	}

	// The three scoped things a mask can carry, as prototypes for InsertScopedChild. Flow Warp is
	// an Effect with a procedural type rather than a type of its own, which is why this exists
	// instead of the callers passing an enum.

	FMixtormatLayerChild MakeScopedPrototype(const EMixtormatLayerChildType ChildType)
	{
		FMixtormatLayerChild Child;
		Child.Type = ChildType;
		return Child;
	}

	FMixtormatLayerChild MakeFlowWarpPrototype()
	{
		FMixtormatLayerChild Child;
		Child.Type = EMixtormatLayerChildType::Effect;
		Child.Effect.ProceduralType = EMixtormatEffectType::FlowWarp;
		return Child;
	}

	bool CanKeepScopedPlacement(
		const FMixtormatLayerChild& Owner,
		const FMixtormatLayerChild& Child)
	{
		if (Child.Type == EMixtormatLayerChildType::Behavior)
		{
			if (Owner.Type == EMixtormatLayerChildType::Generator)
			{
				return true;
			}
			if (Owner.Type != EMixtormatLayerChildType::Behavior)
			{
				return false;
			}
			// A child Flow Field modifies the immediate parent's input; nested
			// operations must remain in the existing scoped child tree.
			return Child.Behavior.Type == EMixtormatBehaviorType::FlowField
				&& (Owner.Behavior.Type == EMixtormatBehaviorType::Push
					|| Owner.Behavior.Type == EMixtormatBehaviorType::Warp
					|| Owner.Behavior.Type == EMixtormatBehaviorType::Deform);
		}
		if (Owner.Type == EMixtormatLayerChildType::IdGroup)
		{
			return IsIdGroupChild(Child);
		}
		if (Child.Type == EMixtormatLayerChildType::Mask)
		{
			return CanOwnScopedMasks(Owner);
		}
		if (IsMaskFilter(Child))
		{
			return CanOwnScopedBlurs(Owner);
		}
		return IsFlowWarp(Child) && CanOwnFlowWarp(Owner);
	}

	// Everything a newly created child needs beyond its Type, in one place.
	//
	// Both Add menus and both containers come through here, which is the point: the red first
	// colour on a Color ID mask and the LayerValues source on a Layer Values mask used to exist
	// only on the layer path, so the same entry on a group produced a differently configured node.
	// The serialised child type one menu entry produces. Several kinds share one: Texture Mask and
	// Layer Values Mask are both Mask, and Strata Carver is a Generator. That collapse is exactly
	// why EMixtormatChildCreation exists alongside the type.

	EMixtormatChildCreation CreationKindForGenerator(const EMixtormatGeneratorType Type)
	{
		switch (Type)
		{
		case EMixtormatGeneratorType::Cracks:        return EMixtormatChildCreation::Cracks;
		case EMixtormatGeneratorType::RockFormation: return EMixtormatChildCreation::RockFormation;
		case EMixtormatGeneratorType::Pebbles:       return EMixtormatChildCreation::Pebbles;
		case EMixtormatGeneratorType::CliffStrata:    return EMixtormatChildCreation::CliffStrata;
		case EMixtormatGeneratorType::Noise:          return EMixtormatChildCreation::Noise;
		default:                                      return EMixtormatChildCreation::StrataCarver;
		}
	}

	EMixtormatLayerChildType ChildTypeForCreation(const EMixtormatChildCreation Kind)
	{
		switch (Kind)
		{
		case EMixtormatChildCreation::PatternIds:      return EMixtormatLayerChildType::PatternId;
		case EMixtormatChildCreation::IdGroup:         return EMixtormatLayerChildType::IdGroup;
		case EMixtormatChildCreation::SurfaceIds:      return EMixtormatLayerChildType::Filter;
		case EMixtormatChildCreation::ClusterIds:      return EMixtormatLayerChildType::Filter;

		case EMixtormatChildCreation::HsvFromIds:      return EMixtormatLayerChildType::HsvFilter;
		case EMixtormatChildCreation::RampFromIds:     return EMixtormatLayerChildType::RampId;
		case EMixtormatChildCreation::UvFromIds:       return EMixtormatLayerChildType::UvFromIds;
		case EMixtormatChildCreation::ReliefFromIds:   return EMixtormatLayerChildType::ReliefFromIds;
		case EMixtormatChildCreation::BoundaryFromIds: return EMixtormatLayerChildType::BoundaryFromIds;
		case EMixtormatChildCreation::GeneratedMask:   return EMixtormatLayerChildType::Generated;
		case EMixtormatChildCreation::ColorIdMask:     return EMixtormatLayerChildType::ColorId;
		case EMixtormatChildCreation::RandomFromIds:   return EMixtormatLayerChildType::RandomId;
		case EMixtormatChildCreation::StrataCarver:    return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::Cracks:          return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::RockFormation:   return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::Pebbles:         return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::CliffStrata:      return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::Noise:            return EMixtormatLayerChildType::Generator;
		case EMixtormatChildCreation::HeightBlend:     return EMixtormatLayerChildType::HeightBlend;
		case EMixtormatChildCreation::HeightCurve:     return EMixtormatLayerChildType::HeightCurve;
		case EMixtormatChildCreation::HeightColorRamp: return EMixtormatLayerChildType::HeightColorRamp;
		case EMixtormatChildCreation::BehaviorWarp:     return EMixtormatLayerChildType::Behavior;
		case EMixtormatChildCreation::BehaviorPush:     return EMixtormatLayerChildType::Behavior;
		case EMixtormatChildCreation::BehaviorCarve:    return EMixtormatLayerChildType::Behavior;
		case EMixtormatChildCreation::BehaviorDeform:   return EMixtormatLayerChildType::Behavior;
		case EMixtormatChildCreation::BehaviorFlowField: return EMixtormatLayerChildType::Behavior;
		case EMixtormatChildCreation::Peeling:         return EMixtormatLayerChildType::Effect;
		default:                                       return EMixtormatLayerChildType::Mask;
		}
	}

	void ApplyChildCreationDefaults(
		FMixtormatLayerChild& Child,
		const EMixtormatChildCreation Kind)
	{
		Child.Type = ChildTypeForCreation(Kind);
		switch (Kind)
		{
		case EMixtormatChildCreation::PatternIds:
			// New Patterns own topology only; keep serialized defaults for existing nodes.
			Child.PatternId.bUVVariation = false;
			Child.PatternId.HeightAmount = 0.0f;
			Child.PatternId.BevelHeight = 0.0f;
			Child.PatternId.GapHeight = 0.0f;
			Child.PatternId.EdgeRoughnessAmount = 0.0f;
			break;
		case EMixtormatChildCreation::SurfaceIds:
			Child.Filter.bSurfaceIds = true;
			break;
		case EMixtormatChildCreation::NoiseMask:
		{
			FMixtormatLayerChild NoiseDefaults;
			NoiseDefaults.Type = EMixtormatLayerChildType::Generator;
			NoiseDefaults.Generator.Type = EMixtormatGeneratorType::Noise;
			MixtormatParameterAuthoring::ApplyAuthoringDefaults(NoiseDefaults);
			Child.Mask.Noise = NoiseDefaults.Generator.Noise;
			Child.Mask.Source = EMixtormatMaskSource::Noise;
			Child.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
			break;
		}
		case EMixtormatChildCreation::LayerValuesMask:
			Child.Mask.Source = EMixtormatMaskSource::LayerValues;
			// Replace, whatever is already on the stack. A new mask is added to be looked at, and
			// Multiply against an existing mask shows nothing wherever that mask is dark -- which
			// reads as the mask having failed to load rather than as two masks combining.
			Child.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
			break;
		case EMixtormatChildCreation::ColorIdMask:
			// One entry to start. A Color Range mask with an empty set selects nothing and is
			// dropped before it reaches the graph, so a new node would otherwise sit in the stack
			// looking broken until the first colour was added by hand. Exact ID ignores the list,
			// and an unused red entry there costs nothing.
			Child.ColorId.Colors.Add(FLinearColor::Red);
			break;
		case EMixtormatChildCreation::StrataCarver:
			Child.Generator.Type = EMixtormatGeneratorType::StrataCarver;
			break;
		case EMixtormatChildCreation::Cracks:
			Child.Generator.Type = EMixtormatGeneratorType::Cracks;
			break;
		case EMixtormatChildCreation::RockFormation:
			Child.Generator.Type = EMixtormatGeneratorType::RockFormation;
			break;
		case EMixtormatChildCreation::Pebbles:
			Child.Generator.Type = EMixtormatGeneratorType::Pebbles;
			break;
		case EMixtormatChildCreation::CliffStrata:
			Child.Generator.Type = EMixtormatGeneratorType::CliffStrata;
			break;
		case EMixtormatChildCreation::Noise:
			Child.Generator.Type = EMixtormatGeneratorType::Noise;
			break;
		case EMixtormatChildCreation::BehaviorPush:
			Child.Behavior.Type = EMixtormatBehaviorType::Push;
			Child.Behavior.Height.Origin = EMixtormatBehaviorFieldOrigin::None;
			break;
		case EMixtormatChildCreation::BehaviorCarve:
			Child.Behavior.Type = EMixtormatBehaviorType::Carve;
			Child.Behavior.Height.Origin = EMixtormatBehaviorFieldOrigin::None;
			break;
		case EMixtormatChildCreation::BehaviorFlowField:
			Child.Behavior.Type = EMixtormatBehaviorType::FlowField;
			break;
		case EMixtormatChildCreation::BehaviorDeform:
			Child.Behavior.Type = EMixtormatBehaviorType::Deform;
			Child.Behavior.Direction.Origin = EMixtormatBehaviorFieldOrigin::None;
			break;
		case EMixtormatChildCreation::Peeling:
			Child.Effect.Effect.Reset();
			Child.Effect.ProceduralType = EMixtormatEffectType::Peeling;
			break;
		default:
			// Every remaining kind is fully described by its type.
			break;
		}

		// Only genuinely-new children pass through here; copies keep their authored values.
		MixtormatParameterAuthoring::ApplyAuthoringDefaults(Child);
	}

	// Writes the Link-mode reference that makes Y the same value as X, using the existing
	// reference system exactly as the context menu does -- Y holds a binding with Mode = Link
	// naming X, so editing either side lands on X and unlinking is the existing Clear Reference.
	// Creation-time only: old saved materials are never touched, and a pair that starts unequal
	// by design gets no link at all.

	void LinkChildPair(
		FMixtormatLayerChild& Child,
		const FGuid& ContainerId,
		const EMixtormatParameterOwnerType Owner,
		const FName XName,
		const FName YName,
		const EMixtormatParameterValueType ValueType)
	{
		FMixtormatParameterAddress Source;
		Source.LayerId = ContainerId;
		Source.ChildId = Child.ChildId;
		Source.Owner = Owner;
		Source.Parameter = XName;
		Source.ValueType = ValueType;

		FMixtormatParameterBinding& Binding = Child.ParameterBindings.AddDefaulted_GetRef();
		Binding.DestinationOwner = Owner;
		Binding.DestinationParameter = YName;
		Binding.ValueType = ValueType;
		Binding.Reference.bEnabled = true;
		Binding.Reference.Mode = EMixtormatReferenceMode::Link;
		Binding.Reference.Source = Source;
	}

	// Which of a new child's X/Y pairs are one uniform quantity split across axes, and therefore
	// start linked. Deliberately short: offsets, Rows/Columns and every min/max range are
	// independent by meaning and stay unlinked.

	void ApplyLinkDefaults(FMixtormatLayerChild& Child, const FGuid& ContainerId)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask:
			// Symmetric tiling. A Layer Values mask has no placement block at all, so it stays out.
			if (Child.Mask.Source == EMixtormatMaskSource::Texture)
			{
				LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Mask,
					GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, TilingX),
					GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, TilingY),
					EMixtormatParameterValueType::Int);
			}
			break;
		case EMixtormatLayerChildType::ColorId:
			// Same symmetric tiling as a texture mask.
			LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::ColorId,
				GET_MEMBER_NAME_CHECKED(FMixtormatColorIdMask, TilingX),
				GET_MEMBER_NAME_CHECKED(FMixtormatColorIdMask, TilingY),
				EMixtormatParameterValueType::Int);
			break;
		case EMixtormatLayerChildType::Blur:
			// Equal radii are the ordinary Gaussian; the pair is anisotropic only on purpose.
			LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Blur,
				GET_MEMBER_NAME_CHECKED(FMixtormatMaskBlur, RadiusX),
				GET_MEMBER_NAME_CHECKED(FMixtormatMaskBlur, RadiusY),
				EMixtormatParameterValueType::Float);
			break;
		case EMixtormatLayerChildType::Effect:
			if (Child.Effect.ProceduralType == EMixtormatEffectType::LayerBlur)
			{
				LinkChildPair(Child, ContainerId, EMixtormatParameterOwnerType::Effect,
					GET_MEMBER_NAME_CHECKED(FMixtormatLayerEffect, LayerBlurRadiusX),
					GET_MEMBER_NAME_CHECKED(FMixtormatLayerEffect, LayerBlurRadiusY),
					EMixtormatParameterValueType::Float);
			}
			break;
		default:
			break;
		}
	}

	bool BuildMaskLayerFromPath(const FSoftObjectPath& MaskPath, FMixtormatMaskLayer& OutMask)
	{
		UObject* MaskObject = MaskPath.TryLoad();
		if (const UMixtormatMask* Mask = Cast<UMixtormatMask>(MaskObject))
		{
			OutMask.Mask = TSoftObjectPtr<UMixtormatMask>(MaskPath);
			OutMask.MaskTexture = TSoftObjectPtr<UTexture2D>(Mask->MaskTexture.Get());
			OutMask.TilingX = FMath::Max(FMath::RoundToInt(Mask->DefaultTiling), 1);
			OutMask.TilingY = OutMask.TilingX;
			OutMask.Shaping.Balance = Mask->DefaultBalance;
			OutMask.Shaping.Contrast = Mask->DefaultContrast;
			OutMask.Shaping.Offset = Mask->DefaultOffset;
			OutMask.Shaping.bInvert = Mask->bDefaultInvert;
			return true;
		}
		if (Cast<UTexture2D>(MaskObject))
		{
			OutMask.MaskTexture = TSoftObjectPtr<UTexture2D>(MaskPath);
			return true;
		}
		return false;
	}
}

FMixtormatLayerEffect* SMixtormat::GetSelectedLayerEffect()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedEffectIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedEffectIndex);
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedLayerEffect() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedEffectIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedEffectIndex);
	return Child.Type == EMixtormatLayerChildType::Effect ? &Child.Effect : nullptr;
}

FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

const FMixtormatMaskLayer* SMixtormat::GetSelectedLayerMask() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Mask ? &Child.Mask : nullptr;
}

int32 SMixtormat::GetSelectedChildIndex() const
{
	if (SelectedLayerIndex == INDEX_NONE && SelectedGroupId.IsValid())
	{
		return SelectedGroupChildIndex;
	}
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return INDEX_NONE;
	}

	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	if (Layer.Children.IsValidIndex(SelectedEffectIndex)
		&& Layer.Children[SelectedEffectIndex].Type == EMixtormatLayerChildType::Effect)
	{
		return SelectedEffectIndex;
	}
	if (Layer.Children.IsValidIndex(SelectedMaskIndex)
		&& (Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Mask
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Generated
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Craquelure
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::ColorId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Filter
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::HsvFilter
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::RandomId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::RampId
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::UvFromIds
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::ReliefFromIds
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::BoundaryFromIds
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::PatternId

			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::IdGroup
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::OutputReference
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Generator
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Blur
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Curvature
			// Generator-layer sublayers are stored through SelectedMaskIndex like every other
			// non-Effect child, so they have to be named here or the resolver chain that reads
			// GetSelectedChildIndex() reports nothing selected for them.
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::HeightBlend
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::HeightCurve
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::HeightColorRamp
			// A Behavior is stored through SelectedMaskIndex too. Without it here the selected
			// address is invalid and every GetSelectedBehavior*() reports nothing selected.
			|| Layer.Children[SelectedMaskIndex].Type == EMixtormatLayerChildType::Behavior))
	{
		return SelectedMaskIndex;
	}
	return INDEX_NONE;
}

bool SMixtormat::IsOutputReferenceAvailable(const FMixtormatChildAddress& Address) const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	return Child && Child->Type == EMixtormatLayerChildType::OutputReference
		&& IsPublishedSourceEnabled(Scope, Address.OwnerId, Address.ChildId)
		&& CanReadPublishedOutputAt(Scope, *Child, Address.OwnerId, ResolveChildIndexAt(Address));
}

bool SMixtormat::CanAddIdGroupSource(
	const FMixtormatChildAddress& Source, const FMixtormatChildAddress& Dest) const
{
	const FMixtormatLayerChild* Producer = ResolveChildAt(Source);
	const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Dest);
	const int32 OwnerIndex = ResolveChildIndexAt(Dest);
	FMixtormatLayerChild Reference;
	if (!Producer || !Children || !Children->IsValidIndex(OwnerIndex)
		|| (*Children)[OwnerIndex].Type != EMixtormatLayerChildType::IdGroup
		|| !CanAddScopedChild(*Children, OwnerIndex)
		|| !MakeRegionIdsReference(*Producer, Source, Reference)) { return false; }
	Reference.ScopeOwnerChildId = Dest.ChildId;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	return IsPublishedSourceEnabled(Scope, Source.OwnerId, Source.ChildId)
		&& IsPublishedSourceEnabled(Scope, Reference.OutputReference.SourceLayerId, Reference.OutputReference.SourceChildId)
		&& CanReadPublishedOutputAt(Scope, Reference, Dest.OwnerId, FindSubtreeEnd(*Children, OwnerIndex));
}

FMixtormatChildAddress SMixtormat::MakeChildAddress(const int32 LayerIndex, const int32 ChildIndex) const
{
	FMixtormatChildAddress Address;
	if (WorkingLayers.IsValidIndex(LayerIndex))
	{
		Address.OwnerType = EMixtormatChildOwnerType::Layer;
		Address.OwnerId = WorkingLayers[LayerIndex].LayerId;
		if (WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
		{
			Address.ChildId = WorkingLayers[LayerIndex].Children[ChildIndex].ChildId;
		}
	}
	return Address;
}

FMixtormatChildAddress SMixtormat::MakeGroupChildAddress(const FGuid GroupId, const int32 ChildIndex) const
{
	FMixtormatChildAddress Address;
	if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId))
	{
		if (Group->Children.IsValidIndex(ChildIndex))
		{
			Address.OwnerType = EMixtormatChildOwnerType::Group;
			Address.OwnerId = GroupId;
			Address.ChildId = Group->Children[ChildIndex].ChildId;
		}
	}
	return Address;
}

FMixtormatChildAddress SMixtormat::MakeSourceChildAddress(const FGuid SourceId) const
{
	FMixtormatChildAddress Address;
	if (const FMixtormatSourceEntry* Entry = WorkingSources.FindByPredicate(
		[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; }))
	{
		Address.OwnerType = EMixtormatChildOwnerType::Source;
		Address.OwnerId = SourceId;
		Address.ChildId = Entry->Child.ChildId;
	}
	return Address;
}

FMixtormatChildAddress SMixtormat::GetSelectedChildAddress() const
{
	// A selected source claims the address first: its lanes are cleared, so the layer/group
	// branches below would build an invalid address and every gate would see "nothing selected".
	if (SelectedSourceId.IsValid())
	{
		return MakeSourceChildAddress(SelectedSourceId);
	}
	const int32 ChildIndex = GetSelectedChildIndex();
	return SelectedGroupId.IsValid() && SelectedLayerIndex == INDEX_NONE
		? MakeGroupChildAddress(SelectedGroupId, ChildIndex)
		: MakeChildAddress(SelectedLayerIndex, ChildIndex);
}

TArray<FMixtormatLayerChild>* SMixtormat::ResolveContainer(const FMixtormatChildAddress& Address)
{
	return const_cast<TArray<FMixtormatLayerChild>*>(
		const_cast<const SMixtormat*>(this)->ResolveContainer(Address));
}

const TArray<FMixtormatLayerChild>* SMixtormat::ResolveContainer(const FMixtormatChildAddress& Address) const
{
	// A source owns its child outright; there is no FMixtormatLayerChild array to return, so
	// container-mutating actions (insert/reorder/scope-owner searches) see "unavailable" and
	// ResolveChildAt handles the Source owner itself below.
	if (Address.OwnerType == EMixtormatChildOwnerType::Source)
	{
		return nullptr;
	}
	if (Address.OwnerType == EMixtormatChildOwnerType::Group)
	{
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Address.OwnerId);
		return Group ? &Group->Children : nullptr;
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId == Address.OwnerId)
		{
			return &Layer.Children;
		}
	}
	return nullptr;
}

FMixtormatLayerChild* SMixtormat::ResolveChildAt(const FMixtormatChildAddress& Address)
{
	return const_cast<FMixtormatLayerChild*>(
		const_cast<const SMixtormat*>(this)->ResolveChildAt(Address));
}

const FMixtormatLayerChild* SMixtormat::ResolveChildAt(const FMixtormatChildAddress& Address) const
{
	// A source's single child lives in its entry, not in a container array.
	if (Address.OwnerType == EMixtormatChildOwnerType::Source)
	{
		const FMixtormatSourceEntry* Entry = WorkingSources.FindByPredicate(
			[&Address](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == Address.OwnerId; });
		return Entry && Entry->Child.ChildId == Address.ChildId ? &Entry->Child : nullptr;
	}
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	if (!Container)
	{
		return nullptr;
	}
	return Container->FindByPredicate([&Address](const FMixtormatLayerChild& Child)
	{
		return Child.ChildId == Address.ChildId;
	});
}

int32 SMixtormat::ResolveChildIndexAt(const FMixtormatChildAddress& Address) const
{
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	return Container
		? Container->IndexOfByPredicate([&Address](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == Address.ChildId;
		})
		: INDEX_NONE;
}

void SMixtormat::SyncChildInstances()
{
	FGuid SelectedChildId;
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FMixtormatLayer& SelectedLayer = WorkingLayers[SelectedLayerIndex];
		const int32 SelectedChildIndex = GetSelectedChildIndex();
		if (SelectedLayer.Children.IsValidIndex(SelectedChildIndex))
		{
			SelectedChildId = SelectedLayer.Children[SelectedChildIndex].ChildId;
		}
	}

	// Against a snapshot: an instance may name a child in a layer this loop has already rewritten,
	// and a chain has to read authored sources rather than half-updated mirrors.
	const TArray<FMixtormatLayer> Snapshot = WorkingLayers;
	const TArray<FMixtormatLayerGroup> GroupSnapshot = WorkingLayerGroups;
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		MixtormatParameterBinding::ResolveChildInstances(
			FMixtormatBindingScope{Snapshot, GroupSnapshot}, Layer);
	}
	// A group's own shared children can be instances too now that Copy as Instance can target a
	// group (see PasteChild) -- ResolveChildInstances only ever mirrors a layer's Children, so each
	// group's stack is wrapped in a scratch layer to reuse it rather than a second resolver.
	for (FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		FMixtormatLayer Scratch;
		Scratch.Children = Group.Children;
		MixtormatParameterBinding::ResolveChildInstances(
			FMixtormatBindingScope{Snapshot, GroupSnapshot}, Scratch);
		Group.Children = MoveTemp(Scratch.Children);
	}

	// An instance source can change its payload kind. Keep selection on the same identity and move
	// it to the matching effect/mask selection lane after the mirror updates.
	if (SelectedChildId.IsValid() && WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		const FMixtormatLayer& SelectedLayer = WorkingLayers[SelectedLayerIndex];
		const int32 SelectedChildIndex = FindChildById(SelectedLayer.Children, SelectedChildId);
		if (SelectedLayer.Children.IsValidIndex(SelectedChildIndex))
		{
			const bool bEffect = SelectedLayer.Children[SelectedChildIndex].Type
				== EMixtormatLayerChildType::Effect;
			SelectedEffectIndex = bEffect ? SelectedChildIndex : INDEX_NONE;
			SelectedMaskIndex = bEffect ? INDEX_NONE : SelectedChildIndex;
		}
	}
}

bool SMixtormat::IsSelectedChildInstance() const
{
	const FMixtormatChildAddress Address = GetSelectedChildAddress();
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	return Child && Child->IsInstance();
}

FText SMixtormat::GetSelectedInstanceSourceText() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	if (!Child || !Child->IsInstance())
	{
		return FText::GetEmpty();
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId == Child->SourceLayerId)
		{
			if (const FMixtormatLayerChild* Source = Layer.Children.FindByPredicate(
				[Child](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Child->SourceChildId; }))
			{
				return FText::Format(
					LOCTEXT("InstanceSourceLine", "Source: {0} / {1}"),
					Layer.DisplayName,
					GetLayerChildName(*Source));
			}
		}
	}
	if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Child->SourceLayerId))
	{
		if (const FMixtormatLayerChild* Source = Group->Children.FindByPredicate(
			[Child](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Child->SourceChildId; }))
		{
			return FText::Format(
				LOCTEXT("InstanceGroupSourceLine", "Source: {0} / {1}"),
				Group->DisplayName,
				GetLayerChildName(*Source));
		}
	}
	return LOCTEXT("InstanceSourceBroken", "Source is missing. Showing the last values it gave.");
}

bool SMixtormat::IsParameterLocked(const FMixtormatParameterAddress& Target) const
{
	if (!Target.IsValid() || !Target.ChildId.IsValid())
	{
		return false;
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Target.LayerId)
		{
			continue;
		}
		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			if (Child.ChildId == Target.ChildId)
			{
				if (!Child.IsInstance())
				{
					return false;
				}
				const bool bLocalMaskBlend = Child.Type == EMixtormatLayerChildType::Mask
					&& Target.Owner == EMixtormatParameterOwnerType::Mask
					&& Target.Parameter == GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, BlendMode);
				const bool bLocalMaskInvert = Child.Type == EMixtormatLayerChildType::Mask
					&& Target.Owner == EMixtormatParameterOwnerType::MaskShaping
					&& Target.Parameter == GET_MEMBER_NAME_CHECKED(FMixtormatMaskShaping, bInvert);
				return !bLocalMaskBlend && !bLocalMaskInvert;
			}
		}
	}
	return false;
}

FText SMixtormat::GetLayerChildName(const FMixtormatLayerChild& Child) const
{
	// An instance is named for what it shows, marked for what it is. The arrow is the whole
	// difference in the stack -- an instance row is otherwise the same row as its source, which is
	// the point of it.
	if (Child.IsInstance())
	{
		FMixtormatLayerChild Named = Child;
		Named.SourceLayerId = FGuid();
		Named.SourceChildId = FGuid();
		return FText::Format(
			LOCTEXT("InstanceChildName", "{0} {1}"),
			FText::FromString(TEXT("\u2197")),
			GetLayerChildName(Named));
	}

	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.UsesNoise())
	{
		return Child.ScopeOwnerChildId.IsValid()
			? LOCTEXT("NoiseGateName", "Noise Gate") : LOCTEXT("NoiseMaskName", "Noise Mask");
	}

	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.UsesLayerValues())
	{
		// Named for the channel, because there is no asset to name it after and two of them on
		// one layer differ by nothing else.
		return FText::Format(
			LOCTEXT("LayerValuesMaskName", "Layer {0}"),
			MixtormatUI::LayerValueChannelText(Child.Mask.LayerValueChannel));
	}

	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.HasPublishedSource())
	{
		const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(
			FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
			Child.Mask.PublishedSourceLayerId, Child.Mask.PublishedSourceChildId);
		if (Child.Mask.PublishedSourceOutput == TEXT("Value") && Source
			&& Source->Type == EMixtormatLayerChildType::Generator
			&& Source->Generator.Type == EMixtormatGeneratorType::Noise)
		{
			return Child.ScopeOwnerChildId.IsValid() ? LOCTEXT("NoiseValueGateName", "Noise Value Gate")
				: LOCTEXT("NoiseValueMaskName", "Noise Value Mask");
		}
		return Child.Mask.PublishedSourceOutput == TEXT("Wear")
			? LOCTEXT("PublishedWearMaskName", "Wear Mask")
			: FText::Format(
				LOCTEXT("PublishedMaskName", "{0} Mask"),
				FText::FromName(Child.Mask.PublishedSourceOutput));
	}

	if (Child.Type == EMixtormatLayerChildType::Effect)
	{
		if (!Child.Effect.Effect.IsNull())
		{
			switch (ResolveChildEffectType(Child))
			{
			case EMixtormatEffectType::Peeling: return LOCTEXT("PeelingEffectName", "Peeling");
			case EMixtormatEffectType::Stain:
				return Child.Effect.StainMode == EMixtormatStainMode::Deposit
					? LOCTEXT("DepositStainEffectName", "Stain Deposit")
					: LOCTEXT("WetStainEffectName", "Wet Stain");
			case EMixtormatEffectType::Grade:   return LOCTEXT("GradeEffectName", "Grade");
			case EMixtormatEffectType::Breakup: return LOCTEXT("BreakupEffectName", "Breakup");
			case EMixtormatEffectType::WornEdges: return LOCTEXT("WornEdgesEffectName", "Worn Edges");
			case EMixtormatEffectType::FlowWarp: return LOCTEXT("FlowWarpEffectName", "Flow Warp");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("LayerBlurEffectName", "Layer Blur");
			case EMixtormatEffectType::Runoff:  return LOCTEXT("RunoffEffectName", "Runoff");
			default:                            return LOCTEXT("ErosionEffectName", "Erosion");
			}
		}
		switch (Child.Effect.ProceduralType)
		{
		case EMixtormatEffectType::Stain:
			return Child.Effect.StainMode == EMixtormatStainMode::Deposit
				? LOCTEXT("DepositStainEffectName", "Stain Deposit")
				: LOCTEXT("WetStainEffectName", "Wet Stain");
		case EMixtormatEffectType::Erosion: return LOCTEXT("ErosionEffectName", "Erosion");
		case EMixtormatEffectType::Grade:   return LOCTEXT("GradeEffectName", "Grade");
		case EMixtormatEffectType::Breakup: return LOCTEXT("BreakupEffectName", "Breakup");
		case EMixtormatEffectType::WornEdges: return LOCTEXT("WornEdgesEffectName", "Worn Edges");
		case EMixtormatEffectType::FlowWarp: return LOCTEXT("FlowWarpEffectName", "Flow Warp");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("LayerBlurEffectName", "Layer Blur");
		case EMixtormatEffectType::Runoff:  return LOCTEXT("RunoffEffectName", "Runoff");
		default:                            return LOCTEXT("ProceduralPeelName", "Peeling");
		}
	}
	if (Child.Type == EMixtormatLayerChildType::Behavior)
	{
		switch (Child.Behavior.Type)
		{
		case EMixtormatBehaviorType::Warp: return LOCTEXT("BehaviorWarpChildName", "Warp");
		case EMixtormatBehaviorType::Push: return LOCTEXT("BehaviorPushChildName", "Push");
		case EMixtormatBehaviorType::Carve: return LOCTEXT("BehaviorCarveChildName", "Carve / Deposit");
		case EMixtormatBehaviorType::Deform: return LOCTEXT("BehaviorDeformChildName", "Deform");
		case EMixtormatBehaviorType::FlowField: return LOCTEXT("BehaviorFlowFieldChildName", "Flow Field");
		default: return LOCTEXT("BehaviorChildName", "Behavior");
		}
	}
	if (Child.Type == EMixtormatLayerChildType::Generated)
	{
		return LOCTEXT("GeneratedChildName", "Generated Mask");
	}
	if (Child.Type == EMixtormatLayerChildType::Craquelure)
	{
		return LOCTEXT("CraquelureChildName", "Craquelure");
	}
	if (Child.Type == EMixtormatLayerChildType::Generator)
	{
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			return LOCTEXT("StrataCarverChildName", "Strata Carver");
		case EMixtormatGeneratorType::Cracks:
			return LOCTEXT("CracksChildName", "Cracks");
		case EMixtormatGeneratorType::RockFormation:
			return LOCTEXT("RockFormationChildName", "Rock Formation");
		case EMixtormatGeneratorType::Pebbles:
			return LOCTEXT("PebblesChildName", "Pebbles");
		case EMixtormatGeneratorType::CliffStrata:
			return LOCTEXT("CliffStrataChildName", "Cliff Strata");
		case EMixtormatGeneratorType::Noise:
			return LOCTEXT("NoiseChildName", "Noise");
		}
		return LOCTEXT("GeneratorChildName", "Generator");
	}
	if (Child.Type == EMixtormatLayerChildType::Filter)
	{
		return Child.Filter.bSurfaceIds
			? LOCTEXT("SurfaceIdsChildName", "Surface IDs")
			: LOCTEXT("ClusterFilterChildName", "Cluster IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::HsvFilter)
	{
		return LOCTEXT("HsvFilterChildName", "HSV From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::RandomId)
	{
		return LOCTEXT("RandomIdChildName", "Random From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::RampId)
	{
		return LOCTEXT("RampIdChildName", "Ramp From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::UvFromIds)
	{
		return LOCTEXT("UvIdChildName", "UV From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::ReliefFromIds)
	{
		return LOCTEXT("ReliefIdChildName", "Relief From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::BoundaryFromIds)
	{
		return LOCTEXT("BoundaryIdChildName", "Boundary From IDs");
	}
	if (Child.Type == EMixtormatLayerChildType::PatternId)
	{
		return LOCTEXT("PatternIdChildName", "Pattern IDs");
	}

	if (Child.Type == EMixtormatLayerChildType::IdGroup)
	{
		return LOCTEXT("IdGroupChildName", "ID Group");
	}
	if (Child.Type == EMixtormatLayerChildType::OutputReference)
	{
		switch (Child.OutputReference.Kind)
		{
		case EMixtormatPublishedFieldKind::RegionIds:
		{
			const FMixtormatLayerChild* Source = &Child;
			TSet<FGuid> Visited;
			while (Source && IsRegionIdsReference(*Source))
			{
				if (Visited.Contains(Source->ChildId))
				{
					return LOCTEXT("IdsReferenceCycleName", "Cyclic source › Region IDs");
				}
				Visited.Add(Source->ChildId);
				Source = MixtormatParameterBinding::FindChild(FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
					Source->OutputReference.SourceLayerId, Source->OutputReference.SourceChildId);
			}
			return FText::Format(LOCTEXT("IdsReferenceProducerName", "{0} › Region IDs"),
				Source ? GetLayerChildName(*Source) : LOCTEXT("IdsReferenceMissingSource", "Missing source"));
		}
		case EMixtormatPublishedFieldKind::Flow:      return LOCTEXT("FlowReferenceChildName", "Flow Reference");
		case EMixtormatPublishedFieldKind::UVMap:     return LOCTEXT("UvReferenceChildName", "UVs Reference");
		case EMixtormatPublishedFieldKind::Color:     return LOCTEXT("ColorReferenceChildName", "Color Reference");
		case EMixtormatPublishedFieldKind::Scalar01:     return LOCTEXT("Scalar01ReferenceChildName", "Scalar 0..1 Reference");
		case EMixtormatPublishedFieldKind::ScalarSigned: return LOCTEXT("ScalarSignedReferenceChildName", "Signed Scalar Reference");
		case EMixtormatPublishedFieldKind::SDF:          return LOCTEXT("SdfReferenceChildName", "Signed Distance Reference");
		case EMixtormatPublishedFieldKind::Vector2:      return LOCTEXT("Vector2ReferenceChildName", "Vector Reference");
		}
		return LOCTEXT("OutputReferenceChildName", "Output Reference");
	}
	if (Child.Type == EMixtormatLayerChildType::Blur)
	{
		// Named for what it does rather than for the mask it is on: the row sits indented under
		// that mask already, so repeating its name would be the only thing on the line.
		return LOCTEXT("BlurChildName", "Blur");
	}
	if (Child.Type == EMixtormatLayerChildType::Curvature)
	{
		return LOCTEXT("CurvatureChildName", "Curvature");
	}
	if (Child.Type == EMixtormatLayerChildType::ColorId)
	{
		// Named after its map rather than after itself, the way a painted mask row is: two id
		// nodes on one layer are two selections out of the same map, and "Color ID" twice says
		// nothing about which is which.
		const FSoftObjectPath IdPath = Child.ColorId.IdTexture.ToSoftObjectPath();
		return IdPath.IsNull()
			? LOCTEXT("ColorIdChildName", "Color ID")
			: FText::FromString(IdPath.GetAssetName());
	}
	if (Child.Type == EMixtormatLayerChildType::HeightBlend)
	{
		return LOCTEXT("HeightBlendChildName", "Height Blend");
	}
	if (Child.Type == EMixtormatLayerChildType::HeightCurve)
	{
		return LOCTEXT("HeightCurveChildName", "Height Remap");
	}
	if (Child.Type == EMixtormatLayerChildType::HeightColorRamp)
	{
		return LOCTEXT("HeightColorRampChildName", "Color Ramp");
	}
	const FSoftObjectPath MaskPath = !Child.Mask.Mask.IsNull()
		? Child.Mask.Mask.ToSoftObjectPath()
		: Child.Mask.MaskTexture.ToSoftObjectPath();
	return FText::FromString(MaskPath.GetAssetName());
}

FMixtormatLayerChild* SMixtormat::ResolveChild(const int32 LayerIndex, const int32 ChildIndex)
{
	return const_cast<FMixtormatLayerChild*>(
		const_cast<const SMixtormat*>(this)->ResolveChild(LayerIndex, ChildIndex));
}

const FMixtormatLayerChild* SMixtormat::ResolveChild(
	const int32 LayerIndex,
	const int32 ChildIndex) const
{
	if (LayerIndex == INDEX_NONE)
	{
		// No layer, but a group selected: the child is one of that group's shared children.
		const FMixtormatLayerGroup* Group =
			MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SelectedGroupId);
		return Group && Group->Children.IsValidIndex(ChildIndex)
			? &Group->Children[ChildIndex]
			: nullptr;
	}
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		? &WorkingLayers[LayerIndex].Children[ChildIndex]
		: nullptr;
}

FMixtormatLayerChild* SMixtormat::AppendGroupChild(
	const FGuid GroupId,
	const EMixtormatLayerChildType ChildType)
{
	// A Behavior names its generator through ScopeOwnerChildId, and a shared group's generator
	// set is not one any member owns, so a group can never host one. Structural modules used to
	// be layer-local for the same reason.
	if (ChildType == EMixtormatLayerChildType::Behavior) { return nullptr; }
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group)
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = Group->Children.AddDefaulted_GetRef();
	Child.Type = ChildType;
	return &Child;
}

// SelectIndex names the child to leave selected. INDEX_NONE means "the one just appended", which
// is what every append path wants; a scoped insert lands in the middle and passes its own.
void SMixtormat::FinishGroupChildEdit(const FGuid GroupId, const int32 SelectIndex)
{
	// A shared child is broadcast to every member, so unlike a rename this genuinely changes what
	// the compositor draws and has to ask for a new composite.
	// SelectGroupChild, not SelectLayerGroup: the getters the inspector is built from read
	// SelectedMaskIndex / SelectedEffectIndex against a cleared layer index, and SelectGroupChild is
	// what points those lanes at the group's stack. Setting SelectedGroupChildIndex alone left the
	// panel with nothing to show, which is why a shared child added here appeared but could not be
	// edited.
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	const int32 ResolvedIndex = SelectIndex != INDEX_NONE
		? SelectIndex
		: (Group ? Group->Children.Num() - 1 : INDEX_NONE);
	SelectGroupChild(GroupId, ResolvedIndex);
	CollapsedGroupIds.Remove(GroupId);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview();
	RebuildLayerList();
}

bool SMixtormat::CanCreateChild(const FMixtormatAddTarget& Target) const
{
	const FMixtormatLayerGroup* Group = Target.IsGroup()
		? MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Target.GroupId) : nullptr;
	const TArray<FMixtormatLayerChild>* Children = Target.IsGroup()
		? (Group ? &Group->Children : nullptr)
		: (WorkingLayers.IsValidIndex(Target.LayerIndex) ? &WorkingLayers[Target.LayerIndex].Children : nullptr);
	if (!Children)
	{
		return false;
	}
	if (!Target.ScopeOwnerChildId.IsValid())
	{
		return true;
	}
	const int32 OwnerIndex = FindChildById(*Children, Target.ScopeOwnerChildId);
	return Children->IsValidIndex(OwnerIndex)
		&& (*Children)[OwnerIndex].Type == EMixtormatLayerChildType::IdGroup
		&& CanAddScopedChild(*Children, OwnerIndex);
}

// Modules live only at the root of a Generator layer; groups and material layers never host them.
bool SMixtormat::CanAddGeneratorModule(const FMixtormatAddTarget& Target) const
{
	return !Target.IsGroup() && !Target.ScopeOwnerChildId.IsValid()
		&& WorkingLayers.IsValidIndex(Target.LayerIndex)
		&& WorkingLayers[Target.LayerIndex].Type == EMixtormatLayerType::Generator;
}

FMixtormatMaskCurvature* SMixtormat::GetSelectedLayerCurvature()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Curvature ? &Child.Curvature : nullptr;
}

const FMixtormatMaskCurvature* SMixtormat::GetSelectedLayerCurvature() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child =
		*ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Curvature ? &Child.Curvature : nullptr;
}

FMixtormatMaskBlur* SMixtormat::GetSelectedLayerBlur()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Blur ? &Child.Blur : nullptr;
}

const FMixtormatMaskBlur* SMixtormat::GetSelectedLayerBlur() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child =
		*ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Blur ? &Child.Blur : nullptr;
}

TSharedRef<SWidget> SMixtormat::BuildMaskBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();

	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor::Transparent)
		.Visibility_Lambda([this]()
		{
			return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(Resolved.GalleryLayout.MaskHeaderInset, Resolved.GalleryLayout.HeaderGap,
				Resolved.GalleryLayout.TilePadding, 0.0f))
			[
				SNew(SBox)
				.MinDesiredHeight(Resolved.Buttons.Height)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MaskGalleryHeading", "MASKS"))
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
						.ToolTipText(LOCTEXT("MaskBarHeading", "MASKS · SELECT, THEN RMB A LAYER OR EFFECT"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(Resolved.GalleryLayout.HeaderGap, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(112.0f)
						[
							SNew(STextBlock)
							.Visibility_Lambda([this]()
							{
								return SelectedMaskPath.IsNull() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
							})
							.Text_Lambda([this]() { return SelectedLibraryMaskName; })
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SMixtormatIconButton)
						.Role(Mixtormat::EMixtormatIconRole::GalleryToolbar)
						.Icon(MixtormatIcons::Folder())
						.ToolTip(LOCTEXT("ImportUserMasksHint", "Import PNG masks from a folder"))
						.OnClicked(FSimpleDelegate::CreateLambda([this]()
						{
							ImportMasks();
						}))
					]
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(Resolved.GalleryLayout.TilePadding)
			[
				SNew(SMixtormatGalleryScrollBox)
				.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
				[
					SAssignNew(MaskListBox, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(
						Resolved.GalleryLayout.TileGap,
						Resolved.GalleryLayout.TileGap))
				]
			]
		];
}

void SMixtormat::ZoomMaskGallery(const int32 Direction)
{
	MaskGalleryTileSize = FMath::Clamp(
		MaskGalleryTileSize + Direction * MixtormatTokens::MaskGalleryTileStep,
		MixtormatTokens::MaskGalleryTileMinimum,
		FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileMaximum);
	RebuildMaskList();
}

TSharedRef<SWidget> SMixtormat::BuildMaskGallery(TFunction<void(const FSoftObjectPath&)> OnChosen)
{
	// One grid, whether the mask is being added or replaced. Picking a mask is the same question
	// both times, and it was answered two ways: a gallery for replacing, a list of names for
	// adding -- so the choice you made blind was the one that created the thing.
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(
			FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileGap,
			FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileGap));

	for (const FMixtormatMaskEntry& Mask : FMixtormatRegistry::GetMasks())
	{
		const FSoftObjectPath Path = Mask.AssetPath;
		Grid->AddSlot()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SMixtormatTile)
				.TileSize_Lambda([this]() { return MaskGalleryTileSize; })
				.DisplayName(Mask.DisplayName)
				.ThumbnailAsset(Mask.ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.ThumbnailResolution(FMath::RoundToInt(FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileMaximum))
				.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
				.OnActivated(FMixtormatOnTileActivated::CreateLambda([OnChosen, Path]()
				{
					FSlateApplication::Get().DismissAllMenus();
					OnChosen(Path);
				}))
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
			[
				MixtormatUI::BuildLibraryOwnershipBadge(Path, false)
			]
		];
	}
	return Grid;
}

TSharedRef<SWidget> SMixtormat::BuildMaskCard(
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset,
	const bool bCompact)
{
	const float CardSize = bCompact ? MaskGalleryTileSize : 52.0f;
	const float ThumbnailSize = bCompact ? FMath::Max(CardSize - 2.0f, 1.0f) : 42.0f;
	TSharedRef<SWidget> ThumbnailWidget = SNew(SBorder)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ThumbnailBackground")));
	if (ThumbnailAsset.IsValid())
	{
		UObject* ThumbnailObject = ThumbnailAsset.GetAsset();
		UTexture2D* ThumbnailTexture = Cast<UTexture2D>(ThumbnailObject);
		if (const UMixtormatMask* MaskAsset = Cast<UMixtormatMask>(ThumbnailObject))
		{
			ThumbnailTexture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
		}
		if (ThumbnailTexture)
		{
			ThumbnailWidget = SNew(SMixtormatTextureTile)
				.Texture(ThumbnailTexture)
				.ImageSize(FVector2D(ThumbnailSize, ThumbnailSize));
		}
	}

	if (bCompact)
	{
		return SNew(SMixtormatMaskCard)
			.DisplayName(Name)
			.MaskPath(AssetPath)
			.ThumbnailAsset(ThumbnailAsset)
			.ThumbnailPool(ThumbnailPool)
			.OnSelected(this, &SMixtormat::SelectMask)
			.OnGalleryZoom(this, &SMixtormat::ZoomMaskGallery)
			.OnGetContextMenu(this, &SMixtormat::BuildMaskLibraryContextMenu, AssetPath)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SMixtormatTile)
					.TileSize_Lambda([this]() { return MaskGalleryTileSize; })
					.DisplayName(Name)
					.ThumbnailAsset(ThumbnailAsset)
					.ThumbnailPool(ThumbnailPool)
					.ThumbnailResolution(FMath::RoundToInt(FMixtormatThemeStore::GetResolved().ControlLayout.MaskGalleryTileMaximum))
					.bShowName(false)
					.bShowNameOnHover(false)
					.bSelected_Lambda([this, AssetPath]() { return SelectedMaskPath == AssetPath; })
					.ToolTip(Name)
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
				[
					MixtormatUI::BuildLibraryOwnershipBadge(AssetPath, false)
				]
			];
	}

	return SNew(SButton)
		.ContentPadding(5.0f)
		.OnClicked_Lambda([this, Name, AssetPath]() { return SelectMask(Name, AssetPath); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ThumbnailWidget]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(2.0f)
					[
						MixtormatUI::BuildLibraryOwnershipBadge(AssetPath, false)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Name)
			]
		];
}

FMixtormatColorIdMask* SMixtormat::GetSelectedColorId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::ColorId ? &Child.ColorId : nullptr;
}

const FMixtormatColorIdMask* SMixtormat::GetSelectedColorId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::ColorId ? &Child.ColorId : nullptr;
}

FMixtormatClusterFilter* SMixtormat::GetSelectedFilter()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Filter ? &Child.Filter : nullptr;
}

const FMixtormatClusterFilter* SMixtormat::GetSelectedFilter() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Filter ? &Child.Filter : nullptr;
}

FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::HsvFilter ? &Child.HsvFilter : nullptr;
}

const FMixtormatHsvIdFilter* SMixtormat::GetSelectedHsvFilter() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::HsvFilter ? &Child.HsvFilter : nullptr;
}

FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::PatternId ? &Child.PatternId : nullptr;
}

const FMixtormatPatternFilter* SMixtormat::GetSelectedPatternId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::PatternId ? &Child.PatternId : nullptr;
}

FMixtormatStrataCarver* SMixtormat::GetSelectedStrataCarver()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::StrataCarver
		? &Generator->StrataCarver : nullptr;
}

const FMixtormatStrataCarver* SMixtormat::GetSelectedStrataCarver() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::StrataCarver
		? &Generator->StrataCarver : nullptr;
}

FMixtormatCracks* SMixtormat::GetSelectedCracks()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Cracks
		? &Generator->Cracks : nullptr;
}

FMixtormatRockFormation* SMixtormat::GetSelectedRockFormation()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::RockFormation
		? &Generator->RockFormation : nullptr;
}

const FMixtormatRockFormation* SMixtormat::GetSelectedRockFormation() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::RockFormation
		? &Generator->RockFormation : nullptr;
}

FMixtormatPebbles* SMixtormat::GetSelectedPebbles()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Pebbles
		? &Generator->Pebbles : nullptr;
}

const FMixtormatPebbles* SMixtormat::GetSelectedPebbles() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Pebbles
		? &Generator->Pebbles : nullptr;
}

FMixtormatCliffStrata* SMixtormat::GetSelectedCliffStrata()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::CliffStrata ? &Generator->CliffStrata : nullptr;
}
const FMixtormatCliffStrata* SMixtormat::GetSelectedCliffStrata() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::CliffStrata ? &Generator->CliffStrata : nullptr;
}

FMixtormatNoise* SMixtormat::GetSelectedNoise()
{
	FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Noise ? &Generator->Noise : nullptr;
}

const FMixtormatNoise* SMixtormat::GetSelectedNoise() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Noise ? &Generator->Noise : nullptr;
}

FMixtormatGeneratorHeightBlend* SMixtormat::GetSelectedHeightBlend()
{
	return const_cast<FMixtormatGeneratorHeightBlend*>(
		static_cast<const SMixtormat*>(this)->GetSelectedHeightBlend());
}

const FMixtormatGeneratorHeightBlend* SMixtormat::GetSelectedHeightBlend() const
{
	if (const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
	{
		return Child->Type == EMixtormatLayerChildType::HeightBlend ? &Child->HeightBlend : nullptr;
	}
	return nullptr;
}



FMixtormatBehavior* SMixtormat::GetSelectedBehaviorWarp()
{
	return const_cast<FMixtormatBehavior*>(
		static_cast<const SMixtormat*>(this)->GetSelectedBehaviorWarp());
}

const FMixtormatBehavior* SMixtormat::GetSelectedBehaviorWarp() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Behavior
		&& Child->Behavior.Type == EMixtormatBehaviorType::Warp ? &Child->Behavior : nullptr;
}

FMixtormatBehavior* SMixtormat::GetSelectedBehaviorPush()
{
	return const_cast<FMixtormatBehavior*>(
		static_cast<const SMixtormat*>(this)->GetSelectedBehaviorPush());
}
const FMixtormatBehavior* SMixtormat::GetSelectedBehaviorPush() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Behavior
		&& Child->Behavior.Type == EMixtormatBehaviorType::Push ? &Child->Behavior : nullptr;
}

FMixtormatBehavior* SMixtormat::GetSelectedBehaviorDeform()
{
	return const_cast<FMixtormatBehavior*>(
		static_cast<const SMixtormat*>(this)->GetSelectedBehaviorDeform());
}
const FMixtormatBehavior* SMixtormat::GetSelectedBehaviorDeform() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Behavior
		&& Child->Behavior.Type == EMixtormatBehaviorType::Deform ? &Child->Behavior : nullptr;
}

FMixtormatBehavior* SMixtormat::GetSelectedBehaviorCarve()
{
	return const_cast<FMixtormatBehavior*>(
		static_cast<const SMixtormat*>(this)->GetSelectedBehaviorCarve());
}
const FMixtormatBehavior* SMixtormat::GetSelectedBehaviorCarve() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Behavior
		&& Child->Behavior.Type == EMixtormatBehaviorType::Carve ? &Child->Behavior : nullptr;
}


FMixtormatGeneratorHeightCurve* SMixtormat::GetSelectedHeightCurve()
{
	return const_cast<FMixtormatGeneratorHeightCurve*>(
		static_cast<const SMixtormat*>(this)->GetSelectedHeightCurve());
}

const FMixtormatGeneratorHeightCurve* SMixtormat::GetSelectedHeightCurve() const
{
	if (const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
	{
		return Child->Type == EMixtormatLayerChildType::HeightCurve ? &Child->HeightCurve : nullptr;
	}
	return nullptr;
}

FMixtormatGeneratorHeightColorRamp* SMixtormat::GetSelectedHeightColorRamp()
{
	return const_cast<FMixtormatGeneratorHeightColorRamp*>(
		static_cast<const SMixtormat*>(this)->GetSelectedHeightColorRamp());
}

const FMixtormatGeneratorHeightColorRamp* SMixtormat::GetSelectedHeightColorRamp() const
{
	if (const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
	{
		return Child->Type == EMixtormatLayerChildType::HeightColorRamp ? &Child->HeightColorRamp : nullptr;
	}
	return nullptr;
}

const FMixtormatCracks* SMixtormat::GetSelectedCracks() const
{
	const FMixtormatGenerator* Generator = GetSelectedGenerator();
	return Generator && Generator->Type == EMixtormatGeneratorType::Cracks
		? &Generator->Cracks : nullptr;
}

// True for any generator, whatever kind. The inspector's two visibility lists ask this rather
// than each generator's own getter, so adding a generator does not mean remembering to extend
// two lambdas that fail silently -- a missed entry shows the new panel *and* the layer's own
// sections at the same time, which is what a forgotten one looks like.
bool SMixtormat::HasSelectedGenerator() const
{
	// A selected source claims the inspector through the same category test a layer generator
	// uses, so its parameter panel opens instead of the layer's sections.
	if (GetSelectedSource())
	{
		return true;
	}
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::Generator;
}

FMixtormatGenerator* SMixtormat::GetSelectedGenerator()
{
	return const_cast<FMixtormatGenerator*>(static_cast<const SMixtormat*>(this)->GetSelectedGenerator());
}

const FMixtormatGenerator* SMixtormat::GetSelectedGenerator() const
{
	// A selected Sources shelf entry resolves its own generator payload, so the generator
	// parameter panels serve sources through this same accessor. Everything below reads the
	// layer stack and stays out of the way.
	if (const FMixtormatSourceEntry* Source = GetSelectedSource())
	{
		return Source->Child.Type == EMixtormatLayerChildType::Generator
			? &Source->Child.Generator : nullptr;
	}
	if (const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
	{
		return Child->Type == EMixtormatLayerChildType::Generator ? &Child->Generator : nullptr;
	}
	return nullptr;
}

FMixtormatIdGroup* SMixtormat::GetSelectedIdGroup()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::IdGroup ? &Child.IdGroup : nullptr;
}

const FMixtormatIdGroup* SMixtormat::GetSelectedIdGroup() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::IdGroup ? &Child.IdGroup : nullptr;
}


FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RampId ? &Child.RampId : nullptr;
}

const FMixtormatRampIdFilter* SMixtormat::GetSelectedRampId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RampId ? &Child.RampId : nullptr;
}

FMixtormatUvIdFilter* SMixtormat::GetSelectedUvId()
{
	FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::UvFromIds ? &Child->UvId : nullptr;
}

const FMixtormatUvIdFilter* SMixtormat::GetSelectedUvId() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::UvFromIds ? &Child->UvId : nullptr;
}

FMixtormatReliefIdFilter* SMixtormat::GetSelectedReliefId()
{
	FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::ReliefFromIds ? &Child->ReliefId : nullptr;
}

const FMixtormatReliefIdFilter* SMixtormat::GetSelectedReliefId() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::ReliefFromIds ? &Child->ReliefId : nullptr;
}

FMixtormatBoundaryIdFilter* SMixtormat::GetSelectedBoundaryId()
{
	FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::BoundaryFromIds ? &Child->BoundaryId : nullptr;
}

const FMixtormatBoundaryIdFilter* SMixtormat::GetSelectedBoundaryId() const
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child && Child->Type == EMixtormatLayerChildType::BoundaryFromIds ? &Child->BoundaryId : nullptr;
}

FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RandomId ? &Child.RandomId : nullptr;
}

const FMixtormatRandomIdMask* SMixtormat::GetSelectedRandomId() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::RandomId ? &Child.RandomId : nullptr;
}

FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
}

const FMixtormatCraquelure* SMixtormat::GetSelectedCraquelure() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Craquelure ? &Child.Craquelure : nullptr;
}

FMixtormatGeneratedMask* SMixtormat::GetSelectedGeneratedMask()
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generated ? &Child.Generated : nullptr;
}

const FMixtormatGeneratedMask* SMixtormat::GetSelectedGeneratedMask() const
{
	if (!ResolveChild(SelectedLayerIndex, SelectedMaskIndex))
	{
		return nullptr;
	}
	const FMixtormatLayerChild& Child = *ResolveChild(SelectedLayerIndex, SelectedMaskIndex);
	return Child.Type == EMixtormatLayerChildType::Generated ? &Child.Generated : nullptr;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedStain()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Stain
		: Effect->ProceduralType != EMixtormatEffectType::Stain))
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedStain() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Stain
		: Effect->ProceduralType != EMixtormatEffectType::Stain))
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedRunoff()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Runoff
		: Effect->ProceduralType != EMixtormatEffectType::Runoff))
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedRunoff() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	const UMixtormatEffect* Asset = Effect ? Effect->Effect.LoadSynchronous() : nullptr;
	if (!Effect || (Asset
		? Asset->EffectType != EMixtormatEffectType::Runoff
		: Effect->ProceduralType != EMixtormatEffectType::Runoff))
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedGrade()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Grade)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedGrade() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Grade)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedBreakup()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Breakup)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedBreakup() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Breakup)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedWornEdges()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::WornEdges)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedWornEdges() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::WornEdges)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedFlowWarp()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::FlowWarp
		? Effect
		: nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedFlowWarp() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::FlowWarp
		? Effect
		: nullptr;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedLayerBlurEffect()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::LayerBlur
		? Effect
		: nullptr;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedLayerBlurEffect() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	return Effect && Effect->Effect.IsNull()
		&& Effect->ProceduralType == EMixtormatEffectType::LayerBlur
		? Effect
		: nullptr;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedProceduralPeel()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Peeling)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedProceduralPeel() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Peeling)
	{
		return nullptr;
	}
	return Effect;
}

FMixtormatLayerEffect* SMixtormat::GetSelectedErosion()
{
	FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Erosion)
	{
		return nullptr;
	}
	return Effect;
}

const FMixtormatLayerEffect* SMixtormat::GetSelectedErosion() const
{
	const FMixtormatLayerEffect* Effect = GetSelectedLayerEffect();
	if (!Effect || !Effect->Effect.IsNull()
		|| Effect->ProceduralType != EMixtormatEffectType::Erosion)
	{
		return nullptr;
	}
	return Effect;
}

#undef LOCTEXT_NAMESPACE
