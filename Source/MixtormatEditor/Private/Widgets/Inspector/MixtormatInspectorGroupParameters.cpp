// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "MixtormatGroupControls.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterDefinition.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SMixtormat"
namespace MixtormatGroupUi
{
	FMixtormatGroupControl* Find(TArray<FMixtormatLayerGroup>& Groups, FGuid GroupId, FGuid Id)
	{
		FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, GroupId);
		return Group ? Group->Controls.FindByPredicate([Id](const FMixtormatGroupControl& Control) { return Control.ControlId == Id; }) : nullptr;
	}
	TSharedRef<SWidget> Button(const FText& Label, const FOnGetContent& Menu)
	{
		return SNew(SComboButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
			.HasDownArrow(false).ContentPadding(FMargin(0.0f)).OnGetMenuContent(Menu)
			.ButtonContent()[SNew(STextBlock).Text(Label)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
	}
	bool Eligible(EMixtormatParameterValueType Type)
	{
		return Type == EMixtormatParameterValueType::Float || Type == EMixtormatParameterValueType::Int
			|| Type == EMixtormatParameterValueType::Enum || Type == EMixtormatParameterValueType::ScalarRamp || Type == EMixtormatParameterValueType::ColorRamp;
	}
}

const FMixtormatParameterValue* SMixtormat::GroupControlValueFor(const void* Owner, UScriptStruct* Struct, FName Parameter) const
{
	const FProperty* Property = Struct ? Struct->FindPropertyByName(Parameter) : nullptr;
	if (!Owner || !Property) { return nullptr; }
	const auto Address = BuildParameterAddress(Owner, Struct, Property->GetOffset_ForInternal());
	const auto* Control = MixtormatGroupControls::FindDrivingControl({WorkingLayers, WorkingLayerGroups}, Address);
	return Control ? &Control->Value : nullptr;
}

void SMixtormat::BeginGroupControlEdit()
{
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bGroupControlInteractiveEdit = true;
	bInteractiveEdit = true;
}
void SMixtormat::EndGroupControlEdit()
{
	bGroupControlInteractiveEdit = false;
	bInteractiveEdit = false;
	bInteractiveHistoryPending = false;
	SyncChildInstances();
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	RefreshLayeredPreview(false);
}
void SMixtormat::EditGroupControl(FGuid GroupId, FGuid Id, TFunction<void(FMixtormatGroupControl&)> Edit)
{
	FMixtormatGroupControl* Control = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id);
	if (!Control) { return; }
	BeginGroupControlEdit();
	Edit(*Control);
	EndGroupControlEdit();
	RebuildGroupControlsPanel();
}
void SMixtormat::SetGroupControlValue(FGuid GroupId, FGuid Id, const FMixtormatParameterValue& Value)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	FMixtormatGroupControl* Control = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id);
	if (!Group || !Control || Value.Type != Control->Value.Type || Value.TypeName != Control->Value.TypeName) { return; }
	FMixtormatGroupControl Candidate = *Control;
	Candidate.Value = Value;
	FText Error;
	if (!MixtormatGroupControls::ValidateControl(Candidate, Error)) { WorkingStatusText = Error.ToString(); return; }
	for (const FMixtormatParameterAddress& Target : Candidate.Targets)
	{
		if (!MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, Candidate, Target, Error))
		{ WorkingStatusText = Error.ToString(); return; }
	}
	const bool bDiscrete = !bGroupControlInteractiveEdit;
	if (bDiscrete) { BeginGroupControlEdit(); }
	Control->Value = Value;
	RefreshLayeredPreview();
	if (bDiscrete) { EndGroupControlEdit(); }
}

void SMixtormat::AddGroupControl(FGuid GroupId, EMixtormatParameterValueType Type, FMixtormatParameterAddress Target)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !MixtormatGroupUi::Eligible(Type)) { return; }
	FMixtormatGroupControl Control;
	Control.Value.Type = Type;
	Control.Label = FText::Format(LOCTEXT("GroupControlDefaultName", "Control {0}"), FText::AsNumber(Group->Controls.Num() + 1));
	if (Target.IsValid())
	{
		if (!MixtormatParameterBinding::TryReadLocalValue({WorkingLayers, WorkingLayerGroups}, Target, Control.Value)) { return; }
		Control.Label = FText::FromName(Target.Parameter);
		Control.ColorDomainMin = Target.Owner == EMixtormatParameterOwnerType::HeightColorRamp ? -1.0f : 0.0f;
		if (const TOptional<FMixtormatParameterDefinitionKey> Key = MixtormatParameterUi::DefinitionKeyOf(Target))
		{
			FMixtormatParameterUiResolution Meta;
			if (MixtormatParameterUi::TryResolveUi(*Key, Meta)) { Control.UiMin = Meta.UiMin; Control.UiMax = Meta.UiMax; Control.Snap = Meta.Snap; }
		}
		FText Error;
		if (!MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, Control, Target, Error))
		{ WorkingStatusText = Error.ToString(); return; }
		Control.Targets.Add(Target);
	}
	else if (Type == EMixtormatParameterValueType::Enum) { return; } // Enum identity comes from a real declared member parameter.
	Control.DefaultValue = Control.Value;
	BeginGroupControlEdit();
	Group->Controls.Add(MoveTemp(Control));
	EndGroupControlEdit();
	RebuildGroupControlsPanel();
}
void SMixtormat::AttachGroupControlTarget(FGuid GroupId, FGuid Id, FMixtormatParameterAddress Target)
{
	FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	FMixtormatGroupControl* Control = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id);
	if (!Group || !Control) { return; }
	if (Control->Targets.ContainsByPredicate([&Target](const FMixtormatParameterAddress& Item) { return MixtormatGroupControls::SameTarget(Item, Target); })) { return; }
	FText Error;
	if (!MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, *Control, Target, Error))
	{ WorkingStatusText = Error.ToString(); return; }
	EditGroupControl(GroupId, Id, [Target](FMixtormatGroupControl& Item) { Item.Targets.Add(Target); });
}

TSharedRef<SWidget> SMixtormat::BuildExposeToGroupMenu(FMixtormatParameterAddress Target)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group = MixtormatGroupControls::FindTargetGroup({WorkingLayers, WorkingLayerGroups}, Target);
	if (!Group || !MixtormatGroupUi::Eligible(Target.ValueType)) { Menu.Caption(LOCTEXT("GroupExposeUnavailable", "Choose a supported parameter inside a group.")); return Menu.Build(); }
	const FGuid GroupId = Group->GroupId;
	FMixtormatGroupControl Probe;
	FText Error;
	const bool bRead = MixtormatParameterBinding::TryReadLocalValue({WorkingLayers, WorkingLayerGroups}, Target, Probe.Value);
	Probe.ColorDomainMin = Target.Owner == EMixtormatParameterOwnerType::HeightColorRamp ? -1.0f : 0.0f;
	const bool bNew = bRead && MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, Probe, Target, Error);
	Menu.Item(LOCTEXT("GroupExposeNew", "New Independent Control"), nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId, Target]() { AddGroupControl(GroupId, Target.ValueType, Target); }))
		.Enabled(bNew).ToolTip(bNew ? LOCTEXT("GroupExposeNewHint", "Copy the current authored value into a new group-owned control. The target's stored base remains unchanged.") : Error);
	for (const FMixtormatGroupControl& Control : Group->Controls)
	{
		FText Issue;
		const bool bCompatible = MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, Control, Target, Issue);
		Menu.Item(Control.Label, nullptr, FSimpleDelegate::CreateLambda([this, GroupId, Id = Control.ControlId, Target]() { AttachGroupControlTarget(GroupId, Id, Target); }))
			.Enabled(bCompatible).ToolTip(Issue);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGroupTargetPicker(FGuid GroupId, FGuid Id, int32 TypeFilter)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group) { return Menu.Build(); }
	const FMixtormatGroupControl* Existing = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id);
	const auto AddOwner = [this, &Menu, Group, Existing, GroupId, Id, TypeFilter](FGuid OwnerId, FGuid ChildId,
		EMixtormatParameterOwnerType Owner, const UScriptStruct* Struct, const FText& Name)
	{
		if (!Struct) { return; }
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit)) { continue; }
			FMixtormatParameterAddress Target;
			Target.LayerId = OwnerId; Target.ChildId = ChildId; Target.Owner = Owner; Target.Parameter = Property->GetFName();
			Target.ValueType = MixtormatParameterBinding::ValueTypeForProperty(Property);
			Target.TypeName = MixtormatParameterBinding::EnumTypeForProperty(Property);
			if (!MixtormatGroupUi::Eligible(Target.ValueType) || (TypeFilter != INDEX_NONE && static_cast<int32>(Target.ValueType) != TypeFilter)) { continue; }
			FMixtormatGroupControl Probe;
			FText Error;
			bool bAllowed = true;
			if (Existing) { Probe = *Existing; }
			else { bAllowed = MixtormatParameterBinding::TryReadLocalValue({WorkingLayers, WorkingLayerGroups}, Target, Probe.Value); }
			if (!Existing) { Probe.ColorDomainMin = Owner == EMixtormatParameterOwnerType::HeightColorRamp ? -1.0f : 0.0f; }
			bAllowed = bAllowed && MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *Group, Probe, Target, Error);
			if (Existing && Existing->Targets.ContainsByPredicate([&Target](const FMixtormatParameterAddress& Item) { return MixtormatGroupControls::SameTarget(Item, Target); })) { continue; }
			Menu.Item(FText::Format(LOCTEXT("GroupTargetLabel", "{0} · {1} · {2}"), Name, FText::FromString(Struct->GetName()), Property->GetDisplayNameText()), nullptr,
				FSimpleDelegate::CreateLambda([this, GroupId, Id, Target]()
				{
					if (Id.IsValid()) { AttachGroupControlTarget(GroupId, Id, Target); }
					else { AddGroupControl(GroupId, Target.ValueType, Target); }
				})).Enabled(bAllowed).ToolTip(Error);
		}
	};
	const auto AddChildren = [&AddOwner](FGuid OwnerId, const TArray<FMixtormatLayerChild>& Children)
	{
		const UEnum* Owners = StaticEnum<EMixtormatParameterOwnerType>();
		for (const FMixtormatLayerChild& Child : Children)
		{
			for (int32 Index = 0; Index < Owners->NumEnums() - 1; ++Index)
			{
				const auto Owner = static_cast<EMixtormatParameterOwnerType>(Owners->GetValueByIndex(Index));
				const UScriptStruct* Struct = nullptr;
				if (MixtormatParameterBinding::GetChildOwnerData(Child, Owner, Struct)) { AddOwner(OwnerId, Child.ChildId, Owner, Struct, Child.DisplayName); }
			}
		}
	};
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		if (Layer.GroupId != GroupId) { continue; }
		AddOwner(Layer.LayerId, FGuid(), EMixtormatParameterOwnerType::Layer, FMixtormatLayer::StaticStruct(), Layer.DisplayName);
		AddChildren(Layer.LayerId, Layer.Children);
	}
	AddChildren(GroupId, Group->Children);
	if (Menu.IsEmpty()) { Menu.Caption(LOCTEXT("GroupNoTargets", "No compatible internal parameters. Enum controls require a declared member enum.")); }
	return SNew(SBox).MaxDesiredHeight(MixtormatTokens::ParameterPopoverMaxHeight)
	[
		SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
		+ SScrollBox::Slot()[Menu.Build()]
	];
}

TSharedRef<SWidget> SMixtormat::BuildGroupControlAddMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FGuid GroupId = SelectedGroupId;
	for (EMixtormatParameterValueType Type : {EMixtormatParameterValueType::Float, EMixtormatParameterValueType::Int, EMixtormatParameterValueType::ScalarRamp, EMixtormatParameterValueType::ColorRamp})
	{
		Menu.Item(StaticEnum<EMixtormatParameterValueType>()->GetDisplayNameTextByValue(static_cast<int64>(Type)), nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId, Type]() { AddGroupControl(GroupId, Type); }));
	}
	Menu.SubMenu(LOCTEXT("GroupAddEnumControl", "Enum from Member…"), nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupTargetPicker, GroupId, FGuid(), static_cast<int32>(EMixtormatParameterValueType::Enum)));
	Menu.SubMenu(LOCTEXT("GroupExposeMember", "Expose Member Parameter…"), nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupTargetPicker, GroupId, FGuid(), static_cast<int32>(INDEX_NONE)));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGroupControlsPanel()
{
	const TSharedRef<SWidget> Panel = SNew(SBox).Visibility_Lambda([this]() { return SelectedGroupId.IsValid() && SelectedGroupChildIndex == INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMixtormatHelp).Text(LOCTEXT("GroupControlsHelp", "Independent group-owned values. Attach compatible internal targets; removing a target restores its authored base. Controls cannot target other controls or instances."))
			[MixtormatGroupUi::Button(LOCTEXT("GroupAddControl", "Add Control…"), FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupControlAddMenu))]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SScrollBox).ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
			.ScrollBarThickness(FVector2D(FMixtormatThemeStore::GetResolved().ShellLayout.ScrollbarThickness))
			+ SScrollBox::Slot()[SAssignNew(GroupControlsBox, SVerticalBox)]
		]
	];
	RebuildGroupControlsPanel();
	return Panel;
}

void SMixtormat::RebuildGroupControlsPanel()
{
	if (!GroupControlsBox) { return; }
	GroupControlsBox->ClearChildren();
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SelectedGroupId);
	if (!Group) { return; }
	const FGuid GroupId = Group->GroupId;
	for (const FMixtormatGroupControl& Snapshot : Group->Controls)
	{
		const FGuid Id = Snapshot.ControlId;
		const auto Read = [this, GroupId, Id]() { return MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id); };
		const auto Change = [this, GroupId, Id](const FMixtormatParameterValue& Value) { SetGroupControlValue(GroupId, Id, Value); };
		const TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		Body->AddSlot().AutoHeight()[SNew(SMixtormatTextFieldGradient)[SNew(SEditableTextBox)
			.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
			.Text(Snapshot.Label).OnTextCommitted_Lambda([this, GroupId, Id](const FText& Text, ETextCommit::Type Commit)
			{
				if (Commit != ETextCommit::OnCleared && !Text.IsEmpty()) { EditGroupControl(GroupId, Id, [Text](FMixtormatGroupControl& Item) { Item.Label = Text; }); }
			})]];
		if (Snapshot.Value.Type == EMixtormatParameterValueType::Float || Snapshot.Value.Type == EMixtormatParameterValueType::Int)
		{
			const bool bInt = Snapshot.Value.Type == EMixtormatParameterValueType::Int;
			Body->AddSlot().AutoHeight()[SNew(SMixtormatSlider).Label(Snapshot.Label).bInteger(bInt)
				.Value_Lambda([Read, bInt]() { const auto* C = Read(); return C ? (bInt ? static_cast<double>(C->Value.IntValue) : C->Value.FloatValue) : 0.0; })
				.MinValue(Snapshot.UiMin).MaxValue(Snapshot.UiMax).Delta(Snapshot.Snap)
				.HardMinValue(bInt ? static_cast<double>(MIN_int32) : -UE_BIG_NUMBER).HardMaxValue(bInt ? static_cast<double>(MAX_int32) : UE_BIG_NUMBER)
				.DefaultValue(bInt ? static_cast<double>(Snapshot.DefaultValue.IntValue) : Snapshot.DefaultValue.FloatValue)
				.OnBeginDrag(this, &SMixtormat::BeginGroupControlEdit).OnEndDrag(this, &SMixtormat::EndGroupControlEdit)
				.OnValueChanged_Lambda([Read, Change, bInt](double Number)
				{
					const auto* C = Read(); if (!C || !FMath::IsFinite(Number)) { return; }
					FMixtormatParameterValue Value = C->Value;
					if (bInt) { Value.IntValue = static_cast<int32>(FMath::RoundToDouble(FMath::Clamp(Number, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)))); }
					else { Value.FloatValue = static_cast<float>(Number); }
					Change(Value);
				})];
		}
		else if (Snapshot.Value.Type == EMixtormatParameterValueType::Enum)
		{
			Body->AddSlot().AutoHeight()[MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([Read]()
						{
							const auto* C = Read(); const UEnum* Enum = C ? MixtormatGroupControls::FindEnumType(C->Value.TypeName) : nullptr;
							return Enum ? Enum->GetDisplayNameTextByValue(C->Value.EnumValue) : LOCTEXT("GroupEnumMissing", "Enum type is missing or ambiguous.");
						}), FOnGetContent::CreateLambda([Read, Change]()
			{
				MixtormatMenu::FBuilder Menu;
				const auto* C = Read(); const UEnum* Enum = C ? MixtormatGroupControls::FindEnumType(C->Value.TypeName) : nullptr;
				if (!Enum) { Menu.Caption(LOCTEXT("GroupEnumMissing", "Enum type is missing or ambiguous.")); return Menu.Build(); }
				for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
				{
					if (Enum->HasMetaData(TEXT("Hidden"), Index) || Enum->HasMetaData(TEXT("Deprecated"), Index) || Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"))) { continue; }
					const int64 Number = Enum->GetValueByIndex(Index);
					Menu.Item(Enum->GetDisplayNameTextByIndex(Index), nullptr, FSimpleDelegate::CreateLambda([Read, Change, Number]()
					{ if (const auto* Current = Read()) { auto Value = Current->Value; Value.EnumValue = Number; Change(Value); } })).Checked(C->Value.EnumValue == Number);
				}
				return Menu.Build();
			}))];
		}
		else if (Snapshot.Value.Type == EMixtormatParameterValueType::ScalarRamp)
		{
			Body->AddSlot().AutoHeight()[SNew(SMixtormatScalarRamp)
				.Ramp_Lambda([Read]() { const auto* C = Read(); return C ? C->Value.ScalarRampValue : FMixtormatScalarRamp(); })
				.CanonicalXMin(Snapshot.Value.ScalarRampValue.DomainMin).CanonicalXMax(Snapshot.Value.ScalarRampValue.DomainMax)
				.ExtendedYMin(FMixtormatScalarRamp::MinOutput).ExtendedYMax(FMixtormatScalarRamp::MaxOutput)
				.OnBeginInteractiveEdit(this, &SMixtormat::BeginGroupControlEdit).OnEndInteractiveEdit(this, &SMixtormat::EndGroupControlEdit)
				.OnChanged_Lambda([Read, Change](const FMixtormatScalarRamp& Ramp) { if (const auto* C = Read()) { auto Value = C->Value; Value.ScalarRampValue = Ramp; Change(Value); } })];
		}
		else if (Snapshot.Value.Type == EMixtormatParameterValueType::ColorRamp)
		{
			Body->AddSlot().AutoHeight()[SNew(SMixtormatColorRamp).DomainMin(Snapshot.ColorDomainMin).DomainMax(Snapshot.ColorDomainMax)
				.Ramp_Lambda([Read]() { const auto* C = Read(); return C ? C->Value.ColorRampValue : FMixtormatColorRamp(); })
				.OnBeginInteractiveEdit(this, &SMixtormat::BeginGroupControlEdit).OnEndInteractiveEdit(this, &SMixtormat::EndGroupControlEdit)
				.OnChanged_Lambda([Read, Change](const FMixtormatColorRamp& Ramp) { if (const auto* C = Read()) { auto Value = C->Value; Value.ColorRampValue = Ramp; Change(Value); } })];
		}
		Body->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true)
			.Text_Lambda([this, GroupId, Id]()
			{
				const auto* C = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id);
				const auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
				if (!C || !G) { return FText::GetEmpty(); }
				FString Issues;
				FText OwnError;
				if (!MixtormatGroupControls::ValidateControl(*C, OwnError)) { Issues = OwnError.ToString() + TEXT("\n"); }
				for (const auto& Target : C->Targets) { FText Error; if (!MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *G, *C, Target, Error)) { Issues += Target.Parameter.ToString() + TEXT(": ") + Error.ToString() + TEXT("\n"); } }
				return Issues.IsEmpty() ? FText::Format(LOCTEXT("GroupTargetCount", "{0} internal targets"), FText::AsNumber(C->Targets.Num())) : FText::FromString(Issues);
			}).TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
		GroupControlsBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::CardGap)
		[
			SNew(SMixtormatInspectorCard).Title(Snapshot.Label)
			.HeaderAction(MixtormatGroupUi::Button(LOCTEXT("GroupControlActions", "Control…"), FOnGetContent::CreateLambda([this, GroupId, Id]()
			{
				MixtormatMenu::FBuilder Menu;
				const auto* C = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id); if (!C) { return Menu.Build(); }
				Menu.Item(LOCTEXT("GroupResetControl", "Reset Control"), nullptr, FSimpleDelegate::CreateLambda([this, GroupId, Id]()
				{ if (const auto* Current = MixtormatGroupUi::Find(WorkingLayerGroups, GroupId, Id)) { const auto Default = Current->DefaultValue; SetGroupControlValue(GroupId, Id, Default); } }));
				Menu.SubMenu(LOCTEXT("GroupAttachTarget", "Attach Target…"), nullptr, FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupTargetPicker, GroupId, Id, static_cast<int32>(INDEX_NONE)));
				Menu.Item(LOCTEXT("GroupToggleControl", "Enabled"), nullptr, FSimpleDelegate::CreateLambda([this, GroupId, Id]() { EditGroupControl(GroupId, Id, [](FMixtormatGroupControl& Item) { Item.bEnabled = !Item.bEnabled; }); })).Checked(C->bEnabled);
				for (const auto& Target : C->Targets)
				{
					Menu.Item(FText::Format(LOCTEXT("GroupDetachTarget", "Detach {0} · {1}"), FText::FromName(Target.Parameter), FText::FromString(Target.ChildId.ToString())), nullptr,
						FSimpleDelegate::CreateLambda([this, GroupId, Id, Target]() { EditGroupControl(GroupId, Id, [Target](FMixtormatGroupControl& Item) { Item.Targets.RemoveAll([&Target](const auto& Address) { return MixtormatGroupControls::SameTarget(Address, Target); }); }); }));
				}
				for (int32 Direction : {-1, 1})
				{
					const auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
					const int32 Index = G->Controls.IndexOfByPredicate([Id](const auto& Item) { return Item.ControlId == Id; });
					Menu.Item(Direction < 0 ? LOCTEXT("GroupControlUp", "Move Up") : LOCTEXT("GroupControlDown", "Move Down"), nullptr,
						FSimpleDelegate::CreateLambda([this, GroupId, Id, Direction]()
						{
							auto* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId); if (!Group) { return; }
							const int32 At = Group->Controls.IndexOfByPredicate([Id](const auto& Item) { return Item.ControlId == Id; });
							if (At == INDEX_NONE || !Group->Controls.IsValidIndex(At + Direction)) { return; }
							BeginGroupControlEdit(); Group->Controls.Swap(At, At + Direction); EndGroupControlEdit(); RebuildGroupControlsPanel();
						})).Enabled(G->Controls.IsValidIndex(Index + Direction));
				}
				Menu.Item(LOCTEXT("GroupRemoveControl", "Remove Control"), nullptr, FSimpleDelegate::CreateLambda([this, GroupId, Id]()
				{ auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId); if (!G) { return; } BeginGroupControlEdit(); G->Controls.RemoveAll([Id](const auto& Item) { return Item.ControlId == Id; }); EndGroupControlEdit(); RebuildGroupControlsPanel(); })).Destructive();
				return Menu.Build();
			})))
			[Body]
		];
	}
}
#undef LOCTEXT_NAMESPACE
