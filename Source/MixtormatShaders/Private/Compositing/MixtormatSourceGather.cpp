// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatSourceGather.h"

#include "Compositing/MixtormatGeneratorGather.h"
#include "Compositing/MixtormatLayerGather.h"
#include "MixtormatMaterial.h"
#include "MixtormatChildScope.h"
#include "MixtormatOutputReference.h"
#include "MixtormatParameterBinding.h"

namespace MixtormatGpuCompositor
{
	namespace
	{
		TArray<FMixtormatSourceEntry> ResolveShelfSources(const TArray<FMixtormatSourceEntry>& Sources)
		{
			TArray<FMixtormatSourceEntry> Resolved = Sources;
			const TArray<FMixtormatLayer> NoLayers;
			const TArray<FMixtormatLayerGroup> NoGroups;
			for (FMixtormatSourceEntry& Entry : Resolved)
			{
				if (Entry.IsInstance()) { continue; }
				FMixtormatLayer Container;
				Container.LayerId = Entry.SourceId;
				Container.Type = EMixtormatLayerType::Generator;
				Container.bEnabled = true;
				Container.Children.Add(Entry.Child);
				MixtormatParameterBinding::ApplyDirectReferences(
					{NoLayers, NoGroups, Sources}, Container);
				Entry.Child = Container.Children[0];
			}
			return Resolved;
		}

		// A producer is evaluable only when its root payload is an enabled generator. Anything else
		// publishes no fields, so it can neither satisfy demand nor carry demand onward. A linked
		// instance is not a producer at all: it publishes nothing of its own and every reference to
		// it resolves to its original before scheduling.
		bool IsEvaluableSource(const FMixtormatSourceEntry& Source)
		{
			return !Source.IsInstance()
				&& Source.Child.Type == EMixtormatLayerChildType::Generator
				&& Source.Child.Generator.bEnabled;
		}

		// The Sources a generator's explicit inputs actually name, resolved by shelf identity. The
		// socket's own kind contract is applied first (Height reads ScalarSigned, Warp reads Flow or
		// UVMap), so an edge exists only where the input is genuinely consumable; a malformed,
		// disabled, wrong-kind or self endpoint contributes no edge and is never scheduled.
		void CollectGeneratorShelfDependencies(const FMixtormatGenerator& Generator,
			const FGuid& SelfId, const TArray<FMixtormatSourceEntry>& Sources,
			TArray<FGuid>& OutDependencies)
		{
			const auto Collect = [&](const FMixtormatOutputReference& Reference, const bool bHeight)
			{
				if (!Reference.IsShelfSource()) { return; }
				// The edge names the producer the reference ultimately reads: a reference to a linked
				// instance resolves to its original, and a link that no longer resolves contributes no
				// edge -- exactly like a missing endpoint.
				const FGuid ProducerId = MixtormatOutputReferences::ResolveShelfProducerId(
					Sources, Reference.SourceShelfId);
				if (!ProducerId.IsValid()) { return; }
				// A self reference can never be an edge; it would be a one-node cycle. Checked after
				// resolution, so a source consuming an instance of itself cannot slip past it.
				if (ProducerId == SelfId) { return; }
				const bool bCompatible = bHeight
					? Reference.Kind == EMixtormatPublishedFieldKind::ScalarSigned
					: Reference.Kind == EMixtormatPublishedFieldKind::Flow
						|| Reference.Kind == EMixtormatPublishedFieldKind::UVMap;
				if (!bCompatible) { return; }
				const MixtormatOutputReferences::FShelfSourceReferenceStatus Status =
					MixtormatOutputReferences::ClassifyShelfSourceReference(Sources, Reference);
				if (Status.Issue != MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
				{
					return;
				}
				OutDependencies.AddUnique(ProducerId);
			};
			Collect(Generator.HeightSource, true);
			Collect(Generator.WarpSource, false);
		}
	}

	void CollectDemandedShelfSources(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources,
		TArray<FGuid>& OutDemandedOrder)
	{
		OutDemandedOrder.Reset();
		if (Sources.IsEmpty()) { return; }

		const TArray<FMixtormatSourceEntry> ResolvedSources = ResolveShelfSources(Sources);

		TMap<FGuid, int32> IndexById;
		IndexById.Reserve(ResolvedSources.Num());
		for (int32 Index = 0; Index < ResolvedSources.Num(); ++Index)
		{
			IndexById.Add(ResolvedSources[Index].SourceId, Index);
		}

		// One edge list per entry, built once. Shelf order is not consulted.
		TArray<TArray<FGuid>> Dependencies;
		Dependencies.SetNum(ResolvedSources.Num());
		for (int32 Index = 0; Index < ResolvedSources.Num(); ++Index)
		{
			if (IsEvaluableSource(ResolvedSources[Index]))
			{
				CollectGeneratorShelfDependencies(ResolvedSources[Index].Child.Generator,
					ResolvedSources[Index].SourceId, ResolvedSources, Dependencies[Index]);
			}
		}

		TSet<FGuid> Emitted;
		TSet<FGuid> OnStack;

		// A source that can reach itself through producer references is in a cycle. Neither it nor
		// its cycle-mates can be evaluated -- each reads a field the other has not published yet --
		// so all of them are excluded rather than emitted with a silently missing input. The
		// consumer then simply finds no field, which is the same unavailable path a missing endpoint
		// takes; there is never a partial producer.
		const auto Reaches = [&](const FGuid& From, const FGuid& Target)
		{
			const int32* Start = IndexById.Find(From);
			if (!Start) { return false; }
			TArray<FGuid> Pending = Dependencies[*Start];
			TSet<FGuid> Seen;
			while (Pending.Num() > 0)
			{
				const FGuid Node = Pending.Pop();
				if (Node == Target) { return true; }
				if (Seen.Contains(Node)) { continue; }
				Seen.Add(Node);
				const int32* Index = IndexById.Find(Node);
				if (!Index) { continue; }
				for (const FGuid& Dependency : Dependencies[*Index]) { Pending.Add(Dependency); }
			}
			return false;
		};
		TSet<FGuid> Cyclic;
		for (const FMixtormatSourceEntry& Source : ResolvedSources)
		{
			if (Reaches(Source.SourceId, Source.SourceId)) { Cyclic.Add(Source.SourceId); }
		}

		// Post-order DFS: a producer is emitted only after everything it reads. Cycle members are
		// never emitted and are not traversed through -- a field reachable only via a cycle is not
		// consumable either.
		TFunction<void(const FGuid&)> Visit = [&](const FGuid& SourceId)
		{
			if (Emitted.Contains(SourceId) || OnStack.Contains(SourceId) || Cyclic.Contains(SourceId))
			{
				return;
			}
			const int32* Index = IndexById.Find(SourceId);
			if (!Index || !IsEvaluableSource(ResolvedSources[*Index])) { return; }

			OnStack.Add(SourceId);
			for (const FGuid& Dependency : Dependencies[*Index])
			{
				if (!OnStack.Contains(Dependency))
				{
					Visit(Dependency);
				}
			}
			OnStack.Remove(SourceId);
			Emitted.Add(SourceId);
			OutDemandedOrder.Add(SourceId);
		};

		// Seed: the stack's own generator inputs and masks. A disabled layer gathers no generator
		// modules, but its mask children still evaluate so other layers and previews can consume them.
		for (const FMixtormatLayer& Layer : EffectiveLayers)
		{
			TArray<bool> DisabledGroupScopes;
			DisabledGroupScopes.Init(false, Layer.Children.Num());
			for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
			{
				const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, Index);
				if (OwnerIndex != INDEX_NONE)
				{
					const FMixtormatLayerChild& Parent = Layer.Children[OwnerIndex];
					DisabledGroupScopes[Index] = DisabledGroupScopes[OwnerIndex]
						|| (Parent.Type == EMixtormatLayerChildType::IdGroup && !Parent.IdGroup.bEnabled);
				}
			}

			if (Layer.bEnabled && Layer.HasGeneratorModules())
			{
				for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
				{
					if (DisabledGroupScopes[ChildIndex]) { continue; }
					const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
					if (Child.Type != EMixtormatLayerChildType::Generator || !Child.Generator.bEnabled)
					{
						continue;
					}
					TArray<FGuid> Roots;
					CollectGeneratorShelfDependencies(Child.Generator, FGuid(), ResolvedSources, Roots);
					for (const FGuid& Root : Roots) { Visit(Root); }
				}
			}
			// V2 Warp, Deform and Push are typed consumers. Demand each connected
			// shelf producer before GPU evaluation or the field will be absent.
			if (Layer.bEnabled && Layer.HasGeneratorModules())
			{
				for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
				{
					if (DisabledGroupScopes[ChildIndex]) { continue; }
					const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
					if (Child.Type != EMixtormatLayerChildType::Behavior
						|| !Child.Behavior.bEnabled
						|| (Child.Behavior.Type != EMixtormatBehaviorType::Warp
							&& Child.Behavior.Type != EMixtormatBehaviorType::Push
							&& Child.Behavior.Type != EMixtormatBehaviorType::Deform)
						|| (Child.Behavior.Stage != EMixtormatBehaviorStage::PostGeneration
							&& !(Child.Behavior.Stage == EMixtormatBehaviorStage::PreGeneration
								&& Child.Behavior.Type == EMixtormatBehaviorType::Warp))
						|| MixtormatChildScope::ResolveBehaviorGeneratorIndex(Layer.Children, ChildIndex) == INDEX_NONE)
					{
						continue;
					}
					const int32 OwnerIndex = MixtormatChildScope::ResolveBehaviorGeneratorIndex(Layer.Children, ChildIndex);
					if (!Layer.Children.IsValidIndex(OwnerIndex)
						|| !Layer.Children[OwnerIndex].Generator.bEnabled) { continue; }
					const bool bPush = Child.Behavior.Type == EMixtormatBehaviorType::Push;
					const FMixtormatBehaviorFieldInput& Input = bPush
						? Child.Behavior.Height : Child.Behavior.Direction;
					const FMixtormatOutputReference& Reference = Input.Published;
					if (Input.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
						|| !Reference.IsShelfSource() || !Reference.bEnabled
						|| (bPush
							? Reference.Kind != EMixtormatPublishedFieldKind::ScalarSigned
							: (Reference.Kind != EMixtormatPublishedFieldKind::Flow
								&& Reference.Kind != EMixtormatPublishedFieldKind::UVMap))) { continue; }
					const auto Status = MixtormatOutputReferences::ClassifyShelfSourceReference(ResolvedSources, Reference);
					if (Status.Issue == MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
					{
						Visit(MixtormatOutputReferences::ResolveShelfProducerId(ResolvedSources, Reference.SourceShelfId));
					}
				}
			}
			// The mask pipeline consumes Noise Value as typed scalar coverage, with
			// the same placement/shaping as layer-published Value. This is a separate
			// consumer contract from the generator Height/Warp input kinds.
			for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
			{
				if (DisabledGroupScopes[ChildIndex]) { continue; }
				const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
				if (Child.Type != EMixtormatLayerChildType::Mask || !Child.Mask.bEnabled
					|| !Child.Mask.HasPublishedSource()
					|| Child.Mask.PublishedSourceOwnerKind != EMixtormatOutputReferenceOwnerKind::Shelf
					|| Child.Mask.PublishedSourceOutput != FName(TEXT("Value"))) { continue; }
				if (Child.ScopeOwnerChildId.IsValid())
				{
					const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, ChildIndex);
					if (OwnerIndex == INDEX_NONE
						|| !MixtormatChildScope::CanOwnScopedMasks(Layer.Children[OwnerIndex]))
					{
						continue;
					}
				}
				// Addressing is checked against the entry the mask names; the producer checks run
				// against the original a linked instance resolves to. A broken link demands nothing.
				const FMixtormatSourceEntry* Named = ResolvedSources.FindByPredicate([&Child](const FMixtormatSourceEntry& Item)
					{ return Item.SourceId == Child.Mask.PublishedSourceShelfId; });
				const int32* Index = IndexById.Find(MixtormatOutputReferences::ResolveShelfProducerId(
					ResolvedSources, Child.Mask.PublishedSourceShelfId));
				if (!Named || Named->Child.ChildId != Child.Mask.PublishedSourceChildId || !Index) { continue; }
				const FMixtormatSourceEntry& Source = ResolvedSources[*Index];
				if (!IsEvaluableSource(Source) || Source.Child.Generator.Type != EMixtormatGeneratorType::Noise) { continue; }
				Visit(Source.SourceId);
			}
		}
	}

	void GatherSourceProducers(
		const TArray<FMixtormatSourceEntry>& Sources,
		const TArray<FGuid>& DemandedOrder,
		TArray<FLayerRenderData>& OutProducers)
	{
		OutProducers.Reset();
		if (DemandedOrder.IsEmpty()) { return; }

		const TArray<FMixtormatSourceEntry> ResolvedSources = ResolveShelfSources(Sources);

		TMap<FGuid, const FMixtormatSourceEntry*> ById;
		ById.Reserve(ResolvedSources.Num());
		for (const FMixtormatSourceEntry& Source : ResolvedSources)
		{
			ById.Add(Source.SourceId, &Source);
		}

		// A producer never reads the material stack, so its layer-kind inputs must resolve to
		// nothing. An empty effective-layer array is exactly that: no shelf producer can silently
		// read a layer by index.
		const TArray<FMixtormatLayer> NoLayers;
		const TArray<FMixtormatLayerGroup> NoGroups;

		OutProducers.Reserve(DemandedOrder.Num());
		for (const FGuid& SourceId : DemandedOrder)
		{
			const FMixtormatSourceEntry* const* Found = ById.Find(SourceId);
			if (!Found || !*Found || !IsEvaluableSource(**Found)) { continue; }
			const FMixtormatSourceEntry& Entry = **Found;

			// Container for the generator gather only: one unscoped root child, no layer state. It
			// exists to satisfy the module-container contract and is never added to the stack, keyed
			// as a layer, or composited.
			FMixtormatLayer Container;
			Container.LayerId = Entry.SourceId;
			Container.Type = EMixtormatLayerType::Generator;
			Container.bEnabled = true;
			Container.Children.Add(Entry.Child);

			FLayerRenderData& Data = OutProducers.AddDefaulted_GetRef();
			// Explicit shelf ownership: addressed by the entry's SourceId, never a material layer.
			Data.LayerId = Entry.SourceId;
			Data.bIsShelfSource = true;
			Data.SourceShelfId = Entry.SourceId;
			Data.bGenerator = true;
			Data.bEnabled = true;

			const uint64 PlacementKey = GatherLayerPlacementKey(Container, true);
			GatherGeneratorChild(Data, Container, Container.Children[0], 0,
				true, false, PlacementKey, 0, NoLayers, ResolvedSources);
		}
	}
}
