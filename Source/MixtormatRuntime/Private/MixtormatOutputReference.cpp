// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatOutputReference.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"

namespace MixtormatOutputReferences
{
	int32 ResolveEarlierSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || !Reference.HasSource()
			|| !Layers.IsValidIndex(DestinationLayerIndex))
		{
			return INDEX_NONE;
		}
		const FName Expected = Reference.Kind == EMixtormatPublishedFieldKind::RegionIds
			? FName(TEXT("RegionIds")) : Reference.Kind == EMixtormatPublishedFieldKind::Flow
				? FName(TEXT("FlowDirection")) : Reference.Kind == EMixtormatPublishedFieldKind::UVMap
					? FName(TEXT("WarpedUV")) : FName(TEXT("Color"));
		if (Reference.OutputName != Expected
			|| (Reference.Kind != EMixtormatPublishedFieldKind::RegionIds
				&& Reference.Kind != EMixtormatPublishedFieldKind::Flow
				&& Reference.Kind != EMixtormatPublishedFieldKind::UVMap
				&& Reference.Kind != EMixtormatPublishedFieldKind::Color))
		{
			return INDEX_NONE;
		}
		for (int32 LayerIndex = 0; LayerIndex < DestinationLayerIndex; ++LayerIndex)
		{
			const FMixtormatLayer& Layer = Layers[LayerIndex];
			if (Layer.LayerId == Reference.SourceLayerId && Layer.bEnabled)
			{
				return Layer.Children.IndexOfByPredicate([&Reference](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == Reference.SourceChildId;
				});
			}
		}
		return INDEX_NONE;
	}

	namespace
	{
		bool ProducesRegionIds(const FMixtormatLayerChild& Child)
		{
			switch (Child.Type)
			{
			case EMixtormatLayerChildType::PatternId: return Child.PatternId.bEnabled;
			case EMixtormatLayerChildType::Filter: return Child.Filter.bEnabled;
			case EMixtormatLayerChildType::CombineId: return Child.CombineId.bEnabled;
			case EMixtormatLayerChildType::IdGroup: return Child.IdGroup.bEnabled;
			case EMixtormatLayerChildType::Generator: return Child.Generator.bEnabled;
			case EMixtormatLayerChildType::OutputReference:
				return Child.OutputReference.bEnabled
					&& Child.OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds;
			case EMixtormatLayerChildType::Effect:
			{
				if (!Child.Effect.bEnabled) { return false; }
				if (Child.Effect.Effect.IsNull())
				{
					return Child.Effect.ProceduralType == EMixtormatEffectType::Breakup;
				}
				const UMixtormatEffect* Asset = Child.Effect.Effect.Get();
				if (!Asset)
				{
					// Dependency validation runs in editor actions and game-thread compose gathering.
					if (!IsInGameThread()) { return false; }
					Asset = Child.Effect.Effect.LoadSynchronous();
				}
				return Asset && Asset->EffectType == EMixtormatEffectType::Breakup;
			}
			default: return false;
			}
		}

		struct FDependencyNode
		{
			int32 Layer = INDEX_NONE;
			int32 Child = INDEX_NONE;
			int32 Parent = INDEX_NONE;
			int32 Completion = INDEX_NONE;
			bool bEnabled = false;
			bool bValidFieldDependency = true;
			TArray<int32> Children;
			TArray<int32> Dependencies;
		};
	}

	bool ValidateDependency(const TArray<FMixtormatLayer>& Layers,
		const FGuid& DestinationLayerId, const FGuid& DestinationChildId,
		const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || !Reference.HasSource()) { return false; }
		const int32 DestinationLayer = Layers.IndexOfByPredicate([&](const FMixtormatLayer& Layer)
		{
			return Layer.LayerId == DestinationLayerId;
		});
		if (!Layers.IsValidIndex(DestinationLayer)) { return false; }
		if (Reference.Kind != EMixtormatPublishedFieldKind::RegionIds)
		{
			return Layers[DestinationLayer].Children.ContainsByPredicate([&](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == DestinationChildId;
			}) && ResolveEarlierSource(Layers, DestinationLayer, Reference) != INDEX_NONE;
		}
		if (Reference.OutputName != FName(TEXT("RegionIds"))) { return false; }

		// Resolve inherited payloads without changing placement identity or the authored arrays.
		TArray<FMixtormatLayer> ResolvedLayers = Layers;
		for (FMixtormatLayer& Layer : ResolvedLayers)
		{
			MixtormatParameterBinding::ResolveChildInstances(FMixtormatBindingScope{Layers}, Layer);
		}
		TArray<FDependencyNode> Nodes;
		TArray<int32> Offsets;
		TSet<FGuid> LayerIds;
		for (int32 LayerIndex = 0; LayerIndex < ResolvedLayers.Num(); ++LayerIndex)
		{
			const FMixtormatLayer& Layer = ResolvedLayers[LayerIndex];
			if (!Layer.LayerId.IsValid() || LayerIds.Contains(Layer.LayerId)) { return false; }
			LayerIds.Add(Layer.LayerId);
			Offsets.Add(Nodes.Num());
			TMap<FGuid, int32> ChildIds;
			for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
			{
				const FMixtormatLayerChild& Child = Layer.Children[Index];
				if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId)) { return false; }
				const int32 NodeIndex = Nodes.Num();
				// Build off-array: no node reference survives a Nodes allocation/growth.
				FDependencyNode Node;
				Node.Layer = LayerIndex;
				Node.Child = Index;
				Node.bEnabled = Layer.bEnabled
					&& (Child.Type != EMixtormatLayerChildType::IdGroup || Child.IdGroup.bEnabled);
				if (Child.ScopeOwnerChildId.IsValid())
				{
					const int32* Parent = ChildIds.Find(Child.ScopeOwnerChildId);
					if (!Parent) { return false; } // Missing/self/forward scope owner.
					Node.Parent = *Parent;
					Node.bEnabled = Node.bEnabled && Nodes[*Parent].bEnabled
						&& (Layer.Children[Nodes[*Parent].Child].Type != EMixtormatLayerChildType::IdGroup
							|| Layer.Children[Nodes[*Parent].Child].IdGroup.bEnabled);
					Nodes[*Parent].Children.Add(NodeIndex);
				}
				Nodes.Add(MoveTemp(Node));
				ChildIds.Add(Child.ChildId, NodeIndex);
			}
		}
		const auto FindNode = [&](const FGuid& LayerId, const FGuid& ChildId) -> int32
		{
			for (int32 Index = 0; Index < Nodes.Num(); ++Index)
			{
				const FDependencyNode& Node = Nodes[Index];
				if (ResolvedLayers[Node.Layer].LayerId == LayerId
					&& ResolvedLayers[Node.Layer].Children[Node.Child].ChildId == ChildId) { return Index; }
			}
			return INDEX_NONE;
		};
		const int32 Destination = FindNode(DestinationLayerId, DestinationChildId);
		const int32 Source = FindNode(Reference.SourceLayerId, Reference.SourceChildId);
		if (Destination == INDEX_NONE || Source == INDEX_NONE || Source == Destination
			|| !Nodes[Destination].bEnabled || !Nodes[Source].bEnabled
			|| !ProducesRegionIds(ResolvedLayers[Nodes[Source].Layer].Children[Nodes[Source].Child])) { return false; }

		int32 Completion = 0;
		TFunction<bool(int32, int32)> CompleteScope;
		CompleteScope = [&](const int32 Index, const int32 Depth)
		{
			if (Depth > 128) { return false; }
			for (const int32 Child : Nodes[Index].Children)
			{
				if (!CompleteScope(Child, Depth + 1)) { return false; }
			}
			Nodes[Index].Completion = Completion++;
			return true;
		};
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index].Parent == INDEX_NONE && !CompleteScope(Index, 0)) { return false; }
		}
		const auto IsEarlier = [&](const int32 From, const int32 To)
		{
			const FMixtormatLayerChild& Reader = ResolvedLayers[Nodes[From].Layer].Children[Nodes[From].Child];
			bool bOwnedInput = false;
			if (Reader.Type == EMixtormatLayerChildType::IdGroup && Nodes[To].Layer == Nodes[From].Layer)
			{
				// Only this group's own subtree can finish after its header row. An unrelated
				// producer allocated by an early pass is not permission to read a future row.
				for (int32 Owner = Nodes[To].Parent; Owner != INDEX_NONE; Owner = Nodes[Owner].Parent)
				{
					if (Owner == From) { bOwnedInput = true; break; }
				}
			}
			return Nodes[To].Layer < Nodes[From].Layer
				|| (Nodes[To].Layer == Nodes[From].Layer
					&& Nodes[To].Completion < Nodes[From].Completion
					&& (bOwnedInput || Nodes[To].Child < Nodes[From].Child));
		};
		if (!IsEarlier(Destination, Source)) { return false; }

		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FDependencyNode& Node = Nodes[Index];
			const FMixtormatLayerChild& Child = ResolvedLayers[Node.Layer].Children[Node.Child];
			if (Child.Type == EMixtormatLayerChildType::IdGroup)
			{
				for (const int32 Input : Node.Children)
				{
					if (Nodes[Input].bEnabled && ProducesRegionIds(
						ResolvedLayers[Node.Layer].Children[Nodes[Input].Child]))
					{
						Nodes[Index].Dependencies.Add(Input);
					}
				}
			}
			if (Index == Destination || Child.Type == EMixtormatLayerChildType::OutputReference)
			{
				const FMixtormatOutputReference& Edge = Index == Destination ? Reference : Child.OutputReference;
				if (Edge.bEnabled && Edge.Kind == EMixtormatPublishedFieldKind::RegionIds)
				{
					const int32 Target = FindNode(Edge.SourceLayerId, Edge.SourceChildId);
					if (!Edge.HasSource() || Edge.OutputName != FName(TEXT("RegionIds"))
						|| Target == INDEX_NONE || !Nodes[Target].bEnabled
						|| !ProducesRegionIds(ResolvedLayers[Nodes[Target].Layer].Children[Nodes[Target].Child]))
					{
						// A broken alias is not a valid leaf of another reference's dependency graph.
						Nodes[Index].bValidFieldDependency = false;
					}
					else { Nodes[Index].Dependencies.Add(Target); }
				}
			}
			if (Child.Type == EMixtormatLayerChildType::CombineId)
			{
				for (int32 Previous = Offsets[Node.Layer] + Node.Child - 1;
					Previous >= Offsets[Node.Layer]; --Previous)
				{
					if (Nodes[Previous].Parent == Node.Parent && Nodes[Previous].bEnabled
						&& ProducesRegionIds(ResolvedLayers[Node.Layer].Children[Nodes[Previous].Child]))
					{
						Nodes[Index].Dependencies.Add(Previous);
						break;
					}
				}
			}
		}
		// Active path, not a global visited set: shared sources and repeated inputs are legal.
		TArray<uint8> State;
		State.Init(0, Nodes.Num());
		TFunction<bool(int32, int32)> Visit;
		Visit = [&](const int32 Index, const int32 Depth)
		{
			if (Depth > 128 || State[Index] == 1
				|| !Nodes[Index].bEnabled || !Nodes[Index].bValidFieldDependency) { return false; }
			if (State[Index] == 2) { return true; }
			State[Index] = 1;
			for (const int32 Dependency : Nodes[Index].Dependencies)
			{
				if (!IsEarlier(Index, Dependency) || !Visit(Dependency, Depth + 1)) { return false; }
			}
			State[Index] = 2;
			return true;
		};
		return Visit(Destination, 0);
	}

	int32 ResolveSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference)
	{
		if (!Layers.IsValidIndex(DestinationLayerIndex)
			|| !Layers[DestinationLayerIndex].Children.IsValidIndex(DestinationChildIndex)) { return INDEX_NONE; }
		if (!ValidateDependency(Layers, Layers[DestinationLayerIndex].LayerId,
			Layers[DestinationLayerIndex].Children[DestinationChildIndex].ChildId, Reference)) { return INDEX_NONE; }
		for (int32 LayerIndex = 0; LayerIndex <= DestinationLayerIndex; ++LayerIndex)
		{
			if (Layers[LayerIndex].LayerId == Reference.SourceLayerId)
			{
				return Layers[LayerIndex].Children.IndexOfByPredicate([&](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == Reference.SourceChildId;
				});
			}
		}
		return INDEX_NONE;
	}
}
