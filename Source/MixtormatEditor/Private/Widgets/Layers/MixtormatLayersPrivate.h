// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/SMixtormat.h"
#include "MixtormatParameterBinding.h"
#include "UI/Layers/SMixtormatLayerHierarchy.h"

// Shared declarations for the private Layers implementation; bodies live in one .cpp each.
namespace MixtormatLayersPrivate
{
	inline constexpr int32 MaximumScopeDepth = 4;

	inline FText MakeGenericMaskName(
		const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups)
	{
		TSet<FString> Names;
		for (const FMixtormatLayer& Layer : Layers)
			for (const FMixtormatLayerChild& Child : Layer.Children)
				if (Child.Type == EMixtormatLayerChildType::Mask && !Child.DisplayName.IsEmpty())
					Names.Add(Child.DisplayName.ToString());
		for (const FMixtormatLayerGroup& Group : Groups)
			for (const FMixtormatLayerChild& Child : Group.Children)
				if (Child.Type == EMixtormatLayerChildType::Mask && !Child.DisplayName.IsEmpty())
					Names.Add(Child.DisplayName.ToString());
		for (int32 Suffix = 0;; ++Suffix)
		{
			const FString Candidate = Suffix == 0 ? TEXT("Mask")
				: FString::Printf(TEXT("Mask%d"), Suffix);
			if (!Names.Contains(Candidate))
				return FText::FromString(Candidate);
		}
	}


	template<typename T>
	FText MakeGenericDefaultName(const TCHAR* Prefix, const int32 FirstSuffix, const TArray<T>& Entries)
	{
		for (int32 Suffix = FirstSuffix;; ++Suffix)
		{
			const FString Candidate = Suffix == 0
				? FString(Prefix)
				: FString::Printf(TEXT("%s%d"), Prefix, Suffix);
			if (!Entries.ContainsByPredicate([&Candidate](const T& Entry)
				{ return Entry.DisplayName.ToString().Equals(Candidate, ESearchCase::CaseSensitive); }))
			{
				return FText::FromString(Candidate);
			}
		}
	}


	DECLARE_DELEGATE_RetVal_TwoParams(bool, FCanDropIdGroupChild,
		const FMixtormatChildDragDropOp&, FMixtormatChildAddress);
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FDropIdGroupChild,
		const FMixtormatChildDragDropOp&, FMixtormatChildAddress);

	// Inside the existing row drop target: ID containment gets first refusal, while every
	// unrelated gesture continues to the original reorder/move handlers unchanged.
	class SMixtormatIdGroupChildDropTarget final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatIdGroupChildDropTarget) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(FMixtormatChildAddress, Address)
			SLATE_EVENT(FCanDropIdGroupChild, OnCanDrop)
			SLATE_EVENT(FDropIdGroupChild, OnIdDrop)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);

		virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& Event) override;

		virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override;

	private:
		FMixtormatChildAddress Address;
		FCanDropIdGroupChild OnCanDrop;
		FDropIdGroupChild OnIdDrop;
	};

	EMixtormatEffectType EffectTypeOf(const FMixtormatLayerChild& Child);

	bool IsFlowWarp(const FMixtormatLayerChild& Child);

	bool CanOwnScopedMasks(const FMixtormatLayerChild& Child);

	bool CanOwnScopedBlurs(const FMixtormatLayerChild& Child);

	bool CanOwnFlowWarp(const FMixtormatLayerChild& Child);

	bool IsMaskFilter(const FMixtormatLayerChild& Child);

	// The authored enable flag of any child, whatever payload carries it. One place, so a new
	// child type cannot be added to the stack and silently read or write the Mask payload's flag.
	bool IsChildEnabled(const FMixtormatLayerChild& Child);
	void SetChildEnabled(FMixtormatLayerChild& Child, bool bEnabled);

	int32 FindChildById(const TArray<FMixtormatLayerChild>& Children, const FGuid& ChildId);

	int32 GetScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex);

	TSharedRef<SWidget> MakeChildTypeIcon(const FMixtormatLayerChild& Child);

	FText OutputReferenceKindText(const FMixtormatLayerChild& Child);


	int32 GetDisplayScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex);

	FMixtormatLayerHierarchyPaint ChildHierarchyPaint(
		const TArray<FMixtormatLayerChild>& Children, int32 ChildIndex, bool bGroupShared = false);

	bool IsDescendantOf(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& AncestorId);

	int32 FindSubtreeEnd(const TArray<FMixtormatLayerChild>& Children, const int32 RootIndex);

	int32 FindSiblingRoot(
		const TArray<FMixtormatLayerChild>& Children,
		const int32 ChildIndex,
		const FGuid& ParentId);

	bool GetPublishedOutputSource(const FMixtormatLayerChild& Child, FGuid& OwnerId, FGuid& ChildId);

	const TArray<FMixtormatLayerChild>* FindChildrenInScope(const FMixtormatBindingScope& Scope, const FGuid OwnerId);

	TArray<FMixtormatLayerChild> CopyChildSubtree(TArray<FMixtormatLayerChild> Copies,
		const FGuid OldOwnerId, const FGuid NewOwnerId);

	bool IsRegionIdsReference(const FMixtormatLayerChild& Child);

	bool MakeRegionIdsReference(const FMixtormatLayerChild& Source,
		const FMixtormatChildAddress& Address, FMixtormatLayerChild& Reference);

	// The general form: builds an OutputReference to the first bCopyableAsField output of the
	// requested kind. MakeRegionIdsReference is the RegionIds specialisation of this.
	bool MakePublishedFieldReference(const FMixtormatLayerChild& Source,
		const FMixtormatChildAddress& Address, EMixtormatPublishedFieldKind Kind,
		FMixtormatLayerChild& Reference);

	bool ValidateRegionIdsPlacement(const FMixtormatBindingScope& Scope,
		const FMixtormatLayerChild& Child, const FGuid OwnerId, const int32 InsertIndex);

	bool IsPublishedSourceEnabled(const FMixtormatBindingScope& Scope, const FGuid OwnerId, const FGuid ChildId,
		TSet<FGuid>* ActiveSources = nullptr);

	bool CanReadPublishedOutputAt(
		const FMixtormatBindingScope& Scope,
		const FMixtormatLayerChild& Child,
		const FGuid DestOwnerId,
		const int32 InsertIndex);

	bool PublishedOutputPlacementsValid(const FMixtormatBindingScope& Scope);

	bool CanAddScopedChild(const TArray<FMixtormatLayerChild>& Children, const int32 OwnerIndex);

	bool IsIdGroupChild(const FMixtormatLayerChild& Child);

	int32 InsertScopedChild(
		TArray<FMixtormatLayerChild>& Children,
		const int32 OwnerChildIndex,
		FMixtormatLayerChild&& Child);

	FMixtormatLayerChild MakeScopedPrototype(const EMixtormatLayerChildType ChildType);

	FMixtormatLayerChild MakeFlowWarpPrototype();

	bool CanKeepScopedPlacement(
		const FMixtormatLayerChild& Owner,
		const FMixtormatLayerChild& Child);

	EMixtormatChildCreation CreationKindForGenerator(const EMixtormatGeneratorType Type);

	EMixtormatLayerChildType ChildTypeForCreation(const EMixtormatChildCreation Kind);

	void ApplyChildCreationDefaults(
		FMixtormatLayerChild& Child,
		const EMixtormatChildCreation Kind);

	void LinkChildPair(
		FMixtormatLayerChild& Child,
		const FGuid& ContainerId,
		const EMixtormatParameterOwnerType Owner,
		const FName XName,
		const FName YName,
		const EMixtormatParameterValueType ValueType);

	void ApplyLinkDefaults(FMixtormatLayerChild& Child, const FGuid& ContainerId);

	bool BuildMaskLayerFromPath(const FSoftObjectPath& MaskPath, FMixtormatMaskLayer& OutMask);
}
