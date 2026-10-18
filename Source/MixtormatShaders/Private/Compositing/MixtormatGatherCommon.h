// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGpuCompositorInternal.h"
#include "MixtormatChildScope.h"

namespace MixtormatGpuCompositor
{
	inline FMixtormatBlur SanitizeBlur(const FMixtormatBlur& Source)
	{
		FMixtormatBlur Blur = Source;
		if (Blur.Mode != EMixtormatBlurMode::Gaussian
			&& Blur.Mode != EMixtormatBlurMode::Directional && Blur.Mode != EMixtormatBlurMode::Smear)
		{
			Blur.bEnabled = false;
			Blur.Mode = EMixtormatBlurMode::Gaussian;
		}
		if (Blur.DirectionSource != EMixtormatBlurDirectionSource::Angle
			&& Blur.DirectionSource != EMixtormatBlurDirectionSource::Flow)
		{
			if (Blur.Mode != EMixtormatBlurMode::Gaussian) { Blur.bEnabled = false; }
			Blur.DirectionSource = EMixtormatBlurDirectionSource::Angle;
		}
		if (Blur.Target != EMixtormatBlurTarget::Height && Blur.Target != EMixtormatBlurTarget::Flow
			&& Blur.Target != EMixtormatBlurTarget::HeightAndFlow)
		{
			Blur.bEnabled = false;
		}
		Blur.Length = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, Length), Blur.Length);
		if (Blur.Mode == EMixtormatBlurMode::Directional) { Blur.Length = FMath::Abs(Blur.Length); }
		Blur.Angle = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, Angle), Blur.Angle);
		Blur.FlowInfluence = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, FlowInfluence), Blur.FlowInfluence);
		Blur.Radius = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, Radius), Blur.Radius);
		Blur.ScaleX = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, ScaleX), Blur.ScaleX);
		Blur.ScaleY = MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Blur, GET_MEMBER_NAME_CHECKED(FMixtormatBlur, ScaleY), Blur.ScaleY);
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

	inline FBlurRenderData GatherBlur(const FMixtormatBlur& Source, const int32 SourceChildIndex,
		const FMixtormatLayer& Layer, const TArray<FMixtormatLayer>& EffectiveLayers)
	{
		FBlurRenderData Blur;
		static_cast<FMixtormatBlur&>(Blur) = SanitizeBlur(Source);
		Blur.SourceChildIndex = SourceChildIndex;
		if (Blur.Mode != EMixtormatBlurMode::Gaussian
			&& Blur.DirectionSource == EMixtormatBlurDirectionSource::Flow)
		{
			const int32 LayerIndex = EffectiveLayers.IndexOfByPredicate(
				[&Layer](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Layer.LayerId; });
			const FMixtormatOutputReference& Reference = Blur.FlowSource;
			const int32 SourceIndex = MixtormatChildScope::ResolveBlurFlowSource(
				EffectiveLayers, LayerIndex, SourceChildIndex, Reference);
			if (SourceIndex != INDEX_NONE)
			{
				Blur.DirectionSourceKey = {Reference.SourceLayerId, SourceIndex, Reference.OutputName};
			}
		}
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
