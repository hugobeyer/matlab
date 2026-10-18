// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Layers/MixtormatLayerShortcuts.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatLocatorOutline.h"
#include "Widgets/Layers/MixtormatLayersPrivate.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Style/MixtormatThemeStore.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MixtormatEditorSettings.h"

// Construct, edit history, the shared numeric/slider row builders, and the preview
// refresh path every panel calls into.
//
// The rest of the class lives in SMixtormat_Document / _Preview / _Library / _Layers /
// _Inspector / _Shell, and the helper widgets in SMixtormatInternal.h.

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	class FMixtormatWorkspaceHotkeys final : public IInputProcessor
	{
	public:
		explicit FMixtormatWorkspaceHotkeys(
			const TWeakPtr<SMixtormat>& InWorkspace,
			TFunction<bool(const FKeyEvent&)>&& InHandleKey)
			: Workspace(InWorkspace)
			, HandleKey(MoveTemp(InHandleKey))
		{
		}

		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
		{
			if (KeyEvent.IsRepeat() || KeyEvent.IsControlDown() || KeyEvent.IsCommandDown()
				|| KeyEvent.IsAltDown())
			{
				return false;
			}

			const TSharedPtr<SMixtormat> Owner = Workspace.Pin();
			const TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget();
			if (!Owner.IsValid() || !FocusedWidget.IsValid())
			{
				return false;
			}

			// Input preprocessors run across *every* Unreal editor window.
			// Only consume keys with real keyboard focus inside this active workspace;
			// a Material Editor, another tab, or a different Mixtormat instance
			// must never change the preview/lighting/layer UI here.
			FWidgetPath FocusPath;
			if (!SlateApp.FindPathToWidget(FocusedWidget.ToSharedRef(), FocusPath, EVisibility::Visible)
				|| !FocusPath.ContainsWidget(Owner.Get())
				|| FocusPath.TopLevelWindow != SlateApp.GetActiveTopLevelWindow())
			{
				return false;
			}

			const FString WidgetType = FocusedWidget->GetType().ToString();
			if (WidgetType.Contains(TEXT("EditableText")) || WidgetType.Contains(TEXT("TextEntry")))
			{
				return false;
			}

			return HandleKey ? HandleKey(KeyEvent) : false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("MixtormatWorkspaceHotkeys"); }

	private:
		TWeakPtr<SMixtormat> Workspace;
		TFunction<bool(const FKeyEvent&)> HandleKey;
	};
}

void SMixtormat::Construct(const FArguments& InArgs)
{
	MaterialGalleryTileSize = FMixtormatThemeStore::GetResolved().GalleryLayout.TileSize;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetUpdatedHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(
		this, &SMixtormat::HandleReferencedCompositionUpdated);

	if (const UMixtormatEditorSettings* Settings = GetDefault<UMixtormatEditorSettings>())
	{
		bGalleryPinned = Settings->bGalleryDrawerPinned;
		GalleryDrawerHeight = Settings->GalleryDrawerHeight;
		bBottomLibraryCollapsed = !bGalleryPinned;
	}

	BuildWorkspaceUI();

	// Start with an editable Fill document instead of the inert "Create Material" empty state.
	// This plugin-content material supplies the neutral preview instance; it is not authored as a
	// surface layer, so the document still begins with one independent Fill layer.
	const FSoftObjectPath DefaultPreviewMaterialPath(
		TEXT("/Mixtormat/Materials/Instances/Preview/MI_Preview_Mixtormat.MI_Preview_Mixtormat"));
	SelectedPreviewMaterial.Reset(Cast<UMaterialInstanceConstant>(DefaultPreviewMaterialPath.TryLoad()));
	StartNewMaterialWith(EMixtormatLayerType::Fill);

	const TWeakPtr<SMixtormat> WeakSelf = StaticCastSharedRef<SMixtormat>(AsShared());
	WorkspaceHotkeyProcessor = MakeShared<FMixtormatWorkspaceHotkeys>(WeakSelf, [WeakSelf](const FKeyEvent& KeyEvent)
	{
		const TSharedPtr<SMixtormat> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return false;
		}
		// Once the popup is open, its buttons can take keyboard focus away from the viewport.
		// Keep Q as a close key in that state only; opening it remains viewport-scoped.
		if (Self->bQuickControlsOpen && KeyEvent.GetKey() == EKeys::Q)
		{
			Self->CloseQuickControls();
			return true;
		}
		if (KeyEvent.IsShiftDown())
		{
			const FKey Key = KeyEvent.GetKey();
			if (Key == EKeys::V)
			{
				Self->ResetDiagnosticOverlays();
				return true;
			}
			const EMixtormatStudioLighting Preset =
				Key == EKeys::One ? EMixtormatStudioLighting::Neutral :
				Key == EKeys::Two ? EMixtormatStudioLighting::Soft :
				Key == EKeys::Three ? EMixtormatStudioLighting::Dramatic :
				Key == EKeys::Four ? EMixtormatStudioLighting::Rim :
				EMixtormatStudioLighting::Workshop;
			if (Key == EKeys::One || Key == EKeys::Two || Key == EKeys::Three
				|| Key == EKeys::Four || Key == EKeys::Five)
			{
				Self->SetStudioLighting(Preset);
				return true;
			}
			return false;
		}
		// Shared diagnostics follow the stack selection, not the widget with keyboard focus.
		// The processor's workspace/text-entry guards prevent stealing typing or other editor keys.
		if (KeyEvent.GetKey() == EKeys::W)
		{
			Self->ToggleMaskOverlayPreview();
			return true;
		}
		if (KeyEvent.GetKey() == EKeys::E)
		{
			Self->ToggleFlowOverlayPreview();
			return true;
		}
		if (KeyEvent.GetKey() == EKeys::R || KeyEvent.GetKey() == EKeys::I)
		{
			Self->ToggleRegionIdPreview();
			return true;
		}
		// V is the composited PBR channel cycle only; it never selects module masks/flow/UVs/IDs.
		if (KeyEvent.GetKey() == EKeys::V)
		{
			Self->CycleChannelPreview();
			return true;
		}
		if (KeyEvent.GetKey() == EKeys::L)
		{
			Self->SetPreviewQuality(Self->PreviewQuality == EMixtormatPreviewQuality::Lumen
				? EMixtormatPreviewQuality::Default : EMixtormatPreviewQuality::Lumen);
			return true;
		}
		if (KeyEvent.GetKey() == EKeys::P)
		{
			Self->ToggleInspectorCollapsed();
			return true;
		}
		if (KeyEvent.GetKey() == EKeys::G)
		{
			Self->ToggleBottomLibraryCollapsed();
			return true;
		}
		return false;
	});
	FSlateApplication::Get().RegisterInputPreProcessor(WorkspaceHotkeyProcessor);
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
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Shell.Ground)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					// Locator identity marker: lets the UI STYLE eye outline the top bar itself.
					SNew(SMixtormatTopBar, BuildTopBar())
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SBox)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SAssignNew(MainSwitcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()[BuildAuthoringPage()]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[BuildStatusBar()]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			// The eye's outline overlay: hit-test-invisible, paints only while a locate is live.
			SNew(SMixtormatLocatorOutline)
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
	SelectedLayerChildIds.Reset();
	SelectionAnchorLayerChildId.Invalidate();
	// A fresh history baseline also begins a fresh document-local disclosure session.
	CollapsedGeneratorAddresses.Reset();
	UndoHistory.Reset();
	RedoHistory.Reset();
	CurrentHistoryState.Layers = WorkingLayers;
	CurrentHistoryState.Groups = WorkingLayerGroups;
	CurrentHistoryState.bRotateUV90 = bGlobalUVRotation90;
	CurrentHistoryState.FinalSettings = WorkingFinalSettings;
	bHistoryInitialized = true;
	bApplyingHistory = false;
	LastHistoryRecordTime = 0.0;
	if (bCurrentStateIsSaved)
	{
		SavedLayers = WorkingLayers;
		SavedLayerGroups = WorkingLayerGroups;
		bSavedGlobalUVRotation90 = bGlobalUVRotation90;
		SavedFinalSettings = WorkingFinalSettings;
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
		&& CurrentHistoryState.bRotateUV90 == bGlobalUVRotation90
		&& CurrentHistoryState.FinalSettings == WorkingFinalSettings)
	{
		return;
	}

	constexpr double InteractiveEditWindowSeconds = 0.3;
	constexpr int32 MaximumHistoryStates = 100;
	const double Now = FPlatformTime::Seconds();
	const bool bCoalesceInteractiveEdit = !UndoHistory.IsEmpty()
		&& Now - LastHistoryRecordTime <= InteractiveEditWindowSeconds
		&& HaveSameLayerStructure(CurrentHistoryState.Layers, WorkingLayers)
		&& AreLayerGroupsEqual(CurrentHistoryState.Groups, WorkingLayerGroups)
;
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
	CurrentHistoryState.FinalSettings = WorkingFinalSettings;
	RedoHistory.Reset();
	LastHistoryRecordTime = Now;
}

bool SMixtormat::IsCurrentStateSaved() const
{
	return WorkingMaterialAsset.IsValid()
		&& AreLayerStacksEqual(WorkingLayers, SavedLayers)
		&& AreLayerGroupsEqual(WorkingLayerGroups, SavedLayerGroups)
		&& bGlobalUVRotation90 == bSavedGlobalUVRotation90
		&& WorkingFinalSettings == SavedFinalSettings;
}

void SMixtormat::ApplyEditHistoryState(const FEditHistoryState& State)
{
	bApplyingHistory = true;
	WorkingLayers = State.Layers;
	WorkingLayerGroups = State.Groups;
	bGlobalUVRotation90 = State.bRotateUV90;
	WorkingFinalSettings = State.FinalSettings;
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
	SelectedLayerChildIds.Reset();
	SelectionAnchorLayerChildId.Invalidate();
	// Undoing past a Group leaves these naming layers and a group the restored state does
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
	CurrentHistoryState.FinalSettings = WorkingFinalSettings;
	if (!UndoHistory.IsEmpty()
		&& AreLayerStacksEqual(UndoHistory.Last().Layers, WorkingLayers)
		&& AreLayerGroupsEqual(UndoHistory.Last().Groups, WorkingLayerGroups)
		&& UndoHistory.Last().bRotateUV90 == bGlobalUVRotation90
		&& UndoHistory.Last().FinalSettings == WorkingFinalSettings)
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
	const TAttribute<FText>& Label,
	const TAttribute<double>& Value,
	const TAttribute<double>& MinValue,
	const TAttribute<double>& MaxValue,
	const TAttribute<double>& DefaultValue,
	const TAttribute<double>& SnapDelta,
	const bool bInteger,
	const FMixtormatOnSliderValueChanged& OnValueChanged,
	const FSimpleDelegate& ResetDelegate,
	const TAttribute<FText>& ToolTip,
	const FMixtormatSliderRangeOptions& RangeOptions)
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
		.ExpandableRange(RangeOptions.bExpandable)
		.HardMinValue(RangeOptions.HardMin)
		.HardMaxValue(RangeOptions.HardMax)
		.KeepPopupOpenOnCommit(RangeOptions.bKeepPopupOpenOnCommit)
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
	// One help system for the whole UI: the shared styled help rather than Slate's default
	// tooltip. The binding keeps the slider itself, so hover-to-reset still finds it inside the
	// wrapper, and a caller with no tooltip of its own still gets the slider's gesture hint.
	const FText HelpText = ToolTip.IsSet()
		? ToolTip.Get(FText::GetEmpty())
		: LOCTEXT("SliderHint", "Drag to adjust · click to type · Shift fine · Ctrl+Shift finer · Ctrl snap · MMB or hover + Backspace to reset");
	return SNew(SMixtormatHelp)
		.Text(HelpText)
		.Enabled_Lambda([this]() { return !bIsBaking; })
		[Slider];
}

TSharedRef<SVerticalBox> SMixtormat::AddCard(
	const TSharedRef<SVerticalBox>& TargetPanel,
	const FText& Title,
	const TSharedPtr<SWidget>& HeaderAction,
	const TSharedPtr<SWidget>& LeadingHeaderContent)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	TargetPanel->AddSlot()
		.AutoHeight()
		[
			SNew(SMixtormatInspectorCard)
			.Title(Title)
			.HeaderAction(HeaderAction)
			.LeadingHeaderContent(LeadingHeaderContent)
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
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
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
	for (int32 Index = EnumResetBindings.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<SWidget> Widget = EnumResetBindings[Index].Widget.Pin();
		if (!Widget.IsValid())
		{
			EnumResetBindings.RemoveAtSwap(Index);
			continue;
		}
		if (Widget->IsHovered())
		{
			EnumResetBindings[Index].Reset.ExecuteIfBound();
			return true;
		}
	}
	return false;
}

FReply SMixtormat::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	(void)MyGeometry;
	// Escape closes the quick controls. Text entry keeps its own Escape -- it never reaches here --
	// so this only fires when the popup itself is the thing on screen.
	if (InKeyEvent.GetKey() == EKeys::Escape && bQuickControlsOpen)
	{
		CloseQuickControls();
		return FReply::Handled();
	}
	const bool bModifierDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	using EShortcut = MixtormatLayerShortcuts::EAction;
	const auto HasShortcut = [&InKeyEvent](EShortcut Action)
	{
		return MixtormatLayerShortcuts::Matches(InKeyEvent, Action);
	};
	if (HasShortcut(EShortcut::Solo) && WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& !SelectedGroupId.IsValid())
		return ToggleLayerSolo(SelectedLayerIndex);
	if (HasShortcut(EShortcut::ToggleEnabled) && WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& !SelectedGroupId.IsValid())
	{
		SetWorkingLayerEnabled(WorkingLayers[SelectedLayerIndex].bEnabled
			? ECheckBoxState::Unchecked : ECheckBoxState::Checked, SelectedLayerIndex);
		return FReply::Handled();
	}
	if (HasShortcut(EShortcut::Copy))
	{
		const FMixtormatChildAddress Address = GetSelectedChildAddress();
		if (ResolveChildAt(Address))
		{
			CopyChild(Address, false);
			return FReply::Handled();
		}
	}
	if (HasShortcut(EShortcut::Paste))
	{
		if (SelectedGroupId.IsValid())
		{
			const FMixtormatChildAddress Address{EMixtormatChildOwnerType::Group, SelectedGroupId, FGuid()};
			if (CanPasteChild(Address, INDEX_NONE)) return PasteChild(Address);
		}
		else if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
		{
			const FMixtormatChildAddress Address{EMixtormatChildOwnerType::Layer,
				WorkingLayers[SelectedLayerIndex].LayerId, FGuid()};
			if (CanPasteChild(Address, INDEX_NONE)) return PasteChild(Address);
		}
	}

	if (!bModifierDown && InKeyEvent.GetKey() == EKeys::BackSpace && ResetHoveredNumericControl())
	{
		return FReply::Handled();
	}
	if (!bModifierDown && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown()
		&& !InKeyEvent.IsRepeat()
		&& (InKeyEvent.GetKey() == EKeys::R || InKeyEvent.GetKey() == EKeys::I))
	{
		return ToggleRegionIdPreview();
	}
	// Rename the selected group or layer. Not double-click: that opens and shuts the row, and one
	// gesture cannot mean both without the user having to guess which it will be this time.
	// F12 is an alias for F2, not a second gesture: on a laptop whose F-row is media keys by
	// default, F2 is the one most likely to be stolen by the OS before Slate ever sees it.
	// F2 stays the shortcut the menus advertise.
	if (!bModifierDown
		&& (InKeyEvent.GetKey() == EKeys::F2 || InKeyEvent.GetKey() == EKeys::F12))
	{
		return ExecuteLayerSelectionCommand(ELayerSelectionCommand::Rename);
	}
	if (bModifierDown && !InKeyEvent.IsAltDown() && !InKeyEvent.IsRepeat())
	{
		if (HasShortcut(EShortcut::Duplicate))
		{
			return ExecuteLayerSelectionCommand(ELayerSelectionCommand::Duplicate);
		}
		if (HasShortcut(EShortcut::CopyAsInstance))
		{
			return ExecuteLayerSelectionCommand(ELayerSelectionCommand::CopyAsInstance);
		}
		if (HasShortcut(EShortcut::Group))
		{
			return ExecuteLayerSelectionCommand(ELayerSelectionCommand::Group);
		}
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Z)
	{
		return InKeyEvent.IsShiftDown() ? RedoMaterialEdit() : UndoMaterialEdit();
	}
	if (bModifierDown && InKeyEvent.GetKey() == EKeys::Y)
	{
		return RedoMaterialEdit();
	}
	if (!bModifierDown && !InKeyEvent.IsAltDown() && !InKeyEvent.IsRepeat()
		&& InKeyEvent.IsShiftDown())
	{
		const FKey Key = InKeyEvent.GetKey();
		if (Key == EKeys::One || Key == EKeys::Two || Key == EKeys::Three
			|| Key == EKeys::Four || Key == EKeys::Five)
		{
			return SetStudioLighting(Key == EKeys::One ? EMixtormatStudioLighting::Neutral
				: Key == EKeys::Two ? EMixtormatStudioLighting::Soft
				: Key == EKeys::Three ? EMixtormatStudioLighting::Dramatic
				: Key == EKeys::Four ? EMixtormatStudioLighting::Rim
				: EMixtormatStudioLighting::Workshop);
		}
	}
	if (!bModifierDown && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown()
		&& !InKeyEvent.IsRepeat())
	{
		if (InKeyEvent.GetKey() == EKeys::L)
		{
			return SetPreviewQuality(PreviewQuality == EMixtormatPreviewQuality::Lumen
				? EMixtormatPreviewQuality::Default : EMixtormatPreviewQuality::Lumen);
		}
		if (InKeyEvent.GetKey() == EKeys::P)
		{
			return ToggleInspectorCollapsed();
		}
	}
	// Del on the selection: the same removals the context menus offer, no confirmation.
	// Undoable like every edit -- the removal functions record history themselves.
	if (!bModifierDown && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown()
		&& HasShortcut(EShortcut::Remove))
	{
		if (SelectedGroupId.IsValid() && SelectedLayerIndex == INDEX_NONE)
		{
			// Multi-select: every selected child of the group, one history entry.
			const TArray<int32> Selected = GetSelectedGroupChildIndices();
			if (Selected.Num() > 1)
			{
				// Remove highest index first so earlier indices stay valid.
				for (int32 Index = Selected.Last(); Index >= 0; --Index)
				{
					if (Selected.Contains(Index))
					{
						RemoveGroupChild(SelectedGroupId, Index);
					}
				}
				return FReply::Handled();
			}
			return RemoveGroupChild(SelectedGroupId, SelectedGroupChildIndex);
		}
		if (WorkingLayers.IsValidIndex(SelectedLayerIndex))
		{
			return ExecuteLayerSelectionCommand(ELayerSelectionCommand::Remove);
		}
		return FReply::Unhandled();
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

	bInteractiveEdit = IsInteractiveEdit() || bInteractiveEdit;
	bPreviewSubmitPending = true;

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
	// is captured and settles on the first frame after it is let go: the final composite, and
	// the history entry the drag deferred.
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
		const bool bStillValid = IsSelectedOutputPreviewReady()
			&& ChildPreviewTarget == ResolveChildPreviewTarget(
				ChildPreviewTarget.OutputName, ChildPreviewTarget.Kind, ChildPreviewTarget.GapMaskName);
		if (!bStillValid)
		{
			DebugPreviewMode = EMixtormatDebugPreviewMode::None;
			ChildPreviewTarget = FMixtormatChildPreviewTarget();
			bPreviewSubmitPending = true;
		}
	}

	// Duplication, undo and other stack edits can change selection without the row-click path.
	// Reconcile at the shared submit boundary so every refresh uses the current selected owner.
	if (bMaskOverlayEnabled && UpdateAutomaticFlowPreview())
	{
		bPreviewSubmitPending = true;
	}

	// Validate ownership before selection-only composition overrides are chosen.
	const FMixtormatChildPreviewTarget OverlayTarget = bMaskOverlayEnabled
		? ResolveMaskOverlayTarget(MaskOverlaySource, MaskOverlayOutputName, MaskOverlayOutputKind)
				: FMixtormatChildPreviewTarget();
	const bool bMaskOverlayReady = bMaskOverlayEnabled && OverlayTarget.IsValid()
		&& OverlayTarget.OutputName == MaskOverlayOutputName && OverlayTarget.Kind == MaskOverlayOutputKind;
	if (bMaskOverlayEnabled && !bMaskOverlayReady)
	{
		bMaskOverlayEnabled = false;
		MaskOverlaySource = FMixtormatChildAddress();
		MaskOverlayOutputName = NAME_None;
		MaskOverlayOutputKind = EMixtormatPreviewOutputKind::Mask;
		bPreviewSubmitPending = true;
	}

	// Mouse held and nothing changed since the last submit: holding still must not recompose.
	if (!bPreviewSubmitPending)
	{
		if (bInteractiveEdit)
		{
			bPreviewRefreshPending = true;
			return EActiveTimerReturnType::Continue;
		}
		return EActiveTimerReturnType::Stop;
	}
	bPreviewSubmitPending = false;

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
	if (!bMaskOverlayEnabled && bBypassSelectedChild
		&& WorkingLayers.IsValidIndex(SelectedLayerIndex)
		&& WorkingLayers[SelectedLayerIndex].Children.IsValidIndex(BypassedChildIndex))
	{
		PreviewOverrideLayers = WorkingLayers;
		FMixtormatLayerChild& Child = PreviewOverrideLayers[SelectedLayerIndex].Children[BypassedChildIndex];
		// Preview-only: the authored child is untouched. The shared helper is the one place that
		// knows which payload carries the enable flag, so a new child type cannot fall through to
		// the Mask payload here either.
		MixtormatLayersPrivate::SetChildEnabled(Child, false);
		PreviewLayers = &PreviewOverrideLayers;
	}
	if (!bMaskOverlayEnabled && DebugPreviewMode == EMixtormatDebugPreviewMode::None
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
	else if (!bMaskOverlayEnabled && DebugPreviewMode == EMixtormatDebugPreviewMode::None
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
	// deferral above, which does no drawing at all, and the viewport spacing composites by their
	// measured cost while the mouse is held. The release frame submits with bInteractive false,
	// so the final value is always composed.
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			FMixtormatDebugPreviewSettings DebugSettings;
			DebugSettings.bMaskOverlay = bMaskOverlayReady;
			DebugSettings.Mode = bMaskOverlayReady
				? EMixtormatDebugPreviewMode::LayerMask : DebugPreviewMode;
			DebugSettings.LayerIndex = SelectedLayerIndex;
			DebugSettings.ChildIndex = DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
				? GetSelectedChildIndex()
				: INDEX_NONE;
			// LayerIndex/ChildIndex above are meaningless for ChildOutput: ChildTarget already
			// names the child by GUID, flattened to one concrete layer at click time (see
			// ResolveChildPreviewTarget), and the compositor resolves it to indices itself, once,
			// at the top of RequestComposeInternal.
			DebugSettings.ChildTarget = bMaskOverlayReady ? OverlayTarget : ChildPreviewTarget;
			Viewport->SetFinalSettings(WorkingFinalSettings);
			Viewport->SetPreviewLayers(
				*PreviewLayers, *PreviewGroups, CompositionResolution, DebugSettings,
				bInteractiveEdit);
		}
	}

	// Keep the timer alive for as long as the mouse is held, so the drag-end frame above is
	// reached even if no further value change arrives. It idles at the check above until then.
	if (bInteractiveEdit)
	{
		bPreviewRefreshPending = true;
		return EActiveTimerReturnType::Continue;
	}
	return EActiveTimerReturnType::Stop;
}

#undef LOCTEXT_NAMESPACE
