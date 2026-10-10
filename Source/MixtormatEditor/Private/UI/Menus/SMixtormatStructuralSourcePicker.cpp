// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatStructuralSourcePicker.h"

#include "Framework/Application/SlateApplication.h"

#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Menus/SMixtormatMenuItem.h"
#include "UI/Primitives/SMixtormatWellBox.h"
#include "UI/Rows/SMixtormatRow.h"
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
	OnSourceLater = InArgs._OnSourceLater;
	EndpointPreview = InArgs._EndpointPreview;
	OnPreviewChanged = InArgs._OnPreviewChanged;
	bPreviewCurrent = InArgs._bPreviewCurrent;
	const auto& Resolved = FMixtormatThemeStore::GetResolved();
	const auto& Layout = Resolved.MenuLayout;
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(Resolved.LayerConnections.PickerWidth)
		[
			SAssignNew(ListRoot, SVerticalBox)
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
				SNew(SBox)
				.MaxDesiredHeight(Resolved.LayerConnections.PickerListMaxHeight)
				[
					SAssignNew(Scroll, SScrollBox)
					.OnUserScrolled_Lambda([this](float)
					{
						HoveredSource = {};
						RefreshEndpointPreview();
					})
					+ SScrollBox::Slot()
					[
						SAssignNew(Rows, SVerticalBox)
					]
				]
			]
		]
	];
	if (OnSourceLater.IsBound())
	{
		// Pinned below the list so the fallback stays visible without scrolling, and separated
		// from the source rows it deliberately does not belong to.
		ListRoot->AddSlot().AutoHeight().Padding(Layout.ItemInset, Layout.CaptionInsetBelow, Layout.ItemInset, 0.0f)
		[
			MixtormatRow::MakeHairline()
		];
		ListRoot->AddSlot().AutoHeight()
		[
			SNew(SMixtormatMenuItem)
			.Label(LOCTEXT("ChooseSourceLater", "Choose source later"))
			.Icon(MixtormatIcons::Generator())
			.ToolTipText(LOCTEXT("ChooseSourceLaterHint", "Create the operation with its target set and no source."))
			.bEnabled(true)
			.bChecked_Lambda([this]() { return HasKeyboardFocus() && bLaterActive; })
			.OnActivate(FSimpleDelegate::CreateSP(this, &SMixtormatStructuralSourcePicker::ActivateLater))
		];
	}
	RebuildList();

	// The popup must be attached before Slate can resolve a focus path to its search field.
	const TWeakPtr<SMixtormatStructuralSourcePicker> WeakThis = SharedThis(this);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakThis, bFocusSet = false](double, float) mutable
	{
		if (const auto Picker = WeakThis.Pin())
		{
			if (!bFocusSet)
			{
				FSlateApplication::Get().SetKeyboardFocus(Picker->Search, EFocusCause::SetDirectly);
				bFocusSet = true;
			}
			// Menu anchors may retain content after dismissal: lifetime alone is not enough.
			FWidgetPath Path;
			if (Picker->EndpointPreview.IsValid() && Picker->bPreviewCurrent.Get(true)
				&& FSlateApplication::Get().GeneratePathToWidgetUnchecked(Picker.ToSharedRef(), Path))
			{
				return EActiveTimerReturnType::Continue;
			}
			Picker->ReleaseEndpointPreview();
		}
		return EActiveTimerReturnType::Stop;
	}));
}

SMixtormatStructuralSourcePicker::~SMixtormatStructuralSourcePicker()
{
	ReleaseEndpointPreview();
}

void SMixtormatStructuralSourcePicker::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bPreviewCurrent.Get(true)) { ReleaseEndpointPreview(); }
	RefreshEndpointPreview();
}

void SMixtormatStructuralSourcePicker::RefreshEndpointPreview()
{
	if (!EndpointPreview.IsValid()) { return; }
	const FMixtormatChildAddress Source = HoveredSource.IsValid() ? HoveredSource
		: HasKeyboardFocus() && EligibleEntries.Contains(ActiveEntry)
			? Entries[ActiveEntry].SourceAddress : FMixtormatChildAddress();
	if (!(EndpointPreview->Source == Source))
	{
		EndpointPreview->Source = Source;
		OnPreviewChanged.ExecuteIfBound();
	}
}

void SMixtormatStructuralSourcePicker::ReleaseEndpointPreview()
{
	if (!EndpointPreview.IsValid()) { return; }
	EndpointPreview.Reset();
	OnPreviewChanged.ExecuteIfBound();
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
	HoveredSource = {};
	ActiveEntry = INDEX_NONE;
	bLaterActive = false;
	RefreshEndpointPreview();
	Rows->ClearChildren();
	EligibleEntries.Reset();
	EligibleOffsets.Reset();
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
			const TWeakPtr<SMixtormatStructuralSourcePicker> WeakThis = SharedThis(this);
			const FSimpleDelegate Action = bAvailable
				? FSimpleDelegate::CreateLambda([WeakThis, OnPicked = OnSourcePicked, Source = Entry.Source]()
				{
					// The menu item dismisses first; activation must not depend on a live picker.
					if (const auto Picker = WeakThis.Pin()) { Picker->ReleaseEndpointPreview(); }
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
				.OnHovered(bAvailable ? FSimpleDelegate::CreateLambda([WeakThis, Source = Entry.SourceAddress]()
				{
					if (const auto Picker = WeakThis.Pin())
					{
						Picker->HoveredSource = Source;
						Picker->RefreshEndpointPreview();
					}
				}) : FSimpleDelegate())
				.OnUnhovered(bAvailable ? FSimpleDelegate::CreateLambda([WeakThis, Source = Entry.SourceAddress]()
				{
					if (const auto Picker = WeakThis.Pin(); Picker && Picker->HoveredSource == Source)
					{
						Picker->HoveredSource = {};
						Picker->RefreshEndpointPreview();
					}
				}) : FSimpleDelegate())
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
	ReleaseEndpointPreview();
	FSlateApplication::Get().DismissAllMenus();
	Action.ExecuteIfBound(Identity);
}

void SMixtormatStructuralSourcePicker::ActivateLater()
{
	if (!OnSourceLater.IsBound()) { return; }
	// Keep the delegate alive if dismissal destroys this popup.
	const FSimpleDelegate Action = OnSourceLater;
	ReleaseEndpointPreview();
	FSlateApplication::Get().DismissAllMenus();
	Action.ExecuteIfBound();
}

void SMixtormatStructuralSourcePicker::Navigate(const int32 Direction)
{
	// Sources navigate first; the footer's "Choose source later" is the position after them, so
	// keyboard users reach the same fallback mouse users see pinned below the list.
	const bool bHasLater = OnSourceLater.IsBound();
	const int32 SourceCount = EligibleEntries.Num();
	const int32 Count = SourceCount + (bHasLater ? 1 : 0);
	if (Count == 0) { return; }
	const int32 Current = bLaterActive ? SourceCount : EligibleEntries.IndexOfByKey(ActiveEntry);
	const int32 Next = Current == INDEX_NONE ? (Direction > 0 ? 0 : Count - 1)
		: FMath::Clamp(Current + Direction, 0, Count - 1);
	HoveredSource = {};
	bLaterActive = bHasLater && Next == SourceCount;
	ActiveEntry = bLaterActive ? INDEX_NONE : EligibleEntries[Next];
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	if (!bLaterActive) { Scroll->SetScrollOffset(EligibleOffsets[Next]); }
	RefreshEndpointPreview();
}

FReply SMixtormatStructuralSourcePicker::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		ReleaseEndpointPreview();
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
		if (Event.GetKey() == EKeys::Enter && HasKeyboardFocus())
		{
			if (bLaterActive) { ActivateLater(); return FReply::Handled(); }
			if (EligibleEntries.Contains(ActiveEntry))
			{
				const FMixtormatOutputReference Source = Entries[ActiveEntry].Source;
				Activate(Source);
				return FReply::Handled();
			}
		}
	}
	return SCompoundWidget::OnPreviewKeyDown(Geometry, Event);
}

#undef LOCTEXT_NAMESPACE
