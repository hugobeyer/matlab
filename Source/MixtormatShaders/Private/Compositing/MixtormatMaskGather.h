// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

struct FMixtormatSourceEntry;

namespace MixtormatGpuCompositor
{
	// Sources carries the document shelf so a mask bound to a linked instance resolves its
	// published-source address to the original producer before the render thread ever sees it.
	// False is a texture-resource failure; inactive children succeed without adding a node.
	bool GatherMaskChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex,
		const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources);
}
