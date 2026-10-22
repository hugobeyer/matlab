// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"

#include "Framework/Application/SlateApplication.h"
#include "MixtormatGroupControls.h"
#include "MixtormatLayerGroups.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Parameters/SMixtormatGroupControlPopover.h"
#include "Widgets/SWindow.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormat::ShowGroupControlSetup(FGuid GroupId, FGuid Id, FMixtormatParameterAddress Target, bool bLabelOnly)
{
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group) { return; }
	const bool bEditing = Id.IsValid();
	FMixtormatGroupControl Initial;
	if (bEditing)
	{
		const auto* Existing = Group->Controls.FindByPredicate([Id](const auto& C) { return C.ControlId == Id; });
		if (!Existing) { return; }
		Initial = *Existing;
	}
	else
	{
		Initial.Value.Type = EMixtormatParameterValueType::Float;
		Initial.Label = LOCTEXT("NewGroupControlLabel", "Parameter");
		if (Target.IsValid())
		{
			if (!MixtormatParameterBinding::TryReadLocalValue({WorkingLayers, WorkingLayerGroups}, Target, Initial.Value)) { return; }
			const FProperty* Property = MixtormatParameterBinding::FindParameterProperty({WorkingLayers, WorkingLayerGroups}, Target);
			Initial.Label = Property ? Property->GetDisplayNameText() : FText::FromName(Target.Parameter);
			Initial.ColorDomainMin = Target.Owner == EMixtormatParameterOwnerType::HeightColorRamp ? -1.0f : 0.0f;
			if (const TOptional<FMixtormatParameterDefinitionKey> Key = MixtormatParameterUi::DefinitionKeyOf(Target))
			{
				FMixtormatParameterUiResolution Meta;
				if (MixtormatParameterUi::TryResolveUi(*Key, Meta)) { Initial.UiMin = Meta.UiMin; Initial.UiMax = Meta.UiMax; Initial.Snap = Meta.Snap; }
			}
			Initial.Targets.Add(Target);
		}
		Initial.DefaultValue = Initial.Value;
	}

	// A small owned dialog, not a transient submenu: selecting a type/ramp preset must not
	// dismiss an uncommitted draft along with the menu that supplied that choice.
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Group->DisplayName)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false).SupportsMinimize(false)
		[
			SNew(SMixtormatGroupControlPopover)
			.Control(Initial).bEditing(bEditing).bTypeLocked(bEditing || Target.IsValid()).bLabelOnly(bLabelOnly)
			.OnCommit(FOnMixtormatGroupControlCommit::CreateLambda([this, GroupId, Id, bEditing, bLabelOnly](const FMixtormatGroupControl& Draft, FText& Error)
			{
				FMixtormatLayerGroup* CurrentGroup = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
				if (!CurrentGroup) { Error = LOCTEXT("GroupControlGroupMissing", "This group no longer exists."); return false; }
				FMixtormatGroupControl* Current = bEditing
					? CurrentGroup->Controls.FindByPredicate([Id](const auto& C) { return C.ControlId == Id; }) : nullptr;
				if (bEditing && !Current) { Error = LOCTEXT("GroupControlMissing", "This control no longer exists."); return false; }
				FMixtormatGroupControl Candidate = Current ? *Current : Draft;
				Candidate.Label = Draft.Label;
				if (bEditing && !bLabelOnly)
				{
					Candidate.UiMin = Draft.UiMin;
					Candidate.UiMax = Draft.UiMax;
					Candidate.Snap = Draft.Snap;
					Candidate.DefaultValue = Draft.DefaultValue;
				}
				if (!bLabelOnly)
				{
					if (!MixtormatGroupControls::ValidateControl(Candidate, Error)) { return false; }
					FMixtormatGroupControl ResetCandidate = Candidate;
					ResetCandidate.Value = Candidate.DefaultValue;
					for (const auto& Address : Candidate.Targets)
					{
						if (!MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *CurrentGroup, Candidate, Address, Error)
							|| !MixtormatGroupControls::ValidateTarget({WorkingLayers, WorkingLayerGroups}, *CurrentGroup, ResetCandidate, Address, Error)) { return false; }
					}
				}
				BeginGroupControlEdit();
				if (Current) { *Current = MoveTemp(Candidate); }
				else { CurrentGroup->Controls.Add(MoveTemp(Candidate)); }
				EndGroupControlEdit();
				RebuildGroupControlsPanel();
				return true;
			}))
		];
	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().FindWidgetWindow(AsShared()), false);
}

TSharedRef<SWidget> SMixtormat::BuildGroupControlContextMenu(FGuid GroupId, FGuid Id)
{
	MixtormatMenu::FBuilder Menu;
	const auto* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	const auto* Control = Group ? Group->Controls.FindByPredicate([Id](const auto& C) { return C.ControlId == Id; }) : nullptr;
	if (!Control) { return Menu.Build(); }
	Menu.Caption(Control->Label);
	Menu.Item(LOCTEXT("GroupControlEditLabel", "Edit Label…"), nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId, Id]() { ShowGroupControlSetup(GroupId, Id, FMixtormatParameterAddress(), true); }));
	Menu.Item(LOCTEXT("GroupControlSettings", "Control Settings…"), MixtormatIcons::Settings(),
		FSimpleDelegate::CreateLambda([this, GroupId, Id]() { ShowGroupControlSetup(GroupId, Id); }));
	Menu.Item(LOCTEXT("GroupResetControl", "Reset Control"), MixtormatIcons::Refresh(), FSimpleDelegate::CreateLambda([this, GroupId, Id]()
	{
		const auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
		const auto* C = G ? G->Controls.FindByPredicate([Id](const auto& Item) { return Item.ControlId == Id; }) : nullptr;
		if (C) { const auto Default = C->DefaultValue; SetGroupControlValue(GroupId, Id, Default); }
	}));
	Menu.Separator().Caption(FText::Format(LOCTEXT("GroupControlTargetsHeading", "Connections · {0}"), FText::AsNumber(Control->Targets.Num())));
	Menu.SubMenu(LOCTEXT("GroupAttachTarget", "Attach Target…"), MixtormatIcons::VariableLink(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupTargetPicker, GroupId, Id, static_cast<int32>(Control->Value.Type)))
		.ToolTip(LOCTEXT("GroupAttachDirectHint", "You can also right-click the member parameter itself → Assign to Group Control."));
	for (const auto& Target : Control->Targets)
	{
		FText OwnerName = Group->DisplayName;
		if (const auto* Child = MixtormatParameterBinding::FindChild({WorkingLayers, WorkingLayerGroups}, Target.LayerId, Target.ChildId))
		{ OwnerName = GetLayerChildName(*Child); }
		else if (const auto* Layer = WorkingLayers.FindByPredicate([&Target](const auto& Item) { return Item.LayerId == Target.LayerId; }))
		{ OwnerName = Layer->DisplayName; }
		const FProperty* Property = MixtormatParameterBinding::FindParameterProperty({WorkingLayers, WorkingLayerGroups}, Target);
		Menu.Item(FText::Format(LOCTEXT("GroupDetachNamedTarget", "Detach {0} · {1}"), OwnerName,
			Property ? Property->GetDisplayNameText() : FText::FromName(Target.Parameter)), MixtormatIcons::VariableUnlink(),
			FSimpleDelegate::CreateLambda([this, GroupId, Id, Target]()
			{ EditGroupControl(GroupId, Id, [Target](auto& C) { C.Targets.RemoveAll([&Target](const auto& Address) { return MixtormatGroupControls::SameTarget(Address, Target); }); }); }));
	}
	Menu.Separator();
	const int32 Index = Group->Controls.IndexOfByPredicate([Id](const auto& C) { return C.ControlId == Id; });
	for (int32 Direction : {-1, 1})
	{
		Menu.Item(Direction < 0 ? LOCTEXT("GroupControlUp", "Move Up") : LOCTEXT("GroupControlDown", "Move Down"),
			Direction < 0 ? MixtormatIcons::ArrowUp() : MixtormatIcons::ArrowDown(),
			FSimpleDelegate::CreateLambda([this, GroupId, Id, Direction]()
			{
				auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId); if (!G) { return; }
				const int32 At = G->Controls.IndexOfByPredicate([Id](const auto& C) { return C.ControlId == Id; });
				if (At == INDEX_NONE || !G->Controls.IsValidIndex(At + Direction)) { return; }
				BeginGroupControlEdit(); G->Controls.Swap(At, At + Direction); EndGroupControlEdit(); RebuildGroupControlsPanel();
			})).Enabled(Group->Controls.IsValidIndex(Index + Direction));
	}
	Menu.Item(LOCTEXT("GroupRemoveControl", "Remove Control"), MixtormatIcons::Trash(), FSimpleDelegate::CreateLambda([this, GroupId, Id]()
	{
		auto* G = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId); if (!G) { return; }
		BeginGroupControlEdit(); G->Controls.RemoveAll([Id](const auto& C) { return C.ControlId == Id; }); EndGroupControlEdit(); RebuildGroupControlsPanel();
	})).Destructive();
	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
