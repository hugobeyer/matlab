// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatChildScope.h"
#include "MixtormatOutputReference.h"

namespace MixtormatChildScope
{
	int32 FindIndexById(const TArray<FMixtormatLayerChild>& Children, const FGuid& ChildId)
	{
		if (!ChildId.IsValid())
		{
			return INDEX_NONE;
		}
		return Children.IndexOfByPredicate([&ChildId](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == ChildId;
		});
	}

	int32 ResolveOwnerIndex(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return INDEX_NONE;
		}
		const FGuid& OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		if (!OwnerId.IsValid() || OwnerId == Children[ChildIndex].ChildId)
		{
			return INDEX_NONE;
		}
		const int32 OwnerIndex = FindIndexById(Children, OwnerId);
		if (OwnerIndex == INDEX_NONE || OwnerIndex >= ChildIndex)
		{
			return INDEX_NONE;
		}
		return OwnerIndex;
	}

	int32 ResolveBehaviorGeneratorIndex(
		const TArray<FMixtormatLayerChild>& Children, const int32 BehaviorChildIndex)
	{
		if (!Children.IsValidIndex(BehaviorChildIndex)
			|| Children[BehaviorChildIndex].Type != EMixtormatLayerChildType::Behavior)
		{
			return INDEX_NONE;
		}
		int32 OwnerIndex = ResolveOwnerIndex(Children, BehaviorChildIndex);
		TSet<int32> Visited;
		while (Children.IsValidIndex(OwnerIndex) && !Visited.Contains(OwnerIndex))
		{
			Visited.Add(OwnerIndex);
			if (Children[OwnerIndex].Type == EMixtormatLayerChildType::Generator)
			{
				return OwnerIndex;
			}
			if (Children[OwnerIndex].Type != EMixtormatLayerChildType::Behavior)
			{
				return INDEX_NONE;
			}
			OwnerIndex = ResolveOwnerIndex(Children, OwnerIndex);
		}
		return INDEX_NONE;
	}

	FBehaviorInputStatus ValidateBehaviorInputs(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const int32 LayerIndex, const int32 BehaviorChildIndex,
		const FMixtormatLayer* ResolvedLayer)
	{
		FBehaviorInputStatus Result;
		if (!EffectiveLayers.IsValidIndex(LayerIndex)) { return Result; }
		// Validation may inspect binding-resolved settings without copying the full
		// effective stack or rewriting its producer indices.
		const FMixtormatLayer& Layer = ResolvedLayer
			? *ResolvedLayer : EffectiveLayers[LayerIndex];
		if (ResolvedLayer && Layer.LayerId != EffectiveLayers[LayerIndex].LayerId)
		{
			return Result;
		}
		const int32 GeneratorIndex = ResolveBehaviorGeneratorIndex(Layer.Children, BehaviorChildIndex);
		if (!Layer.Children.IsValidIndex(GeneratorIndex))
		{
			return Result;
		}
		Result.GeneratorChildIndex = GeneratorIndex;
		const FMixtormatLayerChild& Owner = Layer.Children[GeneratorIndex];
		const FMixtormatBehavior& Behavior = Layer.Children[BehaviorChildIndex].Behavior;
		// Serialized but not executable operations must never appear as valid
		// inputs to a gather, source picker, or later capability query.
		if ((Behavior.Stage != EMixtormatBehaviorStage::PostGeneration
			&& !(Behavior.Stage == EMixtormatBehaviorStage::PreGeneration
				&& Behavior.Type == EMixtormatBehaviorType::Warp
				&& Behavior.Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput))
			|| (Behavior.Type != EMixtormatBehaviorType::Warp
				&& Behavior.Type != EMixtormatBehaviorType::Push
				&& Behavior.Type != EMixtormatBehaviorType::Carve
				&& Behavior.Type != EMixtormatBehaviorType::Deform
				&& Behavior.Type != EMixtormatBehaviorType::FlowField
				&& Behavior.Type != EMixtormatBehaviorType::Feather))
		{
			Result.Issue = EBehaviorInputIssue::UnsupportedOperation;
			return Result;
		}
		if (!Layer.bEnabled || !Owner.Generator.bEnabled || !Behavior.bEnabled)
		{
			Result.Issue = EBehaviorInputIssue::Disabled;
			return Result;
		}
		// A Behavior is a genuine child of its immediate parent, not an
		// independent modifier of the nearest generator. Disabled intermediate
		// Behaviors or scoped generators must suppress the entire subtree.
		for (int32 AncestorIndex = ResolveOwnerIndex(Layer.Children, BehaviorChildIndex);
			Layer.Children.IsValidIndex(AncestorIndex);
			AncestorIndex = ResolveOwnerIndex(Layer.Children, AncestorIndex))
		{
			const FMixtormatLayerChild& Ancestor = Layer.Children[AncestorIndex];
			if ((Ancestor.Type == EMixtormatLayerChildType::Behavior
					&& !Ancestor.Behavior.bEnabled)
				|| (Ancestor.Type == EMixtormatLayerChildType::Generator
					&& !Ancestor.Generator.bEnabled))
			{
				Result.Issue = EBehaviorInputIssue::Disabled;
				return Result;
			}
		}
		if (!FMath::IsFinite(Behavior.Strength))
		{
			Result.Issue = EBehaviorInputIssue::InvalidStrength;
			return Result;
		}
		const auto CheckInput = [&](const FMixtormatBehaviorFieldInput& Input,
			const bool bDirection, const bool bInfluence, const bool bRequired) -> EBehaviorInputIssue
		{
			if (Input.Origin == EMixtormatBehaviorFieldOrigin::None)
			{
				return bRequired ? EBehaviorInputIssue::MissingInput : EBehaviorInputIssue::None;
			}
			if (Input.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput)
			{
				if (bDirection && Input.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight)
				{
					return (Behavior.Type == EMixtormatBehaviorType::Warp
							|| Behavior.Type == EMixtormatBehaviorType::Deform)
						&& Behavior.Stage == EMixtormatBehaviorStage::PostGeneration
						? EBehaviorInputIssue::None : EBehaviorInputIssue::InvalidStage;
				}
				if (bDirection || bInfluence) { return EBehaviorInputIssue::WrongFieldKind; }
				if (Input.Origin == EMixtormatBehaviorFieldOrigin::OwnNativeHeight)
				{
					return Behavior.Stage == EMixtormatBehaviorStage::PostGeneration
						? EBehaviorInputIssue::None : EBehaviorInputIssue::InvalidStage;
				}
				if (Input.Origin == EMixtormatBehaviorFieldOrigin::OwnBoundary)
				{
					if (Behavior.Type != EMixtormatBehaviorType::Carve)
					{
						return EBehaviorInputIssue::WrongFieldKind;
					}
					if (Behavior.Stage != EMixtormatBehaviorStage::PostGeneration)
					{
						return EBehaviorInputIssue::InvalidStage;
					}
					return MixtormatGeneratorHasFlowBoundary(Owner.Generator.Type)
						? EBehaviorInputIssue::None : EBehaviorInputIssue::UnsupportedBoundary;
				}
				return Input.Origin == EMixtormatBehaviorFieldOrigin::PreviousRunningHeight
					&& Behavior.Stage == EMixtormatBehaviorStage::PostGeneration
					? EBehaviorInputIssue::None : EBehaviorInputIssue::WrongFieldKind;
			}
			const FMixtormatOutputReference& Ref = Input.Published;
			if (!Ref.bEnabled || !Ref.HasSource())
			{
				return EBehaviorInputIssue::InvalidPublishedSource;
			}
			const bool bKindCorrect = bDirection
				? (Ref.Kind == EMixtormatPublishedFieldKind::Flow
					|| Ref.Kind == EMixtormatPublishedFieldKind::UVMap)
				: Ref.Kind == (bInfluence
					? EMixtormatPublishedFieldKind::Scalar01
					: Behavior.Type == EMixtormatBehaviorType::Carve
						? EMixtormatPublishedFieldKind::SDF
						: EMixtormatPublishedFieldKind::ScalarSigned);
			if (!bKindCorrect) { return EBehaviorInputIssue::WrongFieldKind; }
			// Generic Scalar01 is not a generator Height/Warp socket. Validate it
			// through the normal published-field dependency rules instead.
			int32 Index = (bInfluence || (!bDirection
				&& Behavior.Type == EMixtormatBehaviorType::Carve))
				? MixtormatOutputReferences::ResolveSource(
					EffectiveLayers, LayerIndex, GeneratorIndex, Ref)
				: MixtormatOutputReferences::ResolveGeneratorInputSource(
					EffectiveLayers, LayerIndex, GeneratorIndex, Ref);
			// Same-generator Behavior-to-Behavior ordering: when both belong to the same generator,
			// the source Behavior must appear before this consumer Behavior in authored order.
			if (Index != INDEX_NONE && Ref.SourceLayerId == Layer.LayerId)
			{
				const FMixtormatLayerChild& SourceChild = Layer.Children[Index];
				if (SourceChild.Type == EMixtormatLayerChildType::Behavior
					&& SourceChild.ScopeOwnerChildId == Owner.ChildId)
				{
					if (Index >= BehaviorChildIndex)
					{
						return EBehaviorInputIssue::InvalidPublishedSource;
					}
				}
			}
		// Behavior Push may consume published generic ScalarSigned outputs (Noise
		// Value, or a copy), not only the legacy generator's canonical Height.
		if (Index == INDEX_NONE && !bDirection && !bInfluence
			&& Behavior.Type == EMixtormatBehaviorType::Push)
		{
			Index = MixtormatOutputReferences::ResolveSource(
				EffectiveLayers, LayerIndex, GeneratorIndex, Ref);
		}
			if (Index == INDEX_NONE) { return EBehaviorInputIssue::InvalidPublishedSource; }
			Result.SourceChildIndex = Index;
			return EBehaviorInputIssue::None;
		};
		// Feather is a signed-height modifier; it never consumes Flow sockets.
		if (Behavior.Type == EMixtormatBehaviorType::Feather
			&& (Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None
				|| Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::None
				|| Behavior.Influence.Origin != EMixtormatBehaviorFieldOrigin::None
				|| !FMath::IsFinite(Behavior.Feather.Radius)
				|| !FMath::IsFinite(Behavior.Feather.DirectionBias)
				|| Behavior.Feather.Radius < 0.0f))
		{
			Result.Issue = EBehaviorInputIssue::WrongFieldKind;
			return Result;
		}
		const bool bTraced = Behavior.Type == EMixtormatBehaviorType::Carve
			|| (Behavior.Type != EMixtormatBehaviorType::Warp
				&& Behavior.Type != EMixtormatBehaviorType::Feather
				&& Behavior.Flow.bUseTracedFlow);
		// An unconnected Warp/Deform automatically consumes the generator's
		// ordered working Flow. A manually authored socket still validates strictly.
		const bool bNeedsDirection = !bTraced
			&& Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None
			&& (Behavior.Type == EMixtormatBehaviorType::Warp
				|| Behavior.Type == EMixtormatBehaviorType::Deform);
		const bool bNeedsHeight = Behavior.Type == EMixtormatBehaviorType::Push;
		// Reject inactive operation sockets too: gather does not evaluate a Warp
		// with Height connected, or a Push/Carve with Direction connected.
		if (((Behavior.Type == EMixtormatBehaviorType::Warp || bNeedsDirection)
			&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::None)
			|| (bNeedsHeight && Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None)
			|| ((bTraced || Behavior.Type == EMixtormatBehaviorType::FlowField)
				&& (Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::None
					|| Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None))
			|| (bTraced && Behavior.Type != EMixtormatBehaviorType::Warp
				&& Behavior.Type != EMixtormatBehaviorType::Carve
				&& Behavior.Type != EMixtormatBehaviorType::Deform))
		{
			Result.Issue = EBehaviorInputIssue::WrongFieldKind;
			return Result;
		}
		// Field composition. Amplitude and reverse are per-socket and judged against
		// the kind the socket actually carries; blend folds a value into a base and
		// only Push has one. An unconnected socket carries no composition at all.
		const auto CheckComposition = [](const FMixtormatBehaviorFieldInput& Input,
			const bool bAllowsBlend) -> EBehaviorInputIssue
		{
			if (Input.Origin == EMixtormatBehaviorFieldOrigin::None) { return EBehaviorInputIssue::None; }
			if (!FMath::IsFinite(Input.Amplitude))
			{
				return EBehaviorInputIssue::InvalidStrength;
			}
			const EMixtormatBehaviorFieldKind Kind = MixtormatBehaviorFieldEffectiveKind(Input);
			if (Input.bReversed && !FMixtormatBehaviorFieldInput::KindSupportsReverse(Kind))
			{
				return EBehaviorInputIssue::WrongFieldKind;
			}
			if (Input.Blend != EMixtormatBehaviorFieldBlend::Operation
				&& !(bAllowsBlend && FMixtormatBehaviorFieldInput::KindSupportsBlend(Kind)))
			{
				return EBehaviorInputIssue::UnsupportedOperation;
			}
			return EBehaviorInputIssue::None;
		};
		Result.Issue = CheckComposition(Behavior.Direction, false);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		Result.Issue = CheckComposition(Behavior.Height,
			Behavior.Type == EMixtormatBehaviorType::Push);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		Result.Issue = CheckComposition(Behavior.Influence, false);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		if (Behavior.Type == EMixtormatBehaviorType::FlowField
			&& Behavior.Flow.FlowSource == EMixtormatBehaviorFlowSource::SignedDistance
			&& !MixtormatGeneratorHasFlowBoundary(Owner.Generator.Type))
		{
			Result.Issue = EBehaviorInputIssue::UnsupportedBoundary;
			return Result;
		}
		Result.Issue = CheckInput(Behavior.Direction, true, false, bNeedsDirection);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		if (Behavior.Type == EMixtormatBehaviorType::Carve && !bTraced
			&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::OwnBoundary
			&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput)
		{
			Result.Issue = EBehaviorInputIssue::WrongFieldKind;
			return Result;
		}
		Result.Issue = CheckInput(Behavior.Height, false, false, bNeedsHeight);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		Result.Issue = CheckInput(Behavior.Influence, false, true, false);
		if (Result.Issue != EBehaviorInputIssue::None) { return Result; }
		Result.Issue = EBehaviorInputIssue::None;
		Result.bCanEvaluate = true;
		return Result;
	}

	int32 ResolveBlurFlowSource(const TArray<FMixtormatLayer>& EffectiveLayers,
		const int32 LayerIndex, const int32 BlurChildIndex,
		const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || !Reference.HasSource()
			|| Reference.Kind != EMixtormatPublishedFieldKind::Flow
			|| !EffectiveLayers.IsValidIndex(LayerIndex)) { return INDEX_NONE; }
		const FMixtormatLayer& Destination = EffectiveLayers[LayerIndex];
		if (!Destination.bEnabled || !Destination.Children.IsValidIndex(BlurChildIndex)
			|| Destination.Children[BlurChildIndex].Type != EMixtormatLayerChildType::Blur
			|| !Destination.Children[BlurChildIndex].Blur.bEnabled) { return INDEX_NONE; }

		int32 SourceLayerIndex = INDEX_NONE;
		TSet<FGuid> LayerIds;
		for (int32 Index = 0; Index < EffectiveLayers.Num(); ++Index)
		{
			const FGuid Id = EffectiveLayers[Index].LayerId;
			if (!Id.IsValid() || LayerIds.Contains(Id)) { return INDEX_NONE; }
			LayerIds.Add(Id);
			if (Id == Reference.SourceLayerId) { SourceLayerIndex = Index; }
		}
		if (SourceLayerIndex == INDEX_NONE || SourceLayerIndex > LayerIndex) { return INDEX_NONE; }
		const FMixtormatLayer& SourceLayer = EffectiveLayers[SourceLayerIndex];
		if (!SourceLayer.bEnabled) { return INDEX_NONE; }

		const auto IsEnabled = [](const FMixtormatLayerChild& Child)
		{
			switch (Child.Type)
			{
			case EMixtormatLayerChildType::Generator: return Child.Generator.bEnabled;
			case EMixtormatLayerChildType::Boolean: return Child.Boolean.bEnabled;
			case EMixtormatLayerChildType::Behavior: return Child.Behavior.bEnabled;
			case EMixtormatLayerChildType::Mask: return Child.Mask.bEnabled;
			case EMixtormatLayerChildType::Effect: return Child.Effect.bEnabled;
			case EMixtormatLayerChildType::Generated: return Child.Generated.bEnabled;
			case EMixtormatLayerChildType::Craquelure: return Child.Craquelure.bEnabled;
			case EMixtormatLayerChildType::ColorId: return Child.ColorId.bEnabled;
			case EMixtormatLayerChildType::RandomId: return Child.RandomId.bEnabled;
			case EMixtormatLayerChildType::Curvature: return Child.Curvature.bEnabled;
			case EMixtormatLayerChildType::HeightColorRamp: return Child.HeightColorRamp.bEnabled;
			case EMixtormatLayerChildType::Blur: return Child.Blur.bEnabled;
			case EMixtormatLayerChildType::OutputReference: return Child.OutputReference.bEnabled;
			default: return false;
			}
		};
		// Reject ambiguous/malformed scopes before walking any ancestry.
		const auto HasValidScopes = [](const TArray<FMixtormatLayerChild>& Children)
		{
			TSet<FGuid> Seen;
			for (const FMixtormatLayerChild& Child : Children)
			{
				if (!Child.ChildId.IsValid() || Seen.Contains(Child.ChildId)
					|| (Child.ScopeOwnerChildId.IsValid() && !Seen.Contains(Child.ScopeOwnerChildId))) { return false; }
				Seen.Add(Child.ChildId);
			}
			return true;
		};
		if (!HasValidScopes(Destination.Children) || !HasValidScopes(SourceLayer.Children)) { return INDEX_NONE; }
		for (int32 Index = BlurChildIndex; Index != INDEX_NONE;
			Index = ResolveOwnerIndex(Destination.Children, Index))
		{
			if (!IsEnabled(Destination.Children[Index])) { return INDEX_NONE; }
		}
		const int32 SourceIndex = FindIndexById(SourceLayer.Children, Reference.SourceChildId);
		if (SourceIndex == INDEX_NONE) { return INDEX_NONE; }
		for (int32 Index = SourceIndex; Index != INDEX_NONE;
			Index = ResolveOwnerIndex(SourceLayer.Children, Index))
		{
			if (!IsEnabled(SourceLayer.Children[Index])) { return INDEX_NONE; }
		}
		// Generator native Flow and completed Behavior Flow are available before an
		// owned Blur executes, even inside its parent scope. Only later rows would
		// read a future producer. Root Layer Blur executes after the full layer rail.
		if (SourceLayerIndex == LayerIndex
			&& Destination.Children[BlurChildIndex].ScopeOwnerChildId.IsValid()
			&& SourceIndex >= BlurChildIndex) { return INDEX_NONE; }
		const FMixtormatLayerChild& Source = SourceLayer.Children[SourceIndex];
		if (Source.Type == EMixtormatLayerChildType::Generator)
		{
			return MixtormatOutputReferences::GeneratorPublishesField(
				Source.Generator, Reference.Kind, Reference.OutputName) ? SourceIndex : INDEX_NONE;
		}
		if (Source.Type == EMixtormatLayerChildType::Behavior)
		{
			if (Reference.OutputName != MixtormatOutputReferences::CanonicalFieldOutputName(Reference.Kind)
				|| !ValidateBehaviorInputs(EffectiveLayers, SourceLayerIndex, SourceIndex).bCanEvaluate) { return INDEX_NONE; }
			const FMixtormatBehavior& Behavior = Source.Behavior;
			if (Behavior.Type == EMixtormatBehaviorType::FlowField
				|| ((Behavior.Type == EMixtormatBehaviorType::Push
					|| Behavior.Type == EMixtormatBehaviorType::Carve
					|| Behavior.Type == EMixtormatBehaviorType::Deform) && Behavior.Flow.bUseTracedFlow))
			{
				return SourceIndex;
			}
			if (Behavior.Type == EMixtormatBehaviorType::Feather)
			{
				// Feather publishes Flow only under a Flow-primary ancestor, not Height.
				for (int32 Index = ResolveOwnerIndex(SourceLayer.Children, SourceIndex); Index != INDEX_NONE;
					Index = ResolveOwnerIndex(SourceLayer.Children, Index))
				{
					const FMixtormatLayerChild& Ancestor = SourceLayer.Children[Index];
					if ((Ancestor.Type == EMixtormatLayerChildType::Behavior
						&& Ancestor.Behavior.Type == EMixtormatBehaviorType::FlowField)
						|| (Ancestor.Type == EMixtormatLayerChildType::Generator
							&& Ancestor.Generator.Type == EMixtormatGeneratorType::Noise
							&& Ancestor.Generator.Noise.NoisePreset == EMixtormatNoisePreset::Flow)) { return SourceIndex; }
				}
			}
			return INDEX_NONE;
		}
		// Published copies retain the existing earlier-layer-only reference contract.
		return SourceLayerIndex < LayerIndex
			? MixtormatOutputReferences::ResolveSource(EffectiveLayers, LayerIndex, BlurChildIndex, Reference)
			: INDEX_NONE;
	}

	bool CanOwnScopedBlurs(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Mask
			|| Child.Type == EMixtormatLayerChildType::Curvature
			|| Child.Type == EMixtormatLayerChildType::Generator
			|| Child.Type == EMixtormatLayerChildType::Behavior
			|| Child.Type == EMixtormatLayerChildType::HeightColorRamp;
	}

	bool CanOwnScopedSurfacing(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Generator
			|| Child.Type == EMixtormatLayerChildType::Surfacing;
	}

	bool HasValidSurfacingScope(const TArray<FMixtormatLayerChild>& Children, int32 ChildIndex)
	{
		if (!Children.IsValidIndex(ChildIndex)
			|| Children[ChildIndex].Type != EMixtormatLayerChildType::Surfacing
			|| !Children[ChildIndex].Surfacing.bEnabled) { return false; }
		bool bDirectOwner = true;
		while (Children[ChildIndex].ScopeOwnerChildId.IsValid())
		{
			ChildIndex = ResolveOwnerIndex(Children, ChildIndex);
			if (!Children.IsValidIndex(ChildIndex)) { return false; }
			const FMixtormatLayerChild& Owner = Children[ChildIndex];
			if ((!CanOwnScopedSurfacing(Owner)
					&& (bDirectOwner || Owner.Type != EMixtormatLayerChildType::Boolean))
				|| (Owner.Type == EMixtormatLayerChildType::Generator && !Owner.Generator.bEnabled)
				|| (Owner.Type == EMixtormatLayerChildType::Surfacing && !Owner.Surfacing.bEnabled)
				|| (Owner.Type == EMixtormatLayerChildType::Boolean && !Owner.Boolean.bEnabled))
			{ return false; }
			bDirectOwner = false;
		}
		return true;
	}

	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask:
		case EMixtormatLayerChildType::Effect:
		case EMixtormatLayerChildType::Boolean:
		case EMixtormatLayerChildType::Generator:
		case EMixtormatLayerChildType::Surfacing:
		case EMixtormatLayerChildType::Generated:
		case EMixtormatLayerChildType::Craquelure:
		case EMixtormatLayerChildType::ColorId:
		case EMixtormatLayerChildType::RandomId:
		case EMixtormatLayerChildType::HsvFilter:
		case EMixtormatLayerChildType::RampId:
		case EMixtormatLayerChildType::ReliefFromIds:
		case EMixtormatLayerChildType::BoundaryFromIds:
		// A Color Ramp module gates where the colour it publishes shows. The mask child's own
		// Weight is the influence; no separate control is needed.
		case EMixtormatLayerChildType::HeightColorRamp:
		case EMixtormatLayerChildType::Behavior:
		case EMixtormatLayerChildType::Blur:
			return true;
		default:
			return false;
		}
	}

	bool SanitizeStaleOwners(TArray<FMixtormatLayerChild>& Children)
	{
		bool bChanged = false;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			FMixtormatLayerChild& Child = Children[Index];
			if (Child.Type == EMixtormatLayerChildType::Surfacing)
			{
				// Preserve a broken scope for repair; never silently promote Skin to a root solve.
				if (Child.ScopeOwnerChildId.IsValid()
					&& ResolveOwnerIndex(Children, Index) == INDEX_NONE && Child.Surfacing.bEnabled)
				{
					Child.Surfacing.bEnabled = false;
					bChanged = true;
				}
				continue;
			}
			if (Child.Type == EMixtormatLayerChildType::Behavior)
			{
				// Behaviors may be owned indirectly through other Behaviors.
				// Preserve invalid owner IDs for repair while disabling invalid operations.
				if (ResolveBehaviorGeneratorIndex(Children, Index) == INDEX_NONE
					&& Child.Behavior.bEnabled)
				{
					Child.Behavior.bEnabled = false;
					bChanged = true;
				}
				continue;
			}
			if (Child.ScopeOwnerChildId.IsValid()
				&& ResolveOwnerIndex(Children, Index) == INDEX_NONE)
			{
				Child.ScopeOwnerChildId.Invalidate();
				bChanged = true;
			}
		}
		return bChanged;
	}
}
