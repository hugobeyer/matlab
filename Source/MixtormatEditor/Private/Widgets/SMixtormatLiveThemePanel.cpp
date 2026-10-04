// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatLiveThemePanel.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatLiveTheme.h"
#include "Style/MixtormatStyle.h"
#include "HAL/FileManager.h"
#include "Styling/AppStyle.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Controls/SMixtormatTabStrip.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MixtormatLiveTheme"

namespace
{
	// Editor metrics follow the shared tokens rather than a private anonymous-namespace copy, so the
	// panel restyles with the rest of the UI instead of drifting from it.
	constexpr float ControlWidth = 100.0f;
	constexpr float ChoiceControlWidth = 140.0f;
	constexpr float ResetGap = 6.0f;

	// The tabs. Registry categories are mapped onto these, and anything unmapped gets a tab of its
	// own appended after them, so adding a category to the registry can never hide tokens.
	struct FTabDefinition
	{
		const TCHAR* Label;
		// Prefixes that belong to this tab. A prefix match keeps related sub-sections together
		// without enumerating every combination by hand.
		TArray<FString> Prefixes;
	};

	TArray<FTabDefinition> BuildTabDefinitions()
	{
		TArray<FTabDefinition> Tabs;
		Tabs.Add({ TEXT("Global"), { TEXT("Base Palette"), TEXT("Surfaces"), TEXT("Scalar Ramp") } });
		Tabs.Add({ TEXT("Controls"), { TEXT("Controls"), TEXT("Rows"), TEXT("Sliders") } });
		Tabs.Add({ TEXT("Foldouts"), { TEXT("Foldouts") } });
		Tabs.Add({ TEXT("Group Cards"), { TEXT("Group Cards") } });
		Tabs.Add({ TEXT("Layers"), { TEXT("Layers") } });
		Tabs.Add({ TEXT("Buttons"), { TEXT("Buttons") } });
		Tabs.Add({ TEXT("Menus"), { TEXT("Menus"), TEXT("Dialogs") } });
		Tabs.Add({ TEXT("Preview"), { TEXT("Preview") } });
		Tabs.Add({ TEXT("Gallery / Shell"), { TEXT("Galleries"), TEXT("Shell"), TEXT("Inspector") } });
		Tabs.Add({ TEXT("Typography"), { TEXT("Typography") } });
		return Tabs;
	}

	const TArray<FTabDefinition>& TabDefinitions()
	{
		static const TArray<FTabDefinition> Tabs = BuildTabDefinitions();
		return Tabs;
	}

	// The authored tab order: one tab per group, then one tab per registry category that no group
	// claimed. Position 0 is always All.
	TArray<FString> BuildTabLabels(const TArray<FString>& Categories, TArray<int32>& OutStripIndex)
	{
		TArray<FString> Labels;
		Labels.Add(FString()); // All
		OutStripIndex.Reset(Categories.Num());
		for (const FString& Category : Categories)
		{
			int32 Slot = INDEX_NONE;
			for (int32 Index = 0; Index < TabDefinitions().Num(); ++Index)
			{
				for (const FString& Prefix : TabDefinitions()[Index].Prefixes)
				{
					if (Category.StartsWith(Prefix))
					{
						Slot = Index + 1;
						break;
					}
				}
				if (Slot != INDEX_NONE)
				{
					break;
				}
			}
			if (Slot == INDEX_NONE)
			{
				// Unmapped: its own tab, so a new registry category can never hide its tokens.
				Slot = Labels.Num();
				Labels.Add(Category);
			}
			OutStripIndex.Add(Slot);
		}
		return Labels;
	}
}

void SMixtormatLiveThemePanel::Construct(const FArguments& InArgs)
{
	FMixtormatLiveTheme::Initialize();
	CanEdit = InArgs._CanEdit;
	OnThemeChanged = InArgs._OnThemeChanged;
	Status = TEXT("Changes preview automatically. Save stores this theme; Load restores it.");

	// One tab per authored group, plus a tab for every category the registry has that the authored
	// groups do not claim. Duplicates are removed, so this is stable across rebuilds.
	const TArray<FString> Categories = FMixtormatLiveTheme::Categories();
	TArray<int32> StripIndex;
	TabLabels = BuildTabLabels(Categories, StripIndex);

	TArray<FText> TabTexts;
	for (const FString& Label : TabLabels)
	{
		TabTexts.Add(Label.IsEmpty() ? LOCTEXT("AllCategories", "All") : FText::FromString(Label));
	}

	// The tab strip lives above the scrolling rows, inside a horizontal scroller: a tab row that
	// wraps reads as several groups of tabs and loses the single-active-row reading entirely.
	const TSharedRef<SBox> TabStrip = SNew(SBox)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SNew(SMixtormatTabStrip)
				.Options(TabTexts)
				.StretchTabs(false)
				.ActiveIndex_Lambda([this]()
				{
					// Strip slot 0 is All; SelectedTab stores that same index, so All is -1.
					return SelectedTab == INDEX_NONE ? 0 : SelectedTab;
				})
				.OnChosen(this, &SMixtormatLiveThemePanel::SelectTab)
			]
		];

	const TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);
	for (int32 CategoryIndex = 0; CategoryIndex < Categories.Num(); ++CategoryIndex)
	{
		const FString& Category = Categories[CategoryIndex];
		const int32 MappedTab = StripIndex[CategoryIndex];
		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		Rows->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap, 0.0f, MixtormatTokens::RowGap)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Category))
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowCaption")))
		];

		for (const FMixtormatThemeNumber& Entry : FMixtormatLiveTheme::Numbers())
		{
			if (!Entry.bExposeInUI || Entry.Category != Category)
			{
				continue;
			}
			const FString Name = Entry.Name.ToString();
			const FString Help = FString::Printf(
				TEXT("%s / %s\nDefault: %.*f. Range: %.*f to %.*f. Step: %.*f."),
				*Entry.Category, *Name,
				Entry.Precision, Entry.Default, Entry.Precision, Entry.Minimum,
				Entry.Precision, Entry.Maximum, Entry.Precision, Entry.Step);
			Rows->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(Help))
				.Visibility_Lambda([this, Name, MappedTab]()
				{
					const bool bTabVisible = SelectedTab == INDEX_NONE || SelectedTab == MappedTab;
					return bTabVisible && Matches(Name) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Name)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(ControlWidth)
					[
						SNew(SSpinBox<float>)
						.MinValue(Entry.Minimum).MaxValue(Entry.Maximum)
						.MinSliderValue(Entry.Minimum).MaxSliderValue(Entry.Maximum)
						// The nudge is the token's own step, not a category guess: a 0..1 opacity and a
						// pixel height are edited with different fingers.
						.Delta(Entry.Step)
						.Value_Lambda([Entry]() { return *Entry.Value; })
						// Drag updates the token only. Widgets that bind their colour per paint follow
						// along live, and nothing expensive happens until the release.
						.OnValueChanged_Lambda([Entry](const float Value)
						{
							FMixtormatLiveTheme::SetNumber(Entry.Name, Value);
						})
						// One workspace rebuild per committed edit, not one per drag tick.
						.OnValueCommitted_Lambda([this, Entry](const float Value, const ETextCommit::Type)
						{
							ChangeNumber(Value, Entry.Name);
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f).VAlign(VAlign_Center)
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

		for (const FMixtormatThemeBool& Entry : FMixtormatLiveTheme::Booleans())
		{
			if (!Entry.bExposeInUI || Entry.Category != Category)
			{
				continue;
			}
			const FString Name = Entry.Name.ToString();
			Rows->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(
					FString::Printf(TEXT("%s / %s\nDefault: %s"), *Entry.Category, *Name,
						Entry.Default ? TEXT("on") : TEXT("off"))))
				.Visibility_Lambda([this, Name, MappedTab]()
				{
					const bool bTabVisible = SelectedTab == INDEX_NONE || SelectedTab == MappedTab;
					return bTabVisible && Matches(Name) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Name)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				// A switch is a switch, not a number on a 0..1 slider: these used to be edited by
				// dragging a value between two numbers to say "yes" or "no".
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(ControlWidth).HAlign(HAlign_Left)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
						.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
						.IsChecked_Lambda([Entry]()
						{
							return *Entry.Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this, Entry](const ECheckBoxState State)
						{
							ChangeBool(State == ECheckBoxState::Checked, Entry.Name);
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SButton).Text(LOCTEXT("ResetBool", "Reset"))
					.IsEnabled_Lambda([Entry]() { return *Entry.Value != Entry.Default; })
					.OnClicked_Lambda([this, Entry]()
					{
						ChangeBool(Entry.Default, Entry.Name);
						return FReply::Handled();
					})
				]
			];
		}

		for (const FMixtormatThemeChoice& Entry : FMixtormatLiveTheme::Choices())
		{
			if (!Entry.bExposeInUI || Entry.Category != Category)
			{
				continue;
			}
			const FString Name = Entry.Name.ToString();
			Rows->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(FString::Printf(
					TEXT("%s / %s\nDefault: %s"), *Entry.Category, *Name,
					Entry.Options.IsValidIndex(Entry.Default) ? *Entry.Options[Entry.Default] : TEXT("?"))))
				.Visibility_Lambda([this, Name, MappedTab]()
				{
					const bool bTabVisible = SelectedTab == INDEX_NONE || SelectedTab == MappedTab;
					return bTabVisible && Matches(Name) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Name)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(ChoiceControlWidth)
					[
						SNew(SComboButton)
						.IsEnabled_Lambda([this]() { return CanEdit.Get(true); })
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text_Lambda([Entry]()
							{
								return Entry.Options.IsValidIndex(*Entry.Value)
									? FText::FromString(Entry.Options[*Entry.Value])
									: FText::GetEmpty();
							})
						]
						.OnGetMenuContent_Lambda([this, Entry]()
						{
							TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
							for (int32 OptionIndex = 0; OptionIndex < Entry.Options.Num(); ++OptionIndex)
							{
								const FString Option = Entry.Options[OptionIndex];
								Menu->AddSlot().AutoHeight()
								[
									SNew(SButton)
									.OnClicked_Lambda([this, Entry, OptionIndex]()
									{
										ChangeChoice(OptionIndex, Entry.Name);
										return FReply::Handled();
									})
									[
										SNew(STextBlock).Text(FText::FromString(Option))
									]
								];
							}
							return SNew(SMixtormatMenuPanel)
								.MinWidth(ChoiceControlWidth)
								[
									Menu
								];
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f).VAlign(VAlign_Center)
				[
					SNew(SButton).Text(LOCTEXT("ResetChoice", "Reset"))
					.IsEnabled_Lambda([Entry]() { return *Entry.Value != Entry.Default; })
					.OnClicked_Lambda([this, Entry]()
					{
						ChangeChoice(Entry.Default, Entry.Name);
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
			const FString Name = Entry.Name.ToString();
			Rows->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(FText::FromString(Entry.Category + TEXT(" / ") + Name
					+ TEXT("\nDefault (linear RGBA): ") + Entry.Default.ToString()
					+ TEXT(". Range: 0-1 per channel.")))
				.Visibility_Lambda([this, Name, MappedTab]()
				{
					const bool bTabVisible = SelectedTab == INDEX_NONE || SelectedTab == MappedTab;
					return bTabVisible && Matches(Name) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromName(Entry.Name)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
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
							.Color_Lambda([Entry]()
							{
								return FMixtormatLiveTheme::ResolveColor(Entry.Name, Entry.Default);
							})
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
			// Scoped to this category, not the whole registry: otherwise the first section header
			// would stay on screen with every row under it filtered away.
			.Visibility_Lambda([this, Category, MappedTab]()
			{
				if (SelectedTab != INDEX_NONE && SelectedTab != MappedTab)
				{
					return EVisibility::Collapsed;
				}
				const auto AnyMatch = [this, &Category](const auto& Entry)
				{
					return Entry.bExposeInUI && Entry.Category == Category
						&& Matches(Entry.Name.ToString());
				};
				for (const FMixtormatThemeNumber& Entry : FMixtormatLiveTheme::Numbers())
				{
					if (AnyMatch(Entry)) { return EVisibility::Visible; }
				}
				for (const FMixtormatThemeBool& Entry : FMixtormatLiveTheme::Booleans())
				{
					if (AnyMatch(Entry)) { return EVisibility::Visible; }
				}
				for (const FMixtormatThemeChoice& Entry : FMixtormatLiveTheme::Choices())
				{
					if (AnyMatch(Entry)) { return EVisibility::Visible; }
				}
				for (const FMixtormatThemeColor& Entry : FMixtormatLiveTheme::Colors())
				{
					if (AnyMatch(Entry)) { return EVisibility::Visible; }
				}
				return EVisibility::Collapsed;
			})
			[Rows]
		];
	}

	Sections->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
	[
		SNew(STextBlock).Text(LOCTEXT("NoMatchingTokens", "No matching tokens in this tab."))
		.Visibility_Lambda([this]()
		{
			return HasMatches() ? EVisibility::Collapsed : EVisibility::Visible;
		})
	];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(MixtormatTokens::PanelPadding)
		.IsEnabled(CanEdit)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ThemeHelp", "Live UI styling. Drags preview on release, so a change costs one rebuild rather than one per tick."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
			[
				SNew(SSearchBox)
				.HintText_Lambda([this]()
				{
					return SelectedTab == INDEX_NONE
						? LOCTEXT("SearchAllTokens", "Search all names or categories...")
						: FText::Format(LOCTEXT("SearchTabTokens", "Search within {0}..."),
							FText::FromString(TabLabels.IsValidIndex(SelectedTab)
								? TabLabels[SelectedTab] : FString()));
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
			// Tabs and search stay put; only the token rows scroll.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
			[TabStrip]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[SAssignNew(TokenScroll, SScrollBox) + SScrollBox::Slot()[Sections]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("SaveTheme", "Save")).OnClicked(this, &SMixtormatLiveThemePanel::Save)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(ResetGap, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("LoadTheme", "Load"))
					.IsEnabled_Lambda([]()
					{
						return IFileManager::Get().FileExists(*FMixtormatLiveTheme::SavePath());
					})
					.OnClicked(this, &SMixtormatLiveThemePanel::Load)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("ResetTheme", "Reset All")).OnClicked(this, &SMixtormatLiveThemePanel::ResetAll)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Status); }).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(FMixtormatLiveTheme::SavePath())).AutoWrapText(true)
			]
		]
	];
}

bool SMixtormatLiveThemePanel::Matches(const FString& Name) const
{
	return Filter.IsEmpty() || Name.Contains(Filter);
}

bool SMixtormatLiveThemePanel::HasMatches() const
{
	for (const FMixtormatThemeNumber& Entry : FMixtormatLiveTheme::Numbers())
	{
		if (Entry.bExposeInUI && Matches(Entry.Name.ToString()))
		{
			return true;
		}
	}
	for (const FMixtormatThemeBool& Entry : FMixtormatLiveTheme::Booleans())
	{
		if (Entry.bExposeInUI && Matches(Entry.Name.ToString()))
		{
			return true;
		}
	}
	for (const FMixtormatThemeChoice& Entry : FMixtormatLiveTheme::Choices())
	{
		if (Entry.bExposeInUI && Matches(Entry.Name.ToString()))
		{
			return true;
		}
	}
	for (const FMixtormatThemeColor& Entry : FMixtormatLiveTheme::Colors())
	{
		if (Entry.bExposeInUI && Matches(Entry.Name.ToString()))
		{
			return true;
		}
	}
	return false;
}

void SMixtormatLiveThemePanel::SelectTab(const int32 Index)
{
	// Strip slot 0 is All, which is stored as -1 so every row's "no tab selected" test is one
	// comparison rather than a special case per row type.
	SelectedTab = Index <= 0 ? INDEX_NONE : Index;
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

void SMixtormatLiveThemePanel::ChangeBool(const bool Value, const FName Name)
{
	if (CanEdit.Get(true) && FMixtormatLiveTheme::SetBool(Name, Value))
	{
		Status = TEXT("Preview update queued. Save to keep these settings.");
		OnThemeChanged.ExecuteIfBound();
	}
}

void SMixtormatLiveThemePanel::ChangeChoice(const int32 Value, const FName Name)
{
	if (CanEdit.Get(true) && FMixtormatLiveTheme::SetChoice(Name, Value))
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
		Args.bOnlyRefreshOnMouseUp = true;
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
