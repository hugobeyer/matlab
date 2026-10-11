// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
// Explicit source contracts; arbitrary scalar/vector fields are not native Flow inputs.
enum class ENativeFlowSource : uint8
{
	SignedHeight, // R32_FLOAT, destination-space signed height.
	SignedBoundary // R32_FLOAT SDF, or G32R32F (negative-inside UV distance, validity).
};

// Unit negative-gradient transport in destination tile UV. Wrapped taps preserve tileability;
// flat/non-finite slopes and untrusted boundary stencils have zero influence and validity.
// Publishes FlowDirection with canonical Flow/FlowSmooth/Validity storage. No extra smoothing
// is authored here, so FlowSmooth aliases Flow. Does not trace, compose or modify the source.
FPublishedField AddNativeFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	int32 SourceChildIndex, FRDGTextureRef Source,
	ENativeFlowSource SourceKind = ENativeFlowSource::SignedHeight);
}
