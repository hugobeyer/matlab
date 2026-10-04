// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatThemeStore.h"
#include "MixtormatLayerGroups.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Layers/SMixtormatLayerGroupContainer.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

void SMixtormat::RebuildLayerList()
{
	if (!LayerListBox.IsValid())
	{
		return;
	}

	LayerListBox->ClearChildren();
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
			for (int32 ChildIndex = 0; ChildIndex < Group->Children.Num(); ++ChildIndex)
			{
				// One indent for being inside the group, plus one per scope level -- the same
				// depth-times-indent a layer's own children get, so a blur under a shared mask
				// reads as being under it rather than beside it.
				const FMixtormatLayerHierarchyPaint Hierarchy = ChildHierarchyPaint(Group->Children, ChildIndex, true);
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
				SNew(SBox).Padding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
				[BuildLayerRow(LayerIndex)]
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
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(MixtormatTokens::LayerStackHeaderHeight)
						[
							SNew(SHorizontalBox)
							.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
							// The count reads first, on the leading edge; the two creation glyphs
							// pair up on the trailing edge, same grouping as the text buttons below.
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							.HAlign(HAlign_Left).VAlign(VAlign_Center)
							.Padding(MixtormatTokens::LayerRowInsetLeading, 0.0f, FMixtormatThemeStore::GetResolved().LayerLayout.ItemGap, 0.0f)
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return FText::Format(LOCTEXT("LayerCountCompact", "{0} LAYERS"), FText::AsNumber(WorkingLayers.Num()));
								})
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
							]
							// Two creations, two glyphs: a square for a material, a circle for a fill. The
							// same icon button the eye and the chevrons are, so the bar costs one row and
							// no plate.
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SMixtormatIconButton)
								.Icon(MixtormatIcons::LayerMaterial())
								.ToolTip(LOCTEXT("AddMaterialLayerHintCompact", "Add a material layer from the selected library surface."))
								.OnClicked_Lambda([this]() { AddWorkingLayer(EMixtormatLayerType::Material); })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, MixtormatTokens::LayerRowInsetTrailing, 0.0f)
							[
								SNew(SMixtormatIconButton)
								.Icon(MixtormatIcons::LayerFill())
								.ToolTip(LOCTEXT("AddFillLayerHintCompact", "Create a constant Base Color, Roughness, IOR, and Metallic fill layer."))
								.OnClicked_Lambda([this]() { AddWorkingLayer(EMixtormatLayerType::Fill); })
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						// Rows handle their own presses, so a left press that reaches this border
						// landed on empty space: deselect, and the inspector falls back to globals.
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
							SNew(SScrollBox)
							+ SScrollBox::Slot()[SAssignNew(LayerListBox, SVerticalBox)]
						]
					]
					// A hairline like the one above the scroll box, so the permanent controls read
					// as their own footer rather than as one more row of the stack.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(SSeparator)]
					// Permanent creation controls, below the rows rather than inside any one of
					// them, so they stay put -- and stay reachable -- whether the stack holds
					// forty layers or none. Same two creations as the header pair above, just
					// spelled out in text since this is the row a user lands on with an empty
					// stack and no icon-only glyph to already have learned. Right-aligned to match
					// the header pair's trailing edge.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, LayerLayout.Gap, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return bHasWorkingMaterial ? EVisibility::Visible : EVisibility::Collapsed; })
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
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
										.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerMaterial())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddMaterialLayerBottom", "Layer"))
									]
								]
							]
						]
						// Beside the two Add buttons rather than in the stack: grouping acts on the
						// selection, so it belongs with the other things that change the stack
						// rather than with anything a single row owns.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						
						[
							SNew(SBox)
							.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonHeight)
							[
								SNew(SMixtormatGroupAction, true)
								.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
								.IsEnabled_Lambda([this]() { return CanCreateGroupFromSelection(); })
								.ToolTipText_Lambda([this]()
								{
									const int32 Count = GetSelectedLayerIndices().Num();
									if (Count == 0)
									{
										return LOCTEXT(
											"CreateGroupDisabledHint",
											"Select one or more layers first. Shift click for a run, Ctrl click to add one.");
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
										.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::Folder())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("CreateGroupBottom", "Group"))
									]
								]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						.Padding(FMixtormatThemeStore::GetResolved().LayerLayout.ItemGap, 0.0f, MixtormatTokens::LayerRowInsetTrailing, 0.0f)
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
										.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.IconButtonSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerFill())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(LOCTEXT("AddFillLayerBottom", "Fill Layer"))
									]
								]
							]
						]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
