// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	// Shared Flow -> destination-UV binding for layer references and explicit generator inputs.
	// The producer's typed field stays intact; only the consumer's placement is traced.
	FRDGTextureRef AddReferencedFlowUVPass(
		FMixtormatComposeContext& Ctx,
		const FOutputReferenceRenderData& Reference,
		const FPublishedField& Field,
		int32 LayerIndex,
		int32 ChildIndex,
		const FScalarDriverRenderData* Drivers = nullptr,
		bool bCanonical = false);

	FRDGTextureRef BuildGeneratorPreCoordinates(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Owner);

	FPublishedField AddFlowFeatherPasses(
		FMixtormatComposeContext& Ctx,
		const FPublishedField& Source,
		const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask,
		bool bHasMask,
		int32 LayerIndex,
		int32 ChildIndex);

	FRDGTextureRef AddGeneratorFeatherPasses(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef Height,
		FRDGTextureRef Boundary,
		const FBehaviorRenderData& Behavior,
		FRDGTextureRef Mask,
		bool bHasMask,
		int32 LayerIndex,
		int32 ChildIndex);

	void ApplyGeneratorPostBehaviors(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Owner,
		FGeneratorBundle& Module,
		FRDGTextureRef PreviousRunningHeight,
		TSet<int32>* CompletedScopes = nullptr);
}
