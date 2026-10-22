// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatSurfacingTypes.h"

namespace MixtormatSurfacing
{
	TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(const EMixtormatSurfacingType Type)
	{
		using K = EMixtormatPublishedFieldKind;
		static const FMixtormatSurfacingOutput SkinOutputs[] = {
			{FName(TEXT("Height")), NSLOCTEXT("MixtormatSurfacing", "Height", "Height"), K::ScalarSigned},
			{FName(TEXT("Color")), NSLOCTEXT("MixtormatSurfacing", "Color", "Albedo"), K::Color},
			{FName(TEXT("Roughness")), NSLOCTEXT("MixtormatSurfacing", "Roughness", "Roughness"), K::Scalar01},
			{FName(TEXT("AO")), NSLOCTEXT("MixtormatSurfacing", "AO", "Cavity AO"), K::Scalar01},
			{FName(TEXT("F0")), NSLOCTEXT("MixtormatSurfacing", "F0", "Dielectric F0"), K::Scalar01},
			{FName(TEXT("Fuzz")), NSLOCTEXT("MixtormatSurfacing", "Fuzz", "Fuzz Amount"), K::Scalar01},
			{FName(TEXT("Pores")), NSLOCTEXT("MixtormatSurfacing", "Pores", "Pores"), K::Scalar01},
			{FName(TEXT("Compression")), NSLOCTEXT("MixtormatSurfacing", "Compression", "Compression Response"), K::Scalar01},
			{FName(TEXT("SSS")), NSLOCTEXT("MixtormatSurfacing", "SSS", "Mean Free Path (cm)"), K::Color, false}
		};
		return Type == EMixtormatSurfacingType::Skin
			? MakeArrayView(SkinOutputs) : TConstArrayView<FMixtormatSurfacingOutput>();
	}

	bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		const EMixtormatPublishedFieldKind Kind, const FName OutputName)
	{
		for (const FMixtormatSurfacingOutput& Output : GetOutputs(Surfacing.Type))
		{
			if (Output.Name == OutputName && Output.Kind == Kind) { return Output.bReferenceable; }
		}
		return false;
	}

	bool PublishesField(const FMixtormatSurfacing& Surfacing,
		const EMixtormatPublishedFieldKind Kind, const FName OutputName)
	{
		for (const FMixtormatSurfacingOutput& Output : GetOutputs(Surfacing.Type))
		{
			if (Output.Name == OutputName && Output.Kind == Kind) { return true; }
		}
		return false;
	}
}
