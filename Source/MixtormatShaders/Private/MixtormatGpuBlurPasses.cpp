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
		for (const FMixtormatBlur& Blur : Layer.SurfaceBlurs)
		{
			MixtormatGaussianBlur::FSettings Settings;
			Settings.RadiusX = Blur.RadiusX;
			Settings.RadiusY = Blur.RadiusY;
			Settings.Amount = Blur.Amount;
			if (Blur.bAppearance)
			{
				Ctx.OutputBC[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputBC[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4);
				Ctx.OutputN[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputN[WriteIndex], Settings, MixtormatGaussianBlur::EField::EncodedNormal);
				Ctx.OutputRAM[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputRAM[WriteIndex], Settings, MixtormatGaussianBlur::EField::Vector4);
			}
			if (Blur.bHeight)
			{
				Ctx.OutputHeight[WriteIndex] = MixtormatGaussianBlur::AddPasses(Ctx.GraphBuilder,
					Ctx.OutputHeight[WriteIndex], Settings, MixtormatGaussianBlur::EField::Scalar);
			}
		}
	}
}
