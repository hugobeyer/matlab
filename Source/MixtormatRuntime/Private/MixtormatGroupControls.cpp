// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "MixtormatGroupControls.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterDefinition.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace MixtormatGroupControls
{
	namespace Private
	{
		bool Fail(FText& Error, const FText& Text) { Error = Text; return false; }
		const TArray<FMixtormatParameterBinding>* Bindings(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target)
		{
			if (Target.ChildId.IsValid())
			{
				const FMixtormatLayerChild* Child = MixtormatParameterBinding::FindChild(Scope, Target.LayerId, Target.ChildId);
				return Child ? &Child->ParameterBindings : nullptr;
			}
			for (const FMixtormatLayer& Layer : Scope.GetLayers()) { if (Layer.LayerId == Target.LayerId) { return &Layer.ParameterBindings; } }
			return nullptr;
		}
	}

	const UEnum* FindEnumType(const FName TypeName)
	{
		const UEnum* Found = nullptr;
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			if (It->GetFName() != TypeName) { continue; }
			if (Found) { return nullptr; } // The existing address contract uses an exact, unique enum FName.
			Found = *It;
		}
		return Found;
	}

	bool SameTarget(const FMixtormatParameterAddress& A, const FMixtormatParameterAddress& B)
	{
		return A.LayerId == B.LayerId && A.ChildId == B.ChildId && A.Owner == B.Owner
			&& A.Parameter == B.Parameter && A.ValueType == B.ValueType && A.TypeName == B.TypeName;
	}

	const FMixtormatLayerGroup* FindTargetGroup(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target)
	{
		if (!Scope.Groups) { return nullptr; }
		for (const FMixtormatLayer& Layer : Scope.GetLayers())
		{
			if (Layer.LayerId == Target.LayerId) { return MixtormatLayerGroups::FindGroup(*Scope.Groups, Layer.GroupId); }
		}
		return Target.ChildId.IsValid() ? MixtormatLayerGroups::FindGroup(*Scope.Groups, Target.LayerId) : nullptr;
	}

	bool ValidateValue(const FMixtormatParameterValue& Value, FText& Error)
	{
		if (Value.Type == EMixtormatParameterValueType::Float && FMath::IsFinite(Value.FloatValue)) { return true; }
		if (Value.Type == EMixtormatParameterValueType::Int) { return true; }
		if (Value.Type == EMixtormatParameterValueType::Enum)
				{
					const UEnum* Enum = FindEnumType(Value.TypeName);
					if (Enum && Enum->IsValidEnumValue(Value.EnumValue) && !Enum->GetNameStringByValue(Value.EnumValue).EndsWith(TEXT("_MAX"))) { return true; }
				}
		if (Value.Type == EMixtormatParameterValueType::ScalarRamp)
		{
			const FMixtormatScalarRamp& Ramp = Value.ScalarRampValue;
			FMixtormatScalarRamp Copy = Ramp;
			Copy.Sanitize();
			bool bValid = FMath::IsFinite(Ramp.DomainMin) && FMath::IsFinite(Ramp.DomainMax)
				&& Ramp.DomainMax > Ramp.DomainMin && FMath::IsFinite(Ramp.DomainMax - Ramp.DomainMin) && Copy.DomainMin == Ramp.DomainMin && Copy.DomainMax == Ramp.DomainMax
				&& Copy.Interpolation == Ramp.Interpolation && Copy.Points.Num() == Ramp.Points.Num();
			for (int32 Index = 0; bValid && Index < Ramp.Points.Num(); ++Index)
			{ bValid = Copy.Points[Index].X == Ramp.Points[Index].X && Copy.Points[Index].Y == Ramp.Points[Index].Y; }
			if (bValid) { return true; }
		}
		if (Value.Type == EMixtormatParameterValueType::ColorRamp)
		{
			const FMixtormatColorRamp& Ramp = Value.ColorRampValue;
			FMixtormatColorRamp Copy = Ramp;
			Copy.Sanitize();
			bool bValid = Copy.Stops.Num() == Ramp.Stops.Num() && Copy.Interpolation == Ramp.Interpolation;
			for (int32 Index = 0; bValid && Index < Ramp.Stops.Num(); ++Index)
			{
				const FLinearColor& Color = Ramp.Stops[Index].Color;
				bValid = FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A)
					&& Copy.Stops[Index].X == Ramp.Stops[Index].X && Copy.Stops[Index].Color == Color;
			}
			if (bValid) { return true; }
		}
		return Private::Fail(Error, NSLOCTEXT("Mixtormat", "InvalidGroupControlValue", "Unsupported type, nonfinite value or invalid ramp. No correction was applied."));
	}

	bool ValidateControl(const FMixtormatGroupControl& Control, FText& Error)
	{
		if (!Control.ControlId.IsValid() || Control.Value.Type != Control.DefaultValue.Type || Control.Value.TypeName != Control.DefaultValue.TypeName
			|| !FMath::IsFinite(Control.UiMin) || !FMath::IsFinite(Control.UiMax) || Control.UiMax < Control.UiMin
			|| !FMath::IsFinite(Control.Snap) || Control.Snap < 0.0f || !FMath::IsFinite(Control.ColorDomainMin)
			|| !FMath::IsFinite(Control.ColorDomainMax) || Control.ColorDomainMax <= Control.ColorDomainMin)
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupControlMetadata", "Invalid control identity, presentation domain or mismatched reset value type.")); }
		return ValidateValue(Control.Value, Error) && ValidateValue(Control.DefaultValue, Error);
	}

	bool ValidateTarget(const FMixtormatBindingScope& Scope, const FMixtormatLayerGroup& Group,
		const FMixtormatGroupControl& Control, const FMixtormatParameterAddress& Target, FText& Error)
	{
		const FMixtormatLayerGroup* Owner = FindTargetGroup(Scope, Target);
		if (!Owner || Owner->GroupId != Group.GroupId)
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetMembership", "Target is missing or is no longer inside this group.")); }
		if (Target.ChildId.IsValid())
		{
			const FMixtormatLayerChild* Child = MixtormatParameterBinding::FindChild(Scope, Target.LayerId, Target.ChildId);
			if (!Child || Child->IsInstance())
			{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetInstance", "Instances cannot be group-control destinations. Expose an editable source inside the group.")); }
		}
		if (Control.Value.Type != Target.ValueType || (Target.ValueType == EMixtormatParameterValueType::Enum && Control.Value.TypeName != Target.TypeName))
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetType", "Target value type or enum identity differs from the control.")); }
		if (Target.Owner == EMixtormatParameterOwnerType::Layer && Target.Parameter == GET_MEMBER_NAME_CHECKED(FMixtormatLayer, HeightReferenceLayerIndex))
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupReferenceIndex", "A document-relative height reference is an address, not a numeric control target.")); }
		const FProperty* Property = MixtormatParameterBinding::FindParameterProperty(Scope, Target);
		FMixtormatParameterValue Local;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || !MixtormatParameterBinding::TryReadLocalValue(Scope, Target, Local))
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetMissing", "The exact target property no longer exists with this type.")); }
		if (!ValidateValue(Control.Value, Error)) { return false; }
		if (const TArray<FMixtormatParameterBinding>* Set = Private::Bindings(Scope, Target))
		{
			if (Set->ContainsByPredicate([&Target](const FMixtormatParameterBinding& Binding)
				{ return Binding.DestinationOwner == Target.Owner && Binding.DestinationParameter == Target.Parameter
					&& (Binding.Reference.bEnabled || Binding.Driver.bEnabled); }))
			{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetBound", "Target already has a reference or spatial driver. Clear it explicitly before attaching a group control.")); }
		}
		// A Link writes to its source. Do not hide that authority underneath a macro override.
		const auto HasIncomingLink = [&Target](const TArray<FMixtormatParameterBinding>& Set)
		{
			return Set.ContainsByPredicate([&Target](const auto& Binding)
			{ return Binding.Reference.bEnabled && Binding.Reference.Mode == EMixtormatReferenceMode::Link && SameTarget(Binding.Reference.Source, Target); });
		};
		bool bLinkedAuthority = false;
		for (const auto& Layer : Scope.GetLayers())
		{
			bLinkedAuthority |= HasIncomingLink(Layer.ParameterBindings);
			for (const auto& Child : Layer.Children) { bLinkedAuthority |= HasIncomingLink(Child.ParameterBindings); }
		}
		if (Scope.Groups)
		{
			for (const auto& Item : *Scope.Groups)
			{
				bLinkedAuthority |= HasIncomingLink(Item.ParameterBindings);
				for (const auto& Child : Item.Children) { bLinkedAuthority |= HasIncomingLink(Child.ParameterBindings); }
			}
		}
		if (bLinkedAuthority) { return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupLinkedAuthority", "This parameter is the authority of an existing Link. Resolve that link explicitly before exposing it.")); }
		for (const FMixtormatGroupControl& Other : Group.Controls)
		{
			if (Other.ControlId != Control.ControlId && Other.Targets.ContainsByPredicate([&Target](const FMixtormatParameterAddress& Item) { return SameTarget(Item, Target); }))
			{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetConflict", "Another group control already owns this target. Detach it first.")); }
		}
		if (Target.ValueType == EMixtormatParameterValueType::Float || Target.ValueType == EMixtormatParameterValueType::Int)
		{
			const double Number = Target.ValueType == EMixtormatParameterValueType::Float
							? static_cast<double>(Control.Value.FloatValue) : static_cast<double>(Control.Value.IntValue);
			if (const FMixtormatParameterContract* Contract = MixtormatParameterContracts::TryGet(Target.Owner, Target.Parameter))
			{
				if ((Contract->HardMin.IsSet() && Number < Contract->HardMin.GetValue()) || (Contract->HardMax.IsSet() && Number > Contract->HardMax.GetValue()))
				{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupTargetRange", "The control value is outside this target's canonical hard bounds.")); }
			}
		}
		if (Target.ValueType == EMixtormatParameterValueType::Enum)
		{
			const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property);
			const FByteProperty* Byte = CastField<FByteProperty>(Property);
			const UEnum* Enum = EnumProperty ? EnumProperty->GetEnum() : (Byte ? Byte->Enum.Get() : nullptr);
			if (!Enum || !Enum->IsValidEnumValue(Control.Value.EnumValue) || Enum->GetNameStringByValue(Control.Value.EnumValue).EndsWith(TEXT("_MAX")))
			{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupEnumValue", "Choose a declared value of this exact enum, not an arbitrary ordinal.")); }
		}
		if (Target.ValueType == EMixtormatParameterValueType::ScalarRamp
			&& (Control.Value.ScalarRampValue.DomainMin != Local.ScalarRampValue.DomainMin || Control.Value.ScalarRampValue.DomainMax != Local.ScalarRampValue.DomainMax))
		{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupScalarDomain", "Scalar ramp domains differ. Create a control from the target; no implicit remap is performed.")); }
		if (Target.ValueType == EMixtormatParameterValueType::ColorRamp)
		{
			const float Min = Target.Owner == EMixtormatParameterOwnerType::HeightColorRamp ? -1.0f : 0.0f;
			if (Control.ColorDomainMin != Min || Control.ColorDomainMax != 1.0f)
			{ return Private::Fail(Error, NSLOCTEXT("Mixtormat", "GroupColorDomain", "Color ramp consumer domains differ. Create a separate control from that target.")); }
		}
		return true;
	}

	const FMixtormatGroupControl* FindDrivingControl(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target)
	{
		const FMixtormatLayerGroup* Group = FindTargetGroup(Scope, Target);
		if (!Group) { return nullptr; }
		const FMixtormatGroupControl* Found = nullptr;
		for (const FMixtormatGroupControl& Control : Group->Controls)
		{
			if (!Control.bEnabled || !Control.Targets.ContainsByPredicate([&Target](const FMixtormatParameterAddress& Item) { return SameTarget(Item, Target); })) { continue; }
			FText Error;
			if (!ValidateControl(Control, Error) || !ValidateTarget(Scope, *Group, Control, Target, Error)) { return nullptr; }
			if (Found) { return nullptr; }
			Found = &Control;
		}
		return Found;
	}

	void Apply(const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups,
		TArray<FMixtormatLayer>& OutLayers, TArray<FMixtormatLayerGroup>& OutGroups)
	{
		OutLayers = Layers;
		OutGroups = Groups;
		const FMixtormatBindingScope Source(Layers, Groups);
		for (const FMixtormatLayerGroup& Group : Groups)
		{
			for (const FMixtormatGroupControl& Control : Group.Controls)
			{
				if (!Control.bEnabled) { continue; }
				for (const FMixtormatParameterAddress& Target : Control.Targets)
				{
					if (FindDrivingControl(Source, Target) == &Control)
					{ MixtormatParameterBinding::TryWriteValue({OutLayers, OutGroups}, Target, Control.Value); }
				}
			}
		}
	}
}
