// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatMaterial.h"

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
		}
		return nullptr;
	}

	inline void* Data(FMixtormatGenerator& Generator)
	{
		return const_cast<void*>(Data(static_cast<const FMixtormatGenerator&>(Generator)));
	}

	inline UScriptStruct* Struct(const FMixtormatGenerator& Generator)
	{
		switch (Generator.Type)
		{
		case EMixtormatGeneratorType::StrataCarver: return FMixtormatStrataCarver::StaticStruct();
		case EMixtormatGeneratorType::Cracks: return FMixtormatCracks::StaticStruct();
		case EMixtormatGeneratorType::RockFormation: return FMixtormatRockFormation::StaticStruct();
		case EMixtormatGeneratorType::Pebbles: return FMixtormatPebbles::StaticStruct();
		}
		return nullptr;
	}
}
