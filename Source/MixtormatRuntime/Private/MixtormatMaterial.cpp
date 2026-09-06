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
			LegacyMask.TilingX = FMath::Clamp(FMath::RoundToInt(MaskTiling), 1, 16);
			LegacyMask.TilingY = LegacyMask.TilingX;
			// Straight into Shaping. Routing these through the deprecated fields instead would
			// work only until MigrateLegacyShaping consumed them, and the single-mask recipes
			// this branch exists for would come back neutral.
			LegacyMask.Shaping.bInvert = bInvertMask;
			LegacyMask.Shaping.Balance = FMath::Clamp(MaskBalance, 0.0f, 1.0f);
			LegacyMask.Shaping.Contrast = MaskContrast;
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

void FMixtormatMaskLayer::MigrateLegacyShaping()
{
	// Negative contrast is the "already on Shaping" marker; see the field's comment.
	if (Contrast < 0.0f)
	{
		return;
	}

	// Balance is clamped rather than carried across because the shader has always saturated it.
	// The old clamp allowed 2, so anything above 1 was already drawing as 1 -- narrowing it here
	// is what the asset was displaying, not a re-grade. Contrast keeps its value even above the
	// new slider's 4, which is why the struct's own clamp stays at 10.
	Shaping.bInvert = bInvert;
	Shaping.Balance = FMath::Clamp(Balance, 0.0f, 1.0f);
	Shaping.Contrast = Contrast;
	Shaping.Offset = Offset;

	bInvert = false;
	Balance = 0.5f;
	Contrast = -1.0f;
	Offset = 0.0f;
}

void UMixtormatMaterial::PostLoad()
{
	Super::PostLoad();
	for (FMixtormatLayer& Layer : Layers)
	{
		Layer.MigrateLegacyChildren();
	}

	// A second pass, after the children exist: MigrateLegacyChildren is what promotes the legacy
	// Masks array into Children, and those entries carry pre-Shaping values of their own.
	for (FMixtormatLayer& Layer : Layers)
	{
		for (FMixtormatLayerChild& Child : Layer.Children)
		{
			if (Child.Type == EMixtormatLayerChildType::Mask)
			{
				Child.Mask.MigrateLegacyShaping();
			}
		}
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
