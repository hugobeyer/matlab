// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	struct FPendingColorRamp
	{
		const FChildRenderData* Child = nullptr;
		FRDGTextureRef SourceHeight = nullptr;
		FRDGTextureRef SourceFlow = nullptr;
		FRDGTextureRef SourceUV = nullptr;
		FRDGTextureRef SourceIds = nullptr;
		FRDGTextureRef Gate = nullptr;
		FRDGTextureRef FilteredColor = nullptr;
		uint32 SourceMode = 0u;
		bool bHasGate = false;
	};

	FRDGTextureRef AddGeneratorHeightBlendPass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef RunningHeight,
		FRDGTextureRef SourceHeight,
		const FGeneratorHeightBlendRenderData& Blend);

	FRDGTextureRef AddGeneratorHeightCurvePass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef RunningHeight,
		const FGeneratorHeightCurveRenderData& Curve);

	FRDGTextureRef AddGeneratorHeightColorRampPass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef SourceHeight,
		FRDGTextureRef SourceFlow,
		FRDGTextureRef SourceUV,
		FRDGTextureRef SourceIds,
		uint32 SourceMode,
		FRDGTextureRef Gate,
		FRDGTextureRef PreviousColor,
		const FGeneratorHeightColorRampRenderData& Ramp,
		bool bOverrideBaseColor,
		const FVector4f& FillColor,
		bool bMapOnly = false);

	FRDGTextureRef AddGeneratorColorCompositePass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef MappedColor,
		FRDGTextureRef Gate,
		FRDGTextureRef PreviousColor,
		const FGeneratorHeightColorRampRenderData& Ramp,
		bool bOverrideBaseColor,
		const FVector4f& FillColor);

	void CaptureGeneratorColorRamp(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		FRDGTextureRef RunningHeight,
		int32 ScopeOwnerIndex,
		FRDGTextureRef ScopeHeight,
		TSet<int32>& CapturedColorRamps,
		TArray<FPendingColorRamp>& PendingColorRamps);

	void FlushPendingGeneratorColorRamps(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		TArray<FPendingColorRamp>& PendingColorRamps);
}
