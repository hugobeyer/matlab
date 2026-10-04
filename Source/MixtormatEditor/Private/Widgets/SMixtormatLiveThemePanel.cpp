// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatLiveThemePanel.h"

#include "Style/MixtormatLiveTheme.h"
#include "HAL/FileManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MixtormatLiveTheme"

namespace
{
	// Keep the editor of the theme usable even while the edited theme is unreadable.
	constexpr float PanelPadding = 10.0f;
	constexpr float RowGap = 3.0f;
	constexpr float ControlWidth = 100.0f;
	constexpr float ResetGap = 6.0f;
}

void SMixtormatLiveThemePanel::Construct(const FArguments& InArgs)
{
	FMixtormatLiveTheme::Initialize();
	CanEdit = InArgs._CanEdit;
	OnThemeChanged = InArgs._OnThemeChanged;
	Status = TEXT("Changes preview automatically. Save stores this theme; Load restores it.");

	const TSharedRef<SWrapBox> Tabs = SNew(SWrapBox).UseAllottedSize(true);
	TArray<FString> TabCategories = {FString()};
	TabCategories.Append(FMixtormatLiveTheme::Categories());
	for (const FString& Category : TabCategories)
	{
		Tabs->AddSlot()
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
			.IsChecked_Lambda([this, Category]()
			{
				return SelectedCategory == Category ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Category](ECheckBoxState) { SelectCategory(Category); })
			[
				SNew(STextBlock)
				.Text(Category.IsEmpty() ? LOCTEXT("AllCategories", "All") : FText::FromString(Category))
			]
		];
	}

	const TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);
	for (const FString& Category : FMixtormatLiveTheme::Categories())
	{
		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		Rows->AddSlot().AutoHeight().Padding(0.0f, RowGap)
		[
			SNew(STextBlock).Text(FText::FromString(Category))
		];
		for (const FMixtormatThemeNumber& Entry : FMixtormatLiveTheme::Numbers())
		{
			if (!Entry.bExposeInUI || Entry.Category != Category)
			{
				continue;
			}
			const FString Name = Entry.Name.ToString();
			Rows->AddSlot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(FString::Printf(TEXT("%s / %s\nDefault: %g. Range: %g–%g."),
					*Entry.Category, *Name, Entry.Default, Entry.Minimum, Entry.Maximum)))
				.Visibility_Lambda([this, Name, Category = Entry.Category]()
				{
					return Matches(Name, Category) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Name))
					.AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f)
				[
					SNew(SBox).WidthOverride(ControlWidth)
					[
						SNew(SSpinBox<float>)
						.MinValue(Entry.Minimum).MaxValue(Entry.Maximum)
						.MinSliderValue(Entry.Minimum).MaxSliderValue(Entry.Maximum)
						.Delta(Entry.Category == TEXT("Typography") ? 1.0f : 0.5f)
						.Value_Lambda([Entry]() { return *Entry.Value; })
						.OnValueChanged(this, &SMixtormatLiveThemePanel::ChangeNumber, Entry.Name)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("ResetToken", "Reset"))
					.IsEnabled_Lambda([Entry]() { return !FMath::IsNearlyEqual(*Entry.Value, Entry.Default); })
					.OnClicked_Lambda([this, Entry]()
					{
						ChangeNumber(Entry.Default, Entry.Name);
						return FReply::Handled();
					})
				]
			];
		}
		for (const FMixtormatThemeColor& Entry : FMixtormatLiveTheme::Colors())
		{
			if (!Entry.bExposeInUI || Entry.Category != Category)
			{
				continue;
			}
			Rows->AddSlot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(Entry.Category + TEXT(" / ") + Entry.Name.ToString()
					+ TEXT("\nDefault (linear RGBA): ") + Entry.Default.ToString() + TEXT(". Range: 0–1 per channel.")))
				.Visibility_Lambda([this, Name = Entry.Name.ToString(), Category = Entry.Category]()
				{
					return Matches(Name, Category) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromName(Entry.Name)).AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f)
				[
					SNew(SBox).WidthOverride(ControlWidth)
					[
						SNew(SButton)
						.ToolTipText(FText::FromName(Entry.Name))
						.OnClicked(this, &SMixtormatLiveThemePanel::OpenColor, Entry.Name)
						[
							SNew(SColorBlock).Size(FVector2D(ControlWidth, 18.0f))
							.Color_Lambda([Entry]() { return FMixtormatLiveTheme::ResolveColor(Entry.Name, Entry.Default); })
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("ResetColor", "Reset"))
					.IsEnabled_Lambda([Entry]()
					{
						return !FMixtormatLiveTheme::ResolveColor(Entry.Name, Entry.Default).Equals(Entry.Default);
					})
					.OnClicked_Lambda([this, Entry]()
					{
						ChangeColor(Entry.Default, Entry.Name);
						return FReply::Handled();
					})
				]
			];
		}
		Sections->AddSlot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this, Category]()
			{
				return HasMatches(Category) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[Rows]
		];
	}
	Sections->AddSlot().AutoHeight().Padding(0.0f, RowGap)
	[
		SNew(STextBlock).Text(LOCTEXT("NoMatchingTokens", "No matching tokens in this category."))
		.Visibility_Lambda([this]() { return HasMatches() ? EVisibility::Collapsed : EVisibility::Visible; })
	];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(PanelPadding)
		.IsEnabled(CanEdit)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ThemeHelp", "Live UI styling — shared tokens only. Derived sizes follow their parents. Menus and dialogs use new sizes when reopened."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(SSearchBox)
				.HintText_Lambda([this]()
				{
					return SelectedCategory.IsEmpty()
						? LOCTEXT("SearchAllTokens", "Search all names or categories...")
						: FText::Format(LOCTEXT("SearchCategoryTokens", "Search within {0}..."), FText::FromString(SelectedCategory));
				})
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					Filter = Text.ToString().TrimStartAndEnd();
					if (TokenScroll.IsValid())
					{
						TokenScroll->ScrollToStart();
					}
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("SaveTheme", "Save")).OnClicked(this, &SMixtormatLiveThemePanel::Save)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("LoadTheme", "Load"))
					.IsEnabled_Lambda([]() { return IFileManager::Get().FileExists(*FMixtormatLiveTheme::SavePath()); })
					.OnClicked(this, &SMixtormatLiveThemePanel::Load)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("ResetTheme", "Reset All")).OnClicked(this, &SMixtormatLiveThemePanel::ResetAll)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, RowGap)
			[Tabs]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[SAssignNew(TokenScroll, SScrollBox) + SScrollBox::Slot()[Sections]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, RowGap)
			[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status); }).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(STextBlock).Text(FText::FromString(FMixtormatLiveTheme::SavePath())).AutoWrapText(true)]
		]
	];
}

bool SMixtormatLiveThemePanel::Matches(const FString& Name, const FString& Category) const
{
	return (SelectedCategory.IsEmpty() || SelectedCategory == Category)
		&& (Filter.IsEmpty() || Name.Contains(Filter) || Category.Contains(Filter));
}

bool SMixtormatLiveThemePanel::HasMatches(const FString& Category) const
{
	for (const FMixtormatThemeNumber& Entry : FMixtormatLiveTheme::Numbers())
	{
		if (Entry.bExposeInUI && (Category.IsEmpty() || Category == Entry.Category)
			&& Matches(Entry.Name.ToString(), Entry.Category))
		{
			return true;
		}
	}
	for (const FMixtormatThemeColor& Entry : FMixtormatLiveTheme::Colors())
	{
		if (Entry.bExposeInUI && (Category.IsEmpty() || Category == Entry.Category)
			&& Matches(Entry.Name.ToString(), Entry.Category))
		{
			return true;
		}
	}
	return false;
}

void SMixtormatLiveThemePanel::SelectCategory(const FString& Category)
{
	SelectedCategory = Category;
	if (TokenScroll.IsValid())
	{
		TokenScroll->ScrollToStart();
	}
}

void SMixtormatLiveThemePanel::ChangeNumber(const float Value, const FName Name)
{
	if (CanEdit.Get(true) && FMixtormatLiveTheme::SetNumber(Name, Value))
	{
		Status = TEXT("Preview update queued. Save to keep these settings.");
		OnThemeChanged.ExecuteIfBound();
	}
}

void SMixtormatLiveThemePanel::ChangeColor(const FLinearColor Value, const FName Name)
{
	if (FMixtormatLiveTheme::SetColor(Name, Value))
	{
		Status = TEXT("Preview update queued. Save to keep these settings.");
		OnThemeChanged.ExecuteIfBound();
	}
	else
	{
		Status = TEXT("UI colors require RGBA values from 0 to 1; HDR colors are not supported.");
	}
}

FReply SMixtormatLiveThemePanel::OpenColor(const FName Name)
{
	const FMixtormatThemeColor* Entry = FMixtormatLiveTheme::Colors().FindByPredicate(
		[Name](const FMixtormatThemeColor& Item) { return Item.Name == Name; });
	if (Entry)
	{
		FColorPickerArgs Args;
		Args.ParentWidget = AsShared();
		Args.bUseAlpha = true;
		Args.bOnlyRefreshOnMouseUp = false;
		Args.InitialColor = FMixtormatLiveTheme::ResolveColor(Name, Entry->Default);
		Args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SMixtormatLiveThemePanel::ChangeColor, Name);
		Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &SMixtormatLiveThemePanel::ChangeColor, Name);
		OpenColorPicker(Args);
	}
	return FReply::Handled();
}

FReply SMixtormatLiveThemePanel::Save()
{
	FString Error;
	Status = FMixtormatLiveTheme::Save(Error) ? TEXT("Theme saved.") : Error;
	return FReply::Handled();
}

FReply SMixtormatLiveThemePanel::Load()
{
	FString Error;
	if (FMixtormatLiveTheme::Load(Error))
	{
		Status = TEXT("Saved theme loaded.");
		OnThemeChanged.ExecuteIfBound();
	}
	else
	{
		Status = Error;
	}
	return FReply::Handled();
}

FReply SMixtormatLiveThemePanel::ResetAll()
{
	FMixtormatLiveTheme::Reset();
	Status = TEXT("Authored defaults restored. Your saved file has not been changed.");
	OnThemeChanged.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
