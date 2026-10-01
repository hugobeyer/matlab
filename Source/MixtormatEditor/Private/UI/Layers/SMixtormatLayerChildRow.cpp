// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerChildRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatStatusDot.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerChildRow::Construct(const FArguments& InArgs)
{
	bSelected = InArgs._bSelected;
	bInstanceSource = InArgs._bInstanceSource;
	OnSelected = InArgs._OnSelected;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			// Horizontal, not vertical: children stay dark at the left and lift toward the right,
			// so they remain subordinate to the owning layer's top-to-bottom gradient.
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatLayerChildRow::GetTintEnd)
			.EndColor(this, &SMixtormatLayerChildRow::GetTintStart)
			.Orientation(Orient_Horizontal)
			.CornerRadius(MixtormatTokens::CornerRadius)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				[
					// Beside the glow, not instead of it: a tint fades into the stack, a bar does not.
					SNew(SBox)
					.WidthOverride(MixtormatTokens::LayerSourceBarWidth)
					.Visibility_Lambda([bInstanceSource = InArgs._bInstanceSource]()
					{
						return bInstanceSource.Get(false) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.ColorAndOpacity(FSlateColor(MixtormatPalette::AccentBright()))
					]
				]
				+ SOverlay::Slot()
				[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::LayerChildRowHeight)
				// Same leading/trailing insets as a layer row, plus the child indent, so both
				// follow the Leading/Trailing inset tokens together.
				.Padding(FMargin(
					MixtormatTokens::LayerRowInsetLeading + MixtormatTokens::LayerChildIndent,
					0.0f,
					MixtormatTokens::LayerRowInsetTrailing,
					0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						// Dimmer than the glyph: it shows structure, not identity.
						SNew(SBox)
						.WidthOverride(MixtormatTokens::LayerChildIconSize)
						.HeightOverride(MixtormatTokens::LayerChildIconSize)
						.Visibility(InArgs._Connector ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(SImage)
							.Image(InArgs._Connector)
							.ColorAndOpacity(FSlateColor(MixtormatPalette::RowText().CopyWithNewOpacity(MixtormatTokens::LayerConnectorOpacity)))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::LayerChildIconSize)
						.HeightOverride(MixtormatTokens::LayerChildIconSize)
						[
							InArgs._Icon.Widget
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
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
						.Text(InArgs._Kind)
						// Same reason as the badge below: a child with no kind mark should not
						// spend the slot's padding saying nothing.
						.Visibility_Lambda([Kind = InArgs._Kind]()
						{
							return Kind.Get(FText::GetEmpty()).IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SMixtormatBadge)
						.Text(InArgs._Badge)
						.OnGetMenuContent(InArgs._OnGetBadgeMenu)
						// Collapsed rather than drawn empty. A badge is a fixed-width pill, so a
						// child whose slot has nothing to say -- an ID node, whose name already
						// says it -- would otherwise print a blank box down the column. The row's
						// height is pinned by the SBox above, so removing it changes only width.
						.Visibility_Lambda([Badge = InArgs._Badge]()
						{
							return Badge.Get(FText::GetEmpty()).IsEmpty()
								? EVisibility::Collapsed
								: EVisibility::Visible;
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
					[
						SNew(SMixtormatStatusDot)
						.Size(MixtormatTokens::StatusDotSize)
						.bFilled(InArgs._bActive)
						// Unnamed falls back to the filled/hollow pair, so the toggle state comes back
						// the moment the preview stops.
						.BrushName_Lambda([bPreviewing = InArgs._bPreviewing]()
						{
							return bPreviewing.Get(false)
								? FName(TEXT("Mixtormat.StatusDot.Previewing"))
								: NAME_None;
						})
						.ToolTip(LOCTEXT("ChildDotHint", "Enable or disable this child."))
						.OnClicked(InArgs._OnToggleActive)
					]
				]
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerChildRow::GetTintStart() const
{
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerChildSelectedRight();
	}
	if (bInstanceSource.Get(false))
	{
		// The selection's gradient mirrored (strong on the left) and softer, so the source reads
		// as linked to the selection without reading as a second selection.
		FLinearColor Glow = MixtormatPalette::Accent();
		Glow.A = 0.10f;
		return Glow;
	}
	return IsHovered() ? MixtormatPalette::LayerChildHoverRight() : FLinearColor::Transparent;
}

FLinearColor SMixtormatLayerChildRow::GetTintEnd() const
{
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerChildSelectedLeft();
	}
	if (bInstanceSource.Get(false))
	{
		FLinearColor Glow = MixtormatPalette::Accent();
		Glow.A = 0.55f;
		return Glow;
	}
	return IsHovered() ? MixtormatPalette::LayerChildHoverLeft() : FLinearColor::Transparent;
}

FReply SMixtormatLayerChildRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Select first, so the menu is built against the child it opened on.
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
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatLayerChildRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
