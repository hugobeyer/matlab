// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatStructuralSourcePicker.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/SMixtormatMenuItem.h"
#include "UI/Primitives/SMixtormatWellBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MixtormatStructuralSourcePicker"

void SMixtormatStructuralSourcePicker::Construct(const FArguments& InArgs)
{
	Entries = InArgs._Entries;
	OnSourcePicked = InArgs._OnSourcePicked;
	const auto& Layout = FMixtormatThemeStore::GetResolved().MenuLayout;
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(Layout.Width)
		.MaxDesiredHeight(MixtormatTokens::MaskPickerMaxHeight)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeCaption(InArgs._Caption)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(Layout.ItemInset, Layout.CaptionInsetBelow)
			[
				SNew(SMixtormatWellBox)
				[
					SAssignNew(Search, SEditableTextBox)
					.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.SearchBox")))
					.HintText(LOCTEXT("Search", "Search sources…"))
					.OnTextChanged_Lambda([this](const FText& Text)
					{
						Filter = Text.ToString().TrimStartAndEnd();
						RebuildList();
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeCaption(TAttribute<FText>::CreateLambda([this]()
				{
					return !EligibleEntries.IsEmpty() ? LOCTEXT("Sources", "Sources — available first")
						: Filter.IsEmpty() ? LOCTEXT("NoCompatibleSources", "No compatible sources. Unavailable sources explain why.")
						: LOCTEXT("NoCompatibleMatches", "No compatible sources match this search.");
				}))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(Scroll, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(Rows, SVerticalBox)
				]
			]
		];
	RebuildList();

	// The popup must be attached before Slate can resolve a focus path to its search field.
	const TWeakPtr<SMixtormatStructuralSourcePicker> WeakThis = SharedThis(this);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double, float)
	{
		if (const auto Picker = WeakThis.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(Picker->Search, EFocusCause::SetDirectly);
		}
		return EActiveTimerReturnType::Stop;
	}));
}

TSharedRef<SWidget> SMixtormatStructuralSourcePicker::MakeCaption(const TAttribute<FText>& Text) const
{
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const auto Style = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Caption),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
	const auto& Layout = Resolved.MenuLayout;
	return SNew(SBox)
		.Padding(FMargin(Layout.ItemInset, Layout.CaptionInsetAbove, Layout.ItemInset, Layout.CaptionInsetBelow))
		[
			SNew(STextBlock)
			.Font(Style.Font)
			.ColorAndOpacity(Style.ColorAndOpacity)
			.Text(Text)
			.AutoWrapText(true)
		];
}

void SMixtormatStructuralSourcePicker::RebuildList()
{
	Rows->ClearChildren();
	EligibleEntries.Reset();
	EligibleOffsets.Reset();
	ActiveEntry = INDEX_NONE;
	Scroll->ScrollToStart();
	float Offset = 0.0f;
	const auto AddCaption = [this, &Offset](const FText& Text)
	{
		const TSharedRef<SWidget> Caption = MakeCaption(Text);
		Caption->SlatePrepass();
		Offset += Caption->GetDesiredSize().Y;
		Rows->AddSlot().AutoHeight()[Caption];
	};

	bool bAnyVisible = false;
	for (const bool bAvailable : {true, false})
	{
		int32 LastLayerIndex = INDEX_NONE;
		bool bStarted = false;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const auto& Entry = Entries[Index];
			if (Entry.bAvailable != bAvailable || (!Filter.IsEmpty() && !Entry.SearchText.Contains(Filter))) { continue; }
			if (!bStarted && !bAvailable) { AddCaption(LOCTEXT("Unavailable", "Unavailable")); }
			bStarted = true;
			bAnyVisible = true;
			if (Entry.LayerIndex != LastLayerIndex)
			{
				AddCaption(Entry.LayerLabel);
				LastLayerIndex = Entry.LayerIndex;
			}
			if (bAvailable)
			{
				EligibleEntries.Add(Index);
				EligibleOffsets.Add(Offset);
			}
			// Disabled rows deliberately have no delegate, in addition to the menu item's guard.
			const FSimpleDelegate Action = bAvailable
				? FSimpleDelegate::CreateLambda([OnPicked = OnSourcePicked, Source = Entry.Source]()
				{
					// The menu item dismisses first; activation must not depend on a live picker.
					OnPicked.ExecuteIfBound(Source);
				})
				: FSimpleDelegate();
			Rows->AddSlot().AutoHeight()
			[
				SNew(SMixtormatMenuItem)
				.Label(Entry.Label)
				.Icon(MixtormatIcons::Generator())
				.ToolTipText(Entry.ToolTip)
				.bEnabled(bAvailable)
				.bChecked_Lambda([this, Index]() { return HasKeyboardFocus() && ActiveEntry == Index; })
				.OnActivate(Action)
			];
			Offset += FMixtormatThemeStore::GetResolved().MenuLayout.RowHeight;
		}
	}
	if (!bAnyVisible && !Filter.IsEmpty()) { AddCaption(LOCTEXT("NoMatches", "No sources match this search.")); }
}

void SMixtormatStructuralSourcePicker::Activate(const FMixtormatOutputReference& Source)
{
	if (!Entries.ContainsByPredicate([&Source](const FMixtormatStructuralSourcePickerEntry& Entry)
	{
		return Entry.bAvailable && Entry.Source.SourceLayerId == Source.SourceLayerId
			&& Entry.Source.SourceChildId == Source.SourceChildId
			&& Entry.Source.OutputName == Source.OutputName && Entry.Source.Kind == Source.Kind;
	})) { return; }
	// Keep the delegate and identity alive if dismissal destroys this popup.
	const FOnMixtormatStructuralSourcePicked Action = OnSourcePicked;
	const FMixtormatOutputReference Identity = Source;
	FSlateApplication::Get().DismissAllMenus();
	Action.ExecuteIfBound(Identity);
}

void SMixtormatStructuralSourcePicker::Navigate(const int32 Direction)
{
	if (EligibleEntries.IsEmpty()) { return; }
	const int32 Current = EligibleEntries.IndexOfByKey(ActiveEntry);
	const int32 Next = Current == INDEX_NONE ? (Direction > 0 ? 0 : EligibleEntries.Num() - 1)
		: FMath::Clamp(Current + Direction, 0, EligibleEntries.Num() - 1);
	ActiveEntry = EligibleEntries[Next];
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	Scroll->SetScrollOffset(EligibleOffsets[Next]);
}

FReply SMixtormatStructuralSourcePicker::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		FSlateApplication::Get().DismissAllMenus();
		return FReply::Handled();
	}
	if (!Event.IsControlDown() && !Event.IsCommandDown() && !Event.IsAltDown() && !Event.IsShiftDown())
	{
		if (Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down)
		{
			Navigate(Event.GetKey() == EKeys::Down ? 1 : -1);
			return FReply::Handled();
		}
		if (Event.GetKey() == EKeys::Enter && HasKeyboardFocus() && EligibleEntries.Contains(ActiveEntry))
		{
			const FMixtormatOutputReference Source = Entries[ActiveEntry].Source;
			Activate(Source);
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
}

#undef LOCTEXT_NAMESPACE
