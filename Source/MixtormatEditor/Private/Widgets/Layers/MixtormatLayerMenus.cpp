// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatThemeStore.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

namespace
{
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

TSharedRef<SWidget> SMixtormat::BuildAddGeneratorLayerMenu()
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatGeneratorType> Entries[] = {
		{LOCTEXT("AddGeneratorLayerStrata", "Strata"), EMixtormatGeneratorType::StrataCarver},
		{LOCTEXT("AddGeneratorLayerCracks", "Cracks"), EMixtormatGeneratorType::Cracks},
		{LOCTEXT("AddGeneratorLayerRock", "Rock Formation"), EMixtormatGeneratorType::RockFormation},
		{LOCTEXT("AddGeneratorLayerPebbles", "Pebbles"), EMixtormatGeneratorType::Pebbles},
		{LOCTEXT("AddGeneratorLayerCliffStrata", "Cliff Strata"), EMixtormatGeneratorType::CliffStrata},
	};
	for (const auto& Entry : Entries)
	{
		Menu.Item(Entry.Key, MixtormatIcons::Generator(),
			FSimpleDelegate::CreateLambda([this, Type = Entry.Value]() { AddGeneratorLayer(Type); }));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildQuickControlsActions()
{
	return SNew(SComboButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
		.OnMenuOpenChanged_Lambda([this](bool bOpen) { bQuickControlsActionMenuOpen = bOpen; })
		.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
		{
			MixtormatMenu::FBuilder Menu;
			const bool bLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
			Menu.Item(LOCTEXT("QuickAddFill", "Fill Layer"), MixtormatIcons::LayerFill(),
				FSimpleDelegate::CreateLambda([this]()
				{
					if (!bHasWorkingMaterial)
					{
						StartNewMaterialWith(EMixtormatLayerType::Fill);
						return;
					}
					// AddWorkingLayer appends. Insert once here, as the positioned surface drop does,
					// so height references are not lost by an append followed by a reorder.
					const int32 Slot = WorkingLayers.IsValidIndex(SelectedLayerIndex)
						? SelectedLayerIndex + 1 : WorkingLayers.Num();
					FMixtormatLayer Layer;
					InitializeNewLayer(Layer, EMixtormatLayerType::Fill, WorkingLayers.Num() + 1);
					WorkingLayers.Insert(MoveTemp(Layer), Slot);
					MixtormatUI::RemapHeightReferencesAfterInsert(WorkingLayers, Slot);
					WorkingLayers[Slot].GroupId = ResolveGroupMembershipAt(Slot);
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
				}));
			Menu.Item(LOCTEXT("QuickAddMaterial", "Material"), MixtormatIcons::LayerMaterial(),
				FSimpleDelegate::CreateLambda([this]() { AddLayerOrStartMaterial(EMixtormatLayerType::Material); }))
				.Enabled(TAttribute<bool>::CreateLambda([this]() { return !SelectedSurfacePath.IsNull(); }));
			Menu.SubMenu(LOCTEXT("QuickAddGenerator", "Generator"), MixtormatIcons::Generator(),
				FOnGetContent::CreateSP(this, &SMixtormat::BuildAddGeneratorLayerMenu));
			Menu.SubMenu(LOCTEXT("QuickAddEffects", "Effects"), MixtormatIcons::Effect(),
				FOnGetContent::CreateLambda([this]() { return BuildAddEffectMenu(SelectedLayerIndex); }))
				.Enabled(bLayer);
			Menu.SubMenu(LOCTEXT("QuickAddIds", "IDs"), MixtormatIcons::Ids(),
				FOnGetContent::CreateLambda([this]() { return BuildAddIdsMenu(FMixtormatAddTarget::Layer(SelectedLayerIndex)); }))
				.Enabled(bLayer);
			Menu.SubMenu(LOCTEXT("QuickGeneratorSubmodules", "Generator Submodules"), MixtormatIcons::Generator(),
				FOnGetContent::CreateLambda([this]() { return BuildAddGeneratorsMenu(FMixtormatAddTarget::Layer(SelectedLayerIndex)); }))
				.Enabled(CanAddGeneratorModule(FMixtormatAddTarget::Layer(SelectedLayerIndex)));
			Menu.SubMenu(LOCTEXT("QuickMasks", "Masks"), MixtormatIcons::Mask(),
				FOnGetContent::CreateLambda([this]() -> TSharedRef<SWidget>
				{
					const FMixtormatChildAddress Address = GetSelectedChildAddress();
					const FMixtormatLayerChild* Child = ResolveChildAt(Address);
					if (!Child || Child->Type != EMixtormatLayerChildType::Mask)
					{
						return BuildAddMasksMenu(FMixtormatAddTarget::Layer(SelectedLayerIndex));
					}
					MixtormatMenu::FBuilder Masks;
					const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
					const int32 Index = ResolveChildIndexAt(Address);
					const bool bCanNest = Children && CanAddScopedChild(*Children, Index) && !Child->IsInstance();
					for (const EMixtormatLayerChildType Type : {EMixtormatLayerChildType::Blur, EMixtormatLayerChildType::Curvature})
					{
						Masks.Item(Type == EMixtormatLayerChildType::Blur
							? LOCTEXT("QuickMaskBlur", "Blur") : LOCTEXT("QuickMaskCurvature", "Curvature"), MixtormatIcons::Mask(),
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
							})).Enabled(bCanNest);
					}
					return Masks.Build();
				})).Enabled(bLayer || ResolveChildAt(GetSelectedChildAddress()) != nullptr);
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const FMixtormatLayerChild* Child = ResolveChildAt(Address);
			if (Child && !GetCopyableOutputs(GetChildCapabilities(*Child)).IsEmpty())
			{
				Menu.SubMenu(LOCTEXT("QuickOutputCopy", "Output Copy"), MixtormatIcons::Duplicate(),
					FOnGetContent::CreateLambda([this]() { return BuildCopyChildOutputMenu(GetSelectedChildAddress()); }));
			}
			if (Child && CanOwnScopedMasks(*Child))
			{
				Menu.SubMenu(LOCTEXT("QuickGates", "Gates"), MixtormatIcons::Mask(),
					FOnGetContent::CreateLambda([this]() -> TSharedRef<SWidget>
					{
						MixtormatMenu::FBuilder Gates;
						const FMixtormatChildAddress Owner = GetSelectedChildAddress();
						const FMixtormatLayerChild* Selected = ResolveChildAt(Owner);
						const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Owner);
						const int32 Index = ResolveChildIndexAt(Owner);
						Gates.Item(LOCTEXT("QuickGalleryGate", "Gallery Mask"), MixtormatIcons::Mask(),
							FSimpleDelegate::CreateLambda([this, Owner]()
							{
								const int32 LayerIndex = WorkingLayers.IndexOfByPredicate([&Owner](const FMixtormatLayer& Layer) { return Layer.LayerId == Owner.OwnerId; });
								AssignScopedMaskToChild(LayerIndex, ResolveChildIndexAt(Owner), SelectedMaskPath);
							})).Enabled(Owner.OwnerType == EMixtormatChildOwnerType::Layer
								&& Selected && !Selected->IsInstance() && CanOwnScopedMasks(*Selected)
								&& Children && CanAddScopedChild(*Children, Index) && !SelectedMaskPath.IsNull());
						Gates.Item(LOCTEXT("QuickPasteGate", "Paste Gating Mask"), MixtormatIcons::Mask(),
							FSimpleDelegate::CreateLambda([this, Owner]() { PasteAsGatingMask(Owner); }))
							.Enabled(TAttribute<bool>::CreateLambda([this, Owner]() { return CanPasteAsGatingMask(Owner); }));
						return Gates.Build();
					}));
			}
			return Menu.Build();
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text(LOCTEXT("QuickActions", "ACTIONS"))
		];
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
	Menu.SubMenu(LOCTEXT("ColumnAddGeneratorLayer", "Generator Layer"), MixtormatIcons::Generator(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddGeneratorLayerMenu));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMoveChildToLayerMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("MoveChildToLayerCaption", "Move To"));
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& (IsMaskFilter(*ResolveChild(LayerIndex, ChildIndex))
			|| IsGeneratorFlow(*ResolveChild(LayerIndex, ChildIndex))))
	{
		Menu.Item(
			IsGeneratorFlow(*ResolveChild(LayerIndex, ChildIndex))
				? LOCTEXT("MoveFlowWithGenerator", "Move the owning generator instead")
				: LOCTEXT("MoveMaskFilterWithMask", "Move the owning mask instead"),
			nullptr,
			FSimpleDelegate()).Enabled(false);
		return Menu.Build();
	}
	for (int32 DestIndex = 0; DestIndex < WorkingLayers.Num(); ++DestIndex)
	{
		if (DestIndex == LayerIndex)
		{
			continue;
		}
		Menu.Item(
			WorkingLayers[DestIndex].DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, DestIndex]()
			{
				MoveChildToLayer(LayerIndex, ChildIndex, DestIndex);
			})).Enabled(!WorkingLayers.IsValidIndex(LayerIndex)
				|| !WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
				|| (ResolveChild(LayerIndex, ChildIndex)->Type != EMixtormatLayerChildType::HeightPush
					&& ResolveChild(LayerIndex, ChildIndex)->Type != EMixtormatLayerChildType::StructuralWarp));
	}
	if (Menu.IsEmpty())
	{
		Menu.Item(LOCTEXT("MoveChildNoLayers", "No other layer"), nullptr, FSimpleDelegate())
			.Enabled(false);
	}
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
				.Enabled(!IsGeneratorFlow(Candidate) || (ScopeOwner && CanOwnGeneratorFlow(*ScopeOwner)));
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
				.Enabled(!IsGeneratorFlow(Candidate) || (ScopeOwner && CanOwnGeneratorFlow(*ScopeOwner)));
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
	MixtormatMenu::FBuilder Menu;
	if (const FMixtormatLayerChild* Child = ResolveChildAt(Address))
	{
		for (const FMixtormatPublishedOutputDesc& Output : GetCopyableOutputs(GetChildCapabilities(*Child)))
		{
			// The submenu already says "Outputs", so each row names the output itself rather than
			// repeating the verb: "Region IDs", "Gap", "Color" -- not "Copy IDs", "Copy Gate · Gap".
			Menu.Item(Output.Label,
				GetPublishedOutputIcon(Output),
				FSimpleDelegate::CreateLambda([this, Address, OutputName = Output.Name]()
				{
					CopyChildOutput(Address, OutputName);
				}))
				.Enabled(TAttribute<bool>::CreateLambda([this, Address, OutputName = Output.Name]()
				{
					return CanCopyChildOutput(Address, OutputName);
				}));
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

	// Generator-flow tools only exist for a generator that can carry them. On anything else the
	// three rows were permanently disabled -- clutter no state could ever enable -- so they are
	// omitted rather than shown greyed. A generator that can own them keeps them, disabled only
	// while the current state (a full scope, say) blocks the add.
	if (Child && CanOwnGeneratorFlow(*Child))
	{
		AddGeneratorFlowMenuItems(Menu, Address);
		Menu.Separator();
	}
	Menu.Item(
		LOCTEXT("CopyChildContext", "Copy"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChild(Address, false);
		}));
	Menu.Item(
		LOCTEXT("CopyChildAsInstanceContext", "Copy as Instance"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			CopyChild(Address, true);
		}));
	if (Child)
	{
		// One entry point for published data. The rows used to be repeated here as flattened
		// "Copy Gate · X" items as well, which said the same thing twice and buried the structural
		// actions; the submenu is the single, discoverable home for them now. Omitted entirely when
		// there is nothing to publish, rather than shown disabled and spending a row on nothing.
		if (!GetCopyableOutputs(GetChildCapabilities(*Child)).IsEmpty())
		{
			Menu.SubMenu(LOCTEXT("CopyChildOutputContext", "Outputs"), nullptr,
				FOnGetContent::CreateSP(this, &SMixtormat::BuildCopyChildOutputMenu, Address));
		}
	}
	Menu.Item(
		LOCTEXT("PasteChildContext", "Paste"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, Address]()
		{
			PasteChild(Address, ResolveChildIndexAt(Address));
		}))
		.Enabled(TAttribute<bool>::CreateLambda([this, Address]()
		{
			return CanPasteChild(Address, ResolveChildIndexAt(Address));
		}));
	// Only offered where it can land: a copied mask on a row that can own scoped masks.
	if (CanPasteAsGatingMask(Address))
	{
		Menu.Item(
			LOCTEXT("PasteAsGatingMaskContext", "Paste as Gating Mask"),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda([this, Address]() { PasteAsGatingMask(Address); }));
	}
	if (Address.OwnerType == EMixtormatChildOwnerType::Layer)
	{
		const int32 LayerIndex = WorkingLayers.IndexOfByPredicate(
			[&Address](const FMixtormatLayer& Layer) { return Layer.LayerId == Address.OwnerId; });
		const int32 ChildIndex = ResolveChildIndexAt(Address);
		Menu.SubMenu(
			LOCTEXT("MoveChildToLayerContext", "Move to Layer..."),
			nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildMoveChildToLayerMenu, LayerIndex, ChildIndex));
	}

	if (Child && Child->Type == EMixtormatLayerChildType::OutputReference && !bInstance)
	{
		Menu.Separator();
		Menu.Item(LOCTEXT("GoToOutputSourceContext", "Go to Source"),
			MixtormatIcons::ArrowUp(),
			FSimpleDelegate::CreateLambda([this, Address]() { GoToChildInstanceSource(Address); }))
			.Enabled(MixtormatParameterBinding::FindChild(FMixtormatBindingScope{WorkingLayers, WorkingLayerGroups},
				Child->OutputReference.SourceLayerId, Child->OutputReference.SourceChildId) != nullptr);
		Menu.SubMenu(LOCTEXT("ReplaceOutputSourceContext", "Source"), nullptr,
			FOnGetContent::CreateSP(this, &SMixtormat::BuildOutputReferenceSourceMenu, Address))
			.Enabled(!bInstance);
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
	// ID children are movable; scoped mask/flow tools still travel with their owner.
	const bool bCanLeaveGroup = Group
		&& Group->Children.IsValidIndex(ChildIndex)
		&& (!Group->Children[ChildIndex].ScopeOwnerChildId.IsValid()
			|| IsIdGroupChild(Group->Children[ChildIndex]))
		&& !IsMaskFilter(Group->Children[ChildIndex]);
	if (Group && Group->Children.IsValidIndex(ChildIndex)
		&& Group->Children[ChildIndex].Type == EMixtormatLayerChildType::Mask)
	{
		// INDEX_NONE for the layer lane throughout: ResolveChild reaches a group's shared stack
		// only that way, and the row selected this child before opening the menu, so
		// SelectedGroupId already names the group these resolve against.
		// Typed, not INDEX_NONE inline: the delegate payload would deduce the literal's own type
		// rather than the int32 the method takes.
		const int32 NoLayerLane = INDEX_NONE;
		Menu.SubMenu(
			LOCTEXT("MaskBlendModeContext", "Blend Mode"),
			nullptr,
			FOnGetContent::CreateSP(
				this, &SMixtormat::BuildMaskBlendModeMenu, NoLayerLane, ChildIndex));
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
	if (bCanLeaveGroup && !WorkingLayers.IsEmpty())
	{
		// "Move to Layer", not "Unshare": the destination has to be named, and there is no
		// sensible default for it -- the child belonged to every member equally.
		Menu.SubMenu(
			LOCTEXT("MoveGroupChildToLayerContext", "Move to Layer..."),
			nullptr,
			FOnGetContent::CreateSP(
				this, &SMixtormat::BuildMoveGroupChildToLayerMenu, GroupId, ChildIndex));
		Menu.Separator();
	}
	// Copy / Copy as Instance / Copy Output / Paste, and (for an instance) Go to Source / Break
	// Instance / Replace Source / Copy Instance Reference -- the same rows a layer child's menus
	// build via AddSharedChildMenuItems, driven by the same address-based clipboard rather than a
	// second, group-specific copy of this logic.
	AddIdGroupMenuItems(Menu, MakeGroupChildAddress(GroupId, ChildIndex));
	AddSharedChildMenuItems(Menu, MakeGroupChildAddress(GroupId, ChildIndex));
	Menu.Item(
		LOCTEXT("RemoveGroupChildContext", "Delete"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex]()
		{
			RemoveGroupChild(GroupId, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildMoveGroupChildToLayerMenu(
	const FGuid GroupId,
	const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("MoveGroupChildToLayerCaption", "Move To"));
	// Every layer, including the group's own members: moving a shared child onto one member is
	// exactly the "this one only" case, and refusing it there would be the surprising answer.
	for (int32 DestIndex = 0; DestIndex < WorkingLayers.Num(); ++DestIndex)
	{
		Menu.Item(
			WorkingLayers[DestIndex].DisplayName,
			nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId, ChildIndex, DestIndex]()
			{
				// INDEX_NONE: no row was aimed at, so it appends -- the same thing the menu
				// version of the layer-to-layer move does.
				MoveGroupChildToLayer(GroupId, ChildIndex, DestIndex, INDEX_NONE);
			}));
	}
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
			.ToolTipText(ToolTip)
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
	Menu.Item(
		LOCTEXT("RenameGroupContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]()
		{
			SelectLayerGroup(GroupId);
			BeginRenameSelection();
		}))
		.Shortcut(LOCTEXT("RenameGroupShortcut", "F2"));
	Menu.SubMenu(
		LOCTEXT("GroupAccentContext", "Colour"),
		nullptr,
		FOnGetContent::CreateSP(this, &SMixtormat::BuildGroupAccentMenu, GroupId));
	Menu.Separator();
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
	// Ungroup, not Delete: it releases the layers and keeps every one of them, which is the only
	// group operation that cannot lose work.
	Menu.Item(
		LOCTEXT("UngroupGroupContext", "Ungroup"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));

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
		}));
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
		}));

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
		LOCTEXT("RenameLayerContext", "Rename"),
		nullptr,
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			SelectWorkingLayer(LayerIndex);
			BeginRenameSelection();
		}))
		.Shortcut(LOCTEXT("RenameShortcut", "F2"));
	Menu.Item(
		LOCTEXT("DuplicateLayerContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]() { DuplicateSelectedLayer(); }));

	// Acts on the multi-selection, not on the row the menu opened over, so shift-picking a run of
	// layers and right-clicking any of them does the same thing as the toolbar button.
	// The menu is rebuilt on every open, so the count in the label is the count at open time.
	const int32 SelectionCount = GetSelectedLayerIndices().Num();
	Menu.Item(
		SelectionCount > 1
			? FText::Format(LOCTEXT("CreateGroupFromManyContext", "Group {0} Layers"),
				FText::AsNumber(SelectionCount))
			: LOCTEXT("CreateGroupContext", "Create Group"),
		MixtormatIcons::Folder(),
		FSimpleDelegate::CreateLambda([this]() { CreateGroupFromSelection(); }))
		.Enabled(TAttribute<bool>::CreateLambda([this]() { return CanCreateGroup(); }));
	if (WorkingLayers.IsValidIndex(LayerIndex) && WorkingLayers[LayerIndex].GroupId.IsValid())
	{
		const FGuid GroupId = WorkingLayers[LayerIndex].GroupId;
		Menu.Item(
			LOCTEXT("UngroupLayerContext", "Ungroup"),
			nullptr,
			FSimpleDelegate::CreateLambda([this, GroupId]() { UngroupLayerGroup(GroupId); }));
	}

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
		LOCTEXT("DeleteLayerContext", "Delete"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this]() { DeleteSelectedLayer(); }))
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
		MixtormatIcons::Ids(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddFiltersMenu, Target));
	Menu.SubMenu(
		LOCTEXT("AddMasksChild", "Masks"),
		MixtormatIcons::Mask(),
		FOnGetContent::CreateSP(this, &SMixtormat::BuildAddMasksMenu, Target));
	// Its own category beside Effect and Filter, not inside either. What separates it is *when*
	// it runs: an effect filters the layer after it has composited, a generator rewrites the
	// height the layer composites from. Filing it under Effect would be the first step toward
	// implementing it as one.
	if (CanAddGeneratorModule(Target))
	{
		Menu.SubMenu(
			LOCTEXT("AddGeneratorChild", "Generators"),
			MixtormatIcons::Generator(),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildAddGeneratorsMenu, Target));
	}
}

TSharedRef<SWidget> SMixtormat::BuildAddIdsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		{ LOCTEXT("AddPatternIdChild", "Pattern IDs"), EMixtormatChildCreation::PatternIds },
		{ LOCTEXT("AddSurfaceIdsChild", "Surface IDs"), EMixtormatChildCreation::SurfaceIds },

		{ LOCTEXT("AddIdGroupChild", "ID Group"), EMixtormatChildCreation::IdGroup },
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
	const bool bEnabled = CanCreateChild(Target);

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
		FSimpleDelegate::CreateLambda([this, Target, MaskPath]()
		{
			AddTextureMask(Target, MaskPath);
		}))
		.Enabled(TAttribute<bool>(bEnabled && !MaskPath.IsNull()));

	const TPair<FText, EMixtormatChildCreation> Entries[] = {
		// Beside the other mask producers rather than under the asset picker above, because it
		// needs no asset: it reads the layer it is added to.
		{ LOCTEXT("AddLayerValuesChild", "Layer Values Mask"), EMixtormatChildCreation::LayerValuesMask },
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
			FSimpleDelegate::CreateLambda([this, Target, Kind = Entry.Value]()
			{
				CreateChild(Target, Kind);
			}))
			.Enabled(TAttribute<bool>(bEnabled));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddGeneratorsMenu(const FMixtormatAddTarget Target)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("AddStrataCarverChild", "Strata Carver"),
		MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			CreateChild(Target, EMixtormatChildCreation::StrataCarver);
		}))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(
		LOCTEXT("AddCracksChild", "Cracks"),
		MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			CreateChild(Target, EMixtormatChildCreation::Cracks);
		}))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(
		LOCTEXT("AddRockFormationChild", "Rock Formation"),
		MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			CreateChild(Target, EMixtormatChildCreation::RockFormation);
		}))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(
		LOCTEXT("AddPebblesChild", "Pebbles"),
		MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target]()
		{
			CreateChild(Target, EMixtormatChildCreation::Pebbles);
		}))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(LOCTEXT("AddCliffStrataChild", "Cliff Strata"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::CliffStrata); }))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	// Generator-layer sublayers: ordered with the modules, they rewrite the running signed height.
	Menu.Separator();
	Menu.Item(LOCTEXT("AddHeightPushChild", "Height Push"), MixtormatIcons::Generator(),
			FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::HeightPush); }))
			.Enabled(TAttribute<bool>(CanCreateChild(Target) && !Target.IsGroup()
						&& !Target.ScopeOwnerChildId.IsValid()));
	Menu.Item(LOCTEXT("AddStructuralWarpChild", "Structural Warp"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::StructuralWarp); }))
		.Enabled(TAttribute<bool>(CanCreateChild(Target) && CanAddGeneratorModule(Target)));
	Menu.Item(LOCTEXT("AddHeightBlendChild", "Height Blend"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::HeightBlend); }))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(LOCTEXT("AddHeightCurveChild", "Height Remap"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::HeightCurve); }))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	Menu.Item(LOCTEXT("AddHeightColorRampChild", "Color Ramp"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this, Target](){ CreateChild(Target, EMixtormatChildCreation::HeightColorRamp); }))
		.Enabled(TAttribute<bool>(CanCreateChild(Target)));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildAddEffectMenu(const int32 LayerIndex)
{
	MixtormatMenu::FBuilder Menu;
	const TArray<FMixtormatEffectEntry> Effects = FMixtormatRegistry::GetEffects();
	for (const FMixtormatEffectEntry& Entry : Effects)
	{
		Menu.Item(
			Entry.DisplayName,
			MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, LayerIndex, EffectPath = Entry.AssetPath]()
			{
				AddEffectToLayer(LayerIndex, EffectPath);
			}));
	}

	// Procedural, so they are not discovered from an imported asset set and always available.
	if (!Effects.IsEmpty())
	{
		Menu.Separator();
	}
	Menu.Item(
		LOCTEXT("AddCraquelureChild", "Craquelure"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddCraquelureToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddWetStainEffect", "Wet Stain"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			AddStainToLayer(LayerIndex, EMixtormatStainMode::Wet);
		}));
	Menu.Item(
		LOCTEXT("AddDepositStainEffect", "Stain Deposit"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			AddStainToLayer(LayerIndex, EMixtormatStainMode::Deposit);
		}));
	Menu.Item(
		LOCTEXT("AddRunoffEffect", "Runoff"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddRunoffToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddErosionEffect", "Erosion"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddErosionToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddGradeEffect", "Grade"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddGradeToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddBreakupEffect", "Breakup"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddBreakupToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddWornEdgesEffect", "Worn Edges"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddWornEdgesToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddFlowWarpEffect", "Flow Warp · Targets Layer"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddFlowWarpToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddLayerBlurEffect", "Layer Blur"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]() { AddLayerBlurToLayer(LayerIndex); }));
	Menu.Item(
		LOCTEXT("AddPeelingEffect", "Peeling"),
		MixtormatIcons::Effect(),
		FSimpleDelegate::CreateLambda([this, LayerIndex]()
		{
			CreateChild(FMixtormatAddTarget::Layer(LayerIndex), EMixtormatChildCreation::Peeling);
		}));

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

	const FSoftObjectPath SelectedEffectMaskPath = SelectedMaskPath;
	const FText SelectedEffectMaskName = SelectedLibraryMaskName.IsEmpty()
		? LOCTEXT("NoSelectedEffectMask", "Select Mask from Gallery")
		: SelectedLibraryMaskName;
	Menu.Item(
		FText::Format(LOCTEXT("AddSelectedMaskToEffect", "Add Gating Mask · {0}"), SelectedEffectMaskName),
		MixtormatIcons::Mask(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, SelectedEffectMaskPath]()
		{
			AssignScopedMaskToChild(LayerIndex, ChildIndex, SelectedEffectMaskPath);
		}))
		.Enabled(TAttribute<bool>(!SelectedEffectMaskPath.IsNull() && bCanNestChild));
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
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveEffectChild", "Remove Effect"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveLayerEffect(LayerIndex, ChildIndex);
		}))
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
	const bool bGenerator = RowType == EMixtormatLayerChildType::Generator;
	const bool bFilter = RowType == EMixtormatLayerChildType::Filter
		|| RowType == EMixtormatLayerChildType::HsvFilter
		|| RowType == EMixtormatLayerChildType::RampId
		|| RowType == EMixtormatLayerChildType::UvFromIds
		|| RowType == EMixtormatLayerChildType::ReliefFromIds
		|| RowType == EMixtormatLayerChildType::BoundaryFromIds
		|| RowType == EMixtormatLayerChildType::PatternId

		|| RowType == EMixtormatLayerChildType::IdGroup
		|| RowType == EMixtormatLayerChildType::OutputReference
		|| RowType == EMixtormatLayerChildType::HeightPush
		|| RowType == EMixtormatLayerChildType::StructuralWarp
		|| bGenerator;

	if (bGenerator && WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		const FMixtormatLayer& Layer = WorkingLayers[LayerIndex];
		const FMixtormatLayerChild& Target = Layer.Children[ChildIndex];
		const auto AddStructuralAction = [this, &Menu, TargetLayerId = Layer.LayerId, TargetChildId = Target.ChildId](
			const EMixtormatLayerChildType ModuleType, const FText& Label)
		{
			TArray<FMixtormatLayer> ProposedLayers;
			int32 ProposedLayerIndex = INDEX_NONE;
			int32 InsertIndex = INDEX_NONE;
			FText Reason;
			const bool bAvailable = PrepareStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType,
				ProposedLayers, ProposedLayerIndex, InsertIndex, Reason);
			const FText EntryLabel = bAvailable ? Label : FText::Format(
				LOCTEXT("StructuralCreationDisabledLabel", "{0} — {1}"), Label, Reason);
			Menu.Item(EntryLabel, MixtormatIcons::Generator(),
				FSimpleDelegate::CreateLambda([this, TargetLayerId, TargetChildId, ModuleType]()
				{
					CreateStructuralModuleForTarget(TargetLayerId, TargetChildId, ModuleType);
				})).Enabled(bAvailable);
		};
		if (Target.Type == EMixtormatLayerChildType::Generator
			&& Target.Generator.Type == EMixtormatGeneratorType::StrataCarver)
		{
			AddStructuralAction(EMixtormatLayerChildType::HeightPush,
				LOCTEXT("AddHeightPushForTarget", "Add Height Push"));
		}
		AddStructuralAction(EMixtormatLayerChildType::StructuralWarp,
			LOCTEXT("AddStructuralWarpForTarget", "Add Structural Warp"));
		Menu.Separator();
	}

	const bool bCanOwnScopedMask = WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex)
		&& CanOwnScopedMasks(WorkingLayers[LayerIndex].Children[ChildIndex]);
	if (bCanOwnScopedMask)
	{
		const bool bCanNestChild = CanAddScopedChild(WorkingLayers[LayerIndex].Children, ChildIndex);
		const FSoftObjectPath SelectedGeneratorMaskPath = SelectedMaskPath;
		const FText SelectedGeneratorMaskName = SelectedLibraryMaskName.IsEmpty()
			? LOCTEXT("NoSelectedGeneratorMask", "Select Mask from Gallery")
			: SelectedLibraryMaskName;
		Menu.Item(
			FText::Format(
				LOCTEXT("AddSelectedMaskToGenerator", "Add Gating Mask · {0}"),
				SelectedGeneratorMaskName),
			MixtormatIcons::Mask(),
			FSimpleDelegate::CreateLambda(
				[this, LayerIndex, ChildIndex, SelectedGeneratorMaskPath]()
			{
				AssignScopedMaskToChild(LayerIndex, ChildIndex, SelectedGeneratorMaskPath);
			}))
			.Enabled(TAttribute<bool>(!SelectedGeneratorMaskPath.IsNull() && bCanNestChild));
		Menu.Separator();
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
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddIdGroupMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	// Named after the row it is on. This menu serves generated masks, craquelure and colour id
	// nodes, and "Remove Generated Mask" on a craquelure row reads like the wrong entry. Resolved
	// here rather than bound, because the menu is rebuilt on every right-click.
	FText RemoveLabel = LOCTEXT("RemoveGeneratedChild", "Remove Generated Mask");
	if (WorkingLayers.IsValidIndex(LayerIndex)
		&& WorkingLayers[LayerIndex].Children.IsValidIndex(ChildIndex))
	{
		switch (ResolveChild(LayerIndex, ChildIndex)->Type)
		{
		case EMixtormatLayerChildType::Craquelure:
			RemoveLabel = LOCTEXT("RemoveCraquelureChild", "Remove Craquelure");
			break;
		case EMixtormatLayerChildType::ColorId:
			RemoveLabel = LOCTEXT("RemoveColorIdChild", "Remove Color ID Mask");
			break;
		case EMixtormatLayerChildType::Filter:
			RemoveLabel = ResolveChild(LayerIndex, ChildIndex)->Filter.bSurfaceIds
				? LOCTEXT("RemoveSurfaceIdsChild", "Remove Surface IDs")
				: LOCTEXT("RemoveFilterChild", "Remove Cluster IDs");
			break;
		case EMixtormatLayerChildType::HsvFilter:
			RemoveLabel = LOCTEXT("RemoveHsvFilterChild", "Remove HSV From IDs");
			break;
		case EMixtormatLayerChildType::RandomId:
			RemoveLabel = LOCTEXT("RemoveRandomIdChild", "Remove Random From IDs");
			break;
		case EMixtormatLayerChildType::RampId:
			RemoveLabel = LOCTEXT("RemoveRampIdChild", "Remove Ramp From IDs");
			break;
		case EMixtormatLayerChildType::UvFromIds:
			RemoveLabel = LOCTEXT("RemoveUvIdChild", "Remove UV From IDs");
			break;
		case EMixtormatLayerChildType::ReliefFromIds:
			RemoveLabel = LOCTEXT("RemoveReliefIdChild", "Remove Relief From IDs");
			break;
		case EMixtormatLayerChildType::BoundaryFromIds:
			RemoveLabel = LOCTEXT("RemoveBoundaryIdChild", "Remove Boundary From IDs");
			break;
		case EMixtormatLayerChildType::PatternId:
			RemoveLabel = LOCTEXT("RemovePatternIdChild", "Remove Pattern IDs");
			break;

		case EMixtormatLayerChildType::IdGroup:
			RemoveLabel = LOCTEXT("RemoveIdGroupChild", "Remove ID Group");
			break;
		case EMixtormatLayerChildType::OutputReference:
			RemoveLabel = LOCTEXT("RemoveOutputReferenceChild", "Remove Output Reference");
			break;
		case EMixtormatLayerChildType::Generator:
			RemoveLabel = LOCTEXT("RemoveGeneratorChild", "Remove Generator");
			break;
		case EMixtormatLayerChildType::HeightBlend:
			RemoveLabel = LOCTEXT("RemoveHeightBlendChild", "Remove Height Blend");
			break;
		case EMixtormatLayerChildType::HeightCurve:
			RemoveLabel = LOCTEXT("RemoveHeightCurveChild", "Remove Height Remap");
			break;
		case EMixtormatLayerChildType::HeightColorRamp:
			RemoveLabel = LOCTEXT("RemoveHeightColorRampChild", "Remove Height Color Ramp");
			break;
		case EMixtormatLayerChildType::HeightPush:
			RemoveLabel = LOCTEXT("RemoveHeightPushChild", "Remove Height Push");
			break;
		case EMixtormatLayerChildType::StructuralWarp:
			RemoveLabel = LOCTEXT("RemoveStructuralWarpChild", "Remove Structural Warp");
			break;
		default:
			break;
		}
	}

	Menu.Item(
		RemoveLabel,
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveGeneratedFromLayer(LayerIndex, ChildIndex);
		}))
		.Destructive();
	return Menu.Build();
}

// Fewer entries than a mask's, because a blur has none of what that menu offers: no source to
// replace, no blend mode, nothing to publish. What it shares is the instancing and the removal.
TSharedRef<SWidget> SMixtormat::BuildBlurContextMenu(const int32 LayerIndex, const int32 ChildIndex)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(
		LOCTEXT("DuplicateBlurContext", "Duplicate"),
		MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			DuplicateLayerChild(LayerIndex, ChildIndex);
		}));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, ChildIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("RemoveMaskFilterContext", "Remove Filter"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex]()
		{
			RemoveMaskFromLayer(LayerIndex, ChildIndex);
		}))
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
		MixtormatIcons::Effect(),
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
			DuplicateLayerChild(LayerIndex, MaskIndex);
		}));
	AddSharedChildMenuItems(Menu, MakeChildAddress(LayerIndex, MaskIndex));
	Menu.Separator();
	Menu.Item(
		LOCTEXT("DeleteMaskContext", "Remove Mask"),
		MixtormatIcons::Trash(),
		FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex]()
		{
			RemoveMaskFromLayer(LayerIndex, MaskIndex);
		}))
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
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		// Ticked rather than merely listed: the mode a mask is already in is the thing you most
		// want to know when you open this.
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, MaskIndex, Mode]()
			{
				SetMaskBlendMode(LayerIndex, MaskIndex, Mode);
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, MaskIndex, Mode]()
			{
				return WorkingLayers.IsValidIndex(LayerIndex)
					&& WorkingLayers[LayerIndex].Children.IsValidIndex(MaskIndex)
					&& WorkingLayers[LayerIndex].Children[MaskIndex].Mask.BlendMode == Mode;
			}));
	}
	return Menu.Build();
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
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				if (!ResolveChild(LayerIndex, ChildIndex)
					|| ResolveChild(LayerIndex, ChildIndex)->Type
						!= EMixtormatLayerChildType::Generated)
				{
					return;
				}
				ResolveChild(LayerIndex, ChildIndex)->Generated.BlendMode = Mode;
				RefreshLayeredPreview();
				RebuildLayerList();
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, LayerIndex, ChildIndex, Mode]()
			{
				return ResolveChild(LayerIndex, ChildIndex)
					&& ResolveChild(LayerIndex, ChildIndex)->Generated.BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

void SMixtormat::AddGeneratorFlowMenuItems(
	MixtormatMenu::FBuilder& Menu, const FMixtormatChildAddress& Owner)
{
	for (const EMixtormatEffectType Type : {EMixtormatEffectType::ShapeDeform,
		EMixtormatEffectType::GeneratorFlow, EMixtormatEffectType::FlowCarve})
	{
		FMixtormatLayerChild Probe;
		Probe.Type = EMixtormatLayerChildType::Effect;
		Probe.Effect.ProceduralType = Type;
		Menu.Item(GetLayerChildName(Probe), MixtormatIcons::Effect(),
			FSimpleDelegate::CreateLambda([this, Owner, Type]() { AddGeneratorFlow(Owner, Type); }))
			.Enabled(TAttribute<bool>::CreateLambda([this, Owner]() { return CanAddGeneratorFlow(Owner); }));
	}
}

#undef LOCTEXT_NAMESPACE
