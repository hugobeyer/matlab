#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"


// The surface library: registry listing, filtering, search, cards and the gallery.

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	const FName UserLibraryCategoryFilter(TEXT("__MixtormatUserLibrary"));
}

FReply SMixtormat::RefreshSurfaceList()
{
	RebuildCategoryList();
	RebuildSurfaceList();
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
			CurrentNormalIntensity = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(
				SelectedPreviewMaterial.Get(), TEXT("DA_NormalIntensity"));
		}
		if (SelectedIdentityText.IsValid())
		{
			SelectedIdentityText->SetText(FText::FromName(Surface->Family));
		}
		if (SelectedMapsText.IsValid())
		{
			SelectedMapsText->SetText(FText::FromString(FString::Printf(
				TEXT("BC %s  N %s  %s %s"),
				Surface->BaseColor ? TEXT("✓") : TEXT("—"),
				Surface->Normal ? TEXT("✓") : TEXT("—"),
				MixtormatUI::PackedMapLabel(*Surface),
				Surface->RoughnessAOMetallic ? TEXT("✓") : TEXT("—"))));
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
		Families.AddUnique(Surface.Family);
	}
	Families.Remove(NAME_None);
	Families.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	if (!CategoryFilter.IsNone()
		&& CategoryFilter != UserLibraryCategoryFilter
		&& !Families.Contains(CategoryFilter))
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
	CategoryListBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(SButton)
		.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(
			CategoryFilter == UserLibraryCategoryFilter
				? TEXT("Mixtormat.TabButtonActive")
				: TEXT("Mixtormat.TabButton")))
		.OnClicked_Lambda([this]() { return SetCategoryFilter(UserLibraryCategoryFilter); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(12.0f)
				.HeightOverride(12.0f)
				[
					SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Folder")))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(5.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("UserLibraryCategory", "User Library"))
			]
		]
	];
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
		const bool bIsUserSurface = MixtormatUI::IsUserLibraryAsset(Surface.AssetPath);
		if ((CategoryFilter == UserLibraryCategoryFilter && !bIsUserSurface)
			|| (!CategoryFilter.IsNone()
				&& CategoryFilter != UserLibraryCategoryFilter
				&& Surface.Family != CategoryFilter))
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
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ContentPadding(FMargin(0.0f))
					.ToolTipText(LOCTEXT("ChooseTextureFolderHint", "Choose Texture Folder..."))
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::LibraryBrowseButtonGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
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
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildSurfaceList()
{
	return SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		+ SScrollBox::Slot()
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
	return SNew(SMixtormatSurfaceCard)
		.DisplayName(Name)
		.SurfacePath(AssetPath)
		.ThumbnailAsset(ThumbnailAsset)
		.ThumbnailPool(ThumbnailPool)
		.OnSelected(this, &SMixtormat::SelectSurface)
		.OnGalleryZoom(this, &SMixtormat::ZoomMaterialGallery)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SMixtormatTile)
				.TileSize(MaterialGalleryTileSize)
				.DisplayName(Name)
				.ThumbnailAsset(ThumbnailAsset)
				.ThumbnailPool(ThumbnailPool)
				.bShowName(false)
				.bSelected_Lambda([this, AssetPath]() { return SelectedSurfacePath == AssetPath; })
				.ToolTip(LOCTEXT("SelectMaterialForLayerActions", "Select for adding, replacing, or dragging to Layers"))
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.0f)
			[
				MixtormatUI::BuildLibraryOwnershipBadge(AssetPath)
			]
		];
}

#undef LOCTEXT_NAMESPACE
