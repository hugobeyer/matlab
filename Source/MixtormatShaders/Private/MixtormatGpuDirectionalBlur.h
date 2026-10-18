// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "MixtormatGpuGaussianBlur.h"

struct FMixtormatBlur;

namespace MixtormatDirectionalBlur
{
	// Single CPU/GPU source of dispatch and trajectory bounds (exported as shader defines).
	namespace GPU
	{
		inline constexpr int32 ThreadGroupSize = 8;
		inline constexpr int32 MaxStepsPerSide = 64;
		inline constexpr float StepTexels = 1.0f;
		inline constexpr float DirectionEpsilonSquared = 1.0e-12f;
	}

	// Settings owns Amount, packed channel weights and coverage clamping; Blur owns
	// mode, enabled state and directional parameters. Gaussian uses Settings' radii.
	// Length is an output-texel extent per side for Directional, total trail for Smear.
	// Angle/Flow directions use texture X/Y axes, converted to UV using each dimension.
	// Guidance and its separate validity must match Source; unavailable Flow is identity.
	// SourceValidity is required only for ValidFlow, preserved separately, never written.
	// ColorAlpha accepts/returns straight RGBA; filtering and blending are premultiplied.
	FRDGTextureRef AddPasses(
		FRDGBuilder& Graph,
		FRDGTextureRef Source,
		const FMixtormatBlur& Blur,
		const MixtormatGaussianBlur::FSettings& Settings,
		MixtormatGaussianBlur::EField Field,
		FRDGTextureRef Mask = nullptr,
		FRDGTextureRef DirectionFlow = nullptr,
		FRDGTextureRef DirectionValidity = nullptr,
		FRDGTextureRef SourceValidity = nullptr);
}
