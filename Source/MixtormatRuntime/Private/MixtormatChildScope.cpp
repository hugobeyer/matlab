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
		const int32 OwnerIndex = ResolveOwnerIndex(Children, BehaviorChildIndex);
		return Children.IsValidIndex(OwnerIndex)
			&& Children[OwnerIndex].Type == EMixtormatLayerChildType::Generator
			? OwnerIndex : INDEX_NONE;
	}

	FBehaviorInputStatus ValidateBehaviorInputs(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const int32 LayerIndex, const int32 BehaviorChildIndex,
		const TArray<FMixtormatSourceEntry>& Sources,
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
		if (!Layer.Children.IsValidIndex(GeneratorIndex) || Layer.Type != EMixtormatLayerType::Generator)
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
				&& Behavior.Type != EMixtormatBehaviorType::FlowField))
		{
			Result.Issue = EBehaviorInputIssue::UnsupportedOperation;
			return Result;
		}
		if (!Layer.bEnabled || !Owner.Generator.bEnabled || !Behavior.bEnabled)
		{
			Result.Issue = EBehaviorInputIssue::Disabled;
			return Result;
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
			if (Ref.IsShelfSource())
			{
				// The current Influence gather supports ordered layer fields only.
				// Report shelf Influence as unavailable rather than validating a source
				// that the renderer cannot bind.
				if (bInfluence || Behavior.Type == EMixtormatBehaviorType::Carve)
				{
					return EBehaviorInputIssue::InvalidPublishedSource;
				}
				const auto ShelfStatus = MixtormatOutputReferences::ClassifyShelfSourceReference(Sources, Ref);
				if (ShelfStatus.Issue != MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
				{
					return EBehaviorInputIssue::InvalidPublishedSource;
				}
				Result.bShelfSource = true;
				return EBehaviorInputIssue::None;
			}
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
		const bool bTraced = Behavior.Flow.bUseTracedFlow;
		const bool bNeedsDirection = !bTraced && (Behavior.Type == EMixtormatBehaviorType::Warp
			|| Behavior.Type == EMixtormatBehaviorType::Deform);
		const bool bNeedsHeight = Behavior.Type == EMixtormatBehaviorType::Push
			|| (Behavior.Type == EMixtormatBehaviorType::Carve && !bTraced);
		// Reject inactive operation sockets too: gather does not evaluate a Warp
		// with Height connected, or a Push/Carve with Direction connected.
		if ((bNeedsDirection && Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::None)
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
		if ((bTraced || Behavior.Type == EMixtormatBehaviorType::FlowField)
			&& Behavior.Flow.GeneratorFlowSource == EMixtormatGeneratorFlowSource::SignedDistance
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

	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Effect:
		case EMixtormatLayerChildType::Generator:
		case EMixtormatLayerChildType::Generated:
		case EMixtormatLayerChildType::Craquelure:
		case EMixtormatLayerChildType::ColorId:
		case EMixtormatLayerChildType::RandomId:
		// A Color Ramp module gates where the colour it publishes shows. The mask child's own
		// Weight is the influence; no separate control is needed.
		case EMixtormatLayerChildType::HeightColorRamp:
		case EMixtormatLayerChildType::Behavior:
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
			if (Child.Type == EMixtormatLayerChildType::Behavior)
			{
				// A Behavior has no valid root or indirect scope. Keep any authored
				// owner identity for repair, but fail closed if it is not a Generator.
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
