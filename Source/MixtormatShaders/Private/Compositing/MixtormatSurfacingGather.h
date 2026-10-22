// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

namespace MixtormatGpuCompositor
{
	void GatherSurfacingChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex,
		int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers);
}
