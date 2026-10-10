// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/Layers/MixtormatStructuralConnectionModel.h"
#include "Widgets/Layers/MixtormatStructuralConnectionProjection.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatStructuralSourcePicker.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Style/MixtormatTypography.h"
#include "Style/MixtormatThemeStore.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

struct FMixtormatStructuralRelationshipPresentation
{
	FText Operation;
	FText Source;
	FText Target;
	FText Output;
	FText Summary;
	FText ToolTip;
	bool bHasIssue = false;
};

namespace
{
	using EIssue = MixtormatOutputReferences::EStructuralLinkIssue;
	using ERole = EMixtormatStructuralConnectionRole;

	bool IsStructuralModule(const FMixtormatLayerChild& Child)
	{
		return Child.Type == EMixtormatLayerChildType::HeightPush
			|| Child.Type == EMixtormatLayerChildType::StructuralWarp;
	}

	FText ConnectionIssueText(const EIssue Issue)
	{
		return MixtormatStructuralConnections::IssueText(Issue);
	}

	FText ConnectionMenuReason(const EIssue Issue)
	{
		switch (Issue)
		{
		case EIssue::None: return FText::GetEmpty();
		case EIssue::Unset: return LOCTEXT("StructuralMenuUnset", "Not connected");
		case EIssue::MissingLayer: return LOCTEXT("StructuralMenuMissingLayer", "Missing layer");
		case EIssue::MissingChild: return LOCTEXT("StructuralMenuMissingChild", "Missing child");
		case EIssue::DuplicateIdentity: return LOCTEXT("StructuralMenuDuplicate", "Ambiguous ID");
		case EIssue::DisabledLayer: return LOCTEXT("StructuralMenuDisabledLayer", "Layer disabled");
		case EIssue::DisabledSource: return LOCTEXT("StructuralMenuDisabledSource", "Source disabled");
		case EIssue::DisabledTarget: return LOCTEXT("StructuralMenuDisabledTarget", "Target disabled");
		case EIssue::DisabledReference: return LOCTEXT("StructuralMenuDisabledReference", "Link disabled");
		case EIssue::WrongOwnerLayer: return LOCTEXT("StructuralMenuWrongLayer", "Generator layer only");
		case EIssue::WrongModuleType: return LOCTEXT("StructuralMenuWrongModule", "Wrong module");
		case EIssue::ScopedModule: return LOCTEXT("StructuralMenuScopedModule", "Module scoped");
		case EIssue::WrongSourceKind: return LOCTEXT("StructuralMenuSourceKind", "Wrong output");
		case EIssue::WrongSourceScope: return LOCTEXT("StructuralMenuSourceScope", "Wrong source scope");
		case EIssue::IncompleteSourceScope: return LOCTEXT("StructuralMenuIncompleteScope", "Scope unfinished");
		case EIssue::InvalidSourceScope: return LOCTEXT("StructuralMenuInvalidScope", "Invalid scope");
		case EIssue::ForwardSource: return LOCTEXT("StructuralMenuForwardSource", "After module");
		case EIssue::ForwardTarget: return LOCTEXT("StructuralMenuForwardTarget", "Before module");
		case EIssue::WrongTargetKind: return LOCTEXT("StructuralMenuTargetKind", "Wrong target type");
		case EIssue::ScopedTarget: return LOCTEXT("StructuralMenuScopedTarget", "Target scoped");
		case EIssue::UnavailableEffectAsset: return LOCTEXT("StructuralMenuMissingEffect", "Missing effect");
		}
		return LOCTEXT("StructuralMenuUnsupported", "Unsupported");
	}


	FText ConnectionIssueCode(const EIssue Issue)
	{
		switch (Issue)
		{
		case EIssue::ForwardSource:
		case EIssue::ForwardTarget: return LOCTEXT("StructuralOrderCode", "ORDER");
		case EIssue::WrongSourceKind:
		case EIssue::WrongTargetKind: return LOCTEXT("StructuralTypeCode", "TYPE");
		case EIssue::WrongSourceScope:
		case EIssue::IncompleteSourceScope:
		case EIssue::InvalidSourceScope:
		case EIssue::ScopedModule:
		case EIssue::ScopedTarget: return LOCTEXT("StructuralScopeCode", "SCOPE");
		case EIssue::DisabledLayer:
		case EIssue::DisabledReference:
		case EIssue::DisabledSource:
		case EIssue::DisabledTarget: return LOCTEXT("StructuralOffCode", "OFF");
		case EIssue::None:
		case EIssue::Unset: return FText::GetEmpty();
		default: return LOCTEXT("StructuralBrokenCode", "BROKEN");
		}
	}

	EIssue ConnectionIssue(const MixtormatOutputReferences::FStructuralLinkStatus& Status, const ERole Role)
	{
		// Disabled modules/layers can still be configured. Producer inactivity is an edge issue.
		if (Status.ModuleIssue != EIssue::None && Status.ModuleIssue != EIssue::DisabledLayer)
		{
			return Status.ModuleIssue;
		}
		return Role == ERole::Target ? Status.Target.Issue : Status.Source.Issue;
	}
}

FText SMixtormat::GetStructuralChildLabel(const FMixtormatLayer& Layer, const int32 ChildIndex) const
{
	if (!Layer.Children.IsValidIndex(ChildIndex)) { return LOCTEXT("StructuralUnavailable", "Unavailable"); }
	const FText Name = GetLayerChildName(Layer.Children[ChildIndex]);
	int32 Matches = 0;
	int32 Ordinal = 0;
	for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
	{
		if (!GetLayerChildName(Layer.Children[Index]).EqualTo(Name)) { continue; }
		++Matches;
		if (Index <= ChildIndex) { ++Ordinal; }
	}
	return Matches > 1 ? FText::Format(LOCTEXT("StructuralChildOrdinal", "{0} {1}"), Name, FText::AsNumber(Ordinal)) : Name;
}

FText SMixtormat::GetStructuralConnectionLabel(
	const FMixtormatChildAddress Address, const ERole Role, const bool bCompact,
	FText* const OutFullLabel) const
{
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!Module || !IsStructuralModule(*Module))
	{
		if (OutFullLabel) { *OutFullLabel = FText::GetEmpty(); }
		return FText::GetEmpty();
	}

	const FString CacheBase = FString::Printf(TEXT("%d_%s_%s_%d"),
		static_cast<int32>(Address.OwnerType), *Address.OwnerId.ToString(), *Address.ChildId.ToString(),
		static_cast<int32>(Role));
	const FString FullCacheKey = CacheBase + TEXT("_Full");
	const FString CompactCacheKey = CacheBase + TEXT("_Compact");
	if (const FText* Cached = StructuralConnectionLabelCache.Find(bCompact ? CompactCacheKey : FullCacheKey))
	{
		if (OutFullLabel)
		{
			if (const FText* Full = StructuralConnectionLabelCache.Find(FullCacheKey)) { *OutFullLabel = *Full; }
		}
		return *Cached;
	}

	const auto CacheLabels = [this, bCompact, OutFullLabel, &FullCacheKey, &CompactCacheKey](
		const FText& FullLabel, const FText& CompactLabel)
	{
		StructuralConnectionLabelCache.Add(FullCacheKey, FullLabel);
		StructuralConnectionLabelCache.Add(CompactCacheKey, CompactLabel);
		if (OutFullLabel) { *OutFullLabel = FullLabel; }
		return bCompact ? CompactLabel : FullLabel;
	};

	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference& Source = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	const FGuid Target = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	if (Role == ERole::Target ? !Target.IsValid() : !Source.SourceLayerId.IsValid() || !Source.SourceChildId.IsValid())
	{
		const FText None = LOCTEXT("StructuralConnectionNone", "None");
		return CacheLabels(None, None);
	}
	const FMixtormatStructuralConnectionContext Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto Status = Projection.Evaluate();
	const auto& Edge = Role == ERole::Target ? Status.Target : Status.Source;
	if (!Projection.Effective.IsValidIndex(Edge.LayerIndex)
		|| !Projection.Effective[Edge.LayerIndex].Children.IsValidIndex(Edge.ChildIndex))
	{
		const EIssue Issue = ConnectionIssue(Status, Role);
		const FText FullLabel = FText::Format(LOCTEXT("StructuralUnavailableReason", "{0} — {1}"),
			LOCTEXT("StructuralUnavailable", "Unavailable"), ConnectionIssueText(Issue));
		const FText CompactLabel = ConnectionIssueCode(Issue).IsEmpty()
			? LOCTEXT("StructuralCompactUnavailable", "Unavailable") : ConnectionIssueCode(Issue);
		return CacheLabels(FullLabel, CompactLabel);
	}
	const FMixtormatLayer& Layer = Projection.Effective[Edge.LayerIndex];
	FText Label = GetStructuralChildLabel(Layer, Edge.ChildIndex);
	if (Role == ERole::Source)
	{
		if (Layer.LayerId != Address.OwnerId)
		{
			Label = FText::Format(LOCTEXT("StructuralOriginLabel", "{0} / {1}"), Layer.DisplayName, Label);
		}
		const FText Output = Source.Kind == EMixtormatPublishedFieldKind::ScalarSigned && Source.OutputName == TEXT("Height")
			? LOCTEXT("StructuralHeightOutput", "Height")
			: StaticEnum<EMixtormatPublishedFieldKind>()->GetDisplayNameTextByValue(static_cast<int64>(Source.Kind));
		Label = FText::Format(LOCTEXT("StructuralOutputLabel", "{0} · {1}"), Label, Output);
	}
	if (Edge.Issue == EIssue::None)
	{
		return CacheLabels(Label, Label);
	}
	const FText FullLabel = FText::Format(LOCTEXT("StructuralConnectionDiagnostic", "{0} · {1} — {2}"),
		ConnectionIssueCode(Edge.Issue), Label, ConnectionIssueText(Edge.Issue));
	const FText CompactLabel = FText::Format(LOCTEXT("StructuralCompactConnection", "{0} · {1}"),
		ConnectionIssueCode(Edge.Issue), Label);
	return CacheLabels(FullLabel, CompactLabel);
}

FText SMixtormat::GetStructuralSourceBreadcrumb(const FMixtormatLayer& Layer, const int32 ChildIndex) const
{
	if (!Layer.Children.IsValidIndex(ChildIndex)) { return LOCTEXT("StructuralUnavailable", "Unavailable"); }
	FText Label = GetStructuralChildLabel(Layer, ChildIndex);
	FGuid ParentId = Layer.Children[ChildIndex].ScopeOwnerChildId;
	TSet<FGuid> Visited;
	Visited.Add(Layer.Children[ChildIndex].ChildId);
	while (ParentId.IsValid() && !Visited.Contains(ParentId))
	{
		Visited.Add(ParentId);
		int32 ParentIndex = INDEX_NONE;
		int32 Matches = 0;
		for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
		{
			if (Layer.Children[Index].ChildId != ParentId) { continue; }
			ParentIndex = Index;
			++Matches;
		}
		if (Matches != 1) { break; }
		Label = FText::Format(LOCTEXT("StructuralPickerBreadcrumb", "{0} / {1}"),
			GetStructuralChildLabel(Layer, ParentIndex), Label);
		ParentId = Layer.Children[ParentIndex].ScopeOwnerChildId;
	}
	return Label;
}

FMixtormatStructuralRelationshipPresentation SMixtormat::DescribeStructuralConnection(const FMixtormatProjectedChildRow& Row,
	const EMixtormatLayerChildType Type) const
{
	const bool bPush = Type == EMixtormatLayerChildType::HeightPush;
	const FText Operation = bPush ? LOCTEXT("StructuralRowPush", "Height Push") : LOCTEXT("StructuralRowWarp", "Warp");
	const FMixtormatStructuralConnectionContext Context(WorkingLayers, WorkingLayerGroups, Row.Address);
	const auto& SourceEdge = Row.Status.Source;
	FText SourceLabel;
	FText FullSourceLabel;
	if (Row.bHasResolvedPayload && SourceEdge.Issue != EIssue::DuplicateIdentity && Context.Effective.IsValidIndex(SourceEdge.LayerIndex)
		&& Context.Effective[SourceEdge.LayerIndex].Children.IsValidIndex(SourceEdge.ChildIndex))
	{
		const FMixtormatLayer& SourceLayer = Context.Effective[SourceEdge.LayerIndex];
		const FText Breadcrumb = GetStructuralSourceBreadcrumb(SourceLayer, SourceEdge.ChildIndex);
		FullSourceLabel = FText::Format(LOCTEXT("StructuralOriginLabel", "{0} / {1}"), SourceLayer.DisplayName, Breadcrumb);
		SourceLabel = SourceLayer.LayerId == Row.Address.OwnerId ? Breadcrumb : FullSourceLabel;
	}
	else
	{
		SourceLabel = !Row.bHasResolvedPayload ? LOCTEXT("StructuralRowSourceUnavailable", "Source evaluation unavailable")
			: SourceEdge.Issue == EIssue::Unset ? LOCTEXT("StructuralRowChooseSource", "Choose source…")
			: SourceEdge.Issue == EIssue::MissingLayer ? LOCTEXT("StructuralRowSourceLayerMissing", "Source layer missing")
			: SourceEdge.Issue == EIssue::DuplicateIdentity ? LOCTEXT("StructuralRowSourceAmbiguous", "Source identity ambiguous")
			: LOCTEXT("StructuralRowSourceMissing", "Source missing");
		FullSourceLabel = SourceLabel;
	}
	FText TargetLabel = !Row.bHasResolvedPayload ? LOCTEXT("StructuralRowTargetUnavailable", "Target evaluation unavailable")
		: !Row.ResolvedTargetId.IsValid() ? LOCTEXT("StructuralRowChooseTarget", "Choose target…")
		: LOCTEXT("StructuralRowTargetMissing", "Target missing");
	if (Row.bHasResolvedPayload && Context.Effective.IsValidIndex(Context.LayerIndex))
	{
		const FMixtormatLayer& Layer = Context.Effective[Context.LayerIndex];
		int32 TargetIndex = INDEX_NONE;
		int32 Matches = 0;
		for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
		{
			if (Layer.Children[Index].ChildId != Row.ResolvedTargetId) { continue; }
			TargetIndex = Index;
			++Matches;
		}
		if (Matches == 1) { TargetLabel = GetStructuralChildLabel(Layer, TargetIndex); }
		else if (Matches > 1) { TargetLabel = LOCTEXT("StructuralRowTargetAmbiguous", "Target identity ambiguous"); }
	}
	const EIssue Issue = Row.Status.ModuleIssue != EIssue::None ? Row.Status.ModuleIssue
		: Row.Status.Target.Issue != EIssue::None ? Row.Status.Target.Issue : SourceEdge.Issue;
	const FText Output = FText::FromName(Row.ResolvedSource.OutputName);
	const FText OutputKind = StaticEnum<EMixtormatPublishedFieldKind>()->GetDisplayNameTextByValue(
		static_cast<int64>(Row.ResolvedSource.Kind));
	const FText Detail = Row.PresentationReason.IsEmpty() ? ConnectionIssueText(Issue) : Row.PresentationReason;
	FText ToolTip = FText::Format(LOCTEXT("StructuralRowTooltip",
		"{0} → {1} → {2}\nOutput: {3} ({4})\n{5}\nAuthored execution position: {6}. Visual nesting does not change order or ownership."),
		FullSourceLabel, Operation, TargetLabel, Output, OutputKind, Detail, FText::AsNumber(Row.AuthoredChildIndex + 1));
	if (!Row.bModuleEnabled)
	{
		ToolTip = FText::Format(LOCTEXT("StructuralRowDisabledTooltip", "{0}\nOperation disabled; configured endpoints are retained."), ToolTip);
	}
	if (Context.Effective.IsValidIndex(Context.LayerIndex)
		&& Context.Effective[Context.LayerIndex].Children.IsValidIndex(Context.ChildIndex)
		&& Context.Effective[Context.LayerIndex].Children[Context.ChildIndex].IsInstance())
	{
		ToolTip = FText::Format(LOCTEXT("StructuralRowInstanceTooltip", "{0}\nSettings instance: break the instance to edit its endpoints."), ToolTip);
	}
	FMixtormatStructuralRelationshipPresentation Presentation;
	Presentation.Operation = Operation;
	Presentation.Source = SourceLabel;
	Presentation.Target = TargetLabel;
	Presentation.Output = Output;
	Presentation.ToolTip = ToolTip;
	Presentation.Summary = FText::Format(LOCTEXT("StructuralRelationshipSummary", "{0} · {1} → {2} → {3}"),
		FullSourceLabel, Output, Operation, TargetLabel);
	Presentation.bHasIssue = Issue != EIssue::None || !Row.PresentationReason.IsEmpty();
	return Presentation;
}

TSharedRef<SWidget> SMixtormat::BuildStructuralConnectionContent(const FMixtormatProjectedChildRow& Row,
	const EMixtormatLayerChildType Type, FText& OutToolTip) const
{
	const auto Presentation = DescribeStructuralConnection(Row, Type);
	OutToolTip = Presentation.ToolTip;
	const bool bIncoming = Row.Kind == EMixtormatProjectedChildKind::IncomingConnection;
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const auto NameStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerName),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const auto SourceStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerSource),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	// The row's first icon conveys the operation type; the inline glyph alone
	// conveys direction. Never encode a second arrow into the operation label.
	const FText Prefix = Presentation.Operation;
	const float ArrowSize = FMath::Min(Resolved.ControlLayout.LayerChildIconSize,
		Resolved.LayerLayout.ChildRowHeight * 0.65f);
	const FText Suffix = bIncoming && !Row.ResolvedSource.OutputName.IsNone() ? Presentation.Output : FText::GetEmpty();
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, Resolved.LayerConnections.TextGap, 0.0f)
		[
			SNew(STextBlock).Font(NameStyle.Font).ColorAndOpacity(NameStyle.ColorAndOpacity).Text(Prefix)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, Resolved.LayerConnections.TextGap, 0.0f)
		[
			SNew(SBox).WidthOverride(ArrowSize).HeightOverride(ArrowSize)
			[
				SNew(SImage)
				.Image(bIncoming ? MixtormatIcons::ChevronLeft() : MixtormatIcons::ChevronRight())
				.ColorAndOpacity(SourceStyle.ColorAndOpacity)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(NameStyle.Font).ColorAndOpacity(NameStyle.ColorAndOpacity)
				.Text(bIncoming ? Presentation.Source : Presentation.Target).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Resolved.LayerConnections.TextGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Font(SourceStyle.Font).ColorAndOpacity(SourceStyle.ColorAndOpacity).Text(Suffix)
				.Visibility(Suffix.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Resolved.LayerConnections.TextGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Font(SourceStyle.Font).ColorAndOpacity(SourceStyle.ColorAndOpacity)
				.Text(LOCTEXT("StructuralRowIssue", "!"))
				.Visibility(Presentation.bHasIssue ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		];
}

bool SMixtormat::GetStructuralRelationshipRow(const FMixtormatChildAddress Address,
	FMixtormatProjectedChildRow& OutRow, EMixtormatLayerChildType& OutType, FText& OutReason) const
{
	OutReason = LOCTEXT("StructuralRelationshipUnavailable", "Structural operation is unavailable or ambiguous");
	if (Address.OwnerType != EMixtormatChildOwnerType::Layer)
	{
		OutReason = LOCTEXT("StructuralRelationshipGroup", "Shared-group structural endpoint provenance is unsupported");
		return false;
	}
	int32 LayerIndex, ChildIndex;
	if (!ResolveHierarchyChildAddress(Address, LayerIndex, ChildIndex)) { return false; }
	const FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
	if (!IsStructuralModule(Child)) { return false; }
	const auto Rows = MixtormatStructuralConnections::BuildChildProjection(WorkingLayers, WorkingLayerGroups, LayerIndex);
	const FMixtormatProjectedChildRow* Row = Rows.FindByPredicate([ChildIndex](const FMixtormatProjectedChildRow& Candidate)
		{ return Candidate.AuthoredChildIndex == ChildIndex; });
	if (!Row) { return false; }
	OutRow = *Row;
	OutType = Child.Type;
	OutReason = FText::GetEmpty();
	return true;
}

FText SMixtormat::GetStructuralRelationshipText(const FMixtormatChildAddress Address, const bool bToolTip) const
{
	const FString Key = FString::Printf(TEXT("Relationship_%d_%s_%s_%d"), static_cast<int32>(Address.OwnerType),
		*Address.OwnerId.ToString(), *Address.ChildId.ToString(), bToolTip ? 1 : 0);
	if (const FText* Cached = StructuralConnectionLabelCache.Find(Key)) { return *Cached; }
	FMixtormatProjectedChildRow Row;
	EMixtormatLayerChildType Type;
	FText Reason;
	FText Text;
	if (GetStructuralRelationshipRow(Address, Row, Type, Reason))
	{
		const auto Presentation = DescribeStructuralConnection(Row, Type);
		Text = bToolTip ? Presentation.ToolTip : Presentation.Summary;
	}
	else { Text = Reason; }
	StructuralConnectionLabelCache.Add(Key, Text);
	return Text;
}

bool SMixtormat::ResolveStructuralSourceAddress(const FMixtormatChildAddress ModuleAddress,
	FMixtormatChildAddress& OutSource, FText& OutReason) const
{
	OutSource = {};
	FMixtormatProjectedChildRow Row;
	EMixtormatLayerChildType Type;
	if (!GetStructuralRelationshipRow(ModuleAddress, Row, Type, OutReason)) { return false; }
	if (!Row.bHasResolvedPayload)
	{
		OutReason = Row.PresentationReason.IsEmpty() ? ConnectionIssueText(Row.Status.ModuleIssue) : Row.PresentationReason;
		if (OutReason.IsEmpty()) { OutReason = LOCTEXT("StructuralSourceEvaluationUnavailable", "Source evaluation is unavailable"); }
		return false;
	}
	const FMixtormatOutputReference& Source = Row.ResolvedSource;
	if (!Source.SourceLayerId.IsValid() || !Source.SourceChildId.IsValid())
	{
		OutReason = LOCTEXT("StructuralRowChooseSource", "Choose source…");
		return false;
	}
	if (Row.Status.Source.Issue == EIssue::DuplicateIdentity)
	{
		OutReason = ConnectionIssueText(EIssue::DuplicateIdentity);
		return false;
	}
	int32 OwnerCount = 0;
	const FMixtormatLayer* SourceLayer = nullptr;
	const FMixtormatLayerGroup* SourceGroup = nullptr;
	for (const auto& Layer : WorkingLayers)
	{
		if (Layer.LayerId != Source.SourceLayerId) { continue; }
		SourceLayer = &Layer;
		++OwnerCount;
	}
	for (const auto& Group : WorkingLayerGroups)
	{
		if (Group.GroupId != Source.SourceLayerId) { continue; }
		SourceGroup = &Group;
		++OwnerCount;
	}
	if (OwnerCount != 1)
	{
		OutReason = ConnectionIssueText(OwnerCount == 0 ? EIssue::MissingLayer : EIssue::DuplicateIdentity);
		return false;
	}
	TArray<FMixtormatChildAddress> Candidates;
	if (SourceLayer)
	{
		for (const auto& Child : SourceLayer->Children)
		{
			if (Child.ChildId == Source.SourceChildId)
			{ Candidates.Add({EMixtormatChildOwnerType::Layer, SourceLayer->LayerId, Child.ChildId}); }
		}
		// An effective shared child navigates to its authored group row, never a fabricated local copy.
		if (SourceLayer->GroupId.IsValid())
		{
			for (const auto& Group : WorkingLayerGroups)
			{
				if (Group.GroupId != SourceLayer->GroupId) { continue; }
				for (const auto& Child : Group.Children)
				{
					if (MixtormatLayerGroups::MakeEffectiveChildId(Group.GroupId, Child.ChildId, SourceLayer->LayerId) == Source.SourceChildId)
					{ Candidates.Add({EMixtormatChildOwnerType::Group, Group.GroupId, Child.ChildId}); }
				}
			}
		}
	}
	else if (SourceGroup)
	{
		for (const auto& Child : SourceGroup->Children)
		{
			if (Child.ChildId == Source.SourceChildId)
			{ Candidates.Add({EMixtormatChildOwnerType::Group, SourceGroup->GroupId, Child.ChildId}); }
		}
	}
	int32 OwnerIndex, ChildIndex;
	if (Candidates.Num() != 1 || !ResolveHierarchyChildAddress(Candidates[0], OwnerIndex, ChildIndex))
	{
		OutReason = ConnectionIssueText(Candidates.IsEmpty() ? EIssue::MissingChild : EIssue::DuplicateIdentity);
		return false;
	}
	OutSource = Candidates[0];
	OutReason = FText::GetEmpty();
	return true;
}

FReply SMixtormat::GoToStructuralSource(const FMixtormatChildAddress ModuleAddress)
{
	FMixtormatChildAddress Source;
	FText Reason;
	if (!ResolveStructuralSourceAddress(ModuleAddress, Source, Reason))
	{
		WorkingStatusText = Reason.ToString();
		return FReply::Handled();
	}
	return NavigateToChild(Source);
}

TSharedRef<SWidget> SMixtormat::BuildStructuralRelationshipHeader()
{
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const auto TextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerName),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Resolved.LayerConnections.TextGap)
		[
			SNew(STextBlock).Font(TextStyle.Font).ColorAndOpacity(TextStyle.ColorAndOpacity).AutoWrapText(true)
				.Text_Lambda([this]() { return GetStructuralRelationshipText(GetSelectedChildAddress(), false); })
				.ToolTipText_Lambda([this]() { return GetStructuralRelationshipText(GetSelectedChildAddress(), true); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SMixtormatGroupAction)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.Text(LOCTEXT("StructuralGoToSource", "Go to source"))
				.IsEnabled_Lambda([this]()
				{
					FMixtormatChildAddress Source;
					FText Reason;
					return ResolveStructuralSourceAddress(GetSelectedChildAddress(), Source, Reason);
				})
				.ToolTipText_Lambda([this]()
				{
					FMixtormatChildAddress Source;
					FText Reason;
					return ResolveStructuralSourceAddress(GetSelectedChildAddress(), Source, Reason)
						? LOCTEXT("StructuralGoToSourceHint", "Select and reveal the authored source; connection endpoints remain unchanged") : Reason;
				})
				.OnClicked_Lambda([this]() { return GoToStructuralSource(GetSelectedChildAddress()); })
		];
}

TSharedRef<SWidget> SMixtormat::BuildStructuralSourcePickerForTarget(const FGuid TargetLayerId,
	const FGuid TargetChildId, const EMixtormatLayerChildType ModuleType)
{
	MixtormatMenu::FBuilder Menu;
	TArray<FMixtormatLayer> ProposedLayers;
	int32 LayerIndex = INDEX_NONE;
	int32 InsertIndex = INDEX_NONE;
	FText Reason;
	if (!PrepareStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType,
		ProposedLayers, LayerIndex, InsertIndex, Reason))
	{
		Menu.Caption(Reason);
		return Menu.Build();
	}
	const bool bPush = ModuleType == EMixtormatLayerChildType::HeightPush;
	const FMixtormatLayerChild& Module = ProposedLayers[LayerIndex].Children[InsertIndex];
	const FMixtormatStructuralConnectionContext Context(ProposedLayers, WorkingLayerGroups,
		{EMixtormatChildOwnerType::Layer, TargetLayerId, Module.ChildId});
	const FMixtormatOutputReference& ModuleSource = bPush ? Module.HeightPush.Source : Module.StructuralWarp.Source;
	TArray<FMixtormatStructuralSourcePickerEntry> Entries;
	for (const FMixtormatStructuralSourceCandidate& Candidate : Context.CollectSources(ProposedLayers, ModuleSource))
	{
		const FMixtormatLayer& Layer = ProposedLayers[Candidate.LayerIndex];
		const FText Breadcrumb = GetStructuralSourceBreadcrumb(Layer, Candidate.ChildIndex);
		const FText OutputKind = StaticEnum<EMixtormatPublishedFieldKind>()->GetDisplayNameTextByValue(
			static_cast<int64>(Candidate.Source.Kind));
		const FText Output = FText::Format(LOCTEXT("StructuralPickerTypedOutput", "{0} ({1})"),
			FText::FromName(Candidate.Source.OutputName), OutputKind);
		const FText Label = FText::Format(LOCTEXT("StructuralPickerOutput", "{0} · {1}"), Breadcrumb, Output);
		// Preparation validated the destination; candidate status still owns source eligibility.
		const EIssue Issue = Candidate.Status.ModuleIssue != EIssue::None ? Candidate.Status.ModuleIssue
			: Candidate.Status.Target.Issue != EIssue::None ? Candidate.Status.Target.Issue : Candidate.Issue;
		FMixtormatStructuralSourcePickerEntry Entry;
		Entry.SourceAddress = Candidate.SourceAddress;
		Entry.Source.SourceLayerId = Candidate.SourceAddress.OwnerId;
		Entry.Source.SourceChildId = Candidate.SourceAddress.ChildId;
		Entry.Source.OutputName = Candidate.Source.OutputName;
		Entry.Source.Kind = Candidate.Source.Kind;
		Entry.Source.bEnabled = true;
		Entry.LayerIndex = Candidate.LayerIndex;
		Entry.LayerLabel = Layer.LayerId == TargetLayerId
			? LOCTEXT("StructuralMenuThisLayer", "This layer") : Layer.DisplayName;
		Entry.bAvailable = Issue == EIssue::None && Candidate.Status.bCanExecuteStructurally;
		Entry.Label = Entry.bAvailable ? Label : FText::Format(LOCTEXT("StructuralPickerShortReason", "{0} — {1}"),
			Label, Issue == EIssue::None ? LOCTEXT("StructuralPickerInactive", "Connection inactive") : ConnectionMenuReason(Issue));
		const FText FullLabel = FText::Format(LOCTEXT("StructuralPickerFullLabel", "{0} / {1}"), Layer.DisplayName, Label);
		Entry.ToolTip = Entry.bAvailable ? FullLabel : FText::Format(LOCTEXT("StructuralPickerFullReason", "{0}\n{1}"),
			FullLabel, Issue == EIssue::None
				? LOCTEXT("StructuralPickerInactiveReason", "Connection is unavailable in the effective generator stack")
				: MixtormatStructuralConnections::IssueText(Issue));
		Entry.SearchText = FullLabel.ToString();
		Entries.Add(MoveTemp(Entry));
	}
	const int32 TargetIndex = ProposedLayers[LayerIndex].Children.IndexOfByPredicate(
		[TargetChildId](const FMixtormatLayerChild& Child) { return Child.ChildId == TargetChildId; });
	const FText Caption = FText::Format(bPush
		? LOCTEXT("StructuralPickerPushCaption", "Height Push {0} from…")
		: LOCTEXT("StructuralPickerWarpCaption", "Warp {0} using…"),
		GetStructuralChildLabel(ProposedLayers[LayerIndex], TargetIndex));
	const TWeakPtr<SMixtormat> WeakThis = SharedThis(this);
	const TSharedRef<FMixtormatStructuralEndpointPreview> Preview = MakeShared<FMixtormatStructuralEndpointPreview>();
	Preview->Target = {EMixtormatChildOwnerType::Layer, TargetLayerId, TargetChildId};
	StructuralEndpointPreview = Preview;
	const TWeakPtr<FMixtormatStructuralEndpointPreview> WeakPreview = Preview;
	const FMixtormatChildAddress TargetAddress = Preview->Target;
	const TWeakPtr<SWidget> TargetRow = ChildRowWidgets.FindRef(TargetAddress);
	Invalidate(EInvalidateWidgetReason::Paint);
	Menu.Widget(SNew(SMixtormatStructuralSourcePicker)
		.EndpointPreview(Preview)
		.bPreviewCurrent_Lambda([WeakThis, WeakPreview, TargetAddress, TargetRow]()
		{
			const auto Editor = WeakThis.Pin();
			// A new row for the same GUID is a rebuild, not the picker session's original row.
			return Editor && WeakPreview.IsValid() && TargetRow.IsValid()
				&& Editor->StructuralEndpointPreview.Pin() == WeakPreview.Pin()
				&& Editor->ChildRowWidgets.FindRef(TargetAddress).Pin() == TargetRow.Pin();
		})
		.OnPreviewChanged(FSimpleDelegate::CreateLambda([WeakThis]()
		{
			if (const auto Editor = WeakThis.Pin()) { Editor->Invalidate(EInvalidateWidgetReason::Paint); }
		}))
		.Caption(Caption)
		.Entries(MoveTemp(Entries))
		.OnSourcePicked(FOnMixtormatStructuralSourcePicked::CreateLambda(
			[WeakThis, TargetLayerId, TargetChildId, ModuleType](const FMixtormatOutputReference& Source)
			{
				if (const auto Editor = WeakThis.Pin())
				{
					Editor->CreateConnectedStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType, Source);
				}
			}))
		.OnSourceLater(FSimpleDelegate::CreateLambda([WeakThis, TargetLayerId, TargetChildId, ModuleType]()
		{
			if (const auto Editor = WeakThis.Pin())
			{
				Editor->CreateStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType);
			}
		})));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildStructuralConnectionMenu(const FMixtormatChildAddress Address, const ERole Role)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (Address.OwnerType != EMixtormatChildOwnerType::Layer || !Module || !IsStructuralModule(*Module)) { return Menu.Build(); }
	const FMixtormatStructuralConnectionContext Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto CurrentStatus = Projection.Evaluate();
	const bool bEditable = !Module->IsInstance() && !Module->ScopeOwnerChildId.IsValid()
		&& (CurrentStatus.ModuleIssue == EIssue::None || CurrentStatus.ModuleIssue == EIssue::DisabledLayer);
	Menu.Item(LOCTEXT("StructuralConnectionNone", "None"), nullptr,
		FSimpleDelegate::CreateLambda([this, Address, Role]() { SetStructuralConnection(Address, Role); })).Enabled(bEditable);
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference CurrentSource = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	struct FEntry
	{
		FText Label;
		FText LayerLabel;
		FGuid LayerId;
		FMixtormatOutputReference Source;
		FGuid Target;
		EIssue Issue = EIssue::None;
	};
	TArray<FEntry> Entries;
	if (Role == ERole::Source)
	{
		for (const FMixtormatStructuralSourceCandidate& Candidate : Projection.CollectSources(WorkingLayers, CurrentSource))
		{
			const FMixtormatLayer& Layer = WorkingLayers[Candidate.LayerIndex];
			const FText Name = GetStructuralChildLabel(Layer, Candidate.ChildIndex);
			FEntry Entry;
			Entry.Source = Candidate.Source;
			Entry.Label = Name;
			if (!bPush)
			{
				const FText OutputLabel = Candidate.Source.Kind == EMixtormatPublishedFieldKind::Flow
					? LOCTEXT("StructuralMenuFlow", "Flow") : LOCTEXT("StructuralMenuUV", "UV");
				Entry.Label = FText::Format(LOCTEXT("StructuralMenuOutput", "{0} · {1}"), Name, OutputLabel);
			}
			Entry.LayerLabel = Layer.DisplayName;
			Entry.LayerId = Layer.LayerId;
			Entry.Issue = Candidate.Issue;
			Entries.Add(MoveTemp(Entry));
		}
	}
	else
	{
		for (const FMixtormatLayer& Layer : WorkingLayers)
		{
			if (Layer.LayerId != Address.OwnerId) { continue; }
			for (int32 Index = 0; Index < Layer.Children.Num(); ++Index)
			{
				const FMixtormatLayerChild& Candidate = Layer.Children[Index];
				if (Candidate.Type != EMixtormatLayerChildType::Generator) { continue; }
				FEntry Entry;
				Entry.Source = CurrentSource;
				Entry.Target = Candidate.ChildId;
				Entry.Label = GetStructuralChildLabel(Layer, Index);
				Entry.LayerLabel = Layer.DisplayName;
				Entry.LayerId = Layer.LayerId;
				Entry.Issue = ConnectionIssue(Projection.Evaluate(nullptr, &Entry.Target), Role);
				Entries.Add(MoveTemp(Entry));
			}
		}
	}
	// A connection picker is a compact hierarchy menu, not a diagnostic report.
	// Ineligible choices remain in the model for Inspector issue reporting.
	FGuid LastLayerId;
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Issue != EIssue::None || !bEditable) { continue; }
		if (Role == ERole::Source && Entry.LayerId != LastLayerId)
		{
			Menu.Caption(Entry.LayerId == Address.OwnerId
				? LOCTEXT("StructuralMenuThisLayer", "This layer") : Entry.LayerLabel);
			LastLayerId = Entry.LayerId;
		}
		Menu.Item(Entry.Label,
			Role == ERole::Source && !bPush ? MixtormatIcons::WarpStructural() : MixtormatIcons::Generator(),
			FSimpleDelegate::CreateLambda([this, Address, Role, Entry]()
			{
				SetStructuralConnection(Address, Role, Role == ERole::Source ? &Entry.Source : nullptr,
					Role == ERole::Target ? &Entry.Target : nullptr);
			}));
	}
	return Menu.Build();
}

EStructuralLinkHighlightRole SMixtormat::GetStructuralHighlightRole(const FMixtormatChildAddress Address) const
{
	if (const auto Preview = StructuralEndpointPreview.Pin())
	{
		const bool bSource = Preview->Source.IsValid() && Address == Preview->Source && ResolveChildAt(Address);
		const bool bTarget = Preview->Target.IsValid() && Address == Preview->Target && ResolveChildAt(Address);
		if (bSource && bTarget) { return EStructuralLinkHighlightRole::Both; }
		if (bSource) { return EStructuralLinkHighlightRole::Source; }
		return bTarget ? EStructuralLinkHighlightRole::Target : EStructuralLinkHighlightRole::None;
	}
	const FMixtormatChildAddress SelectedAddress = GetSelectedChildAddress();
	const FMixtormatLayerChild* Module = ResolveChildAt(SelectedAddress);
	if (!Module || !IsStructuralModule(*Module)) { return EStructuralLinkHighlightRole::None; }
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	const FMixtormatOutputReference& Source = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	const FGuid TargetId = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	// Include the operation itself in the target-side highlight, so the
	// target's vertical link stripe continues through its projected relation row.
	if (Address == SelectedAddress && TargetId.IsValid())
	{
		return EStructuralLinkHighlightRole::Target;
	}
	const bool bSource = Source.SourceLayerId.IsValid() && Source.SourceChildId.IsValid()
		&& Address.OwnerId == Source.SourceLayerId && Address.ChildId == Source.SourceChildId;
	const bool bTarget = TargetId.IsValid() && Address.OwnerType == SelectedAddress.OwnerType
		&& Address.OwnerId == SelectedAddress.OwnerId && Address.ChildId == TargetId;
	if (bSource && bTarget) { return EStructuralLinkHighlightRole::Both; }
	if (bSource) { return EStructuralLinkHighlightRole::Source; }
	return bTarget ? EStructuralLinkHighlightRole::Target : EStructuralLinkHighlightRole::None;
}

bool SMixtormat::IsSelectedStructuralSourceLayer(const FGuid LayerId, const FGuid GroupId) const
{
	if (const auto Preview = StructuralEndpointPreview.Pin())
	{
		return Preview->Source.IsValid() && ResolveChildAt(Preview->Source)
			&& (Preview->Source.OwnerId == LayerId || (GroupId.IsValid() && Preview->Source.OwnerId == GroupId));
	}
	const FMixtormatLayerChild* Module = ResolveChildAt(GetSelectedChildAddress());
	if (!Module || !IsStructuralModule(*Module)) { return false; }
	const FMixtormatOutputReference& Source = Module->Type == EMixtormatLayerChildType::HeightPush
		? Module->HeightPush.Source : Module->StructuralWarp.Source;
	FMixtormatChildAddress SourceAddress;
	SourceAddress.OwnerId = Source.SourceLayerId;
	SourceAddress.ChildId = Source.SourceChildId;
	SourceAddress.OwnerType = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Source.SourceLayerId)
		? EMixtormatChildOwnerType::Group : EMixtormatChildOwnerType::Layer;
	return SourceAddress.IsValid() && ResolveChildAt(SourceAddress)
		&& (Source.SourceLayerId == LayerId || (GroupId.IsValid() && Source.SourceLayerId == GroupId));
}

FText SMixtormat::GetStructuralIncomingCountLabel(const int32 LayerIndex, const int32 ChildIndex) const
{
	if (!WorkingLayers.IsValidIndex(LayerIndex) || !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Generator)
	{
		return FText::GetEmpty();
	}
	const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
	if (const TArray<FText>* Cached = StructuralIncomingCountLabels.Find(Layer.LayerId))
	{
		return Cached->IsValidIndex(ChildIndex) ? (*Cached)[ChildIndex] : FText::GetEmpty();
	}
	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(WorkingLayers, WorkingLayerGroups, Effective);
	FMixtormatLayer Resolved = Effective[LayerIndex];
	MixtormatParameterBinding::ApplyDirectReferences(FMixtormatBindingScope{Effective, WorkingLayerGroups}, Resolved);
	TArray<int32> PushCounts;
	TArray<int32> WarpCounts;
	PushCounts.Init(0, Layer.Children.Num());
	WarpCounts.Init(0, Layer.Children.Num());
	for (int32 Index = 0; Index < Resolved.Children.Num(); ++Index)
	{
		const FMixtormatLayerChild& Module = Resolved.Children[Index];
		if (!IsStructuralModule(Module)) { continue; }
		const auto Status = MixtormatOutputReferences::EvaluateStructuralLinkForGather(Effective, LayerIndex, Index, Resolved);
		if (!Status.bCanExecuteStructurally || Status.Target.LayerIndex != LayerIndex
			|| !Effective.IsValidIndex(Status.Target.LayerIndex)
			|| !Effective[Status.Target.LayerIndex].Children.IsValidIndex(Status.Target.ChildIndex)) { continue; }

		const FMixtormatLayerChild& ResolvedTarget =
			Effective[Status.Target.LayerIndex].Children[Status.Target.ChildIndex];
		if (ResolvedTarget.Type != EMixtormatLayerChildType::Generator) { continue; }
		const int32 AuthoredTargetIndex = Layer.Children.IndexOfByPredicate(
			[&ResolvedTarget](const FMixtormatLayerChild& Child)
			{
				return Child.ChildId == ResolvedTarget.ChildId
					&& Child.Type == EMixtormatLayerChildType::Generator;
			});
		if (!PushCounts.IsValidIndex(AuthoredTargetIndex)) { continue; }
		if (Module.Type == EMixtormatLayerChildType::HeightPush) { ++PushCounts[AuthoredTargetIndex]; }
		else { ++WarpCounts[AuthoredTargetIndex]; }
	}
	TArray<FText> Labels;
	Labels.SetNum(Layer.Children.Num());
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		const int32 PushCount = PushCounts[Index];
		const int32 WarpCount = WarpCounts[Index];
		if (PushCount == 0 && WarpCount == 0) { continue; }
		if (PushCount == 0) { Labels[Index] = FText::Format(LOCTEXT("StructuralWarpCount", "{0} WARP"), FText::AsNumber(WarpCount)); }
		else if (WarpCount == 0) { Labels[Index] = FText::Format(LOCTEXT("StructuralPushCount", "{0} PUSH"), FText::AsNumber(PushCount)); }
		else
		{
			Labels[Index] = FText::Format(LOCTEXT("StructuralBothCount", "{0} PUSH · {1} WARP"),
				FText::AsNumber(PushCount), FText::AsNumber(WarpCount));
		}
	}
	const FText Label = Labels[ChildIndex];
	StructuralIncomingCountLabels.Add(Layer.LayerId, MoveTemp(Labels));
	return Label;
}

TSharedRef<SWidget> SMixtormat::BuildStructuralLinkChips(const FMixtormatChildAddress Address)
{
	const FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!Module || (Module->Type != EMixtormatLayerChildType::HeightPush
		&& Module->Type != EMixtormatLayerChildType::StructuralWarp))
	{
		return SNullWidget::NullWidget;
	}
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle ArrowStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerSource),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const auto MakeConnectionChip = [this, Address](const ERole Role)
	{
		// The list is rebuilt on authored edits, so resolve once here rather than copying the
		// effective projection and validating/loading a source asset on every Slate attribute tick.
		FText FullLabel;
		const FText Label = GetStructuralConnectionLabel(Address, Role, true, &FullLabel);
		return SNew(SBox)
			.MinDesiredWidth(MixtormatTokens::StructuralLinkChipMinWidth)
			.MaxDesiredWidth(MixtormatTokens::StructuralLinkChipMaxWidth)
			[
				SNew(SMixtormatBadge)
				.bAutoWidth(true)
				.Text(Label)
				.ToolTip(FullLabel)
				.OnGetMenuContent_Lambda([this, Address, Role]()
				{
					return BuildStructuralConnectionMenu(Address, Role);
				})
			];
	};
	const ERole SourceRole = ERole::Source;
	const ERole TargetRole = ERole::Target;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeConnectionChip(SourceRole)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(MixtormatTokens::StructuralLinkArrowGap, 0.0f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(ArrowStyle.Font)
				.ColorAndOpacity(ArrowStyle.ColorAndOpacity)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			MakeConnectionChip(TargetRole)
		];
}

FReply SMixtormat::SetStructuralConnection(const FMixtormatChildAddress Address, const ERole Role,
	const FMixtormatOutputReference* Source, const FGuid* TargetId)
{
	FMixtormatLayerChild* Module = ResolveChildAt(Address);
	if (!bHasWorkingMaterial || Address.OwnerType != EMixtormatChildOwnerType::Layer
		|| !Module || !IsStructuralModule(*Module) || Module->IsInstance() || Module->ScopeOwnerChildId.IsValid())
	{
		return FReply::Unhandled();
	}
	const bool bPush = Module->Type == EMixtormatLayerChildType::HeightPush;
	FMixtormatOutputReference& CurrentSource = bPush ? Module->HeightPush.Source : Module->StructuralWarp.Source;
	FGuid& CurrentTarget = bPush ? Module->HeightPush.TargetChildId : Module->StructuralWarp.TargetChildId;
	FMixtormatOutputReference ProposedSource = CurrentSource;
	const FGuid ProposedTarget = TargetId ? *TargetId : FGuid();
	if (Role == ERole::Source)
	{
		if (Source)
		{
			// A connection changes its typed address, not the module's authored trace settings.
			ProposedSource.bEnabled = true;
			ProposedSource.SourceLayerId = Source->SourceLayerId;
			ProposedSource.SourceChildId = Source->SourceChildId;
			ProposedSource.Kind = Source->Kind;
			ProposedSource.OutputName = Source->OutputName;
		}
		else
		{
			ProposedSource.SourceLayerId.Invalidate();
			ProposedSource.SourceChildId.Invalidate();
		}
	}
	const FMixtormatStructuralConnectionContext Projection(WorkingLayers, WorkingLayerGroups, Address);
	const auto Status = Projection.Evaluate(Role == ERole::Source ? &ProposedSource : nullptr,
		Role == ERole::Target ? &ProposedTarget : nullptr);
	const EIssue Issue = ConnectionIssue(Status, Role);
	const bool bDisconnect = Role == ERole::Source ? Source == nullptr : TargetId == nullptr;
	if ((Issue != EIssue::None && !(bDisconnect && Issue == EIssue::Unset))
		|| (Status.ModuleIssue != EIssue::None && Status.ModuleIssue != EIssue::DisabledLayer))
	{
		WorkingStatusText = ConnectionIssueText(Issue).ToString();
		return FReply::Handled();
	}
	const bool bUnchanged = Role == ERole::Target ? CurrentTarget == ProposedTarget
		: CurrentSource.SourceLayerId == ProposedSource.SourceLayerId && CurrentSource.SourceChildId == ProposedSource.SourceChildId
			&& CurrentSource.Kind == ProposedSource.Kind && CurrentSource.OutputName == ProposedSource.OutputName
			&& CurrentSource.bEnabled == ProposedSource.bEnabled;
	if (bUnchanged) { return FReply::Handled(); }
	if (Role == ERole::Target) { CurrentTarget = ProposedTarget; }
	else { CurrentSource = ProposedSource; }
	// Synchronize inherited payloads before capturing the single committed history state.
	RefreshLayeredPreview(false);
	// Discrete connection edits must not coalesce with the preceding or following slider edit.
	LastHistoryRecordTime = 0.0;
	RecordEditHistory();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildLayerList();
	SyncSelectedLayerControls();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
