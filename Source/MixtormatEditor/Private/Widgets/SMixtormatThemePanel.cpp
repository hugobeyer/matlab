// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatThemePanel.h"

#include "Style/MixtormatStyle.h"
#include "Style/MixtormatStyleLocator.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Controls/SMixtormatTabStrip.h"
#include "UI/Menus/SMixtormatHelp.h"

#include "HAL/FileManager.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MixtormatUIStyle"

namespace
{
	// Display order is independent of append-only serialized enum ordinal values.
	constexpr Mixtormat::EMixtormatThemeTab ThemeTabOrder[] = {
		Mixtormat::EMixtormatThemeTab::Global,
		Mixtormat::EMixtormatThemeTab::Controls,
		Mixtormat::EMixtormatThemeTab::Foldouts,
		Mixtormat::EMixtormatThemeTab::Cards,
		Mixtormat::EMixtormatThemeTab::Layers,
		Mixtormat::EMixtormatThemeTab::Sources,
		Mixtormat::EMixtormatThemeTab::Buttons,
		Mixtormat::EMixtormatThemeTab::Menus,
		Mixtormat::EMixtormatThemeTab::Preview,
		Mixtormat::EMixtormatThemeTab::GalleryShell,
		Mixtormat::EMixtormatThemeTab::Typography
	};
	constexpr float EditorControlWidth = 150.0f;
	constexpr float ResetGap = 6.0f;
	constexpr float SectionGap = 8.0f;
	constexpr float RowGap = 2.0f;

	const Mixtormat::FMixtormatTheme& Defaults()
	{
		static const Mixtormat::FMixtormatTheme Theme = Mixtormat::MakeDefaultTheme();
		return Theme;
	}

	FString ValidationText(const TArray<FText>& Issues)
	{
		if (Issues.IsEmpty())
		{
			return FString();
		}
		TArray<FString> Lines;
		Lines.Reserve(Issues.Num());
		for (const FText& Issue : Issues)
		{
			Lines.Add(Issue.ToString());
		}
		return FString::Join(Lines, TEXT(" · "));
	}

	FText ChoiceText(const Mixtormat::FMixtormatThemeProperty& P, const int32 Index)
	{
		return P.Options.IsValidIndex(Index)
			? FText::FromString(P.Options[Index])
			: FText::GetEmpty();
	}
}

void SMixtormatThemePanel::Construct(const FArguments& InArgs)
{
	CanEdit = InArgs._CanEdit;
	OnThemeChanged = InArgs._OnThemeChanged;
	
	// Show startup load status
	FMixtormatThemeStore::EStartupLoadResult LoadResult = FMixtormatThemeStore::GetStartupLoadResult();
	switch (LoadResult)
	{
	case FMixtormatThemeStore::EStartupLoadResult::LoadedSavedTheme:
		Status = TEXT("Loaded saved UI style theme.");
		break;
	case FMixtormatThemeStore::EStartupLoadResult::UsingCompiledDefaults:
		Status = TEXT("Using compiled defaults (no saved theme found).");
		break;
	case FMixtormatThemeStore::EStartupLoadResult::SavedThemeInvalid:
		Status = TEXT("Saved theme invalid, using compiled defaults.");
		break;
	}
	
	TArray<FText> Tabs;
	Tabs.Add(LOCTEXT("AllTab", "ALL"));
	for (const Mixtormat::EMixtormatThemeTab Tab : ThemeTabOrder)
	{
		Tabs.Add(FText::FromString(Mixtormat::FMixtormatThemeSchema::TabLabel(Tab)));
	}

	const TSharedRef<SWidget> TabStrip =
		SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()
		[
			SNew(SMixtormatTabStrip)
			.Options(Tabs)
			.StretchTabs(false)
			.ActiveIndex_Lambda([this]()
			{
				if (SelectedTab == INDEX_NONE) return 0;
				for (int32 I = 0; I < UE_ARRAY_COUNT(ThemeTabOrder); ++I)
				{
					if (static_cast<int32>(ThemeTabOrder[I]) == SelectedTab) return I + 1;
				}
				return 0;
			})
			.OnChosen(this, &SMixtormatThemePanel::SelectTab)
		];

	struct FSectionKey
	{
		Mixtormat::EMixtormatThemeTab Tab;
		FString Name;
	};
	TArray<FSectionKey> Sections;
	for (const Mixtormat::FMixtormatThemeProperty& P : Mixtormat::FMixtormatThemeSchema::Properties())
	{
		if (!Sections.ContainsByPredicate([&P](const FSectionKey& S)
			{ return S.Tab == P.Tab && S.Name == P.Section; }))
		{
			Sections.Add({P.Tab, P.Section});
		}
	}

	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	for (const FSectionKey& Section : Sections)
	{
		const FString SectionKey = FString::Printf(TEXT("%d:%s"), static_cast<int32>(Section.Tab), *Section.Name);
		Content->AddSlot().AutoHeight().Padding(0.0f, SectionGap, 0.0f, 3.0f)
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
			.ContentPadding(2.0f)
			.Visibility_Lambda([this, Section]()
			{
				return SectionVisible(Section.Tab, Section.Name)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this, SectionKey]()
			{
				if (CollapsedSections.Contains(SectionKey)) { CollapsedSections.Remove(SectionKey); }
				else { CollapsedSections.Add(SectionKey); }
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Section, SectionKey]()
				{
					return FText::FromString(FString::Printf(TEXT("%s %s"),
						CollapsedSections.Contains(SectionKey) ? TEXT(">") : TEXT("v"), *Section.Name.ToUpper()));
				})
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowCaption")))
			]
		];

		for (const Mixtormat::FMixtormatThemeProperty& P : Mixtormat::FMixtormatThemeSchema::Properties())
		{
			if (P.Tab != Section.Tab || P.Section != Section.Name)
			{
				continue;
			}
			const Mixtormat::FMixtormatThemeProperty* Property = &P;
			Content->AddSlot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(SBox)
				.Visibility_Lambda([this, Property]()
				{
					const FString Key = FString::Printf(TEXT("%d:%s"), static_cast<int32>(Property->Tab), *Property->Section);
					return PropertyVisible(*Property) && (!Filter.IsEmpty() || !CollapsedSections.Contains(Key))
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				[MakePropertyRow(P)]
			];
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor_Lambda([]()
		{
			return FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground);
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Intro",
					"UI STYLE edits the new theme model directly. Recipe-derived state colors are intentionally absent."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("Search", "Search property, section, or id..."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					Filter = Text.ToString().TrimStartAndEnd();
					if (PropertyScroll.IsValid())
					{
						PropertyScroll->ScrollToStart();
					}
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[TabStrip]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(PropertyScroll, SScrollBox)
				+ SScrollBox::Slot()
				[Content]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Save", "Save"))
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.OnClicked(this, &SMixtormatThemePanel::Save)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Load", "Load"))
					.IsEnabled_Lambda([this]()
					{
						return CanEdit.Get(true)
							&& IFileManager::Get().FileExists(*Mixtormat::FMixtormatThemeSchema::SavePath());
					})
					.OnClicked(this, &SMixtormatThemePanel::Load)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ResetAll", "Reset All"))
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.OnClicked(this, &SMixtormatThemePanel::ResetAll)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Status); })
				.ColorAndOpacity_Lambda([this]()
				{
					// Validation issues are warnings, not errors: the theme still loads and applies,
					// so they read in the palette's Warning colour rather than Error red.
					const bool bHasIssues = !FMixtormatThemeStore::GetValidationIssues().IsEmpty();
					return FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
						bHasIssues ? Mixtormat::EMixtormatColorRole::Warning : Mixtormat::EMixtormatColorRole::Text));
				})
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Mixtormat::FMixtormatThemeSchema::SavePath()))
				.AutoWrapText(true)
			]
		]
	];
}

SMixtormatThemePanel::~SMixtormatThemePanel()
{
	Mixtormat::FMixtormatStyleLocator::End();
}

TSharedRef<SWidget> SMixtormatThemePanel::MakePropertyRow(
	const Mixtormat::FMixtormatThemeProperty& P)
{
	const Mixtormat::FMixtormatThemeProperty* Property = &P;
	const Mixtormat::EMixtormatStyleTarget Target = Mixtormat::FMixtormatStyleLocator::TargetFor(P);
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		.ToolTip(SMixtormatHelp::MakeStyledToolTip(P.Help.IsEmpty()
			? FText::FromString(P.Id.ToString())
			: FText::FromString(P.Help)));

	Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
	[
		SNew(STextBlock)
			.Text(FText::FromString(P.Label))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
	];

	switch (P.Kind)
	{
	case Mixtormat::EMixtormatThemePropertyKind::Number:
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(EditorControlWidth)
			[
				SNew(SSpinBox<float>)
					.MinValue(P.Minimum).MaxValue(P.Maximum)
					.MinSliderValue(P.Minimum).MaxSliderValue(P.Maximum)
					.Delta(P.Step)
					.MinFractionalDigits(P.Precision)
					.MaxFractionalDigits(P.Precision)
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.Value_Lambda([this, Property]() { return NumberValue(*Property); })
					.OnValueChanged_Lambda([this, Id=P.Id](float V) { PreviewNumber(Id, V); })
					.OnValueCommitted_Lambda([this, Id=P.Id](float V, ETextCommit::Type) { CommitNumber(Id, V); })
			]
		];
		break;

	case Mixtormat::EMixtormatThemePropertyKind::Bool:
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(EditorControlWidth).HAlign(HAlign_Left)
			[
				SNew(SCheckBox)
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.IsChecked_Lambda([Property]()
					{
						return Property->GetBool(FMixtormatThemeStore::GetTheme())
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, Id=P.Id](ECheckBoxState State)
					{
						CommitBool(Id, State == ECheckBoxState::Checked);
					})
			]
		];
		break;

	case Mixtormat::EMixtormatThemePropertyKind::Choice:
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(EditorControlWidth)
			[
				SNew(SComboButton)
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([Property]()
						{
							return ChoiceText(*Property, Property->GetChoice(FMixtormatThemeStore::GetTheme()));
						})
					]
					.OnGetMenuContent_Lambda([this, Property]()
					{
						TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
						for (int32 I = 0; I < Property->Options.Num(); ++I)
						{
							Menu->AddSlot().AutoHeight()
							[
								SNew(SButton)
									.Text(FText::FromString(Property->Options[I]))
									.OnClicked_Lambda([this, Id=Property->Id, I]()
									{
										CommitChoice(Id, I);
										return FReply::Handled();
									})
							];
						}
						return SNew(SMixtormatMenuPanel)
							.MinWidth(EditorControlWidth)
							[Menu];
					})
			]
		];
		break;

	case Mixtormat::EMixtormatThemePropertyKind::Color:
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(EditorControlWidth)
			[
				SNew(SButton)
					.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
					.OnClicked(this, &SMixtormatThemePanel::OpenColor, P.Id)
					[
						SNew(SColorBlock)
							.Color_Lambda([Property]() { return Property->GetColor(FMixtormatThemeStore::GetTheme()); })
							.ShowBackgroundForAlpha(true)
							.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Combined)
					]
			]
		];
		break;
	}

	Row->AddSlot().AutoWidth().Padding(ResetGap, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
	[
		SNew(SButton)
			.ContentPadding(FMargin(2.0f))
			.IsEnabled(Target != Mixtormat::EMixtormatStyleTarget::None)
			.ToolTip(SMixtormatHelp::MakeStyledToolTip(FText::Format(
				LOCTEXT("LocateTarget", "Locate / blink: {0}"),
				Mixtormat::FMixtormatStyleLocator::Label(Target))))
			.OnClicked_Lambda([this, Target]()
			{
				LocateTarget(Target);
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.WidthOverride(14.0f)
				.HeightOverride(14.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(MixtormatIcons::Eye())
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
	];

	Row->AddSlot().AutoWidth().Padding(ResetGap, 0.0f).VAlign(VAlign_Center)
	[
		SNew(SButton)
			.Text(LOCTEXT("ResetProperty", "Reset"))
			.IsEnabled_Lambda([this, Property]() { return CanEdit.Get(true) && !IsDefault(*Property); })
			.OnClicked_Lambda([this, Id=P.Id]()
			{
				ResetProperty(Id);
				return FReply::Handled();
			})
	];

	return Row;
}

bool SMixtormatThemePanel::PropertyVisible(const Mixtormat::FMixtormatThemeProperty& P) const
{
	if (SelectedTab != INDEX_NONE && SelectedTab != static_cast<int32>(P.Tab))
	{
		return false;
	}
	if (Filter.IsEmpty())
	{
		return true;
	}
	return P.Label.Contains(Filter, ESearchCase::IgnoreCase)
		|| P.Section.Contains(Filter, ESearchCase::IgnoreCase)
		|| P.Id.ToString().Contains(Filter, ESearchCase::IgnoreCase);
}

bool SMixtormatThemePanel::SectionVisible(
	const Mixtormat::EMixtormatThemeTab Tab, const FString& Section) const
{
	return Mixtormat::FMixtormatThemeSchema::Properties().ContainsByPredicate(
		[this, Tab, &Section](const Mixtormat::FMixtormatThemeProperty& P)
		{
			return P.Tab == Tab && P.Section == Section && PropertyVisible(P);
		});
}

float SMixtormatThemePanel::NumberValue(const Mixtormat::FMixtormatThemeProperty& P) const
{
	if (const float* Pending = PendingNumbers.Find(P.Id))
	{
		return *Pending;
	}
	return P.GetNumber(FMixtormatThemeStore::GetTheme());
}

bool SMixtormatThemePanel::IsDefault(const Mixtormat::FMixtormatThemeProperty& P) const
{
	const Mixtormat::FMixtormatTheme& Current = FMixtormatThemeStore::GetTheme();
	const Mixtormat::FMixtormatTheme& Default = Defaults();
	switch (P.Kind)
	{
	case Mixtormat::EMixtormatThemePropertyKind::Number:
		return FMath::IsNearlyEqual(P.GetNumber(Current), P.GetNumber(Default));
	case Mixtormat::EMixtormatThemePropertyKind::Bool:
		return P.GetBool(Current) == P.GetBool(Default);
	case Mixtormat::EMixtormatThemePropertyKind::Choice:
		return P.GetChoice(Current) == P.GetChoice(Default);
	case Mixtormat::EMixtormatThemePropertyKind::Color:
		return P.GetColor(Current).Equals(P.GetColor(Default), KINDA_SMALL_NUMBER);
	}
	return true;
}

void SMixtormatThemePanel::PreviewNumber(const FName Id, const float Value)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Number || !CanEdit.Get(true))
	{
		return;
	}

	// Keep the spin-box value local so dragging stays stable, but also push the authored value into
	// ThemeStore immediately. The owner already coalesces RequestThemeRefresh() on a 0.1 s active
	// timer, so a fast drag can generate many value events without rebuilding the workspace on every
	// mouse move. Paint-only readers update on the next invalidation; construction-time layout/style
	// readers update on the coalesced workspace refresh.
	const float Clamped = FMath::Clamp(Value, P->Minimum, P->Maximum);
	PendingNumbers.Add(Id, Clamped);
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	P->SetNumber(Theme, Clamped);
	FMixtormatThemeStore::SetTheme(MoveTemp(Theme));
	OnThemeChanged.ExecuteIfBound(P->RefreshMode);
}

void SMixtormatThemePanel::CommitNumber(const FName Id, const float Value)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Number || !CanEdit.Get(true))
	{
		return;
	}
	PendingNumbers.Remove(Id);
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	P->SetNumber(Theme, FMath::Clamp(Value, P->Minimum, P->Maximum));
	CommitTheme(MoveTemp(Theme), TEXT("UI style updated."), P->RefreshMode);
}

void SMixtormatThemePanel::CommitBool(const FName Id, const bool Value)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Bool || !CanEdit.Get(true))
	{
		return;
	}
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	P->SetBool(Theme, Value);
	CommitTheme(MoveTemp(Theme), TEXT("UI style updated."), P->RefreshMode);
}

void SMixtormatThemePanel::CommitChoice(const FName Id, const int32 Value)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Choice || !CanEdit.Get(true)
		|| !P->Options.IsValidIndex(Value))
	{
		return;
	}
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	P->SetChoice(Theme, Value);
	CommitTheme(MoveTemp(Theme), TEXT("UI style updated."), P->RefreshMode);
}

void SMixtormatThemePanel::CommitColor(const FLinearColor Value, const FName Id)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Color || !CanEdit.Get(true))
	{
		return;
	}
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	P->SetColor(Theme, Value);
	CommitTheme(MoveTemp(Theme), TEXT("UI style updated."), P->RefreshMode);
}

void SMixtormatThemePanel::ResetProperty(const FName Id)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || !CanEdit.Get(true))
	{
		return;
	}
	PendingNumbers.Remove(Id);
	Mixtormat::FMixtormatTheme Theme = FMixtormatThemeStore::GetTheme();
	const Mixtormat::FMixtormatTheme& Default = Defaults();
	switch (P->Kind)
	{
	case Mixtormat::EMixtormatThemePropertyKind::Number:
		P->SetNumber(Theme, P->GetNumber(Default)); break;
	case Mixtormat::EMixtormatThemePropertyKind::Bool:
		P->SetBool(Theme, P->GetBool(Default)); break;
	case Mixtormat::EMixtormatThemePropertyKind::Choice:
		P->SetChoice(Theme, P->GetChoice(Default)); break;
	case Mixtormat::EMixtormatThemePropertyKind::Color:
		P->SetColor(Theme, P->GetColor(Default)); break;
	}
	CommitTheme(MoveTemp(Theme), TEXT("Property reset."), P->RefreshMode);
}

void SMixtormatThemePanel::CommitTheme(
	Mixtormat::FMixtormatTheme Theme, const FString& Message, Mixtormat::EMixtormatThemeRefreshMode RefreshMode)
{
	FMixtormatThemeStore::SetTheme(MoveTemp(Theme));
	UpdateStatus(Message);
	OnThemeChanged.ExecuteIfBound(RefreshMode);
}

FReply SMixtormatThemePanel::OpenColor(const FName Id)
{
	const Mixtormat::FMixtormatThemeProperty* P = Mixtormat::FMixtormatThemeSchema::Find(Id);
	if (!P || P->Kind != Mixtormat::EMixtormatThemePropertyKind::Color)
	{
		return FReply::Handled();
	}

	FColorPickerArgs Args;
	Args.ParentWidget = AsShared();
	Args.bUseAlpha = true;
	// UI STYLE is an authoring viewport: colour edits should preview while the picker moves. The
	// workspace owner coalesces rebuild requests, so this stays live without rebuilding per sample.
	Args.bOnlyRefreshOnMouseUp = false;
	Args.InitialColor = P->GetColor(FMixtormatThemeStore::GetTheme());
	Args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this, &SMixtormatThemePanel::CommitColor, Id);
	OpenColorPicker(Args);
	return FReply::Handled();
}

FReply SMixtormatThemePanel::Save()
{
	FString Error;
	Status = Mixtormat::FMixtormatThemeSchema::Save(Error)
		? TEXT("UI style theme saved.")
		: Error;
	return FReply::Handled();
}

FReply SMixtormatThemePanel::Load()
{
	FString Error;
	TArray<FText> Issues;
	if (!Mixtormat::FMixtormatThemeSchema::Load(Error, Issues))
	{
		Status = Error;
		return FReply::Handled();
	}
	PendingNumbers.Reset();
	UpdateStatus(TEXT("UI style theme loaded."));
	OnThemeChanged.ExecuteIfBound(Mixtormat::EMixtormatThemeRefreshMode::Reconstruct);
	return FReply::Handled();
}

FReply SMixtormatThemePanel::ResetAll()
{
	if (!CanEdit.Get(true))
	{
		return FReply::Handled();
	}
	PendingNumbers.Reset();
	FMixtormatThemeStore::ResetToDefaults();
	UpdateStatus(TEXT("Authored defaults restored."));
	OnThemeChanged.ExecuteIfBound(Mixtormat::EMixtormatThemeRefreshMode::Reconstruct);
	return FReply::Handled();
}

void SMixtormatThemePanel::SelectTab(const int32 Index)
{
	SelectedTab = Index <= 0 || Index > UE_ARRAY_COUNT(ThemeTabOrder)
		? INDEX_NONE : static_cast<int32>(ThemeTabOrder[Index - 1]);
	if (PropertyScroll.IsValid())
	{
		PropertyScroll->ScrollToStart();
	}
}

void SMixtormatThemePanel::LocateTarget(const Mixtormat::EMixtormatStyleTarget Target)
{
	Mixtormat::FMixtormatStyleLocator::End();
	LocateFlashPhase = INDEX_NONE;
	LocatedTarget = Mixtormat::EMixtormatStyleTarget::None;

	if (Target == Mixtormat::EMixtormatStyleTarget::None)
	{
		Status = TEXT("This property has no live locate target.");
		return;
	}

	// Anchor the search on this panel: type matches are not unique, and the nearest visible
	// one is the control the artist is actually tuning.
	const FGeometry PanelGeometry = GetCachedGeometry();
	const TOptional<FVector2f> Anchor(FVector2f(
		PanelGeometry.GetAbsolutePosition() + FVector2D(PanelGeometry.GetLocalSize()) * 0.5));
	if (!Mixtormat::FMixtormatStyleLocator::Begin(Target, Anchor))
	{
		Status = FString::Printf(
			TEXT("Target is not currently visible: %s."),
			*Mixtormat::FMixtormatStyleLocator::Label(Target).ToString());
		return;
	}

	LocatedTarget = Target;
	LocateFlashPhase = 0;
	Status = FString::Printf(
		TEXT("Blinking: %s."),
		*Mixtormat::FMixtormatStyleLocator::Label(Target).ToString());

	RegisterActiveTimer(
		0.14f,
		FWidgetActiveTimerDelegate::CreateSP(
			this, &SMixtormatThemePanel::AdvanceLocateFlash));
}

EActiveTimerReturnType SMixtormatThemePanel::AdvanceLocateFlash(
	double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;

	if (LocatedTarget == Mixtormat::EMixtormatStyleTarget::None)
	{
		Mixtormat::FMixtormatStyleLocator::End();
		return EActiveTimerReturnType::Stop;
	}

	// The outline overlay paints the pulse itself; this timer only owns the lifecycle and status.
	if (!Mixtormat::FMixtormatStyleLocator::Tick())
	{
		LocatedTarget = Mixtormat::EMixtormatStyleTarget::None;
		LocateFlashPhase = INDEX_NONE;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SMixtormatThemePanel::UpdateStatus(const FString& Prefix)
{
	const FString Issues = ValidationText(FMixtormatThemeStore::GetValidationIssues());
	Status = Issues.IsEmpty() ? Prefix : Prefix + TEXT("  Validation: ") + Issues;
}

#undef LOCTEXT_NAMESPACE
