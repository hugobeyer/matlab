// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatParameterBinding.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

#include "ObjectTools.h"

// Shipped surface galleries and the user-owned saved-mix/imported-surface library.

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	bool bSuppressDeveloperRefreshSuccessDialog = false;

	class SMixtormatCompositionCard final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatCompositionCard) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FOnGetContent, OnGetContextMenu)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				SAssignNew(ContextAnchor, SMenuAnchor)
				.Placement(MenuPlacement_MenuRight)
				.UseApplicationMenuStack(true)
				.OnGetMenuContent(InArgs._OnGetContextMenu)
				[
					InArgs._Content.Widget
				]
			];
		}

		virtual FReply OnPreviewMouseButtonDown(
			const FGeometry& Geometry,
			const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && ContextAnchor.IsValid())
			{
				ContextAnchor->SetIsOpen(true);
				return FReply::Handled();
			}
			return SCompoundWidget::OnPreviewMouseButtonDown(Geometry, MouseEvent);
		}

	private:
		TSharedPtr<SMenuAnchor> ContextAnchor;
	};
}

FReply SMixtormat::RefreshSurfaceList()
{
	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildUserLibraryList();
	RebuildMaskList();
	return FReply::Handled();
}

void SMixtormat::ZoomMaterialGallery(const int32 Direction)
{
	MaterialGalleryTileSize = FMath::Clamp(
		MaterialGalleryTileSize + Direction * MixtormatTokens::MaterialGalleryTileStep,
		MixtormatTokens::MaterialGalleryTileMinimum,
		MixtormatTokens::MaterialGalleryTileMaximum);
	RebuildSurfaceList();
}

FReply SMixtormat::SelectMask(FText DisplayName, FSoftObjectPath AssetPath)
{
	SelectedMaskPath = AssetPath;
	SelectedLibraryMaskName = DisplayName;
	return FReply::Handled();
}

FReply SMixtormat::SelectSurface(FText DisplayName, FSoftObjectPath AssetPath)
{
	SelectedSurfacePath = AssetPath;
	SelectedLibrarySurfaceName = DisplayName;
	if (bHasWorkingMaterial)
	{
		return FReply::Handled();
	}

	bHasSelectedLayer = false;
	SelectedLayerIndex = INDEX_NONE;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	WorkingLayers.Reset();
	WorkingMaterialName = TEXT("No material");
	if (SelectedSurfaceText.IsValid())
	{
		SelectedSurfaceText->SetText(LOCTEXT("NoSelectedLayer", "No layer selected"));
	}
	if (WorkingBaseLayerText.IsValid())
	{
		WorkingBaseLayerText->SetText(DisplayName);
	}

	const UMixtormatSurface* Surface = Cast<UMixtormatSurface>(AssetPath.TryLoad());
	if (Surface)
	{
		SelectedPreviewMaterial.Reset(Cast<UMaterialInstanceConstant>(Surface->PreviewMaterial.Get()));
		bHasWorkingMaterial = false;
		if (SelectedPreviewMaterial.IsValid())
		{
			CurrentTiling = FMath::Max(1.0f, FMath::RoundToFloat(
				UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
					SelectedPreviewMaterial.Get(), TEXT("DA_Tiling"))));
			CurrentRoughnessBias = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("DA_RoughnessBias"));
			CurrentRoughnessContrast = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("DA_RoughnessContrast"));
			CurrentRoughnessOffset = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("DA_RoughnessOffset"));
		}
		if (SelectedIdentityText.IsValid())
		{
			SelectedIdentityText->SetText(FText::FromName(Surface->Family));
		}
		if (bPreviewDisplacementEnabled)
		{
			PreviewSelectedSurfaceWithDisplacement();
		}
		else
		{
			for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
			{
				if (Viewport.IsValid())
				{
					Viewport->SetPreviewMaterial(Surface->PreviewMaterial);
				}
			}
		}
	}

	RebuildLayerList();
	return FReply::Handled();
}

FReply SMixtormat::HandleSurfaceDropped(FText DisplayName, FSoftObjectPath AssetPath)
{
	SelectSurface(DisplayName, AssetPath);
	return bHasWorkingMaterial
		? AddWorkingLayer(EMixtormatLayerType::Material)
		: StartNewMaterial();
}

FReply SMixtormat::SetCategoryFilter(const FName Family)
{
	CategoryFilter = Family;
	RebuildCategoryList();
	RebuildSurfaceList();
	return FReply::Handled();
}

void SMixtormat::HandleSearchChanged(const FText& SearchTextValue)
{
	SearchText = SearchTextValue.ToString();
	RebuildSurfaceList();
}

void SMixtormat::RebuildCategoryList()
{
	if (!CategoryListBox.IsValid())
	{
		return;
	}

	TArray<FName> Families;
	for (const FMixtormatSurfaceEntry& Surface : FMixtormatRegistry::GetSurfaces())
	{
		if (!MixtormatUI::IsUserLibraryAsset(Surface.AssetPath))
		{
			Families.AddUnique(Surface.Family);
		}
	}
	Families.Remove(NAME_None);
	Families.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	if (!CategoryFilter.IsNone() && !Families.Contains(CategoryFilter))
	{
		CategoryFilter = NAME_None;
	}

	CategoryListBox->ClearChildren();
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const auto AddCategory = [this, &Style](const FName Family, const FText& Label)
	{
		CategoryListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, CategoryListBox->GetChildren()->Num() > 0 ? 4.0f : 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(
				CategoryFilter == Family ? TEXT("Mixtormat.TabButtonActive") : TEXT("Mixtormat.TabButton")))
			.Text(Label)
			.OnClicked_Lambda([this, Family]() { return SetCategoryFilter(Family); })
		];
	};

	AddCategory(NAME_None, LOCTEXT("AllCategory", "All Materials"));
	for (const FName Family : Families)
	{
		AddCategory(Family, FText::FromName(Family));
	}
}

void SMixtormat::RebuildSurfaceList()
{
	if (!SurfaceListBox.IsValid())
	{
		return;
	}

	SurfaceListBox->ClearChildren();
	const TArray<FMixtormatSurfaceEntry> Surfaces = FMixtormatRegistry::GetSurfaces();
	int32 VisibleSurfaceIndex = 0;
	for (const FMixtormatSurfaceEntry& Surface : Surfaces)
	{
		if (MixtormatUI::IsUserLibraryAsset(Surface.AssetPath)
			|| (!CategoryFilter.IsNone() && Surface.Family != CategoryFilter))
		{
			continue;
		}
		if (!SearchText.IsEmpty() && !Surface.DisplayName.ToString().Contains(SearchText))
		{
			continue;
		}

		SurfaceListBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			BuildSurfaceCard(Surface.DisplayName, Surface.AssetPath, Surface.ThumbnailAsset)
		];
		++VisibleSurfaceIndex;
	}

	if (VisibleSurfaceIndex == 0)
	{
		SurfaceListBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(Surfaces.IsEmpty()
				? LOCTEXT("EmptyRegistry", "Built-in Mixtormat content is missing. Repair or reinstall Mixtormat, or import a texture folder.")
				: LOCTEXT("NoMatchingSurfaces", "No materials match the current filters."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

void SMixtormat::HandleUserLibrarySearchChanged(const FText& SearchTextValue)
{
	UserLibrarySearchText = SearchTextValue.ToString();
	RebuildUserLibraryList();
}

void SMixtormat::RebuildUserLibraryList()
{
	if (!UserLibraryListBox.IsValid())
	{
		return;
	}

	UserLibraryListBox->ClearChildren();
	const ISlateStyle& Style = FMixtormatStyle::Get();
	int32 VisibleItemCount = 0;
	const auto AddHeading = [this, &Style](const FText& Label)
	{
		UserLibraryListBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f, UserLibraryListBox->GetChildren()->Num() > 0 ? 10.0f : 2.0f, 2.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontSliderLabel))
		];
	};
	const auto AddCard = [this, &Style](
		const FText& Name,
		const FText& Detail,
		const FOnGetContent& ContextMenu)
	{
		UserLibraryListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SMixtormatCompositionCard)
			.OnGetContextMenu(ContextMenu)
			[
				SNew(SBorder)
				.Padding(FMargin(8.0f, 7.0f))
				.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
				.ToolTipText(LOCTEXT("UserLibraryItemHint", "Right-click for actions."))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(Name)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontSliderLabel))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(Detail)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			]
		];
	};

	bool bAddedCompositionHeading = false;
	for (const FMixtormatCompositionEntry& Composition : FMixtormatRegistry::GetCompositions())
	{
		if (!UserLibrarySearchText.IsEmpty()
			&& !Composition.DisplayName.ToString().Contains(UserLibrarySearchText))
		{
			continue;
		}
		if (!bAddedCompositionHeading)
		{
			AddHeading(LOCTEXT("SavedMixesHeading", "SAVED MIXES"));
			bAddedCompositionHeading = true;
		}
		AddCard(
			Composition.DisplayName,
			FText::Format(
				LOCTEXT("CompositionLayerCount", "{0} editable layer(s) · right-click"),
				FText::AsNumber(Composition.LayerCount)),
			FOnGetContent::CreateSP(
				this,
				&SMixtormat::BuildCompositionLibraryContextMenu,
				Composition.AssetPath));
		++VisibleItemCount;
	}

	bool bAddedSurfaceHeading = false;
	for (const FMixtormatSurfaceEntry& Surface : FMixtormatRegistry::GetSurfaces())
	{
		if (!MixtormatUI::IsUserLibraryAsset(Surface.AssetPath)
			|| (!UserLibrarySearchText.IsEmpty()
				&& !Surface.DisplayName.ToString().Contains(UserLibrarySearchText)))
		{
			continue;
		}
		if (!bAddedSurfaceHeading)
		{
			AddHeading(LOCTEXT("ImportedSurfacesHeading", "IMPORTED SURFACES"));
			bAddedSurfaceHeading = true;
		}
		AddCard(
			Surface.DisplayName,
			FText::Format(
				LOCTEXT("ImportedSurfaceDetail", "{0} · right-click"),
				FText::FromName(Surface.Family)),
			FOnGetContent::CreateSP(
				this,
				&SMixtormat::BuildSurfaceLibraryContextMenu,
				Surface.AssetPath));
		++VisibleItemCount;
	}

	if (VisibleItemCount == 0)
	{
		UserLibraryListBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(UserLibrarySearchText.IsEmpty()
				? LOCTEXT("EmptyUserLibrary", "No saved mixes or imported surfaces yet.")
				: LOCTEXT("NoMatchingUserLibrary", "No user content matches this search."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

TSharedRef<SWidget> SMixtormat::BuildUserLibraryPage()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const TSharedRef<SSearchBox> SearchBox = SNew(SSearchBox)
		.HintText(LOCTEXT("SearchUserLibraryHint", "Search saved mixes..."))
		.OnTextChanged_Lambda([this](const FText& Text)
		{
			if (UserLibrarySearchText != Text.ToString())
			{
				HandleUserLibrarySearchChanged(Text);
			}
		});
	SearchBox->SetText(FText::FromString(UserLibrarySearchText));

	return SNew(SBorder)
		.Padding(MixtormatTokens::PanelPadding)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SearchBox
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT("ChooseUserTextureFolderHint", "Import a texture folder into the user library"))
					.OnClicked(this, &SMixtormat::ImportSurfaces)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::ToolbarIconSize)
						.HeightOverride(MixtormatTokens::ToolbarIconSize)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Folder")))
						]
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(UserLibraryListBox, SVerticalBox)
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBottomLibrary()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	return SNew(SBorder)
		.Padding(3.0f)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(MixtormatTokens::SplitterHandleSize)
			.HitDetectionSplitterHandleSize(MixtormatTokens::SplitterHitSize)
			+ SSplitter::Slot()
						.Value_Lambda([this]() { return MaterialLibraryFraction; })
						.OnSlotResized_Lambda([this](float Value) { MaterialLibraryFraction = Value; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MaterialsColumn", "MATERIALS"))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontSliderLabel))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(150.0f)[BuildLibraryPage()]]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4.0f, 0.0f)[BuildSurfaceList()]
				]
			]
			+ SSplitter::Slot()
						.Value_Lambda([this]() { return MaskLibraryFraction; })
						.OnSlotResized_Lambda([this](float Value) { MaskLibraryFraction = Value; })
			[
				BuildMaskBar()
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildLibraryPage()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const bool bHasDeveloperSources =
		!FMixtormatSurfaceImporter::EnumerateShippedSourceDirectories().IsEmpty();
	const TSharedRef<SSearchBox> SearchBox = SNew(SSearchBox)
		.HintText(LOCTEXT("SearchHint", "Search materials..."))
		.OnTextChanged_Lambda([this](const FText& Text)
		{
			// Restoring the text during a style rebuild must not rebuild the old surface list.
			if (SearchText != Text.ToString())
			{
				HandleSearchChanged(Text);
			}
		});
	SearchBox->SetText(FText::FromString(SearchText));
	return SNew(SBorder)
		.Padding(MixtormatTokens::PanelPadding)
		.BorderImage(Style.GetBrush(TEXT("Mixtormat.Panel")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SearchBox
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(CategoryListBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)[SNew(SSpacer)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Visibility(bHasDeveloperSources ? EVisibility::Visible : EVisibility::Collapsed)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT("RefreshLibraryHint", "Refresh Library"))
					.OnClicked(this, &SMixtormat::RefreshSurfaceList)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::ToolbarIconSize)
						.HeightOverride(MixtormatTokens::ToolbarIconSize)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Refresh")))
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::LibraryBrowseButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Visibility(bHasDeveloperSources ? EVisibility::Visible : EVisibility::Collapsed)
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT(
						"RebuildBuiltInLibraryHint",
						"Developer only: rebuild built-in assets from plugin source PNGs"))
					.OnClicked(this, &SMixtormat::RebuildBuiltInLibrary)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::ToolbarIconSize)
						.HeightOverride(MixtormatTokens::ToolbarIconSize)
						[
							SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Settings")))
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceList()
{
	return SNew(SMixtormatGalleryScrollBox)
		.Orientation(Orient_Vertical)
		.OnGalleryZoom(this, &SMixtormat::ZoomMaterialGallery)
		[
			SAssignNew(SurfaceListBox, SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(
				MixtormatTokens::MaterialGalleryTileGap,
				MixtormatTokens::MaterialGalleryTileGap))
		];
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceCard(
	const FText& Name,
	const FSoftObjectPath& AssetPath,
	const FAssetData& ThumbnailAsset)
{
	FText HoverName = Name;
	if (const UMixtormatSurface* Surface = Cast<UMixtormatSurface>(AssetPath.ResolveObject()))
	{
		const FString Family = Surface->Family.ToString();
		if (!Family.IsEmpty()
			&& !Name.ToString().StartsWith(Family, ESearchCase::IgnoreCase))
		{
			HoverName = FText::Format(
				LOCTEXT("SurfaceHoverNameWithFamily", "{0} · {1}"),
				FText::FromString(Family),
				Name);
		}
	}

	return SNew(SMixtormatSurfaceCard)
		.DisplayName(Name)
		.SurfacePath(AssetPath)
		.ThumbnailAsset(ThumbnailAsset)
		.ThumbnailPool(ThumbnailPool)
		.OnSelected(this, &SMixtormat::SelectSurface)
		.OnGalleryZoom(this, &SMixtormat::ZoomMaterialGallery)
		.OnGetContextMenu(this, &SMixtormat::BuildSurfaceLibraryContextMenu, AssetPath)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SMixtormatTile)
				.TileSize(MaterialGalleryTileSize)
				.DisplayName(HoverName)
				.ThumbnailAsset(ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.bShowName(false)
				.bShowNameOnHover(false)
				.bSelected_Lambda([this, AssetPath]() { return SelectedSurfacePath == AssetPath; })
				.ToolTip(HoverName)
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
			[
				MixtormatUI::BuildLibraryOwnershipBadge(AssetPath)
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceLibraryContextMenu(const FSoftObjectPath AssetPath)
{
	const bool bIsUserAsset = MixtormatUI::IsUserLibraryAsset(AssetPath);
	const UMixtormatSurface* Surface = Cast<UMixtormatSurface>(AssetPath.TryLoad());
	const bool bCanDeveloperRefresh = !bIsUserAsset
		&& Surface
		&& !Surface->SourceTextureBaseName.IsEmpty()
		&& !FMixtormatSurfaceImporter::EnumerateShippedSourceDirectories().IsEmpty();
	MixtormatMenu::FBuilder Menu;
	Menu.Caption(LOCTEXT("LibraryMaterialContextCaption", "Library Material"))
		.Item(
			LOCTEXT("AddLibraryMaterialLayer", "Add as Material Layer"),
			MixtormatUI::LucideIcon(TEXT("plus")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::AddSurfaceFromLibrary, AssetPath))
		.Item(
			LOCTEXT("BrowseLibraryMaterial", "Show in Content Browser"),
			MixtormatUI::LucideIcon(TEXT("folder-open")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::BrowseLibraryAsset, AssetPath));
	if (bCanDeveloperRefresh)
	{
		Menu.Item(
			LOCTEXT("RefreshBuiltInMaterial", "Developer: Refresh / Reimport from Source"),
			MixtormatUI::LucideIcon(TEXT("refresh-cw")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::RefreshBuiltInSurface, AssetPath));
	}
	Menu.Separator()
		.Item(
			LOCTEXT("RemoveImportedMaterial", "Remove Imported Material…"),
			MixtormatUI::LucideIcon(TEXT("trash-2")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::RemoveImportedSurface, AssetPath))
		.Enabled(bIsUserAsset)
		.Destructive();
	return Menu.Build();
}

void SMixtormat::RefreshBuiltInSurface(const FSoftObjectPath AssetPath)
{
	const FMixtormatImportResult Result =
		FMixtormatSurfaceImporter::ReimportShippedSurface(AssetPath);
	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildUserLibraryList();

	const bool bSucceeded = Result.Errors.IsEmpty();
	WorkingStatusText = bSucceeded
		? TEXT("Developer material refresh completed")
		: TEXT("Developer material refresh failed");
	if (bSucceeded && bSuppressDeveloperRefreshSuccessDialog)
	{
		return;
	}

	bool bSuppressSuccessFeedback = false;
	TSharedPtr<SWindow> FeedbackWindow;
	SAssignNew(FeedbackWindow, SWindow)
		.Title(LOCTEXT("DeveloperRefreshResultTitle", "Material Refresh"))
		.ClientSize(FVector2D(520.0f, 210.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
			.Padding(MixtormatTokens::DialogPadding)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(STextBlock)
					.Text(Result.ToMessage())
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f)
				[
					SNew(SCheckBox)
					.IsChecked(ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([&bSuppressSuccessFeedback](const ECheckBoxState State)
					{
						bSuppressSuccessFeedback = State == ECheckBoxState::Checked;
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"SuppressDeveloperRefreshSuccess",
							"Don't show successful refresh feedback again this session"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("CloseDeveloperRefreshResult", "OK"))
					.OnClicked_Lambda([&FeedbackWindow]()
					{
						FeedbackWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		];
	FSlateApplication::Get().AddModalWindow(
		FeedbackWindow.ToSharedRef(),
		FSlateApplication::Get().FindWidgetWindow(AsShared()),
		false);
	bSuppressDeveloperRefreshSuccessDialog |= bSuppressSuccessFeedback;
}

TSharedRef<SWidget> SMixtormat::BuildCompositionLibraryContextMenu(const FSoftObjectPath AssetPath)
{
	MixtormatMenu::FBuilder Menu;
	// References keep the recipe live; the other actions import its layers or its existing bake.
	// Nothing is baked here: the shared compositor belongs to the open document.
	const UMixtormatMaterial* Composition = Cast<UMixtormatMaterial>(AssetPath.TryLoad());
	const bool bHasBakedSurface = Composition && !Composition->BakedSurface.IsNull();
	Menu.Caption(LOCTEXT("SavedMixContextCaption", "Saved Mix"))
		.Item(
			LOCTEXT("AddCompositionReferenceLayer", "Add as Reference Layer"),
			MixtormatUI::LucideIcon(TEXT("layers")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::AddReferenceLayerFromComposition, AssetPath))
		.Enabled(Composition != nullptr)
		.Item(
			LOCTEXT("AddCompositionLayers", "Add All Layers"),
			MixtormatUI::LucideIcon(TEXT("layers")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::AddCompositionLayers, AssetPath))
		.Item(
			LOCTEXT("AddCompositionBakedLayer", "Add as Baked Layer"),
			MixtormatUI::LucideIcon(TEXT("box")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::AddBakedLayerFromComposition, AssetPath))
		.Enabled(bHasBakedSurface)
		.Item(
			LOCTEXT("BrowseComposition", "Show in Content Browser"),
			MixtormatUI::LucideIcon(TEXT("folder-open")),
			FSimpleDelegate::CreateSP(this, &SMixtormat::BrowseLibraryAsset, AssetPath));
	return Menu.Build();
}

void SMixtormat::AddSurfaceFromLibrary(const FSoftObjectPath AssetPath)
{
	const UMixtormatSurface* Surface = Cast<UMixtormatSurface>(AssetPath.TryLoad());
	if (!Surface)
	{
		WorkingStatusText = TEXT("Library surface could not be loaded");
		return;
	}
	const FText DisplayName = Surface->DisplayName.IsEmpty()
		? FText::FromString(Surface->GetName())
		: Surface->DisplayName;
	HandleSurfaceDropped(DisplayName, AssetPath);
}

// The flattened counterpart to AddCompositionLayers: one layer sourcing the mix's own baked
// surface, instead of every layer that produced it. Per-pixel F0 is the one thing that does not
// survive the bake -- it shared the alpha the height now occupies -- so the surface stands in
// with a single IOR.
void SMixtormat::AddBakedLayerFromComposition(const FSoftObjectPath AssetPath)
{
	const UMixtormatMaterial* Composition = Cast<UMixtormatMaterial>(AssetPath.TryLoad());
	if (!Composition)
	{
		WorkingStatusText = TEXT("Saved mix could not be loaded");
		return;
	}
	if (Composition->BakedSurface.IsNull())
	{
		WorkingStatusText = TEXT("Saved mix has no bake yet · bake it first");
		return;
	}
	const UMixtormatSurface* Surface = Composition->BakedSurface.LoadSynchronous();
	if (!Surface)
	{
		WorkingStatusText = TEXT("Baked surface is missing from disk");
		return;
	}

	const FText DisplayName = Composition->DisplayName.IsEmpty()
		? FText::FromString(Composition->GetName())
		: Composition->DisplayName;
	// Same path a library surface takes when it is dropped onto the stack: select it, then add
	// one Material layer that sources it.
	HandleSurfaceDropped(DisplayName, FSoftObjectPath(Surface));
}

void SMixtormat::AddReferenceLayerFromComposition(const FSoftObjectPath AssetPath)
{
	const UMixtormatMaterial* Composition = Cast<UMixtormatMaterial>(AssetPath.TryLoad());
	if (!Composition)
	{
		WorkingStatusText = TEXT("Saved mix could not be loaded");
		return;
	}

	TArray<FMixtormatLayer> ReferenceLayers;
	FMixtormatLayer& Reference = ReferenceLayers.AddDefaulted_GetRef();
	Reference.Type = EMixtormatLayerType::Material;
	Reference.SourceSurface.Reset();
	Reference.SourceComposition = TSoftObjectPtr<UMixtormatMaterial>(AssetPath);
	Reference.DisplayName = Composition->DisplayName.IsEmpty()
		? FText::FromString(Composition->GetName())
		: Composition->DisplayName;

	FText ReferenceError;
	const FSoftObjectPath OwnerPath = bHasWorkingMaterial && WorkingMaterialAsset.IsValid()
		? FSoftObjectPath(WorkingMaterialAsset.Get()) : FSoftObjectPath();
	if (!MixtormatCompositionReferences::Validate(ReferenceLayers, OwnerPath, ReferenceError))
	{
		WorkingStatusText = ReferenceError.ToString();
		return;
	}

	const bool bCreateDocument = !bHasWorkingMaterial;
	if (bCreateDocument)
	{
		bHasWorkingMaterial = true;
		WorkingMaterialAsset.Reset();
		WorkingMaterialName = TEXT("Untitled Mixtormat Material");
		WorkingLayers.Reset();
		// The isolated source already includes its own document rotation.
		bGlobalUVRotation90 = false;
		for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
		{
			if (Viewport.IsValid())
			{
				Viewport->SetGlobalUVRotation90(false);
			}
		}
		SavedLayers.Reset();
	}

	WorkingLayers.Append(MoveTemp(ReferenceLayers));
	if (bCreateDocument)
	{
		ResetEditHistory(false);
	}
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	SelectedLayerIndex = WorkingLayers.Num() - 1;
	SetLayerExpanded(SelectedLayerIndex, false);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = true;
	SyncChildInstances();
	SyncSelectedLayerControls();
	RefreshLayeredPreview(!bCreateDocument);
	bIsWorkingMaterialDirty = true;
	WorkingStatusText = FString::Printf(TEXT("Added reference to %s"), *Composition->GetName());
	RebuildLayerList();
	RebuildMaskList();
}

void SMixtormat::AddCompositionLayers(const FSoftObjectPath AssetPath)
{
	const UMixtormatMaterial* Composition = Cast<UMixtormatMaterial>(AssetPath.TryLoad());
	if (!Composition || Composition->Layers.IsEmpty())
	{
		WorkingStatusText = TEXT("Saved mix has no layers");
		return;
	}

	TArray<FMixtormatLayer> ImportedLayers = Composition->Layers;
	TArray<FMixtormatLayerGroup> ImportedGroups = Composition->LayerGroups;
	MixtormatParameterBinding::RegenerateLayerIdentities(ImportedLayers, ImportedGroups);
	const int32 FirstImportedLayer = bHasWorkingMaterial ? WorkingLayers.Num() : 0;

	if (bHasWorkingMaterial)
	{
		WorkingLayers.Append(MoveTemp(ImportedLayers));
		WorkingLayerGroups.Append(MoveTemp(ImportedGroups));
	}
	else
	{
		bHasWorkingMaterial = true;
		WorkingMaterialAsset.Reset();
		WorkingMaterialName = TEXT("Untitled Mixtormat Material");
		WorkingLayers = MoveTemp(ImportedLayers);
		WorkingLayerGroups = MoveTemp(ImportedGroups);
		bGlobalUVRotation90 = Composition->bRotateUV90;
		for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
		{
			if (Viewport.IsValid())
			{
				Viewport->SetGlobalUVRotation90(bGlobalUVRotation90);
			}
		}
		SavedLayers.Reset();
		SavedLayerGroups.Reset();
		ResetEditHistory(false);
	}

	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	SelectedLayerIndex = FirstImportedLayer;
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	SyncChildInstances();
	SyncSelectedLayerControls();
	RefreshLayeredPreview(false);
	bIsWorkingMaterialDirty = true;
	WorkingStatusText = FString::Printf(
		TEXT("Added %d layers from %s"),
		Composition->Layers.Num(),
		*Composition->GetName());
	RebuildLayerList();
	RebuildMaskList();
}

void SMixtormat::BrowseLibraryAsset(const FSoftObjectPath AssetPath)
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData Asset = AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath);
	if (!Asset.IsValid())
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowserModule.Get().SyncBrowserToAssets({Asset});
}

void SMixtormat::RemoveImportedSurface(const FSoftObjectPath AssetPath)
{
	if (!MixtormatUI::IsUserLibraryAsset(AssetPath))
	{
		return;
	}

	UMixtormatSurface* Surface = Cast<UMixtormatSurface>(AssetPath.TryLoad());
	if (!Surface)
	{
		RefreshSurfaceList();
		RebuildUserLibraryList();
		return;
	}

	TArray<FAssetData> Assets;
	const auto AddUserAsset = [&Assets](UObject* Asset)
	{
		if (Asset && MixtormatUI::IsUserLibraryAsset(FSoftObjectPath(Asset->GetPathName())))
		{
			Assets.AddUnique(FAssetData(Asset));
		}
	};
	AddUserAsset(Surface);
	AddUserAsset(Surface->PreviewMaterial);
	AddUserAsset(Surface->Thumbnail);
	AddUserAsset(Surface->BaseColor);
	AddUserAsset(Surface->Normal);
	AddUserAsset(Surface->RoughnessAOMetallic);

	if (!Surface->SourceTextureBaseName.IsEmpty() && Surface->BaseColor)
	{
		const FString TextureRoot = FPackageName::GetLongPackagePath(
			Surface->BaseColor->GetOutermost()->GetName());
		const TCHAR* PackedSuffixes[] = {TEXT("_RAM"), TEXT("_RAMH"), TEXT("_RAMH_Derived")};
		for (const TCHAR* Suffix : PackedSuffixes)
		{
			const FString AssetName = ObjectTools::SanitizeObjectName(
				Surface->SourceTextureBaseName + Suffix);
			const FString ObjectPath = FString::Printf(
				TEXT("%s/%s.%s"), *TextureRoot, *AssetName, *AssetName);
			AddUserAsset(LoadObject<UObject>(nullptr, *ObjectPath));
		}
	}

	ObjectTools::DeleteAssets(Assets, true);
	if (SelectedSurfacePath == AssetPath)
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath).IsValid())
		{
			SelectedSurfacePath.Reset();
			SelectedLibrarySurfaceName = FText::GetEmpty();
			SelectedPreviewMaterial.Reset();
		}
	}
	RefreshSurfaceList();
	RebuildUserLibraryList();
}

#undef LOCTEXT_NAMESPACE
