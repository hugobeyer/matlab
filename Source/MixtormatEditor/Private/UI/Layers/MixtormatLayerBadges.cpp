// Copyright 2026 Hugo Beyer. All Rights Reserved.

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
		//
		// Deliberately not read from bHeightBlendEnabled. That flag arms Height Mask Blending,
		// where the height decides *coverage* and drags a dozen authored controls with it, and
		// it is an independent opt-in. BLEND merges heights; it does not turn that feature on.
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
			// Coat has to pin this rather than leave it, or the layer's normal keeps whatever
			// blend mode the previous choice left behind -- Combine from Blend, Override from
			// Over -- and a coat only reads correctly by accident of which choice preceded it.
			// A coat sits over what is below, so its own normal replaces rather than reorients.
			Layer.NormalBlendMode = EMixtormatNormalBlendMode::Override;
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
			LOCTEXT("CompositionBlendHint", "Merge this layer's height with the surface below instead of cross-fading it, so an intersection keeps the upper surface rather than sinking to the average. Its normal reorients onto what is below (RNM). Coverage is unchanged: base colour, roughness, AO, metallic and F0 composite exactly as they do under OVER."),
			LOCTEXT("CompositionOverHint", "Cross-fade this layer's height with the surface below, and replace the normal below with this layer's. Ordinary opacity and mask compositing throughout."),
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

	FText ForColorBlendMode(const EMixtormatColorBlendMode Mode)
	{
		// Six characters, like every other badge: MixtormatTokens::BadgeMaxCharacters is what the
		// fixed box is sized for, and a longer word clips rather than widening it.
		switch (Mode)
		{
		case EMixtormatColorBlendMode::Add:        return LOCTEXT("ColorBadgeAdd", "ADD");
		case EMixtormatColorBlendMode::Subtract:   return LOCTEXT("ColorBadgeSub", "SUB");
		case EMixtormatColorBlendMode::Multiply:   return LOCTEXT("ColorBadgeMult", "MULT");
		case EMixtormatColorBlendMode::Divide:     return LOCTEXT("ColorBadgeDiv", "DIV");
		case EMixtormatColorBlendMode::Screen:     return LOCTEXT("ColorBadgeScreen", "SCREEN");
		case EMixtormatColorBlendMode::Overlay:    return LOCTEXT("ColorBadgeOverlay", "OVRLAY");
		case EMixtormatColorBlendMode::HardLight:  return LOCTEXT("ColorBadgeHard", "HARD");
		case EMixtormatColorBlendMode::SoftLight:  return LOCTEXT("ColorBadgeSoft", "SOFT");
		case EMixtormatColorBlendMode::ColorDodge: return LOCTEXT("ColorBadgeDodge", "DODGE");
		case EMixtormatColorBlendMode::ColorBurn:  return LOCTEXT("ColorBadgeBurn", "BURN");
		case EMixtormatColorBlendMode::AddSub:     return LOCTEXT("ColorBadgeAddSub", "ADDSUB");
		case EMixtormatColorBlendMode::Difference: return LOCTEXT("ColorBadgeDiff", "DIFF");
		case EMixtormatColorBlendMode::Exclusion:  return LOCTEXT("ColorBadgeExcl", "EXCL");
		case EMixtormatColorBlendMode::Min:        return LOCTEXT("ColorBadgeMin", "MIN");
		case EMixtormatColorBlendMode::Max:        return LOCTEXT("ColorBadgeMax", "MAX");
		case EMixtormatColorBlendMode::Hue:        return LOCTEXT("ColorBadgeHue", "HUE");
		case EMixtormatColorBlendMode::Saturation: return LOCTEXT("ColorBadgeSat", "SAT");
		case EMixtormatColorBlendMode::Color:      return LOCTEXT("ColorBadgeColor", "COLOR");
		case EMixtormatColorBlendMode::Luminosity: return LOCTEXT("ColorBadgeLum", "LUM");
		default:                                   return FText::GetEmpty();
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
		case EMixtormatEffectType::Stain:     return LOCTEXT("EffectBadgeStain", "STAIN");
		case EMixtormatEffectType::Erosion:   return LOCTEXT("EffectBadgeErosion", "ERODE");
		case EMixtormatEffectType::Grade:     return LOCTEXT("EffectBadgeGrade", "GRADE");
		case EMixtormatEffectType::Breakup:   return LOCTEXT("EffectBadgeBreakup", "BREAK");
		case EMixtormatEffectType::WornEdges: return LOCTEXT("EffectBadgeWorn", "WORN");
		case EMixtormatEffectType::FlowWarp:  return LOCTEXT("EffectBadgeFlowWarp", "WARP");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("EffectBadgeLayerBlur", "BLUR");
		case EMixtormatEffectType::Runoff:    return LOCTEXT("EffectBadgeRunoff", "RUNOFF");
		default:                              return LOCTEXT("EffectBadgePeel", "PEEL");
		}
	}

	FText ForChild(const FMixtormatLayerChild& Child)
	{
		if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			// An asset-backed effect takes its type from the asset; a procedural one has no asset
			// to load and carries the type on the child itself.
			const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
			const EMixtormatEffectType Type = Asset ? Asset->EffectType : Child.Effect.ProceduralType;
			if (Type == EMixtormatEffectType::Stain)
			{
				return Child.Effect.StainMode == EMixtormatStainMode::Deposit
					? LOCTEXT("EffectBadgeDeposit", "DEPOSIT")
					: LOCTEXT("EffectBadgeWet", "WET");
			}
			return ForEffectType(Type);
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
		if (Child.Type == EMixtormatLayerChildType::CombineId)
		{
			// The mode, not the category: two of these in a row differ by exactly this, and
			// which one a row is doing is what the column is scanned for.
			return Child.CombineId.Mode == EMixtormatIdCombineMode::Subtract
				? LOCTEXT("CombineIdBadgeSubtract", "SUB")
				: LOCTEXT("CombineIdBadgeMerge", "MERGE");
		}
		if (Child.Type == EMixtormatLayerChildType::Filter
			|| Child.Type == EMixtormatLayerChildType::PatternId
			|| Child.Type == EMixtormatLayerChildType::HsvFilter
			|| Child.Type == EMixtormatLayerChildType::RampId)
		{
			// Filters have no blend mode -- they emit data and albedo, not coverage -- so the
			// slot that would carry one names the category instead.
			return LOCTEXT("ChildKindFilter", "FILT");
		}
		if (Child.Type == EMixtormatLayerChildType::RandomId)
		{
			return ForMaskBlendMode(Child.RandomId.BlendMode);
		}
		if (Child.Type == EMixtormatLayerChildType::Generator)
		{
			// The generator kind, not a blend mode -- a generator emits no coverage and never
			// joins the mask chain, so the slot that would carry one names what is running.
			switch (Child.Generator.Type)
			{
			case EMixtormatGeneratorType::StrataCarver:
				return LOCTEXT("GeneratorBadgeStrataCarver", "STRATA");
			}
			return LOCTEXT("GeneratorBadgeUnknown", "GEN");
		}
		if (Child.Type == EMixtormatLayerChildType::Curvature)
		{
			// The field it reads, not the invariant: two curvature nodes on one mask most often
			// differ by source, and "HEIGHT" against "MASK" is the distinction worth four glyphs.
			return Child.Curvature.Source == EMixtormatCurvatureSource::Height
				? LOCTEXT("CurvatureBadgeHeight", "HEIGHT")
				: LOCTEXT("CurvatureBadgeMask", "MASK");
		}
		if (Child.Type == EMixtormatLayerChildType::Blur)
		{
			// No blend mode: a blur reshapes the mask it is scoped to rather than joining the
			// chain, so the slot names which axes are running instead.
			const bool bX = Child.Blur.RadiusX > 0.0f;
			const bool bY = Child.Blur.RadiusY > 0.0f;
			if (bX && bY)
			{
				return LOCTEXT("BlurBadgeXY", "XY");
			}
			if (bX)
			{
				return LOCTEXT("BlurBadgeX", "X");
			}
			if (bY)
			{
				return LOCTEXT("BlurBadgeY", "Y");
			}
			return LOCTEXT("BlurBadgeOff", "OFF");
		}
		return ForMaskBlendMode(Child.Mask.BlendMode);
	}

	FText KindForChild(const FMixtormatLayerChild& Child)
	{
		switch (Child.Type)
		{
		case EMixtormatLayerChildType::Effect:    return LOCTEXT("ChildKindEffect", "FX");
		// GMSK, not GEN. GEN now belongs to the GENERATORS category below, and the two nodes are
		// easy to confuse in exactly the way a shared badge would encourage: a generated mask
		// emits coverage into the mask chain, a generator rewrites the layer's input height
		// before the composite. Badge text only -- EMixtormatLayerChildType::Generated is
		// unchanged and nothing serialised moves.
		case EMixtormatLayerChildType::Generated: return LOCTEXT("ChildKindGenerated", "GMSK");
		case EMixtormatLayerChildType::Craquelure: return LOCTEXT("ChildKindCraquelure", "CRAQ");
		case EMixtormatLayerChildType::ColorId:   return LOCTEXT("ChildKindColorId", "ID");
		case EMixtormatLayerChildType::Filter:    return LOCTEXT("ChildKindFilter", "FILT");
		case EMixtormatLayerChildType::PatternId: return LOCTEXT("ChildKindPatternId", "PAT");
		case EMixtormatLayerChildType::CombineId: return LOCTEXT("ChildKindCombineId", "CMB");
		case EMixtormatLayerChildType::HsvFilter: return LOCTEXT("ChildKindHsvFilter", "HSV");
		case EMixtormatLayerChildType::RandomId:  return LOCTEXT("ChildKindRandomId", "RND");
		case EMixtormatLayerChildType::RampId:    return LOCTEXT("ChildKindRampId", "RAMP");
		case EMixtormatLayerChildType::Blur:      return LOCTEXT("ChildKindBlur", "BLUR");
		case EMixtormatLayerChildType::Curvature: return LOCTEXT("ChildKindCurvature", "CURV");
		case EMixtormatLayerChildType::Generator: return LOCTEXT("ChildKindGenerator", "GEN");
		default:                                  return LOCTEXT("ChildKindMask", "MASK");
		}
	}
}

#undef LOCTEXT_NAMESPACE
