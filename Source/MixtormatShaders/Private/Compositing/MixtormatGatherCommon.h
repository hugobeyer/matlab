// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	inline FMixtormatBlur SanitizeBlur(const FMixtormatBlur& Source)
	{
		FMixtormatBlur Blur = Source;
		Blur.RadiusX = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, RadiusX), Blur.RadiusX);
		Blur.RadiusY = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, RadiusY), Blur.RadiusY);
		Blur.Amount = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, Amount), Blur.Amount);
		Blur.BaseColorWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, BaseColorWeight), Blur.BaseColorWeight);
		Blur.NormalWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, NormalWeight), Blur.NormalWeight);
		Blur.RoughnessWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, RoughnessWeight), Blur.RoughnessWeight);
		Blur.AOWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, AOWeight), Blur.AOWeight);
		Blur.MetallicWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, MetallicWeight), Blur.MetallicWeight);
		Blur.F0Weight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, F0Weight), Blur.F0Weight);
		Blur.HeightWeight = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, HeightWeight), Blur.HeightWeight);
		return Blur;
	}

	inline bool IsIdGatherChild(const EMixtormatLayerChildType Type)
	{
		switch (Type)
		{
		case EMixtormatLayerChildType::PatternId:
		case EMixtormatLayerChildType::Filter:

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
