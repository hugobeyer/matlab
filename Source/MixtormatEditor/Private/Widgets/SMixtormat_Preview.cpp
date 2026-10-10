// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "Preview/SMixtormatLightGizmo.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"

#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/Input/SComboButton.h"


// The 3D preview viewport: mesh, quality, camera, lighting, displacement, debug modes.
//
// The control clusters themselves live in SMixtormat_PreviewControls.cpp; this file keeps the
// viewport, the preview state, the debug/output plumbing and the panel's composition.

#define LOCTEXT_NAMESPACE "SMixtormat"

// The eye always toggles the primary; the chevron (built only when Secondary is non-empty) offers
// the rest. Derived from GetChildCapabilities rather than hand-authored here a second time: the
// entry with bPreviewable && !bSecondaryPreview becomes Primary, every bPreviewable &&
// bSecondaryPreview entry becomes Secondary. A capability that is copyable but not previewable
// (Pattern IDs' Gap -- the default Region IDs preview already renders it, blackened) contributes
// nothing here, which is the whole reason capabilities keeps the two flags separate.
static FMixtormatChildPreviewOutputSet GetPreviewOutputSet(const FMixtormatChildCapabilities& Capabilities)
{
	FMixtormatChildPreviewOutputSet Result;
	for (const FMixtormatPublishedOutputDesc& Output : Capabilities.Outputs)
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

FMixtormatChildPreviewOutputSet GetChildPreviewOutputSet(const FMixtormatLayerChild& Child)
{
	return GetPreviewOutputSet(GetChildCapabilities(Child));
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
	if (!bModifierDown && !InKeyEvent.IsAltDown()
		&& InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::V)
	{
		for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
		{
			if (Viewport.IsValid())
			{
				Viewport->ResetChannelPreview();
			}
		}
		DebugPreviewMode = EMixtormatDebugPreviewMode::None;
		ChildPreviewTarget = FMixtormatChildPreviewTarget();
		RefreshLayeredPreview(false);
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SMixtormat::SetPreviewMesh(const EMixtormatPreviewMesh MeshType)
{
	if (MeshType == EMixtormatPreviewMesh::Plane && PreviewMesh == EMixtormatPreviewMesh::Plane)
	{
		// The Plane button is already selected, so a click cycles its orientation rather than
		// re-selecting the mesh. Every other click path below resets it to Horizontal.
		PlaneOrientation = PlaneOrientation == EMixtormatPlaneOrientation::Horizontal
			? EMixtormatPlaneOrientation::VerticalX
			: EMixtormatPlaneOrientation::Horizontal;
	}
	else
	{
		PreviewMesh = MeshType;
		PlaneOrientation = EMixtormatPlaneOrientation::Horizontal;
	}
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->SetPreviewMesh(MeshType, PlaneOrientation);
		}
	}
	CloseQuickControls();
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
	PreviewFov = MixtormatPreviewCamera::OverlayFovDefault;
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
	const FText& ToolTip,
	const float IconSize)
{
	// The same widget the layer stack's eye is, deliberately. This was a plated SButton with its
	// own 14px box and its own teal, so the two eyes in the tool -- one saying "this layer is
	// visible", one saying "the viewport is showing this channel" -- looked like different
	// controls doing unrelated things. One eye, one behaviour: no plate in any state, the accent
	// when it is on.
	return SNew(SMixtormatIconButton)
		.Size(IconSize)
		.ToolTipText(ToolTip)
		.bActive_Lambda([this, Mode]() { return DebugPreviewMode == Mode; })
		.Icon_Lambda([this, Mode]()
		{
			return DebugPreviewMode == Mode ? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
		})
		.OnClicked_Lambda([this, Mode]() { ToggleFeaturePreview(Mode); });
}

FReply SMixtormat::CycleSelectedModulePreview()
{
	const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
	if (!Child || !IsGroupChildEnabled(*Child))
	{
		return FReply::Handled();
	}

	struct FCandidate
	{
		EMixtormatDebugPreviewMode Mode;
		FMixtormatChildPreviewTarget Target;
	};
	TArray<FCandidate> Candidates;
	const bool bMaskProducer = Child->Type == EMixtormatLayerChildType::Mask
		|| Child->Type == EMixtormatLayerChildType::Generated
		|| Child->Type == EMixtormatLayerChildType::Craquelure
		|| Child->Type == EMixtormatLayerChildType::ColorId
		|| Child->Type == EMixtormatLayerChildType::RandomId;
	if (bMaskProducer)
	{
		Candidates.Add({EMixtormatDebugPreviewMode::LayerMask, FMixtormatChildPreviewTarget()});
	}
	Candidates.Add({EMixtormatDebugPreviewMode::LayerUV, FMixtormatChildPreviewTarget()});

	const FMixtormatChildPreviewOutputSet Outputs = GetChildPreviewOutputSet(*Child);
	if (IsChildOutputPreviewReady(*Child))
	{
		TArray<FMixtormatPreviewOutputDesc> Descriptors;
		if (Outputs.Primary.IsSet())
		{
			Descriptors.Add(Outputs.Primary.GetValue());
		}
		Descriptors.Append(Outputs.Secondary);
		for (const FMixtormatPreviewOutputDesc& Output : Descriptors)
		{
			Candidates.Add({
				EMixtormatDebugPreviewMode::ChildOutput,
				ResolveChildPreviewTarget(Output.Name, Output.Kind, Output.GapMaskName)});
		}
	}
	if (Candidates.IsEmpty())
	{
		return FReply::Handled();
	}
	for (const TSharedPtr<SMixtormatPreviewViewport>& Viewport : PreviewViewports)
	{
		if (Viewport.IsValid())
		{
			Viewport->ResetChannelPreview();
		}
	}

	int32 CurrentIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Mode == DebugPreviewMode
			&& (DebugPreviewMode != EMixtormatDebugPreviewMode::ChildOutput
				|| Candidates[Index].Target == ChildPreviewTarget))
		{
			CurrentIndex = Index;
			break;
		}
	}
	const int32 NextIndex = CurrentIndex + 1;
	if (NextIndex >= Candidates.Num())
	{
		DebugPreviewMode = EMixtormatDebugPreviewMode::None;
		ChildPreviewTarget = FMixtormatChildPreviewTarget();
	}
	else
	{
		DebugPreviewMode = Candidates[NextIndex].Mode;
		ChildPreviewTarget = Candidates[NextIndex].Target;
	}
	RefreshLayeredPreview(false);
	return FReply::Handled();
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
		if (!IsSelectedOutputPreviewReady())
		{
			return FReply::Handled();
		}
		DebugPreviewMode = EMixtormatDebugPreviewMode::ChildOutput;
		ChildPreviewTarget = Target;
	}
	RefreshLayeredPreview(false);
	return FReply::Handled();
}

namespace
{
	// The producer whose Region IDs an instance or a Region-IDs reference actually reads, found by
	// following authored layer/group addresses to its end. A missing source or cycle cannot redirect.
	bool ResolveRegionIdSource(
		const FMixtormatBindingScope& Scope,
		const FGuid& OwnerLayerId,
		const FMixtormatLayerChild& Child,
		FGuid& OutOwnerId,
		FGuid& OutChildId)
	{
		const FMixtormatLayerChild* Current = &Child;
		FGuid OwnerId = OwnerLayerId;
		TArray<TPair<FGuid, FGuid>> Visited;
		bool bRedirected = false;
		while (Current)
		{
			FGuid NextOwnerId, NextChildId;
			if (Current->IsInstance())
			{
				// An unset source layer means "this layer", the same reading IsSourceOfSelectedInstance uses.
				NextOwnerId = Current->SourceLayerId.IsValid() ? Current->SourceLayerId : OwnerId;
				NextChildId = Current->SourceChildId;
			}
			else if (Current->Type == EMixtormatLayerChildType::OutputReference
				&& Current->OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds
				&& Current->OutputReference.HasSource())
			{
				NextOwnerId = Current->OutputReference.SourceLayerId;
				NextChildId = Current->OutputReference.SourceChildId;
			}
			else
			{
				return bRedirected;
			}
			const TPair<FGuid, FGuid> Address(OwnerId, Current->ChildId);
			if (Visited.Contains(Address)) { return false; }
			Visited.Add(Address);
			const FMixtormatLayerChild* Next = MixtormatParameterBinding::FindChild(Scope, NextOwnerId, NextChildId);
			if (!Next)
			{
				return false;
			}
			Current = Next;
			OwnerId = NextOwnerId;
			OutOwnerId = NextOwnerId;
			OutChildId = NextChildId;
			bRedirected = true;
		}
		return false;
	}
}

FMixtormatChildPreviewTarget SMixtormat::ResolveChildPreviewTarget(
	const FName OutputName, const EMixtormatPreviewOutputKind Kind, const FName GapMaskName) const
{
	FMixtormatChildPreviewTarget Target;
	Target.OutputName = OutputName;
	Target.Kind = Kind;
	Target.GapMaskName = GapMaskName;
	const FMixtormatBindingScope Scope{WorkingLayers, WorkingLayerGroups};
	const auto RedirectToSource = [this, &Target, &Scope, Kind](const FGuid OwnerId, const FMixtormatLayerChild& Child)
	{
		FGuid SourceOwnerId, SourceChildId;
		if (Kind != EMixtormatPreviewOutputKind::RegionIds
			|| !ResolveRegionIdSource(Scope, OwnerId, Child, SourceOwnerId, SourceChildId)) { return false; }
		Target.OwnerId = SourceOwnerId;
		Target.ChildId = SourceChildId;
		if (const FMixtormatLayerChild* Source = MixtormatParameterBinding::FindChild(Scope, SourceOwnerId, SourceChildId))
		{
			const FMixtormatChildPreviewOutputSet Outputs = GetChildPreviewOutputSet(*Source);
			if (Outputs.Primary.IsSet())
			{
				Target.OutputName = Outputs.Primary.GetValue().Name;
				Target.GapMaskName = Outputs.Primary.GetValue().GapMaskName;
			}
		}
		if (MixtormatLayerGroups::FindGroup(WorkingLayerGroups, SourceOwnerId))
		{
			Target.OwnerId.Invalidate();
			Target.ChildId.Invalidate();
			if (const FMixtormatLayer* Member = WorkingLayers.FindByPredicate(
				[OwnerId, SourceOwnerId](const FMixtormatLayer& Layer)
				{
					return Layer.LayerId == OwnerId && Layer.GroupId == SourceOwnerId && Layer.bEnabled;
				}))
			{
				Target.OwnerId = Member->LayerId;
				Target.ChildId = MixtormatLayerGroups::MakeEffectiveChildId(SourceOwnerId, SourceChildId, Member->LayerId);
				return true;
			}
			int32 First = INDEX_NONE, Last = INDEX_NONE;
			if (MixtormatLayerGroups::GetGroupRange(WorkingLayers, SourceOwnerId, First, Last))
			{
				for (int32 Index = First; Index <= Last; ++Index)
				{
					if (WorkingLayers.IsValidIndex(Index) && WorkingLayers[Index].bEnabled)
					{
						Target.OwnerId = WorkingLayers[Index].LayerId;
						Target.ChildId = MixtormatLayerGroups::MakeEffectiveChildId(SourceOwnerId, SourceChildId, Target.OwnerId);
						break;
					}
				}
			}
		}
		return true;
	};

	if (SelectedLayerIndex != INDEX_NONE)
	{
		if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
		{
			return Target;
		}
		const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
		const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex());
		if (!Child)
		{
			return Target;
		}
		Target.OwnerId = Layer.LayerId;
		Target.ChildId = Child ? Child->ChildId : FGuid();
		// An instance or reference has no map of its own to show: it reads its source's, so that is
		// what the eye and the I key preview. The still-valid check re-resolves through here too,
		// so the preview survives for as long as the instance stays selected.
		RedirectToSource(Layer.LayerId, *Child);
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
	if (RedirectToSource(SelectedGroupId, Group->Children[SelectedGroupChildIndex])) { return Target; }
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


bool SMixtormat::IsSelectedOutputPreviewReady() const
{
	if (const FMixtormatLayerChild* Child = ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
	{
		return IsChildOutputPreviewReady(*Child);
	}
	// Generator layers publish per module, so a layer with no child selected has nothing to show.
	return false;
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
	if (Child.Type == EMixtormatLayerChildType::OutputReference)
	{
		return IsOutputReferenceAvailable(GetSelectedChildAddress());
	}
	if (Child.Type == EMixtormatLayerChildType::Effect)
	{
		const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
		const EMixtormatEffectType Type = Asset ? Asset->EffectType : Child.Effect.ProceduralType;
		if (MixtormatIsGeneratorFlowEffect(Type))
		{
			const FMixtormatChildAddress Address = GetSelectedChildAddress();
			const TArray<FMixtormatLayerChild>* Children = ResolveContainer(Address);
			if (!Children)
			{
				return false;
			}
			if (!Child.ScopeOwnerChildId.IsValid())
			{
				return false;
			}
			const FGuid GroupId = WorkingLayers.IsValidIndex(SelectedLayerIndex)
				? WorkingLayers[SelectedLayerIndex].GroupId : SelectedGroupId;
			if (const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, GroupId))
			{
				if (!Group->bEnabled)
				{
					return false;
				}
			}
			int32 CurrentIndex = ResolveChildIndexAt(Address);
			FGuid OwnerId = Child.ScopeOwnerChildId;
			bool bImmediateOwner = true;
			// Require preceding, enabled ancestors. This also rejects cycles and missing owners.
			while (OwnerId.IsValid())
			{
				const int32 OwnerIndex = Children->IndexOfByPredicate(
					[OwnerId](const FMixtormatLayerChild& Candidate) { return Candidate.ChildId == OwnerId; });
				if (OwnerIndex == INDEX_NONE || OwnerIndex >= CurrentIndex)
				{
					return false;
				}
				const FMixtormatLayerChild& Owner = (*Children)[OwnerIndex];
				if (!IsGroupChildEnabled(Owner)
					|| (bImmediateOwner && (Owner.Type != EMixtormatLayerChildType::Generator
						|| !MixtormatCanOwnGeneratorFlow(Owner.Generator.Type))))
				{
					return false;
				}
				bImmediateOwner = false;
				CurrentIndex = OwnerIndex;
				OwnerId = Owner.ScopeOwnerChildId;
			}
			return ResolveChildPreviewTarget(NAME_None, EMixtormatPreviewOutputKind::Mask, NAME_None).IsValid();
		}
	}
	// An ID Group folds however many producers sit inside it -- none, one, several, or nested
	// groups -- so it always has a map to show.

	if (Child.Type != EMixtormatLayerChildType::Filter)
	{
		return true;
	}
	// Legacy Cluster reads RAMH; Surface IDs only require maps used by the selected guides.
	if (!WorkingLayers.IsValidIndex(SelectedLayerIndex))
	{
		// Group-authored: no single owning layer's surface to check here. The per-member GPU pass
		// leaves the debug output cleared if the member this resolves to has none.
		return true;
	}
	const FMixtormatLayer& Layer = WorkingLayers[SelectedLayerIndex];
	if (Child.Filter.bSurfaceIds
		&& ((Child.Filter.Source == EMixtormatClusterSource::CompositeBelow && SelectedLayerIndex > 0)
			|| !Layer.SourceComposition.IsNull()))
	{
		return true;
	}
	return Child.Filter.CanSampleSurface(Layer.SourceSurface.LoadSynchronous());
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
		return IsSelectedOutputPreviewReady();
	};

	// The eye: a normal single-click toggle, identical in every respect to MakeFeaturePreviewButton
	// above. It always targets the primary -- clicking it while a secondary is active (chosen from
	// the chevron) switches back to the default view; clicking it while lit turns the preview off.
	TSharedRef<SWidget> Eye = SNew(SMixtormatIconButton)
		.Size(MixtormatTokens::PreviewEyeSize)
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
	// Styled like the chips (no stock button plate): a mask-preview glyph and a small caret, so it
	// reads as "pick which output to view" rather than as a bare disclosure arrow.
	TSharedRef<SWidget> Chevron = SNew(SComboButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.InspectorHeaderButton")))
		.HasDownArrow(false)
		.ContentPadding(FMargin(0.0f))
		.IsEnabled_Lambda([IsAnyActive, IsReady]() { return IsAnyActive() || IsReady(); })
		
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::PreviewEyeSize)
				.HeightOverride(MixtormatTokens::PreviewEyeSize)
				[
					SNew(SImage)
					.Image(MixtormatIcons::Mask())
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text)
						.CopyWithNewOpacity(FMixtormatThemeStore::GetResolved().Preview.IconRestOpacity)))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
				.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize * 0.6f)
				[
					SNew(SImage)
					.Image(MixtormatIcons::ChevronDown())
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted)))
				]
			]
		]
		.OnGetMenuContent_Lambda([this, Primary, Secondary, MatchesDesc]() -> TSharedRef<SWidget>
		{
			MixtormatMenu::FBuilder Menu;
			Menu.Caption(LOCTEXT("PreviewMenuOutputs", "OUTPUTS"));
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

	// One pill: eye | outputs, separated by a hairline, on the same well the chips use.
	return SNew(SMixtormatSurfaceBox)
		.Recipe_Lambda([]()
		{
			return Mixtormat::MakeWellRecipe(FMixtormatThemeStore::GetTheme());
		})
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Eye
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(1.0f, 3.0f)
			[
				SNew(SBox)
				.WidthOverride(FMixtormatThemeStore::GetResolved().Well.BorderWidth)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)
						.CopyWithNewOpacity(FMixtormatThemeStore::GetTheme().Well.BorderOpacity)))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 2.0f, 0.0f)
			[
				Chevron
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
	CloseQuickControls();
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
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();


	const TSharedRef<SMixtormatPreviewViewport> PreviewViewport = bReusingViewport
		? PreviewViewports[0].ToSharedRef()
		: SNew(SMixtormatPreviewViewport)
			.OnToggleOverlayUi(FSimpleDelegate::CreateLambda([this]()
			{
				bPreviewOverlayUiVisible = !bPreviewOverlayUiVisible;
			}))
			.OnToggleDisplacement(FSimpleDelegate::CreateLambda([this]()
			{
				SetPreviewDisplacementEnabled(!bPreviewDisplacementEnabled);
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
			}))
			.OnCycleModulePreview(FSimpleDelegate::CreateLambda([this]()
			{
				CycleSelectedModulePreview();
			}))
			// Bare 1-4 in the viewport: same path as the geometry rail buttons.
			.OnSetPreviewMesh(FMixtormatSetPreviewMesh::CreateLambda([this](const EMixtormatPreviewMesh MeshType)
			{
				SetPreviewMesh(MeshType);
			}))
			// Bare Q in the viewport. The delegate is installed once, on creation, and captures the
			// workspace -- which survives a rebuild -- so it stays valid across theme refreshes.
			.OnRequestQuickControls(FSimpleDelegate::CreateLambda([this]()
			{
				ToggleQuickControls();
			}))
			.OnCameraFovChanged(FMixtormatCameraFovChanged::CreateLambda([this](const float Fov)
			{
				SetPreviewFov(Fov);
			}))
			.OnDismissQuickControls(FMixtormatDismissQuickControls::CreateLambda([this]()
			{
				if (!bQuickControlsOpen)
				{
					return false;
				}
				CloseQuickControls();
				return true;
			}));

	// Default viewport control clusters live in GLOBAL and the Q marking menu. Keep Preview clear
	// for the workspace overlays while preserving the shared builders and their existing state.
	TSharedRef<SWidget> PreviewPanel = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			PreviewViewport
		]
		// The gizmo is feedback for RMB lighting rotation, independent of the removed default rails.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)
		.Padding(Resolved.PreviewLayout.OverlayInset)
		[
			SNew(SBox)
			.WidthOverride(MixtormatLightGizmo::Size)
			.HeightOverride(MixtormatLightGizmo::Size)
			.Visibility_Lambda([this, PreviewViewport]()
			{
				return bPreviewLightGizmoVisible && PreviewViewport->IsRotatingLighting()
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SMixtormatLightGizmo)
				.CameraRotation_Lambda([PreviewViewport]() { return PreviewViewport->GetCameraRotation(); })
				.LightDirection_Lambda([PreviewViewport]() { return PreviewViewport->GetLightDirection(); })
			]
		]

		// Inspector overlay only. Layers remains docked in the left column;
		// the frame is self-hit-test-invisible outside its content.
		+ SOverlay::Slot()
		[
			BuildFloatingPanelStack()
		]
		// The Tab quick controls, above the floating panels: it is invoked deliberately, so it takes
		// the top layer.
		+ SOverlay::Slot()
		[
			BuildQuickControlsOverlay()
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
