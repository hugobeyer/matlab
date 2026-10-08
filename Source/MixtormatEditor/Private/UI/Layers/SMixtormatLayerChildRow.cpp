// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerChildRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "UI/Layers/SMixtormatLayerSurface.h"

#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerChildRow::Construct(const FArguments& InArgs)
{
	bSelected = InArgs._bSelected;
	bInstanceSource = InArgs._bInstanceSource;
	OnSelected = InArgs._OnSelected;
	OnRowDragDetected = InArgs._OnDragDetected;
	OnGetContextMenu = InArgs._OnGetContextMenu;
	OnGetOutputMenu = InArgs._OnGetOutputMenu;
	bHasOutputMenu = InArgs._bHasOutputMenu;

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle NameTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerName),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const FTextBlockStyle SourceTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerSource),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const Mixtormat::FMixtormatLayerMetrics& Layout = Resolved.LayerLayout;

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SMixtormatLayerChildRow::BuildContextMenuContent))
		[
			// Horizontal, not vertical: children stay dark at the left and lift toward the right,
			// so they remain subordinate to the owning layer's top-to-bottom gradient.
			SNew(SMixtormatLayerSurface)
			.Kind(Mixtormat::EMixtormatLayerKind::Child)
			.bSelected(bSelected)
			.bInstanceSource(bInstanceSource)
			.bHovered_Lambda([this]() { return IsHovered(); })
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
						.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.Padding(FMargin(MixtormatTokens::LayerSourceBarWidth + MixtormatTokens::StructuralLinkHighlightGap, 0.0f, 0.0f, 0.0f))
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::StructuralLinkHighlightWidth)
						.Visibility_Lambda([Role = InArgs._StructuralHighlightRole]()
						{
							return Role.Get(EStructuralLinkHighlightRole::None) == EStructuralLinkHighlightRole::None
								? EVisibility::Collapsed : EVisibility::HitTestInvisible;
						})
						[
							SNew(SImage)
							.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
							.ColorAndOpacity_Lambda([Role = InArgs._StructuralHighlightRole]()
							{
								const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
								const EStructuralLinkHighlightRole Value = Role.Get(EStructuralLinkHighlightRole::None);
								if (Value == EStructuralLinkHighlightRole::Source)
								{
									return FSlateColor(Palette.Get(Mixtormat::EMixtormatColorRole::Accent));
								}
								if (Value == EStructuralLinkHighlightRole::Target)
								{
									return FSlateColor(Palette.Get(Mixtormat::EMixtormatColorRole::Modified));
								}
								return FSlateColor(FLinearColor::LerpUsingHSV(
									Palette.Get(Mixtormat::EMixtormatColorRole::Accent),
									Palette.Get(Mixtormat::EMixtormatColorRole::Modified), 0.5f));
							})
						]
					]
					+ SOverlay::Slot()
					[
					SNew(SBox)
				.HeightOverride(Layout.ChildRowHeight)
				// Same leading/trailing insets as a layer row, plus the child indent, so both
				// follow the Leading/Trailing inset tokens together.
				.Padding(FMargin(
					Layout.PaddingX + Layout.ChildIndent + InArgs._ExtraIndent,
					0.0f,
					Layout.PaddingX,
					0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(SMixtormatLayerIcon)
						.bVisibility(true)
						.bOn(InArgs._bActive)
						.bActive(InArgs._bPreviewing)
						.ToolTipText(LOCTEXT("ChildToggleHint", "Enable or disable this child."))
						.OnClicked(InArgs._OnToggleActive)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(FMixtormatThemeStore::GetResolved().ControlLayout.LayerChildIconSize)
						.HeightOverride(FMixtormatThemeStore::GetResolved().ControlLayout.LayerChildIconSize)
						[
							InArgs._Icon.Widget
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(NameTextStyle.Font)
						.ColorAndOpacity(NameTextStyle.ColorAndOpacity)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text(InArgs._Name)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(Layout.ItemGap, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(STextBlock)
						.Font(SourceTextStyle.Font)
						.ColorAndOpacity(SourceTextStyle.ColorAndOpacity)
						.Text(InArgs._StructuralCount)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Visibility_Lambda([Count = InArgs._StructuralCount]()
						{
							return Count.Get(FText::GetEmpty()).IsEmpty()
								? EVisibility::Collapsed : EVisibility::Visible;
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(Layout.ItemGap, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([Link = InArgs._StructuralLink.Widget]()
						{
							return Link == SNullWidget::NullWidget ? EVisibility::Collapsed : EVisibility::Visible;
						})
						[
							InArgs._StructuralLink.Widget
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(
						Layout.ItemGap,
						0.0f,
						Layout.ItemGap,
						0.0f)
					[
						SNew(STextBlock)
						.Font(SourceTextStyle.Font)
						.ColorAndOpacity(SourceTextStyle.ColorAndOpacity)
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

				]
				]
			]
		]
	];
}


FReply SMixtormatLayerChildRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Select first, so the menu is built against the child it opened on.
		OnSelected.ExecuteIfBound();
		// Shift is an accelerator onto the published outputs, never the only way to reach them:
		// the full menu still nests the very same content under "Outputs". A child with nothing
		// to publish keeps the normal menu rather than opening an empty popup.
		bOutputMenuOnly = MouseEvent.IsShiftDown() && bHasOutputMenu.Get(false) && OnGetOutputMenu.IsBound();
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

TSharedRef<SWidget> SMixtormatLayerChildRow::BuildContextMenuContent()
{
	// Shift + right button asked for the published outputs directly; everything else gets the full
	// menu, which nests the very same output content under "Outputs".
	if (bOutputMenuOnly && OnGetOutputMenu.IsBound())
	{
		return OnGetOutputMenu.Execute();
	}
	return OnGetContextMenu.Execute();
}

#undef LOCTEXT_NAMESPACE
