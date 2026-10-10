// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "Widgets/Layers/MixtormatStructuralConnectionProjection.h"
#include "Style/MixtormatTypography.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Layout/SScrollBox.h"
#include "UI/Layers/SMixtormatLayerIcon.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace MixtormatLayersPrivate
{

	// Every child shows what kind of thing it is, masks included. An 11px thumbnail of a mask is a
	// grey smudge that says less than the glyph does, and it cost a pooled thumbnail per row; which
	// mask it actually is now answers on hover, at a size worth looking at.

	TSharedRef<SWidget> MakeChildTypeIcon(const FMixtormatLayerChild& Child)
	{
		// Generated Mask and IDs are different categories and no longer share a glyph: the first
		// emits 0..1 coverage into the mask chain, the second emits an ID map. Everything that only
		// reads or coarsens an ID map -- the HSV/Ramp/UV/Relief/Boundary consumers, Color ID,
		// Cluster and Surface IDs -- is the ID family, not the Generated family. Craquelure stays
		// beside Generated because it is a coverage producer, and an Output Reference stays there
		// because it can name a Flow or UV field as readily as an ID map.
		const FSlateBrush* Specific = nullptr;
		if (Child.Type == EMixtormatLayerChildType::HeightPush)
		{
			Specific = MixtormatIcons::WarpPush();
		}
		else if (Child.Type == EMixtormatLayerChildType::StructuralWarp)
		{
			Specific = MixtormatIcons::WarpStructural();
		}
		else if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			switch (EffectTypeOf(Child))
			{
			case EMixtormatEffectType::ShapeDeform: Specific = MixtormatIcons::WarpDeform(); break;
			case EMixtormatEffectType::GravityFlow: Specific = MixtormatIcons::FlowGravity(); break;
			case EMixtormatEffectType::FlowCarve: Specific = MixtormatIcons::FlowDirection(); break;
			case EMixtormatEffectType::GeneratorFlow: Specific = MixtormatIcons::FlowDirection(); break;
			case EMixtormatEffectType::FlowWarp: Specific = MixtormatIcons::WarpStructural(); break;
			default: break;
			}
		}
		return SNew(SImage)
			.Image(Specific ? Specific
				: Child.Type == EMixtormatLayerChildType::Generator
				? MixtormatIcons::Generator()
				: Child.Type == EMixtormatLayerChildType::Effect
				? MixtormatIcons::Effect()
				: (Child.Type == EMixtormatLayerChildType::PatternId
						|| Child.Type == EMixtormatLayerChildType::ColorId
						|| Child.Type == EMixtormatLayerChildType::Filter
						|| Child.Type == EMixtormatLayerChildType::HsvFilter
						|| Child.Type == EMixtormatLayerChildType::RandomId
						|| Child.Type == EMixtormatLayerChildType::RampId
						|| Child.Type == EMixtormatLayerChildType::UvFromIds
						|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
						|| Child.Type == EMixtormatLayerChildType::BoundaryFromIds

						|| Child.Type == EMixtormatLayerChildType::IdGroup)
					? MixtormatIcons::Ids()
					: (Child.Type == EMixtormatLayerChildType::Generated
							|| Child.Type == EMixtormatLayerChildType::Craquelure
							|| Child.Type == EMixtormatLayerChildType::OutputReference)
						? MixtormatIcons::Generated()
						: MixtormatIcons::Mask())
			.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text)));
	}

	FText OutputReferenceKindText(const FMixtormatLayerChild& Child)
	{
		switch (Child.OutputReference.Kind)
		{
		case EMixtormatPublishedFieldKind::RegionIds: return LOCTEXT("OutputReferenceIdsKind", "REF · IDs");
		case EMixtormatPublishedFieldKind::Flow:      return LOCTEXT("OutputReferenceFlowKind", "REF · FLOW");
		case EMixtormatPublishedFieldKind::UVMap:     return LOCTEXT("OutputReferenceUvKind", "REF · UVs");
		case EMixtormatPublishedFieldKind::Color:     return LOCTEXT("OutputReferenceColorKind", "REF · CLR");
		case EMixtormatPublishedFieldKind::Scalar01:     return LOCTEXT("OutputReferenceScalar01Kind", "REF · S01");
		case EMixtormatPublishedFieldKind::ScalarSigned: return LOCTEXT("OutputReferenceScalarSignedKind", "REF · SGN");
		case EMixtormatPublishedFieldKind::SDF:          return LOCTEXT("OutputReferenceSdfKind", "REF · SDF");
		case EMixtormatPublishedFieldKind::Vector2:      return LOCTEXT("OutputReferenceVector2Kind", "REF · V2");
		}
		return LOCTEXT("OutputReferenceKind", "REF · FIELD");
	}


	// Indentation represents authored scope, not an ID producer-consumer relationship.

	int32 GetDisplayScopeDepth(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		return GetScopeDepth(Children, ChildIndex);
	}

	FMixtormatLayerHierarchyPaint ChildHierarchyPaint(
		const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex, const bool bGroupShared)
	{
		FMixtormatLayerHierarchyPaint Paint;
		const Mixtormat::FMixtormatLayerMetrics& Layout = FMixtormatThemeStore::GetResolved().LayerLayout;
		const Mixtormat::FMixtormatHierarchyTheme& Hierarchy = FMixtormatThemeStore::GetResolved().LayerHierarchy;
		Paint.RowHeight = Layout.ChildRowHeight;
		Paint.BranchInset = Layout.PaddingX;
		if (!Children.IsValidIndex(ChildIndex))
		{
			return Paint;
		}
		const auto HasLaterSibling = [&Children, bGroupShared](const int32 Index)
		{
			const FGuid ParentId = Children[Index].ScopeOwnerChildId;
			// Shared roots are followed by the group's member layers on the same trunk.
			if (bGroupShared && !ParentId.IsValid()) { return true; }
			for (int32 Later = Index + 1; Later < Children.Num(); ++Later)
			{
				if (Children[Later].ScopeOwnerChildId == ParentId) { return true; }
			}
			return false;
		};
		const int32 Depth = GetDisplayScopeDepth(Children, ChildIndex);
		Paint.Indent = (1 + Depth) * Hierarchy.Indent;
		Paint.bLast = !HasLaterSibling(ChildIndex);
		Paint.bHasChildren = Children.IsValidIndex(ChildIndex + 1)
			&& Children[ChildIndex + 1].ScopeOwnerChildId == Children[ChildIndex].ChildId;
		int32 Current = ChildIndex;
		// Bound traversal also handles malformed/cyclic authored scopes without retaining pointers.
		for (int32 Level = Depth; Level > 0; --Level)
		{
			Current = FindChildById(Children, Children[Current].ScopeOwnerChildId);
			if (Current == INDEX_NONE) { break; }
			if (HasLaterSibling(Current))
			{
				Paint.AncestorIndents.Add(Level * Hierarchy.Indent);
			}
		}
		return Paint;
	}
}

namespace
{
	float ProjectedScopeIndent(const FMixtormatProjectedChildRow& Row)
	{
		const auto& Style = FMixtormatThemeStore::GetResolved();
		return Row.bInIncomingBlock
			? Style.LayerConnections.Indent + Style.LayerConnections.Inset + Row.ScopeDepthWithinIncoming * Style.LayerHierarchy.Indent
			: Row.AuthoredScopeDepth * Style.LayerHierarchy.Indent;
	}

	FMixtormatLayerHierarchyPaint ProjectedHierarchyPaint(const TArray<FMixtormatProjectedChildRow>& Rows, const int32 RowIndex)
	{
		const auto& Style = FMixtormatThemeStore::GetResolved();
		const auto BranchIndent = [&Rows, &Style](const int32 Index)
		{
			// Incoming operations are visually nested under the target while retaining
			// their authored address and execution position. They need the same
			// painted branch inset as any other projected child.
			return Style.LayerHierarchy.Indent + ProjectedScopeIndent(Rows[Index]);
		};
		const auto HasLaterSibling = [&Rows](const int32 Index)
		{
			for (int32 Later = Index + 1; Later < Rows.Num(); ++Later)
			{
				if (Rows[Later].VisualParentRowIndex == Rows[Index].VisualParentRowIndex) { return true; }
			}
			return false;
		};
		const auto FirstVisualChild = [&Rows](const int32 Index) -> int32
		{
			for (int32 Child = Index + 1; Child < Rows.Num(); ++Child)
			{
				if (Rows[Child].VisualParentRowIndex == Index)
				{ return Child; }
			}
			return INDEX_NONE;
		};
		FMixtormatLayerHierarchyPaint Paint;
		Paint.RowHeight = Style.LayerLayout.ChildRowHeight;
		Paint.BranchInset = Style.LayerLayout.PaddingX;
		Paint.Indent = BranchIndent(RowIndex);
		Paint.bLast = !HasLaterSibling(RowIndex);
		const int32 FirstChild = FirstVisualChild(RowIndex);
		Paint.bHasChildren = FirstChild != INDEX_NONE;
		if (Paint.bHasChildren) { Paint.ChildStemIndent = BranchIndent(FirstChild); }
		int32 Parent = Rows[RowIndex].VisualParentRowIndex;
		for (int32 Step = 0; Rows.IsValidIndex(Parent) && Step < Rows.Num(); ++Step)
		{
			const float Indent = BranchIndent(Parent);
			if (Indent > 0.0f && HasLaterSibling(Parent)) { Paint.AncestorIndents.AddUnique(Indent); }
			// Carry the first visual child's branch across other displayed rows.
			// This is presentation-only: it never changes module scope or ownership.
			const int32 ParentFirstChild = FirstVisualChild(Parent);
			if (ParentFirstChild > RowIndex) { Paint.AncestorIndents.AddUnique(BranchIndent(ParentFirstChild)); }
			Parent = Rows[Parent].VisualParentRowIndex;
		}
		return Paint;
	}
}

bool SMixtormat::ResolveHierarchyChildAddress(const FMixtormatChildAddress Address,
	int32& OutOwnerIndex, int32& OutChildIndex) const
{
	OutOwnerIndex = INDEX_NONE;
	OutChildIndex = INDEX_NONE;
	if (!Address.IsValid()) { return false; }
	const TArray<FMixtormatLayerChild>* Children = nullptr;
	if (Address.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
		{
			if (WorkingLayers[Index].LayerId != Address.OwnerId) { continue; }
			if (Children) { return false; }
			OutOwnerIndex = Index;
			Children = &WorkingLayers[Index].Children;
		}
	}
	else
	{
		for (int32 Index = 0; Index < WorkingLayerGroups.Num(); ++Index)
		{
			if (WorkingLayerGroups[Index].GroupId != Address.OwnerId) { continue; }
			if (Children) { return false; }
			OutOwnerIndex = Index;
			Children = &WorkingLayerGroups[Index].Children;
		}
	}
	if (!Children) { return false; }
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if ((*Children)[Index].ChildId != Address.ChildId) { continue; }
		if (OutChildIndex != INDEX_NONE) { return false; }
		OutChildIndex = Index;
	}
	return OutChildIndex != INDEX_NONE;
}

TArray<FMixtormatProjectedChildRow> SMixtormat::BuildGroupHierarchyRows(const FGuid GroupId) const
{
	TArray<FMixtormatProjectedChildRow> Rows;
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group) { return Rows; }
	// Shared structural payloads retain their authored home; only actual ownership is nested.
	for (int32 Index = 0; Index < Group->Children.Num(); ++Index)
	{
		FMixtormatProjectedChildRow Row;
		Row.Address = {EMixtormatChildOwnerType::Group, GroupId, Group->Children[Index].ChildId};
		Row.AuthoredChildIndex = Index;
		Row.AuthoredScopeDepth = GetDisplayScopeDepth(Group->Children, Index);
		const FGuid ParentId = Group->Children[Index].ScopeOwnerChildId;
		if (ParentId.IsValid())
		{
			int32 ParentIndex = INDEX_NONE;
			for (int32 Parent = 0; Parent < Group->Children.Num(); ++Parent)
			{
				if (Group->Children[Parent].ChildId != ParentId) { continue; }
				if (ParentIndex != INDEX_NONE) { ParentIndex = INDEX_NONE; break; }
				ParentIndex = Parent;
			}
			Row.VisualParentRowIndex = ParentIndex;
		}
		Rows.Add(Row);
	}
	return Rows;
}

FMixtormatLayerHierarchyPaint SMixtormat::BuildGroupHierarchyPaint(
	const TArray<FMixtormatProjectedChildRow>& VisibleRows, const int32 DisplayIndex) const
{
	FMixtormatLayerHierarchyPaint Paint = ProjectedHierarchyPaint(VisibleRows, DisplayIndex);
	// Shared roots continue on the group trunk to its member layers.
	if (VisibleRows[DisplayIndex].VisualParentRowIndex == INDEX_NONE) { Paint.bLast = false; }
	return Paint;
}

TArray<FMixtormatProjectedChildRow> SMixtormat::FilterVisibleHierarchyRows(
	const TArray<FMixtormatProjectedChildRow>& Rows) const
{
	TArray<FMixtormatProjectedChildRow> Visible;
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, Rows.Num());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		bool bHidden = false;
		int32 Parent = Rows[Index].VisualParentRowIndex;
		for (int32 Step = 0; Rows.IsValidIndex(Parent) && Step < Rows.Num(); ++Step)
		{
			if (CollapsedGeneratorAddresses.Contains(Rows[Parent].Address)) { bHidden = true; break; }
			Parent = Rows[Parent].VisualParentRowIndex;
		}
		if (bHidden) { continue; }
		Remap[Index] = Visible.Num();
		Visible.Add(Rows[Index]);
	}
	for (FMixtormatProjectedChildRow& Row : Visible)
	{
		Row.VisualParentRowIndex = Remap.IsValidIndex(Row.VisualParentRowIndex)
			? Remap[Row.VisualParentRowIndex] : INDEX_NONE;
	}
	return Visible;
}

bool SMixtormat::RevealChildInHierarchy(const FMixtormatChildAddress Address)
{
	int32 OwnerIndex, ChildIndex;
	if (!ResolveHierarchyChildAddress(Address, OwnerIndex, ChildIndex)) { return false; }
	const bool bLayer = Address.OwnerType == EMixtormatChildOwnerType::Layer;
	const TArray<FMixtormatProjectedChildRow> Rows = bLayer
		? MixtormatStructuralConnections::BuildChildProjection(WorkingLayers, WorkingLayerGroups, OwnerIndex)
		: BuildGroupHierarchyRows(Address.OwnerId);
	int32 RowIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (!(Rows[Index].Address == Address)) { continue; }
		if (RowIndex != INDEX_NONE) { return false; }
		RowIndex = Index;
	}
	if (RowIndex == INDEX_NONE) { return false; }
	bool bChanged = false;
	if (bLayer)
	{
		bChanged = !ExpandedLayerIds.Contains(Address.OwnerId);
		ExpandedLayerIds.Add(Address.OwnerId);
		bChanged |= CollapsedGroupIds.Remove(WorkingLayers[OwnerIndex].GroupId) > 0;
	}
	else { bChanged = CollapsedGroupIds.Remove(Address.OwnerId) > 0; }
	int32 Parent = Rows[RowIndex].VisualParentRowIndex;
	for (int32 Step = 0; Rows.IsValidIndex(Parent) && Step < Rows.Num(); ++Step)
	{
		bChanged |= CollapsedGeneratorAddresses.Remove(Rows[Parent].Address) > 0;
		Parent = Rows[Parent].VisualParentRowIndex;
	}
	return bChanged;
}

FReply SMixtormat::ToggleGeneratorExpanded(const FMixtormatChildAddress Address)
{
	int32 OwnerIndex, ChildIndex;
	if (!ResolveHierarchyChildAddress(Address, OwnerIndex, ChildIndex)) { return FReply::Unhandled(); }
	const auto& Children = Address.OwnerType == EMixtormatChildOwnerType::Layer
		? WorkingLayers[OwnerIndex].Children : WorkingLayerGroups[OwnerIndex].Children;
	if (Children[ChildIndex].Type != EMixtormatLayerChildType::Generator) { return FReply::Unhandled(); }
	if (!CollapsedGeneratorAddresses.Remove(Address)) { CollapsedGeneratorAddresses.Add(Address); }
	RebuildLayerList();
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildGeneratorDisclosure(const FMixtormatChildAddress Address,
	const bool bHasChildren, const FText ToolTip)
{
	int32 OwnerIndex, ChildIndex;
	if (!bHasChildren || !ResolveHierarchyChildAddress(Address, OwnerIndex, ChildIndex))
	{ return SNullWidget::NullWidget; }
	const auto& Children = Address.OwnerType == EMixtormatChildOwnerType::Layer
		? WorkingLayers[OwnerIndex].Children : WorkingLayerGroups[OwnerIndex].Children;
	if (Children[ChildIndex].Type != EMixtormatLayerChildType::Generator) { return SNullWidget::NullWidget; }
	return SNew(SMixtormatLayerIcon)
		.MaxSize(FMixtormatThemeStore::GetResolved().LayerLayout.ChildRowHeight)
		.ToolTipText(ToolTip.IsEmpty() ? LOCTEXT("GeneratorDisclosure", "Show or hide this generator's displayed children.") : ToolTip)
		.Icon_Lambda([this, Address]()
		{
			return CollapsedGeneratorAddresses.Contains(Address) ? MixtormatIcons::ChevronRight() : MixtormatIcons::ChevronDown();
		})
		.OnClicked_Lambda([this, Address]() { ToggleGeneratorExpanded(Address); });
}

void SMixtormat::RegisterChildRowWidget(const FMixtormatChildAddress Address, TSharedRef<SWidget> Widget)
{
	if (AmbiguousChildRowAddresses.Contains(Address)) { return; }
	if (ChildRowWidgets.Contains(Address))
	{
		ChildRowWidgets.Remove(Address);
		AmbiguousChildRowAddresses.Add(Address);
		return;
	}
	ChildRowWidgets.Add(Address, Widget);
}

FReply SMixtormat::NavigateToChild(const FMixtormatChildAddress Address)
{
	int32 OwnerIndex, ChildIndex;
	if (!LayerScrollBox.IsValid() || !ResolveHierarchyChildAddress(Address, OwnerIndex, ChildIndex)
		|| AmbiguousChildRowAddresses.Contains(Address)) { return FReply::Unhandled(); }
	const bool bRevealed = RevealChildInHierarchy(Address);
	const TWeakPtr<SWidget>* ExistingRow = ChildRowWidgets.Find(Address);
	if (bRevealed || !ExistingRow || !ExistingRow->IsValid()) { RebuildLayerList(); }
	const TWeakPtr<SWidget>* Row = ChildRowWidgets.Find(Address);
	const TSharedPtr<SWidget> Widget = Row ? Row->Pin() : nullptr;
	if (!Widget.IsValid() || AmbiguousChildRowAddresses.Contains(Address)) { return FReply::Unhandled(); }
	if (Address.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		SelectedGroupId.Invalidate();
		SelectedGroupChildIndex = INDEX_NONE;
		SelectWorkingChild(OwnerIndex, ChildIndex);
	}
	else { SelectGroupChild(Address.OwnerId, ChildIndex); }
	LayerScrollBox->ScrollDescendantIntoView(Widget, true, EDescendantScrollDestination::IntoView);
	return FReply::Handled();
}

bool SMixtormat::IsSourceOfSelectedInstance(const FGuid& OwnerId, const FGuid& ChildId) const
{
	const FMixtormatLayerChild* Selected = ResolveChildAt(GetSelectedChildAddress());
	if (!Selected) { return false; }
	if (Selected->Type == EMixtormatLayerChildType::OutputReference && !Selected->IsInstance())
	{
		return Selected->OutputReference.SourceLayerId == OwnerId
			&& Selected->OutputReference.SourceChildId == ChildId;
	}
	return Selected->IsInstance() && Selected->SourceChildId == ChildId
		&& (!Selected->SourceLayerId.IsValid() || Selected->SourceLayerId == OwnerId);
}

TArray<int32> SMixtormat::GetSelectedLayerIndices() const
{
	TArray<int32> Indices;
	for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
	{
		if (SelectedLayerIds.Contains(WorkingLayers[Index].LayerId))
		{
			Indices.Add(Index);
		}
	}
	// Falling back to the inspector's layer keeps the button working before anything has been
	// multi-selected, which is the state the panel opens in.
	if (Indices.IsEmpty() && WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		Indices.Add(SelectedLayerIndex);
	}
	return Indices;
}

bool SMixtormat::IsGroupExpanded(const FGuid& GroupId) const
{
	return !CollapsedGroupIds.Contains(GroupId);
}

FReply SMixtormat::ToggleGroupExpanded(const FGuid GroupId)
{
	if (CollapsedGroupIds.Contains(GroupId))
	{
		CollapsedGroupIds.Remove(GroupId);
	}
	else
	{
		CollapsedGroupIds.Add(GroupId);
	}
	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::SelectLayerGroup(const FGuid GroupId)
{
	SelectedGroupId = GroupId;
	// The header, not one of its shared children.
	SelectedGroupChildIndex = INDEX_NONE;
	// One subject for the inspector: picking the group drops the layer-child selection rather
	// than leaving two things looking selected at once.
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	// One subject for the inspector: a group header claim drops a Sources shelf selection too.
	SelectedSourceId.Invalidate();
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	return FReply::Handled();
}

bool SMixtormat::IsLayerExpanded(const int32 LayerIndex) const
{
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& ExpandedLayerIds.Contains(WorkingLayers[LayerIndex].LayerId);
}

void SMixtormat::SetLayerExpanded(const int32 LayerIndex, const bool bExpanded)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return;
	}
	const FGuid& LayerId = WorkingLayers[LayerIndex].LayerId;
	if (bExpanded)
	{
		ExpandedLayerIds.Add(LayerId);
	}
	else
	{
		ExpandedLayerIds.Remove(LayerId);
	}
}

bool SMixtormat::IsLayerMultiSelected(const int32 LayerIndex) const
{
	return WorkingLayers.IsValidIndex(LayerIndex)
		&& SelectedLayerIds.Contains(WorkingLayers[LayerIndex].LayerId);
}

void SMixtormat::UpdateMultiSelection(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return;
	}
	const FGuid ClickedId = WorkingLayers[LayerIndex].LayerId;

	// Read live rather than plumbed through the row: OnSelected is a bare FSimpleDelegate, and it
	// runs synchronously out of the row's OnMouseButtonDown, so the keys held are still the keys
	// that were held for this click.
	const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
	const bool bToggle = Modifiers.IsControlDown() || Modifiers.IsCommandDown();
	const bool bExtend = Modifiers.IsShiftDown();

	if (bExtend && SelectionAnchorLayerId.IsValid())
	{
		const int32 AnchorIndex = WorkingLayers.IndexOfByPredicate(
			[this](const FMixtormatLayer& Candidate)
			{
				return Candidate.LayerId == SelectionAnchorLayerId;
			});
		if (AnchorIndex != INDEX_NONE)
		{
			SelectedLayerIds.Reset();
			const int32 First = FMath::Min(AnchorIndex, LayerIndex);
			const int32 Last = FMath::Max(AnchorIndex, LayerIndex);
			for (int32 Index = First; Index <= Last; ++Index)
			{
				SelectedLayerIds.Add(WorkingLayers[Index].LayerId);
			}
			return;
		}
	}

	if (bToggle)
	{
		// A plain toggle, including off. SelectedLayerIndex follows the click regardless, because
		// what the inspector shows and what the Group button acts on are two different questions.
		if (SelectedLayerIds.Contains(ClickedId))
		{
			SelectedLayerIds.Remove(ClickedId);
		}
		else
		{
			SelectedLayerIds.Add(ClickedId);
		}
		SelectionAnchorLayerId = ClickedId;
		return;
	}

	// Clicking inside an existing multi-selection keeps it. Right-click runs through here before
	// the context menu opens, so collapsing here would mean picking three layers and then being
	// offered "Create Group" for one of them -- the selection destroyed by the act of acting on it.
	if (SelectedLayerIds.Num() > 1 && SelectedLayerIds.Contains(ClickedId))
	{
		return;
	}

	SelectedLayerIds.Reset();
	SelectedLayerIds.Add(ClickedId);
	SelectionAnchorLayerId = ClickedId;
}

FReply SMixtormat::SelectWorkingLayer(const int32 LayerIndex)
{
	if (!WorkingLayers.IsValidIndex(LayerIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	UpdateMultiSelection(LayerIndex);
	SyncSelectedLayerControls();
	RebuildMaskList();
	// No RebuildLayerList here. Selection highlight is an attribute lambda on each row, so it
	// repaints on its own -- and a rebuild would destroy the row that is, right now, part way
	// through opening its context menu on its own SMenuAnchor.
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FReply SMixtormat::SelectWorkingChild(const int32 LayerIndex, const int32 ChildIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FReply::Handled();
	}

	const bool bWasBypassingChild = bBypassSelectedChild;
	bBypassSelectedChild = false;
	SelectedLayerIndex = LayerIndex;
	const bool bEffect = ResolveChild(LayerIndex, ChildIndex)->Type
		== EMixtormatLayerChildType::Effect;
	SelectedEffectIndex = bEffect ? ChildIndex : INDEX_NONE;
	SelectedMaskIndex = bEffect ? INDEX_NONE : ChildIndex;
	// A layer child claim drops a Sources shelf selection; the inspector shows one subject.
	SelectedSourceId.Invalidate();
	bHasSelectedLayer = true;
	SyncSelectedLayerControls();
	if (RevealChildInHierarchy(MakeChildAddress(LayerIndex, ChildIndex))) { RebuildLayerList(); }
	// No unconditional RebuildLayerList() here: every row's selected-tint and state is attribute-bound already,
	// so nothing needs new widgets. Rebuilding tore down the very row a right-click had just opened
	// its context menu on, closing it before it could show.
	if (bWasBypassingChild || DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput)
	{
		RefreshLayeredPreview(false);
	}
	return FReply::Handled();
}

FText SMixtormat::GetSelectedBadgeText() const
{
	// The inspector strip mirrors the row that selected it, so it prints the same derived mark --
	// the child's when a child is selected, the layer's otherwise.
	if (const FMixtormatLayerChild* GroupChild =
		SelectedLayerIndex == INDEX_NONE ? ResolveChild(INDEX_NONE, GetSelectedChildIndex()) : nullptr)
	{
		return MixtormatLayerBadges::ForChild(*GroupChild);
	}
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		return FText::GetEmpty();
	}
	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	const int32 ChildIndex = GetSelectedChildIndex();
	return Layer.Children.IsValidIndex(ChildIndex)
		? MixtormatLayerBadges::ForChild(Layer.Children[ChildIndex])
		: MixtormatLayerBadges::ForLayer(Layer);
}

void SMixtormat::SyncSelectedLayerControls()
{
	if (SelectedSourceId.IsValid())
	{
		// A source is the inspector's subject; the layer controls below read the layer stack and
		// have nothing to sync. The header still names what is selected.
		const FMixtormatSourceEntry* Source = GetSelectedSource();
		bHasSelectedLayer = false;
		if (SelectedSurfaceText.IsValid())
		{
			SelectedSurfaceText->SetText(Source
				? Source->DisplayName
				: LOCTEXT("NoSelectedSource", "No source selected"));
		}
		if (SelectedIdentityText.IsValid())
		{
			SelectedIdentityText->SetText(LOCTEXT("SourceIdentity", "SOURCE"));
		}
		return;
	}
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		bHasSelectedLayer = false;
		return;
	}

	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	CurrentTiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
	CurrentRoughnessBias = Layer.RoughnessBias;
	CurrentRoughnessContrast = Layer.RoughnessContrast;
	CurrentRoughnessOffset = Layer.RoughnessOffset;

	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(GetLayerDisplayName(SelectedLayerIndex));
	}
	if (SelectedIdentityText.IsValid())
	{
		// The row's source field, not the layer's kind: "MAT - RUST ORANGE" answers what the layer
		// is made of, which is what the stack prints in the same position.
		SelectedIdentityText->SetText(GetLayerSourceText(SelectedLayerIndex));
	}
	if (SelectedThumbnailBox.IsValid())
	{
		const int32 RowThumbnailCount = LayerThumbnails.Num();
		SelectedThumbnailBox->SetContent(BuildLayerThumbnail(SelectedLayerIndex));
		// BuildLayerThumbnail parks what it made in the row list. The strip is not a row and is
		// remade on every selection, so its thumbnail moves to its own slot and replaces the last
		// one instead of piling up until the stack next rebuilds.
		if (LayerThumbnails.Num() > RowThumbnailCount)
		{
			SelectedStripThumbnail = LayerThumbnails.Pop();
		}
	}
	const int32 SelectedChildIndex = GetSelectedChildIndex();
	if (Layer.Children.IsValidIndex(SelectedChildIndex))
	{
		const FMixtormatLayerChild& Child = Layer.Children[SelectedChildIndex];
		if (SelectedSurfaceText.IsValid())
		{
			SelectedSurfaceText->SetText(GetLayerChildName(Child));
		}
		if (SelectedIdentityText.IsValid())
		{
			// Mirror the row's target/owner label so nested selection keeps its context.
			SelectedIdentityText->SetText(
				GetLayerChildSourceText(SelectedLayerIndex, SelectedChildIndex));
		}
	}
}

TSharedRef<SWidget> SMixtormat::BuildInstanceBanner()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	// A band above the rows rather than a wash over them: the values still have to be read, and
	// what changes is who may write them.
	auto Action = [this, &Style](const FText& Label, const FText& Hint, TFunction<void()> OnClick)
	{
		return SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
			.ToolTipText(Hint)
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(Label)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
			];
	};

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return IsSelectedChildInstance() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().CardLayout.Gap, 0.0f, FMixtormatThemeStore::GetResolved().CardLayout.Gap, FMixtormatThemeStore::GetResolved().CardLayout.Gap))
		[
			SNew(SBorder)
			.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().CardLayout.Gap))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetSelectedInstanceSourceText(); })
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				.Padding(0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"InstanceReadOnlyHint",
						"Inherited from the source and read-only here. Break Instance to edit a copy."))
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				.Padding(0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f)
					[
						Action(
							LOCTEXT("InstanceGoToSource", "Go to Source"),
							LOCTEXT("InstanceGoToSourceHint", "Select the child this instance mirrors."),
							[this]() { GoToChildInstanceSource(GetSelectedChildAddress()); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f)
					[
						Action(
							LOCTEXT("InstanceBreak", "Break Instance"),
							LOCTEXT("InstanceBreakHint", "Keep the values it is showing as this child's own and edit them here."),
							[this]() { BreakChildInstanceAt(GetSelectedChildAddress()); })
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SMixtormatChip)
						.Text(LOCTEXT("InstanceReplaceSource", "Replace Source"))
						.OnGetMenuContent_Lambda([this]()
						{
							return BuildReplaceInstanceSourceMenu(GetSelectedChildAddress());
						})
					]
				]
			]
		];
}

FReply SMixtormat::ToggleLayerExpanded(const int32 LayerIndex)
{
	SetLayerExpanded(LayerIndex, !IsLayerExpanded(LayerIndex));
	RebuildLayerList();
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildLayerThumbnail(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

	// A fill layer has no asset to preview, so its own colour is the thumbnail. Read through a
	// lambda rather than captured, because the colour picker edits it live.
	if (Layer.Type == EMixtormatLayerType::Generator)
	{
		return SNew(SBox)
			.WidthOverride(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize)
			.HeightOverride(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SImage).Image(MixtormatIcons::Generator())
				.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
			];
	}
	if (Layer.Type == EMixtormatLayerType::Fill)
	{
		return SNew(SColorBlock)
			.Color_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].BaseColor
					: FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
			})
			.Size(FVector2D(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize, FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize));
	}

	// Thumbnails are pooled and must be kept alive for as long as the widget is: LayerThumbnails
	// is that ownership, and RebuildLayerList resets it in step with the rows.
	const int32 Size = static_cast<int32>(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize);
	if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
		&& Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
		&& !Layer.NormalTexture.IsNull())
	{
		if (UTexture2D* Texture = Layer.NormalTexture.LoadSynchronous())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail =
				MakeShared<FAssetThumbnail>(FAssetData(Texture), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}
	else if (!Layer.SourceComposition.IsNull())
	{
		if (UMixtormatMaterial* Composition = Layer.SourceComposition.LoadSynchronous())
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				FAssetData(Composition), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}
	else if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
	{
		if (Surface->PreviewMaterial)
		{
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(
				FAssetData(Surface->PreviewMaterial.Get()), Size, Size, ThumbnailPool);
			LayerThumbnails.Add(Thumbnail);
			return Thumbnail->MakeThumbnailWidget(MixtormatUI::CleanThumbnailConfig());
		}
	}

	return SNew(SColorBlock)
		.Color(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel))
		.Size(FVector2D(FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize, FMixtormatThemeStore::GetResolved().LayerLayout.ThumbnailSize));
}

FText SMixtormat::GetLayerDisplayName(const int32 LayerIndex) const
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (Layer.SourceComposition.IsNull())
	{
		return Layer.DisplayName;
	}
	const FText Name = Layer.DisplayName.IsEmpty()
		? FText::FromString(Layer.SourceComposition.ToSoftObjectPath().GetAssetName())
		: Layer.DisplayName;
	return FText::Format(LOCTEXT("ReferenceLayerName", "{0} (ref)"), Name);
}

FText SMixtormat::GetLayerSourceText(const int32 LayerIndex) const
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];

	if (!Layer.SourceComposition.IsNull())
	{
		const UMixtormatMaterial* Composition = Layer.SourceComposition.LoadSynchronous();
		const FText Name = Composition && !Composition->DisplayName.IsEmpty()
			? Composition->DisplayName
			: FText::FromString(Layer.SourceComposition.ToSoftObjectPath().GetAssetName());
		return Composition
			? FText::Format(LOCTEXT("ReferenceLayerSource", "REF - {0}"), Name)
			: FText::Format(LOCTEXT("MissingReferenceLayerSource", "MISSING REF - {0}"), Name);
	}

	// What the layer is made of, which is a different question from what it is called. A surface
	// name when there is one, because that is the answer a user is scanning for; the layer's kind
	// only when there is no asset behind it to name.
	if (Layer.Type == EMixtormatLayerType::Fill)
	{
		return LOCTEXT("FillLayerSource", "FILL");
	}
	if (Layer.Type == EMixtormatLayerType::Generator)
	{
		return FText::GetEmpty();
	}
	if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail
		&& Layer.NormalSourceType == EMixtormatNormalSourceType::Texture
		&& !Layer.NormalTexture.IsNull())
	{
		return FText::FromString(Layer.NormalTexture.ToSoftObjectPath().GetAssetName().ToUpper());
	}
	if (const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous())
	{
		const FText Name = Surface->DisplayName.IsEmpty()
			? FText::FromString(Layer.SourceSurface.ToSoftObjectPath().GetAssetName())
			: Surface->DisplayName;
		return FText::FromString(Name.ToString().ToUpper());
	}
	return LOCTEXT("MaterialLayerSource", "MATERIAL");
}

FText SMixtormat::GetLayerChildSourceText(
	const int32 LayerIndex,
	const int32 ChildIndex) const
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return FText::GetEmpty();
	}

	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
	if (Child.Type == EMixtormatLayerChildType::OutputReference)
	{
		return OutputReferenceKindText(Child);
	}
	if (Child.Type == EMixtormatLayerChildType::Mask && Child.Mask.UsesNoise())
	{
		return Child.ScopeOwnerChildId.IsValid() ? LOCTEXT("NoiseGateSource", "NOISE · GATE")
			: LOCTEXT("NoiseMaskSource", "NOISE");
	}
	if (!Child.ScopeOwnerChildId.IsValid())
	{
		return IsFlowWarp(Child)
			? LOCTEXT("FlowWarpLayerTarget", "TARGET · LAYER")
			: MixtormatLayerBadges::KindForChild(Child);
	}

	const int32 OwnerIndex = FindChildById(Layer.Children, Child.ScopeOwnerChildId);
	if (!Layer.Children.IsValidIndex(OwnerIndex))
	{
		return LOCTEXT("MissingScopeOwner", "OWNER MISSING");
	}
	const FMixtormatLayerChild& Owner = Layer.Children[OwnerIndex];
	if (IsGeneratorFlow(Child))
	{
		return LOCTEXT("GeneratorFlowTarget", "TARGET · GEN");
	}
	if (IsFlowWarp(Child))
	{
		return Owner.Type == EMixtormatLayerChildType::Mask
			? LOCTEXT("FlowWarpMaskTarget", "TARGET · MASK")
			: LOCTEXT("FlowWarpEffectTarget", "TARGET · FX");
	}
	if (Child.Type == EMixtormatLayerChildType::Mask)
	{
		if (Owner.Type == EMixtormatLayerChildType::Generator)
		{
			// Not "GATES". A mask under an effect decides where that effect is allowed to act;
			// a mask under a generator steers it -- seed placement, propagation cost, carve
			// depth -- and only reaches a plain multiply at the very end. Calling both gating
			// would teach the wrong thing about Mask Influence on the one row where it matters.
			return LOCTEXT("GeneratorSteerMask", "STEERS · GEN");
		}
		return IsFlowWarp(Owner)
			? LOCTEXT("FlowWarpGateMask", "GATES · WARP")
			: LOCTEXT("EffectGateMask", "GATES · FX");
	}
	if (IsMaskFilter(Child))
	{
		return LOCTEXT("MaskFilterSource", "FILTER · MASK");
	}
	return MixtormatLayerBadges::KindForChild(Child);
}

TSharedRef<SWidget> SMixtormat::BuildLayerChildIcon(const int32 LayerIndex, const int32 ChildIndex)
{
	return MakeChildTypeIcon(*ResolveChild(LayerIndex, ChildIndex));
}

TSharedPtr<IToolTip> SMixtormat::BuildMaskPreviewTooltip(const int32 LayerIndex, const int32 ChildIndex)
{
	// Which mask this row is carrying, answered by showing it. The row itself only has room for a
	// glyph, so the picture is what hovering buys -- at the size the picker draws one, since the
	// question being asked is the same question the picker answers.
	const FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	if (Child.Type != EMixtormatLayerChildType::Mask)
	{
		return nullptr;
	}

	if (Child.Mask.UsesNoise() || (Child.Mask.HasPublishedSource() && Child.Mask.PublishedSourceOutput == TEXT("Value")))
	{
		return SNew(SToolTip)
			.Text(FText::Format(LOCTEXT("NoiseMaskRowTooltip", "{0}\nCoverage mask; shaping and scoped filters are preserved. Inline Noise uses raw Value, not Height Scale or Normalize. Signed Value maps to 0..1, unsigned Value is clamped. A gate affects only its actual scoped owner."),
				GetMaskSourceLabel(MakeChildAddress(LayerIndex, ChildIndex))));
	}

	const FSoftObjectPath MaskPath = !Child.Mask.Mask.IsNull()
		? Child.Mask.Mask.ToSoftObjectPath()
		: Child.Mask.MaskTexture.ToSoftObjectPath();
	UObject* MaskObject = MaskPath.TryLoad();
	if (!MaskObject)
	{
		return nullptr;
	}

	UTexture2D* Texture = Cast<UTexture2D>(MaskObject);
	if (const UMixtormatMask* MaskAsset = Cast<UMixtormatMask>(MaskObject))
	{
		Texture = MaskAsset->Thumbnail ? MaskAsset->Thumbnail.Get() : MaskAsset->MaskTexture.Get();
	}
	if (!Texture)
	{
		return nullptr;
	}

	const int32 Size = static_cast<int32>(FMixtormatThemeStore::GetResolved().GalleryLayout.TileSize);
	TSharedPtr<FAssetThumbnail> Thumbnail =
		MakeShared<FAssetThumbnail>(FAssetData(Texture), Size, Size, ThumbnailPool);
	LayerThumbnails.Add(Thumbnail);

	return SNew(SToolTip)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(FMixtormatThemeStore::GetResolved().GalleryLayout.TileSize)
				.HeightOverride(FMixtormatThemeStore::GetResolved().GalleryLayout.TileSize)
				[
					Thumbnail->MakeThumbnailWidget()
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
				.Text(FText::FromString(MaskPath.GetAssetName()))
			]
		];
}

bool SMixtormat::IsGroupChildEnabled(const FMixtormatLayerChild& Child)
{
	return MixtormatLayersPrivate::IsChildEnabled(Child);
}

void SMixtormat::ClearLayerSelection()
{
	SelectedLayerIndex = INDEX_NONE;
	bHasSelectedLayer = false;
	SelectedMaskIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	SelectedLayerIds.Reset();
	SyncSelectedLayerControls();
	RebuildMaskList();
}

bool SMixtormat::HasAnySelection() const
{
	return WorkingLayers.IsValidIndex(SelectedLayerIndex) || SelectedGroupId.IsValid();
}

FReply SMixtormat::SelectGroupChild(const FGuid GroupId, const int32 ChildIndex)
{
	SelectedGroupId = GroupId;
	SelectedGroupChildIndex = ChildIndex;
	// ResolveChild only reaches a group's shared stack when there is no layer index, so the layer
	// lane has to be cleared before the mask/effect lanes can address a shared child at all.
	SelectedLayerIndex = INDEX_NONE;
	bHasSelectedLayer = false;
	// The inspector's payload panels are selected by these two lanes, not by SelectedGroupChildIndex:
	// every one of them except the effect panel reads SelectedMaskIndex, so every non-effect type
	// rides the mask lane -- the same split SelectWorkingChild makes for a layer's own children.
	// Resolving first also keeps an empty or missing group (FinishGroupChildEdit passes Num() - 1)
	// from pointing a lane at an index that is not there.
	const FMixtormatLayerChild* Child = ResolveChild(INDEX_NONE, ChildIndex);
	const bool bEffect = Child && Child->Type == EMixtormatLayerChildType::Effect;
	SelectedEffectIndex = bEffect ? ChildIndex : INDEX_NONE;
	SelectedMaskIndex = (Child && !bEffect) ? ChildIndex : INDEX_NONE;
	// A shared-child claim drops a Sources shelf selection; the inspector shows one subject.
	SelectedSourceId.Invalidate();
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	if (Child && RevealChildInHierarchy(MakeGroupChildAddress(GroupId, ChildIndex))) { RebuildLayerList(); }
	return FReply::Handled();
}

// A shared child's row. ID children can reorder within their scope or move into an ID Group
// in either container. Scoped mask/flow tools still travel with their owner. Ordinary shared
// children keep the existing header/layer gestures; only an ID Group accepts cross-group drops.
TSharedRef<SWidget> SMixtormat::BuildGroupChildRow(const FGuid GroupId, const int32 ChildIndex)
{
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (!Group || !Group->Children.IsValidIndex(ChildIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FMixtormatLayerChild& Child = Group->Children[ChildIndex];
	const FText ChildName = GetLayerChildName(Child);
	TSharedPtr<IToolTip> NoiseTooltip;
	if (Child.Type == EMixtormatLayerChildType::Mask && (Child.Mask.UsesNoise()
		|| (Child.Mask.HasPublishedSource() && Child.Mask.PublishedSourceOutput == TEXT("Value"))))
	{
		NoiseTooltip = SNew(SToolTip).Text(FText::Format(LOCTEXT("SharedNoiseMaskHint",
			"{0}\nShared coverage mask, projected into every member. Signed Noise Value maps to 0..1; unsigned Value is clamped. Shaping and scoped filters apply afterwards."),
			GetMaskSourceLabel(MakeGroupChildAddress(GroupId, ChildIndex))));
	}
	else if (Child.Type == EMixtormatLayerChildType::HeightPush || Child.Type == EMixtormatLayerChildType::StructuralWarp)
	{
		NoiseTooltip = SNew(SToolTip).Text(LOCTEXT("SharedStructuralUnsupported",
			"Shared-group structural endpoint authoring is unsupported: ordered target/reference provenance is not generalized to member projections. This authored operation remains at its group location; no per-layer copies are created."));
	}
	// Doubles as "can leave the group" on the layer-side targets, so it has to match every guard
	// MoveGroupChildToLayer applies -- otherwise a row lights up for a release that is then
	// refused. A top-level mask filter is unreachable today (one only ever arrives scoped), but
	// the two ends are kept in step rather than relying on that.
	const bool bCanReorder = IsIdGroupChild(Child)
		|| (!Child.ScopeOwnerChildId.IsValid() && !IsMaskFilter(Child));

	const FMixtormatChildAddress Address = MakeGroupChildAddress(GroupId, ChildIndex);
	const bool bHasOwnedChildren = Group->Children.ContainsByPredicate([&Child](const FMixtormatLayerChild& Candidate)
	{
		return Candidate.ScopeOwnerChildId.IsValid() && Candidate.ScopeOwnerChildId == Child.ChildId;
	});
	const TSharedRef<SWidget> Widget = SNew(SMixtormatGroupChildDropTarget)
		.GroupId(GroupId)
		.ChildIndex(ChildIndex)
		.OnChildReordered(this, &SMixtormat::ReorderGroupChild)
		[
			SNew(SMixtormatIdGroupChildDropTarget)
			.Address(MakeGroupChildAddress(GroupId, ChildIndex))
			.OnCanDrop(this, &SMixtormat::CanDropChildIntoIdGroup)
			.OnIdDrop(this, &SMixtormat::DropChildIntoIdGroup)
			[
			SNew(SMixtormatLayerChildRow)
			.Name(ChildName)
			.Disclosure()[BuildGeneratorDisclosure(Address, bHasOwnedChildren)]
			.ToolTip(NoiseTooltip)
			.Icon()[MakeChildTypeIcon(Child)]
			// The caller paints the branch in the existing scope gutter.
			// KindForChild rather than GetLayerChildSourceText: that one resolves scope owners through
			// a layer, and this child's container is a group.
			.Kind(Child.Type == EMixtormatLayerChildType::OutputReference
				? OutputReferenceKindText(Child) : MixtormatLayerBadges::KindForChild(Child))
			.Badge(MixtormatLayerBadges::ForChild(Child))
			.StructuralHighlightRole_Lambda([this, GroupId, ChildIndex]()
			{
				return GetStructuralHighlightRole(MakeGroupChildAddress(GroupId, ChildIndex));
			})
			.bActive_Lambda([this, GroupId, ChildIndex]()
			{
				const FMixtormatLayerGroup* Current =
					MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
				return Current && Current->Children.IsValidIndex(ChildIndex)
					&& IsGroupChildEnabled(Current->Children[ChildIndex]);
			})
			.bSelected_Lambda([this, GroupId, ChildIndex]()
			{
				return SelectedGroupId == GroupId && SelectedGroupChildIndex == ChildIndex;
			})
			.bInstanceSource_Lambda([this, GroupId, ChildIndex]()
			{
				const FMixtormatLayerGroup* Current =
					MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
				return Current && Current->Children.IsValidIndex(ChildIndex)
					&& IsSourceOfSelectedInstance(GroupId, Current->Children[ChildIndex].ChildId);
			})
			.OnSelected_Lambda([this, GroupId, ChildIndex]() { SelectGroupChild(GroupId, ChildIndex); })
			.OnToggleActive_Lambda([this, GroupId, ChildIndex]()
			{
				ToggleGroupChildEnabled(GroupId, ChildIndex);
			})
			.OnGetContextMenu_Lambda([this, GroupId, ChildIndex]()
			{
				return BuildGroupChildContextMenu(GroupId, ChildIndex);
			})
			// Shift + right button opens the published outputs directly, through the same builder
			// the full menu nests under "Outputs". Gated by whether this child publishes anything,
			// so a child with no copyable output keeps the normal menu instead of an empty popup.
			.OnGetOutputMenu_Lambda([this, GroupId, ChildIndex]()
			{
				return BuildCopyChildOutputMenu(MakeGroupChildAddress(GroupId, ChildIndex));
			})
			.bHasOutputMenu_Lambda([this, GroupId, ChildIndex]()
			{
				const FMixtormatLayerGroup* Current = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
				return Current && Current->Children.IsValidIndex(ChildIndex)
					&& !GetCopyableOutputs(GetChildCapabilities(Current->Children[ChildIndex])).IsEmpty();
			})
			// Decided here, not in the lambda: Child is a reference into an array the row outlives,
			// and the answer cannot change without the row being rebuilt anyway.
			.OnDragDetected_Lambda([this, GroupId, ChildIndex, ChildName, bCanReorder]
				(const FGeometry&, const FPointerEvent&)
			{
				return FReply::Handled().BeginDragDrop(
					FMixtormatChildDragDropOp::NewFromGroup(GroupId, ChildIndex, ChildName, bCanReorder));
			})
			]
		];
	RegisterChildRowWidget(Address, Widget);
	return Widget;
}

TSharedRef<SWidget> SMixtormat::BuildLayerGroupRow(const FGuid GroupId)
{
	const TSharedRef<SMixtormatLayerGroupRow> Row = SNew(SMixtormatLayerGroupRow)
		.Name_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group ? Group->DisplayName : FText::GetEmpty();
		})
		.MemberCount_Lambda([this, GroupId]()
		{
			int32 Count = 0;
			for (const FMixtormatLayer& Layer : WorkingLayers)
			{
				Count += Layer.GroupId == GroupId ? 1 : 0;
			}
			return Count;
		})
		.bEnabled_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return !Group || Group->bEnabled;
		})
		.bExpanded_Lambda([this, GroupId]() { return IsGroupExpanded(GroupId); })
		.bSelected_Lambda([this, GroupId]() { return SelectedGroupId == GroupId; })
		.AccentColor_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group ? Group->AccentColor : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		})
		.OnSelected_Lambda([this, GroupId]() { SelectLayerGroup(GroupId); })
		.OnToggleExpanded_Lambda([this, GroupId]() { ToggleGroupExpanded(GroupId); })
		.OnToggleEnabled_Lambda([this, GroupId]()
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			SetLayerGroupEnabled(GroupId, Group ? !Group->bEnabled : true);
		})
		.OnNameCommitted_Lambda([this, GroupId](const FText& Text, ETextCommit::Type)
		{
			RenameLayerGroup(GroupId, Text);
		})
		.OnDragDetected_Lambda([this, GroupId](const FGeometry&, const FPointerEvent&)
		{
			const FMixtormatLayerGroup* Group =
				MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
			return Group
				? FReply::Handled().BeginDragDrop(
					FMixtormatGroupDragDropOp::New(GroupId, Group->DisplayName))
				: FReply::Unhandled();
		})
		.OnGetContextMenu(this, &SMixtormat::BuildLayerGroupContextMenu, GroupId);
	GroupRowWidgets.Add(GroupId, Row);
	return Row;
}

TSharedRef<SWidget> SMixtormat::BuildLayerRow(const int32 LayerIndex)
{
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	const FText DisplayName = GetLayerDisplayName(LayerIndex);
	const TWeakPtr<SMixtormat> WeakOwner = StaticCastSharedRef<SMixtormat>(AsShared());

	const FGuid LayerId = Layer.LayerId;
	TSharedPtr<SMixtormatLayerRow> Row;
	TSharedRef<SMixtormatLayerContainer> Container = SNew(SMixtormatLayerContainer)
		.bExpanded_Lambda([WeakOwner, LayerIndex]()
		{
			const TSharedPtr<SMixtormat> Owner = WeakOwner.Pin();
			return Owner.IsValid() && Owner->IsLayerExpanded(LayerIndex);
		})
		.Header()
		[
			SAssignNew(Row, SMixtormatLayerRow)
			.Name(DisplayName)
			// The raw authored name, not the decorated one above: committing "Foo (ref)" back
			// would write the decoration into the layer and it would grow on every rename.
			.EditableName_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					? WorkingLayers[LayerIndex].DisplayName
					: FText::GetEmpty();
			})
			.OnNameCommitted_Lambda([this, LayerId](const FText& Text, ETextCommit::Type)
			{
				RenameLayer(LayerId, Text);
			})
			.bReference(!Layer.SourceComposition.IsNull())
			.bHoldsInstanceSource_Lambda([this, LayerIndex]()
			{
				// A collapsed layer hides the source row that would otherwise carry the marker.
				const FMixtormatLayerChild* Selected =
					ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
				return Selected && Selected->IsInstance()
					&& WorkingLayers.IsValidIndex(LayerIndex)
					&& !IsLayerExpanded(LayerIndex)
					&& (Selected->SourceLayerId.IsValid()
						? Selected->SourceLayerId == WorkingLayers[LayerIndex].LayerId
						: SelectedLayerIndex == LayerIndex);
			})
			.bStructuralSource_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex) && !IsLayerExpanded(LayerIndex)
					&& IsSelectedStructuralSourceLayer(WorkingLayers[LayerIndex].LayerId, WorkingLayers[LayerIndex].GroupId);
			})
			.Source(GetLayerSourceText(LayerIndex))
			.Badge(MixtormatLayerBadges::ForLayer(Layer))
			.ColorBadge_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].Type != EMixtormatLayerType::Generator
					? MixtormatLayerBadges::ForColorBlendMode(
						WorkingLayers[LayerIndex].BaseColorBlendMode)
					: FText::GetEmpty();
			})
			.bCanDisable(true)
			.Thumbnail()[BuildLayerThumbnail(LayerIndex)]
			.bEnabled_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].bEnabled;
			})
			.bExpanded_Lambda([WeakOwner, LayerIndex]()
			{
				const TSharedPtr<SMixtormat> Owner = WeakOwner.Pin();
				return Owner.IsValid() && Owner->IsLayerExpanded(LayerIndex);
			})
			.bHasChildren_Lambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].Children.Num() > 0;
			})
			.OnGetBadgeMenu_Lambda([this, LayerIndex]() { return BuildLayerHeightOpMenu(LayerIndex); })
			// The colour-blend menu edits the selected layer, so the badge selects its layer first.
			.OnGetColorBadgeMenu_Lambda([this, LayerIndex]()
			{
				SelectWorkingLayer(LayerIndex);
				return BuildBaseColorBlendModeMenu();
			})
			.bSelected_Lambda([this, LayerIndex]()
			{
				return SelectedLayerIndex == LayerIndex || IsLayerMultiSelected(LayerIndex);
			})
			.bSolo_Lambda([this, LayerIndex]() { return SoloLayerIndex == LayerIndex; })
			.OnSelected_Lambda([this, LayerIndex]() { SelectWorkingLayer(LayerIndex); })
			.OnToggleExpanded_Lambda([this, LayerIndex]() { ToggleLayerExpanded(LayerIndex); })
			.OnToggleEnabled_Lambda([this, LayerIndex]()
			{
				if (!WorkingLayers.IsValidIndex(LayerIndex))
				{
					return;
				}
				SetWorkingLayerEnabled(
					WorkingLayers[LayerIndex].bEnabled ? ECheckBoxState::Unchecked : ECheckBoxState::Checked,
					LayerIndex);
			})
			.OnToggleSolo_Lambda([this, LayerIndex]() { ToggleLayerSolo(LayerIndex); })
			.OnGetContextMenu(this, &SMixtormat::BuildLayerContextMenu, LayerIndex)
			.OnDragDetected_Lambda([this, LayerIndex, DisplayName](const FGeometry&, const FPointerEvent&)
			{
				return FReply::Handled().BeginDragDrop(
					FMixtormatLayerDragDropOp::New(LayerIndex, DisplayName));
			})
		];

	LayerRowWidgets.Add(LayerId, Row);

	const TArray<FMixtormatProjectedChildRow> AllRows =
		MixtormatStructuralConnections::BuildChildProjection(WorkingLayers, WorkingLayerGroups, LayerIndex);
	const TArray<FMixtormatProjectedChildRow> ProjectedRows = FilterVisibleHierarchyRows(AllRows);
	for (int32 DisplayIndex = 0; DisplayIndex < ProjectedRows.Num(); ++DisplayIndex)
	{
		const FMixtormatProjectedChildRow& ProjectedRow = ProjectedRows[DisplayIndex];
		const int32 ChildIndex = ProjectedRow.AuthoredChildIndex;
		const FMixtormatLayerChild& Child = Layer.Children[ChildIndex];
		const bool bConnection = ProjectedRow.Kind != EMixtormatProjectedChildKind::Ordinary;
		FText ConnectionToolTip;
		const TSharedRef<SWidget> ConnectionContent = bConnection
			? BuildStructuralConnectionContent(ProjectedRow, Child.Type, ConnectionToolTip) : SNullWidget::NullWidget;
		const TSharedPtr<IToolTip> RowToolTip = bConnection
			? SMixtormatHelp::MakeStyledToolTip(ConnectionToolTip)
			: BuildMaskPreviewTooltip(LayerIndex, ChildIndex);
		const FMixtormatChildAddress RowAddress = ProjectedRow.Address;
		// New relation actions resolve the authored address at activation, never the visual index.
		const bool bAddressedConnection = bConnection && RowAddress.IsValid()
			&& ProjectedRow.Status.ModuleIssue != MixtormatOutputReferences::EStructuralLinkIssue::DuplicateIdentity;
		const auto ResolveRow = [this, RowAddress, LayerIndex, ChildIndex, bAddressedConnection](int32& OutLayer, int32& OutChild)
		{
			OutLayer = LayerIndex;
			OutChild = ChildIndex;
			// Ambiguous repair rows keep their exact authored lane, never the first GUID match.
			if (!bAddressedConnection)
			{
				return WorkingLayers.IsValidIndex(OutLayer) && WorkingLayers[OutLayer].LayerId == RowAddress.OwnerId
					&& WorkingLayers[OutLayer].Children.IsValidIndex(OutChild)
					&& WorkingLayers[OutLayer].Children[OutChild].ChildId == RowAddress.ChildId;
			}
			OutLayer = INDEX_NONE;
			OutChild = INDEX_NONE;
			for (int32 Index = 0; Index < WorkingLayers.Num(); ++Index)
			{
				if (WorkingLayers[Index].LayerId != RowAddress.OwnerId) { continue; }
				if (OutLayer != INDEX_NONE) { return false; }
				OutLayer = Index;
			}
			if (!WorkingLayers.IsValidIndex(OutLayer)) { return false; }
			for (int32 Index = 0; Index < WorkingLayers[OutLayer].Children.Num(); ++Index)
			{
				if (WorkingLayers[OutLayer].Children[Index].ChildId != RowAddress.ChildId) { continue; }
				if (OutChild != INDEX_NONE) { return false; }
				OutChild = Index;
			}
			return OutChild != INDEX_NONE;
		};
		const bool bEffect = Child.Type == EMixtormatLayerChildType::Effect;
		const bool bBlur = Child.Type == EMixtormatLayerChildType::Blur;
		const bool bCurvature = Child.Type == EMixtormatLayerChildType::Curvature;
		// Procedural children share row actions, not mask blending semantics.
		const bool bGenerated = Child.Type == EMixtormatLayerChildType::Generated
			|| Child.Type == EMixtormatLayerChildType::Craquelure
			|| Child.Type == EMixtormatLayerChildType::ColorId
			|| Child.Type == EMixtormatLayerChildType::Filter
			|| Child.Type == EMixtormatLayerChildType::HsvFilter
			|| Child.Type == EMixtormatLayerChildType::RandomId
			|| Child.Type == EMixtormatLayerChildType::RampId
			|| Child.Type == EMixtormatLayerChildType::UvFromIds
			|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
			|| Child.Type == EMixtormatLayerChildType::BoundaryFromIds
			|| Child.Type == EMixtormatLayerChildType::PatternId

			|| Child.Type == EMixtormatLayerChildType::IdGroup
			|| Child.Type == EMixtormatLayerChildType::OutputReference
			// A generator joins them for the row, not for the semantics. What the shared
			// procedural row gives it is the right ones: an enable toggle that writes its own
			// flag, a context menu with no blend mode on it, and the shared duplicate/instance
			// items. It is not a mask and must not fall through to the mask row, which would
			// toggle FMixtormatLayerChild::Mask on a child that has none.
			|| Child.Type == EMixtormatLayerChildType::Generator
			// The Generator sublayers are the same case: they carry their own enable flag, so
			// their row toggle has to route through the procedural path rather than SetMaskEnabled.
			|| Child.Type == EMixtormatLayerChildType::HeightBlend
			|| Child.Type == EMixtormatLayerChildType::HeightCurve
			|| Child.Type == EMixtormatLayerChildType::HeightColorRamp
		|| Child.Type == EMixtormatLayerChildType::HeightPush
		|| Child.Type == EMixtormatLayerChildType::StructuralWarp;
		const FText ChildName = GetLayerChildName(Child);
		const int32 FullIndex = AllRows.IndexOfByPredicate([ChildIndex](const FMixtormatProjectedChildRow& Candidate)
		{
			return Candidate.AuthoredChildIndex == ChildIndex;
		});
		int32 StoredIncoming = 0;
		int32 StoredWarp = 0;
		int32 StoredPush = 0;
		int32 ActiveIncoming = 0;
		int32 IncomingIssues = 0;
		TSet<FMixtormatChildAddress> CountedIncoming;
		bool bHasChildren = false;
		for (const FMixtormatProjectedChildRow& Candidate : AllRows)
		{
			if (Candidate.VisualParentRowIndex != FullIndex) { continue; }
			bHasChildren = true;
			if (Candidate.Kind != EMixtormatProjectedChildKind::IncomingConnection
				|| CountedIncoming.Contains(Candidate.Address)) { continue; }
			CountedIncoming.Add(Candidate.Address);
			++StoredIncoming;
			if (Layer.Children.IsValidIndex(Candidate.AuthoredChildIndex))
			{
				const EMixtormatLayerChildType RelationType = Layer.Children[Candidate.AuthoredChildIndex].Type;
				if (RelationType == EMixtormatLayerChildType::StructuralWarp) { ++StoredWarp; }
				else if (RelationType == EMixtormatLayerChildType::HeightPush) { ++StoredPush; }
			}
			if (Candidate.Status.bCanExecuteStructurally) { ++ActiveIncoming; }
			if (!Candidate.PresentationReason.IsEmpty()
				|| Candidate.Status.ModuleIssue != MixtormatOutputReferences::EStructuralLinkIssue::None
				|| Candidate.Status.Source.Issue != MixtormatOutputReferences::EStructuralLinkIssue::None
				|| Candidate.Status.Target.Issue != MixtormatOutputReferences::EStructuralLinkIssue::None) { ++IncomingIssues; }
		}
		const FText IncomingToolTip = StoredIncoming > 0 ? FText::Format(
			LOCTEXT("GeneratorIncomingSummary", "{0} stored incoming; {1} active-valid; {2} with issues."),
			FText::AsNumber(StoredIncoming), FText::AsNumber(ActiveIncoming), FText::AsNumber(IncomingIssues)) : FText::GetEmpty();
		// Operation-specific marks replace the redundant GEN suffix on a target.
		// The child rows remain the canonical place to edit connections.
		TSharedRef<SHorizontalBox> IncomingMarks = SNew(SHorizontalBox);
		const float MarkSize = FMixtormatThemeStore::GetResolved().ControlLayout.LayerChildIconSize;
		const FLinearColor MarkColor = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
		const auto AddIncomingMark = [&IncomingMarks, MarkSize, MarkColor](
			const FSlateBrush* Icon, const FText& Hint)
		{
			IncomingMarks->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SMixtormatHelp).Text(Hint)
				[
					SNew(SBox).WidthOverride(MarkSize).HeightOverride(MarkSize)
					[
						SNew(SImage).Image(Icon).ColorAndOpacity(FSlateColor(MarkColor))
					]
				]
			];
		};
		if (Child.Type == EMixtormatLayerChildType::Generator)
		{
			if (StoredWarp > 0)
			{
				AddIncomingMark(MixtormatIcons::WarpStructural(), FText::Format(
					LOCTEXT("TargetWarpIndicator", "{0} incoming Warp operations"),
					FText::AsNumber(StoredWarp)));
			}
			if (StoredPush > 0)
			{
				AddIncomingMark(MixtormatIcons::WarpPush(), FText::Format(
					LOCTEXT("TargetPushIndicator", "{0} incoming Height Push operations"),
					FText::AsNumber(StoredPush)));
			}
		}
		TSharedPtr<SMixtormatLayerChildRow> ChildRow;

		Container->AddChild(
			SNew(SMixtormatLayerHierarchy)
			.Hierarchy(ProjectedHierarchyPaint(ProjectedRows, DisplayIndex))
			[
			SNew(SMixtormatChildDropTarget)
			.LayerIndex(LayerIndex)
			.ChildIndex(ChildIndex)
			.OnChildReordered(this, &SMixtormat::ReorderLayerChild)
			.OnChildMovedToLayer(this, &SMixtormat::MoveChildToLayer)
			.OnGroupChildMovedToLayer(this, &SMixtormat::MoveGroupChildToLayer)
			[
				SNew(SMixtormatIdGroupChildDropTarget)
				.Address(MakeChildAddress(LayerIndex, ChildIndex))
				.OnCanDrop(this, &SMixtormat::CanDropChildIntoIdGroup)
				.OnIdDrop(this, &SMixtormat::DropChildIntoIdGroup)
				[
				SAssignNew(ChildRow, SMixtormatLayerChildRow)
				.Disclosure()[BuildGeneratorDisclosure(RowAddress, bHasChildren, IncomingToolTip)]
				.ExtraIndent(ProjectedScopeIndent(ProjectedRow))
				.bConnectionPresentation(bConnection)
				.ConnectionContent()[ConnectionContent]
				.ToolTip(RowToolTip)
				.Name(ChildName)
				.Kind(GetLayerChildSourceText(LayerIndex, ChildIndex))
				.Badge(MixtormatLayerBadges::ForChild(Child))
				.StructuralCount_Lambda([this, RowAddress, StoredIncoming]()
				{
					return StoredIncoming > 0 && CollapsedGeneratorAddresses.Contains(RowAddress)
						? FText::Format(LOCTEXT("CollapsedGeneratorIncoming", "{0} incoming"), FText::AsNumber(StoredIncoming))
						: FText::GetEmpty();
				})
				.StructuralCountToolTip(IncomingToolTip)
				.StructuralHighlightRole_Lambda([this, LayerIndex, ChildIndex]()
				{
					return GetStructuralHighlightRole(MakeChildAddress(LayerIndex, ChildIndex));
				})
				.StructuralLink()[bConnection ? SNullWidget::NullWidget
					: Child.Type == EMixtormatLayerChildType::Generator
						? StaticCastSharedRef<SWidget>(IncomingMarks)
						: BuildStructuralLinkChips(MakeChildAddress(LayerIndex, ChildIndex))]
				.Icon()[bConnection ? SNew(SImage)
					.Image(Child.Type == EMixtormatLayerChildType::HeightPush
						? MixtormatIcons::WarpPush() : MixtormatIcons::WarpStructural())
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
					: BuildLayerChildIcon(LayerIndex, ChildIndex)]
				// The caller paints the branch in the existing scope gutter.
				// Only children that have a blend mode get a badge menu: masks, and generated
				// masks that emit coverage (not filters, ID nodes or generators).
				.OnGetBadgeMenu(Child.Type == EMixtormatLayerChildType::Mask
					? FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskBlendModeMenu, LayerIndex, ChildIndex)
					: (Child.Type == EMixtormatLayerChildType::Generated
						|| Child.Type == EMixtormatLayerChildType::Craquelure
						|| Child.Type == EMixtormatLayerChildType::ColorId
						|| Child.Type == EMixtormatLayerChildType::RandomId)
						? FOnGetContent::CreateSP(this, &SMixtormat::BuildGeneratedBlendModeMenu, LayerIndex, ChildIndex)
						: FOnGetContent())
				.bActive_Lambda([this, LayerIndex, ChildIndex]()
				{
					return IsLayerChildEnabled(LayerIndex, ChildIndex);
				})
				.bSelected_Lambda([this, LayerIndex, ChildIndex, bEffect]()
				{
					// Effects and masks are selected through separate indices, so which one to
					// compare against depends on what the child is.
					return SelectedLayerIndex == LayerIndex
						&& (bEffect ? SelectedEffectIndex : SelectedMaskIndex) == ChildIndex;
				})
				.bPreviewing_Lambda([this, LayerIndex, ChildIndex]()
				{
					if (!WorkingLayers.IsValidIndex(LayerIndex)
						|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
					{
						return false;
					}
					// The two sources RefreshLayeredPreview feeds the compositor from: a child-output
					// preview names its child by GUID, a layer-mask preview by the selection.
					switch (DebugPreviewMode)
					{
					case EMixtormatDebugPreviewMode::ChildOutput:
						return ChildPreviewTarget.OwnerId == WorkingLayers[LayerIndex].LayerId
							&& ChildPreviewTarget.ChildId == WorkingLayers[LayerIndex].Children[ChildIndex].ChildId;
					case EMixtormatDebugPreviewMode::LayerMask:
						return SelectedLayerIndex == LayerIndex && GetSelectedChildIndex() == ChildIndex;
					default:
						return false;
					}
				})
				.bInstanceSource_Lambda([this, LayerIndex, ChildIndex]()
				{
					return WorkingLayers.IsValidIndex(LayerIndex)
						&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
						&& IsSourceOfSelectedInstance(
							WorkingLayers[LayerIndex].LayerId,
							WorkingLayers[LayerIndex].Children[ChildIndex].ChildId);
				})
				.OnSelected_Lambda([this, ResolveRow]()
				{
					int32 LayerIndex, ChildIndex;
					if (ResolveRow(LayerIndex, ChildIndex)) { SelectWorkingChild(LayerIndex, ChildIndex); }
				})
				.OnToggleActive_Lambda([this, ResolveRow, bEffect, bGenerated]()
				{
					int32 LayerIndex, ChildIndex;
					if (!ResolveRow(LayerIndex, ChildIndex)) { return; }
					const ECheckBoxState Next = IsLayerChildEnabled(LayerIndex, ChildIndex)
						? ECheckBoxState::Unchecked
						: ECheckBoxState::Checked;
					if (bEffect)
					{
						ToggleLayerEffect(LayerIndex, ChildIndex);
					}
					else if (bGenerated)
					{
						SetGeneratedEnabled(Next, LayerIndex, ChildIndex);
					}
					else
					{
						SetMaskEnabled(Next, LayerIndex, ChildIndex);
					}
				})
				.OnGetContextMenu_Lambda([this, ResolveRow, bEffect, bGenerated, bBlur, bCurvature]() -> TSharedRef<SWidget>
				{
					int32 LayerIndex, ChildIndex;
					if (!ResolveRow(LayerIndex, ChildIndex)) { return SNullWidget::NullWidget; }
					if (bBlur || bCurvature)
					{
						// One menu: a blur and a curvature offer the same actions, because what
						// they have in common -- scoped, consumed, instanceable -- is everything
						// the menu is about.
						return BuildBlurContextMenu(LayerIndex, ChildIndex);
					}
					if (bGenerated)
					{
						return BuildGeneratedContextMenu(LayerIndex, ChildIndex);
					}
					return bEffect
						? BuildEffectContextMenu(LayerIndex, ChildIndex)
						: BuildMaskContextMenu(LayerIndex, ChildIndex);
				})
				// Shift + right button opens the published outputs directly, through the same builder
				// the full menu nests under "Outputs". Gated by whether this child publishes anything,
				// so a child with no copyable output keeps the normal menu instead of an empty popup.
				.OnGetOutputMenu_Lambda([this, ResolveRow]() -> TSharedRef<SWidget>
				{
					int32 LayerIndex, ChildIndex;
					return ResolveRow(LayerIndex, ChildIndex) ? BuildCopyChildOutputMenu(MakeChildAddress(LayerIndex, ChildIndex)) : SNullWidget::NullWidget;
				})
				.bHasOutputMenu_Lambda([this, LayerIndex, ChildIndex]()
				{
					const FMixtormatLayerChild* Child = ResolveChild(LayerIndex, ChildIndex);
					return Child && !GetCopyableOutputs(GetChildCapabilities(*Child)).IsEmpty();
				})
				// Decided here, not in the lambda: Child is a reference into an array the row
				// outlives, and the answer cannot change without the row being rebuilt anyway.
				.OnDragDetected_Lambda(
					[this, ResolveRow, ChildName, bConnection,
										 bCanLeaveLayer = !IsMaskFilter(Child) && !IsGeneratorFlow(Child)]
					(const FGeometry&, const FPointerEvent&)
				{
					int32 LayerIndex, ChildIndex;
					if (!ResolveRow(LayerIndex, ChildIndex)) { return FReply::Unhandled(); }
					const FText DragLabel = bConnection ? FText::Format(LOCTEXT("StructuralExecutionDrag", "{0} — Move execution operation; target unchanged"), ChildName) : ChildName;
					return FReply::Handled().BeginDragDrop(
						FMixtormatChildDragDropOp::New(
							LayerIndex, ChildIndex, DragLabel, bCanLeaveLayer));
				})
				]
			]
		]);
		RegisterChildRowWidget(RowAddress, ChildRow.ToSharedRef());
	}

	return SNew(SMixtormatLayerRowDropTarget)
		.TargetLayerIndex(LayerIndex)
		.OnLayerInsertedAt(this, &SMixtormat::HandleLayerInsertedAt)
		.OnGroupInsertedAt(this, &SMixtormat::HandleGroupInsertedAt)
		.OnSurfaceInsertedAt(this, &SMixtormat::HandleSurfaceDroppedAt)
		.OnChildMovedToLayer(this, &SMixtormat::MoveChildToLayer)
		.OnGroupChildMovedToLayer(this, &SMixtormat::MoveGroupChildToLayer)
		.OnMaskDropped(this, &SMixtormat::AssignMaskToLayer)
		[
			Container
		];
}

bool SMixtormat::IsLayerChildEnabled(const int32 LayerIndex, const int32 ChildIndex) const
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return false;
	}
	return MixtormatLayersPrivate::IsChildEnabled(*ResolveChild(LayerIndex, ChildIndex));
}

#undef LOCTEXT_NAMESPACE
