// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	// A module of a Generator layer: its payload and its child index in that layer. Publication
	// happens once per module in AddGeneratorLayerPasses, after flow.
	struct FGeneratorPassInput
	{
		const FGeneratorRenderData& Generator;
		int32 SourceChildIndex;
		FRDGTextureRef PreUV = nullptr;
	};

	// Every module sits in the Generator layer's UV placement, so the layer moves all of them.
	template<typename TParameters>
	void FillGeneratorPlacement(TParameters* P, const FLayerRenderData& Layer, FRDGTextureRef PreUV, FRDGTextureRef Fallback)
	{
		P->GeneratorLayer = 1u;
		P->GeneratorUVScale = FVector2f(Layer.Tiling * Layer.UVScaleX, Layer.Tiling * Layer.UVScaleY);
		P->GeneratorUVOffset = Layer.UVOffset;
		P->GeneratorUVRotation = Layer.Rotation;
		P->GeneratorUVFlipU = Layer.bFlipU ? 1u : 0u;
		P->GeneratorUVFlipV = Layer.bFlipV ? 1u : 0u;
		P->UsePreGenerationUV = PreUV ? 1u : 0u;
		P->PreGenerationUV = PreUV ? PreUV : Fallback;
	}

	int32 ResolveFeatherIterations(int32 Requested);
	FIntPoint DistanceSolveResolution(FIntPoint Resolution);

	FRDGTextureRef AddSignedGeneratorHeightPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Field,
		FIntPoint Size,
		bool bNormalize,
		float OutputScale,
		float HeightBias,
		bool bCenterNoise,
		FRDGTextureRef HeightGate,
		const TCHAR* Name);

	void AddGeneratorModuleCombine(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef RunningHeight,
		const FGeneratorBundle& Module,
		const FChildRenderData& Child,
		FRDGTextureRef& OutHeight);

	FRDGTextureRef RemapBundleField(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef Field,
		FRDGTextureRef WarpedUV,
		int32 Stage,
		FRDGTextureRef Boundary = nullptr,
		FRDGTextureRef SourceIds = nullptr,
		float InvalidDistance = 0.0f);

	void RemapGeneratorBundle(
		FMixtormatComposeContext& Ctx,
		FGeneratorBundle& Bundle,
		FRDGTextureRef WarpedUV);

	void RemapCompletedGeneratorBundle(
		FMixtormatComposeContext& Ctx,
		FGeneratorBundle& Bundle,
		FRDGTextureRef CompletedSignedHeight,
		FRDGTextureRef WarpedUV,
		bool bFilterHeight = false);

	void RemapGeneratorModuleOutputs(
		FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Owner,
		FGeneratorBundle& Module,
		FRDGTextureRef Coordinates,
		bool bFilterHeight = false);

	FRDGTextureRef PackScalarBoundary(FMixtormatComposeContext& Ctx, FRDGTextureRef Scalar);

	FRDGTextureRef ResolveSubstrateHeight(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight);

	// MixtormatGpuBooleanPasses.cpp
	void AddBooleanStackPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef TargetHeight,
		FRDGTextureRef SourceHeight,
		FGeneratorBundle* Bundle);

	void AddBooleanCarvePasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight,
		FGeneratorBundle* Bundle);

	// MixtormatGpuRockPasses.cpp
	FRDGTextureRef AddRockFormationPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr);

	FRDGTextureRef AddCracksPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr);

	FRDGTextureRef AddPebblesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr);

	FRDGTextureRef AddCliffStrataPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr);

	// MixtormatGpuOrganicGeneratorPasses.cpp
	FRDGTextureRef AddMyceliumPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight,
		FGeneratorBundle* Bundle);

	FRDGTextureRef AddScalesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle);

	FRDGTextureRef AddRootNetworkPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FGeneratorPassInput& Child,
		FGeneratorBundle* Bundle = nullptr);
}
