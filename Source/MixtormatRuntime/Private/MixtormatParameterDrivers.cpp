// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatParameterDrivers.h"
#include "MixtormatGroupControls.h"
#include "MixtormatScalarRampMath.h"

namespace MixtormatParameterDrivers
{
	FCapability Capability(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target)
	{
		FCapability Result;
		if (!MixtormatParameterBinding::FindParameterProperty(Scope, Target))
		{ Result.Reason = NSLOCTEXT("Mixtormat", "DriverMissingTarget", "The exact parameter target is unavailable."); return Result; }
		if (MixtormatGroupControls::FindDrivingControl(Scope, Target))
		{ Result.Reason = NSLOCTEXT("Mixtormat", "DriverGroupConflict", "An independent group control owns this target. Detach it before adding a spatial driver."); return Result; }
		const FMixtormatLayerChild* Child = Target.ChildId.IsValid() ? MixtormatParameterBinding::FindChild(Scope, Target.LayerId, Target.ChildId) : nullptr;
		if (Child && Child->IsInstance())
		{ Result.Reason = NSLOCTEXT("Mixtormat", "DriverInstanceTarget", "Edit the instance's source; instance parameters cannot own local spatial drivers."); return Result; }
		const auto Match = [&Target](EMixtormatParameterOwnerType Owner, const TCHAR* Name) { return Target.Owner == Owner && Target.Parameter == Name; };
		bool bFloat = Target.ValueType == EMixtormatParameterValueType::Float &&
			(Match(EMixtormatParameterOwnerType::Layer, TEXT("RoughnessInfluence")) || Match(EMixtormatParameterOwnerType::Layer, TEXT("HeightBlendAmount"))
			|| Match(EMixtormatParameterOwnerType::Behavior, TEXT("Strength")) || Match(EMixtormatParameterOwnerType::Behavior, TEXT("GradientReach")));
		if (Child && Child->Type == EMixtormatLayerChildType::Behavior)
		{
			const FMixtormatBehavior& Behavior = Child->Behavior;
			const bool bPublishedFlow = Behavior.Direction.HasPublishedSource()
				&& Behavior.Direction.Published.Kind == EMixtormatPublishedFieldKind::Flow
				&& (Behavior.Type == EMixtormatBehaviorType::Warp || Behavior.Type == EMixtormatBehaviorType::Deform);
			bFloat |= Target.ValueType == EMixtormatParameterValueType::Float && bPublishedFlow
				&& (Match(EMixtormatParameterOwnerType::BehaviorFlow, TEXT("FlowAmount")) || Match(EMixtormatParameterOwnerType::BehaviorFlow, TEXT("FlowTraceLength")));
			bFloat |= Target.ValueType == EMixtormatParameterValueType::Float &&
				((Behavior.Type == EMixtormatBehaviorType::Carve && (Match(EMixtormatParameterOwnerType::BehaviorFlowSettings, TEXT("TraceLength")) || Match(EMixtormatParameterOwnerType::BehaviorFlowSettings, TEXT("Depth"))))
				|| (Behavior.Type == EMixtormatBehaviorType::Warp && Match(EMixtormatParameterOwnerType::BehaviorFlowSettings, TEXT("ModeMix"))));
			if (bPublishedFlow && Target.ValueType == EMixtormatParameterValueType::Int && Match(EMixtormatParameterOwnerType::BehaviorFlow, TEXT("FlowSteps")))
			{ Result.Execution = EExecution::Integer; Result.IntMin = 1; Result.IntMax = 64; return Result; }
			if (Behavior.Type == EMixtormatBehaviorType::Feather && Behavior.Feather.Mode == EMixtormatBehaviorFeatherMode::Eikonal
				&& Target.ValueType == EMixtormatParameterValueType::Enum && Match(EMixtormatParameterOwnerType::BehaviorFeatherSettings, TEXT("Side")))
			{
				const FMixtormatLayerChild* Parent = MixtormatParameterBinding::FindChild(Scope, Target.LayerId, Child->ScopeOwnerChildId);
				if (Parent && (Parent->Type == EMixtormatLayerChildType::Generator
					|| (Parent->Type == EMixtormatLayerChildType::Behavior && (Parent->Behavior.Type == EMixtormatBehaviorType::Warp
						|| Parent->Behavior.Type == EMixtormatBehaviorType::Deform || Parent->Behavior.Type == EMixtormatBehaviorType::Push || Parent->Behavior.Type == EMixtormatBehaviorType::Carve))))
				{ Result.Execution = EExecution::EnumBins; return Result; }
			}
		}
		if (bFloat) { Result.Execution = EExecution::Float; return Result; }
		Result.Reason = Target.ValueType == EMixtormatParameterValueType::Int || Target.ValueType == EMixtormatParameterValueType::Enum
			? NSLOCTEXT("Mixtormat", "DriverStructuralSelector", "This integer/enum selects CPU topology, passes or permutations, or has no typed spatial reader in this configuration. Use a group control or ordinary value reference instead.")
			: NSLOCTEXT("Mixtormat", "DriverNoReader", "No per-pixel driver reader exists for this parameter. Scalar ramps are transfer curves, not spatially mutated stop arrays.");
		return Result;
	}

	bool SourceSupported(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target,
		FGuid SourceOwner, FGuid SourceChild, EMixtormatDriverSourceKind Kind, FText& Error)
	{
		if (Kind == EMixtormatDriverSourceKind::None) { return true; }
		const FCapability Support = Capability(Scope, Target);
		if (!Support.IsSupported()) { Error = Support.Reason; return false; }
		const auto& Layers = Scope.GetLayers();
		int32 TargetIndex = Layers.IndexOfByPredicate([&Target](const auto& Layer) { return Layer.LayerId == Target.LayerId; });
		if (TargetIndex == INDEX_NONE)
		{
			// A shared module executes in every member; the source must precede the first member.
			TargetIndex = Layers.IndexOfByPredicate([&Target](const auto& Layer) { return Layer.GroupId == Target.LayerId; });
		}
		const int32 SourceIndex = Layers.IndexOfByPredicate([SourceOwner](const auto& Layer) { return Layer.LayerId == SourceOwner; });
		if (Kind == EMixtormatDriverSourceKind::CombinedMask && !SourceChild.IsValid()
			&& SourceIndex != INDEX_NONE && TargetIndex != INDEX_NONE && Layers[SourceIndex].bEnabled
			&& (SourceIndex < TargetIndex || (!Target.ChildId.IsValid() && SourceIndex == TargetIndex))) { return true; }
		if (Kind == EMixtormatDriverSourceKind::RegionIds && Target.Owner == EMixtormatParameterOwnerType::Layer
			&& TargetIndex != INDEX_NONE && SourceChild.IsValid()
			&& (SourceOwner == Target.LayerId || SourceOwner == Layers[TargetIndex].GroupId)
			&& MixtormatParameterBinding::FindChild(Scope, SourceOwner, SourceChild)) { return true; }
		Error = NSLOCTEXT("Mixtormat", "DriverSourceUnsupported", "This reader accepts completed earlier-layer masks; layer scalars additionally accept their own mask/Region IDs (including their group's ID producers). Child-mask snapshots and forward sources are unavailable.");
		return false;
	}

	bool ValidateEnumBins(const UEnum* Enum, const TArray<FMixtormatDriverEnumBin>& Bins, FText& Error)
	{
		float Previous = 0.0f;
		bool bValid = Enum && !Bins.IsEmpty() && Bins.Num() <= FMixtormatParameterDriver::MaxEnumBins;
		for (const auto& Bin : Bins)
		{
			bValid &= FMath::IsFinite(Bin.UpperBound) && Bin.UpperBound > Previous && Bin.UpperBound <= 1.0f
				&& Enum && Enum->IsValidEnumValue(Bin.Value) && !Enum->GetNameStringByValue(Bin.Value).EndsWith(TEXT("_MAX"));
			Previous = Bin.UpperBound;
		}
		if (bValid && Previous == 1.0f) { return true; }
		Error = NSLOCTEXT("Mixtormat", "DriverInvalidBins", "Enum bins need increasing upper-inclusive thresholds ending at 1, with valid values of the exact destination enum. No ordinal arithmetic is performed.");
		return false;
	}
	bool ValidateTransfer(const FMixtormatParameterDriver& Driver, FText& Error)
	{
		if (!Driver.bUseTransferCurve) { return true; }
		FMixtormatParameterValue Value; Value.Type = EMixtormatParameterValueType::ScalarRamp; Value.ScalarRampValue = Driver.TransferCurve;
		return MixtormatGroupControls::ValidateValue(Value, Error);
	}
	float MapSignal(const FMixtormatParameterDriver& Driver, float Signal)
	{
		float S = FMath::Clamp(Signal, 0.0f, 1.0f);
		if (Driver.bInvert) { S = 1.0f - S; }
		const float Span = Driver.InputMax - Driver.InputMin;
		S = FMath::Abs(Span) > 1.0e-6f ? FMath::Clamp((S - Driver.InputMin) / Span, 0.0f, 1.0f) : 0.0f;
		return Driver.bUseTransferCurve ? MixtormatScalarRampMath::Evaluate(Driver.TransferCurve,
			FMath::Lerp(Driver.TransferCurve.DomainMin, Driver.TransferCurve.DomainMax, S)) : S;
	}
	int32 QuantizeInteger(double MappedValue, int32 Minimum, int32 Maximum)
	{
		// Nearest with ties toward +infinity, shared with HLSL floor(x + 0.5), then clamp.
		if (!FMath::IsFinite(MappedValue) || Minimum > Maximum) { return Minimum; }
					return static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(MappedValue + 0.5), static_cast<double>(Minimum), static_cast<double>(Maximum)));
	}
}
