// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"

void UMixtormatMaterial::PostLoad()
{
	Super::PostLoad();
	MixtormatParameterBinding::EnsureStableIds(Layers);
}

FPrimaryAssetId UMixtormatMaterial::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatMaterial"), GetFName());
}

bool UMixtormatMaterial::CanAddLayer() const
{
	return true;
}

bool UMixtormatMaterial::AddLayer(const EMixtormatLayerType Type)
{
	if (!CanAddLayer())
	{
		return false;
	}

	FMixtormatLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Type = Type;

	switch (Type)
	{
	case EMixtormatLayerType::Material:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "MaterialLayer", "Material Layer");
		break;
	case EMixtormatLayerType::Fill:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "FillLayer", "Fill Layer");
		Layer.bOverrideBaseColor = true;
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		break;
	case EMixtormatLayerType::Effect:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "EffectLayer", "Effect Layer");
		break;
	}

	return true;
}
