// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Compositing/MixtormatGatherCommon.h"

namespace MixtormatGpuCompositor
{
	UMixtormatMaterial* ResolveLayerReference(const FMixtormatLayer& Layer);
	bool IsInvalidLayerReference(const FMixtormatLayer& Layer,
		const TStrongObjectPtr<UMixtormatMaterial>& Source,
		const TSet<const UMixtormatMaterial*>& ActiveSources);
	void GatherLayerSourceCacheKey(FLayerRenderData& Data, const FMixtormatLayer& Layer);
	bool GatherLayerSource(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		UTexture2D* WhiteTexture, UTexture2D* NormalTexture,
		const UMixtormatSurface*& Surface, UTexture2D*& LayerNormal);
	uint64 GatherLayerPlacementKey(const FMixtormatLayer& Layer, bool bGeneratorLayer);
	void GatherLayerFields(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		int32 LayerIndex, const UMixtormatSurface* Surface, UTexture2D* LayerNormal);
}
