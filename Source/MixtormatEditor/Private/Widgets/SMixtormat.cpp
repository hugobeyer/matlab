#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"

// Construct, edit history, the shared numeric/slider row builders, and the preview
// refresh path every panel calls into.
//
// The rest of the class lives in SMixtormat_Document / _Preview / _Library / _Layers /
// _Inspector / _Shell, and the helper widgets in SMixtormatInternal.h.

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormat::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);

	if (FMixtormatRegistry::GetSurfaces().IsEmpty()
		|| FMixtormatRegistry::GetMasks().IsEmpty())
	{
		FMixtormatSurfaceImporter::ImportDefaultLibrary();
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.Window")))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[BuildTopBar()]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SAssignNew(MainSwitcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()[BuildAuthoringPage()]
				+ SWidgetSwitcher::Slot()
				[
					BuildWorkspacePage(
						LOCTEXT("MixerHeading", "Material Mixer"),
						LOCTEXT("MixerDescription", "Legacy mixer scaffold. Ordered material layers are the primary workflow."))
				]
				+ SWidgetSwitcher::Slot()[BuildPresetsPage()]
			]
			+ SVerticalBox::Slot().AutoHeight()[BuildStatusBar()]
		]
	];

	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildLayerList();
	RebuildMaskList();
	ResetEditHistory(true);
}

bool SMixtormat::AreLayerStacksEqual(
	const TArray<FMixtormatLayer>& A,
	const TArray<FMixtormatLayer>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}

	const UScriptStruct* LayerStruct = FMixtormatLayer::StaticStruct();
	for (int32 LayerIndex = 0; LayerIndex < A.Num(); ++LayerIndex)
	{
		if (!LayerStruct->CompareScriptStruct(&A[LayerIndex], &B[LayerIndex], 0))
		{
			return false;
		}
	}
	return true;
}

bool SMixtormat::HaveSameLayerStructure(
	const TArray<FMixtormatLayer>& A,
	const TArray<FMixtormatLayer>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 LayerIndex = 0; LayerIndex < A.Num(); ++LayerIndex)
	{
		if (A[LayerIndex].Children.Num() != B[LayerIndex].Children.Num())
		{
			return false;
		}
		for (int32 ChildIndex = 0; ChildIndex < A[LayerIndex].Children.Num(); ++ChildIndex)
		{
			if (A[LayerIndex].Children[ChildIndex].Type
				!= B[LayerIndex].Children[ChildIndex].Type)
			{
				return false;
			}
		}
	}
	return true;
}

void SMixtormat::ResetEditHistory(const bool bCurrentStateIsSaved)
{
	UndoHistory.Reset();
	RedoHistory.Reset();
	CurrentHistoryState.Layers = WorkingLayers;
	bHistoryInitialized = true;
	bApplyingHistory = false;
	LastHistoryRecordTime = 0.0;
	if (bCurrentStateIsSaved)
	{
		SavedLayers = WorkingLayers;
	}
}

void SMixtormat::RecordEditHistory()
{
	if (bApplyingHistory)
	{
		return;
	}
	if (!bHistoryInitialized)
	{
		ResetEditHistory(false);
		return;
	}
	if (AreLayerStacksEqual(CurrentHistoryState.Layers, WorkingLayers))
	{
		return;
	}

	constexpr double InteractiveEditWindowSeconds = 0.3;
	constexpr int32 MaximumHistoryStates = 100;
	const double Now = FPlatformTime::Seconds();
	const bool bCoalesceInteractiveEdit = !UndoHistory.IsEmpty()
		&& Now - LastHistoryRecordTime <= InteractiveEditWindowSeconds
		&& HaveSameLayerStructure(CurrentHistoryState.Layers, WorkingLayers);
	if (!bCoalesceInteractiveEdit)
	{
		UndoHistory.Add(CurrentHistoryState);
		if (UndoHistory.Num() > MaximumHistoryStates)
		{
			UndoHistory.RemoveAt(0, UndoHistory.Num() - MaximumHistoryStates);
		}
	}

	CurrentHistoryState.Layers = WorkingLayers;
	RedoHistory.Reset();
	LastHistoryRecordTime = Now;
}

bool SMixtormat::IsCurrentStateSaved() const
{
	return WorkingMaterialAsset.IsValid()
		&& AreLayerStacksEqual(WorkingLayers, SavedLayers);
}

void SMixtormat::ApplyEditHistoryState(const FEditHistoryState& State)
{
	bApplyingHistory = true;
	WorkingLayers = State.Layers;
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	CurrentHistoryState = State;
	LastHistoryRecordTime = 0.0;

	SelectedLayerIndex = WorkingLayers.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(SelectedLayerIndex, 0, WorkingLayers.Num() - 1);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	bHasSelectedLayer = WorkingLayers.IsValidIndex(SelectedLayerIndex);
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");

	SyncSelectedLayerControls();
	RefreshLayeredPreview(false);
	RebuildLayerList();
	RebuildMaskList();
	bApplyingHistory = false;
}

void SMixtormat::SynchronizeHistoryAfterCancelledEdit()
{
	CurrentHistoryState.Layers = WorkingLayers;
	if (!UndoHistory.IsEmpty()
		&& AreLayerStacksEqual(UndoHistory.Last().Layers, WorkingLayers))
	{
		UndoHistory.Pop();
	}
	RedoHistory.Reset();
	LastHistoryRecordTime = 0.0;
	bIsWorkingMaterialDirty = !IsCurrentStateSaved();
	WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
}

FReply SMixtormat::UndoMaterialEdit()
{
	if (UndoHistory.IsEmpty())
	{
		return FReply::Handled();
	}
	RedoHistory.Add(CurrentHistoryState);
	const FEditHistoryState State = UndoHistory.Pop();
	ApplyEditHistoryState(State);
	return FReply::Handled();
}

FReply SMixtormat::RedoMaterialEdit()
{
	if (RedoHistory.IsEmpty())
	{
		return FReply::Handled();
	}
	UndoHistory.Add(CurrentHistoryState);
	const FEditHistoryState State = RedoHistory.Pop();
	ApplyEditHistoryState(State);
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::MakeSlider(
	const FText& Label,
	const TAttribute<double>& Value,
	const double MinValue,
	const double MaxValue,
	const double DefaultValue,
	const double SnapDelta,
	const bool bInteger,
	const FMixtormatOnSliderValueChanged& OnValueChanged,
	const FSimpleDelegate& ResetDelegate,
	const TAttribute<FText>& ToolTip)
{
	const FSimpleDelegate BoundedReset = FSimpleDelegate::CreateLambda([this, ResetDelegate]()
	{
		LastHistoryRecordTime = 0.0;
		ResetDelegate.ExecuteIfBound();
	});

	TSharedRef<SMixtormatSlider> Slider = SNew(SMixtormatSlider)
		.Label(Label)
		.Value(Value)
		.MinValue(MinValue)
		.MaxValue(MaxValue)
		.DefaultValue(DefaultValue)
		.Delta(SnapDelta)
		.bInteger(bInteger)
		.Precision(bInteger ? 0 : 3)
		.ToolTip(ToolTip)
		.OnValueChanged(OnValueChanged)
		.OnReset(BoundedReset)
		// The slider reports its own scrub rather than relying on the global mouse-capture
		// check, which cannot tell a value drag from a viewport orbit or a splitter drag.
		.OnBeginDrag(FSimpleDelegate::CreateLambda([this]()
		{
			bInteractiveEdit = true;
		}))
		.OnEndDrag(FSimpleDelegate::CreateLambda([this]()
		{
			RefreshLayeredPreview();
		}));

	FNumericResetBinding& Binding = NumericResetBindings.AddDefaulted_GetRef();
	Binding.Widget = Slider;
	Binding.Reset = BoundedReset;
	return Slider;
}

void SMixtormat::AddSliderRow(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const TSharedRef<SWidget>& Row)
{
	TargetPanel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[Row];
}

// The four conveniences below exist only so the panels read as one call per row; every one of
// them is the same generic binding with a different resolver.
TSharedRef<SWidget> SMixtormat::MakePeelSlider(
	const FText& Label,
	float FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const double DefaultValue,
	const double SnapDelta,
	const TAttribute<FText>& ToolTip)
{
	return MakeMemberSlider<FMixtormatLayerEffect>(
		Label,
		[this]() { return GetSelectedProceduralPeel(); },
		Member, MinValue, MaxValue, DefaultValue, SnapDelta, ToolTip);
}

TSharedRef<SWidget> SMixtormat::MakePeelSliderInt(
	const FText& Label,
	int32 FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const int32 DefaultValue,
	const TAttribute<FText>& ToolTip)
{
	return MakeMemberSliderInt<FMixtormatLayerEffect>(
		Label,
		[this]() { return GetSelectedProceduralPeel(); },
		Member, MinValue, MaxValue, DefaultValue, ToolTip);
}

TSharedRef<SWidget> SMixtormat::MakeErosionSlider(
	const FText& Label,
	float FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const double DefaultValue,
	const double SnapDelta,
	const TAttribute<FText>& ToolTip)
{
	return MakeMemberSlider<FMixtormatLayerEffect>(
		Label,
		[this]() { return GetSelectedErosion(); },
		Member, MinValue, MaxValue, DefaultValue, SnapDelta, ToolTip);
}

TSharedRef<SWidget> SMixtormat::MakeErosionSliderInt(
	const FText& Label,
	int32 FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const int32 DefaultValue,
	const TAttribute<FText>& ToolTip)
{
	return MakeMemberSliderInt<FMixtormatLayerEffect>(
		Label,
		[this]() { return GetSelectedErosion(); },
		Member, MinValue, MaxValue, DefaultValue, ToolTip);
}

void SMixtormat::AddPeelSlider(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Label,
	float FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const double DefaultValue,
	const double SnapDelta,
	const TAttribute<FText>& ToolTip)
{
	AddSliderRow(TargetPanel, MakePeelSlider(Label, Member, MinValue, MaxValue, DefaultValue, SnapDelta, ToolTip));
}

void SMixtormat::AddPeelSliderInt(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Label,
	int32 FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const int32 DefaultValue,
	const TAttribute<FText>& ToolTip)
{
	AddSliderRow(TargetPanel, MakePeelSliderInt(Label, Member, MinValue, MaxValue, DefaultValue, ToolTip));
}

void SMixtormat::AddErosionSlider(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Label,
	float FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const double DefaultValue,
	const double SnapDelta,
	const TAttribute<FText>& ToolTip)
{
	AddSliderRow(TargetPanel, MakeErosionSlider(Label, Member, MinValue, MaxValue, DefaultValue, SnapDelta, ToolTip));
}

void SMixtormat::AddErosionSliderInt(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Label,
	int32 FMixtormatLayerEffect::* Member,
	const double MinValue,
	const double MaxValue,
	const int32 DefaultValue,
	const TAttribute<FText>& ToolTip)
{
	AddSliderRow(TargetPanel, MakeErosionSliderInt(Label, Member, MinValue, MaxValue, DefaultValue, ToolTip));
}

bool SMixtormat::ResetHoveredNumericControl()
{
	for (int32 Index = NumericResetBindings.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<SWidget> Widget = NumericResetBindings[Index].Widget.Pin();
		if (!Widget.IsValid())
		{
			NumericResetBindings.RemoveAtSwap(Index);
			continue;
		}
		if (Widget->IsHovered())
		{
			NumericResetBindings[Index].Reset.ExecuteIfBound();
			return true;
		}
	}
	return false;
}

FReply SMixtormat::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	(void)MyGeometry;
	const bool bModifierDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::BackSpace && ResetHoveredNumericControl())
	{
		return FReply::Handled();
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Z)
	{
		return InKeyEvent.IsShiftDown() ? RedoMaterialEdit() : UndoMaterialEdit();
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Y)
	{
		return RedoMaterialEdit();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SMixtormat::IsInteractiveEdit() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	const FSlateApplication& App = FSlateApplication::Get();
	return App.HasAnyMouseCaptor()
		&& App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
}

void SMixtormat::RefreshLayeredPreview(const bool bMarkDirty)
{
	if (!bHasWorkingMaterial)
	{
		return;
	}

	bInteractiveEdit = IsInteractiveEdit();

	if (bMarkDirty)
	{
		// RecordEditHistory deep-compares and then deep-copies the whole layer stack, and the
		// dirty check compares it again. That ran on every value change, ahead of the
		// per-frame coalescing below, so a scrub paid for it several times a frame. During a
		// drag it is deferred to the frame the drag ends, which also gives one undo step per
		// drag instead of relying on the 0.3s coalescing window.
		if (bInteractiveEdit)
		{
			bInteractiveHistoryPending = true;
		}
		else
		{
			RecordEditHistory();
			bIsWorkingMaterialDirty = !IsCurrentStateSaved();
			WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
		}
	}
	if (bPreviewRefreshPending)
	{
		return;
	}

	bPreviewRefreshPending = true;
	RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SMixtormat::FlushPendingPreviewRefresh));
}

EActiveTimerReturnType SMixtormat::FlushPendingPreviewRefresh(
	const double CurrentTime,
	const float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	bPreviewRefreshPending = false;
	if (!bHasWorkingMaterial)
	{
		bInteractiveEdit = false;
		bInteractiveHistoryPending = false;
		return EActiveTimerReturnType::Stop;
	}

	// A drag produces no release event here, so the timer keeps itself alive while the mouse
	// is captured and settles on the first frame after it is let go: one full-resolution
	// composite, and the history entry the drag deferred.
	const bool bWasInteractive = bInteractiveEdit;
	bInteractiveEdit = IsInteractiveEdit();
	const bool bDragJustEnded = bWasInteractive && !bInteractiveEdit;
	if (bDragJustEnded && bInteractiveHistoryPending)
	{
		bInteractiveHistoryPending = false;
		RecordEditHistory();
		bIsWorkingMaterialDirty = !IsCurrentStateSaved();
		WorkingStatusText = bIsWorkingMaterialDirty ? TEXT("Unsaved changes") : TEXT("All changes saved");
	}

	TArray<FMixtormatLayer> PreviewOverrideLayers;
	const TArray<FMixtormatLayer>* PreviewLayers = &WorkingLayers;
	const int32 BypassedChildIndex = GetSelectedChildIndex();
	if (bBypassSelectedChild
		&& WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(BypassedChildIndex))
	{
		PreviewOverrideLayers = WorkingLayers;
		FMixtormatLayerChild& Child = PreviewOverrideLayers[SelectedLayerIndex].Children[BypassedChildIndex];
		if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			Child.Effect.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::Generated)
		{
			Child.Generated.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::Craquelure)
		{
			Child.Craquelure.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::ColorId)
		{
			Child.ColorId.bEnabled = false;
		}
		else
		{
			Child.Mask.bEnabled = false;
		}
		PreviewLayers = &PreviewOverrideLayers;
	}
	if (DebugPreviewMode == EMixtormatDebugPreviewMode::None
		&& WorkingLayers.IsValidIndex(SoloLayerIndex))
	{
		FMixtormatLayer SoloLayer = (*PreviewLayers)[SoloLayerIndex];
		PreviewOverrideLayers.Reset();
		PreviewOverrideLayers.Add(MoveTemp(SoloLayer));
		PreviewOverrideLayers[0].bEnabled = true;
		PreviewOverrideLayers[0].HeightReferenceLayerIndex = INDEX_NONE;
		PreviewLayers = &PreviewOverrideLayers;
	}
	else if (DebugPreviewMode == EMixtormatDebugPreviewMode::None
		&& bShowCompositionBefore
		&& !WorkingLayers.IsEmpty())
	{
		FMixtormatLayer BaseLayer = (*PreviewLayers)[0];
		PreviewOverrideLayers.Reset();
		PreviewOverrideLayers.Add(MoveTemp(BaseLayer));
		PreviewOverrideLayers[0].bEnabled = true;
		PreviewOverrideLayers[0].HeightReferenceLayerIndex = INDEX_NONE;
		PreviewLayers = &PreviewOverrideLayers;
	}

	// Always the full composition resolution, dragging included. Halving the side while scrubbing
	// bought latency at the cost of showing something the material is not: the peel's solve
	// resolution is derived from it, so the seeding grid, the eikonal iteration count and the
	// flake cell size all changed under the cursor and the shape settled differently the moment
	// the drag ended. A preview that reshapes itself on mouse-up is worse than a slower one.
	//
	// Drag cost is still reduced, but only where it costs nothing to look at: the undo history
	// deferral above, which does no drawing at all.
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			FMixtormatDebugPreviewSettings DebugSettings;
			DebugSettings.Mode = DebugPreviewMode;
			DebugSettings.LayerIndex = SelectedLayerIndex;
			DebugSettings.ChildIndex = DebugPreviewMode == EMixtormatDebugPreviewMode::LayerMask
				? SelectedMaskIndex
				: INDEX_NONE;
			Viewport->SetPreviewLayers(*PreviewLayers, CompositionResolution, DebugSettings);
		}
	}

	// Keep the timer alive for as long as the mouse is held, so the drag-end frame above is
	// reached even if no further value change arrives.
	if (bInteractiveEdit)
	{
		bPreviewRefreshPending = true;
		return EActiveTimerReturnType::Continue;
	}
	return EActiveTimerReturnType::Stop;
}

#undef LOCTEXT_NAMESPACE
