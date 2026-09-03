#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

// The 3D preview viewport: mesh, quality, camera, lighting, displacement, debug modes.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const bool bModifierDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::BackSpace && ResetHoveredNumericControl())
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SMixtormat::SetPreviewMesh(const EMixtormatPreviewMesh MeshType)
{
	PreviewMesh = MeshType;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewMesh(MeshType);
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::SetPreviewQuality(const EMixtormatPreviewQuality Quality)
{
	PreviewQuality = Quality;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewQuality(Quality);
		}
	}
	return FReply::Handled();
}

void SMixtormat::SetPreviewFov(const float FovDegrees)
{
	PreviewFov = FMath::Clamp(FovDegrees, 20.0f, 90.0f);
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetCameraFov(PreviewFov);
		}
	}
}

FReply SMixtormat::ResetPreviewCameraAndLighting()
{
	PreviewFov = 50.0f;
	StudioLighting = EMixtormatStudioLighting::Neutral;
	SelectedHdriPath.Reset();
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->ResetCameraAndLighting();
		}
	}
	return FReply::Handled();
}

void SMixtormat::SetPreviewDisplacementEnabled(const bool bEnabled)
{
	bPreviewDisplacementEnabled = bEnabled;
	if (bPreviewDisplacementEnabled && !bHasWorkingMaterial)
	{
		PreviewSelectedSurfaceWithDisplacement();
	}
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewDisplacementEnabled(bPreviewDisplacementEnabled);
		}
	}
}

void SMixtormat::SetPreviewDisplacementAmount(const float Amount)
{
	PreviewDisplacementAmount = FMath::Clamp(Amount, 0.0f, 4.0f);
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewDisplacementAmount(PreviewDisplacementAmount);
		}
	}
}

FReply SMixtormat::ToggleFeaturePreview(const EMixtormatDebugPreviewMode Mode)
{
	if (Mode == EMixtormatDebugPreviewMode::LayerMask)
	{
		const FMixtormatMaskLayer* Mask = GetSelectedLayerMask();
		if (!Mask || !Mask->bEnabled)
		{
			return FReply::Handled();
		}
	}

	DebugPreviewMode = DebugPreviewMode == Mode
		? EMixtormatDebugPreviewMode::None
		: Mode;
	RefreshLayeredPreview(false);
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::MakeFeaturePreviewButton(
	const EMixtormatDebugPreviewMode Mode,
	const FText& ToolTip)
{
	return SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.ContentPadding(1.0f)
		.ToolTipText(ToolTip)
		.OnClicked(this, &SMixtormat::ToggleFeaturePreview, Mode)
		[
			SNew(SBox)
			.WidthOverride(14.0f)
			.HeightOverride(14.0f)
			[
				SNew(SImage)
				.Image_Lambda([this, Mode]()
				{
					return FMixtormatStyle::Get().GetBrush(
						DebugPreviewMode == Mode
							? TEXT("Mixtormat.Icon.Eye")
							: TEXT("Mixtormat.Icon.EyeOff"));
				})
				.ColorAndOpacity_Lambda([this, Mode]()
				{
					return DebugPreviewMode == Mode
						? FSlateColor(FLinearColor(0.18f, 0.65f, 0.68f))
						: FSlateColor::UseSubduedForeground();
				})
			]
		];
}

void SMixtormat::PreviewSelectedSurfaceWithDisplacement()
{
	if (bHasWorkingMaterial || SelectedSurfacePath.IsNull())
	{
		return;
	}

	FMixtormatLayer PreviewLayer;
	PreviewLayer.DisplayName = SelectedLibrarySurfaceName;
	PreviewLayer.Type = EMixtormatLayerType::Material;
	PreviewLayer.SourceSurface = TSoftObjectPtr<UMixtormatSurface>(SelectedSurfacePath);
	PreviewLayer.Tiling = CurrentTiling;
	PreviewLayer.RoughnessBias = CurrentRoughnessBias;
	PreviewLayer.RoughnessContrast = CurrentRoughnessContrast;
	PreviewLayer.RoughnessOffset = CurrentRoughnessOffset;
	PreviewLayer.NormalIntensity = CurrentNormalIntensity;

	TArray<FMixtormatLayer> PreviewLayers;
	PreviewLayers.Add(MoveTemp(PreviewLayer));
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewLayers(PreviewLayers, CompositionResolution);
		}
	}
}

FReply SMixtormat::SetStudioLighting(const EMixtormatStudioLighting LightingPreset)
{
	StudioLighting = LightingPreset;
	SelectedHdriPath.Reset();
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetStudioLighting(LightingPreset);
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::SetHdriLighting(const FSoftObjectPath HdriPath)
{
	UTextureCube* Cubemap = Cast<UTextureCube>(HdriPath.TryLoad());
	if (!Cubemap)
	{
		return FReply::Handled();
	}

	SelectedHdriPath = HdriPath;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetHdriLighting(Cubemap);
		}
	}
	return FReply::Handled();
}

void SMixtormat::PreviewSurfaceScalarParameter(const FName ParameterName, const float Value)
{
	(void)ParameterName;
	(void)Value;
	RefreshLayeredPreview();
}

TSharedRef<SWidget> SMixtormat::BuildPreviewPanel()
{
	TSharedPtr<SMixtormatPreviewViewport> PreviewViewport;
	TSharedRef<SHorizontalBox> MeshControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> LightingControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> QualityControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> ComparisonControls = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> FovControls = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(3.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewFovLabel", "FOV"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 3.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(210.0f)
			[
				MakeSlider(
					LOCTEXT("PreviewFovLabel", "FOV"),
					TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewFov); }),
					20.0, 90.0, 50.0, 0.5, false,
					FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
					{
						SetPreviewFov(static_cast<float>(Value));
					}),
					FSimpleDelegate::CreateLambda([this]() { SetPreviewFov(50.0f); }))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("ResetPreviewCameraLightingHint", "Reset camera, FOV, and lighting"))
			.OnClicked(this, &SMixtormat::ResetPreviewCameraAndLighting)
			[
				SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
				[
					SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Icon.Refresh")))
				]
			]
		];
	const FCheckBoxStyle* OverlayToggle = &FMixtormatStyle::Get().GetWidgetStyle<FCheckBoxStyle>(
		TEXT("Mixtormat.ViewportOverlayToggle"));
	const auto AddComparisonButton = [this, &ComparisonControls, OverlayToggle](
		const bool bBefore,
		const FText& Label,
		const FText& ToolTip)
	{
		ComparisonControls->AddSlot().AutoWidth()
		[
			SNew(SCheckBox)
			.Style(OverlayToggle)
			.ToolTipText(ToolTip)
			.IsEnabled_Lambda([this]() { return bHasWorkingMaterial && !WorkingLayers.IsEmpty(); })
			.IsChecked_Lambda([this, bBefore]()
			{
				return SoloLayerIndex == INDEX_NONE && bShowCompositionBefore == bBefore
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, bBefore](const ECheckBoxState State)
			{
				if (State != ECheckBoxState::Checked)
				{
					return;
				}
				bShowCompositionBefore = bBefore;
				if (SoloLayerIndex != INDEX_NONE)
				{
					SoloLayerIndex = INDEX_NONE;
					RebuildLayerList();
				}
				RefreshLayeredPreview(false);
			})
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
			]
		];
	};
	AddComparisonButton(
		true,
		LOCTEXT("PreviewCompositionBefore", "Before"),
		LOCTEXT("PreviewCompositionBeforeHint", "Preview the base layer before added layers are composed"));
	AddComparisonButton(
		false,
		LOCTEXT("PreviewCompositionAfter", "After"),
		LOCTEXT("PreviewCompositionAfterHint", "Preview the complete layer stack"));
	ComparisonControls->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SCheckBox)
		.Style(OverlayToggle)
		.ToolTipText(LOCTEXT(
			"PreviewBypassSelectedChildHint",
			"Temporarily disable the selected Mask or Effect in the preview only"))
		.IsEnabled_Lambda([this]() { return GetSelectedChildIndex() != INDEX_NONE; })
		.IsChecked_Lambda([this]()
		{
			return bBypassSelectedChild && GetSelectedChildIndex() != INDEX_NONE
				? ECheckBoxState::Checked
				: ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
		{
			bBypassSelectedChild = State == ECheckBoxState::Checked
				&& GetSelectedChildIndex() != INDEX_NONE;
			RefreshLayeredPreview(false);
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewBypassSelectedChild", "Bypass Child"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
	];
	ComparisonControls->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SCheckBox)
		.Style(OverlayToggle)
		.ToolTipText(LOCTEXT(
			"PreviewDisplacementHint",
			"Preview the composited Height through the protected master's authored ML_UseHeight displacement path"))
		.IsEnabled_Lambda([this]()
		{
			return bHasWorkingMaterial
				? !WorkingLayers.IsEmpty()
				: SelectedPreviewMaterial.IsValid();
		})
		.IsChecked_Lambda([this]()
		{
			return bPreviewDisplacementEnabled
				? ECheckBoxState::Checked
				: ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
		{
			SetPreviewDisplacementEnabled(State == ECheckBoxState::Checked);
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewDisplacement", "Displacement"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
	];
	ComparisonControls->AddSlot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]()
		{
			return bPreviewDisplacementEnabled
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PreviewDisplacementAmountLabel", "Amount"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(150.0f)
			[
				MakeSlider(
					LOCTEXT("PreviewDisplacementAmountLabel", "Amount"),
					TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewDisplacementAmount); }),
					0.0, 4.0, 1.0, 0.05, false,
					FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
					{
						SetPreviewDisplacementAmount(static_cast<float>(Value));
					}),
					FSimpleDelegate::CreateLambda([this]() { SetPreviewDisplacementAmount(1.0f); }),
					LOCTEXT("PreviewDisplacementAmountHint", "Scale the centered composited Height used by the authored displacement path"))
			]
		]
	];

	const auto AddMeshButton = [this, &MeshControls, OverlayToggle](
		const EMixtormatPreviewMesh MeshType,
		const FText& ToolTip,
		const FName IconName)
	{
		MeshControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, MeshType]()
				{
					return PreviewMesh == MeshType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, MeshType](ECheckBoxState) { SetPreviewMesh(MeshType); })
				[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddMeshButton(EMixtormatPreviewMesh::Sphere, LOCTEXT("SpherePreview", "Sphere"), TEXT("Mixtormat.Icon.Sphere"));
	AddMeshButton(EMixtormatPreviewMesh::Plane, LOCTEXT("PlanePreview", "Plane"), TEXT("Mixtormat.Icon.Plane"));
	AddMeshButton(EMixtormatPreviewMesh::Cube, LOCTEXT("CubePreview", "Cube"), TEXT("Mixtormat.Icon.Cube"));

	const auto AddQualityButton = [this, &QualityControls, OverlayToggle](
		const EMixtormatPreviewQuality Quality,
		const FText& ToolTip,
		const FName IconName)
	{
		QualityControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, Quality]()
				{
					return PreviewQuality == Quality ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Quality](ECheckBoxState) { SetPreviewQuality(Quality); })
				[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddQualityButton(
		EMixtormatPreviewQuality::Low,
		LOCTEXT("LowPreviewQuality", "Low · Direct light only · No AO, SSR, or Lumen"),
		TEXT("Mixtormat.Icon.QualityLow"));
	AddQualityButton(
		EMixtormatPreviewQuality::Medium,
		LOCTEXT("MediumPreviewQuality", "Medium · Stable shadows, AO, and SSR · No Lumen"),
		TEXT("Mixtormat.Icon.QualityMedium"));
	AddQualityButton(
		EMixtormatPreviewQuality::High,
		LOCTEXT("HighPreviewQuality", "High · Lumen · Uses project ray tracing when supported"),
		TEXT("Mixtormat.Icon.QualityHigh"));

	const auto AddPresetButton = [this, &LightingControls, OverlayToggle](
		const EMixtormatStudioLighting Preset,
		const FText& ToolTip,
		const FName IconName)
	{
		LightingControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, Preset]()
				{
					return SelectedHdriPath.IsNull() && StudioLighting == Preset
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Preset](ECheckBoxState) { SetStudioLighting(Preset); })
				[SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))]
			]
		];
	};
	AddPresetButton(EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioButton", "Neutral studio"), TEXT("Mixtormat.Icon.LightNeutral"));
	AddPresetButton(EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioButton", "Soft studio"), TEXT("Mixtormat.Icon.LightSoft"));
	AddPresetButton(EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioButton", "Dramatic studio"), TEXT("Mixtormat.Icon.LightDramatic"));
	AddPresetButton(EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioButton", "Rim lighting"), TEXT("Mixtormat.Icon.LightRim"));

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter HdriFilter;
	HdriFilter.ClassPaths.Add(UTextureCube::StaticClass()->GetClassPathName());
	HdriFilter.PackagePaths.Add(FName(TEXT("/MaterialLab/Lighting")));
	HdriFilter.bRecursiveClasses = true;
	HdriFilter.bRecursivePaths = true;
	TArray<FAssetData> HdriAssets;
	AssetRegistryModule.Get().GetAssets(HdriFilter, HdriAssets);
	HdriAssets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.ToString() < B.AssetName.ToString();
	});

	for (const FAssetData& HdriAsset : HdriAssets)
	{
		const FSoftObjectPath HdriPath = HdriAsset.GetSoftObjectPath();
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(HdriAsset, 20, 20, ThumbnailPool);
		HdriThumbnails.Add(Thumbnail);
		LightingControls->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(FText::FromName(HdriAsset.AssetName))
				.IsChecked_Lambda([this, HdriPath]()
				{
					return SelectedHdriPath == HdriPath ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, HdriPath](ECheckBoxState) { SetHdriLighting(HdriPath); })
				[
					SNew(SBox).WidthOverride(20.0f).HeightOverride(20.0f)
					[Thumbnail->MakeThumbnailWidget()]
				]
			]
		];
	}

	TSharedRef<SWidget> PreviewPanel = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(PreviewViewport, SMixtormatPreviewViewport)
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ViewportOverlayGroup")))
			[QualityControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ViewportOverlayGroup")))
			[FovControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ViewportOverlayGroup")))
			[MeshControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ViewportOverlayGroup")))
			[ComparisonControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.ViewportOverlayGroup")))
			[LightingControls]
		]
		// Bottom centre is the only corner the control groups do not already occupy.
		// Hit-test invisible so it never steals a drag from the viewport underneath it.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(12.0f)
		[
			SNew(SImage)
			.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Brand.Watermark")))
			.Visibility(EVisibility::HitTestInvisible)
		];

	PreviewViewports.Add(PreviewViewport);
	PreviewViewport->SetPreviewQuality(PreviewQuality);
	PreviewViewport->SetCameraFov(PreviewFov);
	PreviewViewport->SetStudioLighting(StudioLighting);
	return PreviewPanel;
}

TSharedRef<SWidget> SMixtormat::BuildStudioLightingMenu()
{
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const TPair<EMixtormatStudioLighting, FText> Presets[] = {
		{EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioMenu", "Neutral Studio")},
		{EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioMenu", "Soft Studio")},
		{EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioMenu", "Dramatic")},
		{EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioMenu", "Rim Light")}};
	for (const TPair<EMixtormatStudioLighting, FText>& Preset : Presets)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
			.Text(Preset.Value)
			.OnClicked_Lambda([this, Lighting = Preset.Key]() { return SetStudioLighting(Lighting); })
		];
	}
	return SNew(SBorder).Padding(4.0f).BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))[Menu];
}

#undef LOCTEXT_NAMESPACE
