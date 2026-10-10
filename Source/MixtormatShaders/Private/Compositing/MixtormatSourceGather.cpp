// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatSourceGather.h"

#include "Compositing/MixtormatGeneratorGather.h"
#include "Compositing/MixtormatLayerGather.h"
#include "MixtormatMaterial.h"
#include "MixtormatOutputReference.h"

namespace MixtormatGpuCompositor
{
	namespace
	{
		// A producer is evaluable only when its root payload is an enabled generator. Anything else
		// publishes no fields, so it can neither satisfy demand nor carry demand onward.
		bool IsEvaluableSource(const FMixtormatSourceEntry& Source)
		{
			return Source.Child.Type == EMixtormatLayerChildType::Generator
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
				// A self reference can never be an edge; it would be a one-node cycle.
				if (Reference.SourceShelfId == SelfId) { return; }
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
				OutDependencies.AddUnique(Reference.SourceShelfId);
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

		TMap<FGuid, int32> IndexById;
		IndexById.Reserve(Sources.Num());
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			IndexById.Add(Sources[Index].SourceId, Index);
		}

		// One edge list per entry, built once. Shelf order is not consulted.
		TArray<TArray<FGuid>> Dependencies;
		Dependencies.SetNum(Sources.Num());
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (IsEvaluableSource(Sources[Index]))
			{
				CollectGeneratorShelfDependencies(Sources[Index].Child.Generator,
					Sources[Index].SourceId, Sources, Dependencies[Index]);
			}
		}

		TSet<FGuid> Emitted;
		TSet<FGuid> OnStack;

		// Post-order DFS: a producer is emitted only after everything it reads. A back-edge (a
		// reference to a node already on the active path) is a cycle and is dropped rather than
		// recursed into, which also terminates self and mutual cycles.
		TFunction<void(const FGuid&)> Visit = [&](const FGuid& SourceId)
		{
			if (Emitted.Contains(SourceId) || OnStack.Contains(SourceId)) { return; }
			const int32* Index = IndexById.Find(SourceId);
			if (!Index || !IsEvaluableSource(Sources[*Index])) { return; }

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

		// Seed: the stack's own generator inputs. A disabled layer gathers no modules, so it
		// demands nothing; each well-formed, enabled shelf endpoint is visited once.
		for (const FMixtormatLayer& Layer : EffectiveLayers)
		{
			if (!Layer.bEnabled || Layer.Type != EMixtormatLayerType::Generator) { continue; }
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.Type != EMixtormatLayerChildType::Generator || !Child.Generator.bEnabled)
				{
					continue;
				}
				TArray<FGuid> Roots;
				CollectGeneratorShelfDependencies(Child.Generator, FGuid(), Sources, Roots);
				for (const FGuid& Root : Roots)
				{
					Visit(Root);
				}
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

		TMap<FGuid, const FMixtormatSourceEntry*> ById;
		ById.Reserve(Sources.Num());
		for (const FMixtormatSourceEntry& Source : Sources)
		{
			ById.Add(Source.SourceId, &Source);
		}

		// A producer never reads the material stack, so its layer-kind inputs must resolve to
		// nothing. An empty effective-layer array is exactly that: no shelf producer can silently
		// read a layer by index.
		const TArray<FMixtormatLayer> NoLayers;

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
				true, false, PlacementKey, 0, NoLayers, Sources);
		}
	}
}
