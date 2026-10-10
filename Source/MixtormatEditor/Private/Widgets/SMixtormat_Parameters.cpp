// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Parameters/MixtormatGeneratorPayload.h"
#include "Widgets/SMixtormatInternal.h"

#include "MixtormatParameterBinding.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Parameters/SMixtormatDriverPopover.h"
#include "UI/Parameters/SMixtormatParameterControl.h"
#include "UI/Parameters/MixtormatShaderParamScanner.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	bool SameAddress(const FMixtormatParameterAddress& A, const FMixtormatParameterAddress& B)
	{
		return A.LayerId == B.LayerId
			&& A.ChildId == B.ChildId
			&& A.Owner == B.Owner
			&& A.Parameter == B.Parameter
			&& A.ValueType == B.ValueType
			&& A.TypeName == B.TypeName;
	}

	EMixtormatParameterValueType ValueTypeForProperty(const FProperty* Property)
	{
		if (CastField<FFloatProperty>(Property)) return EMixtormatParameterValueType::Float;
		if (CastField<FIntProperty>(Property)) return EMixtormatParameterValueType::Int;
		if (CastField<FBoolProperty>(Property)) return EMixtormatParameterValueType::Bool;
		if (CastField<FEnumProperty>(Property)) return EMixtormatParameterValueType::Enum;
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
		{
			return EMixtormatParameterValueType::Enum;
		}
		return EMixtormatParameterValueType::Invalid;
	}

	FName TypeNameForProperty(const FProperty* Property)
	{
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			return Enum->GetEnum() ? Enum->GetEnum()->GetFName() : NAME_None;
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			return Byte->Enum ? Byte->Enum->GetFName() : NAME_None;
		}
		return NAME_None;
	}

	const FProperty* PropertyAtOffset(UScriptStruct* Struct, const int32 Offset)
	{
		if (!Struct || Offset == INDEX_NONE)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->GetOffset_ForInternal() == Offset)
			{
				return *It;
			}
		}
		return nullptr;
	}

	EMixtormatParameterOwnerType OwnerTypeForChild(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask: return EMixtormatParameterOwnerType::Mask;
		case EMixtormatLayerChildType::Effect: return EMixtormatParameterOwnerType::Effect;
		case EMixtormatLayerChildType::Generated: return EMixtormatParameterOwnerType::Generated;
		case EMixtormatLayerChildType::Craquelure: return EMixtormatParameterOwnerType::Craquelure;
		case EMixtormatLayerChildType::ColorId: return EMixtormatParameterOwnerType::ColorId;
		case EMixtormatLayerChildType::Filter: return EMixtormatParameterOwnerType::ClusterId;
		case EMixtormatLayerChildType::HsvFilter: return EMixtormatParameterOwnerType::HsvId;
		case EMixtormatLayerChildType::RandomId: return EMixtormatParameterOwnerType::RandomId;
		case EMixtormatLayerChildType::PatternId: return EMixtormatParameterOwnerType::PatternId;
		case EMixtormatLayerChildType::RampId: return EMixtormatParameterOwnerType::RampId;
		case EMixtormatLayerChildType::UvFromIds: return EMixtormatParameterOwnerType::UvId;
		case EMixtormatLayerChildType::ReliefFromIds: return EMixtormatParameterOwnerType::ReliefId;
		case EMixtormatLayerChildType::BoundaryFromIds: return EMixtormatParameterOwnerType::BoundaryId;
		case EMixtormatLayerChildType::HeightBlend: return EMixtormatParameterOwnerType::HeightBlend;
		case EMixtormatLayerChildType::HeightCurve: return EMixtormatParameterOwnerType::HeightCurve;
		case EMixtormatLayerChildType::HeightColorRamp: return EMixtormatParameterOwnerType::HeightColorRamp;
				case EMixtormatLayerChildType::HeightPush: return EMixtormatParameterOwnerType::HeightPush;
						case EMixtormatLayerChildType::StructuralWarp: return EMixtormatParameterOwnerType::StructuralWarp;

		case EMixtormatLayerChildType::IdGroup: return EMixtormatParameterOwnerType::IdGroup;
		case EMixtormatLayerChildType::Blur: return EMixtormatParameterOwnerType::Blur;
		case EMixtormatLayerChildType::Curvature: return EMixtormatParameterOwnerType::Curvature;
		case EMixtormatLayerChildType::Generator: return EMixtormatParameterOwnerType::Generator;
		default: return EMixtormatParameterOwnerType::Layer;
		}
	}

	bool ChildHasParameterOwner(const FMixtormatLayerChild& Child, const EMixtormatParameterOwnerType Owner)
	{
		const UScriptStruct* Struct = nullptr;
		return MixtormatParameterBinding::GetChildOwnerData(Child, Owner, Struct) != nullptr;
	}

	const void* OwnerPointer(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask: return &Child.Mask;
		case EMixtormatLayerChildType::Effect: return &Child.Effect;
		case EMixtormatLayerChildType::Generated: return &Child.Generated;
		case EMixtormatLayerChildType::Craquelure: return &Child.Craquelure;
		case EMixtormatLayerChildType::ColorId: return &Child.ColorId;
		case EMixtormatLayerChildType::Filter: return &Child.Filter;
		case EMixtormatLayerChildType::HsvFilter: return &Child.HsvFilter;
		case EMixtormatLayerChildType::RandomId: return &Child.RandomId;
		case EMixtormatLayerChildType::PatternId: return &Child.PatternId;
		case EMixtormatLayerChildType::RampId: return &Child.RampId;
		case EMixtormatLayerChildType::UvFromIds: return &Child.UvId;
		case EMixtormatLayerChildType::ReliefFromIds: return &Child.ReliefId;
		case EMixtormatLayerChildType::BoundaryFromIds: return &Child.BoundaryId;
		case EMixtormatLayerChildType::HeightBlend: return &Child.HeightBlend;
		case EMixtormatLayerChildType::HeightCurve: return &Child.HeightCurve;
		case EMixtormatLayerChildType::HeightColorRamp: return &Child.HeightColorRamp;
				case EMixtormatLayerChildType::HeightPush: return &Child.HeightPush;
						case EMixtormatLayerChildType::StructuralWarp: return &Child.StructuralWarp;

		case EMixtormatLayerChildType::IdGroup: return &Child.IdGroup;
		case EMixtormatLayerChildType::Blur: return &Child.Blur;
		case EMixtormatLayerChildType::Curvature: return &Child.Curvature;
		// The payload, not the wrapper -- it has to be the same pointer ChildOwner exposes for
		// EMixtormatParameterOwnerType::Generator, or an address built from a generator slider
		// would fail to find the child it came from.
		case EMixtormatLayerChildType::Generator:
			return MixtormatGeneratorPayload::Data(Child.Generator);
		default: return nullptr;
		}
	}

	FText DriverCombineText(const EMixtormatDriverCombineMode Mode)
	{
		if (const UEnum* Enum = StaticEnum<EMixtormatDriverCombineMode>())
		{
			return Enum->GetDisplayNameTextByValue(static_cast<int64>(Mode));
		}
		return FText::GetEmpty();
	}
}

FMixtormatParameterAddress SMixtormat::BuildParameterAddress(
	const void* Owner,
	UScriptStruct* OwnerStruct,
	const int32 MemberOffset) const
{
	FMixtormatParameterAddress Result;
	if (!Owner || !OwnerStruct)
	{
		return Result;
	}
	const FProperty* Property = PropertyAtOffset(OwnerStruct, MemberOffset);
	if (!Property)
	{
		return Result;
	}
	Result.Parameter = Property->GetFName();
	Result.ValueType = ValueTypeForProperty(Property);
	Result.TypeName = TypeNameForProperty(Property);

	// ContainerId is whatever names the container in an address: a layer's LayerId, or a group's
	// GroupId standing in the same slot. See FMixtormatBindingScope.
	const auto ScanChildren =
		[&Result, Owner, OwnerStruct](
			const TArray<FMixtormatLayerChild>& Children,
			const FGuid& ContainerId)
	{
		for (const FMixtormatLayerChild& Child : Children)
		{
			if (Child.Type == EMixtormatLayerChildType::Mask && Owner == &Child.Mask.Noise
				&& OwnerStruct == FMixtormatNoise::StaticStruct())
			{
				Result.LayerId = ContainerId;
				Result.ChildId = Child.ChildId;
				Result.Owner = EMixtormatParameterOwnerType::MaskNoise;
				return true;
			}
			if (Child.Type == EMixtormatLayerChildType::StructuralWarp
				&& Owner == &Child.StructuralWarp.Source
				&& OwnerStruct == FMixtormatOutputReference::StaticStruct())
			{
				Result.LayerId = ContainerId;
				Result.ChildId = Child.ChildId;
				Result.Owner = EMixtormatParameterOwnerType::StructuralWarpFlow;
				return true;
			}
			if (Owner == OwnerPointer(Child))
			{
				Result.LayerId = ContainerId;
				Result.ChildId = Child.ChildId;
				Result.Owner = OwnerTypeForChild(Child);
				return true;
			}
			if (OwnerStruct == FMixtormatMaskShaping::StaticStruct())
			{
				const void* Shaping = nullptr;
				if (Child.Type == EMixtormatLayerChildType::Mask) Shaping = &Child.Mask.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::Generated) Shaping = &Child.Generated.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::Craquelure) Shaping = &Child.Craquelure.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::ColorId) Shaping = &Child.ColorId.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::RandomId) Shaping = &Child.RandomId.Shaping;
				if (Owner == Shaping)
				{
					Result.LayerId = ContainerId;
					Result.ChildId = Child.ChildId;
					Result.Owner = EMixtormatParameterOwnerType::MaskShaping;
					return true;
				}
			}
		}
		return false;
	};

	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (OwnerStruct == FMixtormatLayer::StaticStruct() && Owner == &Layer)
		{
			Result.LayerId = Layer.LayerId;
			Result.Owner = EMixtormatParameterOwnerType::Layer;
			return Result;
		}
		if (ScanChildren(Layer.Children, Layer.LayerId))
		{
			return Result;
		}
	}

	// A shared group child. No Layer case: a group has no FMixtormatLayer of its own, so only its
	// children are addressable.
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		if (ScanChildren(Group.Children, Group.GroupId))
		{
			return Result;
		}
	}

	for (const FMixtormatSourceEntry& Source : WorkingSources)
	{
		if (Owner == OwnerPointer(Source.Child))
		{
			Result.LayerId = Source.SourceId;
			Result.ChildId = Source.Child.ChildId;
			Result.Owner = OwnerTypeForChild(Source.Child);
			return Result;
		}
		if (ScanChildren(Source.OwnedChildren, Source.SourceId)) { return Result; }
	}

	Result.Parameter = NAME_None;
	return Result;
}

namespace
{
	// The binding for one address within one container's binding array. Shared by the layer and
	// group paths so the create-and-retype rules cannot drift between them.
	FMixtormatParameterBinding* FindBindingIn(
		TArray<FMixtormatParameterBinding>& Bindings,
		const FMixtormatParameterAddress& Target,
		const bool bCreate)
	{
		FMixtormatParameterBinding* Binding = Bindings.FindByPredicate(
			[&Target](const FMixtormatParameterBinding& Item)
			{
				return Item.DestinationOwner == Target.Owner
					&& Item.DestinationParameter == Target.Parameter;
			});
		if (!Binding && bCreate)
		{
			Binding = &Bindings.AddDefaulted_GetRef();
			Binding->DestinationOwner = Target.Owner;
			Binding->DestinationParameter = Target.Parameter;
			Binding->ValueType = Target.ValueType;
			Binding->TypeName = Target.TypeName;
		}
		else if (Binding
			&& (Binding->ValueType != Target.ValueType || Binding->TypeName != Target.TypeName))
		{
			// Reading state must not modify authored bindings. Only an explicit authoring
			// operation is allowed to retype the stored binding for this parameter.
			if (!bCreate) { return nullptr; }
			// The parameter under this address changed type, so whatever was bound to it no
			// longer describes anything. Reset rather than reinterpret.
			Binding->ValueType = Target.ValueType;
			Binding->TypeName = Target.TypeName;
			Binding->Reference = FMixtormatParameterReference{};
			Binding->Driver = FMixtormatParameterDriver{};
		}
		return Binding;
	}
}

FMixtormatParameterBinding* SMixtormat::FindParameterBinding(
	const FMixtormatParameterAddress& Target,
	const bool bCreate)
{
	if (!Target.IsValid())
	{
		return nullptr;
	}
	for (FMixtormatSourceEntry& Source : WorkingSources)
	{
		if (Source.SourceId != Target.LayerId) { continue; }
		if (Source.Child.ChildId == Target.ChildId && ChildHasParameterOwner(Source.Child, Target.Owner))
		{
			return FindBindingIn(Source.Child.ParameterBindings, Target, bCreate);
		}
		for (FMixtormatLayerChild& Child : Source.OwnedChildren)
		{
			if (Child.ChildId == Target.ChildId && ChildHasParameterOwner(Child, Target.Owner))
			{
				return FindBindingIn(Child.ParameterBindings, Target, bCreate);
			}
		}
		return nullptr;
	}
	for (FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		if (Group.GroupId != Target.LayerId)
		{
			continue;
		}
		for (FMixtormatLayerChild& Child : Group.Children)
		{
			if (Child.ChildId == Target.ChildId && ChildHasParameterOwner(Child, Target.Owner))
			{
				return FindBindingIn(Child.ParameterBindings, Target, bCreate);
			}
		}
		return nullptr;
	}
	for (FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Target.LayerId)
		{
			continue;
		}
		TArray<FMixtormatParameterBinding>* Bindings = nullptr;
		if (Target.Owner == EMixtormatParameterOwnerType::Layer)
		{
			Bindings = &Layer.ParameterBindings;
		}
		else
		{
			for (FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == Target.ChildId && ChildHasParameterOwner(Child, Target.Owner))
				{
					Bindings = &Child.ParameterBindings;
					break;
				}
			}
		}
		return Bindings ? FindBindingIn(*Bindings, Target, bCreate) : nullptr;
	}
	return nullptr;
}

const FMixtormatParameterBinding* SMixtormat::FindParameterBinding(const FMixtormatParameterAddress& Target) const
{
	if (!Target.IsValid())
	{
		return nullptr;
	}
	for (const FMixtormatSourceEntry& Source : WorkingSources)
	{
		if (Source.SourceId != Target.LayerId) { continue; }
		const FMixtormatLayerChild* Child = Source.Child.ChildId == Target.ChildId ? &Source.Child :
			Source.OwnedChildren.FindByPredicate([&Target](const FMixtormatLayerChild& Candidate)
			{ return Candidate.ChildId == Target.ChildId; });
		if (!Child || !ChildHasParameterOwner(*Child, Target.Owner)) { return nullptr; }
		return Child->ParameterBindings.FindByPredicate([&Target](const FMixtormatParameterBinding& Item)
		{
			return Item.DestinationOwner == Target.Owner && Item.DestinationParameter == Target.Parameter;
		});
	}
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		if (Group.GroupId != Target.LayerId)
		{
			continue;
		}
		for (const FMixtormatLayerChild& Child : Group.Children)
		{
			if (Child.ChildId == Target.ChildId && ChildHasParameterOwner(Child, Target.Owner))
			{
				return Child.ParameterBindings.FindByPredicate(
					[&Target](const FMixtormatParameterBinding& Item)
					{
						return Item.DestinationOwner == Target.Owner
							&& Item.DestinationParameter == Target.Parameter;
					});
			}
		}
		return nullptr;
	}
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Target.LayerId)
		{
			continue;
		}
		const TArray<FMixtormatParameterBinding>* Bindings = nullptr;
		if (Target.Owner == EMixtormatParameterOwnerType::Layer)
		{
			Bindings = &Layer.ParameterBindings;
		}
		else
		{
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == Target.ChildId && ChildHasParameterOwner(Child, Target.Owner))
				{
					Bindings = &Child.ParameterBindings;
					break;
				}
			}
		}
		return Bindings ? Bindings->FindByPredicate([&Target](const FMixtormatParameterBinding& Item)
		{
			return Item.DestinationOwner == Target.Owner && Item.DestinationParameter == Target.Parameter;
		}) : nullptr;
	}
	return nullptr;
}

bool SMixtormat::IsParameterReferenced(const FMixtormatParameterAddress& Target) const
{
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	return Binding && Binding->Reference.bEnabled;
}

bool SMixtormat::IsParameterDriven(const FMixtormatParameterAddress& Target) const
{
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	return Binding && Binding->Driver.bEnabled;
}

bool SMixtormat::IsParameterReferenceBroken(const FMixtormatParameterAddress& Target) const
{
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	return Binding && Binding->Reference.bEnabled
		&& !MixtormatParameterBinding::IsReferenceSourceValid({WorkingLayers, WorkingLayerGroups, WorkingSources}, Binding->Reference.Source);
}

bool SMixtormat::WouldCreateParameterReferenceCycle(
	const FMixtormatParameterAddress& Destination,
	const FMixtormatParameterAddress& Source) const
{
	FMixtormatParameterAddress Cursor = Source;
	TSet<FString> Seen;
	while (Cursor.IsValid())
	{
		if (SameAddress(Cursor, Destination))
		{
			return true;
		}
		const FString Key = FString::Printf(
			TEXT("%s|%s|%d|%s"),
			*Cursor.LayerId.ToString(EGuidFormats::Digits),
			*Cursor.ChildId.ToString(EGuidFormats::Digits),
			static_cast<int32>(Cursor.Owner),
			*Cursor.Parameter.ToString());
		if (Seen.Contains(Key))
		{
			return true;
		}
		Seen.Add(Key);
		const FMixtormatParameterBinding* Binding = FindParameterBinding(Cursor);
		if (!Binding || !Binding->Reference.bEnabled)
		{
			break;
		}
		Cursor = Binding->Reference.Source;
	}
	return false;
}

bool SMixtormat::CanPasteParameterReference(const FMixtormatParameterAddress& Target) const
{
	return ParameterReferenceClipboard.IsSet()
		&& MixtormatParameterBinding::AreReferenceTypesCompatible(Target, ParameterReferenceClipboard.GetValue())
		&& MixtormatParameterBinding::IsReferenceSourceValid({WorkingLayers, WorkingLayerGroups, WorkingSources}, ParameterReferenceClipboard.GetValue())
		&& !WouldCreateParameterReferenceCycle(Target, ParameterReferenceClipboard.GetValue());
}

void SMixtormat::CopyParameterReference(FMixtormatParameterAddress Source)
{
	if (MixtormatParameterBinding::IsReferenceSourceValid({WorkingLayers, WorkingLayerGroups, WorkingSources}, Source))
	{
		ParameterReferenceClipboard = Source;
		WorkingStatusText = FString::Printf(TEXT("Reference copied · %s"), *Source.Parameter.ToString());
	}
}

void SMixtormat::PasteParameterReference(FMixtormatParameterAddress Destination)
{
	if (!CanPasteParameterReference(Destination))
	{
		return;
	}
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Destination, true))
	{
		Binding->Reference.bEnabled = true;
		Binding->Reference.Source = ParameterReferenceClipboard.GetValue();
		Binding->Driver.bEnabled = false;
		RefreshLayeredPreview();
	}
}

void SMixtormat::ClearParameterReference(FMixtormatParameterAddress Target)
{
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, false))
	{
		Binding->Reference.bEnabled = false;
		RefreshLayeredPreview();
	}
}

void SMixtormat::GoToParameterReferenceSource(FMixtormatParameterAddress Target)
{
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	if (!Binding || !Binding->Reference.bEnabled)
	{
		return;
	}
	const FMixtormatParameterAddress Source = Binding->Reference.Source;
	if (WorkingSources.ContainsByPredicate([&Source](const FMixtormatSourceEntry& Entry)
		{ return Entry.SourceId == Source.LayerId; }))
	{
		SelectSource(Source.LayerId);
		return;
	}
	for (int32 LayerIndex = 0; LayerIndex < WorkingLayers.Num(); ++LayerIndex)
	{
		if (WorkingLayers[LayerIndex].LayerId != Source.LayerId)
		{
			continue;
		}
		if (Source.Owner == EMixtormatParameterOwnerType::Layer)
		{
			SelectWorkingLayer(LayerIndex);
			return;
		}
		for (int32 ChildIndex = 0; ChildIndex < WorkingLayers[LayerIndex].Children.Num(); ++ChildIndex)
		{
			if (WorkingLayers[LayerIndex].Children[ChildIndex].ChildId == Source.ChildId)
			{
				SelectWorkingChild(LayerIndex, ChildIndex);
				return;
			}
		}
	}
}

void SMixtormat::SetParameterReferenceMode(
	FMixtormatParameterAddress Target,
	const EMixtormatReferenceMode Mode)
{
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, false))
	{
		Binding->Reference.Mode = Mode;
		RefreshLayeredPreview();
	}
}

EMixtormatReferenceMode SMixtormat::GetParameterReferenceMode(const FMixtormatParameterAddress& Target) const
{
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	return Binding ? Binding->Reference.Mode : EMixtormatReferenceMode::Follow;
}

// The three linked writes below share one shape: ask the runtime where a linked edit belongs, and
// only if it names somewhere do we write there instead of locally. An unreferenced parameter, a
// Follow, a broken chain or a cycle all answer with an invalid address, so the caller falls back
// to the behaviour it had before Link existed.
bool SMixtormat::TryWriteLinkedFloat(const FMixtormatParameterAddress& Target, const float Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteFloat({WorkingLayers, WorkingLayerGroups, WorkingSources}, Authority, Value);
}

bool SMixtormat::TryWriteLinkedInt(const FMixtormatParameterAddress& Target, const int32 Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteInt({WorkingLayers, WorkingLayerGroups, WorkingSources}, Authority, Value);
}

bool SMixtormat::TryWriteLinkedBool(const FMixtormatParameterAddress& Target, const bool Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteBool({WorkingLayers, WorkingLayerGroups, WorkingSources}, Authority, Value);
}

bool SMixtormat::TryWriteLinkedEnum(const FMixtormatParameterAddress& Target, const int64 Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteEnum({WorkingLayers, WorkingLayerGroups, WorkingSources}, Authority, Value);
}

TSharedRef<SWidget> SMixtormat::BuildParameterContextMenu(FMixtormatParameterAddress Target)
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	MixtormatMenu::FBuilder Menu;
	// One flat list, always the same five rows, greyed where they do not apply. Rows that appear
	// and disappear with the parameter's state move the item under the cursor between two
	// right-clicks on the same row, and hide the vocabulary from anyone who has not already
	// referenced something.
	Menu.Caption(LOCTEXT("ParameterReferenceCaption", "Parameter"))
		.Item(
			LOCTEXT("CopyParameterReference", "Copy Reference"),
			MixtormatIcons::Duplicate(),
			FSimpleDelegate::CreateSP(this, &SMixtormat::CopyParameterReference, Target))
		.Enabled(Target.IsValid())
		.Item(
			LOCTEXT("PasteParameterReference", "Paste Reference"),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::PasteParameterReference, Target))
		.Enabled(TAttribute<bool>::CreateLambda([this, Target]() { return CanPasteParameterReference(Target); }))
		.Item(
			LOCTEXT("ClearParameterReference", "Clear Reference"),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::ClearParameterReference, Target))
		.Enabled(TAttribute<bool>::CreateLambda([this, Target]() { return IsParameterReferenced(Target); }))
		.Item(
			LOCTEXT("GoToParameterReferenceSource", "Go to Source"),
			MixtormatIcons::ArrowUp(),
			FSimpleDelegate::CreateSP(this, &SMixtormat::GoToParameterReferenceSource, Target))
		.Enabled(TAttribute<bool>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
			return Binding
				&& Binding->Reference.bEnabled
				&& MixtormatParameterBinding::IsReferenceSourceValid({WorkingLayers, WorkingLayerGroups, WorkingSources}, Binding->Reference.Source);
		}))
		.Separator()
		// Which way the reference runs, as the two words themselves rather than a lock glyph
		// whose closed state could mean either. Only meaningful once there is a reference, so
		// it is disabled rather than hidden -- same reasoning as the rows above it.
		.Caption(LOCTEXT("ParameterReferenceModeCaption", "Reference"))
		.Widget(
			SNew(SBox)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap,
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap))
			.IsEnabled_Lambda([this, Target]() { return IsParameterReferenced(Target); })
			[
				SNew(SMixtormatSegmentedControl)
				.Options({
					LOCTEXT("ReferenceModeFollow", "Follow Source"),
					LOCTEXT("ReferenceModeLink", "Link Values") })
				.ToolTips({
					LOCTEXT("ReferenceModeFollowHint", "One way. This parameter reads the source; editing here writes locally and drops the reference."),
					LOCTEXT("ReferenceModeLinkHint", "Two way. This parameter and its source are one value -- editing either writes to the source, and the reference survives.") })
				.ActiveIndex_Lambda([this, Target]()
				{
					return GetParameterReferenceMode(Target) == EMixtormatReferenceMode::Link ? 1 : 0;
				})
				.OnChosen_Lambda([this, Target](const int32 Index)
				{
					SetParameterReferenceMode(
						Target,
						Index == 1 ? EMixtormatReferenceMode::Link : EMixtormatReferenceMode::Follow);
				})
			])
		.Separator()
		// The same popover the state dot opens, as a submenu rather than a second editor. The
		// dot is the shortcut for anyone who knows it is there; this is how it is found.
		.SubMenu(
			LOCTEXT("AddParameterDriver", "Add Driver..."),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildParameterDriverPopover, Target))
		.Enabled(Target.IsValid()
			&& !WorkingSources.ContainsByPredicate([&Target](const FMixtormatSourceEntry& Entry)
				{ return Entry.SourceId == Target.LayerId; })
			&& (Target.Owner != EMixtormatParameterOwnerType::StructuralWarpFlow
				|| (Target.ValueType == EMixtormatParameterValueType::Float
					&& (Target.Parameter == FName(TEXT("FlowAmount"))
						|| Target.Parameter == FName(TEXT("FlowTraceLength"))))));

	if (IsParameterDriven(Target))
	{
		Menu.Item(
			LOCTEXT("ClearParameterDriver", "Clear Driver"),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::ClearParameterDriver, Target));
	}

	// Developer-only surface. Gated by one console variable, compiled only into the editor
	// module -- a packaged build carries neither the flag's consumer nor this code.
	if (MixtormatParameterUi::IsDeveloperMetaEnabled() && Target.IsValid())
	{
		const TOptional<FMixtormatParameterDefinitionKey> DevKey =
			MixtormatParameterUi::DefinitionKeyOf(Target);
		const bool bPersistentlyEditable = DevKey.IsSet()
			&& MixtormatParameterAuthoring::IsPersistentlyEditable(*DevKey);

		Menu.Separator()
			.Caption(LOCTEXT("ParameterDeveloperCaption", "Developer"))
			.SubMenu(
				LOCTEXT("DevParameterInfo", "Parameter Info"),
				nullptr,
				FOnGetContent::CreateSP(this, &SMixtormat::BuildParameterInfoPanel, Target));

		if (bPersistentlyEditable)
		{
			Menu.SubMenu(
					LOCTEXT("DevEditAuthoring", "Edit Authoring Setup..."),
					nullptr,
					FOnGetContent::CreateSP(this, &SMixtormat::BuildAuthoringSetupPanel, Target))
				.Item(
					LOCTEXT("DevSaveAuthoring", "Save to Plugin Defaults"),
					nullptr,
					FSimpleDelegate::CreateLambda([this]()
					{
						if (MixtormatParameterAuthoring::SavePendingToPluginDefaults())
						{
							WorkingStatusText = TEXT("Authoring defaults saved to the plugin database");
						}
						else
						{
							WorkingStatusText = TEXT("Authoring save FAILED -- plugin directory may be read-only. Edits kept unsaved.");
						}
					}))
				.Enabled(TAttribute<bool>::CreateLambda([]()
				{
					return MixtormatParameterAuthoring::HasAnyPendingAuthoring();
				}))
				.Item(
					LOCTEXT("DevRevertAuthoring", "Revert Unsaved Changes"),
					nullptr,
					FSimpleDelegate::CreateLambda([DevKey]()
					{
						MixtormatParameterAuthoring::RevertPendingAuthoring(DevKey.GetValue());
					}))
				.Enabled(TAttribute<bool>::CreateLambda([DevKey]()
				{
					return MixtormatParameterAuthoring::HasPendingAuthoring(DevKey.GetValue());
				}))
				.Item(
					LOCTEXT("DevRestoreShipped", "Restore Compiled Defaults"),
					nullptr,
					FSimpleDelegate::CreateLambda([this, DevKey]()
					{
						if (!MixtormatParameterAuthoring::RestoreShippedAuthoring(DevKey.GetValue()))
						{
							WorkingStatusText = TEXT("Authoring restore FAILED -- plugin directory may be read-only.");
						}
					}))
				.Enabled(TAttribute<bool>::CreateLambda([DevKey]()
				{
					return MixtormatParameterAuthoring::HasShippedAuthoring(DevKey.GetValue());
				}));
		}
		else
		{
			// Session-only range override remains the tool for parameters that are not opted in
			// to persistent authoring.
			Menu.SubMenu(
					LOCTEXT("DevOverrideUiRange", "Override UI Range..."),
					nullptr,
					FOnGetContent::CreateSP(this, &SMixtormat::BuildParameterUiRangeOverridePanel, Target))
				.Item(
					LOCTEXT("DevRemoveUiRangeOverride", "Remove UI Range Override"),
					nullptr,
					FSimpleDelegate::CreateLambda([Target]()
					{
						if (const TOptional<FMixtormatParameterDefinitionKey> Key =
							MixtormatParameterUi::DefinitionKeyOf(Target))
						{
							MixtormatParameterUi::ClearUiRangeOverride(Key.GetValue());
						}
					}))
				.Enabled(TAttribute<bool>::CreateLambda([Target]()
				{
					const TOptional<FMixtormatParameterDefinitionKey> Key =
						MixtormatParameterUi::DefinitionKeyOf(Target);
					return Key.IsSet() && MixtormatParameterUi::HasUiRangeOverride(Key.GetValue());
				}));
		}

		Menu.Item(
			LOCTEXT("DevCopyParameterAddress", "Copy Parameter Address"),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::CopyParameterAddress, Target));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::WrapParameterControl(
	const TSharedRef<SWidget>& Control,
	TFunction<FMixtormatParameterAddress()> ResolveTarget)
{
	// Always wrapped, never conditionally. The address is only knowable once something is
	// selected, and the row is built long before that -- so the wrap cannot be decided here. The
	// control asks bHasTarget on every paint instead: no selection, no state slot, and the press
	// falls through to the group so Expand All still answers on header and empty space.
	return SNew(SMixtormatParameterControl)
		.bHasTarget_Lambda([ResolveTarget]() { return ResolveTarget().IsValid(); })
		.bReferenced_Lambda([this, ResolveTarget]() { return IsParameterReferenced(ResolveTarget()); })
		.bDriven_Lambda([this, ResolveTarget]() { return IsParameterDriven(ResolveTarget()); })
		.bBroken_Lambda([this, ResolveTarget]() { return IsParameterReferenceBroken(ResolveTarget()); })
		.OnGetContextMenu(FOnGetContent::CreateSP(
			this, &SMixtormat::BuildParameterContextMenuFor, ResolveTarget))
		.OnGetDriverContent(FOnGetContent::CreateSP(
			this, &SMixtormat::BuildParameterDriverPopoverFor, ResolveTarget))
		[
			Control
		];
}

// The two popovers are declared against a resolved address so their bodies stay readable; these
// resolve at open time, which is the only moment the address is known.
TSharedRef<SWidget> SMixtormat::BuildParameterContextMenuFor(
	TFunction<FMixtormatParameterAddress()> ResolveTarget)
{
	return BuildParameterContextMenu(ResolveTarget());
}

namespace
{
	UScriptStruct* PayloadStructForChild(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Mask: return FMixtormatMaskLayer::StaticStruct();
		case EMixtormatLayerChildType::Effect: return FMixtormatLayerEffect::StaticStruct();
		case EMixtormatLayerChildType::Generated: return FMixtormatGeneratedMask::StaticStruct();
		case EMixtormatLayerChildType::Craquelure: return FMixtormatCraquelure::StaticStruct();
		case EMixtormatLayerChildType::ColorId: return FMixtormatColorIdMask::StaticStruct();
		case EMixtormatLayerChildType::Filter: return FMixtormatClusterFilter::StaticStruct();
		case EMixtormatLayerChildType::HsvFilter: return FMixtormatHsvIdFilter::StaticStruct();
		case EMixtormatLayerChildType::RandomId: return FMixtormatRandomIdMask::StaticStruct();
		case EMixtormatLayerChildType::PatternId: return FMixtormatPatternFilter::StaticStruct();
		case EMixtormatLayerChildType::RampId: return FMixtormatRampIdFilter::StaticStruct();
		case EMixtormatLayerChildType::UvFromIds: return FMixtormatUvIdFilter::StaticStruct();
		case EMixtormatLayerChildType::ReliefFromIds: return FMixtormatReliefIdFilter::StaticStruct();
		case EMixtormatLayerChildType::BoundaryFromIds: return FMixtormatBoundaryIdFilter::StaticStruct();
		case EMixtormatLayerChildType::HeightBlend: return FMixtormatGeneratorHeightBlend::StaticStruct();
		case EMixtormatLayerChildType::HeightCurve: return FMixtormatGeneratorHeightCurve::StaticStruct();
		case EMixtormatLayerChildType::HeightColorRamp: return FMixtormatGeneratorHeightColorRamp::StaticStruct();
					case EMixtormatLayerChildType::HeightPush: return FMixtormatGeneratorHeightPush::StaticStruct();
								case EMixtormatLayerChildType::StructuralWarp: return FMixtormatGeneratorStructuralWarp::StaticStruct();

		case EMixtormatLayerChildType::IdGroup: return FMixtormatIdGroup::StaticStruct();
		case EMixtormatLayerChildType::Blur: return FMixtormatMaskBlur::StaticStruct();
		case EMixtormatLayerChildType::Curvature: return FMixtormatMaskCurvature::StaticStruct();
		case EMixtormatLayerChildType::Generator:
			return MixtormatGeneratorPayload::Struct(Child.Generator);
		default: return nullptr;
		}
	}

	// The authored value behind an address, read straight off the member so Parameter Info can
	// show it without a widget holding a member pointer. Effective (reference-resolved) display
	// goes through GetEffectiveFloatParameter on top of this.
	bool TryReadAuthoredScalar(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		const FMixtormatParameterAddress& Address,
		double& OutValue)
	{
		const FMixtormatLayerChild* Child = MixtormatParameterBinding::FindChild(
			{Layers, Groups}, Address.LayerId, Address.ChildId);
		if (!Child && Address.ChildId.IsValid())
		{
			return false;
		}

		const void* OwnerPtr = nullptr;
		const UScriptStruct* OwnerStruct = nullptr;
		if ((Address.Owner == EMixtormatParameterOwnerType::MaskNoise
			|| Address.Owner == EMixtormatParameterOwnerType::StructuralWarpFlow) && Child)
		{
			OwnerPtr = MixtormatParameterBinding::GetChildOwnerData(*Child, Address.Owner, OwnerStruct);
		}
		else if (Address.Owner == EMixtormatParameterOwnerType::MaskShaping && Child)
		{
			if (Child->Type == EMixtormatLayerChildType::Mask) OwnerPtr = &Child->Mask.Shaping;
			else if (Child->Type == EMixtormatLayerChildType::Craquelure) OwnerPtr = &Child->Craquelure.Shaping;
			else if (Child->Type == EMixtormatLayerChildType::RandomId) OwnerPtr = &Child->RandomId.Shaping;
			OwnerStruct = FMixtormatMaskShaping::StaticStruct();
		}
		else if (Address.Owner == EMixtormatParameterOwnerType::Layer)
		{
			for (const FMixtormatLayer& Layer : Layers)
			{
				if (Layer.LayerId == Address.LayerId)
				{
					OwnerPtr = &Layer;
					OwnerStruct = FMixtormatLayer::StaticStruct();
					break;
				}
			}
		}
		else if (Child)
		{
			OwnerPtr = OwnerPointer(*Child);
			OwnerStruct = PayloadStructForChild(*Child);
		}
		if (!OwnerPtr || !OwnerStruct)
		{
			return false;
		}

		const FProperty* Property = OwnerStruct->FindPropertyByName(Address.Parameter);
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			OutValue = *Float->ContainerPtrToValuePtr<float>(OwnerPtr);
			return true;
		}
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			OutValue = *Int->ContainerPtrToValuePtr<int32>(OwnerPtr);
			return true;
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			OutValue = *Bool->ContainerPtrToValuePtr<bool>(OwnerPtr) ? 1.0 : 0.0;
			return true;
		}
		return false;
	}

	FText BoundText(const TOptional<float>& Bound, const FText& Unbounded)
	{
		return Bound.IsSet() ? FText::AsNumber(Bound.GetValue()) : Unbounded;
	}

	TSharedRef<SWidget> InfoRow(const FText& Label, const FText& Value, const FText& ToolTip = FText::GetEmpty())
	{
		return MixtormatRow::Make(
			Label,
			SNew(STextBlock).Text(Value),
			ToolTip);
	}
}

// Developer > Parameter Info: everything the tool knows about one parameter in one place. The
// point is never having to open four files to answer "what does this slider actually do".
TSharedRef<SWidget> SMixtormat::BuildParameterInfoPanel(const FMixtormatParameterAddress Target)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto AddInfo = [&Rows](const FText& Label, const FText& Value, const FText& ToolTip = FText::GetEmpty())
	{
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				InfoRow(Label, Value, ToolTip)
			];
	};

	AddInfo(LOCTEXT("DevInfoOwner", "Owner"),
		StaticEnum<EMixtormatParameterOwnerType>()->GetDisplayNameTextByValue(static_cast<int64>(Target.Owner)));
	AddInfo(LOCTEXT("DevInfoValueType", "Value Type"),
		StaticEnum<EMixtormatParameterValueType>()->GetDisplayNameTextByValue(static_cast<int64>(Target.ValueType)));

	const TOptional<FMixtormatParameterDefinitionKey> Key = MixtormatParameterUi::DefinitionKeyOf(Target);
	const FMixtormatParameterContract* Contract = Key.IsSet()
		? MixtormatParameterContracts::TryGet(Key.GetValue().Owner, Key.GetValue().Parameter)
		: nullptr;

	if (Key.IsSet())
	{
		TArray<FMixtormatShaderParamTag> ShaderTags;
		TArray<FString> ScannerErrors;
		MixtormatShaderParamScanner::Scan(ShaderTags, ScannerErrors);
		TSet<FString> SeenBindings;
		TArray<FString> Bindings;
		for (const FMixtormatShaderParamTag& ShaderTag : ShaderTags)
		{
			if (ShaderTag.Owner != Key.GetValue().Owner || ShaderTag.Parameter != Key.GetValue().Parameter)
			{
				continue;
			}
			const FString Binding = FString::Printf(
				TEXT("%s (%s)"), *ShaderTag.ShaderFile, *ShaderTag.UniformName);
			if (!SeenBindings.Contains(Binding))
			{
				SeenBindings.Add(Binding);
				Bindings.Add(Binding);
			}
		}
		if (!Bindings.IsEmpty())
		{
			FString BindingText;
			for (const FString& Binding : Bindings)
			{
				if (!BindingText.IsEmpty())
				{
					BindingText += TEXT("\n");
				}
				BindingText += Binding;
			}
			AddInfo(
				LOCTEXT("DevInfoBinding", "Binding"),
				FText::FromString(BindingText),
				LOCTEXT("DevInfoBindingHint", "Shader source and uniform discovered from the adjacent @param annotation."));
		}
	}

	if (double Authored = 0.0; TryReadAuthoredScalar(WorkingLayers, WorkingLayerGroups, Target, Authored))
	{
		const double Effective = Target.ValueType == EMixtormatParameterValueType::Float
			? GetEffectiveFloatParameter(Target, Authored)
			: GetEffectiveIntParameter(Target, static_cast<int32>(Authored));
		const bool bDriven = IsParameterDriven(Target);
		AddInfo(
			LOCTEXT("DevInfoCurrent", "Current"),
			FText::AsNumber(Effective),
			bDriven
				? LOCTEXT("DevInfoCurrentDriven", "Effective value: a spatial driver is modulating the authored number below.")
				: FText::GetEmpty());
	}

	if (Target.ValueType == EMixtormatParameterValueType::Enum)
	{
		const UEnum* Enum = Key.IsSet()
			? MixtormatParameterAuthoring::ResolveParameterEnum(Key.GetValue()) : nullptr;
		const FName CompiledName = Key.IsSet()
			? MixtormatParameterAuthoring::ResolveCompiledEnumDefault(Key.GetValue()) : NAME_None;
		const int64 CompiledValue = Enum
			? Enum->GetValueByNameString(CompiledName.ToString()) : INDEX_NONE;
		AddInfo(
			LOCTEXT("DevInfoDefault", "Compiled Default"),
			Enum && CompiledValue != INDEX_NONE
				? Enum->GetDisplayNameTextByValue(CompiledValue)
				: LOCTEXT("DevInfoNoDefinition", "no definition"),
			LOCTEXT("DevInfoDefaultHint", "The compiled reset value: the property's reflected enum initializer."));
	}
	else
	{
		AddInfo(
			LOCTEXT("DevInfoDefault", "Default"),
			Key.IsSet()
				? FText::AsNumber(MixtormatParameterUi::ResolveUiDefault(Key.GetValue(), 0.0f))
				: LOCTEXT("DevInfoNoDefinition", "no definition"),
			LOCTEXT("DevInfoDefaultHint", "The compiled reset value: the property's CDO initializer, read through reflection."));
	}


	if (Key.IsSet() && (MixtormatParameterAuthoring::HasShippedAuthoring(Key.GetValue())
		|| MixtormatParameterAuthoring::HasPendingAuthoring(Key.GetValue())))
	{
		if (Target.ValueType == EMixtormatParameterValueType::Enum)
		{
			const UEnum* Enum = MixtormatParameterAuthoring::ResolveParameterEnum(Key.GetValue());
			const FName DefaultName = MixtormatParameterAuthoring::ResolveAuthoringEnumDefault(Key.GetValue());
			const int64 DefaultValue = Enum
				? Enum->GetValueByNameString(DefaultName.ToString()) : INDEX_NONE;
			AddInfo(
				LOCTEXT("DevInfoAuthoringDefault", "Authoring Default"),
				Enum && DefaultValue != INDEX_NONE
					? Enum->GetDisplayNameTextByValue(DefaultValue)
					: LOCTEXT("DevInfoNoDefinition", "no definition"),
				LOCTEXT("DevInfoAuthoringDefaultHint", "The plugin authoring database's reset value -- what new instances and Reset use. Existing authored materials are untouched."));
		}
		else
		{
			AddInfo(
				LOCTEXT("DevInfoAuthoringDefault", "Authoring Default"),
				FText::AsNumber(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key.GetValue(), 0.0f)),
				LOCTEXT("DevInfoAuthoringDefaultHint", "The plugin authoring database's reset value -- what new instances and Reset use. Existing authored materials are untouched."));
		}
	}

	if (Target.ValueType != EMixtormatParameterValueType::Enum)
	{
	FMixtormatParameterUiResolution Shipped;
	const bool bShippedResolved = Key.IsSet()
		? MixtormatParameterUi::TryResolveUi(Key.GetValue(), Shipped)
		: false;
	const bool bSessionOverride = Key.IsSet() && MixtormatParameterUi::HasUiRangeOverride(Key.GetValue());
	const FText ShippedUi = bShippedResolved
		? FText::Format(LOCTEXT("DevInfoUiRange", "{0} .. {1}"),
			FText::AsNumber(Shipped.UiMin), FText::AsNumber(Shipped.UiMax))
		: LOCTEXT("DevInfoNoUiMeta", "literal (unmigrated)");
	if (Key.IsSet())
	{
		const float ActiveMin = MixtormatParameterAuthoring::ResolveAuthoringUiBound(
			Key.GetValue(), bShippedResolved ? Shipped.UiMin : 0.0f, false);
		const float ActiveMax = MixtormatParameterAuthoring::ResolveAuthoringUiBound(
			Key.GetValue(), bShippedResolved ? Shipped.UiMax : 1.0f, true);
		FText ActiveUi = FText::Format(LOCTEXT("DevInfoActiveUiRange", "{0} .. {1}"),
			FText::AsNumber(ActiveMin), FText::AsNumber(ActiveMax));
		if (bSessionOverride)
		{
			ActiveUi = FText::Format(LOCTEXT("DevInfoActiveUiOverridden", "{0} (session override)"), ActiveUi);
		}
		AddInfo(
			LOCTEXT("DevInfoActiveUi", "Active UI"),
			ActiveUi,
			LOCTEXT("DevInfoUiHint", "Drag range only. A typed value outside it is legal; only Hard Min/Max restrict."));
	}
	AddInfo(
		LOCTEXT("DevInfoShippedUi", "Shipped UI"),
		ShippedUi,
		LOCTEXT("DevInfoShippedUiHint", "The property's UIMin/UIMax annotation before any authoring-database or session change."));
	if (Key.IsSet())
	{
		const float ResolvedSnap = MixtormatParameterAuthoring::ResolveAuthoringSnap(
			Key.GetValue(), bShippedResolved ? Shipped.Snap : 0.0f);
		AddInfo(LOCTEXT("DevInfoSnap", "Snap"), FText::AsNumber(ResolvedSnap));
	}

	if (Contract)
	{
		AddInfo(
			LOCTEXT("DevInfoHard", "Hard"),
			FText::Format(LOCTEXT("DevInfoHardRange", "{0} .. {1}"),
				BoundText(Contract->HardMin, LOCTEXT("DevInfoUnbounded", "unbounded")),
				BoundText(Contract->HardMax, LOCTEXT("DevInfoUnbounded2", "unbounded"))),
			LOCTEXT("DevInfoHardHint", "Actual legal/runtime-safety bounds. The compositor clamps to these; nothing else does."));
		AddInfo(
			LOCTEXT("DevInfoNormalization", "Normalization"),
			FMath::IsNearlyEqual(Contract->NormalizationScale, 1.0f)
				? LOCTEXT("DevInfoNormalizationNone", "none")
				: FText::Format(LOCTEXT("DevInfoNormalizationScale", "value / {0}"),
					FText::AsNumber(Contract->NormalizationScale)),
			LOCTEXT("DevInfoNormalizationHint", "How the shader consumes the number. The stored value and the effective scale differ by this factor."));
		AddInfo(
			LOCTEXT("DevInfoSaturates", "Shader Saturates"),
			Contract->bShaderSaturates
				? LOCTEXT("DevInfoSaturatesYes", "yes")
				: LOCTEXT("DevInfoSaturatesNo", "no"),
			LOCTEXT("DevInfoSaturatesHint", "The shader saturates this value (or the channel it feeds) after binding, so values past the range are legal but stop changing the result."));
	}
	}

	MixtormatMenu::FBuilder Menu;
	Menu.Caption(FText::FromName(Target.Parameter))
		.Widget(SNew(SBox)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap,
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap))
			[
				SNew(SBox).WidthOverride(260.0f)
				[
					Rows
				]
			]);
	return Menu.Build();
}

// Developer > Override UI Range: a session-lifetime replacement for the drag range. Typed
// values already bypass any UI range, so this is for making the *scrub* reach where you are
// testing, not for unlocking new values.
TSharedRef<SWidget> SMixtormat::BuildParameterUiRangeOverridePanel(
	const FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	const TOptional<FMixtormatParameterDefinitionKey> Key = MixtormatParameterUi::DefinitionKeyOf(Target);
	if (!Key.IsSet())
	{
		Menu.Caption(LOCTEXT("DevOverrideCaption", "Override UI Range"))
			.Item(LOCTEXT("DevOverrideNoTarget", "Nothing selected"), nullptr, FSimpleDelegate()).Enabled(false);
		return Menu.Build();
	}

	const FMixtormatParameterDefinitionKey ParamKey = Key.GetValue();
	FMixtormatParameterUiResolution Shipped;
	const bool bShippedResolved = MixtormatParameterUi::TryResolveUi(ParamKey, Shipped);
	const float MetaMin = bShippedResolved ? Shipped.UiMin : 0.0f;
	const float MetaMax = bShippedResolved ? Shipped.UiMax : 1.0f;

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto AddBoundRow = [this, &Rows, ParamKey](
		const FText& Label,
		const float Fallback,
		const bool bMax,
		const float OtherFallback)
	{
		FMixtormatSliderRangeOptions Options;
		Options.bKeepPopupOpenOnCommit = true;
		Rows->AddSlot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				MakeSlider(
					Label,
					TAttribute<double>::CreateLambda([ParamKey, Fallback, bMax]() -> double
					{
						return MixtormatParameterUi::ResolveUiBound(ParamKey, Fallback, bMax);
					}),
					-1.0e6,
					1.0e6,
					static_cast<double>(Fallback),
					0.0,
					false,
					FMixtormatOnSliderValueChanged::CreateLambda(
						[ParamKey, bMax, OtherFallback](const double Value)
					{
						if (bMax)
						{
							MixtormatParameterUi::SetUiRangeOverride(
								ParamKey,
								MixtormatParameterUi::ResolveUiBound(ParamKey, OtherFallback, false),
								static_cast<float>(Value));
						}
						else
						{
							MixtormatParameterUi::SetUiRangeOverride(
								ParamKey,
								static_cast<float>(Value),
								MixtormatParameterUi::ResolveUiBound(ParamKey, OtherFallback, true));
						}
					}),
					FSimpleDelegate(),
					LOCTEXT("DevOverrideBoundHint", "Session-only. Typed values were never restricted by the UI range."),
					Options)
			];
	};
	AddBoundRow(LOCTEXT("DevOverrideMin", "Override Min"), MetaMin, false, MetaMax);
	AddBoundRow(LOCTEXT("DevOverrideMax", "Override Max"), MetaMax, true, MetaMin);

	Menu.Caption(LOCTEXT("DevOverrideCaption", "Override UI Range"))
		.Widget(SNew(SBox)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap,
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap))
			[
				SNew(SBox).WidthOverride(300.0f)
				[
					Rows
				]
			]);
	return Menu.Build();
}

// Developer > Edit Authoring Setup: live-preview editing of one parameter's persistent
// authoring setup. Every change lands in the unsaved pending set immediately (so the
// Inspector, reset and stripe all preview it) and reaches the plugin database only through
// Developer > Save to Plugin Defaults.
TSharedRef<SWidget> SMixtormat::BuildAuthoringSetupPanel(const FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	const TOptional<FMixtormatParameterDefinitionKey> KeyOpt = MixtormatParameterUi::DefinitionKeyOf(Target);
	if (!KeyOpt.IsSet() || !MixtormatParameterAuthoring::IsPersistentlyEditable(KeyOpt.GetValue()))
	{
		Menu.Caption(LOCTEXT("DevAuthoringCaption", "Edit Authoring Setup"))
			.Item(LOCTEXT("DevAuthoringNotOptedIn", "Parameter is not persistently editable"), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}
	const FMixtormatParameterDefinitionKey Key = KeyOpt.GetValue();
	const FMixtormatParameterContract* Contract = MixtormatParameterContracts::TryGet(
		Key.Owner, Key.Parameter);

	// The panel edits a COMPLETE entry: unset fields seed from the effective current values,
	// so what is saved is the whole setup, not a sparse diff.
	const auto EffectiveEntry = [Key]() -> FMixtormatParameterAuthoringEntry
	{
		FMixtormatParameterAuthoringEntry Entry;
		if (const FMixtormatParameterAuthoringEntry* Pending = MixtormatParameterAuthoring::TryGetPending(Key))
		{
			Entry = *Pending;
		}
		else if (const FMixtormatParameterAuthoringEntry* Shipped = MixtormatParameterAuthoring::TryGetShipped(Key))
		{
			Entry = *Shipped;
		}
		if (Key.ValueType == EMixtormatParameterValueType::Enum)
		{
			Entry.Default.Reset();
			Entry.UiMin.Reset();
			Entry.UiMax.Reset();
			Entry.Snap.Reset();
			Entry.ClampMin.Reset();
			Entry.ClampMax.Reset();
			Entry.DefaultEnum = MixtormatParameterAuthoring::ResolveAuthoringEnumDefault(Key);
		}
		else
		{
			if (!Entry.Default.IsSet())
			{
				Entry.Default = MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f);
			}
			if (!Entry.UiMin.IsSet())
			{
				Entry.UiMin = MixtormatParameterAuthoring::ResolveAuthoringUiBound(Key, 0.0f, false);
			}
			if (!Entry.UiMax.IsSet())
			{
				Entry.UiMax = MixtormatParameterAuthoring::ResolveAuthoringUiBound(Key, 1.0f, true);
			}
			if (!Entry.Snap.IsSet())
			{
				Entry.Snap = MixtormatParameterAuthoring::ResolveAuthoringSnap(Key, 0.0f);
			}
		}
		return Entry;
	};
	const auto Update = [Key, EffectiveEntry](const TFunctionRef<void(FMixtormatParameterAuthoringEntry&)>& Mutate)
	{
		FMixtormatParameterAuthoringEntry Entry = EffectiveEntry();
		Mutate(Entry);
		MixtormatParameterAuthoring::SetPendingAuthoring(Key, Entry);
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto AddInfo = [&Rows](const FText& Label, const FText& Value)
	{
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				MixtormatRow::Make(Label, SNew(STextBlock).Text(Value))
			];
	};
	// Numeric authoring row. The value reads back live from the pending entry, so a drag shows
	// what it wrote. The drag starts inside a soft range seeded from the parameter's current UI
	// range when the menu opened and grows by drag past either end, stopping at HardMin/HardMax
	// -- typing still passes anything. EffectiveEntry/Update/Read/Mutate are captured BY VALUE:
	// the sliders outlive this function's stack frame for as long as the menu is open.
	const auto AddEditRow = [this, &Rows, EffectiveEntry, Update](
		const FText& Label,
		const TFunction<float(const FMixtormatParameterAuthoringEntry&)> Read,
		const TFunction<void(FMixtormatParameterAuthoringEntry&, float)> Mutate,
		const double SoftMin,
		const double SoftMax,
		const TAttribute<double> HardMin,
		const TAttribute<double> HardMax,
		const bool bInteger)
	{
		const double Initial = static_cast<double>(Read(EffectiveEntry()));
		FMixtormatSliderRangeOptions Options;
		Options.bExpandable = true;
		Options.HardMin = HardMin;
		Options.HardMax = HardMax;
		Options.bKeepPopupOpenOnCommit = true;
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				MakeSlider(
					Label,
					TAttribute<double>::CreateLambda([EffectiveEntry, Read]()
					{
						return static_cast<double>(Read(EffectiveEntry()));
					}),
					FMath::Min(SoftMin, Initial),
					FMath::Max(SoftMax, Initial),
					Initial,
					0.0,
					bInteger,
					FMixtormatOnSliderValueChanged::CreateLambda(
						[Update, Mutate](const double NewValue)
					{
						Update([&Mutate, NewValue](FMixtormatParameterAuthoringEntry& Entry)
						{
							Mutate(Entry, static_cast<float>(NewValue));
						});
					}),
					FSimpleDelegate(),
					LOCTEXT("DevAuthoringRowHint",
						"Live preview. Drag past an end to widen the range. Unsaved until Save to Plugin Defaults."),
					Options)
			];
	};

	const FMixtormatParameterAuthoringEntry Current = EffectiveEntry();

	// Label override. Presentation only: the property FName, binding address, serialization
	// and shader uniforms are untouched.
	Rows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
		[
			MixtormatRow::Make(
				LOCTEXT("DevAuthoringLabel", "Label"),
				SNew(SMixtormatTextFieldGradient)
				[
					SNew(SBox)
					.MinDesiredHeight(FMixtormatThemeStore::GetTheme().TextField.MinHeight)
					.MinDesiredWidth(FMixtormatThemeStore::GetTheme().TextField.MinWidth)
					[
						SNew(SEditableTextBox)
						.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
						.Text(FText::FromString(Current.Label))
						.ClearKeyboardFocusOnCommit(false)
						.HintText(LOCTEXT("DevAuthoringLabelHint", "Display label override"))
						.OnTextChanged(FOnTextChanged::CreateLambda([Update](const FText& Text)
						{
							Update([&Text](FMixtormatParameterAuthoringEntry& Entry)
							{
								Entry.Label = Text.ToString();
							});
						}))
				]
				])
		];


	if (Key.ValueType == EMixtormatParameterValueType::Enum)
	{
		const UEnum* Enum = MixtormatParameterAuthoring::ResolveParameterEnum(Key);
		Rows->AddSlot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				MixtormatRow::Make(
					LOCTEXT("DevAuthoringDefault", "Default/Reset"),
					MixtormatRow::MakeChip(
						TAttribute<FText>::CreateLambda([Enum, EffectiveEntry]()
						{
							const FMixtormatParameterAuthoringEntry Entry = EffectiveEntry();
						const int64 Value = Enum
							? Enum->GetValueByNameString(Entry.DefaultEnum.Get(NAME_None).ToString())
							: INDEX_NONE;
						return Enum && Value != INDEX_NONE
							? Enum->GetDisplayNameTextByValue(Value)
							: FText::GetEmpty();
						}),
						FOnGetContent::CreateLambda([this, Enum, Update, EffectiveEntry]()
						{
							return BuildEnumMenu(
								Enum,
								[EffectiveEntry, Enum]() -> int64
								{
									const FMixtormatParameterAuthoringEntry Entry = EffectiveEntry();
									return Enum
										? Enum->GetValueByNameString(Entry.DefaultEnum.Get(NAME_None).ToString())
										: INDEX_NONE;
								},
								[Update, Enum](const int64 Value)
								{
									if (!Enum || Value == INDEX_NONE)
									{
										return;
									}
									FString EntryName = Enum->GetNameByValue(Value).ToString();
									const int32 Separator = EntryName.Find(TEXT("::"));
									if (Separator != INDEX_NONE)
									{
										EntryName.RightChopInline(Separator + 2);
									}
									Update([EntryName](FMixtormatParameterAuthoringEntry& Entry)
									{
										Entry.DefaultEnum = FName(*EntryName);
									});
								},
								FSimpleDelegate());
						})))
			];
	}
	else
	{
		// Soft ranges seeded from the UI range as the menu opened: the default scrubs inside it,
		// each end scrubs one span outward from where it is, snap within a tenth of the span.
		const double UiLow = static_cast<double>(Current.UiMin.GetValue());
		const double UiHigh = static_cast<double>(Current.UiMax.GetValue());
		const double Span = FMath::Max(FMath::Abs(UiHigh - UiLow), 1.0e-3);
		// The back-end clamp as it resolves right now (pending edit included), so editing it below
		// immediately bounds the Default and UI range rows too.
		const auto ClampAttribute = [Key](const bool bMax)
		{
			return TAttribute<double>::CreateLambda([Key, bMax]() -> double
			{
				const TOptional<float> Clamp = MixtormatParameterAuthoring::ResolveAuthoringClamp(Key, bMax);
				return Clamp.IsSet()
					? static_cast<double>(Clamp.GetValue())
					: (bMax ? UE_BIG_NUMBER : -UE_BIG_NUMBER);
			});
		};
		const TAttribute<double> HardLow = ClampAttribute(false);
		const TAttribute<double> HardHigh = ClampAttribute(true);
		const bool bIntegerKey = Key.ValueType == EMixtormatParameterValueType::Int;
	Rows->AddSlot().AutoHeight()[MixtormatRow::MakeCaption(LOCTEXT("DevAuthoringVisual", "Visual"))];
	AddEditRow(LOCTEXT("DevAuthoringDefault", "Default/Reset"),
		[](const FMixtormatParameterAuthoringEntry& Entry) { return Entry.Default.Get(0.0f); },
		[](FMixtormatParameterAuthoringEntry& Entry, const float Value) { Entry.Default = Value; },
		UiLow, UiHigh, HardLow, HardHigh, bIntegerKey);
	AddEditRow(LOCTEXT("DevAuthoringUiMin", "UI Min"),
		[](const FMixtormatParameterAuthoringEntry& Entry) { return Entry.UiMin.Get(0.0f); },
		[](FMixtormatParameterAuthoringEntry& Entry, const float Value) { Entry.UiMin = Value; },
		UiLow - Span, UiHigh, HardLow, HardHigh, bIntegerKey);
	AddEditRow(LOCTEXT("DevAuthoringUiMax", "UI Max"),
		[](const FMixtormatParameterAuthoringEntry& Entry) { return Entry.UiMax.Get(1.0f); },
		[](FMixtormatParameterAuthoringEntry& Entry, const float Value) { Entry.UiMax = Value; },
		UiLow, UiHigh + Span, HardLow, HardHigh, bIntegerKey);
	AddEditRow(LOCTEXT("DevAuthoringSnap", "Snap"),
		[](const FMixtormatParameterAuthoringEntry& Entry) { return Entry.Snap.Get(0.0f); },
		[](FMixtormatParameterAuthoringEntry& Entry, const float Value) { Entry.Snap = Value; },
		0.0, Span * 0.1, 0.0, UE_BIG_NUMBER, false);

	// Back-end clamp: editor-only limits for drags, typed values and range growth. A checkbox
	// turns each end on; off is unlimited (or the runtime contract's Hard bound, if it has one).
	// Never a shader or gather clamp.
	Rows->AddSlot().AutoHeight()[MixtormatRow::MakeCaption(LOCTEXT("DevAuthoringClamp", "Back-end Clamp"))];
	const auto AddClampRow = [this, &Rows, Key, Update, UiLow, UiHigh, Span, bIntegerKey](
		const FText& Label, const bool bMax)
	{
		const auto IsSet = [Key, bMax]()
		{
			return MixtormatParameterAuthoring::ResolveAuthoringClamp(Key, bMax).IsSet();
		};
		const auto Current = [Key, bMax, UiLow, UiHigh]() -> double
		{
			const TOptional<float> Clamp = MixtormatParameterAuthoring::ResolveAuthoringClamp(Key, bMax);
			return Clamp.IsSet() ? static_cast<double>(Clamp.GetValue()) : (bMax ? UiHigh : UiLow);
		};
		const auto Write = [Update, bMax](const TOptional<float> Value)
		{
			Update([bMax, Value](FMixtormatParameterAuthoringEntry& Entry)
			{
				(bMax ? Entry.ClampMax : Entry.ClampMin) = Value;
			});
		};
		FMixtormatSliderRangeOptions Options;
		Options.bExpandable = true;
		Options.bKeepPopupOpenOnCommit = true;
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([IsSet]()
						{
							return IsSet() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([Write, Current](const ECheckBoxState State)
						{
							Write(State == ECheckBoxState::Checked
								? TOptional<float>(static_cast<float>(Current()))
								: TOptional<float>());
						}),
						LOCTEXT("DevAuthoringClampToggle", "Off = unlimited."))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SBox)
					.IsEnabled_Lambda(IsSet)
					[
						MakeSlider(
							Label,
							TAttribute<double>::CreateLambda(Current),
							bMax ? UiLow : UiLow - Span,
							bMax ? UiHigh + Span : UiHigh,
							bMax ? UiHigh : UiLow,
							0.0,
							bIntegerKey,
							FMixtormatOnSliderValueChanged::CreateLambda([Write](const double Value)
							{
								Write(static_cast<float>(Value));
							}),
							FSimpleDelegate(),
							LOCTEXT("DevAuthoringClampHint",
								"Editor clamp: drags and typed values stop here and the slider range never grows past it. Never applied in shaders."),
							Options)
					]
				]
			];
	};
	AddClampRow(LOCTEXT("DevAuthoringClampMin", "Clamp Min"), false);
	AddClampRow(LOCTEXT("DevAuthoringClampMax", "Clamp Max"), true);
	}

	FString ShaderInfo = FString::Printf(
		TEXT("normalization: %s | saturates: %s"),
		Contract && !FMath::IsNearlyEqual(Contract->NormalizationScale, 1.0f)
			? *FString::Printf(TEXT("/%g"), Contract->NormalizationScale)
			: TEXT("none"),
		Contract && Contract->bShaderSaturates ? TEXT("yes") : TEXT("no"));
	AddInfo(LOCTEXT("DevAuthoringShader", "Shader"), FText::FromString(ShaderInfo));

	Menu.Caption(LOCTEXT("DevAuthoringCaption", "Edit Authoring Setup"))
		.Widget(SNew(SBox)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap,
				FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap))
			[
				SNew(SBox).WidthOverride(320.0f)
				[
					Rows
				]
			]);
	return Menu.Build();
}

void SMixtormat::CopyParameterAddress(const FMixtormatParameterAddress Target)
{
	if (!Target.IsValid())
	{
		return;
	}
	const FString Text = FString::Printf(
		TEXT("Owner=%s Parameter=%s ValueType=%s Layer=%s Child=%s"),
		*StaticEnum<EMixtormatParameterOwnerType>()->GetNameByValue(static_cast<int64>(Target.Owner)).ToString(),
		*Target.Parameter.ToString(),
		*StaticEnum<EMixtormatParameterValueType>()->GetNameByValue(static_cast<int64>(Target.ValueType)).ToString(),
		*Target.LayerId.ToString(),
		*Target.ChildId.ToString());
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

TSharedRef<SWidget> SMixtormat::BuildParameterDriverPopoverFor(
	TFunction<FMixtormatParameterAddress()> ResolveTarget)
{
	return BuildParameterDriverPopover(ResolveTarget());
}

TSharedRef<SWidget> SMixtormat::BuildEnumMenu(
	const UEnum* Enum,
	TFunction<int64()> ActiveValue,
	TFunction<void(int64)> WriteValue,
	const FSimpleDelegate& AfterWrite)
{
	MixtormatMenu::FBuilder Menu;
	if (!Enum)
	{
		return Menu.Build();
	}

	for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
	{
		const int64 Value = Enum->GetValueByIndex(Index);
		if (Value == INDEX_NONE || Enum->HasMetaData(TEXT("Hidden"), Index))
		{
			continue;
		}

		Menu.Item(
			Enum->GetDisplayNameTextByIndex(Index),
			nullptr,
			FSimpleDelegate::CreateLambda([WriteValue, AfterWrite, Value]()
			{
				WriteValue(Value);
				AfterWrite.ExecuteIfBound();
			}))
			.Checked(TAttribute<bool>::CreateLambda([ActiveValue, Value]()
			{
				return ActiveValue() == Value;
			}));
	}
	return Menu.Build();
}

int64 SMixtormat::GetEffectiveEnumParameter(
	const FMixtormatParameterAddress& Target,
	const int64 LocalValue) const
{
	int64 Value = LocalValue;
	return Target.IsValid() && MixtormatParameterBinding::TryResolveEnum({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target, Value)
		? Value
		: LocalValue;
}

double SMixtormat::GetEffectiveFloatParameter(
	const FMixtormatParameterAddress& Target,
	const double LocalValue) const
{
	float Value = static_cast<float>(LocalValue);
	return Target.IsValid() && MixtormatParameterBinding::TryResolveFloat({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target, Value)
		? static_cast<double>(Value)
		: LocalValue;
}

int32 SMixtormat::GetEffectiveIntParameter(
	const FMixtormatParameterAddress& Target,
	const int32 LocalValue) const
{
	int32 Value = LocalValue;
	return Target.IsValid() && MixtormatParameterBinding::TryResolveInt({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target, Value)
		? Value
		: LocalValue;
}

bool SMixtormat::GetEffectiveBoolParameter(
	const FMixtormatParameterAddress& Target,
	const bool LocalValue) const
{
	bool Value = LocalValue;
	return Target.IsValid() && MixtormatParameterBinding::TryResolveBool({WorkingLayers, WorkingLayerGroups, WorkingSources}, Target, Value)
		? Value
		: LocalValue;
}

void SMixtormat::SetDriverSource(
	FMixtormatParameterAddress Target,
	FGuid SourceLayerId,
	FGuid SourceChildId,
	const EMixtormatDriverSourceKind SourceKind,
	FName SourceOutput)
{
	if (Target.ValueType != EMixtormatParameterValueType::Float)
	{
		return;
	}
	if (SourceKind != EMixtormatDriverSourceKind::None
		&& WorkingSources.ContainsByPredicate([&Target](const FMixtormatSourceEntry& Entry)
			{ return Entry.SourceId == Target.LayerId; }))
	{
		return;
	}
	if (Target.Owner == EMixtormatParameterOwnerType::StructuralWarpFlow
		&& SourceKind != EMixtormatDriverSourceKind::None)
	{
		if (SourceKind != EMixtormatDriverSourceKind::CombinedMask || SourceChildId.IsValid()) { return; }
		const int32 TargetIndex = WorkingLayers.IndexOfByPredicate([&Target](const FMixtormatLayer& Layer)
			{ return Layer.LayerId == Target.LayerId; });
		const int32 SourceIndex = WorkingLayers.IndexOfByPredicate([&SourceLayerId](const FMixtormatLayer& Layer)
			{ return Layer.LayerId == SourceLayerId && Layer.bEnabled; });
		if (SourceIndex == INDEX_NONE || TargetIndex == INDEX_NONE || SourceIndex >= TargetIndex) { return; }
	}
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, true))
	{
		Binding->Reference.bEnabled = false;
		Binding->Driver.bEnabled = SourceKind != EMixtormatDriverSourceKind::None;
		Binding->Driver.SourceLayerId = SourceLayerId;
		Binding->Driver.SourceChildId = SourceChildId;
		Binding->Driver.SourceKind = SourceKind;
		Binding->Driver.SourceOutput = SourceOutput;
		RefreshLayeredPreview();
	}
}

void SMixtormat::SetDriverCombine(
	FMixtormatParameterAddress Target,
	const EMixtormatDriverCombineMode Combine)
{
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, true))
	{
		Binding->Driver.Combine = Combine;
		RefreshLayeredPreview();
	}
}

void SMixtormat::SetDriverEnabled(FMixtormatParameterAddress Target, const bool bEnabled)
{
	if (bEnabled && WorkingSources.ContainsByPredicate([&Target](const FMixtormatSourceEntry& Entry)
		{ return Entry.SourceId == Target.LayerId; })) { return; }
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, bEnabled))
	{
		if (bEnabled && Target.Owner == EMixtormatParameterOwnerType::StructuralWarpFlow)
		{
			const int32 TargetIndex = WorkingLayers.IndexOfByPredicate([&Target](const FMixtormatLayer& Layer)
				{ return Layer.LayerId == Target.LayerId; });
			const int32 SourceIndex = WorkingLayers.IndexOfByPredicate([Binding](const FMixtormatLayer& Layer)
				{ return Layer.LayerId == Binding->Driver.SourceLayerId && Layer.bEnabled; });
			if (Target.ValueType != EMixtormatParameterValueType::Float
				|| (Target.Parameter != FName(TEXT("FlowAmount"))
					&& Target.Parameter != FName(TEXT("FlowTraceLength")))
				|| Binding->Driver.SourceKind != EMixtormatDriverSourceKind::CombinedMask
				|| Binding->Driver.SourceChildId.IsValid()
				|| SourceIndex == INDEX_NONE || TargetIndex == INDEX_NONE || SourceIndex >= TargetIndex)
			{
				return;
			}
		}
		Binding->Driver.bEnabled = bEnabled && Binding->Driver.SourceKind != EMixtormatDriverSourceKind::None;
		if (Binding->Driver.bEnabled)
		{
			Binding->Reference.bEnabled = false;
		}
		RefreshLayeredPreview();
	}
}

void SMixtormat::ClearParameterDriver(FMixtormatParameterAddress Target)
{
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, false))
	{
		Binding->Driver = FMixtormatParameterDriver{};
		RefreshLayeredPreview();
	}
}

TSharedRef<SWidget> SMixtormat::BuildDriverSourceMenu(FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("DriverSources", "Source"));
	Menu.Item(
		LOCTEXT("DriverSourceNone", "None"),
		nullptr,
		FSimpleDelegate::CreateSP(
			this, &SMixtormat::SetDriverSource, Target, FGuid{}, FGuid{}, EMixtormatDriverSourceKind::None, FName()));

	const bool bStructural = Target.Owner == EMixtormatParameterOwnerType::StructuralWarpFlow;
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		// A structural module can read only a completed mask from an earlier
		// enabled layer; never offer a later, self or child-level source.
		if (bStructural && Layer.LayerId == Target.LayerId) { break; }
		if (bStructural && !Layer.bEnabled) { continue; }
		Menu.Item(
			FText::Format(LOCTEXT("DriverLayerMaskSource", "{0} / Layer Mask"), Layer.DisplayName),
			nullptr,
			FSimpleDelegate::CreateSP(
				this,
				&SMixtormat::SetDriverSource,
				Target,
				Layer.LayerId,
				FGuid{},
				EMixtormatDriverSourceKind::CombinedMask,
				FName(TEXT("Mask"))));

		if (bStructural) { continue; }
		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			EMixtormatDriverSourceKind Kind = EMixtormatDriverSourceKind::None;
			FName Output = NAME_None;
			switch (Child.Type)
			{
			case EMixtormatLayerChildType::Filter:
			case EMixtormatLayerChildType::PatternId:

			case EMixtormatLayerChildType::IdGroup:
				Kind = EMixtormatDriverSourceKind::RegionIds;
				Output = FName(TEXT("RandomPerId"));
				break;
			case EMixtormatLayerChildType::Mask:
			case EMixtormatLayerChildType::Generated:
			case EMixtormatLayerChildType::Craquelure:
			case EMixtormatLayerChildType::ColorId:
			case EMixtormatLayerChildType::RandomId:
				Kind = EMixtormatDriverSourceKind::ChildMask;
				Output = FName(TEXT("Mask"));
				break;
			default:
				break;
			}
			if (Kind == EMixtormatDriverSourceKind::None)
			{
				continue;
			}
			Menu.Item(
				FText::Format(
					LOCTEXT("DriverChildSource", "{0} / {1}"),
					Layer.DisplayName,
					GetLayerChildName(Child)),
				nullptr,
				FSimpleDelegate::CreateSP(
					this,
					&SMixtormat::SetDriverSource,
					Target,
					Layer.LayerId,
					Child.ChildId,
					Kind,
					Output));
		}
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildDriverOutputMenu(FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
	if (!Binding || Binding->Driver.SourceKind == EMixtormatDriverSourceKind::None)
	{
		Menu.Item(LOCTEXT("DriverNoOutput", "No source selected"), nullptr, FSimpleDelegate()).Enabled(false);
		return Menu.Build();
	}
	if (Binding->Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds)
	{
		Menu.Item(
			LOCTEXT("DriverRandomPerId", "Random Per ID"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Target]()
			{
				if (FMixtormatParameterBinding* Mutable = FindParameterBinding(Target, false))
				{
					Mutable->Driver.IdMapping = EMixtormatIdDriverMapping::RandomPerId;
					Mutable->Driver.SourceOutput = FName(TEXT("RandomPerId"));
					RefreshLayeredPreview();
				}
			}));
	}
	else
	{
		Menu.Item(LOCTEXT("DriverMaskOutput", "Mask"), nullptr, FSimpleDelegate()).Checked(true);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildDriverCombineMenu(FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatDriverCombineMode Modes[] = {
		EMixtormatDriverCombineMode::Replace,
		EMixtormatDriverCombineMode::Multiply,
		EMixtormatDriverCombineMode::Add,
		EMixtormatDriverCombineMode::Subtract,
		EMixtormatDriverCombineMode::Min,
		EMixtormatDriverCombineMode::Max,
		EMixtormatDriverCombineMode::Lerp
	};
	for (const EMixtormatDriverCombineMode Mode : Modes)
	{
		Menu.Item(
			DriverCombineText(Mode),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::SetDriverCombine, Target, Mode));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildParameterDriverPopover(FMixtormatParameterAddress Target)
{
	if (WorkingSources.ContainsByPredicate([&Target](const FMixtormatSourceEntry& Entry)
		{ return Entry.SourceId == Target.LayerId; }))
	{
		MixtormatMenu::FBuilder Menu;
		Menu.Caption(LOCTEXT("DriverShelfUnavailable", "Drivers"))
			.Item(LOCTEXT("DriverShelfUnavailableMessage", "Shelf parameter drivers need a published signal consumer."), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}
	if (Target.Owner == EMixtormatParameterOwnerType::StructuralWarpFlow
		&& Target.Parameter != FName(TEXT("FlowAmount"))
		&& Target.Parameter != FName(TEXT("FlowTraceLength")))
	{
		MixtormatMenu::FBuilder Menu;
		Menu.Caption(LOCTEXT("DriverWarpUnavailable", "Drivers"))
			.Item(LOCTEXT("DriverWarpUnavailableMessage", "Only Flow Amount and Trace Length support mask drivers."), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}
	if (Target.ValueType != EMixtormatParameterValueType::Float)
	{
		MixtormatMenu::FBuilder Menu;
		Menu.Caption(LOCTEXT("DriverScalarOnly", "Drivers"))
			.Item(LOCTEXT("DriverScalarOnlyMessage", "Spatial Drivers currently require a float parameter."), nullptr, FSimpleDelegate())
			.Enabled(false);
		return Menu.Build();
	}

	auto SourceText = [this, Target]() -> FText
	{
		const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
		if (!Current || Current->Driver.SourceKind == EMixtormatDriverSourceKind::None)
		{
			return LOCTEXT("DriverSourceNoneText", "None");
		}
		for (const FMixtormatLayer& Layer : WorkingLayers)
		{
			if (Layer.LayerId != Current->Driver.SourceLayerId)
			{
				continue;
			}
			if (!Current->Driver.SourceChildId.IsValid())
			{
				return FText::Format(LOCTEXT("DriverLayerMaskText", "{0} / Layer Mask"), Layer.DisplayName);
			}
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				if (Child.ChildId == Current->Driver.SourceChildId)
				{
					return FText::Format(
						LOCTEXT("DriverChildText", "{0} / {1}"), Layer.DisplayName, GetLayerChildName(Child));
				}
			}
		}
		return LOCTEXT("DriverMissingSourceText", "Missing Source");
	};

	auto OutputText = [this, Target]() -> FText
	{
		const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
		if (!Current || Current->Driver.SourceKind == EMixtormatDriverSourceKind::None)
		{
			return LOCTEXT("DriverOutputNone", "None");
		}
		return Current->Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds
			? LOCTEXT("DriverOutputRandomPerId", "Random Per ID")
			: LOCTEXT("DriverOutputMask", "Mask");
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	auto AddRow = [&Rows](const TSharedRef<SWidget>& Row)
	{
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.DriverPopoverInnerGap)
			[
				Row
			];
	};

	AddRow(MixtormatRow::Make(
		LOCTEXT("DriverSourceLabel", "Source"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda(SourceText),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildDriverSourceMenu, Target),
			nullptr,
			LOCTEXT("DriverSourceHint", "Choose a mask or Region ID producer."))));

	AddRow(MixtormatRow::Make(
		LOCTEXT("DriverOutputLabel", "Output"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda(OutputText),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildDriverOutputMenu, Target))));

	// Only meaningful when the signal comes from a region map: they shape the value one region
	// draws, before the chain's own remap touches it. Hidden rather than disabled for a mask
	// source, where there is no region to draw for.
	auto IsRegionSource = [this, Target]()
	{
		const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
		return Current && Current->Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds
			? EVisibility::Visible
			: EVisibility::Collapsed;
	};

	auto AddRegionRow = [&AddRow, &IsRegionSource](const TSharedRef<SWidget>& Row)
	{
		TSharedRef<SWidget> Wrapped = SNew(SBox)
			.Visibility_Lambda(IsRegionSource)
			[
				Row
			];
		AddRow(Wrapped);
	};

	AddRegionRow(MakeSlider(
		LOCTEXT("DriverIdSeed", "Seed"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? static_cast<double>(Current->Driver.Seed) : 1.0;
		}),
		1.0, 999.0, 1.0, 1.0, true,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.Seed = FMath::RoundToInt(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.Seed = 1;
				RefreshLayeredPreview();
			}
		})));

	AddRegionRow(MakeSlider(
		LOCTEXT("DriverIdRandomMin", "Min"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.IdRandomMin : 0.0;
		}),
		0.0, 1.0, 0.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.IdRandomMin = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.IdRandomMin = 0.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRegionRow(MakeSlider(
		LOCTEXT("DriverIdRandomMax", "Max"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.IdRandomMax : 1.0;
		}),
		0.0, 1.0, 1.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.IdRandomMax = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.IdRandomMax = 1.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MixtormatRow::Make(
		LOCTEXT("DriverCombineLabel", "Combine"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this, Target]()
			{
				const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
				return DriverCombineText(Current ? Current->Driver.Combine : EMixtormatDriverCombineMode::Multiply);
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildDriverCombineMenu, Target))));

	AddRow(MakeSlider(
		LOCTEXT("DriverAmount", "Amount"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.Amount : 1.0;
		}),
		0.0, 1.0, 1.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.Amount = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.Amount = 1.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MakeSlider(
		LOCTEXT("DriverInputMin", "In Min"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.InputMin : 0.0;
		}),
		0.0, 1.0, 0.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.InputMin = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.InputMin = 0.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MakeSlider(
		LOCTEXT("DriverInputMax", "In Max"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.InputMax : 1.0;
		}),
		0.0, 1.0, 1.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.InputMax = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.InputMax = 1.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MakeSlider(
		LOCTEXT("DriverOutputMin", "Out Min"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.OutputMin : 0.0;
		}),
		-2.0, 2.0, 0.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.OutputMin = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.OutputMin = 0.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MakeSlider(
		LOCTEXT("DriverOutputMax", "Out Max"),
		TAttribute<double>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
			return Current ? Current->Driver.OutputMax : 1.0;
		}),
		-2.0, 2.0, 1.0, 0.01, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this, Target](const double Value)
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.OutputMax = static_cast<float>(Value);
				RefreshLayeredPreview();
			}
		}),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
			{
				Current->Driver.OutputMax = 1.0f;
				RefreshLayeredPreview();
			}
		})));

	AddRow(MixtormatRow::MakeTrailing(
		LOCTEXT("DriverInvert", "Invert"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this, Target]()
			{
				const FMixtormatParameterBinding* Current = FindParameterBinding(Target);
				return Current && Current->Driver.bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this, Target](const ECheckBoxState State)
			{
				if (FMixtormatParameterBinding* Current = FindParameterBinding(Target, true))
				{
					Current->Driver.bInvert = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			}))));

	Rows->AddSlot().AutoHeight()[
		MixtormatRow::MakeTrailing(
			LOCTEXT("DriverEnabled", "Enabled"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Target]()
				{
					return IsParameterDriven(Target) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Target](const ECheckBoxState State)
				{
					SetDriverEnabled(Target, State == ECheckBoxState::Checked);
				})))
	];

	return SNew(SMixtormatDriverPopover)
		.Title(FText::Format(LOCTEXT("DriverPopoverTitle", "Drive {0}"), FText::FromName(Target.Parameter)))
		[
			Rows
		];
}

#undef LOCTEXT_NAMESPACE
