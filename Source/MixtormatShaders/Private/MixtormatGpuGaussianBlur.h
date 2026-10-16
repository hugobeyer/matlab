// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

class FRDGBuilder;

namespace MixtormatGaussianBlur
{
	enum class EField : uint8 { Scalar, Vector4, EncodedNormal, ValidFlow };

	struct FSettings
	{
		// Output texels. Each axis uses sigma = radius / 2 and support ceil(3 * sigma).
		float RadiusX = 0.0f;
		float RadiusY = 0.0f;
		float Amount = 1.0f;
		bool bClampCoverage = false;
	};

	// Callers own scheduling. Mask gates only the completed result. ValidFlow requires
	// the compositor's separate validity texture, which is preserved, not filtered.
	// Scalar input/output is R16F or R32F; other fields use float4-compatible textures.
	// Intermediates use full precision; the resolved output retains the source format.
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph,
		FRDGTextureRef Source,
		const FSettings& Settings,
		EField Field,
		FRDGTextureRef Mask = nullptr,
		FRDGTextureRef FlowValidity = nullptr);
}
