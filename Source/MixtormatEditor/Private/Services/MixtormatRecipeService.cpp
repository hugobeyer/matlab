// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Services/MixtormatRecipeService.h"
#include "Services/MixtormatPresetJson.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatGroupControls.h"
#include "UObject/UnrealType.h"
#include "UObject/NoExportTypes.h"

namespace MixtormatRecipeService
{
	namespace Private
	{
		void Walk(MixtormatPresetStore::FRecipePreset& Recipe,
			TFunctionRef<void(const UScriptStruct*, void*)> OnStruct,
			TFunctionRef<void(const FSoftObjectPath&)> OnAsset)
		{
			MixtormatPresetJson::Visit(FMixtormatLayerGroup::StaticStruct(), &Recipe.Group, OnStruct, OnAsset);
			for (FMixtormatLayer& Layer : Recipe.Layers) { MixtormatPresetJson::Visit(FMixtormatLayer::StaticStruct(), &Layer, OnStruct, OnAsset); }
		}
		bool Report(const TArray<FString>& Issues, FText& Error)
		{
			if (Issues.IsEmpty()) { return true; }
			Error = FText::FromString(TEXT("Recipe rejected; no document changes:\n") + FString::Join(Issues, TEXT("\n")));
			return false;
		}
	}

	bool Validate(const MixtormatPresetStore::FRecipePreset& Source, FText& Error)
	{
		// Work only on a temporary copy, including reflection visitors.
		MixtormatPresetStore::FRecipePreset Recipe = Source;
		TArray<FString> Issues;
		TSet<FGuid> Ids;
		TMap<FGuid, TSet<FGuid>> ChildrenByOwner;
		const auto AddId = [&Ids, &Issues](const FGuid& Id)
		{
			if (!Id.IsValid() || Ids.Contains(Id)) { Issues.AddUnique(TEXT("Missing or duplicate owner/module identity: ") + Id.ToString()); }
			Ids.Add(Id);
		};
		AddId(Recipe.Group.GroupId);
		for (const FMixtormatGroupControl& Control : Recipe.Group.Controls) { AddId(Control.ControlId); }
		if (Recipe.Layers.IsEmpty() || Recipe.Layers.Num() > MixtormatPresetJson::MaxArrayItems) { Issues.Add(TEXT("The group must contain a bounded, nonempty ordered member list.")); }
		const auto AddChildren = [&AddId, &ChildrenByOwner](const FGuid& Owner, const TArray<FMixtormatLayerChild>& Children)
		{
			TSet<FGuid>& Owned = ChildrenByOwner.FindOrAdd(Owner);
			for (const FMixtormatLayerChild& Child : Children) { AddId(Child.ChildId); Owned.Add(Child.ChildId); }
		};
		AddChildren(Recipe.Group.GroupId, Recipe.Group.Children);
		for (int32 Index = 0; Index < Recipe.Layers.Num(); ++Index)
		{
			const FMixtormatLayer& Layer = Recipe.Layers[Index];
			AddId(Layer.LayerId);
			AddChildren(Layer.LayerId, Layer.Children);
			if (Layer.GroupId != Recipe.Group.GroupId) { Issues.AddUnique(TEXT("Every captured layer must belong to this group.")); }
			if (!Layer.SourceComposition.IsNull()) { Issues.AddUnique(TEXT("Live composition dependency must be materialized before capture: ") + Layer.SourceComposition.ToSoftObjectPath().ToString()); }
			if (Index == 0 && Layer.bFillPassThrough) { Issues.AddUnique(TEXT("The first member inherits an outside substrate (Fill Pass Through). Include/materialize that substrate before capture.")); }
			if (Layer.HeightReferenceLayerIndex != INDEX_NONE
				&& (!Recipe.Layers.IsValidIndex(Layer.HeightReferenceLayerIndex) || Layer.HeightReferenceLayerIndex >= Index))
			{ Issues.AddUnique(TEXT("Height reference is outside the group or does not evaluate before its destination.")); }
		}
		const TArray<FMixtormatLayerGroup> ScopeGroups{Recipe.Group};
		const FMixtormatBindingScope Scope(Recipe.Layers, ScopeGroups);
		for (const FMixtormatGroupControl& Control : Recipe.Group.Controls)
		{
			FText Issue;
			if (!MixtormatGroupControls::ValidateControl(Control, Issue)) { Issues.AddUnique(Issue.ToString()); }
			for (const FMixtormatParameterAddress& Target : Control.Targets)
			{
				if (!MixtormatGroupControls::ValidateTarget(Scope, Recipe.Group, Control, Target, Issue)) { Issues.AddUnique(Issue.ToString()); }
			}
		}
		Private::Walk(Recipe, [&Issues, &Ids, &ChildrenByOwner, &Scope](const UScriptStruct* Type, void* Data)
		{
			if (Type == TBaseStructure<FGuid>::Get())
			{
				const FGuid& Id = *static_cast<FGuid*>(Data);
				if (Id.IsValid() && !Ids.Contains(Id)) { Issues.AddUnique(TEXT("External or dangling GUID dependency: ") + Id.ToString()); }
				return;
			}
			if (Type == FMixtormatOutputReference::StaticStruct())
			{
				const FMixtormatOutputReference& Reference = *static_cast<FMixtormatOutputReference*>(Data);
				if ((Reference.SourceLayerId.IsValid() || Reference.SourceChildId.IsValid()) && !Reference.HasSource())
				{ Issues.AddUnique(TEXT("An explicit output reference is incomplete (owner, module and output name are required).")); }
			}
			if (Type == FMixtormatParameterReference::StaticStruct())
			{
				const FMixtormatParameterReference& Reference = *static_cast<FMixtormatParameterReference*>(Data);
				if (Reference.bEnabled && !MixtormatParameterBinding::IsReferenceSourceValid(Scope, Reference.Source))
				{ Issues.AddUnique(TEXT("A parameter reference has an unavailable or incompatible source parameter.")); }
			}
			const auto Pair = [&](const TCHAR* OwnerName, const TCHAR* ChildName)
			{
				const FStructProperty* OwnerProperty = FindFProperty<FStructProperty>(Type, OwnerName);
				const FStructProperty* ChildProperty = FindFProperty<FStructProperty>(Type, ChildName);
				if (!OwnerProperty || !ChildProperty || OwnerProperty->Struct != TBaseStructure<FGuid>::Get()
					|| ChildProperty->Struct != TBaseStructure<FGuid>::Get()) { return; }
				const FGuid& Owner = *OwnerProperty->ContainerPtrToValuePtr<FGuid>(Data);
				const FGuid& Child = *ChildProperty->ContainerPtrToValuePtr<FGuid>(Data);
				if (!Owner.IsValid() && !Child.IsValid()) { return; }
				const TSet<FGuid>* Owned = ChildrenByOwner.Find(Owner);
				if (!Owned || (Child.IsValid() && !Owned->Contains(Child)))
				{ Issues.AddUnique(Type->GetName() + TEXT(" contains a foreign or mismatched owner/module address: ") + Owner.ToString() + TEXT(" / ") + Child.ToString()); }
			};
			Pair(TEXT("SourceLayerId"), TEXT("SourceChildId"));
			Pair(TEXT("LayerId"), TEXT("ChildId"));
			Pair(TEXT("PublishedSourceLayerId"), TEXT("PublishedSourceChildId"));
		}, [](const FSoftObjectPath&) {});

		const auto CheckChildren = [&Issues, &ChildrenByOwner, &Scope](const FGuid& Owner, const TArray<FMixtormatLayerChild>& Children)
		{
			const TSet<FGuid>& Owned = ChildrenByOwner.FindChecked(Owner);
			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				const FMixtormatLayerChild& Child = Children[ChildIndex];
				if (Child.ScopeOwnerChildId.IsValid() && (!Owned.Contains(Child.ScopeOwnerChildId)
									|| Children.IndexOfByPredicate([&Child](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Child.ScopeOwnerChildId; }) >= ChildIndex))
				{ Issues.AddUnique(TEXT("Scoped parent is missing, outside its container or self-referencing.")); }
				const auto IsEarlierLocal = [&Children, ChildIndex](const FGuid& Source)
				{
					const int32 SourceIndex = Children.IndexOfByPredicate([&Source](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == Source; });
					return SourceIndex != INDEX_NONE && SourceIndex < ChildIndex;
				};
				if (Child.Type == EMixtormatLayerChildType::HeightColorRamp && Child.HeightColorRamp.SourceChildId.IsValid()
					&& !IsEarlierLocal(Child.HeightColorRamp.SourceChildId)) { Issues.AddUnique(TEXT("Color Ramp source is outside its local container or does not evaluate earlier.")); }
				if (Child.Type == EMixtormatLayerChildType::Generator && Child.Generator.Type == EMixtormatGeneratorType::BooleanOps
					&& Child.Generator.BooleanOps.SourceChildId.IsValid() && !IsEarlierLocal(Child.Generator.BooleanOps.SourceChildId))
				{ Issues.AddUnique(TEXT("Scatter Carve source is outside its local container or does not evaluate earlier.")); }
				if (Child.IsInstance() && MixtormatParameterBinding::ClassifyInstancePlacement(Scope,
					Child.SourceLayerId, Child.SourceChildId, Owner, ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
				{ Issues.AddUnique(TEXT("A module instance is missing, cyclic or evaluates after its destination.")); }
			}
		};
		CheckChildren(Recipe.Group.GroupId, Recipe.Group.Children);
		for (const FMixtormatLayer& Layer : Recipe.Layers) { CheckChildren(Layer.LayerId, Layer.Children); }
		return Private::Report(Issues, Error);
	}

	bool Capture(const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId, MixtormatPresetStore::FRecipePreset& OutRecipe, FText& Error)
	{
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, GroupId);
		int32 First = INDEX_NONE, Last = INDEX_NONE;
		if (!Group || !MixtormatLayerGroups::GetGroupRange(Layers, GroupId, First, Last))
		{ Error = NSLOCTEXT("Mixtormat", "RecipeNoGroup", "Select an existing nonempty group to capture."); return false; }
		MixtormatPresetStore::FRecipePreset Recipe;
		Recipe.Name = Group->DisplayName.ToString();
		Recipe.Group = *Group;
		for (int32 Index = First; Index <= Last; ++Index)
		{
			if (Layers[Index].GroupId != GroupId)
			{ Error = NSLOCTEXT("Mixtormat", "RecipeContiguous", "Group membership must be contiguous before capture."); return false; }
			FMixtormatLayer Layer = Layers[Index];
			if (Layer.HeightReferenceLayerIndex != INDEX_NONE)
			{
				if (Layer.HeightReferenceLayerIndex < First || Layer.HeightReferenceLayerIndex > Last)
				{ Error = NSLOCTEXT("Mixtormat", "RecipeOutsideHeight", "Recipe rejected: a member has a height reference outside the group. Include or reconnect that dependency first."); return false; }
				Layer.HeightReferenceLayerIndex -= First;
			}
			Recipe.Layers.Add(MoveTemp(Layer));
		}
		for (int32 Index = Last + 1; Index < Layers.Num(); ++Index)
		{
			if (Layers[Index].GroupId == GroupId)
			{ Error = NSLOCTEXT("Mixtormat", "RecipeContiguous", "Group membership must be contiguous before capture."); return false; }
		}
		if (!Validate(Recipe, Error)) { return false; }
		Private::Walk(Recipe, [](const UScriptStruct*, void*) {},
			[&Recipe](const FSoftObjectPath& Path) { Recipe.Dependencies.AddUnique(Path); });
		OutRecipe = MoveTemp(Recipe);
		return true;
	}

	bool PrepareInsertion(const MixtormatPresetStore::FRecipePreset& Recipe, int32 LayerOffset,
		MixtormatPresetStore::FRecipePreset& OutCopy, FText& Error)
	{
		if (LayerOffset < 0 || !Validate(Recipe, Error)) { return false; }
		MixtormatPresetStore::FRecipePreset Copy = Recipe;
		TMap<FGuid, FGuid> Remap;
		Remap.Add(Copy.Group.GroupId, FGuid::NewGuid());
		for (const FMixtormatGroupControl& Control : Copy.Group.Controls) { Remap.Add(Control.ControlId, FGuid::NewGuid()); }
		for (const FMixtormatLayerChild& Child : Copy.Group.Children) { Remap.Add(Child.ChildId, FGuid::NewGuid()); }
		for (const FMixtormatLayer& Layer : Copy.Layers)
		{
			Remap.Add(Layer.LayerId, FGuid::NewGuid());
			for (const FMixtormatLayerChild& Child : Layer.Children) { Remap.Add(Child.ChildId, FGuid::NewGuid()); }
		}
		TArray<FString> Missing;
		Private::Walk(Copy, [&Remap](const UScriptStruct* Type, void* Data)
		{
			if (Type == TBaseStructure<FGuid>::Get())
			{
				FGuid& Id = *static_cast<FGuid*>(Data);
				if (const FGuid* NewId = Remap.Find(Id)) { Id = *NewId; }
			}
		}, [&Missing](const FSoftObjectPath& Path)
		{
			if (!Path.TryLoad()) { Missing.AddUnique(TEXT("Missing referenced asset: ") + Path.ToString()); }
		});
		if (!Private::Report(Missing, Error) || !Validate(Copy, Error)
			|| !MixtormatPresetJson::ValidateAssetClasses(FMixtormatLayerGroup::StaticStruct(), &Copy.Group, Error)) { return false; }
		for (const FMixtormatLayer& Layer : Copy.Layers)
		{
			if (!MixtormatPresetJson::ValidateAssetClasses(FMixtormatLayer::StaticStruct(), &Layer, Error)) { return false; }
		}
		for (FMixtormatLayer& Layer : Copy.Layers)
		{
			if (Layer.HeightReferenceLayerIndex != INDEX_NONE) { Layer.HeightReferenceLayerIndex += LayerOffset; }
		}
		OutCopy = MoveTemp(Copy);
		return true;
	}
}
