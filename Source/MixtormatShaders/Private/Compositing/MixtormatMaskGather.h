// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

namespace MixtormatGpuCompositor
{
	// False is a texture-resource failure; inactive children succeed without adding a node.
	bool GatherMaskChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex,
		const TArray<FMixtormatLayer>& EffectiveLayers);
}
