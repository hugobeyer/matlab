// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"
#include "Compositing/MixtormatNoiseRender.h"

namespace MixtormatGpuCompositor
{
// One Noise module of a Generator layer: one dispatch producing the module's value, its signed
// height, its gradient and (for the Worley family) its cell IDs, then publication of the value
// and gradient fields under this child's address. Settings come from the gather's store; a miss
// leaves the module out, the same as a disabled generator.
void AddNoisePasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
	const FLayerRenderData& Layer, int32 SourceChildIndex, FGeneratorBundle* Bundle);

// Explicit downhill transport from completed destination-space height, with flat-slope validity.
void AddNoiseFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	int32 SourceChildIndex, FRDGTextureRef Height);
}