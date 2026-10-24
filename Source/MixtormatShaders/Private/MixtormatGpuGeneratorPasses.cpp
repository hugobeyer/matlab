// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuGeneratorCommon.h"
#include "MixtormatGpuBehaviorPasses.h"
#include "MixtormatGpuGeneratorSublayerPasses.h"
#include "MixtormatGpuNoisePasses.h"
#include "MixtormatGpuSurfacingPasses.h"
#include "MixtormatGpuNativeFlowPasses.h"

#include "Algo/AnyOf.h"
#include "RenderGraphUtils.h"

namespace MixtormatGpuCompositor
{

void AddGeneratorLayerPasses(FMixtormatComposeContext& Ctx,
	FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer)
{
	const bool bHasSurfacing = Layer.Children.ContainsByPredicate([](const FChildRenderData& Child)
	{
		return Child.Type == EMixtormatLayerChildType::Surfacing;
	});
	if (!Layer.bEnabled || (!Layer.bGenerator && !bHasSurfacing)) { return; }
	FGeneratorBundle& Bundle = LayerCtx.GeneratorBundle;
	Bundle = FGeneratorBundle();
	TArray<FPendingColorRamp> PendingColorRamps;
	const FIntPoint Size = Ctx.Request.Resolution;
	FRDGTextureRef Debug = Ctx.OutputDebug[Ctx.Request.PublishedTargetIndex];
	FRDGTextureRef RunningHeight = Ctx.GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
		Size, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("Mixtormat.Generator.SignedRunningHeight"));
	AddClearUAVPass(Ctx.GraphBuilder, Ctx.GraphBuilder.CreateUAV(RunningHeight), FLinearColor::Black);
	const FRDGTextureRef BooleanNeutralHeight = RunningHeight;
	bool bActualHeightAuthored = false;
	const FChildRenderData* PendingBoolean = nullptr;
	auto FinishBoolean = [&]()
	{
		if (!PendingBoolean) { return; }
		const FChildRenderData& Parent = *PendingBoolean;
		const FBooleanStackRenderData& B = Parent.Boolean;
		if (B.bComplete)
		{
			const FRDGTextureRef* Target = LayerCtx.GeneratorModuleHeights.Find(B.TargetChildIndex);
			const FRDGTextureRef* Source = LayerCtx.GeneratorModuleHeights.Find(B.SourceChildIndex);
			if (Target && *Target && Source && *Source)
			{
				FGeneratorBundle Combined;
				AddBooleanStackPasses(Ctx, LayerCtx, Layer, Parent, *Target, *Source, &Combined);
				LayerCtx.GeneratorModuleHeights.Add(Parent.SourceChildIndex, Combined.Height);
				Ctx.PublishedFieldOutputs.Add(
					PublishedKey(Layer, Parent.SourceChildIndex, FName(TEXT("Height"))),
					FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned,
						Combined.Height, nullptr, nullptr, false});
				for (const TPair<FName, FRDGTextureRef>& Named : Combined.NamedMasks)
				{
					Ctx.PublishedMaskOutputs.Add(
						PublishedKey(Layer, Parent.SourceChildIndex, Named.Key), Named.Value);
				}
				if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
					FName(TEXT("Height")), LayerCtx.LayerIndex, Parent.SourceChildIndex))
				{
					AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Combined.Height, Debug, Size);
				}
				AddGeneratorModuleCombine(Ctx, RunningHeight, Combined, Parent, RunningHeight);
				bActualHeightAuthored = true;
			}
		}
		PendingBoolean = nullptr;
	};

	TSet<int32> HeightBlendOperandIndices;
	for (int32 I = 0; I < Layer.Children.Num(); ++I)
	{
		const FChildRenderData& Child = Layer.Children[I];
		if (Child.Type == EMixtormatLayerChildType::HeightBlend)
		{
			const int32 OperandIdx = Child.HeightBlend.SourceChildIndex;
			if (OperandIdx != INDEX_NONE && OperandIdx < Child.SourceChildIndex)
			{
				bool bHasOtherHeightProducer = false;
				for (int32 J = 0; J < I; ++J)
				{
					const FChildRenderData& Prior = Layer.Children[J];
					if (Prior.SourceChildIndex != OperandIdx
						&& (Prior.Type == EMixtormatLayerChildType::Generator || Prior.Type == EMixtormatLayerChildType::Boolean
							|| (Prior.Type == EMixtormatLayerChildType::Surfacing && Prior.ScopeOwnerSourceChildIndex == INDEX_NONE
								&& Prior.Surfacing.bEnabled
								&& (Prior.Surfacing.Type == EMixtormatSurfacingType::Rust
									? Prior.Surfacing.Rust.bWriteHeight && Prior.Surfacing.Rust.Amount > 0 && Prior.Surfacing.Rust.HeightScale != 0
									: Prior.Surfacing.Type == EMixtormatSurfacingType::Moss
									? Prior.Surfacing.Moss.bWriteHeight && Prior.Surfacing.Moss.Amount > 0 && Prior.Surfacing.Moss.HeightScale != 0
									: Prior.Surfacing.Skin.bWriteHeight && Prior.Surfacing.Skin.Amount > 0 && Prior.Surfacing.Skin.HeightScale != 0))))
					{
						bHasOtherHeightProducer = true;
						break;
					}
				}
				if (bHasOtherHeightProducer)
				{
					HeightBlendOperandIndices.Add(OperandIdx);
				}
			}
		}
	}

	// Preserve the legacy bottom-to-top ramp order within each uninterrupted batch. An active
	// Skin color writer is an ordering barrier: consume earlier ramps ONCE, never replay them
	// over the later surface at function exit. Zero-Amount Skin introduces no barrier.
	const auto FlushPendingColorRamps = [&]()
	{
		FlushPendingGeneratorColorRamps(Ctx, LayerCtx, Layer, PendingColorRamps);
	};

	TSet<int32> EvaluatedSkin;
	TSet<int32> CapturedColorRamps;
	TFunction<void(const FChildRenderData&, FRDGTextureRef, int32, FRDGTextureRef)> CaptureColorRamp;
	TSet<int32> CompletedGeneratorScopes;
	TMap<int32, FRDGTextureRef> SurfacingScopeGates;
	const auto FindChild = [&](int32 Index) -> const FChildRenderData*
	{
		return Layer.Children.FindByPredicate([Index](const FChildRenderData& Candidate)
		{
			return Candidate.SourceChildIndex == Index;
		});
	};
	const auto HasSurfacingBelow = [&](int32 OwnerIndex)
	{
		for (const FChildRenderData& Candidate : Layer.Children)
		{
			if (Candidate.Type != EMixtormatLayerChildType::Surfacing) { continue; }
			int32 Parent = Candidate.ScopeOwnerSourceChildIndex;
			while (Parent != INDEX_NONE)
			{
				if (Parent == OwnerIndex) { return true; }
				const FChildRenderData* Ancestor = FindChild(Parent);
				if (!Ancestor || Ancestor->ScopeOwnerSourceChildIndex >= Parent) { break; }
				Parent = Ancestor->ScopeOwnerSourceChildIndex;
			}
		}
		return false;
	};
	const auto ScopeGate = [&](int32 OwnerIndex)
	{
		if (FRDGTextureRef* Existing = SurfacingScopeGates.Find(OwnerIndex)) { return *Existing; }
		FRDGTextureRef Gate = HasScopedGeneratorMasks(Layer, OwnerIndex)
			? AddScopedFeatureMask(Ctx, LayerCtx, Layer, OwnerIndex, true) : nullptr;
		SurfacingScopeGates.Add(OwnerIndex, Gate);
		return Gate;
	};
	// LocalHeight contains a completed parent's delta; UpstreamDelta is outside that scope.
	// Descendants mutate only LocalHeight. The outer generator/Boolean combines it exactly once.
	TFunction<FRDGTextureRef(const FChildRenderData&, FRDGTextureRef, FRDGTextureRef, FRDGTextureRef, float)> EvaluateSkin;
	TFunction<FRDGTextureRef(const FChildRenderData&, FRDGTextureRef, FRDGTextureRef, FRDGTextureRef, float)> EvaluateSurfacing;
	TFunction<FRDGTextureRef(int32, FRDGTextureRef, FRDGTextureRef, FRDGTextureRef, float)> EvaluateSkinChildren;
	EvaluateSkinChildren = [&](int32 OwnerIndex, FRDGTextureRef LocalHeight, FRDGTextureRef UpstreamDelta,
		FRDGTextureRef ParentGate, float AncestorAmount)
	{
		for (const FChildRenderData& Nested : Layer.Children)
		{
			if (Nested.ScopeOwnerSourceChildIndex != OwnerIndex || Nested.SourceChildIndex <= OwnerIndex) { continue; }
			if (Nested.Type == EMixtormatLayerChildType::Surfacing)
			{
				LocalHeight = EvaluateSurfacing(Nested, LocalHeight, UpstreamDelta, ParentGate, AncestorAmount);
			}
			else if (Nested.Type == EMixtormatLayerChildType::HeightColorRamp)
			{
				FRDGTextureRef RampHeight = LocalHeight;
				if (UpstreamDelta)
				{
					FGeneratorBundle Local;
					Local.Height = LocalHeight;
					AddGeneratorModuleCombine(Ctx, UpstreamDelta, Local, Nested, RampHeight);
				}
				CaptureColorRamp(Nested, RampHeight, OwnerIndex, LocalHeight);
			}
			else if (Nested.Type == EMixtormatLayerChildType::Generator
				&& Nested.Generator.Type == EMixtormatGeneratorType::Noise && HasSurfacingBelow(Nested.SourceChildIndex))
			{
				const FRDGTextureRef NestedGate = ScopeGate(Nested.SourceChildIndex);
				if (!CompletedGeneratorScopes.Contains(Nested.SourceChildIndex))
				{
					// A height-less parent skips its height Behavior chain. Its enabled Noise owner
					// still exists for Skin: evaluate it here, never by enabling Behaviors on Skin.
					FMixtormatNoiseRenderData Noise;
					if (!MixtormatNoiseRenderStore().Find(
						FMixtormatNoiseRenderKey{Layer.LayerId, Nested.SourceChildIndex}, Noise)) { continue; }
					FGeneratorBundle Scoped;
					AddNoisePasses(Ctx, LayerCtx, Layer, Nested.SourceChildIndex, &Scoped,
						BuildGeneratorPreCoordinates(Ctx, LayerCtx, Layer, Nested), LocalHeight);
					if (Scoped.Height)
					{
						ApplyGeneratorPostBehaviors(Ctx, LayerCtx, Layer, Nested, Scoped, LocalHeight, &CompletedGeneratorScopes);
						Scoped.Height = AddSignedGeneratorHeightPasses(Ctx.GraphBuilder, Scoped.Height, Size,
							Nested.Generator.bNormalizeHeight, Nested.Generator.HeightScale,
							Nested.Generator.HeightBias, true, NestedGate, TEXT("Mixtormat.Surfacing.NoiseOwnerHeight"));
						AddGeneratorModuleCombine(Ctx, LocalHeight, Scoped, Nested, LocalHeight);
					}
					CompletedGeneratorScopes.Add(Nested.SourceChildIndex);
				}
				// Normally Noise already entered its parent's completed bundle. Do not add it twice.
				LocalHeight = EvaluateSkinChildren(Nested.SourceChildIndex, LocalHeight, UpstreamDelta,
					AddSurfacingGatePass(Ctx, ParentGate, NestedGate), AncestorAmount);
			}
		}
		return LocalHeight;
	};
	EvaluateSkin = [&](const FChildRenderData& Child, FRDGTextureRef LocalHeight,
		FRDGTextureRef UpstreamDelta, FRDGTextureRef ParentGate, float AncestorAmount)
	{
		if (!Child.Surfacing.bEnabled || (Child.Surfacing.Type != EMixtormatSurfacingType::Skin
			&& Child.Surfacing.Type != EMixtormatSurfacingType::Rust)
			|| EvaluatedSkin.Contains(Child.SourceChildIndex)) { return LocalHeight; }
		EvaluatedSkin.Add(Child.SourceChildIndex);
		AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
		if (!LayerCtx.LayerInputBC || !LayerCtx.LayerInputRAM) { AddLayerInputPass(Ctx, LayerCtx, Layer); }
		const bool bRust = Child.Surfacing.Type == EMixtormatSurfacingType::Rust;
		const float EffectiveAmount = (bRust ? Child.Surfacing.Rust.Amount : Child.Surfacing.Skin.Amount) * AncestorAmount;
		if (EffectiveAmount > 0.0f && (bRust ? Child.Surfacing.Rust.bWriteColor : Child.Surfacing.Skin.bWriteColor)) { FlushPendingColorRamps(); }
		FRDGTextureRef Gate = AddSurfacingGatePass(Ctx, ParentGate, ScopeGate(Child.SourceChildIndex));
		FRDGTextureRef AnalysisDelta = LocalHeight;
		if (UpstreamDelta)
		{
			FGeneratorBundle Local;
			Local.Height = LocalHeight;
			AddGeneratorModuleCombine(Ctx, UpstreamDelta, Local, Child, AnalysisDelta);
		}
		// Pass Through's private layer input already contains the composite below.
		// Adding it again doubles the curvature driving Skin's tissue and folds.
		const FRDGTextureRef Substrate = ResolveSubstrateHeight(Ctx, LayerCtx, AnalysisDelta,
			!Layer.bFillPassThrough && LayerCtx.LayerIndex > 0
				? Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)] : nullptr);
		const FSurfacingFields Raw = bRust
			? AddRustSurfacePasses(Ctx, LayerCtx, Layer, Child, Substrate, LayerCtx.ReferencedUV)
			: AddSkinSurfacePasses(Ctx, LayerCtx, Layer, Child, Substrate, LayerCtx.ReferencedUV);
		if (!Raw.Height) { return LocalHeight; }
		const FRDGTextureRef Before = LocalHeight;
		LocalHeight = ApplySurfacingFields(Ctx, LayerCtx, Child, Raw, Gate, LocalHeight, AncestorAmount);
		LocalHeight = EvaluateSkinChildren(Child.SourceChildIndex, LocalHeight, UpstreamDelta, Gate, EffectiveAmount);
		PublishSurfacingFields(Ctx, LayerCtx, Layer, Child, Raw, Gate, Before, LocalHeight, AncestorAmount);
		return LocalHeight;
	};
	TFunction<FRDGTextureRef(const FChildRenderData&, FRDGTextureRef, FRDGTextureRef, FRDGTextureRef, float)> EvaluateMoss;
	EvaluateMoss = [&](const FChildRenderData& Child, FRDGTextureRef LocalHeight,
		FRDGTextureRef UpstreamDelta, FRDGTextureRef ParentGate, float AncestorAmount)
	{
		if (!Child.Surfacing.bEnabled || Child.Surfacing.Type != EMixtormatSurfacingType::Moss
			|| EvaluatedSkin.Contains(Child.SourceChildIndex)) { return LocalHeight; }
		EvaluatedSkin.Add(Child.SourceChildIndex);
		if (!LayerCtx.LayerInputBC || !LayerCtx.LayerInputRAM) { AddLayerInputPass(Ctx, LayerCtx, Layer); }
		const FMixtormatMoss& Mo = Child.Surfacing.Moss;
		if (Mo.Amount * AncestorAmount > 0.0f && Mo.bWriteColor) { FlushPendingColorRamps(); }
		FRDGTextureRef Gate = AddSurfacingGatePass(Ctx, ParentGate, ScopeGate(Child.SourceChildIndex));
		FRDGTextureRef AnalysisDelta = LocalHeight;
		if (UpstreamDelta)
		{
			FGeneratorBundle Local;
			Local.Height = LocalHeight;
			AddGeneratorModuleCombine(Ctx, UpstreamDelta, Local, Child, AnalysisDelta);
		}
		// Pass Through already carries the lower composite in LayerInputHeight.
		// Use it once for both growth analysis and the supported canopy resolve.
		const FRDGTextureRef BelowHeight = !Layer.bFillPassThrough && LayerCtx.LayerIndex > 0
			? Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)] : nullptr;
		FRDGTextureRef RawHeight = AddMossPasses(Ctx, LayerCtx, Layer, Child, AnalysisDelta,
			BelowHeight, nullptr, LayerCtx.ReferencedUV);
		if (!RawHeight) { return LocalHeight; }
		const auto* Fields = LayerCtx.GeneratorFields.Find(Child.SourceChildIndex);
		if (!Fields || Fields->Num() < 8) { return LocalHeight; }
		const bool bRawPreview = Mo.Preview != EMixtormatMossPreview::Moss;
		FSurfacingFields Raw;
		Raw.Height = AddSignedGeneratorHeightPasses(Ctx.GraphBuilder, RawHeight, Size,
			bRawPreview ? false : Mo.bNormalizeHeight, bRawPreview ? 1.0f : Mo.HeightScale,
			0.0f, false, nullptr, TEXT("Mixtormat.Moss.SignedHeight"));
		Raw.Height = AddMossCanopyResolvePass(Ctx, LayerCtx, Child,
			AnalysisDelta, BelowHeight, Raw.Height, (*Fields)[1], Gate);
		Raw.MaterialCoverage = (*Fields)[1];
		Raw.Color = (*Fields)[4];
		Raw.RAM = (*Fields)[5];
		Raw.DetailNormal = (*Fields)[6];
		Raw.Optics = (*Fields)[4];
		Raw.FuzzColor = (*Fields)[4];
		Raw.Subsurface = (*Fields)[4];
		const FRDGTextureRef Before = LocalHeight;
		LocalHeight = ApplySurfacingFields(Ctx, LayerCtx, Child, Raw, Gate, LocalHeight, AncestorAmount);
		LocalHeight = EvaluateSkinChildren(Child.SourceChildIndex, LocalHeight, UpstreamDelta, Gate, Mo.Amount * AncestorAmount);
		// Publish Moss surfacing outputs (Height/Color/Roughness/Normal + growth fields).
		const auto Publish = [&](FName Name, EMixtormatPublishedFieldKind Kind, FRDGTextureRef Tex)
		{
			if (Tex) { Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name),
				FPublishedField{Kind, Tex, nullptr, nullptr, false}); }
		};
		Publish(FName(TEXT("Height")), EMixtormatPublishedFieldKind::ScalarSigned, Raw.Height);
		Publish(FName(TEXT("Color")), EMixtormatPublishedFieldKind::Color, (*Fields)[4]);
		Publish(FName(TEXT("Roughness")), EMixtormatPublishedFieldKind::Scalar01, (*Fields)[7]);
		Publish(FName(TEXT("Normal")), EMixtormatPublishedFieldKind::Color, (*Fields)[6]);
		Publish(FName(TEXT("GrowthCoverage")), EMixtormatPublishedFieldKind::Scalar01, (*Fields)[1]);
		Publish(FName(TEXT("GrowthAge")), EMixtormatPublishedFieldKind::Scalar01, (*Fields)[2]);
		Publish(FName(TEXT("RegionIds")), EMixtormatPublishedFieldKind::RegionIds, (*Fields)[3]);
		Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GrowthCoverage"))), (*Fields)[1]);
		Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GrowthAge"))), (*Fields)[2]);
		return LocalHeight;
	};
	EvaluateSurfacing = [&](const FChildRenderData& Child, FRDGTextureRef LocalHeight,
		FRDGTextureRef UpstreamDelta, FRDGTextureRef ParentGate, float AncestorAmount)
	{
		return Child.Surfacing.Type == EMixtormatSurfacingType::Moss
			? EvaluateMoss(Child, LocalHeight, UpstreamDelta, ParentGate, AncestorAmount)
			: EvaluateSkin(Child, LocalHeight, UpstreamDelta, ParentGate, AncestorAmount);
	};

	// Capture owned ramps at their authored position relative to Skin, even though their
	// completed generator scope is evaluated before the flat list reaches the child rows.
	CaptureColorRamp = [&](const FChildRenderData& Child, FRDGTextureRef RunningHeight,
		int32 ScopeOwnerIndex, FRDGTextureRef ScopeHeight)
	{
		CaptureGeneratorColorRamp(Ctx, LayerCtx, Layer, Child, RunningHeight,
			ScopeOwnerIndex, ScopeHeight, CapturedColorRamps, PendingColorRamps);
	};

	// Modules compose in child order from signed zero; zero is the neutral procedural height.
	for (const FChildRenderData& Child : Layer.Children)
	{
		if (PendingBoolean && Child.SourceChildIndex > PendingBoolean->Boolean.LastChildIndex)
		{
			FinishBoolean();
		}
		if (Child.Type == EMixtormatLayerChildType::Boolean)
		{
			FinishBoolean();
			PendingBoolean = &Child;
			continue;
		}
		const bool bBooleanOperand = PendingBoolean
			&& Child.Type == EMixtormatLayerChildType::Generator
			&& Child.ScopeOwnerSourceChildIndex == PendingBoolean->SourceChildIndex
			&& (Child.SourceChildIndex == PendingBoolean->Boolean.TargetChildIndex
				|| Child.SourceChildIndex == PendingBoolean->Boolean.SourceChildIndex);
		const FRDGTextureRef OperandRunningHeight = bBooleanOperand ? BooleanNeutralHeight : RunningHeight;
		if (Child.Type == EMixtormatLayerChildType::Surfacing)
		{
			// Scoped Skin is evaluated by its completed owner, not once more in the flat list.
			if (Child.ScopeOwnerSourceChildIndex == INDEX_NONE)
			{
				if (HeightBlendOperandIndices.Contains(Child.SourceChildIndex))
				{
					// Publish the isolated operand; Height Blend owns its eventual height contribution.
					EvaluateSurfacing(Child, BooleanNeutralHeight, RunningHeight, nullptr, 1.0f);
				}
				else
				{
					const FRDGTextureRef Before = RunningHeight;
					RunningHeight = EvaluateSurfacing(Child, RunningHeight, nullptr, nullptr, 1.0f);
					bActualHeightAuthored |= RunningHeight != Before;
				}
			}
			continue;
		}
		// Generator sublayers rewrite the running signed height in place (or publish colour).
		if (Child.Type == EMixtormatLayerChildType::HeightBlend)
		{
			// Missing sources retain the operation's neutral behavior rather than reading A twice.
			FRDGTextureRef Source = RunningHeight;
			bool bHasSource = false;
			if (Child.HeightBlend.SourceChildIndex != INDEX_NONE)
			{
				if (const FRDGTextureRef* Found = LayerCtx.GeneratorModuleHeights.Find(Child.HeightBlend.SourceChildIndex))
				{
					Source = *Found;
					bHasSource = true;
				}
			}
			FGeneratorHeightBlendRenderData BlendData = Child.HeightBlend;
			BlendData.SourceChildIndex = bHasSource ? Child.HeightBlend.SourceChildIndex : INDEX_NONE;
			RunningHeight = AddGeneratorHeightBlendPass(Ctx, RunningHeight, Source, BlendData);
			if (bHasSource) { bActualHeightAuthored = true; }
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightCurve)
		{
			RunningHeight = AddGeneratorHeightCurvePass(Ctx, RunningHeight, Child.HeightCurve);
			continue;
		}
		if (Child.Type == EMixtormatLayerChildType::HeightColorRamp)
		{
			CaptureColorRamp(Child, RunningHeight, INDEX_NONE, nullptr);
			continue;
		}
		if (Child.Type != EMixtormatLayerChildType::Generator) { continue; }
		if (Child.ScopeOwnerSourceChildIndex != INDEX_NONE && !bBooleanOperand) { continue; }
		// Only already-published maps may feed groups/references before this module.
		AddReadyRegionIdPasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, false);
		FGeneratorBundle Module;
		// Freeze the parent's child-mask dependency before publishing its own native fields.
		if (HasSurfacingBelow(Child.SourceChildIndex)) { ScopeGate(Child.SourceChildIndex); }
		const FRDGTextureRef PreUV = BuildGeneratorPreCoordinates(Ctx, LayerCtx, Layer, Child);
		const FGeneratorPassInput Input{Child.Generator, Child.SourceChildIndex, PreUV};
		switch (Child.Generator.Type)
		{
		case EMixtormatGeneratorType::RockFormation:
			AddRockFormationPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Pebbles:
			AddPebblesPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Cracks:
			AddCracksPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::CliffStrata:
			AddCliffStrataPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::Mycelium:
			AddMyceliumPasses(Ctx, LayerCtx, Layer, Input, OperandRunningHeight,
				(LayerCtx.LayerIndex > 0) ? Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)] : nullptr,
				&Module);
			break;
		case EMixtormatGeneratorType::Scales:
			AddScalesPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::RootNetwork:
			AddRootNetworkPasses(Ctx, LayerCtx, Layer, Input, &Module);
			break;
		case EMixtormatGeneratorType::BooleanOps:
			AddBooleanCarvePasses(Ctx, LayerCtx, Layer, Input, OperandRunningHeight,
				Ctx.OutputHeight[1 - (LayerCtx.LayerIndex & 1)], &Module);
			break;
		case EMixtormatGeneratorType::Noise:
			// One dispatch, no solve: the field producer publishes its value, gradient and (for
			// Worley) cell IDs, and leaves the signed height for the shared contract below.
			AddNoisePasses(Ctx, LayerCtx, Layer, Child.SourceChildIndex, &Module, PreUV, OperandRunningHeight);
			break;
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::BooleanOps
			|| (!Module.Height && Child.Generator.Type != EMixtormatGeneratorType::Noise
				&& HasSurfacingBelow(Child.SourceChildIndex)))
		{
			// An enabled owner without native relief does not disable its Skin subtree.
			for (const auto& Named : Module.NamedMasks)
			{
				Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Named.Key), Named.Value);
			}
			if (HasSurfacingBelow(Child.SourceChildIndex))
			{
				const FRDGTextureRef Before = Module.Height ? Module.Height : BooleanNeutralHeight;
				FRDGTextureRef ParentGate = ScopeGate(Child.SourceChildIndex);
				// Boolean's mask gates its operation at FinishBoolean, not its input operands.
				const FRDGTextureRef After = EvaluateSkinChildren(Child.SourceChildIndex, Before,
					OperandRunningHeight, ParentGate, 1.0f);
				if (Module.Height || After != Before) { Module.Height = After; }
			}
			CompletedGeneratorScopes.Add(Child.SourceChildIndex);
			// No normalization/second mask pass: BooleanOps and Skin deltas are already resolved.
			if (Module.Height)
			{
				LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Module.Height);
				Ctx.PublishedFieldOutputs.Add(
					PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Height"))),
					FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Module.Height, nullptr, nullptr, false});
				if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::SignedDistance,
					FName(TEXT("Height")), LayerCtx.LayerIndex, Child.SourceChildIndex))
				{
					AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Module.Height, Debug, Size);
				}
				if (!bBooleanOperand && !HeightBlendOperandIndices.Contains(Child.SourceChildIndex))
				{
					AddGeneratorModuleCombine(Ctx, RunningHeight, Module, Child, RunningHeight);
					bActualHeightAuthored = true;
				}
			}
			continue;
		}
		if (!Module.Height && Child.Generator.Type != EMixtormatGeneratorType::Noise) { continue; }
		// A Noise module with Write Height OFF produces no height: it contributes nothing to the
		// running height, runs no normalization, and its scoped Behaviors -- which all modify
		// relief -- have nothing to act on. Its Value/Gradient/FlowDirection/GeneratedFlow
		// publications and Region IDs still run below (P0 contract section 2).
		const bool bNoiseHeightless = Child.Generator.Type == EMixtormatGeneratorType::Noise
			&& !Module.Height;
		// Publish raw generator masks before any owned Behavior may consume them.
		for (const TPair<FName, FRDGTextureRef>& Mask : Module.NamedMasks)
		{
			Ctx.PublishedMaskOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, Mask.Key), Mask.Value);
		}
		// One authored-order pass processes producers and height operations on the native field.
		if (!bNoiseHeightless)
		{
			// Seed native Flow before the ordered Behavior chain, never from its
			// already-modified result. Noise's completed diagnostic is still published
			// separately after normalization; it is not composed back into working Flow.
			const bool bNeedsWorkingFlow = Algo::AnyOf(Layer.Children,
				[&Child](const FChildRenderData& Candidate)
				{
					if (Candidate.ScopeOwnerSourceChildIndex != Child.SourceChildIndex) { return false; }
					if (Candidate.Type == EMixtormatLayerChildType::Generator)
					{
						return Candidate.Generator.Type == EMixtormatGeneratorType::Noise;
					}
					if (Candidate.Type != EMixtormatLayerChildType::Behavior) { return false; }
					const FBehaviorRenderData& Behavior = Candidate.Behavior;
					if (Behavior.DirectionOrigin == EMixtormatBehaviorFieldOrigin::None
						&& (Behavior.Type == EMixtormatBehaviorType::Warp
							|| Behavior.Type == EMixtormatBehaviorType::Deform)) { return true; }
					if (Behavior.Stage != EMixtormatBehaviorStage::PostGeneration) { return false; }
					return Behavior.Type == EMixtormatBehaviorType::Carve
						|| (Behavior.Type == EMixtormatBehaviorType::Deform && Behavior.bUseTracedFlow)
						|| (Behavior.Type == EMixtormatBehaviorType::Warp
							&& (Behavior.Flow.ModeMix > 0.0f || Behavior.Flow.SettingsDrivers[8].bEnabled));
				});
			const bool bNativeFlowGenerator =
				Child.Generator.Type == EMixtormatGeneratorType::Cracks
				|| Child.Generator.Type == EMixtormatGeneratorType::RockFormation
				|| Child.Generator.Type == EMixtormatGeneratorType::Pebbles
				|| Child.Generator.Type == EMixtormatGeneratorType::CliffStrata
				|| Child.Generator.Type == EMixtormatGeneratorType::RootNetwork
				|| Child.Generator.Type == EMixtormatGeneratorType::Scales;
			// Native Flow is an available typed output, not an unconditional GPU job.
			// Geometry fields can be cached even when they are large; the derived Flow
			// textures are transient and currently have no node-cache slot. Demand only
			// when a consumer, explicit output reference, or selected preview needs them.
			const bool bFlowSnapshotDemanded = Ctx.PublishedFieldDemand.Contains(PublishedKey(
				Layer, Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow));
			bool bColorRampNeedsFlow = false;
			for (const FChildRenderData& OtherChild : Layer.Children)
			{
				if (OtherChild.Type == EMixtormatLayerChildType::HeightColorRamp)
				{
					const FGeneratorHeightColorRampRenderData& Ramp = OtherChild.HeightColorRamp;
					const bool bTargetMatch = (Ramp.SourceChildIndex == Child.SourceChildIndex) || (Ramp.SourceChildIndex == INDEX_NONE);
					const bool bUsesFlow = (Ramp.Source == static_cast<int32>(EMixtormatColorRampSource::FlowMagnitude))
						|| (Ramp.Source == static_cast<int32>(EMixtormatColorRampSource::FlowAngle))
						|| (FMath::Abs(Ramp.FlowModulation) > 1.0e-5f);
					if (bTargetMatch && bUsesFlow)
					{
						bColorRampNeedsFlow = true;
						break;
					}
				}
			}
			const bool bNativeFlowDemanded = bNeedsWorkingFlow || bFlowSnapshotDemanded || bColorRampNeedsFlow
				|| Ctx.PublishedFieldDemand.Contains(PublishedKey(
					Layer, Child.SourceChildIndex, FName(TEXT("FlowDirection"))))
				|| IsChildOutputPreviewTarget(Ctx.Request,
					EMixtormatPreviewOutputKind::FlowDirection,
					FName(TEXT("FlowDirection")), LayerCtx.LayerIndex, Child.SourceChildIndex);
			if (bNativeFlowGenerator && bNativeFlowDemanded)
			{
				// Geometry-derived native Flow before Behaviors; one seed, never twice.
				// Rock/Pebbles/Cracks use signed boundaries; Cliff uses native height.
				const bool bHasBoundary = Module.BoundaryField
					&& Module.BoundaryField->Desc.Format == PF_G32R32F;
				AddNativeFlowPass(Ctx, Layer,
					Child.SourceChildIndex, bHasBoundary ? Module.BoundaryField : Module.Height,
					bHasBoundary ? ENativeFlowSource::SignedBoundary : ENativeFlowSource::SignedHeight,
					bHasBoundary ? Module.RegionIds : nullptr, &Child.Generator.NativeFlow, Module.Height);
			}
			else if (bNeedsWorkingFlow || bFlowSnapshotDemanded || bColorRampNeedsFlow)
			{
				AddNativeFlowPass(Ctx, Layer, Child.SourceChildIndex, Module.Height);
			}
			ApplyGeneratorPostBehaviors(Ctx, LayerCtx, Layer, Child, Module, OperandRunningHeight, &CompletedGeneratorScopes);
			// Normalize the unmasked Noise native height, then apply its scoped gate to the
			// resolved signed result. Gating before centring turns masked zeros into negative height.
			// Other generators keep the historical zero-preserving max-abs contract.
			const FRDGTextureRef NoiseHeightGate =
				Child.Generator.Type == EMixtormatGeneratorType::Noise
				&& HasScopedGeneratorMasks(Layer, Child.SourceChildIndex)
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Child.SourceChildIndex, true)
					: nullptr;
			const bool bRawMossPreview = false;
			Module.Height = AddSignedGeneratorHeightPasses(
				Ctx.GraphBuilder, Module.Height, Size,
				bRawMossPreview ? false : Child.Generator.bNormalizeHeight,
				bRawMossPreview ? 1.0f : Child.Generator.HeightScale,
				Child.Generator.HeightBias,
				Child.Generator.Type == EMixtormatGeneratorType::Noise,
				NoiseHeightGate,
				TEXT("Mixtormat.Generator.SignedHeight"));

			if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
			{
				AddNativeFlowPass(Ctx, Layer, Child.SourceChildIndex, Module.Height);
			}
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
		{
			// Direct Feather owns this Noise module's typed Flow outputs as well
			// as its native height. This must run AFTER AddNativeFlowPass, which
			// replaces FlowDirection when Write Height is enabled. Otherwise
			// Flow preview and consumers bypass the child's filter silently.
			for (const FChildRenderData& Filter : Layer.Children)
			{
				if (Filter.Type == EMixtormatLayerChildType::Blur
					&& Filter.ScopeOwnerSourceChildIndex == Child.SourceChildIndex
					&& Filter.Blur.BlursAnything() && Filter.Blur.Target != EMixtormatBlurTarget::Height)
				{
					for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
					{
						const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
						const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
						if (!Field || !Field->IsComplete() || Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
						const FPublishedField Source = *Field;
						const FPublishedField Filtered = AddOwnedFlowBlurPass(Ctx, LayerCtx, Layer, Filter.Blur, Source);
						Ctx.PublishedFieldOutputs.Add(Key, Filtered);
						if (Name == FName(TEXT("GeneratedFlow"))
							|| !Ctx.PublishedFieldOutputs.Contains(PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("GeneratedFlow")))))
						{
							Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Filter.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
							if (bNoiseHeightless)
							{
								Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Filter.SourceChildIndex,
									MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
							}
						}
					}
					continue;
				}
				if (Filter.Type != EMixtormatLayerChildType::Behavior
					|| Filter.Behavior.Type != EMixtormatBehaviorType::Feather
					|| Filter.ScopeOwnerSourceChildIndex != Child.SourceChildIndex
					|| Filter.Behavior.GeneratorChildIndex != Child.SourceChildIndex) { continue; }
				const bool bMask = HasScopedGeneratorMasks(Layer, Filter.SourceChildIndex);
				FRDGTextureRef Gate = bMask
					? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Filter.SourceChildIndex, true)
					: nullptr;
				for (const FName Name : {FName(TEXT("GeneratedFlow")), FName(TEXT("FlowDirection"))})
				{
					const FPublishedFieldKey Key = PublishedKey(Layer, Child.SourceChildIndex, Name);
					const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(Key);
					if (!Field || !Field->IsComplete()
						|| Field->Kind != EMixtormatPublishedFieldKind::Flow) { continue; }
					const FPublishedField Filtered = AddFlowFeatherPasses(Ctx, *Field,
						Filter.Behavior, Gate, bMask, LayerCtx.LayerIndex, Filter.SourceChildIndex);
					Ctx.PublishedFieldOutputs.Add(Key, Filtered);
					if (Name == FName(TEXT("GeneratedFlow"))
						|| !Ctx.PublishedFieldOutputs.Contains(PublishedKey(Layer,
							Child.SourceChildIndex, FName(TEXT("GeneratedFlow")))))
					{
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Filter.SourceChildIndex, FName(TEXT("FlowDirection"))), Filtered);
						if (bNoiseHeightless)
						{
							Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
								Filter.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Filtered);
						}
					}
				}
			}
		}
		if (bNoiseHeightless)
		{
			FMixtormatNoiseRenderData Noise;
			if (MixtormatNoiseRenderStore().Find(
				FMixtormatNoiseRenderKey{Layer.LayerId, Child.SourceChildIndex}, Noise))
			{
				const FName ResultName = Noise.bWriteFlow
					? FName(TEXT("GeneratedFlow")) : FName(TEXT("FlowDirection"));
				if (const FPublishedField* Result = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, ResultName)))
				{
					if (Result->IsComplete())
					{
						const FPublishedField Snapshot = *Result;
						Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer,
							Child.SourceChildIndex, MixtormatDiagnosticOutputs::AccumulatedFlow), Snapshot);
					}
				}
			}
		}
		if (Child.Generator.Type == EMixtormatGeneratorType::Noise)
		{
			for (const FName Name : {FName(TEXT("Value")), FName(TEXT("Gradient")), FName(TEXT("FlowDirection")), FName(TEXT("GeneratedFlow"))})
			{
				const FPublishedField* Field = Ctx.PublishedFieldOutputs.Find(
					PublishedKey(Layer, Child.SourceChildIndex, Name));
				if (!Field) { continue; }

				// Keep authored mask references to Value working, while Copy preserves its typed payload.
				if (Name == FName(TEXT("Value")))
				{
					Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Name), Field->Texture);
				}
			}
		}
		// Native ID/mask publications are complete before owned ramps consume them; Skin changes
		// height/material channels only. Height publication follows the completed Skin scope below.
		// The only publication point: every module publishes under its own child index, after its
		// flow. The layer's default IDs are therefore the last module that produced any.
		if (Module.RegionIds)
		{
			PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Module.RegionIds);
			if (IsChildOutputPreviewTarget(Ctx.Request, EMixtormatPreviewOutputKind::RegionIds,
				NAME_None, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewRegionIdsBlitPass(Ctx.GraphBuilder, Module.RegionIds, nullptr, Debug, Size);
			}
		}
		for (const TPair<FName, FRDGTextureRef>& Mask : Module.NamedMasks)
		{
			Ctx.PublishedMaskOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Mask.Key), Mask.Value);
			if (Child.Generator.Type == EMixtormatGeneratorType::Mycelium)
			{
				Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Mask.Key),
					FPublishedField{EMixtormatPublishedFieldKind::Scalar01, Mask.Value, nullptr, nullptr, false});
			}
			const bool bDistance = Mask.Key == FName(TEXT("RockEdgeDistance"))
				|| Mask.Key == FName(TEXT("PebbleEdgeDistance"))
				|| Mask.Key == FName(TEXT("RootEdgeDistance"));
			// Preserve legacy mask consumers while publishing the declared signed
			// UV-distance metric for typed Carve / Deposit consumers.
			if (bDistance && Mask.Value && Mask.Value->Desc.Format == PF_R32_FLOAT)
			{
				Ctx.PublishedFieldOutputs.Add(PublishedKey(Layer, Child.SourceChildIndex, Mask.Key),
					FPublishedField{EMixtormatPublishedFieldKind::SDF, Mask.Value, nullptr, nullptr, false});
			}
			if (bDistance && IsChildOutputPreviewTarget(Ctx.Request,
				EMixtormatPreviewOutputKind::SignedDistance,
				Mask.Key, LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewSignedDistanceBlitPass(Ctx.GraphBuilder, Mask.Value, (float)Size.X, Debug, Size);
			}
		}

		if (HasSurfacingBelow(Child.SourceChildIndex))
		{
			const FRDGTextureRef Before = Module.Height ? Module.Height : BooleanNeutralHeight;
			// Boolean's mask gates its operation at FinishBoolean, not its input operands.
			const FRDGTextureRef After = EvaluateSkinChildren(Child.SourceChildIndex, Before,
				OperandRunningHeight, ScopeGate(Child.SourceChildIndex), 1.0f);
			if (Module.Height || After != Before) { Module.Height = After; }
		}
		CompletedGeneratorScopes.Add(Child.SourceChildIndex);
		// A height-less owner may acquire real height from Skin; otherwise it remains height-less.
		if (Module.Height)
		{
			LayerCtx.GeneratorModuleHeights.Add(Child.SourceChildIndex, Module.Height);
			Ctx.PublishedFieldOutputs.Add(
				PublishedKey(Layer, Child.SourceChildIndex, FName(TEXT("Height"))),
				FPublishedField{EMixtormatPublishedFieldKind::ScalarSigned, Module.Height, nullptr, nullptr, false});
			if (HasSurfacingBelow(Child.SourceChildIndex) && IsChildOutputPreviewTarget(Ctx.Request,
				EMixtormatPreviewOutputKind::SignedDistance, FName(TEXT("Height")), LayerCtx.LayerIndex, Child.SourceChildIndex))
			{
				AddDebugPreviewSignedHeightBlitPass(Ctx.GraphBuilder, Module.Height, Debug, Size);
			}
			if (!bBooleanOperand && !HeightBlendOperandIndices.Contains(Child.SourceChildIndex))
			{
				AddGeneratorModuleCombine(Ctx, RunningHeight, Module, Child, RunningHeight);
				bActualHeightAuthored = true;
			}
		}
	}
	FinishBoolean();

	FlushPendingColorRamps();
	if (!RunningHeight || !bActualHeightAuthored) { return; }
	Bundle.Height = RunningHeight;
	if (LayerCtx.LayerInputHeight)
	{
		FGeneratorBundle DeltaBundle;
		DeltaBundle.Height = RunningHeight;
		FChildRenderData DummyChild;
		FRDGTextureRef CombinedLayerHeight = nullptr;
		AddGeneratorModuleCombine(
			Ctx, LayerCtx.LayerInputHeight, DeltaBundle, DummyChild, CombinedLayerHeight);
		LayerCtx.LayerInputHeight = CombinedLayerHeight;
	}
}

}
