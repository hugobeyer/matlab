// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatRecipes.h"
#include "MixtormatLayerGroups.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Layers/SMixtormatLayerGroupContainer.h"
#include "UI/Layers/SMixtormatSourceRow.h"
#include "UI/Layers/SMixtormatSourcesShelf.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Layers/MixtormatStructuralConnectionProjection.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

void SMixtormat::RebuildLayerList()
{
	StructuralEndpointPreview.Reset();
	for (auto It = CollapsedGeneratorAddresses.CreateIterator(); It; ++It)
	{
		int32 OwnerIndex, ChildIndex;
		if (!ResolveHierarchyChildAddress(*It, OwnerIndex, ChildIndex)) { It.RemoveCurrent(); continue; }
		const auto& Children = It->OwnerType == EMixtormatChildOwnerType::Layer
			? WorkingLayers[OwnerIndex].Children : WorkingLayerGroups[OwnerIndex].Children;
		if (Children[ChildIndex].Type != EMixtormatLayerChildType::Generator) { It.RemoveCurrent(); }
	}
	ChildRowWidgets.Reset();
	AmbiguousChildRowAddresses.Reset();
	if (!LayerListBox.IsValid())
	{
		return;
	}

	LayerListBox->ClearChildren();
	StructuralIncomingCountLabels.Reset();
	StructuralConnectionLabelCache.Reset();
	LayerThumbnails.Reset();
	LayerRowWidgets.Reset();
	GroupRowWidgets.Reset();

	const Mixtormat::FMixtormatLayerMetrics& LayerLayout = FMixtormatThemeStore::GetResolved().LayerLayout;
	const Mixtormat::FMixtormatHierarchyTheme& HierarchyStyle = FMixtormatThemeStore::GetResolved().LayerHierarchy;

	// Only the visual parent changes; every existing row target keeps its own geometry and handlers.
	TSharedPtr<SVerticalBox> GroupBody;
	for (int32 LayerIndex = 0; LayerIndex < WorkingLayers.Num(); ++LayerIndex)
	{
		const FGuid GroupId = WorkingLayers[LayerIndex].GroupId;
		const FMixtormatLayerGroup* Group =
			MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId);
		if (!Group)
		{
			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, LayerLayout.Gap)
			[
				BuildLayerRow(LayerIndex)
			];
			continue;
		}

		const bool bFirstMember = LayerIndex == 0 || WorkingLayers[LayerIndex - 1].GroupId != GroupId;
		int32 GroupFirstIndex = INDEX_NONE;
		int32 GroupLastIndex = INDEX_NONE;
		MixtormatLayerGroups::GetGroupRange(WorkingLayers, GroupId, GroupFirstIndex, GroupLastIndex);
		if (bFirstMember)
		{
			GroupBody = SNew(SVerticalBox);
			const TSharedRef<SWidget> Header = SNew(SMixtormatGroupRowDropTarget)
				.TargetGroupId(GroupId)
				.FirstMemberIndex(GroupFirstIndex)
				.LastMemberIndex(GroupLastIndex)
				.OnLayerDropped(this, &SMixtormat::HandleLayerDroppedOnGroup)
				.OnLayerInsertedAt(this, &SMixtormat::HandleLayerInsertedAt)
				.OnGroupInsertedAt(this, &SMixtormat::HandleGroupInsertedAt)
				.OnSurfaceInsertedAt(this, &SMixtormat::HandleSurfaceDroppedAt)
				.OnChildDropped(this, &SMixtormat::MoveChildToGroup)
				[
					BuildLayerGroupRow(GroupId)
				];
			LayerListBox->AddSlot().AutoHeight()
			[
				SNew(SMixtormatLayerGroupContainer)
				.Header()[Header]
				.Body()
				[
					SNew(SBox).Padding(FMargin(0.0f, LayerLayout.Gap, 0.0f, 0.0f))
					[GroupBody.ToSharedRef()]
				]
			];
		}

		// A collapsed group hides its members without touching their own expanded state, so
		// reopening it puts every layer back the way it was left.
		if (!IsGroupExpanded(GroupId))
		{
			continue;
		}

		// The shared stack, listed once under the header rather than repeated on every member --
		// which is exactly what it is: one authored copy, applied to each of them at compose time.
		if (bFirstMember)
		{
			const TArray<FMixtormatProjectedChildRow> VisibleRows = FilterVisibleHierarchyRows(BuildGroupHierarchyRows(GroupId));

			for (int32 DisplayIndex = 0; DisplayIndex < VisibleRows.Num(); ++DisplayIndex)
			{
				const int32 ChildIndex = VisibleRows[DisplayIndex].AuthoredChildIndex;
				// One indent for being inside the group, plus one per scope level -- the same
				// depth-times-indent a layer's own children get, so a blur under a shared mask
				// reads as being under it rather than beside it.
				const FMixtormatLayerHierarchyPaint Hierarchy = BuildGroupHierarchyPaint(VisibleRows, DisplayIndex);
				GroupBody->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, LayerLayout.Gap)
				[
					SNew(SMixtormatLayerHierarchy).Hierarchy(Hierarchy)
					[
						SNew(SBox).Padding(FMargin(Hierarchy.Indent, 0.0f, 0.0f, 0.0f))
						[BuildGroupChildRow(GroupId, ChildIndex)]
					]
				];
			}
		}

		FMixtormatLayerHierarchyPaint Hierarchy;
		Hierarchy.Indent = HierarchyStyle.Indent;
		Hierarchy.RowHeight = LayerLayout.RowHeight;
		Hierarchy.bLast = LayerIndex == GroupLastIndex;
		GroupBody->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, LayerLayout.Gap)
		[
			// The tee spans the entire expanded member, continuing behind all of its descendants.
			SNew(SMixtormatLayerHierarchy).Hierarchy(Hierarchy)
			[
				SNew(SBox).Padding(FMargin(LayerLayout.LayerIndent, 0.0f, 0.0f, 0.0f))
				[BuildLayerRow(LayerIndex)]
			]
		];
	}

	// Groups with no members have no position in the stack, so they are listed after it. They are
	// still real containers: the header is the drop target that gives them their first member, and
	// their shared stack is authored here exactly as a membered group's is.
	for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
	{
		int32 FirstIndex = INDEX_NONE;
		int32 LastIndex = INDEX_NONE;
		if (MixtormatLayerGroups::GetGroupRange(WorkingLayers, Group.GroupId, FirstIndex, LastIndex))
		{
			continue;
		}
		// Edge drops on an empty group append at the end of the stack, which is where the group is
		// drawn; the middle of the header is the membership drop.
		const int32 AppendIndex = WorkingLayers.Num();
		TSharedPtr<SVerticalBox> EmptyBody = SNew(SVerticalBox);
		const TSharedRef<SWidget> Header = SNew(SMixtormatGroupRowDropTarget)
			.TargetGroupId(Group.GroupId)
			.FirstMemberIndex(AppendIndex)
			.LastMemberIndex(AppendIndex - 1)
			.OnLayerDropped(this, &SMixtormat::HandleLayerDroppedOnGroup)
			.OnLayerInsertedAt(this, &SMixtormat::HandleLayerInsertedAt)
			.OnGroupInsertedAt(this, &SMixtormat::HandleGroupInsertedAt)
			.OnSurfaceInsertedAt(this, &SMixtormat::HandleSurfaceDroppedAt)
			.OnChildDropped(this, &SMixtormat::MoveChildToGroup)
			[
				BuildLayerGroupRow(Group.GroupId)
			];
		if (IsGroupExpanded(Group.GroupId))
		{
			const TArray<FMixtormatProjectedChildRow> VisibleRows = FilterVisibleHierarchyRows(BuildGroupHierarchyRows(Group.GroupId));

			for (int32 DisplayIndex = 0; DisplayIndex < VisibleRows.Num(); ++DisplayIndex)
			{
				const int32 ChildIndex = VisibleRows[DisplayIndex].AuthoredChildIndex;
				const FMixtormatLayerHierarchyPaint Hierarchy =
					BuildGroupHierarchyPaint(VisibleRows, DisplayIndex);
				EmptyBody->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, LayerLayout.Gap)
				[
					SNew(SMixtormatLayerHierarchy).Hierarchy(Hierarchy)
					[
						SNew(SBox).Padding(FMargin(Hierarchy.Indent, 0.0f, 0.0f, 0.0f))
						[BuildGroupChildRow(Group.GroupId, ChildIndex)]
					]
				];
			}
		}
		LayerListBox->AddSlot().AutoHeight()
		[
			SNew(SMixtormatLayerGroupContainer)
			.Header()[Header]
			.Body()
			[
				SNew(SBox).Padding(FMargin(0.0f, LayerLayout.Gap, 0.0f, 0.0f))
				[EmptyBody.ToSharedRef()]
			]
		];
	}
}

void SMixtormat::RebuildMaskList()
{
	if (!MaskListBox.IsValid())
	{
		return;
	}

	MaskListBox->ClearChildren();
	MaskThumbnails.Reset();

	const TArray<FMixtormatMaskEntry> Masks = FMixtormatRegistry::GetMasks();
	for (int32 MaskIndex = 0; MaskIndex < Masks.Num(); ++MaskIndex)
	{
		const FMixtormatMaskEntry& Mask = Masks[MaskIndex];
		MaskListBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			BuildMaskCard(Mask.DisplayName, Mask.AssetPath, Mask.ThumbnailAsset, true)
		];
	}

	if (Masks.IsEmpty())
	{
		MaskListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"EmptyMaskRegistry",
				"No masks found. Repair or reinstall Mixtormat, or import a PNG mask folder."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

TSharedRef<SWidget> SMixtormat::BuildLayerStackPanel()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatLayerMetrics& LayerLayout = FMixtormatThemeStore::GetResolved().LayerLayout;
	return SNew(SMixtormatLayerDropTarget)
		.OnSurfaceDropped(this, &SMixtormat::HandleSurfaceDropped)
		.OnGetContextMenu(FOnGetContent::CreateSP(this, &SMixtormat::BuildLayerColumnContextMenu))
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::LayerStackWidth)
			[
				SNew(SBorder)
				.Padding(LayerLayout.ColumnGutter)
				.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Collapsed : EVisibility::Visible; })
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton)
							.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
							.Text(LOCTEXT("CreateWorkingMaterial", "Create Material"))
							.IsEnabled_Lambda([this]() { return SelectedPreviewMaterial.IsValid(); })
							.ToolTipText(LOCTEXT("CreateWorkingMaterialHint", "Select a saved library surface first, then create a nondestructive layered recipe."))
							.OnClicked(this, &SMixtormat::StartNewMaterial)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
							.Text(LOCTEXT("OpenWorkingMaterialFromLayers", "Open Saved Recipe..."))
							.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([]()
							{
								return FMixtormatRegistry::GetSurfaces().IsEmpty()
									? LOCTEXT("NoSavedSurfaces", "No saved Mixtormat surfaces were found. Import a complete texture set or open an existing recipe.")
									: LOCTEXT("SelectSurfaceToBegin", "Select or drag a library surface to begin.");
							})
							.AutoWrapText(true)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
					// Sources stays above the creation toolbar and the scrolling layer stack.
					+ SVerticalBox::Slot().AutoHeight()
					[
						BuildSourcesShelf()
					]
					// Layer, Group, and Fill Layer actions sit immediately beneath Sources.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, LayerLayout.SourcesBottomGap, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SBox)
							.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonHeight)
							[
								SNew(SMixtormatGroupAction, true)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.ToolTipText(LOCTEXT("AddMaterialLayerBottomHint", "Add a material layer from the selected library surface."))
								.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Material); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(16.0f)
										.HeightOverride(16.0f)
										[
											SNew(SImage).Image(MixtormatIcons::LayerMaterial())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddMaterialLayerBottom", "Layer"))
									]
								]
							]
						]
						// Beside the two Add buttons rather than in the stack: grouping acts on the
						// selection, so it belongs with the other things that change the stack
						// rather than with anything a single row owns.
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						
						[
							SNew(SBox)
							.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonHeight)
							[
								SNew(SMixtormatGroupAction, true)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.IsEnabled_Lambda([this]() { return CanCreateGroup(); })
								.ToolTipText_Lambda([this]()
								{
									const int32 Count = GetSelectedLayerIndices().Num();
									if (Count == 0)
									{
										return LOCTEXT(
											"CreateEmptyGroupHint",
											"Create an empty group. It is listed after the stack until a layer joins it -- drag a layer onto its header, or drop a surface there.");
									}
									return FText::Format(
										LOCTEXT(
											"CreateGroupHint",
											"Group {0} selected layer(s). They are gathered into one block, so layers between them move and height references that end up pointing upward are dropped."),
										FText::AsNumber(Count));
								})
								.OnClicked_Lambda([this]() { return CreateGroupFromSelection(); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(16.0f)
										.HeightOverride(16.0f)
										[
											SNew(SImage).Image(MixtormatIcons::Folder())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("CreateGroupBottom", "Group"))
									]
								]
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SBox)
							.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonHeight)
							[
								SNew(SMixtormatGroupAction, false)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.ToolTipText(LOCTEXT("AddFillLayerBottomHint", "Create a constant Base Color, Roughness, IOR, and Metallic fill layer."))
								.OnClicked_Lambda([this]() { return AddWorkingLayer(EMixtormatLayerType::Fill); })
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(16.0f)
										.HeightOverride(16.0f)
										[
											SNew(SImage).Image(MixtormatIcons::LayerFill())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddFillLayerBottom", "Fill Layer"))
									]
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
					[
						SNew(SSeparator)
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						// Empty-space presses deselect; individual rows handle their own presses.
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush(TEXT("NoBorder")))
						.Padding(0.0f)
						.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& MouseEvent)
						{
							if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !HasAnySelection())
							{
								return FReply::Unhandled();
							}
							ClearLayerSelection();
							return FReply::Handled();
						})
						[
							SAssignNew(LayerScrollBox, SScrollBox)
							+ SScrollBox::Slot()[SAssignNew(LayerListBox, SVerticalBox)]
						]
					]

				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildSourcesShelf()
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	// The shelf is above the creation buttons and layer list. Limit its expanded body
	// so sources never consume the full column height; additional rows scroll internally.
	constexpr int32 MaxVisibleSourceRows = 6;

	TSharedRef<SWidget> Shelf = SNew(SMixtormatSourcesShelf)
		.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
		.Title(LOCTEXT("SourcesShelfTitle", "SOURCES"))
		.Expanded_Lambda([this]() { return bSourcesExpanded; })
		.OnToggle(FSimpleDelegate::CreateSP(this, &SMixtormat::ToggleSourcesExpanded))
		[
			// Unity-style array card: only rows live inside the card.
			// The add action is a small attached bottom-right tab, not a layer action.
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SMixtormatSurfaceBox)
				.Recipe_Lambda([]()
				{
					const auto& Theme = FMixtormatThemeStore::GetTheme();
					Mixtormat::FMixtormatSurfaceRecipe Recipe =
						Mixtormat::MakeCardBodyRecipe(Theme, 1.0f, 0.0f);
					Recipe.Radius = Theme.Card.Radius;
					return Recipe;
				})
				.InheritWidgetStyle(true)
				.Padding(FMargin(Resolved.CardLayout.Padding, Resolved.CardLayout.Gap))
				[
					SNew(SBox)
					.MinDesiredHeight(Resolved.LayerLayout.SourcesEmptyHeight)
					.MaxDesiredHeight((Resolved.LayerLayout.SourcesRowHeight + Resolved.LayerLayout.SourcesRowGap) * MaxVisibleSourceRows)
					[
						SNew(SScrollBox)
						.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
						.ScrollBarThickness(FVector2D(Resolved.ShellLayout.ScrollbarThickness))
						+ SScrollBox::Slot()[SAssignNew(SourcesListBox, SVerticalBox)]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				// The tab touches the card's lower edge. Reuses the group button
				// surface/hairline tokens and the existing six-kind menu.
				SAssignNew(AddSourceAnchor, SMenuAnchor)
				.Placement(MenuPlacement_AboveAnchor)
				.OnGetMenuContent(this, &SMixtormat::BuildAddSourcesMenu)
				[
					SNew(SBox)
					.WidthOverride(Resolved.LayerLayout.SourcesAddTabWidth)
					.HeightOverride(Resolved.LayerLayout.SourcesAddTabHeight)
					[
						SNew(SMixtormatHelp)
						.Text(LOCTEXT("AddSourceHint", "Add a reusable generator source."))
						[
							SNew(SMixtormatSurfaceBox)
							.Recipe_Lambda([this]()
							{
								return Mixtormat::MakeSourcesAddTabRecipe(
									FMixtormatThemeStore::GetTheme(),
									AddSourceAnchor.IsValid() && AddSourceAnchor->IsHovered());
							})
							.InheritWidgetStyle(true)
							[
								SNew(SButton)
								.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
								.ContentPadding(0.0f)
								.OnClicked_Lambda([this]()
								{
									if (AddSourceAnchor.IsValid()) { AddSourceAnchor->SetIsOpen(true); }
									return FReply::Handled();
								})
								[
									SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
									[
										SNew(SImage)
										.Image(MixtormatIcons::Add())
										.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
											Mixtormat::EMixtormatColorRole::Text)))
									]
								]
							]
						]
					]
				]
			]
		];
	RebuildSourcesList();
	return Shelf;
}

void SMixtormat::RebuildSourcesList()
{
	if (!SourcesListBox.IsValid())
	{
		return;
	}
	SourcesListBox->ClearChildren();
	SourceRowWidgets.Reset();
	for (const FMixtormatSourceEntry& Entry : WorkingSources)
	{
		const FGuid SourceId = Entry.SourceId;
		TSharedPtr<SMixtormatSourceRow> Row;
		SAssignNew(Row, SMixtormatSourceRow)
			.Name_Lambda([this, SourceId]()
			{
				const FMixtormatSourceEntry* Source =
					WorkingSources.FindByPredicate([SourceId](const FMixtormatSourceEntry& Candidate)
					{ return Candidate.SourceId == SourceId; });
				return Source ? Source->DisplayName : FText::GetEmpty();
			})
			.Kind_Lambda([this, SourceId]()
			{
				const FMixtormatSourceEntry* Source =
					WorkingSources.FindByPredicate([SourceId](const FMixtormatSourceEntry& Candidate)
					{ return Candidate.SourceId == SourceId; });
				return Source
					? StaticEnum<EMixtormatGeneratorType>()->GetDisplayNameTextByValue(
						static_cast<int64>(Source->Child.Generator.Type))
					: FText::GetEmpty();
			})
			.bEnabled_Lambda([this, SourceId]()
			{
				const FMixtormatSourceEntry* Source =
					WorkingSources.FindByPredicate([SourceId](const FMixtormatSourceEntry& Candidate)
					{ return Candidate.SourceId == SourceId; });
				return Source && Source->Child.Generator.bEnabled;
			})
			.bSelected_Lambda([this, SourceId]() { return SelectedSourceId == SourceId; })
			.OnSelected(FSimpleDelegate::CreateSP(this, &SMixtormat::SelectSource, SourceId))
			.OnToggleEnabled(FSimpleDelegate::CreateLambda([this, SourceId]()
			{
				FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
					[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; });
				if (!Source) { return; }
				Source->Child.Generator.bEnabled = !Source->Child.Generator.bEnabled;
				RefreshLayeredPreview();
			}))
			.OnNameCommitted(FOnTextCommitted::CreateLambda([this, SourceId](const FText& Name, ETextCommit::Type)
			{
				RenameSource(SourceId, Name);
			}))
			.OnGetContextMenu(FOnGetContent::CreateSP(this, &SMixtormat::BuildSourceContextMenu, SourceId));
		SourceRowWidgets.Add(SourceId, Row);
		SourcesListBox->AddSlot().AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().LayerLayout.SourcesRowGap)
		[Row.ToSharedRef()];
	}
}

TSharedRef<SWidget> SMixtormat::BuildAddSourcesMenu()
{
	// The same six kinds the generator Add menu offers, with the same labels and glyph: a source
	// is a generator payload in a different place, not a different node.
	MixtormatMenu::FBuilder Menu;
	Menu.Item(LOCTEXT("AddStrataCarverSource", "Strata Carver"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::StrataCarver); }));
	Menu.Item(LOCTEXT("AddCracksSource", "Cracks"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Cracks); }));
	Menu.Item(LOCTEXT("AddRockFormationSource", "Rock Formation"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::RockFormation); }));
	Menu.Item(LOCTEXT("AddPebblesSource", "Pebbles"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Pebbles); }));
	Menu.Item(LOCTEXT("AddCliffStrataSource", "Cliff Strata"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::CliffStrata); }));
	Menu.Item(LOCTEXT("AddNoiseSource", "Noise"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Noise); }));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildSourceContextMenu(const FGuid SourceId)
{
	MixtormatMenu::FBuilder Menu;
	Menu.Item(LOCTEXT("RenameSource", "Rename"), nullptr,
		FSimpleDelegate::CreateLambda([this, SourceId]()
		{
			SelectSource(SourceId);
			if (const TWeakPtr<SMixtormatSourceRow>* WeakRow = SourceRowWidgets.Find(SourceId))
			{
				if (const TSharedPtr<SMixtormatSourceRow> Row = WeakRow->Pin()) { Row->BeginRename(); }
			}
		}));
	Menu.Item(
		LOCTEXT("DeleteSource", "Delete Source"),
		MixtormatIcons::Trash(),
		// The action returns FReply; the menu delegate takes void.
		FSimpleDelegate::CreateLambda([this, SourceId]() { DeleteSource(SourceId); }));
	return Menu.Build();
}

FMixtormatSourceEntry* SMixtormat::GetSelectedSource()
{
	return const_cast<FMixtormatSourceEntry*>(
		static_cast<const SMixtormat*>(this)->GetSelectedSource());
}

const FMixtormatSourceEntry* SMixtormat::GetSelectedSource() const
{
	return SelectedSourceId.IsValid()
		? WorkingSources.FindByPredicate([SourceId = SelectedSourceId](const FMixtormatSourceEntry& Candidate)
			{ return Candidate.SourceId == SourceId; })
		: nullptr;
}

void SMixtormat::SelectSource(const FGuid SourceId)
{
	// One subject at a time, the same exclusivity the group selection keeps.
	SelectedSourceId = SourceId;
	SelectedLayerIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	bHasSelectedLayer = false;
	SyncSelectedLayerControls();
}

FReply SMixtormat::AddSource(const EMixtormatGeneratorType Kind)
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	FMixtormatSourceEntry& Entry = WorkingSources.AddDefaulted_GetRef();
	// The same defaults a generator child gets from the Add menu, so a source starts configured
	// exactly like its layer-stack counterpart would.
	ApplyChildCreationDefaults(Entry.Child, CreationKindForGenerator(Kind));
	Entry.Child.ScopeOwnerChildId.Invalidate();

	int32 SameKindCount = 0;
	for (const FMixtormatSourceEntry& Existing : WorkingSources)
	{
		if (Existing.Child.Generator.Type == Kind)
		{
			++SameKindCount;
		}
	}
	Entry.DisplayName = FText::Format(
		LOCTEXT("SourceDefaultName", "{0} Source {1}"),
		StaticEnum<EMixtormatGeneratorType>()->GetDisplayNameTextByValue(static_cast<int64>(Kind)),
		FText::AsNumber(SameKindCount));

	SelectSource(Entry.SourceId);
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildSourcesList();
	if (AddSourceAnchor.IsValid())
	{
		AddSourceAnchor->SetIsOpen(false);
	}
	return FReply::Handled();
}

FReply SMixtormat::DeleteSource(const FGuid SourceId)
{
	const int32 Removed = WorkingSources.RemoveAll([SourceId](const FMixtormatSourceEntry& Candidate)
	{
		return Candidate.SourceId == SourceId;
	});
	if (Removed == 0)
	{
		return FReply::Handled();
	}
	if (SelectedSourceId == SourceId)
	{
		SelectedSourceId.Invalidate();
	}
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	SyncSelectedLayerControls();
	RebuildSourcesList();
	return FReply::Handled();
}

FReply SMixtormat::RenameSource(const FGuid SourceId, const FText NewName)
{
	FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
		[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; });
	// A blank name is refused rather than replaced with a default, the same rule as layers.
	if (!Source || NewName.IsEmptyOrWhitespace() || Source->DisplayName.EqualTo(NewName))
	{
		return FReply::Handled();
	}
	Source->DisplayName = NewName;
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	SyncSelectedLayerControls();
	return FReply::Handled();
}

void SMixtormat::ToggleSourcesExpanded()
{
	bSourcesExpanded = !bSourcesExpanded;
}

#undef LOCTEXT_NAMESPACE
