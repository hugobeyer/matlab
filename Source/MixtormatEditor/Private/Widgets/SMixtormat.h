#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"
#include "Style/MixtormatDesignTokens.h"
#include "Widgets/SMixtormatPreviewViewport.h"
#include "UI/Rows/SMixtormatRow.h"
#include "UI/Controls/SMixtormatSlider.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Materials/MaterialInstanceConstant.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class UMaterialInterface;
class IToolTip;
class SBox;
class SHorizontalBox;
class SMenuAnchor;
class STextBlock;
class SVerticalBox;
class SWrapBox;
class SWidgetSwitcher;
struct FAssetData;
struct FMixtormatBakeSettings;

class SMixtormat final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormat) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	bool CanCloseTab();

private:
	struct FEditHistoryState
	{
		TArray<FMixtormatLayer> Layers;
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
	void ZoomMaterialGallery(int32 Direction);
	FReply SelectSurface(FText DisplayName, FSoftObjectPath AssetPath);
	FReply HandleSurfaceDropped(FText DisplayName, FSoftObjectPath AssetPath);
	FReply SetCategoryFilter(FName Family);
	FReply SetPreviewMesh(EMixtormatPreviewMesh MeshType);
	FReply SetPreviewQuality(EMixtormatPreviewQuality Quality);
	FReply SetPreviewAntiAliasing(EMixtormatPreviewAntiAliasing AntiAliasing);
	void SetPreviewScreenPercentage(int32 Percentage);
	void SetPreviewFov(float FovDegrees);
	FReply ResetPreviewCameraAndLighting();
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void PreviewSelectedSurfaceWithDisplacement();
	FReply ToggleFeaturePreview(EMixtormatDebugPreviewMode Mode);
	TSharedRef<SWidget> MakeFeaturePreviewButton(
		EMixtormatDebugPreviewMode Mode,
		const FText& ToolTip);
	FReply SetStudioLighting(EMixtormatStudioLighting LightingPreset);
	FReply SetHdriLighting(FSoftObjectPath HdriPath);
	FReply StartNewMaterial();
	FReply NewWorkingMaterial();
	FReply OpenWorkingMaterial();
	FReply SaveWorkingMaterial();
	FReply SaveWorkingMaterialAs();
	FReply BakeWorkingMaterial();
	FReply ExecuteBake(
		const FMixtormatBakeSettings& Settings,
		bool bConfirmExistingOutputs);
	void ApplyBakedMaterialToSelectedActors(UMaterialInterface& Material);
	FReply SetCompositionResolution(int32 Resolution);
	FReply UndoMaterialEdit();
	FReply RedoMaterialEdit();
	FReply AddWorkingLayer(EMixtormatLayerType LayerType);
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
	FReply DuplicateLayerChild(int32 LayerIndex, int32 ChildIndex);
	FReply ToggleLayerExpanded(int32 LayerIndex);
	FReply AssignNormalTexture(int32 LayerIndex, FSoftObjectPath NormalPath);
	FReply AddEffectToLayer(int32 LayerIndex, FSoftObjectPath EffectPath);
	FReply AddErosionToLayer(int32 LayerIndex);
	FReply AddProceduralPeelingToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedProceduralPeel();
	const FMixtormatLayerEffect* GetSelectedProceduralPeel() const;
	FMixtormatLayerEffect* GetSelectedErosion();
	const FMixtormatLayerEffect* GetSelectedErosion() const;
	FReply AddGradeToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedGrade();
	const FMixtormatLayerEffect* GetSelectedGrade() const;

	FMixtormatLayerEffect* GetSelectedChipping();
	const FMixtormatLayerEffect* GetSelectedChipping() const;
	FReply AddChippingToLayer(int32 LayerIndex);

	FReply ToggleLayerEffect(int32 LayerIndex, int32 EffectIndex);
	FReply RemoveLayerEffect(int32 LayerIndex, int32 ChildIndex);
	FReply AddGeneratedMaskToLayer(int32 LayerIndex);
	FReply AddCraquelureToLayer(int32 LayerIndex);
	FReply RemoveGeneratedFromLayer(int32 LayerIndex, int32 ChildIndex);
	void SetGeneratedEnabled(ECheckBoxState CheckState, int32 LayerIndex, int32 ChildIndex);
	FMixtormatGeneratedMask* GetSelectedGeneratedMask();
	const FMixtormatGeneratedMask* GetSelectedGeneratedMask() const;

	FMixtormatCraquelure* GetSelectedCraquelure();
	const FMixtormatCraquelure* GetSelectedCraquelure() const;
	TSharedRef<SWidget> BuildCraquelureControls();
	TSharedRef<SWidget> BuildCraquelureBlendModeMenu();
	TSharedRef<SWidget> BuildCraquelureModeMenu();
	TSharedRef<SWidget> BuildCraquelureOutputModeMenu();
	TSharedRef<SWidget> BuildMaskRotationMenu();
	TSharedRef<SWidget> BuildLayerRotationMenu();

	FMixtormatLayerEffect* GetSelectedLayerEffect();
	const FMixtormatLayerEffect* GetSelectedLayerEffect() const;
	FMixtormatMaskLayer* GetSelectedLayerMask();
	const FMixtormatMaskLayer* GetSelectedLayerMask() const;
	int32 GetSelectedChildIndex() const;
	// The derived mark the inspector strip prints -- the selected child's, or the layer's.
	FText GetSelectedBadgeText() const;
	void SetMaskEnabled(ECheckBoxState CheckState, int32 LayerIndex, int32 ChildIndex);
	void SetMaskBlendMode(int32 LayerIndex, int32 ChildIndex, EMixtormatMaskBlendMode BlendMode);
	FReply OpenFillColorPicker(int32 LayerIndex);
	void SetFillBaseColor(FLinearColor NewColor, int32 LayerIndex);
	void RestoreFillBaseColor(FLinearColor OriginalColor, int32 LayerIndex);
	FReply OpenStainColorPicker(int32 LayerIndex, int32 ChildIndex);
	void SetStainColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex);
	void RestoreStainColor(FLinearColor OriginalColor, int32 LayerIndex, int32 ChildIndex);
	FReply OpenErosionColorPicker(int32 LayerIndex, int32 ChildIndex);
	void SetErosionColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex);
	void RestoreErosionColor(FLinearColor OriginalColor, int32 LayerIndex, int32 ChildIndex);

	FReply OpenChipColorPicker(int32 LayerIndex, int32 ChildIndex);
	void SetChipColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex);
	void RestoreChipColor(FLinearColor OriginalColor, int32 LayerIndex, int32 ChildIndex);
	void SetWorkingLayerEnabled(ECheckBoxState CheckState, int32 LayerIndex);
	FReply ToggleLayerSolo(int32 LayerIndex);
	bool IsLayerChildEnabled(int32 LayerIndex, int32 ChildIndex) const;
	void SyncSelectedLayerControls();
	void ResetEditHistory(bool bCurrentStateIsSaved);
	void RecordEditHistory();
	void ApplyEditHistoryState(const FEditHistoryState& State);
	void SynchronizeHistoryAfterCancelledEdit();
	bool IsCurrentStateSaved() const;
	static bool AreLayerStacksEqual(
		const TArray<FMixtormatLayer>& A,
		const TArray<FMixtormatLayer>& B);
	static bool HaveSameLayerStructure(
		const TArray<FMixtormatLayer>& A,
		const TArray<FMixtormatLayer>& B);
	void RefreshLayeredPreview(bool bMarkDirty = true);
	EActiveTimerReturnType FlushPendingPreviewRefresh(double CurrentTime, float DeltaTime);

	// True while a Slate control has the mouse captured with the left button down, which is
	// what a spin box scrub looks like from outside. Read globally rather than wired through
	// each control's OnBeginSliderMovement: there are dozens of spin boxes, and this survives
	// replacing them.
	bool IsInteractiveEdit() const;
	bool ConfirmDiscardUnsavedChanges();

	// One inspector row: label, fill and value in a single bar of fixed height. Every numeric
	// control in the inspector goes through here, which is what makes the rows uniform.
	// Registers itself for hover + Backspace reset like the spin boxes it replaces.
	TSharedRef<SWidget> MakeSlider(
		const FText& Label,
		const TAttribute<double>& Value,
		double MinValue,
		double MaxValue,
		double DefaultValue,
		double SnapDelta,
		bool bInteger,
		const FMixtormatOnSliderValueChanged& OnValueChanged,
		const FSimpleDelegate& ResetDelegate,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	void AddSliderRow(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const TSharedRef<SWidget>& Row);

	// One binding for every parameter row in the inspector, whatever owns it.
	//
	// A panel supplies a resolver for its own selection and a pointer to the member; the row, its
	// reset to default and its preview refresh all come from here. Peel, erosion, generated masks,
	// mask children and layers differ only in the resolver, so without this each grows its own
	// near-identical copy -- which is exactly what had started to happen.
	template <typename TOwner>
	TSharedRef<SWidget> MakeMemberSlider(
		const FText& Label,
		TFunction<TOwner*()> Resolve,
		float TOwner::* Member,
		const double MinValue,
		const double MaxValue,
		const double DefaultValue,
		const double SnapDelta,
		const TAttribute<FText>& ToolTip = TAttribute<FText>())
	{
		return MakeSlider(
			Label,
			TAttribute<double>::CreateLambda([Resolve, Member, DefaultValue]() -> double
			{
				const TOwner* Owner = Resolve();
				return Owner ? static_cast<double>(Owner->*Member) : DefaultValue;
			}),
			MinValue,
			MaxValue,
			DefaultValue,
			SnapDelta,
			false,
			FMixtormatOnSliderValueChanged::CreateLambda([this, Resolve, Member](const double Value)
			{
				if (TOwner* Owner = Resolve())
				{
					Owner->*Member = static_cast<float>(Value);
					RefreshLayeredPreview();
				}
			}),
			FSimpleDelegate::CreateLambda([this, Resolve, Member, DefaultValue]()
			{
				TOwner* Owner = Resolve();
				if (Owner && !FMath::IsNearlyEqual(Owner->*Member, static_cast<float>(DefaultValue)))
				{
					Owner->*Member = static_cast<float>(DefaultValue);
					RefreshLayeredPreview();
				}
			}),
			ToolTip);
	}

	template <typename TOwner>
	TSharedRef<SWidget> MakeMemberSliderInt(
		const FText& Label,
		TFunction<TOwner*()> Resolve,
		int32 TOwner::* Member,
		const double MinValue,
		const double MaxValue,
		const int32 DefaultValue,
		const TAttribute<FText>& ToolTip = TAttribute<FText>())
	{
		return MakeSlider(
			Label,
			TAttribute<double>::CreateLambda([Resolve, Member, DefaultValue]() -> double
			{
				const TOwner* Owner = Resolve();
				return static_cast<double>(Owner ? Owner->*Member : DefaultValue);
			}),
			MinValue,
			MaxValue,
			static_cast<double>(DefaultValue),
			1.0,
			true,
			FMixtormatOnSliderValueChanged::CreateLambda([this, Resolve, Member](const double Value)
			{
				if (TOwner* Owner = Resolve())
				{
					Owner->*Member = FMath::RoundToInt(Value);
					RefreshLayeredPreview();
				}
			}),
			FSimpleDelegate::CreateLambda([this, Resolve, Member, DefaultValue]()
			{
				TOwner* Owner = Resolve();
				if (Owner && Owner->*Member != DefaultValue)
				{
					Owner->*Member = DefaultValue;
					RefreshLayeredPreview();
				}
			}),
			ToolTip);
	}

	// The toggle equivalent, so a checkbox row is declared the same way a value row is instead of
	// being hand-assembled per panel.
	template <typename TOwner>
	TSharedRef<SWidget> MakeMemberToggle(
		const FText& Label,
		TFunction<TOwner*()> Resolve,
		bool TOwner::* Member,
		const TAttribute<FText>& ToolTip = TAttribute<FText>())
	{
		return MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Resolve, Member]()
				{
					const TOwner* Owner = Resolve();
					return Owner && Owner->*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Resolve, Member](const ECheckBoxState State)
				{
					if (TOwner* Owner = Resolve())
					{
						Owner->*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			ToolTip);
	}

	// Procedural peel and erosion rows all read and write one member of the selected effect, so
	// they collapse to a single call each.
	//
	// Each comes in two forms: Make* returns the row so it can be composed -- paired with another
	// row, wrapped, or placed under a caption -- and Add* appends it directly, which is what most
	// call sites want.
	TSharedRef<SWidget> MakePeelSlider(
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		double DefaultValue,
		double SnapDelta,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	TSharedRef<SWidget> MakePeelSliderInt(
		const FText& Label,
		int32 FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		int32 DefaultValue,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	TSharedRef<SWidget> MakeErosionSlider(
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		double DefaultValue,
		double SnapDelta,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	TSharedRef<SWidget> MakeErosionSliderInt(
		const FText& Label,
		int32 FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		int32 DefaultValue,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	void AddPeelSlider(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		double DefaultValue,
		double SnapDelta,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	void AddPeelSliderInt(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		int32 FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		int32 DefaultValue,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	void AddErosionSlider(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		float FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		double DefaultValue,
		double SnapDelta,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

	void AddErosionSliderInt(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Label,
		int32 FMixtormatLayerEffect::* Member,
		double MinValue,
		double MaxValue,
		int32 DefaultValue,
		const TAttribute<FText>& ToolTip = TAttribute<FText>());

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
	TSharedRef<SWidget> BuildLayerThumbnail(int32 LayerIndex);
	TSharedRef<SWidget> BuildLayerChildIcon(int32 LayerIndex, int32 ChildIndex);
	// The mask a child row is carrying, shown on hover -- the row only has room for a glyph.
	// Null for effects and generated masks, which have no picture to show.
	TSharedPtr<IToolTip> BuildMaskPreviewTooltip(int32 LayerIndex, int32 ChildIndex);
	FText GetLayerSourceText(int32 LayerIndex) const;
	FText GetLayerChildName(const FMixtormatLayerChild& Child) const;
	TSharedRef<SWidget> BuildLayerContextMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildAddLayerMenu();
	TSharedRef<SWidget> BuildAddMaskMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildAddEffectMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildEffectContextMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildMaskBar();
	TSharedRef<SWidget> BuildMaskBlendModeMenu(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildMaskContextMenu(int32 LayerIndex, int32 MaskIndex);
	// The mask picker grid, shared by adding and replacing -- the caller says what a pick means.
	TSharedRef<SWidget> BuildMaskGallery(TFunction<void(const FSoftObjectPath&)> OnChosen);
	TSharedRef<SWidget> BuildMaskReplacementGallery(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildMaskReplacementMenu(int32 LayerIndex, int32 MaskIndex);
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
	TSharedRef<SWidget> BuildChannelInfluenceControls();
	TSharedRef<SWidget> BuildColorAdjustmentControls();
	TSharedRef<SWidget> BuildSurfaceMaskInfluenceControls();
	TSharedRef<SWidget> BuildHeightBlendControls();
	TSharedRef<SWidget> BuildLayerMaskControls();
	TSharedRef<SWidget> BuildGeneratedMaskControls();
	TSharedRef<SWidget> BuildErosionControls();
	TSharedRef<SWidget> BuildProceduralPeelControls();
	TSharedRef<SWidget> BuildGeneratedContextMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildGeneratedBlendModeMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildErosionCurvatureModeMenu();
	TSharedRef<SWidget> BuildGradeControls();
	TSharedRef<SWidget> BuildChippingControls();
	TSharedRef<SWidget> BuildGradeTonemapMenu();


	TSharedPtr<SWidgetSwitcher> MainSwitcher;
	TSharedPtr<SWidgetSwitcher> LeftSwitcher;
	TSharedPtr<SVerticalBox> CategoryListBox;
	TSharedPtr<SWrapBox> SurfaceListBox;
	TSharedPtr<SVerticalBox> LayerListBox;
	TSharedPtr<SWrapBox> MaskListBox;
	TSharedPtr<STextBlock> SelectedSurfaceText;
	TSharedPtr<STextBlock> SelectedIdentityText;
	TSharedPtr<STextBlock> SelectedMapsText;
	// The strip mirrors a layer row, so it carries the row's thumbnail too. Swapped on selection
	// rather than bound, because a thumbnail is a widget from the pool and not a brush.
	TSharedPtr<SBox> SelectedThumbnailBox;
	TSharedPtr<STextBlock> WorkingBaseLayerText;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TArray<TSharedPtr<FAssetThumbnail>> SurfaceThumbnails;
	float MaterialGalleryTileSize = MixtormatTokens::MaterialGalleryTileDefault;
	TArray<TSharedPtr<FAssetThumbnail>> LayerThumbnails;
	// The inspector strip's own thumbnail. Kept apart from LayerThumbnails because the strip is
	// re-made on every selection while that array is only cleared when the whole stack rebuilds --
	// pooled thumbnails accumulating there would eat the pool's budget.
	TSharedPtr<FAssetThumbnail> SelectedStripThumbnail;
	TArray<TSharedPtr<FAssetThumbnail>> MaskThumbnails;
	TArray<TSharedPtr<FAssetThumbnail>> HdriThumbnails;
	TArray<TSharedPtr<SMixtormatPreviewViewport>> PreviewViewports;
	TSet<int32> ExpandedLayerIndices;
	TArray<FMixtormatLayer> WorkingLayers;
	TArray<FMixtormatLayer> SavedLayers;
	TArray<FEditHistoryState> UndoHistory;
	TArray<FEditHistoryState> RedoHistory;
	TArray<FNumericResetBinding> NumericResetBindings;
	FEditHistoryState CurrentHistoryState;
	FSoftObjectPath SelectedSurfacePath;
	FText SelectedLibrarySurfaceName;
	TStrongObjectPtr<UMaterialInstanceConstant> SelectedPreviewMaterial;
	TStrongObjectPtr<UMixtormatMaterial> WorkingMaterialAsset;
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

	// Drag state. While scrubbing, the preview composites at a reduced resolution and the
	// undo history is not written; both are settled once on the frame the drag ends.
	bool bInteractiveEdit = false;
	bool bInteractiveHistoryPending = false;
	bool bShowCompositionBefore = false;
	bool bBypassSelectedChild = false;
	bool bPreviewDisplacementEnabled = false;
	bool bIsBaking = false;
	EMixtormatDebugPreviewMode DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	int32 SelectedLayerIndex = INDEX_NONE;
	int32 SoloLayerIndex = INDEX_NONE;
	int32 SelectedEffectIndex = INDEX_NONE;
	int32 SelectedMaskIndex = INDEX_NONE;
	int32 LeftTabIndex = 0;
	int32 CompositionResolution = 2048;
	EMixtormatStudioLighting StudioLighting = EMixtormatStudioLighting::Neutral;
	EMixtormatPreviewMesh PreviewMesh = EMixtormatPreviewMesh::Sphere;
	EMixtormatPreviewQuality PreviewQuality = EMixtormatPreviewQuality::Medium;
	EMixtormatPreviewAntiAliasing PreviewAntiAliasing = EMixtormatPreviewAntiAliasing::Temporal;
	int32 PreviewScreenPercentage = MixtormatPreviewScreenPercentage::Default;
	float PreviewFov = MixtormatPreviewCamera::FovDefault;
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
