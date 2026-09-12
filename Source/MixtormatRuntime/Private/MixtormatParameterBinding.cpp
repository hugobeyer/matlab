// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatParameterBinding.h"

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
		case EMixtormatParameterOwnerType::Mask: return Child.Type == EMixtormatLayerChildType::Mask;
		case EMixtormatParameterOwnerType::Effect: return Child.Type == EMixtormatLayerChildType::Effect;
		case EMixtormatParameterOwnerType::Generated: return Child.Type == EMixtormatLayerChildType::Generated;
		case EMixtormatParameterOwnerType::Craquelure: return Child.Type == EMixtormatLayerChildType::Craquelure;
		case EMixtormatParameterOwnerType::ColorId: return Child.Type == EMixtormatLayerChildType::ColorId;
		case EMixtormatParameterOwnerType::ClusterId: return Child.Type == EMixtormatLayerChildType::Filter;
		case EMixtormatParameterOwnerType::HsvId: return Child.Type == EMixtormatLayerChildType::HsvFilter;
		case EMixtormatParameterOwnerType::RandomId: return Child.Type == EMixtormatLayerChildType::RandomId;
		case EMixtormatParameterOwnerType::PatternId: return Child.Type == EMixtormatLayerChildType::PatternId;
		case EMixtormatParameterOwnerType::RampId: return Child.Type == EMixtormatLayerChildType::RampId;
		case EMixtormatParameterOwnerType::MaskShaping:
			return Child.Type == EMixtormatLayerChildType::Mask
				|| Child.Type == EMixtormatLayerChildType::Craquelure
				|| Child.Type == EMixtormatLayerChildType::RandomId;
		default: return false;
		}
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
		case EMixtormatParameterOwnerType::Mask: View.ConstData = &Child.Mask; View.Struct = FMixtormatMaskLayer::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Effect: View.ConstData = &Child.Effect; View.Struct = FMixtormatLayerEffect::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Generated: View.ConstData = &Child.Generated; View.Struct = FMixtormatGeneratedMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Craquelure: View.ConstData = &Child.Craquelure; View.Struct = FMixtormatCraquelure::StaticStruct(); break;
		case EMixtormatParameterOwnerType::ColorId: View.ConstData = &Child.ColorId; View.Struct = FMixtormatColorIdMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::ClusterId: View.ConstData = &Child.Filter; View.Struct = FMixtormatClusterFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::HsvId: View.ConstData = &Child.HsvFilter; View.Struct = FMixtormatHsvIdFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::RandomId: View.ConstData = &Child.RandomId; View.Struct = FMixtormatRandomIdMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::PatternId: View.ConstData = &Child.PatternId; View.Struct = FMixtormatPatternFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::RampId: View.ConstData = &Child.RampId; View.Struct = FMixtormatRampIdFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::MaskShaping:
			if (Child.Type == EMixtormatLayerChildType::Mask) View.ConstData = &Child.Mask.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::Craquelure) View.ConstData = &Child.Craquelure.Shaping;
			else if (Child.Type == EMixtormatLayerChildType::RandomId) View.ConstData = &Child.RandomId.Shaping;
			View.Struct = FMixtormatMaskShaping::StaticStruct();
			break;
		default: break;
		}
		return View;
	}

	FOwnerView MutableChildOwner(FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner)
	{
		if (!ChildTypeMatchesOwner(Child, Owner))
		{
			return {};
		}
		FOwnerView View;
		View.Bindings = &Child.ParameterBindings;
		View.MutableBindings = &Child.ParameterBindings;
		switch (Owner)
		{
		case EMixtormatParameterOwnerType::Mask: View.MutableData = &Child.Mask; View.ConstData = &Child.Mask; View.Struct = FMixtormatMaskLayer::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Effect: View.MutableData = &Child.Effect; View.ConstData = &Child.Effect; View.Struct = FMixtormatLayerEffect::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Generated: View.MutableData = &Child.Generated; View.ConstData = &Child.Generated; View.Struct = FMixtormatGeneratedMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::Craquelure: View.MutableData = &Child.Craquelure; View.ConstData = &Child.Craquelure; View.Struct = FMixtormatCraquelure::StaticStruct(); break;
		case EMixtormatParameterOwnerType::ColorId: View.MutableData = &Child.ColorId; View.ConstData = &Child.ColorId; View.Struct = FMixtormatColorIdMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::ClusterId: View.MutableData = &Child.Filter; View.ConstData = &Child.Filter; View.Struct = FMixtormatClusterFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::HsvId: View.MutableData = &Child.HsvFilter; View.ConstData = &Child.HsvFilter; View.Struct = FMixtormatHsvIdFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::RandomId: View.MutableData = &Child.RandomId; View.ConstData = &Child.RandomId; View.Struct = FMixtormatRandomIdMask::StaticStruct(); break;
		case EMixtormatParameterOwnerType::PatternId: View.MutableData = &Child.PatternId; View.ConstData = &Child.PatternId; View.Struct = FMixtormatPatternFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::RampId: View.MutableData = &Child.RampId; View.ConstData = &Child.RampId; View.Struct = FMixtormatRampIdFilter::StaticStruct(); break;
		case EMixtormatParameterOwnerType::MaskShaping:
			if (Child.Type == EMixtormatLayerChildType::Mask) { View.MutableData = &Child.Mask.Shaping; View.ConstData = &Child.Mask.Shaping; }
			else if (Child.Type == EMixtormatLayerChildType::Craquelure) { View.MutableData = &Child.Craquelure.Shaping; View.ConstData = &Child.Craquelure.Shaping; }
			else if (Child.Type == EMixtormatLayerChildType::RandomId) { View.MutableData = &Child.RandomId.Shaping; View.ConstData = &Child.RandomId.Shaping; }
			View.Struct = FMixtormatMaskShaping::StaticStruct();
			break;
		default: break;
		}
		return View;
	}

	FOwnerView LocateOwner(const TArray<FMixtormatLayer>& Layers, const FMixtormatParameterAddress& Address)
	{
		for (const FMixtormatLayer& Layer : Layers)
		{
			if (Layer.LayerId != Address.LayerId)
			{
				continue;
			}
			if (Address.Owner == EMixtormatParameterOwnerType::Layer)
			{
				FOwnerView View;
				View.ConstData = &Layer;
				View.Struct = FMixtormatLayer::StaticStruct();
				View.Bindings = &Layer.ParameterBindings;
				return View;
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
		return {};
	}

	// The mutable twin of LocateOwner. ApplyDirectReferences only ever writes into the transient
	// layer it was handed, so it never needed one; a linked edit writes into the authored stack at
	// an arbitrary address and does.
	FOwnerView LocateMutableOwnerByAddress(TArray<FMixtormatLayer>& Layers, const FMixtormatParameterAddress& Address)
	{
		for (FMixtormatLayer& Layer : Layers)
		{
			if (Layer.LayerId != Address.LayerId)
			{
				continue;
			}
			if (Address.Owner == EMixtormatParameterOwnerType::Layer)
			{
				FOwnerView View;
				View.ConstData = &Layer;
				View.MutableData = &Layer;
				View.Struct = FMixtormatLayer::StaticStruct();
				View.Bindings = &Layer.ParameterBindings;
				View.MutableBindings = &Layer.ParameterBindings;
				return View;
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
		return {};
	}

	FOwnerView LocateMutableOwner(FMixtormatLayer& Layer, const FMixtormatParameterBinding& Binding, FMixtormatLayerChild* Child)
	{
		if (Binding.DestinationOwner == EMixtormatParameterOwnerType::Layer)
		{
			FOwnerView View;
			View.ConstData = &Layer;
			View.MutableData = &Layer;
			View.Struct = FMixtormatLayer::StaticStruct();
			View.Bindings = &Layer.ParameterBindings;
			View.MutableBindings = &Layer.ParameterBindings;
			return View;
		}
		return Child ? MutableChildOwner(*Child, Binding.DestinationOwner) : FOwnerView{};
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
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		TSet<FString>& Visiting,
		FResolvedValue& OutValue)
	{
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

		const FOwnerView Owner = LocateOwner(Layers, Address);
		if (!Owner.ConstData || !Owner.Struct)
		{
			Visiting.Remove(Key);
			return false;
		}
		if (const FMixtormatParameterBinding* Binding = FindReferenceBinding(Owner, Address))
		{
			const FMixtormatParameterAddress& Source = Binding->Reference.Source;
			if (MixtormatParameterBinding::AreReferenceTypesCompatible(Address, Source)
				&& ResolveValue(Layers, Source, Visiting, OutValue))
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
		const TArray<FMixtormatLayer>& SourceLayers,
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
			if (ResolveValue(SourceLayers, Binding.Reference.Source, Visiting, Value))
			{
				WriteResolvedValue(Destination, Binding, Value);
			}
		}
	}
}

namespace MixtormatParameterBinding
{
	void EnsureStableIds(TArray<FMixtormatLayer>& Layers)
	{
		TSet<FGuid> LayerIds;
		TSet<FGuid> ChildIds;
		for (FMixtormatLayer& Layer : Layers)
		{
			if (!Layer.LayerId.IsValid() || LayerIds.Contains(Layer.LayerId))
			{
				Layer.LayerId = FGuid::NewGuid();
			}
			LayerIds.Add(Layer.LayerId);
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId))
				{
					Child.ChildId = FGuid::NewGuid();
				}
				ChildIds.Add(Child.ChildId);
			}
		}
	}

	void RegenerateLayerIdentity(FMixtormatLayer& Layer, const bool bRegenerateChildren)
	{
		const FGuid OldLayerId = Layer.LayerId;
		const FGuid NewLayerId = FGuid::NewGuid();
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
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Source)
	{
		const FOwnerView Owner = LocateOwner(Layers, Source);
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
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& LayerId,
		const FGuid& ChildId)
	{
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
		return nullptr;
	}

	void ResolveChildInstances(
		const TArray<FMixtormatLayer>& SourceLayers,
		FMixtormatLayer& InOutLayer)
	{
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
				FindChild(SourceLayers, Child.SourceLayerId, Child.SourceChildId);
			while (Source && Source->IsInstance())
			{
				if (Visited.Contains(Source->ChildId))
				{
					Source = nullptr;
					break;
				}
				Visited.Add(Source->ChildId);
				Source = FindChild(SourceLayers, Source->SourceLayerId, Source->SourceChildId);
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
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& SourceLayerId,
		const FGuid& SourceChildId,
		const FGuid& DestLayerId,
		const int32 DestChildIndex)
	{
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
		if (SourceLayerIndex == INDEX_NONE || SourceChildIndex == INDEX_NONE || DestLayerIndex == INDEX_NONE)
		{
			return EInstancePlacement::SourceMissing;
		}
		if (SourceLayerId == DestLayerId && SourceChildIndex == DestChildIndex)
		{
			return EInstancePlacement::SelfReference;
		}
		// Layers composite in array order and children within a layer in theirs, so "earlier" is
		// simply a smaller index. Equal layer means the source has to sit above the instance.
		if (SourceLayerIndex < DestLayerIndex)
		{
			return EInstancePlacement::Valid;
		}
		if (SourceLayerIndex == DestLayerIndex && SourceChildIndex < DestChildIndex)
		{
			return EInstancePlacement::Valid;
		}
		return EInstancePlacement::SourceEvaluatesLater;
	}

	bool BreakChildInstance(
		const TArray<FMixtormatLayer>& Layers,
		FMixtormatLayerChild& InOutChild)
	{
		if (!InOutChild.IsInstance())
		{
			return false;
		}
		// Bake whatever the source says right now, then forget it. Resolving first means a broken
		// instance breaks into the values it was already showing rather than into empty defaults.
		FMixtormatLayer Scratch;
		Scratch.Children.Add(InOutChild);
		ResolveChildInstances(Layers, Scratch);
		const FGuid KeptChildId = InOutChild.ChildId;
		InOutChild = Scratch.Children[0];
		InOutChild.ChildId = KeptChildId;
		InOutChild.SourceLayerId = FGuid();
		InOutChild.SourceChildId = FGuid();
		return true;
	}

	void RemapChildParent(
		TArray<FMixtormatLayer>& Layers,
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
		for (FMixtormatLayer& Layer : Layers)
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
			}
		}
	}

	void ApplyDirectReferences(
		const TArray<FMixtormatLayer>& SourceLayers,
		FMixtormatLayer& InOutLayer)
	{
		// Instances first: an instance inherits its source's bindings, and those have to be
		// present before the binding pass walks them.
		ResolveChildInstances(SourceLayers, InOutLayer);
		ApplyBindingSet(SourceLayers, InOutLayer, nullptr, InOutLayer.ParameterBindings);
		for (FMixtormatLayerChild& Child : InOutLayer.Children)
		{
			ApplyBindingSet(SourceLayers, InOutLayer, &Child, Child.ParameterBindings);
		}
	}
	FMixtormatParameterAddress ResolveLinkTarget(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Destination)
	{
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

			const FOwnerView Owner = LocateOwner(Layers, Current);
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
				|| !IsReferenceSourceValid(Layers, Source))
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
			TArray<FMixtormatLayer>& Layers,
			const FMixtormatParameterAddress& Address,
			const EMixtormatParameterValueType Expected,
			const TValue Value)
		{
			if (!Address.IsValid() || Address.ValueType != Expected)
			{
				return false;
			}
			const FOwnerView Owner = LocateMutableOwnerByAddress(Layers, Address);
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

	bool TryWriteFloat(TArray<FMixtormatLayer>& Layers, const FMixtormatParameterAddress& Address, const float Value)
	{
		return WriteTyped<FFloatProperty>(Layers, Address, EMixtormatParameterValueType::Float, Value);
	}

	bool TryWriteInt(TArray<FMixtormatLayer>& Layers, const FMixtormatParameterAddress& Address, const int32 Value)
	{
		return WriteTyped<FIntProperty>(Layers, Address, EMixtormatParameterValueType::Int, Value);
	}

	bool TryWriteBool(TArray<FMixtormatLayer>& Layers, const FMixtormatParameterAddress& Address, const bool Value)
	{
		return WriteTyped<FBoolProperty>(Layers, Address, EMixtormatParameterValueType::Bool, Value);
	}

	bool TryResolveFloat(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		float& OutValue)
	{
		if (Address.ValueType != EMixtormatParameterValueType::Float)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Layers, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.FloatValue;
		return true;
	}

	bool TryResolveInt(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		int32& OutValue)
	{
		if (Address.ValueType != EMixtormatParameterValueType::Int)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Layers, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.IntValue;
		return true;
	}

	bool TryResolveBool(
		const TArray<FMixtormatLayer>& Layers,
		const FMixtormatParameterAddress& Address,
		bool& OutValue)
	{
		if (Address.ValueType != EMixtormatParameterValueType::Bool)
		{
			return false;
		}
		FResolvedValue Value;
		TSet<FString> Visiting;
		if (!ResolveValue(Layers, Address, Visiting, Value))
		{
			return false;
		}
		OutValue = Value.BoolValue;
		return true;
	}

}
