// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

class FRDGBuilder;

// Private typed Gaussian service. Callers own execution order and target semantics.
// Radius is in output texels; Angle is in degrees, and rotates the kernel.
// Anisotropy 0 gives a circular Gaussian; 1 is a strongly elongated kernel.
// Optional Flow is a parent-owned vector/validity field, not a Blur source selector.
namespace MixtormatGaussianBlur
{
	enum class EField : uint8 { Scalar, Vector4, EncodedNormal, ValidFlow };
	struct FSettings
	{
		float Radius = 0.0f;
		float Amount = 1.0f;
		float AngleDegrees = 0.0f;
		float Anisotropy = 0.0f;
		bool bClampCoverage = false;
	};
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph,
		FRDGTextureRef Source,
		const FSettings& Settings,
		EField Field,
		FRDGTextureRef Mask = nullptr,
		FRDGTextureRef ParentFlow = nullptr);
}
