#pragma once

#include "CoreMinimal.h"
#include "MaterialLabMaterial.h"
#include "Widgets/SMaterialLabPreviewViewport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInstanceConstant.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class UMaterialInterface;
class SBox;
class SHorizontalBox;
class SMenuAnchor;
class STextBlock;
class SVerticalBox;
class SWrapBox;
class SWidgetSwitcher;
struct FAssetData;
struct FMaterialLabBakeSettings;

class SMaterialLab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMaterialLab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	bool CanCloseTab();

private:
	struct FEditHistoryState
	{
		TArray<FMaterialLabLayer> Layers;
	};

	struct FNumericResetBinding
	{
		TWeakPtr<SWidget> Widget;
		FSimpleDelegate Reset;
	};
	FReply ShowPage(int32 PageIndex);
	FReply ShowLeftPage(int32 PageIndex);
	FReply ImportSurfaces();
	FReply ReimportShippedLibrary();
	FReply RefreshSurfaceList();
	FReply SelectSurface(FText DisplayName, FSoftObjectPath AssetPath);
	FReply HandleSurfaceDropped(FText DisplayName, FSoftObjectPath AssetPath);
	FReply SetCategoryFilter(FName Family);
	FReply SetPreviewMesh(EMaterialLabPreviewMesh MeshType);
	FReply SetPreviewQuality(EMaterialLabPreviewQuality Quality);
	void SetPreviewFov(float FovDegrees);
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void PreviewSelectedSurfaceWithDisplacement();
	FReply SetStudioLighting(EMaterialLabStudioLighting LightingPreset);
	FReply SetHdriLighting(FSoftObjectPath HdriPath);
	FReply StartNewMaterial();
	FReply NewWorkingMaterial();
	FReply OpenWorkingMaterial();
	FReply SaveWorkingMaterial();
	FReply SaveWorkingMaterialAs();
	FReply BakeWorkingMaterial();
	FReply ExecuteBake(
		const FMaterialLabBakeSettings& Settings,
		bool bConfirmExistingOutputs);
	void ApplyBakedMaterialToSelectedActors(UMaterialInterface& Material);
	FReply SetCompositionResolution(int32 Resolution);
	FReply UndoMaterialEdit();
	FReply RedoMaterialEdit();
	FReply AddWorkingLayer(EMaterialLabLayerType LayerType);
	FReply DuplicateSelectedLayer();
	FReply DeleteSelectedLayer();
	FReply MoveSelectedLayer(int32 Direction);
	FReply HandleLayerDropped(int32 SourceLayerIndex, int32 TargetLayerIndex);
	FReply SelectWorkingLayer(int32 LayerIndex);
	FReply SelectWorkingChild(int32 LayerIndex, int32 ChildIndex);
	FReply AssignMaskToLayer(int32 LayerIndex, FSoftObjectPath MaskPath);
	FReply ReplaceMaskInLayer(int32 LayerIndex, int32 MaskIndex, FSoftObjectPath MaskPath);
	FReply ClearLayerMask(int32 LayerIndex);
	FReply RemoveMaskFromLayer(int32 LayerIndex, int32 ChildIndex);
	FReply ReorderLayerChild(int32 LayerIndex, int32 SourceChildIndex, int32 TargetChildIndex);
	FReply ToggleLayerExpanded(int32 LayerIndex);
	FReply SetLayerNormalDetail(int32 LayerIndex, bool bNormalDetail);
	FReply AssignNormalTexture(int32 LayerIndex, FSoftObjectPath NormalPath);
	FReply AddEffectToLayer(int32 LayerIndex, FSoftObjectPath EffectPath);
	FReply ToggleLayerEffect(int32 LayerIndex, int32 EffectIndex);
	FReply RemoveLayerEffect(int32 LayerIndex, int32 ChildIndex);
	FMaterialLabLayerEffect* GetSelectedLayerEffect();
	const FMaterialLabLayerEffect* GetSelectedLayerEffect() const;
	FMaterialLabMaskLayer* GetSelectedLayerMask();
	const FMaterialLabMaskLayer* GetSelectedLayerMask() const;
	int32 GetSelectedChildIndex() const;
	void SetMaskEnabled(ECheckBoxState CheckState, int32 LayerIndex, int32 ChildIndex);
	void SetMaskBlendMode(int32 LayerIndex, int32 ChildIndex, EMaterialLabMaskBlendMode BlendMode);
	FReply OpenFillColorPicker(int32 LayerIndex);
	void SetFillBaseColor(FLinearColor NewColor, int32 LayerIndex);
	void RestoreFillBaseColor(FLinearColor OriginalColor, int32 LayerIndex);
	FReply OpenStainColorPicker(int32 LayerIndex, int32 ChildIndex);
	void SetStainColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex);
	void RestoreStainColor(FLinearColor OriginalColor, int32 LayerIndex, int32 ChildIndex);
	FReply HandleLayerMouseButtonDown(
		const FGeometry& Geometry,
		const FPointerEvent& PointerEvent,
		int32 LayerIndex);
	void SetWorkingLayerEnabled(ECheckBoxState CheckState, int32 LayerIndex);
	void SyncSelectedLayerControls();
	void ResetEditHistory(bool bCurrentStateIsSaved);
	void RecordEditHistory();
	void ApplyEditHistoryState(const FEditHistoryState& State);
	void SynchronizeHistoryAfterCancelledEdit();
	bool IsCurrentStateSaved() const;
	static bool AreLayerStacksEqual(
		const TArray<FMaterialLabLayer>& A,
		const TArray<FMaterialLabLayer>& B);
	static bool HaveSameLayerStructure(
		const TArray<FMaterialLabLayer>& A,
		const TArray<FMaterialLabLayer>& B);
	void RefreshLayeredPreview(bool bMarkDirty = true);
	EActiveTimerReturnType FlushPendingPreviewRefresh(double CurrentTime, float DeltaTime);
	bool ConfirmDiscardUnsavedChanges();
	TSharedRef<SWidget> MakeResettableNumeric(
		const TSharedRef<SWidget>& NumericWidget,
		const FSimpleDelegate& ResetDelegate);
	bool ResetHoveredNumericControl();
	void PreviewSurfaceScalarParameter(FName ParameterName, float Value);
	void HandleSearchChanged(const FText& SearchTextValue);
	void RebuildCategoryList();
	void RebuildSurfaceList();
	void RebuildLayerList();
	void RebuildMaskList();
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildAuthoringPage();
	TSharedRef<SWidget> BuildLeftPanel();
	TSharedRef<SWidget> BuildBottomLibrary();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> BuildWorkflowMenu();
	TSharedRef<SWidget> BuildNavButton(const FText& Label, int32 PageIndex);
	TSharedRef<SWidget> BuildLibraryPage();
	TSharedRef<SWidget> BuildWorkspacePage(const FText& Heading, const FText& Description);
	TSharedRef<SWidget> BuildPresetsPage();
	TSharedRef<SWidget> BuildSurfaceList();
	TSharedRef<SWidget> BuildLayerStackPanel();
	TSharedRef<SWidget> BuildLayerRow(int32 LayerIndex);
	TSharedRef<SWidget> BuildLayerContextMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildAddLayerMenu();
	TSharedRef<SWidget> BuildAddChildMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildEffectContextMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildMaskBar();
	TSharedRef<SWidget> BuildMaskBlendModeMenu(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildMaskContextMenu(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildMaskReplacementGallery(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildNormalSourceMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildMaskCard(
		int32 LayerIndex,
		const FText& Name,
		const FSoftObjectPath& AssetPath,
		const FAssetData& ThumbnailAsset,
		bool bCompact);
	TSharedRef<SWidget> BuildSurfaceCard(
		const FText& Name,
		const FSoftObjectPath& AssetPath,
		const FAssetData& ThumbnailAsset);
	TSharedRef<SWidget> BuildPreviewPanel();
	TSharedRef<SWidget> BuildStudioLightingMenu();
	TSharedRef<SWidget> BuildCompositionResolutionMenu();
	TSharedRef<SWidget> BuildInspectorPanel();
	TSharedRef<SWidget> BuildEffectInspectorControls();
	TSharedRef<SWidget> BuildColorAdjustmentControls();
	TSharedRef<SWidget> BuildSurfaceMaskInfluenceControls();
	TSharedRef<SWidget> BuildHeightBlendControls();
	TSharedRef<SWidget> BuildLayerMaskControls();

	TSharedPtr<SWidgetSwitcher> MainSwitcher;
	TSharedPtr<SWidgetSwitcher> LeftSwitcher;
	TSharedPtr<SVerticalBox> CategoryListBox;
	TSharedPtr<SWrapBox> SurfaceListBox;
	TSharedPtr<SVerticalBox> LayerListBox;
	TSharedPtr<SWrapBox> MaskListBox;
	TSharedPtr<STextBlock> SelectedSurfaceText;
	TSharedPtr<STextBlock> SelectedIdentityText;
	TSharedPtr<STextBlock> SelectedMapsText;
	TSharedPtr<STextBlock> WorkingBaseLayerText;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TArray<TSharedPtr<FAssetThumbnail>> SurfaceThumbnails;
	TArray<TSharedPtr<FAssetThumbnail>> LayerThumbnails;
	TArray<TSharedPtr<FAssetThumbnail>> MaskThumbnails;
	TArray<TSharedPtr<FAssetThumbnail>> HdriThumbnails;
	TArray<TSharedPtr<SMenuAnchor>> LayerContextAnchors;
	TArray<TSharedPtr<SMaterialLabPreviewViewport>> PreviewViewports;
	TSet<int32> ExpandedLayerIndices;
	TArray<FMaterialLabLayer> WorkingLayers;
	TArray<FMaterialLabLayer> SavedLayers;
	TArray<FEditHistoryState> UndoHistory;
	TArray<FEditHistoryState> RedoHistory;
	TArray<FNumericResetBinding> NumericResetBindings;
	FEditHistoryState CurrentHistoryState;
	FSoftObjectPath SelectedSurfacePath;
	FText SelectedLibrarySurfaceName;
	TStrongObjectPtr<UMaterialInstanceConstant> SelectedPreviewMaterial;
	TStrongObjectPtr<UMaterialLabMaterial> WorkingMaterialAsset;
	float CurrentTiling = 2.0f;
	float CurrentRoughnessBias = 0.5f;
	float CurrentRoughnessContrast = 1.0f;
	float CurrentRoughnessOffset = 0.0f;
	float CurrentNormalIntensity = 1.0f;
	bool bHasWorkingMaterial = false;
	bool bHasSelectedLayer = false;
	bool bIsWorkingMaterialDirty = false;
	bool bHistoryInitialized = false;
	bool bApplyingHistory = false;
	bool bPreviewRefreshPending = false;
	bool bShowCompositionBefore = false;
	bool bBypassSelectedChild = false;
	bool bPreviewDisplacementEnabled = false;
	bool bIsBaking = false;
	int32 SelectedLayerIndex = INDEX_NONE;
	int32 SoloLayerIndex = INDEX_NONE;
	int32 SelectedEffectIndex = INDEX_NONE;
	int32 SelectedMaskIndex = INDEX_NONE;
	int32 LeftTabIndex = 0;
	int32 CompositionResolution = 2048;
	EMaterialLabStudioLighting StudioLighting = EMaterialLabStudioLighting::Neutral;
	EMaterialLabPreviewMesh PreviewMesh = EMaterialLabPreviewMesh::Sphere;
	EMaterialLabPreviewQuality PreviewQuality = EMaterialLabPreviewQuality::Medium;
	float PreviewFov = 50.0f;
	float PreviewDisplacementAmount = 1.0f;
	FSoftObjectPath SelectedHdriPath;
	FSoftObjectPath BakeSettingsRecipePath;
	FString BakeDestinationPath;
	FString BakeOutputBaseName;
	FString WorkingMaterialName = TEXT("No material");
	FString WorkingStatusText = TEXT("Ready");
	FString SearchText;
	FName CategoryFilter;
	double LastHistoryRecordTime = 0.0;
};
