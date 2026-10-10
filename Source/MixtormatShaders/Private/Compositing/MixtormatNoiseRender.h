// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

struct FMixtormatNoise;

namespace MixtormatGpuCompositor
{
// The Noise module's settings, resolved from the authored payload to what the GPU pass reads.
//
// This rides a side table rather than the child render-data payload because that payload lives
// in MixtormatGpuCompositorInternal.h, which the Noise pass does not own. When the payload lands
// there (integration pass), this struct moves beside StrataCarver and the store dissolves --
// the gather and the pass keep their shapes.
struct FMixtormatNoiseRenderData
{
	// EMixtormatNoiseType, by value.
	int32 Type = 0;
	int32 Seed = 1;
	// Lattice cells across the tile; floored at 1, the lowest period that closes on the tile.
	float Scale = 8.0f;
	// Octaves, clamped to the shader's 1..8.
	int32 Detail = 4;
	float Roughness = 0.5f;
	// Floored at 1: a lacunarity below one makes finer octaves coarser, which is meaningless.
	float Lacunarity = 2.0f;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	// Bars / Phasor: the direction the patterns advance across, in degrees.
	float Direction = 0.0f;
	float LayerMix = 0.0f;
	float PhasorFrequency = 2.0f;
	float PhasorAnisotropy = 0.0f;
	float PhasorPhaseVariation = 0.5f;
	float PhasorOrientationVariation = 0.35f;
	int32 PhasorComponents = 2;
	float PhasorScale = 1.0f;
	float PhasorBias = 0.0f;
	int32 WorleyMetric = 0;
	float WorleyJitter = 1.0f;
	float WorleyCellDepth = 0.0f;
	float DistortionStrength = 0.0f;
	float DistortionJaggedness = 0.0f;
	float JaggedSharpness = 0.0f;
	float JaggedDetail = 0.0f;
	float DistortionFrequency = 4.0f;
	int32 DistortionOctaves = 2;
	float DistortionRoughness = 0.5f;
	float DistortionLacunarity = 2.0f;
	float DistortionCurlMix = 1.0f;
	float DistortionDirection = 0.0f;
};

// Shared field sanitization for generator gather and inline source-local masks.
FMixtormatNoiseRenderData ResolveNoiseRenderData(const FMixtormatNoise& Noise);

// The address one Noise module publishes under, shared by the gather (writer) and the pass
// (reader). Same shape as the publication keys, so the pass looks settings up by the exact
// address it will publish under.
struct FMixtormatNoiseRenderKey
{
	FGuid LayerId;
	int32 ChildIndex = INDEX_NONE;

	friend bool operator==(const FMixtormatNoiseRenderKey& A, const FMixtormatNoiseRenderKey& B)
	{
		return A.LayerId == B.LayerId && A.ChildIndex == B.ChildIndex;
	}

	friend uint32 GetTypeHash(const FMixtormatNoiseRenderKey& Key)
	{
		return HashCombine(GetTypeHash(Key.LayerId), GetTypeHash(Key.ChildIndex));
	}
};

// Gather-to-pass handoff for Noise settings.
//
// The gather runs while the request is built; the pass runs inside the render command, and the
// two can overlap when composes are pipelined, so both sides take the lock. Entries are
// overwritten every compose for live children; a child that is removed leaves a stale entry that
// is never looked up. The cap bounds a long editing session's churn -- past it the table clears
// and the next compose repopulates it, which costs one frame of nothing.
class FMixtormatNoiseRenderStore
{
public:
	void Set(const FMixtormatNoiseRenderKey& Key, const FMixtormatNoiseRenderData& Data);
	bool Find(const FMixtormatNoiseRenderKey& Key, FMixtormatNoiseRenderData& OutData) const;

private:
	// Distinct Noise children ever seen; 8k is far past any real stack.
	static constexpr int32 MaxEntries = 8192;

	mutable FCriticalSection Guard;
	TMap<FMixtormatNoiseRenderKey, FMixtormatNoiseRenderData> Entries;
};

// Module-local singleton; both sides live in this module.
FMixtormatNoiseRenderStore& MixtormatNoiseRenderStore();
}