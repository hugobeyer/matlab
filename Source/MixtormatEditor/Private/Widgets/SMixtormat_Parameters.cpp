#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

#include "MixtormatParameterBinding.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Parameters/SMixtormatDriverPopover.h"
#include "UI/Parameters/SMixtormatParameterControl.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

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
		return EMixtormatParameterValueType::Float;
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
		default: return EMixtormatParameterOwnerType::Layer;
		}
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

	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (OwnerStruct == FMixtormatLayer::StaticStruct() && Owner == &Layer)
		{
			Result.LayerId = Layer.LayerId;
			Result.Owner = EMixtormatParameterOwnerType::Layer;
			return Result;
		}
		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			if (Owner == OwnerPointer(Child))
			{
				Result.LayerId = Layer.LayerId;
				Result.ChildId = Child.ChildId;
				Result.Owner = OwnerTypeForChild(Child);
				return Result;
			}
			if (OwnerStruct == FMixtormatMaskShaping::StaticStruct())
			{
				const void* Shaping = nullptr;
				if (Child.Type == EMixtormatLayerChildType::Mask) Shaping = &Child.Mask.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::Craquelure) Shaping = &Child.Craquelure.Shaping;
				else if (Child.Type == EMixtormatLayerChildType::RandomId) Shaping = &Child.RandomId.Shaping;
				if (Owner == Shaping)
				{
					Result.LayerId = Layer.LayerId;
					Result.ChildId = Child.ChildId;
					Result.Owner = EMixtormatParameterOwnerType::MaskShaping;
					return Result;
				}
			}
		}
	}
	Result.Parameter = NAME_None;
	return Result;
}

FMixtormatParameterBinding* SMixtormat::FindParameterBinding(
	const FMixtormatParameterAddress& Target,
	const bool bCreate)
{
	if (!Target.IsValid())
	{
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
				if (Child.ChildId == Target.ChildId && OwnerTypeForChild(Child) == Target.Owner)
				{
					Bindings = &Child.ParameterBindings;
					break;
				}
			}
		}
		if (!Bindings)
		{
			return nullptr;
		}
		FMixtormatParameterBinding* Binding = Bindings->FindByPredicate([&Target](const FMixtormatParameterBinding& Item)
		{
			return Item.DestinationOwner == Target.Owner && Item.DestinationParameter == Target.Parameter;
		});
		if (!Binding && bCreate)
		{
			Binding = &Bindings->AddDefaulted_GetRef();
			Binding->DestinationOwner = Target.Owner;
			Binding->DestinationParameter = Target.Parameter;
			Binding->ValueType = Target.ValueType;
			Binding->TypeName = Target.TypeName;
		}
		else if (Binding && (Binding->ValueType != Target.ValueType || Binding->TypeName != Target.TypeName))
		{
			Binding->ValueType = Target.ValueType;
			Binding->TypeName = Target.TypeName;
			Binding->Reference = FMixtormatParameterReference{};
			Binding->Driver = FMixtormatParameterDriver{};
		}
		return Binding;
	}
	return nullptr;
}

const FMixtormatParameterBinding* SMixtormat::FindParameterBinding(const FMixtormatParameterAddress& Target) const
{
	if (!Target.IsValid())
	{
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
				if (Child.ChildId == Target.ChildId && OwnerTypeForChild(Child) == Target.Owner)
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
		&& !MixtormatParameterBinding::IsReferenceSourceValid(WorkingLayers, Binding->Reference.Source);
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
		&& MixtormatParameterBinding::IsReferenceSourceValid(WorkingLayers, ParameterReferenceClipboard.GetValue())
		&& !WouldCreateParameterReferenceCycle(Target, ParameterReferenceClipboard.GetValue());
}

void SMixtormat::CopyParameterReference(FMixtormatParameterAddress Source)
{
	if (MixtormatParameterBinding::IsReferenceSourceValid(WorkingLayers, Source))
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
		MixtormatParameterBinding::ResolveLinkTarget(WorkingLayers, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteFloat(WorkingLayers, Authority, Value);
}

bool SMixtormat::TryWriteLinkedInt(const FMixtormatParameterAddress& Target, const int32 Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget(WorkingLayers, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteInt(WorkingLayers, Authority, Value);
}

bool SMixtormat::TryWriteLinkedBool(const FMixtormatParameterAddress& Target, const bool Value)
{
	const FMixtormatParameterAddress Authority =
		MixtormatParameterBinding::ResolveLinkTarget(WorkingLayers, Target);
	return Authority.IsValid()
		&& MixtormatParameterBinding::TryWriteBool(WorkingLayers, Authority, Value);
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
			Style.GetBrush(TEXT("Mixtormat.Icon.Duplicate")),
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
			Style.GetBrush(TEXT("Mixtormat.Icon.ArrowUp")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::GoToParameterReferenceSource, Target))
		.Enabled(TAttribute<bool>::CreateLambda([this, Target]()
		{
			const FMixtormatParameterBinding* Binding = FindParameterBinding(Target);
			return Binding
				&& Binding->Reference.bEnabled
				&& MixtormatParameterBinding::IsReferenceSourceValid(WorkingLayers, Binding->Reference.Source);
		}))
		.Separator()
		// Which way the reference runs, as the two words themselves rather than a lock glyph
		// whose closed state could mean either. Only meaningful once there is a reference, so
		// it is disabled rather than hidden -- same reasoning as the rows above it.
		.Caption(LOCTEXT("ParameterReferenceModeCaption", "Reference"))
		.Widget(
			SNew(SBox)
			.Padding(FMargin(
				MixtormatTokens::MenuItemInset,
				MixtormatTokens::DriverPopoverInnerGap,
				MixtormatTokens::MenuItemInset,
				MixtormatTokens::DriverPopoverInnerGap))
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
		.Enabled(Target.IsValid());

	if (IsParameterDriven(Target))
	{
		Menu.Item(
			LOCTEXT("ClearParameterDriver", "Clear Driver"),
			nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormat::ClearParameterDriver, Target));
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

TSharedRef<SWidget> SMixtormat::BuildParameterDriverPopoverFor(
	TFunction<FMixtormatParameterAddress()> ResolveTarget)
{
	return BuildParameterDriverPopover(ResolveTarget());
}

double SMixtormat::GetEffectiveFloatParameter(
	const FMixtormatParameterAddress& Target,
	const double LocalValue) const
{
	float Value = static_cast<float>(LocalValue);
	return Target.IsValid() && MixtormatParameterBinding::TryResolveFloat(WorkingLayers, Target, Value)
		? static_cast<double>(Value)
		: LocalValue;
}

int32 SMixtormat::GetEffectiveIntParameter(
	const FMixtormatParameterAddress& Target,
	const int32 LocalValue) const
{
	int32 Value = LocalValue;
	return Target.IsValid() && MixtormatParameterBinding::TryResolveInt(WorkingLayers, Target, Value)
		? Value
		: LocalValue;
}

bool SMixtormat::GetEffectiveBoolParameter(
	const FMixtormatParameterAddress& Target,
	const bool LocalValue) const
{
	bool Value = LocalValue;
	return Target.IsValid() && MixtormatParameterBinding::TryResolveBool(WorkingLayers, Target, Value)
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
	if (FMixtormatParameterBinding* Binding = FindParameterBinding(Target, bEnabled))
	{
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

	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
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

		for (const FMixtormatLayerChild& Child : Layer.Children)
		{
			EMixtormatDriverSourceKind Kind = EMixtormatDriverSourceKind::None;
			FName Output = NAME_None;
			switch (Child.Type)
			{
			case EMixtormatLayerChildType::Filter:
			case EMixtormatLayerChildType::PatternId:
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
			.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::DriverPopoverInnerGap)
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
