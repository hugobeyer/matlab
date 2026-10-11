// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatRecipes.h"
#include "MixtormatLayerGroups.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Layers/MixtormatLayerShortcuts.h"
#include "UI/Layers/SMixtormatLayerGroupContainer.h"
#include "UI/Layers/SMixtormatSourceRow.h"
#include "UI/Layers/SMixtormatSourcesShelf.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

using namespace MixtormatLayersPrivate;

class SMixtormatSourceAddTarget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatSourceAddTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ChildSlot[InArgs._Content.Widget]; }
};

namespace
{
	// Rewrites every reference a source entry's payload owns from one identity set to another: the
	// single place source identity renewal is implemented. The copy's SourceId and child ids must
	// already be assigned by the caller; FromSourceId is the identity being copied from. Independent
	// paste, Duplicate and Break Instance all run through here, so a reference field added to a
	// child can never be forgotten by one of them.
	void CopySourcePayload(const FMixtormatSourceEntry& Source, FMixtormatSourceEntry& Copy,
		const TMap<FGuid, FGuid>& ChildIdRemap, const FGuid& FromSourceId)
	{
		const FGuid ToSourceId = Copy.SourceId;
		const auto Remap = [&ChildIdRemap, &FromSourceId, &ToSourceId](FGuid& Owner, FGuid& Child)
		{
			if (Owner != FromSourceId) return;
			if (const FGuid* Next = ChildIdRemap.Find(Child)) { Owner = ToSourceId; Child = *Next; }
		};
		const auto RemapOutput = [&Remap](FMixtormatOutputReference& Reference)
		{
			if (Reference.IsShelfSource())
			{
				Remap(Reference.SourceShelfId, Reference.SourceChildId);
			}
			else
			{
				Remap(Reference.SourceLayerId, Reference.SourceChildId);
			}
		};
		const auto Rewrite = [&ChildIdRemap, &Remap, &RemapOutput](FMixtormatLayerChild& Child)
		{
			if (const FGuid* Parent = ChildIdRemap.Find(Child.ScopeOwnerChildId)) Child.ScopeOwnerChildId = *Parent;
			Remap(Child.SourceLayerId, Child.SourceChildId);
			RemapOutput(Child.OutputReference);
			RemapOutput(Child.Generator.HeightSource);
			RemapOutput(Child.Generator.WarpSource);
			// Whichever published-source form the mask uses, its child id remaps with the payload.
			Remap(Child.Mask.PublishedSourceShelfId, Child.Mask.PublishedSourceChildId);
			Remap(Child.Mask.PublishedSourceLayerId, Child.Mask.PublishedSourceChildId);
			RemapOutput(Child.Behavior.Direction.Published);
			RemapOutput(Child.Behavior.Height.Published);
			RemapOutput(Child.Behavior.Influence.Published);
			Remap(Child.HeightBlend.SourceLayerId, Child.HeightBlend.SourceChildId);
			if (const FGuid* Input = ChildIdRemap.Find(Child.HeightColorRamp.SourceChildId)) Child.HeightColorRamp.SourceChildId = *Input;
			for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
			{
				Remap(Binding.Reference.Source.LayerId, Binding.Reference.Source.ChildId);
				Remap(Binding.Driver.SourceLayerId, Binding.Driver.SourceChildId);
			}
		};

		// Root and owned children keep the caller's addressing; only their payload moves, and the
		// references they own follow the remap. Owned child ids were assigned by the caller too.
		const FGuid RootChildId = Copy.Child.ChildId;
		Copy.Child = Source.Child;
		Copy.Child.ChildId = RootChildId;
		Copy.OwnedChildren.SetNum(Source.OwnedChildren.Num());
		for (int32 Index = 0; Index < Source.OwnedChildren.Num(); ++Index)
		{
			const FGuid OwnedChildId = Copy.OwnedChildren[Index].ChildId;
			Copy.OwnedChildren[Index] = Source.OwnedChildren[Index];
			Copy.OwnedChildren[Index].ChildId = OwnedChildId;
		}
		Rewrite(Copy.Child);
		for (FMixtormatLayerChild& Owned : Copy.OwnedChildren) { Rewrite(Owned); }
		// A copy is never a link. Only a caller that intends a link sets the field itself.
		Copy.InstanceOfSourceId.Invalidate();
	}

	// One independent copy: fresh SourceId, root and owned child ids, every owned reference
	// remapped onto the copy's identities. The name is the caller's to adjust.
	FMixtormatSourceEntry MakeIndependentSourceCopy(const FMixtormatSourceEntry& Source)
	{
		FMixtormatSourceEntry Copy;
		Copy.SourceId = FGuid::NewGuid();
		Copy.DisplayName = Source.DisplayName;
		Copy.Child.ChildId = FGuid::NewGuid();
		Copy.OwnedChildren.SetNum(Source.OwnedChildren.Num());
		TMap<FGuid, FGuid> ChildIdRemap;
		ChildIdRemap.Add(Source.Child.ChildId, Copy.Child.ChildId);
		for (int32 Index = 0; Index < Source.OwnedChildren.Num(); ++Index)
		{
			Copy.OwnedChildren[Index].ChildId = FGuid::NewGuid();
			ChildIdRemap.Add(Source.OwnedChildren[Index].ChildId, Copy.OwnedChildren[Index].ChildId);
		}
		CopySourcePayload(Source, Copy, ChildIdRemap, Source.SourceId);
		return Copy;
	}
}

void SMixtormat::RebuildLayerList()
{
	for (auto It = CollapsedGeneratorAddresses.CreateIterator(); It; ++It)
	{
		int32 OwnerIndex, ChildIndex;
		if (!ResolveHierarchyChildAddress(*It, OwnerIndex, ChildIndex)) { It.RemoveCurrent(); continue; }
		const auto& Children = It->OwnerType == EMixtormatChildOwnerType::Layer
			? WorkingLayers[OwnerIndex].Children : WorkingLayerGroups[OwnerIndex].Children;
		if (Children[ChildIndex].Type != EMixtormatLayerChildType::Generator) { It.RemoveCurrent(); }
	}
	ChildRowWidgets.Reset();
	EditableChildRowWidgets.Reset();
	AmbiguousChildRowAddresses.Reset();
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
	const Mixtormat::FMixtormatCreateMetrics& CreateLayout = FMixtormatThemeStore::GetResolved().CreateLayout;
	const auto CreationLabelVisibility = [this](const FText& Label) -> EVisibility
	{
		if (!LeftSwitcher.IsValid()) return EVisibility::Visible;
		const float ColumnWidth = LeftSwitcher->GetCachedGeometry().GetLocalSize().X;
		const float Gutter = FMixtormatThemeStore::GetResolved().LayerLayout.ColumnGutter;
		const float Width = FMath::Max(0.0f, ColumnWidth - 2.0f * Gutter) / 3.0f;
		if (Width <= 0.0f) return EVisibility::Visible;
		const auto& Resolved = FMixtormatThemeStore::GetResolved();
		const FSlateFontInfo& Font = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText")).Font;
		const float LabelWidth = FSlateApplication::Get().GetRenderer()
			->GetFontMeasureService()->Measure(Label, Font).X;
		const float Required = Resolved.CreateLayout.CreationButtonIconSize
			+ Resolved.CreateLayout.CreationButtonIconGap
			+ 2.0f * Resolved.Buttons.HorizontalPadding + LabelWidth;
		return Width >= Required ? EVisibility::Visible : EVisibility::Collapsed;
	};
	return SNew(SMixtormatLayerDropTarget)
		.OnSurfaceDropped(this, &SMixtormat::HandleSurfaceDropped)
		.OnGetContextMenu(FOnGetContent::CreateSP(this, &SMixtormat::BuildLayerColumnContextMenu))
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]()
			{
				const float Available = LeftSwitcher.IsValid()
					? LeftSwitcher->GetCachedGeometry().GetLocalSize().X : 0.0f;
				return FOptionalSize(Available > 0.0f
					? FMath::Min(MixtormatTokens::LayerStackWidth, Available)
					: MixtormatTokens::LayerStackWidth);
			})
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
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(CreateLayout.CreationButtonIconSize)
										.HeightOverride(CreateLayout.CreationButtonIconSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerMaterial())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									.Padding(CreateLayout.CreationButtonIconGap, 0.0f, 0.0f, 0.0f)
									[
										SNew(STextBlock).Text(LOCTEXT("AddMaterialLayerBottom", "Layer"))
										.Clipping(EWidgetClipping::ClipToBounds)
										.OverflowPolicy(ETextOverflowPolicy::Clip)
										.Visibility_Lambda([CreationLabelVisibility]()
										{
											return CreationLabelVisibility(LOCTEXT("AddMaterialLayerBottom", "Layer"));
										})
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
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
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(CreateLayout.CreationButtonIconSize)
										.HeightOverride(CreateLayout.CreationButtonIconSize)
										[
											SNew(SImage).Image(MixtormatIcons::Folder())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									.Padding(CreateLayout.CreationButtonIconGap, 0.0f, 0.0f, 0.0f)
									[
										SNew(STextBlock).Text(LOCTEXT("CreateGroupBottom", "Group"))
										.Clipping(EWidgetClipping::ClipToBounds)
										.OverflowPolicy(ETextOverflowPolicy::Clip)
										.Visibility_Lambda([CreationLabelVisibility]()
										{
											return CreationLabelVisibility(LOCTEXT("CreateGroupBottom", "Group"));
										})
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
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
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SBox)
										.WidthOverride(CreateLayout.CreationButtonIconSize)
										.HeightOverride(CreateLayout.CreationButtonIconSize)
										[
											SNew(SImage).Image(MixtormatIcons::LayerFill())
										]
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									.Padding(CreateLayout.CreationButtonIconGap, 0.0f, 0.0f, 0.0f)
									[
										SNew(STextBlock).Text(LOCTEXT("AddFillLayerBottom", "Fill Layer"))
										.Clipping(EWidgetClipping::ClipToBounds)
										.OverflowPolicy(ETextOverflowPolicy::Clip)
										.Visibility_Lambda([CreationLabelVisibility]()
										{
											return CreationLabelVisibility(LOCTEXT("AddFillLayerBottom", "Fill Layer"));
										})
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
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
				SNew(SMixtormatSourceAddTarget)
				[
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
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									.ContentPadding(0.0f)
									.OnClicked_Lambda([this]()
									{
										if (AddSourceAnchor.IsValid()) { AddSourceAnchor->SetIsOpen(true); }
										return FReply::Handled();
									})
									[
										SNew(SBox)
										.WidthOverride(Resolved.LayerLayout.SourcesAddIconSize)
										.HeightOverride(Resolved.LayerLayout.SourcesAddIconSize)
										.HAlign(HAlign_Center).VAlign(VAlign_Center)
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
				if (!Source) { return FText::GetEmpty(); }
				// An instance reads as what it links to; a broken link says so rather than showing the
				// shell's payload as if it were a producer.
				if (Source->IsInstance())
				{
					const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
						WorkingSources, SourceId);
					return Producer
						? FText::Format(LOCTEXT("SourceInstanceKind", "Instance of {0}"), Producer->DisplayName)
						: LOCTEXT("SourceInstanceMissingKind", "Missing source");
				}
				if (Source->Child.Type != EMixtormatLayerChildType::Generator) { return FText::GetEmpty(); }
				// The Noise family is a Noise source's real type: "Worley F1" says more than "Noise"
				// beside a row whose name already reads "Noise 1".
				if (Source->Child.Generator.Type == EMixtormatGeneratorType::Noise)
				{
					return StaticEnum<EMixtormatNoiseType>()->GetDisplayNameTextByValue(
						static_cast<int64>(Source->Child.Generator.Noise.NoiseType));
				}
				return StaticEnum<EMixtormatGeneratorType>()->GetDisplayNameTextByValue(
					static_cast<int64>(Source->Child.Generator.Type));
			})
			.bEnabled_Lambda([this, SourceId]()
			{
				const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
					WorkingSources, SourceId);
				return Producer
					&& Producer->Child.Type == EMixtormatLayerChildType::Generator
					&& Producer->Child.Generator.bEnabled;
			})
			.bSelected_Lambda([this, SourceId]() { return SelectedSourceId == SourceId; })
			.OnSelected(FSimpleDelegate::CreateSP(this, &SMixtormat::SelectSource, SourceId))
			.OnToggleEnabled(FSimpleDelegate::CreateLambda([this, SourceId]()
			{
				FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
					[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; });
				if (!Source) { return; }
				// The switch lives on whatever publishes: an instance toggles its original.
				FMixtormatSourceEntry* Target = Source;
				if (Source->IsInstance())
				{
					const FGuid ProducerId = MixtormatOutputReferences::ResolveShelfProducerId(WorkingSources, SourceId);
					Target = WorkingSources.FindByPredicate([ProducerId](const FMixtormatSourceEntry& Candidate)
						{ return Candidate.SourceId == ProducerId; });
				}
				if (!Target || Target->Child.Type != EMixtormatLayerChildType::Generator) { return; }
				Target->Child.Generator.bEnabled = !Target->Child.Generator.bEnabled;
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
	// Noise and Masks lead: the shelf is the reusable field library, and coverage producers are
	// what most sources are read for. The generator kinds stay, unchanged, under their own caption
	// rather than disappearing from the menu.
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("AddSourceFieldCaption", "Fields"));
	Menu.Item(LOCTEXT("AddNoiseSource", "Noise"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Noise); }));
	Menu.Item(LOCTEXT("AddMaskSource", "Mask"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddMaskSource(); }));
	Menu.Separator();
	Menu.Caption(LOCTEXT("AddSourceGeneratorCaption", "Generators"));

	Menu.Item(LOCTEXT("AddCracksSource", "Cracks"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Cracks); }));
	Menu.Item(LOCTEXT("AddRockFormationSource", "Rock Formation"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::RockFormation); }));
	Menu.Item(LOCTEXT("AddPebblesSource", "Pebbles"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::Pebbles); }));
	Menu.Item(LOCTEXT("AddCliffStrataSource", "Cliff Strata"), MixtormatIcons::Generator(),
		FSimpleDelegate::CreateLambda([this]() { AddSource(EMixtormatGeneratorType::CliffStrata); }));
	Menu.Separator();
	Menu.Item(LOCTEXT("PasteCopiedSource", "Paste Copied Source"), MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]()
		{
			if (!bHasWorkingMaterial || !SourceClipboard.IsSet()) return;
			const FText ClipboardName = SourceClipboard->DisplayName;
			const FGuid NewSourceId = AppendSourceCopy(SourceClipboard.GetValue());
			if (!NewSourceId.IsValid()) { return; }
			if (FMixtormatSourceEntry* Copy = WorkingSources.FindByPredicate(
				[NewSourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == NewSourceId; }))
			{
				Copy->DisplayName = ClipboardName;
			}
			CommitSourcesEdit(NewSourceId);
		})).Enabled(SourceClipboard.IsSet()).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Paste));
	Menu.Item(LOCTEXT("PasteSourceInstance", "Paste as Instance"), MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this]()
		{
			// A link, not a copy: the instance shares the clipboard producer and follows it.
			if (!bHasWorkingMaterial || !SourceInstanceClipboard.IsSet()) return;
			const FGuid NewSourceId = AppendSourceInstance(SourceInstanceClipboard.GetValue());
			if (NewSourceId.IsValid()) { CommitSourcesEdit(NewSourceId); }
		})).Enabled(SourceInstanceClipboard.IsSet()
			&& MixtormatOutputReferences::ResolveShelfProducerEntry(WorkingSources, SourceInstanceClipboard.GetValue()) != nullptr);
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildSourceContextMenu(const FGuid SourceId)
{
	MixtormatMenu::FBuilder Menu;
	// Same shape as every other row menu in the stack: Duplicate / Copy / Copy as Instance first,
	// then the instance-only actions, then Rename, then Remove last and destructive. A source is
	// document data rather than a stack row, but it is reached by the same right-click habit, so
	// it reads the same way.
	Menu.Item(LOCTEXT("DuplicateSource", "Duplicate"), MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, SourceId]()
		{
			if (const FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
				[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; }))
			{
				const FText SourceName = Source->DisplayName;
				const FGuid NewSourceId = AppendSourceCopy(*Source);
				if (!NewSourceId.IsValid()) { return; }
				if (FMixtormatSourceEntry* Copy = WorkingSources.FindByPredicate(
					[NewSourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == NewSourceId; }))
				{
					Copy->DisplayName = SourceName;
				}
				CommitSourcesEdit(NewSourceId);
			}
		}));
	Menu.Item(LOCTEXT("CopyShelfSource", "Copy"), MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, SourceId]()
		{
			if (const FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
				[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; }))
			{
				SourceClipboard = *Source;
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Copy));
	Menu.Item(LOCTEXT("CopyShelfSourceAsInstance", "Copy as Instance"), MixtormatIcons::Duplicate(),
		FSimpleDelegate::CreateLambda([this, SourceId]()
		{
			// The link always names the producer: copying an instance records its original, so a
			// chain of instances can never form.
			if (const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
				WorkingSources, SourceId))
			{
				SourceInstanceClipboard = Producer->SourceId;
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::CopyAsInstance));
	// Instance-only operations, so a producer's menu stays exactly as it was.
	if (const FMixtormatSourceEntry* Source = WorkingSources.FindByPredicate(
		[SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; }))
	{
		if (Source->IsInstance())
		{
			const FGuid ProducerId = MixtormatOutputReferences::ResolveShelfProducerId(WorkingSources, SourceId);
			Menu.Separator();
			Menu.Item(LOCTEXT("GoToSourceInstance", "Go to Original"), nullptr,
				FSimpleDelegate::CreateLambda([this, ProducerId]() { SelectSource(ProducerId); }))
				.Enabled(ProducerId.IsValid());
			Menu.Item(LOCTEXT("BreakSourceInstance", "Break Instance"), nullptr,
				FSimpleDelegate::CreateLambda([this, SourceId]()
				{
					// Keeps the values it is showing as this entry's own: the payload is grafted from
					// the original while the entry's identity -- and every reference to it -- stays.
					if (BreakSourceInstance(SourceId)) { CommitSourcesEdit(SourceId); }
				})).Enabled(ProducerId.IsValid());
		}
	}
	Menu.Separator();
	Menu.Item(LOCTEXT("RenameSource", "Rename"), nullptr,
		FSimpleDelegate::CreateLambda([this, SourceId]()
		{
			SelectSource(SourceId);
			if (const TWeakPtr<SMixtormatSourceRow>* WeakRow = SourceRowWidgets.Find(SourceId))
			{
				if (const TSharedPtr<SMixtormatSourceRow> Row = WeakRow->Pin()) { Row->BeginRename(); }
			}
		})).Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Rename));
	Menu.Item(
		// The verb every other menu uses for this row. Deletion semantics are unchanged: this
		// still removes the source and repairs every binding that named it.
		LOCTEXT("RemoveSource", "Remove"),
		MixtormatIcons::Trash(),
		// The action returns FReply; the menu delegate takes void.
		FSimpleDelegate::CreateLambda([this, SourceId]() { DeleteSource(SourceId); }))
		.Shortcut(MixtormatLayerShortcuts::Label(MixtormatLayerShortcuts::EAction::Remove))
		.Destructive();
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

	Entry.DisplayName = MixtormatLayersPrivate::MakeGenericDefaultName(TEXT("Source"), 0, WorkingSources);

	const FGuid NewSourceId = Entry.SourceId;
	CommitSourcesEdit(NewSourceId);
	if (AddSourceAnchor.IsValid())
	{
		AddSourceAnchor->SetIsOpen(false);
	}
	return FReply::Handled();
}

FReply SMixtormat::AddMaskSource()
{
	if (!bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	// A Mask source is the coverage-producing Noise family: its Value publishes as Scalar01,
	// which the mask pipeline consumes as coverage directly rather than through the
	// signed-to-0..1 conversion. Everything else about it is a regular Noise source.
	const auto IsMaskSource = [](const FMixtormatSourceEntry& Candidate)
	{
		return !Candidate.IsInstance()
			&& Candidate.Child.Type == EMixtormatLayerChildType::Generator
			&& Candidate.Child.Generator.Type == EMixtormatGeneratorType::Noise
			&& MixtormatOutputReferences::NoiseValueKind(Candidate.Child.Generator.Noise.NoiseType)
				== EMixtormatPublishedFieldKind::Scalar01;
	};

	FMixtormatSourceEntry& Entry = WorkingSources.AddDefaulted_GetRef();
	ApplyChildCreationDefaults(Entry.Child, CreationKindForGenerator(EMixtormatGeneratorType::Noise));
	Entry.Child.ScopeOwnerChildId.Invalidate();
	Entry.Child.Generator.Noise.NoiseType = EMixtormatNoiseType::WorleyF1;

	Entry.DisplayName = MixtormatLayersPrivate::MakeGenericDefaultName(TEXT("Source"), 0, WorkingSources);

	const FGuid NewSourceId = Entry.SourceId;
	CommitSourcesEdit(NewSourceId);
	if (AddSourceAnchor.IsValid())
	{
		AddSourceAnchor->SetIsOpen(false);
	}
	return FReply::Handled();
}

void SMixtormat::CommitSourcesEdit(const FGuid& SelectSourceId)
{
	if (SelectSourceId.IsValid()) { SelectSource(SelectSourceId); }
	RecordEditHistory();
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	RebuildSourcesList();
}

FGuid SMixtormat::AppendSourceCopy(const FMixtormatSourceEntry& Source)
{
	// What a source shows is what gets copied: a linked instance keeps its payload on the original,
	// so the copy is made from the resolved producer rather than the link's shell. A broken link
	// has nothing to copy and reports no id rather than duplicating an empty entry.
	const FMixtormatSourceEntry* Payload = MixtormatOutputReferences::ResolveShelfProducerEntry(
		WorkingSources, Source.SourceId);
	if (!Payload) { return FGuid(); }
	FMixtormatSourceEntry Copy = MakeIndependentSourceCopy(*Payload);
	Copy.DisplayName = Source.DisplayName;
	const FGuid NewSourceId = Copy.SourceId;
	WorkingSources.Add(MoveTemp(Copy));
	return NewSourceId;
}

FGuid SMixtormat::AppendSourceInstance(const FGuid& ProducerSourceId)
{
	const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
		WorkingSources, ProducerSourceId);
	if (!Producer) { return FGuid(); }
	const FGuid NewProducerId = Producer->SourceId;
	const FText ProducerName = Producer->DisplayName;

	FMixtormatSourceEntry& Entry = WorkingSources.AddDefaulted_GetRef();
	// Addressing only: a fresh ChildId and the generator child type, so every reference menu
	// addresses the instance as itself. The payload stays a default -- evaluation follows the
	// original, and a snapshot here could only ever drift from it.
	Entry.Child.Type = EMixtormatLayerChildType::Generator;
	Entry.Child.ChildId = FGuid::NewGuid();
	Entry.Child.ScopeOwnerChildId.Invalidate();
	Entry.InstanceOfSourceId = NewProducerId;
	Entry.DisplayName = ProducerName;
	return Entry.SourceId;
}

bool SMixtormat::BreakSourceInstance(const FGuid& SourceId)
{
	FMixtormatSourceEntry* Entry = WorkingSources.FindByPredicate(
		[&SourceId](const FMixtormatSourceEntry& Candidate) { return Candidate.SourceId == SourceId; });
	if (!Entry || !Entry->IsInstance()) { return false; }
	const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
		WorkingSources, SourceId);
	if (!Producer) { return false; }

	// Graft the producer's payload onto the instance while the instance keeps its own addressing,
	// so every reference that names it keeps resolving and starts reading the new payload. The
	// root ChildId is preserved; owned children get fresh ids with their internal references
	// remapped in the same pass.
	FMixtormatSourceEntry Graft;
	Graft.SourceId = Entry->SourceId;
	Graft.DisplayName = Entry->DisplayName;
	Graft.Child.ChildId = Entry->Child.ChildId;
	Graft.OwnedChildren.SetNum(Producer->OwnedChildren.Num());
	TMap<FGuid, FGuid> ChildIdRemap;
	ChildIdRemap.Add(Producer->Child.ChildId, Graft.Child.ChildId);
	for (int32 Index = 0; Index < Producer->OwnedChildren.Num(); ++Index)
	{
		Graft.OwnedChildren[Index].ChildId = FGuid::NewGuid();
		ChildIdRemap.Add(Producer->OwnedChildren[Index].ChildId, Graft.OwnedChildren[Index].ChildId);
	}
	CopySourcePayload(*Producer, Graft, ChildIdRemap, Producer->SourceId);
	*Entry = MoveTemp(Graft);
	return true;
}

bool SMixtormat::LocalizeShelfLink(const FGuid& SourceShelfId, FGuid& OutSourceId, FGuid& OutChildId)
{
	const FMixtormatSourceEntry* Producer = MixtormatOutputReferences::ResolveShelfProducerEntry(
		WorkingSources, SourceShelfId);
	if (!Producer) { return false; }
	FMixtormatSourceEntry Copy = MakeIndependentSourceCopy(*Producer);
	Copy.DisplayName = Producer->DisplayName;
	OutSourceId = Copy.SourceId;
	OutChildId = Copy.Child.ChildId;
	WorkingSources.Add(MoveTemp(Copy));
	return true;
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
