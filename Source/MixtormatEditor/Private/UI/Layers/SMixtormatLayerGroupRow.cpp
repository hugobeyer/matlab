// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerGroupRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Layers/SMixtormatLayerSurface.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UI/Controls/MixtormatEntryCommit.h"
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
	AccentColor = InArgs._AccentColor;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	Name = InArgs._Name;
	OnNameCommitted = InArgs._OnNameCommitted;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatLayerMetrics& Layout = FMixtormatThemeStore::GetResolved().LayerLayout;
	const TAttribute<int32> MemberCount = InArgs._MemberCount;

	// A group row's title is its own size and weight, so it is built by the typography system
	// rather than by patching a registered style's font. This used to copy Mixtormat.LayerName and
	// overwrite TypefaceFontName, which only happened to work because that style's composite font
	// happened to define "Regular" and "Bold" -- it bypassed the weight model entirely, so a
	// SemiBold group title could not have been expressed at all.
	FSlateFontInfo GroupFont = Mixtormat::FMixtormatTypography::MakeFont(
		Mixtormat::FMixtormatTypography::FromCssWeight(MixtormatTokens::LayerGroupTitleWeight),
		MixtormatTokens::LayerGroupTitleSize);

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SMixtormatLayerSurface)
			.Kind(Mixtormat::EMixtormatLayerKind::Group)
			.GroupTint(AccentColor)
			.bVisible(bGroupEnabled)
			.bSelected(bSelected)
			.bHovered_Lambda([this]() { return IsHovered(); })
			[
				SNew(SBox)
				.HeightOverride(Layout.GroupRowHeight)
				.Padding(FMargin(
					Layout.PaddingX,
					0.0f,
					Layout.PaddingX,
					0.0f))
				[
					SNew(SHorizontalBox)

					// The group's eye hides every member for the render without touching any
					// member's own eye, so switching the group back on restores what the user set.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(SMixtormatLayerIcon)
						.bVisibility(true)
						.bOn(bGroupEnabled)
						.ToolTipText(LOCTEXT(
							"GroupEyeHint",
							"Show or hide every layer in this group. Each layer keeps its own visibility."))
						.OnClicked(OnToggleEnabled)
					]

					// Reserve the thumbnail column even though a group has no thumbnail.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, Layout.ItemGap, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(Layout.ThumbnailSize)
						.HAlign(HAlign_Right)
						[
							SNew(SBox)
							.WidthOverride(MixtormatTokens::LayerIconSize)
							.HeightOverride(MixtormatTokens::LayerIconSize)
							[
								SNew(SImage).Image(MixtormatIcons::Folder())
								.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(MixtormatTokens::LayerIconOpacity)))
							]
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
							.Font(GroupFont)
							.ColorAndOpacity(this, &SMixtormatLayerGroupRow::GetNameColor)
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
							.OnTextCommitted(this, &SMixtormatLayerGroupRow::HandleNameCommitted)
						]
					]

					// Subdued and unlabelled: it sits where a layer's source text sits, and a
					// bare number there reads as a count without needing the word.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(Layout.ItemGap, 0.0f, Layout.ItemGap, 0.0f)
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
						SNew(SMixtormatLayerIcon)
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


FSlateColor SMixtormatLayerGroupRow::GetNameColor() const
{
	const Mixtormat::FMixtormatResolvedPalette& Palette = FMixtormatThemeStore::GetResolved().Palette;
	if (!bGroupEnabled.Get(true))
	{
		return FSlateColor(Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	}
	return FSlateColor(Palette.Get(bSelected.Get(false) || IsHovered()
		? Mixtormat::EMixtormatColorRole::Text : Mixtormat::EMixtormatColorRole::TextMuted));
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
	if (!NameEntry.IsValid())
	{
		NameEntry = MakeShared<FMixtormatEntryCommit>();
	}
	const TWeakPtr<SMixtormatLayerGroupRow> WeakSelf = StaticCastSharedRef<SMixtormatLayerGroupRow>(AsShared());
	NameEntry->Begin(NameEditBox.ToSharedRef(), [WeakSelf]()
	{
		const TSharedPtr<SMixtormatLayerGroupRow> Self = WeakSelf.Pin();
		if (Self.IsValid() && Self->NameSwitcher.IsValid())
		{
			Self->NameSwitcher->SetActiveWidgetIndex(0);
		}
	});
}

void SMixtormatLayerGroupRow::HandleNameCommitted(
	const FText& Text,
	const ETextCommit::Type CommitType)
{
	// Only an explicit Escape or right click discards; see SMixtormatLayerRow.
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
