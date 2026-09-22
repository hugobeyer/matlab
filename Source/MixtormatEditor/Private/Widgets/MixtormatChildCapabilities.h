// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"
#include "MixtormatGpuCompositor.h"

// One output a child can publish, and everything downstream needs to know about it: whether it
// can be picked up through the generic preview eye, whether Copy Output can lift it into a
// published-source mask, and (for a RegionIds output whose invalid pixels are not already encoded
// in the map itself) which sibling output to borrow to blacken them.
//
// "Publishable" and "previewable" are deliberately separate flags rather than one. Pattern IDs'
// Gap is the case that forces the split: it is a real, copyable output (Copy Instance Mask from
// Gap has always existed), but it is not a preview option in its own right, because the default
// Region IDs preview already renders it -- Pattern IDs blackens its own grout inline. A second
// "Gap" eye entry would just be a worse way to see the same pixels the primary already shows.
struct FMixtormatPublishedOutputDesc
{
	FName Name;
	FText Label;
	EMixtormatPreviewOutputKind Kind = EMixtormatPreviewOutputKind::Mask;

	// Copy Output (formerly the three separate CopyInstanceMaskFrom* functions) offers this output
	// when set. Never true for Kind == RegionIds -- an ID map is not a scalar mask a Replace-blend
	// mask child could read.
	bool bCopyableAsMask = false;

	// The generic preview eye/chevron offers this output when set.
	bool bPreviewable = false;
	// Distinguishes the eye's single click (the one entry with bPreviewable && !bSecondaryPreview)
	// from the chevron's menu (every entry with bPreviewable && bSecondaryPreview). A child publishes
	// at most one primary.
	bool bSecondaryPreview = false;

	// Only meaningful when Kind == RegionIds and the compositor cannot already tell an invalid pixel
	// from a valid one by itself (Breakup's Region IDs; Cluster/Pattern/Combine IDs need nothing
	// here -- Cluster has no invalid pixels, Pattern/Combine blacken their own inline).
	FName PreviewGapMaskName;
};

// Everything one child type/procedural kind publishes. Preview, Copy Output and (eventually)
// compositor readiness all read this rather than each keeping their own notion of what a child
// makes available -- see priorities.md's clipboard/reuse item for why that used to drift.
struct FMixtormatChildCapabilities
{
	TArray<FMixtormatPublishedOutputDesc> Outputs;
};

// The one place a future output gets taught to the system: a new producer needs one case here and
// nothing else. Driven entirely by Child.Type/Child.Effect.ProceduralType, never by which specific
// instance is selected.
FMixtormatChildCapabilities GetChildCapabilities(const FMixtormatLayerChild& Child);

// Convenience for a probe built from just a type (an inspector group whose panel is only ever
// visible while a child of one known, fixed kind is selected).
FMixtormatChildCapabilities GetChildCapabilitiesForChildType(EMixtormatLayerChildType Type);
FMixtormatChildCapabilities GetChildCapabilitiesForEffectType(EMixtormatEffectType EffectType);

// The outputs Copy Output should list for this child, in Outputs order.
TArray<FMixtormatPublishedOutputDesc> GetCopyableOutputs(const FMixtormatChildCapabilities& Capabilities);
