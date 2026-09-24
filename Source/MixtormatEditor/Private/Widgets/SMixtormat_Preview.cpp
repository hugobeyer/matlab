// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "Preview/SMixtormatLightGizmo.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Widgets/Input/SComboButton.h"


// The 3D preview viewport: mesh, quality, camera, lighting, displacement, debug modes.

#define LOCTEXT_NAMESPACE "SMixtormat"

// The eye always toggles the primary; the chevron (built only when Secondary is non-empty) offers
// the rest. Derived from GetChildCapabilities rather than hand-authored here a second time: the
// entry with bPreviewable && !bSecondaryPreview becomes Primary, every bPreviewable &&
// bSecondaryPreview entry becomes Secondary. A capability that is copyable but not previewable
// (Pattern IDs' Gap -- the default Region IDs preview already renders it, blackened) contributes
// nothing here, which is the whole reason capabilities keeps the two flags separate.
FMixtormatChildPreviewOutputSet GetChildPreviewOutputSet(const FMixtormatLayerChild& Child)
{
	FMixtormatChildPreviewOutputSet Result;
	for (const FMixtormatPublishedOutputDesc& Output : GetChildCapabilities(Child).Outputs)
	{
		if (!Output.bPreviewable)
		{
			continue;
		}
		const FMixtormatPreviewOutputDesc Desc{Output.Name, Output.Label, Output.Kind, Output.PreviewGapMaskName};
		if (Output.bSecondaryPreview)
		{
			Result.Secondary.Add(Desc);
		}
		else
		{
			Result.Primary = Desc;
		}
	}
	return Result;
}

FMixtormatChildPreviewOutputSet GetPreviewOutputSetForChildType(const EMixtormatLayerChildType Type)
{
	FMixtormatLayerChild Probe;
	Probe.Type = Type;
	return GetChildPreviewOutputSet(Probe);
}

FMixtormatChildPreviewOutputSet GetPreviewOutputSetForEffectType(const EMixtormatEffectType EffectType)
{
	FMixtormatLayerChild Probe;
	Probe.Type = EMixtormatLayerChildType::Effect;
	Probe.Effect.ProceduralType = EffectType;
	return GetChildPreviewOutputSet(Probe);
}

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

void SMixtormat::SetGlobalUVRotation90(const bool bEnabled)
{
	if (bGlobalUVRotation90 == bEnabled)
	{
		return;
	}

	bGlobalUVRotation90 = bEnabled;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetGlobalUVRotation90(bGlobalUVRotation90);
		}
	}
	RefreshLayeredPreview();
	// Keep this document-level toggle as its own undo step rather than allowing the next slider
	// edit inside the normal coalescing window to absorb it.
	LastHistoryRecordTime = 0.0;
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

FReply SMixtormat::SetPreviewAntiAliasing(const EMixtormatPreviewAntiAliasing AntiAliasing)
{
	PreviewAntiAliasing = AntiAliasing;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewAntiAliasing(AntiAliasing);
		}
	}
	return FReply::Handled();
}

void SMixtormat::SetPreviewScreenPercentage(const int32 Percentage)
{
	PreviewScreenPercentage = FMath::Clamp(
		Percentage,
		MixtormatPreviewScreenPercentage::Minimum,
		MixtormatPreviewScreenPercentage::Maximum);
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewScreenPercentage(PreviewScreenPercentage);
		}
	}
}

void SMixtormat::SetPreviewFov(const float FovDegrees)
{
	PreviewFov = FMath::Clamp(
		FovDegrees,
		MixtormatPreviewCamera::FovMinimum,
		MixtormatPreviewCamera::FovMaximum);
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
	PreviewFov = MixtormatPreviewCamera::FovDefault;
	StudioLighting = EMixtormatStudioLighting::Neutral;
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

void SMixtormat::SetPreviewLightIntensity(const float Scale)
{
	PreviewLightIntensity = FMath::Clamp(Scale, 0.0f, 2.0f);
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewLightIntensity(PreviewLightIntensity);
		}
	}
}

void SMixtormat::SetPreviewSkylightIntensity(const float Scale)
{
	PreviewSkylightIntensity = FMath::Clamp(Scale, 0.0f, 2.0f);
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewSkylightIntensity(PreviewSkylightIntensity);
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
		// LayerMask previews whichever mask-chain child is selected -- Mask, Generated, Craquelure,
		// ColorId or RandomId -- not only a plain Mask child. GetSelectedLayerMask() only resolves
		// the first of those, so gating on it alone silently refused every other type's eye: a
		// Generated Mask child, say, would clear bLayerMask (and never set it) because the guard
		// treated "not a plain Mask child" as "not enabled" and returned before the toggle ran.
		const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
		if (!Child || !IsGroupChildEnabled(*Child))
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

FReply SMixtormat::ToggleChildOutputPreview(const FMixtormatChildPreviewTarget& Target)
{
	if (!Target.IsValid())
	{
		return FReply::Handled();
	}
	const bool bAlreadyActive =
		DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput && ChildPreviewTarget == Target;
	if (bAlreadyActive)
	{
		DebugPreviewMode = EMixtormatDebugPreviewMode::None;
		ChildPreviewTarget = FMixtormatChildPreviewTarget();
	}
	else
	{
		DebugPreviewMode = EMixtormatDebugPreviewMode::ChildOutput;
		ChildPreviewTarget = Target;
	}
	RefreshLayeredPreview(false);
	return FReply::Handled();
}

FMixtormatChildPreviewTarget SMixtormat::ResolveChildPreviewTarget(
	const FName OutputName, const EMixtormatPreviewOutputKind Kind, const FName GapMaskName) const
{
	FMixtormatChildPreviewTarget Target;
	Target.OutputName = OutputName;
	Target.Kind = Kind;
	Target.GapMaskName = GapMaskName;

	if (SelectedLayerIndex != INDEX_NONE)
	{
		if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
		{
			return Target;
		}
		const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
		if (!Child)
		{
			return Target;
		}
		Target.OwnerId = WorkingLayers[SelectedLayerIndex].LayerId;
		Target.ChildId = Child->ChildId;
		return Target;
	}

	if (!SelectedGroupId.IsValid())
	{
		return Target;
	}
	const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SelectedGroupId);
	if (!Group || !Group->Children.IsValidIndex(SelectedGroupChildIndex))
	{
		return Target;
	}
	const FGuid AuthoredChildId = Group->Children[SelectedGroupChildIndex].ChildId;

	// Flatten to one concrete member before this leaves the editor: the compositor never learns
	// what a group is (groups become ordinary layers once, in RequestComposeInternal, and nowhere
	// else), so a group-authored target has to already name one real member layer and that
	// member's own effective (per-member) child id -- otherwise the same authored child would
	// match every enabled member's clone of it at once, and whichever composited last would win.
	int32 FirstIndex = INDEX_NONE, LastIndex = INDEX_NONE;
	if (!MixtormatLayerGroups::GetGroupRange(WorkingLayers, SelectedGroupId, FirstIndex, LastIndex))
	{
		return Target;
	}
	for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
	{
		if (WorkingLayers.IsValidIndex(Index) && WorkingLayers[Index].bEnabled)
		{
			Target.OwnerId = WorkingLayers[Index].LayerId;
			Target.ChildId = MixtormatLayerGroups::MakeEffectiveChildId(
				SelectedGroupId, AuthoredChildId, WorkingLayers[Index].LayerId);
			return Target;
		}
	}
	// No enabled member: nothing would broadcast this child, so there is nothing to show.
	return Target;
}

namespace
{
	// Combine IDs cannot combine anything without a producer above it in the same layer's child
	// chain (Cluster IDs, Pattern IDs, Breakup, or another Combine IDs) -- the exact requirement
	// MixtormatGpuPatternPasses.cpp's FindRegionIdsAbove enforces at compose time. Mirrored here,
	// on the authored (not effective/expanded) array the editor already has, purely to disable the
	// eye cleanly instead of leaving it clickable with nothing to show.
	int32 CountRegionIdProducersAbove(
		const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		int32 Count = 0;
		for (int32 Index = ChildIndex - 1; Index >= 0; --Index)
		{
			const EMixtormatLayerChildType Type = Children[Index].Type;
			if (Type == EMixtormatLayerChildType::Filter
				|| Type == EMixtormatLayerChildType::PatternId
				|| Type == EMixtormatLayerChildType::CombineId
				|| Type == EMixtormatLayerChildType::IdGroup
				|| (Type == EMixtormatLayerChildType::Effect
					&& Children[Index].Effect.ProceduralType == EMixtormatEffectType::Breakup))
			{
				++Count;
			}
		}
		return Count;
	}
}

bool SMixtormat::IsChildOutputPreviewReady(const FMixtormatLayerChild& Child) const
{
	if (bBypassSelectedChild)
	{
		return false;
	}
	if (WorkingLayers.IsValidIndex(SelectedLayerIndex) && !WorkingLayers[SelectedLayerIndex].bEnabled)
	{
		return false;
	}
	if (!IsGroupChildEnabled(Child))
	{
		return false;
	}
	if (Child.Type == EMixtormatLayerChildType::CombineId
		|| Child.Type == EMixtormatLayerChildType::IdGroup)
	{
		// A group-authored Combine IDs child is checked against its own authored array (Group's
		// shared stack), same as a plain layer's Children -- the producer-above requirement is
		// about position within the one stack a child actually lives in, group or not.
		const TArray<FMixtormatLayerChild>* Siblings = nullptr;
		int32 ChildIndex = INDEX_NONE;
		if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
		{
			Siblings = &WorkingLayers[SelectedLayerIndex].Children;
			ChildIndex = GetSelectedChildIndex();
		}
		else if (const FMixtormatLayerGroup* Group =
			MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SelectedGroupId))
		{
			Siblings = &Group->Children;
			ChildIndex = SelectedGroupChildIndex;
		}
		if (!Siblings || !Siblings->IsValidIndex(ChildIndex))
		{
			return false;
		}
		if (Child.Type == EMixtormatLayerChildType::IdGroup)
		{
			const FGuid GroupChildId = (*Siblings)[ChildIndex].ChildId;
			return Siblings->CountByPredicate([&GroupChildId](const FMixtormatLayerChild& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::PatternId
					&& Candidate.ScopeOwnerChildId == GroupChildId
					&& Candidate.PatternId.bEnabled;
			}) == 2;
		}
		return CountRegionIdProducersAbove(*Siblings, ChildIndex) >= 1;
	}
	if (Child.Type != EMixtormatLayerChildType::Filter)
	{
		return true;
	}
	// Cluster IDs' segmentation scan reads the owning layer's packed RAM + height.
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		// Group-authored: no single owning layer's surface to check here. The per-member GPU pass
		// leaves the debug output cleared if the member this resolves to has none.
		return true;
	}
	const UMixtormatSurface* Surface = WorkingLayers[SelectedLayerIndex].SourceSurface.LoadSynchronous();
	return Surface && Surface->RoughnessAOMetallic;
}

TSharedRef<SWidget> SMixtormat::MakeChildOutputPreviewButton(
	const FMixtormatChildPreviewOutputSet& OutputSet)
{
	if (!OutputSet.Primary.IsSet())
	{
		return SNullWidget::NullWidget;
	}
	const FMixtormatPreviewOutputDesc Primary = OutputSet.Primary.GetValue();
	const TArray<FMixtormatPreviewOutputDesc> Secondary = OutputSet.Secondary;

	const auto MatchesDesc = [this](const FMixtormatPreviewOutputDesc& Desc)
	{
		return DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput
			&& ChildPreviewTarget.IsValid()
			&& ChildPreviewTarget == ResolveChildPreviewTarget(Desc.Name, Desc.Kind, Desc.GapMaskName);
	};
	const auto IsAnyActive = [this, Primary, Secondary, MatchesDesc]()
	{
		if (MatchesDesc(Primary))
		{
			return true;
		}
		for (const FMixtormatPreviewOutputDesc& Desc : Secondary)
		{
			if (MatchesDesc(Desc))
			{
				return true;
			}
		}
		return false;
	};
	const auto IsReady = [this]()
	{
		const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
		return Child != nullptr && IsChildOutputPreviewReady(*Child);
	};

	// The eye: a normal single-click toggle, identical in every respect to MakeFeaturePreviewButton
	// above. It always targets the primary -- clicking it while a secondary is active (chosen from
	// the chevron) switches back to the default view; clicking it while lit turns the preview off.
	TSharedRef<SWidget> Eye = SNew(SMixtormatIconButton)
		.Size(MixtormatTokens::LayerEyeSize)
		.ToolTipText(Primary.Label)
		.IsEnabled_Lambda([IsAnyActive, IsReady]() { return IsAnyActive() || IsReady(); })
		.bActive_Lambda(IsAnyActive)
		.Icon_Lambda([IsAnyActive]()
		{
			return IsAnyActive() ? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
		})
		.OnClicked_Lambda([this, IsAnyActive, Primary]()
		{
			if (IsAnyActive())
			{
				DebugPreviewMode = EMixtormatDebugPreviewMode::None;
				ChildPreviewTarget = FMixtormatChildPreviewTarget();
				RefreshLayeredPreview(false);
			}
			else
			{
				ToggleChildOutputPreview(
					ResolveChildPreviewTarget(Primary.Name, Primary.Kind, Primary.GapMaskName));
			}
		});

	if (Secondary.IsEmpty())
	{
		return Eye;
	}

	// The chevron: a separate, very small control beside the eye, never a replacement for it.
	// Its menu is categorized -- IDS for the primary (so picking it here behaves exactly like
	// clicking the eye), MASKS for everything else this child publishes.
	TSharedRef<SWidget> Chevron = SNew(SComboButton)
		.HasDownArrow(false)
		.ContentPadding(FMargin(0.0f))
		.IsEnabled_Lambda([IsAnyActive, IsReady]() { return IsAnyActive() || IsReady(); })
		.ToolTipText(LOCTEXT("PreviewChildOutputMenuHint", "More preview options for this child"))
		.ButtonContent()
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ChevronSize)
			.HeightOverride(MixtormatTokens::ChevronSize)
			[
				SNew(SImage).Image(MixtormatIcons::ChevronDown())
			]
		]
		.OnGetMenuContent_Lambda([this, Primary, Secondary, MatchesDesc]() -> TSharedRef<SWidget>
		{
			MixtormatMenu::FBuilder Menu;
			Menu.Caption(LOCTEXT("PreviewMenuIds", "IDS"));
			Menu.Item(Primary.Label, nullptr, FSimpleDelegate::CreateLambda([this, Primary]()
			{
				ToggleChildOutputPreview(
					ResolveChildPreviewTarget(Primary.Name, Primary.Kind, Primary.GapMaskName));
			})).Checked(TAttribute<bool>::CreateLambda([MatchesDesc, Primary]() { return MatchesDesc(Primary); }));

			Menu.Caption(LOCTEXT("PreviewMenuMasks", "MASKS"));
			for (const FMixtormatPreviewOutputDesc& Desc : Secondary)
			{
				Menu.Item(Desc.Label, nullptr, FSimpleDelegate::CreateLambda([this, Desc]()
				{
					ToggleChildOutputPreview(
						ResolveChildPreviewTarget(Desc.Name, Desc.Kind, Desc.GapMaskName));
				})).Checked(TAttribute<bool>::CreateLambda([MatchesDesc, Desc]() { return MatchesDesc(Desc); }));
			}
			return Menu.Build();
		});

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Eye
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			Chevron
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

	TArray<FMixtormatLayer> PreviewLayers;
	PreviewLayers.Add(MoveTemp(PreviewLayer));
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			// A library surface on its own, nothing to do with the working document.
			Viewport->SetPreviewLayers(
				PreviewLayers, TArray<FMixtormatLayerGroup>(), CompositionResolution);
		}
	}
}

FReply SMixtormat::SetStudioLighting(const EMixtormatStudioLighting LightingPreset)
{
	StudioLighting = LightingPreset;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetStudioLighting(LightingPreset);
		}
	}
	return FReply::Handled();
}


TSharedRef<SWidget> SMixtormat::BuildPreviewPanel()
{
	const bool bReusingViewport = !PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid();
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
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontCaption))
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
	ComparisonControls->AddSlot().AutoWidth().Padding(MixtormatTokens::PreviewComparisonToggleGap, 0.0f, 0.0f, 0.0f)
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
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontCaption))
		]
	];

	TSharedRef<SVerticalBox> GeometryControls = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> LightingControls = SNew(SVerticalBox);
	const auto AddMeshButton = [this, &GeometryControls, OverlayToggle](
		const EMixtormatPreviewMesh MeshType,
		const FText& ToolTip,
		const FName IconName)
	{
		GeometryControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::ViewportOverlayButtonGap)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::PreviewToolbarButtonSize)
			.HeightOverride(MixtormatTokens::PreviewToolbarButtonSize)
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
					SNew(SBox)
					.WidthOverride(MixtormatTokens::PreviewToolbarIconSize)
					.HeightOverride(MixtormatTokens::PreviewToolbarIconSize)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))
					]
				]
			]
		];
	};
	AddMeshButton(EMixtormatPreviewMesh::Sphere, LOCTEXT("SpherePreview", "Sphere"), TEXT("Mixtormat.Icon.Sphere"));
	AddMeshButton(EMixtormatPreviewMesh::Cylinder, LOCTEXT("CylinderPreview", "Cylinder"), TEXT("Mixtormat.Icon.Cylinder"));
	AddMeshButton(EMixtormatPreviewMesh::Cube, LOCTEXT("CubePreview", "Cube"), TEXT("Mixtormat.Icon.Cube"));
	AddMeshButton(EMixtormatPreviewMesh::Plane, LOCTEXT("PlanePreview", "Plane"), TEXT("Mixtormat.Icon.Plane"));
	GeometryControls->AddSlot().AutoHeight().Padding(
		0.0f,
		0.0f,
		0.0f,
		MixtormatTokens::ViewportOverlayButtonGap)
	[
		SNew(SBox)
		.WidthOverride(MixtormatTokens::PreviewToolbarButtonSize)
		.HeightOverride(MixtormatTokens::PreviewToolbarButtonSize)
		[
			SNew(SCheckBox)
			.Style(OverlayToggle)
			.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
			.ToolTipText(LOCTEXT(
				"GlobalUVRotation90Hint",
				"Rotate the complete material UVs 90 degrees. All channels rotate together, including tangent-space normal direction."))
			.IsChecked_Lambda([this]()
			{
				return bGlobalUVRotation90
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
			{
				SetGlobalUVRotation90(State == ECheckBoxState::Checked);
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GlobalUVRotation90", "90°"))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontCaption))
			]
		]
	];

	const auto AddPresetButton = [this, &LightingControls, OverlayToggle](
		const EMixtormatStudioLighting Preset,
		const FText& ToolTip,
		const FName IconName)
	{
		LightingControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::ViewportOverlayButtonGap)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::PreviewToolbarButtonSize)
			.HeightOverride(MixtormatTokens::PreviewToolbarButtonSize)
			[
				SNew(SCheckBox)
				.Style(OverlayToggle)
				.ToolTipText(ToolTip)
				.IsChecked_Lambda([this, Preset]()
				{
					return StudioLighting == Preset
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Preset](ECheckBoxState) { SetStudioLighting(Preset); })
				[
					SNew(SBox)
					.WidthOverride(MixtormatTokens::PreviewToolbarIconSize)
					.HeightOverride(MixtormatTokens::PreviewToolbarIconSize)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(IconName))
					]
				]
			]
		];
	};
	AddPresetButton(EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioButton", "Neutral studio"), TEXT("Mixtormat.Icon.LightNeutral"));
	AddPresetButton(EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioButton", "Soft studio"), TEXT("Mixtormat.Icon.LightSoft"));
	AddPresetButton(EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioButton", "Dramatic studio"), TEXT("Mixtormat.Icon.LightDramatic"));
	AddPresetButton(EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioButton", "Rim lighting"), TEXT("Mixtormat.Icon.LightRim"));
	AddPresetButton(EMixtormatStudioLighting::Workshop, LOCTEXT("WorkshopStudioButton", "Workshop lighting"), TEXT("Mixtormat.Icon.Globe"));
	LightingControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::ViewportOverlayButtonGap)
	[
		SNew(SBox)
		.WidthOverride(MixtormatTokens::PreviewToolbarButtonSize)
		.HeightOverride(MixtormatTokens::PreviewToolbarButtonSize)
		[
			SNew(SButton)
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.ViewportOverlayButton")))
			.ContentPadding(MixtormatTokens::ViewportOverlayTogglePadding)
			.ToolTipText(LOCTEXT("ResetPreviewCameraLightingHint", "Reset camera, FOV, and lighting"))
			.OnClicked(this, &SMixtormat::ResetPreviewCameraAndLighting)
			[

				SNew(SBox)
				.WidthOverride(MixtormatTokens::PreviewToolbarIconSize)
				.HeightOverride(MixtormatTokens::PreviewToolbarIconSize)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.Icon.Refresh")))
				]
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
				ChildPreviewTarget = FMixtormatChildPreviewTarget();
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
					case EMixtormatDebugPreviewMode::Stain: return LOCTEXT("DebugStain", "Stain ×");
					case EMixtormatDebugPreviewMode::Runoff: return LOCTEXT("DebugRunoff", "Runoff ×");
					case EMixtormatDebugPreviewMode::ChildOutput:
					{
						// Named after whichever output is active, not the mode: one mode covers
						// every child-published output, so "Child output ×" would tell the user
						// nothing "Gap ×" or "Region IDs ×" doesn't.
						if (const FMixtormatLayerChild* Child =
							ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
						{
							const FMixtormatChildPreviewOutputSet OutputSet = GetChildPreviewOutputSet(*Child);
							const auto Matches = [this](const FMixtormatPreviewOutputDesc& Desc)
							{
								return Desc.Name == ChildPreviewTarget.OutputName
									&& Desc.Kind == ChildPreviewTarget.Kind;
							};
							if (OutputSet.Primary.IsSet() && Matches(OutputSet.Primary.GetValue()))
							{
								return FText::Format(
									LOCTEXT("DebugChildOutputFmt", "{0} ×"), OutputSet.Primary.GetValue().Label);
							}
							for (const FMixtormatPreviewOutputDesc& Desc : OutputSet.Secondary)
							{
								if (Matches(Desc))
								{
									return FText::Format(LOCTEXT("DebugChildOutputFmt", "{0} ×"), Desc.Label);
								}
							}
						}
						return LOCTEXT("DebugChildOutput", "Preview ×");
					}
					default: return FText::GetEmpty();
					}
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(MixtormatTokens::ViewportOverlayItemGap, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(MixtormatTokens::PreviewResolutionControlWidth)
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
		LOCTEXT("PreviewQualityLowHint", "Key light and plugin-cubemap skylight. No AO, SSR, or Lumen."),
		LOCTEXT("PreviewQualityMediumHint", "Lumen GI and reflections with reduced final gather. No viewport AO."),
		LOCTEXT("PreviewQualityHighHint", "Full-quality Lumen GI and reflections with viewport AO.")};

	// Two clusters, split by what the control belongs to rather than by where there was room.
	//
	// Render settings -- how the frame is resolved -- stay top left. Scene settings the user
	// reaches for while looking at the surface, quality and displacement, moved to the bottom
	// left, and FOV to the bottom centre where the watermark used to sit.
	const TArray<FText> AntiAliasingOptions = {
		LOCTEXT("PreviewAaFxaa", "FXAA"),
		LOCTEXT("PreviewAaTsr", "TSR")};
	const TArray<FText> AntiAliasingToolTips = {
		LOCTEXT("PreviewAaFxaaHint", "Single frame, no history. Click again to turn anti-aliasing off."),
		LOCTEXT("PreviewAaTsrHint", "Temporal Super-Resolution, the project default. Click again to turn anti-aliasing off.")};

	TSharedRef<SVerticalBox> RenderControls = SNew(SVerticalBox);
	RenderControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
	[
		SNew(SMixtormatSegmentedControl)
		.Options(AntiAliasingOptions)
		.ToolTips(AntiAliasingToolTips)
		.ActiveIndex_Lambda([this]() -> int32
		{
			if (PreviewAntiAliasing == EMixtormatPreviewAntiAliasing::Off)
			{
				return INDEX_NONE;
			}
			return PreviewAntiAliasing == EMixtormatPreviewAntiAliasing::Fxaa ? 0 : 1;
		})
		.OnChosen_Lambda([this](const int32 Index)
		{
			const EMixtormatPreviewAntiAliasing Chosen = Index == 0
				? EMixtormatPreviewAntiAliasing::Fxaa
				: EMixtormatPreviewAntiAliasing::Temporal;
			SetPreviewAntiAliasing(PreviewAntiAliasing == Chosen
				? EMixtormatPreviewAntiAliasing::Off
				: Chosen);
		})
	];
	RenderControls->AddSlot().AutoHeight()
	[
		MakeSlider(
			LOCTEXT("PreviewScaleLabel", "Scale"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewScreenPercentage); }),
			static_cast<double>(MixtormatPreviewScreenPercentage::Minimum),
			static_cast<double>(MixtormatPreviewScreenPercentage::Maximum),
			static_cast<double>(MixtormatPreviewScreenPercentage::Default),
			1.0, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewScreenPercentage(FMath::RoundToInt(Value));
			}),
			FSimpleDelegate::CreateLambda([this]()
			{
				SetPreviewScreenPercentage(MixtormatPreviewScreenPercentage::Default);
			}),
			LOCTEXT("PreviewScaleHint", "Render resolution as a percentage of the viewport. Above 100 the extra samples are real, so it fixes shading aliasing rather than hiding it -- 150 with FXAA is the sharpest stable option here, at 2.25x the fill rate. Below 100 it buys back frame time on an expensive graph."))
	];

	TSharedRef<SVerticalBox> SceneControls = SNew(SVerticalBox);
	SceneControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
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
	SceneControls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
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
	SceneControls->AddSlot().AutoHeight()
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

	// Light and skylight, under the displacement amount and above the camera block: they change
	// how the surface reads without changing what the surface is, which is the same class of
	// control as displacement preview.
	SceneControls->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap, 0.0f, 0.0f)
	[
		MakeSlider(
			LOCTEXT("PreviewLightIntensityLabel", "Light"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewLightIntensity); }),
			0.0, 2.0, 0.5, 0.01, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewLightIntensity(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]() { SetPreviewLightIntensity(0.5f); }),
			LOCTEXT("PreviewLightIntensityHint", "Scales the preset key light. The default 0.5 uses half of the preset's authored brightness."))
	];
	SceneControls->AddSlot().AutoHeight()
	[
		MakeSlider(
			LOCTEXT("PreviewSkylightIntensityLabel", "Skylight"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewSkylightIntensity); }),
			0.0, 2.0, 0.5, 0.01, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewSkylightIntensity(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]() { SetPreviewSkylightIntensity(0.5f); }),
			LOCTEXT("PreviewSkylightIntensityHint", "Scales the boosted plugin-cubemap lighting and reflection capture. The default is half intensity."))
	];
	TSharedRef<SVerticalBox> CameraControls = SNew(SVerticalBox);
	CameraControls->AddSlot().AutoHeight()
	[
		MakeSlider(
			LOCTEXT("PreviewFovLabel", "FOV"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewFov); }),
			static_cast<double>(MixtormatPreviewCamera::FovMinimum),
			static_cast<double>(MixtormatPreviewCamera::FovMaximum),
			static_cast<double>(MixtormatPreviewCamera::FovDefault),
			0.5, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewFov(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]()
			{
				SetPreviewFov(MixtormatPreviewCamera::FovDefault);
			}),
			LOCTEXT("PreviewFovHint", "Preview camera field of view."))
	];

	const TSharedRef<SMixtormatPreviewViewport> PreviewViewport = bReusingViewport
		? PreviewViewports[0].ToSharedRef()
		: SNew(SMixtormatPreviewViewport)
			.OnToggleOverlayUi(FSimpleDelegate::CreateLambda([this]()
			{
				bPreviewOverlayUiVisible = !bPreviewOverlayUiVisible;
			}))
			// Temporary: V has no toolbar readout yet, so the status line is the only feedback.
			.OnChannelPreviewChanged(FSimpleDelegate::CreateLambda([this]()
			{
				if (!PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid())
				{
					WorkingStatusText = FString::Printf(
						TEXT("Preview: %s"),
						*PreviewViewports[0]->GetChannelPreviewLabel());
				}
			}));
	TSharedRef<SWidget> PreviewPanel = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			PreviewViewport
		]
		// Render settings top left -- how the frame is resolved, which is the one cluster that
		// says nothing about the material.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[
				SNew(SBox).WidthOverride(MixtormatTokens::InspectorWidth * 0.5f)[RenderControls]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[ComparisonControls]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SBox)
			.WidthOverride(MixtormatLightGizmo::Size)
			.HeightOverride(MixtormatLightGizmo::Size)
			.Visibility_Lambda([this]()
			{
				return bPreviewOverlayUiVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatLightGizmo)
				.CameraRotation_Lambda([PreviewViewport]()
				{
					return PreviewViewport->GetCameraRotation();
				})
				.LightDirection_Lambda([PreviewViewport]()
				{
					return PreviewViewport->GetLightDirection();
				})
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[LightingControls]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[GeometryControls]
		]
		// Quality and displacement bottom left, where the status readout was. That line said
		// Real-time, SM6 and a layer count, none of which changes in response to anything the
		// user can do here, so it was three constants and a number already on screen.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[
				SNew(SBox).WidthOverride(MixtormatTokens::InspectorWidth * 0.5f)[SceneControls]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[OutputControls]
		]
		// FOV bottom centre, in the slot the watermark held.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(MixtormatTokens::ViewportOverlayInset)
		[
			SNew(SMixtormatGradientBox)
			.Visibility_Lambda([this]() { return bPreviewOverlayUiVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.StartColor(MixtormatPalette::OverlayPlateTop())
			.EndColor(MixtormatPalette::OverlayPlateBottom())
			.CornerRadius(MixtormatTokens::CornerRadius)
			.Padding(MixtormatTokens::ViewportOverlayClusterInset)
			[
				SNew(SBox).WidthOverride(MixtormatTokens::InspectorWidth * 0.5f)[CameraControls]
			]
		];

	if (!bReusingViewport)
	{
		PreviewViewports.Add(PreviewViewport);
		PreviewViewport->SetPreviewQuality(PreviewQuality);
		PreviewViewport->SetPreviewAntiAliasing(PreviewAntiAliasing);
		PreviewViewport->SetPreviewScreenPercentage(PreviewScreenPercentage);
		PreviewViewport->SetCameraFov(PreviewFov);
		PreviewViewport->SetStudioLighting(StudioLighting);
		PreviewViewport->SetGlobalUVRotation90(bGlobalUVRotation90);
	}
	return PreviewPanel;
}


#undef LOCTEXT_NAMESPACE
