// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MixtormatGpuCompositorInternal.h"

// One gather function per migrated effect family: authored values are sanitized through the
// runtime contract table (MixtormatParameterContracts), and every piece of genuinely derived
// math -- Breakup's cell counts and size range, Runoff's strata count and radians conversion --
// stays local to its function. Pure moves of the blocks that used to live inline in
// RequestComposeInternal, in the same order as that if-chain; no behavior change.
//
// The mask-child gathers stay in the compositor: they interleave with palette and HasMask
// scheduling logic (plan §3 Phase 3). Procedural peeling is gathered here with the other effect
// families.
namespace MixtormatGpuCompositor
{
	void GatherErosion(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
	void GatherGrade(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
	void GatherBreakup(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
	void GatherLayerBlur(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
	void GatherFlowWarp(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
	void GatherWornEdges(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);

	// Stain and Runoff resolve into the layer's mask chain, so gathering one makes the layer
	// masked -- exactly like an authored, generated, craquelure or colour-ID child does.
	void GatherStain(
		FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect, bool& bHasMask);
	void GatherRunoff(
		FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect, bool& bHasMask);
	void GatherPeeling(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect);
}
