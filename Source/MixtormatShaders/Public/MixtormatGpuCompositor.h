// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/ObjectKey.h"

#include <atomic>

class UMaterialInstanceDynamic;
class UMixtormatMaterial;
struct FMixtormatLayer;
struct FMixtormatLayerGroup;
struct FMixtormatComposeResources;

// Cache of generated networks that survive between composites. Defined in the compositor
// translation unit and only ever touched on the render thread; the compositor holds it so that
// the lifetime is tied to the panel rather than to the module, and hands a shared reference to
// each render command so a composite still in flight keeps it alive.
struct FMixtormatNetworkCache;
struct FMixtormatPrefixCache;
struct FMixtormatNodeCache;

enum class EMixtormatDebugPreviewMode : uint8
{
	None,
	GeneratedFeature,
	HeightBlend,
	ContactAO,
	BorderNormal,
	LayerMask,
	// Written by the stain resolve rather than by the composite, because stain is a post-layer
	// filter and runs after the composite has already published the layer.
	Stain,
	// Written by the runoff resolve, for the same reason Stain writes its own: the composite
	// would otherwise overwrite the child's published preview with a flat DebugValue.
	Runoff,
	// One generic mode for every named mask/ID output a child publishes (Cluster/Pattern/Combine
	// Region IDs, Breakup's Region IDs/Gap/Edge/Pieces, Worn Edges' Wear, ...), addressed by
	// ChildTarget rather than by a dedicated mode per producer -- see FMixtormatChildPreviewTarget.
	// LayerIndex/ChildIndex are resolved from ChildTarget once, at composite time.
	ChildOutput,
	// The UV this layer actually samples (after UV From IDs, tiling and rotation) as a gradient:
	// red = U, green = V, with faint lines every eighth of a tile. Written by the composite.
	LayerUV
};

// What kind of thing a child-published output is, so the preview knows how to colour it: a
// scalar mask goes through the same red/cyan coverage ramp every other feature preview uses, an
// ID map is hashed to a per-region colour instead.
enum class EMixtormatPreviewOutputKind : uint8
{
	Mask,
	RegionIds,
	// Generator flow diagnostics: direction as hue (angle) and value (influence), and a
	// checker/line grid drawn in the warped coordinates so stretch and folds are visible.
	FlowDirection,
	WarpedUVGrid
};

// Names one previewable output on one child, by stable identity rather than by array position --
// a reorder or a group's broadcast must not retarget the preview to a different child.
//
// OwnerId is always a LayerId by the time this reaches the compositor: a group-authored child is
// flattened to one concrete member layer and its effective (per-member) ChildId on the editor
// side before the request is built, so the compositor itself never has to know what a group is.
struct FMixtormatChildPreviewTarget
{
	FGuid OwnerId;
	FGuid ChildId;
	// Empty for RegionIds -- a child publishes at most one ID map. Named ("Gap", "Edge",
	// "Pieces", "Wear", ...) for Mask, the same names FMixtormatMaskLayer::PublishedSourceOutput
	// already uses to read them.
	FName OutputName;
	EMixtormatPreviewOutputKind Kind = EMixtormatPreviewOutputKind::Mask;
	// Only meaningful when Kind == RegionIds. Names a Mask-kind published output on the same
	// child that the region-id colourist should read to force a pixel to black instead of a
	// hashed colour -- Breakup's Region IDs know nothing about grout on their own, so the preview
	// borrows Breakup's own Gap mask to say which pixels are invalid. NAME_None when the ID map
	// already encodes its own invalid pixels (Cluster IDs has none; Pattern/Combine IDs blacken
	// them inside their own kernel and need no second texture here).
	FName GapMaskName;

	bool IsValid() const { return OwnerId.IsValid() && ChildId.IsValid(); }

	friend bool operator==(const FMixtormatChildPreviewTarget& A, const FMixtormatChildPreviewTarget& B)
	{
		return A.OwnerId == B.OwnerId && A.ChildId == B.ChildId
			&& A.OutputName == B.OutputName && A.Kind == B.Kind
			&& A.GapMaskName == B.GapMaskName;
	}
};

struct FMixtormatDebugPreviewSettings
{
	EMixtormatDebugPreviewMode Mode = EMixtormatDebugPreviewMode::None;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	// Only meaningful when Mode == ChildOutput. LayerIndex/ChildIndex above are resolved from
	// this once, at the top of RequestComposeInternal, and every pass below reads those exactly
	// like it does for LayerMask -- only the resolution step is new.
	FMixtormatChildPreviewTarget ChildTarget;
};

class MIXTORMATSHADERS_API FMixtormatGpuCompositor final
{
public:
	FMixtormatGpuCompositor();
	~FMixtormatGpuCompositor();

	bool Initialize(FIntPoint InResolution = FIntPoint(1024, 1024));
	// False rejects invalid references or a failed gather/initialization; no parent is submitted.
	// True means queued, not GPU-complete. OnComplete runs on the game thread after successful
	// render-graph submission. Sources are evaluated at this compositor's resolution.
	// Pass the document asset path to reject references back into an unsaved edit of that asset.
	bool RequestCompose(
		const TArray<FMixtormatLayer>& Layers,
		FSimpleDelegate OnComplete = FSimpleDelegate(),
		FMixtormatDebugPreviewSettings DebugSettings = FMixtormatDebugPreviewSettings(),
		bool bRotateOutput90 = false,
		const FSoftObjectPath& OwnerPath = FSoftObjectPath());
	// With groups. Each group's shared children are expanded onto its member layers before any of
	// the above happens, so what this composes is still one flat array of the same length and
	// order as Layers -- see MixtormatLayerGroups::BuildEffectiveLayers. The overload without
	// Groups is the same call on a document that has none.
	bool RequestCompose(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		FSimpleDelegate OnComplete = FSimpleDelegate(),
		FMixtormatDebugPreviewSettings DebugSettings = FMixtormatDebugPreviewSettings(),
		bool bRotateOutput90 = false,
		const FSoftObjectPath& OwnerPath = FSoftObjectPath());
	void BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const;

	bool IsInitialized() const { return bInitialized; }
	// True from RequestCompose until the render thread has submitted that composite. A caller
	// that recomposites every frame can skip a request while this is set and keep only its latest.
	bool IsComposeInFlight() const;
	// Document-level passes on the finished surface. Off (zero AO) until a caller sets them, so a
	// referenced composition never applies them a second time inside its parent.
	// The final normal likewise: off until set, so a reference keeps its own relief normals and
	// its parent rebuilds from the height once. Toggling it changes what every effect writes to
	// the normal, so cached layer results are dropped.
	void SetFinalSettings(
		float HeightAOAmount,
		float HeightAORadius,
		bool bNormalFromHeight = false,
		float HeightNormalStrength = 1.0f,
		bool bAutoRemapHeight = false)
	{
		FinalAOAmount = HeightAOAmount;
		FinalAORadius = HeightAORadius;
		FinalNormalStrength = HeightNormalStrength;
		bFinalAutoRemapHeight = bAutoRemapHeight;
		if (bFinalNormalFromHeight != bNormalFromHeight)
		{
			bFinalNormalFromHeight = bNormalFromHeight;
			ResetCaches();
		}
	}
	// Drops every cached layer prefix and referenced composition. Called on resolution change;
	// safe to call any time -- the next composite simply runs in full.
	void ResetCaches();
	FIntPoint GetResolution() const { return Resolution; }
	UTextureRenderTarget2D* GetBaseColorOutput() const;
	UTextureRenderTarget2D* GetNormalOutput() const;
	UTextureRenderTarget2D* GetRAMOutput() const;
	UTextureRenderTarget2D* GetHeightOutput() const;
	UTextureRenderTarget2D* GetDebugOutput() const;
	// The raw Region IDs behind the current Region IDs preview, one float per pixel, -1 where no
	// region covers it. Written only while such a preview is up, and cleared to -1 otherwise, so
	// the Exact ID picker can never read a stale id from a preview that has been switched off.
	UTextureRenderTarget2D* GetRegionIdPickOutput() const;

private:
	bool InitializeTargets(FIntPoint InResolution, bool bWaitForResources);
	bool RequestComposeInternal(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		FSimpleDelegate OnComplete,
		FMixtormatDebugPreviewSettings DebugSettings,
		bool bRotateOutput90,
		TSet<const UMixtormatMaterial*>& ActiveSources);

	// A submission owns its targets independently of this instance and any later resize.
	TSharedPtr<FMixtormatComposeResources, ESPMode::ThreadSafe> PendingOutputs;

	struct FTargetSet
	{
		TStrongObjectPtr<UTextureRenderTarget2D> BaseColor;
		TStrongObjectPtr<UTextureRenderTarget2D> Normal;
		TStrongObjectPtr<UTextureRenderTarget2D> RAM;
		TStrongObjectPtr<UTextureRenderTarget2D> Height;
		TStrongObjectPtr<UTextureRenderTarget2D> Debug;
		TStrongObjectPtr<UTextureRenderTarget2D> RegionIdPick;
	};

	FTargetSet Targets[2];

	// Cache of generated networks keyed on the parameters that shape them. Growing one is by a wide
	// margin the most expensive thing in the graph, and almost nothing a user touches while
	// tuning actually changes it, so it is kept rather than regrown every frame of a drag.
	TSharedPtr<FMixtormatNetworkCache, ESPMode::ThreadSafe> NetworkCache;
	TSharedPtr<FMixtormatPrefixCache, ESPMode::ThreadSafe> PrefixCache;
	TSharedPtr<FMixtormatNodeCache, ESPMode::ThreadSafe> NodeCache;
	// Prefix hashes of the previous request, to find the lowest layer an edit changed.
	TArray<uint64> LastPrefixHashes;
	int32 LastSnapshotLayer = INDEX_NONE;
	TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> InFlight;

	// Referenced compositions, one compositor per source asset, recomposed only when the
	// source's content hash changes. Rebuilding one per reference per composite allocated a full
	// set of render targets every frame of a drag.
	struct FReferenceEntry
	{
		TUniquePtr<FMixtormatGpuCompositor> Compositor;
		uint64 ContentHash = 0;
		bool bValid = false;
	};
	TMap<TObjectKey<UMixtormatMaterial>, TSharedPtr<FReferenceEntry>> ReferenceCompositors;
	// False for compositors created for a reference layer. They recomposite only when their source
	// changes, so layer-prefix and node caches inside them would hold memory for nothing.
	bool bCacheLayerResults = true;
	float FinalAOAmount = 0.0f;
	float FinalAORadius = 8.0f;
	bool bFinalNormalFromHeight = false;
	float FinalNormalStrength = 1.0f;
	bool bFinalAutoRemapHeight = false;

	FIntPoint Resolution = FIntPoint::ZeroValue;
	int32 PublishedTargetIndex = 0;
	bool bInitialized = false;
};
