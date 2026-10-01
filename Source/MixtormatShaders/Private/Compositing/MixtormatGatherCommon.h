// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	inline bool IsIdGatherChild(const EMixtormatLayerChildType Type)
	{
		switch (Type)
		{
		case EMixtormatLayerChildType::PatternId:
		case EMixtormatLayerChildType::Filter:
		case EMixtormatLayerChildType::CombineId:
		case EMixtormatLayerChildType::IdGroup:
		case EMixtormatLayerChildType::ColorId:
		case EMixtormatLayerChildType::HsvFilter:
		case EMixtormatLayerChildType::RandomId:
		case EMixtormatLayerChildType::RampId:
		case EMixtormatLayerChildType::UvFromIds:
		case EMixtormatLayerChildType::ReliefFromIds:
		case EMixtormatLayerChildType::BoundaryFromIds:
		case EMixtormatLayerChildType::OutputReference:
			return true;
		default:
			return false;
		}
	}

	inline bool IsMaskGatherChild(const EMixtormatLayerChildType Type)
	{
		return Type == EMixtormatLayerChildType::Mask
			|| Type == EMixtormatLayerChildType::Generated
			|| Type == EMixtormatLayerChildType::Craquelure
			|| Type == EMixtormatLayerChildType::Blur
			|| Type == EMixtormatLayerChildType::Curvature;
	}
}

namespace MixtormatNetworkKey
{
	// FNV-1a over the raw bytes of whatever is fed in. The values are floats straight out of the
	// panel, so this hashes bit patterns rather than magnitudes -- which is what is wanted: two
	// settings that differ anywhere at all must miss, and a value that round-trips through the
	// UI unchanged must hit.
	inline uint64 Combine(const uint64 Hash, const void* Data, const int32 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		uint64 Result = Hash;
		for (int32 Index = 0; Index < Size; ++Index)
		{
			Result ^= static_cast<uint64>(Bytes[Index]);
			Result *= 1099511628211ull;
		}
		return Result;
	}

	template <typename T>
	inline uint64 Add(const uint64 Hash, const T& Value)
	{
		static_assert(TIsPODType<T>::Value, "Network key inputs are hashed as raw bytes.");
		return Combine(Hash, &Value, sizeof(T));
	}

	inline uint64 Seed()
	{
		return 14695981039346656037ull;
	}
}
