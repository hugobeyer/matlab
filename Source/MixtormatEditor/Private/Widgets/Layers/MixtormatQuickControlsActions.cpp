// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "MixtormatLayerGroups.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

void SMixtormat::CaptureQuickControlsTarget()
{
	TSharedRef<FMixtormatQuickControlsTarget> Context = MakeShared<FMixtormatQuickControlsTarget>();
	Context->Address = GetSelectedChildAddress();
	Context->DocumentRevision = QuickControlsDocumentRevision;
	const FMixtormatChildAddress& Address = Context->Address;
	if (Address.OwnerType == EMixtormatChildOwnerType::Group)
	{
		if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Address.OwnerId))
		{
			Context->Name = Group->DisplayName;
			Context->Label = FText::Format(LOCTEXT("QuickTargetGroup", "Group · {0}"), Context->Name);
		}
	}
	else if (const FMixtormatLayer* Layer = WorkingLayers.FindByPredicate(
		[&Address](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Address.OwnerId; }))
	{
		Context->Name = Layer->DisplayName;
		Context->Label = FText::Format(LOCTEXT("QuickTargetLayer", "Layer · {0}"), Context->Name);
	}
	if (const FMixtormatLayerChild* Child = ResolveChildAt(Address))
	{
		Context->Name = GetLayerChildName(*Child);
		Context->Label = FText::Format(LOCTEXT("QuickTargetChild", "{0} / {1}"), Context->Label, Context->Name);
	}
	if (!Address.OwnerId.IsValid())
	{
		Context->Label = bHasWorkingMaterial
			? LOCTEXT("QuickTargetStack", "Material stack") : LOCTEXT("QuickTargetNew", "New material");
		Context->Name = Context->Label;
	}
	QuickControlsActionTarget = Context;
}

FText SMixtormat::GetQuickControlsTargetLabel() const
{
	return QuickControlsActionTarget.IsValid() ? QuickControlsActionTarget->Label : FText::GetEmpty();
}

FText SMixtormat::GetQuickControlsTargetUnavailableReason(
	const TSharedPtr<const FMixtormatQuickControlsTarget>& Context) const
{
	if (!Context.IsValid() || !bQuickControlsOpen || Context != QuickControlsActionTarget)
	{
		return LOCTEXT("QuickTargetExpired", "This Q session has ended. Reopen Q to choose a target.");
	}
	if (Context->DocumentRevision != QuickControlsDocumentRevision)
	{
		return LOCTEXT("QuickTargetDocumentChanged", "The document changed. Reopen Q to choose a target.");
	}
	const FMixtormatChildAddress& Address = Context->Address;
	if (Address.OwnerId.IsValid() && (!ResolveContainer(Address)
		|| (Address.ChildId.IsValid() && !ResolveChildAt(Address))))
	{
		return LOCTEXT("QuickTargetRemoved", "The captured target was removed or moved to another owner. Reopen Q.");
	}
	return FText::GetEmpty();
}

FMixtormatAddTarget SMixtormat::ResolveQuickControlsAddTarget(FMixtormatAddTarget Target) const
{
	if (!Target.QuickControlsTarget.IsValid()) { return Target; }
	const FMixtormatChildAddress& Address = Target.QuickControlsTarget->Address;
	Target.LayerIndex = INDEX_NONE;
	Target.GroupId.Invalidate();
	if (!GetQuickControlsTargetUnavailableReason(Target.QuickControlsTarget).IsEmpty()) { return Target; }
	if (Address.OwnerType == EMixtormatChildOwnerType::Group)
	{
		Target.GroupId = Address.OwnerId;
	}
	else
	{
		Target.LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
	}
	return Target;
}

FText SMixtormat::GetAddMenuUnavailableReason(FMixtormatAddTarget Target, const EAddMenuKind Kind) const
{
	if (Target.QuickControlsTarget.IsValid())
	{
		const FText Reason = GetQuickControlsTargetUnavailableReason(Target.QuickControlsTarget);
		if (!Reason.IsEmpty()) { return Reason; }
		Target = ResolveQuickControlsAddTarget(Target);
		if (Target.ScopeOwnerChildId.IsValid())
		{
			FMixtormatChildAddress Owner = Target.QuickControlsTarget->Address;
			Owner.ChildId = Target.ScopeOwnerChildId;
			const FMixtormatLayerChild* Child = ResolveChildAt(Owner);
			if (!Child || Child->IsInstance())
			{
				return LOCTEXT("QuickScopedOwnerReadOnly", "The captured scope owner is missing or is a read-only instance.");
			}
		}
	}
	if (Kind == EAddMenuKind::Effect)
	{
		return !Target.IsGroup() && WorkingLayers.IsValidIndex(Target.LayerIndex) ? FText::GetEmpty()
			: LOCTEXT("QuickEffectsNeedLayer", "Effects require a layer target; group effects are not offered here.");
	}
	if (Kind == EAddMenuKind::Generator && CanAddScopedFlowGenerator(Target)) { return FText::GetEmpty(); }
	if (!CanCreateChild(Target))
	{
		return LOCTEXT("QuickCannotCreateChild", "Requires a layer/group target with an eligible owner and available scope depth.");
	}
	if (Kind == EAddMenuKind::Generator && !CanAddGeneratorModule(Target))
	{
		return Target.IsGroup()
			? LOCTEXT("GeneratorModuleGroupUnavailable", "Generator modules cannot be authored in groups")
			: LOCTEXT("QuickGeneratorUnavailable", "This owner cannot accept another generator module.");
	}
	if (Kind == EAddMenuKind::Surfacing && !CanAddSurfacingModule(Target))
	{
		return LOCTEXT("AddSkinUnavailable", "Requires a layer/group root or an editable Generator/Surfacing owner with available scope depth.");
	}
	return FText::GetEmpty();
}

FSimpleDelegate SMixtormat::MakeAddMenuAction(
	const FMixtormatAddTarget Target, TFunction<void(FMixtormatAddTarget)> Action, const EAddMenuKind Kind)
{
	return FSimpleDelegate::CreateLambda([this, Target, Action = MoveTemp(Action), Kind]()
	{
		if (!Target.QuickControlsTarget.IsValid())
		{
			Action(Target);
			return;
		}
		const FText Reason = GetAddMenuUnavailableReason(Target, Kind);
		if (!Reason.IsEmpty())
		{
			ShowPreviewFeedback(Reason);
			return;
		}
		const FMixtormatAddTarget Resolved = ResolveQuickControlsAddTarget(Target);
		const FMixtormatChildAddress& Address = Target.QuickControlsTarget->Address;
		const TArray<FMixtormatLayerChild>* Before = ResolveContainer(Address);
		const int32 Count = Before ? Before->Num() : 0;
		Action(Resolved);
		// FReply::Handled also represents rejected creation in the existing handlers.
		// Check the actual destination, not selection or the reply, before claiming success.
		const TArray<FMixtormatLayerChild>* After = ResolveContainer(Address);
		if (!After || After->Num() == Count)
		{
			ShowPreviewFeedback(LOCTEXT("QuickCreationRejected", "Nothing added · The target or source is no longer available."));
		}
	});
}

FText SMixtormat::GetQuickControlsActionUnavailableReason(
	const TSharedPtr<const FMixtormatQuickControlsTarget>& Context, const EQuickControlsAction Action) const
{
	const FText Invalid = GetQuickControlsTargetUnavailableReason(Context);
	if (!Invalid.IsEmpty()) { return Invalid; }
	const FMixtormatChildAddress& Address = Context->Address;
	FMixtormatAddTarget Target;
	Target.QuickControlsTarget = Context;
	Target = ResolveQuickControlsAddTarget(Target);
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
	const int32 Index = ResolveChildIndexAt(Address);
	const bool bEditableScope = Child && !Child->IsInstance() && Children && CanAddScopedChild(*Children, Index);
	switch (Action)
	{
	case EQuickControlsAction::Fill:
		return FText::GetEmpty();
	case EQuickControlsAction::Material:
		return !SelectedSurfacePath.IsNull() && (bHasWorkingMaterial || SelectedPreviewMaterial.IsValid())
			? FText::GetEmpty() : LOCTEXT("QuickMaterialSourceRequired", "Select a usable surface in Library first.");
	case EQuickControlsAction::Effects:
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Effect);
	case EQuickControlsAction::Ids:
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Child);
	case EQuickControlsAction::Generators:
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Generator);
	case EQuickControlsAction::Surfacing:
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Surfacing);
	case EQuickControlsAction::Masks:
		if (Child && CanOwnScopedBlurs(*Child))
		{
			return bEditableScope ? FText::GetEmpty()
				: LOCTEXT("QuickMaskFilterUnavailable", "Mask filters require an editable owner with available scope depth.");
		}
		if (Child && CanOwnScopedMasks(*Child) && !Child->IsInstance()) { Target.ScopeOwnerChildId = Address.ChildId; }
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Child);
	case EQuickControlsAction::Outputs:
		return Child && !GetCopyableOutputs(GetChildCapabilities(*Child)).IsEmpty() ? FText::GetEmpty()
			: LOCTEXT("QuickNoCopyableOutputs", "The captured module has no copyable published outputs.");
	case EQuickControlsAction::Gates:
		return Child && CanOwnScopedMasks(*Child) ? FText::GetEmpty()
			: LOCTEXT("QuickNoGateOwner", "The captured module cannot own gating masks.");
	case EQuickControlsAction::NoiseGate:
		return CanCreateNoiseGate(Address) ? FText::GetEmpty()
			: LOCTEXT("QuickNoiseGateUnavailable", "Noise gates require an editable supported module with available scope depth.");
	case EQuickControlsAction::GalleryGate:
		if (SelectedMaskPath.IsNull()) { return LOCTEXT("QuickGalleryMaskRequired", "Select a mask in Library first."); }
		return Address.OwnerType == EMixtormatChildOwnerType::Layer && bEditableScope && CanOwnScopedMasks(*Child)
			? FText::GetEmpty() : LOCTEXT("QuickGalleryGateUnavailable", "Gallery gates require an editable layer module with available scope depth.");
	case EQuickControlsAction::PasteGate:
		return CanPasteAsGatingMask(Address) ? FText::GetEmpty()
			: LOCTEXT("QuickPasteGateUnavailable", "Requires a compatible copied mask/output and an eligible gating destination.");
	case EQuickControlsAction::Blur:
	case EQuickControlsAction::Curvature:
		return bEditableScope && CanOwnScopedBlurs(*Child)
			&& (Action == EQuickControlsAction::Blur || Child->Type == EMixtormatLayerChildType::Mask)
			? FText::GetEmpty() : LOCTEXT("QuickFilterUnavailable", "This filter requires a supported editable mask owner with available scope depth.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
