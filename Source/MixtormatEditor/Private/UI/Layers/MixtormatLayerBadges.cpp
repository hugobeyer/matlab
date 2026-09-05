#include "UI/Layers/MixtormatLayerBadges.h"

#include "MixtormatEffect.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

namespace MixtormatLayerBadges
{
	EComposition CompositionOf(const FMixtormatLayer& Layer)
	{
		// Order matters and the cases do not overlap. A normal-detail layer never reaches the
		// composition test, because it contributes no surface to composite.
		if (Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail)
		{
			return EComposition::Detail;
		}
		if (Layer.CompositionMode == EMixtormatCompositionMode::Coat)
		{
			return EComposition::Coat;
		}
		// Replace is the only mode left, so the normal blend distinguishes the two that remain:
		// Over discards the normal below, Blend reorients onto it.
		return Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override
			? EComposition::Over
			: EComposition::Blend;
	}

	void ApplyComposition(FMixtormatLayer& Layer, const EComposition Choice)
	{
		// Every branch writes ChannelMode, so leaving Detail always restores a surface layer --
		// otherwise a layer that had once been Detail kept contributing only its normal while the
		// control claimed it was blending.
		switch (Choice)
		{
		case EComposition::Detail:
			Layer.ChannelMode = EMixtormatLayerChannelMode::NormalDetail;
			break;
		case EComposition::Coat:
			Layer.ChannelMode = EMixtormatLayerChannelMode::CompleteSurface;
			Layer.CompositionMode = EMixtormatCompositionMode::Coat;
			break;
		case EComposition::Over:
			Layer.ChannelMode = EMixtormatLayerChannelMode::CompleteSurface;
			Layer.CompositionMode = EMixtormatCompositionMode::Replace;
			Layer.NormalBlendMode = EMixtormatNormalBlendMode::Override;
			break;
		default:
			Layer.ChannelMode = EMixtormatLayerChannelMode::CompleteSurface;
			Layer.CompositionMode = EMixtormatCompositionMode::Replace;
			Layer.NormalBlendMode = EMixtormatNormalBlendMode::Combine;
			break;
		}
	}

	TArray<FText> CompositionOptions()
	{
		// Spelled out here, abbreviated on the badge: the control has the width and is read once,
		// the badge is scanned down a column and has 40px.
		return {
			LOCTEXT("CompositionBlend", "BLEND"),
			LOCTEXT("CompositionOver", "OVER"),
			LOCTEXT("CompositionCoat", "COAT"),
			LOCTEXT("CompositionDetail", "DETAIL"),
		};
	}

	TArray<FText> CompositionToolTips()
	{
		return {
			LOCTEXT("CompositionBlendHint", "Reorient this layer's normal onto the surface below (RNM), through the layer mask."),
			LOCTEXT("CompositionOverHint", "Replace the normal below with this layer's, through the layer mask."),
			LOCTEXT("CompositionCoatHint", "Sit this layer over what is below rather than blending into it."),
			LOCTEXT("CompositionDetailHint", "Contribute only a normal. The layer's other channels are ignored."),
		};
	}

	FText ForLayer(const FMixtormatLayer& Layer)
	{
		switch (CompositionOf(Layer))
		{
		case EComposition::Detail: return LOCTEXT("LayerBadgeDetail", "DTL");
		case EComposition::Coat:   return LOCTEXT("LayerBadgeCoat", "COAT");
		case EComposition::Over:   return LOCTEXT("LayerBadgeOver", "OVER");
		default:                   return LOCTEXT("LayerBadgeBlend", "BLEND");
		}
	}

	FText ForMaskBlendMode(const EMixtormatMaskBlendMode Mode)
	{
		switch (Mode)
		{
		case EMixtormatMaskBlendMode::Add:      return LOCTEXT("MaskBadgeAdd", "ADD");
		case EMixtormatMaskBlendMode::Subtract: return LOCTEXT("MaskBadgeSub", "SUB");
		case EMixtormatMaskBlendMode::Multiply: return LOCTEXT("MaskBadgeMultiply", "MULT");
		case EMixtormatMaskBlendMode::Min:      return LOCTEXT("MaskBadgeMin", "MIN");
		case EMixtormatMaskBlendMode::Max:      return LOCTEXT("MaskBadgeMax", "MAX");
		case EMixtormatMaskBlendMode::AddSub:   return LOCTEXT("MaskBadgeAddSub", "ADDSUB");
		case EMixtormatMaskBlendMode::Overlay:  return LOCTEXT("MaskBadgeOverlay", "OVRLAY");
		default:                                return LOCTEXT("MaskBadgeReplace", "REPL");
		}
	}

	FText ForEffectType(const EMixtormatEffectType Type)
	{
		switch (Type)
		{
		case EMixtormatEffectType::Stain:   return LOCTEXT("EffectBadgeStain", "STAIN");
		case EMixtormatEffectType::Erosion: return LOCTEXT("EffectBadgeErosion", "ERODE");
		case EMixtormatEffectType::Grade:   return LOCTEXT("EffectBadgeGrade", "GRADE");
		case EMixtormatEffectType::Chipping: return LOCTEXT("EffectBadgeChipping", "CHIP");
		default:                            return LOCTEXT("EffectBadgePeel", "PEEL");
		}
	}

	FText ForChild(const FMixtormatLayerChild& Child)
	{
		if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			// An asset-backed effect takes its type from the asset; a procedural one has no asset
			// to load and carries the type on the child itself.
			const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
			return ForEffectType(Asset ? Asset->EffectType : Child.Effect.ProceduralType);
		}
		if (Child.Type == EMixtormatLayerChildType::Generated)
		{
			return ForMaskBlendMode(Child.Generated.BlendMode);
		}
		if (Child.Type == EMixtormatLayerChildType::Craquelure)
		{
			return ForMaskBlendMode(Child.Craquelure.BlendMode);
		}
		if (Child.Type == EMixtormatLayerChildType::ColorId)
		{
			return ForMaskBlendMode(Child.ColorId.BlendMode);
		}
		return ForMaskBlendMode(Child.Mask.BlendMode);
	}

	FText KindForChild(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Effect:    return LOCTEXT("ChildKindEffect", "FX");
		case EMixtormatLayerChildType::Generated: return LOCTEXT("ChildKindGenerated", "GEN");
		case EMixtormatLayerChildType::Craquelure: return LOCTEXT("ChildKindCraquelure", "CRAQ");
		case EMixtormatLayerChildType::ColorId:   return LOCTEXT("ChildKindColorId", "ID");
		default:                                  return LOCTEXT("ChildKindMask", "MASK");
		}
	}
}

#undef LOCTEXT_NAMESPACE
