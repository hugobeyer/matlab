// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatParameterBinding.h"

#include "MixtormatLayerGroups.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FOwnerView
	{
		const void* ConstData = nullptr;
		void* MutableData = nullptr;
		UScriptStruct* Struct = nullptr;
		const TArray<FMixtormatParameterBinding>* Bindings = nullptr;
		TArray<FMixtormatParameterBinding>* MutableBindings = nullptr;
	};

	struct FResolvedValue
	{
		EMixtormatParameterValueType Type = EMixtormatParameterValueType::Float;
		FName TypeName;
		float FloatValue = 0.0f;
		int32 IntValue = 0;
		bool BoolValue = false;
		int64 EnumValue = 0;
	};

	FName EnumTypeName(const FProperty* Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetFName() : NAME_None;
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum ? ByteProperty->Enum->GetFName() : NAME_None;
		}
		return NAME_None;
	}

	bool PropertyMatchesAddress(const FProperty* Property, const FMixtormatParameterAddress& Address)
	{
		if (!Property)
		{
			return false;
		}
		switch (Address.ValueType)
		{
		case EMixtormatParameterValueType::Float:
			return CastField<FFloatProperty>(Property) != nullptr;
		case EMixtormatParameterValueType::Int:
			return CastField<FIntProperty>(Property) != nullptr;
		case EMixtormatParameterValueType::Bool:
			return CastField<FBoolProperty>(Property) != nullptr;
		case EMixtormatParameterValueType::Enum:
			return !Address.TypeName.IsNone() && EnumTypeName(Property) == Address.TypeName;
		default:
			return false;
		}
	}

	bool ChildTypeMatchesOwner(const FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner)
	{
		switch (Owner)
		{
		case EMixtormatParameterOwnerType::Mask:
		case EMixtormatParameterOwnerType::MaskNoise:
			return Child.Type == EMixtormatLayerChildType::Mask;
		case EMixtormatParameterOwnerType::Effect: return Child.Type == EMixtormatLayerChildType::Effect;
		case EMixtormatParameterOwnerType::Generated: return Child.Type == EMixtormatLayerChildType::Generated;
		case EMixtormatParameterOwnerType::Craquelure: return Child.Type == EMixtormatLayerChildType::Craquelure;
		case EMixtormatParameterOwnerType::ColorId: return Child.Type == EMixtormatLayerChildType::ColorId;
		case EMixtormatParameterOwnerType::ClusterId: return Child.Type == EMixtormatLayerChildType::Filter;
		case EMixtormatParameterOwnerType::HsvId: return Child.Type == EMixtormatLayerChildType::HsvFilter;
		case EMixtormatParameterOwnerType::RandomId: return Child.Type == EMixtormatLayerChildType::RandomId;
		case EMixtormatParameterOwnerType::PatternId: return Child.Type == EMixtormatLayerChildType::PatternId;
		case EMixtormatParameterOwnerType::RampId: return Child.Type == EMixtormatLayerChildType::RampId;
		case EMixtormatParameterOwnerType::UvId: return Child.Type == EMixtormatLayerChildType::UvFromIds;
		case EMixtormatParameterOwnerType::ReliefId: return Child.Type == EMixtormatLayerChildType::ReliefFromIds;
		case EMixtormatParameterOwnerType::BoundaryId: return Child.Type == EMixtormatLayerChildType::BoundaryFromIds;
		case EMixtormatParameterOwnerType::HeightBlend: return Child.Type == EMixtormatLayerChildType::HeightBlend;
		case EMixtormatParameterOwnerType::HeightCurve: return Child.Type == EMixtormatLayerChildType::HeightCurve;
		case EMixtormatParameterOwnerType::HeightColorRamp: return Child.Type == EMixtormatLayerChildType::HeightColorRamp;
				case EMixtormatParameterOwnerType::HeightPush: return Child.Type == EMixtormatLayerChildType::HeightPush;
						case EMixtormatParameterOwnerType::StructuralWarp: return Child.Type == EMixtormatLayerChildType::StructuralWarp;
		case EMixtormatParameterOwnerType::CombineId: return Child.Type == EMixtormatLayerChildType::CombineId;
		case EMixtormatParameterOwnerType::IdGroup: return Child.Type == EMixtormatLayerChildType::IdGroup;
		case EMixtormatParameterOwnerType::Blur: return Child.Type == EMixtormatLayerChildType::Blur;
		case EMixtormatParameterOwnerType::Curvature: return Child.Type == EMixtormatLayerChildType::Curvature;
		case EMixtormatParameterOwnerType::Generator: return Child.Type == EMixtormatLayerChildType::Generator;
		case EMixtormatParameterOwnerType::MaskShaping:
			return Child.Type == EMixtormatLayerChildType::Mask
				|| Child.Type == EMixtormatLayerChildType::Generated
				|| Child.Type == EMixtormatLayerChildType::Craquelure
				|| Child.Type == EMixtormatLayerChildType::ColorId
				|| Child.Type == EMixtormatLayerChildType::RandomId;
		default: return false;
		}
	}

	// Layer and child addresses expose only the selected flat payload, never the wrapper.
	FOwnerView GeneratorOwner(
		const FMixtormatGenerator& Generator,
		const TArray<FMixtormatParameterBinding>& Bindings)
	{
		FOwnerView View;
		View.Bindings = &Bindings;
		switch (Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver:
			View.ConstData = &Generator.StrataCarver;
			View.Struct = FMixtormatStrataCarver::StaticStruct();
			break;
		case EMixtormatGeneratorType::Cracks:
			View.ConstData = &Generator.Cracks;
			View.Struct = FMixtormatCracks::StaticStruct();
			break;
		case EMixtormatGeneratorType::RockFormation:
			View.ConstData = &Generator.RockFormation;
			View.Struct = FMixtormatRockFormation::StaticStruct();
			break;
		case EMixtormatGeneratorType::Pebbles:
			View.ConstData = &Generator.Pebbles;
			View.Struct = FMixtormatPebbles::StaticStruct();
			break;
		case EMixtormatGeneratorType::CliffStrata:
			View.ConstData = &Generator.CliffStrata;
			View.Struct = FMixtormatCliffStrata::StaticStruct();
			break;
		case EMixtormatGeneratorType::Noise:
			View.ConstData = &Generator.Noise;
			View.Struct = FMixtormatNoise::StaticStruct();
			break;
		default: break;
		}
		return View;
	}

	FOwnerView LayerOwner(const FMixtormatLayer& Layer, const EMixtormatParameterOwnerType Owner)
	{
		if (Owner != EMixtormatParameterOwnerType::Layer)
		{
			return {};
		}
		FOwnerView View;
		View.ConstData = &Layer;
		View.Struct = FMixtormatLayer::StaticStruct();
		View.Bindings = &Layer.ParameterBindings;
		return View;
	}

	FOwnerView MutableLayerOwner(FMixtormatLayer& Layer, const EMixtormatParameterOwnerType Owner)
	{
		FOwnerView View = LayerOwner(Layer, Owner);
		View.MutableData = const_cast<void*>(View.ConstData);
		if (View.ConstData)
		{
			View.MutableBindings = &Layer.ParameterBindings;
		}
		return View;
	}

	FOwnerView ChildOwner(const FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner)
	{
		if (!ChildTypeMatchesOwner(Child, Owner))
		{
			return {};
		}
		FOwnerView View;
		View.Bindings = &Child.ParameterBindings;
		switch (Owner)
		{
		case EMixtormatParameterOwnerType::Mask: View.ConstData = &Child.Mask; break;
		case EMixtormatParameterOwnerType::MaskNoise: View.ConstData = &Child.Mask.Noise; break;
		case EMixtormatParameterOwnerType::Effect: View.ConstData = &Child.Effect; break;
		case EMixtormatParameterOwnerType::Generated: View.ConstData = &Child.Generated; break;
		case EMixtormatParameterOwnerType::Craquelure: View.ConstData = &Child.Craquelure; break;
		case EMixtormatParameterOwnerType::ColorId: View.ConstData = &Child.ColorId; break;
		case EMixtormatParameterOwnerType::ClusterId: View.ConstData = &Child.Filter; break;
		case EMixtormatParameterOwnerType::HsvId: View.ConstData = &Child.HsvFilter; break;
		case EMixtormatParameterOwnerType::RandomId: View.ConstData = &Child.RandomId; break;
		case EMixtormatParameterOwnerType::PatternId: View.ConstData = &Child.PatternId; break;
		case EMixtormatParameterOwnerType::RampId: View.ConstData = &Child.RampId; break;
		case EMixtormatParameterOwnerType::UvId: View.ConstData = &Child.UvId; break;
		case EMixtormatParameterOwnerType::ReliefId: View.ConstData = &Child.ReliefId; break;
		case EMixtormatParameterOwnerType::BoundaryId: View.ConstData = &Child.BoundaryId; break;
		case EMixtormatParameterOwnerType::HeightBlend: View.ConstData = &Child.HeightBlend; break;
		case EMixtormatParameterOwnerType::HeightCurve: View.ConstData = &Child.HeightCurve; break;
		case EMixtormatParameterOwnerType::HeightColorRamp: View.ConstData = &Child.HeightColorRamp; break;
				case EMixtormatParameterOwnerType::HeightPush: View.ConstData = &Child.HeightPush; break;
						case EMixtormatParameterOwnerType::StructuralWarp: View.ConstData = &Child.StructuralWarp; break;
		case EMixtormatParameterOwnerType::CombineId: View.ConstData = &Child.CombineId; break;
		case EMixtormatParameterOwnerType::IdGroup: View.ConstData = &Child.IdGroup; break;
		case EMixtormatParameterOwnerType::Blur: View.ConstData = &Child.Blur; break;
		case EMixtormatParameterOwnerType::Curvature: View.ConstData = &Child.Curvature; break;
		case EMixtormatParameterOwnerType::Generator:
			return GeneratorOwner(Child.Generator, Child.ParameterBindings);
		case EMixtormatParameterOwnerType::MaskShaping:
			if (Child.Type == EMixtormatLayerChildType::Mask) View.ConstData = &Child.Mask.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::Generated) View.ConstData = &Child.Generated.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::Craquelure) View.ConstData = &Child.Craquelure.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::ColorId) View.ConstData = &Child.ColorId.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::RandomId) View.ConstData = &Child.RandomId.Shaping;
			break;
		default: break;
		}
		const TArray<UScriptStruct*> Structs = MixtormatParameterBinding::GetOwnerStructs(Owner);
		if (View.ConstData && !Structs.IsEmpty())
		{
			View.Struct = Structs[0];
		}
		return View;
	}

	FOwnerView MutableChildOwner(FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner)
	{
		FOwnerView View = ChildOwner(Child, Owner);
		View.MutableData = const_cast<void*>(View.ConstData);
		if (View.ConstData)
		{
			View.MutableBindings = &Child.ParameterBindings;
		}
		return View;
	}

	// A group's shared children, addressed the same way a layer's are.
	//
	// There is no Layer case: a group has no FMixtormatLayer to expose, so an address naming a
	// group as an owner in its own right resolves to nothing rather than to something wrong.
	FOwnerView LocateGroupOwner(
		const TArray<FMixtormatLayerGroup>* Groups,
		const FMixtormatParameterAddress& Address)
	{
		if (!Groups || Address.Owner == EMixtormatParameterOwnerType::Layer)
		{
			return {};
		}
		for (const FMixtormatLayerGroup& Group : *Groups)
		{
			if (Group.GroupId != Address.LayerId)
			{
				continue;
			}
			for (const FMixtormatLayerChild& Child : Group.Children)
			{
				if (Child.ChildId == Address.ChildId)
				{
					return ChildOwner(Child, Address.Owner);
				}
			}
			return {};
		}
		return {};
	}

	FOwnerView LocateOwner(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Address)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		for (const FMixtormatLayer& Layer : Layers)
		{
			if (Layer.LayerId != Address.LayerId)
			{
				continue;
			}
			if (Address.Owner == EMixtormatParameterOwnerType::Layer)
			{
				return LayerOwner(Layer, Address.Owner);
			}
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == Address.ChildId)
				{
					return ChildOwner(Child, Address.Owner);
				}
			}
			return {};
		}
		// No layer owns that id, so it may be a group's. Groups are checked second and never
		// shadow a layer: a GroupId colliding with a LayerId is not something PostLoad allows.
		return LocateGroupOwner(Scope.Groups, Address);
	}

	// The mutable twin of LocateOwner. ApplyDirectReferences only ever writes into the transient
	// layer it was handed, so it never needed one; a linked edit writes into the authored stack at
	// an arbitrary address and does.
	FOwnerView LocateMutableGroupOwner(
		TArray<FMixtormatLayerGroup>* Groups,
		const FMixtormatParameterAddress& Address)
	{
		if (!Groups || Address.Owner == EMixtormatParameterOwnerType::Layer)
		{
			return {};
		}
		for (FMixtormatLayerGroup& Group : *Groups)
		{
			if (Group.GroupId != Address.LayerId)
			{
				continue;
			}
			for (FMixtormatLayerChild& Child : Group.Children)
			{
				if (Child.ChildId == Address.ChildId)
				{
					return MutableChildOwner(Child, Address.Owner);
				}
			}
			return {};
		}
		return {};
	}

	FOwnerView LocateMutableOwnerByAddress(
		const FMixtormatMutableBindingScope& Scope,
		const FMixtormatParameterAddress& Address)
	{
		for (FMixtormatLayer& Layer : *Scope.Layers)
		{
			if (Layer.LayerId != Address.LayerId)
			{
				continue;
			}
			if (Address.Owner == EMixtormatParameterOwnerType::Layer)
			{
				return MutableLayerOwner(Layer, Address.Owner);
			}
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == Address.ChildId)
				{
					return MutableChildOwner(Child, Address.Owner);
				}
			}
			return {};
		}
		return LocateMutableGroupOwner(Scope.Groups, Address);
	}

	FOwnerView LocateMutableOwner(FMixtormatLayer& Layer, const FMixtormatParameterBinding& Binding, FMixtormatLayerChild* Child)
	{
		if (Binding.DestinationOwner == EMixtormatParameterOwnerType::Layer || !Child)
		{
			return MutableLayerOwner(Layer, Binding.DestinationOwner);
		}
		return MutableChildOwner(*Child, Binding.DestinationOwner);
	}

	const FMixtormatParameterBinding* FindReferenceBinding(
		const FOwnerView& Owner,
		const FMixtormatParameterAddress& Address)
	{
		if (!Owner.Bindings)
		{
			return nullptr;
		}
		return Owner.Bindings->FindByPredicate([&Address](const FMixtormatParameterBinding& Binding)
		{
			return Binding.DestinationOwner == Address.Owner
				&& Binding.DestinationParameter == Address.Parameter
				&& Binding.ValueType == Address.ValueType
				&& Binding.TypeName == Address.TypeName
				&& Binding.Reference.bEnabled;
		});
	}

	FString AddressKey(const FMixtormatParameterAddress& Address)
	{
		return FString::Printf(
			TEXT("%s|%s|%d|%s|%d|%s"),
			*Address.LayerId.ToString(EGuidFormats::Digits),
			*Address.ChildId.ToString(EGuidFormats::Digits),
			static_cast<int32>(Address.Owner),
			*Address.Parameter.ToString(),
			static_cast<int32>(Address.ValueType),
			*Address.TypeName.ToString());
	}

	bool ReadLocalValue(const FOwnerView& Owner, const FMixtormatParameterAddress& Address, FResolvedValue& OutValue)
	{
		if (!Owner.ConstData || !Owner.Struct)
		{
			return false;
		}
		const FProperty* Property = Owner.Struct->FindPropertyByName(Address.Parameter);
		if (!PropertyMatchesAddress(Property, Address))
		{
			return false;
		}

		OutValue.Type = Address.ValueType;
		OutValue.TypeName = Address.TypeName;
		switch (Address.ValueType)
		{
		case EMixtormatParameterValueType::Float:
			OutValue.FloatValue = CastFieldChecked<FFloatProperty>(Property)->GetPropertyValue_InContainer(Owner.ConstData);
			return true;
		case EMixtormatParameterValueType::Int:
			OutValue.IntValue = CastFieldChecked<FIntProperty>(Property)->GetPropertyValue_InContainer(Owner.ConstData);
			return true;
		case EMixtormatParameterValueType::Bool:
			OutValue.BoolValue = CastFieldChecked<FBoolProperty>(Property)->GetPropertyValue_InContainer(Owner.ConstData);
			return true;
		case EMixtormatParameterValueType::Enum:
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				OutValue.EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(
					EnumProperty->ContainerPtrToValuePtr<void>(Owner.ConstData));
				return true;
			}
			if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				OutValue.EnumValue = ByteProperty->GetPropertyValue_InContainer(Owner.ConstData);
				return true;
			}
			return false;
		default:
			return false;
		}
	}

	bool ResolveValue(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		TSet<FString>& Visiting,
		FResolvedValue& OutValue)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (!Address.IsValid())
		{
			return false;
		}
		const FString Key = AddressKey(Address);
		if (Visiting.Contains(Key))
		{
			return false;
		}
		Visiting.Add(Key);

		const FOwnerView Owner = LocateOwner(Scope, Address);
		if (!Owner.ConstData || !Owner.Struct)
		{
			Visiting.Remove(Key);
			return false;
		}
		if (const FMixtormatParameterBinding* Binding = FindReferenceBinding(Owner, Address))
		{
			const FMixtormatParameterAddress& Source = Binding->Reference.Source;
			if (MixtormatParameterBinding::AreReferenceTypesCompatible(Address, Source)
				&& ResolveValue(Scope, Source, Visiting, OutValue))
			{
				Visiting.Remove(Key);
				return true;
			}
		}

		const bool bRead = ReadLocalValue(Owner, Address, OutValue);
		Visiting.Remove(Key);
		return bRead;
	}

	bool WriteResolvedValue(const FOwnerView& Owner, const FMixtormatParameterBinding& Binding, const FResolvedValue& Value)
	{
		if (!Owner.MutableData || !Owner.Struct)
		{
			return false;
		}
		FMixtormatParameterAddress Destination;
		Destination.Owner = Binding.DestinationOwner;
		Destination.Parameter = Binding.DestinationParameter;
		Destination.ValueType = Binding.ValueType;
		Destination.TypeName = Binding.TypeName;
		FProperty* Property = Owner.Struct->FindPropertyByName(Binding.DestinationParameter);
		if (!PropertyMatchesAddress(Property, Destination)
			|| Value.Type != Binding.ValueType
			|| (Value.Type == EMixtormatParameterValueType::Enum && Value.TypeName != Binding.TypeName))
		{
			return false;
		}
		switch (Binding.ValueType)
		{
		case EMixtormatParameterValueType::Float:
			CastFieldChecked<FFloatProperty>(Property)->SetPropertyValue_InContainer(Owner.MutableData, Value.FloatValue);
			return true;
		case EMixtormatParameterValueType::Int:
			CastFieldChecked<FIntProperty>(Property)->SetPropertyValue_InContainer(Owner.MutableData, Value.IntValue);
			return true;
		case EMixtormatParameterValueType::Bool:
			CastFieldChecked<FBoolProperty>(Property)->SetPropertyValue_InContainer(Owner.MutableData, Value.BoolValue);
			return true;
		case EMixtormatParameterValueType::Enum:
			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
					EnumProperty->ContainerPtrToValuePtr<void>(Owner.MutableData),
					Value.EnumValue);
				return true;
			}
			if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				ByteProperty->SetPropertyValue_InContainer(Owner.MutableData, static_cast<uint8>(Value.EnumValue));
				return true;
			}
			return false;
		default:
			return false;
		}
	}

	void ApplyBindingSet(
		const FMixtormatBindingScope& Scope,
		FMixtormatLayer& InOutLayer,
		FMixtormatLayerChild* Child,
		const TArray<FMixtormatParameterBinding>& Bindings)
	{
		for (const FMixtormatParameterBinding& Binding : Bindings)
		{
			if (!Binding.Reference.bEnabled || !Binding.Reference.Source.IsValid())
			{
				continue;
			}
			FOwnerView Destination = LocateMutableOwner(InOutLayer, Binding, Child);
			if (!Destination.MutableData || !Destination.Struct)
			{
				continue;
			}
			FResolvedValue Value;
			TSet<FString> Visiting;
			if (ResolveValue(Scope, Binding.Reference.Source, Visiting, Value))
			{
				WriteResolvedValue(Destination, Binding, Value);
			}
		}
	}
}

namespace MixtormatParameterBinding
{
	TArray<UScriptStruct*> GetOwnerStructs(const EMixtormatParameterOwnerType Owner)
	{
		switch (Owner)
		{
		case EMixtormatParameterOwnerType::Layer: return { FMixtormatLayer::StaticStruct() };
		case EMixtormatParameterOwnerType::Mask: return { FMixtormatMaskLayer::StaticStruct() };
		case EMixtormatParameterOwnerType::MaskNoise: return { FMixtormatNoise::StaticStruct() };
		case EMixtormatParameterOwnerType::Effect: return { FMixtormatLayerEffect::StaticStruct() };
		case EMixtormatParameterOwnerType::Generated: return { FMixtormatGeneratedMask::StaticStruct() };
		case EMixtormatParameterOwnerType::Craquelure: return { FMixtormatCraquelure::StaticStruct() };
		case EMixtormatParameterOwnerType::ColorId: return { FMixtormatColorIdMask::StaticStruct() };
		case EMixtormatParameterOwnerType::ClusterId: return { FMixtormatClusterFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::HsvId: return { FMixtormatHsvIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::RandomId: return { FMixtormatRandomIdMask::StaticStruct() };
		case EMixtormatParameterOwnerType::PatternId: return { FMixtormatPatternFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::RampId: return { FMixtormatRampIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::UvId: return { FMixtormatUvIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::ReliefId: return { FMixtormatReliefIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::BoundaryId: return { FMixtormatBoundaryIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::HeightBlend: return { FMixtormatGeneratorHeightBlend::StaticStruct() };
		case EMixtormatParameterOwnerType::HeightCurve: return { FMixtormatGeneratorHeightCurve::StaticStruct() };
		case EMixtormatParameterOwnerType::HeightColorRamp: return { FMixtormatGeneratorHeightColorRamp::StaticStruct() };
				case EMixtormatParameterOwnerType::HeightPush: return { FMixtormatGeneratorHeightPush::StaticStruct() };
						case EMixtormatParameterOwnerType::StructuralWarp: return { FMixtormatGeneratorStructuralWarp::StaticStruct() };
		case EMixtormatParameterOwnerType::CombineId: return { FMixtormatCombineIdFilter::StaticStruct() };
		case EMixtormatParameterOwnerType::IdGroup: return { FMixtormatIdGroup::StaticStruct() };
		case EMixtormatParameterOwnerType::Blur: return { FMixtormatMaskBlur::StaticStruct() };
		case EMixtormatParameterOwnerType::Curvature: return { FMixtormatMaskCurvature::StaticStruct() };
		case EMixtormatParameterOwnerType::MaskShaping: return { FMixtormatMaskShaping::StaticStruct() };
		case EMixtormatParameterOwnerType::Generator:
			return { FMixtormatStrataCarver::StaticStruct(), FMixtormatCracks::StaticStruct(),
				FMixtormatRockFormation::StaticStruct(), FMixtormatPebbles::StaticStruct(),
				FMixtormatCliffStrata::StaticStruct(), FMixtormatNoise::StaticStruct() };
		default: return {};
		}
	}

	const void* GetChildOwnerData(
		const FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner,
		const UScriptStruct*& OutStruct)
	{
		const FOwnerView View = ChildOwner(Child, Owner);
		OutStruct = View.Struct;
		return View.ConstData;
	}

	void* GetMutableChildOwnerData(
		FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner,
		const UScriptStruct*& OutStruct)
	{
		const FOwnerView View = MutableChildOwner(Child, Owner);
		OutStruct = View.Struct;
		return View.MutableData;
	}

	void EnsureStableIds(TArray<FMixtormatLayer>& Layers)
	{
		TArray<FMixtormatLayerGroup> NoGroups;
		EnsureStableIds(Layers, NoGroups);
	}

	void EnsureStableIds(TArray<FMixtormatLayer>& Layers, TArray<FMixtormatLayerGroup>& Groups)
	{
		// Owner and child IDs are document-wide namespaces. Keep the first valid occurrence stable;
		// an ambiguous duplicate cannot safely retarget existing references, so later owners receive
		// fresh identities while references retain their established first-owner meaning.
		TSet<FGuid> OwnerIds;
		TSet<FGuid> ChildIds;
		for (FMixtormatLayer& Layer : Layers)
		{
			if (!Layer.LayerId.IsValid() || OwnerIds.Contains(Layer.LayerId))
			{
				Layer.LayerId = FGuid::NewGuid();
			}
			OwnerIds.Add(Layer.LayerId);
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId))
				{
					Child.ChildId = FGuid::NewGuid();
				}
				ChildIds.Add(Child.ChildId);
			}
		}
		for (FMixtormatLayerGroup& Group : Groups)
		{
			if (!Group.GroupId.IsValid() || OwnerIds.Contains(Group.GroupId))
			{
				Group.GroupId = FGuid::NewGuid();
			}
			OwnerIds.Add(Group.GroupId);
			for (FMixtormatLayerChild& Child : Group.Children)
			{
				if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId))
				{
					Child.ChildId = FGuid::NewGuid();
				}
				ChildIds.Add(Child.ChildId);
			}
		}
	}

	void EnsureStableIds(
		TArray<FMixtormatLayer>& Layers,
		TArray<FMixtormatLayerGroup>& Groups,
		TArray<FMixtormatSourceEntry>& Sources)
	{
		// Repair layer/group identities first; their established IDs keep precedence over shelf IDs.
		EnsureStableIds(Layers, Groups);

		TSet<FGuid> OwnerIds;
		TSet<FGuid> ChildIds;
		for (const FMixtormatLayer& Layer : Layers)
		{
			OwnerIds.Add(Layer.LayerId);
			for (const FMixtormatLayerChild& Child : Layer.Children) { ChildIds.Add(Child.ChildId); }
		}
		for (const FMixtormatLayerGroup& Group : Groups)
		{
			OwnerIds.Add(Group.GroupId);
			for (const FMixtormatLayerChild& Child : Group.Children) { ChildIds.Add(Child.ChildId); }
		}

		for (FMixtormatSourceEntry& Source : Sources)
		{
			if (!Source.SourceId.IsValid() || OwnerIds.Contains(Source.SourceId))
			{
				Source.SourceId = FGuid::NewGuid();
			}
			OwnerIds.Add(Source.SourceId);

			FMixtormatLayerChild& Child = Source.Child;
			if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId))
			{
				Child.ChildId = FGuid::NewGuid();
			}
			ChildIds.Add(Child.ChildId);

			for (FMixtormatLayerChild& OwnedChild : Source.OwnedChildren)
			{
				if (!OwnedChild.ChildId.IsValid() || ChildIds.Contains(OwnedChild.ChildId))
				{
					OwnedChild.ChildId = FGuid::NewGuid();
				}
				ChildIds.Add(OwnedChild.ChildId);
			}
		}
	}

	void RegenerateLayerIdentity(FMixtormatLayer& Layer, const bool bRegenerateChildren)
	{
		const FGuid OldLayerId = Layer.LayerId;
		const FGuid NewLayerId = FGuid::NewGuid();
		TSet<FGuid> OriginalChildIds;
		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			OriginalChildIds.Add(Child.ChildId);
		}
		TMap<FGuid, FGuid> ChildIdRemap;
		if (bRegenerateChildren)
		{
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				const FGuid OldChildId = Child.ChildId;
				const FGuid NewChildId = FGuid::NewGuid();
				if (OldChildId.IsValid())
				{
					ChildIdRemap.Add(OldChildId, NewChildId);
				}
				Child.ChildId = NewChildId;
			}
		}
		Layer.LayerId = NewLayerId;

		auto RemapBinding = [&](FMixtormatParameterBinding& Binding)
		{
			if (Binding.Reference.Source.LayerId == OldLayerId)
			{
				Binding.Reference.Source.LayerId = NewLayerId;
				if (const FGuid* NewChildId = ChildIdRemap.Find(Binding.Reference.Source.ChildId))
				{
					Binding.Reference.Source.ChildId = *NewChildId;
				}
			}
			if (Binding.Driver.SourceLayerId == OldLayerId)
			{
				Binding.Driver.SourceLayerId = NewLayerId;
				if (const FGuid* NewChildId = ChildIdRemap.Find(Binding.Driver.SourceChildId))
				{
					Binding.Driver.SourceChildId = *NewChildId;
				}
			}
		};
		for (FMixtormatParameterBinding& Binding : Layer.ParameterBindings)
		{
			RemapBinding(Binding);
		}
		for (FMixtormatLayerChild& Child : Layer.Children)
		{
			if (const FGuid* NewOwnerChildId = ChildIdRemap.Find(Child.ScopeOwnerChildId))
			{
				Child.ScopeOwnerChildId = *NewOwnerChildId;
			}
			for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
			{
				RemapBinding(Binding);
			}
			// An instance of a sibling has to follow the copy, exactly as a reference to one does.
			// Left alone, a duplicated layer's instances would go on reading the original layer's
			// children and the copy would not be independent of it.
			if (Child.SourceLayerId == OldLayerId)
			{
				Child.SourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.SourceChildId))
				{
					Child.SourceChildId = *NewSourceChildId;
				}
			}
			if (Child.BoundaryId.RegionIdsSource.SourceLayerId == OldLayerId)
			{
				Child.BoundaryId.RegionIdsSource.SourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.BoundaryId.RegionIdsSource.SourceChildId))
				{
					Child.BoundaryId.RegionIdsSource.SourceChildId = *NewSourceChildId;
				}
			}
			if (Child.HeightPush.Source.SourceLayerId == OldLayerId
				&& OriginalChildIds.Contains(Child.HeightPush.Source.SourceChildId))
			{
				Child.HeightPush.Source.SourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.HeightPush.Source.SourceChildId))
				{
					Child.HeightPush.Source.SourceChildId = *NewSourceChildId;
				}
			}
			if (const FGuid* NewTargetChildId = ChildIdRemap.Find(Child.HeightPush.TargetChildId))
			{
				Child.HeightPush.TargetChildId = *NewTargetChildId;
			}
			if (Child.StructuralWarp.Source.SourceLayerId == OldLayerId
				&& OriginalChildIds.Contains(Child.StructuralWarp.Source.SourceChildId))
			{
				Child.StructuralWarp.Source.SourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.StructuralWarp.Source.SourceChildId))
				{
					Child.StructuralWarp.Source.SourceChildId = *NewSourceChildId;
				}
			}
			if (const FGuid* NewTargetChildId = ChildIdRemap.Find(Child.StructuralWarp.TargetChildId))
			{
				Child.StructuralWarp.TargetChildId = *NewTargetChildId;
			}
			if (Child.OutputReference.SourceLayerId == OldLayerId)
			{
				Child.OutputReference.SourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.OutputReference.SourceChildId))
				{
					Child.OutputReference.SourceChildId = *NewSourceChildId;
				}
			}
			if (Child.Mask.PublishedSourceLayerId == OldLayerId)
			{
				Child.Mask.PublishedSourceLayerId = NewLayerId;
				if (const FGuid* NewSourceChildId = ChildIdRemap.Find(Child.Mask.PublishedSourceChildId))
				{
					Child.Mask.PublishedSourceChildId = *NewSourceChildId;
				}
			}
		}
	}

	void RegenerateLayerIdentities(TArray<FMixtormatLayer>& Layers)
	{
		TArray<FMixtormatLayerGroup> NoGroups;
		RegenerateLayerIdentities(Layers, NoGroups);
	}

	void RegenerateLayerIdentities(TArray<FMixtormatLayer>& Layers, TArray<FMixtormatLayerGroup>& Groups)
	{
		EnsureStableIds(Layers, Groups);

		TMap<FGuid, FGuid> OwnerIdRemap;
		TMap<FGuid, FGuid> ChildIdRemap;
		TMap<FGuid, TSet<FGuid>> OriginalOwnerChildIds;
		const auto AddOwner = [&OwnerIdRemap](const FGuid& Id) { OwnerIdRemap.Add(Id, FGuid::NewGuid()); };
		const auto AddChildren = [&ChildIdRemap, &OriginalOwnerChildIds](const FGuid& OwnerId,
			TArray<FMixtormatLayerChild>& Children)
		{
			for (FMixtormatLayerChild& Child : Children)
			{
				ChildIdRemap.Add(Child.ChildId, FGuid::NewGuid());
				OriginalOwnerChildIds.FindOrAdd(OwnerId).Add(Child.ChildId);
			}
		};
		for (FMixtormatLayer& Layer : Layers)
		{
			AddOwner(Layer.LayerId);
			AddChildren(Layer.LayerId, Layer.Children);
		}
		for (FMixtormatLayerGroup& Group : Groups)
		{
			AddOwner(Group.GroupId);
			AddChildren(Group.GroupId, Group.Children);
		}

		const auto RemapGuid = [](FGuid& Id, const TMap<FGuid, FGuid>& Remap)
		{
			if (const FGuid* Replacement = Remap.Find(Id))
			{
				Id = *Replacement;
			}
		};
		const auto RemapBinding = [&RemapGuid, &OwnerIdRemap, &ChildIdRemap](FMixtormatParameterBinding& Binding)
		{
			RemapGuid(Binding.Reference.Source.LayerId, OwnerIdRemap);
			RemapGuid(Binding.Reference.Source.ChildId, ChildIdRemap);
			RemapGuid(Binding.Driver.SourceLayerId, OwnerIdRemap);
			RemapGuid(Binding.Driver.SourceChildId, ChildIdRemap);
		};
		const auto RemapChildren = [&RemapGuid, &OwnerIdRemap, &ChildIdRemap, &RemapBinding,
			&OriginalOwnerChildIds](const FGuid& OldOwnerId, TArray<FMixtormatLayerChild>& Children)
		{
			for (FMixtormatLayerChild& Child : Children)
			{
				RemapGuid(Child.ChildId, ChildIdRemap);
				RemapGuid(Child.ScopeOwnerChildId, ChildIdRemap);
				RemapGuid(Child.SourceLayerId, OwnerIdRemap);
				RemapGuid(Child.SourceChildId, ChildIdRemap);
				RemapGuid(Child.Mask.PublishedSourceLayerId, OwnerIdRemap);
				RemapGuid(Child.Mask.PublishedSourceChildId, ChildIdRemap);
				RemapGuid(Child.OutputReference.SourceLayerId, OwnerIdRemap);
				RemapGuid(Child.OutputReference.SourceChildId, ChildIdRemap);
				RemapGuid(Child.BoundaryId.RegionIdsSource.SourceLayerId, OwnerIdRemap);
				RemapGuid(Child.BoundaryId.RegionIdsSource.SourceChildId, ChildIdRemap);
				// Structural sources are owner/child pairs: a coincident child GUID in another
				// owner must not make an external or dangling address follow this copied set.
				const auto RemapStructuralSource = [&](FMixtormatOutputReference& Source)
				{
					const TSet<FGuid>* SourceChildren = OriginalOwnerChildIds.Find(Source.SourceLayerId);
					if (SourceChildren && SourceChildren->Contains(Source.SourceChildId))
					{
						RemapGuid(Source.SourceLayerId, OwnerIdRemap);
						RemapGuid(Source.SourceChildId, ChildIdRemap);
					}
				};
				RemapStructuralSource(Child.HeightPush.Source);
				RemapStructuralSource(Child.StructuralWarp.Source);
				const TSet<FGuid>* OwnerChildren = OriginalOwnerChildIds.Find(OldOwnerId);
				if (OwnerChildren)
				{
					if (OwnerChildren->Contains(Child.HeightPush.TargetChildId))
					{
						RemapGuid(Child.HeightPush.TargetChildId, ChildIdRemap);
					}
					if (OwnerChildren->Contains(Child.StructuralWarp.TargetChildId))
					{
						RemapGuid(Child.StructuralWarp.TargetChildId, ChildIdRemap);
					}
				}
				for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
				{
					RemapBinding(Binding);
				}
			}
		};

		for (FMixtormatLayer& Layer : Layers)
		{
			const FGuid OldOwnerId = Layer.LayerId;
			RemapGuid(Layer.LayerId, OwnerIdRemap);
			RemapGuid(Layer.GroupId, OwnerIdRemap);
			for (FMixtormatParameterBinding& Binding : Layer.ParameterBindings)
			{
				RemapBinding(Binding);
			}
			RemapChildren(OldOwnerId, Layer.Children);
		}
		for (FMixtormatLayerGroup& Group : Groups)
		{
			const FGuid OldOwnerId = Group.GroupId;
			RemapGuid(Group.GroupId, OwnerIdRemap);
			RemapChildren(OldOwnerId, Group.Children);
		}
	}

	void RegenerateChildIdentity(FMixtormatLayerChild& Child)
	{
		const FGuid OldChildId = Child.ChildId;
		const FGuid NewChildId = FGuid::NewGuid();
		Child.ChildId = NewChildId;
		for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
		{
			if (Binding.Reference.Source.ChildId == OldChildId)
			{
				Binding.Reference.Source.ChildId = NewChildId;
			}
			if (Binding.Driver.SourceChildId == OldChildId)
			{
				Binding.Driver.SourceChildId = NewChildId;
			}
		}
	}

	bool AreReferenceTypesCompatible(
		const FMixtormatParameterAddress& Destination,
		const FMixtormatParameterAddress& Source)
	{
		if (Destination.ValueType != Source.ValueType)
		{
			return false;
		}
		return Destination.ValueType != EMixtormatParameterValueType::Enum
			|| (!Destination.TypeName.IsNone() && Destination.TypeName == Source.TypeName);
	}

	bool IsReferenceSourceValid(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Source)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		const FOwnerView Owner = LocateOwner(Scope, Source);
		return Owner.ConstData
			&& Owner.Struct
			&& PropertyMatchesAddress(Owner.Struct->FindPropertyByName(Source.Parameter), Source);
	}

	void CopyChildPayload(const FMixtormatLayerChild& From, FMixtormatLayerChild& To)
	{
		// Whole-struct assignment, then identity put back. Field-by-field copying is what would
		// have to be revisited every time the child grows one.
		const FGuid KeptChildId = To.ChildId;
		const FGuid KeptSourceLayerId = To.SourceLayerId;
		const FGuid KeptSourceChildId = To.SourceChildId;
		const FGuid KeptScopeOwnerChildId = To.ScopeOwnerChildId;
		To = From;
		To.ChildId = KeptChildId;
		To.SourceLayerId = KeptSourceLayerId;
		To.SourceChildId = KeptSourceChildId;
		// Scope belongs to this placement, not to the source instance's placement.
		To.ScopeOwnerChildId = KeptScopeOwnerChildId;
	}

	const FMixtormatLayerChild* FindChild(
		const FMixtormatBindingScope& Scope,
		const FGuid& LayerId,
		const FGuid& ChildId)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (!LayerId.IsValid() || !ChildId.IsValid())
		{
			return nullptr;
		}
		for (const FMixtormatLayer& Layer : Layers)
		{
			if (Layer.LayerId != LayerId)
			{
				continue;
			}
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == ChildId)
				{
					return &Child;
				}
			}
			return nullptr;
		}
		// Not a layer id -- may be a group's. A group's shared children are addressed by GroupId
		// in the same slot a layer's are addressed by LayerId (see FMixtormatBindingScope), the
		// same convention LocateGroupOwner uses for a direct parameter reference.
		if (!Scope.Groups)
		{
			return nullptr;
		}
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(*Scope.Groups, LayerId);
		if (!Group)
		{
			return nullptr;
		}
		for (const FMixtormatLayerChild& Child : Group->Children)
		{
			if (Child.ChildId == ChildId)
			{
				return &Child;
			}
		}
		return nullptr;
	}

	void ResolveChildInstances(
		const FMixtormatBindingScope& Scope,
		FMixtormatLayer& InOutLayer)
	{
		const TArray<FMixtormatLayer>& SourceLayers = Scope.GetLayers();
		for (FMixtormatLayerChild& Child : InOutLayer.Children)
		{
			if (!Child.IsInstance())
			{
				continue;
			}
			// An instance may name another instance. Walk to the first child that is not one and
			// take the payload from there, so a chain does not depend on the order layers happen
			// to be visited in. Seeded with this child's own id, which is what makes an instance
			// of itself fail on the first step instead of copying its own empty payload back.
			TSet<FGuid> Visited;
			Visited.Add(Child.ChildId);
			const FMixtormatLayerChild* Source =
				FindChild(Scope, Child.SourceLayerId, Child.SourceChildId);
			while (Source && Source->IsInstance())
			{
				if (Visited.Contains(Source->ChildId))
				{
					Source = nullptr;
					break;
				}
				Visited.Add(Source->ChildId);
				Source = FindChild(Scope, Source->SourceLayerId, Source->SourceChildId);
			}
			if (Source && Source->ChildId != Child.ChildId)
			{
				// A mask instance shares its source and shaping, but its placement in the mask
				// accumulator is local. Blend Mode and Invert are the two intentional overrides:
				// the same mask can add here, subtract there, or select the opposite region without
				// breaking the instance. Every other field still follows the source.
				const bool bKeepMaskOverrides = Child.Type == EMixtormatLayerChildType::Mask;
				const EMixtormatMaskBlendMode LocalBlendMode = Child.Mask.BlendMode;
				const bool bLocalInvert = Child.Mask.Shaping.bInvert;

				CopyChildPayload(*Source, Child);
				if (bKeepMaskOverrides && Child.Type == EMixtormatLayerChildType::Mask)
				{
					Child.Mask.BlendMode = LocalBlendMode;
					Child.Mask.Shaping.bInvert = bLocalInvert;
					Child.ParameterBindings.RemoveAll([](const FMixtormatParameterBinding& Binding)
					{
						return (Binding.DestinationOwner == EMixtormatParameterOwnerType::Mask
								&& Binding.DestinationParameter == GET_MEMBER_NAME_CHECKED(
									FMixtormatMaskLayer, BlendMode))
							|| (Binding.DestinationOwner == EMixtormatParameterOwnerType::MaskShaping
								&& Binding.DestinationParameter == GET_MEMBER_NAME_CHECKED(
									FMixtormatMaskShaping, bInvert));
					});
				}
			}
		}
	}

	EInstancePlacement ClassifyInstancePlacement(
		const FMixtormatBindingScope& Scope,
		const FGuid& SourceLayerId,
		const FGuid& SourceChildId,
		const FGuid& DestLayerId,
		const int32 DestChildIndex)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		int32 SourceLayerIndex = INDEX_NONE;
		int32 SourceChildIndex = INDEX_NONE;
		int32 DestLayerIndex = INDEX_NONE;
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			if (Layers[LayerIndex].LayerId == DestLayerId)
			{
				DestLayerIndex = LayerIndex;
			}
			if (Layers[LayerIndex].LayerId == SourceLayerId)
			{
				SourceLayerIndex = LayerIndex;
				SourceChildIndex = Layers[LayerIndex].Children.IndexOfByPredicate(
					[&SourceChildId](const FMixtormatLayerChild& Child)
					{
						return Child.ChildId == SourceChildId;
					});
			}
		}

		// Neither id named a layer -- try a group. A group has no single position in composite
		// order: its authored children are appended to the tail of every member layer's own list
		// (see MixtormatLayerGroups::BuildEffectiveLayers), so its effective range spans its first
		// to its last member layer index.
		const FMixtormatLayerGroup* SourceGroup = nullptr;
		int32 SourceGroupFirst = INDEX_NONE, SourceGroupLast = INDEX_NONE;
		if (SourceLayerIndex == INDEX_NONE && Scope.Groups)
		{
			SourceGroup = MixtormatLayerGroups::FindGroup(*Scope.Groups, SourceLayerId);
			if (SourceGroup)
			{
				SourceChildIndex = SourceGroup->Children.IndexOfByPredicate(
					[&SourceChildId](const FMixtormatLayerChild& Child)
					{
						return Child.ChildId == SourceChildId;
					});
				MixtormatLayerGroups::GetGroupRange(Layers, SourceLayerId, SourceGroupFirst, SourceGroupLast);
			}
		}
		const bool bSourceIsGroup = SourceGroup != nullptr;

		const FMixtormatLayerGroup* DestGroup = nullptr;
		int32 DestGroupFirst = INDEX_NONE, DestGroupLast = INDEX_NONE;
		if (DestLayerIndex == INDEX_NONE && Scope.Groups)
		{
			DestGroup = MixtormatLayerGroups::FindGroup(*Scope.Groups, DestLayerId);
			if (DestGroup)
			{
				MixtormatLayerGroups::GetGroupRange(Layers, DestLayerId, DestGroupFirst, DestGroupLast);
			}
		}
		const bool bDestIsGroup = DestGroup != nullptr;

		const bool bSourceFound = bSourceIsGroup
			? (SourceGroupFirst != INDEX_NONE && SourceChildIndex != INDEX_NONE)
			: (SourceLayerIndex != INDEX_NONE && SourceChildIndex != INDEX_NONE);
		const bool bDestFound = bDestIsGroup ? (DestGroupFirst != INDEX_NONE) : (DestLayerIndex != INDEX_NONE);
		if (!bSourceFound || !bDestFound)
		{
			return EInstancePlacement::SourceMissing;
		}
		if (SourceLayerId == DestLayerId && SourceChildIndex == DestChildIndex)
		{
			return EInstancePlacement::SelfReference;
		}

		// Same container (both the same layer, or both the same group): ordering is the container's
		// own child order, exactly as it always was for two children of one layer.
		if (SourceLayerId == DestLayerId)
		{
			return SourceChildIndex < DestChildIndex
				? EInstancePlacement::Valid
				: EInstancePlacement::SourceEvaluatesLater;
		}

		// Cross-container. A plain layer's position is its own index; a group's is the [First,Last]
		// range of layer indices its children get appended onto. The rule below is conservative
		// where a group is involved -- it requires the whole member range to be strictly on one
		// side -- rather than reasoning about a source or destination that only partially precedes
		// the other, which BuildEffectiveLayers' per-member broadcast makes ambiguous in general.
		if (!bSourceIsGroup && !bDestIsGroup)
		{
			return SourceLayerIndex < DestLayerIndex
				? EInstancePlacement::Valid
				: EInstancePlacement::SourceEvaluatesLater;
		}
		if (!bSourceIsGroup && bDestIsGroup)
		{
			return SourceLayerIndex < DestGroupFirst
				? EInstancePlacement::Valid
				: EInstancePlacement::SourceEvaluatesLater;
		}
		if (bSourceIsGroup && !bDestIsGroup)
		{
			return SourceGroupLast < DestLayerIndex
				? EInstancePlacement::Valid
				: EInstancePlacement::SourceEvaluatesLater;
		}
		// Both different groups.
		return SourceGroupLast < DestGroupFirst
			? EInstancePlacement::Valid
			: EInstancePlacement::SourceEvaluatesLater;
	}

	bool BreakChildInstance(
		const FMixtormatBindingScope& Scope,
		FMixtormatLayerChild& InOutChild)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (!InOutChild.IsInstance())
		{
			return false;
		}
		// Bake whatever the source says right now, then forget it. Resolving first means a broken
		// instance breaks into the values it was already showing rather than into empty defaults.
		FMixtormatLayer Scratch;
		Scratch.Children.Add(InOutChild);
		ResolveChildInstances(Scope, Scratch);
		const FGuid KeptChildId = InOutChild.ChildId;
		InOutChild = Scratch.Children[0];
		InOutChild.ChildId = KeptChildId;
		InOutChild.SourceLayerId = FGuid();
		InOutChild.SourceChildId = FGuid();
		return true;
	}

	void RemapChildParent(
		const FMixtormatMutableBindingScope& Scope,
		const FGuid& ChildId,
		const FGuid& OldLayerId,
		const FGuid& NewLayerId)
	{
		if (!ChildId.IsValid() || OldLayerId == NewLayerId)
		{
			return;
		}
		auto RemapBinding = [&](FMixtormatParameterBinding& Binding)
		{
			if (Binding.Reference.Source.ChildId == ChildId
				&& Binding.Reference.Source.LayerId == OldLayerId)
			{
				Binding.Reference.Source.LayerId = NewLayerId;
			}
			if (Binding.Driver.SourceChildId == ChildId
				&& Binding.Driver.SourceLayerId == OldLayerId)
			{
				Binding.Driver.SourceLayerId = NewLayerId;
			}
		};
		// A mask that publishes from ChildId's output (Worn Edges' Wear, Pattern's Gap) names it the
		// same way a reference does, just outside ParameterBindings -- so it has to follow the move
		// too, or the mask silently goes back to reading nothing.
		auto RemapPublishedSource = [&](FMixtormatLayerChild& Child)
		{
			if (Child.BoundaryId.RegionIdsSource.SourceChildId == ChildId
				&& Child.BoundaryId.RegionIdsSource.SourceLayerId == OldLayerId)
			{
				Child.BoundaryId.RegionIdsSource.SourceLayerId = NewLayerId;
			}
			if (Child.HeightPush.Source.SourceChildId == ChildId
				&& Child.HeightPush.Source.SourceLayerId == OldLayerId)
			{
				Child.HeightPush.Source.SourceLayerId = NewLayerId;
			}
			if (Child.StructuralWarp.Source.SourceChildId == ChildId
				&& Child.StructuralWarp.Source.SourceLayerId == OldLayerId)
			{
				Child.StructuralWarp.Source.SourceLayerId = NewLayerId;
			}
			if (Child.OutputReference.SourceChildId == ChildId
				&& Child.OutputReference.SourceLayerId == OldLayerId)
			{
				Child.OutputReference.SourceLayerId = NewLayerId;
			}
			if (Child.Type == EMixtormatLayerChildType::Mask
				&& Child.Mask.PublishedSourceChildId == ChildId
				&& Child.Mask.PublishedSourceLayerId == OldLayerId)
			{
				Child.Mask.PublishedSourceLayerId = NewLayerId;
			}
		};
		for (FMixtormatLayer& Layer : *Scope.Layers)
		{
			for (FMixtormatParameterBinding& Binding : Layer.ParameterBindings)
			{
				RemapBinding(Binding);
			}
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
				{
					RemapBinding(Binding);
				}
				if (Child.SourceChildId == ChildId && Child.SourceLayerId == OldLayerId)
				{
					Child.SourceLayerId = NewLayerId;
				}
				RemapPublishedSource(Child);
			}
		}
		// No Group.ParameterBindings pass: that array is reserved for group-level parameters that
		// nothing can address yet (see the FMixtormatLayerGroup declaration), so there is nothing
		// there a reference could already be pointing at. A shared child's own bindings are real
		// and addressable, so those still follow.
		if (Scope.Groups)
		{
			for (FMixtormatLayerGroup& Group : *Scope.Groups)
			{
				for (FMixtormatLayerChild& Child : Group.Children)
				{
					for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
					{
						RemapBinding(Binding);
					}
					if (Child.SourceChildId == ChildId && Child.SourceLayerId == OldLayerId)
					{
						Child.SourceLayerId = NewLayerId;
					}
					RemapPublishedSource(Child);
				}
			}
		}
	}

	void ApplyDirectReferences(
		const FMixtormatBindingScope& Scope,
		FMixtormatLayer& InOutLayer)
	{
		const TArray<FMixtormatLayer>& SourceLayers = Scope.GetLayers();
		// Instances first: an instance inherits its source's bindings, and those have to be
		// present before the binding pass walks them.
		ResolveChildInstances(Scope, InOutLayer);
		// Generator-owner bindings share the layer's set; a null child selects the layer payload.
		ApplyBindingSet(Scope, InOutLayer, nullptr, InOutLayer.ParameterBindings);
		for (FMixtormatLayerChild& Child : InOutLayer.Children)
		{
			ApplyBindingSet(Scope, InOutLayer, &Child, Child.ParameterBindings);
		}
	}
	FMixtormatParameterAddress ResolveLinkTarget(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Destination)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (!Destination.IsValid())
		{
			return {};
		}
		// Same visited-set guard the read path uses, for the same reason: a link chain can close
		// on itself just as a follow chain can, and here it would spin instead of merely resolving
		// to the local value.
		TSet<FString> Visited;
		FMixtormatParameterAddress Current = Destination;
		bool bMoved = false;
		while (true)
		{
			const FString Key = AddressKey(Current);
			if (Visited.Contains(Key))
			{
				return {};
			}
			Visited.Add(Key);

			const FOwnerView Owner = LocateOwner(Scope, Current);
			if (!Owner.ConstData || !Owner.Struct)
			{
				return {};
			}
			const FMixtormatParameterBinding* Binding = FindReferenceBinding(Owner, Current);
			if (!Binding)
			{
				// Arrived: nothing above this one. Only a chain that actually took a step has an
				// authority distinct from where it started.
				return bMoved ? Current : FMixtormatParameterAddress{};
			}
			if (Binding->Reference.Mode != EMixtormatReferenceMode::Link)
			{
				// A Follow step ends the walk in both directions: the destination of a Follow is not
				// writable through, and a parameter that follows something else is not an authority.
				return {};
			}
			const FMixtormatParameterAddress Source = Binding->Reference.Source;
			if (!Source.IsValid()
				|| !AreReferenceTypesCompatible(Current, Source)
				|| !IsReferenceSourceValid(Scope, Source))
			{
				return {};
			}
			Current = Source;
			bMoved = true;
		}
	}

	namespace
	{
		template <typename TProperty, typename TValue>
		bool WriteTyped(
			const FMixtormatMutableBindingScope& Scope,
			const FMixtormatParameterAddress& Address,
			const EMixtormatParameterValueType Expected,
			const TValue Value)
		{
			if (!Address.IsValid() || Address.ValueType != Expected)
			{
				return false;
			}
			const FOwnerView Owner = LocateMutableOwnerByAddress(Scope, Address);
			if (!Owner.MutableData || !Owner.Struct)
			{
				return false;
			}
			FProperty* Property = Owner.Struct->FindPropertyByName(Address.Parameter);
			if (!PropertyMatchesAddress(Property, Address))
			{
				return false;
			}
			CastFieldChecked<TProperty>(Property)->SetPropertyValue_InContainer(Owner.MutableData, Value);
			return true;
		}
	}

	bool TryWriteFloat(
		const FMixtormatMutableBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		const float Value)
	{
		return WriteTyped<FFloatProperty>(Scope, Address, EMixtormatParameterValueType::Float, Value);
	}

	bool TryWriteInt(
		const FMixtormatMutableBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		const int32 Value)
	{
		return WriteTyped<FIntProperty>(Scope, Address, EMixtormatParameterValueType::Int, Value);
	}

	bool TryWriteBool(
		const FMixtormatMutableBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		const bool Value)
	{
		return WriteTyped<FBoolProperty>(Scope, Address, EMixtormatParameterValueType::Bool, Value);
	}

	bool TryResolveFloat(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		float& OutValue)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (Address.ValueType != EMixtormatParameterValueType::Float)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Scope, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.FloatValue;
		return true;
	}

	bool TryResolveInt(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		int32& OutValue)
	{
		const TArray<FMixtormatLayer>& Layers = Scope.GetLayers();
		if (Address.ValueType != EMixtormatParameterValueType::Int)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Scope, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.IntValue;
		return true;
	}

	bool TryResolveBool(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		bool& OutValue)
	{
		if (Address.ValueType != EMixtormatParameterValueType::Bool)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Scope, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.BoolValue;
		return true;
	}

	bool TryWriteEnum(
		const FMixtormatMutableBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		const int64 Value)
	{
		if (!Address.IsValid() || Address.ValueType != EMixtormatParameterValueType::Enum)
		{
			return false;
		}
		const FOwnerView Owner = LocateMutableOwnerByAddress(Scope, Address);
		if (!Owner.MutableData || !Owner.Struct)
		{
			return false;
		}
		FProperty* Property = Owner.Struct->FindPropertyByName(Address.Parameter);
		if (!PropertyMatchesAddress(Property, Address))
		{
			return false;
		}
		// The two property shapes an enum address can name, mirroring ReadLocalValue: a reflected
		// enum property, written through its underlying numeric, and a byte property with an enum.
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(Owner.MutableData), Value);
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue_InContainer(Owner.MutableData, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	bool TryResolveEnum(
		const FMixtormatBindingScope& Scope,
		const FMixtormatParameterAddress& Address,
		int64& OutValue)
	{
		if (Address.ValueType != EMixtormatParameterValueType::Enum)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Scope, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.EnumValue;
		return true;
	}

}
