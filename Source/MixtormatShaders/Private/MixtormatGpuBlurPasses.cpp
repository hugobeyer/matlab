// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatGpuGaussianBlur.h"

namespace MixtormatGpuCompositor
{
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
				Ctx.OutputBC[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputBC[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4, Mask);
			}
			if (Blur.NormalWeight > 0.0f)
			{
				Settings.Amount = Blur.Amount * Blur.NormalWeight;
				Ctx.OutputN[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputN[WriteIndex], Settings, MixtormatGaussianBlur::EField::EncodedNormal, Mask);
			}
			if (Blur.RoughnessWeight > 0.0f || Blur.AOWeight > 0.0f
				|| Blur.MetallicWeight > 0.0f || Blur.F0Weight > 0.0f)
			{
				Settings.Amount = Blur.Amount;
				Settings.ChannelWeights = FVector4f(
					Blur.RoughnessWeight, Blur.AOWeight, Blur.MetallicWeight, Blur.F0Weight);
				Ctx.OutputRAM[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputRAM[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4, Mask);
			}
			if (Blur.HeightWeight > 0.0f)
			{
				Settings.Amount = Blur.Amount * Blur.HeightWeight;
				Ctx.OutputHeight[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputHeight[WriteIndex], Settings, MixtormatGaussianBlur::EField::Scalar, Mask);
			}
		}
	}
}
