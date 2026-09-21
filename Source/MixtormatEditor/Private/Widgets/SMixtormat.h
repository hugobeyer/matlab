// Copyright 2026 Hugo Beyer. All Rights Reserved.

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

// Declared, not included: only a reference to the builder crosses this header.
namespace MixtormatMenu { class FBuilder; }

class FAssetThumbnail;
class FAssetThumbnailPool;
class UMaterialInterface;
class IToolTip;
class SBox;
class SButton;
class SHorizontalBox;
class SMenuAnchor;
class STextBlock;
class SVerticalBox;
class SWrapBox;
class SWidgetSwitcher;
class SWindow;
class UScriptStruct;
struct FAssetData;
struct FMixtormatBakeSettings;
struct FMixtormatSurfaceEntry;

class SMixtormat final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormat) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMixtormat() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	bool CanCloseTab();

private:
	struct FEditHistoryState
	{
		TArray<FMixtormatLayer> Layers;
		// Groups are part of the same edit as the layers they hold -- membership lives on the
		// layer, so restoring one without the other leaves a layer in a group that is not there.
		TArray<FMixtormatLayerGroup> Groups;
		bool bRotateUV90 = false;
	};

	struct FNumericResetBinding
	{
		TWeakPtr<SWidget> Widget;
		FSimpleDelegate Reset;
	};
	void BuildWorkspaceUI();
	void HandleReferencedCompositionUpdated(const FAssetData& AssetData);
	FReply OpenLiveThemePanel();
	FReply OpenDocumentation();
	FReply OpenSettings();
	void RequestThemeRefresh();
	EActiveTimerReturnType ApplyPendingTheme(double CurrentTime, float DeltaTime);
	FReply ShowLeftPage(int32 PageIndex);
	FReply ImportSurfaces();
	FReply ImportMasks();
	FReply RefreshSurfaceList();
	FReply RebuildBuiltInLibrary();
	void ZoomMaterialGallery(int32 Direction);
	void ZoomMaskGallery(int32 Direction);
	FReply SelectSurface(FText DisplayName, FSoftObjectPath AssetPath);
	FReply SelectMask(FText DisplayName, FSoftObjectPath AssetPath);
	FReply HandleSurfaceDropped(FText DisplayName, FSoftObjectPath AssetPath);
	// The positional form. GroupId valid means the drop named a group, so the new layer joins it.
	FReply HandleSurfaceDroppedAt(
		FText DisplayName,
		FSoftObjectPath AssetPath,
		int32 InsertIndex,
		FGuid GroupId);
	FReply SetCategoryFilter(FName Family);
	FReply SetPreviewMesh(EMixtormatPreviewMesh MeshType);
	void SetGlobalUVRotation90(bool bEnabled);
	FReply SetPreviewQuality(EMixtormatPreviewQuality Quality);
	FReply SetPreviewAntiAliasing(EMixtormatPreviewAntiAliasing AntiAliasing);
	void SetPreviewScreenPercentage(int32 Percentage);
	void SetPreviewFov(float FovDegrees);
	FReply ResetPreviewCameraAndLighting();
	void SetPreviewDisplacementEnabled(bool bEnabled);
	void SetPreviewDisplacementAmount(float Amount);
	void SetPreviewLightIntensity(float Scale);
	void SetPreviewSkylightIntensity(float Scale);
	void PreviewSelectedSurfaceWithDisplacement();
	FReply ToggleFeaturePreview(EMixtormatDebugPreviewMode Mode);
	TSharedRef<SWidget> MakeFeaturePreviewButton(
		EMixtormatDebugPreviewMode Mode,
		const FText& ToolTip);
	FReply SetStudioLighting(EMixtormatStudioLighting LightingPreset);
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
	FReply AddBlurToMask(int32 LayerIndex, int32 OwnerChildIndex);
	FReply AddMaskFilterToLayerChild(int32 LayerIndex, int32 OwnerChildIndex, EMixtormatLayerChildType ChildType);
	// The same two, on a group's shared stack. A group mask carries the scoped children a layer's
	// mask does; the runtime rebinds them per member when it flattens the group.
	FReply AddMaskFilterToGroupChild(FGuid GroupId, int32 OwnerChildIndex, EMixtormatLayerChildType ChildType);
	FReply AddFlowWarpToGroupChild(FGuid GroupId, int32 OwnerChildIndex);
	FReply AddCurvatureToMask(int32 LayerIndex, int32 OwnerChildIndex);
	FMixtormatMaskCurvature* GetSelectedLayerCurvature();
	const FMixtormatMaskCurvature* GetSelectedLayerCurvature() const;
	TSharedRef<SWidget> BuildMaskCurvatureControls();
	TSharedRef<SWidget> BuildCurvatureSourceMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildCurvatureModeMenu(int32 LayerIndex, int32 ChildIndex);
	FReply AssignScopedMaskToChild(int32 LayerIndex, int32 OwnerChildIndex, FSoftObjectPath MaskPath);
	FReply ReplaceMaskInLayer(int32 LayerIndex, int32 MaskIndex, FSoftObjectPath MaskPath);
	FReply ReplaceSurfaceInLayer(int32 LayerIndex, FSoftObjectPath SurfacePath);
	FReply ClearLayerMask(int32 LayerIndex);
	FReply RemoveMaskFromLayer(int32 LayerIndex, int32 ChildIndex);
	FReply ReorderLayerChild(int32 LayerIndex, int32 SourceChildIndex, int32 TargetChildIndex);
	FReply DuplicateLayerChild(int32 LayerIndex, int32 ChildIndex);
	FReply MoveChildToLayer(int32 SourceLayerIndex, int32 ChildIndex, int32 DestLayerIndex, int32 DestChildIndex = INDEX_NONE);
	// Same move, but the destination is a group's shared stack rather than another layer: the
	// child leaves its layer and becomes something every member composites, instead of landing in
	// one member in particular.
	FReply MoveChildToGroup(int32 SourceLayerIndex, int32 ChildIndex, FGuid GroupId);
	// The reverse of MoveChildToGroup: the shared child leaves the group and becomes this one
	// layer's own, so every other member loses it.
	FReply MoveGroupChildToLayer(FGuid GroupId, int32 ChildIndex, int32 DestLayerIndex, int32 DestChildIndex = INDEX_NONE);

	// Mirrors every instance's payload down from its source, so the inspector, the badges and the
	// row names all read the resolved values without a second read path. Run on every refresh; the
	// compositor resolves again on its own copy regardless.
	void SyncChildInstances();
	bool IsSelectedChildInstance() const;
	bool IsSelectedInstanceBroken() const;
	FText GetSelectedInstanceSourceText() const;
	TSharedRef<SWidget> BuildInstanceBanner();

	// True when the parameter belongs to a child that is a live instance. Inherited data is the
	// source's to change, so the row refuses the write rather than breaking the instance under a
	// user who only meant to drag a slider.
	bool IsParameterLocked(const FMixtormatParameterAddress& Target) const;

	// Copy takes the payload; Copy as Instance takes the address as well, and the paste decides
	// which of the two it uses.
	void CopyLayerChild(int32 LayerIndex, int32 ChildIndex, bool bAsInstance);
	void CopyInstanceMaskFromWear(int32 LayerIndex, int32 ChildIndex);
	void CopyInstanceMaskFromBreakup(int32 LayerIndex, int32 ChildIndex, FName Output);
	void CopyInstanceMaskFromPatternGap(int32 LayerIndex, int32 ChildIndex);
	bool CanPasteLayerChild() const;
	// Where an instance of the clipboard child may land in this layer, given the row the paste was
	// asked from. INDEX_NONE when no position in the layer can read the source.
	int32 ResolveInstanceInsertIndex(int32 DestLayerIndex, int32 AnchorChildIndex) const;
	bool CanPasteChildInstance(int32 DestLayerIndex, int32 AnchorChildIndex) const;
	FText GetChildInstancePasteReason(int32 DestLayerIndex, int32 AnchorChildIndex) const;
	FReply PasteLayerChild(int32 LayerIndex);
	FReply PasteChildInstance(int32 LayerIndex, int32 AnchorChildIndex = INDEX_NONE);

	FReply GoToChildInstanceSource(int32 LayerIndex, int32 ChildIndex);
	FReply BreakChildInstanceAt(int32 LayerIndex, int32 ChildIndex);
	void CopyChildInstanceReference(int32 LayerIndex, int32 ChildIndex);
	FReply ReplaceChildInstanceSource(int32 LayerIndex, int32 ChildIndex, FGuid NewSourceLayerId, FGuid NewSourceChildId);
	TSharedRef<SWidget> BuildReplaceInstanceSourceMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildMoveChildToLayerMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildMoveGroupChildToLayerMenu(FGuid GroupId, int32 ChildIndex);

	// The rows every child row shares, appended to whichever of the three child menus is open so
	// the vocabulary does not drift between a mask, an effect and a filter.
	void AddSharedChildMenuItems(MixtormatMenu::FBuilder& Menu, int32 LayerIndex, int32 ChildIndex);
	FReply ToggleLayerExpanded(int32 LayerIndex);
	FReply AssignNormalTexture(int32 LayerIndex, FSoftObjectPath NormalPath);
	FReply AddEffectToLayer(int32 LayerIndex, FSoftObjectPath EffectPath);
	FReply AddStainToLayer(int32 LayerIndex, EMixtormatStainMode Mode);
	FMixtormatLayerEffect* GetSelectedStain();
	const FMixtormatLayerEffect* GetSelectedStain() const;
	FReply AddRunoffToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedRunoff();
	const FMixtormatLayerEffect* GetSelectedRunoff() const;
	FReply AddErosionToLayer(int32 LayerIndex);
	FReply AddProceduralPeelingToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedProceduralPeel();
	const FMixtormatLayerEffect* GetSelectedProceduralPeel() const;
	FMixtormatLayerEffect* GetSelectedErosion();
	const FMixtormatLayerEffect* GetSelectedErosion() const;
	FReply AddGradeToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedGrade();
	const FMixtormatLayerEffect* GetSelectedGrade() const;

	FMixtormatLayerEffect* GetSelectedBreakup();
	const FMixtormatLayerEffect* GetSelectedBreakup() const;
	FReply AddBreakupToLayer(int32 LayerIndex);

	FMixtormatLayerEffect* GetSelectedWornEdges();
	const FMixtormatLayerEffect* GetSelectedWornEdges() const;
	FReply AddWornEdgesToLayer(int32 LayerIndex);
	TSharedRef<SWidget> BuildWornEdgesControls();

	FMixtormatLayerEffect* GetSelectedFlowWarp();
	const FMixtormatLayerEffect* GetSelectedFlowWarp() const;
	FReply AddFlowWarpToLayer(int32 LayerIndex, int32 OwnerChildIndex = INDEX_NONE);
	FReply AddLayerBlurToLayer(int32 LayerIndex);
	FMixtormatLayerEffect* GetSelectedLayerBlurEffect();
	const FMixtormatLayerEffect* GetSelectedLayerBlurEffect() const;
	TSharedRef<SWidget> BuildLayerBlurControls();
	TSharedRef<SWidget> BuildLayerBlurScopeMenu();
	TSharedRef<SWidget> BuildFlowWarpControls();
	TSharedRef<SWidget> BuildFlowWarpBlendModeMenu();

	FReply ToggleLayerEffect(int32 LayerIndex, int32 EffectIndex);
	FReply RemoveLayerEffect(int32 LayerIndex, int32 ChildIndex);
	FReply AddLayerValuesMaskToLayer(int32 LayerIndex);
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
	TSharedRef<SWidget> BuildRampIdBlendModeMenu();
	TSharedRef<SWidget> BuildCraquelureModeMenu();

	FReply AddFilterToLayer(int32 LayerIndex);
	FMixtormatClusterFilter* GetSelectedFilter();
	const FMixtormatClusterFilter* GetSelectedFilter() const;
	TSharedRef<SWidget> BuildFilterControls();
	TSharedRef<SWidget> BuildClusterSourceMenu();
	TSharedRef<SWidget> BuildAddFilterMenu(int32 LayerIndex);
	bool CanPreviewSelectedFilter() const;

	FReply AddHsvFilterToLayer(int32 LayerIndex);
	FMixtormatHsvIdFilter* GetSelectedHsvFilter();
	const FMixtormatHsvIdFilter* GetSelectedHsvFilter() const;
	TSharedRef<SWidget> BuildHsvFilterControls();
	FReply AddHsvPaletteEntry();
	FReply RemoveHsvPaletteEntry(int32 ColorIndex);
	FReply OpenHsvPalettePicker(int32 ColorIndex);
	void SetHsvPaletteColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex, int32 ColorIndex);

	FReply AddPatternIdToLayer(int32 LayerIndex);
	FMixtormatPatternFilter* GetSelectedPatternId();
	const FMixtormatPatternFilter* GetSelectedPatternId() const;
	TSharedRef<SWidget> BuildPatternIdControls();
	TSharedRef<SWidget> BuildPatternModeMenu();
	TSharedRef<SWidget> BuildGridModeMenu();

	// GENERATORS. One creator, one getter pair and one panel per generator; HasSelectedGenerator
	// and GetSelectedGenerator are the category-wide pair the inspector's visibility lists and
	// the header chip use, so adding a generator does not mean extending them.
	FReply AddStrataCarverToLayer(int32 LayerIndex);
	FMixtormatStrataCarver* GetSelectedStrataCarver();
	const FMixtormatStrataCarver* GetSelectedStrataCarver() const;
	TSharedRef<SWidget> BuildStrataCarverControls();
	TSharedRef<SWidget> BuildAddGeneratorMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildGroupAddGeneratorMenu(FGuid GroupId);
	FReply AddGeneratorToGroup(FGuid GroupId, EMixtormatGeneratorType GeneratorType);
	bool HasSelectedGenerator() const;
	FMixtormatGenerator* GetSelectedGenerator();

	FReply AddRampIdToLayer(int32 LayerIndex);
	FReply AddCombineIdToLayer(int32 LayerIndex);
	FMixtormatCombineIdFilter* GetSelectedCombineId();
	const FMixtormatCombineIdFilter* GetSelectedCombineId() const;
	FMixtormatRampIdFilter* GetSelectedRampId();
	const FMixtormatRampIdFilter* GetSelectedRampId() const;
	TSharedRef<SWidget> BuildRampIdControls();
	TSharedRef<SWidget> BuildCombineIdControls();
	TSharedRef<SWidget> BuildCombineIdModeMenu();
	TSharedRef<SWidget> BuildCombineIdModeMenuFor(int32 LayerIndex, int32 ChildIndex);

	FReply AddRandomIdToLayer(int32 LayerIndex);
	FMixtormatRandomIdMask* GetSelectedRandomId();
	const FMixtormatRandomIdMask* GetSelectedRandomId() const;
	TSharedRef<SWidget> BuildRandomIdControls();
	TSharedRef<SWidget> BuildRandomIdBlendModeMenu();

	FReply AddColorIdMaskToLayer(int32 LayerIndex);
	FMixtormatColorIdMask* GetSelectedColorId();
	const FMixtormatColorIdMask* GetSelectedColorId() const;
	TSharedRef<SWidget> BuildColorIdControls();
	TSharedRef<SWidget> BuildBaseColorBlendModeMenu();
	TSharedRef<SWidget> BuildColorIdBlendModeMenu();
	TSharedRef<SWidget> BuildColorIdRotationMenu();
	FReply AddColorIdEntry();
	FReply RemoveColorIdEntry(int32 ColorIndex);
	FReply OpenColorIdPicker(int32 ColorIndex);
	void SetColorIdColor(FLinearColor NewColor, int32 LayerIndex, int32 ChildIndex, int32 ColorIndex);
	TSharedRef<SWidget> BuildMaskSourceMenu();
	TSharedRef<SWidget> BuildMaskLayerValueChannelMenu();
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

	void SetWorkingLayerEnabled(ECheckBoxState CheckState, int32 LayerIndex);
	FReply ToggleLayerSolo(int32 LayerIndex);
	bool IsLayerChildEnabled(int32 LayerIndex, int32 ChildIndex) const;
	void SyncSelectedLayerControls();
	void ResetEditHistory(bool bCurrentStateIsSaved);
	void RecordEditHistory();
	void ApplyEditHistoryState(const FEditHistoryState& State);
	void SynchronizeHistoryAfterCancelledEdit();
	bool IsCurrentStateSaved() const;
	// Group operations. Creating gathers the selected layers into one contiguous block, because a
	// group owning a scattered set of layers would have no single position in the stack.
	// Shared group children. Authored once on the group and broadcast onto every member by
	// MixtormatLayerGroups::BuildEffectiveLayers, which is the whole point of a group.
	// The one place the inspector learns what container a child lives in.
	//
	// A child is addressed by (LayerIndex, ChildIndex) everywhere, and a group has no layer index,
	// so INDEX_NONE plus a valid SelectedGroupId means the selected group's shared stack. Neither
	// variable lies -- when a group child is selected there genuinely is no layer selected -- and
	// no setter signature has to change, which is what keeps this from being a rewrite of every
	// slider in the panel.
	FMixtormatLayerChild* ResolveChild(int32 LayerIndex, int32 ChildIndex);
	const FMixtormatLayerChild* ResolveChild(int32 LayerIndex, int32 ChildIndex) const;

	FMixtormatLayerChild* AppendGroupChild(FGuid GroupId, EMixtormatLayerChildType ChildType);
	void FinishGroupChildEdit(FGuid GroupId, int32 SelectIndex = INDEX_NONE);
	FReply AddMaskToGroup(FGuid GroupId, FSoftObjectPath MaskPath);
	FReply AddEffectToGroup(FGuid GroupId, FSoftObjectPath EffectPath);
	FReply AddProceduralChildToGroup(FGuid GroupId, EMixtormatLayerChildType ChildType);
	FReply RemoveGroupChild(FGuid GroupId, int32 ChildIndex);
	FReply ReorderGroupChild(FGuid GroupId, int32 SourceChildIndex, int32 TargetChildIndex);
	FReply ToggleGroupChildEnabled(FGuid GroupId, int32 ChildIndex);
	FReply SelectGroupChild(FGuid GroupId, int32 ChildIndex);
	static bool IsGroupChildEnabled(const FMixtormatLayerChild& Child);
	TSharedRef<SWidget> BuildGroupChildRow(FGuid GroupId, int32 ChildIndex);
	TSharedRef<SWidget> BuildGroupAddEffectMenu(FGuid GroupId);
	TSharedRef<SWidget> BuildGroupAddFilterMenu(FGuid GroupId);
	TSharedRef<SWidget> BuildGroupChildContextMenu(FGuid GroupId, int32 ChildIndex);

	// Rename. Names do not reach the compositor, so committing one records history and marks the
	// document dirty but deliberately does not ask for a new composite.
	FReply RenameLayer(FGuid LayerId, FText NewName);
	FReply RenameLayerGroup(FGuid GroupId, FText NewName);
	// F2 (or its F12 alias), and the context menu, on whichever of the two selections is live.
	bool BeginRenameSelection();

	TSharedRef<SWidget> BuildLayerGroupRow(FGuid GroupId);
	TSharedRef<SWidget> BuildLayerGroupContextMenu(FGuid GroupId);
	// Editor-only tagging: the compositor never reads a group's colour, so this asks for no
	// recomposite -- only a rebuild of the rows that draw it.
	FReply SetLayerGroupAccentColor(FGuid GroupId, FLinearColor AccentColor);
	TSharedRef<SWidget> BuildGroupAccentMenu(FGuid GroupId);
	static int32 InsertIndexToMoveTarget(int32 SourceIndex, int32 InsertIndex);
	FReply HandleLayerInsertedAt(int32 SourceLayerIndex, int32 InsertIndex);
	FReply HandleGroupInsertedAt(FGuid GroupId, int32 InsertIndex);
	FReply HandleLayerDroppedOnGroup(int32 SourceLayerIndex, FGuid TargetGroupId);
	FGuid ResolveGroupMembershipAt(int32 LayerIndex) const;
	FReply CreateGroupFromSelection();
	FReply UngroupLayerGroup(FGuid GroupId);
	bool CanCreateGroupFromSelection() const;
	TArray<int32> GetSelectedLayerIndices() const;
	FText MakeUniqueGroupName() const;
	bool IsGroupExpanded(const FGuid& GroupId) const;
	FReply ToggleGroupExpanded(FGuid GroupId);
	FReply SelectLayerGroup(FGuid GroupId);
	FReply SetLayerGroupEnabled(FGuid GroupId, bool bEnabled);

	// Expansion and multi-select, addressed by index at the call site and stored by identity.
	bool IsLayerExpanded(int32 LayerIndex) const;
	void SetLayerExpanded(int32 LayerIndex, bool bExpanded);
	bool IsLayerMultiSelected(int32 LayerIndex) const;
	// Applies the modifier keys currently held: plain replaces, ctrl/cmd toggles, shift extends
	// from the anchor.
	void UpdateMultiSelection(int32 LayerIndex);

	static bool AreLayerGroupsEqual(
		const TArray<FMixtormatLayerGroup>& A,
		const TArray<FMixtormatLayerGroup>& B);
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

	// Appends a titled card to a panel and hands back the box its rows go in, so a run of
	// values is one extra line at the top rather than a nested SNew tree around every row.
	TSharedRef<SVerticalBox> AddCard(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const FText& Title,
		const TSharedPtr<SWidget>& HeaderAction = nullptr);

	void AddSliderRow(
		const TSharedRef<SVerticalBox>& TargetPanel,
		const TSharedRef<SWidget>& Row);

	// The Invert / Balance / Contrast / Offset block, from one place.
	//
	// Four nodes run the same MixtormatShapeMask and used to write these rows out four times, with
	// four different slider ranges for the same parameter. The panel supplies a resolver for
	// whichever FMixtormatMaskShaping it owns and gets the block, the ranges and the tooltips.
	void AddMaskShapingRows(
		const TSharedRef<SVerticalBox>& TargetPanel,
		TFunction<FMixtormatMaskShaping*()> Resolve);

	FMixtormatParameterAddress BuildParameterAddress(
		const void* Owner,
		UScriptStruct* OwnerStruct,
		int32 MemberOffset) const;
	// The address a parameter row points at, resolved on every use rather than captured once.
	//
	// The inspector is built once, in Construct, before anything is selected -- so a resolver
	// called at construct time hands back nullptr and the address built from it is permanently
	// invalid. Every other binding in the panel is already lazy for that reason; this one was the
	// exception, which is why no row ever got a state dot or a parameter menu.
	template <typename TOwner, typename TMember>
	TFunction<FMixtormatParameterAddress()> MakeAddressResolver(
		TFunction<TOwner*()> Resolve,
		TMember TOwner::* Member)
	{
		return [this, Resolve, Member]() -> FMixtormatParameterAddress
		{
			TOwner* Owner = Resolve();
			if (!Owner)
			{
				return FMixtormatParameterAddress();
			}
			const int32 MemberOffset = static_cast<int32>(
				reinterpret_cast<const uint8*>(&(Owner->*Member))
				- reinterpret_cast<const uint8*>(Owner));
			return BuildParameterAddress(Owner, TOwner::StaticStruct(), MemberOffset);
		};
	}

	TSharedRef<SWidget> WrapParameterControl(
		const TSharedRef<SWidget>& Control,
		TFunction<FMixtormatParameterAddress()> ResolveTarget);
	TSharedRef<SWidget> BuildParameterContextMenu(FMixtormatParameterAddress Target);
	TSharedRef<SWidget> BuildParameterDriverPopover(FMixtormatParameterAddress Target);
	TSharedRef<SWidget> BuildParameterContextMenuFor(TFunction<FMixtormatParameterAddress()> ResolveTarget);
	TSharedRef<SWidget> BuildParameterDriverPopoverFor(TFunction<FMixtormatParameterAddress()> ResolveTarget);
	TSharedRef<SWidget> BuildDriverSourceMenu(FMixtormatParameterAddress Target);
	TSharedRef<SWidget> BuildDriverOutputMenu(FMixtormatParameterAddress Target);
	TSharedRef<SWidget> BuildDriverCombineMenu(FMixtormatParameterAddress Target);
	FMixtormatParameterBinding* FindParameterBinding(
		const FMixtormatParameterAddress& Target,
		bool bCreate);
	const FMixtormatParameterBinding* FindParameterBinding(
		const FMixtormatParameterAddress& Target) const;
	bool IsParameterReferenced(const FMixtormatParameterAddress& Target) const;
	bool IsParameterDriven(const FMixtormatParameterAddress& Target) const;
	bool IsParameterReferenceBroken(const FMixtormatParameterAddress& Target) const;
	bool CanPasteParameterReference(const FMixtormatParameterAddress& Target) const;
	bool WouldCreateParameterReferenceCycle(
		const FMixtormatParameterAddress& Destination,
		const FMixtormatParameterAddress& Source) const;
	void CopyParameterReference(FMixtormatParameterAddress Source);
	void PasteParameterReference(FMixtormatParameterAddress Destination);
	void ClearParameterReference(FMixtormatParameterAddress Target);
	void GoToParameterReferenceSource(FMixtormatParameterAddress Target);
	void SetDriverSource(
		FMixtormatParameterAddress Target,
		FGuid SourceLayerId,
		FGuid SourceChildId,
		EMixtormatDriverSourceKind SourceKind,
		FName SourceOutput);
	void SetDriverCombine(FMixtormatParameterAddress Target, EMixtormatDriverCombineMode Combine);
	void SetDriverEnabled(FMixtormatParameterAddress Target, bool bEnabled);
	void ClearParameterDriver(FMixtormatParameterAddress Target);
	void SetParameterReferenceMode(FMixtormatParameterAddress Target, EMixtormatReferenceMode Mode);
	EMixtormatReferenceMode GetParameterReferenceMode(const FMixtormatParameterAddress& Target) const;

	// True when a Link reference took the edit: the value went to the authoritative source and the
	// link survives. False means the row writes its own member, which is what breaks a Follow.
	bool TryWriteLinkedFloat(const FMixtormatParameterAddress& Target, float Value);
	bool TryWriteLinkedInt(const FMixtormatParameterAddress& Target, int32 Value);
	bool TryWriteLinkedBool(const FMixtormatParameterAddress& Target, bool Value);
	double GetEffectiveFloatParameter(const FMixtormatParameterAddress& Target, double LocalValue) const;
	int32 GetEffectiveIntParameter(const FMixtormatParameterAddress& Target, int32 LocalValue) const;
	bool GetEffectiveBoolParameter(const FMixtormatParameterAddress& Target, bool LocalValue) const;

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
		const TAttribute<FText>& ToolTip = TAttribute<FText>(),
		const double ValueScale = 1.0)
	{
		// The slider always works in the units its Min/Max/Default are written in. ValueScale is
		// what the member is stored in, per one of those units -- so a row can read -1..1 while the
		// data underneath stays in whatever the shader expects. Hue Shift is the case that needs it:
		// the pass is in degrees, but a signed -1..1 is what fills from the centre and reads as
		// untouched at zero.
		checkSlow(ValueScale != 0.0);
		const TFunction<FMixtormatParameterAddress()> ResolveTarget = MakeAddressResolver(Resolve, Member);
		TSharedRef<SWidget> Slider = MakeSlider(
			Label,
			TAttribute<double>::CreateLambda([this, Resolve, Member, DefaultValue, ValueScale, ResolveTarget]() -> double
			{
				const TOwner* Owner = Resolve();
				const double Local = Owner ? static_cast<double>(Owner->*Member) : DefaultValue * ValueScale;
				return GetEffectiveFloatParameter(ResolveTarget(), Local) / ValueScale;
			}),
			MinValue,
			MaxValue,
			DefaultValue,
			SnapDelta,
			false,
			FMixtormatOnSliderValueChanged::CreateLambda(
				[this, Resolve, Member, ValueScale, ResolveTarget](const double Value)
			{
				if (TOwner* Owner = Resolve())
				{
					const FMixtormatParameterAddress Address = ResolveTarget();
					if (IsParameterLocked(Address))
					{
						return;
					}
					const float Authored = static_cast<float>(Value * ValueScale);
					if (!TryWriteLinkedFloat(Address, Authored))
					{
						Owner->*Member = Authored;
						if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
						{
							Binding->Reference.bEnabled = false;
						}
					}
					RefreshLayeredPreview();
				}
			}),
			FSimpleDelegate::CreateLambda([this, Resolve, Member, DefaultValue, ValueScale, ResolveTarget]()
			{
				TOwner* Owner = Resolve();
				const float StoredDefault = static_cast<float>(DefaultValue * ValueScale);
				if (Owner && !FMath::IsNearlyEqual(Owner->*Member, StoredDefault))
				{
					const FMixtormatParameterAddress Address = ResolveTarget();
					if (IsParameterLocked(Address))
					{
						return;
					}
					if (!TryWriteLinkedFloat(Address, StoredDefault))
					{
						Owner->*Member = StoredDefault;
						if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
						{
							Binding->Reference.bEnabled = false;
						}
					}
					RefreshLayeredPreview();
				}
			}),
			ToolTip);
		return WrapParameterControl(Slider, ResolveTarget);
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
		const TFunction<FMixtormatParameterAddress()> ResolveTarget = MakeAddressResolver(Resolve, Member);
		TSharedRef<SWidget> Slider = MakeSlider(
			Label,
			TAttribute<double>::CreateLambda([this, Resolve, Member, DefaultValue, ResolveTarget]() -> double
			{
				const TOwner* Owner = Resolve();
				const int32 Local = Owner ? Owner->*Member : DefaultValue;
				return static_cast<double>(GetEffectiveIntParameter(ResolveTarget(), Local));
			}),
			MinValue,
			MaxValue,
			static_cast<double>(DefaultValue),
			1.0,
			true,
			FMixtormatOnSliderValueChanged::CreateLambda([this, Resolve, Member, ResolveTarget](const double Value)
			{
				if (TOwner* Owner = Resolve())
				{
					const FMixtormatParameterAddress Address = ResolveTarget();
					if (IsParameterLocked(Address))
					{
						return;
					}
					const int32 Authored = FMath::RoundToInt(Value);
					if (!TryWriteLinkedInt(Address, Authored))
					{
						Owner->*Member = Authored;
						if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
						{
							Binding->Reference.bEnabled = false;
						}
					}
					RefreshLayeredPreview();
				}
			}),
			FSimpleDelegate::CreateLambda([this, Resolve, Member, DefaultValue, ResolveTarget]()
			{
				TOwner* Owner = Resolve();
				if (Owner && Owner->*Member != DefaultValue)
				{
					const FMixtormatParameterAddress Address = ResolveTarget();
					if (IsParameterLocked(Address))
					{
						return;
					}
					if (!TryWriteLinkedInt(Address, DefaultValue))
					{
						Owner->*Member = DefaultValue;
						if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
						{
							Binding->Reference.bEnabled = false;
						}
					}
					RefreshLayeredPreview();
				}
			}),
			ToolTip);
		return WrapParameterControl(Slider, ResolveTarget);
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
		// Trailing rather than Make: a toggle's label travels with its toggle. The shared label
		// column exists so a run of values scans down one edge, and a toggle has no value in it
		// to scan -- paired beside a slider, a far-left label left "Invert" sitting against the
		// slider's right edge, closer to the value it did not belong to than to its own control.
		const TFunction<FMixtormatParameterAddress()> ResolveTarget = MakeAddressResolver(Resolve, Member);
		TSharedRef<SWidget> Toggle = MixtormatRow::MakeTrailing(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Resolve, Member, ResolveTarget]()
				{
					const TOwner* Owner = Resolve();
					const bool Local = Owner && Owner->*Member;
					return GetEffectiveBoolParameter(ResolveTarget(), Local) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Resolve, Member, ResolveTarget](const ECheckBoxState State)
				{
					if (TOwner* Owner = Resolve())
					{
						const FMixtormatParameterAddress Address = ResolveTarget();
						if (IsParameterLocked(Address))
						{
							return;
						}
						const bool Authored = State == ECheckBoxState::Checked;
						if (!TryWriteLinkedBool(Address, Authored))
						{
							Owner->*Member = Authored;
							if (FMixtormatParameterBinding* Binding = FindParameterBinding(Address, false))
							{
								Binding->Reference.bEnabled = false;
							}
						}
						RefreshLayeredPreview();
					}
				})),
			ToolTip);
		return WrapParameterControl(Toggle, ResolveTarget);
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
	void HandleUserLibrarySearchChanged(const FText& SearchTextValue);
	void RebuildCategoryList();
	void RebuildSurfaceList();
	void RebuildUserLibraryList();
	void RebuildLayerList();
	void RebuildMaskList();
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildAuthoringPage();
	TSharedRef<SWidget> BuildLeftPanel();
	TSharedRef<SWidget> BuildBottomLibrary();
	TSharedRef<SWidget> BuildStatusBar();
	FReply ToggleBottomLibraryCollapsed();
	TSharedRef<SWidget> BuildWorkflowMenu();
	TSharedRef<SWidget> BuildLibraryPage();
	TSharedRef<SWidget> BuildUserLibraryPage();
	TSharedRef<SWidget> BuildSurfaceList();
	TSharedRef<SWidget> BuildLayerStackPanel();
	TSharedRef<SWidget> BuildLayerRow(int32 LayerIndex);
	TSharedRef<SWidget> BuildLayerThumbnail(int32 LayerIndex);
	TSharedRef<SWidget> BuildLayerChildIcon(int32 LayerIndex, int32 ChildIndex);
	// The mask a child row is carrying, shown on hover -- the row only has room for a glyph.
	// Null for effects and generated masks, which have no picture to show.
	TSharedPtr<IToolTip> BuildMaskPreviewTooltip(int32 LayerIndex, int32 ChildIndex);
	FText GetLayerDisplayName(int32 LayerIndex) const;
	FText GetLayerSourceText(int32 LayerIndex) const;
	FText GetLayerChildName(const FMixtormatLayerChild& Child) const;
	FText GetLayerChildSourceText(int32 LayerIndex, int32 ChildIndex) const;
	TSharedRef<SWidget> BuildLayerContextMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildAddLayerMenu();
	TSharedRef<SWidget> BuildAddEffectMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildEffectContextMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildMaskBar();
	TSharedRef<SWidget> BuildMaskBlendModeMenu(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildMaskContextMenu(int32 LayerIndex, int32 MaskIndex);
	TSharedRef<SWidget> BuildBlurContextMenu(int32 LayerIndex, int32 ChildIndex);
	FMixtormatMaskBlur* GetSelectedLayerBlur();
	const FMixtormatMaskBlur* GetSelectedLayerBlur() const;
	// The mask picker grid, shared by adding and replacing -- the caller says what a pick means.
	TSharedRef<SWidget> BuildMaskGallery(TFunction<void(const FSoftObjectPath&)> OnChosen);

	TSharedRef<SWidget> BuildNormalSourceMenu(int32 LayerIndex);
	TSharedRef<SWidget> BuildMaskCard(
		const FText& Name,
		const FSoftObjectPath& AssetPath,
		const FAssetData& ThumbnailAsset,
		bool bCompact);
	TSharedRef<SWidget> BuildMaskLibraryContextMenu(FSoftObjectPath AssetPath);
	void RemoveImportedMask(FSoftObjectPath AssetPath);
	TSharedRef<SWidget> BuildSurfaceCard(
		const FText& Name,
		const FSoftObjectPath& AssetPath,
		const FAssetData& ThumbnailAsset);
	TSharedRef<SWidget> BuildSurfaceLibraryContextMenu(FSoftObjectPath AssetPath);
	TSharedRef<SWidget> BuildCompositionLibraryContextMenu(FSoftObjectPath AssetPath);
	void AddSurfaceFromLibrary(FSoftObjectPath AssetPath);
	void AddCompositionLayers(FSoftObjectPath AssetPath);
	void AddBakedLayerFromComposition(FSoftObjectPath AssetPath);
	void AddReferenceLayerFromComposition(FSoftObjectPath AssetPath);
	void BrowseLibraryAsset(FSoftObjectPath AssetPath);
	void RefreshBuiltInSurface(FSoftObjectPath AssetPath);
	void RemoveImportedSurface(FSoftObjectPath AssetPath);
	TSharedRef<SWidget> BuildPreviewPanel();
	TSharedRef<SWidget> BuildCompositionResolutionMenu();
	TSharedRef<SWidget> BuildInspectorPanel();
	TSharedRef<SWidget> BuildEffectInspectorControls();
	TSharedRef<SWidget> BuildChannelInfluenceControls();
	// A card rather than a group: colour adjustment is part of how a layer composites, so it
	// lives inside Composition next to the blend mode and opacity that decide the same thing.
	TSharedRef<SWidget> BuildColorAdjustmentCard();
	// The Surface Adjustments body, as titled cards. Extracted from the inline tree it used to
	// be: one column of thirteen sliders with no structure, which is what the cards are for.
	TSharedRef<SWidget> BuildSurfaceAdjustmentCards();

	// Appends the Feature, Curvature and Surface Mask cards. Appends rather than returns,
	// because these are siblings of the cards above them now rather than a group of their own.
	void AddGeneratedFeatureCards(const TSharedRef<SVerticalBox>& Panel);
	TSharedRef<SWidget> BuildHeightBlendControls();
	TSharedRef<SWidget> BuildLayerMaskControls();
	TSharedRef<SWidget> BuildMaskBlurControls();
	TSharedRef<SWidget> BuildGeneratedMaskControls();
	TSharedRef<SWidget> BuildStainControls();
	TSharedRef<SWidget> BuildRunoffControls();
	TSharedRef<SWidget> BuildStainModeMenu();
	TSharedRef<SWidget> BuildErosionControls();
	TSharedRef<SWidget> BuildProceduralPeelControls();
	TSharedRef<SWidget> BuildGeneratedContextMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildGeneratedBlendModeMenu(int32 LayerIndex, int32 ChildIndex);
	TSharedRef<SWidget> BuildErosionCurvatureModeMenu();
	TSharedRef<SWidget> BuildGradeControls();
	TSharedRef<SWidget> BuildBreakupControls();
	TSharedRef<SWidget> BuildGradeTonemapMenu();


	TWeakPtr<SWindow> LiveThemeWindow;
	bool bThemeRefreshPending = false;
	float ShellLeftFraction = 0.19f;
	float ShellCenterFraction = 0.60f;
	float ShellRightFraction = 0.21f;
	float PreviewHeightFraction = 0.64f;
	float LibraryHeightFraction = 0.36f;
	bool bBottomLibraryCollapsed = false;
	float MaterialLibraryFraction = 0.72f;
	float MaskLibraryFraction = 0.28f;
	// One page since the mixer and presets mock-ups were removed. Kept as a switcher rather than
	// unwound to a bare widget because the live-theme rebuild tears the tree down and re-parents
	// through it, and the settings window will want the second slot.
	TSharedPtr<SWidgetSwitcher> MainSwitcher;
	TSharedPtr<SWidgetSwitcher> LeftSwitcher;
	TSharedPtr<SButton> BottomLibraryToggleButton;
	TSharedPtr<SVerticalBox> CategoryListBox;
	TSharedPtr<SWrapBox> SurfaceListBox;
	TSharedPtr<SVerticalBox> UserLibraryListBox;
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
	FDelegateHandle AssetUpdatedHandle;
	float MaterialGalleryTileSize = MixtormatTokens::MaterialGalleryTileDefault;
	float MaskGalleryTileSize = MixtormatTokens::MaskBarTileSize;
	TArray<TSharedPtr<FAssetThumbnail>> LayerThumbnails;
	// The inspector strip's own thumbnail. Kept apart from LayerThumbnails because the strip is
	// re-made on every selection while that array is only cleared when the whole stack rebuilds --
	// pooled thumbnails accumulating there would eat the pool's budget.
	TSharedPtr<FAssetThumbnail> SelectedStripThumbnail;
	TArray<TSharedPtr<FAssetThumbnail>> MaskThumbnails;
	TArray<TSharedPtr<SMixtormatPreviewViewport>> PreviewViewports;
	// Keyed on identity, not position. Grouping gathers layers into a contiguous block and every
	// index in the stack shifts; an index-keyed set would hand one layer's expanded state to
	// whichever layer landed on its old row.
	// The live rows, so F2 can reach the one the selection names. Weak: RebuildLayerList throws
	// the widgets away and builds new ones on every change.
	TMap<FGuid, TWeakPtr<class SMixtormatLayerRow>> LayerRowWidgets;
	TMap<FGuid, TWeakPtr<class SMixtormatLayerGroupRow>> GroupRowWidgets;
	TSet<FGuid> ExpandedLayerIds;
	// Collapsed groups hide their members. UI only -- it never reaches the asset or the render.
	TSet<FGuid> CollapsedGroupIds;
	// Selecting a group clears the layer selection and the other way round, so the inspector
	// always has exactly one subject.
	FGuid SelectedGroupId;
	int32 SelectedGroupChildIndex = INDEX_NONE;
	// What the Group button acts on. Additive: SelectedLayerIndex stays the single layer the
	// inspector shows, and every existing path that reads it is unaffected.
	TSet<FGuid> SelectedLayerIds;
	// Where a shift-extend measures from. Set by a plain click only -- reusing SelectedLayerIndex
	// would move the anchor on every ctrl-click and make the next range unpredictable.
	FGuid SelectionAnchorLayerId;
	TArray<FMixtormatLayer> WorkingLayers;
	TArray<FMixtormatLayer> SavedLayers;
	TArray<FMixtormatLayerGroup> WorkingLayerGroups;
	TArray<FMixtormatLayerGroup> SavedLayerGroups;
	TArray<FEditHistoryState> UndoHistory;
	TArray<FEditHistoryState> RedoHistory;
	TArray<FNumericResetBinding> NumericResetBindings;
	TOptional<FMixtormatParameterAddress> ParameterReferenceClipboard;

	// The child clipboard keeps the payload, so a Copy still pastes after its source is deleted,
	// and separately the address it was taken from, which is all Paste Instance needs.
	TOptional<FMixtormatLayerChild> ChildClipboard;
	FGuid ChildClipboardSourceLayerId;
	FGuid ChildClipboardSourceChildId;
	bool bChildClipboardIsInstance = false;
	FEditHistoryState CurrentHistoryState;
	FSoftObjectPath SelectedSurfacePath;
	FText SelectedLibrarySurfaceName;
	FString UserLibrarySearchText;
	FSoftObjectPath SelectedMaskPath;
	FText SelectedLibraryMaskName;
	TStrongObjectPtr<UMaterialInstanceConstant> SelectedPreviewMaterial;
	TStrongObjectPtr<UMixtormatMaterial> WorkingMaterialAsset;
	float CurrentTiling = 2.0f;
	float CurrentRoughnessBias = 0.5f;
	float CurrentRoughnessContrast = 0.0f;
	float CurrentRoughnessOffset = 0.0f;
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
	bool bPreviewOverlayUiVisible = true;
	bool bBypassSelectedChild = false;
	bool bPreviewDisplacementEnabled = false;
	bool bGlobalUVRotation90 = false;
	bool bSavedGlobalUVRotation90 = false;
	bool bIsBaking = false;
	EMixtormatDebugPreviewMode DebugPreviewMode = EMixtormatDebugPreviewMode::None;
	int32 SelectedLayerIndex = INDEX_NONE;
	int32 SoloLayerIndex = INDEX_NONE;
	int32 SelectedEffectIndex = INDEX_NONE;
	int32 SelectedMaskIndex = INDEX_NONE;
	int32 LeftTabIndex = 0;
	int32 CompositionResolution = 2048;
	// Bake-only, independent of CompositionResolution (the live preview's resolution, which this
	// never changes). Reset from UMixtormatEditorSettings::DefaultBakeResolution the first time
	// the bake dialog opens for a given recipe; edited per-bake after that; never saved back.
	int32 BakeResolution = 2048;
	// Bake-only, same lifecycle as BakeResolution above. Infrastructure only: see
	// UMixtormatEditorSettings::DefaultBakeAASamples.
	int32 BakeAASamples = 1;
	EMixtormatStudioLighting StudioLighting = EMixtormatStudioLighting::Neutral;
	EMixtormatPreviewMesh PreviewMesh = EMixtormatPreviewMesh::Sphere;
	EMixtormatPreviewQuality PreviewQuality = EMixtormatPreviewQuality::Medium;
	EMixtormatPreviewAntiAliasing PreviewAntiAliasing = EMixtormatPreviewAntiAliasing::Temporal;
	int32 PreviewScreenPercentage = MixtormatPreviewScreenPercentage::Default;
	float PreviewFov = MixtormatPreviewCamera::FovDefault;
	float PreviewDisplacementAmount = 1.0f;
	// Multipliers on the active lighting mode's own brightness; 1 is what that mode intended.
	float PreviewLightIntensity = 1.0f;
	float PreviewSkylightIntensity = 1.0f;
	FSoftObjectPath BakeSettingsRecipePath;
	FString BakeDestinationPath;
	FString BakeOutputBaseName;
	FString WorkingMaterialName = TEXT("No material");
	FString WorkingStatusText = TEXT("Ready");
	FString SearchText;
	FName CategoryFilter;
	double LastHistoryRecordTime = 0.0;
};
