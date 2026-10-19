// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatGeneratorTypes.h"
#include "UObject/Class.h"

namespace MixtormatGeneratorPayload
{
	inline const void* Data(const FMixtormatGenerator& Generator)
	{
		switch (Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver: return &Generator.StrataCarver;
		case EMixtormatGeneratorType::Cracks: return &Generator.Cracks;
		case EMixtormatGeneratorType::RockFormation: return &Generator.RockFormation;
		case EMixtormatGeneratorType::Pebbles: return &Generator.Pebbles;
		case EMixtormatGeneratorType::CliffStrata: return &Generator.CliffStrata;
		case EMixtormatGeneratorType::Noise: return &Generator.Noise;
		case EMixtormatGeneratorType::BooleanOps: return &Generator.BooleanOps;
		case EMixtormatGeneratorType::RootNetwork: return &Generator.RootNetwork;
	case EMixtormatGeneratorType::Moss:
	case EMixtormatGeneratorType::Mycelium: return &Generator.OrganicGrowth;
	case EMixtormatGeneratorType::Scales: return &Generator.Scales;
		}
		return nullptr;
	}


	inline UScriptStruct* Struct(const FMixtormatGenerator& Generator)
	{
		switch (Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver: return FMixtormatStrataCarver::StaticStruct();
		case EMixtormatGeneratorType::Cracks: return FMixtormatCracks::StaticStruct();
		case EMixtormatGeneratorType::RockFormation: return FMixtormatRockFormation::StaticStruct();
		case EMixtormatGeneratorType::Pebbles: return FMixtormatPebbles::StaticStruct();
		case EMixtormatGeneratorType::CliffStrata: return FMixtormatCliffStrata::StaticStruct();
		case EMixtormatGeneratorType::Noise: return FMixtormatNoise::StaticStruct();
		case EMixtormatGeneratorType::BooleanOps: return FMixtormatBooleanOps::StaticStruct();
		case EMixtormatGeneratorType::RootNetwork: return FMixtormatRootNetwork::StaticStruct();
	case EMixtormatGeneratorType::Moss:
	case EMixtormatGeneratorType::Mycelium: return FMixtormatOrganicGrowth::StaticStruct();
	case EMixtormatGeneratorType::Scales: return FMixtormatScales::StaticStruct();
		}
		return nullptr;
	}
}
