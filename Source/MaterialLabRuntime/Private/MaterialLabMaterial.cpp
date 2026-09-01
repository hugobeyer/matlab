#include "MaterialLabMaterial.h"

void FMaterialLabLayer::GetEffectiveMasks(TArray<FMaterialLabMaskLayer>& OutMasks) const
{
	OutMasks.Reset();
	for (const FMaterialLabLayerChild& Child : Children)
	{
		if (Child.Type == EMaterialLabLayerChildType::Mask)
		{
			OutMasks.Add(Child.Mask);
		}
	}
}

void FMaterialLabLayer::MigrateLegacyChildren()
{
	if (Children.IsEmpty())
	{
		if (Masks.IsEmpty() && (!Mask.IsNull() || !MaskTexture.IsNull()))
		{
			FMaterialLabMaskLayer& LegacyMask = Masks.AddDefaulted_GetRef();
			LegacyMask.Mask = Mask;
			LegacyMask.MaskTexture = MaskTexture;
			LegacyMask.BlendMode = EMaterialLabMaskBlendMode::Replace;
			LegacyMask.Weight = 1.0f;
			LegacyMask.bInvert = bInvertMask;
			LegacyMask.Tiling = FMath::Clamp(FMath::RoundToInt(MaskTiling), 1, 16);
			LegacyMask.Balance = MaskBalance;
			LegacyMask.Contrast = MaskContrast;
		}

		for (const FMaterialLabMaskLayer& LegacyMask : Masks)
		{
			FMaterialLabLayerChild& Child = Children.AddDefaulted_GetRef();
			Child.Type = EMaterialLabLayerChildType::Mask;
			Child.Mask = LegacyMask;
		}
		for (const FMaterialLabLayerEffect& LegacyEffect : Effects)
		{
			FMaterialLabLayerChild& Child = Children.AddDefaulted_GetRef();
			Child.Type = EMaterialLabLayerChildType::Effect;
			Child.Effect = LegacyEffect;
		}
	}

	Masks.Reset();
	Effects.Reset();
	Mask.Reset();
	MaskTexture.Reset();
}

void UMaterialLabMaterial::PostLoad()
{
	Super::PostLoad();
	for (FMaterialLabLayer& Layer : Layers)
	{
		Layer.MigrateLegacyChildren();
	}
}

FPrimaryAssetId UMaterialLabMaterial::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MaterialLabMaterial"), GetFName());
}

bool UMaterialLabMaterial::CanAddLayer() const
{
	return true;
}

bool UMaterialLabMaterial::AddLayer(const EMaterialLabLayerType Type)
{
	if (!CanAddLayer())
	{
		return false;
	}

	FMaterialLabLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Type = Type;

	switch (Type)
	{
	case EMaterialLabLayerType::Material:
		Layer.DisplayName = NSLOCTEXT("MaterialLabMaterial", "MaterialLayer", "Material Layer");
		break;
	case EMaterialLabLayerType::Fill:
		Layer.DisplayName = NSLOCTEXT("MaterialLabMaterial", "FillLayer", "Fill Layer");
		Layer.bOverrideBaseColor = true;
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		break;
	case EMaterialLabLayerType::Effect:
		Layer.DisplayName = NSLOCTEXT("MaterialLabMaterial", "EffectLayer", "Effect Layer");
		break;
	}

	return true;
}
