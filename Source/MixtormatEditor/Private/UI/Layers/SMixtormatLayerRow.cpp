// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Layers/SMixtormatLayerSurface.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Controls/MixtormatEntryCommit.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerRow::Construct(const FArguments& InArgs)
{
	bLayerEnabled = InArgs._bEnabled;
	bExpanded = InArgs._bExpanded;
	bSelected = InArgs._bSelected;
	bReference = InArgs._bReference;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	OnToggleSolo = InArgs._OnToggleSolo;
	OnRowDragDetected = InArgs._OnDragDetected;
	EditableName = InArgs._EditableName;
	OnNameCommitted = InArgs._OnNameCommitted;

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle NameTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerName),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const FTextBlockStyle SourceTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerSource),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const Mixtormat::FMixtormatLayerMetrics& Layout = Resolved.LayerLayout;
	const bool bCanDisable = InArgs._bCanDisable;
	const TAttribute<bool> bSolo = InArgs._bSolo;

	TSharedRef<SWidget> Eye = SNew(SMixtormatLayerIcon)
		.bVisibility(true)
		.bOn(bLayerEnabled)
		.bActive(bSolo)
		.ToolTipText(bCanDisable
			? LOCTEXT("LayerVisToggleHint", "Show or hide this layer. Ctrl or Alt click to solo it.")
			: LOCTEXT("LayerVisToggleLockedHint", "This layer's visibility is locked."))
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
			SNew(SMixtormatLayerSurface)
			.bSelected(bSelected)
			.bVisible(bLayerEnabled)
			.bReference(bReference)
			.bHovered_Lambda([this]() { return IsHovered(); })
			[
				// The surface paints its lip and glow without adding layout or hit-test slots.
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(SBox)
					.WidthOverride(MixtormatTokens::LayerSourceBarWidth)
					.Visibility_Lambda([bHolds = InArgs._bHoldsInstanceSource]()
					{
						return bHolds.Get(false) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
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
					.Visibility_Lambda([bStructuralSource = InArgs._bStructuralSource]()
					{
						return bStructuralSource.Get(false)
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.HeightOverride(Layout.RowHeight)
					.Padding(FMargin(
						Layout.PaddingX,
						0.0f,
						Layout.PaddingX,
						0.0f))
					[
						SNew(SHorizontalBox)

						// Eye. Its own toggle, so clicking it never also selects the layer.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
						[
							Eye
						]

						// Thumbnail: no border, so the image reads as the surface itself.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(Layout.ThumbnailSize)
							.HeightOverride(Layout.ThumbnailSize)
							[
								InArgs._Thumbnail.Widget
							]
						]

						// Name grows; source is right-aligned beside it so the two form columns.
						// A switcher rather than SInlineEditableTextBlock: that widget enters
						// editing on double-click, and double-click on this row opens and shuts
						// the layer. Rename is F2 (or F12) and the context menu, and only those.
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(MixtormatTokens::LayerNameInset, 0.0f, 0.0f, 0.0f)
						[
							SAssignNew(NameSwitcher, SWidgetSwitcher)
							+ SWidgetSwitcher::Slot()
							[
								SNew(STextBlock)
								.Font(NameTextStyle.Font)
								.ColorAndOpacity(this, &SMixtormatLayerRow::GetNameColor)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text(InArgs._Name)
							]
							+ SWidgetSwitcher::Slot()
							[
								SNew(SMixtormatTextFieldGradient)
								[
									SNew(SBox)
									.MinDesiredHeight(FMixtormatThemeStore::GetTheme().TextField.MinHeight)
									.MinDesiredWidth(FMixtormatThemeStore::GetTheme().TextField.MinWidth)
									[
										SAssignNew(NameEditBox, SEditableTextBox)
										.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
										.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& KeyEvent)
										{
											return NameEntry.IsValid() ? NameEntry->HandleKeyDown(Geometry, KeyEvent) : FReply::Unhandled();
										})
										.SelectAllTextWhenFocused(true)
										.ClearKeyboardFocusOnCommit(true)
										.OnTextCommitted(this, &SMixtormatLayerRow::HandleNameCommitted)
									]
								]
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
							.ColorAndOpacity_Lambda([Enabled = bLayerEnabled, Base = SourceTextStyle.ColorAndOpacity]()
							{ FLinearColor Color = Base.GetSpecifiedColor(); if (!Enabled.Get(true)) Color.A *= 0.45f; return FSlateColor(Color); })
							.Text(InArgs._Source)
						]

						// Colour blend first, composition second, so the composition badge stays
						// flush against the disclosure and the column down the right edge holds
						// its line whether or not a layer carries a colour mode.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
						[
							SNew(SMixtormatBadge)
							.Text(InArgs._ColorBadge)
							.OnGetMenuContent(InArgs._OnGetColorBadgeMenu)
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
							SNew(SMixtormatBadge)
							.Text(InArgs._Badge)
							.OnGetMenuContent(InArgs._OnGetBadgeMenu)
						]

						// Disclosure last, so the badge column stays flush against it. It is an
						// icon button rather than part of the row body: a click here must open the
						// layer, not start dragging it.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(Layout.ItemGap, 0.0f, 0.0f, 0.0f)
						[
							SNew(SMixtormatLayerIcon)
							.IsEnabled(InArgs._bHasChildren)
							.Icon_Lambda([this, bHasChildren = InArgs._bHasChildren]()
							{
								return bExpanded.Get(false) && bHasChildren.Get(true)
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


FSlateColor SMixtormatLayerRow::GetNameColor() const
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const Mixtormat::EMixtormatColorRole Role = !bLayerEnabled.Get(true)
		? Mixtormat::EMixtormatColorRole::TextMuted
		: bSelected.Get(false) || IsHovered()
			? Mixtormat::EMixtormatColorRole::Text : Mixtormat::EMixtormatColorRole::TextMuted;
	FLinearColor Color = Resolved.Palette.Get(Role);
	Color.A *= Mixtormat::FMixtormatTypography::GetSpec(
		Resolved.Typography, Mixtormat::EMixtormatTextRole::LayerName).Opacity;
	if (!bLayerEnabled.Get(true)) { Color.A *= 0.45f; }
	return FSlateColor(Color);
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

void SMixtormatLayerRow::BeginRename()
{
	if (!NameSwitcher.IsValid() || !NameEditBox.IsValid())
	{
		return;
	}
	NameEditBox->SetText(EditableName.Get(FText::GetEmpty()));
	NameSwitcher->SetActiveWidgetIndex(1);
	// Without this the box appears and the keystrokes go on reaching the panel, so F2 looks like
	// it did nothing.
	FSlateApplication::Get().SetKeyboardFocus(NameEditBox, EFocusCause::SetDirectly);
	if (!NameEntry.IsValid())
	{
		NameEntry = MakeShared<FMixtormatEntryCommit>();
	}
	const TWeakPtr<SMixtormatLayerRow> WeakSelf = StaticCastSharedRef<SMixtormatLayerRow>(AsShared());
	NameEntry->Begin(NameEditBox.ToSharedRef(), [WeakSelf]()
	{
		const TSharedPtr<SMixtormatLayerRow> Self = WeakSelf.Pin();
		if (Self.IsValid() && Self->NameSwitcher.IsValid())
		{
			Self->NameSwitcher->SetActiveWidgetIndex(0);
		}
	});
}

void SMixtormatLayerRow::HandleNameCommitted(const FText& Text, const ETextCommit::Type CommitType)
{
	// Only an explicit Escape or right click discards. Every other commit -- Enter, Tab, focus
	// moving or clearing (click elsewhere, leaving the window) -- keeps what was typed.
	const bool bCancelled = NameEntry.IsValid() && NameEntry->Finish();
	if (NameSwitcher.IsValid())
	{
		NameSwitcher->SetActiveWidgetIndex(0);
	}
	if (bCancelled)
	{
		return;
	}
	OnNameCommitted.ExecuteIfBound(Text, CommitType);
}

FReply SMixtormatLayerRow::OnMouseButtonDoubleClick(
	const FGeometry&,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	OnSelected.ExecuteIfBound();
	OnToggleExpanded.ExecuteIfBound();
	return FReply::Handled();
}

FReply SMixtormatLayerRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
