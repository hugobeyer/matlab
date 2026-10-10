// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatOutputReference.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorGroup.h"
#include "UI/Rows/SMixtormatRow.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	bool ShelfSourceReaches(const TArray<FMixtormatSourceEntry>& Sources,
		const FGuid& From, const FGuid& Target, TSet<FGuid>& Visited)
	{
		if (From == Target) { return true; }
		if (Visited.Contains(From)) { return false; }
		Visited.Add(From);
		const FMixtormatSourceEntry* Source = Sources.FindByPredicate(
			[&From](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == From; });
		if (!Source) { return false; }
		const FMixtormatOutputReference* Inputs[] = {
			&Source->Child.Generator.HeightSource, &Source->Child.Generator.WarpSource};
		for (const FMixtormatOutputReference* Input : Inputs)
		{
			if (!Input->bEnabled || !Input->IsShelfSource()) { continue; }
			const auto Status = MixtormatOutputReferences::ClassifyShelfSourceReference(Sources, *Input);
			if (Status.Issue == MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated
				&& ShelfSourceReaches(Sources, Input->SourceShelfId, Target, Visited))
			{
				return true;
			}
		}
		return false;
	}

	bool WouldCreateShelfSourceCycle(const TArray<FMixtormatSourceEntry>& Sources,
		const FGuid& Destination, const FGuid& Candidate)
	{
		if (!Destination.IsValid()) { return false; }
		TSet<FGuid> Visited;
		return ShelfSourceReaches(Sources, Candidate, Destination, Visited);
	}
}

TSharedRef<SWidget> SMixtormat::BuildGeneratorInputControls()
{
	const auto Input = [this](const bool bHeightInput) -> FMixtormatOutputReference*
	{
		FMixtormatGenerator* Generator = GetSelectedGenerator();
		return Generator ? (bHeightInput ? &Generator->HeightSource : &Generator->WarpSource) : nullptr;
	};
	const auto MakeInputRow = [this, Input](const bool bHeightInput, const FText& Label,
		const FText& Hint)
	{
		return MixtormatRow::MakeDropdown(Label,
			SNew(SBox)
			.IsEnabled_Lambda([this]() { return HasSelectedGenerator(); })
			[
				MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this, Input, bHeightInput]()
				{
					const FMixtormatOutputReference* Reference = Input(bHeightInput);
					if (!Reference || !Reference->bEnabled)
					{
						return LOCTEXT("GeneratorInputUnconnected", "None");
					}
					if (!Reference->IsShelfSource())
					{
						return FText::Format(LOCTEXT("GeneratorInputLayerLabel", "Layer field · {0}"),
							FText::FromName(Reference->OutputName));
					}
					const FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
						[Reference](const FMixtormatSourceEntry& Candidate)
						{ return Candidate.SourceId == Reference->SourceShelfId; });
					if (!Source) { return LOCTEXT("GeneratorInputMissing", "Missing source"); }
					return FText::Format(LOCTEXT("GeneratorInputSourceLabel", "{0} · {1}"),
						Source->DisplayName, FText::FromName(Reference->OutputName));
				}), FOnGetContent::CreateLambda([this, bHeightInput]()
				{
					return BuildGeneratorInputMenu(bHeightInput);
				}), nullptr, TAttribute<FText>(), 0.0f)
			], Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MakeInputRow(true, LOCTEXT("GeneratorHeightInput", "Height Input"),
		LOCTEXT("GeneratorHeightInputHint", "Use a signed field from a Sources shelf producer.")));
	AddSliderRow(Panel, MakeInputRow(false, LOCTEXT("GeneratorWarpInput", "Warp Input"),
		LOCTEXT("GeneratorWarpInputHint", "Use a Flow or UV Map field from a Sources shelf producer.")));
	return SNew(SBox)
		.Visibility_Lambda([this]() { return HasSelectedGenerator() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("GeneratorInputsHeading", "GENERATOR INPUTS"))
			.InitiallyExpanded(true)
			[Panel]
		];
}

TSharedRef<SWidget> SMixtormat::BuildGeneratorInputMenu(const bool bHeightInput)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(bHeightInput
		? LOCTEXT("GeneratorHeightSourcesCaption", "Signed Height sources")
		: LOCTEXT("GeneratorWarpSourcesCaption", "Flow / UV Map sources"));
	FMixtormatGenerator* SelectedGenerator = GetSelectedGenerator();
	const FGuid CurrentSourceId = GetSelectedSource() ? GetSelectedSource()->SourceId : FGuid();
	const EMixtormatPublishedFieldKind Kinds[] = {
		bHeightInput ? EMixtormatPublishedFieldKind::ScalarSigned : EMixtormatPublishedFieldKind::Flow,
		EMixtormatPublishedFieldKind::UVMap};
	const FName OutputNames[] = {
		FName(TEXT("Height")), FName(TEXT("Value")), FName(TEXT("FlowDirection")), FName(TEXT("WarpedUV"))};
	bool bHasCompatibleSource = false;
	for (const FMixtormatSourceEntry& Source : WorkingSources)
	{
		if (Source.SourceId == CurrentSourceId || !Source.Child.Generator.bEnabled) { continue; }
		for (int32 KindIndex = 0; KindIndex < (bHeightInput ? 1 : UE_ARRAY_COUNT(Kinds)); ++KindIndex)
		{
			const EMixtormatPublishedFieldKind Kind = Kinds[KindIndex];
			for (const FName OutputName : OutputNames)
			{
				if (!MixtormatOutputReferences::GeneratorPublishesField(Source.Child.Generator, Kind, OutputName))
				{
					continue;
				}
				FMixtormatOutputReference Candidate;
				Candidate.OwnerKind = EMixtormatOutputReferenceOwnerKind::Shelf;
				Candidate.SourceShelfId = Source.SourceId;
				Candidate.SourceChildId = Source.Child.ChildId;
				Candidate.OutputName = OutputName;
				Candidate.Kind = Kind;
				const auto Status = MixtormatOutputReferences::ClassifyShelfSourceReference(
					WorkingSources, Candidate);
				if (Status.Issue != MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated
					|| WouldCreateShelfSourceCycle(WorkingSources, CurrentSourceId, Source.SourceId))
				{
					continue;
				}
				bHasCompatibleSource = true;
				const FText Label = FText::Format(LOCTEXT("GeneratorInputChoice", "{0} · {1}"),
					Source.DisplayName, FText::FromName(OutputName));
				Menu.Item(Label, MixtormatIcons::Generated(),
					FSimpleDelegate::CreateLambda([this, bHeightInput, Candidate]()
					{
						if (FMixtormatGenerator* Generator = GetSelectedGenerator())
						{
						FMixtormatOutputReference& Input = bHeightInput
							? Generator->HeightSource : Generator->WarpSource;
						Input = Candidate;
						Input.bEnabled = true;
						RefreshLayeredPreview();
					}
					}));
			}
		}
	}
	if (SelectedGenerator)
	{
		Menu.Item(LOCTEXT("GeneratorInputDisconnect", "Disconnect"), nullptr,
			FSimpleDelegate::CreateLambda([this, bHeightInput]()
			{
				if (FMixtormatGenerator* Generator = GetSelectedGenerator())
				{
				(bHeightInput ? Generator->HeightSource : Generator->WarpSource).bEnabled = false;
				RefreshLayeredPreview();
				}
			}));
	}
	if (!bHasCompatibleSource)
	{
		Menu.Item(LOCTEXT("GeneratorInputNoSources", "No compatible sources"), nullptr,
			FSimpleDelegate()).Enabled(false);
	}
	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
