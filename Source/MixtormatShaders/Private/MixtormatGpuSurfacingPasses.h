// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "RenderGraphFwd.h"

namespace MixtormatGpuCompositor
{
	struct FMixtormatComposeContext;
	struct FMixtormatLayerPassContext;
	struct FLayerRenderData;
	struct FChildRenderData;
	struct FGeneratorBundle;

	// Raw destination-space fields, never globally published/cached here.
	struct FSurfacingFields
	{
		FRDGTextureRef Height = nullptr;      // Signed displacement delta, already HeightScale-scaled.
		FRDGTextureRef MaterialCoverage = nullptr; // Optional color/RAM/optics footprint; height/detail normals already carry coverage.
		FRDGTextureRef Color = nullptr;       // Linear RGB albedo, alpha 1.
		FRDGTextureRef RAM = nullptr;         // Roughness, AO, 0, dielectric F0.
		FRDGTextureRef Optics = nullptr;      // F0, fuzz amount, fuzz roughness, SSS amount.
		FRDGTextureRef FuzzColor = nullptr;   // Linear RGB tint, alpha 1.
		FRDGTextureRef Subsurface = nullptr; // RGB mean free path in cm, alpha 0 (not albedo).
		FRDGTextureRef Pores = nullptr;       // Unit scalar pore cavity diagnostic.
		FRDGTextureRef Compression = nullptr; // Unit scalar curvature-driven compression cue.
		FRDGTextureRef DetailNormal = nullptr; // Moss leaf-detail normal (encoded RGB). Skin uses height-derived normals.
	};

	// Child.Surfacing must be the instance/binding-resolved, gather-sanitized Runtime payload.
	// SubstrateHeight is an immutable destination-space snapshot, not the running output UAV.
	// PreUV is an optional lifted, periodic destination->source coordinate map, BEFORE placement.
	// Height is a delta: the caller adds Amount * gate * Height when bWriteHeight is true.
	// This producer does not apply Amount/flags/gates. ApplySurfacingFields owns that operation.
	// Layer opacity/channel influence remain final-compositor responsibilities, never applied here.
	// No LayerCtx inputs are mutated by this raw producer; no normals or cache entries are made.
	// Null fields mean disabled/wrong subtype/missing substrate: skip rather than consume them.
	// AddGeneratorLayerPasses now owns ordered dispatch and completed scope publication.
	// Parent: FChildRenderData.Surfacing is an instance/binding-resolved FMixtormatSurfacing copy.
	// SSS preview reads Subsurface RGB directly as cm; never expose it as layer albedo input.
	// AnalysisCS has one UAV; FieldCS has eight. Every in-bounds texel is initialized by its pass.
	// Unresolved detail fades with the mapped footprint. Periods outside float's exact-integer
	// range (>= 2^24) are omitted, not retuned; there is no inherited Noise FBM 4096 artist cap.
	// Input height/PreUV seams and non-tile-compatible layer placement are not repaired here.
	// Full-precision RGB/MFP and height avoid half overflow; normalized RAM/Optics use RGBA16F.
	// Source-reviewed only: compilation, GPU timing, seam images and runtime integration unverified.
	FSurfacingFields AddSkinSurfacePasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV = nullptr);

	FSurfacingFields AddRustSurfacePasses(FMixtormatComposeContext& Ctx,
				FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
				const FChildRenderData& Child, FRDGTextureRef SubstrateHeight, FRDGTextureRef PreUV = nullptr);

	FRDGTextureRef AddMossPasses(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer, const FChildRenderData& Child, FRDGTextureRef RunningHeight,
		FRDGTextureRef BelowHeight, FGeneratorBundle* Bundle, FRDGTextureRef PreUV = nullptr);

	// Returns ScaledMossHeight unchanged for raw previews or inactive canopy support.
	FRDGTextureRef AddMossCanopyResolvePass(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FChildRenderData& Child,
		FRDGTextureRef AnalysisDelta, FRDGTextureRef BelowHeight, FRDGTextureRef ScaledMossHeight,
		FRDGTextureRef GrowthCoverage, FRDGTextureRef Gate);

			// Null gate is identity. Each owner's scoped masks enter the ancestry product once.
	FRDGTextureRef AddSurfacingGatePass(FMixtormatComposeContext& Ctx,
		FRDGTextureRef ParentGate, FRDGTextureRef LocalGate);
	// Mutates only enabled material inputs; returns additive signed running height. AncestorAmount
	// is the product of enclosing Skin Amounts, not layer opacity. Zero retains pointer identity.
	FRDGTextureRef ApplySurfacingFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FChildRenderData& Child, const FSurfacingFields& Fields, FRDGTextureRef Gate,
		FRDGTextureRef RunningHeight, float AncestorAmount = 1.0f);
	// Called after owned Skin descendants finish. Height is this scope's delta; Color/RAM/optics
	// are completed channel snapshots. Pores/Compression are this node's weighted diagnostics.
	void PublishSurfacingFields(FMixtormatComposeContext& Ctx, FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer, const FChildRenderData& Child, const FSurfacingFields& Raw,
		FRDGTextureRef Gate, FRDGTextureRef BeforeHeight, FRDGTextureRef AfterHeight, float AncestorAmount = 1.0f);

	// REQUIRED shared integration (outside this file's ownership):
	// - FChildRenderData must include MixtormatSurfacingTypes.h and own FMixtormatSurfacing Surfacing.
	//   Gather must exclude disabled/invalid ancestor scopes and sanitize the resolved copy.
	// - Call AddGeneratorLayerPasses for Surfacing-only layers too (HasGeneratorModules is the
	//   procedural-dispatch predicate, not a test that every producer has Generator type).
	// - Initialize LayerInputBC/RAM/Optics/FuzzColor/Subsurface to effective source values BEFORE
	//   ordered dispatch, including Fill, overrides, references and pass-through defaults. Preserve
	//   these at zero gate. Use float4 UAV-capable textures; physical Subsurface should be RGBA32F.
	// - Add FRDGTextureRef LayerInputOptics, LayerInputFuzzColor, LayerInputSubsurface (nullptr).
	// - Add/reset bool bSurfacingColor, bSurfacingRoughness, bSurfacingAO, bSurfacingF0,
	//   bSurfacingFuzz, bSurfacingSSS (false). They mean a channel has been resolved in LayerInput*,
	//   NOT occupancy. Do not reapply Fill overrides, Amount or scoped masks.
	// - Rust can write RAM.B; Skin/Moss preserve it. F0 writes both RAM.A and Optics.R. Fuzz writes Optics.GB and tint;
	//   SSS writes Optics.A and RGB MFP. No extra Below* fields are needed by these passes.
	// - FPublishedField::IsComplete must accept PF_A32B32G32R32F for Color (physical MFP/HDR).
	//   SSS remains preview-only through Runtime CanReferenceField; never remap its name to Color.
	// - Exact publications: Height (ScalarSigned); Color (Color albedo); Roughness, AO, F0, Fuzz,
	//   Pores, Compression (Scalar01); SSS (Color-format RGB MFP cm, preview-only, NOT albedo).
	//   Height is the completed scope delta; material diagnostics are completed input snapshots;
	//   Pores/Compression are this Skin's Amount/gate-weighted raw diagnostics, independent of flags.
	// - Final composite/bake and prefix-cache storage/restoration must carry optical inputs and
	//   all nine exact diagnostic publications. Skin has no local node-cache entry to restore.
	// - Generator/BooleanOps parents finish native Behaviors and normalization before Skin reads
	//   their substrate. Scoped Noise already in that bundle is not combined a second time.
	//   Owned ramps are captured relative to Skin in source order; each pending batch retains
	//   legacy reverse compositing order and is consumed once. A zero-Amount Skin is not a barrier.
	// - Boolean stack masks gate the Boolean HEIGHT OPERATION once, not its input operands.
	//   Boolean does not combine optical/material channels; those remain ordered Skin writes.
	// - Runtime ResolveSurfacingOutputSource anchors native Generator/Behavior gates (including
	//   nested Noise and mask/filter wrappers) to the outer native generator's start. It rejects
	//   Skin inside that unfinished scope. Skin/Color Ramp gates stop at their own ordered scope,
	//   so earlier completed Skin siblings remain available. Ancestor/self/forward reads fail.
	//   ResolvePublishedMaskSource and ValidateDependency share that validation; gather must use it.
	// - Behavior-on-Surfacing stays disabled. This dispatcher does not invent Flow/warp support.
}
