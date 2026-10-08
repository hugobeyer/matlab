// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

bool SMixtormat::CanCreateNoiseGate(const FMixtormatChildAddress& Owner) const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Owner);
	const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Owner);
	return Child && !Child->IsInstance() && CanOwnScopedMasks(*Child)
		&& Children && CanAddScopedChild(*Children, ResolveChildIndexAt(Owner));
}

void SMixtormat::CreateNoiseGate(const FMixtormatChildAddress& Owner)
{
	if (!CanCreateNoiseGate(Owner)) { return; }
	FMixtormatAddTarget Target = Owner.OwnerType == EMixtormatChildOwnerType::Group
		? FMixtormatAddTarget::Group(Owner.OwnerId)
		: FMixtormatAddTarget::Layer(WorkingLayers.IndexOfByPredicate(
			[&Owner](const FMixtormatLayer& Layer) { return Layer.LayerId == Owner.OwnerId; }));
	Target.ScopeOwnerChildId = Owner.ChildId;
	CreateChild(Target, EMixtormatChildCreation::NoiseMask);
}

void SMixtormat::SelectMaskSource(const FMixtormatChildAddress& Destination, const EMixtormatMaskSource Source)
{
	FMixtormatLayerChild* Child = ResolveChildAt(Destination);
	if (!Child || Child->Type != EMixtormatLayerChildType::Mask || Child->IsInstance()) { return; }
	Child->Mask.Source = Source;
	// Explicit inline selection must beat the published-address precedence rule.
	Child->Mask.PublishedSourceLayerId.Invalidate();
	Child->Mask.PublishedSourceChildId.Invalidate();
	Child->Mask.PublishedSourceOutput = NAME_None;
	RefreshLayeredPreview(false);
	LastHistoryRecordTime = 0.0;
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
}

bool SMixtormat::CanSelectMaskNoiseValue(const FMixtormatChildAddress& Destination,
	const FMixtormatChildAddress& Source, FText& Reason) const
{
	const FMixtormatLayerChild* Target = ResolveChildAt(Destination);
	const FMixtormatLayerChild* Producer = ResolveChildAt(Source);
	if (!Target || Target->Type != EMixtormatLayerChildType::Mask || Target->IsInstance())
	{
		Reason = LOCTEXT("NoiseMaskInstanceSource", "Break the mask instance before changing its source");
		return false;
	}
	if (!Producer || Producer->Type != EMixtormatLayerChildType::Generator
		|| Producer->Generator.Type != EMixtormatGeneratorType::Noise)
	{
		Reason = LOCTEXT("NoiseMaskMissingProducer", "Requires an existing Noise generator");
		return false;
	}
	const FMixtormatChildCapabilities Caps = GetChildCapabilities(*Producer);
	if (!Caps.Outputs.ContainsByPredicate([](const FMixtormatPublishedOutputDesc& Output)
		{ return Output.Name == TEXT("Value") && Output.bCopyableAsMask; }))
	{
		Reason = LOCTEXT("NoiseMaskWrongOutput", "Value is not available as a mask");
		return false;
	}
	TArray<FMixtormatLayer> Layers = WorkingLayers;
	TArray<FMixtormatLayerGroup> Groups = WorkingLayerGroups;
	TArray<FMixtormatLayerChild>* Children = nullptr;
	for (FMixtormatLayer& Layer : Layers)
	{
		if (Destination.OwnerType == EMixtormatChildOwnerType::Layer && Layer.LayerId == Destination.OwnerId)
		{ Children = &Layer.Children; }
	}
	if (Destination.OwnerType == EMixtormatChildOwnerType::Group)
	{
		if (FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, Destination.OwnerId))
		{ Children = &Group->Children; }
	}
	const int32 Index = ResolveChildIndexAt(Destination);
	if (!Children || !Children->IsValidIndex(Index)) { return false; }
	FMixtormatMaskLayer& Mask = (*Children)[Index].Mask;
	Mask.PublishedSourceLayerId = Source.OwnerId;
	Mask.PublishedSourceChildId = Source.ChildId;
	Mask.PublishedSourceOutput = TEXT("Value");
	const FMixtormatBindingScope Scope{Layers, Groups};
	if (!CanReadPublishedOutputAt(Scope, (*Children)[Index], Destination.OwnerId, Index))
	{
		Reason = LOCTEXT("NoiseMaskUnavailableSource",
			"Requires an enabled, completed earlier Noise scope; no self/owner feedback (all group members)");
		return false;
	}
	if (!PublishedOutputPlacementsValid(Scope))
	{
		Reason = LOCTEXT("NoiseMaskInvalidPlacement", "Published-output placements must be valid");
		return false;
	}
	Reason = FText::GetEmpty();
	return true;
}

void SMixtormat::SelectMaskNoiseValue(const FMixtormatChildAddress& Destination,
	const FMixtormatChildAddress& Source)
{
	FText Reason;
	if (!CanSelectMaskNoiseValue(Destination, Source, Reason))
	{
		WorkingStatusText = Reason.ToString();
		return;
	}
	FMixtormatMaskLayer& Mask = ResolveChildAt(Destination)->Mask;
	Mask.PublishedSourceLayerId = Source.OwnerId;
	Mask.PublishedSourceChildId = Source.ChildId;
	Mask.PublishedSourceOutput = TEXT("Value");
	RefreshLayeredPreview(false);
	LastHistoryRecordTime = 0.0;
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
}

FText SMixtormat::GetMaskSourceLabel(const FMixtormatChildAddress& Destination) const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Destination);
	if (!Child || Child->Type != EMixtormatLayerChildType::Mask) { return FText::GetEmpty(); }
	const FMixtormatMaskLayer& Mask = Child->Mask;
	if (!Mask.HasPublishedSource()) { return MixtormatUI::MaskSourceText(Mask.Source); }
	const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(
		FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups}, Mask.PublishedSourceLayerId, Mask.PublishedSourceChildId);
	const bool bNoise = Source && Source->Type == EMixtormatLayerChildType::Generator
		&& Source->Generator.Type == EMixtormatGeneratorType::Noise && Mask.PublishedSourceOutput == TEXT("Value");
	if (bNoise)
	{
		return FText::Format(LOCTEXT("NoiseValueSourceLabel", "Noise Value from {0}"), GetLayerChildName(*Source));
	}
	return FText::Format(LOCTEXT("PublishedMaskSourceLabel", "{0} from {1}"),
		FText::FromName(Mask.PublishedSourceOutput),
		Source ? GetLayerChildName(*Source) : LOCTEXT("MissingMaskSource", "Missing source"));
}

TSharedRef<SWidget> SMixtormat::BuildMaskNoiseValueMenu(const FMixtormatChildAddress Destination)
{
	MixtormatMenu::FBuilder Menu;
	struct FChoice { FMixtormatChildAddress Address; FText Label; FText Reason; bool bEnabled; };
	TArray<FChoice> Choices;
	const auto AddChoices = [this, &Choices, &Destination](const TArray<FMixtormatLayerChild>& Children,
		const EMixtormatChildOwnerType OwnerType, const FGuid OwnerId, const FText& OwnerName)
	{
		for (const FMixtormatLayerChild& Child : Children)
		{
			if (Child.Type != EMixtormatLayerChildType::Generator || Child.Generator.Type != EMixtormatGeneratorType::Noise)
			{ continue; }
			FChoice Choice;
			Choice.Address = {OwnerType, OwnerId, Child.ChildId};
			Choice.Label = FText::Format(LOCTEXT("NoiseValueChoice", "{0} / {1}"), OwnerName, GetLayerChildName(Child));
			Choice.bEnabled = CanSelectMaskNoiseValue(Destination, Choice.Address, Choice.Reason);
			Choices.Add(MoveTemp(Choice));
		}
	};
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{ AddChoices(Layer.Children, EMixtormatChildOwnerType::Layer, Layer.LayerId, Layer.DisplayName); }
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{ AddChoices(Group.Children, EMixtormatChildOwnerType::Group, Group.GroupId, Group.DisplayName); }
	for (const bool bAvailable : {true, false})
	{
		for (const FChoice& Choice : Choices)
		{
			if (Choice.bEnabled != bAvailable) { continue; }
			Menu.Item(Choice.bEnabled ? Choice.Label : FText::Format(
				LOCTEXT("NoiseValueUnavailableChoice", "{0} — {1}"), Choice.Label, Choice.Reason), MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, Destination, Source = Choice.Address]()
				{ SelectMaskNoiseValue(Destination, Source); })).Enabled(Choice.bEnabled);
		}
	}
	if (Choices.IsEmpty())
	{ Menu.Item(LOCTEXT("NoNoiseValueSources", "No existing Noise generators"), nullptr, FSimpleDelegate()).Enabled(false); }
	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
