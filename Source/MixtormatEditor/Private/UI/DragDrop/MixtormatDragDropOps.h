// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// The drag operations the layer stack and the library speak in.
//
// Moved out of SMixtormatInternal.h unchanged: these are working behaviour, and the UI migration
// re-skins what carries them, not what they do. Each one is a payload plus the ghost that follows
// the cursor -- the ghost is built here rather than by the dragging widget so that a surface
// dragged from the gallery looks the same as one dragged from anywhere else.

#include "CoreMinimal.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

class FMixtormatSurfaceDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatSurfaceDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath SurfacePath;

	static TSharedRef<FMixtormatSurfaceDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InSurfacePath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatSurfaceDragDropOp> Operation =
			MakeShared<FMixtormatSurfaceDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->SurfacePath = InSurfacePath;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("DropSurfaceLayer", "Add {0} to Layers"),
			InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::ThumbnailGround))
			.Size(FVector2D(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(
				ThumbnailAsset,
				MixtormatTokens::DragGhostThumbnailResolution,
				MixtormatTokens::DragGhostThumbnailResolution,
				ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}

		Operation->DecoratorWidget =
			SNew(SBorder)
			.RenderOpacity(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostOpacity)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostPadding)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[SNew(SBox).WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize).HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(InDisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDragGhostLabel))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("CreateLayerGhost", "Create material layer")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
					]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

class FMixtormatMaskDragDropOp final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatMaskDragDropOp, FDecoratedDragDropOp)

	FText DisplayName;
	FSoftObjectPath MaskPath;

	static TSharedRef<FMixtormatMaskDragDropOp> New(
		const FText& InDisplayName,
		const FSoftObjectPath& InMaskPath,
		const FAssetData& ThumbnailAsset,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		TSharedRef<FMixtormatMaskDragDropOp> Operation = MakeShared<FMixtormatMaskDragDropOp>();
		Operation->DisplayName = InDisplayName;
		Operation->MaskPath = InMaskPath;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("AddMaskDrag", "Add {0} to a layer"), InDisplayName);

		TSharedRef<SWidget> ThumbnailWidget = SNew(SColorBlock)
			.Color(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::ThumbnailGround))
			.Size(FVector2D(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize));
		if (ThumbnailAsset.IsValid() && ThumbnailPool.IsValid())
		{
			Operation->DragThumbnail = MakeShared<FAssetThumbnail>(
				ThumbnailAsset,
				MixtormatTokens::DragGhostThumbnailResolution,
				MixtormatTokens::DragGhostThumbnailResolution,
				ThumbnailPool);
			ThumbnailWidget = Operation->DragThumbnail->MakeThumbnailWidget();
		}
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostOpacity)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostPadding)
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize).HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostThumbnailSize)[ThumbnailWidget]]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(InDisplayName)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<FAssetThumbnail> DragThumbnail;
	TSharedPtr<SWidget> DecoratorWidget;
};

// A target describes the existing route, not a second set of acceptance rules.
struct FMixtormatDragTarget
{
	enum class EAction : uint8
	{
		InsertLayer, InsertGroup, LayerIntoGroup,
		ChildToLayer, ChildToGroup, ReorderLayerChild, ReorderGroupChild,
		ReparentChild, IdGroupSource
	};
	EAction Action;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	int32 InsertIndex = INDEX_NONE;
	FGuid GroupId;
	FGuid OwnerId;
	FGuid ChildId;
};

// Only stack drags participate. The corner strip reads the active operation with its own
// workspace pointer; neither another workspace nor library/asset drags can supply its hint.
class FMixtormatGuidedDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatGuidedDragDropOp, FDecoratedDragDropOp)

	void BindGuidance(const TSharedRef<SWidget>& Workspace, TFunction<bool()> InPayloadIsCurrent,
		TFunction<bool(const FMixtormatDragTarget&, FText&)> InCanDrop)
	{
		GuidanceOwner = Workspace;
		PayloadIsCurrent = MoveTemp(InPayloadIsCurrent);
		CanDrop = MoveTemp(InCanDrop);
	}

	// A recognized stack drop owns its rejection too: never let a failed row drop
	// bubble into an ancestor that would reinterpret it as an append.
	template<typename DelegateType, typename... ArgTypes>
	FReply ExecuteDrop(const DelegateType& Handler, ArgTypes&&... Args)
	{
		const TSharedPtr<SWidget> Owner = GuidanceOwner.Pin();
		if (!bFinished && Owner && Handler.IsBoundToObject(Owner.Get()) && HasCurrentPayload())
		{
			const FReply Reply = Handler.Execute(Forward<ArgTypes>(Args)...);
			GuidanceTarget.Reset();
			return Reply.IsEventHandled() ? Reply : FReply::Handled();
		}
		GuidanceTarget.Reset();
		return FReply::Handled();
	}

	template<typename DelegateType>
	void SetGuidance(const TSharedRef<SWidget>& Target, const DelegateType& Handler,
		const FMixtormatDragTarget& Request, const FText& ActionText, bool bRouted = true)
	{
		const TSharedPtr<SWidget> Owner = GuidanceOwner.Pin();
		if (bFinished || !Owner || !Handler.IsBoundToObject(Owner.Get()) || !HasCurrentPayload())
		{
			GuidanceTarget.Reset();
			return;
		}
		GuidanceTarget = Target;
		GuidanceRequest = Request;
		GuidanceAction = ActionText;
		bTargetRouted = bRouted;
	}

	void ClearGuidance(const SWidget* Target)
	{
		// A bubbled leave from an outer wrapper must not clear the inner target's guidance.
		if (GuidanceTarget.Pin().Get() == Target) { GuidanceTarget.Reset(); }
	}

	bool HasGuidance(const SWidget* Workspace) const
	{
		return !bFinished && Workspace && GuidanceOwner.Pin().Get() == Workspace
			&& HasCurrentPayload() && GuidanceTarget.IsValid() && bool(CanDrop);
	}

	FText GetGuidance(const SWidget* Workspace) const
	{
		if (!HasGuidance(Workspace))
		{
			return FText::GetEmpty();
		}
		FText Reason;
		const bool bEligible = bTargetRouted && CanDrop(GuidanceRequest, Reason);
		// A preflight may also describe a no-op or a snapped group edge, without claiming success.
		const FText Text = !Reason.IsEmpty() ? Reason : bEligible ? GuidanceAction
			: LOCTEXT("DragPlacementBlocked", "Cannot drop here. Choose another position or owner.");
		return FText::Format(LOCTEXT("DragContextCancel", "{0} · Esc cancel"), Text);
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		bFinished = true;
		GuidanceTarget.Reset();
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

private:
	bool HasCurrentPayload() const
	{
		if (bPayloadStale || !PayloadIsCurrent || !PayloadIsCurrent())
		{
			bPayloadStale = true;
			GuidanceTarget.Reset();
			return false;
		}
		return true;
	}

	TWeakPtr<SWidget> GuidanceOwner;
	mutable TWeakPtr<SWidget> GuidanceTarget;
	mutable bool bPayloadStale = false;
	TFunction<bool()> PayloadIsCurrent;
	TFunction<bool(const FMixtormatDragTarget&, FText&)> CanDrop;
	FMixtormatDragTarget GuidanceRequest{FMixtormatDragTarget::EAction::InsertLayer};
	FText GuidanceAction;
	bool bTargetRouted = false;
	bool bFinished = false;
};

class FMixtormatChildDragDropOp final : public FMixtormatGuidedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatChildDragDropOp, FMixtormatGuidedDragDropOp)

	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	// Set instead of LayerIndex when the child came from a group's shared stack rather than a
	// layer -- the same INDEX_NONE-plus-a-valid-id convention ResolveChild uses.
	FGuid GroupId;
	// Whether this child may leave its layer at all. Decided once by the owner, which knows what
	// the child is; a target cannot work it out from the indices it was handed, and a target that
	// lights up for a drop that will then be refused is worse than one that stays dark.
	bool bCanLeaveLayer = true;
	FText Name;

	static TSharedRef<FMixtormatChildDragDropOp> New(
		const int32 InLayerIndex,
		const int32 InChildIndex,
		const FText& Name,
		const bool bInCanLeaveLayer = true)
	{
		TSharedRef<FMixtormatChildDragDropOp> Operation = MakeShared<FMixtormatChildDragDropOp>();
		Operation->LayerIndex = InLayerIndex;
		Operation->ChildIndex = InChildIndex;
		Operation->bCanLeaveLayer = bInCanLeaveLayer;
		Operation->Name = Name;
		Operation->DefaultHoverText = FText::Format(LOCTEXT("ReorderChildDrag", "Move {0}"), Name);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostOpacity)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostPadding, FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhost")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SImage).Image(MixtormatIcons::Grip())]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f)[SNew(STextBlock).Text(Name)]
				]
			];
		Operation->Construct();
		return Operation;
	}

	// A group's shared stack has no layer index; the source is named by GroupId instead.
	static TSharedRef<FMixtormatChildDragDropOp> NewFromGroup(
		const FGuid& InGroupId,
		const int32 InChildIndex,
		const FText& Name,
		const bool bInCanLeaveLayer = true)
	{
		TSharedRef<FMixtormatChildDragDropOp> Operation =
			New(INDEX_NONE, InChildIndex, Name, bInCanLeaveLayer);
		Operation->GroupId = InGroupId;
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

// A whole group on the move. Carries the GroupId rather than an index because the block it names
// is found by membership, and every index in the stack shifts the moment it lands.
class FMixtormatGroupDragDropOp final : public FMixtormatGuidedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatGroupDragDropOp, FMixtormatGuidedDragDropOp)

	FGuid GroupId;

	static TSharedRef<FMixtormatGroupDragDropOp> New(
		const FGuid& InGroupId,
		const FText& DisplayName)
	{
		TSharedRef<FMixtormatGroupDragDropOp> Operation =
			MakeShared<FMixtormatGroupDragDropOp>();
		Operation->GroupId = InGroupId;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("MoveGroupDrag", "Move {0}"), DisplayName);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostOpacity)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostPadding, FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
			[
				SNew(STextBlock).Text(DisplayName)
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

class FMixtormatLayerDragDropOp final : public FMixtormatGuidedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMixtormatLayerDragDropOp, FMixtormatGuidedDragDropOp)

	int32 SourceLayerIndex = INDEX_NONE;

	static TSharedRef<FMixtormatLayerDragDropOp> New(
		const int32 InSourceLayerIndex,
		const FText& DisplayName)
	{
		TSharedRef<FMixtormatLayerDragDropOp> Operation =
			MakeShared<FMixtormatLayerDragDropOp>();
		Operation->SourceLayerIndex = InSourceLayerIndex;
		Operation->DefaultHoverText = FText::Format(
			LOCTEXT("MoveLayerDrag", "Move {0}"),
			DisplayName);
		Operation->DecoratorWidget = SNew(SBorder)
			.RenderOpacity(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostOpacity)
			.Padding(FMargin(
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + MixtormatTokens::DragGhostShadowOffsetX,
				FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowInset + FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.PanelShadow")))
			[
				SNew(SBorder)
				.Padding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostPadding, FMixtormatThemeStore::GetResolved().ControlLayout.DragGhostShadowOffsetY))
				.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.DragGhostAccent")))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(MixtormatIcons::Grip())]
					+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::DragGhostTextGap, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(DisplayName).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontDragGhostLabel))]
				]
			];
		Operation->Construct();
		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return DecoratorWidget; }

private:
	TSharedPtr<SWidget> DecoratorWidget;
};

#undef LOCTEXT_NAMESPACE
