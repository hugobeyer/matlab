// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Containers/SMixtormatInspectorCard.h"

// Construct, edit history, the shared numeric/slider row builders, and the preview
// refresh path every panel calls into.
//
// The rest of the class lives in SMixtormat_Document / _Preview / _Library / _Layers /
// _Inspector / _Shell, and the helper widgets in SMixtormatInternal.h.

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormat::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetUpdatedHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(
		this, &SMixtormat::HandleReferencedCompositionUpdated);

	BuildWorkspaceUI();
	ResetEditHistory(true);
}

void SMixtormat::HandleReferencedCompositionUpdated(const FAssetData& AssetData)
{
	if (!bHasWorkingMaterial || !Cast<UMixtormatMaterial>(AssetData.GetAsset()))
	{
		return;
	}
	const bool bHasReferences = WorkingLayers.ContainsByPredicate(
		[](const FMixtormatLayer& Layer)
		{
			return !Layer.SourceComposition.IsNull();
		});
	if (bHasReferences)
	{
		RefreshLayeredPreview(false);
		RebuildLayerList();
	}
}

void SMixtormat::BuildWorkspaceUI()
{
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
			]
			+ SVerticalBox::Slot().AutoHeight()[BuildStatusBar()]
		]
	];

	RebuildCategoryList();
	RebuildSurfaceList();
	RebuildUserLibraryList();
	RebuildLayerList();
	RebuildMaskList();
}

bool SMixtormat::AreLayerGroupsEqual(
	const TArray<FMixtormatLayerGroup>& A,
	const TArray<FMixtormatLayerGroup>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	const UScriptStruct* GroupStruct = FMixtormatLayerGroup::StaticStruct();
	for (int32 GroupIndex = 0; GroupIndex < A.Num(); ++GroupIndex)
	{
		if (!GroupStruct->CompareScriptStruct(&A[GroupIndex], &B[GroupIndex], 0))
		{
			return false;
		}
	}
	return true;
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
				!= B[LayerIndex].Children[ChildIndex].Type
				|| A[LayerIndex].Children[ChildIndex].ScopeOwnerChildId
					!= B[LayerIndex].Children[ChildIndex].ScopeOwnerChildId)
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
	CurrentHistoryState.Groups = WorkingLayerGroups;
	CurrentHistoryState.bRotateUV90 = bGlobalUVRotation90;
	bHistoryInitialized = true;
	bApplyingHistory = false;
	LastHistoryRecordTime = 0.0;
	if (bCurrentStateIsSaved)
	{
		SavedLayers = WorkingLayers;
		SavedLayerGroups = WorkingLayerGroups;
		bSavedGlobalUVRotation90 = bGlobalUVRotation90;
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
	if (AreLayerStacksEqual(CurrentHistoryState.Layers, WorkingLayers)
		&& AreLayerGroupsEqual(CurrentHistoryState.Groups, WorkingLayerGroups)
		&& CurrentHistoryState.bRotateUV90 == bGlobalUVRotation90)
	{
		return;
	}

	constexpr double InteractiveEditWindowSeconds = 0.3;
	constexpr int32 MaximumHistoryStates = 100;
	const double Now = FPlatformTime::Seconds();
	const bool bCoalesceInteractiveEdit = !UndoHistory.IsEmpty()
		&& Now - LastHistoryRecordTime <= InteractiveEditWindowSeconds
		&& HaveSameLayerStructure(CurrentHistoryState.Layers, WorkingLayers)
		&& AreLayerGroupsEqual(CurrentHistoryState.Groups, WorkingLayerGroups);
	if (!bCoalesceInteractiveEdit)
	{
		UndoHistory.Add(CurrentHistoryState);
		if (UndoHistory.Num() > MaximumHistoryStates)
		{
			UndoHistory.RemoveAt(0, UndoHistory.Num() - MaximumHistoryStates);
		}
	}

	CurrentHistoryState.Layers = WorkingLayers;
	CurrentHistoryState.Groups = WorkingLayerGroups;
	CurrentHistoryState.bRotateUV90 = bGlobalUVRotation90;
	RedoHistory.Reset();
	LastHistoryRecordTime = Now;
}

bool SMixtormat::IsCurrentStateSaved() const
{
	return WorkingMaterialAsset.IsValid()
		&& AreLayerStacksEqual(WorkingLayers, SavedLayers)
		&& AreLayerGroupsEqual(WorkingLayerGroups, SavedLayerGroups)
		&& bGlobalUVRotation90 == bSavedGlobalUVRotation90;
}

void SMixtormat::ApplyEditHistoryState(const FEditHistoryState& State)
{
	bApplyingHistory = true;
	WorkingLayers = State.Layers;
	WorkingLayerGroups = State.Groups;
	bGlobalUVRotation90 = State.bRotateUV90;
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetGlobalUVRotation90(bGlobalUVRotation90);
		}
	}
	SoloLayerIndex = INDEX_NONE;
	bShowCompositionBefore = false;
	CurrentHistoryState = State;
	LastHistoryRecordTime = 0.0;

	SelectedLayerIndex = WorkingLayers.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(SelectedLayerIndex, 0, WorkingLayers.Num() - 1);
	SelectedEffectIndex = INDEX_NONE;
	SelectedMaskIndex = INDEX_NONE;
	// Undoing past a Create Group leaves these naming layers and a group the restored state does
	// not have. Dropping them is the same reasoning as clearing the child selection above: the
	// state that comes back is not the state they were taken against.
	SelectedLayerIds.Reset();
	SelectionAnchorLayerId.Invalidate();
	SelectedGroupId.Invalidate();
	SelectedGroupChildIndex = INDEX_NONE;
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
	CurrentHistoryState.Groups = WorkingLayerGroups;
	CurrentHistoryState.bRotateUV90 = bGlobalUVRotation90;
	if (!UndoHistory.IsEmpty()
		&& AreLayerStacksEqual(UndoHistory.Last().Layers, WorkingLayers)
		&& AreLayerGroupsEqual(UndoHistory.Last().Groups, WorkingLayerGroups)
		&& UndoHistory.Last().bRotateUV90 == bGlobalUVRotation90)
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
	const TAttribute<double>& MinValue,
	const TAttribute<double>& MaxValue,
	const double DefaultValue,
	const TAttribute<double>& SnapDelta,
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

TSharedRef<SVerticalBox> SMixtormat::AddCard(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Title,
	const TSharedPtr<SWidget>& HeaderAction)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	TargetPanel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::CardGap)
		[
			SNew(SMixtormatInspectorCard)
			.Title(Title)
			.HeaderAction(HeaderAction)
			[
				Rows
			]
		];
	return Rows;
}

void SMixtormat::AddSliderRow(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const TSharedRef<SWidget>& Row)
{
	TargetPanel->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::SliderRowGap)
		[Row];
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
	// Rename the selected group or layer. Not double-click: that opens and shuts the row, and one
	// gesture cannot mean both without the user having to guess which it will be this time.
	// F12 is an alias for F2, not a second gesture: on a laptop whose F-row is media keys by
	// default, F2 is the one most likely to be stolen by the OS before Slate ever sees it.
	// F2 stays the shortcut the menus advertise.
	if (!bModifierDown
		&& (InKeyEvent.GetKey() == EKeys::F2 || InKeyEvent.GetKey() == EKeys::F12)
		&& BeginRenameSelection())
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
	// G for the bottom galleries, the same idea as H for the viewport overlay: one key,
	// no modifier, toggles the panel that eats the most screen space.
	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::G)
	{
		return ToggleBottomLibraryCollapsed();
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

	// Before anything reads the stack: an instance shows what its source says, and the row,
	// the badge and the inspector all read the authored payload to find that out.
	SyncChildInstances();

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

	// Selection, enable state and source replacement can invalidate a data preview: a different
	// child got selected, the previewed child (or its layer) got disabled, or it was deleted. The
	// target is re-resolved from current selection rather than trusted as-is, so a reorder or a
	// group re-broadcast that still names the same child leaves the preview showing, and anything
	// else clears it.
	if (DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput)
	{
		const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
		const bool bStillValid = Child
			&& ChildPreviewTarget == ResolveChildPreviewTarget(
				ChildPreviewTarget.OutputName, ChildPreviewTarget.Kind, ChildPreviewTarget.GapMaskName)
			&& IsChildOutputPreviewReady(*Child);
		if (!bStillValid)
		{
			DebugPreviewMode = EMixtormatDebugPreviewMode::None;
			ChildPreviewTarget = FMixtormatChildPreviewTarget();
		}
	}

	TArray<FMixtormatLayer> PreviewOverrideLayers;
	const TArray<FMixtormatLayer>* PreviewLayers = &WorkingLayers;
	// Solo and composition-before isolate one layer and force it visible. A disabled group would
	// otherwise put it straight back out of sight, so those two paths preview against groups that
	// are all switched on -- the shared children still apply, only the group gate is lifted.
	TArray<FMixtormatLayerGroup> PreviewOverrideGroups;
	const TArray<FMixtormatLayerGroup>* PreviewGroups = &WorkingLayerGroups;
	const auto UseEnabledGroupsForIsolation = [&]()
	{
		PreviewOverrideGroups = WorkingLayerGroups;
		for (FMixtormatLayerGroup& Group : PreviewOverrideGroups)
		{
			Group.bEnabled = true;
		}
		PreviewGroups = &PreviewOverrideGroups;
	};
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
		else if (Child.Type == EMixtormatLayerChildType::Filter)
		{
			Child.Filter.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::HsvFilter)
		{
			Child.HsvFilter.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::RandomId)
		{
			Child.RandomId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::RampId)
		{
			Child.RampId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::UvFromIds)
		{
			Child.UvId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::ReliefFromIds)
		{
			Child.ReliefId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::PatternId)
		{
			Child.PatternId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::CombineId)
		{
			Child.CombineId.bEnabled = false;
		}
		else if (Child.Type == EMixtormatLayerChildType::Generator)
		{
			// The wrapper, not the payload. Bypass means "this node does not run", and the
			// gather branch tests FMixtormatGenerator::bEnabled before it looks at the kind --
			// so one flag turns off every generator rather than one per payload.
			Child.Generator.bEnabled = false;
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
		UseEnabledGroupsForIsolation();
	}
	else if (DebugPreviewMode == EMixtormatDebugPreviewMode::None
		&& bShowCompositionBefore
		&& !WorkingLayers.IsEmpty())
	{
		FMixtormatLayer BottomLayer = (*PreviewLayers)[0];
		PreviewOverrideLayers.Reset();
		PreviewOverrideLayers.Add(MoveTemp(BottomLayer));
		PreviewOverrideLayers[0].bEnabled = true;
		PreviewOverrideLayers[0].HeightReferenceLayerIndex = INDEX_NONE;
		PreviewLayers = &PreviewOverrideLayers;
		UseEnabledGroupsForIsolation();
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
				? GetSelectedChildIndex()
				: INDEX_NONE;
			// LayerIndex/ChildIndex above are meaningless for ChildOutput: ChildTarget already
			// names the child by GUID, flattened to one concrete layer at click time (see
			// ResolveChildPreviewTarget), and the compositor resolves it to indices itself, once,
			// at the top of RequestComposeInternal.
			DebugSettings.ChildTarget = ChildPreviewTarget;
			Viewport->SetPreviewLayers(
				*PreviewLayers, *PreviewGroups, CompositionResolution, DebugSettings);
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
