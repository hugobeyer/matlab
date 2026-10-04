// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
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

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const bool bCanDisable = InArgs._bCanDisable;
	const TAttribute<bool> bSolo = InArgs._bSolo;

	TSharedRef<SWidget> Eye = SNew(SMixtormatLayerIcon)
		.Size(MixtormatTokens::LayerEyeSize)
		.bVisibility(true)
		.bOn(bLayerEnabled)
		.bActive(bSolo)
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
			SNew(SMixtormatLayerSurface)
			.StartColor(this, &SMixtormatLayerRow::GetBackgroundStart)
			.EndColor(this, &SMixtormatLayerRow::GetBackgroundEnd)
			.bSelected(bSelected)
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
						.ColorAndOpacity(FSlateColor(MixtormatPalette::AccentBright()))
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
								.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
								.ColorAndOpacity(this, &SMixtormatLayerRow::GetNameColor)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text(InArgs._Name)
							]
							+ SWidgetSwitcher::Slot()
							[
								SAssignNew(NameEditBox, SEditableTextBox)
								.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& KeyEvent)
								{
									return NameEntry.IsValid() ? NameEntry->HandleKeyDown(Geometry, KeyEvent) : FReply::Unhandled();
								})
								.SelectAllTextWhenFocused(true)
								.ClearKeyboardFocusOnCommit(true)
								.OnTextCommitted(this, &SMixtormatLayerRow::HandleNameCommitted)
							]
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
						.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
						[
							// FoldoutIconSize, not ChevronSize: the disclosure's glyph is the
							// foldout role, and SMixtormatLayerIcon adds its hit slop to this on
							// top, so the target stays wider than the square glyph box.
							SNew(SMixtormatLayerIcon)
							.Size(MixtormatTokens::FoldoutIconSize)
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

FLinearColor SMixtormatLayerRow::GetBackgroundStart() const
{
	if (bReference.Get(false))
	{
		const float TintAmount = !bLayerEnabled.Get(true) ? 0.08f
			: bSelected.Get(false) ? 0.38f : IsHovered() ? 0.30f : 0.22f;
		return FMath::Lerp(MixtormatPalette::Panel(), MixtormatPalette::Modified(), TintAmount);
	}
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
