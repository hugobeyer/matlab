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
	const FLayerRenderData& Layer, int32 SourceChildIndex, FGeneratorBundle* Bundle,
	FRDGTextureRef PreUV = nullptr, FRDGTextureRef PrecedingHeight = nullptr);

// Source-local R32_FLOAT coverage. No generator publication/height or mask placement/shaping.
FRDGTextureRef AddNoiseMaskPass(FMixtormatComposeContext& Ctx, const FMixtormatNoise& Noise);

// Distinct R32_FLOAT coverage; preserves raw Value and any publication. Non-finite becomes zero.
FRDGTextureRef AddNoiseCoveragePass(FMixtormatComposeContext& Ctx, FRDGTextureRef Value, bool bSigned);

// Explicit downhill transport from completed destination-space height, with flat-slope validity.
void AddNoiseFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	int32 SourceChildIndex, FRDGTextureRef Height);

// P1 generated Flow: the weighted Height/Slope/Curl/Constant MODE field in the canonical
// layout (FIELD_CONTRACT_P0.md section 1), published under "GeneratedFlow". OwnHeight is the
// module's native signed field (the Height basis). The Slope basis is compiled in but disabled
// with Slope sampling the height snapshot preceding this generator in authored order.
FRDGTextureRef AddNoiseGeneratedFlowPass(FMixtormatComposeContext& Ctx, const FLayerRenderData& Layer,
	int32 SourceChildIndex, const FMixtormatNoiseRenderData& Noise, FRDGTextureRef OwnHeight,
	FRDGTextureRef PrecedingHeight);

// Reusable Add/Mix composition over two canonical Flow fields (FIELD_CONTRACT_P0.md section 4):
//   MixWeight = saturate(Mix * Mask); AddWeight = Add * Mask;
//   FlowOut = lerp(FlowIn, Generated, MixWeight) + Generated * AddWeight.
// Mask may be null (weight 1). Writes the composed vector and its validity texture. P1 ships
// the operation; P2 connects it to the ordered working-field accumulation.
FRDGTextureRef AddNoiseFlowComposePass(FMixtormatComposeContext& Ctx,
	FRDGTextureRef FlowIn, FRDGTextureRef Generated, FRDGTextureRef Mask,
	float Add, float Mix, FRDGTextureRef& OutValidity, const TCHAR* DebugName);
// Re-sample canonical accumulated Flow and validity after a bundle Distort.
// The tile-UV vector magnitude is preserved; only its spatial attachment changes.
FRDGTextureRef AddNoiseFlowTransportPass(FMixtormatComposeContext& Ctx,
	FRDGTextureRef Flow, FRDGTextureRef Validity, FRDGTextureRef Coordinates,
	FRDGTextureRef& OutValidity);

}
