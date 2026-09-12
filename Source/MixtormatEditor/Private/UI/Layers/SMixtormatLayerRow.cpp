// Copyright 2026 Hugo Beyer. All Rights Reserved.

﻿#include "UI/Layers/SMixtormatLayerRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerRow::Construct(const FArguments& InArgs)
{
	bLayerEnabled = InArgs._bEnabled;
	bExpanded = InArgs._bExpanded;
	bSelected = InArgs._bSelected;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	OnToggleSolo = InArgs._OnToggleSolo;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const bool bCanDisable = InArgs._bCanDisable;
	const TAttribute<bool> bSolo = InArgs._bSolo;

	TSharedRef<SWidget> Eye = SNew(SMixtormatIconButton)
		.Size(MixtormatTokens::LayerEyeSize)
		.bActive(bSolo)
		.Icon_Lambda([this]()
		{
			return bLayerEnabled.Get(true) ? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
		})
		.ToolTipText(bCanDisable
			? LOCTEXT("LayerEyeHint", "Show or hide this layer. Ctrl or Alt click to solo it.")
			: LOCTEXT("LayerEyeLockedHint", "This layer's visibility is locked."))
		.OnClickedWithModifiers(bCanDisable
			? FOnMixtormatIconClicked::CreateSP(this, &SMixtormatLayerRow::HandleEyeClicked)
			: FOnMixtormatIconClicked());

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatLayerRow::GetBackgroundStart)
			.EndColor(this, &SMixtormatLayerRow::GetBackgroundEnd)
			.Orientation(Orient_Vertical)
			.CornerRadius(MixtormatTokens::CornerRadius)
			[
				// The additive lip along the top edge. It is the seam between one layer and the
				// one above it, and it is what makes the row read as the header of everything
				// indented under it rather than as another item in a flat list.
				//
				// Overlaid rather than stacked above the row: a hairline in its own slot would add
				// its height to every row, and the stack is already the tightest thing in the tool.
				SNew(SOverlay)
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.HeightOverride(MixtormatTokens::HairlineThickness)
					[
						SNew(SImage).Image(this, &SMixtormatLayerRow::GetHairlineBrush)
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.HeightOverride(MixtormatTokens::LayerRowHeight)
					.Padding(FMargin(
						MixtormatTokens::LayerRowInsetLeading,
						0.0f,
						MixtormatTokens::LayerRowInsetTrailing,
						0.0f))
					[
						SNew(SHorizontalBox)

						// Eye. Its own toggle, so clicking it never also selects the layer.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
						[
							Eye
						]

						// Thumbnail: no border, so the image reads as the surface itself.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::LayerThumbnailSize)
							.HeightOverride(MixtormatTokens::LayerThumbnailSize)
							[
								InArgs._Thumbnail.Widget
							]
						]

						// Name grows; source is right-aligned beside it so the two form columns.
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerNameInset, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
							.ColorAndOpacity(this, &SMixtormatLayerRow::GetNameColor)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text(InArgs._Name)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(
							MixtormatTokens::LayerItemGap,
							0.0f,
							MixtormatTokens::LayerItemGap,
							0.0f)
						[
							SNew(STextBlock)
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
							.Text(InArgs._Source)
						]

						// Colour blend first, composition second, so the composition badge stays
						// flush against the disclosure and the column down the right edge holds
						// its line whether or not a layer carries a colour mode.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
						[
							SNew(SMixtormatBadge)
							.Text(InArgs._ColorBadge)
							.Visibility_Lambda([ColorBadge = InArgs._ColorBadge]()
							{
								return ColorBadge.Get(FText::GetEmpty()).IsEmpty()
									? EVisibility::Collapsed : EVisibility::Visible;
							})
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SMixtormatBadge).Text(InArgs._Badge)
						]

						// Disclosure last, so the badge column stays flush against it. It is an
						// icon button rather than part of the row body: a click here must open the
						// layer, not start dragging it.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
						[
							SNew(SMixtormatIconButton)
							.Size(MixtormatTokens::ChevronSize)
							.Icon_Lambda([this]()
							{
								return bExpanded.Get(false)
									? MixtormatIcons::ChevronDown()
									: MixtormatIcons::ChevronRight();
							})
							.OnClicked(OnToggleExpanded)
						]
					]
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerRow::GetBackgroundStart() const
{
	if (!bLayerEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenTop();
	}
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerSelectedTop();
	}
	return IsHovered() ? MixtormatPalette::LayerHoverTop() : MixtormatPalette::Panel();
}

FLinearColor SMixtormatLayerRow::GetBackgroundEnd() const
{
	if (!bLayerEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenEnd();
	}
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerSelectedBottom();
	}
	return IsHovered() ? MixtormatPalette::LayerHoverBottom() : MixtormatPalette::PanelBottom();
}

const FSlateBrush* SMixtormatLayerRow::GetHairlineBrush() const
{
	return FMixtormatStyle::Get().GetBrush(bSelected.Get(false)
		? TEXT("Mixtormat.HeaderHairlineGlow")
		: TEXT("Mixtormat.HeaderHairline"));
}

FSlateColor SMixtormatLayerRow::GetNameColor() const
{
	if (!bLayerEnabled.Get(true))
	{
		return FSlateColor(MixtormatPalette::DisabledText());
	}
	return FSlateColor(bSelected.Get(false) || IsHovered()
		? MixtormatPalette::RowText()
		: MixtormatPalette::LayerName());
}

void SMixtormatLayerRow::HandleEyeClicked(const FPointerEvent& MouseEvent)
{
	// Solo is a modifier on the eye rather than a control of its own: it is the same question --
	// what is the preview showing -- and the row has no width to spare for a second button.
	if (MouseEvent.IsControlDown() || MouseEvent.IsAltDown())
	{
		OnToggleSolo.ExecuteIfBound();
		return;
	}
	OnToggleEnabled.ExecuteIfBound();
}

FReply SMixtormatLayerRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Select first: the menu is built for whichever layer it opened on, and every entry in it
		// acts on the selection.
		OnSelected.ExecuteIfBound();
		if (ContextAnchor.IsValid())
		{
			ContextAnchor->SetIsOpen(true);
		}
		return FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// The empty space of the row selects, and begins a drag. Clicking the row anywhere but on the
	// eye is how a layer is picked up, which is why the eye is a separate widget that handles its
	// own press.
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatLayerRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
