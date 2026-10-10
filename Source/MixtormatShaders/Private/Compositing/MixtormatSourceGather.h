// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

struct FMixtormatSourceEntry;

namespace MixtormatGpuCompositor
{
	// SourceIds of shelf entries the authored stack actually reads, plus the earlier shelf entries
	// those producers themselves read (transitive; shelf dependencies resolve earlier-only, like
	// layer modules). A source nothing references is never gathered, so an unused shelf costs
	// nothing at compose time.
	void CollectDemandedShelfSources(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources,
		TSet<FGuid>& OutDemandedSourceIds);

	// Gathers each demanded, enabled shelf generator as a synthetic producer layer:
	// LayerId = the entry's SourceId, bGenerator, exactly one child (the entry's root generator).
	// Order is shelf order, and a producer's own inputs resolve against this array, so an
	// earlier-only shelf dependency reuses the existing generator-input validator unchanged.
	// Producers are gathered uncached: their node identity is settings-only today and would not
	// notice a dependency resolving to a different entry after a reorder (audit item 11).
	void GatherSourceProducers(
		const TArray<FMixtormatSourceEntry>& Sources,
		const TSet<FGuid>& DemandedSourceIds,
		TArray<FLayerRenderData>& OutProducers);
}
