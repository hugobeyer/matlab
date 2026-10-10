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
		}
		return nullptr;
	}
}
