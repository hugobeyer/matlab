// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Layers/SMixtormatLayerChildRow.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Layers/MixtormatLayerShortcuts.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace
{
	EMixtormatMaskBlendMode* GetChildMaskBlendMode(FMixtormatLayerChild* Child)
	{
		if (!Child) { return nullptr; }
		switch (Child->Type)
		{
		case EMixtormatLayerChildType::Mask: return &Child->Mask.BlendMode;
		case EMixtormatLayerChildType::Generated: return &Child->Generated.BlendMode;
		case EMixtormatLayerChildType::Craquelure: return &Child->Craquelure.BlendMode;
		case EMixtormatLayerChildType::ColorId: return &Child->ColorId.BlendMode;
		case EMixtormatLayerChildType::RandomId: return &Child->RandomId.BlendMode;
		default: return nullptr;
		}
	}

	// One icon per published output, chosen by what the output is rather than by its name: an ID map
	// gets the ID glyph, a scalar mask the mask glyph, and every typed field (colour, flow, UV) falls
	// back to the neutral generated glyph until it has artwork of its own. Kept in one place so the
	// Outputs submenu never grows a second, drifting copy of this mapping.
	const FSlateBrush* GetPublishedOutputIcon(const FMixtormatPublishedOutputDesc& Output)
	{
		if (Output.bCopyableAsMask)
		{
			return MixtormatIcons::Mask();
		}
		if (Output.FieldKind == EMixtormatPublishedFieldKind::RegionIds)
		{
			return MixtormatIcons::Ids();
		}
		return MixtormatIcons::Generated();
	}
}

	TSharedRef<SWidget> SMixtormat::BuildBooleanStackRowMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	if (!WorkingLayers.IsValidIndex(LayerIndex)
		|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		|| WorkingLayers[LayerIndex].Children[ChildIndex].Type != EMixtormatLayerChildType::Boolean)
	{
		return Menu.Build();
	}
	const auto SetOperation = [this, LayerIndex, ChildIndex](EMixtormatBooleanFieldOperation Op)
	{
		return FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Op]()
		{
			if (!WorkingLayers.IsValidIndex(LayerIndex)
				|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)) { return; }
			FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
			if (Child.Type != EMixtormatLayerChildType::Boolean || Child.Boolean.Operation == Op) { return; }
			Child.Boolean.Operation = Op;
			LastHistoryRecordTime = 0.0;
			RefreshLayeredPreview();
			RebuildLayerList();
		});
	};
	const auto SetEdge = [this, LayerIndex, ChildIndex](EMixtormatBooleanEdgeStyle Style)
	{
		return FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Style]()
		{
			if (!WorkingLayers.IsValidIndex(LayerIndex)
				|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)) { return; }
			FMixtormatLayerChild& Child = WorkingLayers[LayerIndex].Children[ChildIndex];
			if (Child.Type != EMixtormatLayerChildType::Boolean || Child.Boolean.EdgeStyle == Style) { return; }
			Child.Boolean.EdgeStyle = Style;
			LastHistoryRecordTime = 0.0;
			RefreshLayeredPreview();
			RebuildLayerList();
		});
	};
	Menu.Item(LOCTEXT("BooleanStackUnion", "Union"), MixtormatIcons::BooleanOps(),
		SetOperation(EMixtormatBooleanFieldOperation::Union));
	Menu.Item(LOCTEXT("BooleanStackSubtract", "Subtract"), MixtormatIcons::BooleanOps(),
		SetOperation(EMixtormatBooleanFieldOperation::Subtract));
	Menu.Item(LOCTEXT("BooleanStackIntersect", "Intersect"), MixtormatIcons::BooleanOps(),
		SetOperation(EMixtormatBooleanFieldOperation::Intersect));
	Menu.Separator();
	Menu.Item(LOCTEXT("BooleanStackHard", "Hard"), MixtormatIcons::BooleanOps(),
		SetEdge(EMixtormatBooleanEdgeStyle::Hard));
	Menu.Item(LOCTEXT("BooleanStackRound", "Round"), MixtormatIcons::BooleanOps(),
		SetEdge(EMixtormatBooleanEdgeStyle::Round));
	Menu.Item(LOCTEXT("BooleanStackBevel", "Bevel"), MixtormatIcons::BooleanOps(),
		SetEdge(EMixtormatBooleanEdgeStyle::Bevel));

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildQuickControlsActionsMenu()
{
	MixtormatMenu::FBuilder Menu;
	const TSharedPtr<const FMixtormatQuickControlsTarget> Context = QuickControlsActionTarget;
	Menu.Caption(FText::Format(LOCTEXT("QuickActionsTarget", "Target · {0}"), GetQuickControlsTargetLabel()));
	const auto Reason = [this, Context](const EQuickControlsAction Action)
	{
		return TAttribute<FText>::CreateLambda([this, Context, Action]()
		{
			return GetQuickControlsActionUnavailableReason(Context, Action);
		});
	};
	const auto Execute = [this, Context](const EQuickControlsAction Action, const FSimpleDelegate& Callback)
	{
		return FSimpleDelegate::CreateLambda([this, Context, Action, Callback]()
		{
			const FText Disabled = GetQuickControlsActionUnavailableReason(Context, Action);
			if (!Disabled.IsEmpty()) { ShowPreviewFeedback(Disabled); return; }
			const bool bCreatesChild = Action == EQuickControlsAction::NoiseGate || Action == EQuickControlsAction::GalleryGate
				|| Action == EQuickControlsAction::PasteGate || Action == EQuickControlsAction::Blur || Action == EQuickControlsAction::Curvature;
			const TArray<FMixtormatLayerChild>* Before = bCreatesChild ? ResolveContainer(Context->Address) : nullptr;
			const int32 Count = Before ? Before->Num() : 0;
			Callback.ExecuteIfBound();
			if (bCreatesChild)
			{
				const TArray<FMixtormatLayerChild>* After = ResolveContainer(Context->Address);
				if (!After || After->Num() == Count)
				{
					ShowPreviewFeedback(LOCTEXT("QuickCreationRejected", "Nothing added · The target or source is no longer available."));
				}
			}
		});
	};
	const auto Open = [this, Context](const EQuickControlsAction Action, const FOnGetContent& Callback)
	{
		return FOnGetContent::CreateLambda([this, Context, Action, Callback]() -> TSharedRef<SWidget>
		{
			const FText Disabled = GetQuickControlsActionUnavailableReason(Context, Action);
			if (!Disabled.IsEmpty())
			{
				MixtormatMenu::FBuilder Unavailable;
				Unavailable.Caption(Disabled);
				return Unavailable.Build();
			}
			return Callback.Execute();
		});
	};
	FMixtormatAddTarget Target;
	Target.QuickControlsTarget = Context;
	Target = ResolveQuickControlsAddTarget(Target);
	Menu.Item(LOCTEXT("QuickAddFill", "Fill Layer"), MixtormatIcons::LayerFill(),
				Execute(EQuickControlsAction::Fill, FSimpleDelegate::CreateLambda([this, Target]()
				{
					if (!bHasWorkingMaterial)
					{
						StartNewMaterialWith(EMixtormatLayerType::Fill);
						if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
						{
							ShowPreviewFeedback(FText::Format(LOCTEXT("QuickLayerAddedFeedback", "Added {0}"),
								WorkingLayers[SelectedLayerIndex].DisplayName));
						}
						return;
					}
					// AddWorkingLayer appends. Insert once here, as the positioned surface drop does,
					// so height references are not lost by an append followed by a reorder.
					const FMixtormatAddTarget Resolved = ResolveQuickControlsAddTarget(Target);
					const int32 Slot = WorkingLayers.IsValidIndex(Resolved.LayerIndex)
						? Resolved.LayerIndex + 1 : WorkingLayers.Num();
					FMixtormatLayer Layer;
					InitializeNewLayer(Layer, EMixtormatLayerType::Fill, WorkingLayers.Num() + 1);
					WorkingLayers.Insert(MoveTemp(Layer), Slot);
					MixtormatUI::RemapHeightReferencesAfterInsert(WorkingLayers, Slot);
					WorkingLayers[Slot].GroupId = ResolveGroupMembershipAt(Slot);
					ShowPreviewFeedback(FText::Format(LOCTEXT("QuickLayerAddedFeedback", "Added {0}"),
						WorkingLayers[Slot].DisplayName));
					MixtormatLayerGroups::ValidateGroups(WorkingLayers, WorkingLayerGroups);
					SoloLayerIndex = INDEX_NONE;
					SelectedLayerIndex = Slot;
					SelectedEffectIndex = INDEX_NONE;
					SelectedMaskIndex = INDEX_NONE;
					SelectedGroupId.Invalidate();
					SelectedGroupChildIndex = INDEX_NONE;
					SelectedLayerIds.Reset();
					SelectionAnchorLayerId.Invalidate();
					bHasSelectedLayer = true;
					RecordEditHistory();
					bIsWorkingMaterialDirty = !IsCurrentStateSaved();
					SyncSelectedLayerControls();
					RefreshLayeredPreview();
					RebuildLayerList();
					RebuildMaskList();
				})))
				.UnavailableReason(Reason(EQuickControlsAction::Fill))
				.ToolTip(LOCTEXT("QuickFillTargetHint", "Insert after the captured layer; with a group or no selection, append to the material stack."));
			Menu.Item(LOCTEXT("QuickAddMaterial", "Material"), MixtormatIcons::LayerMaterial(),
				Execute(EQuickControlsAction::Material, FSimpleDelegate::CreateLambda([this]()
								{
									const int32 PreviousCount = WorkingLayers.Num();
									const bool bHadMaterial = bHasWorkingMaterial;
									AddLayerOrStartMaterial(EMixtormatLayerType::Material);
									if (bHasWorkingMaterial && (!bHadMaterial || WorkingLayers.Num() > PreviousCount)
										&& WorkingLayers.IsValidIndex(SelectedLayerIndex))
									{
										ShowPreviewFeedback(FText::Format(LOCTEXT("QuickLayerAddedFeedback", "Added {0}"),
											WorkingLayers[SelectedLayerIndex].DisplayName));
									}
									else
									{
										ShowPreviewFeedback(LOCTEXT("QuickMaterialUnavailable", "Material unavailable · Select a library surface"));
									}
								})))
				.UnavailableReason(Reason(EQuickControlsAction::Material))
				.ToolTip(LOCTEXT("QuickMaterialTargetHint", "Append the selected Library surface to the material stack."));
			Menu.SubMenu(LOCTEXT("QuickAddEffects", "Effects"), MixtormatIcons::Effect(),
				Open(EQuickControlsAction::Effects, FOnGetContent::CreateLambda([this, Target]()
				{ return BuildAddEffectTargetMenu(ResolveQuickControlsAddTarget(Target)); })))
				.UnavailableReason(Reason(EQuickControlsAction::Effects))
				.ToolTip(LOCTEXT("QuickRootEffectsHint", "Add to the captured layer, not its selected module."));
			Menu.SubMenu(LOCTEXT("QuickAddIds", "IDs"), MixtormatIcons::Ids(),
				Open(EQuickControlsAction::Ids, FOnGetContent::CreateLambda([this, Target]()
				{ return BuildAddIdsMenu(ResolveQuickControlsAddTarget(Target)); })))
				.UnavailableReason(Reason(EQuickControlsAction::Ids))
				.ToolTip(LOCTEXT("QuickRootChildrenHint", "Add at the captured layer/group root."));
			Menu.SubMenu(LOCTEXT("QuickGeneratorSubmodules", "Generator Submodules"), MixtormatIcons::Generator(),
				Open(EQuickControlsAction::Generators, FOnGetContent::CreateLambda([this, Target]()
				{ return BuildAddGeneratorsMenu(ResolveQuickControlsAddTarget(Target)); })))
				.UnavailableReason(Reason(EQuickControlsAction::Generators))
				.ToolTip(LOCTEXT("QuickRootGeneratorsHint", "Add at the captured layer root."));
			Menu.SubMenu(LOCTEXT("QuickSurfacing", "Surfacing"), MixtormatIcons::Surfacing(),
				Open(EQuickControlsAction::Surfacing, FOnGetContent::CreateLambda([this, Target]()
				{ return BuildAddSurfacingMenu(ResolveQuickControlsAddTarget(Target)); })))
				.UnavailableReason(Reason(EQuickControlsAction::Surfacing))
				.ToolTip(LOCTEXT("QuickRootChildrenHint", "Add at the captured layer/group root."));
			Menu.SubMenu(LOCTEXT("QuickMasks", "Masks"), MixtormatIcons::Mask(),
				Open(EQuickControlsAction::Masks, FOnGetContent::CreateLambda([this, Context, Target, Reason, Execute]() -> TSharedRef<SWidget>
				{
					const FMixtormatChildAddress Address = Context->Address;
					const FMixtormatLayerChild* Child = ResolveChildAt(Address);
					if (!Child || !CanOwnScopedBlurs(*Child))
					{
						FMixtormatAddTarget Destination = ResolveQuickControlsAddTarget(Target);
						if (Child && CanOwnScopedMasks(*Child) && !Child->IsInstance())
						{

							Destination.ScopeOwnerChildId = Child->ChildId;
						}
						return BuildAddMasksMenu(Destination);
					}
					MixtormatMenu::FBuilder Masks;

					for (const EMixtormatLayerChildType Type : {EMixtormatLayerChildType::Blur, EMixtormatLayerChildType::Curvature})
					{
						Masks.Item(Type == EMixtormatLayerChildType::Blur
							? LOCTEXT("QuickMaskBlur", "Blur") : LOCTEXT("QuickMaskCurvature", "Curvature"), MixtormatIcons::Mask(),
							Execute(Type == EMixtormatLayerChildType::Blur ? EQuickControlsAction::Blur : EQuickControlsAction::Curvature,
							FSimpleDelegate::CreateLambda([this, Address, Type]()
							{
								const int32 ChildIndex = ResolveChildIndexAt(Address);
								if (Address.OwnerType == EMixtormatChildOwnerType::Group)
								{
									AddMaskFilterToGroupChild(Address.OwnerId, ChildIndex, Type);
								}
								else
								{
									const int32 LayerIndex = WorkingLayers.IndexOfByPredicate([&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
									AddMaskFilterToLayerChild(LayerIndex, ChildIndex, Type);
								}
							}))).UnavailableReason(Reason(Type == EMixtormatLayerChildType::Blur
								? EQuickControlsAction::Blur : EQuickControlsAction::Curvature));
					}
					return Masks.Build();
				}))).UnavailableReason(Reason(EQuickControlsAction::Masks))
				.ToolTip(LOCTEXT("QuickMasksTargetHint", "Use the captured module when it accepts scoped masks/filters; otherwise use its layer/group root."));
			Menu.SubMenu(LOCTEXT("QuickOutputCopy", "Output Copy"), MixtormatIcons::Duplicate(),
				Open(EQuickControlsAction::Outputs, FOnGetContent::CreateLambda([this, Context]()
				{ return BuildCopyChildOutputTargetMenu(Context->Address, Context); })))
				.UnavailableReason(Reason(EQuickControlsAction::Outputs));
			Menu.SubMenu(LOCTEXT("QuickGates", "Gates"), MixtormatIcons::Mask(),
				Open(EQuickControlsAction::Gates, FOnGetContent::CreateLambda([this, Context, Reason, Execute]() -> TSharedRef<SWidget>
					{
						MixtormatMenu::FBuilder Gates;
						const FMixtormatChildAddress Owner = Context->Address;
						Gates.Item(LOCTEXT("QuickNoiseMask", "Noise Mask"), MixtormatIcons::Mask(),
							Execute(EQuickControlsAction::NoiseGate, FSimpleDelegate::CreateLambda([this, Owner]() { CreateNoiseGate(Owner); })))
							.UnavailableReason(Reason(EQuickControlsAction::NoiseGate));
						Gates.Item(LOCTEXT("QuickGalleryGate", "Gallery Mask"), MixtormatIcons::Mask(),
							Execute(EQuickControlsAction::GalleryGate, FSimpleDelegate::CreateLambda([this, Owner]()
							{
								const int32 LayerIndex = WorkingLayers.IndexOfByPredicate([&Owner](const FMixtormatLayer& Layer) { return Layer.LayerId == Owner.OwnerId; });
								AssignScopedMaskToChild(LayerIndex, ResolveChildIndexAt(Owner), SelectedMaskPath);
							}))).UnavailableReason(Reason(EQuickControlsAction::GalleryGate));
						Gates.Item(LOCTEXT("QuickPasteGate", "Paste Gating Mask"), MixtormatIcons::Mask(),
							Execute(EQuickControlsAction::PasteGate, FSimpleDelegate::CreateLambda([this, Owner]() { PasteAsGatingMask(Owner); })))
							.UnavailableReason(Reason(EQuickControlsAction::PasteGate));
						return Gates.Build();
					}))).UnavailableReason(Reason(EQuickControlsAction::Gates))
				.ToolTip(LOCTEXT("QuickGatesTargetHint", "Add a gating mask to the captured module."));

	return Menu.Build();
}

	TSharedRef<SWidget> SMixtormat::BuildLayerColumnContextMenu()
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("ColumnAddMaterialLayer", "Add Material Layer"),
		MixtormatIcons::LayerMaterial(),
		FSimpleDelegate::CreateLambda([this]()
		{
			AddLayerOrStartMaterial(EMixtormatLayerType::Material);
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this]() { return !SelectedSurfacePath.IsNull(); }));
	Menu.Item(
		LOCTEXT("ColumnAddFillLayer", "Add Fill Layer"),
		MixtormatIcons::LayerFill(),
		FSimpleDelegate::CreateLambda([this]()
		{
			AddLayerOrStartMaterial(EMixtormatLayerType::Fill);
		}));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildReplaceInstanceSourceMenu(const FMixtormatChildAddress Address)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("ReplaceInstanceSourceCaption", "Source"));
	const TArray<FMixtormatLayerChild>* Container = ResolveContainer(Address);
	const FMixtormatLayerChild* Placement = ResolveChildAt(Address);
	if (!Container || !Placement)
	{
		return Menu.Build();
	}
	const int32 ChildIndex = ResolveChildIndexAt(Address);
	const int32 OwnerIndex = FindChildById(*Container, Placement->ScopeOwnerChildId);
	const FMixtormatLayerChild* ScopeOwner = Container->IsValidIndex(OwnerIndex)
		? &(*Container)[OwnerIndex]
		: nullptr;
	// Only what this position can legally read is offered, so the menu cannot put the instance
	// into a state the paste path would have refused.
	for (int32 SourceLayerIndex = 0; SourceLayerIndex < WorkingLayers.Num(); ++SourceLayerIndex)
	{
		const FMixtormatLayer& SourceLayer = WorkingLayers[SourceLayerIndex];
		for (const FMixtormatLayerChild& Candidate : SourceLayer.Children)
		{
			if (Candidate.IsInstance()
				|| (ScopeOwner && !CanKeepScopedPlacement(*ScopeOwner, Candidate)))
			{
				continue;
			}
			if (MixtormatParameterBinding::ClassifyInstancePlacement(
				FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				SourceLayer.LayerId,
				Candidate.ChildId,
				Address.OwnerId,
				ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
			{
				continue;
			}
			const FMixtormatChildAddress NewSource{
				EMixtormatChildOwnerType::Layer, SourceLayer.LayerId, Candidate.ChildId};
			Menu.Item(
				FText::Format(
					LOCTEXT("ReplaceInstanceSourceEntry", "{0} / {1}"),
					SourceLayer.DisplayName,
					GetLayerChildName(Candidate)),
				nullptr,
				FSimpleDelegate::CreateLambda([this, Address, NewSource]()
				{
					ReplaceChildInstanceSource(Address, NewSource);
				}))
				.Enabled(Candidate.Type != EMixtormatLayerChildType::Behavior
					|| (ScopeOwner && ScopeOwner->Type == EMixtormatLayerChildType::Generator));
		}
	}
	// A group's shared children are exactly as valid a source as a layer's -- see
	// ClassifyInstancePlacement's conservative member-range ordering rule for groups.
	for (const FMixtormatLayerGroup& SourceGroup : WorkingLayerGroups)
	{
		for (const FMixtormatLayerChild& Candidate : SourceGroup.Children)
		{
			if (Candidate.IsInstance()
				|| (ScopeOwner && !CanKeepScopedPlacement(*ScopeOwner, Candidate)))
			{
				continue;
			}
			if (MixtormatParameterBinding::ClassifyInstancePlacement(
				FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				SourceGroup.GroupId,
				Candidate.ChildId,
				Address.OwnerId,
				ChildIndex) != MixtormatParameterBinding::EInstancePlacement::Valid)
			{
				continue;
			}
			const FMixtormatChildAddress NewSource{
				EMixtormatChildOwnerType::Group, SourceGroup.GroupId, Candidate.ChildId};
			Menu.Item(
				FText::Format(
					LOCTEXT("ReplaceInstanceSourceGroupEntry", "{0} / {1}"),
					SourceGroup.DisplayName,
					GetLayerChildName(Candidate)),
				nullptr,
				FSimpleDelegate::CreateLambda([this, Address, NewSource]()
				{
					ReplaceChildInstanceSource(Address, NewSource);
				}))
				.Enabled(Candidate.Type != EMixtormatLayerChildType::Behavior
					|| (ScopeOwner && ScopeOwner->Type == EMixtormatLayerChildType::Generator));
		}
	}
	if (Menu.IsEmpty())
	{
		Menu.Item(LOCTEXT("ReplaceInstanceNoSource", "Nothing above this position"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildCopyChildOutputMenu(FMixtormatChildAddress Address)
{
	return BuildCopyChildOutputTargetMenu(Address, nullptr);
}

TSharedRef<SWidget> SMixtormat::BuildCopyChildOutputTargetMenu(FMixtormatChildAddress Address,
	TSharedPtr<const FMixtormatQuickControlsTarget> Context)
{
	MixtormatMenu::FBuilder Menu;
	if (const FMixtormatLayerChild* Child = ResolveChildAt(Address))
	{
		for (const FMixtormatPublishedOutputDesc& Output : GetCopyableOutputs(GetChildCapabilities(*Child)))
		{
			const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Address, Context, OutputName = Output.Name]()
			{
				if (Context.IsValid())
				{
					const FText Invalid = GetQuickControlsTargetUnavailableReason(Context);
					if (!Invalid.IsEmpty()) { return Invalid; }
				}
				return CanCopyChildOutput(Address, OutputName) ? FText::GetEmpty()
					: LOCTEXT("QuickOutputUnavailable", "This output is no longer published or cannot currently be copied.");
			});
			// Keep the output's row even if its availability changes while the submenu is open.
			Menu.Item(Output.Label, GetPublishedOutputIcon(Output),
				FSimpleDelegate::CreateLambda([this, Address, Context, Reason, OutputName = Output.Name]()
				{
					const FText Disabled = Reason.Get();
					if (!Disabled.IsEmpty())
					{
						if (Context.IsValid()) { ShowPreviewFeedback(Disabled); }
						return;
					}
					CopyChildOutput(Address, OutputName);
				})).UnavailableReason(Reason);
		}
	}
	return Menu.Build();
}

void SMixtormat::AddSharedChildMenuItems(
	MixtormatMenu::FBuilder& Menu,
	const FMixtormatChildAddress& Address)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Address);
	const bool bInstance = Child && Child->IsInstance();
	const bool bSupportsInstancing = !Child || GetChildCapabilities(*Child).bSupportsInstancing;

	// Type-specific actions first. Everything below this block is what *this* child can do that a
	// sibling cannot -- its published outputs, the mask source it reads, the gate it owns. Callers
	// put applicable child creation above this function, and the Duplicate/Copy/Paste rows this
	// function appends below come after, so a menu always reads create -> specialise -> copy ->
	// remove without any call site having to know the rule.
	if (Child)
	{
		// One entry point for published data. The rows used to be repeated here as flattened
		// "Copy Gate · X" items as well, which said the same thing twice and buried the structural
		// actions; the submenu is the single, discoverable home for them now. Omitted entirely when
		// there is nothing to publish, rather than shown disabled and spending a row on nothing.
		if (!GetCopyableOutputs(GetChildCapabilities(*Child)).IsEmpty())
		{
			Menu.SubMenu(LOCTEXT("CopyChildOutputContext", "Outputs"), MixtormatIcons::Generated(),
				FOnGetContent::CreateSP(this, &SMixtormat::BuildCopyChildOutputMenu, Address));
		}
	}
	if (Child && Child->Type == EMixtormatLayerChildType::Mask
		&& !bInstance && !Child->Mask.HasPublishedSource())
	{
		Menu.SubMenu(LOCTEXT("MaskSourceContext", "Source"), nullptr,
			FOnGetContent::CreateLambda([this, Address]()
			{
				return BuildMaskSourceMenuFor(Address);
			}));
	}
	if (Child && CanOwnScopedMasks(*Child))
	{
		Menu.Item(
			LOCTEXT("PasteAsGatingMaskContext", "Paste as Gating Mask"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, Address]() { PasteAsGatingMask(Address); }))
			.Enabled(TAttribute<bool>::CreateLambda([this, Address]() { return CanPasteAsGatingMask(Address); }));
	}
	if (Child && Child->Type == EMixtormatLayerChildType::OutputReference && !bInstance)
	{
		Menu.Item(LOCTEXT("GoToOutputSourceContext", "Go to Source"),
			MixtormatIcons::ArrowUp(),
			FSimpleDelegate::CreateLambda([this, Address]() { GoToChildInstanceSource(Address); }))
			.Enabled(MixtormatParameterBinding::FindChild(FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				Child->OutputReference.SourceLayerId, Child->OutputReference.SourceChildId) != nullptr);
		Menu.SubMenu(LOCTEXT("ReplaceOutputSourceContext", "Source"), nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildOutputReferenceSourceMenu, Address))
			.Enabled(!bInstance);
	}

	// The clipboard verbs, in the one order every menu uses: Copy, then Copy as Instance, then the
	// single Paste whose own Mode decides which of the two it lands as.
	Menu.Item(
		LOCTEXT("CopyChildContext", "Copy"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChild(Address, false);
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Copy));
	// Only where a second row could share this child's identity. A scoped mask filter cannot, so
	// offering the entry here would be an invitation the paste path refuses a click later.
	if (bSupportsInstancing)
	{
		Menu.Item(
			LOCTEXT("CopyChildAsInstanceContext", "Copy as Instance"),
			MixtormatIcons::Duplicate(),
			FSimpleDelegate::CreateLambda([this, Address]()
			{
				CopyChild(Address, true);
			})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::CopyAsInstance));
	}
	if (CanPasteChild(Address, ResolveChildIndexAt(Address)))
	{
		Menu.Item(LOCTEXT("PasteChildContext", "Paste"), nullptr,
			FSimpleDelegate::CreateLambda([this, Address]()
			{
				PasteChild(Address, ResolveChildIndexAt(Address));
			})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Paste));
	}
	if (Child && Child->Type == EMixtormatLayerChildType::Mask)
	{
		Menu.Item(LOCTEXT("RenameMaskChildContext", "Rename"), nullptr,
			FSimpleDelegate::CreateLambda([this, Address]()
			{
				if (const TWeakPtr<SMixtormatLayerChildRow>* Row = EditableChildRowWidgets.Find(Address))
				{
					RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
						[WeakRow = *Row](double, float)
						{
							if (const TSharedPtr<SMixtormatLayerChildRow> Pinned = WeakRow.Pin())
								Pinned->BeginRename();
							return EActiveTimerReturnType::Stop;
						}));
				}
			})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Rename));
	}
	if (!bInstance)
	{
		return;
	}
	Menu.Separator();
	Menu.Caption(LOCTEXT("ChildInstanceCaption", "Instance"));
	Menu.Item(
		LOCTEXT("GoToInstanceSourceContext", "Go to Source"),
		MixtormatIcons::ArrowUp(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			GoToChildInstanceSource(Address);
		}));
	Menu.Item(
		LOCTEXT("BreakInstanceContext", "Break Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			BreakChildInstanceAt(Address);
		}));
	Menu.SubMenu(
		LOCTEXT("ReplaceInstanceSourceContext", "Replace Source"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildReplaceInstanceSourceMenu, Address));
	Menu.Item(
		LOCTEXT("CopyInstanceReferenceContext", "Copy Instance Reference"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChildInstanceReference(Address);
		}));
}

TSharedRef<SWidget> SMixtormat::BuildGroupChildContextMenu(
	const FGuid GroupId,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		Menu.SubMenu(
			LOCTEXT("MaskBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateLambda([this, GroupId, ChildIndex]()
			{
				SelectGroupChild(GroupId, ChildIndex);
				return BuildMaskBlendModeMenu(INDEX_NONE, ChildIndex);
			}));
		// The same entry a layer's mask has, and the only way to replace one: a grid inside a
		// context menu is not how a mask gets picked -- that is the gallery, or a drag from it.
		const FSoftObjectPath ReplacementPath = SelectedMaskPath;
		Menu.Item(
			FText::Format(
				LOCTEXT("ReplaceWithSelectedMask", "Replace with {0}"),
				SelectedLibraryMaskName.IsEmpty()
					? LOCTEXT("NoSelectedReplacementMask", "Select Mask from Gallery")
					: SelectedLibraryMaskName),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex, ReplacementPath]()
			{
				// ReplaceMaskInLayer resolves through ResolveChild, which reaches a group's shared
				// stack only when the layer lane is clear and SelectedGroupId names the group --
				// so the selection is moved onto this child first rather than assumed.
				SelectGroupChild(GroupId, ChildIndex);
				ReplaceMaskInLayer(INDEX_NONE, ChildIndex, ReplacementPath);
			}))
			.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));

		// A shared mask carries the same scoped children a layer's mask does. The group is
		// flattened into each member at compose time and ScopeOwnerChildId is rebound per member,
		// so one blur authored here gates this mask in every one of them.
		const bool bCanNestChild = CanAddScopedChild(Group->Children, ChildIndex);
		Menu.Item(
			LOCTEXT("AddBlurToMaskContext", "Add Blur"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddMaskFilterToGroupChild(GroupId, ChildIndex, EMixtormatLayerChildType::Blur);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Item(
			LOCTEXT("AddCurvatureToMaskContext", "Add Curvature"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddMaskFilterToGroupChild(GroupId, ChildIndex, EMixtormatLayerChildType::Curvature);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Item(
			LOCTEXT("AddFlowWarpToMask", "Add Flow Warp · Targets This Mask"),
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddFlowWarpToGroupChild(GroupId, ChildIndex);
			}))
			.Enabled(TAttribute<bool>(bCanNestChild));
		Menu.Separator();
	}
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& Group->Children[ChildIndex].Type != EMixtormatLayerChildType::Mask
		&& GetChildMaskBlendMode(ResolveChildAt(MakeGroupChildAddress(GroupId, ChildIndex))))
	{
		Menu.SubMenu(LOCTEXT("GeneratedBlendModeContext", "Blend Mode"), nullptr,
			FOnGetContent::CreateLambda([this, GroupId, ChildIndex]()
			{
				SelectGroupChild(GroupId, ChildIndex);
				return BuildGeneratedBlendModeMenu(INDEX_NONE, ChildIndex);
			}));
		Menu.Separator();
	}
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& (Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Blur
			|| Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Surfacing)
		&& CanOwnScopedMasks(Group->Children[ChildIndex]))
	{
		FMixtormatAddTarget Destination = FMixtormatAddTarget::Group(GroupId);
		Destination.ScopeOwnerChildId = Group->Children[ChildIndex].ChildId;
		if (Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Surfacing)
		{
			Menu.SubMenu(LOCTEXT("AddSurfacingChild", "Surfacing"), MixtormatIcons::Surfacing(),
				FOnGetContent::CreateSP(this, &SMixtormat::BuildAddSurfacingMenu, Destination))
				.Enabled(CanAddSurfacingModule(Destination));
			Menu.Item(LOCTEXT("SurfacingBehaviorsUnavailable", "Behaviors"), MixtormatIcons::WarpStructural(), FSimpleDelegate())
				.Enabled(false).ToolTip(LOCTEXT("SurfacingBehaviorsUnavailableHint", "Surfacing has no Behavior GPU rewrite adapter. Author upstream changes on a Generator instead."));
			Menu.Item(LOCTEXT("SurfacingBlurUnavailable", "Blur"), MixtormatIcons::Filter(), FSimpleDelegate())
				.Enabled(false).ToolTip(LOCTEXT("SurfacingBlurUnavailableHint", "Surfacing-local Blur is not integrated. Use a layer-output Blur instead."));
		}
		Menu.SubMenu(LOCTEXT("AddScopedMasksToChild", "Masks"), MixtormatIcons::Mask(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Destination))
			.Enabled(!Group->Children[ChildIndex].IsInstance()
				&& CanAddScopedChild(Group->Children, ChildIndex));
		Menu.Separator();
	}
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& Group->Children[ChildIndex].Type != EMixtormatLayerChildType::Mask
		&& CanOwnScopedBlurs(Group->Children[ChildIndex]))
	{
		Menu.Item(LOCTEXT("AddBlurToScopedOwnerContext", "Add Blur"), MixtormatIcons::Filter(),
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
			{
				AddMaskFilterToGroupChild(GroupId, ChildIndex, EMixtormatLayerChildType::Blur);
			})).Enabled(CanAddScopedChild(Group->Children, ChildIndex)
				&& !Group->Children[ChildIndex].IsInstance());
	}
	// Copy / Copy as Instance / Copy Output / Paste, and (for an instance) Go to Source / Break
	// Instance / Replace Source / Copy Instance Reference -- the same rows a layer child's menus
	// build via AddSharedChildMenuItems, driven by the same address-based clipboard rather than a
	// second, group-specific copy of this logic.
	AddIdGroupMenuItems(Menu, MakeGroupChildAddress(GroupId, ChildIndex));
	AddSharedChildMenuItems(Menu, MakeGroupChildAddress(GroupId, ChildIndex));
	Menu.Item(
		LOCTEXT("RemoveGroupChildContext", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
		{
			RemoveGroupChild(GroupId, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGroupAddEffectMenu(const FGuid GroupId)
{
	MixtormatMenu::FBuilder Menu;
	for (const FMixtormatEffectEntry& Entry : FMixtormatRegistry::GetEffects())
	{
		Menu.Item(
			Entry.DisplayName,
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, GroupId, EffectPath = Entry.AssetPath]()
			{
				AddEffectToGroup(GroupId, EffectPath);
			}));
	}
	Menu.Item(
		LOCTEXT("AddPeelingEffect", "Peeling"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			CreateChild(FMixtormatAddTarget::Group(GroupId), EMixtormatChildCreation::Peeling);
		}));
	return Menu.Build();
}

// A strip of swatches rather than a submenu of named colours or the engine's colour dialog: it is
// one click from the menu that opened it, and a colour is a thing you point at, not a thing you
// read the name of. The leftmost clears back to no colour.
TSharedRef<SWidget> SMixtormat::BuildGroupAccentMenu(const FGuid GroupId)
{
	const TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	const auto AddSwatch =
		[this, &Strip, GroupId](const FLinearColor& Swatch, const FText& ToolTip)
	{
		Strip->AddSlot()
		.AutoWidth()
		.Padding(MixtormatTokens::GroupAccentSwatchGap * 0.5f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::GroupAccentSwatchSize)
			.HeightOverride(MixtormatTokens::GroupAccentSwatchSize)
			.ToolTip(SMixtormatHelp::MakeStyledToolTip(ToolTip))
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), TEXT("NoBorder"))
				.ContentPadding(0.0f)
				.OnClicked_Lambda([this, GroupId, Swatch]()
				{
					FSlateApplication::Get().DismissAllMenus();
					return SetLayerGroupAccentColor(GroupId, Swatch);
				})
				[
					SNew(SColorBlock)
					.Color(Swatch.A > 0.0f ? Swatch : FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel))
					.ShowBackgroundForAlpha(false)
					.Size(FVector2D(
						MixtormatTokens::GroupAccentSwatchSize,
						MixtormatTokens::GroupAccentSwatchSize))
				]
			]
		];
	};

	AddSwatch(
		FLinearColor(0.0f, 0.0f, 0.0f, 0.0f),
		LOCTEXT("GroupAccentNone", "No colour"));
	static const FLinearColor GroupAccentSwatches[] = {
		FLinearColor::FromSRGBColor(FColor(0xE0, 0x52, 0x52)),
		FLinearColor::FromSRGBColor(FColor(0xE0, 0x8A, 0x42)),
		FLinearColor::FromSRGBColor(FColor(0xE0, 0xC2, 0x4A)),
		FLinearColor::FromSRGBColor(FColor(0x6F, 0xBF, 0x5A)),
		FLinearColor::FromSRGBColor(FColor(0x4F, 0xB0, 0xB5)),
		FLinearColor::FromSRGBColor(FColor(0x5A, 0x8F, 0xD6)),
		FLinearColor::FromSRGBColor(FColor(0x9B, 0x72, 0xD0)),
		FLinearColor::FromSRGBColor(FColor(0xD0, 0x66, 0xA5)),
	};
	for (const FLinearColor& Swatch : GroupAccentSwatches)
	{
		AddSwatch(Swatch, LOCTEXT("GroupAccentSwatch", "Tag this group with this colour"));
	}

	MixtormatMenu::FBuilder Menu;
	Menu.Widget(SNew(SBox).Padding(MixtormatTokens::GroupAccentSwatchGap)[Strip]);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerGroupContextMenu(const FGuid GroupId)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerGroup* Group =
		MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
	const bool bGroupEnabled = !Group || Group->bEnabled;

	// Everything added here is authored once and applies to every member. That is the reason a
	// group exists -- the alternative is the same effect copied into each layer by hand.
	Menu.Caption(LOCTEXT("GroupAddSection", "Add · Shared"));
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAddEffectMenu, GroupId));
	// The same four submenus the layer menu builds, from the same four functions. A shared child
	// works here for the reason every other one does: BuildEffectiveLayers appends a remapped copy
	// of the group's stack to each member before composition, so one node authored here runs over
	// every member's own input with that member's own scoped masks and region IDs rebound to it.
	AddCreationSections(Menu, FMixtormatAddTarget::Group(GroupId));

	Menu.Separator();
	Menu.SubMenu(LOCTEXT("CreateGroupRecipe", "Create Recipe…"), MixtormatIcons::Add(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildRecipeCaptureMenu, GroupId))
		.Enabled(Group != nullptr)
		.ToolTip(LOCTEXT("CreateGroupRecipeHelp", "Capture this group and its ordered members. Outside references are rejected and asset dependencies are reported."));
	// Type-specific actions next: the things only a group row can do, before the shared verbs.
	Menu.SubMenu(
		LOCTEXT("GroupAccentContext", "Colour"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAccentMenu, GroupId));
	Menu.Item(
		bGroupEnabled
			? LOCTEXT("DisableGroupContext", "Disable")
			: LOCTEXT("EnableGroupContext", "Enable"),
		bGroupEnabled ? MixtormatIcons::EyeOff() : MixtormatIcons::Eye(),
		FSimpleDelegate::CreateLambda([this, GroupId, bGroupEnabled]()
		{
			SetLayerGroupEnabled(GroupId, !bGroupEnabled);
		}));

	Menu.Separator();
	// Ungroup, not Remove: it releases the layers and keeps every one of them, which is the only
	// group operation that cannot lose work. A group therefore has no Remove row at all -- the
	// verb means the same thing everywhere else and would promise a loss this action does not do.
	Menu.Item(
		LOCTEXT("UngroupGroupContext", "Ungroup"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));
	Menu.Item(
		LOCTEXT("RenameGroupContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			SelectLayerGroup(GroupId);
			BeginRenameSelection();
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Rename));

	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerContextMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;

	// Creation lives here and nowhere else. The stack used to carry an "Add Child" button at the
	// bottom of every expanded layer, which cost a row of height per layer to say something the
	// right button already implies.
	Menu.Caption(LOCTEXT("LayerAddSection", "Add"));
	Menu.SubMenu(
		LOCTEXT("AddEffectChild", "Effect"),
		MixtormatIcons::Effect(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddEffectMenu, LayerIndex));
	// IDs / Filter / Masks / Generators, built by the same four functions the group menu calls.
	AddCreationSections(Menu, FMixtormatAddTarget::Layer(LayerIndex));
	Menu.Separator();

	// Solo is reachable two ways on purpose: ctrl or alt on the eye for someone who knows, and
	// here for someone who does not. A modifier that exists nowhere in the UI is a secret.
	Menu.Item(
		LOCTEXT("SoloLayerContext", "Solo"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { ToggleLayerSolo(LayerIndex); }))
		.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return SoloLayerIndex == LayerIndex;
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Solo));
	Menu.Item(
		LOCTEXT("DisableLayerContext", "Disable"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			if (!WorkingLayers.IsValidIndex(LayerIndex))
			{
				return;
			}
			SetWorkingLayerEnabled(
				WorkingLayers[LayerIndex].bEnabled ? ECheckBoxState::Unchecked : ECheckBoxState::Checked,
				LayerIndex);
		}))
		.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].bEnabled;
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::ToggleEnabled));

	Menu.Separator();

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Type == EMixtormatLayerType::Material)
	{
		// Reuse the persistent bottom-library selection instead of opening a second thumbnail gallery.
		// Capturing the path keeps the action deterministic for the lifetime of this menu.
		const FSoftObjectPath ReplacementPath = SelectedSurfacePath;
		const FText ReplacementName = SelectedLibrarySurfaceName.IsEmpty()
			? LOCTEXT("SelectedMaterialFallback", "Selected Material")
			: SelectedLibrarySurfaceName;
		Menu.Item(
			FText::Format(LOCTEXT("ReplaceWithSelectedSurfaceContext", "Replace with {0}"), ReplacementName),
			MixtormatIcons::LayerMaterial(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ReplacementPath]()
			{
				ReplaceSurfaceInLayer(LayerIndex, ReplacementPath);
			}))
			.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));
	}

	Menu.Item(
		LOCTEXT("DuplicateLayerContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]() { ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate); }))
				.Enabled(TAttribute<bool>::CreateLambda([this]()
				{
					return CanExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
				})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Duplicate));

	// Acts on the multi-selection, not on the row the menu opened over, so shift-picking a run of
	// layers and right-clicking any of them does the same thing as the toolbar button.
	// The menu is rebuilt on every open, so the count in the label is the count at open time.
	// One verb, like Remove: "Group" says the same whether it groups one layer or twenty, and the
	// count rides along rather than turning the action into a second, differently named command.
	const int32 SelectionCount = GetSelectedLayerIndices().Num();
	Menu.Item(
		SelectionCount > 1
			? FText::Format(LOCTEXT("CreateGroupFromManyContext", "Group {0} Layers"),
				FText::AsNumber(SelectionCount))
			: LOCTEXT("CreateGroupContext", "Group"),
		MixtormatIcons::Folder(),
		FSimpleDelegate::CreateLambda([this]() { ExecuteLayerSelectionCommand(ELayerSelectionCommand::Group); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]()
		{
			return CanExecuteLayerSelectionCommand(ELayerSelectionCommand::Group);
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Group));
	if (WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].GroupId.IsValid())
	{
		const FGuid GroupId = WorkingLayers[LayerIndex].GroupId;
		Menu.Item(
			LOCTEXT("UngroupLayerContext", "Ungroup"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));
	}

	Menu.Item(
		LOCTEXT("RenameLayerContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			SelectWorkingLayer(LayerIndex);
			BeginRenameSelection();
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Rename));

	// A plain copy lands at the end of the layer; an instance lands at the top, except that a
	// source in this same layer pushes it to the first slot below that source. One Paste row for
	// both -- the clipboard's own Mode decides which -- stays visible and disabled when nothing
	// works.
	Menu.Item(
		LOCTEXT("PasteChildContext", "Paste"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			if (WorkingLayers.IsValidIndex(LayerIndex))
			{
				PasteChild({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()});
			}
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex)
				&& CanPasteChild({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()}, INDEX_NONE);
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Paste));

	Menu.Item(
		LOCTEXT("PasteAsGatingMaskContext", "Paste as Gating Mask"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			if (WorkingLayers.IsValidIndex(LayerIndex))
			{
				PasteAsGatingMask({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()});
			}
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, LayerIndex]()
		{
			return WorkingLayers.IsValidIndex(LayerIndex)
				&& CanPasteAsGatingMask({EMixtormatChildOwnerType::Layer, WorkingLayers[LayerIndex].LayerId, FGuid()});
		}));

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.ContainsByPredicate([](const FMixtormatLayerChild& Child)
		{
			return Child.Type == EMixtormatLayerChildType::Mask
				&& !Child.ScopeOwnerChildId.IsValid();
		}))
	{
		Menu.Item(
			LOCTEXT("ClearLayerMasksContext", "Remove All Masks"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex]() { ClearLayerMask(LayerIndex); }));
	}

	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveLayerContext", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this]() { ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]()
		{
			return CanExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();

	return Menu.Build();
}

// The Add tree, authored once.
//
// IDs create or modify Region IDs; Filters consume them and transform something else; Masks make
// coverage; Generators write structural height. The categories are the plugin's own taxonomy and
// the submenu each node sits in is the claim the inspector and the compositor make about it -- an
// ID node buried under Filter said the opposite of what it does.
void SMixtormat::AddCreationSections(MixtormatMenu::FBuilder& Menu, const FMixtormatAddTarget Target)
{
	Menu.SubMenu(
		LOCTEXT("AddIdsChild", "IDs"),
		MixtormatIcons::Ids(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddIdsMenu, Target));
	Menu.SubMenu(
		LOCTEXT("AddFilterChild", "Filter"),
		MixtormatIcons::Filter(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFiltersMenu, Target));
	Menu.SubMenu(
		LOCTEXT("AddMasksChild", "Masks"),
		MixtormatIcons::Mask(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Target));
	// Its own category beside Effect and Filter, not inside either. What separates it is *when*
	// it runs: an effect filters the layer after it has composited, a generator rewrites the
	// height the layer composites from. Filing it under Effect would be the first step toward
	// implementing it as one.
	if (CanAddGeneratorModule(Target) || CanAddScopedFlowGenerator(Target))
	{
		Menu.SubMenu(
			LOCTEXT("AddGeneratorChild", "Generators"),
			MixtormatIcons::Generator(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddGeneratorsMenu, Target));
	}
	Menu.SubMenu(LOCTEXT("AddSurfacingChild", "Surfacing"), MixtormatIcons::Surfacing(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddSurfacingMenu, Target))
		.Enabled(CanAddSurfacingModule(Target));
}

TSharedRef<SWidget> SMixtormat::BuildAddSurfacingMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Target]()
	{
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Surfacing);
	});
	Menu.Item(LOCTEXT("AddSkinChild", "Skin"), MixtormatIcons::Surfacing(),
		MakeAddMenuAction(Target, [this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Skin); }, EAddMenuKind::Surfacing))
		.UnavailableReason(Reason)
		.ToolTip(LOCTEXT("AddSkinHint", "Curvature-driven tissue, folds and material response. Gates are authored as child masks."));
	Menu.Item(LOCTEXT("AddRustChild", "Rust · Ferrous Corrosion"), MixtormatIcons::Surfacing(),
		MakeAddMenuAction(Target, [this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Rust); }, EAddMenuKind::Surfacing))
		.UnavailableReason(Reason)
		.ToolTip(LOCTEXT("AddRustHint", "Assumes the selected scope is ferrous. Clustered metal loss and retained oxide; no generated steel or coating recovery."));
	Menu.Item(LOCTEXT("AddMossChild", "Moss"), MixtormatIcons::Surfacing(),
		MakeAddMenuAction(Target, [this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Moss); }, EAddMenuKind::Surfacing))
		.UnavailableReason(Reason)
		.ToolTip(LOCTEXT("AddMossHint", "Habitat-driven moss growth, relief and material response. Gates are authored as child masks."));
	Menu.Item(LOCTEXT("AddWoodChild", "Wood"), MixtormatIcons::Surfacing(),
		MakeAddMenuAction(Target, [this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Wood); }, EAddMenuKind::Surfacing))
		.UnavailableReason(Reason)
		.ToolTip(LOCTEXT("AddWoodHint", "Oak, Maple or Pine anatomy with independent figure, preparation and stackable finish passes."));
	Menu.Item(LOCTEXT("AddStoneChild", "Stone · Marble & Granite"), MixtormatIcons::Surfacing(),
		MakeAddMenuAction(Target, [this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Stone); }, EAddMenuKind::Surfacing))
		.UnavailableReason(Reason)
		.ToolTip(LOCTEXT("AddStoneHint", "Shared mineral fabric: warped veins, bands and crystal cells for marble, granite, limestone, travertine, sandstone, quartzite and slate."));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddIdsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddPatternIdChild", "Pattern IDs"), EMixtormatChildCreation::PatternIds },
		{ LOCTEXT("AddSurfaceIdsChild", "Surface IDs"), EMixtormatChildCreation::SurfaceIds },

		{ LOCTEXT("AddIdGroupChild", "ID Group"), EMixtormatChildCreation::IdGroup },
	};
	const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Target]()
	{
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Child);
	});
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Ids(),
			MakeAddMenuAction(Target, [this, Kind = Entry.Value](FMixtormatAddTarget Resolved)
			{
				CreateChild(Resolved, Kind);
			}))
			.UnavailableReason(Reason);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildIdGroupSourceMenu(const FMixtormatChildAddress Dest)
{
	return BuildPublishedSourceMenu(Dest, EMixtormatPublishedFieldKind::RegionIds);
}

TSharedRef<SWidget> SMixtormat::BuildOutputReferenceSourceMenu(const FMixtormatChildAddress Dest)
{
	const FMixtormatLayerChild* Destination = ResolveChildAt(Dest);
	return BuildPublishedSourceMenu(Dest, Destination
		? Destination->OutputReference.Kind : EMixtormatPublishedFieldKind::RegionIds);
}

TSharedRef<SWidget> SMixtormat::BuildPublishedSourceMenu(
	const FMixtormatChildAddress Dest, const EMixtormatPublishedFieldKind Kind)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(Kind == EMixtormatPublishedFieldKind::RegionIds
		? LOCTEXT("IdGroupSourcesCaption", "Region IDs sources")
		: LOCTEXT("PublishedSourcesCaption", "Published field sources"));
	const FMixtormatLayerChild* Destination = ResolveChildAt(Dest);
	if (!Destination) { return Menu.Build(); }
	const bool bAdd = Destination->Type == EMixtormatLayerChildType::IdGroup;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	const auto AddSources = [this, &Menu, &Scope, &Dest, Destination, bAdd, Kind](
		const EMixtormatChildOwnerType OwnerType, const FGuid OwnerId, const FText& OwnerName,
		const TArray<FMixtormatLayerChild>& Children)
	{
		for (const FMixtormatLayerChild& Producer : Children)
		{
			const FMixtormatChildAddress Source{OwnerType, OwnerId, Producer.ChildId};
			FMixtormatLayerChild Reference;
			if (!MakePublishedFieldReference(Producer, Source, Kind, Reference)) { continue; }
			Reference.ChildId = Destination->ChildId;
			Reference.ScopeOwnerChildId = Destination->ScopeOwnerChildId;
			const bool bAvailable = bAdd ? CanAddIdGroupSource(Source, Dest)
				: Destination->Type == EMixtormatLayerChildType::OutputReference
					&& Destination->OutputReference.Kind == Kind && !Destination->IsInstance()
					&& IsPublishedSourceEnabled(Scope, Source.OwnerId, Source.ChildId)
					&& IsPublishedSourceEnabled(Scope, Reference.OutputReference.SourceLayerId, Reference.OutputReference.SourceChildId)
					&& CanReadPublishedOutputAt(Scope, Reference, Dest.OwnerId, ResolveChildIndexAt(Dest));
			Menu.Item(FText::Format(LOCTEXT("IdGroupSourceEntry", "{0} / {1}"), OwnerName,
				GetLayerChildName(Producer)),
				Kind == EMixtormatPublishedFieldKind::RegionIds ? MixtormatIcons::Ids() : MixtormatIcons::Generated(),
				FSimpleDelegate::CreateLambda([this, Dest, Source, Ref = Reference.OutputReference, bAdd]()
				{
					if (bAdd) { AddIdGroupSource(Source, Dest); }
					else { ReplaceOutputReferenceSource(Dest, Ref); }
				})).Enabled(bAvailable);
		}
	};
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		AddSources(EMixtormatChildOwnerType::Layer, Layer.LayerId, Layer.DisplayName, Layer.Children);
	}
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		AddSources(EMixtormatChildOwnerType::Group, Group.GroupId, Group.DisplayName, Group.Children);
	}
	if (Menu.IsEmpty())
	{
		Menu.Item(Kind == EMixtormatPublishedFieldKind::RegionIds
			? LOCTEXT("IdGroupNoSources", "No Region IDs outputs")
			: LOCTEXT("PublishedNoSources", "No compatible outputs"),
			nullptr, FSimpleDelegate()).Enabled(false);
	}
	return Menu.Build();
}

void SMixtormat::AddIdGroupMenuItems(MixtormatMenu::FBuilder& Menu, const FMixtormatChildAddress& Owner)
{
	const FMixtormatLayerChild* Child = ResolveChildAt(Owner);
	if (!Child || Child->Type != EMixtormatLayerChildType::IdGroup)
	{
		return;
	}
	FMixtormatAddTarget Target = Owner.OwnerType == EMixtormatChildOwnerType::Group
		? FMixtormatAddTarget::Group(Owner.OwnerId)
		: FMixtormatAddTarget::Layer(WorkingLayers.IndexOfByPredicate(
			[&Owner](const FMixtormatLayer& Layer) { return Layer.LayerId == Owner.OwnerId; }));
	Target.ScopeOwnerChildId = Owner.ChildId;
	Menu.SubMenu(LOCTEXT("IdGroupAddSource", "Add Source"), MixtormatIcons::Ids(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildIdGroupSourceMenu, Owner));
	const bool bEnabled = CanCreateChild(Target);
	Menu.SubMenu(
		LOCTEXT("IdGroupAddIds", "Add IDs"),
		MixtormatIcons::Ids(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddIdsMenu, Target))
		.Enabled(TAttribute<bool>(bEnabled));
	Menu.SubMenu(
		LOCTEXT("IdGroupAddFromIds", "Add From IDs"),
		MixtormatIcons::Ids(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFromIdsMenu, Target))
		.Enabled(TAttribute<bool>(bEnabled));
	Menu.Separator();
}

TSharedRef<SWidget> SMixtormat::BuildAddFromIdsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddColorIdChild", "Color ID Mask"), EMixtormatChildCreation::ColorIdMask },
		{ LOCTEXT("AddBoundaryIdChild", "Boundary From IDs"), EMixtormatChildCreation::BoundaryFromIds },
		{ LOCTEXT("AddHsvFilterChild", "HSV From IDs"), EMixtormatChildCreation::HsvFromIds },
		{ LOCTEXT("AddRandomIdChild", "Random From IDs"), EMixtormatChildCreation::RandomFromIds },
		{ LOCTEXT("AddRampIdChild", "Ramp From IDs"), EMixtormatChildCreation::RampFromIds },
		{ LOCTEXT("AddUvIdChild", "UV From IDs"), EMixtormatChildCreation::UvFromIds },
		{ LOCTEXT("AddReliefIdChild", "Relief From IDs"), EMixtormatChildCreation::ReliefFromIds },
	};
	const bool bEnabled = CanCreateChild(Target);
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Ids(),
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]() { CreateChild(Target, Kind); }))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddFiltersMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("AddRootBlur", "Blur"),
		MixtormatIcons::Filter(),
		FSimpleDelegate::CreateLambda([this, Target]() { AddLayerBlurToLayer(Target.LayerIndex); }))
		.Enabled(!Target.IsGroup() && !Target.ScopeOwnerChildId.IsValid() && CanCreateChild(Target));
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddHsvFilterChild", "HSV From IDs"), EMixtormatChildCreation::HsvFromIds },
		{ LOCTEXT("AddRampIdChild", "Ramp From IDs"), EMixtormatChildCreation::RampFromIds },
		{ LOCTEXT("AddBoundaryIdChild", "Boundary From IDs"), EMixtormatChildCreation::BoundaryFromIds },
		// The two halves Pattern IDs used to carry itself. Beside Ramp rather than under IDs,
		// because they consume Region IDs and change something else -- which is what a Filter is.
		{ LOCTEXT("AddUvIdChild", "UV From IDs"), EMixtormatChildCreation::UvFromIds },
		{ LOCTEXT("AddReliefIdChild", "Relief From IDs"), EMixtormatChildCreation::ReliefFromIds },
	};
	const bool bEnabled = CanCreateChild(Target);
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		Menu.Item(
			Entry.Key,
			MixtormatIcons::Ids(),
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]()
			{
				CreateChild(Target, Kind);
			}))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddMasksMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Target]()
	{
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Child);
	});
	const TArray<FMixtormatLayerChild>* ScopedChildren = nullptr;
	if (Target.IsGroup())
	{
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Target.GroupId);
		ScopedChildren = Group ? &Group->Children : nullptr;
	}
	else if (WorkingLayers.IsValidIndex(Target.LayerIndex))
	{
		ScopedChildren = &WorkingLayers[Target.LayerIndex].Children;
	}
	const int32 ScopedOwnerIndex = ScopedChildren
		? MixtormatLayersPrivate::FindChildById(*ScopedChildren, Target.ScopeOwnerChildId)
		: INDEX_NONE;
	const bool bScopedGateOwner = ScopedChildren && ScopedChildren->IsValidIndex(ScopedOwnerIndex)
		&& (*ScopedChildren)[ScopedOwnerIndex].Type != EMixtormatLayerChildType::IdGroup;

	// Texture Mask first, and it takes whatever the gallery has selected. The ellipsis is the
	// honest part of the label: unlike everything below it, this one cannot create anything until
	// a mask has been picked downstairs.
	const FSoftObjectPath MaskPath = SelectedMaskPath;
	Menu.Item(
		FText::Format(
			LOCTEXT("AddTextureMaskChild", "Texture Mask · {0}"),
			SelectedLibraryMaskName.IsEmpty()
				? LOCTEXT("NoSelectedLibraryMask", "Select Mask from Gallery")
				: SelectedLibraryMaskName),
		MixtormatIcons::Mask(),
		MakeAddMenuAction(Target, [this, MaskPath](FMixtormatAddTarget Resolved)
		{
			AddTextureMask(Resolved, MaskPath);
		}))
		.UnavailableReason(TAttribute<FText>::CreateLambda([Reason, MaskPath]()
		{
			const FText Disabled = Reason.Get();
			return !Disabled.IsEmpty() ? Disabled : MaskPath.IsNull()
				? LOCTEXT("QuickGalleryMaskRequired", "Select a mask in Library first.") : FText::GetEmpty();
		}));

	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		// Beside the other mask producers rather than under the asset picker above, because it
		// needs no asset: it reads the layer it is added to.
		{ LOCTEXT("AddLayerValuesChild", "Layer Values Mask"), EMixtormatChildCreation::LayerValuesMask },
		{ LOCTEXT("AddNoiseMaskChild", "Noise Mask"), EMixtormatChildCreation::NoiseMask },
		{ LOCTEXT("AddGeneratedChild", "Generated Mask"), EMixtormatChildCreation::GeneratedMask },
		{ LOCTEXT("AddColorIdChild", "Color ID Mask"), EMixtormatChildCreation::ColorIdMask },
		// Listed with the mask producers rather than under Filter, because that is what it is: it
		// emits 0..1 coverage and blends like any other mask. Only what it reads is unusual.
		{ LOCTEXT("AddRandomIdChild", "Random From IDs"), EMixtormatChildCreation::RandomFromIds },
	};
	for (const TPair<FText, EMixtormatChildCreation>& Entry : Entries)
	{
		// The glyph is the node this creates, not the menu it happens to be filed under: Color ID
		// and Random From IDs are listed beside the mask producers because that is how they blend,
		// but both derive from an ID map and so carry the IDs glyph, as their rows will once made.
		Menu.Item(
			Entry.Key,
			Entry.Value == EMixtormatChildCreation::ColorIdMask
				|| Entry.Value == EMixtormatChildCreation::RandomFromIds
				? MixtormatIcons::Ids()
				: Entry.Value == EMixtormatChildCreation::GeneratedMask
					? MixtormatIcons::Generated()
					: MixtormatIcons::Mask(),
			MakeAddMenuAction(Target, [this, Kind = Entry.Value](FMixtormatAddTarget Resolved)
			{
				CreateChild(Resolved, Kind);
			}))
			.UnavailableReason(Reason);
	}
	return Menu.Build();
}


TSharedRef<SWidget> SMixtormat::BuildAddGeneratorsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Target]()
	{
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Generator);
	});
	const auto Add = [this, Target, &Menu, Reason](const FText& Label, const FSlateBrush* Icon, const EMixtormatChildCreation Kind)
	{
		Menu.Item(Label, Icon, MakeAddMenuAction(Target, [this, Kind](FMixtormatAddTarget Resolved)
		{
			CreateChild(Resolved, Kind);
		}, EAddMenuKind::Generator)).UnavailableReason(Reason);
	};
	if (CanAddScopedFlowGenerator(ResolveQuickControlsAddTarget(Target)))
	{
		Add(LOCTEXT("AddScopedFlowChild", "Flow"), MixtormatIcons::Generator(), EMixtormatChildCreation::NoiseFlow);
		return Menu.Build();
	}
	if (!Reason.Get().IsEmpty()) { Menu.Caption(Reason.Get()); }

	Add(LOCTEXT("AddCracksChild", "Cracks"), MixtormatIcons::Generator(), EMixtormatChildCreation::Cracks);
	Add(LOCTEXT("AddRockFormationChild", "Rock Formation"), MixtormatIcons::Generator(), EMixtormatChildCreation::RockFormation);
	Add(LOCTEXT("AddPebblesChild", "Pebbles"), MixtormatIcons::Generator(), EMixtormatChildCreation::Pebbles);
	Add(LOCTEXT("AddRootNetworkChild", "Root Network"), MixtormatIcons::Generator(), EMixtormatChildCreation::RootNetwork);
	Add(LOCTEXT("AddMyceliumChild", "Mycelium"), MixtormatIcons::Generator(), EMixtormatChildCreation::Mycelium);
	Add(LOCTEXT("AddScalesChild", "Scales"), MixtormatIcons::Generator(), EMixtormatChildCreation::Scales);
	Add(LOCTEXT("AddCliffStrataChild", "Cliff Strata"), MixtormatIcons::Generator(), EMixtormatChildCreation::CliffStrata);
	Add(LOCTEXT("AddNoiseChild", "Noise"), MixtormatIcons::Generator(), EMixtormatChildCreation::Noise);
	Add(LOCTEXT("AddScatterCarveChild", "Scatter Carve"), MixtormatIcons::BooleanOps(), EMixtormatChildCreation::ScatterCarve);
	Add(LOCTEXT("AddBooleanOpsChild", "Boolean"), MixtormatIcons::BooleanOps(), EMixtormatChildCreation::BooleanOps);
	// Generator-layer sublayers: ordered with the modules, they rewrite the running signed height.
	Menu.Separator();
	Add(LOCTEXT("AddHeightBlendChild", "Height Blend"), MixtormatIcons::Generator(), EMixtormatChildCreation::HeightBlend);
	Add(LOCTEXT("AddHeightCurveChild", "Height Remap"), MixtormatIcons::Generator(), EMixtormatChildCreation::HeightCurve);
	Add(LOCTEXT("AddHeightColorRampChild", "Color Ramp"), MixtormatIcons::Generator(), EMixtormatChildCreation::HeightColorRamp);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddEffectMenu(const int32 LayerIndex)
{
	return BuildAddEffectTargetMenu(FMixtormatAddTarget::Layer(LayerIndex));
}

TSharedRef<SWidget> SMixtormat::BuildAddEffectTargetMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TAttribute<FText> Reason = TAttribute<FText>::CreateLambda([this, Target]()
	{
		return GetAddMenuUnavailableReason(Target, EAddMenuKind::Effect);
	});
	const auto Add = [this, Target, &Menu, Reason](const FText& Label, const FSlateBrush* Icon,
		TFunction<void(FMixtormatAddTarget)> Action)
	{
		Menu.Item(Label, Icon, MakeAddMenuAction(Target, MoveTemp(Action), EAddMenuKind::Effect))
			.UnavailableReason(Reason);
	};
	const TArray<FMixtormatEffectEntry> Effects = FMixtormatRegistry::GetEffects();
	for (const FMixtormatEffectEntry& Entry : Effects)
	{
		Add(Entry.DisplayName, MixtormatIcons::Effect(), [this, EffectPath = Entry.AssetPath](FMixtormatAddTarget Resolved)
		{
			AddEffectToLayer(Resolved.LayerIndex, EffectPath);
		});
	}

	// Procedural, so they are not discovered from an imported asset set and always available.
	if (!Effects.IsEmpty()) { Menu.Separator(); }
	Add(LOCTEXT("AddCraquelureChild", "Craquelure"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddCraquelureToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddWetStainEffect", "Wet Stain"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddStainToLayer(Resolved.LayerIndex, EMixtormatStainMode::Wet); });
	Add(LOCTEXT("AddDepositStainEffect", "Stain Deposit"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddStainToLayer(Resolved.LayerIndex, EMixtormatStainMode::Deposit); });
	Add(LOCTEXT("AddRunoffEffect", "Runoff"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddRunoffToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddErosionEffect", "Erosion"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddErosionToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddGradeEffect", "Grade"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddGradeToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddBreakupEffect", "Breakup"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddBreakupToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddWornEdgesEffect", "Worn Edges"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { AddWornEdgesToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddFlowWarpEffect", "Flow Warp · Targets Layer"), MixtormatIcons::WarpStructural(),
		[this](FMixtormatAddTarget Resolved) { AddFlowWarpToLayer(Resolved.LayerIndex); });
	Add(LOCTEXT("AddPeelingEffect", "Peeling"), MixtormatIcons::Effect(),
		[this](FMixtormatAddTarget Resolved) { CreateChild(Resolved, EMixtormatChildCreation::Peeling); });
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildEffectContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	bool bCanNestChild = false;
	bool bCanOwnFlowWarp = false;
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& SourceChild = *ResolveChild(LayerIndex, ChildIndex);
		bCanNestChild = CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex);
		bCanOwnFlowWarp = CanOwnFlowWarp(SourceChild);
	}
	// Copy Instance Mask from Wear/Gap/Edge/Pieces used to be hardcoded here per effect type; it is
	// now the generic Copy Output section AddSharedChildMenuItems builds below from
	// GetChildCapabilities, driven by bCopyableAsMask rather than a bWornEdges/bBreakup switch.

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
		Destination.ScopeOwnerChildId = WorkingLayers[LayerIndex].Children[ChildIndex].ChildId;
		Menu.SubMenu(
			LOCTEXT("AddScopedMasksToEffect", "Masks"),
			MixtormatIcons::Mask(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Destination));
	}
	Menu.Item(
		LOCTEXT("AddFlowWarpToEffect", "Add Flow Warp · Targets This Effect"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			AddFlowWarpToLayer(LayerIndex, ChildIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild && bCanOwnFlowWarp));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateEffectChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
			}
			else
			{
				DuplicateLayerChild(LayerIndex, ChildIndex);
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Duplicate));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveEffectChild", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
			}
			else
			{
				RemoveLayerEffect(LayerIndex, ChildIndex);
			}
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedContextMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	// Filters emit data rather than coverage, so there is nothing for a blend mode to mean on
	// one. The random-value mask is a mask and keeps its submenu like every other mask row.
	const EMixtormatLayerChildType RowType =
		WorkingLayers.IsValidIndex(LayerIndex)
			&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
			? ResolveChild(LayerIndex, ChildIndex)->Type
			: EMixtormatLayerChildType::Mask;
	// A generator has no blend mode either, and for a stronger reason than a filter does: a
	// filter at least emits something into the layer, whereas a generator rewrites the height
	// the layer is built from before any blending exists to take part in.
	const bool bGenerator = RowType == EMixtormatLayerChildType::Generator
		|| RowType == EMixtormatLayerChildType::Boolean;
	if (RowType == EMixtormatLayerChildType::Surfacing)
	{
		Menu.Item(LOCTEXT("SurfacingBehaviorsUnavailable", "Behaviors"), MixtormatIcons::WarpStructural(), FSimpleDelegate())
			.Enabled(false).ToolTip(LOCTEXT("SurfacingBehaviorsUnavailableHint", "Surfacing has no Behavior GPU rewrite adapter. Author upstream changes on a Generator instead."));
		Menu.Item(LOCTEXT("SurfacingBlurUnavailable", "Blur"), MixtormatIcons::Filter(), FSimpleDelegate())
			.Enabled(false).ToolTip(LOCTEXT("SurfacingBlurUnavailableHint", "Surfacing-local Blur is not integrated. Use a layer-output Blur instead."));
	}
	const bool bFilter = RowType == EMixtormatLayerChildType::Filter
		|| RowType == EMixtormatLayerChildType::HsvFilter
		|| RowType == EMixtormatLayerChildType::RampId
		|| RowType == EMixtormatLayerChildType::UvFromIds
		|| RowType == EMixtormatLayerChildType::ReliefFromIds
		|| RowType == EMixtormatLayerChildType::BoundaryFromIds
		|| RowType == EMixtormatLayerChildType::PatternId

		|| RowType == EMixtormatLayerChildType::IdGroup
		|| RowType == EMixtormatLayerChildType::OutputReference
		|| RowType == EMixtormatLayerChildType::Surfacing
		|| RowType == EMixtormatLayerChildType::Behavior
		|| bGenerator;

	if (RowType == EMixtormatLayerChildType::HeightColorRamp
		&& WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& Owner = WorkingLayers[LayerIndex].Children[ChildIndex];
		Menu.Item(LOCTEXT("AddBlurToColorRamp", "Blur"), MixtormatIcons::Filter(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, OwnerId = Owner.ChildId]()
			{
				const int32 OwnerIndex = MixtormatLayersPrivate::FindChildById(WorkingLayers[LayerIndex].Children, OwnerId);
				if (OwnerIndex != INDEX_NONE) { AddMaskFilterToLayerChild(LayerIndex, OwnerIndex, EMixtormatLayerChildType::Blur); }
			})).Enabled(CanOwnScopedBlurs(Owner) && !Owner.IsInstance()
				&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex));
	}
	if ((bGenerator || RowType == EMixtormatLayerChildType::Behavior)
		&& WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		const FMixtormatLayerChild& Target = Layer.Children[ChildIndex];
		const FGuid TargetChildId = Target.ChildId;
		// A Behavior is a genuinely generator-owned child, so it is authored in place
		// under the generator rather than as a separate module that targets one later.
		if (!Target.IsInstance() && CanAddScopedChild(Layer.Children, ChildIndex))
		{
			struct FBehaviorEntry
			{
				EMixtormatChildCreation Creation;
				FText Label;
				bool bPushIcon;
			};
			const FBehaviorEntry Entries[] = {
				{EMixtormatChildCreation::BehaviorWarp, LOCTEXT("AddBehaviorWarpToGenerator", "Warp"), false},
				{EMixtormatChildCreation::BehaviorPush, LOCTEXT("AddBehaviorPushToGenerator", "Push"), true},
				{EMixtormatChildCreation::BehaviorCarve, LOCTEXT("AddBehaviorCarveToGenerator", "Sculpt"), true},
				{EMixtormatChildCreation::BehaviorDeform, LOCTEXT("AddBehaviorDeformToGenerator", "Deform"), false},
				{EMixtormatChildCreation::BehaviorFeather, LOCTEXT("AddBehaviorFeatherToGenerator", "Feather"), false}};
			if (Target.Type == EMixtormatLayerChildType::Boolean)
			{
				FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
				Destination.ScopeOwnerChildId = TargetChildId;
				if (CanAddGeneratorModule(Destination))
				{
					struct FOperandEntry { EMixtormatChildCreation Kind; FText Label; };
					const FOperandEntry Options[] = {
						{EMixtormatChildCreation::RockFormation, LOCTEXT("BooleanAddRock", "Add Rock Formation")},
						{EMixtormatChildCreation::Cracks, LOCTEXT("BooleanAddCracks", "Add Cracks")},
						{EMixtormatChildCreation::Pebbles, LOCTEXT("BooleanAddPebbles", "Add Pebbles")},
						{EMixtormatChildCreation::RootNetwork, LOCTEXT("BooleanAddRootNetwork", "Add Root Network")},
						{EMixtormatChildCreation::Moss, LOCTEXT("BooleanAddMoss", "Add Moss")},
						{EMixtormatChildCreation::Mycelium, LOCTEXT("BooleanAddMycelium", "Add Mycelium")},
						{EMixtormatChildCreation::Scales, LOCTEXT("BooleanAddScales", "Add Scales")},
						{EMixtormatChildCreation::CliffStrata, LOCTEXT("BooleanAddCliff", "Add Cliff Strata")},
						{EMixtormatChildCreation::Noise, LOCTEXT("BooleanAddNoise", "Add Noise")},
						{EMixtormatChildCreation::ScatterCarve, LOCTEXT("BooleanAddScatterCarve", "Add Scatter Carve")}};
					for (const FOperandEntry& Entry : Options)
					{
						Menu.Item(Entry.Label, MixtormatIcons::Generator(),
							FSimpleDelegate::CreateLambda([this, Destination, Kind = Entry.Kind]()
							{
								CreateChild(Destination, Kind);
							}));
					}
				}
			}
			if (Target.Type == EMixtormatLayerChildType::Generator
				|| Target.Type == EMixtormatLayerChildType::Boolean)
			{
				FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
				Destination.ScopeOwnerChildId = TargetChildId;
				if (Target.Type == EMixtormatLayerChildType::Generator
					&& CanAddScopedFlowGenerator(Destination))
				{
					Menu.Item(LOCTEXT("AddFlowToGenerator", "Flow"), MixtormatIcons::Generator(),
						FSimpleDelegate::CreateLambda([this, Destination]()
						{
							CreateChild(Destination, EMixtormatChildCreation::NoiseFlow);
						}));
				}
				Menu.Item(LOCTEXT("AddColorRampToGenerator", "Color Ramp"), MixtormatIcons::Generator(),
					FSimpleDelegate::CreateLambda([this, Destination]()
					{
						CreateChild(Destination, EMixtormatChildCreation::HeightColorRamp);
					}));
			}
			Menu.Separator();
			for (const FBehaviorEntry& Entry : Entries)
			{
				if (Target.Type == EMixtormatLayerChildType::Generator
					&& Target.Generator.Type == EMixtormatGeneratorType::Noise
					&& Target.Generator.Noise.NoisePreset == EMixtormatNoisePreset::Flow
					&& Entry.Creation != EMixtormatChildCreation::BehaviorFeather) { continue; }
				FMixtormatLayerChild Prototype;
				ApplyChildCreationDefaults(Prototype, Entry.Creation);
				if (!CanKeepScopedPlacement(Target, Prototype)) { continue; }
				Menu.Item(Entry.Label,
					Entry.Creation == EMixtormatChildCreation::BehaviorFeather
						? MixtormatIcons::Filter()
						: (Entry.bPushIcon ? MixtormatIcons::WarpPush() : MixtormatIcons::WarpStructural()),
					FSimpleDelegate::CreateLambda([this, LayerIndex, TargetChildId, Entry]()
					{
						FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
						Destination.ScopeOwnerChildId = TargetChildId;
						CreateChild(Destination, Entry.Creation);
					}));
			}

			FMixtormatLayerChild BlurPrototype;
			BlurPrototype.Type = EMixtormatLayerChildType::Blur;
			if (CanKeepScopedPlacement(Target, BlurPrototype))
			{
				Menu.Item(LOCTEXT("AddBlurToGenerator", "Blur"), MixtormatIcons::Filter(),
					FSimpleDelegate::CreateLambda([this, LayerIndex, TargetChildId]()
					{
						const int32 OwnerIndex = MixtormatLayersPrivate::FindChildById(
							WorkingLayers[LayerIndex].Children, TargetChildId);
						if (OwnerIndex != INDEX_NONE)
						{
							AddMaskFilterToLayerChild(LayerIndex, OwnerIndex, EMixtormatLayerChildType::Blur);
						}
					}));
			}
		}
		Menu.Separator();
	}

	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayerChild& TargetChild = WorkingLayers[LayerIndex].Children[ChildIndex];
		const bool bCanNestChild = !TargetChild.IsInstance()
			&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex);
		const bool bCanMask = CanOwnScopedMasks(TargetChild);
		const bool bCanFilterIds = TargetChild.Type != EMixtormatLayerChildType::IdGroup
			&& CanOwnIdFilters(TargetChild);
		const bool bCanWarp = CanOwnFlowWarp(TargetChild);
		if (bCanMask || bCanFilterIds || bCanWarp)
		{
			FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
			Destination.ScopeOwnerChildId = TargetChild.ChildId;
			if (TargetChild.Type == EMixtormatLayerChildType::Generator
				|| TargetChild.Type == EMixtormatLayerChildType::Surfacing)
			{
				Menu.SubMenu(LOCTEXT("AddSurfacingChild", "Surfacing"), MixtormatIcons::Surfacing(),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildAddSurfacingMenu, Destination))
					.Enabled(CanAddSurfacingModule(Destination));
			}
			if (bCanFilterIds)
			{
				Menu.SubMenu(
					LOCTEXT("AddScopedIdFiltersToChild", "Filter (From IDs)"),
					MixtormatIcons::Ids(),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFiltersMenu, Destination));
			}
			if (bCanMask)
			{
				Menu.SubMenu(
					LOCTEXT("AddScopedMasksToChild", "Masks"),
					MixtormatIcons::Mask(),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Destination));
			}
			if (bCanWarp)
			{
				Menu.Item(
					LOCTEXT("AddFlowWarpToGeneratedChild", "Add Flow Warp"),
					MixtormatIcons::WarpStructural(),
					FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
					{
						AddFlowWarpToLayer(LayerIndex, ChildIndex);
					}))
					.Enabled(TAttribute<bool>(bCanNestChild));
			}
			Menu.Separator();
		}
	}

	// Copy Instance Mask from Gap used to be hardcoded here for a PatternId row; it is now the
	// generic Copy Output section AddSharedChildMenuItems builds below (Pattern IDs' Gap is
	// bCopyableAsMask but deliberately not bPreviewable -- see MixtormatChildCapabilities.h).

	if (!bFilter)
	{
		Menu.SubMenu(
			LOCTEXT("GeneratedBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildGeneratedBlendModeMenu, LayerIndex, ChildIndex));
		Menu.Separator();
	}

	Menu.Item(
		LOCTEXT("DuplicateGeneratedChild", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
			}
			else
			{
				DuplicateLayerChild(LayerIndex, ChildIndex);
			}
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this]()
		{
			return CanExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Duplicate));
	if (CanGroupSelectedChildrenAsBoolean())
	{
		Menu.Item(
			LOCTEXT("GroupChildrenAsBooleanContext", "Group as Boolean"),
			MixtormatIcons::BooleanOps(),
			FSimpleDelegate::CreateLambda([this]()
			{
				GroupSelectedChildrenAsBoolean();
			})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Group));
	}
	else if (CanGroupSelectedChildrenAsIdGroup())
	{
		Menu.Item(
			LOCTEXT("GroupChildrenAsIdGroupContext", "Group as ID Group"),
			MixtormatIcons::Ids(),
			FSimpleDelegate::CreateLambda([this]()
			{
				GroupSelectedChildrenAsIdGroup();
			})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Group));
	}
	if (RowType == EMixtormatLayerChildType::Boolean
		|| RowType == EMixtormatLayerChildType::IdGroup)
	{
		Menu.Item(
			LOCTEXT("UngroupChildContainerContext", "Ungroup"),
			MixtormatIcons::Folder(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
			{
				SelectWorkingChild(LayerIndex, ChildIndex);
				UngroupSelectedChildContainer();
			}));
	}
	AddIdGroupMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	// One verb for every generated row. This menu serves masks, craquelure, colour ID nodes,
	// generators, behaviors and the height ops, and the labels used to name the row type -- so the
	// same menu said "Remove Generated Mask" on a craquelure row, "Remove Behavior" on a behavior
	// and "Remove ID Group" on an ID group, three ways of saying one action on rows that are
	// already visibly their own type. The row glyph and name say what it is; the menu says what
	// pressing it does.
	Menu.Item(
		LOCTEXT("RemoveGeneratedChild", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
			}
			else
			{
				RemoveGeneratedFromLayer(LayerIndex, ChildIndex);
			}
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();
	return Menu.Build();
}

// Fewer entries than a mask's, because a blur has none of what that menu offers: no source to
// replace, no blend mode, nothing to publish. What it shares is the instancing and the removal.
TSharedRef<SWidget> SMixtormat::BuildBlurContextMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatLayerChild* Child = ResolveChild(LayerIndex, ChildIndex);
	if (Child && CanOwnScopedMasks(*Child))
	{
		FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
		Destination.ScopeOwnerChildId = Child->ChildId;
		Menu.SubMenu(LOCTEXT("AddScopedMasksToChild", "Masks"), MixtormatIcons::Mask(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Destination))
			.Enabled(!Child->IsInstance()
				&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex));
	}
	if (Child && Child->Type == EMixtormatLayerChildType::Curvature)
	{
		Menu.Item(LOCTEXT("AddBlurToCurvatureContext", "Add Blur"), MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
			{
				AddBlurToMask(LayerIndex, ChildIndex);
			})).Enabled(CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex)
				&& !Child->IsInstance());
	}
	Menu.Item(
		LOCTEXT("DuplicateBlurContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
			}
			else
			{
				DuplicateLayerChild(LayerIndex, ChildIndex);
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Duplicate));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveMaskFilterContext", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
			}
			else
			{
				RemoveMaskFromLayer(LayerIndex, ChildIndex);
			}
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskContextMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	// A menu, like every other right-click in the stack. This used to open a 340px gallery titled
	// REPLACE MASK with two buttons under it -- so right-clicking a mask did something entirely
	// unlike right-clicking the effect directly beneath it, and the common actions were below the
	// fold of a picker you had not asked for. Replacing is still here; it is one entry now.
	MixtormatMenu::FBuilder Menu;
	const bool bCanNestChild = WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(MaskIndex)
		&& CanAddScopedChild(WorkingLayers[LayerIndex].Children, MaskIndex);
	Menu.SubMenu(
		LOCTEXT("MaskBlendModeContext", "Blend Mode"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildMaskBlendModeMenu, LayerIndex, MaskIndex));
	const FSoftObjectPath ReplacementPath = SelectedMaskPath;
	const FText ReplacementName = SelectedLibraryMaskName.IsEmpty()
		? LOCTEXT("NoSelectedReplacementMask", "Select Mask from Gallery")
		: SelectedLibraryMaskName;
	Menu.Item(
		FText::Format(LOCTEXT("ReplaceWithSelectedMask", "Replace with {0}"), ReplacementName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex, ReplacementPath]()
		{
			ReplaceMaskInLayer(LayerIndex, MaskIndex, ReplacementPath);
		}))
		.Enabled(TAttribute<bool>(!ReplacementPath.IsNull()));
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(MaskIndex))
	{
		FMixtormatAddTarget Destination = FMixtormatAddTarget::Layer(LayerIndex);
		Destination.ScopeOwnerChildId = WorkingLayers[LayerIndex].Children[MaskIndex].ChildId;
		Menu.SubMenu(
			LOCTEXT("AddScopedMasksToMask", "Masks"),
			MixtormatIcons::Mask(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Destination));
	}
	Menu.Item(
		LOCTEXT("AddBlurToMaskContext", "Add Blur"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddBlurToMask(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Item(
		LOCTEXT("AddCurvatureToMaskContext", "Add Curvature"),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddCurvatureToMask(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Item(
		LOCTEXT("AddFlowWarpToMask", "Add Flow Warp · Targets This Mask"),
		MixtormatIcons::WarpStructural(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			AddFlowWarpToLayer(LayerIndex, MaskIndex);
		}))
		.Enabled(TAttribute<bool>(bCanNestChild));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DuplicateMaskContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
			}
			else
			{
				DuplicateLayerChild(LayerIndex, MaskIndex);
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Duplicate));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, MaskIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveMaskContext", "Remove"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			if (GetSelectedLayerChildIndices().Num() > 1)
			{
				ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
			}
			else
			{
				RemoveMaskFromLayer(LayerIndex, MaskIndex);
			}
		}))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildLayerHeightOpMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	const UEnum* OpEnum = StaticEnum<EMixtormatHeightOp>();
	for (int32 Index = 0; Index < OpEnum->NumEnums(); ++Index)
	{
		if (OpEnum->HasMetaData(TEXT("Hidden"), Index))
		{
			continue;
		}
		const auto Choice = static_cast<EMixtormatHeightOp>(OpEnum->GetValueByIndex(Index));
		Menu.Item(
			OpEnum->GetDisplayNameTextByIndex(Index),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, Choice]()
			{
				if (WorkingLayers.IsValidIndex(LayerIndex))
				{
					WorkingLayers[LayerIndex].HeightBlend.Op = Choice;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, Choice]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].HeightBlend.Op == Choice;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskBlendModeMenu(const int32 LayerIndex, const int32 MaskIndex)
{
	return BuildGeneratedBlendModeMenu(LayerIndex, MaskIndex);
}

TSharedRef<SWidget> SMixtormat::BuildNormalSourceMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	if (WorkingLayers.IsValidIndex(LayerIndex) && !WorkingLayers[LayerIndex].SourceSurface.IsNull())
	{
		Menu.Caption(LOCTEXT("NormalSourceFromLayer", "From this layer"));
		Menu.Item(
			LOCTEXT("UseSurfaceNormal", "Surface Normal"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex]()
			{
				if (!WorkingLayers.IsValidIndex(LayerIndex))
				{
					return;
				}
				WorkingLayers[LayerIndex].NormalSourceType = EMixtormatNormalSourceType::Surface;
				RefreshLayeredPreview();
				RebuildLayerList();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].NormalSourceType == EMixtormatNormalSourceType::Surface;
			}));
		Menu.Separator();
	}

	const TArray<FMixtormatNormalEntry> Normals = FMixtormatRegistry::GetNormals();
	if (!Normals.IsEmpty())
	{
		Menu.Caption(LOCTEXT("NormalSourceLibrary", "Library"));
	}
	for (const FMixtormatNormalEntry& Normal : Normals)
	{
		Menu.Item(
			Normal.DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, Path = Normal.AssetPath]()
			{
				AssignNormalTexture(LayerIndex, Path);
			}));
	}
	if (Normals.IsEmpty())
	{
		Menu.Item(LOCTEXT("NormalsUnavailable", "No standalone normals available"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMaskLibraryContextMenu(const FSoftObjectPath AssetPath)
{
	const bool bIsUserAsset = MixtormatUI::IsUserLibraryAsset(AssetPath);
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("LibraryMaskContextCaption", "Library Mask"))
		.Item(
			LOCTEXT("BrowseLibraryMask", "Show in Content Browser"),
			MixtormatIcons::Folder(),
			FSimpleDelegate::CreateSP(this, &SMixtormat::BrowseLibraryAsset, AssetPath))
		.Separator()
		.Item(
			LOCTEXT("RemoveImportedMask", "Remove Imported Mask…"),
			MixtormatIcons::Trash(),
			FSimpleDelegate::CreateSP(this, &SMixtormat::RemoveImportedMask, AssetPath))
		.Enabled(bIsUserAsset)
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildBoundaryIdSourceMenu()
{
	MixtormatMenu::FBuilder Menu;
	const FMixtormatChildAddress Dest = GetSelectedChildAddress();
	const int32 DestIndex = ResolveChildIndexAt(Dest);
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	const auto Assign = [this, Dest](const FMixtormatOutputReference& Reference)
	{
		if (FMixtormatLayerChild* Child = ResolveChildAt(Dest))
		{
			if (Child->Type == EMixtormatLayerChildType::BoundaryFromIds && !Child->IsInstance())
			{
				Child->BoundaryId.RegionIdsSource = Reference;
				RefreshLayeredPreview();
				RebuildLayerList();
			}
		}
	};
	Menu.Item(LOCTEXT("BoundaryIdNearestSource", "Nearest preceding Region IDs"), nullptr,
		FSimpleDelegate::CreateLambda([Assign]() { Assign(FMixtormatOutputReference{}); }));
	const auto AddSources = [this, &Menu, &Scope, &Dest, DestIndex, Assign](
		const FGuid OwnerId, const FText& OwnerName, const TArray<FMixtormatLayerChild>& Children,
		const bool bOwnerEnabled)
	{
		for (const FMixtormatLayerChild& Source : Children)
		{
			const FMixtormatChildCapabilities Caps = GetChildCapabilities(Source);
			for (const FMixtormatPublishedOutputDesc& Output : Caps.Outputs)
			{
				if (!Output.bCopyableAsField || Output.FieldKind != EMixtormatPublishedFieldKind::RegionIds)
				{
					continue;
				}
				FMixtormatOutputReference Reference;
				Reference.SourceLayerId = OwnerId;
				Reference.SourceChildId = Source.ChildId;
				Reference.OutputName = Output.Name;
				Reference.Kind = EMixtormatPublishedFieldKind::RegionIds;
				FMixtormatLayerChild Candidate;
				Candidate.Type = EMixtormatLayerChildType::BoundaryFromIds;
				Candidate.BoundaryId.RegionIdsSource = Reference;
				if (const FMixtormatLayerChild* Selected = ResolveChildAt(Dest))
				{
					Candidate.ScopeOwnerChildId = Selected->ScopeOwnerChildId;
				}
				const int32 SourceIndex = FindChildById(Children, Source.ChildId);
				const int32 SourceLayerIndex = WorkingLayers.IndexOfByPredicate(
					[OwnerId](const FMixtormatLayer& Layer) { return Layer.LayerId == OwnerId; });
				const bool bSourceEnabled = SourceLayerIndex != INDEX_NONE
					? IsLayerChildEnabled(SourceLayerIndex, SourceIndex)
					: IsGroupChildEnabled(Source);
				const bool bAvailable = bOwnerEnabled && bSourceEnabled && DestIndex != INDEX_NONE
					&& CanReadPublishedOutputAt(Scope, Candidate, Dest.OwnerId, DestIndex);
				Menu.Item(FText::Format(LOCTEXT("BoundaryIdSourceEntry", "{0} / {1} / {2}"),
					OwnerName, GetLayerChildName(Source), Output.Label), nullptr,
					FSimpleDelegate::CreateLambda([Assign, Reference]() { Assign(Reference); }))
					.Enabled(TAttribute<bool>(bAvailable));
			}
		}
	};
	for (const FMixtormatLayer& Layer : WorkingLayers)
	{
		AddSources(Layer.LayerId, Layer.DisplayName, Layer.Children, Layer.bEnabled);
	}
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		AddSources(Group.GroupId, Group.DisplayName, Group.Children, Group.bEnabled);
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGeneratedBlendModeMenu(
	const int32 LayerIndex,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	// Capture ownership once: menu callbacks must not follow later selection or index changes.
	const FMixtormatChildAddress Address = LayerIndex == INDEX_NONE
		? MakeGroupChildAddress(SelectedGroupId, ChildIndex)
		: MakeChildAddress(LayerIndex, ChildIndex);
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay,
		EMixtormatMaskBlendMode::Difference,
		EMixtormatMaskBlendMode::Exclusion,
		EMixtormatMaskBlendMode::Divide
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Address, Mode]()
			{
				FMixtormatLayerChild* Child = ResolveChildAt(Address);
				EMixtormatMaskBlendMode* BlendMode = GetChildMaskBlendMode(Child);
				if (!BlendMode || Child->IsInstance() || *BlendMode == Mode) { return; }
				LastHistoryRecordTime = 0.0;
				*BlendMode = Mode;
				RefreshLayeredPreview();
				LastHistoryRecordTime = 0.0;
				RebuildLayerList();
			}))
			.Enabled(TAttribute<bool>::CreateLambda([this, Address]()
			{
				FMixtormatLayerChild* Child = ResolveChildAt(Address);
				return GetChildMaskBlendMode(Child) && !Child->IsInstance();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Address, Mode]()
			{
				const EMixtormatMaskBlendMode* BlendMode = GetChildMaskBlendMode(ResolveChildAt(Address));
				return BlendMode && *BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
