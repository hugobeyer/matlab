// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

struct FMixtormatSourceEntry;

namespace MixtormatGpuCompositor
{
	void GatherGeneratorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex,
		bool bGeneratorLayer, bool bCacheLayers, uint64 PlacementKey,
						int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers,
						const TArray<FMixtormatSourceEntry>& Sources);

	// Generic generator-owned V2 gather. Post-generation Warp only for now;
	// deliberately not dispatched through legacy Structural Warp or Effect paths.
	void GatherGeneratorBehaviorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 BehaviorChildIndex, int32 LayerIndex,
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources);

	// Generator-layer Height Blend / Height Curve / Height Color Ramp sublayers. Ordered in the
	// layer's child chain with the Generator modules.
	void GatherGeneratorHeightModuleChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex, bool bGeneratorLayer,
					int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers);
}
