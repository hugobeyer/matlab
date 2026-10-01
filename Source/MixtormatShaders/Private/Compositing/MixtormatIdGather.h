// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

namespace MixtormatGpuCompositor
{
	// False is a texture-resource failure; inactive children succeed without adding a node.
	bool GatherIdChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, int32 SourceChildIndex, int32 LayerIndex,
		const TArray<FMixtormatLayer>& EffectiveLayers, const UMixtormatSurface* Surface,
		UTexture2D* WhiteTexture, bool bCacheLayers);
}
