// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/MixtormatLayerBadges.h"

#include "Widgets/MixtormatChildCapabilities.h"

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
		// Override discards the normal below, Combine reorients onto it.
		return Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override
			? EComposition::Override
			: EComposition::Combine;
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
		case EComposition::Override:
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
			LOCTEXT("CompositionCombine", "COMBINE"),
			LOCTEXT("CompositionOverride", "OVERRIDE"),
			LOCTEXT("CompositionCoat", "COAT"),
			LOCTEXT("CompositionDetail", "DETAIL"),
		};
	}

	TArray<FText> CompositionToolTips()
	{
		return {
			LOCTEXT("CompositionCombineHint", "This layer's normal reorients onto the normal below (RNM). Height is separate: see BLEND."),
			LOCTEXT("CompositionOverrideHint", "This layer's normal replaces the normal below. Height is separate: see BLEND."),
			LOCTEXT("CompositionCoatHint", "Sit this layer over what is below rather than blending into it."),
			LOCTEXT("CompositionDetailHint", "Contribute only a normal. The layer's other channels are ignored."),
		};
	}

	FText ForHeightOp(const EMixtormatHeightOp HeightOp)
	{
		switch (HeightOp)
		{
		case EMixtormatHeightOp::Add:        return LOCTEXT("HeightOpBadgeAdd", "ADD");
		case EMixtormatHeightOp::Subtract:   return LOCTEXT("HeightOpBadgeSub", "SUB");
		case EMixtormatHeightOp::Multiply:   return LOCTEXT("HeightOpBadgeMul", "MUL");
		case EMixtormatHeightOp::Min:        return LOCTEXT("HeightOpBadgeMin", "MIN");
		case EMixtormatHeightOp::Max:        return LOCTEXT("HeightOpBadgeMax", "MAX");
		case EMixtormatHeightOp::Difference: return LOCTEXT("HeightOpBadgeDif", "DIF");
		case EMixtormatHeightOp::HeightBlend: return LOCTEXT("HeightOpBadgeHB", "HB");
		default:                            return LOCTEXT("HeightOpBadgeRep", "REP");
		}
	}

	// The Generator-layer Height Blend sublayer's own op set, distinct from the layer height op.
	FText ForGeneratorHeightOp(const EMixtormatGeneratorHeightOp Op)
	{
		switch (Op)
		{
		case EMixtormatGeneratorHeightOp::Add:        return LOCTEXT("GenHeightOpBadgeAdd", "ADD");
		case EMixtormatGeneratorHeightOp::Subtract:   return LOCTEXT("GenHeightOpBadgeSub", "SUB");
		case EMixtormatGeneratorHeightOp::Min:        return LOCTEXT("GenHeightOpBadgeMin", "MIN");
		case EMixtormatGeneratorHeightOp::Max:        return LOCTEXT("GenHeightOpBadgeMax", "MAX");
		case EMixtormatGeneratorHeightOp::Difference: return LOCTEXT("GenHeightOpBadgeDif", "DIF");
		case EMixtormatGeneratorHeightOp::Multiply:   return LOCTEXT("GenHeightOpBadgeMul", "MUL");
		case EMixtormatGeneratorHeightOp::HeightBlend: return LOCTEXT("GenHeightOpBadgeHB", "HB");
		default:                                      return LOCTEXT("GenHeightOpBadgeAdd2", "ADD");
		}
	}

	FText ForLayer(const FMixtormatLayer& Layer)
	{
		if (CompositionOf(Layer) == EComposition::Detail)
		{
			return LOCTEXT("LayerBadgeDetail", "DTL");
		}
		return ForHeightOp(Layer.HeightBlend.Op);
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
		case EMixtormatEffectType::ShapeDeform: return LOCTEXT("EffectBadgeShapeDeform", "SHAPE");
		case EMixtormatEffectType::GeneratorFlow: return LOCTEXT("EffectBadgeGeneratorFlow", "FLOW");
				case EMixtormatEffectType::GravityFlow: return LOCTEXT("EffectBadgeGravityFlow", "GRAV");
		case EMixtormatEffectType::FlowCarve: return LOCTEXT("EffectBadgeFlowCarve", "CARVE");
		case EMixtormatEffectType::LayerBlur: return LOCTEXT("EffectBadgeLayerBlur", "BLUR");
		case EMixtormatEffectType::Runoff:    return LOCTEXT("EffectBadgeRunoff", "RUNOFF");
		default:                              return LOCTEXT("EffectBadgePeel", "PEEL");
		}
	}

	FText ForChild(const FMixtormatLayerChild& Child)
	{
		if (Child.Type == EMixtormatLayerChildType::Effect)
		{
			const EMixtormatEffectType Type = ResolveChildEffectType(Child);
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

		if (Child.Type == EMixtormatLayerChildType::Filter
			|| Child.Type == EMixtormatLayerChildType::PatternId
			|| Child.Type == EMixtormatLayerChildType::HsvFilter
			|| Child.Type == EMixtormatLayerChildType::RampId
			|| Child.Type == EMixtormatLayerChildType::UvFromIds
			|| Child.Type == EMixtormatLayerChildType::ReliefFromIds
			|| Child.Type == EMixtormatLayerChildType::BoundaryFromIds
			|| Child.Type == EMixtormatLayerChildType::IdGroup)
		{
			// Nothing. These have no blend mode -- they emit data and albedo, not coverage -- and
			// the slot used to say "FILT", which every one of their names already says. A badge
			// that repeats the row it sits on is a column of noise to scan past; the row collapses
			// the slot when the text is empty, so this costs no width and no height either.
			return FText::GetEmpty();
		}
		if (Child.Type == EMixtormatLayerChildType::RandomId)
		{
			return ForMaskBlendMode(Child.RandomId.BlendMode);
		}
		if (Child.Type == EMixtormatLayerChildType::Generator)
		{
			// Generators are plain signed field producers now; the row name says which one it is.
			return FText::GetEmpty();
		}
		if (Child.Type == EMixtormatLayerChildType::HeightBlend)
		{
			return ForGeneratorHeightOp(Child.HeightBlend.Op);
		}
		if (Child.Type == EMixtormatLayerChildType::HeightCurve
			|| Child.Type == EMixtormatLayerChildType::HeightColorRamp
			|| Child.Type == EMixtormatLayerChildType::HeightPush
						|| Child.Type == EMixtormatLayerChildType::StructuralWarp)
		{
			return FText::GetEmpty();
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
		// Cluster IDs, Pattern IDs and ID Group need no kind mark: the row names them in full.
		case EMixtormatLayerChildType::Filter:    return FText::GetEmpty();
		case EMixtormatLayerChildType::PatternId: return FText::GetEmpty();

		case EMixtormatLayerChildType::IdGroup:   return FText::GetEmpty();
		case EMixtormatLayerChildType::HsvFilter: return LOCTEXT("ChildKindHsvFilter", "HSV");
		case EMixtormatLayerChildType::RandomId:  return LOCTEXT("ChildKindRandomId", "RND");
		case EMixtormatLayerChildType::RampId:    return LOCTEXT("ChildKindRampId", "RAMP");
		// No kind mark, for the reason the three ID rows above carry none: the row name is
		// already the shortest true description, and a UV or RLF abbreviation two columns to
		// its right would only repeat it.
		case EMixtormatLayerChildType::UvFromIds: return FText::GetEmpty();
		case EMixtormatLayerChildType::ReliefFromIds: return FText::GetEmpty();
		case EMixtormatLayerChildType::BoundaryFromIds: return FText::GetEmpty();
		case EMixtormatLayerChildType::Blur:      return LOCTEXT("ChildKindBlur", "BLUR");
		case EMixtormatLayerChildType::Curvature: return LOCTEXT("ChildKindCurvature", "CURV");
		case EMixtormatLayerChildType::Generator: return LOCTEXT("ChildKindGenerator", "GEN");
		case EMixtormatLayerChildType::HeightBlend: return LOCTEXT("ChildKindHeightBlend", "HBLD");
		case EMixtormatLayerChildType::HeightCurve: return LOCTEXT("ChildKindHeightCurve", "HCRV");
		case EMixtormatLayerChildType::HeightColorRamp: return LOCTEXT("ChildKindHeightColorRamp", "HCLR");
					case EMixtormatLayerChildType::HeightPush: return LOCTEXT("ChildKindHeightPush", "HPUSH");
							case EMixtormatLayerChildType::StructuralWarp: return LOCTEXT("ChildKindStructuralWarp", "WARP");
		default:                                  return LOCTEXT("ChildKindMask", "MASK");
		}
	}
}

#undef LOCTEXT_NAMESPACE
