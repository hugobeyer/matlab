// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatGpuGaussianBlur.h"
#include "MixtormatGpuDirectionalBlur.h"

namespace MixtormatGpuCompositor
{
	FRDGTextureRef AddOwnedBlurPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FBlurRenderData& Blur,
		FRDGTextureRef Source,
		const MixtormatGaussianBlur::FSettings& Settings,
		const MixtormatGaussianBlur::EField Field,
		FRDGTextureRef Mask,
		FRDGTextureRef SourceValidity)
	{
		if (!Source || !Blur.BlursAnything() || Settings.Amount <= 0.0f) { return Source; }
		if (!Mask && Blur.SourceChildIndex != INDEX_NONE
			&& Layer.Children.ContainsByPredicate([&Blur](const FChildRenderData& Child)
			{
				return IsScopedMaskChild(Child, Blur.SourceChildIndex);
			}))
		{
			Mask = AddScopedFeatureMask(Ctx, LayerCtx, Layer, Blur.SourceChildIndex, true);
		}
		FRDGTextureRef DirectionFlow = nullptr;
		FRDGTextureRef DirectionValidity = nullptr;
		if (Blur.Mode != EMixtormatBlurMode::Gaussian
			&& Blur.DirectionSource == EMixtormatBlurDirectionSource::Flow
			&& Blur.DirectionSourceKey.LayerId.IsValid()
			&& Blur.DirectionSourceKey.ChildIndex != INDEX_NONE
			&& !Blur.DirectionSourceKey.Output.IsNone())
		{
			const FPublishedField* Direction = Ctx.PublishedFieldOutputs.Find(Blur.DirectionSourceKey);
			if (Direction && Direction->Kind == EMixtormatPublishedFieldKind::Flow && Direction->IsComplete())
			{
				DirectionFlow = Direction->Texture;
				DirectionValidity = Direction->Validity;
			}
		}
		return MixtormatDirectionalBlur::AddPasses(Ctx.GraphBuilder, Source, Blur, Settings,
			Field, Mask, DirectionFlow, DirectionValidity, SourceValidity);
	}

	FPublishedField AddOwnedFlowBlurPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FBlurRenderData& Blur,
		const FPublishedField& Source,
		FRDGTextureRef Mask)
	{
		// Scoped mask passes can publish fields and invalidate registry-backed references.
		FPublishedField Result = Source;
		if (Result.Kind != EMixtormatPublishedFieldKind::Flow || !Result.IsComplete()) { return Result; }
		MixtormatGaussianBlur::FSettings Settings;
		Settings.RadiusX = Blur.GetRadiusX();
		Settings.RadiusY = Blur.GetRadiusY();
		Settings.Amount = Blur.Amount;
		Result.Texture = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Blur, Result.Texture, Settings,
			MixtormatGaussianBlur::EField::ValidFlow, Mask, Result.Validity);
		Result.FlowSmooth = Result.Texture;
		return Result;
	}

	void AddLayerSurfaceBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		// The pipeline binds isolated, fully prepared layer channels here. Never
		// dispatch against the accumulated output: coverage/opacity belong to merge.
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		for (const FLayerSurfaceBlurRenderData& Blur : Layer.SurfaceBlurs)
		{
			const bool bHasMask = Layer.Children.ContainsByPredicate([&Blur](const FChildRenderData& Child)
			{
				return IsScopedMaskChild(Child, Blur.SourceChildIndex);
			});
			const FRDGTextureRef Mask = bHasMask
				? AddScopedFeatureMask(Ctx, LayerCtx, Layer, Blur.SourceChildIndex, true) : nullptr;
			MixtormatGaussianBlur::FSettings Settings;
			Settings.RadiusX = Blur.GetRadiusX();
			Settings.RadiusY = Blur.GetRadiusY();
			if (Blur.BaseColorWeight > 0.0f)
			{
				Settings.Amount = Blur.Amount * Blur.BaseColorWeight;
				Settings.ChannelWeights = FVector4f(1.0f, 1.0f, 1.0f, 0.0f);
				Ctx.OutputBC[WriteIndex] = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Blur,
					Ctx.OutputBC[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4, Mask);
			}
			if (Blur.NormalWeight > 0.0f)
			{
				Settings.Amount = Blur.Amount * Blur.NormalWeight;
				Ctx.OutputN[WriteIndex] = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Blur,
					Ctx.OutputN[WriteIndex], Settings, MixtormatGaussianBlur::EField::EncodedNormal, Mask);
			}
			if (Blur.RoughnessWeight > 0.0f || Blur.AOWeight > 0.0f
				|| Blur.MetallicWeight > 0.0f || Blur.F0Weight > 0.0f)
			{
				Settings.Amount = Blur.Amount;
				Settings.ChannelWeights = FVector4f(
					Blur.RoughnessWeight, Blur.AOWeight, Blur.MetallicWeight, Blur.F0Weight);
				Ctx.OutputRAM[WriteIndex] = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Blur,
					Ctx.OutputRAM[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4, Mask);
			}
			if (Blur.HeightWeight > 0.0f)
			{
				Settings.Amount = Blur.Amount * Blur.HeightWeight;
				Ctx.OutputHeight[WriteIndex] = AddOwnedBlurPass(Ctx, LayerCtx, Layer, Blur,
					Ctx.OutputHeight[WriteIndex], Settings, MixtormatGaussianBlur::EField::Scalar, Mask);
			}
		}
	}
}
