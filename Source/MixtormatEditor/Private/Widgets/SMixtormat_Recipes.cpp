// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "Services/MixtormatPresetStore.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatRecipes.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/Gallery/SMixtormatGalleryScrollBox.h"
#include "Widgets/Gallery/SMixtormatSurfaceCard.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormat::RefreshRecipeGallery()
{
	TArray<MixtormatPresetStore::FRecipePreset> Presets;
	MixtormatPresetStore::ListRecipes(Presets, RecipeGalleryError);
	RecipeEntries.Reset();
	for (MixtormatPresetStore::FRecipePreset& Preset : Presets)
	{
		RecipeEntries.Add(MakeShared<MixtormatPresetStore::FRecipePreset>(MoveTemp(Preset)));
	}
	if (!RecipeCategoryFilter.IsEmpty() && !RecipeEntries.ContainsByPredicate([this](const TSharedPtr<MixtormatPresetStore::FRecipePreset>& Entry)
		{ return Entry->Category == RecipeCategoryFilter; })) { RecipeCategoryFilter.Reset(); }
	RebuildRecipeGallery();
}

void SMixtormat::RebuildRecipeGallery()
{
	if (!RecipeListBox.IsValid()) { return; }
	RecipeListBox->ClearChildren();
	const auto AddText = [this](const FText& Text)
	{
		RecipeListBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(Text).AutoWrapText(true)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		];
	};
	if (!RecipeGalleryError.IsEmpty()) { AddText(RecipeGalleryError); }
	int32 Visible = 0;
	for (const TSharedPtr<MixtormatPresetStore::FRecipePreset>& Entry : RecipeEntries)
	{
		if (!RecipeCategoryFilter.IsEmpty() && Entry->Category != RecipeCategoryFilter) { continue; }
		if (!RecipeSearchText.IsEmpty() && !(Entry->Name + TEXT(" ") + Entry->Category + TEXT(" ") + Entry->Description).Contains(RecipeSearchText)) { continue; }
		++Visible;
		const FString Filename = Entry->Filename;
		const FText Origin = Entry->bShipped ? LOCTEXT("ShippedRecipeBadge", "Shipped · Read-only") : LOCTEXT("UserRecipeBadge", "User Recipe");
		RecipeListBox->AddSlot().AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().GalleryLayout.TileGap)
		[
			SNew(SBox).MinDesiredHeight(FMixtormatThemeStore::GetResolved().ShellLayout.LibraryRowHeight)
			[
				SNew(SMixtormatSurfaceCard)
				.DisplayName(FText::Format(LOCTEXT("RecipeCardHelp", "{0}\n{1}\nClick to insert an editable group; right-click for details and export."), FText::FromString(Entry->Name), Origin))
				.AllowSurfaceDrag(false)
				.OnSelected(FOnMixtormatSurfaceSelected::CreateLambda([this, Filename](FText, FSoftObjectPath) { return InsertRecipe(Filename); }))
				.OnGetContextMenu(FOnGetContent::CreateSP(this, &SMixtormat::BuildRecipeEntryMenu, Filename))
				[
					SNew(SMixtormatSurfaceBox).Recipe_Lambda([]() { return Mixtormat::MakeGroundRecipe(); })
					.Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.TilePadding)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(Entry->Name)).AutoWrapText(true)
							.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(Origin).AutoWrapText(true)
							.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("RecipeLayerCount", "{0} layers · {1}"), FText::AsNumber(Entry->Layers.Num()), FText::FromString(Entry->Category)))
							.AutoWrapText(true)
							.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
						]
					]
				]
			]
		];
	}
	if (Visible == 0)
	{
		AddText(LOCTEXT("RecipesEmpty", "No matching recipes. Right-click a group → Create Recipe, or import JSON from the Recipes menu."));
	}
}

TSharedRef<SWidget> SMixtormat::BuildRecipeGallery()
{
	const TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight()
	[
		SNew(SMixtormatHelp).Text(LOCTEXT("RecipeGalleryHelp", "Editable group recipes, not live composition references. Shipped entries are read-only; referenced assets must be installed."))
		[
			SNew(SComboButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
			.HasDownArrow(false).ContentPadding(FMargin(FMixtormatThemeStore::GetResolved().GalleryLayout.TilePadding))
			.OnGetMenuContent(this, &SMixtormat::BuildRecipeGalleryActions)
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("RecipesGalleryTitle", "RECIPES"))
				.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.GroupButtonText")))
			]
		]
	];
	Panel->AddSlot().AutoHeight().Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.TilePadding)
	[
		SNew(SMixtormatTextFieldGradient)
		[
			SNew(SEditableTextBox)
			.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
			.HintText(LOCTEXT("RecipeSearchHint", "Search recipes"))
			.Text(FText::FromString(RecipeSearchText))
			.OnTextChanged_Lambda([this](const FText& Text) { RecipeSearchText = Text.ToString(); RebuildRecipeGallery(); })
		]
	];
	Panel->AddSlot().AutoHeight()
	[
		SNew(SComboButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
		.HasDownArrow(false).ContentPadding(FMargin(FMixtormatThemeStore::GetResolved().GalleryLayout.TilePadding))
		.OnGetMenuContent_Lambda([this]()
		{
			MixtormatMenu::FBuilder Menu;
			Menu.Item(LOCTEXT("RecipeAllCategories", "All Categories"), nullptr,
				FSimpleDelegate::CreateLambda([this]() { RecipeCategoryFilter.Reset(); RebuildRecipeGallery(); }));
			TArray<FString> Categories;
			for (const TSharedPtr<MixtormatPresetStore::FRecipePreset>& Entry : RecipeEntries)
			{ if (!Entry->Category.IsEmpty()) { Categories.AddUnique(Entry->Category); } }
			Categories.Sort();
			for (const FString& Category : Categories)
			{
				Menu.Item(FText::FromString(Category), nullptr, FSimpleDelegate::CreateLambda([this, Category]()
				{ RecipeCategoryFilter = Category; RebuildRecipeGallery(); }));
			}
			return Menu.Build();
		})
		.ButtonContent()
		[
			SNew(STextBlock).Text_Lambda([this]() { return RecipeCategoryFilter.IsEmpty() ? LOCTEXT("RecipeAllCategories", "All Categories") : FText::FromString(RecipeCategoryFilter); })
			.AutoWrapText(true)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		]
	];
	Panel->AddSlot().FillHeight(1.0f).Padding(FMixtormatThemeStore::GetResolved().GalleryLayout.TilePadding)
	[
		SNew(SMixtormatGalleryScrollBox)
		[
			SAssignNew(RecipeListBox, SVerticalBox)
		]
	];
	RefreshRecipeGallery();
	return Panel;
}

#undef LOCTEXT_NAMESPACE
