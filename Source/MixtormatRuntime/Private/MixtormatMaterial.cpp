#include "MixtormatMaterial.h"

void FMixtormatLayer::GetEffectiveMasks(TArray<FMixtormatMaskLayer>& OutMasks) const
{
	OutMasks.Reset();
	for (const FMixtormatLayerChild& Child : Children)
	{
		if (Child.Type == EMixtormatLayerChildType::Mask)
		{
			OutMasks.Add(Child.Mask);
		}
	}
}

void FMixtormatLayer::MigrateLegacyChildren()
{
	if (Children.IsEmpty())
	{
		if (Masks.IsEmpty() && (!Mask.IsNull() || !MaskTexture.IsNull()))
		{
			FMixtormatMaskLayer& LegacyMask = Masks.AddDefaulted_GetRef();
			LegacyMask.Mask = Mask;
			LegacyMask.MaskTexture = MaskTexture;
			LegacyMask.BlendMode = EMixtormatMaskBlendMode::Replace;
			LegacyMask.Weight = 1.0f;
			LegacyMask.bInvert = bInvertMask;
			LegacyMask.TilingX = FMath::Clamp(FMath::RoundToInt(MaskTiling), 1, 16);
			LegacyMask.TilingY = LegacyMask.TilingX;
			LegacyMask.Balance = MaskBalance;
			LegacyMask.Contrast = MaskContrast;
		}

		for (const FMixtormatMaskLayer& LegacyMask : Masks)
		{
			FMixtormatLayerChild& Child = Children.AddDefaulted_GetRef();
			Child.Type = EMixtormatLayerChildType::Mask;
			Child.Mask = LegacyMask;
		}
		for (const FMixtormatLayerEffect& LegacyEffect : Effects)
		{
			FMixtormatLayerChild& Child = Children.AddDefaulted_GetRef();
			Child.Type = EMixtormatLayerChildType::Effect;
			Child.Effect = LegacyEffect;
		}
	}

	Masks.Reset();
	Effects.Reset();
	Mask.Reset();
	MaskTexture.Reset();
}

void UMixtormatMaterial::PostLoad()
{
	Super::PostLoad();
	for (FMixtormatLayer& Layer : Layers)
	{
		Layer.MigrateLegacyChildren();
	}
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
