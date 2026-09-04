#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Menus/MixtormatMenuBuilder.h"

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
	// The same widget the layer stack's eye is, deliberately. This was a plated SButton with its
	// own 14px box and its own teal, so the two eyes in the tool -- one saying "this layer is
	// visible", one saying "the viewport is showing this channel" -- looked like different
	// controls doing unrelated things. One eye, one behaviour: no plate in any state, the accent
	// when it is on.
	return SNew(SMixtormatIconButton)
		.Size(MixtormatTokens::LayerEyeSize)
		.ToolTipText(ToolTip)
		.bActive_Lambda([this, Mode]() { return DebugPreviewMode == Mode; })
		.Icon_Lambda([this, Mode]()
		{
			return DebugPreviewMode == Mode ? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
		})
		.OnClicked_Lambda([this, Mode]() { ToggleFeaturePreview(Mode); });
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
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FCheckBoxStyle* OverlayToggle = &Style.GetWidgetStyle<FCheckBoxStyle>(TEXT("Mixtormat.ViewportOverlayToggle"));

	TSharedRef<SHorizontalBox> ComparisonControls = SNew(SHorizontalBox);
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
		LOCTEXT("PreviewCompositionBefore", "BEFORE"),
		LOCTEXT("PreviewCompositionBeforeHint", "Preview the base layer before added layers are composed"));
	AddComparisonButton(
		false,
		LOCTEXT("PreviewCompositionAfter", "AFTER"),
		LOCTEXT("PreviewCompositionAfterHint", "Preview the complete layer stack"));
	ComparisonControls->AddSlot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SCheckBox)
		.Style(OverlayToggle)
		.ToolTipText(LOCTEXT(
			"PreviewBypassSelectedChildHint",
			"Temporarily disable the selected Mask, Generated Mask, or Effect in the preview only"))
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
			.Text(LOCTEXT("PreviewBypassSelectedChild", "Bypass child"))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		]
	];

	TSharedRef<SVerticalBox> SubjectAndLight = SNew(SVerticalBox);
	const auto AddMeshButton = [this, &SubjectAndLight, OverlayToggle](
		const EMixtormatPreviewMesh MeshType,
		const FText& ToolTip,
		const FName IconName)
	{
		SubjectAndLight->AddSlot().AutoHeight()
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
				[
					SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))
				]
			]
		];
	};
	AddMeshButton(EMixtormatPreviewMesh::Sphere, LOCTEXT("SpherePreview", "Sphere"), TEXT("Mixtormat.Icon.Sphere"));
	AddMeshButton(EMixtormatPreviewMesh::Plane, LOCTEXT("PlanePreview", "Plane"), TEXT("Mixtormat.Icon.Plane"));
	AddMeshButton(EMixtormatPreviewMesh::Cube, LOCTEXT("CubePreview", "Cube"), TEXT("Mixtormat.Icon.Cube"));

	const auto AddPresetButton = [this, &SubjectAndLight, OverlayToggle](
		const EMixtormatStudioLighting Preset,
		const FText& ToolTip,
		const FName IconName)
	{
		SubjectAndLight->AddSlot().AutoHeight()
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
				[
					SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))
				]
			]
		];
	};
	AddPresetButton(EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioButton", "Neutral studio"), TEXT("Mixtormat.Icon.LightNeutral"));
	AddPresetButton(EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioButton", "Soft studio"), TEXT("Mixtormat.Icon.LightSoft"));
	AddPresetButton(EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioButton", "Dramatic studio"), TEXT("Mixtormat.Icon.LightDramatic"));
	AddPresetButton(EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioButton", "Rim lighting"), TEXT("Mixtormat.Icon.LightRim"));

	SubjectAndLight->AddSlot().AutoHeight()
	[
		SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
		[
			SNew(SComboButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
			.HasDownArrow(false)
			.ToolTipText(LOCTEXT("PreviewHdriMenuHint", "Choose an HDRI or studio lighting preset"))
			.OnGetMenuContent(this, &SMixtormat::BuildStudioLightingMenu)
			.ButtonContent()
			[
				SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Globe")))
			]
		]
	];
	SubjectAndLight->AddSlot().AutoHeight()
	[
		SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
		[
			SNew(SButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
			.ContentPadding(2.0f)
			.ToolTipText(LOCTEXT("ResetPreviewCameraLightingHint", "Reset camera, FOV, and lighting"))
			.OnClicked(this, &SMixtormat::ResetPreviewCameraAndLighting)
			[
				SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Refresh")))
			]
		]
	];

	const TArray<FText> ResolutionOptions = {
		LOCTEXT("Resolution1K", "1K"),
		LOCTEXT("Resolution2K", "2K"),
		LOCTEXT("Resolution4K", "4K")};
	TSharedRef<SHorizontalBox> OutputControls = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Visibility_Lambda([this]()
			{
				return DebugPreviewMode == EMixtormatDebugPreviewMode::None
					? EVisibility::Collapsed
					: EVisibility::Visible;
			})
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
			.ToolTipText(LOCTEXT("ClearDebugPreviewHint", "Return to the composite preview"))
			.OnClicked_Lambda([this]()
			{
				DebugPreviewMode = EMixtormatDebugPreviewMode::None;
				RefreshLayeredPreview(false);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					switch (DebugPreviewMode)
					{
					case EMixtormatDebugPreviewMode::GeneratedFeature: return LOCTEXT("DebugGeneratedFeature", "Feature ×");
					case EMixtormatDebugPreviewMode::HeightBlend: return LOCTEXT("DebugHeightBlend", "Height blend ×");
					case EMixtormatDebugPreviewMode::ContactAO: return LOCTEXT("DebugContactAO", "Contact AO ×");
					case EMixtormatDebugPreviewMode::BorderNormal: return LOCTEXT("DebugBorderNormal", "Border normal ×");
					case EMixtormatDebugPreviewMode::LayerMask: return LOCTEXT("DebugLayerMask", "Layer mask ×");
					default: return FText::GetEmpty();
					}
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(92.0f)
			[
				SNew(SMixtormatSegmentedControl)
				.Options(ResolutionOptions)
				.ActiveIndex_Lambda([this]()
				{
					return CompositionResolution >= 4096 ? 2 : CompositionResolution >= 2048 ? 1 : 0;
				})
				.OnChosen_Lambda([this](const int32 Index)
				{
					SetCompositionResolution(Index == 2 ? 4096 : Index == 1 ? 2048 : 1024);
				})
			]
		];

	// Camera and render settings, on the viewport rather than in the inspector.
	//
	// These belong to what you are looking at, not to the layer you are editing: FOV and
	// displacement change nothing about the material, and preview quality is a property of the
	// render. Put in the inspector they scroll away with the selection and change meaning
	// depending on what happens to be selected, which is what sent them back here.
	const TArray<FText> QualityOptions = {
		LOCTEXT("PreviewQualityLow", "LOW"),
		LOCTEXT("PreviewQualityMedium", "MED"),
		LOCTEXT("PreviewQualityHigh", "HIGH")};
	const TArray<FText> QualityToolTips = {
		LOCTEXT("PreviewQualityLowHint", "Direct light only. No AO, SSR, or Lumen."),
		LOCTEXT("PreviewQualityMediumHint", "Stable shadows, AO, and SSR. No Lumen."),
		LOCTEXT("PreviewQualityHighHint", "Lumen quality. Uses project ray tracing when supported.")};

	TSharedRef<SVerticalBox> CameraControls = SNew(SVerticalBox);
	CameraControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
	[
		SNew(SMixtormatSegmentedControl)
		.Options(QualityOptions)
		.ToolTips(QualityToolTips)
		.ActiveIndex_Lambda([this]()
		{
			switch (PreviewQuality)
			{
			case EMixtormatPreviewQuality::Low: return 0;
			case EMixtormatPreviewQuality::Medium: return 1;
			default: return 2;
			}
		})
		.OnChosen_Lambda([this](const int32 Index)
		{
			SetPreviewQuality(Index == 0
				? EMixtormatPreviewQuality::Low
				: Index == 1
					? EMixtormatPreviewQuality::Medium
					: EMixtormatPreviewQuality::High);
		})
	];
	CameraControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
	[
		MakeSlider(
			LOCTEXT("PreviewFovLabel", "FOV"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewFov); }),
			20.0, 90.0, 50.0, 0.5, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewFov(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]() { SetPreviewFov(50.0f); }),
			LOCTEXT("PreviewFovHint", "Preview camera field of view."))
	];
	CameraControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
	[
		MixtormatRow::Make(
			LOCTEXT("PreviewDisplacement", "Displacement"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					return bPreviewDisplacementEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
				{
					SetPreviewDisplacementEnabled(State == ECheckBoxState::Checked);
				}),
				LOCTEXT("PreviewDisplacementHint", "Preview the composited Height through the protected master's authored displacement path.")))
	];
	CameraControls->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this]() { return bPreviewDisplacementEnabled; })
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
				LOCTEXT("PreviewDisplacementAmountHint", "Scale the centered composited Height used by the authored displacement path."))
		]
	];

	TSharedRef<SWidget> PreviewPanel = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(PreviewViewport, SMixtormatPreviewViewport)
		]
		// Top left, which is where quality already was, and the only edge the newer clusters
		// have not taken.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(2.0f)
			[
				SNew(SBox).WidthOverride(MixtormatUI::InspectorWidth * 0.5f)[CameraControls]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(8.0f)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(2.0f)
			[ComparisonControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(8.0f)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(2.0f)
			[SubjectAndLight]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10.0f)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText")))
			.Text_Lambda([this]()
			{
				const FText LightingText = PreviewQuality == EMixtormatPreviewQuality::High
					? LOCTEXT("PreviewStatusLumen", "Lumen")
					: PreviewQuality == EMixtormatPreviewQuality::Medium
						? LOCTEXT("PreviewStatusMedium", "Medium")
						: LOCTEXT("PreviewStatusLow", "Low");
				return FText::Format(
					LOCTEXT("PreviewStatusText", "Real-time · {0} · SM6 · {1} layers"),
					LightingText,
					FText::AsNumber(WorkingLayers.Num()));
			})
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8.0f)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(MixtormatPalette::WellTop())
			.EndColor(MixtormatPalette::WellBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(2.0f)
			[OutputControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(12.0f)
		[
			SNew(SImage)
			.Image(Style.GetBrush(TEXT("Mixtormat.Brand.Watermark")))
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
	MixtormatMenu::FBuilder Menu;
	const TPair<EMixtormatStudioLighting, FText> Presets[] = {
		{EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioMenu", "Neutral Studio")},
		{EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioMenu", "Soft Studio")},
		{EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioMenu", "Dramatic")},
		{EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioMenu", "Rim Light")}};

	Menu.Caption(LOCTEXT("StudioLightingCaption", "Studio"));
	for (const TPair<EMixtormatStudioLighting, FText>& Preset : Presets)
	{
		// Ticked only while no HDRI is overriding it -- a preset and a cubemap cannot both be
		// what the viewport is lit by, and showing two ticks would say they can.
		Menu.Item(
			Preset.Value,
			nullptr,
			FSimpleDelegate::CreateLambda([this, Lighting = Preset.Key]() { SetStudioLighting(Lighting); }))
			.Checked(TAttribute<bool>::CreateLambda([this, Lighting = Preset.Key]()
			{
				return SelectedHdriPath.IsNull() && StudioLighting == Lighting;
			}));
	}

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter HdriFilter;
	HdriFilter.ClassPaths.Add(UTextureCube::StaticClass()->GetClassPathName());
	HdriFilter.PackagePaths.Add(FName(TEXT("/MaterialLab/Lighting")));
	HdriFilter.bRecursiveClasses = true;
	HdriFilter.bRecursivePaths = true;
	TArray<FAssetData> HdriAssets;
	AssetRegistryModule.Get().GetAssets(HdriFilter, HdriAssets);

	if (!HdriAssets.IsEmpty())
	{
		Menu.Caption(LOCTEXT("HdriLightingCaption", "HDRI"));
	}
	for (const FAssetData& HdriAsset : HdriAssets)
	{
		const FSoftObjectPath HdriPath = HdriAsset.GetSoftObjectPath();
		Menu.Item(
			FText::FromName(HdriAsset.AssetName),
			nullptr,
			FSimpleDelegate::CreateLambda([this, HdriPath]() { SetHdriLighting(HdriPath); }))
			.Checked(TAttribute<bool>::CreateLambda([this, HdriPath]()
			{
				return SelectedHdriPath == HdriPath;
			}));
	}

	return Menu.Build();
}

#undef LOCTEXT_NAMESPACE
