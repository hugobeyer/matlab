// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerGroupRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerGroupRow::Construct(const FArguments& InArgs)
{
	bGroupEnabled = InArgs._bEnabled;
	bExpanded = InArgs._bExpanded;
	bSelected = InArgs._bSelected;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	Name = InArgs._Name;
	OnNameCommitted = InArgs._OnNameCommitted;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const TAttribute<int32> MemberCount = InArgs._MemberCount;

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatLayerGroupRow::GetBackgroundStart)
			.EndColor(this, &SMixtormatLayerGroupRow::GetBackgroundEnd)
			.Orientation(Orient_Vertical)
			.CornerRadius(MixtormatTokens::CornerRadius)
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::LayerGroupRowHeight)
				.Padding(FMargin(
					MixtormatTokens::LayerRowInsetLeading,
					0.0f,
					MixtormatTokens::LayerRowInsetTrailing,
					0.0f))
				[
					SNew(SHorizontalBox)

					// The group's eye hides every member for the render without touching any
					// member's own eye, so switching the group back on restores what the user set.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(SMixtormatIconButton)
						.Size(MixtormatTokens::LayerEyeSize)
						.Icon_Lambda([this]()
						{
							return bGroupEnabled.Get(true)
								? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
						})
						.ToolTipText(LOCTEXT(
							"GroupEyeHint",
							"Show or hide every layer in this group. Each layer keeps its own visibility."))
						.OnClicked(OnToggleEnabled)
					]

					// Where a layer carries its thumbnail. A folder at the same inset keeps the
					// two kinds of row sharing one left edge.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						// Eye-sized, not IconBrushSize: this row is child height, and a 20px glyph
						// between a 15px eye and a 14px chevron reads as the loudest thing on a
						// row whose whole job is to be quieter than a layer.
						SNew(SBox)
						.WidthOverride(MixtormatTokens::LayerEyeSize)
						.HeightOverride(MixtormatTokens::LayerEyeSize)
						[
							SNew(SImage)
							.Image(MixtormatIcons::Folder())
							.ColorAndOpacity(this, &SMixtormatLayerGroupRow::GetNameColor)
						]
					]

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
							.ColorAndOpacity(this, &SMixtormatLayerGroupRow::GetNameColor)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text(InArgs._Name)
						]
						+ SWidgetSwitcher::Slot()
						[
							SAssignNew(NameEditBox, SEditableTextBox)
							.SelectAllTextWhenFocused(true)
							.ClearKeyboardFocusOnCommit(true)
							.OnTextCommitted(this, &SMixtormatLayerGroupRow::HandleNameCommitted)
						]
					]

					// Subdued and unlabelled: it sits where a layer's source text sits, and a
					// bare number there reads as a count without needing the word.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(STextBlock)
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
						.Text_Lambda([MemberCount]()
						{
							return FText::AsNumber(MemberCount.Get(0));
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SMixtormatIconButton)
						.Size(MixtormatTokens::ChevronSize)
						.Icon_Lambda([this]()
						{
							return bExpanded.Get(true)
								? MixtormatIcons::ChevronDown()
								: MixtormatIcons::ChevronRight();
						})
						.OnClicked(OnToggleExpanded)
					]
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerGroupRow::GetBackgroundStart() const
{
	if (!bGroupEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenTop();
	}
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerSelectedTop();
	}
	return IsHovered() ? MixtormatPalette::LayerHoverTop() : MixtormatPalette::Panel();
}

FLinearColor SMixtormatLayerGroupRow::GetBackgroundEnd() const
{
	if (!bGroupEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenEnd();
	}
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerSelectedBottom();
	}
	return IsHovered() ? MixtormatPalette::LayerHoverBottom() : MixtormatPalette::PanelBottom();
}

FSlateColor SMixtormatLayerGroupRow::GetNameColor() const
{
	if (!bGroupEnabled.Get(true))
	{
		return FSlateColor(MixtormatPalette::DisabledText());
	}
	return FSlateColor(bSelected.Get(false) || IsHovered()
		? MixtormatPalette::RowText()
		: MixtormatPalette::LayerName());
}

void SMixtormatLayerGroupRow::BeginRename()
{
	if (!NameSwitcher.IsValid() || !NameEditBox.IsValid())
	{
		return;
	}
	NameEditBox->SetText(Name.Get(FText::GetEmpty()));
	NameSwitcher->SetActiveWidgetIndex(1);
	FSlateApplication::Get().SetKeyboardFocus(NameEditBox, EFocusCause::SetDirectly);
}

void SMixtormatLayerGroupRow::HandleNameCommitted(
	const FText& Text,
	const ETextCommit::Type CommitType)
{
	if (NameSwitcher.IsValid())
	{
		NameSwitcher->SetActiveWidgetIndex(0);
	}
	if (CommitType == ETextCommit::OnCleared)
	{
		return;
	}
	OnNameCommitted.ExecuteIfBound(Text, CommitType);
}

FReply SMixtormatLayerGroupRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
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
	// The body of the row is the drag handle, exactly as it is on a layer.
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatLayerGroupRow::OnDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

FReply SMixtormatLayerGroupRow::OnMouseButtonDoubleClick(
	const FGeometry&,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	// The same action the chevron performs, so the two ways in cannot disagree.
	OnSelected.ExecuteIfBound();
	OnToggleExpanded.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
