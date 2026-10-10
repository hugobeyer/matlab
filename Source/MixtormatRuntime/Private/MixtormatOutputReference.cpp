// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatOutputReference.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"

namespace MixtormatOutputReferences
{
	FName CanonicalFieldOutputName(const EMixtormatPublishedFieldKind Kind)
	{
		switch (Kind)
		{
		case EMixtormatPublishedFieldKind::RegionIds: return FName(TEXT("RegionIds"));
		case EMixtormatPublishedFieldKind::Flow:      return FName(TEXT("FlowDirection"));
		case EMixtormatPublishedFieldKind::UVMap:     return FName(TEXT("WarpedUV"));
		case EMixtormatPublishedFieldKind::Color:     return FName(TEXT("Color"));
		// Generic field kinds are addressed by the producer's own output name.
		case EMixtormatPublishedFieldKind::Scalar01:
		case EMixtormatPublishedFieldKind::ScalarSigned:
		case EMixtormatPublishedFieldKind::SDF:
		case EMixtormatPublishedFieldKind::Vector2:   return NAME_None;
		default:                                      return NAME_None;
		}
	}

	bool IsValidFieldKind(const EMixtormatPublishedFieldKind Kind)
	{
		switch (Kind)
		{
		case EMixtormatPublishedFieldKind::RegionIds:
		case EMixtormatPublishedFieldKind::Flow:
		case EMixtormatPublishedFieldKind::UVMap:
		case EMixtormatPublishedFieldKind::Color:
		case EMixtormatPublishedFieldKind::Scalar01:
		case EMixtormatPublishedFieldKind::ScalarSigned:
		case EMixtormatPublishedFieldKind::SDF:
		case EMixtormatPublishedFieldKind::Vector2:
			return true;
		default:
			return false;
		}
	}

	FShelfSourceReferenceStatus ClassifyShelfSourceReference(
		const TArray<FMixtormatSourceEntry>& Sources,
		const FMixtormatOutputReference& Reference)
	{
		FShelfSourceReferenceStatus Status;
		if (!Reference.HasKnownOwnerKind())
		{
			Status.Issue = EShelfSourceReferenceIssue::InvalidOwnerKind;
			return Status;
		}
		if (!Reference.IsShelfSource())
		{
			Status.Issue = EShelfSourceReferenceIssue::NotShelfReference;
			return Status;
		}
		if (!Reference.HasSource())
		{
			Status.Issue = EShelfSourceReferenceIssue::Unset;
			return Status;
		}
		if (!IsValidFieldKind(Reference.Kind))
		{
			Status.Issue = EShelfSourceReferenceIssue::InvalidFieldKind;
			return Status;
		}
		const FName Expected = CanonicalFieldOutputName(Reference.Kind);
		if (Expected != NAME_None && Reference.OutputName != Expected)
		{
			Status.Issue = EShelfSourceReferenceIssue::WrongOutputName;
			return Status;
		}

		int32 SourceMatches = 0;
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (Sources[Index].SourceId == Reference.SourceShelfId)
			{
				Status.SourceIndex = Index;
				++SourceMatches;
			}
		}
		if (SourceMatches == 0)
		{
			Status.Issue = EShelfSourceReferenceIssue::MissingSource;
			return Status;
		}
		if (SourceMatches != 1)
		{
			Status.SourceIndex = INDEX_NONE;
			Status.Issue = EShelfSourceReferenceIssue::DuplicateSourceIdentity;
			return Status;
		}
		const FMixtormatSourceEntry& Source = Sources[Status.SourceIndex];
		if (Source.Child.ChildId != Reference.SourceChildId)
		{
			Status.Issue = EShelfSourceReferenceIssue::MissingChild;
			return Status;
		}
		if (Source.Child.Type != EMixtormatLayerChildType::Generator)
		{
			Status.Issue = EShelfSourceReferenceIssue::WrongSourceKind;
			return Status;
		}
		if (!Source.Child.Generator.bEnabled)
		{
			Status.Issue = EShelfSourceReferenceIssue::DisabledSource;
			return Status;
		}

		// The endpoint is stable and eligible, but Phase B has not scheduled or materialized its
		// fields. Do not return success until that producer contract actually exists.
		Status.Issue = EShelfSourceReferenceIssue::Unevaluated;
		Status.bHasResolvedEndpoint = true;
		return Status;
	}

	int32 ResolveEarlierSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || Reference.IsShelfSource() || !Reference.HasSource()
			|| !Layers.IsValidIndex(DestinationLayerIndex))
		{
			return INDEX_NONE;
		}
		if (!IsValidFieldKind(Reference.Kind)) { return INDEX_NONE; }
		// Named kinds keep their canonical published output name; a generic kind carries the
		// producer's own name, so only the named kinds constrain Reference.OutputName here.
		const FName Expected = CanonicalFieldOutputName(Reference.Kind);
		if (Expected != NAME_None && Reference.OutputName != Expected) { return INDEX_NONE; }
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
		if (!Reference.bEnabled || Reference.IsShelfSource() || !Reference.HasSource()) { return false; }
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
		if (Reference.OutputName != CanonicalFieldOutputName(EMixtormatPublishedFieldKind::RegionIds)) { return false; }

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
					if (!Edge.HasSource() || Edge.OutputName != CanonicalFieldOutputName(EMixtormatPublishedFieldKind::RegionIds)
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

	static int32 EvaluateGeneratorInputSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference, FStructuralEdgeStatus& Status,
		const bool bIgnoreDestinationState)
	{
		const auto Reject = [&Status](const EStructuralLinkIssue Issue)
		{
			Status.Issue = Issue;
			return INDEX_NONE;
		};
		// The remaining scope failures are deliberately not described as missing/type errors.
		Status.Issue = EStructuralLinkIssue::InvalidSourceScope;
		if (!Layers.IsValidIndex(DestinationLayerIndex)) { return Reject(EStructuralLinkIssue::MissingLayer); }
		// Shelf endpoints are classified by ClassifyShelfSourceReference and remain unavailable to
		// structural modules until producer scheduling exists; never reinterpret them as layers.
		if (Reference.IsShelfSource()) { return Reject(EStructuralLinkIssue::WrongSourceKind); }
		if (!Reference.SourceLayerId.IsValid() || !Reference.SourceChildId.IsValid())
		{
			return Reject(EStructuralLinkIssue::Unset);
		}
		// Resolve presentation indices independently of eligibility. Keep the rejection order
		// below unchanged, including disabled references and unsupported output kinds.
		int32 LayerMatches = 0;
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Layers[Index].LayerId == Reference.SourceLayerId)
			{
				Status.LayerIndex = Index;
				++LayerMatches;
			}
		}
		if (LayerMatches != 1) { Status.LayerIndex = INDEX_NONE; }
		if (Layers.IsValidIndex(Status.LayerIndex))
		{
			const TArray<FMixtormatLayerChild>& Children = Layers[Status.LayerIndex].Children;
			int32 ChildMatches = 0;
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				if (Children[Index].ChildId == Reference.SourceChildId)
				{
					Status.ChildIndex = Index;
					++ChildMatches;
				}
			}
			if (ChildMatches != 1) { Status.ChildIndex = INDEX_NONE; }
		}
		if (!Reference.bEnabled) { return Reject(EStructuralLinkIssue::DisabledReference); }
		if (!Reference.HasSource()) { return Reject(EStructuralLinkIssue::WrongSourceKind); }
		const FMixtormatLayer& DestinationLayer = Layers[DestinationLayerIndex];
		if ((!bIgnoreDestinationState && !DestinationLayer.bEnabled) || DestinationLayer.Type != EMixtormatLayerType::Generator
					|| !DestinationLayer.Children.IsValidIndex(DestinationChildIndex))
		{
			return INDEX_NONE;
		}
		const FMixtormatLayerChild& Destination = DestinationLayer.Children[DestinationChildIndex];
		const bool bPush = Destination.Type == EMixtormatLayerChildType::HeightPush;
		const bool bWarp = Destination.Type == EMixtormatLayerChildType::StructuralWarp;
		if ((!bIgnoreDestinationState && Destination.ScopeOwnerChildId.IsValid())
			|| (bPush ? (!bIgnoreDestinationState && !Destination.HeightPush.bEnabled)
				: bWarp ? (!bIgnoreDestinationState && !Destination.StructuralWarp.bEnabled)
				: Destination.Type != EMixtormatLayerChildType::Generator || !Destination.Generator.bEnabled))
		{
			return INDEX_NONE;
		}
		const bool bHeight = Reference.Kind == EMixtormatPublishedFieldKind::ScalarSigned
			&& Reference.OutputName == FName(TEXT("Height"));
		const bool bFlow = Reference.Kind == EMixtormatPublishedFieldKind::Flow
			&& Reference.OutputName == CanonicalFieldOutputName(EMixtormatPublishedFieldKind::Flow);
		const bool bUV = Reference.Kind == EMixtormatPublishedFieldKind::UVMap
			&& Reference.OutputName == CanonicalFieldOutputName(EMixtormatPublishedFieldKind::UVMap);
		if ((!bHeight && !bFlow && !bUV) || (bPush && !bHeight)
					|| (bWarp && !bFlow && !bUV)) { return Reject(EStructuralLinkIssue::WrongSourceKind); }

		int32 SourceLayerIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Layers[Index].LayerId != Reference.SourceLayerId) { continue; }
			if (SourceLayerIndex != INDEX_NONE) { return Reject(EStructuralLinkIssue::DuplicateIdentity); }
			SourceLayerIndex = Index;
		}
		Status.LayerIndex = SourceLayerIndex;
		if (SourceLayerIndex == INDEX_NONE) { return Reject(EStructuralLinkIssue::MissingLayer); }
		const FMixtormatLayer& SourceLayer = Layers[SourceLayerIndex];
		int32 SourceIndex = INDEX_NONE;
		for (int32 Index = 0; Index < SourceLayer.Children.Num(); ++Index)
		{
			if (SourceLayer.Children[Index].ChildId != Reference.SourceChildId) { continue; }
			if (SourceIndex != INDEX_NONE) { return Reject(EStructuralLinkIssue::DuplicateIdentity); }
			SourceIndex = Index;
		}
		Status.ChildIndex = SourceIndex;
		if (SourceIndex == INDEX_NONE) { return Reject(EStructuralLinkIssue::MissingChild); }
		if (SourceLayerIndex > DestinationLayerIndex) { return Reject(EStructuralLinkIssue::ForwardSource); }
		if (!SourceLayer.bEnabled) { return Reject(EStructuralLinkIssue::DisabledLayer); }
		if (SourceLayer.Type != EMixtormatLayerType::Generator) { return Reject(EStructuralLinkIssue::WrongOwnerLayer); }
		if (SourceLayerIndex == DestinationLayerIndex && SourceIndex >= DestinationChildIndex)
		{
			return Reject(EStructuralLinkIssue::ForwardSource);
		}
		const FMixtormatLayerChild& Source = SourceLayer.Children[SourceIndex];
		if (bHeight)
		{
			if (Source.Type != EMixtormatLayerChildType::Generator) { return Reject(EStructuralLinkIssue::WrongSourceKind); }
			if (!Source.Generator.bEnabled) { return Reject(EStructuralLinkIssue::DisabledSource); }
			if (Source.ScopeOwnerChildId.IsValid()) { return Reject(EStructuralLinkIssue::WrongSourceScope); }
			Status.Issue = EStructuralLinkIssue::None;
			return SourceIndex;
		}
		// Noise's explicit Flow is derived from its completed height, not its raw Vector2 Gradient.
		const bool bNoiseFlow = bFlow && Source.Type == EMixtormatLayerChildType::Generator
			&& Source.Generator.Type == EMixtormatGeneratorType::Noise;
		if (!bNoiseFlow && Source.Type != EMixtormatLayerChildType::Effect) { return Reject(EStructuralLinkIssue::WrongSourceKind); }
		if (bNoiseFlow ? !Source.Generator.bEnabled : !Source.Effect.bEnabled) { return Reject(EStructuralLinkIssue::DisabledSource); }
		if (bNoiseFlow ? Source.ScopeOwnerChildId.IsValid() : !Source.ScopeOwnerChildId.IsValid())
		{
			return Reject(EStructuralLinkIssue::WrongSourceScope);
		}
		const int32 OwnerIndex = bNoiseFlow ? SourceIndex : SourceLayer.Children.IndexOfByPredicate([&](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == Source.ScopeOwnerChildId;
		});
		if (OwnerIndex == INDEX_NONE || (!bNoiseFlow && OwnerIndex >= SourceIndex)) { return INDEX_NONE; }
		const FMixtormatLayerChild& Owner = SourceLayer.Children[OwnerIndex];
		if (Owner.Type == EMixtormatLayerChildType::Generator && !Owner.Generator.bEnabled)
		{
			return Reject(EStructuralLinkIssue::DisabledSource);
		}
		if (Owner.Type != EMixtormatLayerChildType::Generator || !Owner.Generator.bEnabled
			|| Owner.ScopeOwnerChildId.IsValid() || !MixtormatCanOwnGeneratorFlow(Owner.Generator.Type)
			|| (SourceLayerIndex == DestinationLayerIndex && OwnerIndex >= DestinationChildIndex))
		{
			return INDEX_NONE;
		}
		if (bWarp)
		{
			// Structural sources must identify one completed generator scope, not an ambiguous owner.
			for (int32 Index = OwnerIndex + 1; Index < SourceLayer.Children.Num(); ++Index)
			{
				if (SourceLayer.Children[Index].ChildId == Owner.ChildId) { return Reject(EStructuralLinkIssue::DuplicateIdentity); }
			}
		}
		if (bWarp && SourceLayerIndex == DestinationLayerIndex)
		{
			// A tool row before the module is not enough if its generator scope finishes later.
			for (int32 Index = DestinationChildIndex; Index < SourceLayer.Children.Num(); ++Index)
			{
				FGuid ParentId = SourceLayer.Children[Index].ScopeOwnerChildId;
				for (int32 Depth = 0; ParentId.IsValid() && Depth < SourceLayer.Children.Num(); ++Depth)
				{
					if (ParentId == Owner.ChildId) { return Reject(EStructuralLinkIssue::IncompleteSourceScope); }
					const int32 ParentIndex = SourceLayer.Children.IndexOfByPredicate(
						[&](const FMixtormatLayerChild& Child) { return Child.ChildId == ParentId; });
					if (ParentIndex == INDEX_NONE) { return INDEX_NONE; }
					ParentId = SourceLayer.Children[ParentIndex].ScopeOwnerChildId;
				}
				if (ParentId.IsValid()) { return INDEX_NONE; }
			}
		}
		if (bNoiseFlow)
		{
			Status.Issue = EStructuralLinkIssue::None;
			return SourceIndex;
		}
		EMixtormatEffectType Type = Source.Effect.ProceduralType;
		if (!Source.Effect.Effect.IsNull())
		{
			const UMixtormatEffect* Asset = Source.Effect.Effect.Get();
			if (!Asset && IsInGameThread()) { Asset = Source.Effect.Effect.LoadSynchronous(); }
			if (!Asset) { return Reject(EStructuralLinkIssue::UnavailableEffectAsset); }
			Type = Asset->EffectType;
		}
		if (!MixtormatIsGeneratorFlowEffect(Type) || (bUV && Type == EMixtormatEffectType::FlowCarve)
					|| (Owner.Generator.Type == EMixtormatGeneratorType::Noise
						&& Source.Effect.GeneratorFlowSource != EMixtormatGeneratorFlowSource::Height))
		{
			return Reject(EStructuralLinkIssue::WrongSourceKind);
		}
		Status.Issue = EStructuralLinkIssue::None;
		return SourceIndex;
	}

	int32 ResolveGeneratorInputSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex,
		const FMixtormatOutputReference& Reference)
	{
		FStructuralEdgeStatus Status;
		return EvaluateGeneratorInputSource(Layers, DestinationLayerIndex, DestinationChildIndex,
			Reference, Status, false);
	}

	static FStructuralEdgeStatus EvaluateStructuralTarget(const FMixtormatLayer& Layer,
		const int32 LayerIndex, const int32 ModuleIndex, const FGuid& TargetChildId, const bool bPush)
	{
		FStructuralEdgeStatus Status;
		Status.LayerIndex = LayerIndex;
		if (!TargetChildId.IsValid() && !bPush) { return Status; }
		int32 Matches = 0;
		for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& Target = Layer.Children[Index];
			if (Target.ChildId != TargetChildId) { continue; }
			++Matches;
			if (Status.ChildIndex == INDEX_NONE) { Status.ChildIndex = Index; }
			// Push historically takes the first eligible later match; do not tighten that
			// render contract to Warp's unique-identity rule in a presentation refactor.
			if (bPush && Index > ModuleIndex && Target.Type == EMixtormatLayerChildType::Generator
				&& Target.Generator.bEnabled && !Target.ScopeOwnerChildId.IsValid()
				&& Target.Generator.Type == EMixtormatGeneratorType::StrataCarver)
			{
				Status.ChildIndex = Index;
				Status.Issue = EStructuralLinkIssue::None;
				return Status;
			}
		}
		if (Matches == 0) { Status.Issue = EStructuralLinkIssue::MissingChild; }
		else if (!bPush && Matches > 1) { Status.Issue = EStructuralLinkIssue::DuplicateIdentity; }
		else
		{
			const FMixtormatLayerChild& Target = Layer.Children[Status.ChildIndex];
			if (Status.ChildIndex <= ModuleIndex) { Status.Issue = EStructuralLinkIssue::ForwardTarget; }
			else if (Target.Type != EMixtormatLayerChildType::Generator
				|| (bPush && Target.Generator.Type != EMixtormatGeneratorType::StrataCarver))
			{
				Status.Issue = EStructuralLinkIssue::WrongTargetKind;
			}
			else if (!Target.Generator.bEnabled) { Status.Issue = EStructuralLinkIssue::DisabledTarget; }
			else if (Target.ScopeOwnerChildId.IsValid()) { Status.Issue = EStructuralLinkIssue::ScopedTarget; }
			else { Status.Issue = EStructuralLinkIssue::None; }
		}
		return Status;
	}

	FStructuralLinkStatus EvaluateStructuralLink(const TArray<FMixtormatLayer>& Layers,
		const int32 ModuleLayerIndex, const int32 ModuleChildIndex,
		const FMixtormatOutputReference* ProposedSource, const FGuid* ProposedTarget)
	{
		FStructuralLinkStatus Status;
		if (!Layers.IsValidIndex(ModuleLayerIndex))
		{
			Status.ModuleIssue = EStructuralLinkIssue::MissingLayer;
			return Status;
		}
		const FMixtormatLayer& Layer = Layers[ModuleLayerIndex];
		if (!Layer.Children.IsValidIndex(ModuleChildIndex))
		{
			Status.ModuleIssue = EStructuralLinkIssue::MissingChild;
			return Status;
		}
		const FMixtormatLayerChild& Module = Layer.Children[ModuleChildIndex];
		const bool bPush = Module.Type == EMixtormatLayerChildType::HeightPush;
		if (!bPush && Module.Type != EMixtormatLayerChildType::StructuralWarp)
		{
			Status.ModuleIssue = EStructuralLinkIssue::WrongModuleType;
			return Status;
		}
		Status.bModuleEnabled = bPush ? Module.HeightPush.bEnabled : Module.StructuralWarp.bEnabled;
		const FMixtormatOutputReference& Source = ProposedSource ? *ProposedSource
			: bPush ? Module.HeightPush.Source : Module.StructuralWarp.Source;
		const FGuid& Target = ProposedTarget ? *ProposedTarget
			: bPush ? Module.HeightPush.TargetChildId : Module.StructuralWarp.TargetChildId;
		// Destination inactivity/scoping is orthogonal to the saved edges. The shared
		// predicate ignores only destination flags; producer flags and scope checks remain.
		if (Layer.Type != EMixtormatLayerType::Generator)
		{
			Status.ModuleIssue = EStructuralLinkIssue::WrongOwnerLayer;
			return Status;
		}
		if (!Layer.bEnabled) { Status.ModuleIssue = EStructuralLinkIssue::DisabledLayer; }
		else if (Module.ScopeOwnerChildId.IsValid()) { Status.ModuleIssue = EStructuralLinkIssue::ScopedModule; }
		int32 ModuleLayerMatches = 0;
		for (const FMixtormatLayer& Candidate : Layers)
		{
			if (Candidate.LayerId == Layer.LayerId) { ++ModuleLayerMatches; }
		}
		int32 ModuleChildMatches = 0;
		for (const FMixtormatLayerChild& Candidate : Layer.Children)
		{
			if (Candidate.ChildId == Module.ChildId) { ++ModuleChildMatches; }
		}
		if (!Layer.LayerId.IsValid()) { Status.ModuleIssue = EStructuralLinkIssue::MissingLayer; }
		else if (!Module.ChildId.IsValid()) { Status.ModuleIssue = EStructuralLinkIssue::MissingChild; }
		else if (ModuleLayerMatches > 1 || ModuleChildMatches > 1)
		{
			Status.ModuleIssue = EStructuralLinkIssue::DuplicateIdentity;
		}
		EvaluateGeneratorInputSource(Layers, ModuleLayerIndex, ModuleChildIndex, Source, Status.Source, true);
		if (Target.IsValid())
		{
			Status.Target = EvaluateStructuralTarget(Layer, ModuleLayerIndex, ModuleChildIndex, Target, bPush);
		}
		Status.bCanExecuteStructurally = Status.ModuleIssue == EStructuralLinkIssue::None
			&& Status.bModuleEnabled && Status.Source.Issue == EStructuralLinkIssue::None
			&& Status.Target.Issue == EStructuralLinkIssue::None;
		return Status;
	}

	FStructuralLinkStatus EvaluateStructuralLinkForGather(const TArray<FMixtormatLayer>& EffectiveLayers,
		const int32 ModuleLayerIndex, const int32 ModuleChildIndex, const FMixtormatLayer& ResolvedModuleLayer)
	{
		FStructuralLinkStatus Status;
		if (!EffectiveLayers.IsValidIndex(ModuleLayerIndex))
		{
			Status.ModuleIssue = EStructuralLinkIssue::MissingLayer;
			return Status;
		}
		if (!ResolvedModuleLayer.Children.IsValidIndex(ModuleChildIndex)
			|| !EffectiveLayers[ModuleLayerIndex].Children.IsValidIndex(ModuleChildIndex))
		{
			Status.ModuleIssue = EStructuralLinkIssue::MissingChild;
			return Status;
		}
		const FMixtormatLayerChild& Module = ResolvedModuleLayer.Children[ModuleChildIndex];
		const bool bPush = Module.Type == EMixtormatLayerChildType::HeightPush;
		if (!bPush && Module.Type != EMixtormatLayerChildType::StructuralWarp)
		{
			Status.ModuleIssue = EStructuralLinkIssue::WrongModuleType;
			return Status;
		}
		const FMixtormatOutputReference& Source = bPush ? Module.HeightPush.Source : Module.StructuralWarp.Source;
		const FGuid& TargetId = bPush ? Module.HeightPush.TargetChildId : Module.StructuralWarp.TargetChildId;
		Status = EvaluateStructuralLink(EffectiveLayers, ModuleLayerIndex, ModuleChildIndex, &Source, &TargetId);
		Status.bModuleEnabled = bPush ? Module.HeightPush.bEnabled : Module.StructuralWarp.bEnabled;
		if (!ResolvedModuleLayer.bEnabled) { Status.ModuleIssue = EStructuralLinkIssue::DisabledLayer; }
		else if (ResolvedModuleLayer.Type != EMixtormatLayerType::Generator) { Status.ModuleIssue = EStructuralLinkIssue::WrongOwnerLayer; }
		else if (Module.ScopeOwnerChildId.IsValid()) { Status.ModuleIssue = EStructuralLinkIssue::ScopedModule; }
		if (bPush && TargetId.IsValid())
		{
			Status.Target = EvaluateStructuralTarget(ResolvedModuleLayer, ModuleLayerIndex, ModuleChildIndex, TargetId, true);
		}
		// Source gathering still gates the authored destination's enabled/type/scope flags.
		// Do not turn an inherited UI payload into a newly supported render connection.
		FStructuralEdgeStatus GatherSource;
		const int32 SourceIndex = EvaluateGeneratorInputSource(EffectiveLayers, ModuleLayerIndex,
			ModuleChildIndex, Source, GatherSource, false);
		Status.bCanExecuteStructurally = Status.ModuleIssue == EStructuralLinkIssue::None
			&& Status.bModuleEnabled && SourceIndex != INDEX_NONE
			&& Status.Source.Issue == EStructuralLinkIssue::None && Status.Target.Issue == EStructuralLinkIssue::None;
		return Status;
	}

	int32 ResolveHeightPushTarget(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex, const FGuid& TargetChildId)
	{
		if (!Layers.IsValidIndex(DestinationLayerIndex)) { return INDEX_NONE; }
		return ResolveHeightPushTarget(Layers[DestinationLayerIndex], DestinationChildIndex, TargetChildId);
	}

	int32 ResolveHeightPushTarget(const FMixtormatLayer& Layer,
		const int32 DestinationChildIndex, const FGuid& TargetChildId)
	{
		if (!Layer.bEnabled || Layer.Type != EMixtormatLayerType::Generator
			|| !Layer.Children.IsValidIndex(DestinationChildIndex)) { return INDEX_NONE; }
		const FMixtormatLayerChild& Module = Layer.Children[DestinationChildIndex];
		if (Module.Type != EMixtormatLayerChildType::HeightPush
			|| !Module.HeightPush.bEnabled || Module.ScopeOwnerChildId.IsValid()) { return INDEX_NONE; }
		const FStructuralEdgeStatus Status = EvaluateStructuralTarget(
			Layer, INDEX_NONE, DestinationChildIndex, TargetChildId, true);
		return Status.Issue == EStructuralLinkIssue::None ? Status.ChildIndex : INDEX_NONE;
	}

	int32 ResolveStructuralWarpTarget(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex, const FGuid& TargetChildId)
	{
		if (!Layers.IsValidIndex(DestinationLayerIndex)) { return INDEX_NONE; }
		const FMixtormatLayer& Layer = Layers[DestinationLayerIndex];
		if (!Layer.bEnabled || Layer.Type != EMixtormatLayerType::Generator
			|| !Layer.Children.IsValidIndex(DestinationChildIndex)) { return INDEX_NONE; }
		const FMixtormatLayerChild& Module = Layer.Children[DestinationChildIndex];
		if (Module.Type != EMixtormatLayerChildType::StructuralWarp
			|| !Module.StructuralWarp.bEnabled || Module.ScopeOwnerChildId.IsValid()) { return INDEX_NONE; }
		const FStructuralEdgeStatus Status = EvaluateStructuralTarget(
			Layer, DestinationLayerIndex, DestinationChildIndex, TargetChildId, false);
		return Status.Issue == EStructuralLinkIssue::None ? Status.ChildIndex : INDEX_NONE;
	}

	int32 ResolvePublishedMaskSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const int32 DestinationChildIndex,
		const FMixtormatMaskLayer& Mask)
	{
		if (!Mask.HasPublishedSource() || !Layers.IsValidIndex(DestinationLayerIndex)
			|| !Layers[DestinationLayerIndex].Children.IsValidIndex(DestinationChildIndex)) { return INDEX_NONE; }
		const bool bNoiseValue = Mask.PublishedSourceOutput == FName(TEXT("Value"));
		int32 SourceLayerIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Layers[Index].LayerId != Mask.PublishedSourceLayerId) { continue; }
			if (SourceLayerIndex != INDEX_NONE) { return INDEX_NONE; }
			SourceLayerIndex = Index;
			if (!bNoiseValue) { break; }
		}
		if (SourceLayerIndex == INDEX_NONE) { return INDEX_NONE; }
		const FMixtormatLayer& AuthoredSourceLayer = Layers[SourceLayerIndex];
		const int32 SourceIndex = AuthoredSourceLayer.Children.IndexOfByPredicate(
			[&](const FMixtormatLayerChild& Child) { return Child.ChildId == Mask.PublishedSourceChildId; });
		if (SourceIndex == INDEX_NONE || !bNoiseValue) { return SourceIndex; }

		// Value stays a typed field. Only its use as coverage has this mask-specific ordering.
		if (!Mask.bEnabled || !Layers[DestinationLayerIndex].bEnabled
			|| SourceLayerIndex > DestinationLayerIndex || !AuthoredSourceLayer.bEnabled
			|| AuthoredSourceLayer.Type != EMixtormatLayerType::Generator) { return INDEX_NONE; }
		FMixtormatLayer SourceLayer = AuthoredSourceLayer;
		MixtormatParameterBinding::ResolveChildInstances(FMixtormatBindingScope{Layers}, SourceLayer);
		const FMixtormatLayerChild& Source = SourceLayer.Children[SourceIndex];
		if (Source.Type != EMixtormatLayerChildType::Generator
			|| Source.Generator.Type != EMixtormatGeneratorType::Noise
			|| !Source.Generator.bEnabled || Source.ScopeOwnerChildId.IsValid()) { return INDEX_NONE; }

		// Reject ambiguous identities and malformed scopes before computing completion order.
		TMap<FGuid, int32> ChildIndices;
		for (int32 Index = 0; Index < SourceLayer.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& Child = SourceLayer.Children[Index];
			if (!Child.ChildId.IsValid() || ChildIndices.Contains(Child.ChildId)) { return INDEX_NONE; }
			if (Child.ScopeOwnerChildId.IsValid() && !ChildIndices.Contains(Child.ScopeOwnerChildId)) { return INDEX_NONE; }
			ChildIndices.Add(Child.ChildId, Index);
		}
		if (SourceLayerIndex != DestinationLayerIndex) { return SourceIndex; }

		// A gate is evaluated by its owner, not at the gate row's authored position.
		int32 ConsumerStart = DestinationChildIndex;
		FGuid ParentId = Layers[DestinationLayerIndex].Children[DestinationChildIndex].ScopeOwnerChildId;
		while (ParentId.IsValid())
		{
			const int32* ParentIndex = ChildIndices.Find(ParentId);
			if (!ParentIndex || *ParentIndex >= ConsumerStart) { return INDEX_NONE; }
			ConsumerStart = *ParentIndex;
			ParentId = SourceLayer.Children[ConsumerStart].ScopeOwnerChildId;
		}
		if (SourceIndex >= ConsumerStart) { return INDEX_NONE; }
		for (int32 Index = ConsumerStart; Index < SourceLayer.Children.Num(); ++Index)
		{
			ParentId = SourceLayer.Children[Index].ScopeOwnerChildId;
			while (ParentId.IsValid())
			{
				if (ParentId == Source.ChildId) { return INDEX_NONE; }
				ParentId = SourceLayer.Children[ChildIndices.FindChecked(ParentId)].ScopeOwnerChildId;
			}
		}
		return SourceIndex;
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
