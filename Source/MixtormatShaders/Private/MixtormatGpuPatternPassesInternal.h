// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	bool HasPixelRootIds(const FLayerRenderData& Layer, int32 ProducerIndex);

	FRDGTextureRef AddRegionIndexPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef RegionIds,
		const bool bPixelRootIds,
		const FIntPoint OutputSize,
		TMap<FRDGTextureRef, FRDGTextureRef>& RootCache);

	FRDGTextureRef FindRegionIdsAboveWithIndex(
		const FLayerRenderData& Layer,
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const FChildRenderData& Child,
		int32& OutProducerIndex);
}
