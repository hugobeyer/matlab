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
		// The only shelf consumer contract that exists: a generator module's explicit Height/Warp
		// input. OutputReference children and structural modules stay layer-only until their own
		// shelf paths are built, so they contribute no demand here.
		void CollectEntryDemand(const FMixtormatGenerator& Generator,
			const TArray<FMixtormatSourceEntry>& Sources, TSet<FGuid>& OutDemandedSourceIds)
		{
			const auto Collect = [&](const FMixtormatOutputReference& Reference)
			{
				if (!Reference.bEnabled || !Reference.IsShelfSource()) { return; }
				const MixtormatOutputReferences::FShelfSourceReferenceStatus Status =
					MixtormatOutputReferences::ClassifyShelfSourceReference(Sources, Reference);
				// Only a well-formed, enabled endpoint is schedulable; everything else stays an
				// explicit repair state for the editor instead of a silently dropped dependency.
				if (Status.Issue == MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
				{
					OutDemandedSourceIds.Add(Reference.SourceShelfId);
				}
			};
			Collect(Generator.HeightSource);
			Collect(Generator.WarpSource);
		}
	}

	void CollectDemandedShelfSources(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources,
		TSet<FGuid>& OutDemandedSourceIds)
	{
		if (Sources.IsEmpty()) { return; }

		// Direct demand from the stack. A disabled layer gathers no modules, so it demands nothing.
		for (const FMixtormatLayer& Layer : EffectiveLayers)
		{
			if (!Layer.bEnabled || Layer.Type != EMixtormatLayerType::Generator) { continue; }
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.Type == EMixtormatLayerChildType::Generator && Child.Generator.bEnabled)
				{
					CollectEntryDemand(Child.Generator, Sources, OutDemandedSourceIds);
				}
			}
		}

		// Transitive demand between producers. A shelf dependency may only name an earlier entry
		// (the synthetic producer array preserves shelf order, and the generator-input validator
		// enforces earlier-only there), so one reverse pass closes the set. A disabled intermediate
		// publishes nothing, so demand does not travel through it.
		for (int32 Index = Sources.Num() - 1; Index >= 0; --Index)
		{
			const FMixtormatSourceEntry& Source = Sources[Index];
			if (!OutDemandedSourceIds.Contains(Source.SourceId)
				|| Source.Child.Type != EMixtormatLayerChildType::Generator
				|| !Source.Child.Generator.bEnabled)
			{
				continue;
			}
			CollectEntryDemand(Source.Child.Generator, Sources, OutDemandedSourceIds);
		}
	}

	void GatherSourceProducers(
		const TArray<FMixtormatSourceEntry>& Sources,
		const TSet<FGuid>& DemandedSourceIds,
		TArray<FLayerRenderData>& OutProducers)
	{
		if (DemandedSourceIds.IsEmpty()) { return; }

		// The synthetic authored stack the producers gather against: one Generator layer per
		// demanded entry, in shelf order. A producer's layer-kind inputs resolve against this array
		// and find nothing -- sources are producers only, they never read the material stack.
		TArray<FMixtormatLayer> SyntheticLayers;
		TArray<int32> SyntheticSourceIndices;
		SyntheticLayers.Reserve(Sources.Num());
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			const FMixtormatSourceEntry& Source = Sources[Index];
			if (!DemandedSourceIds.Contains(Source.SourceId)
				|| Source.Child.Type != EMixtormatLayerChildType::Generator
				|| !Source.Child.Generator.bEnabled)
			{
				continue;
			}
			FMixtormatLayer& Synthetic = SyntheticLayers.AddDefaulted_GetRef();
			Synthetic.LayerId = Source.SourceId;
			Synthetic.Type = EMixtormatLayerType::Generator;
			Synthetic.bEnabled = true;
			Synthetic.Children.Add(Source.Child);
			SyntheticSourceIndices.Add(Index);
		}

		OutProducers.Reserve(SyntheticLayers.Num());
		for (int32 SyntheticIndex = 0; SyntheticIndex < SyntheticLayers.Num(); ++SyntheticIndex)
		{
			const FMixtormatLayer& Synthetic = SyntheticLayers[SyntheticIndex];
			FLayerRenderData& Data = OutProducers.AddDefaulted_GetRef();
			Data.LayerId = Synthetic.LayerId;
			Data.bGenerator = true;
			Data.bEnabled = true;

			// One unscoped root child: no scope owners to resolve, no masks, no effects.
			const uint64 PlacementKey = GatherLayerPlacementKey(Synthetic, true);
			GatherGeneratorChild(Data, Synthetic, Synthetic.Children[0], 0,
				true, false, PlacementKey, SyntheticIndex, SyntheticLayers, Sources);
		}
	}
}
