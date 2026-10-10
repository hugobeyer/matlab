// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

struct FMixtormatSourceEntry;

namespace MixtormatGpuCompositor
{
	// SourceIds of shelf entries the authored stack reads, transitively closed over producer-to-
	// producer references and returned in dependency order: a producer always appears after every
	// source it reads. Shelf array order is organisational only and never affects the result.
	//
	// The graph is a DAG, not a position rule. Self references and cycles are rejected -- a
	// reference that would close a cycle contributes no edge, so the offending producer is absent
	// from the order and its consumer's input resolves to nothing (a missing field is treated as
	// unavailable, never as a layer fallback). A disabled or malformed endpoint likewise adds no
	// edge, so an unused or unreachable shelf entry costs nothing at compose time.
	void CollectDemandedShelfSources(
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources,
		TArray<FGuid>& OutDemandedOrder);

	// Gathers each demanded entry as an explicitly source-owned producer (bIsShelfSource, addressed
	// by SourceShelfId), in the supplied dependency order and uncached. A producer carries no layer
	// state, is never composited, snapshotted or indexed as a layer, and resolves its inputs against
	// the Sources shelf alone -- never the material stack -- so a shelf reference can never be
	// reinterpreted as a layer.
	void GatherSourceProducers(
		const TArray<FMixtormatSourceEntry>& Sources,
		const TArray<FGuid>& DemandedOrder,
		TArray<FLayerRenderData>& OutProducers);
}
